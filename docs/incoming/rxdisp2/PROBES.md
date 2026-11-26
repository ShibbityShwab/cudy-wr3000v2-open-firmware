# PROBES - the read-only pass that splits C5 / C6 / C7 / C8

**Lane:** rxdisp2 (task-242). **READ-ONLY** static document: every item is a load, no write,
no retry, no address outside the host-readable windows. The Lead fires; this lane touches no
device. Use the resident read helper (bar0read; physical addresses; O_RDONLY + PROT_READ;
it refuses unaligned addresses and the RC misc window). Firmware-RAM conversion:
**phys = 0x40000000 + 0x6B8000 + CA** (region 5). Conversions for all windows: TABLES.md T5.

**Why this pack:** task-226 left three candidates (C5 local queue not drained / C6 CRX gate
drop / C7 transport queue dead) and the counters P0/P12-P19 could not separate the
*upstream* case "the g2 id3 reached the dispatcher but no descriptor ever reaches the
builder". This lane adds the exact dest0 notifier/customer expectations (P20), the
chip RX-ctx gate value (P21), the forward/dest table (P22) and a six-counter ladder
(P24) that localises the break to one stage.

## Prior pack (recap, task-226; unchanged) [V*]

| # | item | CA | phys | width | meaning |
| ---: | --- | ---: | ---: | ---: | --- |
| P0 | host D2H catalogue | - | - | - | ready pair g4 id1/id2, heartbeat g4 id4, config replies g1 id3, any g2 id3, any g1 id13 |
| P12 | band0 object word 0 | 0x174BF0 | 0x4082CBF0 | 4 | byte0 must be 0 (chip/dest index) |
| P12b | band1 word 0 | 0x175A18 | 0x4082E618 | 4 | band-1 mirror |
| P13 | band0 post-fail +0xCCE | 0x1758BE | 0x4082D8BE | 1 | 0 expected (now P24f) |
| P13b | band1 +0xCCE | 0x1766E6 | 0x4082E6E6 | 1 | band-1 mirror |
| P14 | dest0 record (0x171FB4) | 0x171FB4 | 0x40829FB4 | 6x4 | [4]=map ptr, [8]=queue array, [0x10]=group array |
| P15 | queue array state/+0x48/+0xC0/+0xC4 | 0x171B70 | 0x40829B70 | 3x0xE4 | q1 notifier expected 0x171E84; q1 list count +0x48 |
| P16 | group2 item[3] {id, handler, aux, stats} | follow P14 group array | - | 16 | handler 0x000BBC9D; stats delta = accepted g2 id3 |
| P17 | group0 item stats control | follow group0 item array | - | - | layout control |
| P18 | band0 CRX cap drop +0xCC5 | 0x1758B5 | 0x4082D8B5 | 1 | 0 expected (now P24e) |
| P19 | band1 mirrors of P14-P18 | - | - | - | - |

## New pack P20-P24 (this lane) [V decode + U read]

| # | item | CA | phys | width | expected / meaning |
| ---: | --- | ---: | ---: | ---: | --- |
| P20 | customer1 record | 0x171E84 | 0x40829E84 | 7x4 | +0=0x00171FB4; +0x48=0/1 pending; +0x4C=0x0010CFF8 (RX thread record); +0x50=nonzero task handle; +0x54=+0x58=0x00171C54 (q1) |
| P20b | q1 notifier slot | 0x171D14 | 0x40829D14 | 4 | **0x00171E84**; anything else (esp. 0) = C5 anomaly |
| P20c | customer0 / customer2 +0x54 | 0x171E70 / 0x171F40 | 0x40829E70 / 0x40829F40 | 4 | expect q0 (0x171B70) / q2 (0x171D38) - transport sanity |
| P21 | chip0 RX-ctx gate | 0x15E408 | 0x40816408 | 4 | nonzero (G1); chip1 mirror CA 0x15FF70 / phys 0x40817F70 |
| P22 | chip-manager forward/dest table | 0x171504 (base u16) + 0x171458 (0x13 u32 entries) | 0x40829504 / 0x40829458 | 2 + 0x4C | dest_i = u16[base] + u32[entry_i]; nibble = u8[dest_i+2]&0xF; 0 = host path OK |
| P23 | chip0 band index | 0x15D178 | 0x40815178 | 1 | 0 or 1 (min(ctx[1],2)) |
| P24a | band0 +0xCC0 producer entries | 0x1758B0 | 0x4082D8B0 | 1 | moves (measured before); control |
| P24b | band0 +0xCC1 descriptor stage | 0x1758B1 | 0x4082D8B1 | 1 | **moves = 0xBA1C8 started a descriptor** |
| P24c | band0 +0xCCC forward-prep fail | 0x1758BC | 0x4082D8BC | 1 | moves = 0xB2C5C failed |
| P24d | band0 +0xCCD report alloc fail | 0x1758BD | 0x4082D8BD | 1 | moves = 0x102822 alloc failed |
| P24e | band0 +0xCC5 CRX cap drop | 0x1758B5 | 0x4082D8B5 | 1 | moves = builder G8 |
| P24f | band0 +0xCCE g2id3 post fail | 0x1758BE | 0x4082D8BE | 1 | moves = producer post rejected |
| P24g | band1 mirrors | +0xE28 | - | 1 each | P12b/P13b/P19 copies |

All P20/P22/P24 addresses are static or derived from the init code (TABLES T2); P21 is a
BSS pointer whose value is [U]; if a read returns 0xFFFFFFFF or faults, record and stop -
do not retry another width/address.

## Decision matrix (one pre/post pair around the laptop auth burst, one coherent boot)

| P24a +0xCC0 | P24b +0xCC1 | P24e +0xCC5 | P24f +0xCCE | P20b q1 notifier | conclusion | next |
| --- | --- | --- | --- | --- | --- | --- |
| no move | - | - | - | - | the SMAC producer itself is not running; the F1 is upstream of everything here | task-221 producer rung |
| moves | no move | no move | no move | 0x171E84 | **C8**: g2 id3 accepted+drained, 0xBA1C8 never starts a descriptor (empty/invalid descriptor chain, class fields 0) -> SMAC/hardware RX descriptor side | re-check P12 (band byte), P5/P6 rung, woc-class live bank |
| moves | moves | no move | no move | 0x171E84 | **C6**: builder ran and dropped on a no-counter gate (G1/Gclass/G7) or the silent post; P21 + P22 decide which | if P21=0 -> init/G1; if Gclass/G7 pattern, the RX descriptor class/forward fields |
| moves | moves | moves | no move | 0x171E84 | **C6/G8**: length cap; frames > 0x644 - check the RX frame length programming | frame-length/BA window |
| moves | moves | no move | moves | 0x171E84 | producer post rejected (dest/group resolution) - P14/P16 layout re-check, read P12 | producer post rung |
| moves | moves | no move | no move | != 0x171E84 | **C5**: queue/customer init anomaly (init aborted) | init logs + D2H ready pair; no host fix |
| moves | any | any | any | any | g1 id13 appears in P0 -> the RX path works; the F1 was pre-fix or the burst was not seen | record and close |

P22 extra rule: if any dest_i computed from P22 (with the index that the firmware log or a
follow-up capture yields) has u8[dest_i+2]&0xF != 0, the g1 id13 is posted to an
uninitialized destination and dropped silently (C7) - the only host lever would be the H2D
forward-config message, which is not yet decoded; do not guess it on-air.

## Safety

Read-only loads only; no writes anywhere; never write CA 0x400392F0; never read the RC misc
window 0x10160000..0x1016FFFF; no retries of a failed width; one coherent boot per tuple;
re-probe after any reboot; P0 before P20/P24 so a dead delivery channel is not misread as an
RX fault.
