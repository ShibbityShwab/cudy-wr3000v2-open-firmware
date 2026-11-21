/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd.
 * hi5622 alg_cfg codec - interface (PORT: original text, GPL-2.0).
 * SPDX-License-Identifier: GPL-2.0
 */
#ifndef WAL_CONFIG_ALG_CFG_H
#define WAL_CONFIG_ALG_CFG_H

#include "oal_compat.h"
#include "alg_cfg.h"

#define OAL_ERR_CODE_SUB_ALG_ARGS_INVALID 0x8b2dU /* parser rejected the args */

extern wlan_alg_cfg_sub_alg_args_analysis_tbl_stru g_wlan_alg_cfg_sub_alg_args_analysis_table[];

/* non-twinned parsers kept so the dispatch table links (twin subset + extras) */
uint32_t alg_cfg_args_analysis_txmode_mode_sw(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len);
uint32_t alg_cfg_args_analysis_txmode_user_fix_chain(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len);
uint32_t alg_cfg_args_analysis_txmode_debug_log_switch(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len);
uint32_t alg_cfg_args_analysis_txmode_chain_prob_sw(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len);
uint32_t alg_cfg_args_analysis_waterfilling_bw_mode(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len);
uint32_t alg_cfg_args_analysis_waterfilling_sub_asnr(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len);
uint32_t alg_cfg_args_analysis_waterfilling_asnr(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len);
uint32_t alg_cfg_args_analysis_waterfilling_debug(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len);
uint32_t alg_cfg_args_analysis_wf_force_flag(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len);
uint32_t alg_cfg_args_analysis_hdtx_force_flag(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len);

wlan_alg_cfg_sub_alg_args_analysis_tbl_stru *wal_get_wlan_alg_cfg_analysis_entry(
    wlan_alg_cfg_sub_alg_args_analysis_tbl_stru *process_info_tbl, uint32_t tbl_size,
    mac_alg_cfg_enum_uint16 cfg_id);

extern int32_t wal_wlan_cfg_alg_process_entry(char *args[WLAN_CFG_MAX_ARGS_NUM], uint8_t args_num,
    uint8_t *msg_type, uint8_t *mem_block, uint16_t *mem_len,
    wlan_cfg_process_info_stru *process_info);

#endif /* WAL_CONFIG_ALG_CFG_H */
