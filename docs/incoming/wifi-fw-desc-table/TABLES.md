# TABLES - lane wifi-fw-desc-table (task-187)

Grades: [V] re-derived by tools/fw_desc_table_re.py --selftest (from the pinned
local artifacts); [V*] decoded by a committed sibling lane; [I] inference;
[U] needs a device read. No vendor binary or firmware byte is stored here.

## T1. Conventions

| item | value |
| --- | --- |
| firmware image | recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin, 928,920 B, sha256 7fc87e20...c3ef311b |
| firmware CPU address | **CPU = file offset + 0x40000**; BAR0 = CPU + 0x6B8000; phys = 0x40000000 + BAR0 = CPU + 0x46B8000 |
| vendor modules | recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko (3,581,748 B) and hi5622v100_plat.ko; addresses are **.text section offsets** (ET_REL) |
| firmware image ends | CA 0x122C98; everything at higher CA (incl. the pool 0x1ADC98, maps 0x117AD4/0x117B48) is runtime RAM |
| forbidden | never read the RC misc window 0x10160000..0x1016ffff; never write CA 0x400392F0 |
| this lane | wrote nothing to any device, module or image |

## T2. The H2D 0x00F7 frame (wire) [V]

HCC **group 0, message id 1**; total = 12 + 8 + 2 + 2 + len + 4 = **len + 28**
(our clean driver's family; the vendor constructor declares len+24 and the
4-byte tail is the inherited 0xFF tail already present in every working frame).

| offset | len | field | value for the 2-record fix |
| ---: | ---: | --- | --- |
| +0x00 | 1 | group | 0x00 (group 0) |
| +0x01 | 1 | bid: low nibble alloc=1, high nibble dst | 0x01 (dst 0) |
| +0x02 | 1 | fa | 0x00 |
| +0x03 | 1 | fb | 0x04 |
| +0x04 | 2 | total length u16 | 0x0044 (68 = len 40 + 28) |
| +0x06 | 2 | message id u16 | 0x0001 (g0 id1 config event) |
| +0x08 | 1 | fc | 0x00 |
| +0x09 | 1 | dst (set by hcc_msg_tx from hdr[1]>>4) | 0x00 |
| +0x0A | 2 | tag | 0x5A5A |
| +0x0C | 8 | preamble/token | 00 00 00 00 01 00 00 00 |
| +0x14 | 2 | **config key u16** | **0x00F7** |
| +0x16 | 2 | **payload len u16** | **0x0028 (8+2*16 = 40)** |
| +0x18 | len | **descriptor blob** (T3) | 40 B |
| +0x18+len | 4 | inherited tail | FF FF FF FF |

Frame (68 B) emitted by `tools/fw_desc_table_re.py --frame`:

```
00010004 4400 0100 0000 5a5a  0000000001000000  f700 2800
5553000002000000
000080001e002c0100000000ff1f0000
010280001e002c0100000000ff000000
ffffffff
```

SR node: word1 = (total << 16) | 0x6D2B = **0x00446D2B** (the existing h2d_post
machinery derives this; no new transport work).

## T3. The descriptor blob (the pool image for N records) [V]

| offset | len | field | 2-record fix value |
| ---: | ---: | --- | --- |
| +0x00 | 2 | country code cc[0..1] | "US" (55 53) - **must differ from the pool's** |
| +0x02 | 1 | NUL | 00 |
| +0x03 | 1 | chip / regdomain id | 00 |
| +0x04 | 1 | **record count N** | 02 |
| +0x05 | 3 | zero | 00 00 00 |
| +0x08 | 16*N | record array | see below |

Record i at blob + 8 + 16*i:

| rec off | len | field | rec0 | rec1 |
| ---: | ---: | --- | --- | --- |
| +0x00 | 4 | word: byte0 band (0=2.4G, 1=5G, 2=other), byte1 bw code (2=20, 4=40, 5=80, 6=other), **byte2 bit7 = 0x00800000 armed/valid**, byte3 0 | 0x00800000 | **0x00800201** |
| +0x04 | 1 | power/plan field | 0x1E (30) | 0x1E |
| +0x05 | 1 | zero | 0 | 0 |
| +0x06 | 2 | power/plan field | 0x012C (300) | 0x012C |
| +0x08 | 4 | zero | 0 | 0 |
| +0x0C | 4 | **channel bitmap** (bit b = map row b; band0 14 rows, band1 29 rows) | 0x00001FFF (ch1..13) | **0x000000FF (ch36..64 = rows 0..7)** |

Limits: N <= **20** (firmware destMax 0x148 = 8+20*16); N >= 1; no CRC, no
version, no length inside the blob. A record with band 0 arms the 2.4G map, band
1 the 5G map; the populator ignores byte2, the band-1 predicate reads it.

Variants (same frame, different blob): full 5G bitmap **0x1FFFFFFF**
(ch36..196) instead of 0xFF; band-1 bw code 4/5 if the fire runs 40/80 MHz.

## T4. Vendor module addresses [V]

| element | addr (.text) | fact |
| --- | ---: | --- |
| **emitter** | **0x63B6C** | hmac_config_set_country; exported via __ksymtab (.data+0xE64) |
| **0xF7 call site** | **0x63C3C** | hmac_config_send_event(ctx, 0xF7, 8+blob[4]*16, blob) |
| builder | 0x5717C | key@hdr+20, len@hdr+22, data@hdr+24, total = 12+8+2+2+len |
| shared allocator | 0x57090 | hdr[6]=1, group/dst nibbles, tag 0x5A5A |
| host twin setter | 0x206E0 | mac_regdomain_set_country: strcmp + memcpy_s(host_copy,0x148,blob,8+N*16) |
| host twin channel maps | 0x205E4 | mac_init_channel_list = the populator twin |
| WAL thunk | 0x12AE0C | wal_config_set_country -> b hmac_config_set_country; pointer in .data+0x8900 |
| blob filler | 0x121E10 | the vendor regdomain table builder (walks 96-byte channel-plan blocks) |
| WAL 0xF7 (decoy) | 0x12290C | in wal_regdomain_update 0x1226F0: g2 cmd 0xF7 with an 8-byte {0x000400F7, host_ptr} |
| WAL 0xF7 GET (decoy) | 0x108454 | wal_ioctl_get_reg_domain: g2 cmd 0xF7, 2-byte body |
| ini country init | 0x1558 | hwifi_config_init_ini_country -> wal_hipriv_setcountry 0x122D0C |
| ini entry | 0x122FE0 | hwifi_config_init_ini |

## T5. Firmware addresses [V]

| element | CA | fact |
| --- | ---: | --- |
| g0-id1 handler | 0x43D59 | key = u16 [msg+20]; worker = entry[+4]; call worker(r1=[msg+22], r2=msg+24) |
| table A (0xF7) | 0x10C23C, 217 entries | row 92 key 0x00F7 -> worker 0x045A31 |
| table B | 88 entries (bound 0x2C0) | second key table searched |
| **pool worker** | **0x45A30** | gate -> install -> populator -> band feature bits |
| pool base literal | 0x45B40 | 0x001ADC98 |
| **gate** | **0x1037BC** | ARM strcmp(pool, body); 0 = identical -> skip copy, still populate |
| memcpy_s | 0xC1E44 | dest=pool, destMax=0x148, src=body, len=8+body[4]*16 |
| populator | 0x1017F8 | clear channel maps to 0xFF, expand pool bitmaps |
| band-0 map / band-1 map | 0x117B48 (14 rows) / 0x117AD4 (29 rows) | row = {ch, slot, 0, 0} |
| descriptor mapper | 0x101650 | (band, t) -> slot = map row byte1; rec = 0x1ADC98 + slot*16 + 8 |
| **band-1 predicate** | **0x101890** | 1 iff rec byte2 bit 0x80 |
| latch | 0x89840 | R+0x410 bit0 = predicate |
| boot pool builder | 0x485BA..0x48620 | cc "99", count 2, rec0 0x00000000 + 0x1FFF; rec1 0x00000201 + 0x1FFFFFFF; then populator |

Worker return codes: 0 = applied/deduped; **0x8B2D** = memcpy_s failed
(count > 20); **0x64** = band object [ctx+0x4B8] == 0 (installed, band feature
bits not programmed).

## T6. Boot-default pool CA 0x1ADC98 (the pre-fix state) [V]

| offset | value | meaning |
| ---: | --- | --- |
| +0x00 | 39 39 00 | country string "99" |
| +0x03 | 00 | chip id |
| +0x04 | 02 | count |
| +0x08 | 00 00 00 00 | rec0 word: band 0, byte2 0 (valid clear) |
| +0x0C / +0x0E | 1E / 2C 01 | 30 / 300 |
| +0x14 | FF 1F 00 00 | 2.4G bitmap 0x1FFF |
| +0x18 | 01 02 00 00 | **rec1 word 0x00000201**: band 1, bw 20 MHz, **valid clear** -> C1 symptom |
| +0x1C / +0x1E | 1E / 2C 01 | 30 / 300 |
| +0x24 | FF FF FF 1F | 5G bitmap 0x1FFFFFFF |

After a good 0x00F7: +0x00 = 55 53 ("US"), +0x18 = 01 02 80 00
(0x00800201, **valid set**), +0x24 = FF 00 00 00 (ch36-64 only).

## T7. Read-only probe pack (phys = 0x40000000 + CPU + 0x6B8000) [V]

| # | object | CPU | BAR0 | phys | len | pass after the fix |
| ---: | --- | ---: | ---: | ---: | ---: | --- |
| 1 | pool header cc + count | 0x1ADC98 | 0x865C98 | 0x40865C98 | 8 | "US", 02 |
| 2 | pool rec0 word | 0x1ADCA0 | 0x865CA0 | 0x40865CA0 | 4 | 0x00800000 |
| 3 | **pool rec1 word** | **0x1ADCB0** | **0x865CB0** | **0x40865CB0** | 4 | **0x00800201** (bit23 set) |
| 4 | pool rec1 bitmap | 0x1ADCBC | 0x865CBC | 0x40865CBC | 4 | 0x000000FF |
| 5 | band-1 map ch36 row (idx 0) | 0x117AD4 | 0x7CFAD4 | 0x407CFAD4 | 4 | byte1 = 01 (was 0xFF) |
| 6 | band-0 map ch1 row (idx 0) | 0x117B48 | 0x7CFB48 | 0x407CFB48 | 4 | byte1 = 00 |
| 7 | **P+0x1A00 (O)** | 0x178238 | 0x830238 | 0x40830238 | 4 | **nonzero** (the perlink publish word) |
| 7a | **P+0xF04 mode byte** | 0x17773C | 0x82F73C | 0x4082F73C | 4 | **nonzero** (byte = 3, the band-1 PHY mode) |
| 7b | R+0x410 (RF block) | chain: perlink=u32(ctx+0x4B0); O=u32(perlink+0x2820); R=u32(O+0x2D8) | | | 4 | bit0 = 1 after a 0x0124 re-post |
| 8 | R+0x3AC | same R | | | 4 | 0x03E6138A |
| 9 | band object ctx+0x4B8 | - | - | - | 4 | nonzero (else the worker returns 0x64) |
| 10 | ctx0 slot/gates | sibling pack | | | | 0x00119758, 2, 1, 1 |

Two-step pointers per the sibling lanes: ctx0 = u32(0x171458) + u16(0x172504);
perlink = u32(ctx+0x4B0); O = u32(perlink+0xE20+0x1A00) = u32(perlink+0x2820);
R = u32(O+0x2D8).

## T8. Decision tree (R1/R2)

~~~
R0 host: selftest + --frame green (68 B, bit23 set, cc != "99")
fire: 0x0064 -> 0x0065 -> 0x00F7 -> 0x0124
read 1,3,4:  pool = "US"/0x00800201/0xFF
  header still "99" -> the gate skipped the install (cc collision? count>20?) -> R3a
  count/cc changed but rec1 word bit23 clear -> frame truncated (len/total/offset bug) -> R3b
read 5: map row ch36 byte1 == 01 -> populator ran
  still 0xFF -> populator not run (worker early-out) -> read 9 (0x64 path) -> report
read 7: R+0x410 bit0 == 1 -> the predicate saw the armed record; then
  read 8: R+0x3AC == 0x03E6138A and a ch36 beacon -> C1 closed
  R+0x3AC stale -> the RF programme did not consume it: re-rank (sibling RF lane)
  bit0 == 0 with row=01 and word bit23 set -> the 0x40684 branch was skipped
     (ctx[4] != 2 or the three gate bytes != 1/1/1) -> probe the sibling gates
~~~
