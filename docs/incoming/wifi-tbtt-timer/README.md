# wifi-tbtt-timer - the 5 GHz TBTT/beacon-timer arm (the last 5 GHz link)

**Lane:** `wifi-tbtt-timer` (shared task **task-214**). **HOST-ONLY**: this lane
read the pinned local vendor firmware image in place; it never contacted the
router, never flashed and never fired. **The Lead fires.**

**Why this lane exists.** The 2026-11-26 R5 fire made the whole host/firmware
5 GHz chain green - AP config posted before the 0x0065 VAP start / 0x0242 band
start, the band-1 MAC start armed (state[4]=0x00, state[1]=2, counter=1), the RF
programmed (mode 3, R+0x3A8/R+0x3AC) and the per-link beacon state present - but
no 5 GHz beacon reaches the air. The only remaining gap is the **TBTT (beacon
timer)** that must fire the template every 100 ms. This lane decodes the arm
path now that the band start is armed, and turns it into read-only evidence.

## Bottom line

1. **No host message arms the TBTT.** The g0-id1 dispatch (CA 0x43D58) is an
   exact u16 key lookup in table A (CA 0x10C23C, 217 rows) then table B (CA
   0x103BF8, 88 rows); no key maps to a beacon/TBTT worker. The stock boot's
   captured g0-id1 keys (committed `wifi-f1-from-capture` evidence) contain
   no timer key either; the only captured beacon-path keys are 0x0127 (set
   beacon template), 0x0124 (channel) and 0x0065 (VAP start). **The TBTT arm is
   firmware-internal**; the host cannot post it with a known family.
2. **The band start does not arm a timer through MMIO.** CA 0x971D8 (band start)
   contains no device-IO literal at all: it calls hooks G+0x370 (NULL) and
   G+0x374 (=0x57638). CA 0x4AF0C (MAC start) writes only the reset/status
   words, the two SMAC IRQ-enable words (band1 0x4010C220 |= 0x1029, band0
   0x40108214 |= 0xF), the per-band MAC-start bit (0x4011080C/0x4011480C bit0),
   and 0x40101220; then it arms the per-link state and returns. Both timer
   words found by this lane (0x401015CC and 0x400024B0) are **not** touched by
   either function.
3. **Correction to `wifi-fw-beacon-tx`/`wifi-fw-tbtt-arm`:** the G+0x374
   callback 0x57638 is a locked-list teardown (list-pop/free loop over 184-byte
   records, no device-IO literal) - it is not an MMIO beacon arm. The beacon
   submission into the hardware is deferred to firmware task/timer context.
4. **NEW - the per-band SMAC interrupt entry points.** At boot the firmware
   registers 8 INTC sources per band through 0xC6DA2(irq, prio, handler):
   2.4G = IRQ 47/49/50/51/52/82/83/59, 5G = IRQ 53/55/56/57/58/84/85/60. Each
   handler is a 3-instruction band wrapper that loads the per-band SMAC object
   from RAM 0x180814 (+8 band0, +0x1C band1) and jumps to one of 7 common ISR
   bodies (table below). This is the first code-level identity of the SMAC
   vectors that the 45-name debug blob (CA 0x12266C) left [U]. The TBTT ISR is
   one of these bodies; the ranking is in REPORT sec. 4.
5. **Timer candidates [U].** 0x401015CC (SMAC common latch/control: written 1
   to start, 0 to clear, a count in the per-band init paths) and 0x400024B0
   (16-bit word written **100** - the only literal 100 in the arm/init code -
   by both per-band init paths; candidate beacon-interval/tick word). Both are
   inside the host region-3 window and are read-only probes; their exact roles
   remain [U] until read on device.
6. **The last host-visible check.** The firmware INTC itself (pending-id
   0x4016010C, handler table 0x17D430, enable 0x40161180/0x40161280) sits at CA
   0x4016xxxx, **outside** the host region-3 window (CA 0x40000000..0x4011FFFF),
   so the host cannot read it directly. The read-only liveness proxies are the
   SMAC IRQ enable/status words and the per-band SMAC objects (+672/+700/+740,
   mask/ack +228, status +232). FIRE-RECIPE R6 uses them as a band0-vs-band1
   differential.

## Deliverables

| file | what |
| --- | --- |
| [REPORT.md](REPORT.md) | the decode: host-message negative, band/MAC start MMIO, the per-band SMAC ISR table, TBTT candidate ranking, timer candidates, corrections |
| [TABLES.md](TABLES.md) | every address, key, IRQ, wrapper, timer word and probe |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | R6 read-only band0/band1 differential, interpretation matrix, conditional rungs, abort rules |
| [tools/fw_tbtt_timer_re.py](tools/fw_tbtt_timer_re.py) | re-derives every claim from the pinned artifacts (`--selftest` 238/238 PASS) |
| evidence/ | `selftest.txt`, `decode.txt`, `tables.txt`, `probes.txt` |

## Boundary

No vendor binary, firmware byte, raw capture or secret is stored in this lane;
the tool reads the pinned local artifacts
(`/home/shibbityshwab/recovery-extract/.../FIRMWARE.bin`, 928,920 B, sha256
7fc87e20...311b) at run time and prints only derived values (addresses,
instruction mnemonics, register constants, probe addresses). Probe output from
a future fire stays under `build/` (gitignored).
