/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_rate_ctl_logic.h - portable pure logic of the Hi5622V100
 * rate-control / alg_cfg command surface (clean-room).
 *
 * Original code for the project's clean-room driver.  Every constant and
 * wire layout below is a decoded fact about the vendor firmware command
 * contract, documented in ../REPORT.md and ../TABLES.md (the vendor module
 * stays local and is never committed).  This translation unit has no kernel
 * dependency so the host selftest can compile it directly.
 *
 * The request object is the protocol-level "alg cfg" command:
 *   alg_cfg blob = 10-byte header + value[], carried by the proven HCC
 *   group-0 / id-2 config event with key 0x0101 (the calibration ladder's
 *   carrier).  The firmware answers with a g1 id3 config-sync frame whose
 *   body is the request blob with dev_ret updated (SET) / value filled (GET).
 *
 * Safety model: construction only.  Nothing here writes a register, sends a
 * frame or reads the device; the caller passes the request through the
 * kernel-side gate (the hi5622_rate_ctl.c fail-closed glue).
 */
#ifndef HI5622_RATE_CTL_LOGIC_H
#define HI5622_RATE_CTL_LOGIC_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/stddef.h>
/* the kernel has no stdint.h: expose the fixed-width names this unit uses */
typedef uint8_t uint8_t;
typedef uint16_t uint16_t;
typedef uint32_t uint32_t;
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif

/* ---- the alg_cfg blob (vendor decode; REPORT section 4) [V] ---- */
#define RC_ALG_HDR_LEN		10u
#define RC_ALG_OFF_CFG_ID	0u
#define RC_ALG_OFF_TYPE		2u
#define RC_ALG_OFF_PKT_TYPE	3u
#define RC_ALG_OFF_DEV_RET	4u
#define RC_ALG_OFF_NEED_W4	8u
#define RC_ALG_VALUE_OFF	10u
#define RC_ALG_MAX_VALUE	260u
#define RC_ALG_MAX_BLOB		(RC_ALG_HDR_LEN + RC_ALG_MAX_VALUE)

/* ---- the proven config-event carrier [V] ---- */
#define RC_CFG_KEY		0x0101u		/* HCC g0 id2 config key */
#define RC_CFG_CARRIER_OVERHEAD	0x1cu		/* 12 hdr + 16 cfg wrapper */
#define RC_CFG_MAX_FRAME	(RC_CFG_CARRIER_OVERHEAD + RC_ALG_MAX_BLOB)
#define RC_CFG_FB_DEFAULT	4u		/* module context that answered */
#define RC_CFG_FB_MAX		0x12u

/* ---- cfg access type [V] ---- */
#define RC_CFG_SET		0u
#define RC_CFG_GET		1u

/* ---- packet-type field values (vendor g_ast_alg_cfg_pkt_type_info_table) ---- */
#define RC_PKT_UCAST_DATA	0u
#define RC_PKT_MCAST_DATA	1u
#define RC_PKT_BCAST_DATA	2u
#define RC_PKT_MUMIMO_DATA	3u
#define RC_PKT_OFDMA_DATA	4u
#define RC_PKT_UCAST_MGMT	5u
#define RC_PKT_MCAST_MGMT	6u
#define RC_PKT_BCAST_MGMT	7u
#define RC_PKT_PROT_CTRL	8u
#define RC_PKT_NDP		9u
#define RC_PKT_NDPA		10u
#define RC_PKT_RESP_CTRL	11u
#define RC_PKT_OTHER_CTRL	12u
#define RC_PKT_NONE		13u
#define RC_PKT_ANY		0xffu

/* ---- rate-control cfg ids (vendor process table) [V] ---- */
#define RC_CFG_RATE_MODE	401
#define RC_CFG_FEC_CODING	402
#define RC_CFG_MU_FEC_CODING	403
#define RC_CFG_PROTECT_MODE	404
#define RC_CFG_HE_LTF_TYPE	405
#define RC_CFG_DCM_ENABLE	406
#define RC_CFG_FREQ_BW_MODE	407
#define RC_CFG_DEBUG_LOG_SWITCH	408
#define RC_CFG_AR_DEV_PARAM	409
#define RC_CFG_TX_CNT		410
#define RC_CFG_HI_RATE		411
#define RC_CFG_HE_SIGB_RATE	412
#define RC_CFG_MU_TX_CNT	413
#define RC_CFG_MU_RATE		414
#define RC_CFG_MU_RTS_MODE	415
#define RC_CFG_LEGACY_RATE	416
#define RC_CFG_PREAMBLE_TYPE	417
#define RC_CFG_GI_TYPE		418
#define RC_CFG_AR_PROBE		419
#define RC_CFG_SIGB_DCM		420
#define RC_CFG_CURRENT_RATION	421
#define RC_CFG_AR_SCEN_MODE	422
#define RC_CFG_TPC_MODE		1801
#define RC_CFG_TPC_POW_LVL	1802
#define RC_CFG_TPC_CODE		1803
#define RC_CFG_POW_MODE		1804
#define RC_CFG_RESP_CHAIN_SEL	1805
#define RC_CFG_SET_STA_ID	1806
#define RC_CFG_FAR_DIST_SWITCH	1807
#define RC_CFG_NEAR_DIST_SWITCH	1808
#define RC_CFG_WATERFILLING	1809

/* ---- rate_mode enum (g_ast_alg_cfg_rate_mode_tbl) [V] ---- */
#define RC_RATE_MODE_AUTO	0u
#define RC_RATE_MODE_FIXED	1u

/* ---- tpc_pow_lvl mode enum (g_ast_alg_cfg_tpc_fix_pow_mode_tbl) [V] ---- */
#define RC_TPC_POW_DATA		0u
#define RC_TPC_POW_MAG		1u
#define RC_TPC_POW_CTRL		2u

/* ---- pow_mode enum (g_ast_alg_cfg_tpc_pow_mode_tbl) [V] ---- */
#define RC_TPC_MODE_INIT	0u
#define RC_TPC_MODE_SLEEP	1u
#define RC_TPC_MODE_THIRD	2u

/* ---- the decoded per-command blob lengths (vendor LUT+14) [V] ---- */
#define RC_LEN_RATE_MODE	11u
#define RC_LEN_AR_DEV_PARAM	15u
#define RC_LEN_TPC_POW_LVL	12u
#define RC_LEN_TPC_MODE		11u
#define RC_LEN_POW_MODE		11u
#define RC_LEN_RESP_CHAIN_SEL	11u
#define RC_LEN_WATERFILLING	11u
#define RC_LEN_CURRENT_RATION	30u

/* ---- the 26 ar_dev_param knobs (vendor g_ast_alg_cfg_ar_dev_param_tbl) ---- */
#define RC_AR_KNOB_COUNT	26u

/* A protocol-level alg config request (what the carrier carries). */
struct rc_req {
	uint16_t cfg_id;
	uint8_t  cfg_type;		/* RC_CFG_SET / RC_CFG_GET */
	uint8_t  pkt_type;		/* RC_PKT_* */
	uint16_t value_len;
	uint16_t blob_len;		/* 10 + value_len for the known commands */
	uint8_t  value[RC_ALG_MAX_VALUE];
};

/* The firmware's config-sync answer (g1 id3, syn 0x0101), decoded. */
struct rc_sync {
	uint16_t cfg_id;
	uint8_t  cfg_type;
	uint8_t  pkt_type;
	uint32_t dev_ret;
	uint8_t  need_w4;
	uint16_t body_len;
	uint16_t value_len;
	uint8_t  value[RC_ALG_MAX_VALUE];
};

/* A fail-closed policy snapshot (filled by the kernel glue from its params). */
struct rc_policy {
	bool     enabled;		/* rate_ctl != 0 */
	bool     arbitrary;		/* rate_ctl_arbitrary != 0 */
	uint32_t budget;		/* per-load frame budget */
	uint32_t sent;			/* frames already committed this load */
};

enum rc_op {
	RC_OP_RATE_MODE_SET = 0,
	RC_OP_RATE_MODE_GET,
	RC_OP_AR_SET,
	RC_OP_AR_GET,
	RC_OP_TPC_POW_LVL_SET,
	RC_OP_TPC_GET,
	RC_OP_RATION_GET,
	RC_OP_ARBITRARY,
};

enum rc_decision {
	RC_DEC_ALLOW = 0,
	RC_DEC_DISABLED,
	RC_DEC_OP_UNKNOWN,
	RC_DEC_ID_NOT_ALLOWED,
	RC_DEC_BAD_VALUE,
	RC_DEC_ARBITRARY_OFF,
	RC_DEC_BAD_LEN,
	RC_DEC_BAD_TYPE,
	RC_DEC_BUDGET,
};

/* ---- builders (return 0 on success, negative errno-style on refusal) ---- */
int rc_req_rate_mode(struct rc_req *req, unsigned mode);
int rc_req_rate_mode_get(struct rc_req *req);
int rc_req_ar_dev_param(struct rc_req *req, const char *knob, unsigned long value);
int rc_req_ar_dev_param_get(struct rc_req *req, const char *knob);
int rc_req_tpc_pow_lvl(struct rc_req *req, unsigned mode, unsigned level);
int rc_req_tpc_get(struct rc_req *req, uint16_t cfg_id);
int rc_req_ration_get(struct rc_req *req, const uint8_t mac[6]);
int rc_req_raw(struct rc_req *req, uint16_t cfg_id, uint8_t cfg_type,
	       uint8_t pkt_type, const uint8_t *value, uint16_t value_len);

/* Build the alg_cfg blob bytes for a request; validates id/len/type. */
int rc_blob_build(const struct rc_req *req, uint8_t *blob, size_t cap,
		  size_t *len);

/* Decode a g1 id3 config-sync payload; returns 0 when it is a well-formed
 * key-0x0101 alg answer, negative otherwise (never touches the device). */
int rc_parse_cfg_sync(uint8_t group, uint16_t id, const uint8_t *payload,
		      size_t payload_len, struct rc_sync *out);

/* The fail-closed decision matrix (pure). */
enum rc_decision rc_check(const struct rc_policy *pol, const struct rc_req *req,
			  enum rc_op op);

/* ---- decoded enum/name lookups (NULL / -1 when unknown) ---- */
const char *rc_name_rate_mode(unsigned value);
const char *rc_name_tpc_pow_mode(unsigned value);
const char *rc_name_tpc_mode(unsigned value);
const char *rc_name_tpc_fix_pow_mode(unsigned value);
const char *rc_name_gi_type(unsigned value);
const char *rc_name_ar_probe(unsigned value);
const char *rc_name_ar_scen_mode(unsigned value);
const char *rc_name_legacy_rate(unsigned value);
const char *rc_name_protocol(unsigned value);
const char *rc_name_decision(enum rc_decision dec);
int rc_ar_knob_by_name(const char *name);
const char *rc_ar_knob_name(unsigned id);

#endif /* HI5622_RATE_CTL_LOGIC_H */