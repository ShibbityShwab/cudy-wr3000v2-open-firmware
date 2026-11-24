# BUILD-NOTES - hi5622_wifi.ko with scan-complete (task-123)

**Lane:** wifi-scan-complete (shared task task-123), host-only. **Date:** 2026-11-26.
**Source:** this directory's `src/` = the wifi-scan-ap tree (task-113, module
fired 2026-11-26) + the wifi-vap-start provider patch (task-117) +
`vapstart_frames.c/.h` (task-117) + the scan-complete changes (REPORT.md
section 5). No build-time patch is applied to the lane; the integration patch
in `patches/` exists to port the changes back onto the base tree.

The built `.ko` is local evidence only and is NOT committed
(`src/.gitignore` covers build products; no vendor binary/object/firmware byte
is involved).

## Build line

    ./tools/build.sh

Environment (canonical project tree):

    KDIR=/home/shibbityshwab/openwrt-lab-upstream-target/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
    STAGING_DIR=/home/shibbityshwab/openwrt-lab-upstream-target/staging_dir
    CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-

## Result (canonical run: tools/build.sh, 2026-11-26; base 738,112 B ->
748,888 B, +10,776 B from the scan-complete decoder/telemetry and debug info)

| item | value |
| --- | --- |
| module | hi5622_wifi.ko |
| size | **748,888 B** |
| sha256 | **b42ff4ec775e7b223baefeb4a655bb925e3c88eca3560634ed87ac53029e1884** |
| ELF | 32-bit LSB relocatable, ARM, EABI5 (with debug_info, not stripped) |
| vermagic | 6.18.60 SMP mod_unload ARMv7 p2v8 |
| depends | cfg80211 |
| cfg80211 imports (U) | 9 (wiphy_new_nm, wiphy_register, wiphy_unregister, wiphy_free, cfg80211_register_netdevice, cfg80211_unregister_wdev, cfg80211_scan_done, cfg80211_inform_bss_data, cfg80211_put_bss) |
| compiler/modpost warnings | 0 (project tree builds with -Werror) |
| host scan-complete selftest | 195/195 PASS |
| host inherited scan/provider selftest | 290/290 PASS |
| host inherited cfg80211 regression | 454/454 PASS |
| host inherited H2D regression | 242/242 PASS |
| cfg80211 provider-symbols check | PASS (9 symbols) |
| static lane selftest | 373/373 PASS |
| integration patch round-trip | `patches/0001-scan-complete.patch` applies to the pinned base tree and reproduces this tree |

The module **size and sha256 are build-path dependent** (debug info records
the build directory): the same committed tree rebuilt from
`/tmp/final-check` produced 748,564 B sha256 `02162d4a...` with the same
behaviour, zero warnings and identical selftest counts. Re-record the numbers
printed by the build you deploy.

## Module parameters

New in this lane (beyond the inherited ones):

| param | default | meaning |
| --- | ---: | --- |
| `scan_timeout_ms` | 0 | scan completion bound; 0 = auto `3000 + 900*n_channels` (14,700 ms for 13), clamped [1000, 45000] by the loader; writable |
| `scan_dwell_ms` | 0 | g0 id0xC dwell override ms (0 = vendor 60; 20..200), bench sweep |
| `scan_trust_rssi` | 0 | report cfg80211 signal from g1 id15 trailer byte +5 ([I]; opt-in correlation only) |

Inherited and unchanged: `fw_path`, `wifi_block_path`, `plat_block_path`,
`allow_unseeded_release`, `hcc_poll_ms`, `hcc_cece_mode`, `rd_ab_write`,
`h2d_send_budget`, `cfg80211_auto`, `band_force`, `allow_frames`,
`frame_gap_ms`, `vap_start_first`, `vap_start_mode`, `vap_start_module`.

## Tree quirks this build works around

Inherited from wifi-scan-ap (still true): Module.symvers grammar for the
9-symbol cfg80211 provider file, heap-allocated `struct h2d_frame` (0x674 B
payload > the 1024 B stack frame limit), heap-allocated `struct vs_frame`
(1.7 KB), cfg80211-unset header inlines (call the exported symbols), shared
KDIR must be idle during a world build.

New for this lane: none - the changes are compiler-clean against the same tree
and add no new external symbols (the cfg80211 import list is unchanged at 9,
checked by tools/gen_cfg80211_symvers.py).
