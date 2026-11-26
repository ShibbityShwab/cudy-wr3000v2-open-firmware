# FIRE-RECIPE - the attended read-only pass (no writes, Lead fires)

**Lane:** rxdisp2 (task-242). HOST-ONLY lane: this document is the procedure only; the Lead
fires with a human at the bench, bench link PASS and the recovery door READY (RUNBOOK).
Every item is a load. **No writes anywhere.** One coherent boot per tuple.

**Goal:** in one pre/post pair around a laptop auth burst, decide which stage of the
local-dispatch/CRX chain is dead, using the newly decoded expectations:
q1 notifier = 0x171E84 (P20b), customer1 task alive (P20), chip RX ctx != 0 (P21), forward
table nibbles (P22), and the six-counter ladder (P24a-f).

## Preconditions

1. tools/bench-link.sh PASS; recovery door armed (tools/recovery-card.sh) - the Lead's job.
2. Device on the IMAGE #3 clean-room stack, AP up, beacons on air, one BSS, no client.
3. Read helper loaded; it refuses unaligned reads and the RC misc window.
4. Freeze the tuple: one boot for the whole pack; note the boot ID / uptime.

## Steps (one pass)

1. **P0 baseline:** host D2H catalogue: record the ready pair, heartbeat, config replies,
   and assert no g2 id3 / no g1 id13 in the pre-window.
2. **P20 block:** read q1+0xC0 (0x171D14/0x40829D14) = expect 0x00171E84;
   customer1+0/+0x48/+0x4C/+0x50/+0x54/+0x58 (0x171E84..); q1 list count
   (0x171C54+0x48). Interpretation: notifier != 0x171E84 or task handle 0 -> the
   per-dest init did not run (C5 anomaly; stop, no host fix).
3. **P21/P22/P23:** chip0+0x1A00 (0x15E408/0x40816408) != 0; chip0+0xD70 = 0/1; forward
   table base u16 at 0x171504/0x40829504 and entries at 0x171458/0x40829458 (read only
   the entries whose dest_i lands in the firmware-RAM window; stop at the first
   out-of-window pointer).
4. **P24 ladder:** band0 +0xCC0/+0xCC1/+0xCCC/+0xCCD/+0xCC5/+0xCCE
   (0x1758B0..0x4082D8B0 ff.) and the band1 mirrors (+0xE28). Record exact values.
5. **Burst:** laptop sends auth frames at the AP (the standard F1 stimulus).
6. **Repeat P0 + P20 + P24** in the same boot, same order. Compute deltas.
7. **Stop and report:** write the pre/post deltas into a dated mem-entries entry with the
   decision-matrix row (PROBES.md). No second stimulus, no reboot inside the tuple.

## Decision table (delta-based; see PROBES.md for the full matrix)

| observation | conclusion | next |
| --- | --- | --- |
| P24a no move | producer not running | task-221 producer rung |
| P24a moves, P24b no move | **C8**: no descriptor reaches 0xBA1C8 (empty/invalid descriptor chain) | SMAC/hardware RX descriptor side; woc-class live bank |
| P24b moves, P24e moves | C6/G8 length cap | frame length programming |
| P24b moves, P24e no move, P24f no move, no g1 id13 | C6: class nibble / forward flag / dest (G1,Gclass,G7) | P21/P22 decide; then the MAC RX descriptor class/forward fields |
| P24f moves | producer post rejected | P12/P14/P16 layout re-check |
| P20b != 0x171E84 or P20 task handle 0 | C5 init anomaly | init logs; no host fix |
| P0 shows g1 id13 | path works | record, close the F1 |

## Abort / safety

* Any failed read: record the address, width and returned status, **stop** - do not retry
  another width or address.
* Any incremental counter that moves without a stimulus: re-baseline on a quiet BSS.
* Never write CA 0x400392F0. Never read the RC misc window 0x10160000..0x1016FFFF.
* No vendor binary, firmware byte or secret leaves the machine from this lane.
