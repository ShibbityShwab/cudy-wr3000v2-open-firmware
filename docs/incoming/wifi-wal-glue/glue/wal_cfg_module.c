/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd.
 * hi5622 wal config-command glue - module dispatch + hipriv-style entry
 * (ORIGINAL text, GPL-2.0).
 *
 * Implements the wal config-command dispatch the vendor binary exposes as
 * wal_wlan_cfg_module_process_entry / _host_process_entry /
 * _device_process_entry (census CSVs in wifi-framework-port/census/,
 * wifi-datapath-slice/mapping/), wired onto the alg_cfg command codec port
 * (wifi-algcfg-slice/codec/, the ORACLE-pinned 17-twin core).
 *
 * The generic framework shape follows the GPL-2.0 hi3881 WLAN framework
 * lineage vendored in wifi-algcfg-slice/vendor/hi3881-wal/ (wal_hipriv.c
 * wal_hipriv_alg_cfg + the wal_send_cfg_event/WLAN_CFGID_* event contract
 * in wal_ioctl.h / wal_event_msg.h).  The hi1105 tree is used locally as a
 * name/structure reference only (no license grant); no hi1105 text is
 * reproduced.  Host-only: nothing is sent to any device - the
 * *_device_process_entry path logs and returns, it never writes a
 * mailbox/doorbell (see wifi-driver-re/REPORT.md s4 for the real device
 * contract that a later transport-enabled slice would use).
 * SPDX-License-Identifier: GPL-2.0
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include "oal_compat.h"
#include "alg_cfg.h"
#include "wal_config_alg_cfg.h"
#include "wal_cfg_glue.h"

/* ------------------------------------------------------------------ */
/* module-level process table: wlan cfg id -> entry + need flags       */
/* ------------------------------------------------------------------ */
static int32_t wal_cfg_cali_process_entry(char *args[WLAN_CFG_MAX_ARGS_NUM],
	uint8_t args_num, uint8_t *msg_type, uint8_t *mem_block,
	uint16_t *mem_len, wlan_cfg_process_info_stru *process_info);
static int32_t wal_cfg_alg_param_legacy_entry(char *args[WLAN_CFG_MAX_ARGS_NUM],
	uint8_t args_num, uint8_t *msg_type, uint8_t *mem_block,
	uint16_t *mem_len, wlan_cfg_process_info_stru *process_info);

static wal_wlan_cfg_process_info_table_stru g_wlan_cfg_process_info_table[] = {
	/* cfg_id                              entry                       need_host w4host need_dev w4dev */
	{ WLAN_CFGID_ALG_ENTRY,  {0, 0}, wal_wlan_cfg_alg_process_entry,
		{ OAL_TRUE,  OAL_TRUE,  OAL_TRUE,  OAL_TRUE } },
	{ WLAN_CFGID_CALI_ENTRY, {0, 0}, wal_cfg_cali_process_entry,
		{ OAL_TRUE,  OAL_TRUE,  OAL_TRUE,  OAL_TRUE } },
	{ WLAN_CFGID_ALG_PARAM,  {0, 0}, wal_cfg_alg_param_legacy_entry,
		{ OAL_FALSE, OAL_FALSE, OAL_TRUE,  OAL_TRUE } },
};

/* cali config-command entry - NOT ported yet (the vendor routes this to
 * the alg_cfg_cali/param-size census families; a later slice).  Kept so
 * the module table carries the same 0x0101/0x0102 dispatch pair the vendor
 * ioctl surface registers (wifi-driver-re/REPORT.md s2.4). */
static int32_t wal_cfg_cali_process_entry(char *args[WLAN_CFG_MAX_ARGS_NUM],
	uint8_t args_num, uint8_t *msg_type, uint8_t *mem_block,
	uint16_t *mem_len, wlan_cfg_process_info_stru *process_info)
{
	if ((msg_type == NULL) || (mem_block == NULL) || (mem_len == NULL)) {
		return OAL_ERR_CODE_PTR_NULL;
	}
	pr_warn("hi5622_wal_cfg: cali entry not ported (argc=%u) - bench-gated slice\n",
		args_num);
	*msg_type = WAL_MSG_TYPE_WRITE;
	*mem_len = 0;
	return OAL_SUCC;
}

/* legacy one-shot alg-param entry (WLAN_CFGID_ALG_PARAM, the hi3881
 * wal_hipriv_alg_cfg event form {en_alg_cfg, value}) - recorded, not
 * executed: the new-framework entry (WLAN_CFGID_ALG_ENTRY) is the path the
 * codec serves. */
static int32_t wal_cfg_alg_param_legacy_entry(char *args[WLAN_CFG_MAX_ARGS_NUM],
	uint8_t args_num, uint8_t *msg_type, uint8_t *mem_block,
	uint16_t *mem_len, wlan_cfg_process_info_stru *process_info)
{
	if ((msg_type == NULL) || (mem_block == NULL) || (mem_len == NULL)) {
		return OAL_ERR_CODE_PTR_NULL;
	}
	pr_warn("hi5622_wal_cfg: legacy ALG_PARAM entry invoked (argc=%u); "
		"use the alg entry (0x%x)\n", args_num, WLAN_CFGID_ALG_ENTRY);
	*msg_type = WAL_MSG_TYPE_WRITE;
	*mem_len = 0;
	return OAL_SUCC;
}

/* module-table row lookup by wlan cfg id (linear scan, framework shape) */
static wal_wlan_cfg_process_info_table_stru *wal_get_wlan_cfg_info(
	wal_wlan_cfg_process_info_table_stru *tbl, uint32_t tbl_size,
	wlan_cfgid_enum_uint16 cfg_id)
{
	uint32_t index;

	for (index = 0; index < tbl_size; index++) {
		if (tbl[index].en_wlan_cfg_id == cfg_id) {
			return &tbl[index];
		}
	}
	return NULL;
}

/* last-executed command record (host-side only; the bench probe replays
 * the vendor CLI and this ring is the local echo of what arrived) */
#define WAL_CFG_LAST_CMD_RING 8
struct wal_cfg_last_cmd {
	wlan_cfgid_enum_uint16 cfg_id;
	uint8_t msg_type;
	uint8_t used;
	uint16_t mem_len;
	uint8_t mem_block[WLAN_CFG_HOST_MAX_RSP_LEN];
};
static struct wal_cfg_last_cmd g_wal_cfg_last_cmd[WAL_CFG_LAST_CMD_RING];
static unsigned int g_wal_cfg_last_cmd_idx;

static void wal_cfg_record_last_cmd(wlan_cfg_param_stru *param, uint8_t msg_type,
	const uint8_t *mem_block, uint16_t mem_len)
{
	struct wal_cfg_last_cmd *slot = &g_wal_cfg_last_cmd[
		g_wal_cfg_last_cmd_idx++ % WAL_CFG_LAST_CMD_RING];

	slot->cfg_id = param->en_wlan_cfg_id;
	slot->msg_type = msg_type;
	slot->used = OAL_TRUE;
	if ((mem_block != NULL) && (mem_len > 0)) {
		slot->mem_len = (mem_len > WLAN_CFG_HOST_MAX_RSP_LEN) ?
			WLAN_CFG_HOST_MAX_RSP_LEN : mem_len;
		memcpy(slot->mem_block, mem_block, slot->mem_len);
	} else {
		slot->mem_len = 0;
	}
}

/* ------------------------------------------------------------------ */
/* the vendor module-command entry names                               */
/* ------------------------------------------------------------------ */

/* wal_wlan_cfg_module_process_entry: wlan配置模块入口函数.  Finds the
 * per-cfg-id process entry, runs it (parse + format into the transfer
 * block), then routes host-side / device-side processing per the need_*
 * flags.  Host-only slice: the device leg logs and does NOT transmit. */
int32_t wal_wlan_cfg_module_process_entry(wlan_cfg_param_stru *wlan_cfg_param)
{
	wal_wlan_cfg_process_info_table_stru *wlan_cfg_process_info;
	wal_cfg_write_msg_stru *write_msg;
	uint8_t msg_type = WAL_MSG_TYPE_WRITE;
	uint16_t mem_block_len = 0;
	int32_t ret;

	if (wlan_cfg_param == NULL) {
		pr_err("hi5622_wal_cfg: module_process_entry NULL param\n");
		return OAL_ERR_CODE_PTR_NULL;
	}

	wlan_cfg_process_info = wal_get_wlan_cfg_info(g_wlan_cfg_process_info_table,
		oal_array_size(g_wlan_cfg_process_info_table),
		wlan_cfg_param->en_wlan_cfg_id);
	if ((wlan_cfg_process_info == NULL) ||
	    (wlan_cfg_process_info->p_func == NULL)) {
		pr_err("hi5622_wal_cfg: cfg id 0x%x has no process entry\n",
			wlan_cfg_param->en_wlan_cfg_id);
		return OAL_ERR_CODE_CFG_PROCESS_ENTRY_NOT_FOUND;
	}

	write_msg = kzalloc(sizeof(*write_msg), GFP_KERNEL);
	if (write_msg == NULL) {
		return -ENOMEM;
	}

	/* 1. per-cfg parser: fills msg_type + transfer mem_block, may refine
	 *    the process-info flags for 2-level (sub-cfg) commands */
	ret = wlan_cfg_process_info->p_func(wlan_cfg_param->apc_args,
		wlan_cfg_param->uc_args_num, &msg_type, write_msg->auc_value,
		&mem_block_len, &wlan_cfg_process_info->st_wlan_cfg_process_info);
	if (ret != OAL_SUCC) {
		wlan_cfg_param->process_ret = (uint32_t)ret;
		kfree(write_msg);
		return ret;
	}

	/* 2. frame the wal write message (12-byte header + payload) */
	write_msg->us_cfg_id = wlan_cfg_param->en_wlan_cfg_id;
	write_msg->us_msg_len = mem_block_len;
	wal_cfg_record_last_cmd(wlan_cfg_param, msg_type, write_msg->auc_value,
		mem_block_len);

	/* 3. need_* dispatch: host leg always runs here; device leg is the
	 *    bench-gated transport (hcc_msg + doorbell per wifi-driver-re) */
	if (wlan_cfg_process_info->st_wlan_cfg_process_info.en_need_host_process) {
		ret = wal_wlan_cfg_module_host_process_entry(wlan_cfg_param, msg_type,
			write_msg->auc_value, mem_block_len);
		if (ret != OAL_SUCC) {
			wlan_cfg_param->process_ret = (uint32_t)ret;
			kfree(write_msg);
			return ret;
		}
	}
	if (wlan_cfg_process_info->st_wlan_cfg_process_info.en_need_device_process) {
		ret = wal_wlan_cfg_module_device_process_entry(wlan_cfg_param, msg_type,
			write_msg->auc_value, mem_block_len);
		if (ret != OAL_SUCC) {
			wlan_cfg_param->process_ret = (uint32_t)ret;
			kfree(write_msg);
			return ret;
		}
	}

	wlan_cfg_param->process_ret = OAL_SUCC;
	kfree(write_msg);
	return OAL_SUCC;
}

/* host-side processing leg.  In the vendor this consumes the parsed block
 * in the host thread context (need_host_process rows, e.g. the 1400-range
 * host-only ids per the algcfg census).  Here: record + echo the block. */
int32_t wal_wlan_cfg_module_host_process_entry(wlan_cfg_param_stru *wlan_cfg_param,
	uint8_t msg_type, uint8_t *mem_block, uint16_t mem_len)
{
	if (wlan_cfg_param == NULL) {
		return OAL_ERR_CODE_PTR_NULL;
	}
	pr_info("hi5622_wal_cfg: host process cfg_id=0x%x type=%u len=%u (stub)\n",
		wlan_cfg_param->en_wlan_cfg_id, msg_type, mem_len);
	if ((wlan_cfg_param->pc_output != NULL) && (wlan_cfg_param->us_output_len > 0)) {
		/* GET formatters are part of the 120-entry hi5622 parser families
		 * (alg_*_config_param_output_entry) - bench-gated; leave the
		 * response empty and log the gap. */
		snprintf_s(wlan_cfg_param->pc_output, wlan_cfg_param->us_output_len,
			(uint32_t)strlen("GET output formatter not ported\n"),
			"GET output formatter not ported\n");
		wlan_cfg_param->us_output_len = 0;
	}
	return OAL_SUCC;
}

/* device-side processing leg.  Host-only slice: the vendor would frame the
 * hcc_msg {type,bid,vap_id,chip_id,flag,id,rsvd,dst,params[]} header
 * (wifi-driver-re s3.1) and hand it to hmac_send_msg -> hcc_msg_tx ->
 * pcie_msg_send_irq (s3.4).  We log the intent and never touch hardware. */
int32_t wal_wlan_cfg_module_device_process_entry(wlan_cfg_param_stru *wlan_cfg_param,
	uint8_t msg_type, uint8_t *mem_block, uint16_t mem_len)
{
	if (wlan_cfg_param == NULL) {
		return OAL_ERR_CODE_PTR_NULL;
	}
	pr_info("hi5622_wal_cfg: device process cfg_id=0x%x type=%u len=%u "
		"NOT SENT (host-only slice; transport is bench-gated)\n",
		wlan_cfg_param->en_wlan_cfg_id, msg_type, mem_len);
	return OAL_SUCC;
}

/* ------------------------------------------------------------------ */
/* hipriv-style alg_cfg entry (vendored hi3881 wal_hipriv.c base shape) */
/* ------------------------------------------------------------------ */

/* split a command line into space-separated tokens (in place) */
uint8_t wal_cfg_split_args(char *line, char *args[], uint8_t max_args)
{
	uint8_t count = 0;

	if (line == NULL) {
		return 0;
	}
	while (*line != '\0' && count < max_args) {
		while (*line == ' ' || *line == '\t' || *line == '\n') {
			line++;
		}
		if (*line == '\0') {
			break;
		}
		args[count++] = line;
		while (*line != '\0' && *line != ' ' && *line != '\t' && *line != '\n') {
			line++;
		}
		if (*line != '\0') {
			*line++ = '\0';
		}
	}
	return count;
}

/* name -> cfg id, used to fill the exported g_wal_hipriv_alg_cfg_get_cfg_id
 * slot (the vendor exports this exact name through __ksymtab). */
uint32_t wal_hipriv_alg_cfg_get_cfg_id(const char *cmd_name,
	mac_alg_cfg_enum_uint16 *cfg_id)
{
	alg_cfg_process_info_stru *info;

	if ((cmd_name == NULL) || (cfg_id == NULL)) {
		return OAL_ERR_CODE_PTR_NULL;
	}
	info = alg_cfg_search_process_info_by_cfg_name(cmd_name);
	if (info == NULL) {
		return OAL_ERR_CODE_ALG_PROCESS_INFO_NOT_FOUND;
	}
	*cfg_id = info->en_alg_cfg_id;
	return OAL_SUCC;
}

uint32_t wal_hipriv_alg_cfg_get_cfg_id_normal(const char *cmd_name,
	uint32_t arg_size, mac_alg_cfg_enum_uint16 *cfg_id)
{
	alg_cfg_process_info_stru *info = NULL;

	if ((cmd_name == NULL) || (cfg_id == NULL)) {
		return OAL_ERR_CODE_PTR_NULL;
	}
	alg_cfg_search_process_info_by_cfg_name_normal(cmd_name, arg_size, &info);
	if (info == NULL) {
		return OAL_ERR_CODE_ALG_PROCESS_INFO_NOT_FOUND;
	}
	*cfg_id = info->en_alg_cfg_id;
	return OAL_SUCC;
}

/* wal_hipriv_alg_cfg: hipriv-style entry, "hipriv "vap0 alg_cfg NAME args""
 * (hi3881 form).  Parses the command name, looks the process-info row up in
 * the codec table, runs the codec entry and records the result.  SET/GET of
 * a census row whose parser family is not ported returns the vendor's
 * 1017 (SUB_ALG_ANALYSIS_ENTRY_NOT_FOUND), exactly as the codec does. */
uint32_t wal_hipriv_alg_cfg(void *net_dev, char *pc_param, char *out,
	uint32_t out_len)
{
	char *args[WLAN_CFG_MAX_ARGS_NUM];
	wlan_cfg_param_stru cfg_param;
	uint8_t args_num;
	uint32_t ret;

	if ((pc_param == NULL) || (out == NULL) || (out_len == 0)) {
		return OAL_ERR_CODE_PTR_NULL;
	}
	memset(&cfg_param, 0, sizeof(cfg_param));

	/* split: first token = command name, rest = args */
	args_num = wal_cfg_split_args(pc_param, args, WLAN_CFG_MAX_ARGS_NUM);
	if (args_num < 1) {
		pr_warn("hi5622_wal_cfg: alg_cfg: no command name\n");
		return OAL_FAIL;
	}

	/* command name must be a process-info row (census placeholders have
	 * hi5622_cmd_<id>_<s|g> names until the bench name harvest) */
	if (alg_cfg_search_process_info_by_cfg_name(args[0]) == NULL) {
		pr_warn("hi5622_wal_cfg: alg_cfg: invalid command [%s]\n", args[0]);
		snprintf_s(out, out_len, (uint32_t)strlen("ERROR: invalid alg_cfg command\r\n"),
			"ERROR: invalid alg_cfg command\r\n");
		return OAL_ERR_CODE_ALG_PROCESS_INFO_NOT_FOUND;
	}

	cfg_param.pst_net_dev = net_dev;
	cfg_param.en_wlan_cfg_id = WLAN_CFGID_ALG_ENTRY;
	cfg_param.uc_args_num = args_num;
	{
		uint8_t i;
		for (i = 0; i < args_num; i++) {
			cfg_param.apc_args[i] = args[i];
		}
	}
	cfg_param.pc_output = out;
	cfg_param.us_output_len = (uint16_t)out_len;

	ret = (uint32_t)wal_wlan_cfg_module_process_entry(&cfg_param);
	if (ret != OAL_SUCC) {
		snprintf_s(out, out_len, 64, "ERROR: %u\r\n", ret);
		return ret;
	}

	snprintf_s(out, out_len, (uint32_t)strlen("OK\r\n"), "OK\r\n");
	return OAL_SUCC;
}

/* ------------------------------------------------------------------ */
/* wal message plumbing stubs (driver-re s3.7 semantics, no device)    */
/* ------------------------------------------------------------------ */

/* request/response pairing ring - mirrors wal_msg_request_add_queue /
 * wal_set_dev_msg_response_by_addr / wal_check_and_release_msg_resp.
 * Request-id word convention (driver-re s3.7): 0x0d01xxxx, 2g/5g variants. */
#define WAL_CFG_REQ_RING 16
struct wal_cfg_req_slot {
	uint32_t req_id;
	void *rsp;
	uint32_t len;
	uint8_t used;
};
static struct wal_cfg_req_slot g_wal_cfg_req_ring[WAL_CFG_REQ_RING];

int32_t wal_msg_request_add_queue(uint32_t req_id, void *rsp)
{
	unsigned int i;

	for (i = 0; i < WAL_CFG_REQ_RING; i++) {
		if (!g_wal_cfg_req_ring[i].used) {
			g_wal_cfg_req_ring[i].req_id = req_id;
			g_wal_cfg_req_ring[i].rsp = rsp;
			g_wal_cfg_req_ring[i].len = 0;
			g_wal_cfg_req_ring[i].used = 1;
			return OAL_SUCC;
		}
	}
	pr_warn("hi5622_wal_cfg: req ring full\n");
	return OAL_FAIL;
}

int32_t wal_msg_request_remove_queue(uint32_t req_id)
{
	unsigned int i;

	for (i = 0; i < WAL_CFG_REQ_RING; i++) {
		if (g_wal_cfg_req_ring[i].used &&
		    (g_wal_cfg_req_ring[i].req_id == req_id)) {
			g_wal_cfg_req_ring[i].used = 0;
			return OAL_SUCC;
		}
	}
	return OAL_FAIL;
}

int32_t wal_set_dev_msg_response_by_addr(uint32_t req_id, void *addr, uint32_t len)
{
	unsigned int i;

	for (i = 0; i < WAL_CFG_REQ_RING; i++) {
		if (g_wal_cfg_req_ring[i].used &&
		    (g_wal_cfg_req_ring[i].req_id == req_id)) {
			if (g_wal_cfg_req_ring[i].rsp != NULL) {
				memcpy(g_wal_cfg_req_ring[i].rsp, addr, len);
			}
			g_wal_cfg_req_ring[i].len = len;
			return OAL_SUCC;
		}
	}
	return OAL_FAIL;
}

int32_t wal_check_and_release_msg_resp(uint32_t req_id)
{
	int32_t ret = wal_msg_request_remove_queue(req_id);

	if (ret != OAL_SUCC) {
		pr_warn("hi5622_wal_cfg: no pending request 0x%x\n", req_id);
	}
	return ret;
}

/* config-event process entry family (wal_config_process_*): the vendor's
 * per-event hooks on the wal config path; stubs record + echo. */
int32_t wal_config_process_entry(void *net_dev, uint8_t msg_type, uint16_t len,
	uint8_t *param)
{
	pr_info("hi5622_wal_cfg: wal_config_process_entry type=%u len=%u (stub)\n",
		msg_type, len);
	return OAL_SUCC;
}

int32_t wal_config_process_pkt(void *net_dev, uint8_t msg_type, uint16_t len,
	uint8_t *param)
{
	return wal_config_process_entry(net_dev, msg_type, len, param);
}

int32_t wal_config_process_pkt_memcpy_proc(uint8_t *dst, uint16_t dst_len,
	const uint8_t *src, uint16_t src_len)
{
	if ((dst == NULL) || (src == NULL) || (dst_len < src_len)) {
		return OAL_FAIL;
	}
	memcpy(dst, src, src_len);
	return OAL_SUCC;
}

int32_t wal_config_process_recently_cfg_event(void)
{
	pr_info("hi5622_wal_cfg: recently-cfg-event replay (stub)\n");
	return OAL_SUCC;
}

int32_t wal_sync_query_dmac_sta_info(void *net_dev, uint32_t *rsp)
{
	if (rsp != NULL) {
		*rsp = 0;
	}
	pr_warn("hi5622_wal_cfg: wal_sync_query_dmac_sta_info (stub, no device)\n");
	return OAL_FAIL;
}
