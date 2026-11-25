// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_rate_ctl_logic.c - portable pure logic for the vendor alg_cfg /
 * rate-control command surface (clean-room).  See hi5622_rate_ctl_logic.h
 * and ../REPORT.md for the decoded facts this file encodes.
 */
#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "hi5622_rate_ctl_logic.h"

/* ---- the 26 autorate knobs: vendor g_ast_alg_cfg_ar_dev_param_tbl [V] ---- */
static const char *const rc_ar_knob_names[RC_AR_KNOB_COUNT] = {
	"sudden_good_delta_gdpt_ratio",		/* 0 */
	"sudden_bad_delta_gdpt_ratio",		/* 1 */
	"descend_protocol_pre_thrd",		/* 2 */
	"11b_ascend_protocol_per_thrd",		/* 3 */
	"11a_ascend_protocol_per_thrd",		/* 4 */
	"ascend_bw_rate_keep_cnt_thrd",		/* 5 */
	"descend_bw_min_pktcnt_thrd",		/* 6 */
	"ascend_bw_probe_pktcnt_min_thrd",	/* 7 */
	"descend_bw_probe_pktcnt_min_thrd",	/* 8 */
	"probe_gi_start_intvl_pktnum",		/* 9 */
	"probe_reduction_strategy",		/* 10 */
	"probe_per_init_strategy",		/* 11 */
	"tx_rate_aging_time",			/* 12 */
	"rx_rate_aging_time",			/* 13 */
	"descend_bw_event_trig_per_thrd",	/* 14 */
	"probe_update_best_opt_en",		/* 15 */
	"probe_update_best_same_nss_th",	/* 16 */
	"probe_update_best_diff_nss_th",	/* 17 */
	"vht_priv_rate_probe_switch",		/* 18 */
	"ht_priv_2_vht_probe_switch",		/* 19 */
	"suspect_collision_ppdu_cnt_th",	/* 20 */
	"cfg_vi_spec_per",			/* 21 */
	"cfg_vo_spec_per",			/* 22 */
	"gi1_with_2xltf_en",			/* 23 */
	"descend_bw_gdpt_better_thrd",		/* 24 */
	"ascend_bw_gdpt_better_thrd",		/* 25 */
};

/* Decoded per-command value length (value bytes after the 10-byte header).
 * 0xffff marks a command the portable logic does not build (dedicated parser /
 * layout [U]); the caller must refuse it. */
static uint16_t rc_known_value_len(uint16_t cfg_id, uint8_t cfg_type)
{
	(void)cfg_type;
	switch (cfg_id) {
	case RC_CFG_RATE_MODE:
	case RC_CFG_FEC_CODING:
	case RC_CFG_HE_SIGB_RATE:
	case RC_CFG_DEBUG_LOG_SWITCH:
	case RC_CFG_AR_PROBE:
	case RC_CFG_GI_TYPE:
	case RC_CFG_MU_RTS_MODE:
	case RC_CFG_SIGB_DCM:
	case RC_CFG_PREAMBLE_TYPE:
	case RC_CFG_TPC_MODE:
	case RC_CFG_POW_MODE:
	case RC_CFG_RESP_CHAIN_SEL:
	case RC_CFG_WATERFILLING:
		return 1u;
	case RC_CFG_AR_DEV_PARAM:
		return 5u;	/* uint8_t knob + uint32_t value */
	case RC_CFG_MU_FEC_CODING:
	case RC_CFG_HE_LTF_TYPE:
	case RC_CFG_DCM_ENABLE:
	case RC_CFG_TPC_POW_LVL:
		return 2u;
	case RC_CFG_TX_CNT:
		return 4u;
	case RC_CFG_PROTECT_MODE:
		return 4u;
	case RC_CFG_CURRENT_RATION:
		return 20u;	/* GET: 6-byte MAC in, 20-byte report out */
	default:
		return 0xffffu;	/* not a documented portable layout */
	}
}

static int rc_req_set_header(struct rc_req *req, uint16_t cfg_id, uint8_t type)
{
	if (!req)
		return -1;
	memset(req, 0, sizeof(*req));
	req->cfg_id = cfg_id;
	req->cfg_type = type;
	req->pkt_type = RC_PKT_NONE;
	return 0;
}

int rc_req_rate_mode(struct rc_req *req, unsigned mode)
{
	if (!req || mode > RC_RATE_MODE_FIXED)
		return -1;
	if (rc_req_set_header(req, RC_CFG_RATE_MODE, RC_CFG_SET))
		return -1;
	req->value[0] = (uint8_t)mode;
	req->value_len = RC_LEN_RATE_MODE - RC_ALG_HDR_LEN;
	req->blob_len = RC_LEN_RATE_MODE;
	return 0;
}

int rc_req_rate_mode_get(struct rc_req *req)
{
	if (!req)
		return -1;
	if (rc_req_set_header(req, RC_CFG_RATE_MODE, RC_CFG_GET))
		return -1;
	req->value_len = RC_LEN_RATE_MODE - RC_ALG_HDR_LEN;
	req->blob_len = RC_LEN_RATE_MODE;
	return 0;
}

int rc_req_ar_dev_param(struct rc_req *req, const char *knob, unsigned long value)
{
	int id;

	if (!req || !knob)
		return -1;
	id = rc_ar_knob_by_name(knob);
	if (id < 0)
		return -1;
	if (rc_req_set_header(req, RC_CFG_AR_DEV_PARAM, RC_CFG_SET))
		return -1;
	req->value[0] = (uint8_t)id;
	req->value[1] = (uint8_t)(value & 0xffu);
	req->value[2] = (uint8_t)((value >> 8) & 0xffu);
	req->value[3] = (uint8_t)((value >> 16) & 0xffu);
	req->value[4] = (uint8_t)((value >> 24) & 0xffu);
	req->value_len = 5u;
	req->blob_len = RC_LEN_AR_DEV_PARAM;
	return 0;
}

int rc_req_ar_dev_param_get(struct rc_req *req, const char *knob)
{
	int id;

	if (!req || !knob)
		return -1;
	id = rc_ar_knob_by_name(knob);
	if (id < 0)
		return -1;
	if (rc_req_set_header(req, RC_CFG_AR_DEV_PARAM, RC_CFG_GET))
		return -1;
	req->value[0] = (uint8_t)id;
	req->value_len = 5u;
	req->blob_len = RC_LEN_AR_DEV_PARAM;
	return 0;
}

int rc_req_tpc_pow_lvl(struct rc_req *req, unsigned mode, unsigned level)
{
	if (!req || mode > RC_TPC_POW_CTRL || level > 0xffu)
		return -1;
	if (rc_req_set_header(req, RC_CFG_TPC_POW_LVL, RC_CFG_SET))
		return -1;
	req->value[0] = (uint8_t)mode;
	req->value[1] = (uint8_t)level;
	req->value_len = 2u;
	req->blob_len = RC_LEN_TPC_POW_LVL;
	return 0;
}

int rc_req_tpc_get(struct rc_req *req, uint16_t cfg_id)
{
	uint16_t vlen = rc_known_value_len(cfg_id, RC_CFG_GET);

	if (!req)
		return -1;
	if (cfg_id != RC_CFG_TPC_MODE && cfg_id != RC_CFG_TPC_POW_LVL &&
	    cfg_id != RC_CFG_POW_MODE && cfg_id != RC_CFG_RESP_CHAIN_SEL &&
	    cfg_id != RC_CFG_WATERFILLING)
		return -1;
	if (rc_req_set_header(req, cfg_id, RC_CFG_GET))
		return -1;
	req->value_len = vlen;
	req->blob_len = (uint16_t)(RC_ALG_HDR_LEN + vlen);
	return 0;
}

int rc_req_ration_get(struct rc_req *req, const uint8_t mac[6])
{
	if (!req || !mac)
		return -1;
	if (rc_req_set_header(req, RC_CFG_CURRENT_RATION, RC_CFG_GET))
		return -1;
	memcpy(req->value, mac, 6u);
	req->value_len = RC_LEN_CURRENT_RATION - RC_ALG_HDR_LEN;
	req->blob_len = RC_LEN_CURRENT_RATION;
	return 0;
}

int rc_req_raw(struct rc_req *req, uint16_t cfg_id, uint8_t cfg_type,
	       uint8_t pkt_type, const uint8_t *value, uint16_t value_len)
{
	if (!req || cfg_type > RC_CFG_GET || pkt_type > RC_PKT_NONE)
		return -1;
	if (value_len && !value)
		return -1;
	if (value_len > RC_ALG_MAX_VALUE)
		return -1;
	if (rc_req_set_header(req, cfg_id, cfg_type))
		return -1;
	if (value_len)
		memcpy(req->value, value, value_len);
	req->value_len = value_len;
	req->blob_len = (uint16_t)(RC_ALG_HDR_LEN + value_len);
	req->pkt_type = pkt_type;
	return 0;
}

int rc_blob_build(const struct rc_req *req, uint8_t *blob, size_t cap,
		  size_t *len)
{
	uint16_t known;

	if (!req || !blob || !len)
		return -1;
	known = rc_known_value_len(req->cfg_id, req->cfg_type);
	if (known == 0xffffu)
		return -1;
	if (req->cfg_type > RC_CFG_GET || req->pkt_type > RC_PKT_NONE)
		return -1;
	if (req->value_len != known)
		return -1;
	if (req->blob_len != RC_ALG_HDR_LEN + known)
		return -1;
	if (cap < req->blob_len)
		return -1;
	memset(blob, 0, req->blob_len);
	blob[RC_ALG_OFF_CFG_ID] = (uint8_t)(req->cfg_id & 0xffu);
	blob[RC_ALG_OFF_CFG_ID + 1u] = (uint8_t)((req->cfg_id >> 8) & 0xffu);
	blob[RC_ALG_OFF_TYPE] = req->cfg_type;
	blob[RC_ALG_OFF_PKT_TYPE] = req->pkt_type;
	/* dev_ret stays 0 (the device overwrites it) */
	blob[RC_ALG_OFF_NEED_W4] = 1u;	/* the device answers every SET/GET */
	memcpy(blob + RC_ALG_VALUE_OFF, req->value, req->value_len);
	*len = req->blob_len;
	return 0;
}

/* The config-sync answer: g1 id3, payload = cookie0|cookie1|syn_id|body_len|
 * body|pad; body = the alg_cfg blob (10-byte header + value). */
int rc_parse_cfg_sync(uint8_t group, uint16_t id, const uint8_t *payload,
		      size_t payload_len, struct rc_sync *out)
{
	const uint8_t *body;
	uint16_t syn_id, body_len, need_w4;
	uint32_t dev_ret;

	if (!payload || !out)
		return -1;
	if (group != 1u || id != 3u)
		return -1;
	if (payload_len < 16u)
		return -1;
	syn_id = (uint16_t)(payload[8] | ((uint16_t)payload[9] << 8));
	if (syn_id != RC_CFG_KEY)
		return -1;
	body_len = (uint16_t)(payload[10] | ((uint16_t)payload[11] << 8));
	if (body_len < RC_ALG_HDR_LEN || body_len > RC_ALG_MAX_BLOB)
		return -1;
	if ((size_t)body_len + 12u > payload_len)
		return -1;
	body = payload + 12;
	memset(out, 0, sizeof(*out));
	out->cfg_id = (uint16_t)(body[0] | ((uint16_t)body[1] << 8));
	out->cfg_type = body[2];
	out->pkt_type = body[3];
	dev_ret = (uint32_t)body[4] | ((uint32_t)body[5] << 8) |
		  ((uint32_t)body[6] << 16) | ((uint32_t)body[7] << 24);
	out->dev_ret = dev_ret;
	need_w4 = body[8];
	out->need_w4 = (uint8_t)need_w4;
	out->body_len = body_len;
	out->value_len = (uint16_t)(body_len - RC_ALG_HDR_LEN);
	if (out->value_len > RC_ALG_MAX_VALUE)
		return -1;
	memcpy(out->value, body + RC_ALG_VALUE_OFF, out->value_len);
	return 0;
}

enum rc_decision rc_precond_check(const struct rc_policy *pol)
{
	if (!pol)
		return RC_DEC_OP_UNKNOWN;
	/* The corrected C2 (fw-h2d-stall task-181): the type-3 create never
	 * initialised ctx+0x190, so the bench-proven fault-free ladder is
	 * TYPE 1 -> ctx4+0x190 != 0 -> ordered TXQ sync -> config events. */
	if (!pol->ctx_type1)
		return RC_DEC_NO_CTX_TYPE1;
	if (!pol->ctx_attested)
		return RC_DEC_NO_CTX_ATTEST;
	if (!pol->txq_sent)
		return RC_DEC_NO_TXQ_SYNC;
	return RC_DEC_ALLOW;
}

enum rc_decision rc_check(const struct rc_policy *pol, const struct rc_req *req,
			  enum rc_op op)
{
	enum rc_decision pre;

	if (!pol || !req)
		return RC_DEC_OP_UNKNOWN;
	if (!pol->enabled)
		return RC_DEC_DISABLED;
	pre = rc_precond_check(pol);
	if (pre != RC_DEC_ALLOW)
		return pre;
	if (pol->sent >= pol->budget)
		return RC_DEC_BUDGET;
	if (req->cfg_type > RC_CFG_GET || req->pkt_type > RC_PKT_NONE)
		return RC_DEC_BAD_TYPE;
	if (req->blob_len != RC_ALG_HDR_LEN + req->value_len ||
	    req->blob_len > RC_ALG_MAX_BLOB)
		return RC_DEC_BAD_LEN;
	switch (op) {
	case RC_OP_RATE_MODE_SET:
		if (req->cfg_id != RC_CFG_RATE_MODE || req->cfg_type != RC_CFG_SET ||
		    req->blob_len != RC_LEN_RATE_MODE)
			return RC_DEC_ID_NOT_ALLOWED;
		if (req->value[0] > RC_RATE_MODE_FIXED)
			return RC_DEC_BAD_VALUE;
		return RC_DEC_ALLOW;
	case RC_OP_RATE_MODE_GET:
		if (req->cfg_id != RC_CFG_RATE_MODE || req->cfg_type != RC_CFG_GET ||
		    req->blob_len != RC_LEN_RATE_MODE)
			return RC_DEC_ID_NOT_ALLOWED;
		return RC_DEC_ALLOW;
	case RC_OP_AR_SET:
		if (req->cfg_id != RC_CFG_AR_DEV_PARAM || req->cfg_type != RC_CFG_SET ||
		    req->blob_len != RC_LEN_AR_DEV_PARAM)
			return RC_DEC_ID_NOT_ALLOWED;
		if (req->value[0] >= RC_AR_KNOB_COUNT)
			return RC_DEC_BAD_VALUE;
		return RC_DEC_ALLOW;
	case RC_OP_AR_GET:
		if (req->cfg_id != RC_CFG_AR_DEV_PARAM || req->cfg_type != RC_CFG_GET ||
		    req->blob_len != RC_LEN_AR_DEV_PARAM)
			return RC_DEC_ID_NOT_ALLOWED;
		if (req->value[0] >= RC_AR_KNOB_COUNT)
			return RC_DEC_BAD_VALUE;
		return RC_DEC_ALLOW;
	case RC_OP_TPC_POW_LVL_SET:
		if (req->cfg_id != RC_CFG_TPC_POW_LVL || req->cfg_type != RC_CFG_SET ||
		    req->blob_len != RC_LEN_TPC_POW_LVL)
			return RC_DEC_ID_NOT_ALLOWED;
		if (req->value[0] > RC_TPC_POW_CTRL)
			return RC_DEC_BAD_VALUE;
		return RC_DEC_ALLOW;
	case RC_OP_TPC_GET:
		if (req->cfg_type != RC_CFG_GET)
			return RC_DEC_ID_NOT_ALLOWED;
		if (req->cfg_id != RC_CFG_TPC_MODE && req->cfg_id != RC_CFG_TPC_POW_LVL &&
		    req->cfg_id != RC_CFG_POW_MODE &&
		    req->cfg_id != RC_CFG_RESP_CHAIN_SEL &&
		    req->cfg_id != RC_CFG_WATERFILLING)
			return RC_DEC_ID_NOT_ALLOWED;
		if (req->blob_len != RC_ALG_HDR_LEN + req->value_len ||
		    req->blob_len > RC_ALG_MAX_BLOB)
			return RC_DEC_BAD_LEN;
		return RC_DEC_ALLOW;
	case RC_OP_RATION_GET:
		if (req->cfg_id != RC_CFG_CURRENT_RATION || req->cfg_type != RC_CFG_GET ||
		    req->blob_len != RC_LEN_CURRENT_RATION)
			return RC_DEC_ID_NOT_ALLOWED;
		return RC_DEC_ALLOW;
	case RC_OP_ARBITRARY:
		if (!pol->arbitrary)
			return RC_DEC_ARBITRARY_OFF;
		/* the arbitrary probe still refuses the dedicated-parser ids whose
		 * wire layout is not decoded (they need an explicit probe) */
		if (rc_known_value_len(req->cfg_id, req->cfg_type) == 0xffffu)
			return RC_DEC_ID_NOT_ALLOWED;
		return RC_DEC_ALLOW;
	default:
		return RC_DEC_OP_UNKNOWN;
	}
}

/* ---- names ---- */
static const char *const rc_rate_mode_names[] = { "auto", "fixed" };
static const char *const rc_tpc_pow_names[] = { "init", "sleep", "third" };
static const char *const rc_tpc_fix_names[] = { "data", "mag", "ctrl" };
static const char *const rc_gi_type_names[] = { "normal", "fixed" };
static const char *const rc_ar_probe_names[] = { "normal", "slow" };
static const char *const rc_ar_scen_names[] = { "normal", "move" };

const char *rc_name_rate_mode(unsigned value)
{
	return value < 2u ? rc_rate_mode_names[value] : NULL;
}

const char *rc_name_tpc_pow_mode(unsigned value)
{
	return value < 3u ? rc_tpc_pow_names[value] : NULL;
}

const char *rc_name_tpc_fix_pow_mode(unsigned value)
{
	return value < 3u ? rc_tpc_fix_names[value] : NULL;
}

const char *rc_name_tpc_mode(unsigned value)
{
	return value < 3u ? rc_tpc_fix_names[value] : NULL;
}

const char *rc_name_gi_type(unsigned value)
{
	return value < 2u ? rc_gi_type_names[value] : NULL;
}

const char *rc_name_ar_probe(unsigned value)
{
	return value < 2u ? rc_ar_probe_names[value] : NULL;
}

const char *rc_name_ar_scen_mode(unsigned value)
{
	return value < 2u ? rc_ar_scen_names[value] : NULL;
}

const char *rc_name_legacy_rate(unsigned value)
{
	switch (value) {
	case 0: return "1M";
	case 1: return "2M";
	case 2: return "5.5M";
	case 3: return "11M";
	case 11: return "6M";
	case 15: return "9M";
	case 10: return "12M";
	case 14: return "18M";
	case 9: return "24M";
	case 13: return "36M";
	case 8: return "48M";
	case 12: return "54M";
	default: return NULL;
	}
}

const char *rc_name_protocol(unsigned value)
{
	switch (value) {
	case 0: return "11b";
	case 1: return "11ag";
	case 2: return "11n";
	case 3: return "11n_green";
	case 4: return "11ac";
	case 8: return "11ax_su";
	case 9: return "11ax_mu";
	case 10: return "11ax_er_su";
	default: return NULL;
	}
}

const char *rc_name_decision(enum rc_decision dec)
{
	switch (dec) {
	case RC_DEC_ALLOW: return "allow";
	case RC_DEC_DISABLED: return "disabled";
	case RC_DEC_OP_UNKNOWN: return "op_unknown";
	case RC_DEC_ID_NOT_ALLOWED: return "id_not_allowed";
	case RC_DEC_BAD_VALUE: return "bad_value";
	case RC_DEC_ARBITRARY_OFF: return "arbitrary_off";
	case RC_DEC_BAD_LEN: return "bad_len";
	case RC_DEC_BAD_TYPE: return "bad_type";
	case RC_DEC_BUDGET: return "budget";
	case RC_DEC_NO_CTX_TYPE1: return "ctx_type1_required";
	case RC_DEC_NO_CTX_ATTEST: return "ctx190_attest_required";
	case RC_DEC_NO_TXQ_SYNC: return "txq_sync72_required";
	default: return "?";
	}
}

int rc_ar_knob_by_name(const char *name)
{
	unsigned i;

	if (!name)
		return -1;
	for (i = 0; i < RC_AR_KNOB_COUNT; i++) {
		if (strcmp(name, rc_ar_knob_names[i]) == 0)
			return (int)i;
	}
	/* decimal id accepted for scripting */
	if (name[0] >= '0' && name[0] <= '9') {
		unsigned long v = 0;
		unsigned i2;

		for (i2 = 0; name[i2]; i2++) {
			if (name[i2] < '0' || name[i2] > '9')
				return -1;
			v = v * 10u + (unsigned)(name[i2] - '0');
			if (v >= RC_AR_KNOB_COUNT)
				return -1;
		}
		return (int)v;
	}
	return -1;
}

const char *rc_ar_knob_name(unsigned id)
{
	return id < RC_AR_KNOB_COUNT ? rc_ar_knob_names[id] : NULL;
}