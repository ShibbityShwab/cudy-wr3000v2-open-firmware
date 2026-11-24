# wifi-beacon-static — evidence tables (T1–T9)

All addresses are derived constants from the pinned local artifacts (see
REPORT.md §1).  Firmware CPU address (CA) = file offset + 0x40000; host-module
`.text` addresses are relocatable section offsets.  No vendor byte is
reproduced.  Reproduce with `tools/beacon_re.py`.

## T1. Vendor host AP-start anchors (both versions) [V]

| function | 2.5.24 | 2.4.15 | role |
| --- | ---: | ---: | --- |
| `wal_cfg80211_start_ap` | 0x103634 | 0x10342c | cfg80211 .start_ap: channel → SSID → auth → write_msg |
| channel helper (unnamed static, `wal_cfg80211_set_channel_info`) | 0x102e84 (call @0x10368c) | 0x102c7c (call @0x103484) | builds + sends the 0x0124 channel config |
| `wal_send_set_ssid_event` | 0x1284ec | 0x1280ac | SSID event (cmd 0x0005) |
| `wal_cfg80211_set_auth_mode` | 0x101584 | 0x10137c | auth event (cmd 0x006D) |
| `wal_cfg80211_start_ap_write_msg` | 0x103548 | 0x103340 | AP-config cfg event (0x0127) then VAP start |
| ↳ `wal_send_cfg_event` call | 0x103594 | 0x10338c | AP config (body 156 B) |
| ↳ `wal_start_vap` call | 0x1035a4 | 0x10339c | VAP start (body 12 B) |

## T2. Vendor AP-start sequence (wire order) [V]

| # | message | carrier | body | note |
| ---: | --- | --- | ---: | --- |
| 1 | module channel config (cmd **0x0124**) | WAL g2 cfg event / g0 id1 key 0x0124 | 4 B | FIRST call in start_ap; our clean-room path never sends it |
| 2 | set SSID (cmd 0x0005) | g0 id1 key 0x0005 | 36 B | sent by our AP path |
| 3 | auth (cmd 0x006D) | WAL g2 only (no table-A key) | 4 B | omitted by the g0 carrier; RSN IE covers WPA2 |
| 4 | AP config (cmd 0x0127) | g0 id1 key 0x0127 | 156 B | sent by our AP path |
| 5 | VAP start (cmd 0x0065) | g0 id1 key 0x0065 | 12 B | vendor sends it AFTER the AP config; our path posts it BEFORE |

## T3. Channel-config body (4 B) [V]/[I]

| off | host source (vendor) | firmware worker CA 0x841B8 store | meaning |
| ---: | --- | --- | --- |
| +0 | `chandef.chan->band` (u32, ldrb) | module ctx +0x11 | band 0 = 2.4 GHz, 1 = 5 GHz [V] |
| +1 | vendor `mac_vap+2` | module ctx +0x12 | vendor per-VAP field; clean-room sends 0 (single VAP) [I] |
| +2 | `chandef.chan->hw_value` | module ctx +0x10 | channel number [V] |
| +3 | not written | unused | — |

Worker: (band, channel) validated by helper CA 0x10172C → ctx +0x13; then
CA 0xFDCE4 (PHY channel/band program), 0xF4FD8, 0x40684, optional 0x97008.

## T4. Firmware config-key table A rows for the AP family [V]

Table A CA 0x10C23C, 217 × 8-byte rows `{u16 key; u16 pad; u32 worker|thumb}`:

| key | worker CA | role |
| ---: | ---: | --- |
| 0x0064 | 0x449e4 | module/VAP add |
| 0x0065 | 0x46394 | VAP start |
| 0x0005 | 0x82878 | set SSID |
| 0x0124 | 0x841b8 | module channel config |
| 0x0127 | 0x84da4 | AP config |

## T5. Firmware worker field maps [V] (probe F2–F4)

| worker | body → context | following calls |
| --- | --- | --- |
| channel CA 0x841B8 | body[2]→ctx+0x10, body[0]→ctx+0x11, body[1]→ctx+0x12, validate→ctx+0x13 | 0xFDCE4, 0xF4FD8, 0x40684, 0x97008 |
| AP config CA 0x84DA4 | VAP ctx +0x10 interval / +0x14 DTIM / IE slots / HT-VHT-MD (wifi-ap-start map) | 0xD41AC → 0x94518 → 0x961AE (VAP/MAC start path) |
| VAP start CA 0x46394 | body[1]→protocol arg, body[2]→ctx+0x11, body[3]→ctx+0x12, body[4..7]→ctx+0x444 | 0xD4048, 0x94518, 0x961AE, VAP state machine + peer table (0xBF128 family) |

## T6. Firmware beacon ownership [V]

| observation | evidence |
| --- | --- |
| TBTT ISRs in firmware image | `smac_tbtt_2g_isr`, `smac_tbtt_5g_isr` (probe F5) |
| beacon-TX suspend/resume handlers in firmware image | `smac_msg_proc_beacon_tx_suspend_req`, `..._resume_req` (probe F5) |
| no fixed 802.11 beacon template in the image | FC 0x0080 + broadcast DA absent (probe F6); frame assembled at runtime |
| host AP path carries no beacon frame | start_ap/change_beacon carry the 156-B AP-config body only; host "beacon" symbols are received-beacon report code |

⇒ beacons are firmware-generated and firmware-transmitted after a correct VAP
start; there is no host beacon-template/bcn-ring message to add.

## T7. Read-only readback pack for the beacon ladder [V*]

All reads go through the existing `hi5622_fw_ram_read()` path (no new interface).

| name | CPU address | expectation |
| --- | ---: | --- |
| module-table slot 0 | 0x175458 (BAR0 0x829458) | nonzero after the VAP base |
| module-table base offset | 0x17a504 (BAR0 0x82a504) | 0 on the validated PD4 boot |
| module ctx CA | slot0 value + base value | base of the AP-start worker context |
| module channel ctx | module ctx CA + 0x10 (u32) | packed bytes: channel / band / field / valid — new dmesg line in the beacon path |
| firmware VAP object | module ctx CA + 0x190 | nonzero after user_add (used by the existing stale-VAP guard) |

## T8. Patch parameter contract [V]

| parameter | default | meaning |
| --- | ---: | --- |
| `beacon_static` | **0** | 0 = bench-proven legacy path unchanged; 1 = vendor order (channel config + VAP start after AP config) |
| `beacon_vap_start` | 1 | beacon_static rung isolation: 1 = post VAP start after AP config; 0 = channel config only (B1 control) |
| `ap_start_mode` (existing) | 0 | carrier: 0 = g0 id1 keys, 1 = WAL g2 (its own sequence already ends with VAP start) |
| `allow_frames` (existing) | image policy | every new frame still passes the fail-closed post gate |

## T9. Host verification counts [V]

| check | count | evidence |
| --- | ---: | --- |
| `beacon_selftest` checks | 65/65 PASS | evidence/host-selftest.txt |
| `beacon_re.py` probes (firmware + 2.5.24) | 33/33 PASS | evidence/beacon-re-2.5.24.txt |
| `beacon_re.py` probes (firmware + 2.4.15) | 33/33 PASS | evidence/beacon-re-2.4.15.txt |
| module build warnings/errors | 0 | evidence/module-build.txt |
| cfg80211 imports | 6 (unchanged contract) | evidence/module-build.txt |
## T10. AP data path (assoc/auth side) is a separate, still-open host gap [V]

| observation | evidence |
| --- | --- |
| vendor host implements the AP-side management exchange in software | `hmac_ap_up_rx_mgmt`, `hmac_mgmt_encap_auth_rsp`, `hmac_mgmt_encap_asoc_rsp_ap` symbols in the 2.4.15/2.5.24 host module |
| AP MLME frames leave the host through the cfg80211 mgmt-tx op | `wal_cfg80211_mgmt_tx` → `wal_cfg80211_mgmt_tx_pre_proc` / `..._try`; status via `oal_cfg80211_mgmt_tx_status` |
| clean-room driver mgmt_tx is fail-closed today | `hi5622_cfg80211_ops.c`: `.mgmt_tx` returns `-EOPNOTSUPP` (image-polish lane `mgmt_diag`) |
| RX of client frames | 0 captures on the bench (wifi-rx-static lane scope) |

⇒ beacons (this lane) and client assoc/auth/data (mgmt-tx message family + RX
lane) are independent blockers for a usable AP.
