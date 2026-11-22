# BUILD-NOTES - hi5622_wal_cfg

Build log evidence (host-side; module is not loaded anywhere).

## Command

    export KDIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
    export STAGING_DIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir
    export TC=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin
    make ARCH=arm CROSS_COMPILE=$TC/arm-openwrt-linux-muslgnueabi- -C $KDIR M=$(pwd) clean
    make ARCH=arm CROSS_COMPILE=$TC/arm-openwrt-linux-muslgnueabi- -C $KDIR M=$(pwd) modules

## Result (2026-11-22)

- hi5622_wal_cfg.ko: 222,528 B
- sha256: f3b57a215a092f51e98d7a26268efc763e63d003a7e606690fcc5ca396d02323
- 0 errors, 0 warnings (kernel CONFIG_WERROR + -Werror effective; grep -ciE
  'error|warning' on the full build log -> 0)
- .modinfo: license=GPL; description present; depends=cfg80211;
  vermagic=6.18.60 SMP mod_unload ARMv7 p2v8
- objects: wal_cfg_frontend.o wal_cfg_module.o wal_cfg_lut.o
  hmac_cfg_stubs.o + codec-linked wal_config_alg_cfg.o kernel_util.o

## Export check

__ksymtab_strings carries the 14 vendor-exact alg names + the glue extras
(see ../mapping/dispatch-surface.csv): g_alg_cfg_process_find_by_cfg_id,
g_alg_cfg_process_find_by_name, g_alg_cfg_search_alg_debug_lut,
g_alg_cfg_search_dbg_cfg_name_by_cfg_id, g_alg_cfg_bool_tbl_size,
g_alg_cfg_snd_fback_type_tbl_size, g_alg_cfg_snd_user_mat_type_tbl_size,
g_alg_cfg_su_aggr_mode_tbl_size, alg_cfg_args_analysis_param,
alg_cfg_args_param_analyse_equipment_param, alg_cfg_get_match_string,
alg_cfg_get_match_value, alg_cfg_get_tail_num, alg_cfg_param_get_tbl_size,
+ g_alg_cfg_tx_mode_tbl_size, g_wal_hipriv_alg_cfg_get_cfg_id,
wal_wlan_cfg_module_process_entry, wal_wlan_cfg_module_host_process_entry,
wal_wlan_cfg_module_device_process_entry, wal_hipriv_alg_cfg,
wal_hipriv_alg_cfg_get_cfg_id, wal_cfg_register_vendor_commands,
hmac_config_sync_cmd_common (22 total).

## Tree quirks this build works around (documented for reviewers)

1. CONFIG_CFG80211 is unset on the luofu tree (OpenWrt ships cfg80211 as the
   kmod-cfg80211 package) - the two provider symbols are declared in
   cfg80211-provider.symvers (KBUILD_EXTRA_SYMBOLS); the .ko depends on
   cfg80211 exactly like the vendor hi5622v100_wifi.ko imports its 25
   cfg80211 symbols.
2. CONFIG_MODULE_STRIPPED=y - MODULE_DESCRIPTION() is compiled to nothing;
   the front end uses MODULE_INFO(description, ...) so modpost stays quiet.
3. The codec sources live in ../wifi-algcfg-slice/codec/ - explicit
   cc_o_c rules in the Kbuild compile them into this module.
