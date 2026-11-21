# Wi-Fi source hunt: existing source for the HiSilicon HCC `hmac`/`wal_cfg80211` driver family

Date: 2026-11-21 (host-only, read-only research lane `wifi-source-hunt`).
Target chip stack: Cudy WR3000 v2.0 = Hi5671YV200 "luofu" SoC + Hi5622V100 Wi-Fi
(`hi5622v100_plat.ko` + `hi5622v100_wifi.ko`, full-MAC cfg80211 driver). Symbol families:
`hmac_*`, `wal_cfg80211_*`, `hdpp_*`, `alg_*`, `shuangta_*`, `hal_*`, `oal_*`,
`oam_*`, `hcc_*`, `pcie_*`, `bal_*`, `mac_*`, `dmac_*`, firmware handlers
`smac_msg_proc_*`, firmware banner `ChenTangV100R001C20T13`. Goal kernel: 6.18.

Search surfaces used: GitHub API (repo + recursive tree), gitee API (tree/raw),
gitcode API (tree), grep.app, searchcode.com, general web search (EN/RU/CN), vendor GPL
centers (Cudy, Jeton, Huawei, Tenda/TP-Link/ZTE/JCG checked indirectly), HiSTBLinux /
HiRouter SDK dump repos, OpenWrt forum / OpenIPC / 4pda / right.com.cn / acwifi / efxn.

Notebook evidence reused (not re-derived): ledger 2026-10-09 (TR6560 cousin-chip BSP,
`peanut_wifi.ko` shares 1403 `hmac_*` + 677 `wal_*` symbols), `docs/phase29` +
`docs/phase29/jeton-shared-sdk-proven.md` (Jeton = same `luofu_V200` SDK, byte-identical
wifi blob), `docs/GPL-REQUEST.md`, `docs/TIMELINE-AND-WAY-FORWARD.md` item 1.

## 1. Headline

1. **No public source for the exact Hi5622V100 / luofu driver exists** (re-verified today:
   zero repos matching `hi5622`/`hi5671` on GitHub; gitee code search is login-walled and
   shows nothing public; mainline has no Hisilicon full-MAC driver).
2. **Two license-clean sources of the SAME driver family DO exist**: OpenHarmony's
   `device_soc_hisilicon` (complete hi3881v100 driver: `wal`/`hmac`/`mac`/`hcc`/`oal`/
   `oam`/`frw`, GPL-2.0 file headers, HiSilicon copyright) and the `hongmengkernel`
   GitHub tree (linux-5.10-lts `drivers/connectivity/hi11xx/hi1105/wifi`, 875 files incl.
   `dpe` data-path engine, `oam_adapter`, `chip`, `customize`, `main`).
3. Those two cover the **shared architecture** of `hi5622v100_wifi.ko`; the chip-specific
   PCIe HAL (`bal_*`, `alg_*`, `hal_*`, `pcie_*`, device-side `dmac_*`) and the
   firmware blob remain closed. Adaptation is therefore a structural adoption of the shared
   layers plus original chip work — consistent with ROADMAP-OPENWRT.md options 2/3, and the
   measured position in `docs/PORT-SURFACE.md`.

## 2. Ranked candidate table

| # | Candidate | URL | License | Chip family | Symbols | Completeness | Kernel | Verdict |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | **OpenHarmony hi3881v100 driver** (in `device_soc_hisilicon`) | https://gitee.com/openharmony/device_soc_hisilicon/tree/master/common/platform/wifi/hi3881v100/driver (mirror: https://gitcode.com/suxiansheng32/device_soc_hisilicon/tree/master/common/platform/wifi/hi3881v100/driver) | File headers GPL-2.0 (HiSilicon copyright); repo LICENSE lists `common/platform` as Apache-2.0 — dual-labeled, treat driver .c/.h as GPL-2.0 | Hi3881V100 (SDIO combo, Hi3516/Hi3518 IPC) | `wal_cfg80211_*` (wal/wal_cfg80211.c, wal_mac80211.c), `hmac_*` (35+ files), `mac_*`, `hcc_*` (hcc/hcc_comm|hmac|host|slave|task), `oal_*` (40+ files incl. oal_cfg80211.c), `oam_*` (oam_ext_if.h), `dmac_ext_if.h`, `frw_*` | Driver-only but complete wal/hmac/mac/hcc/oal for that chip; HDF adapter + linuxkpi present | HDF (LiteOS/Linux); linux Makefiles target 4.x/5.10 | **BEST adaptation base for the shared layers** |
| 2 | **hongmengkernel — hi1105 wifi driver** (linux-5.10-lts tree) | https://github.com/5-Super-Rookie-5/hongmengkernel/tree/HEAD/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/wifi | Linux kernel tree = GPL-2.0; mirror lacks per-file headers (provenance caution: unofficial re-upload; official OH kernel branches no longer carry it) | Hi1105 (phone combo, SDIO) | `hmac_*` (incl. aiwifi/chba/cali sub-layers), `wal_*`, `mac_*`, `oam_adapter`, `dpe` (data-path engine — the `hdpp_*` analog), `chip`, `customize`, `main` | Driver-only, 875 files — the richest public copy of the family; also hi1102a (2 files, negligible) | Linux 5.10 | Strong second; use for dpe/oam/chip layers; pin provenance caveat |
| 3 | Cudy WR3000 v2.0 GPL archive | https://www.cudy.com/en-apac/pages/download-center/wr3000-2-0 (GPL tab present, no file listed; JS-loaded) | would-be GPL-2.0 (obligation) | **exact chip** | n/a (not published) | n/a | 5.10.201 | **NOT published.** Only public Cudy GPL is the v1/MediaTek archive (see §5) |
| 4 | Jeton AX3000 Core GPL archive | https://www.jetontechno.com/product/ax3000-core/ (firmware-only Downloads; no GPL link on EN/RU pages, FAQ likewise) | would-be GPL-2.0 (second obligor) | **same SoC** Hi5671YV200 + Hi5622, same `luofu_V200` SDK, wifi blob md5-identical to ours | n/a (not published) | n/a | 5.10.201 | **NOT published.** Request channel remains open (letter `JETON-GPL-EMAIL.txt` drafted, unsent by human decision 2026-10-04) |
| 5 | Huawei consumer Open Source Release Center (AX3 / TC7102 / WS7200 — Hi5651T/Hi5651L + **Hi1152**, the AX3000 DBDC sibling of Hi5622) | https://consumer.huawei.com/en/opensource/ | would-be GPL-2.0 | Hi1152 (acwifi: "Hi5622 等同 Hi1152"), Hi5651 CPU | n/a — no drop found | n/a | vendor kernels | **Request-only portal.** Community GPL request for AX3 Pro unanswered (OpenWrt forum 66551); gitcode "TC7102 openwrt" repos are paid/AI-content, not clean |
| 6 | Banana Pi THG6500-TAX2 OpenWrt BSP (Triductor TR6560 cousin chip) | https://github.com/BPI-SINOVOIP/THG6500-TAX2-OPENWRT-BSP (wifi: package/triductor/tr5220/files/ko/peanut_{wifi,plat}.ko) | GPL-2.0 (OpenWrt tree) but wifi = prebuilt .ko | TR6560/Triductor + "peanut" HCC wifi | `hmac_*` 1403 / `wal_*` 677 / `oal_*` / `hcc_*` in the .ko (ledger 2026-10-09) — **no source** | Platform BSP complete, wifi source absent | 5.10.138 | Platform reference + driver-contract oracle via symbols; NOT a wifi source |
| 7 | HiSTBLinux SDK dumps (JasonFreeLab SPC050/SPC060, 07bug, AyFun, tegzwn, LeeSSXX, wangyunlin545259389) | https://github.com/JasonFreeLab/HiSTBLinuxV100R005C00SPC050 , https://github.com/JasonFreeLab/HiSTBLinuxV100R005C00SPC060 | **No license = leak**, NOT-usable | Hi3798M/C/MV300 STB | none of ours: `source/component/wifi/drv` = bcmdhd/mt76/qca/rtl only (checked tree) | full SDK | 4.4.35 | NOT-usable; symbol/platform reference only |
| 8 | XePeleato/Hi1101_HisiWifi | https://github.com/XePeleato/Hi1101_HisiWifi | none stated (phone-SDK-era dump) | Hi110x (HSDIO) | `hwifi_*` cfg80211/hcc/msg layer — older sibling naming | driver-only | ~4.x | Reference-only (license unclear); superseded by #1/#2 |
| 9 | themactep/wifi-hi3881 | https://github.com/themactep/wifi-hi3881 | none stated | Hi3881 | same driver as #1 without license hygiene | driver + demo apps | 4.x/5.x | Reference-only; use #1's clean copy instead |
| 10 | gtxaspec/ws73v100-wifi | https://github.com/gtxaspec/ws73v100-wifi | none stated | WS73V100 (SLE/NearLink, different product line) | platform/hcc (HCC concept shared; SLE, not Wi-Fi MAC) | chip SDK subset | 5.10 | Low value; HCC-architecture reference at most |
| 11 | propmok/BPI_WIFI6_TR6560 | https://github.com/propmok/BPI_WIFI6_TR6560 | n/a | TR6560 | none — repo is a README + a sysupgrade .rar | binary only | 5.10 | NOT a source |

## 3. Candidate details (record fields)

### 3.1 OpenHarmony `device_soc_hisilicon` — hi3881v100 driver (rank 1)
- **URLs:** tree https://gitee.com/openharmony/device_soc_hisilicon/tree/master/common/platform/wifi/hi3881v100/driver ;
  `wal_cfg80211.c` https://gitee.com/openharmony/device_soc_hisilicon/blob/master/common/platform/wifi/hi3881v100/driver/wal/wal_cfg80211.c ;
  LICENSE https://gitee.com/openharmony/device_soc_hisilicon/blob/master/LICENSE ;
  GitHub-reachable mirror via gitcode https://gitcode.com/suxiansheng32/device_soc_hisilicon/tree/master/common/platform/wifi/hi3881v100/driver
- **License:** file headers are GPL-2.0 ("Copyright (C) 2021 HiSilicon (Shanghai) Technologies CO., LIMITED ... GNU General Public License ... version 2 ... or (at your option) any later version") — verified on `wal_cfg80211.c`; repo LICENSE additionally declares `common/platform` Apache-2.0 (dual-labeled; the .c/.h headers govern the driver code).
- **Chip:** Hi3881V100, SDIO Wi-Fi/BT combo for Hi3516/Hi3518 IPC SoCs. Same HiSilicon HCC architecture, one generation away from the router line.
- **Symbols present:** `wal_cfg80211_*` (wal_cfg80211.c, wal_mac80211.c, wal_cfg80211.h), `hmac_*` (mac/hmac/: hmac_11i, blockack, cali_dpd, chan_mgmt, config, crypto_tkip, device, edca_opt, encap_frame{,_ap,_sta}, event, ext_if, frag, fsm, main, mgmt_{ap,sta,bss_comm,classifier}, p2p, protection, rx_data, rx_filter, scan, sme_sta, traffic_classify, tx_data, uapsd, user, vap, wapi*, wow), `mac_*` (mac/common/: mac_cfg/data/device/frame/ie/mib/pm_driver/regdomain/resource/user/vap), `hcc_*` (hcc/: hcc_comm, hcc_hmac, hcc_host, hcc_slave, hcc_task, hmac_hcc_adapt), `oal_*` (oal/: oal_cfg80211, oal_main, oal_mem, oal_net, oal_sdio_host, oal_chr, ...), `oam_*` (oam_ext_if.h), `dmac_ext_if.h` (device-MAC interface), `frw_*` (frw/: frw_main/task/timer/event[_sched]).
  Absent here: `hdpp_*`, `alg_*`, `shuangta_*`, `hal_*`, `bal_*`, `pcie_*` (chip/board-specific; transport here is SDIO).
- **Completeness:** driver-only; includes hdfadapt/ (HDF driver registration, net adapter) and linuxkpi/ headers; BUILD.gn + Linux Makefiles.
- **Kernel:** built for OpenHarmony HDF (LiteOS + linux); the Linux Makefile path targets 4.x/5.10 kernels.

### 3.2 hongmengkernel hi1105 wifi (rank 2)
- **URL:** https://github.com/5-Super-Rookie-5/hongmengkernel/tree/HEAD/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/wifi ; architecture doc (DeepWiki, page kept): https://deepwiki.com/5-Super-Rookie-5/hongmengkernel/3.2-hisilicon-connectivity-drivers-(hi11xx-wifibtgnss)
- **License:** GPL-2.0 by being a Linux kernel tree; the mirror's files lack individual license headers (GBK/UTF-8 mixed, headers stripped) — provenance caveat: prefer #1 for vendored text, use this for structure/symbols or obtain the same driver from an official OH tree if one reappears.
- **Chip:** Hi1105 (phone connectivity combo) + hi1102a (2 files only).
- **Symbols:** `hmac_*` (incl. aiwifi/, ba/, cali/, chba/ sub-layers — richer than #1), `wal_*`, `mac_*`, `oam_adapter`, `dpe` data-path engine (closest public analog to the `hdpp_*` data-path family), `chip`, `customize`, `main` (plat/board layer), `inc`.
- **Kernel:** linux-5.10-lts (in-tree Kconfig/Makefile under drivers/connectivity/hi11xx/).

### 3.3 Cudy GPL (exact chip) — checked today
- Product download-center WR3000 2.0 page has a `gpl-files` section/tab but lists **no file** (content is JS-loaded; no static GPL link anywhere on the page).
- The only public Cudy GPL archive is the **MediaTek v1** one: Google Drive id `1und0klzZ22tw4VvS7hxvMr5BmfwD-o0m`, resolved filename `GPL_for_AX3000_mt7981.tar.gz` (47,208,344 bytes) — WR3000 v1 (MT7981B), not our HiSilicon v2.
- GPL-REQUEST.md status stands: request sent 2026-10-01, no reply held.

### 3.4 Jeton (same SoC) — checked today (see §4 for the lead resolution)
- Product page lists firmware only: `Jeton-AX3000-CORE_V2.1.00-260819`, `...V3.1.00-260924`, and a newer `Jeton-AX3000-Core_V3.1.00-261026_JETONTECHver.zip` (2026-11) — no GPL/source link on EN or RU pages; FAQ explains firmware download only.

### 3.5 Huawei (Hi1152 sibling) — request-only
- https://consumer.huawei.com/en/opensource/ is a search/request portal ("download or REQUEST any open source codes").
- No public GPL zip found for AX3/TC7102/WS7200; the community request thread (forum.openwrt.org/t/support-for-huawei-ax3-pro/66551, incl. the Huawei-community GPL request for WS7200) is unanswered.
- Chip corroboration: acwifi.net/19630.html ("Hi5622 等同 Hi1152"), efxn ZN-M5 teardown, szlcsc AX3 teardown (Hi5651 + Hi1152). TC7102/AX3 OpenWrt ports exist as community resources on gitcode but are paid/AI-generated content repos — not license-clean sources.

### 3.6 Banana Pi TR6560 BSP (rank 6)
- Tree-checked today: wifi driver ships as prebuilt `peanut_wifi.ko` + `peanut_plat.ko` under `package/triductor/tr5220/files/ko/` — **no source**. BSP remains valuable for platform patches and as the binary contract oracle already exploited by the project (ledger 2026-10-09).

### 3.7 HiSTBLinux dumps (rank 7) — NOT-usable
- Tree-checked: `source/component/wifi/drv` contains only third-party chips (bcmdhd AP6356, mt76x2, qca6174, rtl8812/8822/8821 families); **no Hisilicon full-MAC driver** in SPC050/SPC060. No license → leak; NOT-usable for vendoring.

## 4. The Jeton lead — RESOLVED

- **Proven part (prior notebook work, re-verified):** Jeton AX3000 Core is the same Hi5671YV200 + Hi5622v100 platform running the same `luofu_V200` SDK (`hisilicon/luofu`, `arm_cortex-a9`, kernel 5.10.201, DT strings `hsan,*`, `/etc/hi_version` product `ChenTang_1.2.16.linux0` — the "ChenTang" banner line of our own firmware), Wi-Fi firmware blob md5-identical, Jeton's `hi5622v100_wifi.ko` a newer superset build of the same tree (5867 vs 5356 symbols; md5 `7f1fb14a...`). Jeton is a genuine second GPL obligor.
- **Disproven part (checked today):** Jeton publishes **no GPL/source archive** — jetontechno.com product page and FAQ offer firmware downloads only (newest V3.1.00-261026, 2026-11). The earlier "download-center GPL archive NEEDS-VERIFICATION (JS page)" note resolves to: firmware-only, no source.
- **Resolution:** the lead is **disproven as a public-source route**; it survives only as a GPL *request* target (draft letter `JETON-GPL-EMAIL.txt` exists, unsent — human set the vendor-source routes aside on 2026-10-04 per TIMELINE-AND-WAY-FORWARD.md). No action was taken by this lane (host-only mandate).

## 5. Negative results (recorded so they are not re-hunted)

- GitHub repo search: `hi5622` → 0, `hi5671` → 0, `hi5651` → 0, `hisilicon wifi` → unrelated, `hirouter` → unrelated JS routers, `tr6560` → 1 (binary-only).
- gitee: code/repo search is a JS SPA (no anonymous results); targeted Bing `site:gitee.com` queries found nothing for hi1152/hi5622/HiRouter.
- grep.app API: no results returned (now gated); searchcode.com: nothing for `smac_msg_proc`.
- HiRouter SDK leaks (HiRouterV100R001/R002): not found on GitHub/gitee/web.
- Mainline Linux / wireless-next: no Hisilicon full-MAC (hcc/hmac/wal) driver, ever.
- OpenIPC: no Hisilicon Wi-Fi driver (their wlan packages are aic8800/atbm/ssw101b/ssv6x5x/realtek/mt7601u/rda).
- `smac_msg_proc_*`, `shuangta`, `ChenTangV100R001`: zero public code hits (firmware-side names; the "ChenTang" line identifies the closed `luofu_V200` SDK build).

## 6. Best candidate and adaptation plan (for the 6.18 target)

**Best candidate: OpenHarmony `device_soc_hisilicon` hi3881v100 driver (rank 1), with hongmengkernel hi1105 (rank 2) as the structural supplement for `dpe`/`oam_adapter`/`chip` layers.** Both are GPL-2.0-compatible and implement the same wal/hmac/mac/hcc/oal/oam architecture that `hi5622v100_wifi.ko` was built from.

### 6.1 What to vendor (import as-is, keeping GPL-2.0 headers)
- `wal/` — cfg80211 op surface (`wal_cfg80211.c` = the `wal_cfg80211_*` family; `wal_mac80211.c` is misnamed in upstream, it registers cfg80211 ops, not mac80211).
- `mac/hmac/` + `mac/common/` — `hmac_*`/`mac_*` families.
- `hcc/` — `hcc_*` message/transport channel, including `hcc_hmac` glue and `hmac_hcc_adapt`.
- `oal/` — `oal_*` OS-abstraction shims (already thin over Linux).
- `include/`, `frw/` — platform types and the firmware-runtime framework.
Suggested location (parent decides): `opensource/target-skeleton/target/linux/hisilicon/files-6.18/drivers/net/wireless/hisi_luofu/` or a `package/kernel/hisi-hcc` package.

### 6.2 What to port (new work)
1. Drop HDF: replace `hdfadapt/hdf_driver_register.c` + `net_adpater.c` with a plain platform/pci driver entry; register wiphy via the wal layer directly.
2. Transport swap (the hard part): discard `oal_sdio_host.c`/`hcc_slave` SDIO path; write a PCIe `hcc_host` variant against our own luofu PCIe root-complex driver (opensource patch 013) and the already-reversed Hi5622 mailbox/doorbell/ring contract (`docs/phase42` ring map, `phase43` channel map, `phase45` mailbox; HCC service map from `wifi-driver-re`). This replaces the vendor `pcie_*`/`bal_*` layers.
3. Chip HAL: `hal_*`/`alg_*`/`shuangta_*` have no public source — reimplement from the existing RE tables (PORT-SURFACE.md work) or keep the vendor modules under the documented-blob route (ROADMAP option 2). The 928,920-byte firmware blob stays closed regardless; that fact is unchanged by any source find.

### 6.3 Expected 6.18 port diffs (5.10-era code → 6.18)
- cfg80211 API drift, mechanical but wide: `cfg80211_ops` signature changes (`mgmt_tx`/frame-injection params, `set_txpower`, `get_station`/`dump_station`, `crit_proto_*`, `set_sar_specs`, channel-context), `cfg80211_inform_bss*` call changes, `cfg80211_vendor_*_cmd` return-code conventions, removal of legacy helpers (`cfg80211_get_bss` → `cfg80211_get_bss_beacon`, ...).
- Kernel API drift: `kfree_sensitive` (was `kzfree`), `proc_ops` (already needed for 5.10+), `eth_hw_addr_set`, `refcount_t`, `usleep_range`-class timer constraints, genl family registration, `module_layout`/export-symbol hygiene.
- The vendor module's `HW_RTOS_NS` 211-symbol shim (PORT-SURFACE.md) becomes unnecessary once the source is vendored — that whole compatibility layer disappears.
- Estimate: vendoring + cfg80211/kernel API port = weeks of mechanical work; the PCIe transport + chip HAL = the same multi-month long pole the project already measured. No source find changes the firmware-blob limitation.

### 6.4 License hygiene verdict per candidate
- #1 OpenHarmony hi3881: **USABLE** (GPL-2.0 file headers; repo dual-label noted; attribute HiSilicon).
- #2 hongmengkernel hi1105: **USABLE with caution** (GPL-2.0 kernel tree; unofficial mirror without per-file headers — prefer #1 for text, #2 for structure).
- #3/#4/#5 vendor channels: no public artifact — nothing to license; keep GPL-REQUEST.md evidence trail and Jeton letter.
- #6 Banana Pi BSP: GPL tree usable for platform reference; its `.ko` are binaries — do not vendor.
- #7 HiSTBLinux dumps: **NOT-usable** (unlicensed leak; and no matching wifi source anyway).
- #8/#9/#10: no stated license — **reference-only**, never vendor.

## 7. Key evidence links

- OH wal source with GPL header: https://gitee.com/openharmony/device_soc_hisilicon/blob/master/common/platform/wifi/hi3881v100/driver/wal/wal_cfg80211.c
- OH LICENSE: https://gitee.com/openharmony/device_soc_hisilicon/blob/master/LICENSE
- hi1105 tree: https://github.com/5-Super-Rookie-5/hongmengkernel/tree/HEAD/kernel/linux-5.10-lts/drivers/connectivity/hi11xx
- DeepWiki hi11xx architecture: https://deepwiki.com/5-Super-Rookie-5/hongmengkernel/3.2-hisilicon-connectivity-drivers-(hi11xx-wifibtgnss)
- Cudy v2 download-center: https://www.cudy.com/en-apac/pages/download-center/wr3000-2-0 ; Cudy v1 GPL archive (MediaTek): https://drive.google.com/file/d/1und0klzZ22tw4VvS7hxvMr5BmfwD-o0m (`GPL_for_AX3000_mt7981.tar.gz`)
- OpenWrt forum WR3000S GPL pointer: https://forum.openwrt.org/t/cudy-wr3000s-support/204032
- Jeton product (firmware only): https://www.jetontechno.com/product/ax3000-core/ ; FAQ: https://www.jetontechno.com/faq/how-to-download-firmware-from-the-jeton-tech-official-website/
- Huawei portal: https://consumer.huawei.com/en/opensource/ ; AX3 Pro GPL thread: https://forum.openwrt.org/t/support-for-huawei-ax3-pro/66551
- Hi5622≡Hi1152 corroboration: https://www.acwifi.net/19630.html , https://www.efxn.net.cn/threads/兆能-zn-m5-移动定制版ax3000-路由器拆解.44/
- Banana Pi BSP (prebuilt peanut ko): https://github.com/BPI-SINOVOIP/THG6500-TAX2-OPENWRT-BSP/tree/HEAD/package/triductor/tr5220/files/ko
- HiSTBLinux dumps: https://github.com/JasonFreeLab/HiSTBLinuxV100R005C00SPC050 , https://github.com/JasonFreeLab/HiSTBLinuxV100R005C00SPC060
- Reference-only repos: https://github.com/XePeleato/Hi1101_HisiWifi , https://github.com/themactep/wifi-hi3881 , https://github.com/gtxaspec/ws73v100-wifi
