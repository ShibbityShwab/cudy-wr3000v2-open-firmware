# BUILD-NOTES - hi5622_wifi.ko with the cfg80211 scan/AP wiring

**Lane:** wifi-scan-ap (shared task task-113), host-only. **Date:** 2026-11-24.
**Source:** this directory's src/ (the wifi-cfg80211 tree plus the new
scan/provider/post layer, the task-110 wifi-vap-scan builders and the task-118 scan-D2H decoders
(`vapscan_frames.c/h` and `hi5622_scan_d2h.c/h` integrated with a
one-line portability edit each), and the documented integration edits of
REPORT.md sec 7).  No build-time patch is
applied.  The built .ko is local evidence only and is NOT committed
(src/.gitignore covers build products; no vendor binary/object/firmware byte is
involved).

## Result (canonical run: tools/build.sh)

| item | value |
| --- | --- |
| module | hi5622_wifi.ko |
| size | **704,720 B** |
| sha256 | **20d38c5c54a8e8673367e52d3564bd0f473ac49901075d4863a91a8671c5128b** |
| ELF | 32-bit LSB relocatable, ARM, EABI5 (with debug_info, not stripped) |
| license | GPL |
| vermagic | 6.18.60 SMP mod_unload ARMv7 p2v8 |
| depends | **cfg80211** |
| cfg80211 imports (U) | **9**: wiphy_new_nm, wiphy_register, wiphy_unregister, wiphy_free, cfg80211_register_netdevice, cfg80211_unregister_wdev, **cfg80211_scan_done**, **cfg80211_inform_bss_data**, **cfg80211_put_bss** |
| compiler/modpost warnings | **0** (project tree builds with -Werror) |
| host scan/provider/vapscan/scan-d2h selftest | **284/284 PASS** |
| host inherited cfg80211 logic selftest | **454/454 PASS** |
| host inherited H2D logic selftest | **242/242 PASS** |
| cfg80211 provider-symbols check | PASS (9 symbols, all real net/wireless exports, none tree-provided) |
| static driver/source selftest | **317/317 PASS** |

New module parameters (besides the inherited ones): **allow_frames** (uint,
default **0** = fail-closed: no provider-built frame is ever posted),
**frame_gap_ms** (uint, default **20**, minimum inter-frame gap; the post path
also bounded-waits for the previous coalesced announce to clear, so the vendor
order user_add -> user_add_notify is not refused as 'inflight').  The scan
decoder is always installed (tolerant; the g1 id15 trailer is [U] and the
reported signal is the documented 0 mBm placeholder).  Inherited params unchanged: `fw_path`, `wifi_block_path`, `plat_block_path`,
`allow_unseeded_release`, `hcc_poll_ms`, `hcc_cece_mode`, `rd_ab_write`,
`h2d_send_budget`, `cfg80211_auto`, `band_force`.

The module hash is **build-path dependent** (debug info records the build
directory); a copy built from another path has a different hash with the same
behaviour. Re-record the hash printed by the build you deploy.

## Verified tree / toolchain

    KDIR=/home/shibbityshwab/openwrt-lab-upstream-target/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
    STAGING_DIR=/home/shibbityshwab/openwrt-lab-upstream-target/staging_dir
    CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-

The tree has CONFIG_CFG80211_HEADERS=y and CONFIG_CFG80211 unset (OpenWrt
ships cfg80211 as kmod-cfg80211), so the 9 cfg80211 imports are declared for
modpost in src/cfg80211-provider.symvers. The target kernel must set
**CONFIG_CFG80211=m** (see ../wifi-cfg80211/cfg80211.config and
../wifi-upstream-target/CFG80211-BENCH-IMAGE.md). Override the tree with
KDIR_OVERRIDE / STAGING_DIR_OVERRIDE.

## Build line

    ./tools/build.sh

Log: /tmp/hi5622-wifi-scan-ap-build.log. Host test binaries:
/tmp/wifi-scan-ap-build/{scan_logic_selftest,cfg80211_logic_selftest,h2d_logic_selftest}.

## Tree quirks this build works around

1. **Module.symvers grammar**: a KBUILD_EXTRA_SYMBOLS file must contain NO
   comment lines and every line must be
   `<crc>TAB<sym>TAB<module>TAB<export>TAB<namespace>`; modpost's
   read_dump() `fatal()`s otherwise ("parse error in symbol dump file").
   tools/gen_cfg80211_symvers.py was corrected for this lane (it previously
   wrote header comments and a source filename in the export column).
2. **frame-larger-than**: the inherited `struct h2d_frame` carries a 0x674 B
   payload; putting it on the stack in the post path fails the kernel's
   1024-byte frame limit. hi5622_frame_post() heap-allocates it.
3. **cfg80211-unset header inlines**: the driver calls the unconditional
   exported symbols (cfg80211_unregister_wdev, cfg80211_scan_done, ...), never
   the CONFIG_CFG80211-conditional inline wrappers.
4. **vapscan integration**: the task-110 builders are pure C99; the only
   edit to the integrated copy is the shared host-type guard in
   `vapscan_frames.h` (no protocol bytes changed, pinned in
   tools/src-baseline.sha256).  Its `struct vs_frame` is 1.7 KB, so the kernel
   adapter heap-allocates it (same frame-larger-than reason as item 2).
5. **Shared tree**: other lanes build against their own OpenWrt clones; if
   "make modules" fails with a missing Module.symvers/vmlinux, another world
   build is in progress - rerun when it is idle. No source-tree state is
   modified by this lane.

## Verification steps (all device-free)

| step | command | result |
| --- | --- | --- |
| host scan/provider/vapscan/scan-d2h selftest | tools/scan_logic_selftest.c + src/hi5622_scan_logic.c + src/hi5622_vapscan_map.c + src/hi5622_scan_d2h.c + src/vapscan_frames.c + src/h2d_frames.c | **284/284 PASS** |
| host inherited cfg80211 regression | tools/cfg80211_logic_selftest.c + src/hi5622_cfg80211_logic.c | 454/454 PASS |
| host inherited H2D regression | tools/h2d_logic_selftest.c + h2d_send_logic.c + h2d_frames.c + hcc_core.c | 242/242 PASS |
| kernel module build | project 6.18.60 tree, -Werror, modpost | PASS, 0 warnings |
| artifact evidence | modinfo / sha256sum / cross-nm | table above |
| provider symbols | tools/gen_cfg80211_symvers.py --check | PASS (9 symbols) |
| static driver selftest | tools/selftest.py | all checks PASS |

No image build, no boot claim, no device contact: this is an out-of-tree
module against an already-built 6.18.60 tree; the bench sequence is
TEST-LADDER.md.
