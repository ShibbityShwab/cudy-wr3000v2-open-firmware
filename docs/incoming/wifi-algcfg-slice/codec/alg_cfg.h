/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd.
 * hi5622 alg_cfg command codec - core types (PORT: original text, GPL-2.0).
 *
 * Contract origin (see REPORT.md in wifi-algcfg-slice/):
 *  - struct shapes and enum values verified against the local vendor
 *    hi5622v100_wifi.ko (symbols, table dumps, disassembly);
 *  - generic core semantics match the GPL-2.0 hi3881/hi1105 WLAN framework
 *    lineage (wal_config_alg_cfg.c, alg_cfg.h; the hi1105 reference file is
 *    used locally as an execution oracle only and is NOT vendored).
 * SPDX-License-Identifier: GPL-2.0
 */
#ifndef ALG_CFG_H
#define ALG_CFG_H

#include "oal_compat.h"

#define ALG_CFG_PARAM_MAX_NUM           20    /* max per-command parameter bytes */
#define ALG_CFG_PARAM_TXMODE_MAX_CHAIN_BITMAP 0x0f
#define WLAN_CFG_MAX_ARGS_NUM           20
#define WLAN_CFG_MAX_LEN_EACH_ARG       50
#define WLAN_CFG_HOST_MAX_RSP_LEN       2048
#define WLAN_MAC_ADDR_LEN               6

typedef enum {
    ALG_CFG_TYPE_SET,
    ALG_CFG_TYPE_GET,
    ALG_CFG_TYPE_BUTT
} alg_cfg_type_enum;

/* pkt-type sub-command filter; IRRELEVANT==13 verified in the binary
 * (get_pkt_type returns r0=13 on no match). */
typedef enum {
    ALG_CFG_PKT_TYPE_UCAST_DATA,
    ALG_CFG_PKT_TYPE_MCAST_DATA,
    ALG_CFG_PKT_TYPE_BCAST_DATA,
    ALG_CFG_PKT_TYPE_MUMIMO_DATA,
    ALG_CFG_PKT_TYPE_OFDMA_DATA,
    ALG_CFG_PKT_TYPE_UCAST_MGMT,
    ALG_CFG_PKT_TYPE_MCAST_MGMT,
    ALG_CFG_PKT_TYPE_BCAST_MGMT,
    ALG_CFG_PKT_TYPE_PROT_CTRL,
    ALG_CFG_PKT_TYPE_NDP,
    ALG_CFG_PKT_TYPE_NDPA,
    ALG_CFG_PKT_TYPE_RESP_CTRL,
    ALG_CFG_PKT_TYPE_OTHER_CTRL,
    ALG_CFG_PKT_TYPE_IRRELEVANT,
    ALG_CFG_PKT_TYPE_BUTT = ALG_CFG_PKT_TYPE_IRRELEVANT
} alg_cfg_pkt_type_enum;

typedef enum {
    ALG_CFG_TX_MODE_AUTO = 0,
    ALG_CFG_TX_MODE_CHAIN0_FIX,
    ALG_CFG_TX_MODE_CHAIN1_FIX,
    ALG_CFG_TX_MODE_CHAIN2_FIX,
    ALG_CFG_TX_MODE_CHAIN3_FIX,
    ALG_CFG_TX_MODE_CSD2_FIX,
    ALG_CFG_TX_MODE_CSD3_FIX,
    ALG_CFG_TX_MODE_CSD4_FIX,
    ALG_CFG_TX_MODE_STBC2_FIX,
    ALG_CFG_TX_MODE_STBC3_FIX,
    ALG_CFG_TX_MODE_STBC4_FIX,
    ALG_CFG_TX_MODE_TXBF_FIX,
    ALG_CFG_TX_MODE_STBC_2NSS_FIX,
    ALG_CFG_TX_MODE_BUTT
} alg_cfg_param_tx_mode_enum;

typedef enum {
    ALG_CFG_TX_WATERFILLING_AUTO_MODE = 0,
    ALG_CFG_TX_WATERFILLING_FIX_MODE = 1,
    ALG_CFG_TX_WATERFILLING_ENABLE = 1,
    ALG_CFG_TX_WATERFILLING_DISABLE = 0,
    ALG_CFG_TX_HARDMARD_AUTO_MODE = 0,
    ALG_CFG_TX_HARDMARD_FIX_MODE = 1,
    ALG_CFG_TX_WATERFILLING_MAX_LIMIT = 127,
    ALG_CFG_TX_WATERFILLING_MIN_LIMIT = 0,
    ALG_CFG_TX_WATERFILLING_SNR_NUM = 4
} alg_cfg_param_tx_waterfilling_enum;

/* twinned cfg-id subset (reference numbering, kept for the 17-twin oracle) */
typedef enum {
    MAC_ALG_CFG_TXMODE_START,
    MAC_ALG_CFG_TXMODE_MODE_SW,
    MAC_ALG_CFG_TXMODE_ALL_USER_FIX_MODE,
    MAC_ALG_CFG_TXMODE_USER_FIX_MODE,
    MAC_ALG_CFG_TXMODE_USER_FIX_CHAIN,
    MAC_ALG_CFG_TXMODE_DEBUG_LOG_SWITCH,
    MAC_ALG_CFG_TXMODE_CHAIN_PROB_SW,
    MAC_ALG_CFG_TXMODE_END,
    MAC_ALG_CFG_WATERFILLING_START,
    MAC_ALG_CFG_WATERFILLING_MODE,
    MAC_ALG_CFG_WATERFILLING_ENABLE,
    MAC_ALG_CFG_HARDAMARDTX_MODE,
    MAC_ALG_CFG_HARDAMARDTX_ENABLE,
    MAC_ALG_CFG_WATERFILLING_BW_MODE,
    MAC_ALG_CFG_WATERFILLING_SUB_ASNR,
    MAC_ALG_CFG_WATERFILLING_CI_ASNR,
    MAC_ALG_CFG_WATERFILLING_LIMIT_ASNR,
    MAC_ALG_CFG_WATERFILLING_DEBUG,
    MAC_ALG_CFG_WATERFILLING_FORCE_FLAG,
    MAC_ALG_CFG_HARDAMARDTX_FORCE_FLAG,
    MAC_ALG_CFG_WATERFILLING_END
} mac_alg_cfg_enum;

/* ---- message header: 12 bytes, offsets verified in the binary ---- */
typedef struct {
    mac_alg_cfg_enum_uint16 en_cfg_id;
    alg_cfg_type_enum_uint8 en_cfg_type;
    alg_cfg_pkt_type_enum_uint8 en_pkt_type;
    uint32_t dev_ret;
    oal_bool_enum_uint8 en_need_w4_dev_return;
    uint8_t auc_resv[3];
} alg_cfg_hdr_stru;

typedef struct {
    alg_cfg_hdr_stru alg_cfg_hdr;
    uint8_t value[ALG_CFG_PARAM_MAX_NUM];
} alg_cfg_param_all_alg_stru;

/* per-command parameter payloads (txmode / waterfilling twin subset) */
typedef struct {
    alg_cfg_hdr_stru st_alg_cfg_hdr;
    oal_bool_enum_uint8 en_txmode_sw;
    uint8_t auc_resv[3];
} alg_cfg_param_txmode_dev_stru;

typedef struct {
    alg_cfg_hdr_stru st_alg_cfg_hdr;
    uint8_t auc_mac_addr[WLAN_MAC_ADDR_LEN];
    uint8_t auc_resv[2];
    alg_cfg_param_tx_mode_enum en_fix_txmode;
} alg_cfg_param_txmode_user_stru;

typedef struct {
    alg_cfg_hdr_stru st_alg_cfg_hdr;
    alg_cfg_param_tx_mode_enum en_fix_txmode;
} alg_cfg_param_txmode_all_user_stru;

typedef struct {
    alg_cfg_hdr_stru alg_cfg_hdr;
    oal_bool_enum_uint8 waterfilling_mode;
    uint8_t auc_resv[3];
} alg_cfg_param_waterfilling_mode_stru;

typedef struct {
    alg_cfg_hdr_stru alg_cfg_hdr;
    oal_bool_enum_uint8 waterfilling_enable;
    uint8_t auc_resv[3];
} alg_cfg_param_waterfilling_enable_stru;

typedef struct {
    alg_cfg_hdr_stru alg_cfg_hdr;
    oal_bool_enum_uint8 hardamard_mode;
    uint8_t auc_resv[3];
} alg_cfg_param_hardamard_mode_stru;

typedef struct {
    alg_cfg_hdr_stru alg_cfg_hdr;
    oal_bool_enum_uint8 hardamard_enable;
    uint8_t auc_resv[3];
} alg_cfg_param_hardamard_enable_stru;

typedef struct {
    alg_cfg_hdr_stru st_alg_cfg_hdr;
    int8_t waterfilling_limit;
    uint8_t auc_resv[3];
} alg_cfg_param_waterfilling_limit_stru;

/* ---- process-info table entry: 12 bytes, offsets verified in the binary
 * (name at +0, u16 id at +4, u8 type at +6, resv at +7, 4 flags at +8) ---- */
typedef struct {
    oal_bool_enum_uint8 en_need_host_process;
    oal_bool_enum_uint8 en_need_w4_host_return;
    oal_bool_enum_uint8 en_need_device_process;
    oal_bool_enum_uint8 en_need_w4_device_return;
} wlan_cfg_process_info_stru;

typedef struct {
    char *pc_alg_cfg_name;
    mac_alg_cfg_enum_uint16 en_alg_cfg_id;
    alg_cfg_type_enum_uint8 en_cfg_type;
    uint8_t auc_resv[1];
    wlan_cfg_process_info_stru st_process_info;
} alg_cfg_process_info_stru;

/* string->value table: 8 bytes {char* name, u32 value} */
typedef struct {
    char *pc_name;
    uint32_t value;
} alg_cfg_param_tbl_stru;

/* param-size table entry: 4 bytes {u8 type, u8 size, u8 resv[2]} */
typedef struct {
    alg_cfg_param_tbl_size_enum_uint8 en_param_type;
    uint8_t uc_tbl_size;
    uint8_t auc_resv[2];
} alg_cfg_param_tbl_size_info_stru;

typedef enum {
    ALG_CFG_PARAM_PKT_TYPE,
    ALG_CFG_PARAM_RATE_MODE,
    ALG_CFG_PARAM_FEC_CODING,
    ALG_CFG_PARAM_LEGACY_RATE,
    ALG_CFG_PARAM_PROTOCOL_MODE,
    ALG_CFG_PARAM_BOOL,
    ALG_CFG_PARAM_CHAIN,
    ALG_CFG_PARAM_PROTECT_MODE,
    ALG_CFG_PARAM_HE_LTF,
    ALG_CFG_PARAM_PREAMBLE,
    ALG_CFG_PARAM_RTS_BW_MODE,
    ALG_CFG_PARAM_FREQ_BW_MODE,
    ALG_CFG_PARAM_BANDWIDTH_MODE,
    ALG_CFG_PARAM_TX_MODE,
    ALG_CFG_PARAM_SND_MODE,
    ALG_CFG_PARAM_SND_FBACK_TYPE,
    ALG_CFG_PARAM_SND_SCH_MODE,
    ALG_CFG_PARAM_SND_SW_TYPE,
    ALG_CFG_PARAM_SND_USER_MAT_TYPE,
    ALG_CFG_PARAM_FIX_POW_MODE,
    ALG_CFG_PARAM_TPC_POW_MODE,
    ALG_CFG_PARAM_GI_TYPE_NONAX,
    ALG_CFG_PARAM_GI_TYPE_AX,
    ALG_CFG_PARAM_AR_DEV_PARAM,
    ALG_CFG_PARAM_ULSCH_DEV_SCH_MODE,
    ALG_CFG_PARAM_SCEN_USER_INFO_TYPE,
    ALG_CFG_PARAM_AGGR_MODE,
    ALG_CFG_PARAM_AGGR_TIME_OPT,
    ALG_CFG_PARAM_BUTT,
    /* hi5622 additions (types 29..46 exist in the binary's 40-entry table;
     * exact per-chip names to be pinned by the bench probe) */
    ALG_CFG_PARAM_HI5622_MAX = 46
} alg_cfg_param_tbl_size_enum;

/* ---- common parser parameter block ---- */
typedef struct alg_cfg_param_stru {
    void *pst_mac_vap;                       /* vap context (unused by codec core) */
    uint8_t *apuc_param_args[WLAN_CFG_MAX_ARGS_NUM];
    uint8_t *puc_output_str;                 /* error/response text sink */
    uint8_t uc_param_num;
    uint8_t auc_resv[3];
} alg_cfg_param_stru;

/* ---- parser entry: 8 bytes {u16 id, u8 resv[2], fn*} ---- */
typedef uint32_t (*alg_cfg_sub_alg_args_analysis_func)(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len);
typedef struct {
    mac_alg_cfg_enum_uint16 en_alg_cfg_id;
    uint8_t auc_resv[2];
    alg_cfg_sub_alg_args_analysis_func p_sub_alg_args_analysis_func;
} wlan_alg_cfg_sub_alg_args_analysis_tbl_stru;

/* ---- hi5622 per-command LUT descriptor: 176 bytes (0xb0) per entry,
 * 416 entries at .data+0x4934 in the binary; key u32 = (type<<16)|id,
 * verified: key at +0; flags at +12 (0x000b0001 SET / 0x000b0000 GET),
 * 0x00010101 at +16 (SET), param-count-ish 0x0a at +20; the rest of the
 * descriptor is populated at runtime by the vendor fixup path (see REPORT). */
typedef struct {
    uint32_t key;                 /* (en_cfg_type << 16) | en_alg_cfg_id */
    uint32_t resv0;
    uint32_t resv1;
    uint32_t msg_flags;           /* 0x000b0001 SET, 0x000b0000 GET (verified) */
    uint32_t need_flags;          /* 0x00010101 on SET entries (verified) */
    uint32_t fixed_field;         /* 0x0a (verified) */
    uint8_t opaque[152];
} alg_cfg_lut_entry_stru;

/* ---- externs ---- */
extern alg_cfg_process_info_stru g_alg_cfg_process_info_table[];
extern alg_cfg_param_tbl_stru g_alg_cfg_pkt_type_info_table[];
extern alg_cfg_param_tbl_stru g_alg_cfg_bool_tbl[];
extern alg_cfg_param_tbl_stru g_alg_cfg_tx_mode_tbl[ALG_CFG_TX_MODE_BUTT];
extern alg_cfg_param_tbl_size_info_stru g_alg_cfg_param_size_tbl[3]; /* twin subset; hi5622 census rows in mapping/ */

uint16_t alg_cfg_process_tbl_size(void);
uint8_t alg_cfg_param_get_tbl_size(alg_cfg_param_tbl_size_enum_uint8 param_type);
uint32_t alg_cfg_get_cmd_one_arg(const char *cmd, char *arg, uint32_t arg_size, uint32_t *cmd_offset);
alg_cfg_process_info_stru *alg_cfg_search_process_info_by_cfg_name(const char *alg_cfg_name);
void alg_cfg_search_process_info_by_cfg_name_normal(const char *args, uint32_t arg_size,
    alg_cfg_process_info_stru **target_cfg_info);
alg_cfg_process_info_stru *alg_cfg_search_process_info_by_cfg_id(mac_alg_cfg_enum_uint16 alg_cfg_id,
    alg_cfg_type_enum_uint8 cfg_type);
void alg_cfg_search_process_info_by_cfg_id_normal(mac_alg_cfg_enum_uint16 alg_cfg_id,
    alg_cfg_type_enum_uint8 cfg_type, alg_cfg_process_info_stru **target_cfg_info);
alg_cfg_pkt_type_enum_uint8 alg_cfg_get_pkt_type(const char *cmd_args);
uint32_t alg_cfg_analysis_args_head(char *real_args[WLAN_CFG_MAX_ARGS_NUM],
    alg_cfg_process_info_stru **real_alg_cfg_info, uint8_t *real_args_offset,
    alg_cfg_pkt_type_enum_uint8 *real_pkt_type);
uint32_t alg_cfg_analysis_args(char *args[WLAN_CFG_MAX_ARGS_NUM], uint8_t args_num,
    alg_cfg_param_stru *alg_cfg_param, alg_cfg_hdr_stru *alg_cfg_hdr,
    wlan_cfg_process_info_stru *process_info);
uint32_t alg_cfg_get_match_value(const alg_cfg_param_tbl_stru *tbl, uint16_t tbl_size,
    const uint8_t *str, uint32_t *out_value);

/* parser entry points (the 7 twinned parsers) */
uint32_t alg_cfg_args_analysis_txmode_user_fix_mode(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len);
uint32_t alg_cfg_args_analysis_txmode_all_user_fix_mode(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len);
uint32_t alg_cfg_args_analysis_waterfilling_mode(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len);
uint32_t alg_cfg_args_analysis_waterfilling_enable(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len);
uint32_t alg_cfg_args_analysis_waterfilling_limit(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len);
uint32_t alg_cfg_args_analysis_hardamard_mode(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len);
uint32_t alg_cfg_args_analysis_hardamard_enable(alg_cfg_param_stru *alg_cfg_param,
    uint8_t *mem_block, uint16_t *len);

#endif /* ALG_CFG_H */
