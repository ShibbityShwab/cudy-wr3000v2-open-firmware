# FIRE-RECIPE - R6: the read-only TBTT differential (band0 working vs band1 silent)

**Lane:** `wifi-tbtt-timer` (task-214). **HOST-ONLY deliverable: the Lead
fires this.** R6 performs **no writes of any kind** - not to the device, not to
the firmware RAM, not to the timer words. It is a measurement, not an arm.

## Why R6

The R5 fire made every documented precondition green (AP config before the
0x0065 VAP start / 0x0242 band start, band-1 MAC start armed, RF programmed,
per-link beacon state present, page golden:1) and the beacon still does not
reach the air. This lane's static result is that **no host message arms the
TBTT** (TABLES T8): the g0-id1 key tables do not contain a beacon-timer key and
the stock capture never sends one. The only remaining offline discriminator is
the firmware's per-band SMAC interrupt/timer state, which is partly host-readable
in the same boot in which band 0 beacons.

## R6a - the probe pass (read-only)

Prerequisite: the same boot as the working 2.4 GHz AP and the armed 5 GHz band
(R5 ladder already accepted). Use the committed probe pack:
`.provision-venv/bin/python opensource/docs/incoming/wifi-tbtt-timer/tools/fw_tbtt_timer_re.py --probes`
(or the Lead's bench probe runner) and record every value under
`build/` (gitignored), never in the repo.

Order (all reads are 32-bit aligned; the 16-bit timer word may be read as a
32-bit value and masked with 0xFFFF):

1. **Start/run bits [V*]:** 0x4011080C and 0x4011480C bit0; 0x4010C220
   (expect 0x1029 set); 0x40108214; 0x40101220; 0x40101208.
2. **Timer words [U]:** 0x401015CC (u32) and 0x400024B0 (u16) - first-ever
   read of these values.
3. **TRX:** 0x4010E150/54/58 (expect 0x33/0xFF/0xFF) and band0 0x4010820C.
4. **Per-band SMAC objects:** read CA 0x180814 (u32) and its +8 field (band0
   object) and +0x1C field (band1 object); for each object read +672, +700,
   +740 (pointers, follow only if in firmware RAM 0x00040000..0x00180000 or
   device IO 0x40000000..0x4011FFFF); then read [obj+672]+228 and +232.
5. **Repeat step 4 twice with ~200 ms between reads** to see whether the
   status word moves (ISR activity) on either band.
6. **Beacon state (reference):** perlink beacon buffer/descriptor/len/enable
   through the band objects (TABLES T7 #16).

## Interpretation matrix

| observation | conclusion | next rung |
| --- | --- | --- |
| band1 MAC-start bit0 = 0, or 0x4010C220 lacks 0x1029 | R5 state did not persist | re-run the R5 ladder, repeat R6a |
| band1 SMAC status [[obj]+672]+232 never changes, band0's does | no SMAC ISR fires on band1 -> **TBTT/timer not armed** | R6b |
| both bands' status words identical and static, 0x400024B0 reads 100 on both | timer words are not the discriminator; the TBTT is firmware-internal [I] | R6b |
| 0x400024B0 reads 100 on band0 but 0/other on band1 | per-band timer word not initialised for band1 | R6b |
| band1 status changes but no beacon | TBTT ISR fires; the gap is beacon TX gating (0x40101220/0x40101208 or TRX) | escalate to the beacon-TX lanes, not the timer |
| 0x401015CC / 0x400024B0 read 0xFFFFFFFF or an unlocked page | probe/access problem - stop | abort |

## R6b - conditional re-arm (still no new message)

Only if R6a says the timer/ISR state is dead on band1: **re-run the accepted R5
ladder** (AP config 0x0127 -> VAP start 0x0065 -> band start 0x0242 subcmd 1;
the fallbacks are the documented ones) and repeat R6a. The R5 writes are
idempotent: 0x4AF0C early-outs when request[1]==2. If the band1 SMAC status
starts moving after the re-run, capture and run the 5200 scan.

If the status still does not move, the verdict is **firmware-internal only**:
there is no known host message that arms the TBTT and another host fire cannot
close it. The next step is a firmware-side hypothesis (the TBTT scheduler is
deferred task context in the firmware image, ISR body 0xAEC74 / IRQ 53 is the
prime candidate), not another H2D frame.

## Abort rules

* **Read-only:** R6/R6b add no writes. The timer words 0x401015CC and
  0x400024B0 are probe-only - never write them.
* Never read the RC misc window phys 0x10160000..0x1016FFFF.
* Never write CA 0x400392F0; never rmmod the vendor modules.
* Stop and report if a probe returns a value with an obviously unstable page
  (0xFFFFFFFF/0xDEADBEEF) or if the page/golden gate changes.
* No human at the bench = no fire. Recovery door armed first (RUNBOOK).
* Raw probe output stays under `build/` (gitignored); commit only derived
  values.
