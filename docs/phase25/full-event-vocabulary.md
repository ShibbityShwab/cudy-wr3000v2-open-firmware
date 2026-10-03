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

