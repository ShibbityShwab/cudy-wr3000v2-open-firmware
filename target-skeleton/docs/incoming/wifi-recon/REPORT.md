# Wi-Fi recon report - Hi5622V100 on the Cudy WR3000 v2.0 (HiSilicon luofu, board R116)

**Lane:** wifi-recon (host-only). **Written:** 2026-11-21.
**Target context:** OpenWrt 6.18.60 / hisilicon-luofu target skeleton (patch stack ~001-062).
**Task:** answer (a) what the Wi-Fi hardware is, (b) how the vendor stack drives it, (c) whether any
mainline/OpenWrt driver exists, (d) concrete options with effort/risk and the first three steps,
(e) what extra evidence would be needed.
**Method / limits:** local files only. No device access, no reboot, no flash, no vendor-binary
commit. Nothing in this lane was re-measured on the box; claims a previous lane measured on the
device are marked **[V*]** and carry the recording doc. This lane adds reproduction of binary/log
facts on the host (**[V]**).

**Evidence markers:** **[V]** reproduced in this lane from local artifacts (module binaries, rootfs,
MTD dumps, stock UART boot capture); **[V*]** device-measured in an earlier lane and recorded in the
cited repo doc (not re-run here); **[I]** inference; **[U]** unmeasured/open; **[X]** blocked/absent.

**Index of the answers:**

| question | answer | section |
| --- | --- | --- |
| (a) hardware | Hi5622V100 full-MAC radio, **two PCIe endpoint functions** (one per band) behind two SoC PCIe root complexes; closed 928,920-byte firmware + 2 config INIs + per-unit calibration | §2 |
| (b) vendor stack | two GPL-declared modules (plat + wifi) over OpenWrt cfg80211 backports; HCC/DMA rings; firmware downloaded by plat at boot; stock wpad over a patched netifd that creates VAPs through /sys/hisys/hipriv | §3 |
| (c) existing driver | **none**: no mainline, no OpenWrt, no public BSP/Jeton source; only vendor 5.10 binaries (plus newer Jeton binaries of the same SDK) | §4 |
| (d) options | **C (declare unsupported) now**; **A (port vendor source) only if GPL source arrives** - 10-16 engineer-weeks after source; **B (hybrid) impossible on 6.18**, only proven on the vendor 5.10 kernel; **D (clean-room) years** | §5 |
| (e) extra evidence | vendor source + vendor kernel config; full endpoint config-space/BAR snapshots + sysfs binding; firmware redistribution terms; calibration format; exact vendor netifd patch; vendor kernel System.map | §6 |

---

## 1. Scope and artifacts

### 1.1 Local artifacts used

| artifact | location | what it proves |
| --- | --- | --- |
| stock rootfs 2.4.15 / 2.5.24 (extracted) | rootfs-2.4.15/, rootfs-2.5.24/ (parent repo, gitignored) | vendor modules, firmware, INIs, init/load scripts, patched userland |
| vendor module set | rootfs-*/lib/hisilicon/ko/ (58 .ko) and rootfs-*/lib/modules/5.10.201/ (198/207 .ko) | symbol tables, vermagic, licenses, dependency closure |
| stock kernel slot A | dumps/router-20261108/full/mtd11-kernela.bin | uImage + zImage, extracts to a 5.10.201 "jenkins@cudyrd1" Image (no symbols, no IKCONFIG) |
| kernel slot B (lab kernel) | dumps/router-20261108/kernelb/kernelb.{zImage,dtb,vmlinux} | lab-built 5.10.201 ("luofu-lane@local") and its DTS (not the vendor base) |
| vendor base DTB / overlays | dumps/router-20261108/full/mtd1-uboota.bin (base + overlays), decoded in opensource/docs/soc/vendor-dt-notes.md | two hsan,pcie root complexes, iATU tables, overlay pcie-gpios |
| stock boot UART capture | build/uart-logs/20261107T191003Z-poweron-uboot.log | live PCIe enumeration, firmware download, wiphy/netdev, VAP creation |
| calibration snapshots | build/linux-kbuild/cal-snapshots/2569111{6,7}-*/ | FIRMWARE/INI/factory KV + iwpriv alg values copied off the box (read-only) |
| 6.18.60 kernel source | build/linux-kbuild/netport-pristine/linux-6.18.60/ | mainline driver search + API-delta checks |
| 6.18 build validation | build/linux-kbuild/waves/2026-11-19-lsw-egress/validation/compile-tree/vmlinux.symvers | 6.18 export set used for the API delta |
| vendor stack docs | opensource/docs/DRIVER-BLACKBOX.md, phase12-49, PORT-SURFACE.md, soc/*.md | device-measured reverse-engineering record |

### 1.2 What this report is not

It is not a driver, not a schedule commitment, and not a statement that Wi-Fi can be made to work on
6.18 with the artifacts that exist today. It is the decision-facing consolidation of a large local
evidence base, plus four corrections to existing docs (§7).

---

## 2. (a) The Wi-Fi hardware

### 2.1 Silicon identity

| property | value | evidence |
| --- | --- | --- |
| radio silicon | **Hi5622V100** (vendor driver codename **ShuangtaV100**) | strings in hi5622v100_plat.ko: ShuangtaV100; HARDWARE-IDENTITY.md; TARGET-DESIGN.md:93 |
| board SDK identity | chip name :luofu_V200; product name :wrt_ax3000_lite; product version :ChenTang_1.2.16.linux0 | rootfs-2.4.15/etc/hi_version **[V]** |
| chip id / rev | chip id:0x34 version:0x00 (iwpriv Hisilicon0 get_chipid) | calibration snapshot identity.txt **[V*]**; DRIVER-BLACKBOX.md:15 |
| die id | 16-word device-unique ID readable via get_dieid | DRIVER-BLACKBOX.md:16 **[V*]** (value not reproduced) |
| firmware build tag | ChenTangV100R001C20T13 | FIRMWARE.bin strings **[V]**; DRIVER-BLACKBOX.md:188 |
| SoC | Hi5671YV200, 2x Cortex-A9, luofu R116 | HARDWARE-IDENTITY.md |
| stock kernel | Linux 5.10.201 (#0 SMP Sat Nov 25 18:18:57 2023, jenkins@cudyrd1), OpenWrt 22.03.6 vendor target hisilicon/luofu | extracted mtd11 Image **[V]**; calibration snapshot identity **[V*]** |

The radio is a **host-offloaded full-MAC**: the vendor kernel module owns the MAC/MLME logic and the
on-chip processor runs a closed firmware image (see §3.2 and DRIVER-BLACKBOX.md sections 2-3). There
is no mac80211 soft-MAC boundary to reuse.

### 2.2 Bus: PCIe, not SDIO/USB

The radio is reached over **PCIe**. There is no SDIO or USB path in any local artifact; the vendor
module imports pci_*/dma_* APIs and the vendor DTB describes two PCIe root complexes.

**Topology [V][V*]:** two DesignWare-root-complex host bridges, one Wi-Fi endpoint function per root
complex, one function per band:

| endpoint | RC / domain | BAR0 (16 MiB) | BAR4 (8 MiB) | BAR2 (16 KiB) | cfg word 0xff8 (phy devid) | INTx | band [I] |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 0000:00:00.0 | RC0, domain 0 (pcie@0x10160000) | 0x40000000 | 0x41000000 | 0x41800000 | 0x00011521 (devid 1) | 207 | 2.4 GHz (radio0/phy0) |
| 0001:00:00.0 | RC1, domain 1 (pcie@0x10164000) | 0x58000000 | 0x59000000 | 0x59800000 | 0x00011520 (devid 0) | 209 | 5 GHz (radio1/phy1) |

- Both functions are **59e7:0005**, class **0x028000** (network controller, "other"), subsystem
  vendor 19e5 (Huawei). Vendor ID 59e7 is not in the public PCI database. **[V]** from the stock boot
  log (lines 626/645) and the module's pci_device_id table (see §3.1); **[V*]**
  DRIVER-BLACKBOX.md:8-13, phase21/sibling-ep.md:99-104.
- BARs are 64-bit non-prefetchable, 16 MiB + 16 KiB + 8 MiB each; MSI capable (Count=1/1) but the
  vendor driver **uses INTx** (request_irq 207/209) and only the sibling's INTx (209) is observed to
  fire in normal operation. **[V*]** DRIVER-BLACKBOX.md:12-13, phase21/both-eps.md:143-157.
- The two functions are **cross-wired**: is_pcie_cross=1; RC0's host view enumerates the device with
  phy_devid 1 and RC1 with phy_devid 0 (stock boot log lines 836-860 [PCIEL] messages;
  phase21/sibling-ep.md:87-104). Do not assume domain N == band N without the config word.
- pcie@0x10160000 / pcie@0x10164000 in the vendor base DT: hsan,pcie + hsan,acp-pcie0/1,
  reg-names = "dbi" "misc" "cfg" "mem" "io", three outbound iATU viewports per RC (iatu_rc), endpoint
  table iatu_ep, four CRG resets (0x34 bits 0xc..0xf / 0x10..0x13), radm/linkdown SPIs 59/69 and
  63/70, acp-gate at 0x10100174 bit 25/26. **[V]** opensource/docs/soc/vendor-dt-notes.md:84-111,
  soc/luofu-r116-pinned.dts.
- The board overlay (board_id=3) adds capability = <3>, gpio-delay = "2", and pcie-gpios
  (GPIO 0x14/0x15/0x1c) to each RC node. **[V]** vendor-dt-notes.md:168-190, TARGET-DESIGN.md:443-448.
- Hard access rules inherited from previous lanes: the RC misc window (0x10161000/0x10165000) is
  **write-only; reading it panicked the box**; every DBI/cfg access must be an aligned dword.
  **[V*]** phase20/host-window.md C.4, TARGET-DESIGN.md:664; these rules are already encoded in the
  6.18 draft host driver (patch 013) and must stay.

### 2.3 Firmware and data files the driver needs

All of these are closed vendor artifacts. They are **identical between the two stock releases
(2.4.15 and 2.5.24)**; hashes re-computed in this lane **[V]**:

| file | size (B) | md5 | sha256 | role |
| --- | --- | --- | --- | --- |
| /lib/firmware/hi_wifi/FIRMWARE.bin | 928,920 | 0e530b976d5a20e87358671f1a577695 | 7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b | closed radio firmware (Thumb code + tables); banner ChenTangV100R001C20T13; header 71 69 04 00 2d 74 0c 00 |
| /lib/firmware/hi_wifi/cfg_device_hisi.ini | 799 | - | 34d9ce145d8d0ef91068d5e9b66e3a4ed001b1d182bdb2eb0288c755a5ad5f5e | per-device INI ("boundary" ITCM/DTCM/custom segment addresses written by a vendor build script) |
| /lib/firmware/hi_wifi/cfg_hi5622v100_hisi.ini | 32,315 | - | a7ac5bcd9adf8c00fd004a113827ad85ae592da5738ca3c94afa30768ef44297 | HOST_WIFI_NORMAL (~186 keys incl. firmware segment load map), HOST_WIFI_PWR_LIMIT_LU (per-channel/rate power), CUSTOM_REGDOMAIN_CFG |
| /usr/local/factory/wifi_cali_data.kv | 8,912 | - | 9b8c11662295fac9666cdc635c1088ca730b054e9d6644a052c60ee8d2d8d926 | per-unit calibrated data, factory partition (mtd5, offset ~0x18306c) |
| /usr/local/factory/wifi_cali_data_2g.kv | 2,336 | - | 9899b6263ce43ee9a3b65cbba4f233ea8615ecc45467be25089cfc146799d8b5 | per-unit 2.4 GHz calibrated data |
| /usr/local/factory/hi5622v100.cal | per-unit; absent/empty on the captured unit | - | - | userland calibration hand-off between hi5622v100_cal_get.sh and hi5622v100_cal_init.sh |

- Firmware load is done by **hi5622v100_plat.ko**, not the wifi module: strings finds
  /lib/firmware/hi_wifi/FIRMWARE.bin in plat.ko, and the stock boot log shows
  path=/lib/firmware/hi_wifi/FIRMWARE.bin, download firmware file buf len is [524288],
  firmware_download success (log lines 861-864) **[V]**.
- The INI defines the load map relative to the firmware file's chip address space (not file offsets):
  firmware_itcm_src_addr=0xf009c len=0xadd0, firmware_dtcm_src_addr=0xfae6c len=0x1098,
  firmware_custom_src_addr=0x1b2800 len=0x10; cfg_device_hisi.ini carries a second "boundary" map.
  **[V]** rootfs files; DRIVER-BLACKBOX.md:45-49.
- Per-unit calibration is read by the driver from the factory partition at boot
  ({hwifi_rf_cali_file_load_2g succ!} in the stock log line 886), and separately re-applied by
  userland via iwpriv Hisilicon0 alg set_* (see §3.8).
- The firmware blob is **not per-release**: 2.4.15 and 2.5.24 ship byte-identical blobs, and the
  Jeton AX3000 Core (same SDK, different product) ships the **same md5**
  0e530b976d5a20e87358671f1a577695 - a sibling data point
  (phase29/jeton-shared-sdk-proven.md:19-30) and a second GPL obligor, not a source of open code.
- **No license/redistribution terms for FIRMWARE.bin exist anywhere in the rootfs** (searched
  lib/firmware, usr/share/licenses) **[V]**. This is an open legal question for any blob-shipping
  path, not just an upstream question.

### 2.4 What is not known from local artifacts

- The firmware's internal register/message spec (partially reversed in phases 37-49; the host-side
  pieces are mapped, device-side dispatcher consumption is still not reachable - see §3.9).
- The endpoint-to-band mapping is **inferred** ([I], phase21/sibling-ep.md:243-245), not measured by
  a host-side read in this lane.
- The exact PHY/PMU power sequence (the GPIOs and delays are board-overlay data; the register writes
  live in plat.ko and are not decoded end-to-end).

---

## 3. (b) How the vendor stack drives the chip

### 3.1 Modules and metadata

The Wi-Fi stack is two loadable modules plus dependencies. Measured with modinfo/readelf/nm on both
shipped releases **[V]**:

| module | 2.4.15 size / md5 | 2.5.24 size / md5 | vermagic | license | depends | exported syms | undefined | kernel imports |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| hi5622v100_wifi.ko | 3,581,748 / 1745ef77e728c411cef1921af3dc8731 | 3,564,728 / 4737fcb21a1a2262a96f84d780ad8b35 | 5.10.201 SMP mod_unload ARMv7 | GPL (several) | hi5622v100_plat | 778 (__ksymtab_strings) | 308 | 133 |

(the exported/undefined/import columns are measured on the 2.4.15 binary; the undefined set is identical in 2.5.24 and the 2.5.24 module is the one PORT-SURFACE/DRIVER-BLACKBOX measured)
| hi5622v100_plat.ko | 371,708 / f688618b4daa52473be41577f992f7d4 | 364,660 / 23660bc285393e678d5cade1c36c194b | same | GPL / GPL v2 | (none) | 299 | 126 | 117 |
| wifi_debug.ko | 437,500 | 436,164 | same | GPL | wifi, plat | - | - | - |
| plat_debug.ko | (present) | (present) | same | - | - | - | - | - |
| hi_pcie.ko | 21,596 / c1f34a2a94f2920b | present | same | GPL | hi_basic | - | - | - |
| ksecurec.ko, hi_kpie.ko | 16,984 / cf912058056fbf88, 68,524 / 333cb968eed464eb | present | same | GPL | - | - | - | - |
| hi_woc_wifi_rtx.ko, hi_kcfe_lrn.ko | 4,652 / f338499d19bdb2e9, 72,152 / 87b6d806a86cb69f | present | same | GPL | hi_kpie,hi_lsw_pub / switch modules | - | - | - |
| cfg80211.ko (OpenWrt) | 802,508 / 52f1d03bb0f4374d | present | 5.10.201 SMP mod_unload ARMv7 | GPL | compat | - | - | - |

Notes measured on the binary **[V]**:

- **No __versions section** in either module and the vermagic has no modversions token => the vendor
  kernel was built with CONFIG_MODVERSIONS=n; no .plt/.init.plt sections => CONFIG_ARM_MODULE_PLTS=n
  (consistent with VENDOR-STACK-ON-MAINLINE.md:18-27). Both modules are ET_REL ARM EABI5, not
  stripped (full symbol tables; the earlier lanes disassembled them by name).
- The two releases have **identical undefined-symbol sets** (diff empty) but different defined symbol
  counts (2.4.15 wifi 21,662 vs 2.5.24 21,177; plat 2,981 vs 2,786): the newer build dropped local
  symbols, not kernel API. The 2.5.24 module is the one PORT-SURFACE/DRIVER-BLACKBOX measured.
- wifi.ko is mostly vendor code: DRIVER-BLACKBOX.md §2 records 3,847 functions / 1,243,224 bytes of
  .text in the 2.5.24 build, layered as hmac_ 1853, wal_ 990, mac_ 299, hdpp_ 208, alg_ 199,
  shuangta_ 97, hal_ 93, oal_ 42, oam_ 33.
- **wifi.ko is not a mac80211 driver**: it imports **0** ieee80211_/mac80211 symbols. Its 26 cfg80211
  imports are wiphy_new_nm, wiphy_register, cfg80211_inform_bss_frame_data, cfg80211_rx_mgmt_khz,
  cfg80211_mgmt_tx_status, cfg80211_new_sta, cfg80211_del_sta_sinfo, cfg80211_connect_done,
  cfg80211_disconnected, cfg80211_scan_done, cfg80211_sched_scan_results, cfg80211_get_bss,
  cfg80211_put_bss, cfg80211_ft_event, cfg80211_michael_mic_failure, cfg80211_ready_on_channel,
  cfg80211_remain_on_channel_expired, cfg80211_vendor_cmd_reply, __cfg80211_alloc_reply_skb,
  cfg80211_calculate_bitrate, cfg80211_chandef_valid, wiphy_apply_custom_regulatory, wiphy_free,
  wiphy_unregister, ieee80211_get_channel_khz. It registers netdevs itself
  (alloc_netdev_mqs/register_netdev) and holds a wiphy per band; the MAC/MLME logic is its own.
  It exposes **74 wal_cfg80211_* functions** (the cfg80211_ops implementation, incl.
  wal_cfg80211_add_virtual_intf / del_virtual_intf / start_ap / stop_ap / change_beacon / scan /
  connect / add_key / get_station / sched_scan_* / mgmt_tx / remain_on_channel / set_mac_acl /
  set_bitrate_mask / dump_survey / external_auth).
- The cfg80211 ABI the modules were compiled against is **OpenWrt's backports v5.15.92**, not vanilla
  5.10: modinfo cfg80211.ko says version: backported from Linux (v5.15.92-0-ge515b9902f5f) using
  backports v5.15.92-1-0-gdfe0f60c **[V]**. (phase14/abi-match.md additionally proves the vendor
  kernel behaves like CONFIG_PM=n, moving wiphy->bands[] by 8 bytes and changing sizeof(struct
  wiphy) - the modules are binary-coupled to that layout.)
- The kernel modules were built with **GPL declared** (license=GPL, license=GPL v2), which is the
  GPL-request lever; the firmware image carries no license text (§2.3).

### 3.2 Correction: the "211 namespaced symbols" claim

PORT-SURFACE.md §2 (repeated in TARGET-DESIGN.md:741 and ROADMAP-OPENWRT.md:162) says the wifi
module "imports 211 symbols through the vendor HW_RTOS_NS namespace - none of which exist in an
upstream kernel". The local binary evidence does **not** support that reading **[V]**:

- .modinfo does contain 211 import_ns=HW_RTOS_NS records for wifi.ko (46 for plat.ko), but the same
  section repeats license=GPL records interleaved; the count tracks **per-object compiled-unit
  declarations** (a vendor header defines MODULE_IMPORT_NS for every translation unit), not distinct
  symbols. A symbol-level reading is also arithmetically impossible: wifi.ko has **308** undefined
  symbols total; **175** of them are provided by other shipped vendor modules and **133** by
  the project's 5.10.201 kernel export set (build/linux-kbuild/linux-5.10.201/vmlinux.symvers), i.e.
  **0 unresolved**. 211 cannot fit into the 133 kernel
  imports, and 211 namespaced imports would exceed the 175 module-provided symbols.
- No shipped .ko (all 256) and not the stock vendor Image contains the string HW_RTOS_NS as an
  **exporter**; the only matches are the four importer modules (hi5622v100_wifi/plat, wifi_debug,
  plat_debug). So there is no runtime namespace provider to reproduce, and nothing in the load path
  fails for the lack of one (the stock system loads these modules successfully).
- What is true and port-relevant: **175 imported symbols live in the vendor's own modules** (plat 138,
  cfg80211 26, ksecurec 9, hi_kpie 2) - i.e. a port needs those modules' sources, or their
  reimplementation, not a namespace patch. The 133 kernel imports are ordinary 5.10 APIs.

Recommended restatement: "175 of 308 imports come from vendor-shipped modules, 26 from OpenWrt's
cfg80211 backports, 133 from the 5.10 kernel; the module's import_ns tags are not 211 symbols." This
lowers one of the claimed blockers but does not change the decision (source + firmware remain the
blockers).

### 3.3 Hard dependency closure (what must be ported/loaded with it)

Measured by matching nm -u against every shipped module **[V]**:

| owner | needs | symbols |
| --- | --- | --- |
| hi5622v100_wifi.ko | hi5622v100_plat.ko | 138 (hcc_* msg/queue/timer, oal_*, oam_*, hwifi_*, bal_*, wlan_pm_*, pcie_*, sysfs objects) |
| | cfg80211.ko | 26 (list in §3.1) |
| | ksecurec.ko | 9 (memcpy_s, memset_s, str*_s, snprintf_s, ...) |
| | hi_kpie.ko | 2 (hi_net_woc_alloc_skb, hi_net_woc_skb_free) |
| hi5622v100_plat.ko | ksecurec.ko | 8 |
| | hi_kpie.ko | 1 (hi_net_woc_skb_free) |
| | 5.10 kernel | 117 |

Runtime-only (resolved by __symbol_get, not link-time) hooks installed by
hmac_softnp_woe_hook_init **[V*]** phase15/unload-patch.md:95-104, verified against module symbols
**[V]**:

| hook | provider |
| --- | --- |
| hi_wifi_rx_pkt, hi_wifi_reg, hi_wifi_unreg, hi_wifi_tx_only_lrn, hi_wifi_tx_hook | hi_woc_wifi_rtx.ko (Wi-Fi offload/switch RTX) |
| hi_cfe_flush_by_mac, hi_cfe_flush_mc_by_devname | hi_kcfe_lrn.ko (switch/CFE learning path) |

So the "two Wi-Fi modules" are really a **two-module Wi-Fi stack sitting on the SoC switch/PIE/offload
stack**: plat (PCIe/firmware/HCC) + wifi (MAC/cfg80211) + cfg80211 + ksecurec + hi_kpie, with soft
hooks into hi_woc_wifi_rtx/hi_kcfe_lrn. A port that ignores the switch stack still needs at least the
five hard modules/APIs; the soft hooks can degrade if their providers are absent (the vendor sequence
loads hi_woc_wifi_rtx before Wi-Fi, so this is untested in production).

### 3.4 DT nodes

The vendor base device tree **does not describe the radio**; the radio is discovered by PCIe
enumeration. The relevant nodes are the two root complexes (vendor-dt-notes.md:84-111 **[V]**):

- pcie@0x10160000 (domain 0) and pcie@0x10164000 (domain 1): compatible = "hsan,pcie" +
  "hsan,acp-pcie0|1", reg = <dbi><misc><cfg><mem><io>, iatu_rc (three outbound viewports each),
  iatu_ep, four resets (CRG 0x34 bits 0xc..0xf / 0x10..0x13), clocks = <&crg LUOFU_CLK_PCIE0|1>,
  interrupts radm/linkdown, acp-gate (0x10100174 bit 25/26), bus-range = <0 0xa>.
- Board overlay (board_id=3) patches in capability = <3>, gpio-delay = "2", pcie-gpios
  (GPIO 0x14/0x15/0x1c) and the LED/key nodes; the base DT has no interrupt-map/msi-parent for the
  endpoints.
- The endpoint's identity/version word lives in its **private config window at offset 0xff8**
  (0x00011521 on domain 0, 0x00011520 on domain 1) and the vendor driver reads it as
  phy_devid = cfg[0xff8] & 0xf **[V*]** phase21/sibling-ep.md:74-104. This is how the driver resolves
  the cross-wiring; it is not a DT property.
- The 6.18 target DTS already documents both endpoints as disabled children
  (wifi@0,0 { compatible = "pci59e7,0005"; status = "disabled"; }, dts/luofu-r116.dts:1261 and :1292)
  and keeps the RC nodes disabled behind patch 013's CONFIG_PCIE_LUOFU_HOST.

### 3.5 Boot / load sequence (the vendor stack in operation)

The load is split between a base-module script and the Wi-Fi script **[V]**:

1. /etc/init.d/hsan_prepare (S09) histart() runs /etc/rc.d/$chip (hsan_prepare:61-67). The chip token
   resolves to luofu, i.e. **/etc/rc.d/luofu** - the base-module script (there is no
   /etc/init.d/luofu; the file is a bare rc.d script run by hsan_prepare).
2. /etc/rc.d/luofu loads the SoC base: hi_kpinctrl (+ gpio-dwapb), rng, efuse, kpmupwm,
   clk_corrector, cpufreq, hi_sysctrl, hi_acp, hi_kpie, hi_lsw_pub/dp/pfe, hi_kmdio/kphy/kmac,
   hi_woc_wifi_rtx, the hi_kcfe_* family, hi_kcara/hi_khslink, hi_knet_cdev, hi_tvsensor (lines 5-81).
   Wi-Fi modules are **not** in this script.
3. /etc/init.d/hsan_start (S15) calls /usr/bin/wifi_init.sh (hsan_start:45).
4. /usr/bin/wifi_init.sh gates on hi_cfm get board.wifi_vender; for shuangta (this board) it runs
   (lines 76-85):
   - insmod hi_kwificlk.ko - **absent from both shipped rootfs**; the insmod fails harmlessly (no
     set -e);
   - insmod hi_pcie.ko (host bridge, depends hi_basic);
   - insmod hi5622v100_plat.ko (firmware download + INI + HCC + rox_pci0 PCI driver);
   - insmod hi5622v100_wifi.ko (MAC + cfg80211 + netdevs);
   - hi5622v100_cal_init.sh (applies factory calibration via iwpriv Hisilicon0 alg set_*);
   - wifi_debug.ko only if /etc/hi_version contains "turbo".
5. Stock OpenWrt wpad/hostapd + netifd then bring the interfaces up. **The vendor patched
   OpenWrt's netifd**, not a vendor daemon: /lib/netifd/wireless/mac80211.sh creates and deletes VAPs
   by writing to **/sys/hisys/hipriv** (Hisilicon0 create vap0 ap 2g, vap0 destroy, wds_vap_mode,
   isolation...) instead of iw phy ... interface add (lines 608-641, 950-951). The base netdevs are
   Hisilicon0/Hisilicon1; the VAPs are vap0..vap11, bridged into br-lan by netifd.
   /etc/config/wireless is stock mac80211 UCI with option type 'mac80211' + option chiptype
   'hsanwifi' + phy0/phy1.
6. /etc/rc.d/luofu also sets /sys/module/hi_kcfe_pub/parameters/g_intf_bypass=1 and loads hi_swa.ko
   (switchdev glue); the Wi-Fi path is wired into the switch/offload path (hi_woc_wifi_rtx,
   hi_kcfe_*), which is why the module closure in §3.3 is wider than the two Wi-Fi modules.

### 3.6 Observed stock boot (one capture, no device access in this lane)

Stock 2.4.15 boot from the read-only UART capture
build/uart-logs/20261107T191003Z-poweron-uboot.log **[V]**:

| time (s) | log line | meaning |
| --- | --- | --- |
| 11.48 | PCIe:0 switch to RC mode / PCI:0 1.1 Device Link Up (616-617) | RC0 up; RC1 follows at 11.61 |
| 11.54-11.59 | pci 0000:00:00.0: [59e7:0005] ... class 0x028000; BAR0 0x40000000, BAR4 0x41000000, BAR2 0x41800000 (626-635) | domain-0 endpoint enumerated and resourced |
| 11.63-11.73 | same on 0001:00:00.0 (BAR0 0x58000000, 645-658) | domain-1 endpoint |
| 11.80-12.6 | [low_power_init] success, then a long INI parse (INI_DRV:E] ... have end of .ini file!!!) | plat.ko INI load |
| 13.48 | [PCIEL]pcie_init_chip_res chip_id[0] bus_id[0] (835) | plat endpoint init |
| 13.95-14.4 | wlan_pm_open, [PCIEL]pcie_main_init, enter firmware_download_function, download firmware file buf len is [524288], firmware_download success, Device plat ready! (838-867) | power-on + firmware download to the radio |
| 15.29 | swa_netdevice_event ... name:Hisilicon0 (892) | wifi.ko registered the 2.4 GHz netdev (wiphy follows) |
| 15.30 | 2g the cali result to device succ / 5g ... succ (894-895) | per-unit calibration applied |
| 21.47-32.2 | drv_mac80211_setup: dev=radio0 phy=phy0 ... Create VAP: ... band=2g ... then radio1/phy1/5g (1062-1207) | netifd creates VAPs through hisys/hipriv; hostapd runs on them |
| 36.8-38.9 | vap8/vap11 enter br-lan forwarding (1220-1250) | Wi-Fi bridged into LAN |

Also cfg80211: failed to load regulatory.db at 0.93 s (no wireless-regdb installed) - harmless in
the vendor image, but a port must ship wireless-regdb or accept world regdomain.

### 3.7 Driver internals (what a port would carry)

From the named-symbol disassembly record (opensource/docs/soc/vendor-wifi-driver-decompiled.md,
DRIVER-BLACKBOX.md) **[V*]**:

- hi5622v100_wifi.ko is not stripped: 4,838 functions in the 2.4.15 build (Ghidra dump, 8.1 MB text;
  regenerate per that doc). Prefix families in §3.1.
- Bring-up chain: init_module -> hal_main_init -> hal_board_init -> (oal_bus_get_chip_num) ->
  hal_chip_init -> hal_device_init -> hal_device_init_mac_and_phy ->
  shuangta_host_initialize_machw / shuangta_host_initialize_phy -> hal_ring_init;
  shuangta_host_chip_irq_init registers the host IRQ glue via pcie_wlan_func_register
  (vendor-wifi-driver-decompiled.md:16-25, 233-286).
- Device side: firmware runs on the radio; the host talks to it through HCC message queues and the
  ETE SR/DR DMA rings that previous lanes mapped and partially drove (phases 17-25, 37-49). The host
  half of the message service was executed against real device traffic; the firmware's own H2D
  dispatcher could not be entered from a takeover (phase22/h2d-accept.md; phase49/README.md;
  PORT-STATUS.md §2).
- The RF configuration is largely **data-driven** from the two INIs (power tables, regdomain,
  calibration protect limits, temperature compensation); per-unit calibration lives in the factory
  KV files (§2.3). This is the part a port must reproduce exactly to keep the radio legal and
  calibrated.

### 3.8 Userspace contracts

| contract | detail | evidence |
| --- | --- | --- |
| VAP creation/destruction | /sys/hisys/hipriv sysfs ABI; hisys kobject from plat.ko, hipriv attribute group + handlers from wifi.ko (wal_hipriv_* functions); commands include create, destroy, wds_vap_mode, isolation_mode, isolation_fwd | rootfs lib/netifd/wireless/mac80211.sh:608-641,950-951; module strings **[V]**; DRIVER-BLACKBOX.md §1 (private ioctl table) |
| calibration | /usr/bin/hi5622v100_cal_init.sh reads /usr/local/factory/hi5622v100.cal and issues six iwpriv Hisilicon0 alg set_* commands; hi5622v100_cal_get.sh dumps them; snapshots in build/linux-kbuild/cal-snapshots | rootfs scripts **[V]**; DRIVER-BLACKBOX.md §5 |
| factory init | /usr/bin/wlan_factory_init uses the same module load; wifi_init.sh also handles other vendor radio families (wuyi/tiangong) that are not on this board | rootfs, wifi_init.sh:28-90 |
| wpad / UCI | stock OpenWrt hostapd/wpa_supplicant over nl80211; option type 'mac80211', chiptype 'hsanwifi', phy0/phy1, vapN ifnames | /etc/config/wireless **[V]** |
| vendor app layer | hi_cfm, hipriv, hsan_*, cmagent/cmsd supervise the system and the upgrade path; not Wi-Fi-specific but they are why the vendor rootfs cannot be swapped wholesale | phase26/rootfs-swap-verdict.md:36-58 |

The practical consequence: **the vendor Wi-Fi has no vendor daemon, but it does have a vendor sysfs
ABI and a patched netifd**. A port that keeps the vendor driver must carry that patch; a driver that
implements nl80211 add_virtual_intf (the vendor's wal_cfg80211_add_virtual_intf suggests the shape
exists) can use stock OpenWrt scripts and stay upstreamable.

### 3.9 Operational constraints measured by earlier lanes

- **The vendor module cannot be unloaded**: rmmod hi5622v100_wifi panics in its own exit path
  (hmac_main_exit -> hdpp_main_exit -> ... NULL call), and a binary patch only moves the fault to a
  still-registered callback in plat's RX thread (~2 s later), rebooting the box. There is no safe
  "swap the driver under the vendor stack" path. **[V*]** phase15/bringup.md:85-111,
  phase15/unload-patch.md:1-15.
- modprobe -r does not exist on the device (kmodloader rejects -r); only rmmod <name>.
  **[V*]** phase15/bringup.md:48-60.
- The from-scratch/takeover line reached: endpoint claimed, firmware downloaded, ETE rings and HCC
  host half exercised, both endpoints claimable; it did **not** reach a working data path because the
  firmware's H2D dispatcher never runs and the TX-credit callback is installed by the chip layer at
  runtime (a takeover never runs it). This is a device/firmware property, not a missing host poke.
  **[V*]** PORT-STATUS.md §1b-§2, phase49/README.md, DEPLOYMENT-DECISION.md:42-66.
- The vendor stack **does not tolerate** the Wi-Fi modules being missing at boot for a
  production-looking system: hsan_start logs the insmod failures, but the radio then simply does not
  exist (that is the current 6.18 image state).

---

## 4. (c) Is there a mainline/OpenWrt driver for this chip?

**No.** Searched local trees, not the internet:

| source | search | result |
| --- | --- | --- |
| Linux 6.18.60 pristine tree | grep -rIl for hi115x/hi5622/hi_wifi/hi_sdio/hi1152/hi1151 over the whole tree | only a false positive in drivers/mmc/host/renesas_sdhi_core.c; drivers/net/wireless/ has **no hisilicon directory**; no hi115x/hi_wifi Kconfig/Makefile entry **[V]** |
| their OpenWrt 6.18 build tree | package/, target/linux/ | no matching package or driver **[V]** |
| target-skeleton / upstream series | all files | no Wi-Fi driver, no package, no DTS node enabled; only the disabled pci59e7,0005 children **[V]** |
| public Triductor TR6560 BSP (same silicon family) | phase50/bsp-build-status.md:14-15,45 | Wi-Fi drivers are **prebuilt blobs for other chips** (tr5120/tr5220), not Hi5622V100; no Hi5622 source anywhere in that BSP **[V*]** |
| Jeton AX3000 Core (same SDK, second vendor) | phase29/jeton-shared-sdk-proven.md:19-30 | same SDK, byte-identical FIRMWARE.bin, **newer builds of the same closed hi5622v100_*.ko**; no source **[V*]** |
| OpenWrt upstream | as carried by the local tree | OpenWrt has no closed-driver package for this chip; a kernel driver must be in-tree. Consistent with the target's own wired-first decision **[I]** |

- There is also **no open register documentation** for Hi5622V100; the only public "family" reference
  is the Triductor BSP, which ships the HCC-family drivers as binaries for different radios.
- The vendor stack is a **cfg80211 full-MAC driver, not mac80211**. Upstream does carry full-MAC
  cfg80211 drivers (brcmfmac, mwifiex, ...), so the architecture is not alien; the code and firmware
  are proprietary.
- Vendor source/licensing status **[V*]** (GPL-REQUEST.md:1-21): the modules declare license=GPL and
  the GPLv2 §3 request was **sent 2026-10-01; no reply is held**; project decision (2026-11-18) is
  that the request is **not a gate** and "no further action is required" - it is an optional
  accelerator. Jeton (same SDK) is a second obligor and its letter is still an unsent draft.

**Conclusion:** for a 6.18 OpenWrt target there is today **no driver**, open or closed-compatible. The
only runnable Wi-Fi on this board is the vendor 5.10 module stack (§3), and only on a 5.10 kernel.

---

## 5. (d) Options, effort, risk, first steps

### 5.1 Option matrix

| option | what it means | prerequisites | effort | risk | upstreamable? |
| --- | --- | --- | --- | --- | --- |
| **A. Port the vendor driver to 6.18** | port hi5622v100_plat + hi5622v100_wifi (and their hard deps) from the vendor 5.10 tree to 6.18, keep the closed firmware blob | **vendor GPL source** + build config (not held); source gate is external/optional; firmware redistribution terms unknown | **10-16 engineer-weeks after source** (breakdown in §5.2) + bench time | medium-high: 4,838-function vendor MAC, cfg80211 5.15.92 -> 6.18 API moves, PCIe host write path never booted, blob licence unresolved, vendor code quality | **No** as a whole (proprietary code + blob); only separable host-glue pieces could ever be discussed |
| **B. Hybrid** | 6.18 kernel + something vendor | - | - | - | - |
| B1 6.18 kernel + vendor 5.10 .ko | **impossible** | vermagic/ABI (§5.3) | - | guaranteed failure | no |
| B2 vendor 5.10.201 kernel + vendor modules + current OpenWrt userland | proven on the bench | vendor 5.10 kernel build + PLT workaround + vendor rootfs | 1-2 days per vendor release to keep current | vendor kernel age/security; no upstream path; still 5.10 | no |
| B3 vendor userspace on 6.18 kernel | irrelevant to Wi-Fi | - | - | - | - |
| **C. Declare Wi-Fi unsupported on the 6.18 target** | wired-only target; document the vendor blob and "Wi-Fi requires the vendor 5.10 stack" | none | 0 (already the recorded position) | none technical; product gap | **Yes** (for the wired target) |
| **D. Clean-room driver + firmware** | new cfg80211 full-MAC driver and new firmware for the radio cores | register/message spec (partially reversed), firmware reverse-engineering, funding | **years** (PORT-PLAN.md Path B; PORT-SURFACE.md Path B) | very high | yes in principle |

### 5.2 Option A scope (only if source arrives)

Workstreams and measured deltas:

1. **Legal/source (gate).** The two modules, the vendor kernel's build config and patch set, and the
   SDK build rules. Not held today (GPL request sent 2026-10-01, no reply; optional per project
   decision). Firmware redistribution terms still unknown even with source.
2. **PCIe host controller (prerequisite, independent of vendor source).** The 6.18 target already has
   patch 013 (drivers/pci/controller/dwc/pcie-luofu.c, 22,689 B patch, host-compile only, **never
   booted**) + patch 014 (CRG reset polarity). It must be validated in an attended run: enable RC
   nodes, enumerate **both** 59e7:0005 functions, keep the misc window unread and every access
   aligned-dword. This is required for A **and** is the only useful Wi-Fi prerequisite that can be
   done without vendor source. Estimated **2-4 weeks** incl. bench iteration.
3. **Module/kernel-API port.** Measured binary import surface: **wifi.ko 133 kernel imports, plat.ko
   117**. Checked against the 6.18.60 validation build's export set (vmlinux.symvers, 7,057 exports):
   wifi 122/133 still exported under the same name, **11** not; plat 101/117, **16** not. The deltas
   are mechanical/source-level, not architectural (list is reproducible in §8):
   - wifi: printk (now _printk), kfree_skb/consume_skb (inline wrappers), __kmalloc/
     kmem_cache_alloc_trace/kmalloc_order_trace (noprof/inline allocator wrappers),
     __dev_kfree_skb_any (inline), vmalloc (macro), wireless_send_event (deprecated/not exported),
     __stack_chk_* (6.18 ARM uses TLS stack guard);
   - plat: additionally kern_path_create/done_path_create (kernel file API changed - real code edit),
     hrtimer_init -> hrtimer_setup, alloc_workqueue -> _noprof macro, dma_sync_single_for_cpu,
     skb_queue_purge -> _reason, sprint_symbol, mem_section.
   Beyond exports, the source must be compiled against 6.18 headers: net_device, sk_buff, pci_dev,
   cfg80211 structs and callbacks all moved. This is the only way to find the full set.
   Estimated **2-4 weeks**.
4. **cfg80211/nl80211 API port.** The binary was built against OpenWrt's **cfg80211 backports
   v5.15.92** with CONFIG_PM=n (phase14). 26 cfg80211 symbols + 74 wal_cfg80211_* callbacks. In 6.18
   several of the imported notifications are inline _ext wrappers (cfg80211_rx_mgmt_khz ->
   cfg80211_rx_mgmt_ext, cfg80211_get_bss -> __cfg80211_get_bss, cfg80211_mgmt_tx_status -> _ext),
   and the cfg80211_ops struct gained MLO-era parameters (start_ap/change_beacon/station/link
   structures). Estimated **1-2 weeks**.
5. **Firmware/calibration/userspace.** request_firmware path, the 3 INIs and the factory KV
   calibration flow; keep the per-unit calibration semantics; either carry the patched netifd
   /sys/hisys/hipriv contract or implement nl80211 add_virtual_intf so stock scripts work; ship
   wireless-regdb. Estimated **1-2 weeks**.
6. **Bench bring-up.** enumerate -> firmware download -> wiphy registration -> scan/assoc -> data
   path, attended with recovery armed and UART captured. Estimated **2-4 weeks** of bench iteration.
   There is a known device-side gate on the from-scratch line (§3.9); a source port starts from the
   vendor's known-good driver and is less exposed, but the first firmware download on our own host
   driver is still unproven.

Total: **10-16 engineer-weeks after the source arrives** (host work) + generous bench margin, and a
realistic chance the source port itself is the smaller half. Risk: medium-high. Upstreamability of the
result: poor (vendor MAC code is not mainline-shaped, the firmware is closed, and the vendor userspace
contract would have to be documented as an exception).

### 5.3 Why the hybrid cannot give Wi-Fi on 6.18

- The modules are built for vermagic "5.10.201 SMP mod_unload ARMv7" with **no CONFIG_MODVERSIONS**
  and **no PLT sections**; the 6.18 kernel's module loader rejects the vermagic mismatch outright.
  Even with the vermagic spoofed, the 5.10 module calls struct pci_dev, struct net_device,
  struct sk_buff and struct wiphy fields by offset - the vendor kernel already differs from vanilla
  5.10 by CONFIG_PM=n (phase14/abi-match.md:3-63), and 6.18 moved every one of those structures
  further. The vendor module also imports 133 5.10 kernel symbols, 11 of which do not exist under the
  same name/ABI in 6.18; and it cannot be unloaded (§3.9).
- The one proven hybrid is **vendor 5.10.201 kernel + vendor modules + a current userland**: the
  vendor's switch/MDIO/PHY/MAC/Wi-Fi modules load on a vanilla 5.10.201 ARM kernel after the
  .plt/.init.plt objcopy workaround, producing a working router with Wi-Fi vaps bridged
  (VENDOR-STACK-ON-MAINLINE.md:1-57). That is the fallback to preserve, but it is not a 6.18 answer
  and cannot be upstreamed.
- "Vendor userspace on our 6.18 kernel" is irrelevant to the radio: the vendor has no Wi-Fi daemon;
  the radio is kernel-side, and the stock wpad is already what the 6.18 image would use if a driver
  existed.

### 5.4 Recommended position today: C

Keep the current recorded position (RELEASE-NOTES.md:96 "**No Wi-Fi**: Hi5622V100 is
vendor-module-only [X]"; TARGET-DESIGN.md:734-758 wired-first target). Concretely:

- **Do not promise Wi-Fi** on the 6.18 target, and do not put a vendor driver or blob in the target.
- State the reason precisely: *the Hi5622V100 is driven by GPL-declared vendor modules that also need
  the SoC switch/offload stack, and by a closed 928,920-byte firmware; the only working system is the
  vendor 5.10 kernel; no open driver exists and no source is held.*
- Keep the vendor 5.10 hybrid track alive as the **interoperability/fallback** lane (it is the only
  Wi-Fi-capable system on the box), clearly labelled not-6.18 and not-upstream.
- Preserve the PCIe enumeration work (patch 013/014) as **enabling work for any future Wi-Fi option**
  and as a host-controller deliverable in its own right; it needs no Wi-Fi decision.

### 5.5 First three concrete steps (recommended sequence)

1. **PCIe enumeration on 6.18 (host-now, bench-attended later).** Validate patch 013/014 in an
   attended run: enable the RC nodes, prove both 59e7:0005 functions enumerate with the expected BARs
   (0x40000000-family on domain 0, 0x58000000-family on domain 1), leave the endpoint driver absent,
   keep the misc window unread and aligned-dword accesses only. Acceptance: 6.18 boot log shows two
   endpoints; no vendor module loads; unit recovered healthy. This is the only Wi-Fi prerequisite
   that does not need the vendor source.
2. **Close the source gate and freeze the interface contract (host-now).** Record the source status
   as an explicit decision (GPL request sent, no reply, optional per GPL-REQUEST.md), keep this
   report + SUMMARY as the interface contract (module closure, firmware/cal files and hashes,
   /sys/hisys/hipriv userspace contract, cfg80211 surface, API delta), and correct the misleading
   claims listed in §7 in a follow-up doc edit. Acceptance: the target docs consistently say Wi-Fi
   [X] with the right reason; a later source arrival has a ready port spec.
3. **Only if source arrives: reproduce, then port.** Build the received SDK on 5.10 first and
   reproduce the shipped modules (symbol/vermagic/size contract in §3.1), then rebase in order plat
   (PCIe/firmware/INI/HCC) -> wifi (cfg80211 wiphy + 74 callbacks) -> ETE/data path, bench-gated per
   stage with the recovery card armed. If source does not arrive: **stop at C and do not fund D**
   (clean-room is years, and the firmware can never be made open).

---

## 6. (e) Extra evidence needed

Priority-ordered; "blocks" says which decision it affects.

| # | evidence | why it matters | how to get it | blocks? |
| --- | --- | --- | --- | --- |
| 1 | **Vendor GPL source + build config** for the two modules and the vendor kernel patch set | only path to option A; also settles the import_ns/kernel-patch question and the CONFIG_PM=n/PLT build flags | GPLv2 request follow-up to support@cudy.com (sent 2026-10-01, no reply; optional per project decision) or the Jeton same-SDK obligor | A only (not C) |
| 2 | **Vendor kernel config** (not embedded: stock Image has no IKCONFIG) | reproduce the module build flags and confirm PM/PLT/modversions/cfg80211-backports assumptions | vendor GPL tarball (item 1); until then the phase-14 ABI probe is the substitute | A |
| 3 | **Full endpoint config-space dump** (0x00..0xffc, incl. 0xff8), BAR0/BAR2 register snapshots, /proc/interrupts, sysfs binding | closes the endpoint<->band mapping (currently [I]) and the MSI-vs-INTx routing; needed for any own host-driver bring-up | read-only bench capture (attended; recovery armed; never read the RC misc window) | A/B; not C |
| 4 | **Firmware redistribution/licence terms** | any path that ships or ports the blob | vendor (item 1) or Cudy support; nothing in the rootfs | A (legal), B2 |
| 5 | **Calibration format/flow spec**: wifi_cali_data*.kv layout, factory partition offsets, alg set_* semantics, Hi5622V200_* scripts | a port must reproduce per-unit calibration exactly | local analysis of the snapshots (build/linux-kbuild/cal-snapshots) + read-only iwpriv alg probes at the bench; the 2.5.24 capture is marked "incomplete: 4 payload file(s) missing" | A |
| 6 | **Exact vendor netifd patch** (/lib/netifd/wireless/mac80211.sh vs stock OpenWrt 22.03.6) and the full hipriv command vocabulary | needed to decide "carry the vendor ABI" vs "implement nl80211"; the wal_hipriv_* handler symbols give the vocabulary, the exact script diff does not exist locally | local diff against the stock 22.03.6 package (needs the upstream package source; not in the local tree) | A |
| 7 | **Vendor kernel with symbols** (System.map/kallsyms from the stock kernel) | verify the actual export/namespace surface instead of the lab 5.10 proxy symvers | only from a vendor GPL build, or a kallsyms extraction from a rooted stock boot (read-only) | A |
| 8 | hi_kwificlk.ko clarification | referenced by wifi_init.sh but absent from both rootfs; presumably a no-op on this board, but it is a loose end in the load recipe | vendor source or another board's rootfs | A (minor) |
| 9 | From-scratch only: firmware register/message spec and the device-side dispatcher trace | needed only for option D | phases 37-49 continued + device-side instrumentation (host-unreachable per phase22/49) | D |

**Not a gate:** the GPL request itself. The project decision of 2026-11-18 (GPL-REQUEST.md:20-24) is
explicit that the source is an optional accelerator, not a blocker for the port's other lanes. This
report preserves that position: C is defensible without any new evidence; A waits on item 1.

---

## 7. Corrections to existing docs (recommendations; no other file was edited in this lane)

1. **PORT-SURFACE.md §2 / TARGET-DESIGN.md:741 / ROADMAP-OPENWRT.md:162** - "imports 211 symbols
   through the vendor HW_RTOS_NS namespace; none of which exist in upstream" is not supported by the
   binary evidence (§3.2): the 211 count is per-compiled-unit import_ns tags, the 308 undefined
   symbols resolve as 175 vendor-module + 26 cfg80211-backport + 133 kernel-5.10 imports with zero
   unresolved, and no shipped object or stock kernel exports HW_RTOS_NS. Recommend restating the
   numbers as measured and removing the "namespace provider must come with the SDK" implication.
2. **HYBRID-IMAGE-PLAN.md:43** - "the vendor Wi-Fi is mac80211/cfg80211 in the kernel modules" - the
   mac80211 half is wrong: 0 mac80211 symbols; it is a cfg80211-only full-MAC driver. (The cfg80211
   half is right.)
3. **REMAINING-DRIVERS.md:410** - the recorded private cfg+0x004 = 0x0006 conflicts with the PCI boot
   enumeration [59e7:0005] (stock boot log lines 626/645) and with the module's only pci_device_id
   entry (59e7:0005, plat.ko .rodata at file offset 0x20494 **[V]**). The PCI identity to use is
   59e7:0005; the private cfg-window word read should be re-labelled (it is not the standard
   config-space device ID).
4. **phase26/rootfs-swap-verdict.md:36-43 / HYBRID-IMAGE-PLAN.md:36-43** - "there is no vendor Wi-Fi
   userland at all ... it is all stock OpenWrt": true for daemons, but the shipped netifd
   mac80211.sh is **vendor-patched** to create/destroy VAPs via /sys/hisys/hipriv (§3.8). Any
   porting/blobbing decision must carry or replace that contract; "stock userspace" alone is not
   enough.

---

## 8. Reproduce the measurements in this lane

All commands are read-only and run from the parent repo root; no device is touched.

~~~sh
# (a) 2.3 blobs - sizes/hashes
sha256sum rootfs-2.4.15/lib/firmware/hi_wifi/* rootfs-2.5.24/lib/firmware/hi_wifi/*
md5sum    rootfs-2.4.15/lib/firmware/hi_wifi/FIRMWARE.bin

# (b) 3.1 module metadata and symbol surface
modinfo rootfs-2.5.24/lib/modules/5.10.201/hi5622v100_wifi.ko
modinfo rootfs-2.5.24/lib/modules/5.10.201/hi5622v100_plat.ko
readelf -S rootfs-2.5.24/lib/modules/5.10.201/hi5622v100_wifi.ko   # no __versions/.plt
nm -u rootfs-2.5.24/lib/modules/5.10.201/hi5622v100_wifi.ko | wc -l  # 308
readelf -p __ksymtab_strings rootfs-2.5.24/lib/modules/5.10.201/hi5622v100_wifi.ko | grep -c '^  ['  # 778

# (b) 3.3 provider map: for each undefined name, find the defining shipped module
#   (nm --defined-only over rootfs-*/lib/**/*.ko; see the lane log)

# (b) 3.1 "not a mac80211 driver"
nm -u rootfs-2.5.24/lib/modules/5.10.201/hi5622v100_wifi.ko | grep -c ieee80211_   # 1 = ieee80211_get_channel_khz (cfg80211)

# (c) 4.1/4.2 mainline + OpenWrt absence
grep -rIl 'hi115x\|hi5622\|hi_wifi\|hi_sdio' build/linux-kbuild/netport-pristine/linux-6.18.60 \
  --include='*.c' --include='*.h' --include=Kconfig --include=Makefile

# (d) 5.2 API delta: classify the module's kernel imports against the 6.18 export set
#   build/linux-kbuild/waves/2026-11-19-lsw-egress/validation/compile-tree/vmlinux.symvers
#   (133 wifi / 117 plat kernel imports; 11 / 16 not exported under the same name)

# (a)/(b) 2.2/3.6 stock boot evidence (UART capture, read-only)
grep -nE 'PCIe:|59e7:0005|FIRMWARE|Device plat ready|Hisilicon0|Create VAP|drv_mac80211_setup' \
  build/uart-logs/20261107T191003Z-poweron-uboot.log

# (b) 3.4 DT nodes (vendor base DTB/overlays decoded in docs/soc/vendor-dt-notes.md)
dtc -I dtb -O dts dumps/router-20261108/kernelb/kernelb.dtb -o /tmp/lab.dts   # lab DTB (shape only)

# (b) 3.5/3.8 userland load path
sed -n '1,95p'  rootfs-2.4.15/usr/bin/wifi_init.sh
sed -n '600,650p;940,960p' rootfs-2.4.15/lib/netifd/wireless/mac80211.sh
grep -n 'hsan\|wifi_init' rootfs-2.4.15/etc/init.d/hsan_prepare rootfs-2.4.15/etc/init.d/hsan_start
~~~

---

## 9. Bottom line

- **Hardware/bus/firmware:** Hi5622V100, a two-endpoint **PCIe** full-MAC radio (59e7:0005, 16 MiB
  BAR0 + 16 KiB BAR2 + 8 MiB BAR4 per function, INTx 207/209, cross-wired domains), driven by a
  closed **928,920-byte FIRMWARE.bin** (md5 0e530b97..., identical across releases and to Jeton), two
  config INIs and per-unit factory calibration. **[V]**
- **Vendor stack:** hi5622v100_plat.ko (PCIe/firmware/INI/HCC) + hi5622v100_wifi.ko (own MAC + 74
  cfg80211 callbacks; **not mac80211**), GPL-declared, needing cfg80211 (OpenWrt backports 5.15.92),
  ksecurec, hi_kpie and soft hooks into the switch/offload stack; loaded by a vendor rc.d sequence and
  driven by stock wpad over a **vendor-patched netifd** using /sys/hisys/hipriv. **[V]**
- **Driver availability:** none in mainline 6.18, none in OpenWrt, no source in the public TR6560 BSP
  (different radios) or Jeton (same SDK, newer binaries). GPL request sent 2026-10-01, no reply,
  optional. **[V]/[V*]**
- **Options:** **C (declare Wi-Fi unsupported on 6.18) now**; **A (port vendor source to 6.18, 10-16
  engineer-weeks after source)** only if the source arrives; **B2 (vendor 5.10 kernel hybrid)**
  remains the only Wi-Fi-capable system and is not a 6.18 answer; **B1 (vendor .ko on 6.18) is
  impossible**; **D (clean-room) is years** and cannot open the firmware.
- **First steps:** (1) PCIe enumeration milestone on 6.18 (patch 013/014, no vendor source needed);
  (2) close the source gate and freeze this interface contract / correct the docs; (3) if source
  arrives, reproduce the 5.10 build then port plat -> wifi -> data path.
