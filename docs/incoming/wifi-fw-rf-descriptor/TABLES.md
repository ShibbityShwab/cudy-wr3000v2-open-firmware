# TABLES - lane wifi-fw-rf-descriptor (task-189)

All firmware addresses are CPU addresses in the project's image-linear
convention: **CPU = file offset + 0x40000** of the pinned FIRMWARE.bin
(928,920 B, sha256 7fc87e20...c3ef311b). Host read path:
**BAR0 = CPU + 0x6B8000**, **phys = 0x40000000 + BAR0**. Everything here is
re-derived by tools/fw_rf_descriptor_re.py --selftest from the local artifacts;
no vendor byte is stored in this repository.

Grades as in REPORT.md: **[V]** re-derived here, **[V*]** bench-measured by a
committed lane or the Lead, **[I]** inference, **[U]** needs a device read.

---

## T1. Conventions and safety

| item | value |
| --- | --- |
| firmware CPU base | file + 0x40000 |
| host physical (bar0read) | 0x40000000 + CPU + 0x6B8000 |
| forbidden reads | RC misc window 0x10160000..0x1016ffff |
| forbidden writes | none at all in this lane; never CA 0x400392f0 |
| firmware identity | 928,920 B, sha256 7fc87e20...311b (checked by the tool) |

## T2. The corrected object chain [V]

| element | instruction | address |
| --- | --- | --- |
| P load (0x0124 worker) | `ldr.w r1,[r7,#0xE20]` | CA 0x84228 (prologue: r7 = u32(ctx+0x4B0) at CA 0x841BC) |
| P load (0x0124 worker, sibling) | `ldr.w r9,[r7,#0xE20]` | CA 0x83CCC |
| CA 0x40684 call | `bl 0x40684` with r0=ctx, r1=P, r2=perlink | CA 0x8422E (also 0x83AEA, 0x420B4) |
| P load (inside 0x40684) | `ldr.w r4,[r5,#0xE20]` | CA 0x406BA |
| CA 0xA2FA2 call | `bl 0xa2fa2` with r0=P, r1=ctx | CA 0x406AA (also 0x617FA) |
| O address | `add.w r3,r0,#0x1A00` | CA 0xA2FA8 |
| O load | `ldr r6,[r3,#0]` | CA 0xA2FAC |
| O==0 log | `movw r1,#2999` | CA 0xA2FB6 |
| mode byte | `ldrb.w r2,[r0,#0xF04]` | CA 0xA2FC0 |
| mode must be 3 | `cmp r2,#3` / `bne 0xA30FC` | CA 0xA2FC4 (silent return) |
| R load | `ldr.w r4,[r6,#0x2D8]` | CA 0xA2FD4 (not null-checked) |
| R load (latch) | `ldr.w r2,[r0,#0x2D8]` | CA 0x89840 |
| R load (short image) | `ldr.w r4,[r0,#0x2D8]` | CA 0x89928 |
| pitch of per-bank contexts | CA 0xB1EE4: `movw r2,#0xE28` | CA 0xB1F20 |

Chain text: `perlink = u32(ctx+0x4B0); P = u32(perlink+0xE20);
O = u32(P+0x1A00); R = u32(O+0x2D8)`.

## T3. Who writes what

| object | writer | value | evidence |
| --- | --- | --- | --- |
| `bandobj+0x1A00` (= O when P==bandobj) | boot-task per-record body at CA 0x48A68..0x48A6C (`add.w r3,r4,#0x1A00`; `str.w sl,[r3]`) | CA 0xB1EE4(bank) per-bank context = the published per-link | [V]; bench `u32(0x15FF70)=0x00175A18` [V*] |
| `bandobj+0xD6C` (band/bank ids) | CA 0x4854A `strh.w r6,[r5,#0xD6C]` | band record index pair | [V] |
| `bandobj+0xD70` (attach flag) | CA 0x48A7E `strb.w r3,[r4,#0xD70]` | min(u8(perlink+1),2) | [V] |
| `bandobj+0xF04` (RF mode) | **CA 0xB5D1C** `strb.w r5,[r4,#0xF04]` (only writer) | body[0] of H2D key 0x01F7, 1..5 | [V] |
| `bandobj+0xF87` (branch flag) | CA 0xA30F8 `strb.w r3,[r5,#0xF87]` | 0 ch36-64 / 1 ch100+ | [V] |
| R block `R+0x3A8..+0x418` | CA 0xA2FA2 (0x0124) and CA 0xB5B9C (0x01F7) | per-channel / per-mode register images | [V] |
| `perlink+0xE20` (P) | **none in the image** | - | [V] negative |
| `perlink+0x2D8` (R pointer) | **none in the image** | - | [V] negative |

## T4. H2D g0-id1 key table A (CA 0x10C23C) - the relevant rows [V]

Entry layout: 8 bytes `{u16 key; u16 pad; u32 worker|thumb}`. Handler: CA 0x43D58
reads key = u16 msg+0x14 and dispatches; worker called as
`worker(r0, r1=len(msg+0x16), r2=body(msg+0x18))`.

| entry | CA | key | worker | role |
| ---: | ---: | ---: | ---: | --- |
| 92 | 0x10C51C | 0x00F7 | 0x45A30 | descriptor table (fw-desc-table lane) |
| **93** | **0x10C524** | **0x01F7** | **0xB5B9C** | **RF mode + register image (this lane)** |
| 94 | 0x10C52C | 0x07E7 | 0xB58FC | CAC state -> MAC regs 0x4010E0xx |
| 95 | 0x10C534 | 0x0136 | 0xB5F3C | [U] |
| 96 | 0x10C53C | 0x07E8 | 0x429B0 | [U] |

Cross-check: `../wifi-datapath/tables/firmware-config-keys.md` row 93 lists
key 0x01F7 -> worker 0x0B5B9D (thumb), independently derived there.

## T5. CA 0xB5B9C - the RF mode / register-image worker [V]

| CA | instruction | meaning |
| ---: | --- | --- |
| 0xB5B9C | `push {r0,r1,r4,r5,r6,lr}` | worker entry (r0=ctx, r2=body) |
| 0xB5B9E | `ldr.w r4,[r0,#0x4B8]` | r4 = band object |
| 0xB5BA6 | `add.w r6,r4,#0x1A00` | publish word |
| 0xB5BAA | `ldr r0,[r6,#0]` | published per-link; 0 -> log 1442 |
| 0xB5BC8 | `ldrb.w r1,[r4,#0xD70]` | band attach flag; must be 1 else log 1448 |
| 0xB5BE2 | `ldrb r5,[r2,#0]` | mode = body[0] |
| 0xB5BE8 | `ldr.w r3,[r0,#0x2D8]` | R = *(perlink+0x2D8) |
| 0xB5BF0 | `str.w r2,[r3,#0x3A8]` | RF image word 0x3A8 |
| 0xB5D6A..0xB5D72 | `subs r3,r5,#1` / `cmp r3,#4` / `bhi` / `tbb [pc,r3]` | mode 1..5 selects the image |
| **0xB5D1C** | **`strb.w r5,[r4,#0xF04]`** | **the only write to the mode byte** |
| 0xB5C90 | `str.w r2,[r3,#0x418]` | last RF image word |

## T6. The RF programme CA 0xA2FA2 and its R image [V]

Object preconditions: P != 0; ctx != 0; u8(P+0xF04) == 3; R = u32(O+0x2D8)
(no NULL check). Then per channel range:

| word | ch 36..64 (CA 0x89924) | ch 100..196 (inline) |
| ---: | ---: | ---: |
| +0x3A8 | 0x01800060 | 0x01800060 |
| +0x3AC | 0x03E6138A | 0x056B0597 |
| +0x3B4 | 0x026F138A | 0x0F9B0FA5 |
| +0x3CC | top byte 0x16 | top byte 0x1D |
| +0x3D8 | 0xC14580B0 | 0xC08E141A |
| +0x410 | bit0 = CA 0x101890 predicate | same |
| P+0xF87 | 0 | 1 |

Full image table: ../wifi-fw-rf-programme/TABLES.md T5 (unchanged, only the
object addresses are corrected here).

## T7. Host-side emitters (hi5622v100_wifi.ko, read in place) [V]

| key | host function | evidence |
| ---: | --- | --- |
| 0x01F7 | `hmac_config_set_country_for_dfs` (.text+0x5762C) -> `hmac_config_send_event` at +0x57644 with `movw r1,#0x01F7` at +0x5763C | exactly 2 `MOVW r1,#0x01F7` words in .text; the other is at .text+0xC3254 (inside a WAL command handler); WAL command name `set_country_for_dfs`; thunk `wal_config_set_country_for_dfs` .text+0x12AE10, ops slot .data+0x890C |
| 0x07E7 | `hmac_config_cac_sync_state` (.text+0xAD1D4), `hmac_dfs_cac_start` (+0xAD420), `hmac_dfs_cac_stop` (+0xACD28), `hmac_dfs_cac_timeout_fn` (+0xAE5xx), `hmac_save_cali_data_to_file` (+0xC3E30) | 6 `MOVW r1,#0x07E7` words; 1-byte CAC state body; firmware worker CA 0xB58FC touches MAC regs 0x4010E000.. |

Frame: g0-id1 config event - 12 B header, 8 B prologue (msg id 1, tag 0x5A5A),
u16 key at hdr+20, u16 len at hdr+22, body at hdr+24, plus the 4-byte inherited
tail our driver family appends. For key 0x01F7: `len = 1`, `body[0] = mode`.

## T8. Corrected read-only probe pack (CPU -> BAR0 -> phys) [V]

Convention: BAR0 = CPU + 0x6B8000; bar0read arg = 0x40000000 + BAR0.

| # | object | CPU | BAR0 | phys | width | expected / note |
| ---: | --- | ---: | ---: | ---: | ---: | --- |
| 1 | module-table slot0 | 0x171458 | 0x829458 | 0x40829458 | 4 | 0x00119758 |
| 2 | module-table base u16 | 0x172504 | 0x82A504 | 0x4082A504 | 4 | 0 -> ctx0 = slot0+base |
| 3 | ctx0 | 0x119758 | 0x7D1758 | 0x407D1758 | - | computed (G6a) |
| 4 | u32(ctx0+0x4B0) perlink | 0x119C08 | 0x7D1C08 | 0x407D1C08 | 4 | 0x00175A18 |
| 5 | u32(ctx0+0x4B8) bandobj | 0x119C10 | 0x7D1C10 | 0x407D1C10 | 4 | 0x0015E570 |
| 6 | **u32(perlink+0xE20) P** | **0x176838** | **0x82E838** | **0x4082E838** | 4 | **expect 0x0015E570; 0 = chain broken** |
| 7 | u32(P+0x1A00) O | 0x15FF70 | 0x817F70 | 0x40817F70 | 4 | 0x00175A18 (published per-link) |
| 8 | **u8(P+0xF04) mode** | **0x15F474** | **0x817474** | **0x40817474** | 1 | **3 after 0x01F7; 0 = never armed** |
| 9 | u8(P+0xF87) branch flag | 0x15F4F7 | 0x8174F7 | 0x408174F7 | 1 | 0 ch36-64 / 1 ch100+ |
| 10 | u8(P+0xD70) attach flag | 0x15F3E0 | 0x8172E0 | 0x408172E0 | 1 | 1 |
| 11 | u16(P+0xD6C) band ids | 0x15F2DC | 0x8172DC | 0x408172DC | 2 | reference |
| 12 | u32(perlink+0x2D8) R | 0x175CF0 | 0x82DCF0 | 0x4082DCF0 | 4 | nonzero block pointer |
| 13 | u32(R+0x3A8) | two-step | - | - | 4 | 0x01800060 |
| 14 | u32(R+0x3AC) | two-step | - | - | 4 | 0x03E6138A (ch36-64) / 0x056B0597 (ch100+) |
| 15 | u32(R+0x3B4) | two-step | - | - | 4 | 0x026F138A / 0x0F9B0FA5 |
| 16 | u32(R+0x410) | two-step | - | - | 4 | bit0 = CA 0x101890 predicate |
| 17 | u32(R+0x418) | two-step | - | - | 4 | 0x003C0513 |

**What the committed rf-programme probe pack got wrong:** it read
`perlink+0x2820` (0x178238) and `perlink+0x1F04`-relative addresses as if
`+0xE20` were an inline struct. Row 6 above (`perlink+0xE20`) is the real
P; rows 7-8 are addressed through it.

## T9. Negative writer census (asserted by --selftest) [V]

| access form | count |
| --- | ---: |
| Thumb-2 T4 `ldr` imm 0xE20 | 50 (the only accesses to +0xE20) |
| Thumb-2 T4 `str` imm 0xE20 | 0 |
| Thumb-2 `strb/strh` imm 0xE20 | 0 |
| ARM `ldr`/`str` imm 0xE20 | 0 |
| `add/movw/str` immediate 0xE20 in the disassembly | 0 |
| LE32 literals 0x00000E20 / 0x00002820 | 0 / 0 |
| stores in the +0x2D8 family | 1 (CA 0xF39E8, stores into R+0x2D8 itself) |

## T10. Reproduction

```
./tools/fw_rf_descriptor_re.py --selftest   # 62/62 PASS, exit 0
./tools/fw_rf_descriptor_re.py --decode
./tools/fw_rf_descriptor_re.py --probes
./tools/fw_rf_descriptor_re.py --frame
```
Evidence snapshots: evidence/selftest.txt, decode.txt, probes.txt, frame.txt.
