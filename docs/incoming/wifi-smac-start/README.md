# wifi-smac-start - the firmware SMAC/beacon-timer start decode (2.4G working vs 5G silent)

**Lane:** `wifi-smac-start` (shared task **task-215**). **HOST-ONLY**: this lane
read the pinned local vendor firmware/module artifacts in place; it never
contacted the router, never flashed and never fired.

**Why this lane exists.** The 5 GHz AP is armed end-to-end on our clean-room
stack (R5: the 0x0065 VAP start, the 0x0242 band start, the band-1 MAC start at
0x4010C220 = 0x1029, the RF, the beacon template) yet no beacon reaches the
air, while the *same driver* beacons on 2.4 GHz. The tbtt-timer lane (task-214)
ranked 0x401015CC / 0x400024B0 as the "SMAC timer latch" candidates. The Lead's
2026-11-26 11:30 2.4 GHz differential fire (working 2.4 AP vs silent armed 5G
AP on the same boot) supplies the decisive bench datum: **the timer words are
IDENTICAL on both bands (0x401015CC = 0, 0x400024B0 = 0x64)** - so they are
*not* the 5 GHz discriminator - and the **only measured difference is the
per-band SMAC object's +672 pointer** (band0 -> a RAM block, band1 -> the
device-IO default 0x40062000).

## Bottom line

1. **The tbtt-timer lane's SMAC-latch hypothesis is refuted by the bench
   datum.** 0x401015CC is written by a ref-counted TSF-latch/sync pair
   (start helper CA 0x8980C writes 1, stop helper CA 0x897D6 writes 0, the
   16-bit word 0x400024B0 is save/restored around it) and by the per-link
   init/clear paths (0x9F43x, 0xA0052, 0xA02Ax, 0xA047E). Since the working
   2.4 GHz band has the same 0/0x64 state, these words cannot be the beacon
   start latch. Full writer/caller map in [REPORT.md](REPORT.md) sec. 2.
2. **The real discriminator is the per-band SMAC context pointer at object
   +672 (the value the SMAC ISR bodies dereference).** It is *not* set by the
   timer helper; it is set by the boot SMAC init **CA 0xD1B68** (single caller
   0x48314). That function zeroes the 7,272-byte band region at 0x174BE0 and
   then walks the band instances (stride 0xE28, object base 0x174C30 + band x
   0xE28) setting, per band instance:
   - a **device-base chain** at +608..644 (0x40042000 for instance 0,
     **0x40062000 for instance 1**, step 0x2000) - i.e. on the band-1 object
     (0x175A18) the field at +672 is 0x40062000 **by construction**;
   - a second device chain at +648..708 (0x40080000 / 0x400B0000);
   - SMAC sub-bases at +720/+724 (0x4010A000/0x40109000 vs
     0x4010E000/0x4010D000);
   - a **RAM block set** at +576..600: 0x114F18 (+488), 0x115128, 0x183438,
     0x1817A0, 0x180904, 0x180E68, filled at boot from tables 0x1B2A87 /
     0x1B2AAC. The Lead's band-0 value 0x114F18 is exactly the first of these.
   => The silent band-1 object still holds the **boot device-base default
   0x40062000**, i.e. the band-1 SMAC context was never advanced to the
   attached-RAM-block state that the working 2.4 GHz band shows. Details,
   including the +32-byte offset reconciliation the next read-only fire must
   settle, in [REPORT.md](REPORT.md) sec. 3-4.
3. **Host-message candidate.** The committed stock-boot H2D table has 25 valid
   g0-id1 keys; our documented ladder uses 9 of them (0x0064/0x0065/0x0067/
   0x00D8/0x0124/0x0127/0x01F7/0x0242/0x0243). The stock boot also sends 20
   further keys, several in the per-band config family (0x01CF->0x84D14,
   0x03E4->0x84260, 0x0073->0x84920, 0x0000->0x840B4, 0x0069->0x839B4,
   0x0005->0x82878). These are the host-side candidates our ladder may lack.
   The firmware-internal alternatives and the discriminating probes are in
   [REPORT.md](REPORT.md) sec. 5 and [FIRE-RECIPE.md](FIRE-RECIPE.md).
4. **New ISR-name decode.** The name blob at CA 0x12266C is the per-band SMAC
   ISR/handler name table: the first 7 5g/2g pairs match the 7 registered
   common ISR bodies, so the INTC map can be named (band0 IRQ51 / band1 IRQ57
   = `smac_common_timer_*`; body 0xAEB80). Per-band named handlers
   `smac_tbtt_5g_isr`/`smac_tbtt_2g_isr` exist and are *not* among the 8
   registered INTC sources - the TBTT event is delivered through the firmware
   timer/task context, confirming task-214's "firmware-internal arm" verdict.
   Mapping (inference [I]) in [TABLES.md](TABLES.md) T6.

## Deliverables

| file | what |
| --- | --- |
| [REPORT.md](REPORT.md) | the decode: timer words (refuted), the per-band SMAC init/attach field map, the discriminating probes, candidates and patch hypothesis |
| [TABLES.md](TABLES.md) | every address, field offset, literal and probe |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | the read-only R7 band0/band1 SMAC-context probe pack, interpretation matrix, conditional rungs, abort rules |
| [tools/fw_smac_start_re.py](tools/fw_smac_start_re.py) | re-derives every claim from the pinned artifacts (`--selftest`) |
| evidence/ | selftest, decode, tables and probes output |

## Boundary

No vendor binary, firmware byte, raw capture or secret is stored in this lane;
the tool reads the pinned local artifacts
(`/home/shibbityshwab/recovery-extract/.../FIRMWARE.bin`, 928,920 B, sha256
7fc87e20...311b, CPU = file offset + 0x40000) at run time and prints only
derived values (addresses, mnemonics, register constants, probe addresses).
Probe output from a future fire stays under `build/` (gitignored).
