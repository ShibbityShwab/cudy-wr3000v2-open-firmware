# BUILD-NOTES - merged provider + data-plane module (wifi-dp-air, task-126)

**Lane:** wifi-dp-air (shared task task-126), host-only. **Date:** 2026-11-26.
No device was contacted while writing, building or testing this lane.

## 1. Environment used (authoring host)

* kernel tree (canonical): `/home/shibbityshwab/openwrt-lab-upstream-target/
  build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60`
  (configured; the build script falls back to another configured luofu 6.18.60
  tree if it was cleaned - a fallback changes the .ko sha256, see section 4)
* toolchain: `openwrt-lab-upstream-target/staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi`
* host compiler: `cc` (gcc, C99, -Werror -pedantic for host selftests)
* host python: `.provision-venv/bin/python`
* base source: `../wifi-scan-complete/src` (provider, 47 files) - **never**
  edited on disk; the merge is staged as a scratch copy
* vendor artifacts: read in place by the inherited decode tools only (nothing
  vendor is copied or committed)

## 2. Canonical build

    cd opensource/docs/incoming/wifi-dp-air && ./tools/build.sh

Sections (all pass on the authoring host):

    == 1. merge provenance (provider base -> this src tree) ==
    added : dp_rings.c dp_rings.h dp_txrx.c dp_txrx.h hi5622_dp.c hi5622_dp.h
            hi5622_dp_txrx.c hi5622_dp_txrx.h
    mod   : Kbuild h2d_send.c h2d_send.h h2d_send_logic.c h2d_send_logic.h
            hi5622_driver.h hi5622_hcc.h main.c
    patch : 139848 B sha256 254148c310b4970f2bcb07b78f1d0687b3b29fc9bae519244db707fe281a6e54
    PASS: merge patch reproduces the lane tree; scope + drift checks OK
    == 2. inherited host regressions on the merged tree ==
    h2d_send_logic selftest: 242/242 checks PASS
    SUMMARY: 254 passed, 0 failed            (wifi-dp-txrx portable TX/RX)
    290/290 checks passed                    (wifi-scan-ap scan/provider logic)
    195/195 PASS                             (wifi-scan-complete)
    454/454 checks passed                    (wifi-cfg80211 logic)
    == 3. merged-tree data-plane selftest ==
    wifi-dp-air merged-tree selftest: 48/48 checks PASS
    == 4. F-AIR protocol-sequence validation (bench tooling) ==
    sequence: 30 steps, 83 checks, 7 writes, 344 rendered lines (order checked)
    sequence sha256: 96354365867beaedeb77c001fed31a2afdb0d4d2edb949cd898fab2fcc8342f6
    == 5. kernel module build (project 6.18.60 tree, staged src copy) ==
    module      : /tmp/wifi-dp-air-build/src/hi5622_wifi.ko
    size_bytes  : 884576
    sha256      : ec74fccd7c6d26585b30604828d4469100bb6714ada7dddf1e1b2ea0320a98e3
    vermagic    : 6.18.60 SMP mod_unload ARMv7 p2v8
    cfg80211 U  : 9 (wiphy_* + cfg80211_*)
    warnings    : 0 (compiler+modpost; 0 expected)
    == 6. device-free source/publication checks ==
    OK: merge provenance + 1435 inherited host checks + 48 merged checks +
        sequence + clean module build + publication checks
    artifact: 884576 B sha256 ec74fccd...

Total device-free host checks on the merged tree: **1,483**
(242 + 254 + 290 + 195 + 454 inherited + 48 merge-specific).

## 3. Artifact (canonical run, 2026-11-26)

| item | value |
| --- | --- |
| module | `hi5622_wifi.ko` |
| size | **884,576 B** |
| sha256 | **ec74fccd7c6d26585b30604828d4469100bb6714ada7dddf1e1b2ea0320a98e3** |
| ELF | 32-bit LSB relocatable, ARM, EABI5 (with debug_info, not stripped) |
| vermagic | 6.18.60 SMP mod_unload ARMv7 p2v8 |
| depends | cfg80211 |
| cfg80211 imports (U) | 9 (wiphy_new_nm, wiphy_register, wiphy_unregister, wiphy_free, cfg80211_register_netdevice, cfg80211_unregister_wdev, cfg80211_scan_done, cfg80211_inform_bss_data, cfg80211_put_bss) |
| compiler/modpost warnings | 0 (project tree builds with -Werror) |
| base provider module | 748,888 B sha256 b42ff4ec... (../wifi-scan-complete/BUILD-NOTES.md) |
| merge delta | +135,688 B (dp_rings + hi5622_dp + dp_txrx + hi5622_dp_txrx objects and their debug info) |

Required data-plane symbols in the built module (checked by the script):
`hi5622_dp_txrx_bind`, `h2d_post_txq_addr`, `dp_mac_program`,
`dp_txrx_txe_encode`, `hi5622_dp_probe`, `hi5622_dp_rx_poll_store`,
`hi5622_cfg80211_init`.

## 4. Determinism / build-path sensitivity

The .ko sha256 is **build-path dependent** (debug info records the build
directory), like every prior lane. The numbers above belong to the script's
canonical staging path `/tmp/wifi-dp-air-build/src` with the default kernel
tree. Re-running the script on this host reproduced byte-identical
size/sha256; a different staging path or a fallback kernel tree produces a
different (behaviour-identical) artifact - always record
`sha256sum`/`modinfo` of the file you actually deploy. The F-AIR sequence does
not pin the module hash for this reason (it pins the attribute surface, the
budget and the load parameters instead; a wrong module is refused at plan time
by module-param/attribute checks).

## 5. Module parameters (all present in the merged build)

Inherited and unchanged: `fw_path`, `wifi_block_path`, `plat_block_path`,
`allow_unseeded_release`, `hcc_poll_ms`, `hcc_cece_mode`, `rd_ab_write`,
`h2d_send_budget`, `cfg80211_auto`, `band_force`, `allow_frames`,
`frame_gap_ms`, `vap_start_first`, `vap_start_mode`, `vap_start_module`,
`scan_timeout_ms`, `scan_dwell_ms`, `scan_trust_rssi`.

No new module parameter was added by the merge. The F-AIR sequence load params:

    fw_path=hi_wifi/FIRMWARE.PD4.bin  wifi_block_path=<local block, never committed>
    h2d_send_budget=4   # 3 H2D frames: ctx_create + txq_sync72 + key-0x0320
    allow_frames=12 frame_gap_ms=100
    vap_start_first=1 vap_start_mode=1 vap_start_module=0

## 6. Deployment order (attended; see LADDER-F-AIR.md)

1. `insmod /tmp/cfg80211.ko` (cfg80211 image; the module depends on it),
2. `insmod /tmp/hi5622_wifi.ko <params above>`,
3. the standard boot ladder (`prepare/upload/verify/seed/hcc_arm/hcc_loop/release`)
   via the bench session - the F-AIR sequence starts after the ready pair and
   the release,
4. run the F-AIR sequence (bench tooling) or the manual stages in
   LADDER-F-AIR.md.

## 7. Handoffs

* **bench-session (task-127):** the merged module is ready for the attended
  fire package. Exact F-AIR sequence path:
  `../wifi-bench-tooling/tools/sequences/bringup-f-air-txrx.json`
  (30 steps / 83 checks / 7 device writes). bench_guards already
  allowlists the four txrx attributes (DP_ATTRS/DP_WO_ATTRS + WO readback
  mapping committed with this lane). Build the module with this lane's
  `tools/build.sh`, keep the printed size/sha256 in the session evidence, and
  compare it with `sha256sum` on the box before insmod.
* **bench tooling:** 563/563 device-free selftest checks pass with the new
  sequence + policy (was 546; +17 F-AIR allowlist/policy/sequence checks).
* **lead / STATUS:** this lane is host-only and not fired; it does not move the
  bench state machine. It is the module+ladder for the next attended
  data-plane rung (F-AIR) after the mode-1 VAP/scan milestone.
* **wifi-dp-txrx:** the merge this lane was asked for is done; the standalone
  E1-post-only ladder F remains valid for that module and is unchanged.
