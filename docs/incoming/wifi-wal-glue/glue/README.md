# hi5622_wal_cfg - wal config-command front end (6.18)

Host-side glue module wiring the alg_cfg command codec
(`../wifi-algcfg-slice/codec/`) into a 6.18 wal config-command front end.

## Contents

| file | role |
| --- | --- |
| wal_cfg_glue.h | shared types: wlan_cfg_param_stru, module table, hcc_msg header, LUT layout, fixup pairs |
| wal_cfg_frontend.c | module init/exit, sysfs surface, cfg80211 vendor-cmd helper, export surface |
| wal_cfg_module.c | wal_wlan_cfg_module_process_entry (+host/device legs), hipriv-style wal_hipriv_alg_cfg, wal msg plumbing stubs |
| wal_cfg_lut.c | 176-B LUT builder, name-fixup loader, runtime layout descriptor |
| hmac_cfg_stubs.c | hmac cfg dispatch stub surface + hcc registration stubs |

The codec objects (wal_config_alg_cfg.c, kernel_util.c from
wifi-algcfg-slice/codec/) are linked into the same module - do not load
this module together with hi5622_alg_cfg_codec.ko (duplicate symbols).

## Build

    export KDIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
    export TC=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin
    make ARCH=arm CROSS_COMPILE=$(TC)/arm-openwrt-linux-muslgnueabi- -C $(KDIR) M=$(pwd) modules

## Runtime surface (host-only; never loaded on the bench device)

- `/sys/kernel/wal_cfg/cmd` - write a command line:
  - `alg_cfg txmode_user_fix_mode 3 0x1f` (hipriv form)
  - `alg_cfg get_txmode_mode_sw` (GET)
  - `257 txmode_mode_sw 0` (module entry by wlan cfg id)
  - `cali ...` (cali entry; not ported)
  - `help`
- `/sys/kernel/wal_cfg/{help,process_table,lut,layout,param_size_tbl,fixup_list}` - read-only dumps
- `/sys/kernel/wal_cfg/fixup_add` - write `ROW_IDX NAME` to fill one census
  placeholder name (the bench name-harvest mechanism; see ../probes/bench-probes.md)

## Notes

- The 17-twin subset runs through the oracle-verified codec; hi5622 census
  rows (ids 401..4401) return 1017 (SUB_ALG_ANALYSIS_ENTRY_NOT_FOUND) until
  their parser families are ported (120 annotated entries, see ../REPORT.md).
- cfg80211 vendor-command surface: `wal_cfg_register_vendor_commands(wiphy)`
  is exported for the future ported driver; this module registers nothing
  itself (no fake wiphy).
- License: GPL-2.0 original text; framework lineage headers retained. The
  vendored hi3881 GPL-2.0-or-later base sits in
  ../wifi-algcfg-slice/vendor/hi3881-wal/.
