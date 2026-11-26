# PROBES - read-only probe pack P1-P10 (MAC-RX intake attribution)

**Lane:** wifi-rx-mac-delivery (task-220).  **READ-ONLY**: every item is a load; no write,
no retry, no address outside the host-readable windows.  Use the resident read helper
(`bar0read`, physical addresses, O_RDONLY + PROT_READ; it refuses unaligned addresses and
the RC misc window).  Conversions: firmware RAM phys = CA + 0x406B8000; device IO phys =
CA + 0x3B8000; MAC live bank phys 0x40408000; MAC config block phys 0x403F8000
(see TABLES T9, committed by ../wifi-ppdu-init).  The Lead fires; this lane touches no
device.

## Why this pack

The decode (REPORT) shows the D2H deposit/notify/consume path is shared and alive, and the
only unresolved layer is **whether the SMAC RX-completion producer posts a received-frame
node into the queue consumed at CA 0xBA0C0**.  Three observable classes decide it:

1. the firmware-owned per-band device object **is initialised** (P1 - a precondition: if the
   name word is absent the addresses below are wrong for this boot and must not be read as
   data);
2. the datapath **touched received frames** (P5-P8 counters, differential across a client
   auth burst);
3. the MAC **saw the frames** (P9/P10, the committed live-bank reads) and the host D2H
   catalogue has/hasn't the g1 id13 event (P0, host-side, no device read).

## P0 (host, no device) - baseline D2H catalogue

Before and after the client burst: `cat rd_catalog` (direct D2H reader) must show the
ready pair (g4 id1/id2) + HEARTBEAT (g4 id4) in both snapshots and **zero g1 id13**.  This
re-confirms the delivery channel is alive in this boot (the lane's premise).

## P1-P10 (one pass before and one after the auth burst)

| # | item | CA | phys | width | expect / meaning |
| ---: | --- | ---: | ---: | ---: | --- |
| P1 | band-0 device-object name word | 0x0017526C | 0x4082D26C | 4 | **0x001060F7** = the PPDU-list init ran (precondition / self-check) |
| P2 | band-0 list next (+0x66C) | 0x0017525C | 0x4082D25C | 4 | bench [M] head node **0x01019F80** (PKTRAM phys 0x401F1F80); self-pointer only if the list is empty |
| P3 | band-0 list count (+0x674) | 0x00175264 | 0x4082D264 | 4 | bench [M] **0x10** = the 16 pre-allocated PKTRAM descriptors are present |
| P4 | band-1 name word (+0x67C) | 0x00176094 | 0x4082E094 | 4 | 0x001060F7 iff band 1 object inited |
| P5 | datapath counter +0xCC5 (u8) | 0x001758B5 | 0x4082D8B5 | 1 | bump = a received frame exceeded the 0x680 CRX cap (0xBA12E) |
| P6 | datapath counter +0x1ADA (u8) | 0x001766CA | 0x4082E6CA | 1 | bump = a per-frame negative status (0xBAF2A path) |
| P7 | datapath flag +0x1AD4 (u8) | 0x001766C4 | 0x4082E6C4 | 1 | compared with 1 before a per-frame field write (0xBAF38) |
| P8 | band-1 flag +0x1AD4 (u8) | 0x001774EC | 0x4082F4EC | 1 | band-1 mirror of P7 |
| P9 | MAC live-bank status | - | 0x40408000 | 4 | CA 0x40050000 via iATU: bit 10 = rx_ppdu_desc class (stock-latched, benign) |
| P10 | MAC config mask | - | 0x403F8048 | 4 | CA 0x40040048: 0xfffffcc1 stock/vendor value (bit 10 masked) |
| P11 | band-0 list prev (+0x670) | 0x00175260 | 0x4082D260 | 4 | bench [M] tail node 0x010186A8 (PKTRAM phys 0x401F06A8) |

Notes:
* P5-P8 are read as u8; a **+1 or more** between the two passes is the signal.  An
  unchanged value is meaningful only with P1-P4 coherent (same boot, list present).
* P9/P10 are the committed bench reads (R0 parity / R2 observe); they are included so one
  pass records the whole attribution tuple.  Do not read the RC misc window.
* All addresses are inside the same BAR windows the earlier lanes read successfully; if a
  read aborts or returns 0xFFFFFFFF, record and stop - do not retry another address/width.

## Decision table (one pre/post pair during the laptop auth burst)

| observation | conclusion | next |
| --- | --- | --- |
| P1 != 0x001060F7 | the object/address is not valid for this boot | do **not** interpret P2-P8; re-derive the boot state (ppdu-init P1/P7), go to FIRE-RECIPE R3 |
| P1 ok, P5/P6/P7/P8 move, P9 transitions | the RX datapath touched the client frames -> **intake produced frames**; the drop is in the consumer's gates (CRX ctx NULL / forward flag / cap) or the fault | capture dmesg for the CRX gate log codes 0xA4E / 0x49F / 0x497 and g1 id13; FIRE-RECIPE R2 |
| P1 ok, counters move, **P0 shows g1 id13** | **the MAC-RX -> firmware -> D2H path works end-to-end** | the F1 is closed on the fw side; the host decoder delivery is the next check |
| P1 ok, counters do **not** move, P9/P10 unchanged | intake dry: the SMAC RX-completion producer never posted a node | FIRE-RECIPE R2/R3 (stock control + escalation) |
| P1 ok, counters do not move, **P9 bit 10 transitions** | the MAC ran the completion write path but the firmware never saw a node: the break is between the MAC PPDU completion and the SMAC RX ISR (the producer layer) | FIRE-RECIPE R2/R3 (this is the B1 shape) |
| page/mask anomaly at any point | firmware fault | power-cycle, recovery door, UART capture (read-only); do not fire |

## Safety

* Read-only, no writes, no retries of a failed width.
* One coherent boot per tuple; re-probe after any reboot.
* P0 before P1-P10 so a dead delivery channel is not misread as an RX fault.
* Never read the RC misc window phys 0x10160000..0x1016FFFF; never write 0x400392F0.
