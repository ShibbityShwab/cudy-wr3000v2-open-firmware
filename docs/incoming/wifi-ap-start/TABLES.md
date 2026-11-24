# wifi-ap-start - evidence tables (T1-T11)

All addresses are derived constants from the pinned local artifacts
(firmware sha256 7fc87e20..., CA = file + 0x40000; vendor host module 2.5.24
sha256 de78ec07..., .text section offsets).  No vendor byte is reproduced.

## T1. Host module anchors (2.5.24 hi5622v100_wifi.ko)

| function | .text | role |
| --- | ---: | --- |
| wal_cfg80211_start_ap | 0x103634 | cfg80211 .start_ap: SSID event -> auth event -> AP-config body -> write_msg |
| wal_cfg80211_start_ap_write_msg | 0x103548 | 160-byte WAL cfg event (cmd 0x0127, body 156) + wal_start_vap |
| wal_cfg80211_fill_beacon_param | 0x101774 | PHY/IE/RSN/WMM/MD body fill from cfg80211_beacon_data |
| wal_cfg80211_set_auth_mode | 0x101584 | WAL cfg cmd 0x006D, body 4 (u32 nl80211_auth_type) |
| wal_send_set_ssid_event | 0x1284EC | WAL cfg cmd 0x0005, body 36 |
| wal_start_vap | 0x11519C | WAL cfg cmd 0x0065, body 12 (flag 1, cookie) |
| wal_parse_ht_vht_ie | 0x1076FC | HT cap (0x2D) / VHT cap (0xBF) / ext (0xFF) -> body +0x8A..+0x8E |
| wal_parse_md_ie | 0xFFFF4 | MD IE (0x36) -> body +0x94..+0x98 |
| wal_cfg80211_change_beacon | 0x101AAC | same cmd 0x0127 body with +0x93 = 0 |

## T2. Vendor AP-start sequence (host, WAL cfg events)

| # | command | body len | body | wire (WAL envelope) |
| ---: | ---: | ---: | --- | ---: |
| 1 | 0x0005 set SSID | 36 | u8 len, u8 pad[2], ssid[33] | 68 B |
| 2 | 0x006D auth algs | 4 | u32 nl80211_auth_type, allowlist {0,1,4,8} | 36 B |
| 3 | 0x0127 AP config | 156 | T3 | 188 B |
| 4 | 0x0065 VAP start | 12 | u8 start=1, ..., u32 cookie | 44 B |

## T3. AP-config body (156 B) - [V] host builder + [V] firmware worker

| off | width | field | host source | firmware worker 0x84DA4 consumer |
| ---: | ---: | --- | --- | --- |
| +0x00 | u32 | beacon_interval | cfg80211 beacon_interval | VAP ctx +0x10 |
| +0x04 | u32 | dtim_period | cfg80211 dtim_period | VAP ctx +0x14 |
| +0x08 | u8 | RSN/vendor-IE present | beacon head capability bit 0x10 | branches the RSN/vendor path |
| +0x09..+0x48 | 64 B | RSN IE copy (id 0x30) | beacon tail IE 48 | +0x09 checked == 0x30 |
| +0x49..+0x88 | 64 B | vendor IE copy (OUI 00:50:f2) | beacon tail IE 0xDD | +0x49 checked == 0xDD |
| +0x89 | u8 | hidden_ssid | cfg80211 hidden_ssid (ZERO_LEN) | ctx +0x18D bit0 |
| +0x8A | u8 | HT cap bit5 | HT cap IE payload[0] bit5 | VAP +0x34C bit3 |
| +0x8B | u8 | HT cap bit6 (non-2.4) | HT cap IE payload[0] bit6 | passed to CA 0xFDDF8 |
| +0x8C | u8 | VHT cap bit5 (5G hint) | VHT cap IE payload[0] bit5 | VAP +0x360 bit2 |
| +0x8D | u8 | VHT cap bit6 (5G hint) | VHT cap IE payload[0] bit6 | VAP +0x360 bit3 |
| +0x8E | u8 | wireless mode | 10 ext / 6 VHT / 5 HT / legacy 1,2 | passed to CA 0xD4048 |
| +0x8F..+0x92 | 4 B | reserved | zero | no reader |
| +0x93 | u8 | start flag | 1 start_ap / 0 change_beacon | no reader found |
| +0x94 | u16 | MDID | MD IE payload[0..1] | VAP +0x13A |
| +0x96 | u8 | MD present | MD IE present | VAP +0x138 bit0 |
| +0x97 | u8 | MD capability bit0 | MD IE payload[2] bit0 | VAP +0x138 bit1 |
| +0x98 | u8 | MD capability bit1 | MD IE payload[2] bit1 | VAP +0x138 bit2 |
| +0x99..+0x9B | 3 B | reserved | zero | no reader |

## T4. WAL group-2 cfg-event envelope (vendor order)

| off | width | field |
| ---: | ---: | --- |
| +0x00 | 12 B | HCC header: group 2 low nibble; hdr[1] dst<<4|alloc 1; hdr[3] VAP id; total u16 at +0x04; header id 0 at +0x06; tag 0x5A5A at +0x0A |
| +0x0C | u32 | response cookie |
| +0x10 | u32 | WAL cfg-event id = 1 |
| +0x14 | u16 | pad |
| +0x16 | u16 | data_len = 4 + body_len |
| +0x18 | u32 | sequence counter |
| +0x1C | u16 | WAL command |
| +0x1E | u16 | body_len |
| +0x20 | body | body[body_len] |

total = 32 + body_len.

## T5. G0 id-1 config-event envelope (static-dispatch carrier, default)

| off | width | field |
| ---: | ---: | --- |
| +0x00 | 12 B | HCC header: group 0; hdr[1] dst<<4|alloc 1; hdr[3] module/chip id; total u16 at +0x04; id 1 at +0x06; tag 0x5A5A at +0x0A |
| +0x0C | 8 B | inherited token[8] (not read by the AP workers) |
| +0x14 | u16 | config key (0x0005 / 0x0127) |
| +0x16 | u16 | body length (firmware reads the low byte) |
| +0x18 | body | body[body_len] |
| +0x18+len | 4 B | inherited pad 0xFF |

total = 28 + body_len.  Firmware group-0 id-1 handler CA 0x43D58: key =
u16 at hdr+0x14, body = frame+0x18, len byte = frame+0x16, worker from table A.

## T6. Other bodies

| body | len | layout |
| --- | ---: | --- |
| set SSID | 36 | [0] = ssid_len; [1..2] pad; [3..35] ssid (worker copies [3..] into VAP ctx+0x190 -> +0x24) |
| auth algs | 4 | u32 nl80211_auth_type, allowlist {0,1,4,8} |
| VAP start | 12 | [0] = 1; [8..11] = cookie (0 default) |

## T7. Firmware config-key table A (CA 0x10C23C, 217 rows)

| row | key | worker CA | note |
| ---: | ---: | ---: | --- |
| 1 | 0x0064 | 0x449E4 | module/VAP add - bench-proven mode-1 base |
| 2 | 0x0065 | 0x46394 | VAP start - bench-proven mode-1 base |
| 18 | 0x0005 | 0x82878 | set SSID |
| 110 | 0x0127 | 0x84DA4 | AP config |
| - | 0x006D | absent | auth has no static key |

## T8. Static vs dynamic carrier

| carrier | dispatch | evidence |
| --- | --- | --- |
| g0 id1 keys 0x0005/0x0127 | static table A via handler 0x43D58 | [V] firmware |
| g2 id0 WAL commands | no static receive-table slot; expected dynamic registration | [U]/[I]; mode-0 bench frames consumed but no firmware effect [V*] |

## T9. Bench readback pack (BAR0 = device CA + 0x6B8000)

| name | address | PASS expectation |
| --- | --- | --- |
| golden page | BAR0 0x829E28 | 0x86807FEF, state 1 |
| exception mask | CA 0x40008 | 0 |
| staging frame | BAR0 0x6F800C | all zero |
| module-table slot 0 | BAR0 0x829458 | nonzero after VAP base |
| base offset u16 | BAR0 0x82A504 | 0 on the validated PD4 boot |
| ctx0 | u32(slot0) + u16(base) | module-0 context CA |
| VAP pointer | ctx0 + 0x6B8000 + 0x190 | nonzero after VAP add |
| ctx0 flags 0x18C/0x18D | ctx0 + 0x6B8000 + 0x18C/0x18D | changed by the AP-config worker |
| VAP +0x10 / +0x14 | u32(VAP ptr) + 0x6B8000 + 0x10 / +0x14 | 100 / 2 after AP config ([I] deref window) |
| VAP +0x64 / +0x138 / +0x13A | u32(VAP ptr) + 0x6B8000 + off | flags / MD fields ([I] deref) |
| g+0xB4 / g+0xB8 | BAR0 0x829EDC / 0x829EE0 | 0 |

## T10. Verification results

| check | result |
| --- | --- |
| host strict compile + selftest | 128/128 PASS (-std=c99 -Wall -Wextra -Werror -pedantic) |
| static RE probes (firmware + 2.5.24 host module) | 63/63 PASS |
| generated frames freshness | tools/gen_frames_md.py --check current |
| kernel-context module build (6.18.60) | 770,304 B sha256 11511980..., zero warnings, 14 haf_ symbols |
| hardware validation | not done - host-only lane |

## T11. Stop rules (bench)

* exception mask CA 0x40008 != 0 or nonzero staging frame at BAR0 0x6F800C;
* golden page/state or g+0xB4/+0xB8 change;
* any posted frame not consumed inside the provider's bounded poll
  (h2d_status refused/timeout increments);
* dmesg fault or the H2D lane closing;
* never re-send in the same boot; power-cycle and analyse offline;
* never write CA 0x400392F0, never post mailbox id 5, never send group 1/4.
