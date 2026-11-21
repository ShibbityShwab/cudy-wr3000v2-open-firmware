# alg_cfg command codec - port for 6.18

Original GPL-2.0 text implementing the table-driven algorithm-config text
command codec of the Hi5622 wifi stack. Every struct offset, enum value,
error code and bound was verified against the vendor hi5622v100_wifi.ko
(see ../REPORT.md and ../mapping/twin-divergence.csv).

Build (host-only research; the module is not loaded on the bench in this
slice):

    export KDIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
    export STAGING_DIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir
    make ARCH=arm CROSS_COMPILE=$(STAGING_DIR)/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi- -C $(KDIR) M=$(pwd) modules

Result: hi5622_alg_cfg_codec.ko (97,108 B, license=GPL,
vermagic 6.18.60 SMP mod_unload ARMv7 p2v8).

Files:
- alg_cfg.h / wal_config_alg_cfg.h - codec types and interface
- wal_config_alg_cfg.c - tables + the 17 twin functions + dispatch + entry
- alg_cfg_hi5622_census.h - 414 census rows (ids/types/flags from the binary)
- kernel_util.c - securec/oal shims (self-contained)
- skel_mod.c - module init + the vendor's 8-name g_alg_* export surface shape
- Kbuild
