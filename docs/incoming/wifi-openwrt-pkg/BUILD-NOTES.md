# BUILD-NOTES - hi5622-pciet-hcc OpenWrt package

**Lane:** wifi-openwrt-pkg (task-95, host-only). **Date:** 2026-11-24.
**Tree:** /home/shibbityshwab/openwrt-lab-selectorfix-img
**Kernel:** linux-6.18.60 (arm, cortex-a9 musl-eabi, profile DEVICE_cudy_wr3000-v2)
**Toolchain:** OpenWrt GCC 14.4.0
(staging_dir/toolchain-arm_cortex_a9_gcc-14.4.0_musl_eabi)
**Driver source:** local snapshot package/hi5622-pciet-hcc/src (no download,
no vendor blob) - the assembled wifi-d2h-direct build of hi5622_pciet_hcc.ko.

## Result

| item | value |
| --- | --- |
| build command | ./tools/build-package.sh (-> make package/hi5622-pciet-hcc/compile V=s) |
| make rc | 0 |
| compiler warnings/errors | none |
| full log | build-log.txt (31,617 B, sha256 8030efd57597e9722b9f2ce1896bdfc24becb35ba7e8d47fe6065933e243426a) |
| module | hi5622_pciet_hcc.ko, unstripped 262,920 B |
| module sha256 | dba4f8c28356b5a65b46da4ed6c9766f627672c286a20e3899f10c27dbf0f75e |
| vermagic | 6.18.60 SMP mod_unload ARMv7 p2v8 |
| package module (stripped) | 80,328 B installed as /lib/modules/6.18.60/hi5622_pciet_hcc.ko |
| seed helper | hi5622-seed 62,536 B static ARM ELF |
| seed sha256 | 56df0591e322afd2108a4690925e5515aa589a0c56fb18e69e6f217805488c0f |
| apk | kmod-hi5622-pciet-hcc-6.18.60-r1.apk 73,795 B |
| apk sha256 | a7e2e58401641045926cf11e7a5101b062746cf505d765e939ddea5e5d4ce2db |
| apk kernel dependency | kernel=6.18.60~5de0e425f1313eeec82615317fdc1f76-r1 |
| .config after build | byte-identical to pre-build (diff in build-summary.txt) |
| reproducibility | 3 consecutive builds: identical module + apk sha256 |

build-summary.txt in this directory is the recorded artifacts/metadata dump
(sha256 55ae4672944bdedfd98da5fbedd62f496c46ee1911e4a6cee54c35c92db84143).

## Build line (exactly what the script runs)

    OWRT=/home/shibbityshwab/openwrt-lab-selectorfix-img
    WORK=/home/shibbityshwab/wifi-openwrt-pkg-work

    ./tools/install-into-tree.sh --owrt $OWRT --force
    cp $OWRT/.config $WORK/.config.pre-build
    echo 'CONFIG_PACKAGE_kmod-hi5622-pciet-hcc=m' >> $OWRT/.config
    cd $OWRT
    make package/hi5622-pciet-hcc/clean
    make package/hi5622-pciet-hcc/compile V=s 2>&1 | tee $WORK/build.log
    cp $WORK/.config.pre-build $OWRT/.config

The package is opt-in, so the script selects it temporarily and restores the
caller's .config on exit (verified byte-identical).  Building with the symbol
passed on the make command line also works but makes OpenWrt print a
configuration-out-of-sync warning; the .config method gives a clean log.

## Why the seed helper is static

The first recorded build linked hi5622-seed dynamically; OpenWrt's package
dependency audit then failed the apk with:

    Package kmod-hi5622-pciet-hcc is missing dependencies for the following
    libraries: libc.so libgcc_s.so.1

A kmod package must not carry userspace library dependencies.  The helper is
now built with -static -s (no DT_NEEDED; rstrip's patchelf reports "statically
linked", which is expected).  The kernel module build was unaffected.

## Lab-tree state after the recorded build

* package source installed at $OWRT/package/hi5622-pciet-hcc (untracked in
  the lab tree; not selected in .config);
* build_dir outputs:
  build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/hi5622-pciet-hcc/
  (module, seed helper, ipkg tree);
* apk:
  $OWRT/bin/targets/hisilicon/luofu/packages/kmod-hi5622-pciet-hcc-6.18.60-r1.apk;
* the staging-root copies the build installed into
  staging_dir/target-arm_cortex-a9_musl_eabi/root-hisilicon/ were REMOVED
  after recording (module, init/config/status/seed files and the
  .kmod-hi5622-pciet-hcc_installed stamp), so an unselected incremental image
  build cannot accidentally include stale driver files.  Re-running
  tools/build-package.sh re-installs them; select the package in .config
  before building a bench image that should contain it;
* .config unchanged; no target/linux, DTS or kernel patch file was touched.

## Verify the committed evidence yourself

    ./tools/selftest.sh               # 22/22 checks, no OpenWrt tree needed
    sha256sum build-log.txt build-summary.txt
    (cd package/hi5622-pciet-hcc/src && sha256sum -c SHA256SUMS)
    ./tools/build-package.sh          # rebuild from scratch (host-only)

Self-test expected tail:

    wifi-openwrt-pkg selftest: 22 checks PASS

## What the build does NOT prove

* The module was not loaded on the bench box by this lane (host-only).
* No image was built in this lane; the package integration into a bench image
  is a Lead step (select the package, rebuild the image).
* The firmware post-ready IRQ/H2D gate remains open (see the project ledger
  2026-11-24 entries); packaging does not change driver behaviour.
