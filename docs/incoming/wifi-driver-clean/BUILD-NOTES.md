# BUILD-NOTES - hi5622_wifi.ko clean-room transport + HCC service driver

**Lane:** wifi-driver-clean (task-94), host-only.  **Date:** 2026-11-24.
**Source:** this directory's `src/` (the only inputs are the committed
deliverables of wifi-transport-port, wifi-hcc-service, wifi-d2h-direct and
wifi-hcc-core; no patch is applied at build time).  The built `.ko` is
evidence only and is NOT committed (`src/.gitignore` covers build products).

## Result

| item | value |
| --- | --- |
| module | hi5622_wifi.ko |
| size | **305,276 B** |
| sha256 | **ad334f7fe1600b4c1acbcfa3cd4b11314573f73ea86fe112b01f068ded71639e** |
| ELF | 32-bit LSB relocatable, ARM, EABI5 (with debug_info, not stripped) |
| license | GPL |
| vermagic | 6.18.60 SMP mod_unload ARMv7 p2v8 |
| depends | (none - one self-contained module) |
| compilers/modpost warnings | **0** (the project kernel builds with -Werror; `CONFIG_MODULE_STRIPPED` is handled in main.c so description/author still reach modinfo) |
| PCI alias | pci:v000059E7d00000005sv*sd*bc*sc*i* |
| objects | main.o transport.o ete.o d2h.o hcc.o service.o hcc_core.o hcc_core_kernel.o |

Module parameters: fw_path (charp), wifi_block_path (charp), plat_block_path
(charp), allow_unseeded_release (uint), hcc_poll_ms (uint), hcc_cece_mode
(uint), rd_ab_write (uint).

## Build line

    export KDIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
    export STAGING_DIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir
    export CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
    make ARCH=arm CROSS_COMPILE=$CROSS_COMPILE -C $KDIR M=$(pwd)/src modules

Canonical driver: `./tools/build.sh` (clean build + size/sha256/vermagic/param
report + selftest; logs to /tmp/hi5622-wifi-build.log).  The OpenWrt GCC
wrapper requires STAGING_DIR in the environment (the script exports it).

## Verification

| step | result |
| --- | --- |
| clean module build (project kernel tree, -Werror) | PASS, 0 warnings / 0 errors |
| driver selftest `tools/selftest.py` | **133/133 PASS** (regions, F4 CA->BAR0 map incl out[3]/out[4], firmware-RAM addresses, custom-block layout, release-only gates, write allowlist, module/source/attr layout, hcc_core provenance hashes) |
| baked-in protocol core selftest (wifi-hcc-core lane) | **528/528 PASS** (`cd ../wifi-hcc-core && ./tools/build.sh --no-kernel`) |
| `tools/mkblocks.py --selftest` | 6/6 PASS |

## Build-time notes

* The kernel is built with `CONFIG_MODULE_STRIPPED`, so standard
  `MODULE_DESCRIPTION()/MODULE_AUTHOR()` expand to no-ops (linux/moduleparam.h);
  main.c emits `MODULE_INFO(description/author, ...)` under that config and
  keeps the standard macros otherwise, so modinfo/modpost see the metadata.
* The OpenWrt GCC wrapper warns if `STAGING_DIR` is not in the environment;
  `tools/build.sh` exports it (10 spurious "STAGING_DIR not defined" compiler
  warnings otherwise, which the script would correctly flag as a non-clean
  build).
* No image, no world build, no boot claims: this is an out-of-tree module
  against the already-built 6.18.60 tree.
