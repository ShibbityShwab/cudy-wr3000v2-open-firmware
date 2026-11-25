# wifi-5ghz-vap-msg - evidence tables (T1-T9)

All host-module offsets are link-time section offsets of the pinned local
2.5.24 vendor module (`hi5622v100_wifi.ko`, not stripped; local artifact, not
committed). Firmware CPU address (CA) = file offset + 0x40000. No vendor byte,
object or firmware word is reproduced here - only derived field maps and
addresses. Reproduce the host decode with the OpenWrt
`arm-openwrt-linux-objdump -dr --start-address=... --stop-address=...`.

## T1. Decoded vendor functions `[V]`

| function | offset | size | role |
| --- | ---: | ---: | --- |
| `hmac_config_add_user` | 0x8f0b4 | 0x288 | cfg80211 user-add entry: validates hmac_dev, resolves chip, calls hmac_user_add, fills info, sends id7 then id8 |
| `hmac_user_add` | 0x8e91c | 0x62c | builds + sends g0 id7 (168 B payload) |
| `hmac_user_add_notify_alg` | 0x8e40c | 0x1c4 | builds + sends g0 id8 (168 B payload) |
| `hmac_add_user_init` | 0x8dbd8 | - | allocates the host user object and its rate handles |
| `hmac_config_add_user_set_user_info` | 0x8e1cc | 0x240 | VAP/band-derived user info (protocol mode, qos, tx depth) |
| `hmac_config_add_user_set_ht_hdl` | 0x8dc68 | 0xf8 | HT handle setup |
| `hmac_config_add_user_init_he_rate` | 0x8dd60 | 0x5c | HE rate init |
| `hmac_config_add_vap` | 0x15c0e8 | 0x2f0 | VAP create entry (host object; band triple checked) |
| `hmac_config_add_vap_check` | 0x15b900 | 0x1c0 | validates band type/id/chip of the VAP parameters |
| `hmac_config_add_vap_init` | 0x15bac0 | 0x384 | allocates host VAP, writes vap index, creates its users |
| `hmac_vap_add_multi_user` | 0x15b53c | 0x1d4 | creates the VAP's users (AP case) in one call |
| `hmac_config_add_vap_sync_all` | 0x15be44 | 0x2a4 | wire carrier for the VAP create |
| `hmac_config_check_band_type` | 0x15b870 | - | firmware-side band triple check (same triple) |
| `wal_cfg80211_add_virtual_intf` | 0x103878 | 0x34 | **stub: log + `mvn r0,#10` = -EOPNOTSUPP** |
| `wal_cfg80211_start_ap` | 0x10342c | 0x324 | AP-start: band `dev_priv[0]`, channel->ssid->auth->ap_cfg->write |
| `wal_cfg80211_fill_beacon_param` | 0x10156c | 0x338 | 156-byte AP-config body, band-gated HT/VHT hints |
| `wal_cfg80211_start_ap_write_msg` | 0x103340 | - | writes `{u16 cmd; u16 len; body}` cfg event |
| `wal_send_set_ssid_event` | 0x1280ac | - | set-SSID event (band as call operand) |
| `wal_cfg80211_set_auth_mode` | 0x10137c | - | auth event |

## T2. g0 id7 `hmac_user_add` - 12 B header + 168 B payload = 180 B `[V]`

| msg offset | width | field | source in `hmac_user_add` |
| ---: | ---: | --- | --- |
| +0x00 | u8 | flags/alloc nibble | low nibble cleared by the sender (allocator owns it) |
| +0x01 | u8 | high nibble = hmac_dev[2] (dst), low preserved | `bfi` |
| +0x02 | u8 | hmac_dev[1] | `strb` |
| +0x03 | u8 | **chip id** = hmac_dev[0] (0 for both bands) | `strb` |
| +0x06 | u8 | **message id = 0x07** | `mov r1,#7` |
| +0x07 | u8 | 0 | - |
| +0x08 | u8 | 0 | - |
| +0x0c | u16 | payload +0x00 **user index** | `user+0xa84` |
| +0x0e..13 | u8[6] | payload +0x02 **MAC** | 6 x `strb` |
| +0x9a | u8 | payload +0x8e **user type** (2 = normal) | `sl` |
| +0xb0 | s8 | payload +0xa4 **RSSI** (+100 default) | `0x64` / scanned BSS byte |
| +0x0a..0b | u16 | HCC tag 0x5a5a | transport layer (unchanged) |

No band, channel, rate, HT/VHT or phy-mode field: the band is resolved from
the host VAP (`hmac_band_get_by_vap_id`) inside `hmac_user_add`.

## T3. g0 id8 `hmac_user_add_notify_alg` - 12 B header + 168 B payload `[V]`

| payload offset | width | field |
| ---: | ---: | --- |
| +0x00 | u16 | user index |
| +0x08 | u16 | alg / rate-config value (`hmac_dev+0x3e0`) |
| +0x10 | 24 B | VHT capability block (`mac_user_get_vht_hdl`) |
| +0x28 | 44 B | HT capability block (`mac_user_get_ht_hdl`) |
| +0x54 | 58 B | HE capability block (`mac_user_get_he_hdl`) |
| +0xa5 | u8 | NSS (`user+0xaa6`) |
| +0x06 | u8 | message id = **0x08**; header [1]/[2]/[3] from hmac_dev[2]/[1]/[0] |

## T4. Vendor band model `[V]`

| item | value |
| --- | --- |
| logical bands | 2, ids 0 and 1 |
| chip that owns the band objects | chip 0 only (`hmac_band_get_by_id` rejects chip != 0) |
| band of a user/VAP | stored in the VAP object; user add messages carry no band |
| VAP-create parameter triple | `params+0x11` band type, `params+0x12` band id, `params+0x13` chip (checked by `hmac_config_check_band_type`) |
| cfg80211 add-interface | stub, returns -EOPNOTSUPP; no wire message |
| AP start path | 0x0124 -> 0x0005 -> 0x006D -> 0x0127 -> 0x0065, band = `dev_priv[0]` |

## T5. AP-start band-1 fields `[V]` (re-check of `wifi-5ghz` lane)

| step | carrier | size | band-1 bytes |
| --- | --- | ---: | --- |
| channel config | WAL 0x0124 / g0 id1 key 0x0124 | 4 | `[0]=1` band, `[1]`=vap byte `[I]`, `[2]=ch`, `[3]=0` |
| set SSID | WAL 0x0005 | 36 | band is a call operand, not a body byte |
| auth | WAL 0x006D | 4 | u32 auth type, band-free |
| AP config | WAL 0x0127 | 156 | band-gated HT/VHT hints +0x8a..+0x8d, phy mode +0x8e, start +0x93 |
| VAP start | WAL 0x0065 | 12 | start flag, band-free |

## T6. Our 5 GHz module vs the vendor (band-1 AP) `[V]`/`[I]`

| message | vendor | ours (57-param ready module) | status |
| --- | --- | --- | --- |
| g0 id7 user_add | not sent from add_interface; sent (if at all) after the VAP create | sent from `.add_virtual_intf` before any VAP exists | **order defect - this lane's fix** |
| g0 id8 notify | same order, real VAP band caps | immediately after id7, caps zero | same order defect; caps unchanged (2.4-proven) |
| vap_add | band id +0x12, chip +0x13=0 | data[0x12]=band, data[0x13]=0 | match |
| ap_chan | 0x0124 body[0]=band | band from chandef | match |
| ap_cfg | +0x8a..+0x8e band-gated | band from chandef | match |
| vap_start | after ap_cfg | after ap_cfg | match |

## T7. Patch contract (`vap5g` module parameter) `[V]` host-tested

| input | `vap5g=0` | `vap5g=1` |
| --- | --- | --- |
| wiphy bands = 1 | legacy (no defer) | legacy (no defer) |
| wiphy bands = 2, iftype AP | legacy (no defer) | **defer user add; post it after vap_add** |
| wiphy bands = 2, iftype != AP | legacy | legacy |
| `vap5g > 1` | - | -EINVAL, no frame |
| deferred user_add fails | - | start_ap aborts before any AP frame |

Order with `vap5g=1` (band-1 AP): `vap_add(0x0064, band=1)` ->
`user_add(id7)` -> `user_add_notify(id8)` -> `ap_chan(0x0124, band=1)` ->
`ap_ssid` -> `ap_cfg` -> `ap_vap_start`. The deferred user add uses the
legacy builder bytes (chip id 0, uid=vap slot) - the band reaches the firmware
through the preceding VAP create.

## T8. Host-test matrix (evidence/host-tests.txt)

| case | expectation |
| --- | --- |
| legacy mode | `defer=0`, `post_after=0`, order = legacy |
| `vap5g=1`, 2 bands, AP | `defer=1`, `post_after=1`, order = vendor |
| `vap5g=1`, 1 band, AP | legacy |
| `vap5g=1`, 2 bands, managed/STA/monitor | legacy |
| `vap5g=2` | -EINVAL (fail closed) |
| unknown iftype/band count | legacy (never guess) |
| g0 id7/id8 bytes at band 1 | vendor layout: hdr[3]=chip 0, body offsets as T2/T3 |
| vap_add bytes at band 1 | data[0x12]=1, data[0x13]=0 (chip) |

## T9. Artifact contract (built by this lane)

| item | expected |
| --- | --- |
| module params | 58 (57 ready-module + `vap5g`) |
| `vap5g` default | 0 (fail closed) |
| feature strings | vendor-order deferral + post-after-vap-add lines |
| build | 0 warnings / 0 errors against linux-6.18.60, undefsym clean |
| debug strip | `.text` byte-identical before/after `strip --strip-debug` |
