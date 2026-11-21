# Wi-Fi framework port — SUMMARY

**Lane:** `wifi-framework-port` (host-only, read-only). **2026-11-22.** Full detail: `REPORT.md`.
**Folds in** `wifi-source-hunt` (b0b330d8), `wifi-firmware-re`, `wifi-driver-re`, `wifi-shim-port`.

## One-line verdict

The Hi5622V100 host driver is built on the **HiSilicon WLAN SDK framework** — the same code
family that OpenHarmony ships (GPL-2.0) for the sibling Hi3881 and that an unofficial kernel
mirror ships for Hi1105. **Open trees now name-cover 1,183 of the 4,946 functions (23.9 %) and
28.5 % of the function-code bytes** in `hi5622v100_wifi.ko` + `hi5622v100_plat.ko`; the
framework skeleton **compiles into a real `hi3881_fw_skel.ko` against our 6.18.60 kernel**.
The remaining 3,767 functions (≈1.0 MB of code) are the hi5622 chip delta (PCIe/BAL transport,
hdpp data path, chip-specific hmac/wal) and stay original reverse work.

## Census numbers (3 trees: hi3881 GPL + themactep + hi1105 mirror)

| measure | value |
| --- | --- |
| wifi.ko FUNC symbols | 4,245 (all LOCAL-bound; exports go via ksymtab: 778 names) |
| plat.ko FUNC symbols | 703 (ksymtab exports: 299 names) |
| .ko union (unique) | 4,946 (2 shared: init_module/cleanup_module aliases) |
| hi3881 source (OpenHarmony, GPL-2.0) | 2,174 function definitions |
| themactep/wifi-hi3881 (no license grant) | 2,273 definitions |
| hi1105/hi11xx mirror (no license grant) | 9,106 definitions |
| **shared by name, hi3881 trees only** | **560 (11.3 %)** |
| **shared by name, all 3 trees** | **1,183 (23.9 %)** |
| **shared bytes of the 1,418,440 B function code** | **403,920 B (28.5 %)** |
| hi5622 delta (ko-only) | **3,767 functions / 1,014,560 B** |

Cross-validation: **33 of 39** cfg80211_ops callbacks and **12 of 44** decoded D2H event
handlers match hi3881 source by name — the wal_cfg80211 surface and the core scan/BA/connect
event layer are the shared framework; the hi5622 extras (ACS, DFS, spectral, CSI, multiap,
temperature, power) are not.

## Family classification (all-3-tree census)

| family | .ko | shared | ko-only | reading |
| --- | ---: | ---: | ---: | --- |
| wal_ | 1014 | 330 | 684 | cfg80211 glue: 33 % shared |
| hmac_ | 2029 | 526 | 1503 | MAC/MLME: 26 % shared |
| mac_ | 367 | 218 | 149 | shared MAC core: 59 % shared |
| hdpp_ | 238 | 0 | 238 | hi5622 data path (hi1105 calls it dpe_) |
| alg_ | 239 | 17 | 222 | algo/cali config: hi5622-specific |
| shuangta_ | 173 | 0 | 173 | chip-codename layer, no source anywhere |
| oal_ | 211 | 41 | 170 | OS layer: hi3881 naming diverged |
| oam_ | 109 | 10 | 99 | logging/ops layer |
| hal_ | 99 | 8 | 91 | chip register HAL: hi5622-specific |
| hcc_ | 87 | 1 | 86 | msg-layer renamed between hi3881/hi5622 |
| pcie_ | 101 | 0 | 101 | PCIe/ETE transport: hi5622-specific |
| bal_ | 26 | 0 | 26 | bus abstraction: hi5622-specific |
| smac_/dmac_ | 0 | 0 | 0 | firmware-side (device .bin); host glue lives in hmac_dmac_* |

## Build milestone (M3)

`hi3881_fw_skel.ko` — **892 KB, ELF32 ARM, license=GPL, vermagic=6.18.60**, built with
`arm-openwrt-linux-muslgnueabi-gcc` (OpenWrt toolchain, GCC 14.4.0) against the built
6.18.60 tree. Contains libsec + frw + oal_mem/oal_util + oam_log/oam_main + mac/common +
skeleton glue (28 objects, 307 text symbols). 13 portability fix classes applied — full log in
`build/BUILD-NOTES.md`; the biggest single fix is the vendor `LINUX_VERSION_CODE = 3.10.0`
pin in `oal_util.h`, which had silently selected every oldest kernel-API branch.

## Port plan (M4) and verdict (M5)

See `REPORT.md` §6/§7. Short form: **framework-from-source** (wal + mac/common + frw/oal/oam
core + the hi1105 hmac files matching the shared 1,183 names) **+ hi5622 delta** (pcie_/bal_/
shuangta_/hal_/hdpp_ families + the ko-only hmac_/wal_/alg_/oal_ work: **≈3,767 functions,
≈1.0 MB of code**, of which 445 are ≤16-byte stubs) — transport to be rebuilt from the
`wifi-driver-re` mailbox/ETE contract, **not** from the hi3881 SDIO hcc_host. Hi1152 is the
hi5622 sibling (acwifi.net/19630); no public hi5622/luofu source exists (source-hunt lane).
Verdict: **23.9 % name / 28.5 % byte coverage by open source; the module pair is now
framework-covered but still 71 % original work, and the closed firmware keeps any result
upstream-impossible** — vendor GPL source remains the only fast path.

## Files

`REPORT.md` (full), `census/` (family tables, shared-by-name CSV, hi5622-delta CSV,
sizecov.json), `build/` (Kbuild draft, linuxkpi_compat.h, skel_mod.c, BUILD-NOTES.md).
Commit: see git log (submodule `opensource`).
