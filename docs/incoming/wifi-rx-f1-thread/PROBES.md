# PROBES - R7: the read-only F1 probe pack (RX delivery internals + EDCA context)

**Lane:** wifi-rx-f1-thread (task-216).  **READ-ONLY**: every item is a load; there
is no write, no retry and no address outside the host-readable windows.  The Lead
runs it with the resident read helper (`bar0read`, physical addresses, O_RDONLY +
PROT_READ; it refuses unaligned addresses and the RC misc window).  Conversion
**[V\*]**: firmware RAM phys = 0x40000000 + CA + 0x6B8000; device IO phys =
CA + 0x3B8000; PKTRAM phys = 0x40000000 + 0x1D8000 + (CA - 0x01000000); ACP phys =
0x40000000 + 0x4D8000 + (CA - 0x02000000).  Never read the RC misc window
phys 0x10160000..0x1016FFFF; never write CA 0x400392F0.

Run order: **P0 -> P1 -> P2 -> P3 -> P4 -> P5 before the EDCA post**, then repeat
**P1/P4** after the EDCA post and **P2/P3/P1/P4** after the laptop auth attempt.
Physical addresses are printed by `tools/fw_rx_f1_thread_re.py --probes`.

## P0 - driver/sysfs gates (read-only)

| item | how | expect |
| --- | --- | --- |
| page | `cat $D/boot` | 0x86807FEF / 7 / 2 / 0xABCD (golden) |
| state / exception mask | `cat $D/status` | state 1, mask 0 |
| D2H direct reader counters | `cat $D/rd_status ; cat $D/rd_catalog` | ready pair seen; RX families absent / frames=0 (the F1 symptom) |
| bench link, recovery, UART | `tools/bench-link.sh`, recovery card, `tools/uart-capture.sh` | PASS / READY / running (no bench contact by this lane) |

## P1 - D2H publish state (does the firmware stage anything?)

| # | item | CA | width | expected |
| ---: | --- | ---: | ---: | --- |
| 1 | notify object pointer `u32[0x172130]` | 0x00172130 | 4 | 0x0010C0F4 (the HCC object) [I]; 0 = object not created |
| 2 | D2H bitmap `out[0]` | 0x40039010 | 4 | 0 idle |
| 3 | D2H bitmap `out[1]` | 0x40039014 | 4 | 0 idle (host drains it) |
| 4 | D2H doorbell | 0x40101434 | 4 | 0 idle (bit0 while a notify is pending) |
| 5 | D2H ack / re-arm (host-written) | 0x40101438 / 0x40101414 | 4 | 0 / 0 |
| 6 | H2D pending / doorbell | 0x40039010 / 0x400392D4 | 4 | 0 / 0 between sends |
| 7 | gate word | 0x4000010C | 4 | 0x0000CECE |
| 8 | G object +0xB4 (in-flight) | 0x0010C1A8 | 4 | 0 idle; nonzero only during a notify |
| 9 | G object +0xB8 (deferred) | 0x0010C1AC | 4 | 0 idle |
| 10 | G object +0x9C (out word ptr) | 0x0010C190 | 4 | a device-IO CA (0x4003xxxx / 0x4010xxxx) |
| 11 | G object +0xA4 (doorbell ptr) | 0x0010C198 | 4 | a device-IO CA (0x4010xxxx) |

**PASS/interpretation:** if an RX D2H is staged, item 2/3/4 or 8/9 change in the
same interval as the auth attempt (and `rd_status` frames rise).  If everything
stays idle, the firmware produced nothing on the D2H surface - the failure is
upstream of the D2H publish (RX consumer / first-PPDU path).

## P2 - RX ring state (host config + live bank)

The host config block is CA 0x40040000; the live bank is CA 0x40050000.  The
committed stock dump gives the reference words (TABLES T7).

| # | item | CA | width | stock |
| ---: | --- | ---: | ---: | ---: |
| 1 | free-ring base(s) | 0x40040020 / 0x40040024 | 4 | 0x844C7000 / 0x844C8000 (our window differs) |
| 2 | completion-ring base | 0x40040028 | 4 | 0x83734000 |
| 3 | ring depth words | 0x4004002C / 0x40040030 / 0x40040034 | 4 | 0x100 / 0x200 / 0xFFF |
| 4 | completion producer/consumer | 0x40040038 / 0x4004003C / 0x40040040 | 4 | 0x8000 / 0x8000 / 0 |
| 5 | host-MAC status mask | 0x40040048 | 4 | 0xFFFFFCC1 (bit10 masked) |
| 6 | live MAC status | 0x40050000 | 4 | 0x400 idle stock; 0x182 idle ours; 0x582 after received frames |
| 7 | free-ring read pointers | 0x40050008 / 0x4005000C | 4 | 0x20 / 0x20 (init prefetch) |
| 8 | completion write pointer | 0x40050010 | 4 | 0 (the device never wrote a completion) |
| 9 | live support words | 0x40050024 / 0x28 / 0x2C / 0x30 | 4 | 0x00800000 / 0x01800800 / 0x02801000 / 0x03801800 |

**PASS/interpretation:** rings conformant and rptrs still 0x20 with cmp wptr 0 ->
the device never consumed the free ring into a completion (the known F1 shape).
A movement here without a frame is a different (host-ring) fault.

## P3 - ETE / D2H ring state

| # | item | CA | width | expected |
| ---: | --- | ---: | ---: | --- |
| 1 | DR0 enable | 0x4003A590 | 4 | 1 |
| 2 | DR0 group-A base (device ring) | 0x4003A5A0 | 4 | a PKTRAM CA (0x01xxxxxx) |
| 3 | DR0 group-A prd/cns | 0x4003A5A8 / 0x4003A5AC | 4 | stock idle 3 / 3 |
| 4 | DR0 group-B base (host ring) | 0x4003A5C0 | 4 | our host DMA window |
| 5 | DR0 group-B prd/cns | 0x4003A5C8 / 0x4003A5CC | 4 | stock idle 3 / 3 |
| 6 | SR0 enable / group-A base | 0x4003A400 / 0x4003A410 | 4 | 1 / host DMA window |
| 7 | ETE status / DR0 send word | 0x4003A874 / 0x4003AC00 | 4 | 0 idle / 0x00030003 idle stock |

**Two-step:** read DR0 group-A base (item 2); if it is in PKTRAM
(0x01000000..0x011DFFFF), read the first 8 nodes (8 B each) at
`PKTRAM phys = 0x40000000 + 0x1D8000 + (base - 0x01000000)`.  A D2H message
stage appends a node with `(len<<16)|0x6d2b` and a nonzero length.  Nodes are
host-readable (the same window our direct D2H reader uses).

**PASS/interpretation:** a new node after the auth attempt == the firmware staged a
D2H frame; then `rd_status`/`rd_catalog` must show it (if not, the F1 side has
moved to the host reader, a different lane).  No new node and idle P1 == the
firmware never entered the RX delivery path.

## P4 - the PPDU descriptor list + the EDCA context (the new probe)

Two-step formula (**TABLES T5**): read `a = u32[0x00171458]` and
`b = u16[0x00171504]` (both firmware-RAM reads), then `ctx = a + b` (chip 0).
Gate: `ctx` must be in firmware RAM (0x00100000..0x001BFFFF); otherwise STOP the
interpretation (the formula/context is not what we think).

| # | item | offset | width | expected |
| ---: | --- | ---: | ---: | --- |
| 1 | ctx table word / offset | 0x00171458 / 0x00171504 | 4 / 2 | pointer / small offset |
| 2 | PPDU list next / prev | ctx+0x66C / +0x670 | 4 | both == ctx+0x66C when the list is empty |
| 3 | PPDU descriptor count | ctx+0x674 | 4 | 0 if nothing is allocated |
| 4 | **name-pointer self-check** | ctx+0x67C | 4 | **exactly 0x001060F7** |
| 5 | band-object pointer | ctx+0x4B8 | 4 | nonzero with the 2.4 GHz AP up; bandobj+0x1A00 != 0 |
| 6 | EDCA checksum | ctx+0x4AA | 2 | pre-fire record; predicted 0x0000 after the fire |
| 7 | EDCA flags | ctx+0x533 | 1 | pre-fire record; 0x00 predicted after the fire |
| 8 | EDCA bit2 | ctx+0x0DB | 1 | bit2 == 0 predicted after the fire (zero payload) |

**PASS/interpretation:** item 4 matching is the proof that the per-chip context
formula and the PPDU-list init are both correct; then 2/3 tell whether the
firmware has descriptors allocated, and 6/7/8 are the EDCA handler-effect
observable (the only new evidence the EDCA fire adds beside the consumed
announce).  If item 4 does not match, record a = / b = and STOP - the ctx formula
needs a correction lane, do not chase the RX fault with a wrong base.

## P5 - SMAC/band words (working reference and 5 GHz state)

| # | item | CA | width | expected |
| ---: | --- | ---: | ---: | --- |
| 1 | band-0 SMAC status / enable | 0x4010820C / 0x40108214 | 4 | 0x40 / 0x0F (working reference) |
| 2 | band-0 TRX triple | 0x4010E150 / 54 / 58 | 4 | 0x33 / 0xFF / 0xFF |
| 3 | band-1 SMAC status / enable | 0x4010C200 / 0x4010C220 | 4 | 0x1630 / 0x1039 (or 0x1029 after the R5 start) |

These are the same words the tbtt-timer lane's R6 pack uses; include them so the
RX pass and the 5 GHz arm pass share one differential.

## Decision table (one reading pass)

| observation | conclusion | next step |
| --- | --- | --- |
| P1/P3 idle after auth, P2 unchanged, P4 item4 matches and list empty | firmware never entered the RX delivery path / no PPDU descriptor consumer | escalate: stock-control run + the stock capture around a client auth (FIRE-RECIPE sec. 4) |
| P3 new node / P1 bitmap set, but `rd_status` frames 0 | firmware delivered a D2H; host reader missed it | host D2H-reader lane (not F1) |
| P2 rptrs move / cmp wptr moves, no node | device consumed the free ring but did not stage | firmware RX consumer fault (pairs with the R2b page-loss class) |
| P4 item4 mismatch | ctx formula wrong / PPDU list not in this object | record a,b; STOP; correction lane |
| page/mask anomaly at any point | firmware fault | power-cycle; recovery door; capture UART |

## Safety

Read-only, no retries, no writes.  If a read returns 0xFFFFFFFF or an
external-abort, the window is not mapped in this boot - record and stop, do not
retry another address width.  The EDCA post itself is a **write path** and is
governed by [FIRE-RECIPE.md](FIRE-RECIPE.md), not by this document.
