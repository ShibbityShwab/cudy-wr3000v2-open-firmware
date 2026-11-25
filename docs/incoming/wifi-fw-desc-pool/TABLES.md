# TABLES - lane wifi-fw-desc-pool (task-184)

All firmware addresses are CPU addresses in the project's image-linear
convention: **CPU = file offset + 0x40000** of the pinned FIRMWARE.bin
(928,920 B, sha256 7fc87e20...311b). **BAR0 offset = CPU + 0x6B8000**; host
read path **phys = 0x40000000 + BAR0 offset = CPU + 0x46B8000**. Everything
here is re-derived by tools/fw_desc_pool_re.py --selftest; no vendor byte is
stored in this repository.

Grades: [V] re-derived by the committed tool; [V*] measured by a committed
lane/Lead; [I] inference; [U] needs a device read.

---

## T1. Conventions and safety

| item | value |
| --- | --- |
| firmware CPU base | file + 0x40000 |
| BAR0 offset | CPU + 0x6B8000 |
| host physical (bar0read) | CPU + 0x46B8000 |
| forbidden reads | RC misc window 0x10160000..0x1016ffff |
| writes | none in this lane; never CA 0x400392f0 |
| **correction** | RF-lane T7 row 20 printed BAR0 0x872AD4 for CPU 0x117AD4; correct BAR0 = **0x7CFAD4**, phys = **0x407CFAD4** (all other T7 rows consistent) |

## T2. Descriptor pool CA 0x1ADC98 - layout and boot defaults [V]

| offset | type | meaning | boot default |
| --- | --- | --- | --- |
| +0x04 | u8 | descriptor count | **2** |
| record i: +0x08 | u32 | record word: byte0 band (0=2.4G, 1=5G), **byte2 bit7 = 0x00800000 valid** | rec0: band 0; rec1: **0x00000201** |
| record i: +0x0C | u8 | channel-plan field | 30 (0x1E) |
| record i: +0x0E | u16 | channel-plan field | 300 (0x12C) |
| record i: +0x14 | u32 | channel bitmap (bit b = index b) | rec0 **0x1FFF**, rec1 **0x1FFFFFFF** |

Record address used by the descriptor predicate:
`CA 0x101650(band, slot) = CA 0x1ADC98 + 16*slot + 8`.
Boot-default 5G slot = 1 -> word 0x00000201 -> byte2 = 0x02 -> valid bit **0**.

## T3. The populator CA 0x1017F8 and its trigger paths [V]

| element | address | facts |
| --- | --- | --- |
| populator | **CA 0x1017F8** | clears both maps to slot 0xFF, then expands pool bitmaps to row slot bytes |
| band-0 map | CA 0x117B48 | 14 rows {channel, slot, 0, 0}; 14 bitmap bits |
| band-1 map | CA 0x117AD4 | 29 rows {channel, slot, 0, 0}; 29 bitmap bits |
| pool base literal | CA 0x10188C = 0x001ADB80; +0x118 -> 0x1ADC98 | the header/pool split |
| boot builder | CA 0x485FA..0x4861E, `bl 0x1017F8` at CA 0x48620 | writes count + rec0/rec1 defaults |
| **H2D 0x00F7** | table-A row 92 -> worker **CA 0x45A30**; populator call CA 0x45A76 | host descriptor download |
| **H2D 0x00DA** | table-A row 22 -> worker **CA 0x43460** | edits a record's +0x0C/+0x0E only |
| pool-state gate | CA 0x1037BC(pool, body) | nonzero -> install path [I] |
| blob installer | CA 0xC1E44(pool, 0x148, body, 8+body[4]*16) | memcpy_s semantics; destMax 0x148 (= max idx 20) |

0x00F7 worker flow: gate -> install host blob -> populator -> band-object
feature bits +0xEC0 bits 0/1/2. Body fields: body[4] = descriptor count;
body[0..] = descriptor table image.

## T4. All code sites that use the pool literal CA 0x1ADC98 [V]

| site | role |
| --- | --- |
| CA 0x43460 (ref 0x434B2) | H2D 0x00DA: record +0x0C/+0x0E edit from body {channel 1..30, apply/restore} |
| CA 0x45A30 (refs 0x45A38/0x45A4E) | H2D 0x00F7: host descriptor install + populator |
| CA 0x48520 block (ref 0x485BC) | boot init: default pool + populator + band-0 object setup |
| CA 0xF27F8 (ref 0xF2866) | band/perlink path: pool-state gate + per-band tables (host config) |
| CA 0xF40E4 (ref 0xF40E4) | pool/channel helper; exact role [U] |
| CA 0x101650 (ref 0x101680) | the mapper (read-only consumer) |

## T5. Read-only probe pack (phys = 0x40000000 + CPU + 0x6B8000) [V]

| # | object | CPU | BAR0 | phys | len | expected |
| ---: | --- | ---: | ---: | ---: | ---: | --- |
| 1 | ctx slot0 | 0x171458 | 0x829458 | 0x40829458 | 4 | 0x00119758 |
| 2 | module-table base u16 | 0x172504 | 0x82A504 | 0x4082A504 | 4 | 0 |
| 3 | ctx0+0x004 create type | 0x11975C | 0x7D175C | 0x407D175C | 4 | 2 |
| 4 | ctx0+0x440 gate | 0x119B98 | 0x7D1B98 | 0x407D1B98 | 4 | 1 |
| 5 | bandobj+0xEC8 gate | 0x15F438 | 0x817438 | 0x40817438 | 4 | 1 |
| 6 | ctx0+0x4B0 perlink | 0x119C08 | 0x7D1C08 | 0x407D1C08 | 4 | 0x00175A18 |
| 7 | ctx0+0x4B8 bandobj | 0x119C10 | 0x7D1C10 | 0x407D1C10 | 4 | 0x0015E570 |
| 8 | bandobj+0x1A00 publish | 0x15FF70 | 0x817F70 | 0x40817F70 | 4 | == read 6 |
| 9 | P+0x1A00 (O) | 0x178238 | 0x830238 | 0x40830238 | 4 | nonzero |
| 10 | P+0xF04 mode byte | 0x17773C | 0x82F73C | 0x4082F73C | 4 | byte 3 |
| 11 | P+0xF87 range flag | 0x1777BF | 0x82F7BF | 0x4082F7BF | 1 | 0 ch36-64 / 1 ch100+ |
| 12 | **pool header/count** | **0x1ADC98** | **0x865C98** | **0x40865C98** | 8 | +0x04: 2 if boot default |
| 13 | **pool rec0 word** | **0x1ADCA0** | **0x865CA0** | **0x40865CA0** | 4 | band-0 word if boot default |
| 14 | **pool rec1 word** | **0x1ADCB0** | **0x865CB0** | **0x40865CB0** | 4 | 0x00000201 if boot default |
| 15 | pool rec1 bitmap | 0x1ADCBC | 0x865CBC | 0x40865CBC | 4 | 0x1FFFFFFF if boot default |
| 16 | **chan map B1 row idx** | **0x117AD4+4*idx** | **0x7CFAD4+4*idx** | **0x407CFAD4+4*idx** | 4 | byte1 = slot; boot default 1 |
| 17 | chan map B0 row | 0x117B48+4*idx | 0x7CFB48+4*idx | 0x407CFB48+4*idx | 4 | byte1 = slot; boot default 0 |
| 18 | **G+0x000 head** | **0x1747C4** | **0x82C7C4** | **0x4082C7C4** | 4 | nonzero when module started |
| 19 | **G+0x370 MAC-start cb** | **0x174B34** | **0x82CB34** | **0x4082CB34** | 4 | nonzero |
| 20 | **G+0x374 beacon-start cb** | **0x174B38** | **0x82CB38** | **0x4082CB38** | 4 | nonzero |
| 21 | band0 object | 0x15CA08 | 0x814A08 | 0x40814A08 | 4 | chain reference |
| 22 | band1 object | 0x15E570 | 0x816570 | 0x40816570 | 4 | chain reference |
| 23 | VAP = u32(ctx+0x190); VAP+0x58 byte | - | - | - | 1 | bit2 = band-1 VAP flag |
| 24 | VAP+0x13A u16 | - | - | - | 2 | beacon interval (100 TU vendor default) |
| 25 | R+0x3A8/0x3AC/0x3B4/0x3CC/0x410 | - | - | - | 4 | RF image from RF lane T5 |

Two-step pointers: ctx0 = u32(0x171458) + u16(0x172504); perlink =
u32(ctx+0x4B0); O = u32(perlink+0xE20+0x1A00) = u32(perlink+0x2820);
R = u32(O+0x2D8). Pool record word = u32(0x1ADC98 + 16*slot + 8) with
slot = byte1 of the current channel's map row.

## T6. C4 - plat.ko ini path [V]

| element | address / offset | facts |
| --- | --- | --- |
| ini keys | cfg_hi5622v100_hisi.ini [HOST_WIFI_NORMAL] | beacon_2g_tx_policy=0, beacon_5g_tx_policy=2 |
| key 2g string | plat .rodata.str1.4+0x1F5C | "beacon_2g_tx_policy" |
| key 5g string | plat .rodata.str1.4+0x1F70 | "beacon_5g_tx_policy" |
| key table entry 2g | plat .data+0x5D0 | {id 0xAE, 0x1F5C} |
| key table entry 5g | plat .data+0x5D8 | {id 0xAF, 0x1F70} |
| module init | plat_main_init 0x3054 -> hwifi_config_init(17/18/19) 0x19A0 | |
| accessor | hwifi_get_init_value 0x16AC(module, id), id <= 0xCB | g_al_host_init_params / g_al_dts_params |
| crop to blob | hwifi_get_dts_dpd_pwr_customize_param **0x4950** | ids 0xAD..0xB1 |
| 2g landing | g_dev_custom_info **+0x91C** (byte) / +0x91D | beacon_2g_tx_policy |
| 5g landing | g_dev_custom_info **+0x7AE** (u16 lo) / +0x7B4 (b16-31) | beacon_5g_tx_policy = 2 |
| blob | g_dev_custom_info, plat bss 0xC88, size 0x9A8, exported | imported by hi5622v100_wifi.ko |
| register | wifi_dev_custom_register 0x1BE30 | stores size 0x9A8 + blob ptr |
| send | dev_custom_send 0x1BE98 -> bal_write (0x60-byte record) | the only send path |

Verdict: platform/ini host state; not present in the five H2D frames and not
synthesisable by reordering them. Only a host config change/vendor plat module
can supply it.

## T7. C5 - band-1 start/TBTT objects [V]

| element | address | facts |
| --- | --- | --- |
| VAP start (H2D 0x0065) | CA 0x46394 | VAP+0x0E=1, VAP+0x58 bit2=(band!=0), -> CA 0x971D8 |
| VAP start-on-band | CA 0x971D8 | reconcile band/link; if G+0: call G+0x370(ctx), G+0x374(ctx) |
| global G | **CA 0x1747C4** (literal CA 0x9745C) | band callback table |
| MAC start hook | **G+0x370 = CA 0x174B34** | called with VAP ctx (CA 0x97360/0x97366) |
| beacon start hook | **G+0x374 = CA 0x174B38** | called with VAP ctx (CA 0x9736A/0x97370) |
| AP config (H2D 0x0127) | CA 0x84DA4 | VAP+0x13A u16 beacon interval = body+0x94 |
| beacon copy | CA 0x7E5CC | VAP+0x13A -> beacon descriptor +0x260 |
| ISR name blob | CA 0x12266C..0x122BA4 | 45 names; smac_tbtt_5g_isr CA 0x1229E8; **no code reference [V]** |

## T8. Decision tree (R0 read-only pass)

~~~
read 1,2 -> ctx0 ; 3,4,5 -> gates (expect 2,1,1)
  gates not as expected -> prior CW-2 verdict stands, stop
read 6,7,8 -> perlink / band object / publish-word consistency
  mismatch -> wrong band object bound: report, do not patch
read 9,10,11,(13 via chain) -> O, mode byte, R
  O==0 or R==0 or R+0x3AC==0 -> C1 (RF programme never ran)
  P+0xF04 != 3 -> band-1 PHY init never ran: different blocker
read 12 -> pool count
  2  -> boot-default map (0xF7 not sent by our stack) -> read 14/16
  >2 -> a host 0xF7 table was installed -> read 16/record word
read 16 -> current channel map row byte1 = slot
  slot == 1 and word(14/record) == 0x201 -> default; 0x00800000 == 0 BY DESIGN
  slot >= 2 -> host table: valid bit is host-supplied (C2 becomes meaningful
               only if that record expects the bit set)
read 18,19,20 -> G head and both start callbacks
  any 0 -> C5a (band-1 MAC/beacon start hook not installed)
read 23,24 (VAP) + band-object diff -> C5b/C5c
if RF image full + callbacks nonzero + no beacon -> rf-programme lane T8 diff
~~~

## T9. Reproduction

| mode | output |
| --- | --- |
| `--selftest` | 97 assertions over every window/table above; exit 0 PASS |
| `--decode` | C2 populator flow |
| `--tables` | C2 defaults + C4 landing + C5 hooks |
| `--policy` | C4 path detail |
| `--tbtt` | C5 path detail |
| `--probes` | T5 with CPU/BAR0/phys |
