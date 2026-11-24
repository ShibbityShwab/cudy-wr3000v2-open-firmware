# BUILD-NOTES - hi5622_wifi.ko with the cfg80211 full-MAC M0-M2 layer

**Lane:** wifi-cfg80211 (shared task task-104), host-only.  **Date:** 2026-11-24.
**Source:** this directory's src/ (a copy of the wifi-h2d-send tree - itself
the wifi-driver-clean tree plus the H2D frame/send path - with the new
hi5622_cfg80211* files and the integration edits listed in REPORT.md sec 2).
No build-time patch is applied.  The built .ko is local evidence only and is
NOT committed (src/.gitignore covers build products; no vendor
binary/object/firmware byte is involved).

## Result

| item | value |
| --- | --- |
| module | hi5622_wifi.ko |
| size | **504,800 B** |
| sha256 | **7d01e340965a1a9711fe6893cbe343e298adb5ebf1d75ea4df3b2f1fd1215e1a** |
| ELF | 32-bit LSB relocatable, ARM, EABI5 (with debug_info, not stripped) |
| license | GPL |
| vermagic | 6.18.60 SMP mod_unload ARMv7 p2v8 |
| depends | **cfg80211** |
| objects | main.o transport.o ete.o d2h.o hcc.o service.o hcc_core.o hcc_core_kernel.o h2d_frames.o h2d_send_logic.o h2d_send.o **hi5622_cfg80211_logic.o hi5622_cfg80211.o hi5622_cfg80211_ops.o** |
| compiler/modpost warnings | **0** (project tree builds with -Werror) |
| cfg80211 imports (U) | 6: wiphy_new_nm, wiphy_register, wiphy_unregister, wiphy_free, cfg80211_register_netdevice, cfg80211_unregister_wdev |
| host cfg80211 logic selftest | **447/447 PASS** |
| host inherited H2D logic selftest | **242/242 PASS** |
| driver selftest (source/contract) | **138/138 PASS** |
| cfg80211 provider-symbols check | PASS (6 symbols, all real net/wireless exports, none tree-provided) |

Module params: fw_path, wifi_block_path, plat_block_path,
allow_unseeded_release, hcc_poll_ms, hcc_cece_mode, rd_ab_write,
h2d_send_budget (all inherited) + **cfg80211_auto** (uint, default 1: register
the wiphy automatically once the HCC ready handshake is decoded) +
**band_force** (int, default -1: auto per PCI domain; 0 = 2.4 GHz; 1 = 5 GHz).

The module hash is **build-path dependent** (debug info records the build
directory), so a copy built from another path has a different hash but the
same behaviour/flags; record the hash printed by the build you deploy.

## Build line

Canonical (host selftests + kernel module + provider check + driver selftest):

    ./tools/build.sh

It exports STAGING_DIR (the OpenWrt GCC wrapper requires it), builds with:

    KDIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
    STAGING_DIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir
    CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
    make ARCH=arm CROSS_COMPILE=$CROSS_COMPILE -C $KDIR M=$(pwd)/src modules

Logs: /tmp/hi5622-wifi-cfg80211-build.log.
Host test binaries: /tmp/wifi-cfg80211-build/{cfg80211_logic_selftest,h2d_logic_selftest}.

## Tree quirks this build works around

1. **CONFIG_CFG80211 unset on the luofu tree** (OpenWrt ships cfg80211 as the
   kmod-cfg80211 package), while CONFIG_CFG80211_HEADERS=y keeps the header
   API and struct net_device's ieee80211_ptr available.  The six cfg80211
   imports are declared for modpost in src/cfg80211-provider.symvers
   (KBUILD_EXTRA_SYMBOLS), the same technique as
   ../wifi-wal-glue/glue/cfg80211-provider.symvers.  The target kernel needs
   CONFIG_CFG80211=m - see ../cfg80211.config (handoff to task-107).
   Re-validate with: tools/gen_cfg80211_symvers.py --check (or --update).
2. **IS_ENABLED(CONFIG_CFG80211) header inlines** (e.g.
   cfg80211_unregister_netdevice()) compile to nothing on a cfg80211-unset
   build tree and would be wrong at runtime; the driver calls the
   unconditional exported cfg80211_unregister_wdev() instead (selftest
   enforced).
3. **CONFIG_MODULE_STRIPPED=y** - module metadata is emitted by the copied
   main.c unconditionally (unchanged from the clean driver).
4. **Shared kernel tree**: the project tree is also used by other lanes; at
   one point an OpenWrt world rebuild removed its Module.symvers/vmlinux
   mid-run.  If "make modules" fails with "Module.symvers is missing" /
   unresolved symbols, wait for the other build to finish (or rerun after the
   tree is rebuilt) and re-run tools/build.sh; the failure is tree state, not
   this module.

## Verification steps (all device-free)

| step | command / file | result |
| --- | --- | --- |
| host compile of the cfg80211 logic | cc -std=c99 -O2 -Wall -Wextra -Werror -pedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes on cfg80211_logic_selftest.c + hi5622_cfg80211_logic.c | PASS |
| host cfg80211 model/op-map selftest | /tmp/wifi-cfg80211-build/cfg80211_logic_selftest | **447/447 PASS** |
| host inherited H2D logic selftest | /tmp/wifi-cfg80211-build/h2d_logic_selftest | **242/242 PASS** |
| kernel module build | project 6.18.60 tree, -Werror, modpost | PASS, 0 warnings/errors |
| artifact evidence | modinfo / sha256sum / nm | see table above |
| cfg80211 provider symbols | tools/gen_cfg80211_symvers.py --check | PASS |
| driver selftest | tools/selftest.py | **138/138 PASS** |

The driver selftest checks: required layout; Kbuild objects + provider
declaration; main.c/service.c/driver.h integration; exactly one
wiphy_register call site inside the readiness work; the pure registration-gate
function; RE-derived capabilities (13 + 19 channels, no out-of-plan 5 GHz
channels, cipher list, 12 scan SSIDs, documentation perm_addr); no WDS/NAN
advertisement; the exact 6 populated cfg80211 ops and -EOPNOTSUPP stubs; no
deferred op registered; netdev TX drop stub + needs_free_netdev; the
cfg80211_register_netdevice / cfg80211_unregister_wdev call discipline and the
"do not free the wdev" rule; the 39-row op map and 19-row frame reference
table with the task-103 pending markers; the provider-symbols check; the
kernel-config fragment; the REPORT/README content requirements; file-by-file
parity with the inherited wifi-h2d-send protocol tree (only main.c, service.c,
hi5622_driver.h, Kbuild and .gitignore may differ); and that no vendor
binary/firmware/calibration file is committed (build products stay
gitignored).

No image build, no boot claim: this is an out-of-tree module against the
already-built 6.18.60 tree.  Nothing in this lane contacted the device; the
bench gate is the attended sequence in REPORT.md sec 8.
