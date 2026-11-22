/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd.
 * hi5622 wal config-command glue - LUT builder + name-fixup loader
 * (ORIGINAL text, GPL-2.0).
 *
 * Implements the runtime-table-layout machinery documented in
 * wifi-algcfg-slice/REPORT.md section 3 for the vendor hi5622v100_wifi.ko:
 *
 *   g_ast_alg_cfg_process_info_table  .data + 0x35c8  414 x 12 B
 *   g_ast_alg_cfg_param_size_tbl      .data + 0x2d9c  40 x 4 B
 *   g_ast_alg_cfg_pkt_type_info_table .data + 0x2e4c  13 x 8 B
 *   g_ast_alg_cfg_bool_tbl            .data + 0x2e3c  2 x 8 B
 *   g_ast_alg_cfg_tx_mode_tbl         .data + 0x301c  13 x 8 B
 *   g_alg_cfg_lut                     .data + 0x4934  416 x 176 B
 *       key u32 = (type<<16)|id, same order as the process table;
 *       verified fields: +12 msg_flags 0x000b0001 SET / 0x000b0000 GET,
 *       +16 need_flags 0x00010101 on SET, +20 fixed_field 0x0a;
 *       the remaining descriptors are populated at runtime by the vendor
 *       fixup path (the .data fixup list below).
 *   custom fixup list                 .data + 0x0a9874  4608 8-B pairs
 *       {target_slot_offset, value} - patches the process-table NAME
 *       slots and the LUT fn/aux fields at runtime; the standard
 *       .rel.data has no entries for those slots.
 *   name vocabulary                   .rodata + 0x23a680  (txmode_*,
 *       ofdma_*, spectral_*, sounding_*, trigger_*, ersru_*, smartant_*)
 *
 * This loader builds the same shape host-side from the codec's ported
 * tables, keeps the vendor layout descriptor as data, and applies the
 * name/LUT fixups.  The 4608 vendor pairs are decoded on the bench
 * (probes/bench-probes.md); until then the compiled-in fixup list is
 * empty and census rows keep their placeholder names.
 * SPDX-License-Identifier: GPL-2.0
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include "oal_compat.h"
#include "alg_cfg.h"
#include "wal_config_alg_cfg.h"
#include "wal_cfg_glue.h"

/* the codec's twin-subset parser dispatch table size (17 rows) */
#define WAL_CFG_SUB_ALG_ARGS_ANALYSIS_TBL_SIZE 17

/* compiled-in fixup list: decoded vendor pairs land here after the bench
 * name harvest (probes/bench-probes.md P1); empty until then. */
static struct wal_cfg_fixup_pair g_wal_cfg_fixup_pairs[WAL_CFG_MAX_FIXUPS];
static uint32_t g_wal_cfg_fixup_count;

/* ------------------------------------------------------------------ */
/* runtime layout descriptor (wifi-algcfg-slice/REPORT.md s3, [V])      */
/* ------------------------------------------------------------------ */
static const struct wal_cfg_runtime_layout g_wal_cfg_layout = {
	.process_tbl_off      = 0x35c8,
	.process_tbl_entries  = 414,
	.process_entry_size   = 12,
	.param_size_tbl_off   = 0x2d9c,
	.param_size_entries   = 40,
	.bool_tbl_off         = 0x2e3c,
	.pkt_type_tbl_off     = 0x2e4c,
	.tx_mode_tbl_off      = 0x301c,
	.alg_lut_off          = 0x4934,
	.alg_lut_entries      = 416,
	.alg_lut_entry_size   = 176,
	.fixup_list_off       = 0x0a9874,
	.fixup_pairs          = 4608,
	.name_vocab_off       = 0x23a680,
};

const struct wal_cfg_runtime_layout *wal_cfg_runtime_layout(void)
{
	return &g_wal_cfg_layout;
}

/* ------------------------------------------------------------------ */
/* the 176-B per-command LUT (host build of the vendor's g_alg_cfg_lut) */
/* ------------------------------------------------------------------ */
static alg_cfg_lut_entry_stru g_alg_cfg_lut[416];

alg_cfg_lut_entry_stru *wal_cfg_lut_entry(wlan_cfgid_enum_uint16 cfg_id,
	alg_cfg_type_enum_uint8 cfg_type)
{
	uint32_t key = ((uint32_t)cfg_type << 16) | cfg_id;
	uint32_t i;

	for (i = 0; i < g_wal_cfg_layout.alg_lut_entries; i++) {
		if (g_alg_cfg_lut[i].key == key) {
			return &g_alg_cfg_lut[i];
		}
	}
	return NULL;
}

/* LUT fn-pointer slots inside the 176-B descriptor.  The vendor offsets of
 * these fields are the bench probe P2 deliverable (the binary populates
 * them via the fixup path); host-side we use this provisional placement
 * until the probe pins the real offsets. */
#define WAL_CFG_LUT_SLOT_ARGS_ANALYSIS  0  /* opaque[0..3]   */
#define WAL_CFG_LUT_SLOT_OUTPUT_ENTRY   4  /* opaque[4..7]   */
#define WAL_CFG_LUT_SLOT_HOST_PROCESS   8  /* opaque[8..11]  */
#define WAL_CFG_LUT_SLOT_DEV_PROCESS    12 /* opaque[12..15] */

static void wal_cfg_lut_set_fn(alg_cfg_lut_entry_stru *entry, uint32_t slot,
	void *fn)
{
	uint32_t *slot_ptr = (uint32_t *)(entry->opaque + slot);

	*slot_ptr = (uint32_t)(unsigned long)fn;
}

static void *wal_cfg_lut_get_fn(const alg_cfg_lut_entry_stru *entry, uint32_t slot)
{
	const uint32_t *slot_ptr = (const uint32_t *)(entry->opaque + slot);

	return (void *)(unsigned long)(*slot_ptr);
}

/* build: one LUT entry per process-table row, key (type<<16)|id, with the
 * verified flag fields; parser fns are filled for the twinned subset via
 * the codec dispatch table, the hi5622 parser families stay NULL until
 * their per-command port (or the bench LUT harvest). */
int wal_cfg_lut_init(void)
{
	uint32_t i;
	uint32_t total = alg_cfg_process_tbl_size();

	if (total > g_wal_cfg_layout.alg_lut_entries) {
		pr_err("hi5622_wal_cfg: process table (%u) larger than LUT (%u)\n",
			total, g_wal_cfg_layout.alg_lut_entries);
		return -1;
	}

	memset(g_alg_cfg_lut, 0, sizeof(g_alg_cfg_lut));

	for (i = 0; i < total; i++) {
		alg_cfg_process_info_stru *row = &g_alg_cfg_process_info_table[i];
		alg_cfg_lut_entry_stru *entry = &g_alg_cfg_lut[i];
		wlan_alg_cfg_sub_alg_args_analysis_tbl_stru *sub;

		entry->key = ((uint32_t)row->en_cfg_type << 16) | row->en_alg_cfg_id;
		/* verified binary fields (algcfg REPORT s3) */
		if (row->en_cfg_type == ALG_CFG_TYPE_SET) {
			entry->msg_flags = 0x000b0001U;
			entry->need_flags = 0x00010101U;
		} else {
			entry->msg_flags = 0x000b0000U;
			entry->need_flags = 0x00010100U;
		}
		entry->fixed_field = 0x0aU;

		/* twinned-subset parsers: fill the args-analysis fn slot.
		 * The extern dispatch table is an incomplete array here (it is
		 * defined in wal_config_alg_cfg.c); its size is the 17-row twin
		 * subset per the codec slice. */
		sub = wal_get_wlan_alg_cfg_analysis_entry(
			g_wlan_alg_cfg_sub_alg_args_analysis_table,
			WAL_CFG_SUB_ALG_ARGS_ANALYSIS_TBL_SIZE,
			row->en_alg_cfg_id);
		if ((sub != NULL) && (sub->p_sub_alg_args_analysis_func != NULL)) {
			wal_cfg_lut_set_fn(entry, WAL_CFG_LUT_SLOT_ARGS_ANALYSIS,
				sub->p_sub_alg_args_analysis_func);
		}
		/* host/device legs of the module entry serve every row */
		wal_cfg_lut_set_fn(entry, WAL_CFG_LUT_SLOT_HOST_PROCESS,
			(void *)wal_wlan_cfg_module_host_process_entry);
		wal_cfg_lut_set_fn(entry, WAL_CFG_LUT_SLOT_DEV_PROCESS,
			(void *)wal_wlan_cfg_module_device_process_entry);
	}

	pr_info("hi5622_wal_cfg: LUT built: %u entries x %u B "
		"(vendor layout .data+0x%x, fixup list .data+0x%x, %u pairs)\n",
		total, g_wal_cfg_layout.alg_lut_entry_size,
		g_wal_cfg_layout.alg_lut_off, g_wal_cfg_layout.fixup_list_off,
		g_wal_cfg_layout.fixup_pairs);
	return 0;
}

void wal_cfg_lut_deinit(void)
{
	/* release only the names this loader duplicated (fixup_add);
	 * compiled-in/literal names are never freed */
	uint32_t i;

	for (i = 0; i < g_wal_cfg_fixup_count; i++) {
		uint32_t target = g_wal_cfg_fixup_pairs[i].target;
		char *name = (char *)(unsigned long)g_wal_cfg_fixup_pairs[i].value;

		if ((target < alg_cfg_process_tbl_size()) && (name != NULL) &&
		    (g_alg_cfg_process_info_table[target].pc_alg_cfg_name == name)) {
			g_alg_cfg_process_info_table[target].pc_alg_cfg_name = NULL;
			kfree(name);
		}
		g_wal_cfg_fixup_pairs[i].value = 0;
	}
	g_wal_cfg_fixup_count = 0;
	memset(g_alg_cfg_lut, 0, sizeof(g_alg_cfg_lut));
}

/* ------------------------------------------------------------------ */
/* name-fixup loader                                                    */
/* ------------------------------------------------------------------ */

uint32_t wal_cfg_fixup_count(void)
{
	return g_wal_cfg_fixup_count;
}

uint32_t wal_cfg_fixup_target(uint32_t i)
{
	return (i < g_wal_cfg_fixup_count) ? g_wal_cfg_fixup_pairs[i].target : 0;
}

const char *wal_cfg_fixup_name(uint32_t i)
{
	return (i < g_wal_cfg_fixup_count) ?
		(const char *)(unsigned long)g_wal_cfg_fixup_pairs[i].value : NULL;
}

/* apply a fixup batch: pairs whose target names a process-table slot
 * (row index < process_tbl_entries) patch the row name; the remaining
 * pairs are LUT-field patches recorded into the LUT (fn slots). */
int wal_cfg_name_fixup_apply(const struct wal_cfg_fixup_pair *pairs, uint32_t count)
{
	uint32_t i;
	int applied = 0;

	if (pairs == NULL) {
		return OAL_ERR_CODE_PTR_NULL;
	}
	if (count > WAL_CFG_MAX_FIXUPS) {
		return OAL_ERR_CODE_ARRAY_OVERFLOW;
	}

	for (i = 0; i < count; i++) {
		uint32_t target = pairs[i].target;

		if (target < g_wal_cfg_layout.process_tbl_entries) {
			/* process-table name slot (host format: row index) */
			g_alg_cfg_process_info_table[target].pc_alg_cfg_name =
				(char *)(unsigned long)pairs[i].value;
			applied++;
		} else if ((target >= g_wal_cfg_layout.alg_lut_off) &&
			   (target < g_wal_cfg_layout.alg_lut_off +
			    g_wal_cfg_layout.alg_lut_entries *
			    g_wal_cfg_layout.alg_lut_entry_size)) {
			/* LUT descriptor patch: absolute .data offset of the slot */
			uint32_t lut_idx = (target - g_wal_cfg_layout.alg_lut_off) /
				g_wal_cfg_layout.alg_lut_entry_size;
			uint32_t in_entry = (target - g_wal_cfg_layout.alg_lut_off) %
				g_wal_cfg_layout.alg_lut_entry_size;

			if ((lut_idx < oal_array_size(g_alg_cfg_lut)) && (in_entry >= 24)) {
				uint32_t *slot_ptr = (uint32_t *)((uint8_t *)&g_alg_cfg_lut[lut_idx] +
					in_entry);
				*slot_ptr = pairs[i].value;
				applied++;
			}
		}
	}

	pr_info("hi5622_wal_cfg: name fixup batch applied: %d/%u pairs\n",
		applied, count);
	return applied;
}

/* runtime name patch for one process-table row (bench name fill + sysfs
 * fixup_add entry).  The name is duplicated so the codec table stays
 * stable; the census placeholder rows are the targets. */
int wal_cfg_name_fixup_add(uint32_t process_tbl_idx, const char *name)
{
	alg_cfg_process_info_stru *row;
	char *dup;

	if (name == NULL) {
		return OAL_ERR_CODE_PTR_NULL;
	}
	if (process_tbl_idx >= alg_cfg_process_tbl_size()) {
		return OAL_ERR_CODE_ARRAY_OVERFLOW;
	}
	if (g_wal_cfg_fixup_count >= WAL_CFG_MAX_FIXUPS) {
		return OAL_ERR_CODE_ARRAY_OVERFLOW;
	}

	row = &g_alg_cfg_process_info_table[process_tbl_idx];

	/* a previous fixup on this row holds a heap name: release it before
	 * overwriting (census placeholders are string literals, never freed) */
	{
		uint32_t i;
		for (i = 0; i < g_wal_cfg_fixup_count; i++) {
			if ((g_wal_cfg_fixup_pairs[i].target == process_tbl_idx) &&
			    (row->pc_alg_cfg_name ==
			     (char *)(unsigned long)g_wal_cfg_fixup_pairs[i].value)) {
				row->pc_alg_cfg_name = NULL;
				kfree((char *)(unsigned long)g_wal_cfg_fixup_pairs[i].value);
				g_wal_cfg_fixup_pairs[i].value = 0;
			}
		}
	}

	dup = kstrdup(name, GFP_KERNEL);
	if (dup == NULL) {
		return OAL_FAIL;
	}
	row->pc_alg_cfg_name = dup;

	g_wal_cfg_fixup_pairs[g_wal_cfg_fixup_count].target = process_tbl_idx;
	g_wal_cfg_fixup_pairs[g_wal_cfg_fixup_count].value = (uint32_t)(unsigned long)dup;
	g_wal_cfg_fixup_count++;

	pr_info("hi5622_wal_cfg: name fixup row %u -> [%s]\n", process_tbl_idx, name);
	return OAL_SUCC;
}

/* ------------------------------------------------------------------ */
/* the two exported debug-lut searcher slots                           */
/* ------------------------------------------------------------------ */
void *wal_cfg_search_alg_debug_lut(void)
{
	return g_alg_cfg_lut;
}

const char *wal_cfg_search_dbg_cfg_name_by_cfg_id(mac_alg_cfg_enum_uint16 cfg_id)
{
	alg_cfg_lut_entry_stru *entry;

	entry = wal_cfg_lut_entry(cfg_id, ALG_CFG_TYPE_SET);
	if (entry == NULL) {
		entry = wal_cfg_lut_entry(cfg_id, ALG_CFG_TYPE_GET);
	}
	if (entry == NULL) {
		return NULL;
	}
	{
		uint32_t idx = (uint32_t)(entry - g_alg_cfg_lut);

		if (idx >= alg_cfg_process_tbl_size()) {
			return NULL;
		}
		return g_alg_cfg_process_info_table[idx].pc_alg_cfg_name;
	}
}

/* ------------------------------------------------------------------ */
/* text dumps for the sysfs surface                                    */
/* ------------------------------------------------------------------ */
int wal_cfg_process_table_dump(char *buf, uint32_t buf_len)
{
	uint32_t i;
	uint32_t total = alg_cfg_process_tbl_size();
	int off = 0;
	int n;

	n = scnprintf(buf + off, buf_len - off,
		"idx|name|cfg_id|type|need_h|w4h|need_d|w4d\n");
	off += n;
	for (i = 0; i < total; i++) {
		alg_cfg_process_info_stru *row = &g_alg_cfg_process_info_table[i];

		n = scnprintf(buf + off, buf_len - off,
			"%u|%s|%u|%s|%u|%u|%u|%u\n", i, row->pc_alg_cfg_name,
			row->en_alg_cfg_id,
			(row->en_cfg_type == ALG_CFG_TYPE_SET) ? "SET" : "GET",
			row->st_process_info.en_need_host_process,
			row->st_process_info.en_need_w4_host_return,
			row->st_process_info.en_need_device_process,
			row->st_process_info.en_need_w4_device_return);
		off += n;
		if (off >= (int)buf_len - 96) {
			off += scnprintf(buf + off, buf_len - off, "... (truncated)\n");
			break;
		}
	}
	return off;
}

int wal_cfg_lut_dump(char *buf, uint32_t buf_len)
{
	uint32_t i;
	int off = 0;
	int n;

	n = scnprintf(buf + off, buf_len - off,
		"idx|key|type|id|msg_flags|need_flags|fixed|args_fn|out_fn|host_fn|dev_fn\n");
	off += n;
	for (i = 0; i < g_wal_cfg_layout.alg_lut_entries; i++) {
		alg_cfg_lut_entry_stru *entry = &g_alg_cfg_lut[i];

		if (entry->key == 0) {
			continue;
		}
		n = scnprintf(buf + off, buf_len - off,
			"%u|0x%08x|%u|%u|0x%08x|0x%08x|0x%x|%p|%p|%p|%p\n",
			i, entry->key, entry->key >> 16, entry->key & 0xffff,
			entry->msg_flags, entry->need_flags, entry->fixed_field,
			wal_cfg_lut_get_fn(entry, WAL_CFG_LUT_SLOT_ARGS_ANALYSIS),
			wal_cfg_lut_get_fn(entry, WAL_CFG_LUT_SLOT_OUTPUT_ENTRY),
			wal_cfg_lut_get_fn(entry, WAL_CFG_LUT_SLOT_HOST_PROCESS),
			wal_cfg_lut_get_fn(entry, WAL_CFG_LUT_SLOT_DEV_PROCESS));
		off += n;
		if (off >= (int)buf_len - 96) {
			off += scnprintf(buf + off, buf_len - off, "... (truncated)\n");
			break;
		}
	}
	return off;
}

int wal_cfg_param_size_tbl_dump(char *buf, uint32_t buf_len)
{
	uint32_t i;
	int off = 0;
	int n;

	n = scnprintf(buf + off, buf_len - off, "type|size\n");
	off += n;
	for (i = 0; i < g_wal_cfg_layout.param_size_entries; i++) {
		/* host port keeps the 29 named rows in the codec table; the
		 * 40-row binary table rows 29..46 names are bench probe P3 */
		if (i < oal_array_size(g_alg_cfg_param_size_tbl)) {
			n = scnprintf(buf + off, buf_len - off, "%u|%u\n",
				g_alg_cfg_param_size_tbl[i].en_param_type,
				g_alg_cfg_param_size_tbl[i].uc_tbl_size);
		} else {
			n = scnprintf(buf + off, buf_len - off, "%u|?\n", i);
		}
		off += n;
	}
	return off;
}
