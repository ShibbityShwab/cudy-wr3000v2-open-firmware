/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_alg.h - clean-room public surface of the vendor alg_* config layer
 *
 * ORIGINAL text by the wifi-datapath-slice lane.  The alg_ family is the
 * vendor's "algorithm configuration" command interface: a text command such
 * as "txmode_user_fix_mode 3 0x1f" is looked up in a process-info table
 * (name -> cfg id + SET/GET type + host/device processing flags), its
 * argument list is parsed by an alg_cfg_args_analysis_* handler, and the
 * current value is rendered back by an alg_*_config_param_output_entry /
 * alg_cfg_process_*_output formatter.  The hi1105 tree keeps the same
 * design in wal/wal_config_alg_cfg.c (17 of our 239 names match it exactly;
 * used as name/structure reference only - no license grant, no text
 * reproduced).
 *
 * 239 alg_* functions / 103,476 B in the vendor binary, of which 6 function
 * names + 8 g_alg_* data names are __ksymtab exports (all stubbed below).
 */
#ifndef HI5622_ALG_H
#define HI5622_ALG_H

#include <linux/types.h>

/* Opaque config objects; layouts deliberately unknown. */
struct hi5622_alg_cfg_param;	/* one parsed algorithm-config parameter set */
struct hi5622_alg_cfg_process_info; /* name/id/type/flags table row */

typedef uint32_t hi5622_alg_cfg_id;

enum hi5622_alg_cfg_op {
	HI5622_ALG_CFG_OP_SET = 0,
	HI5622_ALG_CFG_OP_GET = 1,
};

/* ------------------------------------------------------------------ */
/* Command dispatch / table lookup                                     */
/* ------------------------------------------------------------------ */

/* Top of the "config alg" command: split the command line, find the
 * process-info row, dispatch to the per-cfg parser.  Twin: alg_cfg_analysis_args
 * (wal/wal_config_alg_cfg.c), same 4-parameter shape. */
int32_t alg_cfg_analysis_args(const char *cmd, uint32_t cmd_len,
			      struct hi5622_alg_cfg_param *param, void *out);

/* Row lookups.  Shapes follow the measured call sites and the reference
 * table contract (name search, id search, normalised variants). */
struct hi5622_alg_cfg_process_info *alg_cfg_search_process_info_by_cfg_id(
	hi5622_alg_cfg_id cfg_id, uintptr_t a1, uintptr_t a2);
struct hi5622_alg_cfg_process_info *alg_cfg_search_process_info_by_cfg_name(
	const char *cfg_name);
struct hi5622_alg_cfg_process_info *alg_cfg_search_process_info_by_cfg_id_normal(
	hi5622_alg_cfg_id cfg_id, uintptr_t a1, uintptr_t a2);
struct hi5622_alg_cfg_process_info *alg_cfg_search_process_info_by_cfg_name_normal(
	const char *cfg_name, uintptr_t a1, uintptr_t a2);

/* Argument helpers.  Shapes: (cmd, arg, arg_size, out offset) per the
 * reference twin for get_cmd_one_arg; 4 regs measured for get_match_*. */
int32_t alg_cfg_get_cmd_one_arg(const char *cmd, const char *arg,
				uint32_t arg_size, uint32_t *cmd_offset);
int32_t alg_cfg_get_match_string(const char *args, const char *match,
				 uint32_t match_len, uint32_t *out);
int32_t alg_cfg_get_match_value(const char *args, const char *match,
				uint32_t match_len, uint32_t *out);
int32_t alg_cfg_get_tail_num(const char *args, uint32_t *out);
int32_t alg_cfg_get_pkt_type(const char *cmd_args);

/* Table sizes (vendor exports the sizes as g_alg_*_tbl_size data). */
int32_t alg_cfg_param_get_tbl_size(const struct hi5622_alg_cfg_process_info *tbl);

/* ------------------------------------------------------------------ */
/* Per-cfg parsers: alg_cfg_args_analysis_<cfg>                         */
/* ------------------------------------------------------------------ */
/* Contract (from the 17 exact reference twins): parse the argument text
 * for one cfg row into (param, scratch mem block, block length), return
 * error/success.  The vendor binary has 108 of these; the exact-name
 * twins are declared explicitly, the rest follow the same shape and are
 * inventoried in ../mapping/alg-inventory.csv. */

#define HI5622_ALG_ARGS_ANALYSIS(name)					\
	int32_t alg_cfg_args_analysis_##name(				\
		struct hi5622_alg_cfg_param *param, void *mem_block,	\
		uint32_t len)

HI5622_ALG_ARGS_ANALYSIS(hardamard_enable);
HI5622_ALG_ARGS_ANALYSIS(hardamard_mode);
HI5622_ALG_ARGS_ANALYSIS(txmode_all_user_fix_mode);
HI5622_ALG_ARGS_ANALYSIS(txmode_user_fix_mode);
HI5622_ALG_ARGS_ANALYSIS(waterfilling_enable);
HI5622_ALG_ARGS_ANALYSIS(waterfilling_limit);
HI5622_ALG_ARGS_ANALYSIS(waterfilling_mode);
/* Dispatch helper (twin: same name in the reference; shape from it:
 * cmd, process-info row, args offset, packet type - kept generic here). */
int32_t alg_cfg_analysis_args_head(const char *cmd, uintptr_t info,
				   uintptr_t args_offset, uintptr_t pkt_type);

/* Post-parse "process" handlers present in the vendor binary under the
 * alg_cfg_param_analyze_* naming (reference keeps the *debug* twins in the
 * same file).  Measured arity 1, return consumed as int. */
int32_t alg_cfg_param_analyze_ns_aggr_process(uintptr_t param);
int32_t alg_cfg_param_analyze_rate_log_sw_process(uintptr_t param);
int32_t alg_cfg_param_analyze_aggr_log_sw_process(uintptr_t param);
int32_t alg_cfg_param_analyze_ns_rate_process(uintptr_t param);
int32_t alg_cfg_param_analyze_rate_protect_mode_process(uintptr_t param);

/* Parse the generic "alg cfg <id> <args>" tail (exported by the vendor). */
int32_t alg_cfg_args_analysis_param(const char *args);
/* hi5622-specific equipment-parameter parser (exported; no hi1105 twin). */
int32_t alg_cfg_args_param_analyse_equipment_param(uintptr_t a0);

/* ------------------------------------------------------------------ */
/* Per-cfg formatters: alg_*_config_param_output_entry                  */
/* ------------------------------------------------------------------ */
/* Contract: render the cfg row's current value as text (vendor evidence:
 * snprintf_s + memcpy_s + alg_cfg_param_get_tbl_size).  37 of these exist;
 * the top-by-size ones are declared here, the rest follow the same shape.
 * TODO(probe): arity is only partially measured (2..4 regs seen); the
 * shape below is (param, out buffer, out len). */
int32_t alg_ofdma_config_param_output_entry(struct hi5622_alg_cfg_param *param,
					    char *out, uint32_t out_len);
int32_t alg_atf_config_param_output_entry(struct hi5622_alg_cfg_param *param,
					  char *out, uint32_t out_len);
int32_t alg_trigger_config_param_output_entry(struct hi5622_alg_cfg_param *param,
					      char *out, uint32_t out_len);
int32_t alg_schedule_su_sch_config_param_output_entry(
	struct hi5622_alg_cfg_param *param, char *out, uint32_t out_len);
int32_t alg_amsdu_config_param_output_entry(struct hi5622_alg_cfg_param *param,
					    char *out, uint32_t out_len);
int32_t alg_aggr_config_param_output_entry(struct hi5622_alg_cfg_param *param,
					   char *out, uint32_t out_len);
int32_t alg_txmode_config_param_output_entry(struct hi5622_alg_cfg_param *param,
					     char *out, uint32_t out_len);

/* ------------------------------------------------------------------ */
/* Exported data symbols (vendor __ksymtab; wifi_debug.ko imports them) */
/* ------------------------------------------------------------------ */

typedef struct hi5622_alg_cfg_process_info *(*hi5622_alg_find_by_cfg_id_fn)(
	hi5622_alg_cfg_id cfg_id);
typedef struct hi5622_alg_cfg_process_info *(*hi5622_alg_find_by_name_fn)(
	const char *cfg_name);

extern hi5622_alg_find_by_cfg_id_fn g_alg_cfg_process_find_by_cfg_id;
extern hi5622_alg_find_by_name_fn g_alg_cfg_process_find_by_name;
/* debug-lut searchers: table-driven name/id translation used by dfx */
extern void *g_alg_cfg_search_alg_debug_lut;
extern void *g_alg_cfg_search_dbg_cfg_name_by_cfg_id;
/* per-feature table sizes (u32 in the vendor) */
extern uint32_t g_alg_cfg_bool_tbl_size;
extern uint32_t g_alg_cfg_snd_fback_type_tbl_size;
extern uint32_t g_alg_cfg_snd_user_mat_type_tbl_size;
extern uint32_t g_alg_cfg_su_aggr_mode_tbl_size;

#endif /* HI5622_ALG_H */
