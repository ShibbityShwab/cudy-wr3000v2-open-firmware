/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd.
 * hi5622 wal config-command glue - shared types (ORIGINAL text, GPL-2.0).
 *
 * This is the host-side glue slice for the alg_cfg command codec ported by
 * the wifi-algcfg-slice (opensource/docs/incoming/wifi-algcfg-slice/): it
 * wires the codec into a 6.18 wal config-command front end.  All struct
 * shapes, enum values and offsets in this header were verified against the
 * vendor hi5622v100_wifi.ko / hi5622v100_plat.ko (local, never committed)
 * and the host-driver contract in wifi-driver-re/REPORT.md; the generic
 * framework shapes follow the GPL-2.0 hi3881 WLAN framework lineage
 * (wal_hipriv.c / wal_ioctl.h vendored in
 * wifi-algcfg-slice/vendor/hi3881-wal/).  No hi1105/hongmengkernel text
 * (no license grant there) is reproduced.
 * SPDX-License-Identifier: GPL-2.0
 */
#ifndef WAL_CFG_GLUE_H
#define WAL_CFG_GLUE_H

#include <linux/types.h>
#include "oal_compat.h"   /* codec shim: securec/oam/error-code surface */
#include "alg_cfg.h"      /* codec core: process table, LUT entry, codec entry */

struct wiphy;
struct wireless_dev;

/* ------------------------------------------------------------------ */
/* vendor wlan cfg-ids used by the config-command path                 */
/* (values pinned against the framework wlan_cfg_id.h lineage and the  */
/*  0x0101 "alg" / 0x0102 "cali" dispatch documented in                */
/*  wifi-driver-re/REPORT.md section 2.4)                              */
/* ------------------------------------------------------------------ */
#define WLAN_CFGID_ALG_PARAM    254   /* 算法参数配置 (legacy one-shot)      */
#define WLAN_CFGID_ALG_ENTRY    257   /* 0x0101 算法参数配置新框架入口 (alg) */
#define WLAN_CFGID_CALI_ENTRY   258   /* 0x0102 校准参数配置入口 (cali)      */
#define WLAN_CFGID_ALG_CFG      298   /* alg_cfg算法命令                      */
#define WLAN_CFGID_CALI_CFG     299   /* 校准配置命令                        */

typedef uint16_t wlan_cfgid_enum_uint16;

/* wal message types (codec contract: SET -> WRITE, GET -> QUERY) */
#define WAL_MSG_TYPE_WRITE      3
#define WAL_MSG_TYPE_QUERY      4
/* vendor 12-byte write-msg header ahead of the payload (driver-re s3.1) */
#define WAL_MSG_WRITE_MSG_HDR_LENGTH 12

/* hipriv-style entry limits (mirror the vendored hi3881 wal_hipriv.h) */
#define WAL_CFG_CMD_MAX_LEN      (4096 - 4)
#define WAL_CFG_CMD_NAME_MAX_LEN 36

/* ------------------------------------------------------------------ */
/* driver command-line parameter block (framework contract shape)      */
/* ------------------------------------------------------------------ */
typedef struct {
	void *pst_net_dev;                       /* oal_net_device_stru == net_device */
	wlan_cfgid_enum_uint16 en_wlan_cfg_id;   /* entry key (WLAN_CFGID_ALG_ENTRY ...) */
	uint8_t uc_args_num;                     /* 入参个数 */
	char *apc_args[WLAN_CFG_MAX_ARGS_NUM];   /* per-arg string pointers */
	char *pc_output;                         /* driver response sink */
	uint16_t us_output_len;                  /* response bytes */
	uint32_t process_ret;                    /* last error code */
} wlan_cfg_param_stru;

/* process-entry function: parses the arg vector, fills the transfer
 * mem_block, chooses the msg_type, may refine the process-info flags.
 * hi5622 note: the binary's wal_wlan_cfg_alg_process_entry takes the
 * flattened (args, args_num, msg_type, mem_block, mem_len, process_info)
 * form - the codec port keeps exactly that 6-arg shape. */
typedef int32_t (*wal_wlan_cfg_process_entry_func)(
	char *args[WLAN_CFG_MAX_ARGS_NUM], uint8_t args_num,
	uint8_t *msg_type, uint8_t *mem_block, uint16_t *mem_len,
	wlan_cfg_process_info_stru *process_info);

/* module-level table row: wlan cfg id -> process entry + need flags */
typedef struct {
	wlan_cfgid_enum_uint16 en_wlan_cfg_id;
	uint8_t auc_resv[2];
	wal_wlan_cfg_process_entry_func p_func;
	wlan_cfg_process_info_stru st_wlan_cfg_process_info;
} wal_wlan_cfg_process_info_table_stru;

/* ------------------------------------------------------------------ */
/* wal write message (host-side copy of the vendor transfer block)     */
/* ------------------------------------------------------------------ */
typedef struct {
	wlan_cfgid_enum_uint16 us_cfg_id;
	uint16_t us_msg_len;                     /* payload bytes */
	uint8_t auc_resv[8];                     /* -> 12-byte header total */
	uint8_t auc_value[WLAN_CFG_HOST_MAX_RSP_LEN];
} wal_cfg_write_msg_stru;

/* ------------------------------------------------------------------ */
/* hcc_msg contract (wifi-driver-re/REPORT.md s3.1-3.3, our spelling)  */
/* ------------------------------------------------------------------ */
struct wal_hcc_msg_header {                  /* at *(hcc_msg_obj + 0x118) */
	uint8_t type;                            /* +0x00; low 4 bits = group */
	uint8_t bid;                             /* +0x01; [7:4] dst, [3:0] src */
	uint8_t vap_id;                          /* +0x02 */
	uint8_t chip_id;                         /* +0x03 */
	uint16_t flag;                           /* +0x04 */
	uint16_t id;                             /* +0x06 dispatch key */
	uint8_t rsvd;                            /* +0x08 */
	uint8_t dst;                             /* +0x09 */
	uint8_t params[];                        /* +0x0a */
};

/* 16-byte dispatch handler record {id, func, aux, stats} */
struct wal_hcc_handler_record {
	uint32_t id;
	uint32_t func;
	uint32_t aux;
	uint32_t stats;
};

/* registration table container (driver-re s3.2: item_array @ reg_tab+0x10,
 * 12-byte group records {count, handler_tab}, handler records 16 B) */
struct wal_hcc_reg_tab {
	uint32_t reg_tab_words[4];
	struct {
		uint32_t count;                      /* max message id + 1 */
		uint32_t handler_tab;
		uint32_t aux;
	} group[4];
};

/* ------------------------------------------------------------------ */
/* runtime table layout of the vendor binary                          */
/* (wifi-algcfg-slice/REPORT.md section 3, verified offsets)          */
/* ------------------------------------------------------------------ */
struct wal_cfg_runtime_layout {
	uint32_t process_tbl_off;        /* .data + 0x35c8 */
	uint32_t process_tbl_entries;    /* 414 */
	uint32_t process_entry_size;     /* 12 */
	uint32_t param_size_tbl_off;     /* .data + 0x2d9c */
	uint32_t param_size_entries;     /* 40 (4 B each) */
	uint32_t bool_tbl_off;           /* .data + 0x2e3c (2 x 8 B) */
	uint32_t pkt_type_tbl_off;       /* .data + 0x2e4c (13 x 8 B) */
	uint32_t tx_mode_tbl_off;        /* .data + 0x301c (13 x 8 B) */
	uint32_t alg_lut_off;            /* .data + 0x4934 */
	uint32_t alg_lut_entries;        /* 416 */
	uint32_t alg_lut_entry_size;     /* 176 */
	uint32_t fixup_list_off;         /* .data + 0x0a9874 (8-B pairs) */
	uint32_t fixup_pairs;            /* 4608 */
	uint32_t name_vocab_off;         /* .rodata + 0x23a680 (name cluster) */
};

/* name/LUT fixup pair - the vendor patcher walks 8-B {target, value}
 * pairs at fixup_list_off and writes value into target (name slots of the
 * process table and the runtime fields of the 176-B LUT entries). */
struct wal_cfg_fixup_pair {
	uint32_t target;                 /* slot offset to patch */
	uint32_t value;                  /* name pointer / fn pointer */
};

#define WAL_CFG_MAX_FIXUPS 512     /* host-side capacity; bench decodes 4608 */

/* ------------------------------------------------------------------ */
/* glue surface                                                        */
/* ------------------------------------------------------------------ */
/* module dispatch (the vendor name "wal_wlan_cfg_module_process_entry") */
int32_t wal_wlan_cfg_module_process_entry(wlan_cfg_param_stru *wlan_cfg_param);
int32_t wal_wlan_cfg_module_host_process_entry(wlan_cfg_param_stru *wlan_cfg_param,
	uint8_t msg_type, uint8_t *mem_block, uint16_t mem_len);
int32_t wal_wlan_cfg_module_device_process_entry(wlan_cfg_param_stru *wlan_cfg_param,
	uint8_t msg_type, uint8_t *mem_block, uint16_t mem_len);

/* hipriv-style alg_cfg entry (vendored hi3881 wal_hipriv.c base shape) */
uint32_t wal_hipriv_alg_cfg(void *net_dev, char *pc_param, char *out, uint32_t out_len);
uint32_t wal_hipriv_alg_cfg_get_cfg_id(const char *cmd_name, mac_alg_cfg_enum_uint16 *cfg_id);
uint32_t wal_hipriv_alg_cfg_get_cfg_id_normal(const char *cmd_name,
	uint32_t arg_size, mac_alg_cfg_enum_uint16 *cfg_id);

/* module-table lookup miss (framework contract value) */
#define OAL_ERR_CODE_CFG_PROCESS_ENTRY_NOT_FOUND 1016

/* front end: execute one config command line (sysfs + vendor-cmd path) */
int wal_cfg_exec_command_line(const char *cmd, char *out, uint32_t out_len);
int wal_cfg_register_vendor_commands(struct wiphy *wiphy);

/* command-line token splitter (space-separated, vendor get_cmd_one_arg shape) */
uint8_t wal_cfg_split_args(char *line, char *args[], uint8_t max_args);

/* vendor-exported alg_ functions kept on the 120-entry port list (stubs) */
uint32_t alg_cfg_get_match_string(const char *args, const char *match,
	uint32_t match_len, uint32_t *out);
uint32_t alg_cfg_get_tail_num(const char *args, uint32_t *out);
uint32_t alg_cfg_args_analysis_param(const char *args);
uint32_t alg_cfg_args_param_analyse_equipment_param(uintptr_t a0);

/* LUT / process-table text dumps for the sysfs surface */
int wal_cfg_lut_dump(char *buf, uint32_t buf_len);
int wal_cfg_process_table_dump(char *buf, uint32_t buf_len);
int wal_cfg_param_size_tbl_dump(char *buf, uint32_t buf_len);

/* LUT / name-fixup loader */
int wal_cfg_lut_init(void);
void wal_cfg_lut_deinit(void);
const struct wal_cfg_runtime_layout *wal_cfg_runtime_layout(void);
int wal_cfg_name_fixup_apply(const struct wal_cfg_fixup_pair *pairs, uint32_t count);
int wal_cfg_name_fixup_add(uint32_t process_tbl_idx, const char *name);
uint32_t wal_cfg_fixup_count(void);
uint32_t wal_cfg_fixup_target(uint32_t i);
const char *wal_cfg_fixup_name(uint32_t i);
alg_cfg_lut_entry_stru *wal_cfg_lut_entry(wlan_cfgid_enum_uint16 cfg_id,
	alg_cfg_type_enum_uint8 cfg_type);
const char *wal_cfg_search_dbg_cfg_name_by_cfg_id(mac_alg_cfg_enum_uint16 cfg_id);
void *wal_cfg_search_alg_debug_lut(void);

/* hmac cfg dispatch stub surface */
void hmac_event_config_syn(void *reg_tab, void *msg_obj);
int hmac_config_sync_cmd_common(void *board, void *msg_obj);
void hmac_sync_dmac_alg_cfg_rsp_entry(void *board, void *msg_obj);
void hmac_sync_dmac_alg_cfg_rsp_get_entry_normal(void *board, void *msg_obj);
void hmac_sync_dmac_cali_cfg_rsp_entry(void *board, void *msg_obj);
void hmac_chan_tx_cali_sync(void *board, void *msg_obj);
int hmac_config_sync_ssid(void *board, void *msg_obj);
int hmac_cfg_stubs_register(void);

/* hcc registration stubs (host-only; mirror plat.ko exports) */
int hcc_msg_register_tab_chip(uint32_t group_id, void *handler_tab, uint32_t count);
int hcc_msg_register_tab_core(uint32_t group_id, void *handler_tab, uint32_t count);
int hcc_msg_register_tab(uint32_t group_id, void *handler_tab, uint32_t count);
int hcc_msg_register_tab_customise(uint32_t group_id, void *handler_tab, uint32_t count);

/* wal message plumbing stubs (driver-re s3.7 semantics, no device) */
int32_t wal_config_process_entry(void *net_dev, uint8_t msg_type, uint16_t len, uint8_t *param);
int32_t wal_config_process_pkt(void *net_dev, uint8_t msg_type, uint16_t len, uint8_t *param);
int32_t wal_config_process_pkt_memcpy_proc(uint8_t *dst, uint16_t dst_len,
	const uint8_t *src, uint16_t src_len);
int32_t wal_config_process_recently_cfg_event(void);
int32_t wal_msg_request_add_queue(uint32_t req_id, void *rsp);
int32_t wal_msg_request_remove_queue(uint32_t req_id);
int32_t wal_set_dev_msg_response_by_addr(uint32_t req_id, void *addr, uint32_t len);
int32_t wal_check_and_release_msg_resp(uint32_t req_id);
int32_t wal_sync_query_dmac_sta_info(void *net_dev, uint32_t *rsp);

#endif /* WAL_CFG_GLUE_H */
