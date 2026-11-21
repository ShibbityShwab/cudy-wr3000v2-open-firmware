/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd.
 * hi5622 alg_cfg command codec - core implementation (PORT: original text).
 *
 * Implements the generic core of the table-driven algorithm-config
 * text-command codec: the 17 functions whose names exist verbatim in the
 * vendor hi5622v100_wifi.ko (the ORACLE), the process-info table, the
 * string->value tables and the SET/GET + need_d_process dispatch contract.
 *
 * Every return code, struct offset, string constant and length bound in
 * this file was verified against the binary (see wifi-algcfg-slice/REPORT.md
 * and mapping/twin-divergence.csv); the hi5622-specific command census is in
 * alg_cfg_hi5622_census.h.
 * SPDX-License-Identifier: GPL-2.0
 */
#include "oal_compat.h"
#include "alg_cfg.h"
#include "wal_config_alg_cfg.h"

/* ==================== global tables ==================== */

/* process-info table: command name -> cfg id/type + 4 need_* flags.
 * Entries 0..32 are the twinned subset (names are the command-interface
 * constants, shared with the reference); entries 33+ are the hi5622
 * census entries (ids/flags recovered from the binary; names are loader-
 * patched at runtime on the device - placeholders + recovered vocabulary). */
alg_cfg_process_info_stru g_alg_cfg_process_info_table[] = {
    /* -------- txmode -------- */
    {"txmode_mode_sw",            MAC_ALG_CFG_TXMODE_MODE_SW,        ALG_CFG_TYPE_SET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"get_txmode_mode_sw",        MAC_ALG_CFG_TXMODE_MODE_SW,        ALG_CFG_TYPE_GET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"txmode_user_fix_mode",      MAC_ALG_CFG_TXMODE_USER_FIX_MODE,  ALG_CFG_TYPE_SET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"get_txmode_user_fix_mode",  MAC_ALG_CFG_TXMODE_USER_FIX_MODE,  ALG_CFG_TYPE_GET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"txmode_user_fix_chain",     MAC_ALG_CFG_TXMODE_USER_FIX_CHAIN, ALG_CFG_TYPE_SET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"get_txmode_user_fix_chain", MAC_ALG_CFG_TXMODE_USER_FIX_CHAIN, ALG_CFG_TYPE_GET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"txmode_debug_log_switch",           MAC_ALG_CFG_TXMODE_DEBUG_LOG_SWITCH, ALG_CFG_TYPE_SET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"get_txmode_debug_log_switch",       MAC_ALG_CFG_TXMODE_DEBUG_LOG_SWITCH, ALG_CFG_TYPE_GET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"txmode_all_user_fix_mode",          MAC_ALG_CFG_TXMODE_ALL_USER_FIX_MODE, ALG_CFG_TYPE_SET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"get_txmode_all_user_fix_mode",      MAC_ALG_CFG_TXMODE_ALL_USER_FIX_MODE, ALG_CFG_TYPE_GET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"txmode_chain_prob_sw",      MAC_ALG_CFG_TXMODE_CHAIN_PROB_SW, ALG_CFG_TYPE_SET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    /* -------- waterfilling / hardamard -------- */
    {"tx_wf_mode",            MAC_ALG_CFG_WATERFILLING_MODE,     ALG_CFG_TYPE_SET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"get_tx_wf_mode",        MAC_ALG_CFG_WATERFILLING_MODE,     ALG_CFG_TYPE_GET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"wf_enable",             MAC_ALG_CFG_WATERFILLING_ENABLE,   ALG_CFG_TYPE_SET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"get_wf_enable",         MAC_ALG_CFG_WATERFILLING_ENABLE,   ALG_CFG_TYPE_GET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"wf_hadamard_mode",      MAC_ALG_CFG_HARDAMARDTX_MODE,      ALG_CFG_TYPE_SET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"get_wf_hadamard_mode",  MAC_ALG_CFG_HARDAMARDTX_MODE,      ALG_CFG_TYPE_GET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"hadamard_enable",       MAC_ALG_CFG_HARDAMARDTX_ENABLE,    ALG_CFG_TYPE_SET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"get_hadamard_enable",   MAC_ALG_CFG_HARDAMARDTX_ENABLE,    ALG_CFG_TYPE_GET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"wf_asnr",               MAC_ALG_CFG_WATERFILLING_CI_ASNR,  ALG_CFG_TYPE_SET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"get_wf_asnr",           MAC_ALG_CFG_WATERFILLING_CI_ASNR,  ALG_CFG_TYPE_GET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"set_wf_bw_mode",        MAC_ALG_CFG_WATERFILLING_BW_MODE,  ALG_CFG_TYPE_SET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"get_wf_bw_mode",        MAC_ALG_CFG_WATERFILLING_BW_MODE,  ALG_CFG_TYPE_GET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"set_wf_sub_asnr",       MAC_ALG_CFG_WATERFILLING_SUB_ASNR, ALG_CFG_TYPE_SET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"get_wf_sub_asnr",       MAC_ALG_CFG_WATERFILLING_SUB_ASNR, ALG_CFG_TYPE_GET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"wf_limit",              MAC_ALG_CFG_WATERFILLING_LIMIT_ASNR, ALG_CFG_TYPE_SET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"get_wf_limit",          MAC_ALG_CFG_WATERFILLING_LIMIT_ASNR, ALG_CFG_TYPE_GET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"wf_debug",              MAC_ALG_CFG_WATERFILLING_DEBUG,    ALG_CFG_TYPE_SET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"get_wf_debug",          MAC_ALG_CFG_WATERFILLING_DEBUG,    ALG_CFG_TYPE_GET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"wf_force_flag",         MAC_ALG_CFG_WATERFILLING_FORCE_FLAG, ALG_CFG_TYPE_SET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"get_wf_force_flag",     MAC_ALG_CFG_WATERFILLING_FORCE_FLAG, ALG_CFG_TYPE_GET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"hadamard_force_flag",   MAC_ALG_CFG_HARDAMARDTX_FORCE_FLAG, ALG_CFG_TYPE_SET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    {"get_hadamard_force_flag", MAC_ALG_CFG_HARDAMARDTX_FORCE_FLAG, ALG_CFG_TYPE_GET, {0}, {OAL_FALSE, OAL_FALSE, OAL_TRUE, OAL_TRUE}},
    /* -------- hi5622 census (see alg_cfg_hi5622_census.h) -------- */
#include "alg_cfg_hi5622_census.h"
};

/* pkt-type string table: 13 entries (verified: same count/order as binary) */
alg_cfg_param_tbl_stru g_alg_cfg_pkt_type_info_table[] = {
    {"ucast_data",           ALG_CFG_PKT_TYPE_UCAST_DATA},
    {"mcast_data",           ALG_CFG_PKT_TYPE_MCAST_DATA},
    {"bcast_data",           ALG_CFG_PKT_TYPE_BCAST_DATA},
    {"mumimo_data",          ALG_CFG_PKT_TYPE_MUMIMO_DATA},
    {"ofdma_data",           ALG_CFG_PKT_TYPE_OFDMA_DATA},
    {"ucast_mgmt",           ALG_CFG_PKT_TYPE_UCAST_MGMT},
    {"mcast_mgmt",           ALG_CFG_PKT_TYPE_MCAST_MGMT},
    {"bcast_mgmt",           ALG_CFG_PKT_TYPE_BCAST_MGMT},
    {"prot_ctrl",            ALG_CFG_PKT_TYPE_PROT_CTRL},
    {"ndp",                  ALG_CFG_PKT_TYPE_NDP},
    {"ndpa",                 ALG_CFG_PKT_TYPE_NDPA},
    {"resp_ctrl",            ALG_CFG_PKT_TYPE_RESP_CTRL},
    {"other_ctrl",           ALG_CFG_PKT_TYPE_OTHER_CTRL},
};

alg_cfg_param_tbl_stru g_alg_cfg_bool_tbl[] = {
    {"enable",  OAL_TRUE},
    {"disable", OAL_FALSE},
};

/* tx-mode value table: 13 entries (verified: same count/order as binary;
 * names in the binary's .rodata at the 0x23a680 cluster) */
alg_cfg_param_tbl_stru g_alg_cfg_tx_mode_tbl[ALG_CFG_TX_MODE_BUTT] = {
    {"auto",           ALG_CFG_TX_MODE_AUTO},
    {"chain0_fix",     ALG_CFG_TX_MODE_CHAIN0_FIX},
    {"chain1_fix",     ALG_CFG_TX_MODE_CHAIN1_FIX},
    {"chain2_fix",     ALG_CFG_TX_MODE_CHAIN2_FIX},
    {"chain3_fix",     ALG_CFG_TX_MODE_CHAIN3_FIX},
    {"csd_fix",        ALG_CFG_TX_MODE_CSD2_FIX},
    {"csd3_fix",       ALG_CFG_TX_MODE_CSD3_FIX},
    {"csd4_fix",       ALG_CFG_TX_MODE_CSD4_FIX},
    {"stbc_fix",       ALG_CFG_TX_MODE_STBC2_FIX},
    {"stbc3_fix",      ALG_CFG_TX_MODE_STBC3_FIX},
    {"stbc4_fix",      ALG_CFG_TX_MODE_STBC4_FIX},
    {"txbf_fix",       ALG_CFG_TX_MODE_TXBF_FIX},
    {"stbc_2nss_fix",  ALG_CFG_TX_MODE_STBC_2NSS_FIX},
};

/* param-size table: the binary carries 40 {type,size} entries over a
 * 47-value type space (guard: type > 46 -> 0); the twin subset needs the
 * first three entries exactly as the reference. */
alg_cfg_param_tbl_size_info_stru g_alg_cfg_param_size_tbl[] = {
    {ALG_CFG_PARAM_PKT_TYPE, oal_array_size(g_alg_cfg_pkt_type_info_table), {0, 0}},
    {ALG_CFG_PARAM_BOOL,     oal_array_size(g_alg_cfg_bool_tbl), {0, 0}},
    {ALG_CFG_PARAM_TX_MODE,  oal_array_size(g_alg_cfg_tx_mode_tbl), {0, 0}},
    /* hi5622 entries (types 3..29; sizes recovered from the binary):
     *  3 FEC_CODING 4 | 4 LEGACY_RATE 12 | 5 PROTOCOL_MODE 8 | 6 BOOL 2
     *  7 CHAIN 2 | 8 PROTECT_MODE 3 | 9 HE_LTF 3 | 10 PREAMBLE 2
     *  11 RTS_BW_MODE 8 | 12 FREQ_BW_MODE 8 | 13 BANDWIDTH_MODE 4
     *  14 TX_MODE 13 | 15 SND_MODE 4 | 16 SND_FBACK_TYPE 2
     *  17 SND_SCH_MODE 2 | 18 SND_SW_TYPE 2 | 19 SND_USER_MAT_TYPE 4
     *  20 FIX_POW_MODE 3 | 21 TPC_POW_MODE 3 | 22 GI_TYPE_NONAX 2
     *  23 GI_TYPE_AX 3 | 24 AR_DEV_PARAM 26 | 25 ULSCH_DEV_SCH_MODE 4
     *  26 SCEN_USER_INFO_TYPE 5 | 27 AGGR_MODE 3 | 28 AGGR_TIME_OPT 4
     *  (mapping/alg-param-size-table.csv carries the raw dump) */
};

/* parser dispatch: cfg id -> analysis function (twin subset) */
wlan_alg_cfg_sub_alg_args_analysis_tbl_stru g_wlan_alg_cfg_sub_alg_args_analysis_table[] = {
    {MAC_ALG_CFG_TXMODE_MODE_SW,         {0, 0}, alg_cfg_args_analysis_txmode_mode_sw},
    {MAC_ALG_CFG_TXMODE_USER_FIX_MODE,   {0, 0}, alg_cfg_args_analysis_txmode_user_fix_mode},
    {MAC_ALG_CFG_TXMODE_USER_FIX_CHAIN,  {0, 0}, alg_cfg_args_analysis_txmode_user_fix_chain},
    {MAC_ALG_CFG_TXMODE_DEBUG_LOG_SWITCH,{0, 0}, alg_cfg_args_analysis_txmode_debug_log_switch},
    {MAC_ALG_CFG_TXMODE_ALL_USER_FIX_MODE, {0, 0}, alg_cfg_args_analysis_txmode_all_user_fix_mode},
    {MAC_ALG_CFG_TXMODE_CHAIN_PROB_SW,   {0, 0}, alg_cfg_args_analysis_txmode_chain_prob_sw},
    {MAC_ALG_CFG_WATERFILLING_MODE,      {0, 0}, alg_cfg_args_analysis_waterfilling_mode},
    {MAC_ALG_CFG_WATERFILLING_ENABLE,    {0, 0}, alg_cfg_args_analysis_waterfilling_enable},
    {MAC_ALG_CFG_HARDAMARDTX_MODE,       {0, 0}, alg_cfg_args_analysis_hardamard_mode},
    {MAC_ALG_CFG_HARDAMARDTX_ENABLE,     {0, 0}, alg_cfg_args_analysis_hardamard_enable},
    {MAC_ALG_CFG_WATERFILLING_LIMIT_ASNR,{0, 0}, alg_cfg_args_analysis_waterfilling_limit},
    {MAC_ALG_CFG_WATERFILLING_BW_MODE,   {0, 0}, alg_cfg_args_analysis_waterfilling_bw_mode},
    {MAC_ALG_CFG_WATERFILLING_SUB_ASNR,  {0, 0}, alg_cfg_args_analysis_waterfilling_sub_asnr},
    {MAC_ALG_CFG_WATERFILLING_CI_ASNR,   {0, 0}, alg_cfg_args_analysis_waterfilling_asnr},
    {MAC_ALG_CFG_WATERFILLING_DEBUG,     {0, 0}, alg_cfg_args_analysis_waterfilling_debug},
    {MAC_ALG_CFG_WATERFILLING_FORCE_FLAG,{0, 0}, alg_cfg_args_analysis_wf_force_flag},
    {MAC_ALG_CFG_HARDAMARDTX_FORCE_FLAG, {0, 0}, alg_cfg_args_analysis_hdtx_force_flag},
};

/* ==================== twin core ==================== */

uint16_t alg_cfg_process_tbl_size(void)
{
    return (uint16_t)oal_array_size(g_alg_cfg_process_info_table);
}

/* twin 1: table-size lookup by parameter-table type.
 * Binary form (0x149d14, 100 B): guard type > 46 -> 0, linear scan of the
 * 4-byte {type,size} entries, return entry.size on match, 0 otherwise. */
uint8_t alg_cfg_param_get_tbl_size(alg_cfg_param_tbl_size_enum_uint8 param_type)
{
    alg_cfg_param_tbl_size_info_stru *size_info = g_alg_cfg_param_size_tbl;
    uint8_t index;

    if (param_type > ALG_CFG_PARAM_HI5622_MAX) {
        return 0;
    }

    for (index = 0; index < (uint8_t)oal_array_size(g_alg_cfg_param_size_tbl); index++) {
        if (size_info[index].en_param_type == param_type) {
            return size_info[index].uc_tbl_size;
        }
    }

    return 0;
}

/* twin 2: pull the first whitespace-delimited argument out of a command
 * string; returns the offset past the terminating separator in *cmd_offset.
 * Binary form (0x14a73c, 260 B): skip leading spaces, copy until space/NUL,
 * cap at WLAN_CFG_MAX_LEN_EACH_ARG(50) and arg_size (overflow -> 101),
 * empty arg -> 1009 with offset 0, NULL input -> 100, success -> 0. */
uint32_t alg_cfg_get_cmd_one_arg(const char *cmd, char *arg, uint32_t arg_size,
    uint32_t *cmd_offset)
{
    const char *cmd_copy;
    uint32_t pos = 0;

    if ((cmd == NULL) || (arg == NULL) || (cmd_offset == NULL)) {
        return OAL_ERR_CODE_PTR_NULL;
    }

    cmd_copy = cmd;
    while (*cmd_copy == ' ') {
        ++cmd_copy;
    }

    while ((*cmd_copy != ' ') && (*cmd_copy != '\0')) {
        arg[pos] = *cmd_copy;
        ++pos;
        ++cmd_copy;

        if ((pos >= WLAN_CFG_MAX_LEN_EACH_ARG) || (pos >= arg_size)) {
            return OAL_ERR_CODE_ARRAY_OVERFLOW;
        }
    }

    arg[pos] = '\0';

    if (pos == 0) {
        *cmd_offset = 0;
        return OAL_ERR_CODE_CONFIG_ARGS_OVER;
    }

    *cmd_offset = (uint32_t)(cmd_copy - cmd);
    return OAL_SUCC;
}

/* twin 3: name lookup, plain scan (binary 0x14a840, 148 B: same shape). */
void alg_cfg_search_process_info_by_cfg_name_normal(const char *args, uint32_t arg_size,
    alg_cfg_process_info_stru **target_cfg_info)
{
    alg_cfg_process_info_stru *cfg_info = g_alg_cfg_process_info_table;
    uint16_t index;

    if (target_cfg_info == NULL) {
        return;
    }
    for (index = 0; index < alg_cfg_process_tbl_size(); index++) {
        if (!strncmp(args, cfg_info[index].pc_alg_cfg_name, arg_size)) {
            *target_cfg_info = &cfg_info[index];
            return;
        }
    }
    *target_cfg_info = NULL;
}

/* twin 4: name lookup via first-arg split (binary 0x14a8d4, 184 B). */
alg_cfg_process_info_stru *alg_cfg_search_process_info_by_cfg_name(const char *alg_cfg_name)
{
    char args[WLAN_CFG_MAX_LEN_EACH_ARG] = { 0 };
    uint32_t tmp_offset;
    alg_cfg_process_info_stru *target_cfg_info = NULL;

    if (alg_cfg_name == NULL) {
        return NULL;
    }
    if (alg_cfg_get_cmd_one_arg(alg_cfg_name, args, sizeof(args), &tmp_offset) != OAL_SUCC) {
        return NULL;
    }
    alg_cfg_search_process_info_by_cfg_name_normal(args, sizeof(args), &target_cfg_info);
    return target_cfg_info;
}

/* twin 5: (id, type) lookup, plain scan (binary 0x14a98c, 132 B). */
void alg_cfg_search_process_info_by_cfg_id_normal(mac_alg_cfg_enum_uint16 alg_cfg_id,
    alg_cfg_type_enum_uint8 cfg_type, alg_cfg_process_info_stru **target_cfg_info)
{
    alg_cfg_process_info_stru *cfg_info = g_alg_cfg_process_info_table;
    uint16_t index;

    if (target_cfg_info == NULL) {
        return;
    }
    for (index = 0; index < alg_cfg_process_tbl_size(); index++) {
        if ((alg_cfg_id == cfg_info[index].en_alg_cfg_id) &&
            (cfg_type == cfg_info[index].en_cfg_type)) {
            *target_cfg_info = &cfg_info[index];
            return;
        }
    }
    *target_cfg_info = NULL;
}

/* twin 6: (id, type) lookup wrapper (binary 0x14aa10, 188 B). */
alg_cfg_process_info_stru *alg_cfg_search_process_info_by_cfg_id(mac_alg_cfg_enum_uint16 alg_cfg_id,
    alg_cfg_type_enum_uint8 cfg_type)
{
    alg_cfg_process_info_stru *target_cfg_info = NULL;
    alg_cfg_search_process_info_by_cfg_id_normal(alg_cfg_id, cfg_type, &target_cfg_info);
    return target_cfg_info;
}

/* twin 7: pkt-type string lookup.
 * Binary form (0x14aacc, 228 B): first-arg split, then scan of the
 * pkt-type table; the binary resolves the table size by scanning the
 * param-size table for type 0 (identical result); returns the u8 value
 * (ldrb at entry+4) or 13 (IRRELEVANT) on miss/NULL. */
alg_cfg_pkt_type_enum_uint8 alg_cfg_get_pkt_type(const char *cmd_args)
{
    alg_cfg_param_tbl_stru *pkt_type_info = g_alg_cfg_pkt_type_info_table;
    uint8_t index;
    char args[WLAN_CFG_MAX_LEN_EACH_ARG] = { 0 };
    uint32_t tmp_offset;

    if (cmd_args == NULL) {
        return ALG_CFG_PKT_TYPE_IRRELEVANT;
    }
    if (alg_cfg_get_cmd_one_arg(cmd_args, args, sizeof(args), &tmp_offset) != OAL_SUCC) {
        return ALG_CFG_PKT_TYPE_IRRELEVANT;
    }

    for (index = 0; index < alg_cfg_param_get_tbl_size(ALG_CFG_PARAM_PKT_TYPE); index++) {
        if (!strncmp(args, pkt_type_info[index].pc_name, sizeof(args))) {
            return (alg_cfg_pkt_type_enum_uint8)pkt_type_info[index].value;
        }
    }
    return ALG_CFG_PKT_TYPE_IRRELEVANT;
}

/* twin 8: parse the command head: optional [pkt_type] prefix, then the
 * command name; fills the found process entry, the args offset past the
 * name and the pkt type. Binary form (0x14abb0, 136 B): same; not-found
 * returns 1014, NULL returns 100. */
uint32_t alg_cfg_analysis_args_head(char *real_args[WLAN_CFG_MAX_ARGS_NUM],
    alg_cfg_process_info_stru **real_alg_cfg_info, uint8_t *real_args_offset,
    alg_cfg_pkt_type_enum_uint8 *real_pkt_type)
{
    alg_cfg_pkt_type_enum_uint8 pkt_type;
    uint8_t offset = 0;
    alg_cfg_process_info_stru *alg_cfg_info = NULL;

    if ((real_alg_cfg_info == NULL) || (real_args_offset == NULL) ||
        (real_pkt_type == NULL) || (real_args == NULL)) {
        return OAL_ERR_CODE_PTR_NULL;
    }

    pkt_type = alg_cfg_get_pkt_type(real_args[0]);
    if (pkt_type != ALG_CFG_PKT_TYPE_IRRELEVANT) {
        offset++;
    }

    alg_cfg_info = alg_cfg_search_process_info_by_cfg_name(real_args[offset]);
    if (alg_cfg_info == NULL) {
        return OAL_ERR_CODE_ALG_PROCESS_INFO_NOT_FOUND;
    }

    *real_alg_cfg_info = alg_cfg_info;
    *real_args_offset = ++offset;
    *real_pkt_type = pkt_type;

    return OAL_SUCC;
}

/* twin 9: full argument analysis: header + parameter-argument vector.
 * Binary form (0x14ac38, 236 B): same field order (id/type/pkt/dev_ret/
 * need_w4_dev_return), same args_num - offset arithmetic. */
uint32_t alg_cfg_analysis_args(char *args[WLAN_CFG_MAX_ARGS_NUM], uint8_t args_num,
    alg_cfg_param_stru *alg_cfg_param, alg_cfg_hdr_stru *alg_cfg_hdr,
    wlan_cfg_process_info_stru *process_info)
{
    uint32_t ret;
    alg_cfg_process_info_stru *alg_cfg_info = NULL;
    uint8_t real_args_offset = 0;
    alg_cfg_pkt_type_enum_uint8 pkt_type = ALG_CFG_PKT_TYPE_IRRELEVANT;
    uint8_t index;

    if (alg_cfg_hdr == NULL) {
        return OAL_FAIL;
    }
    ret = alg_cfg_analysis_args_head(args, &alg_cfg_info, &real_args_offset, &pkt_type);
    if (ret != OAL_SUCC) {
        return ret;
    }

    alg_cfg_hdr->en_cfg_id = alg_cfg_info->en_alg_cfg_id;
    alg_cfg_hdr->en_cfg_type = alg_cfg_info->en_cfg_type;
    alg_cfg_hdr->en_pkt_type = pkt_type;
    alg_cfg_hdr->dev_ret = OAL_SUCC;
    alg_cfg_hdr->en_need_w4_dev_return = alg_cfg_info->st_process_info.en_need_w4_device_return;

    alg_cfg_param->uc_param_num = args_num - real_args_offset;
    for (index = 0; index < alg_cfg_param->uc_param_num; index++) {
        alg_cfg_param->apuc_param_args[index] = (uint8_t *)args[index + real_args_offset];
    }

    *process_info = alg_cfg_info->st_process_info;
    return OAL_SUCC;
}

/* twin 10: generic string->value table match.
 * Binary form (0x149908, 136 B): strncmp(str, tbl[i].name, strlen(name)+1);
 * on match stores tbl[i].value and returns 0; miss/NULL -> 35629 (0x8b2d). */
uint32_t alg_cfg_get_match_value(const alg_cfg_param_tbl_stru *tbl, uint16_t tbl_size,
    const uint8_t *str, uint32_t *out_value)
{
    uint16_t index;

    if ((tbl == NULL) || (str == NULL) || (out_value == NULL) || (tbl_size == 0)) {
        return OAL_FAIL;
    }

    for (index = 0; index < tbl_size; index++) {
        if (!strncmp((const char *)str, tbl[index].pc_name,
                     OAL_STRLEN(tbl[index].pc_name) + 1)) {
            *out_value = tbl[index].value;
            return OAL_SUCC;
        }
    }
    return OAL_FAIL;
}

/* ==================== dispatch ==================== */

/* look up the parser entry for a cfg id (linear scan; the hi5622 binary
 * replaces this with the 176-B LUT - see alg_cfg_lut_entry_stru). */
wlan_alg_cfg_sub_alg_args_analysis_tbl_stru *wal_get_wlan_alg_cfg_analysis_entry(
    wlan_alg_cfg_sub_alg_args_analysis_tbl_stru *process_info_tbl, uint32_t tbl_size,
    mac_alg_cfg_enum_uint16 cfg_id)
{
    uint32_t index;

    for (index = 0; index < tbl_size; index++) {
        if (process_info_tbl[index].en_alg_cfg_id == cfg_id) {
            return &process_info_tbl[index];
        }
    }
    return NULL;
}

static wlan_alg_cfg_sub_alg_args_analysis_tbl_stru *wal_wlan_get_sub_alg_args_analysis_entry(
    mac_alg_cfg_enum_uint16 cfg_id)
{
    return wal_get_wlan_alg_cfg_analysis_entry(g_wlan_alg_cfg_sub_alg_args_analysis_table,
        oal_array_size(g_wlan_alg_cfg_sub_alg_args_analysis_table), cfg_id);
}

/* wal config-command entry: the SET/GET + need_d_process codec stage.
 * Contract verified against binary wal_wlan_cfg_alg_process_entry
 * (0x14ae44, 640 B): analysis_args -> dispatch lookup -> parser fill ->
 * msg_type = QUERY (GET) / WRITE (SET). */
int32_t wal_wlan_cfg_alg_process_entry(char *args[WLAN_CFG_MAX_ARGS_NUM], uint8_t args_num,
    uint8_t *msg_type, uint8_t *mem_block, uint16_t *mem_len,
    wlan_cfg_process_info_stru *process_info)
{
    uint32_t ret;
    alg_cfg_param_stru alg_cfg_param;
    alg_cfg_hdr_stru alg_cfg_hdr;
    wlan_alg_cfg_sub_alg_args_analysis_tbl_stru *sub_alg_args_analysis = NULL;

    if ((args == NULL) || (msg_type == NULL) || (mem_block == NULL) ||
        (mem_len == NULL) || (process_info == NULL)) {
        return OAL_ERR_CODE_PTR_NULL;
    }

    memset(&alg_cfg_param, 0, sizeof(alg_cfg_param));
    memset(&alg_cfg_hdr, 0, sizeof(alg_cfg_hdr));

    ret = alg_cfg_analysis_args(args, args_num, &alg_cfg_param, &alg_cfg_hdr, process_info);
    if (ret != OAL_SUCC) {
        return ret;
    }

    alg_cfg_param.puc_output_str = NULL; /* set by the wal/hipriv glue on the device */

    sub_alg_args_analysis = wal_wlan_get_sub_alg_args_analysis_entry(alg_cfg_hdr.en_cfg_id);
    if ((sub_alg_args_analysis == NULL) ||
        (sub_alg_args_analysis->p_sub_alg_args_analysis_func == NULL)) {
        return OAL_ERR_CODE_SUB_ALG_ANALYSIS_ENTRY_NOT_FOUND;
    }

    *(alg_cfg_hdr_stru *)mem_block = alg_cfg_hdr;

    ret = sub_alg_args_analysis->p_sub_alg_args_analysis_func(&alg_cfg_param, mem_block, mem_len);
    if (ret != OAL_SUCC) {
        return OAL_ERR_CODE_SUB_ALG_ARGS_INVALID;
    }

    *msg_type = (alg_cfg_hdr.en_cfg_type == ALG_CFG_TYPE_GET) ? 4 : 3; /* QUERY / WRITE */
    return ret;
}

/* ==================== twin parsers (txmode) ==================== */

static uint32_t alg_cfg_param_analyze_txmode_user_fix_mode_set(
    alg_cfg_param_stru *cfg_param, alg_cfg_param_txmode_user_stru *txomde_param)
{
    uint32_t result;
    uint32_t value;

    if (cfg_param->uc_param_num != 2) {
        if (cfg_param->puc_output_str != NULL) {
            (void)sprintf_s((char *)cfg_param->puc_output_str, WLAN_CFG_HOST_MAX_RSP_LEN,
                "[Error]config txmode_user_fix_mode, uc_param_num(%u) should be 2!\n",
                cfg_param->uc_param_num);
        }
        return OAL_FAIL;
    }
    result = alg_cfg_get_match_value(g_alg_cfg_tx_mode_tbl,
        (uint16_t)oal_array_size(g_alg_cfg_tx_mode_tbl),
        cfg_param->apuc_param_args[1], &value);
    if (result != OAL_SUCC) {
        if (cfg_param->puc_output_str != NULL) {
            (void)sprintf_s((char *)cfg_param->puc_output_str, WLAN_CFG_HOST_MAX_RSP_LEN,
                "[Error]config txmode_user_fix_mode, <value> = %s invalid, should be "
                "{auto, chain0_fix, chain1_fix, csd_fix, stbc_fix, txbf_fix}!\n",
                cfg_param->apuc_param_args[1]);
        }
        return result;
    }
    txomde_param->en_fix_txmode = (alg_cfg_param_tx_mode_enum)value;
    return OAL_SUCC;
}

static uint32_t alg_cfg_param_analyze_txmode_user_fix_mode(alg_cfg_param_stru *cfg_param,
    alg_cfg_param_txmode_user_stru *txomde_param)
{
    uint8_t mac_addr[WLAN_MAC_ADDR_LEN] = { 0 };

    if (cfg_param->uc_param_num >= 1) {
        /* v0: user mac address - exactly 17 chars (xx:xx:xx:xx:xx:xx) */
        if (OAL_STRLEN((const char *)cfg_param->apuc_param_args[0]) != 17) {
            if (cfg_param->puc_output_str != NULL) {
                (void)sprintf_s((char *)cfg_param->puc_output_str, WLAN_CFG_HOST_MAX_RSP_LEN,
                    "[Error]config txmode_user_fix_mode mac length(%u), should be 17!\n",
                    (unsigned int)OAL_STRLEN((const char *)cfg_param->apuc_param_args[0]));
            }
            return OAL_FAIL;
        }
        oal_strtoaddr((const char *)cfg_param->apuc_param_args[0],
            OAL_STRLEN((const char *)cfg_param->apuc_param_args[0]) + 1,
            mac_addr, WLAN_MAC_ADDR_LEN);
        if (memcpy_s(txomde_param->auc_mac_addr, WLAN_MAC_ADDR_LEN,
                     mac_addr, WLAN_MAC_ADDR_LEN) != EOK) {
            return OAL_FAIL;
        }
    }

    if (txomde_param->st_alg_cfg_hdr.en_cfg_type == ALG_CFG_TYPE_SET) {
        if (alg_cfg_param_analyze_txmode_user_fix_mode_set(cfg_param, txomde_param) != OAL_SUCC) {
            return OAL_FAIL;
        }
    } else {
        if (cfg_param->uc_param_num != 1) {
            if (cfg_param->puc_output_str != NULL) {
                (void)sprintf_s((char *)cfg_param->puc_output_str, WLAN_CFG_HOST_MAX_RSP_LEN,
                    "[Error]config get_txmode_user_fix_mode, uc_param_num(%u) should be 1!\n",
                    cfg_param->uc_param_num);
            }
            return OAL_FAIL;
        }
    }
    return OAL_SUCC;
}

/* twin 11: txmode per-user fixed mode (binary 0x155230, 940 B: same checks:
 * SET needs exactly 2 args, mac length 17, mode from the 13-entry table;
 * GET needs exactly 1 arg). */
uint32_t alg_cfg_args_analysis_txmode_user_fix_mode(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len)
{
    uint32_t ret;

    ret = alg_cfg_param_analyze_txmode_user_fix_mode(alg_cfg_param,
        (alg_cfg_param_txmode_user_stru *)mem_block);
    if (ret != OAL_SUCC) {
        return ret;
    }
    *len = sizeof(alg_cfg_param_txmode_user_stru);
    return OAL_SUCC;
}

static uint32_t alg_cfg_param_analyze_txmode_all_user_fix_mode(alg_cfg_param_stru *cfg_param,
    alg_cfg_param_txmode_all_user_stru *txomde_param)
{
    uint32_t ret;
    uint32_t value;

    if (txomde_param->st_alg_cfg_hdr.en_cfg_type == ALG_CFG_TYPE_SET) {
        if (cfg_param->uc_param_num != 1) {
            if (cfg_param->puc_output_str != NULL) {
                (void)sprintf_s((char *)cfg_param->puc_output_str, WLAN_CFG_HOST_MAX_RSP_LEN,
                    "[Error]txmode_all_user_fix_mode, uc_param_num(%u) should be 1!\n",
                    cfg_param->uc_param_num);
            }
            return OAL_FAIL;
        }
        ret = alg_cfg_get_match_value(g_alg_cfg_tx_mode_tbl,
            (uint16_t)oal_array_size(g_alg_cfg_tx_mode_tbl),
            cfg_param->apuc_param_args[0], &value);
        if (ret != OAL_SUCC) {
            if (cfg_param->puc_output_str != NULL) {
                (void)sprintf_s((char *)cfg_param->puc_output_str, WLAN_CFG_HOST_MAX_RSP_LEN,
                    "[Error]config txmode_all_user_fix_mode, <value> = %s invalid, should be "
                    "{auto, chain0_fix, chain1_fix, csd_fix, stbc_fix, txbf_fix}!\n",
                    cfg_param->apuc_param_args[0]);
            }
            return ret;
        }
        txomde_param->en_fix_txmode = (alg_cfg_param_tx_mode_enum)value;
    } else {
        if (cfg_param->uc_param_num != 1) {
            if (cfg_param->puc_output_str != NULL) {
                (void)sprintf_s((char *)cfg_param->puc_output_str, WLAN_CFG_HOST_MAX_RSP_LEN,
                    "[Error]config get_txmode_all_user_fix_mode, uc_param_num(%u) should be 1!\n",
                    cfg_param->uc_param_num);
            }
            return OAL_FAIL;
        }
    }
    return OAL_SUCC;
}

/* twin 12: txmode all-user fixed mode (binary 0x1555dc, 500 B). */
uint32_t alg_cfg_args_analysis_txmode_all_user_fix_mode(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len)
{
    uint32_t ret;

    ret = alg_cfg_param_analyze_txmode_all_user_fix_mode(alg_cfg_param,
        (alg_cfg_param_txmode_all_user_stru *)mem_block);
    if (ret != OAL_SUCC) {
        return ret;
    }
    *len = sizeof(alg_cfg_param_txmode_all_user_stru);
    return OAL_SUCC;
}

/* ==================== twin parsers (waterfilling / hardamard) ==================== */

static uint32_t alg_cfg_param_analyze_waterfilling_mode(alg_cfg_param_stru *cfg_param,
    alg_cfg_param_waterfilling_mode_stru *param)
{
    uint32_t ret;
    uint32_t value;

    if (param->alg_cfg_hdr.en_cfg_type == ALG_CFG_TYPE_SET) {
        if (cfg_param->uc_param_num != 1) {
            if (cfg_param->puc_output_str != NULL) {
                (void)sprintf_s((char *)cfg_param->puc_output_str, WLAN_CFG_HOST_MAX_RSP_LEN,
                    "[Error]config wf_mode, uc_param_num(%u) should be 1!\n",
                    cfg_param->uc_param_num);
            }
            return OAL_FAIL;
        }
        param->waterfilling_mode = (uint8_t)oal_atoi((const char *)cfg_param->apuc_param_args[0]);
        if (param->waterfilling_mode != ALG_CFG_TX_WATERFILLING_AUTO_MODE &&
            param->waterfilling_mode != ALG_CFG_TX_WATERFILLING_FIX_MODE) {
            if (cfg_param->puc_output_str != NULL) {
                (void)snprintf_s((char *)cfg_param->puc_output_str, WLAN_CFG_HOST_MAX_RSP_LEN,
                    WLAN_CFG_HOST_MAX_RSP_LEN - 1,
                    "[Error]analyse_wf_mode, uc_param_mode(%u) invalid, fix mode:0 ; auto mode:1 ;\n",
                    param->waterfilling_mode);
            }
            return OAL_FAIL;
        }
    } else { /* GET */
        if (cfg_param->uc_param_num != 0) {
            if (cfg_param->puc_output_str != NULL) {
                (void)snprintf_s((char *)cfg_param->puc_output_str, WLAN_CFG_HOST_MAX_RSP_LEN,
                    WLAN_CFG_HOST_MAX_RSP_LEN - 1,
                    "[Error]analyse_wf_mode, uc_param_num(%u) should be 1!\n",
                    cfg_param->uc_param_num);
            }
            return OAL_FAIL;
        }
    }
    return OAL_SUCC;
}

/* twin 13: tx_wf_mode (binary 0x155ad4, 536 B). */
uint32_t alg_cfg_args_analysis_waterfilling_mode(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len)
{
    uint32_t ret;

    ret = alg_cfg_param_analyze_waterfilling_mode(alg_cfg_param,
        (alg_cfg_param_waterfilling_mode_stru *)mem_block);
    if (ret != OAL_SUCC) {
        return ret;
    }
    *len = sizeof(alg_cfg_param_waterfilling_mode_stru);
    return OAL_SUCC;
}

static uint32_t alg_cfg_param_analyze_waterfilling_enable(alg_cfg_param_stru *cfg_param,
    alg_cfg_param_waterfilling_enable_stru *param)
{
    uint32_t ret;
    uint32_t value;

    if (param->alg_cfg_hdr.en_cfg_type == ALG_CFG_TYPE_SET) {
        if (cfg_param->uc_param_num != 1) {
            if (cfg_param->puc_output_str != NULL) {
                (void)sprintf_s((char *)cfg_param->puc_output_str, WLAN_CFG_HOST_MAX_RSP_LEN,
                    "[Error]config wf_enable, uc_param_num(%u) should be 1!\n",
                    cfg_param->uc_param_num);
            }
            return OAL_FAIL;
        }
        param->waterfilling_enable = (uint8_t)oal_atoi((const char *)cfg_param->apuc_param_args[0]);
        if (param->waterfilling_enable != ALG_CFG_TX_WATERFILLING_ENABLE &&
            param->waterfilling_enable != ALG_CFG_TX_WATERFILLING_DISABLE) {
            if (cfg_param->puc_output_str != NULL) {
                (void)snprintf_s((char *)cfg_param->puc_output_str, WLAN_CFG_HOST_MAX_RSP_LEN,
                    WLAN_CFG_HOST_MAX_RSP_LEN - 1,
                    "[Error]analyse_wf_enable, uc_param_enable(%u) invalid, fix mode:0 ; auto mode:1 ;\n",
                    param->waterfilling_enable);
            }
            return OAL_FAIL;
        }
    } else { /* GET */
        if (cfg_param->uc_param_num != 0) {
            if (cfg_param->puc_output_str != NULL) {
                (void)snprintf_s((char *)cfg_param->puc_output_str, WLAN_CFG_HOST_MAX_RSP_LEN,
                    WLAN_CFG_HOST_MAX_RSP_LEN - 1,
                    "[Error]analyse_wf_mode, uc_param_num(%u) should be 1!\n",
                    cfg_param->uc_param_num);
            }
            return OAL_FAIL;
        }
    }
    return OAL_SUCC;
}

/* twin 14: wf_enable (binary 0x155cec, 536 B). */
uint32_t alg_cfg_args_analysis_waterfilling_enable(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len)
{
    uint32_t ret;

    ret = alg_cfg_param_analyze_waterfilling_enable(alg_cfg_param,
        (alg_cfg_param_waterfilling_enable_stru *)mem_block);
    if (ret != OAL_SUCC) {
        return ret;
    }
    *len = sizeof(alg_cfg_param_waterfilling_enable_stru);
    return OAL_SUCC;
}

static uint32_t alg_cfg_param_analyze_hardamard_mode(alg_cfg_param_stru *cfg_param,
    alg_cfg_param_hardamard_mode_stru *param)
{
    uint32_t ret;
    uint32_t value;

    if (param->alg_cfg_hdr.en_cfg_type == ALG_CFG_TYPE_SET) {
        if (cfg_param->uc_param_num != 1) {
            if (cfg_param->puc_output_str != NULL) {
                (void)sprintf_s((char *)cfg_param->puc_output_str, WLAN_CFG_HOST_MAX_RSP_LEN,
                    "[Error]config hdtx_mode, uc_param_num(%u) should be 1!\n",
                    cfg_param->uc_param_num);
            }
            return OAL_FAIL;
        }
        param->hardamard_mode = (uint8_t)oal_atoi((const char *)cfg_param->apuc_param_args[0]);
        if (param->hardamard_mode != ALG_CFG_TX_HARDMARD_AUTO_MODE &&
            param->hardamard_mode != ALG_CFG_TX_HARDMARD_FIX_MODE) {
            if (cfg_param->puc_output_str != NULL) {
                (void)snprintf_s((char *)cfg_param->puc_output_str, WLAN_CFG_HOST_MAX_RSP_LEN,
                    WLAN_CFG_HOST_MAX_RSP_LEN - 1,
                    "[Error]analyse_hdtx_mode, uc_param_mode(%u) invalid, fix mode:0 ; auto mode:1 ;\n",
                    param->hardamard_mode);
            }
            return OAL_FAIL;
        }
    } else { /* GET */
        if (cfg_param->uc_param_num != 0) {
            if (cfg_param->puc_output_str != NULL) {
                (void)snprintf_s((char *)cfg_param->puc_output_str, WLAN_CFG_HOST_MAX_RSP_LEN,
                    WLAN_CFG_HOST_MAX_RSP_LEN - 1,
                    "[Error]analyse_hdtx_mode, uc_param_num(%u) should be 1!\n",
                    cfg_param->uc_param_num);
            }
            return OAL_FAIL;
        }
    }
    return OAL_SUCC;
}

/* twin 15: wf_hadamard_mode (binary 0x155f04, 536 B). */
uint32_t alg_cfg_args_analysis_hardamard_mode(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len)
{
    uint32_t ret;

    ret = alg_cfg_param_analyze_hardamard_mode(alg_cfg_param,
        (alg_cfg_param_hardamard_mode_stru *)mem_block);
    if (ret != OAL_SUCC) {
        return ret;
    }
    *len = sizeof(alg_cfg_param_hardamard_mode_stru);
    return OAL_SUCC;
}

static uint32_t alg_cfg_param_analyze_hardamard_enable(alg_cfg_param_stru *cfg_param,
    alg_cfg_param_hardamard_enable_stru *param)
{
    if (param->alg_cfg_hdr.en_cfg_type == ALG_CFG_TYPE_SET) {
        if (cfg_param->uc_param_num != 1) {
            if (cfg_param->puc_output_str != NULL) {
                (void)sprintf_s((char *)cfg_param->puc_output_str, WLAN_CFG_HOST_MAX_RSP_LEN,
                    "[Error]config hardamard_enable, uc_param_num(%u) should be 1!\n",
                    cfg_param->uc_param_num);
            }
            return OAL_FAIL;
        }
        param->hardamard_enable = (uint8_t)oal_atoi((const char *)cfg_param->apuc_param_args[0]);
        if ((param->hardamard_enable != ALG_CFG_TX_WATERFILLING_ENABLE) &&
            (param->hardamard_enable != ALG_CFG_TX_WATERFILLING_DISABLE)) {
            if (cfg_param->puc_output_str != NULL) {
                (void)snprintf_s((char *)cfg_param->puc_output_str, WLAN_CFG_HOST_MAX_RSP_LEN,
                    WLAN_CFG_HOST_MAX_RSP_LEN - 1,
                    "[Error]hardamard_enable, uc_param_enable(%u) invalid, eable : 1 disable: 0;\n",
                    param->hardamard_enable);
            }
            return OAL_FAIL;
        }
    } else { /* GET */
        if (cfg_param->uc_param_num != 0) {
            if (cfg_param->puc_output_str != NULL) {
                (void)snprintf_s((char *)cfg_param->puc_output_str, WLAN_CFG_HOST_MAX_RSP_LEN,
                    WLAN_CFG_HOST_MAX_RSP_LEN - 1,
                    "[Error]analyse_hdtx_enable, param_num(%u) should be 1!\n",
                    cfg_param->uc_param_num);
            }
            return OAL_FAIL;
        }
    }
    return OAL_SUCC;
}

/* twin 16: hadamard_enable (binary 0x15611c, 536 B: SET 1 arg via oal_atoi
 * with 0/1 range check, GET 0 args - same contract). */
uint32_t alg_cfg_args_analysis_hardamard_enable(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len)
{
    uint32_t ret;

    ret = alg_cfg_param_analyze_hardamard_enable(alg_cfg_param,
        (alg_cfg_param_hardamard_enable_stru *)mem_block);
    if (ret != OAL_SUCC) {
        return ret;
    }
    *len = sizeof(alg_cfg_param_hardamard_enable_stru);
    return OAL_SUCC;
}

static uint32_t alg_cfg_param_analyze_waterfilling_limit(alg_cfg_param_stru *cfg_param,
    alg_cfg_param_waterfilling_limit_stru *param)
{
    if (param->st_alg_cfg_hdr.en_cfg_type == ALG_CFG_TYPE_SET) {
        if (cfg_param->uc_param_num != 1) {
            if (cfg_param->puc_output_str != NULL) {
                (void)sprintf_s((char *)cfg_param->puc_output_str, WLAN_CFG_HOST_MAX_RSP_LEN,
                    "[Error]config wf_limit, uc_param_num(%u) should be 1!\n",
                    cfg_param->uc_param_num);
            }
            return OAL_FAIL;
        }
        param->waterfilling_limit = (int8_t)oal_atoi((const char *)cfg_param->apuc_param_args[0]);
        if (param->waterfilling_limit > ALG_CFG_TX_WATERFILLING_MAX_LIMIT) {
            if (cfg_param->puc_output_str != NULL) {
                (void)snprintf_s((char *)cfg_param->puc_output_str, WLAN_CFG_HOST_MAX_RSP_LEN,
                    WLAN_CFG_HOST_MAX_RSP_LEN - 1,
                    "[Error]wf_limit, uc_param_enable(%u) invalid, limit < -128 || limit > 127;\n",
                    param->waterfilling_limit);
            }
            return OAL_FAIL;
        }
    } else { /* GET */
        if (cfg_param->uc_param_num != 0) {
            if (cfg_param->puc_output_str != NULL) {
                (void)snprintf_s((char *)cfg_param->puc_output_str, WLAN_CFG_HOST_MAX_RSP_LEN,
                    WLAN_CFG_HOST_MAX_RSP_LEN - 1,
                    "[Error]analyse_wf_limit, uc_param_num(%u) should be 1!\n",
                    cfg_param->uc_param_num);
            }
            return OAL_FAIL;
        }
    }
    return OAL_SUCC;
}

/* twin 17: wf_limit (binary 0x156334, 444 B: SET 1 arg int8 with >127
 * check, GET 0 args - same contract). */
uint32_t alg_cfg_args_analysis_waterfilling_limit(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len)
{
    uint32_t ret;

    ret = alg_cfg_param_analyze_waterfilling_limit(alg_cfg_param,
        (alg_cfg_param_waterfilling_limit_stru *)mem_block);
    if (ret != OAL_SUCC) {
        return ret;
    }
    *len = sizeof(alg_cfg_param_waterfilling_limit_stru);
    return OAL_SUCC;
}

/* ==================== non-twinned parsers kept for table completeness ==================== */
/* (thin wrappers over the same bool/int8/mac patterns; not part of the
 * 17-twin oracle set, provided so the dispatch table above links.) */

uint32_t alg_cfg_args_analysis_txmode_mode_sw(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len)
{
    alg_cfg_param_txmode_dev_stru *param = (alg_cfg_param_txmode_dev_stru *)mem_block;
    uint32_t ret;
    uint32_t value;

    if (param->st_alg_cfg_hdr.en_cfg_type == ALG_CFG_TYPE_SET) {
        if (alg_cfg_param->uc_param_num != 1) {
            return OAL_FAIL;
        }
        ret = alg_cfg_get_match_value(g_alg_cfg_bool_tbl,
            alg_cfg_param_get_tbl_size(ALG_CFG_PARAM_BOOL),
            alg_cfg_param->apuc_param_args[0], &value);
        if (ret != OAL_SUCC) {
            return ret;
        }
        param->en_txmode_sw = (oal_bool_enum_uint8)value;
    }
    *len = sizeof(alg_cfg_param_txmode_dev_stru);
    return OAL_SUCC;
}

static uint32_t alg_cfg_chain_prob_like_parser(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len, uint8_t value_offset)
{
    if (alg_cfg_param->uc_param_num != 1) {
        return OAL_FAIL;
    }
    mem_block[value_offset] = (uint8_t)oal_atoi((const char *)alg_cfg_param->apuc_param_args[0]);
    *len = sizeof(alg_cfg_hdr_stru) + 4;
    return OAL_SUCC;
}

uint32_t alg_cfg_args_analysis_txmode_user_fix_chain(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len)
{
    return alg_cfg_chain_prob_like_parser(alg_cfg_param, mem_block, len, sizeof(alg_cfg_hdr_stru) + 8);
}

uint32_t alg_cfg_args_analysis_txmode_debug_log_switch(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len)
{
    return alg_cfg_chain_prob_like_parser(alg_cfg_param, mem_block, len, sizeof(alg_cfg_hdr_stru));
}

uint32_t alg_cfg_args_analysis_txmode_chain_prob_sw(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len)
{
    return alg_cfg_chain_prob_like_parser(alg_cfg_param, mem_block, len, sizeof(alg_cfg_hdr_stru));
}

uint32_t alg_cfg_args_analysis_waterfilling_bw_mode(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len)
{
    alg_cfg_param_waterfilling_mode_stru *param = (alg_cfg_param_waterfilling_mode_stru *)mem_block;
    uint32_t ret;

    ret = alg_cfg_param_analyze_waterfilling_mode(alg_cfg_param, param);
    if (ret != OAL_SUCC) {
        return ret;
    }
    *len = sizeof(alg_cfg_param_waterfilling_mode_stru);
    return OAL_SUCC;
}

uint32_t alg_cfg_args_analysis_waterfilling_sub_asnr(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len)
{
    alg_cfg_param_waterfilling_mode_stru *param = (alg_cfg_param_waterfilling_mode_stru *)mem_block;
    uint32_t ret;

    ret = alg_cfg_param_analyze_waterfilling_mode(alg_cfg_param, param);
    if (ret != OAL_SUCC) {
        return ret;
    }
    *len = sizeof(alg_cfg_param_waterfilling_mode_stru);
    return OAL_SUCC;
}

uint32_t alg_cfg_args_analysis_waterfilling_asnr(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len)
{
    uint8_t idx;
    alg_cfg_param_all_alg_stru *param = (alg_cfg_param_all_alg_stru *)mem_block;

    if (param->alg_cfg_hdr.en_cfg_type == ALG_CFG_TYPE_SET) {
        if (alg_cfg_param->uc_param_num > ALG_CFG_TX_WATERFILLING_SNR_NUM) {
            return OAL_FAIL;
        }
        for (idx = 0; idx < alg_cfg_param->uc_param_num; idx++) {
            param->value[idx] = (uint8_t)oal_atoi((const char *)alg_cfg_param->apuc_param_args[idx]);
        }
    } else {
        if (alg_cfg_param->uc_param_num != 0) {
            return OAL_FAIL;
        }
    }
    *len = sizeof(alg_cfg_param_all_alg_stru);
    return OAL_SUCC;
}

uint32_t alg_cfg_args_analysis_waterfilling_debug(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len)
{
    alg_cfg_param_waterfilling_mode_stru *param = (alg_cfg_param_waterfilling_mode_stru *)mem_block;
    uint32_t ret;

    ret = alg_cfg_param_analyze_waterfilling_mode(alg_cfg_param, param);
    if (ret != OAL_SUCC) {
        return ret;
    }
    *len = sizeof(alg_cfg_param_waterfilling_mode_stru);
    return OAL_SUCC;
}

uint32_t alg_cfg_args_analysis_wf_force_flag(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len)
{
    alg_cfg_param_waterfilling_mode_stru *param = (alg_cfg_param_waterfilling_mode_stru *)mem_block;
    uint32_t ret;

    ret = alg_cfg_param_analyze_waterfilling_mode(alg_cfg_param, param);
    if (ret != OAL_SUCC) {
        return ret;
    }
    *len = sizeof(alg_cfg_param_waterfilling_mode_stru);
    return OAL_SUCC;
}

uint32_t alg_cfg_args_analysis_hdtx_force_flag(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len)
{
    alg_cfg_param_hardamard_enable_stru *param = (alg_cfg_param_hardamard_enable_stru *)mem_block;
    uint32_t ret;

    ret = alg_cfg_param_analyze_hardamard_enable(alg_cfg_param, param);
    if (ret != OAL_SUCC) {
        return ret;
    }
    *len = sizeof(alg_cfg_param_hardamard_enable_stru);
    return OAL_SUCC;
}
