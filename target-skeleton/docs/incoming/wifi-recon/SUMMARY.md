# Wi-Fi recon SUMMARY - Hi5622V100 / Cudy WR3000 v2.0 (luofu R116)

**Lane:** wifi-recon, host-only (local artifacts; no device access, no reboots). **Date:** 2026-11-21.
**Full report:** [REPORT.md](REPORT.md) (evidence tables, module closure, API-delta numbers, corrections,
reproduction commands). **Status of the recommendation: no code, no commitment; evidence + options.**
**Markers:** [V] reproduced from local artifacts in this lane; [V*] device-measured earlier and recorded
in the cited doc; [I] inference; [U] open; [X] absent/blocked.

## One-paragraph verdict

The WR3000 v2.0's radio is a **Hi5622V100** reached over **PCIe** as **two endpoint functions**
(59e7:0005, one per band) behind the SoC's two DesignWare root complexes, driven by the vendor's
**closed, GPL-declared** modules hi5622v100_plat.ko + hi5622v100_wifi.ko (a cfg80211 full-MAC driver,
**not mac80211**) and a **closed 928,920-byte FIRMWARE.bin**, with a per-unit calibration flow. No
mainline, OpenWrt, public-BSP or Jeton-source driver exists for it; the GPL request was sent
2026-10-01 with no reply and is explicitly optional per project decision. Therefore the 6.18 target
keeps **no Wi-Fi** (option C) until vendor source arrives (option A, 10-16 engineer-weeks after
source); the only working Wi-Fi today is the vendor 5.10 kernel stack (option B2, not 6.18).

## Hardware / bus / firmware

| item | value | evidence |
| --- | --- | --- |
| radio | Hi5622V100, vendor codename ShuangtaV100; chip id 0x34/rev 0x00; firmware banner ChenTangV100R001C20T13 | module strings + FIRMWARE.bin + cal snapshot identity [V]/[V*] |
| bus | **PCIe**, 2 endpoint functions, one per DWC root complex; not SDIO/USB | vendor DTB + stock boot log [V] |
| functions | 0000:00:00.0 (domain 0, BAR0 0x40000000, BAR2 0x41800000, BAR4 0x41000000, phy_devid 1, INTx 207) and 0001:00:00.0 (domain 1, 0x58000000 / 0x59800000 / 0x59000000, phy_devid 0, INTx 209); both 59e7:0005 class 0x028000; cross-wired (is_pcie_cross=1) | stock boot log lines 626-658 + phase21/sibling-ep.md:99-104 [V]/[V*] |
| mmio per function | 16 MiB BAR0 + 16 KiB BAR2 + 8 MiB BAR4, 64-bit; MSI capable but vendor uses INTx | DRIVER-BLACKBOX.md:8-13 [V*] |
| firmware blob | /lib/firmware/hi_wifi/FIRMWARE.bin, 928,920 B, md5 0e530b976d5a20e87358671f1a577695 - identical in 2.4.15 and 2.5.24 and in Jeton's sibling build; loaded by plat.ko (download firmware file buf len is [524288] ... firmware_download success) | [V] rootfs + stock log 861-864; phase29 |
| config | cfg_device_hisi.ini 799 B + cfg_hi5622v100_hisi.ini 32,315 B (ITCM/DTCM load map, power/regdomain tables) | [V]; DRIVER-BLACKBOX.md:45-63 |
| calibration | per-unit factory KV (wifi_cali_data.kv 8,912 B + _2g.kv 2,336 B) + userland iwpriv alg set_* flow | [V] snapshots + scripts |
| firmware licence | **no licence/redistribution text anywhere in the rootfs** | [V] |

## Vendor stack (what drives it)

- **Two modules**: hi5622v100_plat.ko (364,660 B 2.5.24; PCIe rox_pci0 driver, firmware download,
  INI parser, HCC msg/queue/timer layer, /sys/hisys kobject, 299 exported symbols) and
  hi5622v100_wifi.ko (3,564,728 B; own MAC/MLME + 74 wal_cfg80211_* callbacks + 778 exports;
  **0 mac80211 symbols**, 26 cfg80211 imports; GPL; vermagic "5.10.201 SMP mod_unload ARMv7").
  Both built **without CONFIG_MODVERSIONS and without ARM module PLTs**; cfg80211 ABI is OpenWrt
  **backports v5.15.92** with the vendor kernel behaving as **CONFIG_PM=n** (phase14). [V]/[V*]
- **Hard closure**: wifi <- plat (138 symbols) + cfg80211 (26) + ksecurec (9) + hi_kpie (2); plat <-
  ksecurec (8) + hi_kpie (1). Runtime __symbol_get hooks: hi_woc_wifi_rtx (5 hi_wifi_* hooks) and
  hi_kcfe_lrn (2 hi_cfe_flush_* hooks) - i.e. the stack also leans on the SoC switch/offload stack.
  [V]
- **Load path**: /etc/init.d/hsan_prepare (S09) runs /etc/rc.d/luofu (base hi_* modules; no Wi-Fi) ->
  hsan_start (S15) -> /usr/bin/wifi_init.sh (hi_pcie -> plat -> wifi -> hi5622v100_cal_init.sh) ->
  stock wpad/netifd. **The vendor patched OpenWrt's netifd**, which creates/destroys VAPs by writing
  /sys/hisys/hipriv instead of the nl80211 interface-add path (mac80211.sh:608-641); there is no
  vendor Wi-Fi daemon, but there is a vendor userspace ABI. [V]
- **Boot evidence** (stock UART capture lines 616-1250): both endpoints enumerated/resourced, INI
  parse, power-on, firmware download, Device plat ready, Hisilicon0 netdev, 2g/5g calibration
  success, then VAP creation on phy0/phy1 and bridging. [V]
- **Constraints**: vendor module cannot be unloaded (exit-path NULL call -> panic; a patch only moves
  the fault, phase15); modprobe -r absent; the takeover line reached firmware/mailbox/rings but not a
  working data path (firmware H2D dispatcher never runs; TX-credit comes from the chip layer).
  [V*]
- **Two corrections** for existing docs: (1) PORT-SURFACE's "211 namespaced symbols" is a per-object
  .modinfo tag count, not 211 symbols - all 308 undefined symbols resolve against shipped modules +
  5.10 kernel exports, and no object/kernel exports HW_RTOS_NS; (2) HYBRID-IMAGE-PLAN's "vendor Wi-Fi
  is mac80211/cfg80211" is wrong on the mac80211 half (0 mac80211 symbols). See REPORT.md §3.2, §7.

## Driver availability

| candidate | result |
| --- | --- |
| Linux 6.18.60 mainline tree | **no** hi5622/hi115x/hi_wifi/hi_sdio driver, no hisilicon wireless directory [V] |
| their OpenWrt 6.18 tree / target-skeleton / upstream series | **no** driver, package or enabled DTS node [V] |
| public Triductor TR6560 BSP (same IP family) | Wi-Fi blobs are for **other chips** (tr5120/tr5220); no Hi5622 source [V*] |
| Jeton AX3000 Core (same SDK) | same FIRMWARE.bin md5, **newer closed** hi5622v100_*.ko builds; no source [V*] |
| vendor modules | GPL-declared; GPLv2 request sent 2026-10-01, no reply, **not a gate** per project decision [V*] |

**Conclusion: no driver exists for a 6.18 OpenWrt target today.**

## Options with effort / risk

| option | effort | risk | verdict |
| --- | --- | --- | --- |
| **A. Port vendor driver to 6.18** (requires vendor GPL source) | **10-16 engineer-weeks after source** (PCIe host 2-4 wks; module/kernel API 2-4; cfg80211 1-2; firmware/cal/userspace 1-2; bench 2-4) | medium-high: 4,838-function vendor MAC, cfg80211 5.15.92->6.18 moves, PCIe write path never booted, blob licence unresolved | only path to Wi-Fi on 6.18; blocked on source |
| **B1. 6.18 kernel + vendor 5.10 .ko** | - | guaranteed failure (vermagic, no modversions, unload oops, struct ABI) | **impossible** |
| **B2. Vendor 5.10.201 kernel + vendor modules + current userland** | 1-2 days per vendor release | vendor kernel age; no upstream path | **only working Wi-Fi today; keep as fallback, not as 6.18 answer** |
| **C. Declare Wi-Fi unsupported on 6.18** | 0 (already recorded position) | product gap only | **recommended now** (RELEASE-NOTES.md:96; TARGET-DESIGN.md:734-758) |
| **D. Clean-room driver + firmware** | years | very high | not recommended (firmware can never be made open) |

## First three concrete steps

1. **PCIe enumeration milestone on 6.18** - validate patch 013/014 in an attended run; enable the RC
   nodes; prove both 59e7:0005 functions enumerate with the expected BARs; keep the endpoint driver
   absent, misc window unread, dword-aligned accesses only. No vendor source needed; acceptance is a
   6.18 boot log with two endpoints and a healthy unit.
2. **Close the source gate and freeze the interface contract** - record GPL status (sent 2026-10-01,
   no reply, optional); keep this REPORT/SUMMARY as the port spec; correct the four misleading claims
   in §7 of REPORT.md. Acceptance: target docs say Wi-Fi [X] with the correct reason.
3. **Only if source arrives: reproduce, then port** - build the received SDK on 5.10 first (reproduce
   the shipped modules), then rebase in order plat -> wifi -> ETE/data path, bench-gated per stage
   with recovery armed. If no source: stop at C; do not fund D.

## Extra evidence needed (priority)

1. Vendor GPL source + vendor kernel config/build flags (option A gate; optional not a project gate).
2. Full endpoint config-space dump (incl. 0xff8), BAR0/BAR2 snapshots, /proc/interrupts, sysfs binding
   - closes domain<->band mapping (currently [I]) and MSI/INTx routing.
3. Firmware redistribution/licence terms (any blob path).
4. Calibration format/flow spec (wifi_cali_data*.kv, factory offsets, alg set_* semantics).
5. Exact vendor netifd patch + full hipriv vocabulary (carry vs replace decision).
6. Vendor kernel System.map/kallsyms (verify export/namespace surface).
7. hi_kwificlk.ko clarification (referenced by wifi_init.sh, absent from both rootfs).

## Evidence pointers

- This lane: opensource/target-skeleton/docs/incoming/wifi-recon/REPORT.md (§2 hardware/bus/firmware,
  §3 vendor stack, §4 driver absence, §5 options, §6 evidence, §7 corrections, §8 reproduce commands).
- Firmware/cal hashes: REPORT.md §2.3; module metadata: §3.1; dependency closure: §3.3; load path and
  stock boot evidence: §3.5-§3.6; userspace contract: §3.8; API delta: §5.2.
- Prior docs: opensource/docs/DRIVER-BLACKBOX.md, PORT-SURFACE.md, TARGET-DESIGN.md §8.3,
  REMAINING-DRIVERS.md §4.2, phase14/abi-match.md, phase15/bringup.md + unload-patch.md,
  phase21/sibling-ep.md, phase29/jeton-shared-sdk-proven.md, phase50/bsp-build-status.md,
  GPL-REQUEST.md, opensource/target-skeleton/RELEASE-NOTES.md.
- Local artifacts: rootfs-2.4.15/ and rootfs-2.5.24/ (parent repo, gitignored), dumps/router-20261108/,
  build/uart-logs/20261107T191003Z-poweron-uboot.log, build/linux-kbuild/cal-snapshots/,
  build/linux-kbuild/netport-pristine/linux-6.18.60/, dts/luofu-r116.dts, patches-6.18/013+014.
- Constraints: none of this authorises a device action; bench work is attended with the recovery door
  armed, UART read-only, never read RC misc 0x10161000, aligned-dword DBI/cfg only.
