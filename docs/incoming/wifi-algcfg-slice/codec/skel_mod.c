/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd.
 * hi5622 alg_cfg codec - 6.18 module skeleton (original text, GPL-2.0).
 * Links the codec core; exports the vendor's g_alg_* data surface shape
 * (function-pointer slots + per-table sizes) and logs the recovered census.
 * SPDX-License-Identifier: GPL-2.0
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include "alg_cfg.h"
#include "wal_config_alg_cfg.h"

/* ---- the vendor's exported g_alg_* surface (8 names, from __ksymtab) ---- */
alg_cfg_process_info_stru *(*g_alg_cfg_process_find_by_cfg_id)(mac_alg_cfg_enum_uint16, alg_cfg_type_enum_uint8) =
    alg_cfg_search_process_info_by_cfg_id;
alg_cfg_process_info_stru *(*g_alg_cfg_process_find_by_name)(const char *) =
    alg_cfg_search_process_info_by_cfg_name;
void *g_alg_cfg_search_alg_debug_lut;
void *g_alg_cfg_search_dbg_cfg_name_by_cfg_id;
void *g_wal_alg_cfg_analysic_find;
void *g_wal_hipriv_alg_cfg_get_cfg_id;

uint8_t g_alg_cfg_bool_tbl_size = 2;
uint8_t g_alg_cfg_tx_mode_tbl_size = ALG_CFG_TX_MODE_BUTT;
uint8_t g_alg_cfg_pkt_type_tbl_size = 13;

EXPORT_SYMBOL(g_alg_cfg_process_find_by_cfg_id);
EXPORT_SYMBOL(g_alg_cfg_process_find_by_name);
EXPORT_SYMBOL(g_alg_cfg_search_alg_debug_lut);
EXPORT_SYMBOL(g_alg_cfg_search_dbg_cfg_name_by_cfg_id);
EXPORT_SYMBOL(g_wal_alg_cfg_analysic_find);
EXPORT_SYMBOL(g_wal_hipriv_alg_cfg_get_cfg_id);
EXPORT_SYMBOL(g_alg_cfg_bool_tbl_size);
EXPORT_SYMBOL(g_alg_cfg_tx_mode_tbl_size);

static int __init alg_cfg_codec_init(void)
{
    uint16_t twin = 0;
    uint16_t total = alg_cfg_process_tbl_size();
    uint16_t i;
    for (i = 0; i < total; i++) {
        if (g_alg_cfg_process_info_table[i].en_alg_cfg_id <
            (mac_alg_cfg_enum_uint16)MAC_ALG_CFG_WATERFILLING_END) {
            twin++;
        }
    }
    pr_info("hi5622_alg_cfg_codec: process table %u entries (%u twin subset + %u hi5622 census)\n",
            total, twin, total - twin);
    pr_info("hi5622_alg_cfg_codec: pkt_type=%u bool=%u tx_mode=%u param_size_tbl=%u\n",
            alg_cfg_param_get_tbl_size(ALG_CFG_PARAM_PKT_TYPE),
            alg_cfg_param_get_tbl_size(ALG_CFG_PARAM_BOOL),
            alg_cfg_param_get_tbl_size(ALG_CFG_PARAM_TX_MODE),
            (uint8_t)oal_array_size(g_alg_cfg_param_size_tbl));
    return 0;
}

static void __exit alg_cfg_codec_exit(void)
{
}

module_init(alg_cfg_codec_init);
module_exit(alg_cfg_codec_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("hi5622 alg_cfg command codec core (clean-room port, 6.18)");
