# PROBES - read-only pack P0-P11 (SMAC PPDU-completion producer attribution)

**Lane:** wifi-rx-smac-complete (task-221). **READ-ONLY**: every item is a load; no write, no
retry, no address outside the host-readable windows. Use the resident read helper
(`bar0read`, physical addresses, O_RDONLY + PROT_READ; it refuses unaligned addresses and the
RC misc window). The Lead fires; this lane touches no device.

**Why this pack:** the previous F1 pack measured the *consumer* counters (+0xCC5/+0x1AD4/+0x1ADA,
which stayed 0). This pack measures the **producer** itself: the ISR's own entry counter
(`band_obj+0xCC0`), the SMAC ack word it writes (`[base+0xe4] = 4`), the SMAC status word its
first gate reads (`[base+0xe8]` bit2), and the other SMAC sources' counters. That splits
"the RX ISR never entered" (event/mask/gate) from "the ISR entered but the consumer gates are
the break" in one pass. Conversions are in [TABLES.md](TABLES.md) T4.

## P0 (host, no device) - baseline D2H catalogue

Before and after the burst: `cat rd_catalog`. Expected: ready pair (g4 id1/id2) + HEARTBEAT
(g4 id4) present in both; **no g1 id13**. Also record whether any **group-2 id-3** D2H frame is
seen: this decides the one direction question (does the producer's message reach the host?).

## P1-P11 (one pass before and one after the client auth burst; same boot)

| # | item | CA (device window) | phys | width | expect / meaning |
| ---: | --- | ---: | ---: | ---: | --- |
| P1 | band dispatch word | 0x180814 | 0x40838814 | 4 | +8 -> 0x174BF0, +0x1C -> 0x175A18; sanity for P2/P3 |
| P2 | band0 obj +0x2a0 (SMAC base) | 0x174E90 | 0x4082CE90 | 4 | 0x40042000 (boot default) or a FW-RAM SMAC context (Lead bench: 0x00114F18) |
| P3 | band1 obj +0x2a0 | 0x175CB8 | 0x4082DCB8 | 4 | 0x40062000 = band1 SMAC context never attached |
| P4 | SMAC status word | BASE+0xE8 | if BASE is device IO: BASE+0x3B800E8; if FW RAM: BASE+0x406B80E8 | 4 | bit2 = producer's first gate; compare before/after |
| P5 | band0 producer entry counter | 0x1758B0 | 0x4082D8B0 | 1 | any increment = the ISR body ran past the bit2 gate |
| P5b | band1 producer entry counter | 0x1766D8 | 0x4082E6D8 | 1 | band1 mirror |
| P6 | SMAC ack word | BASE+0xE4 | as P4 | 4 | producer writes 4; change = ISR work |
| P7 | other SMAC source counters | 0x1758A8 (body D +0xCB8) / 0x1758A9 (+0xCB9) | 0x4082D8A8 / 0x4082D8A9 | 1 | movement = SMAC IRQ delivery works at all (other sources) |
| P8 | MAC band0 status/enable | 0x4010820C / 0x40108214 | 0x404C020C / 0x404C0214 | 4 | enable written by MAC start 0x4AF0C: band0 = 0xF (bits 0..3) |
| P9 | MAC band1 status/enable | 0x4010C200 / 0x4010C220 | 0x404C4200 / 0x404C4220 | 4 | enable written by MAC start: band1 = 0x1029 (bits 0,3,5,12); bit->INTC id mapping [U] |
| P10 | consumer counters (existing pack) | band0 obj +0xCC5 (0x1758B5), +0x1ADA (0x1776CA), +0x1AD4 (0x1776C4) | 0x4082D8B5 / 0x4082E6CA / 0x4082E6C4 | 1 | bump = the consumer's own gates saw a frame |
| P11 | PPDU alloced list (band0) | 0x17525C / 0x175260 / 0x175264 | 0x4082D25C / 0x4082D260 / 0x4082D264 | 4 | head / tail / count; producer unlinks + re-arms one per completion |

Notes:
* P2/P3/P4/P6: read `[band_obj+0x2a0]` first, then follow it. If it is a device-IO base
  (0x400xxxxx) use the device-IO rule; if it is a firmware-RAM pointer (0x00114F18 per the Lead
  bench) use the firmware-RAM rule. Do not guess: record the pointer value itself (P2/P3).
* P8/P9 are the only host-visible view of the MAC interrupt enables; the INTC itself
  (CA 0x4016xxxx) is not mapped in the host region-3 window.
* All reads are within windows earlier lanes read successfully. If a read aborts or returns
  0xFFFFFFFF, record and stop - do not retry another address/width.

## Decision table (one pre/post pair during the laptop auth burst)

| observation | conclusion | next |
| --- | --- | --- |
| P5 increments and P6 changes | the producer ISR ran and posted a PPDU completion; the break is downstream | P10: if P10 does not move, it is a consumer gate (RX ctx NULL / forward flag / cap); capture firmware logs (0xA4E/0x49F/0x497) and escalate to the rx-mac-delivery consumer rung |
| P5 increments, P6 does not change | the handler entered but returned between the counter and the ack (dequeue/alloc failure path) | read firmware log lines 0x2a1/0x257/0x275 (the D2H g4 id0 OAM log class); P11 list state |
| P5 does not move, P7 moves | other SMAC sources are serviced but the RX source is not -> **the RX-complete source is not delivered** (MAC mask bit or status gate; compare P4 bit2) | P4: if bit2 is stuck SET the gate blocks before the counter; if bit2 moves, the mask-bit hypothesis (P8/P9) is the best lead; escalate to a firmware enable/mask lane |
| P5 and P7 both stay 0 | SMAC IRQ delivery is dead on this boot (context attach / MAC start / INTC) | re-run the AP ladder, re-read P1-P3; if the SMAC bases stay boot defaults, escalate to the smac-start attach rung (wifi-smac-start R7) |
| P0 shows a group-2 id-3 D2H frame | the producer's message reaches the host; the consumer 0xBBC9C is not being driven by this producer | record the frame and route to the hcc/D2H lane |
| page / mask anomaly at any point | firmware fault | power-cycle, recovery door, UART capture (read-only); do not fire |

## Safety

* Read-only, no writes, no retries of a failed width; never read phys 0x10160000..0x1016FFFF
  (RC misc window); never write CA 0x400392F0.
* One coherent boot per tuple; re-probe after any reboot.
* P0 before P1-P11 so a dead delivery channel is not misread as an RX fault.
