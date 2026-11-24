# wifi-vap-start - tables (T1-T14)

Evidence markers: [V] verified in this lane (`tools/vap_start_re.py --selftest`, 56/56),
[V\*] device-measured by an earlier lane, [I] inference, [U] unknown.
No vendor byte is reproduced: all values are derived addresses/ids/offsets.

## T1. Address conventions

| quantity | rule | evidence |
| --- | --- | --- |
| firmware CPU address | CA = image file offset + 0x40000 | pinned by earlier lanes [V\*] |
| firmware BAR0 (host) | BAR0 = CA + 0x6B8000 | pinned by earlier lanes [V\*] |
| vendor module instruction | ET_REL .text file offset = CA + 0x38 | section layout [V] |
| radio-0 object G | CA 0x170E28 (runtime BSS) | `ctx_create_re` + this lane's math [V] |
| module-table slot m | u32 at G + (0x18C+m)*4 | `FE578` decode [V] |
| module context | ctx(m) = G + u16[G+0x6DC] + u32[G+(0x18C+m)*4] | `CA 0xFE578` decode [V] |

## T2. HCC header (all families)

| off | width | value | note |
| ---: | ---: | --- | --- |
| +0x00 | u8 | group nibble 0 or 2 | only groups 0/2 have H2D tables [V] |
| +0x01 | u8 | alloc nibble 1, high nibble dst | hcc_msg_alloc [V] |
| +0x02 | u8 | field A | family-specific |
| +0x03 | u8 | field B - handler module lookup index | [V] |
| +0x04 | u16 | total = wire length | validator enforces equality [V] |
| +0x06 | u16 | message id (g0) / 0 in the WAL builder | [V] |
| +0x08 | u8 | field C | 0 in every frame here [V] |
| +0x09 | u8 | dst (mirror of hdr[1] high nibble) | hcc_msg_tx [V] |
| +0x0A | u16 | tag 0x5A5A | firmware receive check [V] |

Carrier: SR node {devva, (total<<16)|0x6D2B} + packed producer update + coalesced mailbox id-3
announce; bounded out[0] poll; one frame per idea.

## T3. Config-event key table A (CA 0x10C23C, 217 x {u16 key; u16 pad; u32 worker})

| row | key | worker CA | role |
| ---: | ---: | ---: | --- |
| 0 | 0x0004 | 0x811CD | module-flag write |
| 1 | 0x0064 | 0x449E4 | module/VAP add (create helper 0xFE6BC) |
| 2 | 0x0065 | 0x46394 | VAP start |
| 3 | 0x0066 | 0x4455A | VAP delete ([U] body) |
| 4 | 0x0067 | 0x45F74 | VAP down ([U] body) |
| 5 | 0x0069 | 0x839B4 | set-mode event |
| 41 | 0x00D8 | 0x81038 | TXQ config sync [V\*] |
| 164 | 0x0320 | 0x81444 | TXQ-table address sync [V\*] |

Table B (fallback search): CA 0x103BF8, 88 x 8 B. Handler CA 0x43D58: key at payload+8,
len at payload+0xa, data at payload+0xc; worker called as worker(ctx, len, data).

## T4. g0 direct start_sta pair

| id | handler CA | payload | issuer (vendor CA) | field sources |
| ---: | ---: | ---: | ---: | --- |
| 0x000F | 0x62EC0 | 64 B | hmac_sta_sync_join_req_params 0x52FAC | BSSID join+0x36, channel join+0x2c, band info vap+0xFB8, 1 at [16], IE 33 B at [20], flag [54] |
| 0x0010 | 0x5E28C | 16 B | hmac_sta_wait_join 0x538B8 | [0] join+5, [4] join+6, MAC vap+0xF68 at [8..13], 1 at [14] |

## T5. g0 id1 VAP-add data (key 0x0064, 20 B)

| off | field | worker read | default in builder |
| ---: | --- | --- | --- |
| +0x00 | create type (1/2/3 allocate the ctx; hmac vap modes are 1/2/3) | data[0] | 2 (AP) |
| +0x01 | parent module index (handler-context lookup gate) | data[1] | 0x12 (boot-created module) [I] |
| +0x04 | module/VAP index created (<= 0x12) | data[4] | 0 |
| +0x06 | flag byte | data[6] | 0 |
| +0x08 | flag bit0 (used when type == 2) | data[8] | 0 |
| +0x10 | flag byte | data[0x10] | 0 |
| +0x11 | helper flag | data[0x11] | 0 |
| +0x12 | band selector 0/1 | data[0x12] | 0 |

Success binds module+0x4B0/+0x4B4/+0x4B8 (requires [band+0x1A00] != 0) and allocates
ctx+0x190 through CA 0xFE6BC.

## T6. g0 id1 VAP-start data (key 0x0065, 12 B)

| off | field | source in vendor hmac_config_start_vap_event | default |
| ---: | --- | --- | --- |
| +0 | channel argument | do_start_vap r1 | 0 |
| +1 | state byte | vap+0x0f | 1 [I] |
| +2 | cfg byte | vap+0x11 | 0 |
| +3 | cfg byte | vap+0x12 | 0 |
| +4..7 | u32 | vap+0x414 | 0 |

## T7. WAL group-2 cfg-event envelope

| off | width | field | evidence |
| ---: | ---: | --- | --- |
| +0x00.. | 12 B | HCC header: group 2, alloc 1, id 0, total = 32+body_len | wal_alloc_cfg_event [V] |
| +0x0C | u32 | response cookie (echoed by firmware for host matching) | [V] |
| +0x10 | u32 | WAL cfg-event id = 1 | wal_send_cfg_event [V] |
| +0x14 | u16 | pad (0) | [V] |
| +0x16 | u16 | data_len = 4 + body_len | [V] |
| +0x18 | u32 | sequence counter | [V] |
| +0x1C | u16 | WAL command id | [V] |
| +0x1E | u16 | body_len | [V] |
| +0x20 | body | body[body_len] | [V] |

## T8. WAL commands recovered

| cmd | body | issuer | meaning |
| ---: | ---: | --- | --- |
| 0x0064 | 24 | wal_hipriv_add_vap 0x147B7C | VAP add (create interface) |
| 0x0065 | 12 | wal_start_vap 0x11519C | VAP start (body[0]=1, cookie at body[8]) |
| 0x0127 | 156 | wal_cfg80211_start_ap 0x103634 | AP config (body still [U], gap G1) |
| 0x00D0 | 16 | wal_kick_sta 0x1152DC | kick STA (for completeness) |

## T9. WAL VAP-add body (24 B, [V] structure / [I] field meaning)

| off | field | default |
| ---: | --- | --- |
| +0 | vap mode: 2 = AP, 3 = STA | 2 |
| +1 | sub type byte (hipriv runtime state) | 0 |
| +6/+7 | flag bytes | 0 |
| +12 | band (from the vendor band parse) | 0 |
| +17 | band type byte | 0 |
| +18 | band id | 0 |
| +19 | sub byte | 0 |
| +20..23 | response cookie | 0 |

## T10. Frame totals

| family | rule | example |
| --- | --- | --- |
| g0 id1 config event | total = 28 + data_len | VAP add 48, VAP start 40 |
| g0 direct | total = 12 + payload | 0xF -> 76, 0x10 -> 28 |
| WAL group-2 cfg event | total = 32 + body_len | VAP add 56, VAP start 44, AP cfg 188 |

## T11. Scan handler gate (CA 0x6494C)

| step | check | failure behaviour |
| ---: | --- | --- |
| 1 | hdr = hcc_msg_get_hdr_validate(msg) | log, return 100 |
| 2 | module = 0x961AE(hdr[3]) | log, return 100 |
| 3 | **[module+0x4B8] != 0** | log, return 100 (the 2026-11-26 no-result case) |
| 4 | vap0 = CA 0x581C8(0) | log, return 100 |
| 5 | vap+0x502 = payload+0x13B | - |
| 6 | tail-call CA 0x644A4(module resource, vap, chip, payload) | validates payload+0x1F0 (4/8 rejected), copies 508 B to vap+0x300 |

## T12. Bench readback pack

| name | address | expect |
| --- | --- | --- |
| G (radio 0 object) | BAR0 0x829E28 | stable |
| module-table slot 0 | BAR0 0x829458 | 0, then nonzero after VAP add |
| module-table slot 4 | BAR0 0x829468 | 0x0011C458 after the C2 ctx-create [V\*] |
| base offset u16 | BAR0 0x82A504 | 0 on the validated PD4 boot |
| ctx(0) | u32(slot0) + u16(base) | module-0 context CA |
| ctx0+0x190 | ctx0 + 0x6B8000 + 0x190 | nonzero after the add |
| ctx0+0x4B0 | ctx0 + 0x6B8000 + 0x4B0 | nonzero after a successful bind |
| ctx0+0x4B8 (PASS gate) | ctx0 + 0x6B8000 + 0x4B8 | nonzero before S1 |
| exception mask | BAR0 0x40008 | 0 |
| exception staging frame | BAR0 0x6F800C | all zero |

## T13. Benchmark tool contract

| tool | purpose | result |
| --- | --- | --- |
| `tools/vap_start_re.py --selftest` | static re-verification (firmware + vendor module, sha256-pinned) | 56/56 PASS |
| `tools/vap_start_re.py --tables` | print key table A rows / g0 / g2 tables | derived output only |
| `tools/vap_start_re.py --frames` | frame-length arithmetic | derived output only |
| `tools/build.sh` | host strict build + 61 KATs + emit | PASS |
| `tools/gen_frames_md.py` | regenerate `recipes/frames.md`; `--check` freshness gate | current |
| `tools/kernel-check.sh` | apply `provider/vapstart-provider.patch` to a scratch copy of wifi-scan-ap and build the module against 6.18.60 | module 707,912 B sha256 6b611037..., zero warnings, 20 vsf_ symbols |

## T14. Stop rules (bench)

*mask CA 0x40008 != 0; nonzero staging frame at BAR0 0x6F800C; golden page 0x86807FEF/7/2/0xABCD
or state 1 changes; g+0xB4/+0xB8 != 0; SR node not consumed in the bounded poll; h2d_status
refused increments; any dmesg fault. On stop: record, power-cycle, analyse offline; never
re-send, never write CA 0x400392F0, never post mailbox id 5, never send group 1/4.*

## T15. h2d_frames WAL family validation (post-fire fix, 2026-11-26)

The inherited validator refused the WAL VAP-add frame in hi5622_frame_post() because the
firmware's static group-2 receive table has no id-0 slot (H2D_ENOHANDLER). The provider patch
adds a structural family check; every other (group,id) keeps the generic slot rule.

| check | value | reject |
| --- | --- | --- |
| HCC family | group 2, header id 0 | n/a (other ids -> generic slot lookup) |
| envelope length | payload >= 20 B | `H2D_ESHORT` |
| WAL cfg-event id | u32 payload+4 == 1 | `H2D_EWALCMD` |
| data length | u16 payload+0x0A == body_len + 4 | `H2D_EARG` |
| payload length | payload_len == 20 + body_len | `H2D_ELEN` |
| command allowlist | 0x0064/24, 0x0065/12, 0x00D0/16, 0x0127/156 | `H2D_EWALCMD` (unknown cmd) / `H2D_EARG` (bad size) |

Evidence: `tools/validator-check.sh` -> 31/31 WAL cases + inherited 233/233; enum/string
`H2D_EWALCMD` = "WAL cfg-event command/envelope not in the decoded allowlist".

## T16. Bench feedback (2026-11-26, lead's fire of this lane's provider module)

| item | observed |
| --- | --- |
| module fired | 707,912 B sha256 6b611037... (pre-validator-fix provider patch on the cfg80211 image) |
| boot / C2 | golden page 0x86807FEF/7/2/0xABCD, state 1; ctx-create + txq sync consumed |
| user_add + notify (g0 id7/id8) | posted OK |
| WAL VAP add (56 B) | **refused by the driver's h2d_frames validator**: "firmware receive slot has no handler" -> VAP start not posted, scan aborted |
| readback | module-4 ctx+0x4B0 = 0x00174BF0, ctx+0x4B8 = 0x0015CA08, G+0x630 slot0 = 0x00119758 (targets valid) |
| fix | h2d_check_wal_cfg() family validation (T15); rebuilt module 737,504 B sha256 5a0bfce5f92c1e5eb940892ca46cbac209d85784ad05f3bc37251ba36c79eeb1, zero warnings, 20 vsf_ symbols |
| next | reload the fixed module with the same params; expect "frame post vap_add: ... CONSUMED", then VAP start consumed, then scan results |

## T17. Frame-post gate for the WAL family (second fire fix)

`hi5622_frame_post()` applies its own `h2d_fw_handler_lookup()` check after the h2d_frames
validation. For the WAL group-2 cfg-event family the firmware handler is the WAL/core module
(no static H2C table slot), so the post path now uses the new predicate:

| condition | post path behaviour |
| --- | --- |
| `h2d_frame_is_wal_cfg(f)` true (group 2, id 0, structural family check passed) | handled by the firmware WAL module; risk log shows `WAL_G2_CFG` |
| any other frame without a non-NULL static handler slot | still refused with -EOPNOTSUPP ("no firmware handler for group/id") |
| unknown/malformed WAL command | never reaches the post gate - `h2d_frame_decode(H2D_VF_HANDLER)` fails first with `H2D_EWALCMD` |

Evidence: `tools/validator-check.sh` 37/37 (includes predicate accept/reject cases);
kernel module 738,080 B sha256 9e26138de06c94371bfa776a8a511b3386bc852fca0020947729866f55b94bcc,
zero warnings; second benchmark fire observed the pre-fix refusal "no firmware handler for
group/id" at this exact gate.

## T18. Module-context readback convention (corrected after third fire)

| item | value | evidence |
| --- | --- | --- |
| BAR0 of a firmware CA | BAR0 = CA + 0x6B8000 | C2 lane: ctx4 CA 0x11C458 -> BAR0 0x7D4458 [V*] |
| context array base | CA 0x119758 (runtime) | slot0 value 0x119758 on every pack [V*] |
| context array stride | 0xB40 | module-4 CA 0x119758 + 4*0xB40 = 0x11C458 matches the C2 lane [V] |
| module m context | CA(m) = 0x119758 + m*0xB40 | [V] |
| module-table slot | boot-static CA of the context body; does NOT change on create | C2 fire: slot4 = 0x11C458 before and after the create [V*] |
| ctx0+0x00 word | reads 0 pre-create (chip id byte 0 + static zeros) - not a failure | third fire pack [V*] |
| ctx0+0x190 | 0 until a g0 id1 key-0x64 create with data[0] in 1/2/3 | creation helper CA 0xFE6BC [V] |
| ctx0+0x4B0 / +0x4B8 | 0 until the create worker's band bind runs; +0x4B8 is the scan handler gate | CA 0x44BBC bind / CA 0x64986 gate [V] |
| WAL g2 VAP add/start effect | does NOT populate the hCC module context (different layer) | third fire readback: +0x190/+0x4B0/+0x4B8 zero [V*] |
| VAP lookup CA 0x581C8(0) | returns radio object G (0x170E28) for id 0, always non-NULL | CA 0x943AC accepts only id 0 [V] |

Third fire: user_add/notify/vap_add(56 B)/vap_start(44 B)/scan_req(520 B) all CONSUMED, page
golden, mask 0; scan empty -> V3/V4 g0-mode rung is next.
