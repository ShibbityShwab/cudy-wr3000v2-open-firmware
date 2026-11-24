# Vendor userspace Wi-Fi stack — interface scope after DEVICE READY

Lane: host-only static reverse engineering (task-96). Date: 2026-11-24.
Status: **complete for the scope defined below**. No device was contacted; nothing was
executed on the bench router; no vendor binary, firmware byte, calibration value or
extracted blob is reproduced here (names, paths, sizes and hashes only — all local
evidence stays gitignored).

Evidence base (local, gitignored):

* `/tmp/rootfs2524b` — extracted stock rootfs, firmware **2.5.24-20260727-122111**
  (`/etc/rom_version`), chip `luofu_V200`, product `wrt_ax3000_lite`
  (`/etc/hi_version`). This is the current bench firmware family; primary input for
  this lane.
* `/home/shibbityshwab/recovery-extract` — 2.4.15-20251030-114751 rootfs, used only
  for version-delta comparison (the device has booted both; see §1.2 and TABLE T1).
* Prior committed lanes, cross-referenced rather than re-derived:
  [wifi-hcc-re](../wifi-hcc-re/REPORT.md) (post-ready contract, D2H catalogues),
  [wifi-h2d-catalog](../wifi-h2d-catalog/REPORT.md) (H2D builders ↔ firmware handlers),
  [wifi-driver-re](../wifi-driver-re/REPORT.md) (vendor module structure, cfg80211 ops),
  [wifi-host-replica](../wifi-host-replica/SPEC.md) (H2D wire contract),
  [wifi-hcc-service](../wifi-hcc-service/REPORT.md) /
  [wifi-d2h-direct](../wifi-d2h-direct/REPORT.md) (working host counterpart),
  [wifi-transport-port](../wifi-transport-port/REPORT.md) (clean-room transport slice),
  [wifi-driver-integration](../wifi-driver-integration/REPORT.md) (prior driver wiring notes),
  [opaque-binaries](../opaque-binaries/REPORT.md) §2 (softapd/HyFi userspace),
  [wifi-thermal-re](../wifi-thermal-re/REPORT.md) (calibration + port-cost context),
  [phase16](../../phase16/boot-takeover.md) (module load mechanics),
  [phase6](../../phase6/register-dump.md)/[phase7](../../phase7/firmware-ram-dump.md)
  (`/sys/hisys/hipriv` on-device observations),
  [phase8](../../phase8/cali-buffer-populator.md) (calibration container),
  [phase49](../../phase49/README.md) (stock-runtime health cross-check).

Evidence tags used throughout: **[V]** verified statically from a local artifact
(script, config, module symbol/string); **[V\*]** verified on the bench by a prior lane
(runtime observation, cited); **[I]** inference from adjacent evidence; **[U]** unknown.

---

## 0. TL;DR — the five answers

1. **Which interface talks to the Wi-Fi driver.** There is **no Wi-Fi character device and
   no ioctl node**. The kernel driver (`hi5622v100_wifi.ko`, a cfg80211 full-MAC)
   exposes five userspace surfaces: (a) **cfg80211/nl80211 generic netlink** — the
   interface used by the stock `wpad` (hostapd + wpa_supplicant v2.10), `iw`
   and `libiwinfo`; (b) **WEXT private ioctls** (`iwpriv`/`iwconfig`,
   which is the same binary; the `alg` 0x0101 / `cali` 0x0102 dispatch and
   ~200 `wal_hipriv_*` handlers) — used by the factory calibration scripts and
   `hdb` dumps; (c) **`/sys/hisys/hipriv`** — a vendor sysfs command pipe (100
   commands in the base driver, 181 with `wifi_debug.ko`) that the vendor-patched
   netifd `mac80211.sh` uses for **VAP create/destroy** and per-VAP knobs; (d) a
   **vendor netlink channel** (the "oam netlink" layer: raw `netlink_kernel_create`
   channel + per-netdev pid registry `dev_netlink_*` + registered ops sets
   `g_st_nl_hiwifi` / `g_st_nl_hiwifi_multiap`) consumed by `softapd`'s
   `nlwifi_*` wrappers —
   the one surface whose byte layout is still opaque; (e) **`/proc`** per-VAP dumps
   (`/proc/<vap>/ap_info`) and misc proc entries. `/dev/hi_ipc`, `ubus`,
   `hi_cfm`, `hcshd` are *platform* surfaces, not Wi-Fi driver interfaces
   (§3.6).

2. **What userspace sends at startup.** Module load and the entire boot/ready exchange
   happen **inside the kernel driver** — userspace sends nothing until `insmod` of the
   Wi-Fi pair returns. The first post-ready H2D message is the driver's own **TXQ-table
   sync, HCC group 0 id 1** (`hdpp_config_send_event`), sent from `hmac_main_init`
   → `hmac_board_init` → `hdpp_chip_sync_add` → `hmac_txq_table_init` →
   `hmac_txq_table_addr_sync`, i.e. **no userspace involvement** [V chain;
   wifi-hcc-link]. The first *userspace-triggered* messages are the **6 calibration
   `iwpriv … alg set_*` writes** (`/usr/bin/hi5622v100_cal_init.sh` → WEXT
   0x0101 → `wal_hipriv_alg_cfg` → alg name→cfg-id LUT →
   `hmac_config_alg_send_event` = **HCC g0 id 2**; responses arrive as **D2H chip g1
   id 3** `hmac_event_config_syn`). Then netifd creates VAPs over
   `/sys/hisys/hipriv`; hostapd starts APs over nl80211 (`wal_cfg80211_*` →
   `wal_send_cfg_event` = WAL config events on **HCC group 2**, and hmac builders
   g0 id 7/8/9 for users, g0 id 0xC for scan, g0 id 0xF/0x10 for STA join); `softapd`
   adds scan/ACS/WPS/11k/11v through iwpriv + nlwifi. Full trace and mapping: §5, TABLE T6.

3. **Minimum viable interface for our clean-room driver.** Keep the stock OpenWrt
   userspace: `wpad`, netifd, `iw`, `iwinfo`, `hostapd_cli`/
   `wpa_cli`. Implement a **cfg80211 full-MAC kernel driver with an HCC transport
   backend** and a ~39-op cfg80211 subset (the vendor's own populated set). Drop the vendor
   `/sys/hisys/hipriv` VAP ABI in favour of `nl80211`/`add_virtual_intf`/
   `del_virtual_intf` and use the stock (unpatched) netifd `mac80211.sh`; if the
   vendor patched script must be kept for a bench experiment, provide a small
   hipriv-compatible shim instead. Calibration cannot be dropped: the vendor applies
   factory power/RSSI/XO parameters through the alg-config message path (g0 id 2), so the
   minimal port must carry either that path or an equivalent calibration upload.
   Vendor-proprietary and *not* required for a basic AP: `softapd`, `bandsteering`,
   the EasyMesh stack (`easymesh*`, `hyfibrd`, `libhyfi-bridge`), the
   `nlwifi` ABI, and the cloud agents. §6–§7.

4. **Recommended architecture and next phase.** One kernel module (or two: transport + WLAN
   MAC) per PCIe endpoint, exposing cfg80211 per band; internal layering mirroring the
   vendor: HCC message service (already prototyped as `hi5622_pciet_hcc.ko`) →
   cfg80211/netdev MAC layer → optional vendor-compat shim. Milestones M0–M6 in §8, each
   with a host-only gate and an attended bench gate.

5. **Interaction with the current bench blocker.** Nothing in the userspace analysis
   removes the current firmware-internal stall: the image's IRQ/thread layer never starts
   (`hcc_irq_probe` scheduler fingerprint absent), the ETE engine stays idle, and the
   H2D mailbox ack is never raised ([wifi-hcc-service](../wifi-hcc-service/REPORT.md) §7,
   [wifi-d2h-direct](../wifi-d2h-direct/REPORT.md) §4, ledger GP9/GP10). That blocker is
   **upstream of userspace**: a userspace process cannot run until a kernel driver consumes
   the ready handshake and finishes `hmac_main_init`/`wal_main_init` to register
   wiphys and netdevs. This lane therefore specifies the target interface and its gaps; it
   does not claim the interface is reachable on the bench today.

---

## 1. Inputs, version pins and method

### 1.1 Method

Static only: shell/config reading, binary string/symbol extraction (`strings`,
`readelf`, `objdump`, `nm` from the notebook toolchain), cross-reference
against the committed HCC/H2D analyses. No disassembler run was needed for the conclusion:
the vendor userspace ABI is visible at the script + symbol + string level, which is the
right layer for an *interface scope* document. Deeper per-field layouts are explicitly out
of scope and are listed as gaps in §7.

### 1.2 Version deltas that matter (TABLE T1)

* `FIRMWARE.bin` is **identical** in 2.4.15 and 2.5.24: 928,920 B,
  md5 `0e530b976d5a20e87358671f1a577695` [V], matching the bench's stock firmware used
  by every prior Wi-Fi lane. `cfg_device_hisi.ini` (799 B) and
  `cfg_hi5622v100_hisi.ini` (32,315 B) are the firmware-load descriptors
  (ITCM/DTCM/plat-custom/wifi-custom block addresses; see TABLE T1).
* The **Wi-Fi modules changed location between releases**: 2.4.15 rootfs carries
  `hi5622v100_{plat,wifi}.ko` in `/lib/hisilicon/ko/`; 2.5.24 carries them in
  `/lib/modules/5.10.201/` (the only version delta in that directory: 207 vs 198
  `.ko`). The shipped `wifi_init.sh`/`wlan_factory_init` still name the
  `/lib/hisilicon/ko/` paths; this works because `/sbin/insmod` is OpenWrt
  `kmodloader`, which resolves a **basename** into `/lib/modules/$(uname -r)/`
  when the literal path is missing — proven on-device in
  [phase16](../../phase16/boot-takeover.md) §1.3 [V\*].
* `hi5622v100_wifi.ko` in this 2.5.24 extraction is md5
  `4737fcb21a1a2262a96f84d780ad8b35` (the fixture the HCC/H2D lanes analysed);
  `hi5622v100_plat.ko` is md5 `23660bc285393e678d5cade1c36c194b`. The live bench
  box has been observed with an **overlay-patched** wifi.ko (md5 `e21629d2…` per
  phase16), so treat module hashes as version/overlay-dependent and re-pin before any
  ABI-adjacent bench run [V].
* Vendor userspace scripts split cleanly: `wifi_init.sh`, the calibration scripts,
  `etc/init.d/wpad`, `etc/init.d/softapd`, `etc/config/wifi_schedule` are
  **byte-identical** between 2.4.15 and 2.5.24; `/etc/config/wireless`,
  `/lib/netifd/wireless/mac80211.sh`, `/lib/netifd/hostapd.sh`,
  `/lib/netifd/netifd-wireless.sh` and `/sbin/wifi` differ (2.5.24 adds EHT/
  `be` handling, `MAC_OFFSET`/VAP-count logic from `hi_cfm`, and changes
  `channel auto` → `channel 0`). The userspace interface *surface* is
  unchanged; the command parameters moved.

---

## 2. Post-DEVICE-READY timeline: who does what

The rc.d order below is from `/etc/rc.d/` in the 2.5.24 rootfs; all paths are inside
the extracted rootfs. TABLE T2 has the file:line evidence.

1. **S09 `hsan_prepare`** — mounts factory/config/log/upgrade partitions; runs the
   chip-specific `/etc/rc.d/luofu` module script (pinctrl, PIE, LSW, GMAC, CFE,
   IPsec, HSLINK, `hi_woc_wifi_rtx.ko`, temperature, watchdog; no Wi-Fi). Creates
   device nodes.
2. **S15 `hsan_start`** — the only Wi-Fi loader: `/usr/bin/wifi_init.sh`. It
   selects the RF stack from `hi_cfm get board.wifi_vender`; on this board the value
   is empty ([phase16](../../phase16/boot-takeover.md) §1.2 [V\*]) so the **“shuangta”**
   branch runs: `insmod hi_kwificlk (missing — harmless)`, `hi_pcie`,
   `hi5622v100_plat`, `hi5622v100_wifi`, then `hi5622v100_cal_init.sh`.
   `insmod` of the Wi-Fi pair does the **PCI probe, firmware upload
   (`request_firmware` of `/lib/firmware/hi_wifi/FIRMWARE.bin` + the two
   `.ini` files), chip reset/ROM S0/S1 release, the DEVICE-READY exchange (g4 id1/id2),
   `hmac_main_init` (first H2D: TXQ sync g0 id1; calibration container load + save),
   `wal_main_init` (wiphy/netdev registration, cfg80211 state 5)**. Userspace sees only
   module load + `/dev`/`/sys` later.
3. **S16 `hsan_appm`** — platform only: sysctls, LSW/CFE queue tuning,
   `/usr/bin/hi_app start` (`hi_appm` = the “owal” app framework,
   `sysinfo/board/encrypt/upgrade` apps over `/dev/hi_ipc` + ubus),
   `/sys/devices/platform/sysenv/boot_done`. Not Wi-Fi.
4. **S18 `softapd`** — vendor WLAN helper daemon (`/usr/sbin/softapd`): the
   `wireless` ubus object (`up/down/notify/smt/smt*/wps*/wds*/roaming/acl*/debug/
   cleanup` per [opaque-binaries](../opaque-binaries/REPORT.md) §2.3); uses **nl80211
   genl + iwpriv (`set_priv_scan`, `ap_chan_scan`, `curscan`,
   `set_ie`, `kick_user`, `usernum`, `bintval`, `dtim_period`,
   `resp_hide`) + `/sys/hisys/hipriv` (`wds_sta_del`,
   `11k_set_neigh`, `11v_tx_request`) + its vendor `nlwifi_*` netlink
   wrappers**, and drives `hostapd_cli` for state. It also carries a WPA
   authenticator implementation (WPS/SMT/EasyMesh backhaul), which is why it is not just a
   small helper.
5. **S19 `wpad`** — stock OpenWrt combined `hostapd`+`wpa_supplicant`
   v2.10 (`/usr/sbin/hostapd` and `/usr/sbin/wpa_supplicant` are symlinks to
   `/usr/sbin/wpad`), started by procd with the global control sockets
   `/var/run/hostapd/global`, `/var/run/wpa_supplicant/global`. **Stock nl80211
   driver path** — no vendor binary patch was found in it (strings: `nl80211`,
   `Linux nl80211/cfg80211`, `wpa_supplicant v2.10`, ubus
   `config_add`/`config_remove`). The vendor integration is in the
   shell/scripts and the driver, not in wpad.
6. **S20 `network`** — netifd loads the **vendor-patched**
   `/lib/netifd/wireless/mac80211.sh` (2084 lines vs ~900 stock). This is where the
   VAP ABI lives: `echo "Hisilicon0 create $ifname $type $band" > /sys/hisys/hipriv`
   for each AP/STA VAP (line 640), `echo "$ifname destroy"` (664),
   `wds_vap_mode`, `sae_pwe`, `isolation_*`, `11k_switch`,
   `m2u_snoop_on`, `add_country_ie_switch`, `11v_cfg_bsst`,
   `radartool`, `set_regdomain_pwr`, `create_mld/delete_mld` (MLO) etc.;
   hostapd config is generated per VAP and handed to wpad over ubus
   (`ubus wait_for hostapd; ubus call hostapd config_add …`), wpa_supplicant likewise
   (`driver: nl80211`); per-VAP tuning follows with `iwpriv` (`txpower`,
   `frag_th`, `shpreamble`, `alg atf_mode`, `alg atf_min_prop`,
   `acs sw/idle_scan` …).
7. **S21 `hyfibrd`** — EasyMesh/HyFi L2 bridge daemon; not required for basic Wi-Fi
   (`hyctl attach br-lan`, `ubus call hyfibr init …`).
8. Later: `cmagent`/`cmsd` (cloud), `hcsh` (`hcshd`, dormant
   root-command UDP receiver), `easymesh`, `bandsteering` etc. —
   supervision/management, not driver interfaces.

**Ordering consequence:** the driver must have completed its boot/ready/post-ready init
during S15, i.e. **before** softapd/wpad/netifd can see a wiphy or a netdev. Any
clean-room replacement must preserve that: transport boot (GP1 pulse + upload + S1 +
ready) → HCC service → driver init (TXQ sync + wiphy/netdev registration) → only then can
userspace run its normal OpenWrt flow.

---

## 3. The userspace→kernel interface, surface by surface

### 3.1 cfg80211 / nl80211 (the primary interface)

`wal_cfg80211_init` (wifi.ko) registers a wiphy per band after
`multi_chip_loading`'s ready waits complete; the driver imports 26
cfg80211/wiphy/ieee80211 symbols and populates **39 of the 112 `struct cfg80211_ops`
slots** ([wifi-driver-re](../wifi-driver-re/REPORT.md) §2.2 [V]). Observed wiphy
configuration: interface_modes `0x1070` (AP_VLAN|WDS|MONITOR|NAN), per-band tables,
custom regulatory from the INI country tables. The vendor provides **71
`wal_cfg80211_*` functions** (callbacks plus helpers) [V, TABLE T3].

Consumers [V]: `wpad` (hostapd + wpa_supplicant), `iw`, `libiwinfo`/
`iwinfo` (`nl80211` backend strings), and the generic `nl80211` family is
also resolved by `softapd` for standard queries. There is no vendor-specific netlink
attribute use in `wpad`.

### 3.2 WEXT private ioctls (`iwpriv` / `iwconfig`)

`/usr/sbin/iwpriv` and `/usr/sbin/iwlist` are symlinks to
`/usr/sbin/iwconfig` (58,696 B); the driver registers the private-ioctl tables in
`.rel.rodata` (offsets 0x8a20–0x9be0 per [wifi-driver-re](../wifi-driver-re/REPORT.md)
§2.4): `alg` (cmd 0x0101) and `cali` (0x0102) dispatch,
`setcountry/getcountry`, `set_txpower/get_txpower`, `set_hwaddr`,
`get_channel`, `get_mode`, `set_wme_params`, `get_chipid/get_dieid`,
`get_version`, `get_sta_info`, `get_chan_list`, `get_rate_list`,
`get_temp`, `get_ant_rssi`, plus ~200 `wal_hipriv_*`/m2u/isolation/11v
handlers. Users [V]: `hi5622v100_cal_init.sh` (6 `alg set_*`),
`hi5622v100_cal_get.sh` (reverse, writes the factory text file),
`hdb_cmd/info/dump` (diagnostic battery), the existing
`opensource/tools/wifictl.sh` (`iwpriv Hisilicon0 alg …`) and
`wifi-cal-snapshot.sh` (24 `alg get_*` probes). The vendor netifd script also
uses the same WEXT surface for per-VAP tuning (`txpower`, `frag_th`,
`rts_th`, `shpreamble`, `car_cfg`, `noforward`, `he_gi`,
`probe_host_en`, `ie_rpt_en`, `set_oui_filter` with the Cudy OUI
`b4-4b-d6`, `set_rssi_acce`, `acs`, `alg atf_mode/atf_min_prop`), and
the legacy `/lib/wifi/mac80211.sh` path adds `set_max_user` (TABLE T5) [V].

### 3.3 `/sys/hisys/hipriv` (the VAP command pipe)

The base driver exposes a vendor sysfs command file: `plat.ko` creates the
`hisys` kobject (`kobject_create_and_add`/`sysfs_create_group` imports) and
`wifi.ko` provides the `hipriv` attribute and its 100-entry command table;
loading `wifi_debug.ko` extends it to **181 commands** (observed on-device:
`wc -l /sys/hisys/hipriv` = 100 → 181 after the debug module,
[phase7](../../phase7/firmware-ram-dump.md) §1.2 [V\*]). Grammar is
`<object> <command> <args…>` (e.g. `Hisilicon0 create vap0 ap 2g`,
`vap0 isolation_mode 4`, `Hisilicon0 stat dump_hdpp`).

The driver exports **140 unique `wal_hipriv_*` symbols** [V]; the ones used by shipped
scripts are TABLE T4. This is the surface a clean-room port should **replace**, not
reproduce, except where a bench experiment depends on the vendor script sequence. A
compatibility shim is cheap because every command the scripts use is a line of text on one
attribute.

### 3.4 Vendor netlink (softapd `nlwifi`)

`softapd` carries a vendor netlink client: symbols `nlwifi_init`,
`nlwifi_hi_wifi_init`, `nlwifi_hi_wifi_rcv_msg_func`, `nlwifi_set/get_*`,
plus libnl `genl_connect`/`genl_ctrl_resolve` for `nl80211` itself. The
driver side is a **vendor raw-netlink ("oam netlink")** layer in `plat.ko`:
`oam_main_init_netlink` → `oam_netlink_kernel_create` (on top of
`__netlink_kernel_create`), `oam_netlink_kernel_recv/send(_ex)`, and an
ops-registration API `oam_netlink_ops_register/unregister` keyed by a type byte; wifi.ko
registers its op sets as `g_st_nl_hiwifi` / `g_st_nl_hiwifi_multiap` and imports
`genlmsg_put`/`nla_put` for formatting, but **no `genl_register_family` was
found** — this is a raw-netlink vendor protocol, not a standard generic-netlink family.
wifi.ko also keeps a per-netdev pid registry (`dev_netlink_create`,
`dev_netlink_info_add/del/find/getpid/update/dump`, logs
“add_netdev_name:%s, netlink_info_cnt:%d”, “pid:%u”). On the bench the stock-runtime health line
reports **`netlink_info_cnt:6`** ([phase49](../../phase49/README.md) [V\*]) — six
netdev names registered for event delivery, consistent with the 6 netdevs of the default
config (4 AP + 2 inactive STA VAPs).

What is still opaque: **the nlwifi message/attribute layout** (and which of the two driver
transports — the raw-device netlink vs the `hiwifi` genl families — each `nlwifi_*`
call uses). [opaque-binaries](../opaque-binaries/REPORT.md) §2.4 already pinned this as
kernel-side. For the clean-room port this surface can be **dropped** unless EasyMesh/SMT
compatibility is required; standard cfg80211 has no equivalent and none of the basic AP
flows need it.

### 3.5 `/proc` and debug surfaces

`wal_add_vap_proc_file` creates per-VAP proc entries; `hdb_cmd/info/dump` reads
`/proc/vap0/ap_info`, `/proc/vap8/ap_info` [V]. `wifi_debug.ko` adds
`/sys/hisys/plat/pcie` with `savemem`/`readmem` device-RAM commands used by
the notebook's read-only tooling ([phase7](../../phase7/firmware-ram-dump.md) [V\*]). These
are diagnostic; none is required for Wi-Fi function.

### 3.6 Look-alikes that are NOT Wi-Fi driver interfaces

* `/dev/hi_ipc` (`hi_kipc.ko`) + ubus + `libhi_owal_ssf.so` — the “owal”
  app framework used by `hi_appm` (apps `sysinfo,board,encrypt,upgrade`) and by
  `bandsteering`/`easymesh`. Platform IPC, not WLAN.
* `hi_cfm` (9 KB ELF + `libhi_cfm.so`) — board-info lookup from
  `/usr/local/factory/boardinfo` and `/config/conf/cfm`, used by
  `wifi_init.sh`, `hsan_*`, and the netifd script (`hi_cfm get
  board.wlan24/wlan58`). It does not open the Wi-Fi driver.
* `hcshd` (S98, 9,968 B) — dormant UDP command receiver (RSA-signed datagrams →
  `popen`); unrelated to Wi-Fi ([opaque-binaries](../opaque-binaries/REPORT.md) §2.2).
* `hyfibrd` / `hyctl` / `libhyfi-bridge.so` — EasyMesh/HyFi L2 relay over
  HSLINK/`hi_swa`; works *above* the Wi-Fi netdevs, not as a driver ABI.
* `bandsteering`, `easymesh*`, `cmagent`/`cmsd` — management/cloud.
  Not required for a first AP.

---

## 4. Component classification (TABLE T8)

**Essential (keep stock):** `wpad` (hostapd + wpa_supplicant v2.10), netifd +
`/lib/netifd/*`, `iw`, `iwinfo`/`libiwinfo`, `hostapd_cli`/
`wpa_cli`, the OpenWrt network stack, `hi_cfm`-equivalent board data (or
DTS/UCI), the factory calibration data itself.

**Essential to reproduce, vendor-owned:** the kernel WLAN driver (cfg80211 full-MAC + HCC
backend), the VAP lifecycle semantics, the calibration application path (`alg` config
messages and/or `.kv` container load), regulatory/country tables from
`cfg_hi5622v100_hisi.ini`.

**Vendor-proprietary, replaceable/droppable for a basic AP:** `softapd`
(ACS/WPS/11k/11v/ACL/TWT/SMT/EasyMesh helpers), `bandsteering`, `easymesh*`,
`hyfibrd`/`hyctl`/`libhyfi-bridge`, cloud agents, the `nlwifi` netlink
ABI, `/sys/hisys/hipriv` (drop, or shim), the vendor netifd `mac80211.sh` patches
(drop in favour of stock + nl80211 `add_virtual_intf`), `/etc/hi_wifi/*ssid.conf`
(only for other RF variants).

**Diagnostic-only:** `hdb`/`hdb_cmd`, `wifi_debug.ko`,
`/sys/hisys/plat/pcie`.

---

## 5. Startup command trace and HCC mapping

The mapping below combines the userspace evidence (this lane) with the committed HCC
catalogues; confidence is per row. The full table with evidence links is TABLE T6.

| # | userspace action / script | kernel entry | HCC H2D (group/id) | D2H response / event |
| --- | --- | --- | --- | --- |
| 0 | *(none — kernel driver)* firmware upload + S1 + ready | `request_firmware`, ROM/PCIe, `multi_chip_loading` | *(nothing until ready)*; then **g0 id1 TXQ-table sync** from `hmac_main_init` | g4 id1 `device_plat_ready`, g4 id2 `host_ready` (driver consumes) |
| 0b | *(none — kernel driver)* wiphy/netdev creation, cal container save | `hal_main_init` → `hmac_main_init` → `wal_main_init` | g0 id1 (TXQ); device config events as needed | — |
| 1 | `hi5622v100_cal_init.sh` — 6× `iwpriv Hisilicon0 alg set_*` from `/usr/local/factory/hi5622v100.cal` | WEXT 0x0101 → `wal_hipriv_alg_cfg` → alg process table name→cfg_id | **g0 id2** `hmac_config_alg_send_event` (cali/alg cfg blob) | **g1 id3** `hmac_event_config_syn` → `hmac_sync_dmac_alg_cfg_rsp_entry`; completes the wait |
| 2 | netifd `mac80211.sh`: `echo "Hisilicon0 create <ifname> <type> <band>" > /sys/hisys/hipriv` | `wal_hipriv_add_vap` (also `add_vap_proc_file`, netdev registration) | user/vap-add family `hmac_user_add` **g0 id7** / `hmac_user_add_notify_alg` g0 id8 **[I]** | cfg/user sync via g1 id3 / station events |
| 3 | hostapd via `ubus call hostapd config_add` then nl80211 `START_AP` | `wal_cfg80211_start_ap` / `_start_req` → `wal_send_cfg_event` (WAL cfg event, group 2) + `hmac_config_send_event` g0 id1 for hmac-layer configs | **g2 cfg event** (exact id = WAL cfg command id [U]); `start_ap_write_msg` path [V symbol] | g1 id3 syncs; beacon/sta events |
| 4 | per-VAP setup lines (`sae_pwe`, `isolation_*`, `11k_switch`, `11v_cfg_bsst`, `m2u_snoop_on`, `add_country_ie_switch`, `radartool`, `wds_vap_mode`) | `wal_hipriv_*` (sae_pwe, isolation_*, 11k_switch, …) → cfg-event helpers (`wal_hipriv_send_cfg_uint32_data` / `_without_data`) | g0 id1 / g2 cfg events (per command) **[I]** | g1 id3 |
| 5 | `iwpriv <vap> txpower/frag_th/rts_th/shpreamble`, `alg atf_mode`, `alg atf_min_prop`, `acs sw/idle_scan` | WEXT priv ioctls → `wal_hipriv_*`/alg LUT | g0 id2 for `alg`; g0 id1/g2 for the rest **[I]** | g1 id3 |
| 6 | STA scan: `iw … scan`, hostapd ACS, softapd `iwpriv set_priv_scan/ap_chan_scan/curscan` | `wal_cfg80211_scan` → `hmac_enc_scan_send_req` | **g0 id0xC** (payload 0x1FC [U]) | chip **g1 ids 15,16,17** (scanned BSS / scan comp / chan result) + **g2 id2** `hmac_handle_scan_rsp_sta`; driver calls `cfg80211_inform_bss_frame_data` |
| 7 | STA connect / AP station lifecycle (hostapd/wpa_supplicant) | `wal_cfg80211_connect/start_connect`, `add/del_station`; `hmac_user_add/del_user_notify_dmac` | **g0 id0xF/0x10** (`hmac_sta_sync_join_req_params` / `hmac_sta_wait_join…`), **g0 id7/8/9** users, connect-result g0 id0x11 | g2 ids 3/4/5/6 (asoc/connect/disconnect), g1 id3, `cfg80211_connect_done`/`del_sta_sinfo` |
| 8 | keys / security (nl80211 add_key, PMKSA, mgmt frames) | `wal_cfg80211_add_key/remove_key/set_default_*` → `wal_send_cfg_event`; `mgmt_tx` | g2 WAL cfg events (ids [U]); mgmt tx/mgmt event families | cfg80211 mgmt notifications; g2 id9/0xd etc. |
| 9 | WDS / 11k / 11v / EasyMesh (optional) | `wds_sta_*`, `11k_*`, `11v_*`, `multiap_*` hipriv/nlwifi handlers | g0 ids 0x1d/0x2b/0x2c/0x31 etc.; multiap g2 0x25/0x26 | mgmt/rrm events (g1/g2) |
| 10 | PM / green AP (optional) | `wal_hipriv_green_ap`, power-save cfg | cfg events | g3 id17 `hmac_d2h_pm_event` |

**Cross-check that the model is complete for a default boot.** The stock-runtime health
lines report 4 hostapd VAPs `AP-ENABLED` (vap0/vap3/vap8/vap11), **`alg:[SUCC]
x14`**, and `netlink_info_cnt:6` ([phase49](../../phase49/README.md) [V\*]).
`14 = 6` calibration `alg set_*` + `4 AP × 2` per-AP alg writes
(`atf_mode`, `atf_min_prop`). That arithmetic matches the script inventory
exactly and is the strongest confirmation that TABLE T6's sequence is the real boot order,
not a reconstruction.

**What userspace does *not* send.** No H2D message is sent to *make* DEVICE READY happen
([wifi-hcc-re](../wifi-hcc-re/REPORT.md) §2.4/§7 [V]); userspace post-ready messages are
all configuration/scan/management, which is exactly the layer the vendor driver builds.
The clean-room port must therefore split responsibilities the same way: the transport/
HCC-service layer owns boot/ready; the WLAN layer owns cfg80211 commands.

---

## 6. Minimum viable interface for the clean-room driver

### 6.1 Userspace policy

Use **stock** OpenWrt userspace and remove the vendor scripts from the path:

* `wpad` (hostapd + wpa_supplicant) — unchanged, stock nl80211.
* netifd + **stock** `/lib/netifd/wireless/mac80211.sh` — its standard flow creates
  interfaces with `iw phy <phy> interface add` and then hands config to hostapd; that
  maps cleanly onto cfg80211 `add_virtual_intf`/`del_virtual_intf`. The vendor
  `mac80211.sh` patches (hipriv create/destroy, `MAC_OFFSET`, `hisys`
  knobs, MLO `create_mld`) are only needed if a bench experiment must reproduce the
  vendor script *verbatim*.
* `iw`, `iwinfo`, `hostapd_cli`, `wpa_cli` — unchanged.
* Board data: replace `hi_cfm`-dependent values (`board.wlan24`,
  `board.wlan58`, `board.wifi_vender`, `sysinfo.factory`, MAC layout) with
  DTS/UCI/board.d values.
* Calibration: keep the factory data and its application (see §6.3) — this is the one
  vendor userspace piece that cannot simply be dropped, because it configures RF power
  and RSSI limits that the firmware consumes.

### 6.2 Kernel surface to implement (cfg80211 subset)

Mirror the vendor's populated op set ([wifi-driver-re](../wifi-driver-re/REPORT.md) §2.2):
`add/del/change_virtual_intf`, `start_ap`/`stop_ap`/`change_beacon`,
`change_bss`, `add_key`/`get_key`/`del_key`/
`set_default_key`/`set_default_mgmt_key`, `add_station`/`del_station`/
`change_station`/`get_station`/`dump_station`, `scan` (+ optional
`sched_scan_start/stop`), `connect`/`disconnect`, `set_wiphy_params`,
`set_bitrate_mask`, `dump_survey`, `mgmt_tx`/`mgmt_frame_register`/
`mgmt_tx_cancel_wait`, `set_power_mgmt`, `remain_on_channel`,
`get_channel`, plus the vendor extras that matter for EasyMesh later
(`external_auth`, `set_mac_acl`, `set_blacklist*` — optional). Per-band
wiphy with interface_modes AP/STA/AP_VLAN(WDS)/MONITOR.

Backend contract (from [wifi-hcc-re](../wifi-hcc-re/REPORT.md) §8 and
[wifi-h2d-catalog](../wifi-h2d-catalog/REPORT.md) §6):
1. H2D builder API: 12-byte HCC header + payload, one SR node + mailbox id-3 announce,
   ring credit/back-pressure, never post mailbox id 5.
2. D2H dispatch framework with the vendor tables (chip g4 boot 5; chip g3 5; chip g2
   12+19; chip g1 28; core g0 7) so cfg80211 events reach the right object.
3. Data path: hdpp consumers on chip queue 2 / core queue 0, netdev TX/RX hooks.
4. Driver init order: ready → `hal/hmac/wal` init → wiphy register → cfg80211 state,
   with the first H2D being the TXQ sync.

### 6.3 What must be reproduced from the firmware/HCC layer (gaps)

See §7 and TABLE T9. In short: message **payload structs** for config (g0 id1/g2 cfg
events), user add (g0 id7/8, 0xA8 B), scan request (g0 id0xC, 0x1FC B), key/security
messages and the `.kv` calibration container are not recoverable from the firmware
image alone; the vendor modules are the only static source, and they are the subject of the
open GPL request. Until that source (or a black-box trace) exists, the clean-room kernel
driver can implement the framework and the *known* messages, but AP/STA control paths are
gated on those layouts.

---

## 7. Gap analysis

| # | gap | status | impact |
| --- | --- | --- | --- |
| G1 | cfg80211 config-message payload structs (start_ap/keys/stations; vendor `wal_send_cfg_event` blobs) | [U] (framework [V], payload [U]) | blocks M3 (AP) and M5 (STA) |
| G2 | `hmac_config_*` group-0 id-1/id-2 payload fields beyond the shared helper (payload+8/+0xA/+0xC) | [I] partial (h2d-catalog §5.2) | TXQ sync/alg config scaffolding possible; full config not |
| G3 | user-add/del message (0xA8 B) field layout | [I] partial (h2d-catalog §5.2) | VAP/station creation |
| G4 | scan request (0x1FC B) + scan-result events layout | [U]/[I] | scan/ACS, STA join |
| G5 | hdpp data path (TX/RX queue setup, netdev hookup, BA) | [U] for clean-room | data plane after AP-ENABLED |
| G6 | calibration: `.kv` container layout, message content and firmware-side application | [U] (loaders/savers [V] in phase8) | RF power/RSSI correctness; AP may associate without full cal but TX power/regulatory are wrong |
| G7 | nlwifi vendor-netlink attribute layout | [U] | only needed for vendor softapd/EasyMesh compatibility; droppable |
| G8 | VAP-create semantics inside the driver (what `wal_hipriv_add_vap` sends to firmware) | [U] | removable if cfg80211 add_virtual_intf is implemented instead |
| G9 | per-endpoint (2.4G vs 5G chip) instantiation: one firmware/ready exchange per PCIe function, two wiphys | [I] (2 endpoints + phy0/phy1 observed) | must be designed in from the start, not retrofitted |
| G10 | firmware post-ready IRQ/thread layer (current bench blocker) | [V\*] negative (hcc-service/d2h-direct) | **gates every milestone below M1**; userspace cannot run until the HCC command service runs |
| G11 | vendor GPL source (module licence strings are GPL; firmware not) | open request | fastest path to G1–G6 |

---

## 8. Recommended architecture and milestones

**Architecture.** Two kernel modules or one with two layers:

```nuserspace:  wpad (hostapd+wpa_supplicant)   netifd   iw/iwinfo   [optional vendor compat]
                        |  nl80211/cfg80211       |  nl80211
 ======================= kernel ====================================================
 cfg80211 full-MAC driver  — wiphy+netdev per band (2 endpoints)
   ^ per-op handlers: start_ap/scan/connect/key/station/mgmt_tx   (M2-M5)
 hcc message service (H2D builders + D2H dispatch tables, SR/DR, mailbox id3, credit)
   (extends the proven hi5622_pciet_hcc.ko service, M1)
 pcie transport: BAR windows, ETE rings, GPIO pulse, upload, release, S1/ready (exists)
 ====================================================================================
 firmware (vendor blob, per endpoint; boot/ready already decoded)n```

**Milestones** (each has a host-only gate: compile + selftest + review; bench gates are
attended only, per the project rules):

* **M0 — transport/ready (done).** GP1 pulse, upload, release-only, S1, ready pair
  received (committed `hcc-service`/`d2h-direct` modules). Gate met.
* **M1 — HCC message service in kernel.** Register the D2H tables; H2D builder API with
  SR announce, credit/back-pressure, sysfs debug; selftests on frame/ring builders.
  *No new firmware behaviour claimed.* Gate: module loads and the ready pair + D2H
  catalogue still received.
* **M2 — cfg80211 shell.** Two wiphys/netdevs, `add_virtual_intf`/`del_virtual_intf`
  mapped to the firmware VAP object (either via cfg event or the vendor-ish create
  semantics), regulatory from INI/DTS, stock netifd path reaches `wifi up`.
  Gate: hostapd/wpa_supplicant start and `iw phy`/`iw dev` enumerate.
* **M3 — AP control path.** start_ap config, beacon, keys, station events → four APs
  `AP-ENABLED` equivalent on our image. Gate: hostapd reports enabled, beacons
  counted from a monitor vif.
* **M4 — data path.** hdpp queues + netdev TX/RX; first DHCP/ICMP through an AP.
  Gate: ping through the AP from the bench host.
* **M5 — scan/STA.** scan request/results, connect/join, key handshake.
  Gate: `iw scan` returns BSSes; STA associates to a trusted test AP (never the bench
  router in a way that breaks recovery).
* **M6 — calibration/power/optional extras.** Calibration application, country/regulatory,
  power limits; optional `/sys/hisys/hipriv` compatibility shim and 11k/11v/ACS
  extras only if the vendor userspace is to be reused.
* **M7 (conditional) — vendor compatibility.** If a bench experiment requires the vendor
  softapd/EasyMesh stack, port the `nlwifi` netlink ABI and hipriv shim; otherwise
  skip.

**Estimate anchor:** the userspace/calibration workstream is the smallest of the port
workstreams in [wifi-thermal-re](../wifi-thermal-re/REPORT.md) §2.6 (1–2 engineer-weeks);
the bulk is source/ABI port and bench bring-up. That remains true after this analysis —
the interface is not the hard part, the closed message payloads are.

---

## 9. What this lane changes for the bench plan

1. **It removes a false dependency.** No userspace process, hi_cfm call, or hcc utility is
   involved in the DEVICE READY handshake or in the `0xCECE`/release ordering; the
   kernel driver owns it. Bench scripts should not look for a userspace trigger after
   release.
2. **It names the first three things to observe once the command service runs:** (a) the
   post-ready TXQ sync g0 id1 from driver init, (b) 6 alg config writes g0 id2 with g1 id3
   responses, (c) VAP-create/user-add traffic when netifd runs. Those are the natural
   bench probes for M1/M2 and they are all read-only observes (no extra writes).
3. **It defines the cleanup path for vendor userspace:** drop `softapd`/EasyMesh and
   the patched netifd script in favour of stock userspace + cfg80211; keep the calibration
   data and its application. This is the same direction already recommended in
   [wifi-thermal-re](../wifi-thermal-re/REPORT.md) §2.6 item 4, now backed by the full
   call-site inventory.
4. **It does not change the blocker.** The firmware post-ready IRQ/thread layer
   ([hcc-service](../wifi-hcc-service/REPORT.md) §7, ledger GP9/GP10) remains the gate:
   until it runs, the HCC command service cannot deliver, and no cfg80211 op can reach the
   firmware. Userspace analysis is not the bottleneck and must not be used to justify
   speculative writes.

---

## 10. Reproduction, provenance and limits

* All extractions above are reproducible with `tools/regen-evidence.sh` against the
  local rootfs (`/tmp/rootfs2524b`, optionally a second rootfs as argument). The
  script prints only names/paths/hashes/symbol lists — no vendor bytes.
* Artifact pins: see TABLE T1 (rootfs versions, firmware md5, module md5/size, script
  hashes). The local extractions are **not evidence** per project rules until a claim is
  added to the ledger/EVIDENCE-INDEX by the lead; this document cites the committed prior
  lanes for the runtime claims ([V\*]).
* No vendor binary, firmware byte, calibration value, DTS extraction or per-unit identity
  is included. The report deliberately contains no command output that embeds vendor code
  or data — only symbol/string names needed to describe the interface.
* Limits: this is a **scope** document, not a wire-format spec. The payload layouts of the
  config/scan/user messages and the `.kv` container are explicitly left to the
  kernel-side/GPL-source lanes (G1–G6). The nlwifi attribute layout is left to the
  softapd/EasyMesh lane if it is ever needed.
