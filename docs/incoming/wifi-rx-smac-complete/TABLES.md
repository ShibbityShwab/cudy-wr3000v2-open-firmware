# TABLES - IRQ rows, the producer's instruction sequence, and host conversion

**Lane:** wifi-rx-smac-complete (task-221). CA = file offset + 0x40000 of `FIRMWARE.bin`
(928,920 B, sha256 `7fc87e20...c3ef311b`). Every row is asserted by
`tools/fw_rx_smac_complete_re.py --selftest` (197/197 PASS). Grades: [V] here, [V*] earlier lane.

## T1 - per-band SMAC interrupt registration (per-band init CA 0xD1B68)

| band0 IRQ | band1 IRQ | prio | wrapper band0 / band1 | common body | body's status test on `[[band_obj+0x2a0]+0xe8]` and work |
| ---: | ---: | ---: | --- | ---: | --- |
| 47 | 53 | 5 | 0xAEC94 / 0xAEC88 | 0xAEC74 | bit0: set -> return; else post RTOS event 1 (`0xC0550`) |
| **49** | **55** | 5 | **0xB54C8 / 0xB54BC** | **0xB53A4** | **bit2: set -> return; else the PRODUCER (g2 id3 + PPDU re-arm)** |
| 50 | 56 | 5 | 0xB0D50 / 0xB0D44 | 0xB0CE0 | status dispatch; disables own IRQ, 16-B D2H class |
| 51 | 57 | 5 | 0xAEC68 / 0xAEC5C | 0xAEB80 | bit7: set -> return; else 16-bit ring walk, 6-B D2H class |
| 52 | 58 | 5 | 0xAEB74 / 0xAEB68 | 0xAEA9C | bit8: set -> return; else counter + g2 id0x23 D2H class |
| 82 | 84 | 14 | 0xAE48C / 0xAE480 | 0xAE45C | mask-ack: `(word0[obj+0x2bc] & 0x01000200) & ~status` -> `[base+0xe4]` |
| 83 | 85 | 5 | 0xB0D50 / 0xB0D44 | 0xB0CE0 | (shared with 50/56) |
| 59 | 60 | 5 | 0xB0C44 / 0xB0C38 | 0xB0AB8 | error bits (0/1/2) class |

Registration plumbing [V]: `0xC6DA2(irq,prio,handler)` validates irq <= 0x5F and stores
`[0x17D398 + 0x98 + 4*irq] = handler` (registrar 0xC74B0); `0xC6FF4(irq)` validates 0x10..0x5F
and sets the W1S enable at `0x40161100 + (irq>>5)*4` (+0x100 mirror). The last band1 block
(IRQ 60) branches to the shared enable call at 0xD1F6A instead of falling through.

Wrappers: `ldr r3,=0x180814; ldr r0,[r3,#8] (band0) / [r3,#0x1c] (band1); b <body>`.
Band objects: band0 = 0x174BF0, band1 = 0x175A18 [V smac-start bench].

## T2 - producer body 0xB53A4 instruction-level sequence [V]

| CA | fact |
| ---: | --- |
| 0xB53A4 | `push.w {r0..r8,lr}` |
| 0xB53A8 / 0xB53AA | `ldr.w r3,[r0,#0x2a0]` - SMAC base pointer field |
| 0xB53AE / 0xB53B0 | `ldr.w r3,[r3,#0xe8]` - status word |
| 0xB53B2 / 0xB53B4 | `lsls r3,r3,#0x1d` (bit2 -> sign) / `bmi return` |
| 0xB53BE / 0xB53C4 | `ldrb/strb.w [r0,#0xcc0]` - entry counter (before dequeue) |
| 0xB53C8 | `bl 0xBFD4C` |
| 0xB53D0 / 0xB53D4 | `add.w r0,r4,#0x348` / `bl 0xB084C` - dequeue completion entry, class 3 |
| 0xB53F2 / 0xB53F4 / 0xB53F8 | `movs r2,#4` / `ldr [r4,#0x2a0]` / `str.w r2,[r3,#0xe4]` - SMAC ack |
| 0xB5402 | `bl 0xB0C88` - received-frame chain |
| 0xB5406 | `sub.w r7,r0,#8` |
| 0xB5410 / 0xB5414 | `addw r0,r4,#0x66c` / `bl 0xB2BD8` - unlink PPDU descriptor |
| 0xB5418..0xB5420 | ack 4 again |
| 0xB5424 / 0xB5426 | `movs r0,#0x14` / `bl 0x102822` - allocate the 20-byte message |
| 0xB5462 / 0xB5468 | `movs r2,#2` / `bfi r3,r2,#0,#4` - group nibble 2 |
| 0xB546E / 0xB5470 | `movs r3,#3` / `strb r3,[r0,#6]` - id 3 (hdr[7]=0) |
| 0xB5472/0xB547A | destination nibble = `u8[band_obj]` into hdr[1] high nibble |
| 0xB547C/0xB5480 | hdr[2] = u8[band_obj+2]; u8[band_obj] into the same dest nibble |
| 0xB5482/0xB5484 | hdr[3] = 0x12 |
| 0xB5488 / 0xB548A | hdr[0x14] = band object; hdr[0x13] = 1 |
| 0xB548C / 0xB548E | hdr[0xc] = frame chain; hdr[0x1d] = 0 |
| 0xB5492 | `bl 0x102760` - post the group-2/id-3 message |
| 0xB5458 | `bl 0xB52F0` - re-arm a fresh 0x198-B PPDU descriptor into `band_obj+0x320` |

## T3 - HCC header fields used by this lane [V]

| field | meaning | producer value | consumer read |
| --- | --- | --- | --- |
| `u8 hdr[0] & 0xf` | group | 2 | dispatcher 0xFD7E8 `0xFD806/0xFD80A` |
| `u8 hdr[1] >> 4` | destination nibble (hcc_msg_tx resolves it against 0x171B6C +0x448 + 0x18*dest) | u8[band_obj] | - |
| `u16 hdr[6]` | id (hdr[7] = 0) | 3 | dispatcher `0xFD808` -> group-2 table id 3 |
| `hdr[0xc]` | payload+0 | received-frame chain | 0xBBC9C `0xBBCD0` list plumbing |
| `hdr[0x14]` | payload+8 | band object | 0xBBC9C `0xBBCB8` |
| header fetch | `0x409EA` | - | both producer (0xB545E) and consumer (0xBBCB4) |

## T4 - host conversion (committed convention)

| window | formula | examples |
| --- | --- | --- |
| device IO (CA 0x40000000..0x4011FFFF) | phys = CA + **0x3B8000** | 0x400420e8 -> 0x403FA0E8; 0x40108214 -> 0x404C0214; 0x4010C220 -> 0x404C4220 |
| firmware RAM | phys = CA + **0x406B8000** | band0 obj 0x174BF0 -> 0x4082CBF0; band0 +0xcc0 -> 0x4082D8B0 |
| PKTRAM | phys = 0x40000000 + 0x1D8000 + (CA - 0x01000000) | PPDU nodes |

## T5 - the two new producer-side observables (see PROBES)

| item | CA (band0 / band1) | width | meaning |
| --- | --- | --- | --- |
| producer entry counter | 0x174BF0+0xCC0 = 0x1758B0 / 0x175A18+0xCC0 = 0x1766D8 | u8 | incremented after the bit2 gate, before the dequeue |
| SMAC ack word | [[band_obj+0x2a0]]+0xE4 | u32 | producer writes 4; body D 0x80; body E 0x100; body F mask&~status |
| SMAC status word | [[band_obj+0x2a0]]+0xE8 | u32 | body gates read their bit here (producer: bit2) |
| failure-path counter | band0 0x174BF0+0xCCE = 0x1758BE | u8 | message dequeue/alloc/post failure paths |
