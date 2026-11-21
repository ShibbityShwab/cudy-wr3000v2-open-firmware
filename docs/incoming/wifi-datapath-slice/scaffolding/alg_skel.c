// SPDX-License-Identifier: GPL-2.0
/*
 * alg_skel.c - linkable skeleton of the vendor alg_* algorithm-config layer.
 *
 * ORIGINAL text by the wifi-datapath-slice lane (see hi5622_alg.h for the
 * provenance and the signature policy).  Stubs return -EOPNOTSUPP on
 * semantics-bearing paths and 0 for benign formatters; every function
 * carries a TODO describing the contract the real slice must implement.
 * Names + export set match the vendor __ksymtab (6 functions + 8 g_alg_*
 * data symbols) so vendor-style consumers (wifi_debug.ko imports them) can
 * link against this module.
 */
#include <linux/export.h>
#include <linux/module.h>
#include "hi5622_alg.h"

/* ---------------- dispatch / table lookup ---------------- */

/* TODO: top of the "wal config alg <cfg> <args>" command path: tokenize the
 * command, resolve the process-info row by name, then call the row's parser
 * (args_analysis) or formatter (output) according to the row's SET/GET
 * type.  The h/w4h/w4d flags of the row decide whether the value must also
 * be written to the device (need_d_process) - wire that through the
 * transport slice when the bench allows writes. */
int32_t alg_cfg_analysis_args(const char *cmd, uint32_t cmd_len,
			      struct hi5622_alg_cfg_param *param, void *out)
{
	return -EOPNOTSUPP;
}

int32_t alg_cfg_analysis_args_head(const char *cmd, uintptr_t info,
				   uintptr_t args_offset, uintptr_t pkt_type)
{
	return -EOPNOTSUPP;
}

/* TODO: table lookups over the process-info table.  The port should build
 * this table from the decoded cfg-id list (see REPORT section on the alg
 * table contract); ids/names are the wire-facing ABI of the cfg commands. */
struct hi5622_alg_cfg_process_info *alg_cfg_search_process_info_by_cfg_id(
	hi5622_alg_cfg_id cfg_id, uintptr_t a1, uintptr_t a2)
{
	return NULL;
}

struct hi5622_alg_cfg_process_info *alg_cfg_search_process_info_by_cfg_name(
	const char *cfg_name)
{
	return NULL;
}

struct hi5622_alg_cfg_process_info *alg_cfg_search_process_info_by_cfg_id_normal(
	hi5622_alg_cfg_id cfg_id, uintptr_t a1, uintptr_t a2)
{
	return NULL;
}

struct hi5622_alg_cfg_process_info *alg_cfg_search_process_info_by_cfg_name_normal(
	const char *cfg_name, uintptr_t a1, uintptr_t a2)
{
	return NULL;
}

/* TODO: argument helpers - token extraction from the command tail.  These
 * are pure string functions: port them 1:1 (contract fixed by the 17
 * exact-name reference twins), no device involvement. */
int32_t alg_cfg_get_cmd_one_arg(const char *cmd, const char *arg,
				uint32_t arg_size, uint32_t *cmd_offset)
{
	return -EOPNOTSUPP;
}

int32_t alg_cfg_get_match_string(const char *args, const char *match,
				 uint32_t match_len, uint32_t *out)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(alg_cfg_get_match_string);

int32_t alg_cfg_get_match_value(const char *args, const char *match,
				uint32_t match_len, uint32_t *out)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(alg_cfg_get_match_value);

int32_t alg_cfg_get_tail_num(const char *args, uint32_t *out)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(alg_cfg_get_tail_num);

int32_t alg_cfg_get_pkt_type(const char *cmd_args)
{
	return -EOPNOTSUPP;
}

int32_t alg_cfg_param_get_tbl_size(const struct hi5622_alg_cfg_process_info *tbl)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(alg_cfg_param_get_tbl_size);

/* ---------------- per-cfg parsers ---------------- */

/* TODO: parse one cfg's argument text into the param object.  Parser
 * contract: validate arg count/ranges (oal_atoi on numeric fields), then
 * memcpy_s the result into the scratch block; on error emit an oam log and
 * return an error code. */
#define ALG_ARGS_ANALYSIS_STUB(name)					\
	int32_t alg_cfg_args_analysis_##name(				\
		struct hi5622_alg_cfg_param *param, void *mem_block,	\
		uint32_t len)						\
	{								\
		return -EOPNOTSUPP;					\
	}

ALG_ARGS_ANALYSIS_STUB(hardamard_enable)
ALG_ARGS_ANALYSIS_STUB(hardamard_mode)
ALG_ARGS_ANALYSIS_STUB(txmode_all_user_fix_mode)
ALG_ARGS_ANALYSIS_STUB(txmode_user_fix_mode)
ALG_ARGS_ANALYSIS_STUB(waterfilling_enable)
ALG_ARGS_ANALYSIS_STUB(waterfilling_limit)
ALG_ARGS_ANALYSIS_STUB(waterfilling_mode)

/* TODO: parse the tail of a generic cfg command (exported by the vendor). */
int32_t alg_cfg_args_analysis_param(const char *args)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(alg_cfg_args_analysis_param);

/* TODO: hi5622-specific equipment-parameter parser; no hi1105 twin.
 * Bench probe: feed the vendor userspace cfg strings and record which ids
 * arrive here. */
int32_t alg_cfg_args_param_analyse_equipment_param(uintptr_t a0)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(alg_cfg_args_param_analyse_equipment_param);

/* TODO: post-parse processors (vendor naming alg_cfg_param_analyze_*_process;
 * measured arity 1).  Implement per cfg id once the param layout is fixed. */
int32_t alg_cfg_param_analyze_ns_aggr_process(uintptr_t param)
{
	return -EOPNOTSUPP;
}

int32_t alg_cfg_param_analyze_rate_log_sw_process(uintptr_t param)
{
	return -EOPNOTSUPP;
}

int32_t alg_cfg_param_analyze_aggr_log_sw_process(uintptr_t param)
{
	return -EOPNOTSUPP;
}

int32_t alg_cfg_param_analyze_ns_rate_process(uintptr_t param)
{
	return -EOPNOTSUPP;
}

int32_t alg_cfg_param_analyze_rate_protect_mode_process(uintptr_t param)
{
	return -EOPNOTSUPP;
}

/* ---------------- per-cfg formatters ---------------- */

/* TODO: render the current value of the cfg row as text (vendor evidence:
 * snprintf_s/memcpy_s with alg_cfg_param_get_tbl_size walking the row
 * tables).  Return 0 on success so a linked debug consumer keeps working. */
#define ALG_OUTPUT_ENTRY_STUB(name)					\
	int32_t alg_##name##_config_param_output_entry(			\
		struct hi5622_alg_cfg_param *param, char *out,		\
		uint32_t out_len)					\
	{								\
		return 0;						\
	}

ALG_OUTPUT_ENTRY_STUB(ofdma)
ALG_OUTPUT_ENTRY_STUB(atf)
ALG_OUTPUT_ENTRY_STUB(trigger)
ALG_OUTPUT_ENTRY_STUB(schedule_su_sch)
ALG_OUTPUT_ENTRY_STUB(amsdu)
ALG_OUTPUT_ENTRY_STUB(aggr)
ALG_OUTPUT_ENTRY_STUB(txmode)

/* ---------------- exported data symbols ---------------- */
/* TODO: fill the function pointers from the table search implementations
 * above and the size scalars from the port's own tables.  The vendor
 * exports them as data so that debug modules can reach the cfg tables
 * without linking each accessor. */

hi5622_alg_find_by_cfg_id_fn g_alg_cfg_process_find_by_cfg_id;
EXPORT_SYMBOL(g_alg_cfg_process_find_by_cfg_id);
hi5622_alg_find_by_name_fn g_alg_cfg_process_find_by_name;
EXPORT_SYMBOL(g_alg_cfg_process_find_by_name);
void *g_alg_cfg_search_alg_debug_lut;
EXPORT_SYMBOL(g_alg_cfg_search_alg_debug_lut);
void *g_alg_cfg_search_dbg_cfg_name_by_cfg_id;
EXPORT_SYMBOL(g_alg_cfg_search_dbg_cfg_name_by_cfg_id);
uint32_t g_alg_cfg_bool_tbl_size;
EXPORT_SYMBOL(g_alg_cfg_bool_tbl_size);
uint32_t g_alg_cfg_snd_fback_type_tbl_size;
EXPORT_SYMBOL(g_alg_cfg_snd_fback_type_tbl_size);
uint32_t g_alg_cfg_snd_user_mat_type_tbl_size;
EXPORT_SYMBOL(g_alg_cfg_snd_user_mat_type_tbl_size);
uint32_t g_alg_cfg_su_aggr_mode_tbl_size;
EXPORT_SYMBOL(g_alg_cfg_su_aggr_mode_tbl_size);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cudy WR3000 v2.0 hi5622 alg_* clean-room skeleton (datapath-slice)");
