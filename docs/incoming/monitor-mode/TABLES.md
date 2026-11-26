# TABLES - monitor mode + spectral, every pinned reference

Grades: **[V]** verified by this lane from the named artifact; **[V\*]** committed lane;
**[M]** bench-measured earlier; **[I]** inference; **[U]** unknown.

Path conventions: `K/` = the pinned 6.18.60 kernel tree
`openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60/`;
`IMG3/` = the IMAGE #3 merged module tree `/tmp/wifi-image-3-module` (scratch, not
committed); `V25/` = `rootfs-2.5.24/lib/modules/5.10.201/hi5622v100_wifi.ko` (3,564,728 B,
sha256 `de78ec07e46e70ce8befa788a2a5d11d17e80cdea7c2789b721b0be5dcfd9521`); `V24/` =
`rootfs-2.4.15/lib/hisilicon/ko/hi5622v100_wifi.ko` (3,581,748 B, sha256
`22fa789a3d431f49f9c52c76c402e5740d1d14118b5e47a347f2cc247532c7d2`); `OH/` = the GPL-2.0
sibling tree `wifi-algcfg-slice-work/hi3881-openharmony/common/platform/wifi/hi3881v100/
driver/` (local reference only; nothing vendored). Vendor addresses below are .text-relative
(`.ko` is ET_REL, sh_addr 0).

---

## T1. The 6.18.60 cfg80211 contract for a full-MAC monitor [V]

| # | fact | source |
| --- | --- | --- |
| 1 | there is **no** `cfg80211_rx_monitor()` / monitor-RX helper in 6.18.60; the driver builds radiotap + delivers the skb itself | `K/net/wireless/` (grep: only `radiotap.c` exists, no rx_monitor symbol), `K/include/net/cfg80211.h` |
| 2 | `cfg80211_change_iface()` requires `wiphy.interface_modes & BIT(ntype)` and `rdev->ops->change_virtual_intf`, else -EOPNOTSUPP | `K/net/wireless/util.c:1191-1237` |
| 3 | after a successful change cfg80211 **WARNs** unless the driver stored the new iftype in `wdev->iftype` | `K/net/wireless/util.c:1237` |
| 4 | monitor has its own bridge branch ("monitor can't bridge anyway") | `K/net/wireless/util.c:1261` |
| 5 | `iw dev mon0 set channel` -> `nl80211` SET_CHANNEL -> `cfg80211_set_monitor_channel()`; **-EOPNOTSUPP if `ops->set_monitor_channel` is NULL** | `K/net/wireless/nl80211.c:3831`, `K/net/wireless/chan.c:1544-1553` |
| 6 | combinations are enforced only when the driver calls `cfg80211_check_combinations()`; `n_iface_combinations == 0` means no restriction | `K/net/wireless/util.c:2579-2593` |
| 7 | software iftypes must not be listed inside a combination; they are exempt from slot counting | `K/net/wireless/core.c:727-731`, `K/net/wireless/util.c:2928-2930` |
| 8 | monitor netdev type is chosen by the driver; cfg80211 only requires `ARPHRD_IEEE80211_RADIOTAP` skbs for tools to parse radiotap | standard radiotap contract; the IMAGE #3 driver sets it at `IMG3/hi5622_cfg80211_ops.c:171-172` |

## T2. IMAGE #3 merged driver, monitor-relevant surface [V]

| item | file:line | value |
| --- | --- | --- |
| interface_modes | `IMG3/hi5622_cfg80211.c:756-758` | `BIT(STATION) \| BIT(AP) \| BIT(MONITOR)` |
| monitor mgmt stypes | `IMG3/hi5622_cfg80211.c:663-666` | `tx = rx = 0xffff` |
| iftype mapping | `IMG3/hi5622_cfg80211_ops.c:57-65` | monitor -> `HI5622_IFTYPE_MONITOR` (6) |
| add_virtual_intf | `IMG3/hi5622_cfg80211_ops.c:104-...` | allocates netdev + wdev, registers via `cfg80211_register_netdevice` |
| monitor netdev type | `IMG3/hi5622_cfg80211_ops.c:171-172` | `ARPHRD_IEEE80211_RADIOTAP` |
| change_virtual_intf | `IMG3/hi5622_cfg80211_ops.c:333-362` | stores `wdev->iftype`, monitor/ether switch |
| ops table | `IMG3/hi5622_cfg80211_ops.c:505-...` | add/del/change intf, scan, start/stop AP, keys, station, mgmt_tx; **no set_monitor_channel** |
| module params | `IMG3/*.c` | 64 `module_param` declarations; none named monitor/radiotap |
| mgmt RX sink | `IMG3/hi5622_mgmt.c:184` `hi5622_mgmt_deliver` | parses D2H mgmt event, delivers to the AP wdev (`cfg80211_rx_mgmt`), gated by `mgmt_rx` |
| mgmt RX caller | `IMG3/hcc.c:108` | D2H dispatch -> `hi5622_mgmt_deliver` |
| data RX sink | `IMG3/hi5622_sta_ops.c:468` `hi5622_sta_ops_rx_deliver` | EAPOL/data to the AP netdev; gated by `eapol_rx` |
| data RX caller | `IMG3/hi5622_dp_txrx.c:1492` | completion-ring drain -> `hi5622_sta_ops_rx_deliver(ctx, cpu+0x28, len)` |
| RX metadata (mgmt) | `IMG3/hi5622_mgmt_logic.h:159-166` | `{frame, frame_len, freq, rssi, channel, subtype}` |
| RX metadata (data) | `IMG3/hi5622_rx_live.h:`struct hi5622_rx_hdr`` | only `payload_len/next_devva/flags9/status_b` are decoded; rate/rssi/channel are [U] |
| wiphy flags | `IMG3/hi5622_cfg80211.c:~810` | `WIPHY_FLAG_REPORTS_OBSS` (hostapd bookkeeping) |

## T3. Vendor monitor-mode absence, both module versions [V]

| check | V25 | V24 |
| --- | --- | --- |
| `nm | grep -i 'monitor'` | only `hmac_fbt_start_monitor_timer-ish` FBT timer (no monitor-mode symbol) | same |
| `strings | grep -i 'monitor'` | only `{hmac_fbt_start_monitor_timer::pst_mac_vap null.}` | same |
| `promis` symbol/string | none | none |
| `radiotap` symbol/string | none | none |
| `wal_cfg80211_add_virtual_intf` | @0x103a80, 52 B, ends `mvn r0, #10` (-11) | same shape (driver-re: 0x103878, 52 B) [V\*] |
| `wal_cfg80211_change_virtual_intf` | @0x103958, stores type at `wdev+4`, returns 0 | same shape (driver-re: 0x103750, 120 B) [V\*] |
| `.set_monitor_channel` | NULL (not in the 39 populated ops) | NULL [V\*, `wifi-driver-re/REPORT.md:120`] |
| `interface_modes` | 0x1070 = AP_VLAN\|WDS\|MONITOR\|NAN [V\*, `wifi-driver-re` sec 2.3] | live `iw phy` shows AP+STA+monitor [M, `wifi-re-report/TABLES.md:599`] |
| HMAC VAP create types | 1/2/3 only (2=AP, 3=STA; 1 unassigned [U]) [V\*, `wifi-vap-start/TABLES.md` T5/T9] | same |

## T4. Family reference: monitor mode in the GPL-2.0 sibling driver [V, local OH tree]

| piece | OH file:line | content |
| --- | --- | --- |
| cfg id | `OH/oal/wlan_mib.h:1052` | `WLAN_CFGID_MONITOR_EN = 522` |
| cfg dispatch | `OH/mac/hmac/hmac_event.c:206` | `{WLAN_CFGID_MONITOR_EN, HI_FALSE, {0}, HI_NULL, hmac_config_set_monitor_switch}` |
| driver send | `OH/mac/hmac/hmac_config.c:4451-4471` | sets `mac_dev->promis_switch` from `param[0]`, then `hmac_config_send_event(mac_vap, WLAN_CFGID_MONITOR_EN, len, param)` |
| user command | `OH/wal/wal_hipriv.c:3047-3086,6162` | `set_monitor [0\|1\|2\|3\|4]`; builds the write msg with `WLAN_CFGID_MONITOR_EN`, `sizeof(hi_u8)` |
| public API | `OH/include/hi_wifi_api.h:289-299,680-690,737,1632-1670` | `hi_wifi_monitor_mode` {OFF, MCAST_DATA, UCAST_DATA, MCAST_MANAGEMENT, UCAST_MANAGEMENT}; 4-bit `hi_wifi_ptype_filter`; callback `hi_wifi_promis_cb(void *buf, int len, signed char rssi)`; `hi_wifi_promis_enable()` |
| promis state | `OH/mac/common/mac_device.h:499` | `promis_switch : 1` |
| RX branches | `OH/mac/hmac/hmac_rx_data.c:917`, `hmac_rx_filter.c:340`, `hmac_mgmt_classifier.c:303,325` | data/mgmt promis branches when `promis_switch` |
| per-VAP RX filter | `OH/mac/hmac/hmac_rx_filter.c:392-452` [[V\*] `wifi-sibling-gpl/TABLES.md:114`] | 32-bit DMAC filter: AP UP 0xF7B9FAEA, AP INIT 0xF7B9FEFA, **monitor 0x1**, `support_any` clears BIT4 |
| gate | `OH/Makefile/BUILD.gn`, `_PRE_WLAN_FEATURE_PROMIS` conditional | family build feature; the hi5622 build has no trace of it [V] |

**Warning:** this is a *different chip generation*. No equivalent cfg id / symbol / string
exists in V24 or V25 [V]; sending 522 to hi5622 firmware is an unvalidated write. Reference
only.

## T5. Spectral symbols in the device-version module V25 [V]

| function | addr | role |
| --- | ---: | --- |
| `hmac_spectral_init` | 0x8a320 | per-band state init (2 bands): `mac_res_get_mac_chip`, `hmac_band_get_by_id`, memset 2x120 B, spinlocks, `shuangta_init_spectral_param_defaults` - **no H2D** |
| `hmac_spectral_exit` | 0x8a484 | state teardown |
| `hmac_get_spectral` | 0x896ec | command handler: reads psd_en via `shuangta_get_spectral_psd_en`, calls `hmac_spectral_set_scan_param_proc_SHUANGTA` (+0x438), `hmac_spectral_free_SHUANGTA` |
| `hmac_alg_spectral_scan_config_param` | 0x8a530 | WAL alg-cfg entry for spectral; calls `hmac_get_spectral`-family + logging |
| `hmac_spectral_scan_complete` | 0x89e70 | completion; called from `hmac_spectral_scan_timeout_fn+0x7c` |
| `hmac_spectral_scan_timeout_fn` | 0x8a234 | timeout -> complete |
| `hmac_spectral_dlist_choose` | 0x89e20 | data-list pick; called from alloc/print helpers |
| `hmac_spectral_get_count` | 0x89dcc | report count via `hal_get_psd_cfg0_cfg_psd_count` |
| `hmac_spectral_alloc_addr_SHUANGTA` | 0x8b3f0 | per-chip data address allocation (calls dlist_choose) |
| `hmac_spectral_free_SHUANGTA` | 0x8b358 | free the address |
| `hmac_spectral_set_scan_param_proc_SHUANGTA` | 0x8ad08 | applies the config through 19 `hal_set_psd_*` setters |
| `hmac_spectral_get_scan_param_proc_SHUANGTA` | 0x8a684 | reads back via `hal_get_psd_*` + `sprintf_s` |
| `hmac_spectral_get_count_SHUANGTA` | 0x8a5c0 | count readback |
| `hmac_spectral_print_ddr_data_info_SHUANGTA` | 0x8b5dc | DDR data dump (calls dlist_choose) |
| `shuangta_set_spectral_psd_en` | 0x3af2c | `obj[0x150]->[0x10]` bank ptr; bit 8 of +0x438 |
| `shuangta_get_spectral_psd_en` | 0x3af78 | reads bit 8 of +0x438 |
| `shuangta_set_spectral_data_addr` | 0x3afc4 | programs the data address (two null checks) |
| `shuangta_set_spectral_rd_len` | 0x3b084 | read length |
| `shuangta_set_spectral_intr_delay_time` | 0x3b088 | interrupt delay |
| `shuangta_get_spectral_scan_data{in,out}_abnormal` | 0x3b09c/0x3b120 | over/under-run counters |
| `shuangta_init_spectral_param_defaults` | 0x3b4d4 | defaults |
| `shuangta_spectral_print_ddr_data_to_file` | 0x3b1a4 | debug dump |
| 37 `hal_{set,get}_psd_*` | 0x32734-0x32b40 | the register accessors (T6) |
| 2.4.15-only | 30 `alg_cfg_args_analysis_spectral_*` + hardamard/protect/args-head | deleted in 2.5.24 [V\*, phase10] |

## T6. The PSD register map (decoded bit-for-bit from V25 accessors) [V]

Three 32-bit registers in one mapped bank pointer passed as the HAL object:

| reg | bank off | field | bits | setter (V25) | getter (V25) |
| --- | ---: | --- | --- | ---: | ---: |
| cfg0 | +0x430 | `psd_fft_size` | 0..2 (3) | 0x32aac | 0x327c4 |
| cfg0 | +0x430 | `psd_fft_period` | 4..15 (12) | 0x3298c | 0x32734 |
| cfg0 | +0x430 | `psd_count` | 16..31 (16) | 0x3295c | 0x32854 |
| cfg1 | +0x434 | `psd_bottom_noise` | 0..7 (8) | 0x32ac4 | 0x32794 |
| cfg1 | +0x434 | `psd_rssi_nb` | 8..15 (8) | 0x32a1c | 0x3283c |
| cfg1 | +0x434 | `psd_the_power_nb` | 16..22 (7) | 0x32b0c | 0x327ac |
| cfg1 | +0x434 | `psd_the_num_nb` | 23..31 (9) | 0x329ec | 0x3274c |
| cfg2 | +0x438 | `psd_rpt_mode` | 0 | 0x32af4 | 0x3280c |
| cfg2 | +0x438 | `psd_agc_lock_en` | 1 | 0x32944 | 0x327f4 |
| cfg2 | +0x438 | `psd_fftin_len_type` | 2..3 (2) | 0x32a04 | 0x32824 |
| cfg2 | +0x438 | `psd_ofdm_det_en` | 4 | 0x329bc | 0x3289c |
| cfg2 | +0x438 | `psd_11b_det_en` | 5 | 0x32a64 | 0x32764 |
| cfg2 | +0x438 | `psd_nb_det_en` | 6 | 0x329a4 | 0x3286c |
| cfg2 | +0x438 | `psd_en` | 8 | 0x32a7c | 0x3af78 (shuangta) |
| cfg2 | +0x438 | `psd_ant_index` | 9 | 0x32b24 | 0x327dc |
| cfg2 | +0x438 | `psd_p2dbm_offset` | 12..21 (10) | 0x32a94 | - (set-only) |
| cfg2 | +0x438 | `psd_deci_coef_man` | 24..25 (2) | 0x329d4 | 0x32884 |
| cfg2 | +0x438 | `psd_deci_coef_sel` | 26 | 0x32a4c | 0x3277c |
| cfg2 | +0x438 | `psd_auto_cg_disable` | 28 | 0x32a34 | - (set-only) |
| cfg2 | +0x438 | `psd_auto_rst_disable` | 29 | 0x32adc | - (set-only) |
| cfg2 | +0x438 | `psd_fft_cken` | 30 | 0x32974 | - (set-only) |

Unassigned bits in the decoded set: cfg2 bit 7, bits 10-11, bits 22-23, bit 27, bit 31 [U].
The report/event side uses the same bank family:

| reg | bank off | field | bits | setter |
| --- | ---: | --- | --- | ---: |
| cfg_lmi_ctrl1 | +0x528 | `init_event_addr` | whole word | 0x328c4 |
| cfg_lmi_ctrl2 | +0x52c | `lmi_event_rpt_en` | bit 8 | 0x32904 |
| cfg_lmi_ctrl2 | +0x52c | `lmi_psd_en` | bit 9 | 0x3291c |
| cfg_lmi_ctrl3 | +0x530 | `event_mem_max_num` | bits 16..27 | 0x328ec |
| cfg_lmi_ctrl6 | +0x53c | `event_rpt_num_en` | whole word | 0x32934 |
| host intr mask | +0x1b0 | `cfg_intr_mask_host` | whole word | 0x328b4 |
| timer | +0x28c | `wl_timer_en` | bit 17 | 0x328d4 |

## T7. Spectral control flow (V25 call sites) [V]

| edge | evidence |
| --- | --- |
| init at boot | `hmac_main_init+0xf4` -> `hmac_spectral_init` (0x3f7bc) |
| reinit on band/protection event | `hmac_protection_info_syn_event+0x51c` -> `hmac_spectral_init` (0x3f618) |
| host command entry | `wal_config_alg_cfg_param_host_entry+0x338` -> `hmac_alg_spectral_scan_config_param` (0xfc7c4) |
| apply config | `hmac_get_spectral+0x438` -> `hmac_spectral_set_scan_param_proc_SHUANGTA` (0x89b24) |
| scan completion | `hmac_spectral_scan_timeout_fn+0x7c` -> `hmac_spectral_scan_complete` (0x8a2b0) |
| data list | `hmac_spectral_alloc_addr_SHUANGTA+0x74`, `hmac_spectral_print_ddr_data_info_SHUANGTA+0x5c` -> `hmac_spectral_dlist_choose` |

## T8. Open questions / the next decode (host-only)

| id | question | why it blocks | candidate evidence |
| --- | --- | --- | --- |
| S-1 | the **CA base** of the bank holding +0x430/+0x434/+0x438 and +0x528..+0x53c | without it no read-only probe, no safe write | the driver's mapped-bank .rodata CA table (same family the RX lanes decoded: 10 mapped banks, band0 word5=0x40040000, word8=0x40050000) |
| S-2 | the report/data memory: what `init_event_addr` (+0x528) points at, where PSD reports/data land, how the host reads them | the data path | `shuangta_set_spectral_data_addr`, `rd_len`, `datain/out_abnormal`, LMI regs T6; the lab `bar0read`/physread tooling |
| S-3 | the activation sequence + delays (psd_en order, fft_cken, rpt_mode, count/period) | required for a working scan | `hmac_spectral_set_scan_param_proc_SHUANGTA` order (19 setters, T6) + `intr_delay_time` |
| S-4 | the report format (what a PSD report record contains, FFT-size/count relation) | to parse a capture | `hmac_spectral_get_count_SHUANGTA`/`get_scan_param_proc_SHUANGTA` readbacks; bench |
| M-1 | does the hi5622 firmware have a promiscuous/monitor switch at all? | a true sniffer | firmware decode of the config-event table (the same table the F1 lanes decode); family reference is **not** evidence |
| M-2 | the VAP mode-1 meaning (VAP add create type 1) | a monitor VAP record if one exists | key 0x0064 worker CA 0x449E4 decode (existing lands) |

## T9. Lane file inventory

| file | what |
| --- | --- |
| `README.md` / `REPORT.md` | overview / analysis |
| `CFG80211-MONITOR.md` | monitor design + deltas |
| `SPECTRAL.md` | PSD decode + next decode |
| `TABLES.md` | this file |
| `FIRE-RECIPE.md` | the attended ladder |
| `BUILD-NOTES.md` | prototype build facts |
| `patches/0001-monitor-mirror.patch` | prototype patch |
| `src/hi5622_monitor_logic.{c,h}`, `src/hi5622_monitor.c` | prototype sources (snapshot) |
| `tools/` | `monitor_selftest.c`, build/evidence scripts |
| `evidence/` | base manifest, test/build logs, patch round-trip |
