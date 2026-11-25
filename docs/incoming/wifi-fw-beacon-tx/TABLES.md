# TABLES - lane wifi-fw-beacon-tx (task-192)

All firmware addresses are CPU addresses in the project's image-linear
convention: **CPU = file offset + 0x40000** of the pinned FIRMWARE.bin
(928,920 B, sha256 7fc87e20...311b). Host read paths:
**firmware RAM phys = 0x40000000 + CA + 0x6B8000**;
**device IO phys = 0x40000000 + 0x3B8000 + (CA - 0x40000000)**.
Everything here is re-derived by tools/fw_beacon_tx_re.py --selftest from the
local artifacts; no vendor byte is stored in this repository.

Grades: **[V]** re-derived by the committed tool; **[V\*]** measured by a
committed lane/Lead; **[I]** inference; **[U]** needs a device read.

---

## T1. Conventions and safety

| item | value |
| --- | --- |
| firmware CPU base | file + 0x40000 |
| firmware RAM BAR0 | CA + 0x6B8000 (phys 0x40000000 + that) |
| device IO BAR0 | 0x3B8000 + (CA - 0x40000000) (phys 0x40000000 + that) |
| forbidden reads | RC misc window 0x10160000..0x1016FFFF |
| forbidden writes | none at all in this lane; never CA 0x400392F0 |
| firmware identity | 928,920 B, sha256 7fc87e20...311b (checked by the tool) |

## T2. H2D g0-id1 keys on the beacon path (table A CA 0x10C23C) [V]

Handler CA 0x43D58: `ctx = 0xFE578(0, hdr[3])`, key = u16 hdr+0x14,
len = u8 hdr+0x16, body = hdr+0x18, worker = u32 table entry+4 (bit0 = Thumb).
The table has 217 rows of `{u16 key; u16 pad; u32 worker|thumb}`; a second
table B at CA 0x103BF8 has 88 rows and the same layout.

| key | row | worker CA | role |
| ---: | ---: | ---: | --- |
| 0x0064 | 1 | 0x449E4 | VAP add / ctx create: body[4] = module index created, body[0x12] = band -> binds ctx+0x4B0/+0x4B4/+0x4B8 |
| 0x0065 | 2 | 0x46394 | VAP start: creates the per-link beacon state, calls band start CA 0x971D8 |
| 0x0066 | 3 | 0x4455A | another VAP command [U] |
| 0x0067 | 4 | 0x45F74 | VAP stop/teardown (see T8) |
| 0x0124 | 108 | 0x841B8 | channel worker: per-link RF programme via CA 0x40684 |
| 0x0127 | 110 | 0x84DA4 | AP config (see T4) |
| 0x00F7 | 92 | 0x45A30 | descriptor-table download |
| 0x01F7 | 93 | 0xB5B9C | RF mode + R image (the F2 arm; ../wifi-fw-rf-descriptor) |

## T3. Field B = per-module context; band binding [V]

`ctx = 0xFE578(0, fieldB)`:

| element | value |
| --- | --- |
| bound | `cmp fieldB, #0x12` at CA 0xFE59C -> 19 contexts |
| slot address | `0x171458 + 4*fieldB` (`add.w r4,r4,#0x18C` + `ldr.w r0,[r0,r4,lsl #2]`, CA 0xFE5A8/0xFE5B0) |
| ctx | slot pointer + u16 `0x172504` bias (CA 0xFE5B4) |
| ctx bodies | boot init: `0x119758 + k*0xB40`, k = 0..18 (0x119758..0x126D18) |
| bench ctx0 | 0x119758 [V\*] (module-table slot0 = 0x00119758, bias 0) |

The **0x0064 VAP-add worker CA 0x449E4** binds the *created* module context
(from body[4]) to the band from **body[0x12]**:

| CA | instruction | meaning |
| ---: | --- | --- |
| 0x44B6C | `ldrb r1,[r7,#0x12]` | band selector 0/1 |
| 0x44B6E | `ldrb r0,[r4,#2]` | module argument |
| 0x44B70 | `bl 0xA4FC8` | band object = 0x15CA08 + 0x1B68*band |
| 0x44B76/7A | `add.w r1,r0,#0x1A00; ldr r3,[r1]` | published per-link must be nonzero |
| 0x44BBC | `str.w r3,[r4,#0x4B0]` | ctx+0x4B0 = per-link |
| 0x44BC2 | `str.w r0,[r4,#0x4B8]` | ctx+0x4B8 = band object |
| 0x44BC6 | `str.w r3,[r4,#0x4B4]` | ctx+0x4B4 = bank context |

| band | band object | note |
| ---: | ---: | --- |
| 0 (2.4 GHz) | 0x15CA08 | 0xA4FC8 literal CA 0xA4FF4 |
| 1 (5 GHz) | 0x15E570 | == the F2 bench P [V\*] |

Cross-reference: ../wifi-fw-dfs-worker section 3 (the same model, from the
0x01F7 fire's field-B bug).

## T4. AP config key 0x0127 (worker CA 0x84DA4) [V]

Body map; the firmware destinations are asserted at the listed CA.

| body off | width | destination | CA |
| ---: | ---: | --- | ---: |
| +0x00 | u32 | VAP+0x10 beacon interval | 0x84F4A (`str r3,[r2,#16]` after 0x84F38 load VAP) |
| +0x04 | u32 | VAP+0x14 DTIM | 0x84F3C |
| +0x08 | u8 | branches RSN/vendor path | |
| +0x09..+0x48 | 64 B | RSN IE copy (id 0x30) | 0x84F96 checks body[0x49].. |
| +0x49..+0x88 | 64 B | vendor IE copy (id 0xDD) | |
| +0x89 | u8 | ctx+0x18D.b0 hidden_ssid | 0x84DBC |
| +0x8A | u8 | VAP+0x34C.b3 HT cap bit5 | 0x84E52..0x84E60 |
| +0x8B | u8 | CA 0xFDDF8 HT cap bit6 | 0x84E64/0x84E68 |
| +0x8C | u8 | VAP+0x360.b2 VHT cap bit5 | 0x84E70/0x84E7A |
| +0x8D | u8 | VAP+0x360.b3 VHT cap bit6 | 0x84E86/0x84E8E |
| +0x8E | u8 | CA 0xD4048 wireless mode | 0x84EDE/0x84EE2 |
| +0x93 | u8 | no reader | start flag |
| +0x94 | u16 | VAP+0x13A MDID | 0x84E96/0x84E9E |
| +0x96 | u8 | VAP+0x138.b0 MD present | 0x84ECE |
| +0x97 | u8 | VAP+0x138.b1 MD cap b0 | 0x84EA6 |
| +0x98 | u8 | VAP+0x138.b2 MD cap b1 | 0x84EBA |

Verdict: **no band field and no beacon-enable field**; the only band-correlated
inputs are the HT/VHT capability bits (+0x8A..+0x8D) [V].

## T5. VAP start key 0x0065 (worker CA 0x46394) - per-link beacon state [V]

| CA | instruction | meaning |
| ---: | --- | --- |
| 0x463D4 | `ldr.w sl,[r4,#0x4B8]` | band object |
| 0x4641E | `add.w r5,sl,#0x1A00` | published per-link location |
| 0x46450 | `ldr.w r6,[r8,#0x4B0]` | per-link |
| 0x464F2 | `ldrb.w r3,[r6,#0x6C6]` | first-start guard (beacon enable) |
| 0x46588 | `strh.w r3,[r6,#0x6C4]` | = 16 |
| 0x46590 | `str.w r7,[r6,#0x6C0]` | 92 B descriptor buffer |
| 0x46596 | `str.w r9,[r6,#0x6B8]` | 280 B beacon buffer |
| 0x4659A | `strb.w r3,[r6,#0x6C6]` | = 1 (beacon state enabled) |
| 0x464A6 | `mov r0,r8; bl 0x5E95C` | VAP publish helper |
| 0x464B0 | `bl 0x971D8` | band start (r0 = bandobj, r1 = ctx) |

| per-link field | type | meaning |
| --- | --- | --- |
| +0x6B8 | ptr | 280 B buffer, module 0xCE2 alloc, filled from the VAP on the first start (role [I]: beacon buffer) |
| +0x6BC | ptr | aux object written from CA 0xEE394's result |
| +0x6C0 | ptr | 92 B buffer (0x5C alloc, [0]=0xE4, [+4..+15]=0xFF) (role [I]: descriptor) |
| +0x6C4 | u16 | 16 |
| +0x6C6 | u8 | 1 = beacon state exists; 0 = never created |

The pointer/field *existence and creation order* are [V]; the exact role names
(beacon buffer / descriptor) are [I] from the shape (per-VAP allocation on the
AP VAP-start path, freed only by the teardown CA 0x87024).
| +0x11D | u8 | band index used by CA 0x4AF0C (0/1) |
| +0xE20 | ptr | P (RF lane; -> band object at +0x1A00 when bound) |
| +0x1C8 | u8 | after the channel worker, nonzero -> CA 0x97008 call |

Teardown: CA 0x87024 frees +0x6BC, +0x6B8, +0x6C0 and clears +0x6C4/+0x6C6
[V].

## T6. Band start CA 0x971D8 and the runtime hooks [V/U]

| element | value | grade |
| --- | --- | --- |
| G table | CA 0x1747C4 (literal CA 0x9745C) | [V] |
| G+0x370 | MAC start hook, called with ctx | [V] call, [U] value |
| G+0x374 | beacon start hook, called with ctx | [V] call, [U] value |
| call sites | CA 0x9735C..0x97372 | [V] |
| G head gate | `if (*G != 0)` CA 0x9735C | [V] |

## T7. Per-band MAC blocks (device IO CA) [V]

| element | band0 | band1 |
| --- | ---: | ---: |
| register block base | 0x40110000 | 0x40114000 |
| init table CA | 0x105714 | 0x105770 |
| table literal (image) | CA 0x9F494 | CA 0x9F964 |
| apply loop | CA 0x9F3CA (base 0x40000000+0x110000) | CA 0x9F912 (base 0x40004000+0x110000) |
| reset/start register base | 0x40110500 | 0x40114500 |
| reset sequence | write 0x55 then 0xFF to base+0x500 | same |
| status reads | base+0x51C / +0x520 | same |
| SMAC IRQ enable | 0x4010C220 bits 1\|8\|0x20\|0x1000 | same |
| global IRQ enable | 0x40101220 = 2 | = 1 |
| enable-TRX regs | 0x4010820C / 0x40108214 | 0x4010E150 / 0x4010E154 |
| band-id register | 0x401105BC = band value | 0x401145BC = 0 |
| init table writes | 0x548/0x54C final 0x0101 | final 0x6101 (bit 0x6000) |
| mask entries | 0x588/0x594 = 0x33/0x22 | = 0x35/0x24 |
| band byte source | u8(perlink+0x11D) at CA 0x4AF5C | same code |

The 23-entry tables (CA 0x105714 / 0x105770) are dumped by
`--tables`; band1 differs in 10 pairs (indices 2,3,4,5,7,12,13,14,15,17).
The two enable-TRX paths (worker CA 0xE8520) are asserted at CA 0xE8548
(band branch), 0xE85E6/0xE85EA (band1 write 0x33 to 0x4010E150), and the
literals CA 0xE8634/38/3C/40/44/48/54/58/5C/60/64/68/6C.

## T8. Key 0x0067 = VAP stop/teardown, not a beacon enable [V]

Worker CA 0x45F74: resolves bandobj = ctx+0x4B8 and published per-link,
calls CA 0x5E95C(ctx) and CA 0x971D8(bandobj, ctx) on the stop path, calls
CA 0xA4C0E/0xA4CA4, CA 0xB23A4, CA 0x95B40, CA 0x4C3FC(bandobj, 2), releases
the VAP objects. There is no "beacon enable" write here; the beacon state is
removed by the teardown CA 0x87024 (T5). [V classification of the calls; [I]
name "VAP stop".]

## T9. Negative result: no separate "beacon enable" H2D key [V]

Table A and table B keys were enumerated; the beacon path keys are the ones in
T2. In particular there is no key whose worker is a beacon enable; 0x0067 is
VAP stop (T8), 0x0128 is an IE/symbol lookup worker, and the beacon transmit is
switched by the VAP start (T5) -> band start (T6) path. [V for the table scan
and the worker addresses; [U] for the WAL group-2 command space, which is a
different carrier (../wifi-vap-start / ../wifi-ap-start).]

## T10. ISR name blob and TBTT [V names / U entry]

| element | value |
| --- | --- |
| blob range | CA 0x12266C..0x122BA0 |
| names | 45, `*_5g_isr`/`*_2g_isr` pairs: coex rx abort end, rx complete, tx complete, common timer, coex pta rx abort timeout, coex tx abort end, coex pta tx abort timeout, msg_proc (set_mu_edca_timer, ac_suspend, cali self_cts, cali ppdu, rx_ring_reset, update_tx_bypass_ctrl, beacon_tx_resume, beacon_tx_suspend, clr_hw_fifo, tx_resume, tx_suspend, enable_trx, disable_trx), tbtt, rx_timeout, pre_tx, backoff_timeout, user_queue_empty, user_ps, tx_abort_start, lifetime_expire, tx_exit |
| smac_tbtt_5g_isr | CA 0x1229E8 |
| smac_tbtt_2g_isr | CA 0x1229FC |
| code pointer to the blob | none (LE32 scan over the image) |
| handler addresses | **[U]** |
| per-band "common timer" ISR | adjacent name -> the TBTT bucket is per band on the common-timer block [I] |

## T11. Read-only probe pack (summary; exact tool output in evidence/probes.txt)

Firmware RAM (BAR0 = CA + 0x6B8000, phys = 0x40000000 + BAR0):

| # | object | CA | expected |
| ---: | --- | ---: | --- |
| 1 | module-table slot0 | 0x171458 | 0x00119758 [V\*] |
| 2 | module-table bias u16 | 0x172504 | 0 (validated PD4 boot) |
| 3 | slot k ptr (k = 0..18) | 0x171458 + 4k | nonzero = context exists |
| 4 | ctx_k + 0x4B0 per-link | ctx_k = slot + bias | 0x00175A18 for k=0 [V\*] |
| 5 | ctx_k + 0x4B4 bank ctx | | nonzero when bound |
| 6 | ctx_k + 0x4B8 band object | | 0x15CA08 (band0) / 0x15E570 (band1) |
| 7 | ctx_k + 0x190 VAP | | nonzero after VAP add |
| 8 | band0/band1 published per-link | 0x15CA08/0x15E570 + 0x1A00 | nonzero |
| 9 | band_b perlink + 0x6B8/+0x6BC/+0x6C0 | | nonzero if beacon state exists |
| 10 | band_b perlink + 0x6C4 / +0x6C6 | | 16 / 1 |
| 11 | band_b perlink + 0x11D | | band index 0/1 |
| 12 | band_b perlink + 0xE20 P | | 0x15CA08/0x15E570 when bound |
| 13 | P + 0xF04 RF mode / +0xF87 flag | | 3 / 0 after 0x01F7 (F2) |
| 14 | VAP + 0x10/+0x14/+0x138/+0x13A | | 100/2/MD/MDID after 0x0127 |
| 15 | VAP + 0x34C / +0x360 | | HT/VHT hint flags |
| 16 | G head / G+0x370 / G+0x374 | 0x1747C4/0x174B34/0x174B38 | nonzero = armed |

Device IO (BAR0 = 0x3B8000 + (CA - 0x40000000), phys = 0x40000000 + BAR0):
band0 0x40110500/0x51C/0x520/0x548/0x54C/0x550/0x554/0x55C/0x560/0x564/0x568/
0x588/0x594/0x5BC and band1 0x40114500/.../0x5BC; 0x4010C200, 0x4010C220,
0x40101220, 0x4010E150, 0x4010E154, 0x4010820C, 0x40108214,
SoC INTC 0x40002264/0x40002268/0x4000226C. All read-only.

## T12. Reproduction

| mode | output |
| --- | --- |
| `--selftest` | 105 assertions over every window above; exit 0 PASS |
| `--decode` | the beacon chain with the asserted instructions |
| `--tables` | H2D rows, band objects, beacon state, both band init tables, ISR names |
| `--probes` | the RAM/IO probe pack with CA/BAR0/phys |
| `--frames` | H2D body specs (protocol spec only) and the band-1 order |