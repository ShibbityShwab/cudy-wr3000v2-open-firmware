# TABLES - lane wifi-tbtt-timer (task-214)

All firmware addresses are CPU addresses (CA = file offset + 0x40000 of the
pinned FIRMWARE.bin, 928,920 B, sha256 7fc87e20...311b). Host read paths:
**device IO phys = CA + 0x3B8000** (BAR0 offset = CA + 0x3B8000 - 0x40000000);
**firmware RAM phys = 0x40000000 + CA + 0x6B8000** (BAR0 offset = CA + 0x6B8000).
Everything here is re-derived by `tools/fw_tbtt_timer_re.py --selftest` from
the local artifacts. Grades: **[V]** (tool), **[V*]** (committed lane/Lead),
**[I]** inference, **[U]** needs a device read.

## T1. Conventions and safety

| item | value |
| --- | --- |
| firmware CPU base | file + 0x40000 |
| firmware RAM BAR0 | CA + 0x6B8000 |
| device IO BAR0 | CA + 0x3B8000 - 0x40000000 |
| host region-3 CA window | 0x40000000 .. 0x4011FFFF (only these IO CAs are host-readable) |
| **not host-readable** | firmware INTC CA 0x4016xxxx; RC misc window phys 0x10160000..0x1016FFFF (never read) |
| forbidden writes | CA 0x400392F0; the timer words in T6 are probe-only |
| firmware identity | 928,920 B, sha256 7fc87e20...311b (tool-checked) |

## T2. MAC / band start - complete MMIO footprint [V]

CA 0x4AF0C (MAC start), complete device IO literal set (exactly 10 values):

| site | value | effect |
| ---: | ---: | --- |
| 0x4AF6A | 0x40114500 | band1 reset base (write 0x55 then 0xFF) |
| 0x4AF90 | 0x4011451C | band1 reset status readback (expect 0xFF) |
| 0x4B06C | 0x40114520 | band1 reset status readback (expect 0xFF) |
| 0x4B07C | 0x40110500 | band0 reset base |
| 0x4B0A2 | 0x4011051C | band0 status readback |
| 0x4B0B2 | 0x40110520 | band0 status readback |
| 0x4AFAE | 0x4010C220 | band1 SMAC IRQ enable |= 1|8|0x20|0x1000 (0x1029) |
| 0x4B0C4 | 0x40108214 | band0 SMAC IRQ enable |= 0xF |
| 0x4AFE0 | 0x4011080C | MAC-start bit0 (+0x4000 = 0x4011480C for band1) |
| 0x4B00C | 0x40101220 | global run bit |= 1 (band1) / 2 (band0) |

Arm record at the end: request[1] = 2 (0x4B05E), request[0x64] += 1 (0x4B062) -
the exact R5 success path. **No timer word is referenced** (0x401015CC and
0x400024B0 absent from the literal set).

CA 0x971D8 (band start): **zero** device IO literals; calls G+0x370 (slot 0)
and G+0x374 (=0x57639 Thumb, body 0x57638).

## T3. Beacon hook 0x57638 (correction) [V]

| element | fact |
| --- | --- |
| installer | CA 0x5D13E loads static word at CA 0x5D280 = 0x00057639 |
| install site | CA 0x5D144 `str.w r3, [fp, #884]` = G+0x374 |
| hook body | mutex lock (0xFDF8E/0xFDF50), list-pop 0x77324 + free 0xFDF50 loop over obj+0xC lists, list re-init, returns 0/100/1015 |
| device IO literal in 0x57638..0x576E8 | **none** |

=> the hook is a list teardown, not an MMIO beacon arm; the hardware beacon
submission is deferred to firmware task/timer context.

## T4. Per-band SMAC ISR registration [V]

Registered at boot from the per-band init (CA 0xD1B68) via
0xC6DA2(irq, prio, handler) + 0xC6FF4(irq):

| band | IRQs | prios |
| --- | --- | --- |
| 0 (2.4G) | 47, 49, 50, 51, 52, 82, 83, 59 | 5,5,5,5,5,14,5,5 |
| 1 (5G) | 53, 55, 56, 57, 58, 84, 85, 60 | 5,5,5,5,5,14,5,5 |

Call sites: band0 0xD1EF4, 0xD1F04, 0xD1F14, 0xD1F24, 0xD1F34, 0xD1F44,
0xD1F54, 0xD1F64; band1 0xD21DC, 0xD21EC, 0xD21FC, 0xD220C, 0xD221C, 0xD222C,
0xD223C, 0xD224C (handler literal = the `ldr r2,[pc,#N]` 6 bytes before the
call; values are Thumb pointers).

| band0 IRQ | band1 IRQ | wrapper band0 | wrapper band1 | common body |
| --- | --- | --- | --- | --- |
| 47 | 53 | 0xAEC94 | 0xAEC88 | 0xAEC74 |
| 49 | 55 | 0xB54C8 | 0xB54BC | 0xB53A4 |
| 50, 83 | 56, 85 | 0xB0D50 | 0xB0D44 | 0xB0CE0 |
| 51 | 57 | 0xAEC68 | 0xAEC5C | 0xAEB80 |
| 52 | 58 | 0xAEB74 | 0xAEB68 | 0xAEA9C |
| 82 | 84 | 0xAE48C | 0xAE480 | 0xAE45C |
| 59 | 60 | 0xB0C44 | 0xB0C38 | 0xB0AB8 |

Wrapper shape: `ldr r3,=0x180814 ; ldr r0,[r3,#8]|#0x1C ; b.n <body>` already
re-derived for all 14 wrappers. Band dispatch word CA 0x180814: +8 = band0
SMAC object, +0x1C = band1 SMAC object.

## T5. Common ISR body signatures [V]

| body | first test / effect |
| ---: | --- |
| 0xAE45C | writes [ctx+672]+228 from a literal mask & [ctx+700]+0, minus [ctx+672]+232 |
| 0xAEA9C | [ctx+672]+232 bit8; counter [ctx+3256]; 18-B D2H msg (hdr[6]=0x23) |
| 0xAEB80 | [ctx+672]+232 bit7; 16-bit ring status loop; callback [ctx+3212]; 6-B D2H msg |
| 0xAEC74 | [ctx+672]+232 bit0; if clear -> 0xC0550(1) (deferred task event) |
| 0xB53A4 | [ctx+672]+232 bit2; ring dequeue (0xBFD4C/0xB084C/0xB2BD8); 20-B D2H msg |
| 0xB0CE0 | reads [ctx+700]+0 word; disables own IRQ; dispatch via 0xB0CB6/0xB07C2 (16-B D2H msg, hdr[6]=0x26) |
| 0xB0AB8 | [[ctx+740]+208] & ~[+48] bits 0/1/2; bit0 -> 0xB0930; logs for bits 1/2 |

ctx = the per-band SMAC object from 0x180814.

## T6. Timer candidate words [V addresses, U semantics]

| CA | width | evidence |
| ---: | ---: | --- |
| 0x401015CC | 4 | write 1 = latch/start (0x89812/0x89816); write 0 = clear (0xA0052, 0x897EE); write a count in per-band init (0x9F438); polled in many wait paths |
| 0x400024B0 | 2 | literal 100 written by both per-band init paths (0x9F434/0x9F43E/0x9F440); timer-sync path saves the 16-bit value then reloads 100 (0x8980C..0x89828) |

Derivation of the second address: 0x401015CC - 0xFF000 - 0x11C = 0x400024B0
(tool-checked). Nothing else in the start path references either word. The
literal 100 is the only interval-sized constant in the arm/init code.

## T7. R6 read-only probe pack [V addresses]

Device IO (host region-3; BAR0 offset = CA - 0x3C48000, phys = CA + 0x3B8000):

| # | object | CA | width | band0 reference |
| ---: | --- | ---: | ---: | --- |
| 1 | MAC start band0/band1 | 0x4011080C / 0x4011480C | 4 | bit0 = 1 after R5 |
| 2 | SMAC IRQ enable band1 / band0 | 0x4010C220 / 0x40108214 | 4 | 0x1029 / 0x0F |
| 3 | SMAC IRQ status band1 | 0x4010C200 | 4 | [U] pending source bits |
| 4 | SMAC TRX band1 | 0x4010E150/54/58 | 4 | 0x33 / 0xFF / 0xFF |
| 5 | SMAC TRX0 band0 | 0x4010820C | 4 | enable-TRX path |
| 6 | global run / gate | 0x40101220 / 0x40101208 | 4 | run bits / beacon+TRX gates |
| 7 | timer word 0 | 0x401015CC | 4 | [U] differential |
| 8 | timer word 1 | 0x400024B0 | 2 | [U] differential (expect 100 if armed) |
| 9 | band0 MAC block | 0x40110500/51C/520/548/54C/5BC | 4 | reset done, 0x0101 class |
| 10 | band1 MAC block | 0x40114500/51C/520/548/54C/5AC/5BC | 4 | reset done, 0x6101 class |

Firmware RAM (BAR0 offset = CA + 0x6B8000, phys = 0x40000000 + BAR0):

| # | object | CA | width | note |
| ---: | --- | ---: | ---: | --- |
| 11 | band dispatch word | 0x180814 | 4 | +8 band0 SMAC ptr, +0x1C band1 SMAC ptr |
| 12 | G head / G+0x370 / G+0x374 | 0x1747C4 / +0x370 / +0x374 | 4 | expect 0 hover NULL / 0x57639 |
| 13 | per-band SMAC object +672 | two-step | 4 | pointer to the per-band control block |
| 14 | per-band SMAC object +700 / +740 | two-step | 4 | HAL / error objects |
| 15 | [obj+672]+228 / +232 | two-step | 4 | mask/ack / status word |
| 16 | perlink beacon state | via bandobj+0x1A00 | - | +0x6B8/6BC/6C0/6C4/6C6 |
| 17 | both 92-B beacon descriptors | two-step | 92 | perlink+0x6C0 |

Two-step rule: follow a pointer only if it is in firmware RAM
(0x00040000..0x00180000) or in device IO (0x40000000..0x4011FFFF). Never write
any probe; never read the RC misc window.

## T8. Negative results [V]

* No g0-id1 key in table A (217 rows) or table B (88 rows) maps to a
  beacon/TBTT/ISR worker, the beacon hook 0x57638, the band start 0x971D8 or
  the MAC start 0x4AF0C.
* The 25 valid g0-id1 stock-capture keys contain no 0x0242/0x0243 and none
  resolves to a SMAC ISR/common body.
* CA 0x4AF0C and CA 0x971D8 contain no reference to 0x401015CC / 0x400024B0.
* The beacon hook 0x57638 contains no device IO literal (list teardown only).
* The firmware INTC (CA 0x4016xxxx) is outside the host region-3 window.

## T9. Reproduction

| mode | output |
| --- | --- |
| `--selftest` | 238 assertions over every table/constant/negative above; exit 0 PASS |
| `--decode` | the host-message negative, start MMIO set, ISR table, timer candidates |
| `--tables` | the ISR registration key/IRQ/wrapper/common-body tables + timer words |
| `--probes` | the R6 pack with CA/BAR0/phys |
