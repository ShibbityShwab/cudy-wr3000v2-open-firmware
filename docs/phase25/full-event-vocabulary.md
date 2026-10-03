# The full event vocabulary: five more message tables (phase 25s, 2026-10-03)

Following up the user's point that the work was not finished: reading the record showed that
`docs/phase15/teardown.md` had already located **more registrations than the two G002 covered** -
`hmac_main_init` registers three tables and `wal_main_init` one. G002 dumped only `hdpp_main_init`'s two,
which was accurate **as scoped** and **incomplete as a picture**. Dumping the rest names the event
vocabulary.

## What is registered, in full

| caller | call | id base | entries |
| --- | --- | --- | --- |
| `hdpp_main_init` | `hcc_msg_register_tab_chip` | 3 | 5 |
| `hdpp_main_init` | `hcc_msg_register_tab_core` | 0 | 7 |
| **`hmac_main_init`** | `hcc_msg_register_tab_chip` | **1** | **28** |
| **`hmac_main_init`** | `hcc_msg_register_tab_chip` (pc-literal table) | 2 | 12 |
| **`hmac_main_init`** | `hcc_msg_register_tab_chip` (pc-literal table) | 3 | 8 |
| **`wal_main_init`** | `hcc_msg_register_tab_chip` | 2 | 19 |

## The 28-entry hmac table (`.data+0x584`, id base 1), by name

| id | handler | | id | handler |
| --- | --- | --- | --- | --- |
| 0 | `hmac_sdt_up_reg_val` | | 20 | `hmac_mgmt_send_disasoc_deauth_event` |
| 1 | `hmac_create_ba_event` | | 21 | `hmac_mgmt_send_disasoc_deauth_event` |
| 2 | `hmac_del_ba_event` | | 22 | `hmac_chan_switch_to_new_chan_complete` |
| 3 | `hmac_event_config_syn` | | 24 | `hmac_mgmt_tbtt_event` |
| 5 | `hmac_syn_info_event` | | 25 | `hmac_dfs_radar_detect_event` |
| 9 | `hmac_bandwidth_info_syn_event` | | 26 | `hmac_proc_disasoc_misc_event` |
| 10 | `hmac_protection_info_syn_event` | | 29 | `hmac_acs_process_rescan_event` |
| 11 | `hmac_ch_status_info_syn_event` | | 31 | `hmac_csi_complete` |
| 13 | `hmac_rx_process_mgmt_event` | | 34 | `hmac_sr_bss_color_change_event` |
| 14 | `hmac_mgmt_rx_delba_event` | | 35 | `hmac_sr_syn_parameter_event` |
| 15 | `hmac_scan_proc_scanned_bss` | | 36 | `hmac_chr_report_msg` |
| 16 | `hmac_scan_proc_scan_comp_event` | | 37 | `hmac_pfm_detect_process_event` |
| 17 | `hmac_scan_process_chan_result_event` | | 38 | `hmac_config_set_security_port_event` |
| 18 | `hmac_event_acs_response` | | 39 | `hmac_update_fbt_scan_result` |

## Why this is the vocabulary the payload question needed

These are the host's **named handlers for device->host events**, and the names are the semantics:

- **scan** (15, 16, 17) - scanned BSS, scan complete, per-channel results;
- **channel / regulatory** (9, 10, 11, 18, 22, 25) - bandwidth, protection, channel status, ACS response,
  channel-switch complete, **DFS radar detect**;
- **management** (13, 14, 20, 21, 24, 26) - rx management, delba, deauth, **TBTT**, disassoc misc;
- **BA / block-ack** (1, 2) - create and delete;
- **BSS / sync** (0, 3, 5, 34, 35) - registration value, config sync, sync info, **SR BSS-colour change**,
  **SR sync parameter**;
- **others** (31 CSI complete, 36 CHR report, 37 PFM detect, 38 security port, 39 FBT scan).

So the ID space is not opaque tokens but a documented-by-name event set - and it is **directly relevant to
what the firmware wants next**. The firmware emits **id 6** shortly after release and holds **id 2**; the
host side's id 2 in the *plat* table is `device_plat_ready_msg_process` (G002's table) while the *hmac*
table's id 2 is `hmac_del_ba_event` - **different domains over the same id space**, which is itself a
finding: the id alone does not determine the handler, the domain does.

## Scope, stated honestly

This **extends G002's picture** and does not invalidate it: G002's criteria were scoped to
`hdpp_main_init`'s two calls and it answered those correctly. What this adds is that the registration
set is **five tables larger** than that scope, which is why the vocabulary had looked so thin.

**Still not established:** which event the firmware expects the host to *send* at the point the port has
reached, and the payload bodies of those events. Naming the handlers gives the vocabulary; it does not
give the message formats. That remains G006 (structure, measurable) and G004 (the vendor's definitions).


## The two pc-literal tables, resolved (the remaining hmac registrations)

The two tables `hmac_main_init` loads through a literal pool (id bases 2 and 3) sit directly after the
28-entry one, which is what made them findable: the literal resolved to `.data+0x6d4` = `0x584 + 28*12`,
and the next to `.data+0x764` = `0x6d4 + 12*12`. The layout is contiguous, so the three hmac tables are one
array read three ways.

**id base 2 `(.data+0x6d4, 12 entries)`**

| id | handler | | id | handler |
| --- | --- | --- | --- | --- |
| 22 | *unnamed* | | 28 | `hmac_ba_action_msg_process` |
| 23 | *unnamed* | | 29 | `hcc_timer_process` |
| 24 | `hmac_spectral_scan_complete` | | 33 | `hmac_mgmt_read_error_msg_process` |
| 25 | `hmac_phy_event_complete` | | 34 | `hmac_mgmt_send_deauth_frame_process` |
| 26 | `hmac_phy_event_complete` | | 35 | `hmac_del_user_msg_process` |
| 27 | `hmac_mgmt_deauth_msg_process` | | 36 | `hmac_rx_data_send_disasoc_frame_process` |

**id base 3 `(.data+0x764, 8 entries)`**

| id | handler |
| --- | --- |
| 9 | `hmac_smps_update_device_capbility` |
| 10 | `hmac_pfm_hiex_rx_local_msg` |
| 15 | `hmac_bsd_update_msg` |
| 16 | `hmac_multiap_report_11v_event` |
| 17 | `hmac_d2h_pm_event` |
| 18 | `hmac_d2h_temp_state_update` |
| 19 | `hmac_receive_beacon_probrsp_to_reprot` |
| 20 | `hmac_report_beacon_frame` |

## Vocabulary now: 79 named handlers across six tables

Two of the new names are worth calling out because they name a **direction** explicitly rather than a
subsystem: `hmac_d2h_pm_event` and `hmac_d2h_temp_state_update` - "d2h" being **device-to-host**. The
project has been treating the id space as one namespace with a direction implied by context; these two
show the vendor names the direction in the handler itself, which is the same lesson as the
domain-dependent id 2, from a different angle.

Note also `hcc_timer_process` at id 29 in the base-2 table: a **timer** dispatched through the message
table, which is consistent with phase 20's finding that `hcc_msg_tx` is called from
`hcc_timer_timeout_proc` - the message service and the timer service share this table.

## Coverage, stated honestly

| table | id base | entries | named |
| --- | --- | --- | --- |
| hdpp tab_chip | 3 | 5 | 5 |
| hdpp tab_core | 0 | 7 | 7 |
| hmac tab_chip | 1 | 28 | 28 |
| hmac tab_chip | 2 | 12 | **10** (two unnamed) |
| hmac tab_chip | 3 | 8 | 8 |
| wal tab_chip | 2 | 19 | **not yet dumped** |

So 58 of the 79 entries are named here; the wal table's 19 remain, and two entries in the base-2 table
resolve to no symbol at all (which is stated rather than omitted, as C002 of G002 required).


## The wal table (`.data+0x1c74`, id base 2, 19 entries) - the vocabulary is complete

| id | handler | | id | handler |
| --- | --- | --- | --- | --- |
| 0 | `wal_config_process_pkt` | | 12 | `wal_cfg80211_init_evt_handle` |
| 1 | `wal_acs_netlink_recv_handle` | | 13 | `wal_cfg80211_mgmt_tx_status` |
| 2 | `wal_scan_comp_proc_sta` | | 20 | `wal_cfg80211_cac_report` |
| 3 | `wal_asoc_comp_proc_sta` | | 21 | `wal_receive_all_sta_rssi_proc` |
| 4 | `wal_disasoc_comp_proc_sta` | | 30 | `wal_report_external_auth_req` |
| 5 | `wal_connect_new_sta_proc_ap` | | 31 | `wal_process_packet_xmit` |
| 6 | `wal_disconnect_sta_proc_ap` | | 32 | `wal_dfr_power_down_dev` |
| 7 | `wal_mic_failure_proc` | | 37 | `wal_multiap_report_proc` |
| 8 | `wal_acs_response_event_handler` | | 38 | `wal_hiwifi_report_proc` |
| 9 | `wal_send_mgmt_to_host` | | | |

This is the **WAL** layer - the highest of the three, and the one that fronts **cfg80211/mac80211**. Three
names matter directly to this project's goal:

- **`wal_send_mgmt_to_host`** (id 9) - a management frame being **sent to the host**, i.e. the path by
  which the vendor stack delivers received management frames upward;
- **`wal_cfg80211_mgmt_tx_status`** (13) and **`wal_cfg80211_cac_report`** (20) - **cfg80211 callbacks**,
  which is precisely the interface a genuinely open driver would have to implement;
- **`wal_receive_all_sta_rssi_proc`** (21), `wal_report_external_auth_req` (30), `wal_process_packet_xmit`
  (31) - the data-path and roaming entry points.

So the answer to "what would an open driver have to speak" is now partly written down in the vendor's own
handler names: the event set spans **scan / association / deauth / MIC failure / ACS / CAC / packet xmit /
rssi**, and the WAL table is where it meets cfg80211.

## Coverage: complete

| table | id base | entries | named |
| --- | --- | --- | --- |
| hdpp tab_chip | 3 | 5 | 5 |
| hdpp tab_core | 0 | 7 | 7 |
| hmac tab_chip | 1 | 28 | 28 |
| hmac tab_chip | 2 | 12 | 10 (2 unnamed) |
| hmac tab_chip | 3 | 8 | 8 |
| **wal tab_chip** | **2** | **19** | **19** |
| | | **79** | **77 named, 2 unnamed and stated as such** |

Every table the binaries register is now enumerated, every slot is accounted for, and the two that resolve
to no symbol are reported rather than omitted.

