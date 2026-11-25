# TABLES - lane wifi-fw-tbtt-arm (task-193)

All firmware addresses are CPU addresses in the project's image-linear
convention: **CPU = file offset + 0x40000** of the pinned FIRMWARE.bin
(928,920 B, sha256 7fc87e20...311b). Host read paths:
**firmware RAM phys = 0x40000000 + (CPU + 0x6B8000)**;
**device IO phys = CPU + 0x3B8000** (equivalently BAR0 offset =
0x3B8000 + (CPU - 0x40000000), phys = 0x40000000 + BAR0 offset).
Everything here is re-derived by tools/fw_tbtt_arm_re.py --selftest from the
local artifacts; no vendor byte is stored in this repository.

Grades: **[V]** re-derived by the committed tool; **[V\*]** measured by a
committed lane/Lead; **[I]** inference; **[U]** needs a device read.

## T1. Conventions and safety

| item | value |
| --- | --- |
| firmware CPU base | file + 0x40000 |
| firmware RAM BAR0 | CPU + 0x6B8000 |
| device IO BAR0 | 0x3B8000 + (CPU - 0x40000000) |
| forbidden reads | RC misc window 0x10160000..0x1016FFFF |
| writes in this lane | none at all; never write CA 0x400392F0 |
| firmware identity | 928,920 B, sha256 7fc87e20...311b (tool-checked) |

## T2. Per-band MAC block and init tables [V]

| element | band0 | band1 |
| --- | ---: | ---: |
| device CA block base | 0x40110000 | 0x40114000 |
| init table CA | 0x105714 | 0x105770 |
| table literal CA | 0x9F494 | 0x9F964 |
| applier base loop | CA 0x9F3D6/0x9F3DA | CA 0x9F91E/0x9F922 |
| base expression | 0x40000000+0x110000 | 0x40004000+0x110000 |
| band-id register write | 0x401105BC = band value | 0x401145BC = 0 |
| entries | 23 x {u16 offset, u16 value} | same |

**Complete offset set of both tables:** 0x548, 0x54C, 0x550, 0x554, 0x55C,
0x560, 0x564, 0x568, 0x588, 0x594, 0x5BC. **No 0x500-0x544 entry, nothing
above 0x5BC.** The value set is {0x0000, 0x0001, 0x0002, 0x0003, 0x0011,
0x0022, 0x0024, 0x0033, 0x0035, 0x0101, 0x6000, 0x6001, 0x6101}.

Band-1 differences are exactly 10 pairs, indices 2,3,4,5,7,12,13,14,15,17:

| idx | band0 | band1 | note |
| ---: | --- | --- | --- |
| 2 | 0x588 = 0x0033 | 0x588 = 0x0035 | |
| 3 | 0x588 = 0x0022 | 0x588 = 0x0024 | |
| 4 | 0x548 = 0x0000 | 0x548 = 0x6000 | 0x6000 band marker |
| 5 | 0x548 = 0x0001 | 0x548 = 0x6001 | |
| 7 | 0x548 = 0x0101 | 0x548 = 0x6101 | **final value; NOT an arm bit** |
| 12 | 0x594 = 0x0033 | 0x594 = 0x0035 | |
| 13 | 0x594 = 0x0022 | 0x594 = 0x0024 | |
| 14 | 0x54C = 0x0000 | 0x54C = 0x6000 | |
| 15 | 0x54C = 0x0001 | 0x54C = 0x6001 | |
| 17 | 0x54C = 0x0101 | 0x54C = 0x6101 | final value |

Neither table stores a beacon interval, DTIM, TSF or other time value: the
maximum value in both tables is 0x6101 and the offset map has no timer window.

## T3. Per-band start CA 0x4AF0C / stop CA 0xBE294 [V]

Start (r0 = band object, r1 = request; r7 = band object, r4 = request):

| CA | instruction effect |
| ---: | --- |
| 0x4AF5C | band = u8(perlink+0x11D) |
| 0x4AF6A | band1 base = 0x40114500 (literal CA 0x4B118) |
| 0x4AF72/0x4AF84 | write 0x55 then 0xFF to base+0x500 (reset pulse) |
| 0x4AF90/0x4AF98 | check 0x4011451C == 0xFF |
| 0x4B06C/0x4B074 | check 0x40114520 == 0xFF |
| 0x4AFB2/0x4AFB6 | 0x4010C220 |= 1 |
| 0x4AFBC/0x4AFC0 | 0x4010C220 |= 8 |
| 0x4AFC6/0x4AFCA | 0x4010C220 |= 0x20 |
| 0x4AFD0/0x4AFD4 | 0x4010C220 |= 0x1000 -> **band-1 enable = 0x1029** |
| 0x4B07C | band0 base = 0x40110500 (literal CA 0x4B11C) |
| 0x4B084/0x4B096 | 0x55 then 0xFF to base+0x500 |
| 0x4B0A2/0x4B0B2 | check 0x4011051C/0x40110520 == 0xFF |
| 0x4B0CC/0x4B0D6/0x4B0E0/0x4AFD4 | 0x40108214 |= 1,2,4,8 -> **band-0 enable = 0x0F** |
| 0x4AFE0/0x4AFFC | set bit0 of 0x4011080C (band0, literal 0x4B100) or 0x4011480C (band1, +0x4000) |
| 0x4B008/0x4B00A/0x4B012 | 0x40101220 |= 1 (band1) / 2 (band0) (literal 0x4B104) |
| 0x4AF2A/0x4AF32 | early-out: request[1]==2 or request[4]==0xFF -> no-op (already started/never started) |

Stop CA 0xBE294 is the mirror: picks 0x40114500/0x40110500 (literals CA
0xBE330/0xBE334), clears 0x4010C220 bits 1|8|0x20|0x1000 (literal 0xBE338) or
0x40108214 bits 0..3 (literal 0xBE340), clears bit0 of 0x4011080C (literal
0xBE33C)/0x4011480C, and zeroes the band reset word.

## T4. Per-band restore CA 0xD9504 - fixed constants only [V]

| element | value |
| --- | --- |
| table base literal CA 0xD95BC | 0x00106D4C |
| band0 register literals CA 0xD95C0..0xD95D8 | 0x40110548/54C/55C/560/554/564/5BC |
| band0 block base literal CA 0xD95DC | 0x40110000 |
| band1 register literals CA 0xD95E0..0xD95FC | 0x40114548/54C/55C/560/554/564/5BC/5AC |
| band1 block base literal CA 0xD9600 | 0x40114000 |
| band0 writes | 0x548/0x54C/0x55C/0x560 = 0x1000; 0x554/0x564 = 3; 0x5BC = 0 |
| band1 writes | 0x548/0x54C = 0x3000; 0x55C/0x560 = 0x1000; 0x554/0x564 = 3; 0x5BC = 0; 0x5AC = table_byte<<4\|table_byte |
| per-mode restore lists | array of 14 {ptr,count} at CA 0x106F24; e.g. idx0 {0x1158AA,25}, idx13 {0x1153E6,11} |
| idx13 offsets | 0x570,0x544,0x5A0,0x57C,0x57C,0x574,0x57C,0x580,0x580,0x574,0x580 |
| mode-value range | all values < 0x8000 (no time/TSF value) |

## T5. Reset/check helpers [V]

| CA | writes | verifies |
| ---: | --- | --- |
| 0xD968C (band1) | 4,12,0x4C,0xCC,0xCD,0xCF,0xDF,0xFF -> 0x40114500 | 0x4011451C = 0x40114520 = 0xFF |
| 0xD97BC (band0) | same -> 0x40110500 | 0x4011051C/0x40110520 = 0xFF |
| 0xD9604 (SMAC) | 16,24,56,0x1038,0x1039 -> 0x4010C220 | 0x4010C238 = 0x1039, 0x4010C23C = 0x39 |
| 0xD973C (MAC0) | 4,5,7,0xF -> 0x40108214 | 0x40108228/0x4010822C = 0xF00 |

## T6. VAP start CA 0x46394 - beacon state creation [V]

| CA | effect |
| ---: | --- |
| 0x463D4 | bandobj = *(ctx+0x4B8) |
| 0x46450 | perlink = *(ctx+0x4B0) |
| 0x464F2 | first-start guard: u8(perlink+0x6C6) == 1 -> skip allocation |
| 0x464FA/0x4650A | alloc 0x118 = 280 B (CA 0xBF128) |
| 0x46524/0x46530 | alloc 0x5C = 92 B (CA 0xBF128) |
| 0x46586/0x46588 | perlink+0x6C4 = 16 |
| 0x46590 | perlink+0x6C0 = 92 B descriptor |
| 0x46596 | perlink+0x6B8 = 280 B buffer |
| 0x4659A | perlink+0x6C6 = 1 |
| 0x466CE | CA 0xB5290(perlink, buffer, &stack) |
| 0x466E8 | CA 0xB7208(module ctx, 0x310 B frame, 1, 14) |
| 0x466F8 | CA 0xB3018(perlink, buffer, frame, &stack2) |
| 0x4670C/0x46714 | CA 0xEE394(buffer+32) -> *perlink+0x6BC |
| 0x464B0 | CA 0x971D8(bandobj, ctx) - band start |
| range check | **no device-IO literal load in 0x46394..0x467C8** (tool) |
| band start | **no device-IO literal load in 0x971D8..0x9744C** (tool) |

## T7. Beacon/TRX message-processor bit operations [V]

`[r2,#-24]` = 0x40101220 - 0x18 = 0x40101208.

| CA | body gate | effect |
| ---: | --- | --- |
| 0xE6A26 | body[2] == 0 | 0x40101220 |= 0x1 ; 0x40101208 &= ~(0x1\|0x4) |
| 0xE6A70 | body[2] == 0 | 0x40101220 &= ~0x1 ; 0x40101208 |= 0x1\|0x4 |
| 0xE6AB4 | body[2] != 0 | 0x40101220 |= 0x2 ; 0x40101208 &= ~(0x2\|0x8) |
| 0xE6B00 | body[2] != 0 | 0x40101220 &= ~0x2 ; 0x40101208 |= 0x2\|0x8 |

Name blob: `smac_msg_proc_beacon_tx_resume_req` CA 0x122904,
`smac_msg_proc_beacon_tx_suspend_req` CA 0x122928. **No g0-id1 table-A
(0x10C23C, 217 rows) or table-B (0x103BF8, 88 rows) key maps to 0xE6A26 /
0xE6A70 / 0xE6AB4 / 0xE6B00** (tool-checked negative).

## T8. Band-1 TRX-enable worker CA 0xE8520 [V]

| element | fact |
| --- | --- |
| body fields | +0xA = band index (1 = 5 GHz), +0xE = module/on flag (u8) |
| band1 enable | 0x4010E150 = 0x33, 0x4010E154 = 0xFF, 0x4010E158 = 0xFF |
| literals | CA 0xE8660/0xE8664 = 0x4010E150/0x4010E154; +4 store = 0x4010E158 |
| band1 disable | zeroes 0x4010E1B8..+0x1C (literal CA 0xE8668) |
| band0 | 0x4010820C/0x40108214 (literals CA 0xE8638/0xE863C) |
| SoC INTC literals | CA 0xE8640/0xE8644/0xE8648 = 0x40002268/0x40002264/0x4000226C; also 0x40000068/0x40000064/0x4000006C |

## T9. TBTT names and G hooks [V names / U entries]

| element | value |
| --- | --- |
| G table | CA 0x1747C4 (literal CA 0x9745C) |
| G+0x370 MAC-start hook | read @0x97360, cbz, call @0x97366; G+0x370 = 0 [V\*] |
| G+0x374 beacon-start hook | read @0x9736A, cbz, call @0x97372; = 0x00057639 [V\*] |
| name blob | CA 0x12266C..0x122BA4; `smac_tbtt_5g_isr` CA 0x1229E8, `smac_tbtt_2g_isr` CA 0x1229FC |
| code pointer to blob | none (LE32 scan; also no pointer to the two TBTT names) |
| handler entries | **[U] offline** |
| 0x57639 decode | locked list walk over 184-byte records (not an MMIO beacon arm) [V] |

## T10. R3 read-only probe pack (differential band0 vs band1)

Firmware RAM (BAR0 = CPU + 0x6B8000; phys = 0x40000000 + BAR0):

| # | object | CA | BAR0 | phys | len |
| ---: | --- | ---: | ---: | ---: | ---: |
| 1 | ctx slot k ptr (k=0..18) | 0x171458+4k | 0x829458+4k | 0x40829458+4k | 4 |
| 2 | ctx table u16 bias | 0x172504 | 0x82A504 | 0x4082A504 | 2 |
| 3 | ctx0 body | 0x119758 | 0x7D1758 | 0x407D1758 | 4 |
| 4 | bandobj0 published perlink | 0x15E408 | 0x816408 | 0x40816408 | 4 |
| 5 | bandobj1 published perlink | 0x15FF70 | 0x817F70 | 0x40817F70 | 4 |
| 6 | G head / G+0x370 / G+0x374 | 0x1747C4/34/38 | 0x82C7C4/34/38 | 0x4082C7C4/34/38 | 4 each |
| 7 | bandobj b +0xD6C/D70 | 0x15D774/78, 0x15F2DC/E0 | +0x815774/78, +0x8172DC/E0 | | 1 each |
| 8 | perlink[b] +0x6B8/6BC/6C0/6C4/6C6 | two-step | | | 4/4/4/2/1 |
| 9 | 92-byte descriptor *(perlink[b]+0x6C0) | two-step | | | 92 |
| 10 | 280-byte buffer *(perlink[b]+0x6B8) first 32 B | two-step | | | 32 |

Device IO (BAR0 = 0x3B8000 + (CPU - 0x40000000); phys = 0x40000000 + BAR0):

| # | object | CA | BAR0 | phys | expected (band-0 reference) |
| ---: | --- | ---: | ---: | ---: | --- |
| 11 | band0 +0x500/51C/520/548/54C/588/594/5BC | 0x40110500..5BC | 0x4C85xx | 0x404C85xx | reset done; 0x548/54C class 0x0101 |
| 12 | band1 +0x500/51C/520/548/54C/588/594/5AC/5BC | 0x40114500..5BC | 0x4CC5xx | 0x404CC5xx | reset done; 0x548/54C = 0x6101 |
| 13 | band0 MAC-start bit | 0x4011080C | 0x4C880C | 0x404C880C | bit0 = 1 |
| 14 | band1 MAC-start bit | 0x4011480C | 0x4CC80C | 0x404CC80C | bit0 = 1 expected |
| 15 | band0 enable | 0x40108214 | 0x4C0214 | 0x404C0214 | bits 0..3 = 0xF |
| 16 | band0 TRX | 0x4010820C | 0x4C020C | 0x404C020C | enable-TRX path |
| 17 | band1 TRX triple | 0x4010E150/54/58 | 0x4C6150/54/58 | 0x404C6150/54/58 | 0x33 / 0xFF / 0xFF |
| 18 | SMAC IRQ status | 0x4010C200 | 0x4C4200 | 0x404C4200 | pending bits |
| 19 | SMAC IRQ enable | 0x4010C220 | 0x4C4220 | 0x404C4220 | contains 0x1029 |
| 20 | per-band run bits | 0x40101220 | 0x4B9220 | 0x404B9220 | bit0 (band1) and bit1 (band0) = 1 |
| 21 | beacon/TRX gate bits | 0x40101208 | 0x4B9208 | 0x404B9208 | equal on both bands expected |
| 22 | SoC INTC | 0x40002264/68/6C | 0x3BA264/68/6C | 0x403BA264/68/6C | band TRX path |

All reads are side-effect free. Never read the RC misc window
0x10160000..0x1016FFFF; never write CA 0x400392F0.

## T11. Negative results [V]

* No per-band block register stores a beacon interval / DTIM / time value.
* No init-table or restore-table entry has an offset in a 0x500-0x544 timer
  window; 0x548/0x54C = 0x6101 is the fixed band-1 marker.
* VAP start CA 0x46394 and band start CA 0x971D8 contain no device-IO literal
  load: they do not arm anything through MMIO.
* CA 0x7E5CC is the rate-control TX-descriptor filler (single caller 0x72184
  in the rate-adaptation function 0x70490), not a beacon-descriptor builder.
* No g0-id1 table-A/B key maps to any of the four beacon/TRX workers
  (0xE6A26/0xE6A70/0xE6AB4/0xE6B00) or to the enable-TRX worker 0xE8520.
* The 45-name ISR blob is unreferenced: no code pointer, no reference to its
  base, no pointer to the two TBTT names.

## T12. Reproduction

| mode | output |
| --- | --- |
| `--selftest` | 103 assertions over every table/constant/negative above; exit 0 PASS |
| `--decode` | the per-band block/start/beacon-state decode with the asserted instructions |
| `--tables` | the init tables, restore lists, reset/check tables, message-processor bit ops, probe summary |
| `--probes` | the R3 pack with CA/BAR0/phys (RAM and device IO) |
