# wifi-5ghz - evidence tables (T1-T11)

All addresses are derived constants from the pinned local artifacts (see
REPORT.md sec 1).  Host-module offsets are link-time section offsets
(`.text`/`.data`/`.rodata`/`.bss`) of the 2.5.24 build; the 2.4.15
build has the same structures modulo layout.  Firmware CPU address (CA) =
file offset + 0x40000.  No vendor byte is reproduced.  Reproduce with
`tools/band5g_re.py`.

## T1. Vendor host anchors [V]

| function | offset (2.5.24) | role |
| --- | ---: | --- |
| `g_ast_band` | `.bss+0x49288` (2 x 0x2330) | the two logical band objects |
| `g_ast_channel_list_2G` | `.data+0x3b8` (14 x 4 B) | 2.4 GHz channel list {u8 chan; u8 0xff; u16 0} |
| `g_ast_channel_list_5G` | `.data+0x3f0` (29 x 4 B) | 5 GHz channel list, same shape |
| `g_ast_freq_map_2g` | `.rodata+0x918` (14 x 4 B) | {u16 freq; u8 chan; u8 0} |
| `g_ast_freq_map_5g` | `.rodata+0x8a4` (29 x 4 B) | {u16 freq; u8 chan; u8 0} |
| `hmac_device_add_band` | `.text+0x93c2c` | creates band id 0 then band id 1 |
| `hmac_band_init` | `.text+0x13f880` | band object init (mac_band/AP chan list/DFS), type at band+0x10d1 |
| `hmac_band_get_by_id` | `.text+0x13f6f4` | chip-0 only, band id <= 1, `g_ast_band + id*0x2330` |
| `hmac_band_id_to_band_type` | `.text+0x1407b4` | reads type byte at band+0x10d1 |
| `hmac_band_type_to_band_id` | `.text+0x14067c` | scans the two band objects for the type |
| `wal_cfg80211_init` | `.text+0x106a74` | one wiphy per board band; type 0 -> bands[0]=2.4G, type 1 -> bands[1]=5G |
| `wal_hipriv_add_vap` | `.text+0x147b7c` | parses the g2/g5 VAP-create commands (band type 0/1) |
| `hmac_config_check_band_type` | `.text+0x15c0e4` | firmware-side band type/id/chip check |
| `mac_get_channel_num_from_idx` | `.text+0x1f42c` | band-local index -> channel number (bounds 13 / 28) |
| `mac_is_channel_num_valid` | `.text+0x1f684` | channel-number validity against the 14/29 lists |
| `wal_cfg80211_start_ap` | `.text+0x103634` | AP start; band = `dev_priv[0]`, channel helper first |
| channel helper | `.text+0x102e84` | validates channel/width/DFS, builds the 0x0124 cfg event |
| `wal_cfg80211_fill_beacon_param` | `.text+0x101774` | 156-byte AP config body; band-gated HT/VHT hints |

## T2. Band tables (host and firmware identical) [V]

| band | count | channels | frequencies |
| --- | ---: | --- | --- |
| 2.4 GHz | 14 | 1..14 | 2412..2472 (ch1..13), 2484 (ch14) |
| 5 GHz | 29 | 36 40 44 48, 52 56 60 64, 100 104 108 112 116 120 124 128 132 136 140 **144**, 149 153 157 161 165, **184 188 192 196** | 5000+5*ch; 4.9 GHz block 4000+5*ch = 4920..4980 |

Firmware copies: 5 GHz CA 0x117AD4 (29 x 4 B), 2.4 GHz CA 0x117B48 (14 x 4 B),
same {u8 chan; u8 0xff; u16 0} shape.

## T3. Band-local index of the 5 GHz table [V]

| ch | 36 | 40 | 44 | 48 | 52 | 56 | 60 | 64 | 100 | 104 | 108 | 112 | 116 | 120 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| idx | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 |

| ch | 124 | 128 | 132 | 136 | 140 | **144** | 149 | 153 | 157 | 161 | 165 | 184 | 188 | 192 | 196 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| idx | 14 | 15 | 16 | 17 | 18 | **19** | 20 | 21 | 22 | 23 | 24 | 25 | 26 | 27 | 28 |

2.4 GHz: ch1..14 = index 0..13.

## T4. Band type / band id / chip [V]

| item | value | source |
| --- | --- | --- |
| band type 0 | 2.4 GHz | `wal_hipriv_add_vap` g2 branch; `wal_cfg80211_init` 2g template |
| band type 1 | 5 GHz | `wal_hipriv_add_vap` g5 branch; `wal_cfg80211_init` 5g template |
| band ids | 0 and 1, both on chip 0 | `hmac_device_add_band` / `hmac_band_get_by_id` |
| WAL VAP-create body | band id at body+0x12, chip id at body+0x13 | `wal_hipriv_add_vap`/`fill_param` |
| firmware AP check | `vap[0x11]` type vs band object type; `vap[0x12]` band id; `vap[0x13]` chip | `hmac_config_check_band_type` |

## T5. Per-band wiphy registration (vendor) [V]

`wal_cfg80211_init`: `hmac_board_get_instance` -> band count at board+6,
band ids at board+7+i; per band: `wiphy_new_nm(ops, priv=4)` ->
`hmac_band_get_by_id(0, loop_index)` -> per-band type; type 0:
`hmac_device_check_2g_enable` -> `wiphy->bands[0]` = 2.4 GHz template
(`.data+0xdf8`); type 1: `hmac_device_check_5g_enable` ->
`wiphy->bands[1]` = 5 GHz template (`.data+0xe54`); then
`wiphy_apply_custom_regulatory` + `wiphy_register`.

## T6. AP-start band/channel flow [V]

| step | host | wire |
| --- | --- | --- |
| 1 | `wal_cfg80211_start_ap` reads band from `dev_priv[0]` | - |
| 2 | channel helper: verify channel (`mac_is_channel_num_valid`), width conversion, DFS check | WAL cfg event cmd **0x0124**, body 4 B: band / vendor mac_vap+2 [I] / channel number / 0 |
| 3 | firmware worker CA 0x841B8: body[2]->ctx+0x10 channel, body[0]->ctx+0x11 band, body[1]->ctx+0x12, valid->ctx+0x13; PHY program CA 0xFDCE4 | - |
| 4 | set SSID | WAL 0x0005 (36 B) |
| 5 | AP config (156 B; band-gated HT/VHT hints at body+0x8a..0x8d) | WAL 0x0127 |
| 6 | VAP start | WAL 0x0065 (12 B) |

There is **no** separate 5 GHz enable message in this path [V].

## T7. Firmware band/channel tables and validator [V]

| item | CA | evidence |
| --- | ---: | --- |
| 5 GHz channel table (29 x 4 B {u8 ch; u8 0xff}) | 0x117AD4 | values 36..196 |
| 2.4 GHz channel table (14 x 4 B) | 0x117B48 | values 1..14 |
| validator `(band, channel) -> index` | 0x10172C | count 29 / 14, literals 0x117AD4 / 0x117B48 |
| AP module channel worker | 0x841B8 | validates, stores ctx+0x10..0x13, PHY program 0xFDCE4 |
| 5 GHz TBTT ISR (name) | - | `smac_tbtt_5g_isr` present in image |

## T8. H1 g0 id10 handler and channel resolver [V]

Handler CA 0x5FF64 (payload starts at msg+0x0c):

| payload off | field | use |
| ---: | --- | --- |
| +0x00 | type | must be 3 |
| +0x01 | subtype | 1 = per-slot context config, 2 = arm flag |
| +0x02 | u16 channel | read as `ldrh r0,[r4,#0xe]` -> CA 0xD6A1E |
| +0x07 | slot | bounded to <= 7 |
| +0x0c | u16 start | -> context +0x106 (minimum 1) |
| +0x10 | u16 field10 | -> context +0x104 |
| +0x12 | u8 mode | -> context +0x103 |
| +0x14 | u16 compare | -> slot +0x15 |

Resolver chain:

| step | CA | role |
| --- | ---: | --- |
| thunk | 0xD6A1E | `mov r1,r0; movs r0,#0; b.w 0x94728` |
| chip resource | 0x943AC | chip 0 only |
| valid-channel bitmap | 0xC1FBA | `count > idx` and bit `idx` set |
| element lookup | 0xC1FF4 | `base[+0x14] + stride[+4] * idx` on `{u16 count; u32 stride; ...; u32 base}` |

The returned per-channel object is the H1 context base:
`base + 0xEC + slot*0x20`.

## T9. 5 GHz capability words (static templates) [V]

| word | 2.4 GHz template (`.data+0xdf8`) | 5 GHz template (`.data+0xe54`) |
| --- | --- | --- |
| n_channels | 14 | 29 |
| n_bitrates | 12 | 8 |
| ht_supported / ht cap | 1 / 0x0062 | 1 / 0x0062 |
| VHT supported / cap | 1 / 0x00400000 | 1 / 0x00400064 |
| VHT MCS map (rx/tx) | 0xfffa | 0xfffa |

## T10. `band5g` parameter + fail-closed contract [V]

| parameter | default | behaviour |
| --- | ---: | --- |
| `band5g` | 0 | 0: 5 GHz AP start -> -EOPNOTSUPP before any frame; 5 GHz H1 config -> -EOPNOTSUPP; 2.4 GHz unchanged. 1: 5 GHz endpoint wiphy gets the 29-channel/HT 0x0062/VHT 0x00400064 profile; AP/H1 channel must be in the vendor table. |

Precedence: explicit `band_vendor` / `ht_mode` / `vht_cap` settings take
precedence over the `band5g` preset; `band_force` still selects the
endpoint band.

## T11. Host verification counts / evidence

| check | count | evidence |
| --- | ---: | --- |
| `band5g` host selftest | 98/98 PASS | `evidence/host-selftest.txt` |
| static-RE probes 2.5.24 | 28/28 PASS | `evidence/band5g-re-2.5.24.txt` |
| static-RE probes 2.4.15 | 28/28 PASS | `evidence/band5g-re-2.4.15.txt` |
| static-RE probes firmware PD4 | 17/17 PASS | `evidence/band5g-re-firmware.txt` |
| patch apply on final all-patch tree | clean | `evidence/patch-apply.txt` |
| module build warnings/errors | 0 | `evidence/build-verify.txt` |
| params (47 chain + `band5g`) | 48 | `evidence/artifact-identity.txt` |
| stripped artifact (local, never committed) | 312,432 B sha256 `2bee4391...` | `evidence/artifact-identity.txt` |
