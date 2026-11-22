/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd.
 * hi5622 wal config-command glue - 6.18 front end (ORIGINAL text, GPL-2.0).
 *
 * Module skeleton of the wal config-command front end: it links the
 * alg_cfg command codec (wifi-algcfg-slice/codec/) and exposes
 *
 *   1. a hipriv-style sysfs entry (/sys/kernel/wal_cfg/cmd) accepting the
 *      vendor command-line forms "alg_cfg NAME ARGS..." and
 *      "CFG_ID ARGS..." (the hi3881 hipriv surface vendored in
 *      wifi-algcfg-slice/vendor/hi3881-wal/),
 *   2. a cfg80211 vendor-command registration helper (the future real
 *      driver calls wal_cfg_register_vendor_commands(wiphy); nothing is
 *      registered by this module itself - host-only, no fake wiphy),
 *   3. the vendor-exact g_alg_* / alg_* export surface (__ksymtab names
 *      from wifi-framework-port/census/hi5622v100_wifi.ksymtab.txt).
 *
 * Host-only research module: no device is touched, no message is sent.
 * SPDX-License-Identifier: GPL-2.0
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <net/cfg80211.h>
#include "oal_compat.h"
#include "alg_cfg.h"
#include "wal_config_alg_cfg.h"
#include "wal_cfg_glue.h"

/* ------------------------------------------------------------------ */
/* the vendor-exported g_alg_* data surface (8 exact + 1 continuity)   */
/* ------------------------------------------------------------------ */
alg_cfg_process_info_stru *(*g_alg_cfg_process_find_by_cfg_id)(
	mac_alg_cfg_enum_uint16, alg_cfg_type_enum_uint8) =
	alg_cfg_search_process_info_by_cfg_id;
alg_cfg_process_info_stru *(*g_alg_cfg_process_find_by_name)(const char *) =
	alg_cfg_search_process_info_by_cfg_name;
void *(*g_alg_cfg_search_alg_debug_lut)(void) = wal_cfg_search_alg_debug_lut;
const char *(*g_alg_cfg_search_dbg_cfg_name_by_cfg_id)(mac_alg_cfg_enum_uint16) =
	wal_cfg_search_dbg_cfg_name_by_cfg_id;
uint32_t (*g_wal_hipriv_alg_cfg_get_cfg_id)(const char *,
	mac_alg_cfg_enum_uint16 *) = wal_hipriv_alg_cfg_get_cfg_id;

uint8_t g_alg_cfg_bool_tbl_size = 2;
uint8_t g_alg_cfg_tx_mode_tbl_size = ALG_CFG_TX_MODE_BUTT; /* continuity extra */
uint32_t g_alg_cfg_snd_fback_type_tbl_size = 2;   /* binary 40-row type 16 */
uint32_t g_alg_cfg_snd_user_mat_type_tbl_size = 4; /* binary type 19 */
uint32_t g_alg_cfg_su_aggr_mode_tbl_size = 3;     /* binary type 27 */

/* ------------------------------------------------------------------ */
/* vendor-exported alg_ functions still on the 120-entry port list     */
/* (stubs with the vendor names; semantics bench-gated, see probes)    */
/* ------------------------------------------------------------------ */
/* match-string helper (trigger formatter family); PROVISIONAL shape
 * (args, match, match_len, out) per wifi-datapath-slice hi5622_alg.h. */
uint32_t alg_cfg_get_match_string(const char *args, const char *match,
	uint32_t match_len, uint32_t *out)
{
	if ((args == NULL) || (match == NULL) || (out == NULL)) {
		return OAL_ERR_CODE_PTR_NULL;
	}
	if (strncmp(args, match, match_len) == 0) {
		*out = 0;
		return OAL_SUCC;
	}
	pr_warn("hi5622_wal_cfg: alg_cfg_get_match_string (stub, bench-gated)\n");
	return OAL_FAIL;
}

/* trailing-number parser; PROVISIONAL shape (args, out). */
uint32_t alg_cfg_get_tail_num(const char *args, uint32_t *out)
{
	if ((args == NULL) || (out == NULL)) {
		return OAL_ERR_CODE_PTR_NULL;
	}
	/* simplified: numeric tail after the last space */
	{
		const char *tail = strrchr(args, ' ');

		if (tail == NULL) {
			tail = args;
		} else {
			tail++;
		}
		*out = (uint32_t)oal_atoi(tail);
	}
	return OAL_SUCC;
}

/* generic "alg cfg ID ARGS" tail parser (vendor-exported); the codec
 * table drives per-cfg parsing - stub logs and defers. */
uint32_t alg_cfg_args_analysis_param(const char *args)
{
	pr_warn("hi5622_wal_cfg: alg_cfg_args_analysis_param (stub, bench-gated)\n");
	return OAL_ERR_CODE_SUB_ALG_ANALYSIS_ENTRY_NOT_FOUND;
}

/* hi5622-specific equipment-parameter parser (vendor-exported); stub. */
uint32_t alg_cfg_args_param_analyse_equipment_param(uintptr_t a0)
{
	pr_warn("hi5622_wal_cfg: alg_cfg_args_param_analyse_equipment_param "
		"(stub, bench-gated)\n");
	return OAL_ERR_CODE_SUB_ALG_ANALYSIS_ENTRY_NOT_FOUND;
}

/* ------------------------------------------------------------------ */
/* command execution (shared by sysfs + vendor-cmd paths)              */
/* ------------------------------------------------------------------ */
#define WAL_CFG_RSP_BUF_LEN WLAN_CFG_HOST_MAX_RSP_LEN

/* execute one command line.  Forms:
 *   "help"                      - command list
 *   "alg_cfg NAME ARGS..."      - hipriv alg_cfg entry (name in table)
 *   "alg NAME ARGS..."          - same
 *   "257 ARGS..." / "0x0101 ARGS..." - module entry by wlan cfg id
 *   "cali ..."                  - cali entry (not ported)
 * Returns the vendor error code (0 = OK); response text in out. */
int wal_cfg_exec_command_line(const char *cmd, char *out, uint32_t out_len)
{
	char *args[WLAN_CFG_MAX_ARGS_NUM];
	wlan_cfg_param_stru param;
	char *line;
	uint8_t args_num;
	uint32_t cfg_id;
	int ret = OAL_SUCC;

	if ((cmd == NULL) || (out == NULL) || (out_len == 0)) {
		return OAL_ERR_CODE_PTR_NULL;
	}
	if (strlen(cmd) >= WAL_CFG_CMD_MAX_LEN) {
		snprintf_s(out, out_len, 64, "ERROR: command too long\r\n");
		return OAL_ERR_CODE_ARRAY_OVERFLOW;
	}
	line = kzalloc(WAL_CFG_CMD_MAX_LEN, GFP_KERNEL);
	if (line == NULL) {
		return -ENOMEM;
	}
	memcpy(line, cmd, strlen(cmd) + 1);
	out[0] = '\0';

	args_num = wal_cfg_split_args(line, args, WLAN_CFG_MAX_ARGS_NUM);
	if (args_num == 0) {
		goto out;
	}

	if (strcmp(args[0], "help") == 0) {
		snprintf_s(out, out_len, 512,
			"wal_cfg front end (hi5622 codec, host-only)\n"
			"  help\n"
			"  alg_cfg NAME ARGS...   (hipriv form)\n"
			"  CFG_ID ARGS...         (module entry: 257 alg, 258 cali)\n");
		goto out;
	}

	/* strip the "alg_cfg" / "alg" prefix and run the hipriv path */
	if ((strcmp(args[0], "alg_cfg") == 0) || (strcmp(args[0], "alg") == 0)) {
		/* rebuild the remaining tokens into one string */
		uint32_t pos = 0;
		uint8_t i;

		for (i = 1; i < args_num; i++) {
			uint32_t l = (uint32_t)strlen(args[i]);

			if (pos + l + 2 > WAL_CFG_CMD_MAX_LEN) {
				break;
			}
			if (pos > 0) {
				line[pos++] = ' ';
			}
			memcpy(line + pos, args[i], l);
			pos += l;
			line[pos] = '\0';
		}
		ret = (int)wal_hipriv_alg_cfg(NULL, line, out, out_len);
		goto out;
	}

	/* "cali" prefix -> module cali entry */
	if (strcmp(args[0], "cali") == 0) {
		memset(&param, 0, sizeof(param));
		param.en_wlan_cfg_id = WLAN_CFGID_CALI_ENTRY;
		param.uc_args_num = (uint8_t)(args_num - 1);
		{
			uint8_t i;
			for (i = 0; i < param.uc_args_num && i < WLAN_CFG_MAX_ARGS_NUM; i++) {
				param.apc_args[i] = args[i + 1];
			}
		}
		param.pc_output = out;
		param.us_output_len = (uint16_t)out_len;
		ret = wal_wlan_cfg_module_process_entry(&param);
		if (ret != OAL_SUCC) {
			snprintf_s(out, out_len, 64, "ERROR: %d\r\n", ret);
		}
		goto out;
	}

	/* numeric first token -> module entry by cfg id */
	if ((args[0][0] >= '0' && args[0][0] <= '9') ||
	    ((args[0][0] == '0') && (args[0][1] == 'x' || args[0][1] == 'X'))) {
		if ((args[0][0] == '0') && (args[0][1] == 'x' || args[0][1] == 'X')) {
			if (kstrtou32(args[0] + 2, 16, &cfg_id) != 0) {
				snprintf_s(out, out_len, 64, "ERROR: bad cfg id\r\n");
				ret = OAL_FAIL;
				goto out;
			}
		} else if (kstrtou32(args[0], 10, &cfg_id) != 0) {
			snprintf_s(out, out_len, 64, "ERROR: bad cfg id\r\n");
			ret = OAL_FAIL;
			goto out;
		}

		memset(&param, 0, sizeof(param));
		param.en_wlan_cfg_id = (wlan_cfgid_enum_uint16)cfg_id;
		param.uc_args_num = (uint8_t)(args_num - 1);
		{
			uint8_t i;
			for (i = 0; i < param.uc_args_num && i < WLAN_CFG_MAX_ARGS_NUM; i++) {
				param.apc_args[i] = args[i + 1];
			}
		}
		param.pc_output = out;
		param.us_output_len = (uint16_t)out_len;
		ret = wal_wlan_cfg_module_process_entry(&param);
		if (ret != OAL_SUCC) {
			snprintf_s(out, out_len, 64, "ERROR: %d\r\n", ret);
		}
		goto out;
	}

	/* default: bare command name -> hipriv alg_cfg form */
	ret = (int)wal_hipriv_alg_cfg(NULL, line, out, out_len);

out:
	kfree(line);
	return ret;
}

/* ------------------------------------------------------------------ */
/* cfg80211 vendor-command surface (registration helper, not auto-hooked) */
/* ------------------------------------------------------------------ */
/* Research placeholder OUI - the vendor driver registers NO nl80211
 * vendor commands (its private surface is iwpriv/hipriv, wifi-driver-re
 * s2.4), so this helper is an API surface for the future ported driver.
 * Replace with an assigned OUI before any real registration. */
#define WAL_CFG_VENDOR_OUI 0x001122U
#define WAL_CFG_SUBCMD_SET  1
#define WAL_CFG_SUBCMD_GET  2

static int wal_cfg_vendor_cmd_doit(struct wiphy *wiphy, struct wireless_dev *wdev,
	const void *data, int data_len)
{
	struct sk_buff *reply;
	char *rsp;
	char *cmd;
	int ret;

	if ((data == NULL) || (data_len <= 0)) {
		return -EINVAL;
	}
	/* payload = the config command line (our convention) */
	cmd = kzalloc(data_len + 1, GFP_KERNEL);
	rsp = kzalloc(WAL_CFG_RSP_BUF_LEN, GFP_KERNEL);
	if ((cmd == NULL) || (rsp == NULL)) {
		kfree(cmd);
		kfree(rsp);
		return -ENOMEM;
	}
	memcpy(cmd, data, data_len);
	cmd[data_len] = '\0';

	ret = wal_cfg_exec_command_line(cmd, rsp, WAL_CFG_RSP_BUF_LEN);
	kfree(cmd);

	reply = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, strlen(rsp) + 8);
	if (reply == NULL) {
		kfree(rsp);
		return -ENOMEM;
	}
	skb_put_data(reply, rsp, strlen(rsp) + 1);
	kfree(rsp);
	/* nl80211 doit returns errno only; the vendor error code travels in
	 * the reply text (see wal_cfg_exec_command_line) */
	if (ret != 0) {
		ret = -EINVAL;
	}
	return cfg80211_vendor_cmd_reply(reply) ? -EINVAL : ret;
}

static const struct wiphy_vendor_command wal_cfg_vendor_commands[] = {
	{
		.info = { .vendor_id = WAL_CFG_VENDOR_OUI, .subcmd = WAL_CFG_SUBCMD_SET },
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV,
		.doit = wal_cfg_vendor_cmd_doit,
		.policy = VENDOR_CMD_RAW_DATA,
		.maxattr = 0,
	},
	{
		.info = { .vendor_id = WAL_CFG_VENDOR_OUI, .subcmd = WAL_CFG_SUBCMD_GET },
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV,
		.doit = wal_cfg_vendor_cmd_doit,
		.policy = VENDOR_CMD_RAW_DATA,
		.maxattr = 0,
	},
};

int wal_cfg_register_vendor_commands(struct wiphy *wiphy)
{
	if (wiphy == NULL) {
		return -EINVAL;
	}
	if (wiphy->vendor_commands != NULL) {
		return -EBUSY;
	}
	wiphy->vendor_commands = wal_cfg_vendor_commands;
	wiphy->n_vendor_commands = oal_array_size(wal_cfg_vendor_commands);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sysfs surface: /sys/kernel/wal_cfg/                                 */
/* ------------------------------------------------------------------ */
static struct kobject *g_wal_cfg_kobj;
static char g_wal_cfg_last_rsp[WAL_CFG_RSP_BUF_LEN];

static ssize_t wal_cfg_cmd_show(struct kobject *kobj, struct kobj_attribute *attr,
	char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s", g_wal_cfg_last_rsp);
}

static ssize_t wal_cfg_cmd_store(struct kobject *kobj, struct kobj_attribute *attr,
	const char *buf, size_t count)
{
	char *cmd;
	size_t len = count;

	if (len >= WAL_CFG_CMD_MAX_LEN) {
		len = WAL_CFG_CMD_MAX_LEN - 1;
	}
	cmd = kzalloc(len + 1, GFP_KERNEL);
	if (cmd == NULL) {
		return -ENOMEM;
	}
	memcpy(cmd, buf, len);
	cmd[len] = '\0';
	/* strip trailing newline */
	while (len > 0 && (cmd[len - 1] == '\n' || cmd[len - 1] == '\r')) {
		cmd[--len] = '\0';
	}

	g_wal_cfg_last_rsp[0] = '\0';
	wal_cfg_exec_command_line(cmd, g_wal_cfg_last_rsp, sizeof(g_wal_cfg_last_rsp));
	if (g_wal_cfg_last_rsp[0] == '\0') {
		strcpy(g_wal_cfg_last_rsp, "OK\r\n");
	}
	kfree(cmd);
	return count;
}

static ssize_t help_show(struct kobject *kobj, struct kobj_attribute *attr,
	char *buf)
{
	return scnprintf(buf, PAGE_SIZE,
		"wal_cfg front end (hi5622 alg_cfg codec, host-only)\n"
		"  cmd          write: alg_cfg NAME ARGS... | CFG_ID ARGS... | cali ...\n"
		"  process_table read: 414-row process-info dump\n"
		"  lut          read: 416-entry 176-B LUT dump\n"
		"  layout       read: vendor runtime layout descriptor\n"
		"  param_size_tbl read: 40-row param-size dump\n"
		"  fixup_add    write: ROW_IDX NAME  (bench name fill)\n"
		"  fixup_list   read: applied fixups\n");
}

static ssize_t process_table_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	return wal_cfg_process_table_dump(buf, PAGE_SIZE);
}

static ssize_t lut_show(struct kobject *kobj, struct kobj_attribute *attr,
	char *buf)
{
	return wal_cfg_lut_dump(buf, PAGE_SIZE);
}

static ssize_t layout_show(struct kobject *kobj, struct kobj_attribute *attr,
	char *buf)
{
	const struct wal_cfg_runtime_layout *l = wal_cfg_runtime_layout();

	return scnprintf(buf, PAGE_SIZE,
		"process_tbl:     .data+0x%x  %u x %u B\n"
		"param_size_tbl:  .data+0x%x  %u x 4 B\n"
		"bool_tbl:        .data+0x%x\n"
		"pkt_type_tbl:    .data+0x%x\n"
		"tx_mode_tbl:     .data+0x%x\n"
		"alg_lut:         .data+0x%x  %u x %u B\n"
		"fixup_list:      .data+0x%x  %u 8-B pairs\n"
		"name_vocab:      .rodata+0x%x\n",
		l->process_tbl_off, l->process_tbl_entries, l->process_entry_size,
		l->param_size_tbl_off, l->param_size_entries,
		l->bool_tbl_off, l->pkt_type_tbl_off, l->tx_mode_tbl_off,
		l->alg_lut_off, l->alg_lut_entries, l->alg_lut_entry_size,
		l->fixup_list_off, l->fixup_pairs, l->name_vocab_off);
}

static ssize_t param_size_tbl_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	return wal_cfg_param_size_tbl_dump(buf, PAGE_SIZE);
}

static ssize_t fixup_add_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	char line[128];
	unsigned int idx;
	char *name;
	int ret;

	if (count >= sizeof(line)) {
		return -EINVAL;
	}
	memcpy(line, buf, count);
	line[count] = '\0';
	name = strchr(line, ' ');
	if (name == NULL) {
		return -EINVAL;
	}
	*name++ = '\0';
	if (kstrtouint(line, 10, &idx) != 0) {
		return -EINVAL;
	}
	/* strip newline from name */
	{
		size_t l = strlen(name);
		while (l > 0 && (name[l - 1] == '\n' || name[l - 1] == '\r')) {
			name[--l] = '\0';
		}
	}
	ret = wal_cfg_name_fixup_add(idx, name);
	return (ret == OAL_SUCC) ? (ssize_t)count : -EINVAL;
}

static ssize_t fixup_list_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	uint32_t i;
	int off = 0;

	off += scnprintf(buf + off, PAGE_SIZE - off, "applied fixups: %u\n",
		wal_cfg_fixup_count());
	for (i = 0; i < wal_cfg_fixup_count(); i++) {
		const char *name = wal_cfg_fixup_name(i);

		if (name == NULL) {
			name = "(null)";
		}
		off += scnprintf(buf + off, PAGE_SIZE - off, "%u %s\n",
			wal_cfg_fixup_target(i), name);
	}
	return off;
}

static struct kobj_attribute wal_cfg_cmd_attr = __ATTR(cmd, 0644,
	wal_cfg_cmd_show, wal_cfg_cmd_store);
static struct kobj_attribute wal_cfg_help_attr = __ATTR_RO(help);
static struct kobj_attribute wal_cfg_process_table_attr = __ATTR_RO(process_table);
static struct kobj_attribute wal_cfg_lut_attr = __ATTR_RO(lut);
static struct kobj_attribute wal_cfg_layout_attr = __ATTR_RO(layout);
static struct kobj_attribute wal_cfg_param_size_tbl_attr = __ATTR_RO(param_size_tbl);
static struct kobj_attribute wal_cfg_fixup_add_attr = __ATTR_WO(fixup_add);
static struct kobj_attribute wal_cfg_fixup_list_attr = __ATTR_RO(fixup_list);

static struct attribute *wal_cfg_attrs[] = {
	&wal_cfg_cmd_attr.attr,
	&wal_cfg_help_attr.attr,
	&wal_cfg_process_table_attr.attr,
	&wal_cfg_lut_attr.attr,
	&wal_cfg_layout_attr.attr,
	&wal_cfg_param_size_tbl_attr.attr,
	&wal_cfg_fixup_add_attr.attr,
	&wal_cfg_fixup_list_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(wal_cfg);

/* ------------------------------------------------------------------ */
/* module init/exit                                                    */
/* ------------------------------------------------------------------ */
static int __init wal_cfg_init(void)
{
	int ret;

	ret = wal_cfg_lut_init();
	if (ret != 0) {
		return ret;
	}
	ret = hmac_cfg_stubs_register();
	if (ret != 0) {
		pr_warn("hi5622_wal_cfg: hmac cfg stub registration failed %d\n", ret);
	}
	g_wal_cfg_kobj = kobject_create_and_add("wal_cfg", kernel_kobj);
	if (g_wal_cfg_kobj == NULL) {
		wal_cfg_lut_deinit();
		return -ENOMEM;
	}
	ret = sysfs_create_groups(g_wal_cfg_kobj, wal_cfg_groups);
	if (ret != 0) {
		kobject_put(g_wal_cfg_kobj);
		wal_cfg_lut_deinit();
		return ret;
	}
	pr_info("hi5622_wal_cfg: wal config-command front end ready "
		"(codec process table %u, LUT %u x %u B, sysfs /sys/kernel/wal_cfg)\n",
		alg_cfg_process_tbl_size(),
		wal_cfg_runtime_layout()->alg_lut_entries,
		wal_cfg_runtime_layout()->alg_lut_entry_size);
	return 0;
}

static void __exit wal_cfg_exit(void)
{
	sysfs_remove_groups(g_wal_cfg_kobj, wal_cfg_groups);
	kobject_put(g_wal_cfg_kobj);
	wal_cfg_lut_deinit();
	pr_info("hi5622_wal_cfg: front end removed\n");
}

module_init(wal_cfg_init);
module_exit(wal_cfg_exit);
MODULE_LICENSE("GPL");
/* MODULE_INFO(description, ...) instead of MODULE_DESCRIPTION(): the luofu
 * OpenWrt tree sets CONFIG_MODULE_STRIPPED, which disables the latter
 * (and makes modpost warn). */
MODULE_INFO(description, "hi5622 wal config-command front end (alg_cfg codec glue, 6.18)");

/* ------------------------------------------------------------------ */
/* export surface                                                      */
/* ------------------------------------------------------------------ */
EXPORT_SYMBOL(g_alg_cfg_process_find_by_cfg_id);
EXPORT_SYMBOL(g_alg_cfg_process_find_by_name);
EXPORT_SYMBOL(g_alg_cfg_search_alg_debug_lut);
EXPORT_SYMBOL(g_alg_cfg_search_dbg_cfg_name_by_cfg_id);
EXPORT_SYMBOL(g_alg_cfg_bool_tbl_size);
EXPORT_SYMBOL(g_alg_cfg_tx_mode_tbl_size);
EXPORT_SYMBOL(g_alg_cfg_snd_fback_type_tbl_size);
EXPORT_SYMBOL(g_alg_cfg_snd_user_mat_type_tbl_size);
EXPORT_SYMBOL(g_alg_cfg_su_aggr_mode_tbl_size);
EXPORT_SYMBOL(g_wal_hipriv_alg_cfg_get_cfg_id);
/* vendor-exported alg_ functions (4 stubs + 2 codec impls) */
EXPORT_SYMBOL(alg_cfg_get_match_string);
EXPORT_SYMBOL(alg_cfg_get_tail_num);
EXPORT_SYMBOL(alg_cfg_args_analysis_param);
EXPORT_SYMBOL(alg_cfg_args_param_analyse_equipment_param);
EXPORT_SYMBOL(alg_cfg_get_match_value);
EXPORT_SYMBOL(alg_cfg_param_get_tbl_size);
/* glue entry points */
EXPORT_SYMBOL(wal_wlan_cfg_module_process_entry);
EXPORT_SYMBOL(wal_wlan_cfg_module_host_process_entry);
EXPORT_SYMBOL(wal_wlan_cfg_module_device_process_entry);
EXPORT_SYMBOL(wal_hipriv_alg_cfg);
EXPORT_SYMBOL(wal_hipriv_alg_cfg_get_cfg_id);
EXPORT_SYMBOL(wal_cfg_register_vendor_commands);
