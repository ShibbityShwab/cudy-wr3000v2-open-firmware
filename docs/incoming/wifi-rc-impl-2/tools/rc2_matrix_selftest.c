// SPDX-License-Identifier: GPL-2.0
/*
 * rate_ctl_logic_selftest.c - device-free host selftest of the portable
 * rate-control / alg_cfg logic (clean-room driver, wifi-rate-ctl lane).
 *
 * Builds only src/hi5622_rate_ctl_logic.c (no kernel tree, no device, no
 * vendor artifact) and asserts the decoded wire contract:
 *   - the exact alg_cfg blob bytes for every allowed command (KAT);
 *   - the blob builder's strict refusals (ids/lengths/types/bounds);
 *   - the g1 id3 config-sync parser (valid SET/GET, every malformed shape);
 *   - the fail-closed decision matrix (disabled, budget, id allowlist,
 *     value ranges, arbitrary-probe gate);
 *   - the decoded enum/name tables.
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_rate_ctl_logic.h"

static int checks_total;
static int checks_fail;

static void check(const char *name, int cond, const char *detail)
{
	checks_total++;
	if (!cond) {
		checks_fail++;
		printf("FAIL %s%s%s\n", name, detail ? " - " : "",
		       detail ? detail : "");
	}
}

static void check_blob(const char *name, const struct rc_req *req,
		       const unsigned char *want, size_t want_len)
{
	unsigned char blob[RC_ALG_MAX_BLOB];
	size_t len = 0;

	if (rc_blob_build(req, blob, sizeof(blob), &len) != 0) {
		check(name, 0, "blob_build refused");
		return;
	}
	if (len != want_len || memcmp(blob, want, want_len) != 0) {
		size_t i;
		char detail[256];

		snprintf(detail, sizeof(detail), "len %zu != %zu", len, want_len);
		check(name, 0, detail);
		printf("  got ");
		for (i = 0; i < len && i < 32; i++)
			printf("%02x ", blob[i]);
		printf("\n  want ");
		for (i = 0; i < want_len && i < 32; i++)
			printf("%02x ", want[i]);
		printf("\n");
		return;
	}
	check(name, 1, NULL);
}

int main(void)
{
	struct rc_req req;
	struct rc_policy pol;
	struct rc_sync sync;
	static const unsigned char test_mac[6] = {2, 0, 0, 0, 0, 1};
	unsigned char blob[RC_ALG_MAX_BLOB];
	size_t len = 0;
	int rc;

	/* ============ 1. exact blob KATs ============ */
	check("rate_mode auto builder", rc_req_rate_mode(&req, RC_RATE_MODE_AUTO) == 0, NULL);
	{
		static const unsigned char want[] = {
			0x91, 0x01, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x00,
			0x01, 0x00, 0x00,
		};
		check_blob("SET rate_mode auto blob", &req, want, sizeof(want));
	}
	check("rate_mode fixed builder", rc_req_rate_mode(&req, RC_RATE_MODE_FIXED) == 0, NULL);
	{
		static const unsigned char want[] = {
			0x91, 0x01, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x00,
			0x01, 0x00, 0x01,
		};
		check_blob("SET rate_mode fixed blob", &req, want, sizeof(want));
	}
	check("rate_mode invalid value refused",
	      rc_req_rate_mode(&req, 2) != 0, NULL);
	check("GET rate_mode builder", rc_req_rate_mode_get(&req) == 0, NULL);
	{
		static const unsigned char want[] = {
			0x91, 0x01, 0x01, 0x0d, 0x00, 0x00, 0x00, 0x00,
			0x01, 0x00, 0x00,
		};
		check_blob("GET rate_mode blob", &req, want, sizeof(want));
	}
	check("ar_dev_param builder",
	      rc_req_ar_dev_param(&req, "tx_rate_aging_time", 100) == 0, NULL);
	{
		static const unsigned char want[] = {
			0x99, 0x01, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x00,
			0x01, 0x00, 0x0c, 0x64, 0x00, 0x00, 0x00,
		};
		check_blob("SET ar_dev_param blob", &req, want, sizeof(want));
	}
	check("ar_dev_param decimal id",
	      rc_req_ar_dev_param(&req, "12", 0x11223344ul) == 0, NULL);
	{
		static const unsigned char want[] = {
			0x99, 0x01, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x00,
			0x01, 0x00, 0x0c, 0x44, 0x33, 0x22, 0x11,
		};
		check_blob("SET ar_dev_param u32 LE blob", &req, want, sizeof(want));
	}
	check("ar_dev_param unknown name refused",
	      rc_req_ar_dev_param(&req, "no_such_knob", 1) != 0, NULL);
	check("ar_dev_param out-of-range id refused",
	      rc_req_ar_dev_param(&req, "26", 1) != 0, NULL);
	check("ar_dev_param get builder",
	      rc_req_ar_dev_param_get(&req, "probe_per_init_strategy") == 0, NULL);
	{
		static const unsigned char want[] = {
			0x99, 0x01, 0x01, 0x0d, 0x00, 0x00, 0x00, 0x00,
			0x01, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x00,
		};
		check_blob("GET ar_dev_param blob", &req, want, sizeof(want));
	}
	check("tpc_pow_lvl builder",
	      rc_req_tpc_pow_lvl(&req, RC_TPC_POW_CTRL, 3) == 0, NULL);
	{
		static const unsigned char want[] = {
			0x0a, 0x07, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x00,
			0x01, 0x00, 0x02, 0x03,
		};
		check_blob("SET tpc_pow_lvl blob", &req, want, sizeof(want));
	}
	check("tpc_pow_lvl invalid mode refused",
	      rc_req_tpc_pow_lvl(&req, 3, 0) != 0, NULL);
	check("GET tpc_mode builder", rc_req_tpc_get(&req, RC_CFG_TPC_MODE) == 0, NULL);
	{
		static const unsigned char want[] = {
			0x09, 0x07, 0x01, 0x0d, 0x00, 0x00, 0x00, 0x00,
			0x01, 0x00, 0x00,
		};
		check_blob("GET tpc_mode blob", &req, want, sizeof(want));
	}
	check("GET current_ration builder",
	      rc_req_ration_get(&req, test_mac) == 0, NULL);
	{
		static const unsigned char want[] = {
			0xa5, 0x01, 0x01, 0x0d, 0x00, 0x00, 0x00, 0x00,
			0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		};
		check_blob("GET current_ration blob", &req, want, sizeof(want));
	}

	/* ============ 2. blob builder refusals ============ */
	check("rate_mode get builder ok", rc_req_rate_mode_get(&req) == 0, NULL);
	req.value_len = 2;	/* forged mismatch */
	check("blob refuses forged value_len",
	      rc_blob_build(&req, blob, sizeof(blob), &len) != 0, NULL);
	check("blob refuses small cap",
	      rc_blob_build(&req, blob, 4, &len) != 0, NULL);
	check("raw builder refuses type > GET",
	      rc_req_raw(&req, RC_CFG_RATE_MODE, 2, 0, NULL, 0) != 0, NULL);
	check("raw builder refuses oversized value",
	      rc_req_raw(&req, RC_CFG_RATE_MODE, RC_CFG_SET, 0, NULL,
			 RC_ALG_MAX_VALUE + 1u) != 0, NULL);
	check("raw builder refuses pkt_type overflow",
	      rc_req_raw(&req, RC_CFG_RATE_MODE, RC_CFG_SET, RC_PKT_NONE + 1u,
			 NULL, 0) != 0, NULL);

	/* ============ 3. config-sync parser ============ */
	{
		/* a well-formed g1 id3 SET answer for cfg 401, dev_ret 0 */
		static const unsigned char p[] = {
			0, 0, 0, 0,		/* cookie0 */
			0, 0, 0, 0,		/* cookie1 */
			0x01, 0x01,		/* syn_id 0x0101 */
			0x0b, 0x00,		/* body_len 11 */
			0x91, 0x01, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x00,
			0x01, 0x00, 0x00,	/* value: auto */
			0, 0, 0, 0,		/* pad */
		};
		rc = rc_parse_cfg_sync(1, 3, p, sizeof(p), &sync);
		check("sync parse SET answer", rc == 0, NULL);
		check("sync cfg id 401", sync.cfg_id == 401u, NULL);
		check("sync cfg type SET", sync.cfg_type == RC_CFG_SET, NULL);
		check("sync dev_ret 0", sync.dev_ret == 0u, NULL);
		check("sync need_w4 1", sync.need_w4 == 1u, NULL);
		check("sync value len 1", sync.value_len == 1u, NULL);
		check("sync value auto", sync.value[0] == RC_RATE_MODE_AUTO, NULL);
	}
	{
		/* a GET answer with a non-zero dev_ret and a value */
		unsigned char p[32];
		memset(p, 0, sizeof(p));
		p[8] = 0x01; p[9] = 0x01;
		p[10] = 0x0b; p[11] = 0x00;
		p[12] = 0x91; p[13] = 0x01; p[14] = 0x01; p[15] = 0x00;
		p[16] = 0x55; p[17] = 0x00; p[18] = 0x00; p[19] = 0x00;
		p[20] = 0x01; p[21] = 0x00;
		p[22] = 0x01;	/* fixed */
		rc = rc_parse_cfg_sync(1, 3, p, sizeof(p), &sync);
		check("sync parse GET answer", rc == 0, NULL);
		check("sync GET type", sync.cfg_type == RC_CFG_GET, NULL);
		check("sync GET dev_ret 0x55", sync.dev_ret == 0x55u, NULL);
		check("sync GET value fixed", sync.value[0] == RC_RATE_MODE_FIXED, NULL);
	}
	{
		unsigned char p[32];
		memset(p, 0, sizeof(p));
		p[8] = 0x01; p[9] = 0x01; p[10] = 0x0b; p[11] = 0x00;
		check("sync rejects wrong group",
		      rc_parse_cfg_sync(0, 3, p, sizeof(p), &sync) != 0, NULL);
		check("sync rejects wrong id",
		      rc_parse_cfg_sync(1, 4, p, sizeof(p), &sync) != 0, NULL);
		check("sync rejects short payload",
		      rc_parse_cfg_sync(1, 3, p, 15, &sync) != 0, NULL);
		check("sync rejects body over payload",
		      rc_parse_cfg_sync(1, 3, p, 16, &sync) != 0, NULL);
		p[9] = 0x02;	/* syn 0x0201 */
		check("sync rejects other syn",
		      rc_parse_cfg_sync(1, 3, p, sizeof(p), &sync) != 0, NULL);
		p[9] = 0x01;
		p[10] = 0x02; p[11] = 0x00;	/* body_len 2 < header */
		check("sync rejects short body",
		      rc_parse_cfg_sync(1, 3, p, sizeof(p), &sync) != 0, NULL);
		check("sync rejects NULL payload",
		      rc_parse_cfg_sync(1, 3, NULL, 20, &sync) != 0, NULL);
	}

	/* ============ 4. corrected-C2 precondition matrix (RC-2) ============
	 * Every rate_ctl write now requires the ordered ladder proven this load:
	 * a TYPE-1 context-create, its ctx4+0x190 attestation, and the ordered
	 * TXQ sync (fw-h2d-stall task-181; the earlier RC fire wrote type 3). */
	memset(&pol, 0, sizeof(pol));
	pol.enabled = true;
	pol.budget = 8;
	pol.sent = 0;
	check("precond refuses without a type-1 create",
	      rc_precond_check(&pol) == RC_DEC_NO_CTX_TYPE1, NULL);
	pol.ctx_type1 = true;
	check("precond refuses without the ctx190 attestation",
	      rc_precond_check(&pol) == RC_DEC_NO_CTX_ATTEST, NULL);
	pol.ctx_attested = true;
	check("precond refuses without the ordered txq sync",
	      rc_precond_check(&pol) == RC_DEC_NO_TXQ_SYNC, NULL);
	pol.txq_sent = true;
	check("precond allows the fully proven ladder",
	      rc_precond_check(&pol) == RC_DEC_ALLOW, NULL);
	check("precond name",
	      strcmp(rc_name_decision(RC_DEC_NO_CTX_TYPE1),
		     "ctx_type1_required") == 0 &&
	      strcmp(rc_name_decision(RC_DEC_NO_CTX_ATTEST),
		     "ctx190_attest_required") == 0 &&
	      strcmp(rc_name_decision(RC_DEC_NO_TXQ_SYNC),
		     "txq_sync72_required") == 0, NULL);
	check("precond gates the rate SET (type 3 state refuses)",
	      (rc_req_rate_mode(&req, RC_RATE_MODE_AUTO) == 0) &&
	      (memset(&pol, 0, sizeof(pol)), pol.enabled = true, pol.budget = 8,
	       pol.ctx_type1 = false, pol.ctx_attested = true, pol.txq_sent = true,
	       rc_check(&pol, &req, RC_OP_RATE_MODE_SET) ==
						RC_DEC_NO_CTX_TYPE1), NULL);

	/* ============ 4b. fail-closed decision matrix ============ */
	memset(&pol, 0, sizeof(pol));
	pol.enabled = true;
	pol.budget = 8;
	pol.sent = 0;
	pol.ctx_type1 = true;
	pol.ctx_attested = true;
	pol.txq_sent = true;
	check("policy allows SET rate_mode auto",
	      (rc_req_rate_mode(&req, RC_RATE_MODE_AUTO) == 0) &&
	      rc_check(&pol, &req, RC_OP_RATE_MODE_SET) == RC_DEC_ALLOW, NULL);
	check("policy allows GET rate_mode",
	      (rc_req_rate_mode_get(&req) == 0) &&
	      rc_check(&pol, &req, RC_OP_RATE_MODE_GET) == RC_DEC_ALLOW, NULL);
	check("policy allows SET ar_dev_param",
	      (rc_req_ar_dev_param(&req, "gi1_with_2xltf_en", 1) == 0) &&
	      rc_check(&pol, &req, RC_OP_AR_SET) == RC_DEC_ALLOW, NULL);
	check("policy allows SET tpc_pow_lvl",
	      (rc_req_tpc_pow_lvl(&req, RC_TPC_POW_DATA, 10) == 0) &&
	      rc_check(&pol, &req, RC_OP_TPC_POW_LVL_SET) == RC_DEC_ALLOW, NULL);
	check("policy allows GET current_ration",
	      (rc_req_ration_get(&req, test_mac) == 0) &&
	      rc_check(&pol, &req, RC_OP_RATION_GET) == RC_DEC_ALLOW, NULL);
	pol.enabled = false;
	check("policy refuses when disabled",
	      rc_check(&pol, &req, RC_OP_RATION_GET) == RC_DEC_DISABLED, NULL);
	pol.enabled = true;
	pol.sent = pol.budget;
	check("policy refuses when budget exhausted",
	      rc_check(&pol, &req, RC_OP_RATION_GET) == RC_DEC_BUDGET, NULL);
	pol.sent = 0;
	check("policy refuses rate mode via the wrong op",
	      (rc_req_rate_mode_get(&req) == 0) &&
	      rc_check(&pol, &req, RC_OP_RATE_MODE_SET) == RC_DEC_ID_NOT_ALLOWED,
	      NULL);
	check("policy refuses GET via the SET op",
	      (rc_req_rate_mode_get(&req) == 0) &&
	      rc_check(&pol, &req, RC_OP_AR_SET) == RC_DEC_ID_NOT_ALLOWED, NULL);
	pol.arbitrary = false;
	check("arbitrary probe refused by default",
	      (rc_req_raw(&req, RC_CFG_LEGACY_RATE, RC_CFG_SET, RC_PKT_UCAST_DATA,
			  NULL, 0) == 0) &&
	      rc_check(&pol, &req, RC_OP_ARBITRARY) == RC_DEC_ARBITRARY_OFF, NULL);
	pol.arbitrary = true;
	check("arbitrary probe refused for dedicated-parser ids",
	      (rc_req_raw(&req, RC_CFG_LEGACY_RATE, RC_CFG_SET, RC_PKT_UCAST_DATA,
			  NULL, 0) == 0) &&
	      rc_check(&pol, &req, RC_OP_ARBITRARY) == RC_DEC_ID_NOT_ALLOWED, NULL);
	check("arbitrary probe allows a documented id",
	      (rc_req_raw(&req, RC_CFG_DEBUG_LOG_SWITCH, RC_CFG_SET, RC_PKT_NONE,
			  (const unsigned char *)"\x01", 1) == 0) &&
	      rc_check(&pol, &req, RC_OP_ARBITRARY) == RC_DEC_ALLOW, NULL);

	/* ============ 5. decoded names/enums ============ */
	check("rate_mode names", strcmp(rc_name_rate_mode(0), "auto") == 0 &&
	      strcmp(rc_name_rate_mode(1), "fixed") == 0, NULL);
	check("tpc fix names", strcmp(rc_name_tpc_fix_pow_mode(2), "ctrl") == 0, NULL);
	check("tpc pow names", strcmp(rc_name_tpc_pow_mode(1), "sleep") == 0, NULL);
	check("gi names", strcmp(rc_name_gi_type(1), "fixed") == 0, NULL);
	check("ar_probe names", strcmp(rc_name_ar_probe(1), "slow") == 0, NULL);
	check("ar_scen names", strcmp(rc_name_ar_scen_mode(1), "move") == 0, NULL);
	check("legacy rate names",
	      strcmp(rc_name_legacy_rate(11), "6M") == 0 &&
	      strcmp(rc_name_legacy_rate(12), "54M") == 0 &&
	      rc_name_legacy_rate(4) == NULL, NULL);
	check("protocol names",
	      strcmp(rc_name_protocol(4), "11ac") == 0 &&
	      strcmp(rc_name_protocol(8), "11ax_su") == 0 &&
	      rc_name_protocol(5) == NULL, NULL);
	check("knob count", rc_ar_knob_name(25) != NULL &&
	      rc_ar_knob_name(26) == NULL, NULL);
	check("knob 0 name",
	      strcmp(rc_ar_knob_name(0), "sudden_good_delta_gdpt_ratio") == 0, NULL);
	check("knob decimal by name",
	      rc_ar_knob_by_name("12") == 12 && rc_ar_knob_by_name("26") == -1 &&
	      rc_ar_knob_by_name("nope") == -1, NULL);
	check("decision names",
	      strcmp(rc_name_decision(RC_DEC_ALLOW), "allow") == 0 &&
	      strcmp(rc_name_decision(RC_DEC_BUDGET), "budget") == 0, NULL);

	printf("rc2_matrix_selftest: %d passed, %d failed, %d checks\n",
	       checks_total - checks_fail, checks_fail, checks_total);
	return checks_fail == 0 ? 0 : 1;
}