# BUILD-NOTES - hdpp/alg clean-room scaffolding on Linux 6.18.60

**Result:** hi5622_hdpp_alg_skel.ko (73,404 B, ELF32 ARM, license=GPL,
vermagic=6.18.60 SMP mod_unload ARMv7 p2v8), 2 objects, built with the OpenWrt
external toolchain (GCC 14.4.0) against the built kernel tree
openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60.
Compiler output is CLEAN: zero cc warnings under the tree's flags.  The only
modpost note is "missing MODULE_DESCRIPTION() in hi5622_hdpp_alg_skel.o" -
a tree artifact: this luofu kernel is built with CONFIG_MODULE_STRIPPED=y,
which makes MODULE_INFO_STRIP drop the description tag by design; the tag IS
present in both source files.

**Build line** (see Kbuild in this dir; the .ko evidence copy lives at
/home/shibbityshwab/wifi-datapath-slice-work/hi5622_hdpp_alg_skel.ko,
sha256 6cb2e7249e5c8493331d7f2fbccff02b82b632323843a5c703e805248690175e;
it is NOT committed):

    export KDIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
    export STAGING_DIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir
    make ARCH=arm CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi- \
         -C $KDIR M=$(pwd) modules

**Export contract check (scripted):** the vendor hi5622v100_wifi.ko exports
46 hdpp_* + 6 alg_* function names + 8 g_alg_* data names through its
__ksymtab.  This module re-exports ALL 60 names (60/60 coverage, verified by
dumping both __ksymtab_strings sections and diffing) plus 2 extra
(hdpp_main_init/hdpp_main_exit, exported so the port can bind the lifecycle
from another translation unit).  Consumer evidence: wifi_debug.ko imports 62
hdpp_/alg_ names and links against exactly this export set.

**Source base:** none vendored.  All C/header files in this dir are ORIGINAL
text written by the wifi-datapath-slice lane from decoded interface
contracts: the vendor binary's symbol table, __ksymtab strings, bl-call
graph and call-site arities (see ../REPORT.md methods), plus the hi1105
dpe_/alg_ tree used as a NAME/STRUCTURE reference only (the hongmengkernel
mirror carries no license grant; none of its text is reproduced here).  No
vendor source, object code, firmware blob or calibration data is committed.

## Signature policy (contract-first, provisional where unmeasured)

1. int32_t returns everywhere (vendor s32 ABI; call-site r0-consumption
   agrees where measurable).
2. Parameter counts come from measured call sites
   (../mapping/hdpp-inventory.csv arity_* columns).  Where no static call
   site exists (function-pointer registration only, e.g. the 40 of 46
   exported hdpp_stat_* handlers), the declaration uses one uintptr_t
   argument (two where a switch/set pair was measurable) and is marked
   PROVISIONAL in hi5622_hdpp.h.
3. Object-model getters return opaque pointers (every measured call site
   NULL-checks r0).
4. The 17 alg names that match the hi1105 reference exactly keep the
   reference function SHAPE (parameter count and pointer/scalar kinds), in
   our own typedef spelling.

## What the stubs do (and do not)

- Every stub returns -EOPNOTSUPP (semantics-bearing paths) or 0 (benign dfx
  control points); none touch hardware, so the module is safe to insmod on
  the bench as a link-check with zero side effects.
- g_alg_cfg_* data exports are zero-initialised placeholders (function
  pointers NULL, sizes 0) - documented TODO in alg_skel.c.
- hdpp_stat_* dfx stubs are no-ops returning 0 so a linked vendor-style
  debug consumer keeps running while the real dfx slice is pending.

## License

All files in this dir are original GPL-2.0 text by this lane (matching the
license of the kernel they link against).  Kbuild is build metadata.
