# wifi-fw-tbtt-arm - the 5 GHz TBTT arm: what the firmware actually arms, and the read-only differential that decides it

**Lane:** `wifi-fw-tbtt-arm` (shared task **task-193**). **HOST-ONLY**: this
lane read the pinned local vendor firmware image in place; it never contacted
the router, never flashed and never fired. **The Lead fires.**

**Why this lane exists.** The R0 probe closed C1/C2/C3: the band-1 per-link
beacon state exists (+0x6C4 = 16, +0x6C6 = 1, buffer pointers nonzero), the
band-1 MAC block carries its init values (0x548/0x54C = 0x6101), and the G
hooks read G+0x374 = 0x00057639 / G+0x370 = 0 (NULL by design). The AP is
enabled but the 5 GHz beacon still does not reach the air. Remaining: C5 (the
TBTT arm) and C4 (the per-band hints). This lane decodes what the firmware
does between "VAP started" and "beacon timer running", enumerates the only
per-band enable bits the image carries, and turns the result into a **read-only
differential probe** that compares the working band-0 path against the silent
band-1 path in the same boot.

## Bottom line

1. **There is no beacon-interval or time/TSF register in the per-band MAC
   block.** The block (band0 device CA 0x40110000, band1 0x40114000) is
   programmed by exactly two 23-entry {u16 offset,u16 value} init tables
   (CA 0x105714 / 0x105770). The complete offset set is
   0x548/0x54C/0x550/0x554/0x55C/0x560/0x564/0x568/0x588/0x594/0x5BC and every
   value is a small fixed constant (<= 0x6101). **0x548/0x54C = 0x6101 is not
   the TBTT arm**: it is the fixed band-1 marker (bit 0x6000) written on every
   band-1 MAC init; band0's table writes 0x0101. The R0 readback is expected,
   not a smoking gun.
2. **The per-band start/stop functions arm/clear the only per-band enable
   bits the image has:** start CA 0x4AF0C ORs bits 1|8|0x20|0x1000 (0x1029)
   into **0x4010C220** for band 1, or bits 1|2|4|8 (0xF) into **0x40108214**
   for band 0, ORs **1 (band1) / 2 (band0) into 0x40101220**, and sets bit0 of
   **0x4011080C / 0x4011480C**; stop CA 0xBE294 clears the same bits.
3. **The VAP start that creates the beacon state performs no MMIO at all.**
   CA 0x46394 allocates per-link+0x6B8 (280 B) and +0x6C0 (92 B), sets
   +0x6C4 = 16 / +0x6C6 = 1, builds/stages the beacon frame through CA 0xB5290
   / 0xB7208 / 0xB3018 / 0xEE394, calls band start CA 0x971D8 - and the whole
   function range contains **zero device-IO literal loads** (checked). The
   beacon submission is not an MMIO arm step; either the TX engine consumes the
   staged descriptor, or nothing does.
4. **Correction to lane wifi-fw-beacon-tx:** CA 0x7E5CC is the **rate-control
   TX-descriptor filler** (single caller CA 0x72184 inside the rate-adaptation
   function CA 0x70490). VAP+0x138 -> desc+0x25E and VAP+0x13A -> desc+0x260
   land next to the rate/retry fields desc+0x254..0x272; they are **not** a
   beacon-descriptor validity or beacon-enable gate.
5. **The only beacon-named handlers in the image** are four smac message
   processors (CA 0xE6A26 / 0xE6A70 / 0xE6AB4 / 0xE6B00; name blob
   `smac_msg_proc_beacon_tx_resume_req` / `_suspend_req`) and their only
   register effect is toggling **0x40101220 bit0/bit1** and **0x40101208
   bits 0..3** - the closest thing to "beacon-enable bits". **No g0-id1
   table-A/B key maps to any of them** (checked), so no H2D frame of our known
   family posts them.
6. **The 5 GHz TRX-enable path is separate and concrete:** CA 0xE8520 writes
   **0x4010E150 = 0x33, 0x4010E154 = 0xFF, 0x4010E158 = 0xFF** for band 1
   (band 0 uses 0x4010820C/0x40108214), and programs the SoC INTC
   0x40002264/0x40002268/0x4000226C. If those three band-1 words are not live,
   no 5 GHz TX (beacon included) can leave the chip.
7. **The R3 rung is read-only and differential:** read the per-band
   start/enable/TRX registers and both per-link beacon objects in the same
   boot; band 0 (working) is the reference for band 1 (silent). Decision
   table in FIRE-RECIPE.md; every probe is side-effect free.

## Deliverables

| file | what |
| --- | --- |
| [REPORT.md](REPORT.md) | the decode: per-band block inventory, start/stop/restore, beacon state path, corrections, ranked causes, limits |
| [TABLES.md](TABLES.md) | every address, table, constant, bit and probe |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | R3 read-only differential (no writes), the interpretation table, the conditional R3b/R3c rungs, abort rules |
| [tools/fw_tbtt_arm_re.py](tools/fw_tbtt_arm_re.py) | re-derives every claim from the pinned artifacts (`--selftest` 103/103 PASS) |
| evidence/ | `selftest.txt`, `decode.txt`, `tables.txt`, `probes.txt` |

## Boundary

No vendor binary or firmware byte is stored in this lane; the tool reads the
pinned local artifacts (`/home/shibbityshwab/recovery-extract/.../FIRMWARE.bin`,
928,920 B, sha256 7fc87e20...311b) at run time and prints only derived values
(addresses, instruction mnemonics, register constants, probe addresses). No
device, bench link, UART or network access was used; no write was performed on
any artifact. Probe output from a future fire stays under build/ (gitignored).
