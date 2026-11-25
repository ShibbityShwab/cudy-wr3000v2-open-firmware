// SPDX-License-Identifier: GPL-2.0
/*
 * frame_crosscheck_selftest.c - host cross-check between the portable
 * rate-control logic (src/hi5622_rate_ctl_logic.c) and the driver's exact
 * frame library (h2d_frames.c / h2d_send_logic.c, wifi-h2d-send lane).
 *
 * Asserts, for every command the kernel surface can emit:
 *   - the alg_cfg blob produced by rc_blob_build is exactly the payload the
 *     g0 id2 / key-0x0101 builder puts on the wire (blob length included);
 *   - the wire header: group 0, id 2, tag 0x5A5A, declared total = 28 + blob;
 *   - the payload wrapper: token[8] zero | key 0x0101 | len = blob_len | blob
 *     | 4-byte zero tail;
 *   - the SR node word (total << 16) | 0x6D2B from h2d_plan_post.
 *
 * Build (from the patched tree, see tools/run-host-tests.sh):
 *   cc -std=c99 -Wall -Wextra -Werror -pedantic -I. -I<lane>/src \
 *      <lane>/tools/frame_crosscheck_selftest.c hi5622_rate_ctl_logic.c \
 *      h2d_send_logic.c h2d_frames.c hcc_core.c
 */
#include <stdio.h>
#include <string.h>

#include "h2d_frames.h"
#include "h2d_send_logic.h"
#include "hcc_core.h"
#include "hi5622_rate_ctl_logic.h"

static int checks_total;
static int checks_fail;

static void check(const char *name, int cond)
{
	checks_total++;
	if (!cond) {
		checks_fail++;
		printf("FAIL %s\n", name);
	}
}

static void test_one(const char *name, const struct rc_req *req)
{
	struct h2d_frame f;
	struct h2d_post_plan plan;
	enum h2d_err err = H2D_OK;
	unsigned char wire[H2D_MAX_TOTAL];
	unsigned char blob[RC_ALG_MAX_BLOB];
	size_t blob_len = 0;
	size_t wire_len = 0;
	u16 total, len_field;
	u32 idx;

	if (rc_blob_build(req, blob, sizeof(blob), &blob_len) != 0) {
		check(name, 0);
		return;
	}
	if (h2d_build_alg_cfg(&f, 0u, RC_CFG_FB_DEFAULT, req->cfg_id,
			      req->cfg_type, req->pkt_type, req->value,
			      req->value_len, &err) != 0) {
		printf("FAIL %s: builder refused (%s)\n", name,
		       h2d_strerror(err));
		check(name, 0);
		return;
	}
	if (h2d_frame_encode(&f, wire, sizeof(wire), &wire_len, &err) != 0) {
		printf("FAIL %s: encode refused (%s)\n", name,
		       h2d_strerror(err));
		check(name, 0);
		return;
	}
	total = (u16)(wire[4] | ((u16)wire[5] << 8));
	len_field = (u16)(wire[22] | ((u16)wire[23] << 8));
	check("group 0", (wire[0] & 0x0fu) == 0u);
	check("id 2", (wire[6] | (wire[7] << 8)) == 2);
	check("tag 0x5a5a", (wire[10] | (wire[11] << 8)) == 0x5a5au);
	check("total = 28 + blob", total == (u16)(RC_CFG_CARRIER_OVERHEAD + blob_len));
	check("wire length == total", wire_len == total);
	check("field B default", wire[3] == RC_CFG_FB_DEFAULT);
	check("token zero", memcmp(wire + 12, "\0\0\0\0\0\0\0\0", 8) == 0);
	check("key 0x0101", (wire[20] | (wire[21] << 8)) == RC_CFG_KEY);
	check("len field == blob len", len_field == blob_len);
	check("blob on the wire", memcmp(wire + 24, blob, blob_len) == 0);
	check("tail zero", memcmp(wire + 24 + blob_len, "\0\0\0\0", 4) == 0);
	if (h2d_plan_post(&f, 0u, 32u, 0x08000000u, 0u, 0u, &plan, &err) != 0) {
		printf("FAIL %s: post plan refused (%s)\n", name,
		       h2d_strerror(err));
		check(name, 0);
		return;
	}
	check("node word1 = (total<<16)|0x6d2b",
	      plan.sr.node_word1 == (((u32)total << 16) | H2D_NODE_FLAGS));
	check("node total", plan.frame_total == total);
	/* the node index producer arithmetic is not command dependent */
	idx = h2d_idx_plus(0u, 32u);
	check("producer arithmetic", idx == 1u);
	check(name, 1);
}

int main(void)
{
	struct rc_req req;
	static const unsigned char mac[6] = {2, 0, 0, 0, 0, 1};

	check("SET rate_mode auto",
	      rc_req_rate_mode(&req, RC_RATE_MODE_AUTO) == 0);
	test_one("frame: SET rate_mode auto", &req);
	check("SET rate_mode fixed",
	      rc_req_rate_mode(&req, RC_RATE_MODE_FIXED) == 0);
	test_one("frame: SET rate_mode fixed", &req);
	check("GET rate_mode", rc_req_rate_mode_get(&req) == 0);
	test_one("frame: GET rate_mode", &req);
	check("SET ar_dev_param",
	      rc_req_ar_dev_param(&req, "probe_per_init_strategy", 7) == 0);
	test_one("frame: SET ar_dev_param", &req);
	check("GET ar_dev_param",
	      rc_req_ar_dev_param_get(&req, "0") == 0);
	test_one("frame: GET ar_dev_param", &req);
	check("SET tpc_pow_lvl",
	      rc_req_tpc_pow_lvl(&req, RC_TPC_POW_CTRL, 3) == 0);
	test_one("frame: SET tpc_pow_lvl", &req);
	check("GET tpc_mode", rc_req_tpc_get(&req, RC_CFG_TPC_MODE) == 0);
	test_one("frame: GET tpc_mode", &req);
	check("GET tpc_pow_lvl", rc_req_tpc_get(&req, RC_CFG_TPC_POW_LVL) == 0);
	test_one("frame: GET tpc_pow_lvl", &req);
	check("GET current_ration", rc_req_ration_get(&req, mac) == 0);
	test_one("frame: GET current_ration", &req);

	printf("frame_crosscheck_selftest: %d/%d checks PASS\n",
	       checks_total - checks_fail, checks_total);
	return checks_fail == 0 ? 0 : 1;
}