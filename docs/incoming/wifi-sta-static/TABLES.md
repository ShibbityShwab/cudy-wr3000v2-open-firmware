# TABLES - wifi-sta-static (task-141)

All addresses are link-time offsets in **hi5622v100_wifi.ko 2.5.24**
(md5 `4737fcb2…`); the 2.4.15 build (md5 `1745ef77…`) carries the same
functions and constants at its own offsets. Re-derive with `tools/sta_re.py`.

## T1 - cfg80211 STA-relevant ops (2.5.24)

| cfg80211_ops slot | vendor function | .text | size | note |
| --- | --- | ---: | ---: | --- |
| .connect | `wal_cfg80211_connect` | 0x100590 | 0x2b4 | WAL opcode 0x123, 108 B body |
| .disconnect | `wal_cfg80211_disconnect` | 0x100844 | 0x1b8 | WAL opcode 0x00d0, 16 B body |
| .set_power_mgmt | `wal_cfg80211_set_power_mgmt` | 0x1050ac | - | not needed for connect |
| .scan | `wal_cfg80211_scan` | 0x10027c | - | already ported (scan lane) |
| .auth/.assoc/.deauth | (NULL) | - | - | firmware performs them [V] |
| .update_connect_params | (NULL) | - | - | - |

## T2 - WAL cfg-event connect

| field | value |
| --- | --- |
| HCC group / id | 2 / 0 |
| WAL message id (payload+4) | 1 (WAL_MSG_CFG) |
| data_len (payload+0x0a) | 4 + 108 = 112 |
| opcode (payload+0x10) | **0x0123** |
| body_len (payload+0x12) | **108** |
| wire total | 12 + 20 + 108 = **140 B** |
| source builder | `wal_cfg80211_start_connect` .text+0x128310 -> `wal_cfg80211_start_req` .text+0x127ec4 |
| bench carrier | same envelope as the AP-start lane's WAL frames (firmware-consumed [V*]) |

## T3 - WAL cfg-event disconnect

| field | value |
| --- | --- |
| opcode | **0x00d0** (inherited allowlist name `H2D_WAL_CMD_KICK_STA`) |
| body_len | **16** |
| wire total | 12 + 20 + 16 = **48 B** |
| body | +0x00 bssid[6]; +0x06 u16 reason; +0x0f = 1 (vendor constant) |
| source builder | `wal_cfg80211_start_disconnect` .text+0x128334 |

## T4 - connect body (108 B)

| off | width | value |
| ---: | ---: | --- |
| 0x00 | u8 | 0 |
| 0x01 | u8 | ssid_len |
| 0x02 | u8 | mfp |
| 0x03 | u8 | 0 |
| 0x04 | u32 | IE buffer address (host DRAM) |
| 0x08 | u32 | SSID buffer address |
| 0x0c | u32 | BSSID buffer address |
| 0x10 | u8 | privacy (0/1) |
| 0x11 | u8 | auth_type (nl80211 value 0..8) |
| 0x12 | u8 | key_len |
| 0x13 | u8 | key_idx |
| 0x14 | u32 | key buffer address |
| 0x18..0x67 | - | 0 |
| 0x68 | u32 | ie_len |

## T5 - vendor band tables (static .data)

| band | supported_band | channels | n | bitrates | n | max_power field |
| --- | --- | --- | ---: | --- | ---: | ---: |
| 2.4 GHz | .data+0xdf8 | .data+0x16e4 | 14 | .data+0x19f4 | 12 | 30 |
| 5 GHz | .data+0xe54 | .data+0x108c | 29 | .data+0x1a24 | 8 | 30 |

5 GHz channel list [V]: 36(5180) 40(5200) 44(5220) 48(5240) 52(5260)
56(5280) 60(5300) 64(5320) 100(5500) 104(5520) 108(5540) 112(5560)
116(5580) 120(5600) 124(5620) 128(5640) 132(5660) 136(5680) 140(5700)
144(5720) 149(5745) 153(5765) 157(5785) 161(5805) 165(5825) 184(4920)
188(4940) 192(4960) 196(4980).

2.4 GHz channel list [V]: 1..13 (2412..2472) + 14 (2484).
Rates [V]: 2.4 GHz 10 20 55 110 60 90 120 180 240 360 480 540;
5 GHz 60 90 120 180 240 360 480 540 (shared OFDM tail of the same array).

Channel struct: 56 B, `{band u32; center_freq u32; freq_offset u16; hw_value
u16; flags u32; max_antenna_gain s32; max_power s32; ...}`; hw_value = IEEE
channel number; static flags = 0.

## T6 - capability words

| field | 2.4 GHz | 5 GHz |
| --- | --- | --- |
| ht_cap.cap | 0x0062 | 0x0062 |
| ht_cap.ht_supported | 1 | 1 |
| ht_cap.ampdu_factor / density | 0 / 0 | 0 / 0 |
| ht_cap.mcs.rx_mask | 0x00… (all zero, runtime-filled [U]) | same |
| vht_supported | 1 | 1 |
| vht_cap.cap | 0x00400000 | 0x00400064 |
| vht rx/tx mcs map | 0xfffa / 0xfffa | 0xfffa / 0xfffa |

0x0062 = SUP_WIDTH_20_40 | SHORT_GI_20 | SHORT_GI_40.
0x00400000 = HTC_VHT. 0x00400064 = HTC_VHT | SUPP_CHAN_WIDTH_160MHZ |
SHORT_GI_80 | SHORT_GI_160.
0xfffa = 2 spatial streams, MCS0-9.

## T7 - join / wait messages (legacy direct family)

| message | builder | group/id | payload | payload map |
| --- | --- | --- | ---: | --- |
| STA join request | `hmac_sta_sync_join_req_params` 0x52fac | runtime group (0), id 0x0f | 64 | T8 |
| wait for join | `hmac_sta_wait_join` 0x538b8 | runtime, id 0x10 | 16 | [U] |
| wait join rx beacon | `hmac_sta_wait_join_rx_beacon` 0x514d4 | 0, id 0x10 | 16 | [U] |
| assoc response handling | `hmac_handle_asoc_rsp_sta` 0x99280 | 2, id 3 | 28 | [U] |
| connect failed report | `hmac_report_connect_failed_result` 0x988a8 | 2, id 3 | 28 | [U] |
| disassoc response | `hmac_sta_handle_disassoc_rsp` 0x53c44 | 2, id 4 | dyn | [U] |

## T8 - legacy join request payload (64 B)

| off | width | value |
| ---: | ---: | --- |
| 0x00..0x05 | 6 | BSSID (event params +0x36) |
| 0x06..0x07 | u16 | event params +0x2c |
| 0x08..0x0b | u32 | user object +0xfb8 |
| 0x0c..0x0f | u32 | 0 |
| 0x10..0x13 | u32 | 1 |
| 0x14..0x34 | 33 B | memcpy from event params +0x0b |
| 0x34 | u8 | 0 |
| 0x36 | u8 | bandwidth (0x1/0x2 from chip res +0x34c bits) |
| 0x35, 0x37..0x3f | - | written by `hmac_sta_update_join_multi_bssid_info` |

Header: group 0, id 0x0f, vap id / chip id from user+0xfa9/+0xfa8, band nibble
in header[1] high nibble from user+0xfaa.

## T9 - module parameters added by this lane

| param | default | meaning |
| --- | ---: | --- |
| `sta_join` | 0 | 0 fail-closed; 1 build+validate only; 2 post the WAL connect/disconnect |
| `ht_mode` | 0 | 0 baseline HT20; 1 vendor HT20 (0x20); 2 vendor HT20/40 (0x62) |
| `vht_cap` | 0 | advertise vendor VHT (2 SS MCS0-9; 5G 160 MHz/SGI bits) |
| `band_vendor` | 0 | use the vendor 14/29-channel tables instead of the shipped lists |

## T10 - band model facts

| fact | value | evidence |
| --- | --- | --- |
| wiphys per host MAC | one per enabled band (2 on stock) | `wal_cfg80211_init` loop + bench WIPHY=2 [V*] |
| host MAC chips accepted | chip 0 only | `hmac_band_get_by_id` chip check [V] |
| band objects | `g_ast_band[2]`, stride 0x2330 | `.bss+0x49288` + index math [V] |
| band type byte | band object +0x10d1 | `hmac_band_id_to_band_type` [V] |
| band enable | band object with matching type exists | `hmac_device_check_band_enable` [V] |
| wiphy perm_addr marker | last byte = band_id + 1 | `wal_cfg80211_init` [V] |
| iface combinations | 1 entry at .data+0x1070 | `[wiphy+20] = &ptr` [V] |
| interface_modes | 12 = STATION | MONITOR | `strh r3,[wiphy+32]` [V] |
