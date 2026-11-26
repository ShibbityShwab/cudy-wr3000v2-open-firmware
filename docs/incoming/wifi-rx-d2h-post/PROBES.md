# PROBES - read-only pack P0/P12-P19: decide "local dispatch dead" vs "CRX gates" vs "transport queue dead"

**Lane:** wifi-rx-d2h-post (task-226). **READ-ONLY**: every item is a load; no write, no retry,
no address outside the host-readable windows. The Lead fires; this lane touches no device.
Use the resident read helper (`bar0read`, physical addresses, O_RDONLY + PROT_READ; it refuses
unaligned addresses and the RC misc window). Conversions: [TABLES.md](TABLES.md) T5.

**Why this pack:** the F1 measured the SMAC producer entry counter (`band_obj+0xCC0` +105) and
the PPDU-list unlink, and the host catalogue shows g1 id3 + g4 id4 but no g2 id3 and no g1 id13.
The producer's post was accepted by `0x102760` only if the g2 id3 also resolved the local
group-2 table (same call validates it) - so the next read must split:
**is the g2 id3 sitting in the local queue (C5), dropped by the CRX gates (C6), or built as
g1 id13 but never deposited (C7)?** Four counters decide it: the g2-item stats counter (accepted
posts), the queue-1 list count (undrained), `band+0xCC5` (CRX drops) and the host catalogue.

## P0 (host) - baseline and post-burst D2H catalogue [V\*]

Before and after the burst: `cat rd_catalog` (or the direct-reader sysfs catalogue). Record:
the ready pair g4 id1/id2, the heartbeat g4 id4, config replies g1 id3, **any group-2 id-3**,
**any g1 id-13**. This is the final "did an RX frame reach the host" observable.

## P12-P19 (one pass before and one after the client auth burst; same boot)

| # | item | CA | phys | width | expect / meaning |
| ---: | --- | ---: | ---: | ---: | --- |
| P12 | band0 object word 0 (dest-nibble source) | 0x174BF0 | 0x4082CBF0 | 4 | bytes: [0] = chip-id byte copied to hdr[1] high nibble, [1] = consumer resolver arg2, [2] = hdr[2] source; record the exact word |
| P12b | band1 object word 0 | 0x175A18 | 0x4082E618 | 4 | band-1 mirror |
| P13 | producer post-failure counter | 0x1758BE (band0 +0xCCE) | 0x4082D8BE | 1 | must stay 0; any increment = `0x102760` rejected the g2 id3 (queue lookup/group/id fail) |
| P13b | band1 post-failure counter | 0x1766E6 | 0x4082E6E6 | 1 | band-1 mirror |
| P14 | destination record 0 | 0x171FB4 | 0x4082D? - use 0x8283B4 | 4x6 | [0]=dest idx, [1]=map ptr, [2]=queue-array ptr, [4]=group-array ptr; sanity for P15/P16 |
| P15 | queue array (dest 0) | follow P14[2] | - | 3 x 0xE4 | q0 = base, q1 = base+0xE4, q2 = base+0x1C8; per queue read `+8` state, `+0x48` list count, `+0xC0` notifier, `+0xC4` flag |
| P16 | group array (dest 0) | follow P14[4] | - | 5 x 12 | group record = {max_id, item_array, qidx}; for group 2 follow `item_array` and read entry[3] = {id, handler, aux, stats}: **handler must be 0xBBC9C**; **stats delta = number of g2 id3 messages `0x102760` accepted** |
| P17 | control: group 0 item stats | follow P16 group-0 item_array | - | 16 | g0 traffic counter as a control that the item-array layout is right |
| P18 | CRX drop counter | 0x1758B5 (band0 +0xCC5) | 0x4082D8B5 | 1 | increment = the CRX builder dropped frames at its gates (`chip+0x1a00` / forward flag / length window / total cap) |
| P19 | band1 mirrors of P14-P18 | - | - | - | band-1 copy of the same reads |

Notes:
* P15/P16 require following two pointers. If a followed value is not in the host-readable
  firmware-RAM window (0x6B8000 + CA, CA < 0x1C0000), record it and stop - do not chase.
* The exact runtime queue/group bases are in [TABLES.md](TABLES.md) T4; P14 verifies them
  against the decode (P14[2] should equal 0x171B70, P14[4] should equal 0x171F54).
* The queue-1 list count is where a stuck local dispatch shows up: `0x102760` appends to
  `queue+0x40` and only removes on the worker's pop (`0x1024EA`). If item stats (P16) grows
  but the queue-1 count (P15) grows by the same amount, the local worker is not draining.
* P13 = 0 while P16 stats does not move means `0x102760` failed *before* the stats update
  (dest/group resolution), not in the queue lookup.
* All reads are within windows earlier lanes read successfully. If a read aborts or returns
  0xFFFFFFFF, record and stop - do not retry another address/width.

## Decision matrix (one pre/post pair during the laptop auth burst)

| P16 g2-id3 stats | P15 q1 list count | P18 band+0xCC5 | P0 catalogue | conclusion | next |
| --- | --- | --- | --- | --- | --- |
| no move | no move | no move | no g1 id13 | the g2 id3 never reaches `0x102760` stats: dest/group resolution fails or the ISR post is rejected earlier | read P12 (dest nibble) + P13; if P13=0 re-check P14/P16 layout with the decode; escalate to the producer/post rung |
| move | grows by same delta | no move | no g1 id13 | **C5: local dispatch queue not drained** (queue-1 notifier/worker not wired or not scheduled) | inspect P15 notifier pointers (q1 vs q0/q2): a NULL/garbage q1 notifier is the gate; next rung = the hcc worker/notifier registration |
| move | stays 0 | moves | no g1 id13 | **C6: local dispatch ran, CRX gates dropped the frames** | `band+0xCC5` increments confirm; next rung = CRX gate (per-chip RX ctx / forward flag / length window) |
| move | stays 0 | no move | no g1 id13 | **C7: g1 id13 built but not deposited** (transport queue/customer for group 1) | read the group-1 queue counts/notifiers from P15 (q0) and the g1 id13 post counter at `0xBA17A`'s call site |
| move | stays 0 | no move | **g1 id13 appears** | the path works end-to-end; the F1's "no host RX" was the pre-fix state | record the frame, close the RX chain |
| any counter moves with no burst | - | - | - | baseline noise/other traffic | re-baseline on a quiet BSS, one coherent boot per tuple |

## Safety

* Read-only loads only; no writes anywhere; never write CA 0x400392F0; never read the RC misc
  window 0x10160000..0x1016FFFF; no retries of a failed width.
* One coherent boot per tuple; re-probe after any reboot.
* P0 before P12-P19 so a dead delivery channel is not misread as an RX fault.
