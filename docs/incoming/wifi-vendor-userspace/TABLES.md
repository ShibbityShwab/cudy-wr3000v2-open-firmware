# Evidence tables — vendor userspace Wi-Fi stack (task-96)

Companion to [REPORT.md](REPORT.md). All data extracted locally from
`/tmp/rootfs2524b` (firmware 2.5.24-20260727-122111) and, for deltas, from
`/home/shibbityshwab/recovery-extract` (2.4.15-20251030-114751). No vendor code,
firmware bytes, calibration values or per-unit identities are reproduced. Regenerate with
[`tools/regen-evidence.sh`](tools/regen-evidence.sh).

Evidence tags: **[V]** static artifact evidence, **[V\*]** prior bench observation
(cited), **[I]** inference, **[U]** unknown.

---

## T1 — artifact pins and version deltas

| item | value / evidence |
| --- | --- |
| 2.5.24 rootfs | `/tmp/rootfs2524b`; `/etc/rom_version` = `2.5.24-20260727-122111`; `/etc/hi_version`: chip `luofu_V200`, product `wrt_ax3000_lite`, release type `release` [V] |
| 2.4.15 rootfs | `/home/shibbityshwab/recovery-extract`; `/etc/rom_version` = `2.4.15-20251030-114751` [V] |
| firmware blob | `/lib/firmware/hi_wifi/FIRMWARE.bin` **928,920 B**, md5 **`0e530b976d5a20e87358671f1a577695`**, identical in both rootfs; the md5 every prior Wi-Fi lane used [V] |
| firmware ini (device) | `/lib/firmware/hi_wifi/cfg_device_hisi.ini`, 799 B: `dev_tcxo_selection=0x2`; `firmware_itcm_src_addr=0x117bc4 len=0xa968`; `firmware_dtcm_src_addr=0x12252c len=0x76c`; `firmware_plat_custom_src_addr=0x1b2800 len=0x60` (24 words); `firmware_wifi_custom_src_addr=0x1b2860 len=0x9a8` (618 words) [V]. The 24+618 pre-seeded custom words used by the bench transport match these lengths. |
| firmware ini (RF) | `/lib/firmware/hi_wifi/cfg_hi5622v100_hisi.ini`, 32,315 B — PHY/regulatory/country parameters, parsed by plat.ko (`hwifi_config_init`, `hwifi_config_init_nvram_main` strings: power-limit/NV/ini-count/"ini error" logs) [V] |
| Wi-Fi module (extract) | `/lib/modules/5.10.201/hi5622v100_wifi.ko`, 3,564,728 B, md5 **`4737fcb21a1a2262a96f84d780ad8b35`** (matches the fixture of the committed HCC/H2D lanes) [V] |
| plat module (extract) | `/lib/modules/5.10.201/hi5622v100_plat.ko`, 364,660 B, md5 **`23660bc285393e678d5cade1c36c194b`** [V] |
| overlay-patched module | live bench box has been observed with `hi5622v100_wifi.ko` md5 `e21629d2…` (overlay copy) — re-pin hashes before ABI-adjacent runs ([phase16](../../phase16/boot-takeover.md) §1.3) [V\*] |
| 2.4.15 module variants | 2.4.15 extract carries *different* builds (wifi md5 `1745ef77…`, plat md5 `f688618b…`) under `/lib/hisilicon/ko/`; only the 2.5.24 extract matches the committed HCC analyses — do not mix fixtures [V] |
| module packaging delta | 2.4.15: `hi5622v100_{plat,wifi}.ko` in `/lib/hisilicon/ko/` (58 files); 2.5.24: in `/lib/modules/5.10.201/` (207 vs 198 `.ko`), absent from `/lib/hisilicon/ko/`. `wifi_init.sh` still names the old paths; `kmodloader insmod` resolves by basename ([phase16](../../phase16/boot-takeover.md) §1.3) [V]/[V\*] |
| identical vendor scripts | `wifi_init.sh`, `hi5622v100_cal_init.sh`, `hi5622v100_cal_get.sh`, `etc/init.d/wpad`, `etc/init.d/softapd`, `etc/config/wifi_schedule` byte-identical 2.4.15 ↔ 2.5.24 [V] |
| changed vendor scripts | `/etc/config/wireless`, `/lib/netifd/wireless/mac80211.sh`, `/lib/netifd/hostapd.sh`, `/lib/netifd/netifd-wireless.sh`, `/sbin/wifi` differ; 2.5.24 adds EHT/`be` support, `MAC_OFFSET`/VAP-count logic and changes `channel auto`→`channel 0` [V] |
| driver licence metadata | both modules' `.modinfo` carries `license=GPL` entries (vendor wrapper string); relevant to the open GPL request [V] |
| module vermagic | `5.10.201 SMP mod_unload ARMv7` (both modules) [V] |
| userspace binaries of interest (sizes) | `wpad` 1,843,236 B (hostapd+wpa_supplicant symlinks); `hostapd_cli` 46,672; `wpa_cli` 84,648; `softapd` 238,564; `hcshd` 9,968; `hyfibrd` 17,712; `bandsteering` 17,716; `easymesh` 9,416; `em_cli` 30,696; `iwinfo` 21,728; `iw` 121,952; `iwconfig` 58,696; `hi_cfm` 9,416; `hi_appm` 5,320; `libhi_owal_ssf.so` 21,752 [V] |

---

## T2 — boot/init chain (vendor rootfs)

| order | unit | action | interface / evidence |
| --- | --- | --- | --- |
| S09 | `/etc/init.d/hsan_prepare` | mounts `fac/cfga/cfgb/log/upgrade`; `histart()` runs `/etc/rc.d/luofu` (pinctrl, PIE, LSW, GMAC, MDIO, CFE, HSLINK, `hi_woc_wifi_rtx`, temp, watchdog), sets `printk`, USB modules; creates `/dev` nodes | no Wi-Fi driver [V] |
| S15 | `/etc/init.d/hsan_start` (START=15) | `/usr/bin/wifi_init.sh` | **the only Wi-Fi module load**; `hsan_start:45` [V] |
| | `/usr/bin/wifi_init.sh` | `hi_cfm get board.{wlan24,wlan58,wifi_vender,sysinfo.factory}`; selects branch; `shuangta` branch insmods `hi_kwificlk` (missing → harmless), `hi_pcie`, `hi5622v100_plat`, `hi5622v100_wifi`; calls `hi5622v100_cal_init.sh`; `touch /tmp/wifi_done` | lines 76–81 of the script; branch evidence phase16 §1.2 [V]/[V\*] |
| | kernel driver (inside insmod) | PCI probe, `request_firmware` (`/lib/firmware/hi_wifi/{FIRMWARE.bin,cfg_device_hisi.ini,cfg_hi5622v100_hisi.ini}`), chip reset/S1, ready exchange, `hal/hmac/wal_main_init`, wiphy + netdev registration | prior lanes [V]; no userspace involvement |
| S16 | `/etc/init.d/hsan_appm` | sysctls; `hi_app start` (`hi_appm` owal apps: `sysinfo,board,encrypt,upgrade`); `/sys/devices/platform/sysenv/boot_done`; falls back to `wifi_inner_init.sh` if `hi_app` is not running | platform IPC `/dev/hi_ipc` + ubus; not WLAN [V] |
| S18 | `/etc/init.d/softapd` | `/usr/sbin/softapd` | ubus `wireless` object + nl80211 genl + iwpriv + `/sys/hisys/hipriv` + `nlwifi_*` [V]; prior ABI analysis [opaque-binaries](../opaque-binaries/REPORT.md) §2.3 |
| S19 | `/etc/init.d/wpad` (START=19, STOP=21) | `/usr/sbin/hostapd -s -g /var/run/hostapd/global` and `/usr/sbin/wpa_supplicant -n -s -g /var/run/wpa_supplicant/global`, via procd (jailed with `/etc/capabilities/wpad.json`) | stock nl80211 wpad v2.10 [V] |
| S20 | `/etc/init.d/network` | netifd drives `/lib/netifd/wireless/mac80211.sh`: VAP create/destroy via `/sys/hisys/hipriv`, hostapd/wpa_supplicant config via ubus, per-VAP iwpriv tuning | vendor-patched netifd script, 2084 lines [V] |
| S21 | `/etc/init.d/hyfibrd` | `/usr/sbin/hyfibrd`; `hyctl attach br-lan`; ubus `hyfibr init` | EasyMesh L2, not required for basic AP [V] |
| S82 | `cmagent` / S99 `cmsd` | cloud/MQTT control planes | management only [V] |
| S98 | `hcsh` → `/usr/sbin/hcshd` | dormant UDP RSA command receiver | unrelated to Wi-Fi [V] |

---

## T3 — userspace→kernel interface inventory

| # | surface | carrier | producer (userspace) | driver entry points (evidence) | consumers / status |
| --- | --- | --- | --- | --- | --- |
| 1 | cfg80211 / nl80211 | generic netlink family `nl80211` | `wpad` (hostapd/wpa_supplicant), `iw`, `libiwinfo`, `softapd` (standard queries) | 26 imported cfg80211/wiphy/ieee80211 symbols; 71 `wal_cfg80211_*` functions; 39/112 `cfg80211_ops` slots populated ([wifi-driver-re](../wifi-driver-re/REPORT.md) §2.2) [V] | primary control-plane ABI; stock and stable |
| 2 | WEXT private ioctls | per-netdev ioctl (`SIOCSIWPRIV`-class) | `iwconfig`/`iwpriv`/`iwlist` (one binary); cal scripts; `hdb_cmd`; notebook tooling | private tables `.rel.rodata 0x8a20–0x9be0`: `alg` 0x0101, `cali` 0x0102, `setcountry/getcountry`, `set_txpower/get_txpower`, `get_channel/get_mode`, `get_chipid/get_dieid`, …; ~200 `wal_hipriv_*` handlers [V] | factory calibration + diagnostics |
| 3 | `/sys/hisys/hipriv` | sysfs command attribute (vendor kobject `hisys`) | `/lib/netifd/wireless/mac80211.sh`, `/etc/wireless/{wds,smt}/*.sh`, `softapd`, `hdb_cmd` | base 100-command table, 181 with `wifi_debug.ko`; 140 `wal_hipriv_*` symbols [V]; on-device counts [phase7](../../phase7/firmware-ram-dump.md) §1.2 [V\*] | VAP lifecycle + per-VAP knobs; replace with cfg80211 or shim |
| 4 | vendor netlink ("oam netlink") | raw `AF_NETLINK` channel created by plat.ko | `softapd` (`nlwifi_*`) | plat.ko: `oam_netlink_kernel_create/recv/send`, `oam_netlink_ops_register/unregister`; wifi.ko: `g_st_nl_hiwifi`, `g_st_nl_hiwifi_multiap`, `dev_netlink_create`, `dev_netlink_info_{add,del,find,getpid,update,dump}`; `genlmsg_put`/`nla_put` formatting only, no `genl_register_family` [V]; runtime `netlink_info_cnt:6` [phase49](../../phase49/README.md) [V\*] | optional (softapd/EasyMesh); attribute layout [U] |
| 5 | proc/sysfs dumps | procfs/sysfs | `hdb_cmd/info/dump`; debug tooling | `wal_add_vap_proc_file` (`/proc/<vap>/ap_info`), `/sys/hisys/plat/pcie` (wifi_debug) [V]/[V\*] | diagnostics only |
| 6 | not Wi-Fi: `/dev/hi_ipc` | char device + ioctl | `hi_appm`/owal apps, `bandsteering`, `easymesh` | `hi_kipc.ko`; `libhi_owal_ssf.so` [V] | platform app framework |
| 7 | not Wi-Fi: ubus | unix socket | `softapd`, `hostapd`/`wpa_supplicant` (config_add), `hyfibrd`, netifd | service objects only [V] | intra-userspace control |
| 8 | not Wi-Fi: `hi_cfm` | file/CLI | scripts/netifd (`board.wlan24/wlan58/wifi_vender`) | `libhi_cfm.so`, `/usr/local/factory/boardinfo`, `/config/conf/cfm` [V] | board configuration |

---

## T4 — `/sys/hisys/hipriv` command vocabulary used by shipped userspace

Extracted from the call sites in the rootfs scripts (grep `/sys/hisys/hipriv`); the
driver side symbol is the matching `wal_hipriv_*` function (140 unique symbols in
wifi.ko) [V].

| group | commands (script spelling) | call sites | driver symbol family |
| --- | --- | --- | --- |
| VAP lifecycle | `Hisilicon0 create <ifname> <type> [band] [mlo]`, `<ifname> destroy`, `Hisilicon1 create …`, `create_mld <id> <type>`, `delete_mld <id>` | netifd `mac80211.sh:635,637,640,664,1381,1383,1394,1451,1456,1468,1497,1500,1506,1512`; `etc/wireless/wds/10-wds.sh:318,320,323,340` | `wal_hipriv_add_vap`, `wal_hipriv_del_vap` (+ `add_vap_proc_file`, `add_vap_fill_param`) |
| VAP mode / WDS | `<ifname> wds_vap_mode <1|2>` | mac80211.sh:651,653,1031; 10-wds.sh | `wal_hipriv_wds_vap_mode`, `wal_hipriv_wds_vap_show` |
| WDS station table | `<ifname> wds_sta_del <mac>` | softapd strings | `wal_hipriv_wds_sta_add/del/age`, `wal_hipriv_wds_node_add` |
| security/mode knobs | `sae_pwe <0|2>`, `set_regdomain_pwr` | mac80211.sh:818,1012,1045,1047 | `wal_hipriv_set_*`/cfg-event helpers |
| isolation | `isolation_mode 4`, `isolation_fwd 2`, `isolation_type 2 <0|1>` | mac80211.sh:1055–1063 | `wal_hipriv_set_isolation_{mode,fwd,type,clear,show}` |
| 11k | `11k_switch <0|1>`, `11k_set_neigh …`, `11k_stat_en` | mac80211.sh:1068,1070; softapd | `wal_hipriv_set_11k_switch`, `wal_hipriv_11k_set_neighbor`, `wal_hipriv_enable_11k_stat` |
| 11v | `11v_cfg_bsst <0|1>`, `11v_tx_request …`, `11v_set_neighbor …` | mac80211.sh:1083,1085; softapd | `wal_hipriv_11v_cfg_bsst`, `wal_hipriv_11v_tx_request`, `wal_hipriv_11v_set_neighbor`, `wal_hipriv_11v_cfg_wl_mgmt`, `wal_hipriv_11v_tx_query` |
| multi-user / m2u | `m2u_snoop_on 0 1` | mac80211.sh:1073 | `wal_hipriv_m2u_snoop_on`, `wal_hipriv_m2u_*` |
| country | `add_country_ie_switch 1` | mac80211.sh:1077 | `wal_hipriv_add_country_switch`, `wal_hipriv_add_bcn_country_ie`, `wal_hipriv_setcountry` |
| radar/DFS | `radartool dfsenable <0|1>`, `radartool cacenable <0|1>`, `radartool set_next_chan …` | mac80211.sh:1896–1909,1996 | `wal_hipriv_dfs_radartool` (cfg event) |
| stats/debug | `stat dump_hdpp`, `<vap> stat tx_drop_detail` | `hdb_cmd/info/dump:232–238` | `wal_hipriv_query_stat_info`, `wal_hipriv_query_{dev,vap,user}` |
| debug extension | `get_all_regs all`, `reginfo`, `regwrite` (only after `wifi_debug.ko`; 100→181 commands) | [phase6](../../phase6/register-dump.md), [phase7](../../phase7/firmware-ram-dump.md) | `wal_hipriv_get_all_reg_value`, `wal_hipriv_reg_info`, `wal_hipriv_reg_write` |
| other available (not used by scripts) | `add_user/del_user`, `set_mode`, `set_rate/mcs/mcsac/mcsax`, `set_bw`, `set_txpower`, `set_freq/rfch/rxch`, `apsd/twt/oma`, `green_ap`, `kick_user`, `hide_ssid`, `mbo/bsd`, `blacklist/whitelist`, `trc_cfg`, `speedtest_opt`, `custom_test`, … | — | all have `wal_hipriv_*` symbols in wifi.ko [V] |

---

## T5 — WEXT/iwpriv commands used by shipped userspace

| user | command(s) | purpose |
| --- | --- | --- |
| `/usr/bin/hi5622v100_cal_init.sh` | `iwpriv Hisilicon0 alg set_{2g_power_param,2g_low_power_param,5g_power_param,5g_low_power_param,xo_ppm_cali_param,rssi_param} <blob>` | apply factory text calibration to driver/firmware at every boot (6 commands) |
| `/usr/bin/hi5622v100_cal_get.sh` | `iwpriv Hisilicon0 alg get_{…}_param` → `/usr/local/factory/hi5622v100.cal` | factory calibration capture (production) |
| `/usr/bin/wifi_cal_init.sh` / `wifi_cal_get.sh` | `alg set/get_{2g,5g}_dpd_{,low_}power_param`, `set_xo_ppm_cali_param`, `set_rssi_param` | alternative variant path (8 commands; not the shuangta one) |
| netifd `mac80211.sh` | `txpower`, `frag_th`, `rts_th`, `shpreamble`, `alg atf_mode 2`, `alg atf_min_prop 1`, `acs sw/idle_scan`, `he_gi`; commented `blacklist*` | per-VAP tuning; the two `alg` calls × 4 AP VAPs are the other half of the 14 `alg:[SUCC]` boot-time messages |
| `hdb_cmd/info/dump` | `get_version/get_channel/get_mode/get_essid/get_sta_info/get_current_pwr/get_txpower/get_pwr_ref/get_chan_list/get_rate_list/getcountry/get_temp/get_ant_rssi`; `alg get_{su_sch_mode,atf_mode,smartant_mode}` | field diagnostic dump |
| `softapd` | `ap_chan_scan`, `set_priv_scan`, `curscan`, `set_ie`, `kick_user`, `usernum`, `bintval`, `dtim_period`, `resp_hide` | ACS/scan/WPS/ACL/sta management |
| `opensource/tools/wifictl.sh` | `iwpriv Hisilicon0 alg <name> [args]`, `iwpriv Hisilicon0 get_chipid/get_dieid/get_mode/get_channel` | notebook lab helper (existing) |
| `opensource/tools/wifi-cal-snapshot.sh` | 24 `alg get_*` probes + `get_chipid/get_dieid` | notebook calibration snapshot (existing) |
| netifd `mac80211.sh` (additional) | `car_cfg "car_enable 1"`, `car_cfg "multicast 256|512"` (lines 969–973), `noforward <0|1>` (1053,1061), `he_gi 1` (1078), `probe_host_en` (1112,1116), `ie_rpt_en 1` (1987), `set_oui_filter 180 75 214` (1988, Cudy OUI `b4-4b-d6`), `set_rssi_acce` (2004) | rate/ATF/steering/OBSS tuning |
| legacy `/lib/wifi/mac80211.sh` (old `/sbin/wifi` path) | `iwpriv $ifname set_max_user "$CHIP_MAX $MAX_STA_2G $MAX_STA_5G"` (line 479) | pre-netifd path; still present in the rootfs |
| `opensource/tools/wifictl.sh` | `iwpriv Hisilicon0 alg <name> [args]`, `iwpriv Hisilicon0 get_chipid/get_dieid/get_mode/get_channel` | notebook lab helper (existing) |

---

## T6 — post-DEVICE-READY command trace and HCC mapping

Confidence per row. HCC ids from the committed catalogues
([wifi-h2d-catalog](../wifi-h2d-catalog/REPORT.md) §5.1, [wifi-hcc-re](../wifi-hcc-re/REPORT.md)
§5.5); driver-side mapping [V] where a builder/function is named, [I] where inferred.

| # | trigger | userspace artifact | driver entry | H2D (group/id) | D2H / completion | conf |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | dev boot (no userspace) | kernel `insmod` | `pcie_main_init` → firmware upload → ROM S1 → `pcie_msg_init` | none until ready | **g4 id1** `device_plat_ready_msg_process`, **g4 id2** `host_ready_msg_process` → completes `multi_chip_loading` | [V] |
| 0b | driver init after ready | kernel | `hal_main_init` → `hmac_main_init` (`hmac_board_init` → `hdpp_chip_sync_add` → `hmac_txq_table_init` → `hmac_txq_table_addr_sync` → `hdpp_config_send_event`) → `wal_main_init` (`wiphy_new_nm/register`, cfg80211 state 5) | **g0 id1** TXQ-table sync (first H2D after ready); also cali `.kv` save to file | none required | [V] chain |
| 1 | S15 last step | `hi5622v100_cal_init.sh` (6×) | WEXT 0x0101 `wal_hipriv_alg_cfg` → alg process-table name→cfg-id → `hmac_config_alg_send_event` | **g0 id2** alg/cali config event | **g1 id3** `hmac_event_config_syn` → `hmac_sync_dmac_alg_cfg_rsp_entry` (request-id pair 0x0d01xxxx) | [V] ids / [I] trigger-to-op |
| 2 | S20 netifd per VAP | `echo "Hisilicon0 create …" > /sys/hisys/hipriv` | `wal_hipriv_add_vap` (+ proc/netdev registration; `dev_netlink_info_add`) | user/vap-add family: `hmac_user_add` **g0 id7**, `hmac_user_add_notify_alg` g0 id8; delete via `hmac_del_user_notify_dmac` g0 id9 | g1 id3 responses; station events | [V] builders, [I] mapping |
| 3 | S20 hostapd config_add → hostapd nl80211 START_AP | `ubus call hostapd config_add`; hostapd nl80211 | `wal_cfg80211_start_ap` / `_start_ap_write_msg` / `_start_req` → `wal_send_cfg_event` | **g2 WAL cfg event** (id = WAL cfg command id; exact per-op id [U]) + `hmac_config_send_event` g0 id1 for hmac-level configs | g1 id3 sync; beacon/sta notifications | [V] symbols / [U] payload |
| 4 | S20 per-VAP hipriv knobs | `sae_pwe`, `isolation_*`, `11k_switch`, `11v_cfg_bsst`, `m2u_snoop_on`, `add_country_ie_switch`, `radartool`, `wds_vap_mode` | `wal_hipriv_*` → `wal_hipriv_send_cfg_uint32_data` / `_without_data` | g0 id1 / g2 cfg events (one per command) | g1 id3 | [I] |
| 5 | S20 per-VAP iwpriv tuning | `txpower/frag_th/rts_th/shpreamble`, `alg atf_mode`, `alg atf_min_prop`, `acs sw/idle_scan` | WEXT 0x0101/priv → `wal_hipriv_*` / alg LUT | `alg` → **g0 id2**; others g0 id1/g2 | g1 id3 | [V] alg path, [I] others |
| 6 | scan (STA/ACS/softapd) | `iw scan`, hostapd ACS, `iwpriv set_priv_scan/ap_chan_scan/curscan` | `wal_cfg80211_scan` → `hmac_enc_scan_send_req` | **g0 id0xC** scan request (payload 0x1FC [U]) | chip **g1 id15** `hmac_scan_proc_scanned_bss`, **id16** `…scan_comp`, **id17** `…chan_result`; **g2 id2** `hmac_handle_scan_rsp_sta`; events → `cfg80211_inform_bss_frame_data` | [V] ids / [U] payload |
| 7 | STA connect / AP sta lifecycle | wpa_supplicant / hostapd nl80211 | `wal_cfg80211_connect/start_connect`, `add/del/change_station`; `hmac_user_add/del_user_notify_dmac` | **g0 id0xF** `hmac_sta_sync_join_req_params`, **g0 id0x10** `hmac_sta_wait_join(_rx_beacon)`; users g0 7/8/9; connect-result g0 0x11 | g2 id3 `hmac_handle_asoc_rsp_sta`, id4 `sta_handle_disassoc_rsp`, id5 `handle_connect_rsp_ap`, id6 `handle_disconnect_rsp_ap`; g1 id3; `cfg80211_connect_done`/`del_sta_sinfo` | [V] builders/ids, [I] pairing |
| 8 | keys / PMKSA / mgmt tx | hostapd/wpa_supplicant | `wal_cfg80211_add_key/remove_key/set_default_key/set_default_mgmt_key/set_pmksa/…` → `wal_send_cfg_event`; `wal_cfg80211_mgmt_tx` | g2 WAL cfg events (per-op ids [U]); mgmt-tx family | cfg80211 mgmt notifications; g2 id9 `send_mgmt_to_host`, id0xd `mgmt_tx_event_status` | [V] symbols / [U] ids |
| 9 | WDS/11k/11v/EasyMesh (optional) | softapd, wds/mesh scripts | `wal_hipriv_wds_*`, `11k_*`, `11v_*`, `multiap_*` | g0 0x1d/0x2b/0x2c/0x31; multiap g2 0x25/0x26 | mgmt/rrm events | [I] |
| 10 | PM / green AP (optional) | hostapd, softapd | `wal_hipriv_green_ap`, power-save cfg | cfg events | **g3 id17** `hmac_d2h_pm_event` | [I] |

Runtime cross-checks (all [V\*]): ready pair received and decoded as g4 id1/id2 over the
direct DR reader ([wifi-d2h-direct](../wifi-d2h-direct/REPORT.md)); stock boot health
`4 hostapd vaps AP-ENABLED, alg:[SUCC] x14, netlink_info_cnt:6`
([phase49](../../phase49/README.md)); `Wiphy phy0/phy1 + 6 wlan interfaces` on stock
([phase19](../../phase19/fw-boot.md)); `/sys/hisys/hipriv` 100 commands (181 with
wifi_debug) ([phase7](../../phase7/firmware-ram-dump.md)).

---

## T7 — D2H event tables whose consumers are userspace-visible

From [wifi-hcc-re](../wifi-hcc-re/REPORT.md) §5.5 / [wifi-driver-re](../wifi-driver-re/REPORT.md)
§3.6 (registration sites and counts re-decoded there). These are the tables a clean-room
driver must register so a cfg80211 MAC can surface events to userspace.

| registration site | space/group | count | handlers relevant to userspace |
| --- | --- | --- | --- |
| `plat_init_bal_hcc_excp` | chip g4 (boot) | 5 | id1 `device_plat_ready`, id2 `host_ready`, id3 `exception_info`, id4 `heartbeat`, id0 device log |
| `hdpp_main_init` | chip g3 | 5 | id5 rx-schedule req, id4 WoW data report, id7/8 PPDU stat records |
| `hdpp_main_init` | core g0 | 7 | id0/1 TX complete/event, id2/3 RX data event, id7 MAC exception, id8 BA timeout — the data-path events |
| `hmac_main_init` | chip g1 | 28 | id3 config/alg/cali sync dispatcher (userspace command completions), id13 mgmt rx, ids15–18 scan/ACS, id21/26 disassoc/deauth, id24 TBTT, id25 radar |
| `hmac_main_init` | chip g2 | 12 | id22–26 spectral/PHY, id27/28 deauth/BA, id29 timer, id33 read error, id34/35/36 disconnect/deauth frames |
| `hmac_main_init` | chip g3 | 8 | id9 SMPS, id16 multiap 11v, id17 PM event, id18 temp state, id19/20 beacon reports |
| `wal_main_init` | chip g2 (merged) | 19 | id1 ACS netlink recv, id2/3 scan/asoc comp STA, id5/6 connect/disconnect AP, id7 MIC failure, id9 send mgmt to host, id12 cfg80211 init evt, id13 mgmt tx status, id20 CAC, id21 all-sta RSSI, id30 external auth, id31 packet xmit, id37 multiap, id38 hiwifi report |

---

## T8 — component classification

| component | class | disposition for clean-room |
| --- | --- | --- |
| `/usr/sbin/wpad` (hostapd+wpa_supplicant), `hostapd_cli`, `wpa_cli` | stock OpenWrt | **keep** |
| netifd + `/lib/netifd/*` (+ stock wireless script) | stock OpenWrt | **keep**; replace vendor-patched `mac80211.sh` |
| `iw`, `iwinfo`/`libiwinfo` | OpenWrt | **keep** |
| `hi_cfm`/board info values | vendor platform | replace with DTS/UCI/board.d |
| kernel WLAN driver (`hi5622v100_plat/wifi.ko`) | vendor | **reimplement** (cfg80211 + HCC backend) |
| calibration data (`/usr/local/factory/hi5622v100.cal`) + applier (cal scripts/alg path) | vendor but data is per-unit factory | **keep data, reproduce application** |
| `/sys/hisys/hipriv` + vendor `mac80211.sh` patches | vendor ABI | drop or shim (cfg80211 `add_virtual_intf`) |
| `softapd` (ACS/WPS/11k/11v/ACL/TWT/SMT) | vendor proprietary | optional/drop for basic AP |
| `bandsteering`, `easymesh*`, `hyfibrd`/`hyctl`/`libhyfi-bridge` | vendor EasyMesh | optional/drop |
| `nlwifi_*` vendor netlink ABI | vendor proprietary | drop unless vendor softapd reused; layout [U] |
| `hdb`/`hdb_cmd`, `wifi_debug.ko`, `/sys/hisys/plat/pcie` | diagnostics | optional |
| `hcshd`, `hi_appm`/owal, `cmagent`/`cmsd`, `mosquitto`/cloud | unrelated platform | out of scope |

---

## T9 — gaps (and what would close each)

| # | gap | status | closure route |
| --- | --- | --- | --- |
| G1 | cfg80211 config-event payload structs (start_ap/keys/station) | [U] | vendor GPL source; kprobe trace on stock boot; or black-box differential on a recovered command service |
| G2 | hmac config helper payload beyond +8/+0xA/+0xC | [I] | h2d-catalog §5.2 + source |
| G3 | user-add/del 0xA8 B struct | [I] | source/bench |
| G4 | scan request 0x1FC B + scan-result structs | [U] | source/bench |
| G5 | hdpp data path (queues, netdev, BA) | [U] | source; measured inventory already has PIE/QM evidence |
| G6 | cali `.kv` container layout + message content + device application | [U] | phase8 loaders [V]; source/producer tooling |
| G7 | nlwifi attribute layout | [U] | only if vendor softapd/EasyMesh needed |
| G8 | VAP-create firmware semantics | [U] | avoid via cfg80211 add_virtual_intf |
| G9 | two-endpoint (2.4/5G) firmware instance design | [I] | transport/driver lanes must instantiate per PCI function |
| G10 | firmware post-ready IRQ/thread layer (current bench blocker) | [V\*] negative | firmware/ROM lanes (ready-emit, postboot, interrupt/unmask); not userspace |
| G11 | vendor GPL source | open request | [GPL-REQUEST.md](../../GPL-REQUEST.md) |

---

## T10 — evidence ledger / regeneration

Everything in these tables can be re-derived with
[`tools/regen-evidence.sh`](tools/regen-evidence.sh) (run it against a rootfs, no
device access):

| evidence | command (abridged) |
| --- | --- |
| rootfs versions / chip | `cat` `/etc/rom_version /etc/hi_version` |
| firmware hashes | `md5sum lib/firmware/hi_wifi/FIRMWARE.bin`; `cat lib/firmware/hi_wifi/cfg_device_hisi.ini` |
| module pins | `md5sum lib/modules/5.10.201/hi5622v100_{wifi,plat}.ko` |
| module inventory/delta | `ls lib/modules/5.10.201/*.ko | wc -l`; `ls lib/hisilicon/ko` |
| interface inventory | `grep -rn 'hisys/hipriv' etc lib sbin usr/bin usr/sbin`; `grep -rn 'iwpriv' etc lib sbin usr/bin` |
| init chain | `cat etc/init.d/{hsan_start,hsan_prepare,hsan_appm,wpad,softapd}`; `ls etc/rc.d` |
| driver symbol surfaces | `readelf -sW lib/modules/5.10.201/hi5622v100_wifi.ko` → `wal_hipriv_*` (140), `wal_cfg80211_*` (71), `dev_netlink_*`, `g_st_nl_hiwifi*` |
| userspace binaries | `strings`/`readelf -d` on `usr/sbin/{wpad,softapd,hcshd}`, `usr/bin/{iwinfo,hi_appm}`, `usr/lib/libhi_owal_ssf.so` |
| netifd VAP ABI | `grep -n 'hipriv|ubus call hostapd|iwpriv' lib/netifd/wireless/mac80211.sh` |
| calibration chain | `cat usr/bin/hi5622v100_cal_{init,get}.sh`; `cat lib/firmware/hi_wifi/cfg_device_hisi.ini` |
| prior runtime evidence | grep the committed lanes listed at the top of [REPORT.md](REPORT.md) |

Local-input caveat: the extracted rootfs and module hashes above are **local evidence**;
the project rule is that they are not committed and not quoted as device facts until a
claim lands in the ledger/EVIDENCE-INDEX (lead action).
