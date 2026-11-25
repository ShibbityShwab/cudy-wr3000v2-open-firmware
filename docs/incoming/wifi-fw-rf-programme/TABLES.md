# TABLES - lane wifi-fw-rf-programme (task-182)

All firmware addresses are CPU addresses in the project's image-linear
convention: **CPU = file offset + 0x40000** of the pinned FIRMWARE.bin
(928,920 B, sha256 7fc87e20...c3ef311b). The image is loaded contiguously to
device CA 0x01240000 (BAR0+0x6f8000), which is the same space:
**BAR0 offset = CPU + 0x6B8000**, and the host read path uses
**phys = 0x40000000 + BAR0 offset** (BAR0 base 0x40000000, 24 MB window, per
the committed bringup decode). Everything here is re-derived by
tools/fw_rf_programme_re.py --selftest from the local image + the lab objdump;
no vendor byte is stored in this repository.

Grades as in REPORT.md: **[V]** re-derived here, **[V*]** bench-measured by a
committed lane or the Lead, **[I]** inference, **[U]** needs a device read.

---

## T1. Conventions and safety

| item | value |
| --- | --- |
| firmware CPU base | file + 0x40000 |
| BAR0 offset | CPU + 0x6B8000 |
| host physical (bar0read) | 0x40000000 + BAR0 offset = CPU + 0x46B8000 |
| BAR0 window | base 0x40000000, 24 MB (limit CPU ~0x1148000) |
| forbidden reads | RC misc window 0x10160000..0x1016ffff (bar0read refuses) |
| forbidden writes | none at all in this lane; never CA 0x400392f0 |

## T2. The corrected call chain - "the 5 GHz RF programme" [V]

| element | address | facts |
| --- | --- | --- |
| 0x0124 worker | CA 0x841B8 | stores ctx, then the post-store calls (fw-chan-worker) |
| 5 GHz-only branch | CA **0x40684** | if ctx[4]!=2 return; if CA 0x944DE(ctx)!=1 return; then 0x101890 -> 0xA2FA2 -> 0x89840 |
| the 3 gate bytes | CA 0x944DE | band==1 (ctx+0x11), ctx+0x440==1, bandobj+0xEC8==1 |
| **predicate** | CA **0x101890**(band, field, channel) | band-1 only; returns 0/1 from a descriptor bit; **no register writes** |
| channel validator | CA 0x10172C | band-0 table CA 0x117B48 (14 ch), band-1 CA 0x117AD4 (29 ch) |
| param matrix | CA **0x10BE68** | 15 field-columns x 29 channel indices, u32 entries, top byte = descriptor slot |
| slot->record mapper | CA **0x101650** | record = CA 0x1ADC98 + slot*16 + 8 (runtime pool) |
| **RF register image** | CA **0xA2FA2**(perlink+0xE20, ctx) | writes R+0x3A8..R+0x418 per channel range (T5) |
| ch36-64 image builder | CA **0x89924**(O) | the other channel-range variant (T5) |
| latch | CA **0x89840**(perlink, r6) | R+0x410 bit0 = r6 (read-modify-write, no log) |
| chip-type query | CA 0x40D60 | reads [0x40000280+0x28]: 2->1, {5,0x34,0x85}->2, else 3 |
| caller sites of CA 0x40684 | CA 0x420B4 (key 0x0052 worker), 0x83AEA, 0x8422E (the 0x0124 worker) | three callers |
| callers of CA 0xA2FA2 | CA 0x406AA, 0x617FA | 0x617xx is a sibling 5 GHz RF site (same gate+predicate+image sequence) |
| callers of CA 0x101890 | 0x406A0, 0x4AEBE, 0x5156C, 0x5FB42, 0x60A2C, 0x61810, 0x61DA8, 0x65292, 0xF1510 | seven sibling per-link RF sites + the 0xF15xx hal site |

## T3. CA 0x101890 - the band-1 predicate [V]

Signature: u32 f(u32 band, u32 field, u32 channel).

| step | address | facts |
| --- | --- | --- |
| band gate | 0x101894 | cmp r0,#1; !=1 -> return 0 |
| validate | 0x1018AC | CA 0x10172C(band, channel, &idx) |
| field split | 0x1018B6 | field in [0,6] -> 0x101AC6; field==7 -> default; field in [8,14] -> tbh at 0x1018D0; else -> log id 808 and return 0 |
| table base literal | 0x101BA0 | 0x0010BE68 |
| loop | 0x10191A..0x10193C | for i<count: slot = byte3(word_i); d = CA 0x101650(1, slot); if (d && (byte[d+2]&0x80)) return 1 |
| success | 0x101BD8 | movs r0,#1 |
| no write | - | the function touches no memory outside the stack |

Field windows (base = word offset into CA 0x10BE68 for channel index idx;
slot of word at base+4k is idx+k+(base-0x150)/4):

| field | base word | count | slots |
| ---: | ---: | ---: | --- |
| 0 | +0x150 (CA 0x10BFB8) | 1 | idx |
| 1 | +0x150 | 2 | idx, idx+1 |
| 2 | +0x14C | 2 | idx-1, idx |
| 3 | +0x148 | 4 | idx-2 .. idx+1 |
| 4 | +0x148 | 4 | idx-2 .. idx+1 |
| 5 | +0x14C | 4 | idx-1 .. idx+2 |
| 6 | [U] | [U] | [U] |
| 7 | +0x150 | 8 | idx .. idx+7 |
| 8 | +0x140 | 8 | idx-4 .. idx+3 |
| 9 | +0x148 | 8 | idx-2 .. idx+5 |
| 10 | +0x14C | 8 | idx-1 .. idx+6 |
| 11 | +0x138 | 8 | idx-6 .. idx+1 |
| 12 | +0x13C | 8 | idx-5 .. idx+2 |
| 13 | +0x144 | 8 | idx-3 .. idx+4 |
| 14 | +0x134 | 8 | idx-7 .. idx |

Our AP is field 0 (the 0x0124 body byte 1 is the vendor chip id, 0), so:

~~~
word = u32(CA 0x10BFB8 + 4*idx)      top byte t (= idx, verified 0..28)
row  = u32(CA 0x117AD4 + 4*t)        byte1 = slot; 0xFF = no record -> result 0
rec  = CA 0x1ADC98 + slot*16 + 8     only when slot != 0xFF
result = (u32(rec) >> 23) & 1        bit 7 of rec+2
~~~

**Static fact [V]:** both channel tables in the shipped image are
{channel, 0xFF, 0x00, 0x00} for every row (band-1 CA 0x117AD4, band-0
CA 0x117B48), so the mapper returns NULL for all 43 channels and CA 0x101890
is statically 0. The pool CA 0x1ADC98 is outside the image (ends CA 0x122CD8)
so the subsystem is runtime-built; probe 20 reads the row to decide whether
the runtime tables were populated.

## T4. CA 0x10BE68 field-0 word and the mapper slot per band-1 channel [V]

The field-0 word's top byte is the mapper input t (= idx, verified for all 29).
The mapper's channel-table row for t has byte1 = slot; in the shipped image
that byte is **0xFF for every channel** (last column), i.e. no record.

| idx | ch | word (CA 0x10BFB8+4*idx) | t | slot (shipped) |
| ---: | ---: | ---: | ---: | --- |
| 0 | 36 | 0x0024143C | 0 | 0xFF (row ch 36) |
| 1 | 40 | 0x01281450 | 1 | 0xFF (row ch 40) |
| 2 | 44 | 0x022C1464 | 2 | 0xFF (row ch 44) |
| 3 | 48 | 0x03301478 | 3 | 0xFF (row ch 48) |
| 4 | 52 | 0x0434148C | 4 | 0xFF (row ch 52) |
| 5 | 56 | 0x053814A0 | 5 | 0xFF (row ch 56) |
| 6 | 60 | 0x063C14B4 | 6 | 0xFF (row ch 60) |
| 7 | 64 | 0x074014C8 | 7 | 0xFF (row ch 64) |
| 8 | 100 | 0x0864157C | 8 | 0xFF (row ch 100) |
| 9 | 104 | 0x09681590 | 9 | 0xFF (row ch 104) |
| 10 | 108 | 0x0A6C15A4 | 10 | 0xFF (row ch 108) |
| 11 | 112 | 0x0B7015B8 | 11 | 0xFF (row ch 112) |
| 12 | 116 | 0x0C7415CC | 12 | 0xFF (row ch 116) |
| 13 | 120 | 0x0D7815E0 | 13 | 0xFF (row ch 120) |
| 14 | 124 | 0x0E7C15F4 | 14 | 0xFF (row ch 124) |
| 15 | 128 | 0x0F801608 | 15 | 0xFF (row ch 128) |
| 16 | 132 | 0x1084161C | 16 | 0xFF (row ch 132) |
| 17 | 136 | 0x11881630 | 17 | 0xFF (row ch 136) |
| 18 | 140 | 0x128C1644 | 18 | 0xFF (row ch 140) |
| 19 | 144 | 0x13901658 | 19 | 0xFF (row ch 144) |
| 20 | 149 | 0x14951671 | 20 | 0xFF (row ch 149) |
| 21 | 153 | 0x15991685 | 21 | 0xFF (row ch 153) |
| 22 | 157 | 0x169D1699 | 22 | 0xFF (row ch 157) |
| 23 | 161 | 0x17A116AD | 23 | 0xFF (row ch 161) |
| 24 | 165 | 0x18A516C1 | 24 | 0xFF (row ch 165) |
| 25 | 184 | 0x19B81338 | 25 | 0xFF (row ch 184) |
| 26 | 188 | 0x1ABC134C | 26 | 0xFF (row ch 188) |
| 27 | 192 | 0x1BC01360 | 27 | 0xFF (row ch 192) |
| 28 | 196 | 0x1CC41374 | 28 | 0xFF (row ch 196) |

## T5. The RF register block and its image [V]

Object chain (read-only): perlink = *(ctx+0x4B0); P = perlink+0xE20;
O = *(P+0x1A00); R = *(O+0x2D8). Pre-conditions: *(P+0xF04) u8 == 3, R != 0.

| word | ch 36..64 (CA 0x89924) | ch 100..196 (CA 0xA2FA2 inline) | write kind |
| ---: | ---: | ---: | --- |
| +0x3A8 | 0x01800060 | 0x01800060 | str |
| +0x3AC | 0x03E6138A | 0x056B0597 | str |
| +0x3B0 | bits4-7=3, bits8-11=3, bits0-3=4/5 (note a) | bits4-7=mode, bits8-11=mode, bits0-3=4/5 (note a) | bfi |
| +0x3B4 | 0x026F138A | 0x0F9B0FA5 | str |
| +0x3B8 | 0x00E800E8 | 0x009400E8 | str |
| +0x3BC | 0x00C601F6 | 0x00C601F6 | str |
| +0x3C0 | 0x00C601F6 | 0x014A014F | str |
| +0x3C4 | 0x1FFF1FFF | 0x1FFF1FFF | str |
| +0x3C8 | 0x1C29614B | 0x1C29614B | str |
| +0x3CC | bits0-7=0xBE, bits24-31=0x16 | bits0-7=0xBE, bits24-31=0x1D | bfi |
| +0x3D0 | 0x005200B2 | 0x005200B2 | str |
| +0x3D4 | 0x164021FF | 0x164021FF | str |
| +0x3D8 | 0xC14580B0 | 0xC08E141A (note b) | str/bfi |
| +0x3DC | 0x80000614 | 0x80000614 | str |
| +0x3E0 | 0x03FF1AA8 | 0x03FF1AA8 | str |
| +0x3E4 | 0x00000052 | 0x00000052 | str |
| +0x3EC | 0x003C0050 | 0x003C0050 | str |
| +0x3F0 | 0x1001F405 | 0x1001F405 | str |
| +0x3F4 | 0x05B57409 | 0x05B57409 | str |
| +0x3F8 | 0 | 0 | str |
| +0x3FC | 0x18200003 | 0x18200003 | str |
| +0x400 | 0x00507D0B | 0x00507D0B | str |
| +0x404 | 0x00F80064 | 0x00F80064 | str |
| +0x408 | 0x0300000F | 0x0300000F | str |
| +0x40C | 0x03790000 | 0x03790000 | str |
| +0x410 | bit0 = CA 0x101890 predicate (CA 0x89840) | same | bfi |
| +0x414 | bits0-1 = 1 | bits0-1 = 1 | bfi |
| +0x418 | 0x003C0513 | 0x003C0513 | str |
| P+0xF87 | 0 | 1 | strb |

note a: the low nibble is 4 when CA 0x40D60()==3 and [O+0x12E]==0, else 5.
note b: if CA 0x40D60()!=1 and [O+0x12E]==1, bits22-27 of +0x3D8 are set to 1
first, so the readback may be 0xC0CE141A.
bfi writes leave untouched bits as they were: compare masked fields, not the
whole word, when the block was previously programmed for another channel.

## T6. The three helpers - no RF writes [V]

| helper | behaviour |
| --- | --- |
| CA 0x97008(ctx) | link ctx CA 0xA4CA4; CA 0x5FA6C / CA 0x5FB18 / CA 0xB819C; reconciles modules sharing the band object (CA 0x961E0); requires ctx[4] in {1,2}; per-link+0x1C8 gate |
| CA 0xFDCE4(ctx) | band-class: band0 field1-2 -> 1; band1 field1-6 -> 2, field7-14 -> 6, else 0; writes VAP+0x1A0 bits1-7 |
| CA 0xF4FD8(obj=ctx+0x4AC) | type 1/2 -> CA 0xF4F48 lookup then CA 0xD98FC/0xD98C8/0xD9894 bookkeeping; other types log |

## T7. Read-only probe pack (the Lead's rung) [V*] expectations from G6a

Convention: CPU / BAR0 = CPU+0x6B8000 / phys = 0x40000000+BAR0. One-step reads
(1-9 are the existing lane's pack, re-listed so the rung is one pass):

| # | object | CPU | BAR0 | phys | len | expected |
| ---: | --- | ---: | ---: | ---: | ---: | --- |
| 1 | ctx slot0 (module table) | 0x171458 | 0x829458 | 0x40829458 | 4 | 0x00119758 (context array base) |
| 2 | module-table base u16 | 0x172504 | 0x82A504 | 0x4082A504 | 4 | 0 -> ctx0 = slot0+base |
| 3 | ctx0+0x004 create type | 0x11975C | 0x7D175C | 0x407D175C | 4 | 2 (AP) |
| 4 | ctx0+0x440 gate | 0x119B98 | 0x7D1B98 | 0x407D1B98 | 4 | 1 |
| 5 | bandobj+0xEC8 gate | 0x15F438 | 0x817438 | 0x40817438 | 4 | 1 |
| 6 | ctx0+0x4B0 perlink ptr | 0x119C08 | 0x7D1C08 | 0x407D1C08 | 4 | 0x00175A18 (band-1 bank) |
| 7 | ctx0+0x4B8 bandobj ptr | 0x119C10 | 0x7D1C10 | 0x407D1C10 | 4 | 0x0015E570 (band-1 object) |
| 8 | bandobj+0x1A00 publish | 0x15FF70 | 0x817F70 | 0x40817F70 | 4 | == read 6 (band-1 chain consistent) |
| 9 | perlink+0x1C8 (0x97008 flag) | 0x175BE0 | 0x82DBE0 | 0x4082DBE0 | 4 | non-zero |
| 10 | P+0x1A00 = perlink+0x2820 (O) | 0x178238 | 0x830238 | 0x40830238 | 4 | non-zero pointer |
| 11 | P+0xF04 RF mode (byte) | 0x17773C | 0x82F73C | 0x4082F73C | 4 | byte 3 |
| 12 | P+0xF87 branch flag (byte) | 0x1777BF | 0x82F7BF | 0x4082F7BF | 4 | 0 on ch36-64, 1 on ch100+ |
| 13 | O+0x2D8 = R | (O+0x2D8) | - | - | 4 | non-zero pointer |
| 14 | R+0x3A8 | (R+0x3A8) | - | - | 4 | 0x01800060 |
| 15 | R+0x3AC | (R+0x3AC) | - | - | 4 | 0x03E6138A (ch36-64) / 0x056B0597 (ch100+) |
| 16 | R+0x3B4 | (R+0x3B4) | - | - | 4 | 0x026F138A / 0x0F9B0FA5 |
| 17 | R+0x3CC | (R+0x3CC) | - | - | 4 | top byte 0x16 (ch36-64) / 0x1D (ch100+) |
| 18 | R+0x410 | (R+0x410) | - | - | 4 | bit0 = the predicate; read 20 first to decide if it is meaningful |
| 19 | R+0x414 | (R+0x414) | - | - | 4 | bits0-1 = 1 |
| 20 | channel-table row for the current channel | 0x117AD4 + 4*idx | 0x872AD4 + 4*idx | 0x40872AD4 + 4*idx | 4 | byte1 = slot; **0xFF in the shipped image** -> descriptor subsystem inactive, bit0 not meaningful |
| 21 | pool record (only if 20 slot != 0xFF) | 0x1ADC98 + 16*slot + 8 | +0x6B8000 | +0x40000000 | 4 | u32 & 0x00800000 (bit 7 of byte+2) = 0x00800000 |

Rows 20/21 replaced the pre-CW-3 descriptor probe: the shipped channel tables
are all-0xFF, so a slot of 0xFF means "no descriptor for this channel", not a
failure of the RF programme.

Two-step path notes: for a non-G6a boot resolve ctx0 first (reads 1+2), then
perlink/band object (reads 6+7), then O/R. Every address above is a READ;
none may be written.

## T8. Differential band-object dump (the C5 discriminator) [V*/I]

The 2.4 GHz beacon works; the 5 GHz one does not. Read both band objects over
the same offsets and compare - the working band is the reference.

| object | CPU (G6a) | BAR0 | phys |
| --- | ---: | ---: | ---: |
| band-0 object | 0x15CA08 | 0x812A08 | 0x40812A08 |
| band-1 object | 0x15E570 | 0x816570 | 0x40816570 |
| band-0 publish word (+0x1A00) | 0x15E408 | 0x816408 | 0x40816408 |
| band-1 publish word (+0x1A00) | 0x15FF70 | 0x817F70 | 0x40817F70 |
| VAP object (ctx0+0x190) | 0x1198E8 | 0x7D18E8 | 0x407D18E8 |

Reads: bandobj+0x000..+0x2D8 in 4-byte steps for both objects (0xB6 words
each; the helper takes at most 64 addresses, so three calls per object), plus
VAP+0x138 (u32: low half = DTIM/flag bits, high half = beacon interval at
+0x13A) and VAP+0x058 (byte, bit2 = the band-1 VAP flag).
Expected: VAP+0x13A != 0 (vendor default 100 TU) and VAP+0x58 bit2 == 1 for
the band-1 VAP. A zero beacon interval is a C5-class finding.

## T9. Decision tree (from the probe pack)

~~~
read 1,2 -> ctx0 ; read 3,4,5 -> gates (expect 2,1,1)
  gates not as expected -> the CW-2 pack's verdict stands, stop
read 6,7,8 -> perlink, band object, publish-word consistency
  ctx+0x4B0 != publish word -> wrong band object bound: report, do not patch
read 10,11,13 -> O, mode byte, R
  O == 0 or R == 0          -> C1 (the band-1 RF object chain absent)
  [P+0xF04] != 3            -> the band-1 PHY init never ran: different blocker
read 15 -> R+0x3AC
  == 0                      -> C1 (RF image never written)
  == 0x03E6138A (ch36-64)   -> image written; go to read 18
  == 0x056B0597 on ch36-64  -> C3 (wrong/stale variant)
read 20 -> channel-table row for idx: byte1 = slot
  slot == 0xFF              -> descriptor subsystem inactive: R+0x410 bit0=0
                               is BY DESIGN, ignore it -> beacon engine (C4/C5)
  slot != 0xFF              -> read 21 (pool record bit7) and 18 (bit0):
                               bit0 == 0 -> C2 (live table, predicate failed)
read 18 -> R+0x410 bit0
  == 1 (or slot == 0xFF)    -> the RF programme completed -> beacon engine:
                               read 12, T8 band diff, VAP+0x13A
                               -> C4 (beacon policy / ini) or C5 (TBTT arm)
~~~

## T10. Beacon-engine address set [V] (handler mapping is [U])

| element | address | facts |
| --- | --- | --- |
| VAP start worker (H2D 0x0065) | CA 0x46394 | body[2]->ctx+0x11, body[3]->ctx+0x12, u32 body[4]->ctx+0x444; ctx+0x0E=1 (VAP enabled); VAP+0x58 bit2 = band-1 flag; CA 0x971D8(band obj, ctx) |
| VAP start on band | CA 0x971D8 | resolves the band/link objects (CA 0xA4CA4), reconciles the modules on the band, calls global callbacks G+0x370 / G+0x374 with the module (sites CA 0x97360 / 0x97366) |
| AP config worker (H2D 0x0127) | CA 0x84DA4 | VAP+0x13A = u16 body+0x94 (beacon interval), VAP+0x138 bits0/1/2, VAP+0x64 flags, VAP+0x34C, VAP+0x360; CA 0x94728(ctx+0xACC), CA 0xFDDF8 |
| beacon param copy | CA 0x7E5CC | VAP+0x138 -> desc+0x25E, VAP+0x13A -> desc+0x260, VAP+0xF8 -> desc+0x262, plus EDCA/HT fields |
| band-class helper | CA 0xFDCE4 | VAP+0x1A0 bits1-7 (see T6) |
| ISR name blob | CA 0x12266C..0x122BA4 | 45 names; smac_tbtt_5g_isr @ 0x1229E8, smac_tbtt_2g_isr @ 0x1229FC, smac_msg_proc_beacon_tx_resume_req @ 0x122904, _suspend_req @ 0x122928; no code xref -> name-only debug blob |
| per-band ini | cfg_hi5622v100_hisi.ini | beacon_2g_tx_policy=0, beacon_5g_tx_policy=2; parser module hi5622v100_plat.ko |

Relevant H2D key rows of table A (CA 0x10C23C, 223 entries; worker = row+4):
0x0064 -> 0x449E4 (VAP create), 0x0065 -> 0x46394 (VAP start), 0x0066 ->
0x4455A (VAP delete), 0x0067 -> 0x45F74, 0x0005 -> 0x82878 (set SSID),
0x0124 -> 0x841B8 (channel), 0x0127 -> 0x84DA4 (AP config), 0x0052 -> 0x42044
(set freq), 0x03E4 -> 0x84260 (sibling channel worker).

## T11. Reproduction

tools/fw_rf_programme_re.py (reads the pinned local image + the lab objdump;
copies no byte into the repo):

| mode | output |
| --- | --- |
| --selftest | asserts the decoded windows: the 0x40684 gate sequence, the 0x101890 band gate + field switch + table literals, the T4 words, the T5 literal values, the 0x89840 latch, the helper address set, the probe-pack arithmetic; exit 0 on PASS |
| --decode | the call chain + field windows + both RF images |
| --tables | T4/T5 |
| --readback | T7/T8 with physical addresses |
