# D2H event vocabulary, groups 0-3: promoted to VERIFIED (phase 38, 2026-10-04)

Prior state: `opensource/docs/phase25/full-event-vocabulary.md` recorded the six message tables and
their `id -> handler` names (79 slots) by reading `.data` and resolving each entry's relocation.
This report promotes that record, for **groups 0-3**, from record-only to **byte-verified**.

- Binary: `opensource/build/tmp/hi5622v100_wifi.ko` (md5 `4737fcb21a1a2262a96f84d780ad8b35`).
- Method: ELF `.symtab` and `.rel.data` parsed with pyelftools; the `.data` image read at its file
  offset `0x243ab0`; the three registering init functions disassembled with capstone in ARM mode.
- Entry layout (confirmed byte-wise): 12-byte stride, `{u32 id @ +0, u32 handler @ +4, u32 pad @ +8}`;
  each handler word `@ +4` carries an `R_ARM_ABS32` (type 2) relocation whose stored addend is `0`.

Status vocabulary used per slot:

- **byte-verified (local)** - the handler word has an `R_ARM_ABS32` reloc to a defined `STT_FUNC`
  symbol whose `st_shndx` is `.text`; the id word reads back the id named in phase 25.
- **byte-verified (import)** - the handler word has an `R_ARM_ABS32` reloc to an *undefined* global
  symbol (`SHN_UNDEF`), i.e. a function imported from another module and bound at load time.
- **absent (null)** - the handler word is `0x00000000` and there is **no** relocation: the slot names
  no symbol at all.

Every entry's id->handler pair also matches the phase-25 record exactly; no slot disagrees and no
phase-25 id is missing from its table.

## Summary

| group | side | table | `.data` offset | file offset | entries | verified | absent |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Group 0 | core | hdpp `tab_core` | `0x40` | `0x243af0` | 7 | 7 | 0 |
| Group 1 | chip | hmac table (#1) | `0x584` | `0x244034` | 28 | 28 | 0 |
| Group 2 | chip | hmac table (#2) | `0x6d4` | `0x244184` | 12 | 10 | 2 |
| Group 2 | chip | wal table (#2) | `0x1c74` | `0x245724` | 19 | 19 | 0 |
| Group 3 | chip | hdpp `tab_chip` | `0x4` | `0x243ab4` | 5 | 5 | 0 |
| Group 3 | chip | hmac table (#3) | `0x764` | `0x244214` | 8 | 8 | 0 |
| **total** | | | | | **79** | **77** | **2** |

Reloc type codes used by the registering code: `R_ARM_CALL` = 28, `R_ARM_MOVW_ABS_NC` = 43,
`R_ARM_MOVT_ABS` = 44, `R_ARM_ABS32` = 2 (all 77 handler relocations).

## Group 0 - core: hdpp `tab_core`

- table address: `.data+0x40` (file offset `0x243af0`)
- entry count: **7**
- registered by: `hdpp_main_init` @ `0x01360`-`0x0136c`: `add r1, r4, #0x40` (r4 = `.LANCHOR0` = `.data+0x0`), `mov r0, r5` (=0), `mov r2, #7`, `bl hcc_msg_register_tab_core`

| idx | slot `.data` off | slot file off | id | handler @ | handler symbol | symbol class | sym `st_value` | status |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | `0x40` | `0x243af0` | 0 | `0x44` | `hmac_tx_complete_event_handle` | `STT_FUNC`, `.text` | `0x1c470` | byte-verified (local) |
| 1 | `0x4c` | `0x243afc` | 1 | `0x50` | `hmac_tx_event_process` | `STT_FUNC`, `.text` | `0x17f50` | byte-verified (local) |
| 2 | `0x58` | `0x243b08` | 2 | `0x5c` | `hmac_rx_process_data_event` | `STT_FUNC`, `.text` | `0x119e8` | byte-verified (local) |
| 3 | `0x64` | `0x243b14` | 4 | `0x68` | `hmac_tx_complete_notify_other_core_event_handle` | `STT_FUNC`, `.text` | `0x1bbb4` | byte-verified (local) |
| 4 | `0x70` | `0x243b20` | 3 | `0x74` | `hmac_rx_process_data_event` | `STT_FUNC`, `.text` | `0x119e8` | byte-verified (local) |
| 5 | `0x7c` | `0x243b2c` | 7 | `0x80` | `hmac_mac_exception_proc` | `STT_FUNC`, `.text` | `0x3fa44` | byte-verified (local) |
| 6 | `0x88` | `0x243b38` | 8 | `0x8c` | `hmac_ba_timeout_proc` | `STT_FUNC`, `.text` | `0xdcc88` | byte-verified (local) |

## Group 1 - chip: hmac table (#1)

- table address: `.data+0x584` (file offset `0x244034`)
- entry count: **28**
- registered by: `hmac_main_init` @ `0x03f768`-`0x03f778`: `mov r2, #0x1c` (28), `mov r0, r7` (=1), `movw/movt r1` = `.LANCHOR1` = `.data+0x584`, `bl hcc_msg_register_tab_chip`

| idx | slot `.data` off | slot file off | id | handler @ | handler symbol | symbol class | sym `st_value` | status |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | `0x584` | `0x244034` | 0 | `0x588` | `hmac_sdt_up_reg_val` | `STT_FUNC`, `.text` | `0x3eb88` | byte-verified (local) |
| 1 | `0x590` | `0x244040` | 1 | `0x594` | `hmac_create_ba_event` | `STT_FUNC`, `.text` | `0x3ecf4` | byte-verified (local) |
| 2 | `0x59c` | `0x24404c` | 2 | `0x5a0` | `hmac_del_ba_event` | `STT_FUNC`, `.text` | `0x3eeb8` | byte-verified (local) |
| 3 | `0x5a8` | `0x244058` | 3 | `0x5ac` | `hmac_event_config_syn` | `STT_FUNC`, `.text` | `0x6b19c` | byte-verified (local) |
| 4 | `0x5b4` | `0x244064` | 5 | `0x5b8` | `hmac_syn_info_event` | `STT_FUNC`, `.text` | `0x3e860` | byte-verified (local) |
| 5 | `0x5c0` | `0x244070` | 9 | `0x5c4` | `hmac_bandwidth_info_syn_event` | `STT_FUNC`, `.text` | `0x3e9f0` | byte-verified (local) |
| 6 | `0x5cc` | `0x24407c` | 10 | `0x5d0` | `hmac_protection_info_syn_event` | `STT_FUNC`, `.text` | `0x3f0fc` | byte-verified (local) |
| 7 | `0x5d8` | `0x244088` | 11 | `0x5dc` | `hmac_ch_status_info_syn_event` | `STT_FUNC`, `.text` | `0x3e650` | byte-verified (local) |
| 8 | `0x5e4` | `0x244094` | 13 | `0x5e8` | `hmac_rx_process_mgmt_event` | `STT_FUNC`, `.text` | `0x49fe0` | byte-verified (local) |
| 9 | `0x5f0` | `0x2440a0` | 14 | `0x5f4` | `hmac_mgmt_rx_delba_event` | `STT_FUNC`, `.text` | `0x49230` | byte-verified (local) |
| 10 | `0x5fc` | `0x2440ac` | 15 | `0x600` | `hmac_scan_proc_scanned_bss` | `STT_FUNC`, `.text` | `0x9e00c` | byte-verified (local) |
| 11 | `0x608` | `0x2440b8` | 16 | `0x60c` | `hmac_scan_proc_scan_comp_event` | `STT_FUNC`, `.text` | `0x9a0ec` | byte-verified (local) |
| 12 | `0x614` | `0x2440c4` | 17 | `0x618` | `hmac_scan_process_chan_result_event` | `STT_FUNC`, `.text` | `0x9eb54` | byte-verified (local) |
| 13 | `0x620` | `0x2440d0` | 18 | `0x624` | `hmac_event_acs_response` | `STT_FUNC`, `.text` | `0x3e5b0` | byte-verified (local) |
| 14 | `0x62c` | `0x2440dc` | 20 | `0x630` | `hmac_mgmt_send_disasoc_deauth_event` | `STT_FUNC`, `.text` | `0x4a5bc` | byte-verified (local) |
| 15 | `0x638` | `0x2440e8` | 21 | `0x63c` | `hmac_mgmt_send_disasoc_deauth_event` | `STT_FUNC`, `.text` | `0x4a5bc` | byte-verified (local) |
| 16 | `0x644` | `0x2440f4` | 22 | `0x648` | `hmac_chan_switch_to_new_chan_complete` | `STT_FUNC`, `.text` | `0x48bb4` | byte-verified (local) |
| 17 | `0x650` | `0x244100` | 24 | `0x654` | `hmac_mgmt_tbtt_event` | `STT_FUNC`, `.text` | `0x4a3b4` | byte-verified (local) |
| 18 | `0x65c` | `0x24410c` | 25 | `0x660` | `hmac_dfs_radar_detect_event` | `STT_FUNC`, `.text` | `0xaca10` | byte-verified (local) |
| 19 | `0x668` | `0x244118` | 26 | `0x66c` | `hmac_proc_disasoc_misc_event` | `STT_FUNC`, `.text` | `0x4a878` | byte-verified (local) |
| 20 | `0x674` | `0x244124` | 29 | `0x678` | `hmac_acs_process_rescan_event` | `STT_FUNC`, `.text` | `0xb553c` | byte-verified (local) |
| 21 | `0x680` | `0x244130` | 31 | `0x684` | `hmac_csi_complete` | `STT_FUNC`, `.text` | `0xb9ef8` | byte-verified (local) |
| 22 | `0x68c` | `0x24413c` | 34 | `0x690` | `hmac_sr_bss_color_change_event` | `STT_FUNC`, `.text` | `0x95fac` | byte-verified (local) |
| 23 | `0x698` | `0x244148` | 35 | `0x69c` | `hmac_sr_syn_parameter_event` | `STT_FUNC`, `.text` | `0x96294` | byte-verified (local) |
| 24 | `0x6a4` | `0x244154` | 36 | `0x6a8` | `hmac_chr_report_msg` | `STT_FUNC`, `.text` | `0x49438` | byte-verified (local) |
| 25 | `0x6b0` | `0x244160` | 38 | `0x6b4` | `hmac_config_set_security_port_event` | `STT_FUNC`, `.text` | `0xa3dfc` | byte-verified (local) |
| 26 | `0x6bc` | `0x24416c` | 39 | `0x6c0` | `hmac_update_fbt_scan_result` | `STT_FUNC`, `.text` | `0xa9ccc` | byte-verified (local) |
| 27 | `0x6c8` | `0x244178` | 37 | `0x6cc` | `hmac_pfm_detect_process_event` | `STT_FUNC`, `.text` | `0xf6f0c` | byte-verified (local) |

## Group 2 - chip: hmac table (#2)

- table address: `.data+0x6d4` (file offset `0x244184`)
- entry count: **12**
- registered by: `hmac_main_init` @ `0x03f77c`-`0x03f788`: `ldr r1, [pc, #0x1e4]` = `.data+0x6d4` (R_ARM_ABS32 against the `.data` section symbol, addend `0x6d4`), `mov r2, #0xc` (12), `mov r0, #2`, `bl hcc_msg_register_tab_chip`

| idx | slot `.data` off | slot file off | id | handler @ | handler symbol | symbol class | sym `st_value` | status |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | `0x6d4` | `0x244184` | 22 | `0x6d8` | *none* | none | - | absent (null) |
| 1 | `0x6e0` | `0x244190` | 23 | `0x6e4` | *none* | none | - | absent (null) |
| 2 | `0x6ec` | `0x24419c` | 24 | `0x6f0` | `hmac_spectral_scan_complete` | `STT_FUNC`, `.text` | `0x89e70` | byte-verified (local) |
| 3 | `0x6f8` | `0x2441a8` | 25 | `0x6fc` | `hmac_phy_event_complete` | `STT_FUNC`, `.text` | `0x8bd4c` | byte-verified (local) |
| 4 | `0x704` | `0x2441b4` | 26 | `0x708` | `hmac_phy_event_complete` | `STT_FUNC`, `.text` | `0x8bd4c` | byte-verified (local) |
| 5 | `0x710` | `0x2441c0` | 27 | `0x714` | `hmac_mgmt_deauth_msg_process` | `STT_FUNC`, `.text` | `0x43788` | byte-verified (local) |
| 6 | `0x71c` | `0x2441cc` | 28 | `0x720` | `hmac_ba_action_msg_process` | `STT_FUNC`, `.text` | `0xaf48c` | byte-verified (local) |
| 7 | `0x728` | `0x2441d8` | 29 | `0x72c` | `hcc_timer_process` | `STT_NOTYPE`, `SHN_UNDEF` (import) | - | byte-verified (import) |
| 8 | `0x734` | `0x2441e4` | 33 | `0x738` | `hmac_mgmt_read_error_msg_process` | `STT_FUNC`, `.text` | `0x4abe4` | byte-verified (local) |
| 9 | `0x740` | `0x2441f0` | 34 | `0x744` | `hmac_mgmt_send_deauth_frame_process` | `STT_FUNC`, `.text` | `0x410d0` | byte-verified (local) |
| 10 | `0x74c` | `0x2441fc` | 35 | `0x750` | `hmac_del_user_msg_process` | `STT_FUNC`, `.text` | `0x413f8` | byte-verified (local) |
| 11 | `0x758` | `0x244208` | 36 | `0x75c` | `hmac_rx_data_send_disasoc_frame_process` | `STT_FUNC`, `.text` | `0x41bac` | byte-verified (local) |

## Group 2 - chip: wal table (#2)

- table address: `.data+0x1c74` (file offset `0x245724`)
- entry count: **19**
- registered by: `wal_main_init` @ `0x123cd0`-`0x123ce0`: `mov r2, #0x13` (19), `movw/movt r1` = `.LANCHOR1` = `.data+0x1c74`, `mov r0, #2`, `bl hcc_msg_register_tab_chip`

| idx | slot `.data` off | slot file off | id | handler @ | handler symbol | symbol class | sym `st_value` | status |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | `0x1c74` | `0x245724` | 0 | `0x1c78` | `wal_config_process_pkt` | `STT_FUNC`, `.text` | `0xfcd9c` | byte-verified (local) |
| 1 | `0x1c80` | `0x245730` | 1 | `0x1c84` | `wal_acs_netlink_recv_handle` | `STT_FUNC`, `.text` | `0x128fbc` | byte-verified (local) |
| 2 | `0x1c8c` | `0x24573c` | 2 | `0x1c90` | `wal_scan_comp_proc_sta` | `STT_FUNC`, `.text` | `0x125714` | byte-verified (local) |
| 3 | `0x1c98` | `0x245748` | 3 | `0x1c9c` | `wal_asoc_comp_proc_sta` | `STT_FUNC`, `.text` | `0x125b6c` | byte-verified (local) |
| 4 | `0x1ca4` | `0x245754` | 4 | `0x1ca8` | `wal_disasoc_comp_proc_sta` | `STT_FUNC`, `.text` | `0x125e3c` | byte-verified (local) |
| 5 | `0x1cb0` | `0x245760` | 5 | `0x1cb4` | `wal_connect_new_sta_proc_ap` | `STT_FUNC`, `.text` | `0x125f60` | byte-verified (local) |
| 6 | `0x1cbc` | `0x24576c` | 6 | `0x1cc0` | `wal_disconnect_sta_proc_ap` | `STT_FUNC`, `.text` | `0x1260d8` | byte-verified (local) |
| 7 | `0x1cc8` | `0x245778` | 7 | `0x1ccc` | `wal_mic_failure_proc` | `STT_FUNC`, `.text` | `0x126248` | byte-verified (local) |
| 8 | `0x1cd4` | `0x245784` | 8 | `0x1cd8` | `wal_acs_response_event_handler` | `STT_FUNC`, `.text` | `0x129288` | byte-verified (local) |
| 9 | `0x1ce0` | `0x245790` | 9 | `0x1ce4` | `wal_send_mgmt_to_host` | `STT_FUNC`, `.text` | `0x1265d4` | byte-verified (local) |
| 10 | `0x1cec` | `0x24579c` | 12 | `0x1cf0` | `wal_cfg80211_init_evt_handle` | `STT_FUNC`, `.text` | `0x107058` | byte-verified (local) |
| 11 | `0x1cf8` | `0x2457a8` | 13 | `0x1cfc` | `wal_cfg80211_mgmt_tx_status` | `STT_FUNC`, `.text` | `0x106034` | byte-verified (local) |
| 12 | `0x1d04` | `0x2457b4` | 20 | `0x1d08` | `wal_cfg80211_cac_report` | `STT_FUNC`, `.text` | `0x10767c` | byte-verified (local) |
| 13 | `0x1d10` | `0x2457c0` | 21 | `0x1d14` | `wal_receive_all_sta_rssi_proc` | `STT_FUNC`, `.text` | `0x126398` | byte-verified (local) |
| 14 | `0x1d1c` | `0x2457cc` | 30 | `0x1d20` | `wal_report_external_auth_req` | `STT_FUNC`, `.text` | `0x12680c` | byte-verified (local) |
| 15 | `0x1d28` | `0x2457d8` | 31 | `0x1d2c` | `wal_process_packet_xmit` | `STT_FUNC`, `.text` | `0xff774` | byte-verified (local) |
| 16 | `0x1d34` | `0x2457e4` | 32 | `0x1d38` | `wal_dfr_power_down_dev` | `STT_FUNC`, `.text` | `0x129614` | byte-verified (local) |
| 17 | `0x1d40` | `0x2457f0` | 37 | `0x1d44` | `wal_multiap_report_proc` | `STT_FUNC`, `.text` | `0x126a4c` | byte-verified (local) |
| 18 | `0x1d4c` | `0x2457fc` | 38 | `0x1d50` | `wal_hiwifi_report_proc` | `STT_FUNC`, `.text` | `0x126b50` | byte-verified (local) |

## Group 3 - chip: hdpp `tab_chip`

- table address: `.data+0x4` (file offset `0x243ab4`)
- entry count: **5**
- registered by: `hdpp_main_init` @ `0x01350`-`0x0135c`: `add r1, r4, #4` (r4 = `.LANCHOR0` = `.data+0x0`), `mov r2, #5`, `mov r0, #3`, `bl hcc_msg_register_tab_chip`

| idx | slot `.data` off | slot file off | id | handler @ | handler symbol | symbol class | sym `st_value` | status |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | `0x4` | `0x243ab4` | 0 | `0x8` | `hmac_voice_aggr_event` | `STT_FUNC`, `.text` | `0x11ec` | byte-verified (local) |
| 1 | `0x10` | `0x243ac0` | 4 | `0x14` | `hmac_device_wow_data_report` | `STT_FUNC`, `.text` | `0x11d58` | byte-verified (local) |
| 2 | `0x1c` | `0x243acc` | 5 | `0x20` | `hmac_rx_schedule_req` | `STT_FUNC`, `.text` | `0x1b720` | byte-verified (local) |
| 3 | `0x28` | `0x243ad8` | 7 | `0x2c` | `hdpp_stat_save_tx_ppdu_record_process` | `STT_FUNC`, `.text` | `0xcdd4` | byte-verified (local) |
| 4 | `0x34` | `0x243ae4` | 8 | `0x38` | `hdpp_stat_save_rx_ppdu_record_process` | `STT_FUNC`, `.text` | `0xc8cc` | byte-verified (local) |

## Group 3 - chip: hmac table (#3)

- table address: `.data+0x764` (file offset `0x244214`)
- entry count: **8**
- registered by: `hmac_main_init` @ `0x03f78c`-`0x03f798`: `ldr r1, [pc, #0x1d8]` = `.data+0x764`, `mov r2, #8`, `mov r0, #3`, `bl hcc_msg_register_tab_chip`

| idx | slot `.data` off | slot file off | id | handler @ | handler symbol | symbol class | sym `st_value` | status |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | `0x764` | `0x244214` | 9 | `0x768` | `hmac_smps_update_device_capbility` | `STT_FUNC`, `.text` | `0xa70c4` | byte-verified (local) |
| 1 | `0x770` | `0x244220` | 10 | `0x774` | `hmac_pfm_hiex_rx_local_msg` | `STT_FUNC`, `.text` | `0xf703c` | byte-verified (local) |
| 2 | `0x77c` | `0x24422c` | 15 | `0x780` | `hmac_bsd_update_msg` | `STT_FUNC`, `.text` | `0x4b3d4` | byte-verified (local) |
| 3 | `0x788` | `0x244238` | 16 | `0x78c` | `hmac_multiap_report_11v_event` | `STT_FUNC`, `.text` | `0xc674c` | byte-verified (local) |
| 4 | `0x794` | `0x244244` | 17 | `0x798` | `hmac_d2h_pm_event` | `STT_FUNC`, `.text` | `0xb3904` | byte-verified (local) |
| 5 | `0x7a0` | `0x244250` | 18 | `0x7a4` | `hmac_d2h_temp_state_update` | `STT_FUNC`, `.text` | `0x6b040` | byte-verified (local) |
| 6 | `0x7ac` | `0x24425c` | 19 | `0x7b0` | `hmac_receive_beacon_probrsp_to_reprot` | `STT_FUNC`, `.text` | `0x4a1c8` | byte-verified (local) |
| 7 | `0x7b8` | `0x244268` | 20 | `0x7bc` | `hmac_report_beacon_frame` | `STT_FUNC`, `.text` | `0x49ce8` | byte-verified (local) |

## Findings

1. **77 of 79 slots byte-verify, 2 are absent.** The two absent slots are in the group-2 hmac table
   (`.data+0x6d4`, ids **22** and **23**): both handler words are `0x00000000` with no relocation, so
   device->host ids 22/23 are reserved with no handler in this table. That is exactly the pair phase 25
   flagged as `*unnamed*`, now resolved to an explicit null instead of a missing dump.
2. **One handler is an import, not a local function:** group-2 hmac id **29** = `hcc_timer_process`,
   an `SHN_UNDEF`/`STT_NOTYPE` global (symbol index 21321). Its `R_ARM_ABS32` reloc still points at that
   named symbol, so the entry verifies; it is simply bound against the HCC/platform module at load.
   This matches phase 25's note that the message service and the timer service share this table.
3. **Table order is not id order.** Group 1's hmac table keeps ids ...36, **38, 39, 37** in its last
   three slots, and group 0's `tab_core` keeps id **4** (slot 3) before id **3** (slot 4). Phase 25
   listed both tables sorted by id, so its printed row order differs from on-disk order while every
   `id -> handler` pair matches.
4. All six table addresses and counts were re-derived independently from the registering code, not
   assumed: `hdpp_main_init` passes `(3, .data+0x04, 5)` and `(0, .data+0x40, 7)`; `hmac_main_init`
   passes `(1, .data+0x584, 28)`, `(2, .data+0x6d4, 12)`, `(3, .data+0x764, 8)`; `wal_main_init`
   passes `(2, .data+0x1c74, 19)`. These match phase 25's offsets and counts.

## Captured check output

Slot-by-slot resolver output against the binary above (id word, handler relocation target, class):

```
=== Group 0 core hdpp `tab_core` @ .data+0x40 (file 0x243af0) count=7 ===
  idx0  id=0   id@0x40    handler@0x44    R_ARM_ABS32->hmac_tx_complete_event_handle    byte-verified (local STT_FUNC @0x1c470)
  idx1  id=1   id@0x4c    handler@0x50    R_ARM_ABS32->hmac_tx_event_process            byte-verified (local STT_FUNC @0x17f50)
  idx2  id=2   id@0x58    handler@0x5c    R_ARM_ABS32->hmac_rx_process_data_event       byte-verified (local STT_FUNC @0x119e8)
  idx3  id=4   id@0x64    handler@0x68    R_ARM_ABS32->hmac_tx_complete_notify_other_core_event_handle byte-verified (local STT_FUNC @0x1bbb4)
  idx4  id=3   id@0x70    handler@0x74    R_ARM_ABS32->hmac_rx_process_data_event       byte-verified (local STT_FUNC @0x119e8)
  idx5  id=7   id@0x7c    handler@0x80    R_ARM_ABS32->hmac_mac_exception_proc          byte-verified (local STT_FUNC @0x3fa44)
  idx6  id=8   id@0x88    handler@0x8c    R_ARM_ABS32->hmac_ba_timeout_proc             byte-verified (local STT_FUNC @0xdcc88)
=== Group 1 chip hmac table (#1) @ .data+0x584 (file 0x244034) count=28 ===
  idx0  id=0   id@0x584   handler@0x588   R_ARM_ABS32->hmac_sdt_up_reg_val              byte-verified (local STT_FUNC @0x3eb88)
  idx1  id=1   id@0x590   handler@0x594   R_ARM_ABS32->hmac_create_ba_event             byte-verified (local STT_FUNC @0x3ecf4)
  idx2  id=2   id@0x59c   handler@0x5a0   R_ARM_ABS32->hmac_del_ba_event                byte-verified (local STT_FUNC @0x3eeb8)
  idx3  id=3   id@0x5a8   handler@0x5ac   R_ARM_ABS32->hmac_event_config_syn            byte-verified (local STT_FUNC @0x6b19c)
  idx4  id=5   id@0x5b4   handler@0x5b8   R_ARM_ABS32->hmac_syn_info_event              byte-verified (local STT_FUNC @0x3e860)
  idx5  id=9   id@0x5c0   handler@0x5c4   R_ARM_ABS32->hmac_bandwidth_info_syn_event    byte-verified (local STT_FUNC @0x3e9f0)
  idx6  id=10  id@0x5cc   handler@0x5d0   R_ARM_ABS32->hmac_protection_info_syn_event   byte-verified (local STT_FUNC @0x3f0fc)
  idx7  id=11  id@0x5d8   handler@0x5dc   R_ARM_ABS32->hmac_ch_status_info_syn_event    byte-verified (local STT_FUNC @0x3e650)
  idx8  id=13  id@0x5e4   handler@0x5e8   R_ARM_ABS32->hmac_rx_process_mgmt_event       byte-verified (local STT_FUNC @0x49fe0)
  idx9  id=14  id@0x5f0   handler@0x5f4   R_ARM_ABS32->hmac_mgmt_rx_delba_event         byte-verified (local STT_FUNC @0x49230)
  idx10 id=15  id@0x5fc   handler@0x600   R_ARM_ABS32->hmac_scan_proc_scanned_bss       byte-verified (local STT_FUNC @0x9e00c)
  idx11 id=16  id@0x608   handler@0x60c   R_ARM_ABS32->hmac_scan_proc_scan_comp_event   byte-verified (local STT_FUNC @0x9a0ec)
  idx12 id=17  id@0x614   handler@0x618   R_ARM_ABS32->hmac_scan_process_chan_result_event byte-verified (local STT_FUNC @0x9eb54)
  idx13 id=18  id@0x620   handler@0x624   R_ARM_ABS32->hmac_event_acs_response          byte-verified (local STT_FUNC @0x3e5b0)
  idx14 id=20  id@0x62c   handler@0x630   R_ARM_ABS32->hmac_mgmt_send_disasoc_deauth_event byte-verified (local STT_FUNC @0x4a5bc)
  idx15 id=21  id@0x638   handler@0x63c   R_ARM_ABS32->hmac_mgmt_send_disasoc_deauth_event byte-verified (local STT_FUNC @0x4a5bc)
  idx16 id=22  id@0x644   handler@0x648   R_ARM_ABS32->hmac_chan_switch_to_new_chan_complete byte-verified (local STT_FUNC @0x48bb4)
  idx17 id=24  id@0x650   handler@0x654   R_ARM_ABS32->hmac_mgmt_tbtt_event             byte-verified (local STT_FUNC @0x4a3b4)
  idx18 id=25  id@0x65c   handler@0x660   R_ARM_ABS32->hmac_dfs_radar_detect_event      byte-verified (local STT_FUNC @0xaca10)
  idx19 id=26  id@0x668   handler@0x66c   R_ARM_ABS32->hmac_proc_disasoc_misc_event     byte-verified (local STT_FUNC @0x4a878)
  idx20 id=29  id@0x674   handler@0x678   R_ARM_ABS32->hmac_acs_process_rescan_event    byte-verified (local STT_FUNC @0xb553c)
  idx21 id=31  id@0x680   handler@0x684   R_ARM_ABS32->hmac_csi_complete                byte-verified (local STT_FUNC @0xb9ef8)
  idx22 id=34  id@0x68c   handler@0x690   R_ARM_ABS32->hmac_sr_bss_color_change_event   byte-verified (local STT_FUNC @0x95fac)
  idx23 id=35  id@0x698   handler@0x69c   R_ARM_ABS32->hmac_sr_syn_parameter_event      byte-verified (local STT_FUNC @0x96294)
  idx24 id=36  id@0x6a4   handler@0x6a8   R_ARM_ABS32->hmac_chr_report_msg              byte-verified (local STT_FUNC @0x49438)
  idx25 id=38  id@0x6b0   handler@0x6b4   R_ARM_ABS32->hmac_config_set_security_port_event byte-verified (local STT_FUNC @0xa3dfc)
  idx26 id=39  id@0x6bc   handler@0x6c0   R_ARM_ABS32->hmac_update_fbt_scan_result      byte-verified (local STT_FUNC @0xa9ccc)
  idx27 id=37  id@0x6c8   handler@0x6cc   R_ARM_ABS32->hmac_pfm_detect_process_event    byte-verified (local STT_FUNC @0xf6f0c)
=== Group 2 chip hmac table (#2) @ .data+0x6d4 (file 0x244184) count=12 ===
  idx0  id=22  id@0x6d4   handler@0x6d8   no-reloc (handler word 0x00000000)            absent (null)
  idx1  id=23  id@0x6e0   handler@0x6e4   no-reloc (handler word 0x00000000)            absent (null)
  idx2  id=24  id@0x6ec   handler@0x6f0   R_ARM_ABS32->hmac_spectral_scan_complete      byte-verified (local STT_FUNC @0x89e70)
  idx3  id=25  id@0x6f8   handler@0x6fc   R_ARM_ABS32->hmac_phy_event_complete          byte-verified (local STT_FUNC @0x8bd4c)
  idx4  id=26  id@0x704   handler@0x708   R_ARM_ABS32->hmac_phy_event_complete          byte-verified (local STT_FUNC @0x8bd4c)
  idx5  id=27  id@0x710   handler@0x714   R_ARM_ABS32->hmac_mgmt_deauth_msg_process     byte-verified (local STT_FUNC @0x43788)
  idx6  id=28  id@0x71c   handler@0x720   R_ARM_ABS32->hmac_ba_action_msg_process       byte-verified (local STT_FUNC @0xaf48c)
  idx7  id=29  id@0x728   handler@0x72c   R_ARM_ABS32->hcc_timer_process                byte-verified (import)
  idx8  id=33  id@0x734   handler@0x738   R_ARM_ABS32->hmac_mgmt_read_error_msg_process byte-verified (local STT_FUNC @0x4abe4)
  idx9  id=34  id@0x740   handler@0x744   R_ARM_ABS32->hmac_mgmt_send_deauth_frame_process byte-verified (local STT_FUNC @0x410d0)
  idx10 id=35  id@0x74c   handler@0x750   R_ARM_ABS32->hmac_del_user_msg_process        byte-verified (local STT_FUNC @0x413f8)
  idx11 id=36  id@0x758   handler@0x75c   R_ARM_ABS32->hmac_rx_data_send_disasoc_frame_process byte-verified (local STT_FUNC @0x41bac)
=== Group 2 chip wal table (#2) @ .data+0x1c74 (file 0x245724) count=19 ===
  idx0  id=0   id@0x1c74  handler@0x1c78  R_ARM_ABS32->wal_config_process_pkt           byte-verified (local STT_FUNC @0xfcd9c)
  idx1  id=1   id@0x1c80  handler@0x1c84  R_ARM_ABS32->wal_acs_netlink_recv_handle      byte-verified (local STT_FUNC @0x128fbc)
  idx2  id=2   id@0x1c8c  handler@0x1c90  R_ARM_ABS32->wal_scan_comp_proc_sta           byte-verified (local STT_FUNC @0x125714)
  idx3  id=3   id@0x1c98  handler@0x1c9c  R_ARM_ABS32->wal_asoc_comp_proc_sta           byte-verified (local STT_FUNC @0x125b6c)
  idx4  id=4   id@0x1ca4  handler@0x1ca8  R_ARM_ABS32->wal_disasoc_comp_proc_sta        byte-verified (local STT_FUNC @0x125e3c)
  idx5  id=5   id@0x1cb0  handler@0x1cb4  R_ARM_ABS32->wal_connect_new_sta_proc_ap      byte-verified (local STT_FUNC @0x125f60)
  idx6  id=6   id@0x1cbc  handler@0x1cc0  R_ARM_ABS32->wal_disconnect_sta_proc_ap       byte-verified (local STT_FUNC @0x1260d8)
  idx7  id=7   id@0x1cc8  handler@0x1ccc  R_ARM_ABS32->wal_mic_failure_proc             byte-verified (local STT_FUNC @0x126248)
  idx8  id=8   id@0x1cd4  handler@0x1cd8  R_ARM_ABS32->wal_acs_response_event_handler   byte-verified (local STT_FUNC @0x129288)
  idx9  id=9   id@0x1ce0  handler@0x1ce4  R_ARM_ABS32->wal_send_mgmt_to_host            byte-verified (local STT_FUNC @0x1265d4)
  idx10 id=12  id@0x1cec  handler@0x1cf0  R_ARM_ABS32->wal_cfg80211_init_evt_handle     byte-verified (local STT_FUNC @0x107058)
  idx11 id=13  id@0x1cf8  handler@0x1cfc  R_ARM_ABS32->wal_cfg80211_mgmt_tx_status      byte-verified (local STT_FUNC @0x106034)
  idx12 id=20  id@0x1d04  handler@0x1d08  R_ARM_ABS32->wal_cfg80211_cac_report          byte-verified (local STT_FUNC @0x10767c)
  idx13 id=21  id@0x1d10  handler@0x1d14  R_ARM_ABS32->wal_receive_all_sta_rssi_proc    byte-verified (local STT_FUNC @0x126398)
  idx14 id=30  id@0x1d1c  handler@0x1d20  R_ARM_ABS32->wal_report_external_auth_req     byte-verified (local STT_FUNC @0x12680c)
  idx15 id=31  id@0x1d28  handler@0x1d2c  R_ARM_ABS32->wal_process_packet_xmit          byte-verified (local STT_FUNC @0xff774)
  idx16 id=32  id@0x1d34  handler@0x1d38  R_ARM_ABS32->wal_dfr_power_down_dev           byte-verified (local STT_FUNC @0x129614)
  idx17 id=37  id@0x1d40  handler@0x1d44  R_ARM_ABS32->wal_multiap_report_proc          byte-verified (local STT_FUNC @0x126a4c)
  idx18 id=38  id@0x1d4c  handler@0x1d50  R_ARM_ABS32->wal_hiwifi_report_proc           byte-verified (local STT_FUNC @0x126b50)
=== Group 3 chip hdpp `tab_chip` @ .data+0x4 (file 0x243ab4) count=5 ===
  idx0  id=0   id@0x4     handler@0x8     R_ARM_ABS32->hmac_voice_aggr_event            byte-verified (local STT_FUNC @0x11ec)
  idx1  id=4   id@0x10    handler@0x14    R_ARM_ABS32->hmac_device_wow_data_report      byte-verified (local STT_FUNC @0x11d58)
  idx2  id=5   id@0x1c    handler@0x20    R_ARM_ABS32->hmac_rx_schedule_req             byte-verified (local STT_FUNC @0x1b720)
  idx3  id=7   id@0x28    handler@0x2c    R_ARM_ABS32->hdpp_stat_save_tx_ppdu_record_process byte-verified (local STT_FUNC @0xcdd4)
  idx4  id=8   id@0x34    handler@0x38    R_ARM_ABS32->hdpp_stat_save_rx_ppdu_record_process byte-verified (local STT_FUNC @0xc8cc)
=== Group 3 chip hmac table (#3) @ .data+0x764 (file 0x244214) count=8 ===
  idx0  id=9   id@0x764   handler@0x768   R_ARM_ABS32->hmac_smps_update_device_capbility byte-verified (local STT_FUNC @0xa70c4)
  idx1  id=10  id@0x770   handler@0x774   R_ARM_ABS32->hmac_pfm_hiex_rx_local_msg       byte-verified (local STT_FUNC @0xf703c)
  idx2  id=15  id@0x77c   handler@0x780   R_ARM_ABS32->hmac_bsd_update_msg              byte-verified (local STT_FUNC @0x4b3d4)
  idx3  id=16  id@0x788   handler@0x78c   R_ARM_ABS32->hmac_multiap_report_11v_event    byte-verified (local STT_FUNC @0xc674c)
  idx4  id=17  id@0x794   handler@0x798   R_ARM_ABS32->hmac_d2h_pm_event                byte-verified (local STT_FUNC @0xb3904)
  idx5  id=18  id@0x7a0   handler@0x7a4   R_ARM_ABS32->hmac_d2h_temp_state_update       byte-verified (local STT_FUNC @0x6b040)
  idx6  id=19  id@0x7ac   handler@0x7b0   R_ARM_ABS32->hmac_receive_beacon_probrsp_to_reprot byte-verified (local STT_FUNC @0x4a1c8)
  idx7  id=20  id@0x7b8   handler@0x7bc   R_ARM_ABS32->hmac_report_beacon_frame         byte-verified (local STT_FUNC @0x49ce8)

reloc type histogram over the 79 slots: R_ARM_ABS32=77, none=2
```
