# wifi-5g-tx-scheduler - the firmware band-1 beacon-TX gate (the 5 GHz beacon's last layer)

**Lane:** `wifi-5g-tx-scheduler` (shared task **task-231**). **HOST-ONLY**:
this lane read the pinned local vendor firmware image in place; it never
contacted the router, never flashed and never fired. **The Lead fires.**

**Why this lane exists.** The definitive bench state (2026-11-27) says every
host-writable/host-visible element of the 5 GHz AP equals the working 2.4 GHz
element (SMAC sub-base 0x106 pattern, TBTT 0x401015CC = 0, MAC 0x6101 + the
enables, RF mode 3 + R image, per-link beacon state and the full vendor message
order) - and the 5200 beacon is absent while the same firmware image beacons on
2.4 GHz. The stock Cudy-1C73-5G beacons with the same image. The remaining
difference therefore has to be firmware-internal. This lane decodes the
firmware's beacon-TX path and finds the one per-band state that a host-visible
register copy would not reproduce.

Deliverables:

| file | what |
| --- | --- |
| [REPORT.md](REPORT.md) | the decode: the 45 names, the two static handler tables, the boot per-band IRQ registration, the per-band beacon-TX gate words, why band-1 can stay silent, ranked causes and what stays [U] |
| [TABLES.md](TABLES.md) | every address, table row, handler, register, bit and probe |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | R0 read-only differential, the interpretation matrix, the R1 host gate-arm rung, the R2/R3 follow-ups and abort rules |
| [tools/fw_tx_scheduler_re.py](tools/fw_tx_scheduler_re.py) | re-derives every claim from the pinned artifacts (`--selftest` 128/128 PASS) |
| evidence/ | `selftest.txt`, `decode.txt`, `tables.txt`, `probes.txt` |

## Bottom line (details in REPORT.md)

1. **The 45 names are now fully enumerated** (CA 0x12266C..0x122BA4): 13
   `smac_msg_proc_*` names + 32 per-band ISR names (16 x 5g/2g pairs). They are
   **dead static data** in this build - no literal and no movw/movt reference to
   any name exists [V] - but they name two static pointer tables that follow the
   same order: **CA 0x10B058** (32 thumb ISR handlers; idx14/15 =
   `smac_tbtt_5g_isr`/`smac_tbtt_2g_isr`) and **CA 0x10B0DC** (10 SMAC
   message processors, including the four beacon TX resume/suspend handlers and
   the enable/disable-TRX worker CA 0xE8520).
2. **The firmware has no beacon-build/queue task.** The TBTT handlers are
   6/11-instruction leaf stubs (no BL, no device access) that only report
   per-band timer counters into an event message. The beacon frame is staged in
   the host-key VAP-start path (per-link +0x6B8/+0x6BC/+0x6C0/+0x6C4/+0x6C6)
   and the per-TBTT transmit is hardware-driven. This settles the "which RTOS
   task TXs the beacon" question: none.
3. **The live entry/registration is the boot init CA 0xD1B68**: it registers
   **8 IRQ lines per band** with per-band wrapper functions (CA 0x180814 +8 =
   band0 object / +0x1C = band1 object; 7 shared bodies) - band0 IRQ
   47/49/50/51/52/82/83/59, band1 IRQ 53/55/56/57/58/84/85/60. The install fn
   CA 0xC74B0 stores the handler at RAM `0x17D398 + 0x98 + 4*irq`, which is
   host-readable. **Band-1's ISR entry is registered identically to band-0's**
   [V] - registration is *not* the band-1 gap.
4. **The one firmware-side per-band gate that a register-value copy does not
   reproduce is a pair of host-visible words:**
   **0x40101220** (per-band SMAC/TX enable: **bit0 = 5 GHz, bit1 = 2.4 GHz**,
   set per band by MAC start CA 0x4AF0C) and **0x40101208** (0x40101220 - 24;
   per-band suspend latch: 5 GHz bits **0x1|0x4**, 2.4 GHz **0x2|0x8**). The
   four firmware SMAC message processors CA 0xE6A26 / 0xE6A70 / 0xE6AB4 /
   0xE6B00 are the *only* writers: resume 5g = `0x40101220 |= 1` +
   `0x40101208 &= ~(0x1|0x4)`, suspend 5g = the inverse, and the 2.4 GHz pair
   is the bit1/0x2/0x8 twin [V].
5. **The host cannot post those resume/suspend messages**: no g0-id1 table A
   (217 rows) / table B (88 rows) worker is any gate processor or any of the 45
   handlers [V]. The host can only set the two gate words - which is exactly
   the minimal R1 rung of the fire recipe.
6. **Ranked cause for the silent 5 GHz beacon:** the band-1 gate is closed
   (bit0 clear and/or the 0x1/0x4 suspend latches set) because the firmware's
   internal band-start path never posted/consumed the 5 GHz `beacon_tx_resume`
   event, while the 2.4 GHz one did. Every host-visible copy test still passes
   because the message - not a register value - is the missing step. R0
   (read-only, both bands, same boot) decides this in one pass; R1 then arms the
   gate words directly.

## Boundary

No vendor binary or firmware byte is stored in this lane; the tool reads the
pinned local artifacts (`/home/shibbityshwab/recovery-extract/lib/firmware/
hi_wifi/FIRMWARE.bin`, 928,920 B, sha256 `7fc87e20...311b`) at run time and
prints only derived values (addresses, instruction mnemonics, register
constants, probe addresses). No device, bench link, UART or network access was
used; no write was performed on any artifact. Probe output from a future fire
stays under `build/` (gitignored).
