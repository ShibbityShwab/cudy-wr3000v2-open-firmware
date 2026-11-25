/* SPDX-License-Identifier: GPL-2.0 */
/*
 * vap_start_g0_selftest.c - host selftest for the H2D g0-id1 key 0x0065
 * VAP-start step (lane wifi-band1-start-send-3, task 209).
 *
 * ORIGINAL clean-room test code.  Compiles the REAL patched-tree model
 * (hi5622_vap_start_g0.c), the REAL AP-start plan model
 * (hi5622_scan_logic.c) and the REAL sibling builders (vapstart_frames.c for
 * key 0x0065, hi5622_band_start.c for key 0x0242) with a plain C99 host
 * compiler - no kernel, no device, no firmware.  It asserts:
 *   - the byte-exact 40-B key-0x0065 frame (and its cross-check against the
 *     independent sibling builder for every module index 0..0x12);
 *   - the AP-start plan order: vap_add -> vap_start_g0 -> desc_table ->
 *     dfs_mode -> band_start -> ap_chan -> ... ;
 *   - the fail-closed parameter/build matrix;
 *   - every G1..G14 probe gate and the G12 post-check.
 */
#include <stdio.h>
#include <string.h>

/* hi5622_scan_logic.h pulls hi5622_frames_api.h (the shared portable type
 * set incl. u64); the other headers then reuse it through the guard. */
#include "hi5622_scan_logic.h"
#include "hi5622_vap_start_g0.h"
#include "vapstart_frames.h"
#include "hi5622_band_start.h"

static int checks;
static int failures;

#define CK(cond, ...)							\
	do {								\
		checks++;						\
		if (!(cond)) {						\
			failures++;					\
			printf("FAIL %s:%d: ", __FILE__, __LINE__);	\
			printf(__VA_ARGS__);				\
			printf("\n");					\
		}							\
	} while (0)

static void hexify(const u8 *buf, size_t len, char *out)
{
	static const char d[] = "0123456789abcdef";
	size_t i;

	for (i = 0; i < len; i++) {
		out[2 * i] = d[buf[i] >> 4];
		out[2 * i + 1] = d[buf[i] & 0x0fu];
	}
	out[2 * len] = '\0';
}

/* the required 40-B frame, module (field B) 0 - the fire KAT */
static const char KAT40_M0[] =
	"000100002800010000005a5a000000000100000065000c00"
	"000101000000000000000000ffffffff";
static const char KAT40_M4[] =
	"000100042800010000005a5a000000000100000065000c00"
	"000101000000000000000000ffffffff";

static void probe_default(struct hi5622_vap_start_g0_probe *p)
{
	memset(p, 0, sizeof(*p));
	p->mctx = 0x40000010u;
	p->type = 2u;			/* AP create type */
	p->band = 1u;			/* 5 GHz */
	p->field = 0u;
	p->bandobject = 0x50000020u;
	p->state0 = 0u;
	p->state1 = 0u;
	p->state4 = 0x00u;
	p->count = 1u;
	p->entry0 = 0u;
	p->published = 0x00000001u;
	p->d6e = 0u;
	p->ec8 = 0u;
	p->b190 = 0x60000030u;
	p->b440 = 0u;
	p->b4ac = 0x70000040u;
	p->plink6c6 = 0u;
	p->expected_index = 0u;
}

static void test_frame_kat(void)
{
	struct hi5622_vap_start_g0_plan pl;
	u8 wire[64];
	char hex[128];
	size_t len = 0;
	int rc;

	rc = hi5622_vap_start_g0_plan(1u, 1u, 1u, 0u, &pl);
	CK(rc == 0, "plan(1,1,1,0) rc=%d", rc);
	CK(pl.post == true, "plan(1,1,1,0) post=%d", (int)pl.post);
	CK(pl.module == 0u, "plan(1,1,1,0) module=%u", (unsigned)pl.module);
	rc = hi5622_vap_start_g0_build(wire, sizeof(wire), &len, &pl);
	CK(rc == 0, "build rc=%d", rc);
	CK(len == 40u, "wire len=%zu != 40", len);
	hexify(wire, len, hex);
	CK(strcmp(hex, KAT40_M0) == 0, "40-B KAT mismatch: %s", hex);
	CK(wire[0] == 0x00u, "hdr[0] group");
	CK(wire[1] == 0x01u, "hdr[1] alloc/dst");
	CK(wire[2] == 0x00u, "hdr[2] fa");
	CK(wire[3] == 0x00u, "hdr[3] field B = module 0");
	CK((wire[4] | (wire[5] << 8)) == 0x28u, "total field 0x28");
	CK((wire[6] | (wire[7] << 8)) == 1u, "msg id 1");
	CK((wire[10] | (wire[11] << 8)) == 0x5a5au, "tag 0x5a5a");
	CK(wire[16] == 0x01u, "token payload word at +4 = 1");
	CK((wire[20] | (wire[21] << 8)) == 0x0065u, "key 0x0065");
	CK((wire[22] | (wire[23] << 8)) == 12u, "body len 12");
	CK(wire[24] == 0x00u && wire[25] == 0x01u && wire[26] == 0x01u &&
	   wire[27] == 0x00u, "body arg/state/band/field");
	CK(wire[28] == 0u && wire[29] == 0u && wire[30] == 0u &&
	   wire[31] == 0u, "body word 0");
	CK(wire[32] == 0u && wire[33] == 0u && wire[34] == 0u &&
	   wire[35] == 0u, "body word tail");
	CK(wire[36] == 0xffu && wire[37] == 0xffu && wire[38] == 0xffu &&
	   wire[39] == 0xffu, "4-byte FF tail");
	CK(HI5622_VAP_START_G0_WIRE_LEN ==
	   HI5622_VAP_START_G0_HDR_LEN + HI5622_VAP_START_G0_TOKEN_LEN + 2u +
	   2u + HI5622_VAP_START_G0_BODY_LEN + 4u, "carrier geometry sum");
	CK(HI5622_VAP_START_G0_NODE_WORD1 ==
	   (((u32)HI5622_VAP_START_G0_WIRE_LEN << 16) | 0x6d2bu),
	   "node word1 0x%08x", (unsigned)HI5622_VAP_START_G0_NODE_WORD1);
	CK(HI5622_VAP_START_G0_TOTAL_FIELD ==
	   (u16)HI5622_VAP_START_G0_WIRE_LEN, "total field == wire len");

	/* field B tracks the module index (0x0064/0x0242 ctx agreement) */
	for (rc = 0; rc <= (int)HI5622_VAP_START_G0_MODULE_MAX; rc++) {
		u8 w[64];
		size_t l = 0;
		struct hi5622_vap_start_g0_plan p2;

		CK(hi5622_vap_start_g0_plan(1u, 1u, 1u, (unsigned)rc, &p2) == 0,
		   "plan module %d", rc);
		CK(hi5622_vap_start_g0_build(w, sizeof(w), &l, &p2) == 0,
		   "build module %d", rc);
		CK(l == 40u && w[3] == (u8)rc,
		   "module %d -> hdr[3]=%u", rc, (unsigned)w[3]);
	}
	{
		struct hi5622_vap_start_g0_plan p4;
		u8 w[64];
		size_t l = 0;

		CK(hi5622_vap_start_g0_plan(1u, 1u, 1u, 4u, &p4) == 0, "plan m4");
		CK(hi5622_vap_start_g0_build(w, sizeof(w), &l, &p4) == 0, "build m4");
		hexify(w, l, hex);
		CK(strcmp(hex, KAT40_M4) == 0, "module-4 KAT mismatch: %s", hex);
	}
}

/* independent cross-check: the sibling lane's real key-0x0065 builder
 * (vapstart_frames.c, shipped by wifi-vap-start) must produce the same
 * 40-B bytes for the same field values. */
static void test_sibling_cross_check(void)
{
	int m;

	for (m = 0; m <= (int)HI5622_VAP_START_G0_MODULE_MAX; m++) {
		struct vsf_frame sf;
		struct vsf_vap_start sp;
		struct hi5622_vap_start_g0_plan pl;
		u8 wire[64];
		size_t len = 0;

		memset(&sf, 0, sizeof(sf));
		memset(&sp, 0, sizeof(sp));
		sp.channel = 0u;
		sp.state = 1u;
		sp.cfg_a = 1u;
		sp.cfg_b = 0u;
		sp.cfg_word = 0u;
		sp.field_a = 0u;
		sp.field_b = (u8)m;
		sp.field_c = 0u;
		sp.dst = 0u;
		CK(vsf_build_vap_start(&sf, &sp) == 0, "sibling build m%d", m);
		CK(vsf_frame_validate(&sf) == 0, "sibling validate m%d", m);
		CK(hi5622_vap_start_g0_plan(1u, 1u, 1u, (unsigned)m, &pl) == 0,
		   "our plan m%d", m);
		CK(hi5622_vap_start_g0_build(wire, sizeof(wire), &len, &pl) == 0,
		   "our build m%d", m);
		CK(len == (size_t)sf.len && memcmp(wire, sf.buf, len) == 0,
		   "frame differs from the sibling 0x0065 builder at m=%d", m);
	}
}

static void test_plan_matrix(void)
{
	struct hi5622_vap_start_g0_plan pl;
	int rc;

	CK(hi5622_vap_start_g0_plan(0u, 0u, 1u, 0u, &pl) == 0 &&
	   !pl.post, "default off is a clean no-post");
	CK(hi5622_vap_start_g0_plan(0u, 1u, 1u, 0u, &pl) == 0 &&
	   !pl.post, "off with beacon_static is a clean no-post");
	CK(hi5622_vap_start_g0_plan(1u, 1u, 1u, 0u, &pl) == 0 && pl.post,
	   "rung armed on band 1");
	CK(hi5622_vap_start_g0_plan(1u, 1u, 0u, 0u, &pl) == 0 && !pl.post,
	   "band 0 is a clean no-post");
	rc = hi5622_vap_start_g0_plan(1u, 0u, 1u, 0u, &pl);
	CK(rc == -EINVAL, "vap_start_g0=1 without beacon_static -> %d", rc);
	rc = hi5622_vap_start_g0_plan(2u, 1u, 1u, 0u, &pl);
	CK(rc == -EINVAL, "vap_start_g0=2 -> %d", rc);
	rc = hi5622_vap_start_g0_plan(1u, 1u, 2u, 0u, &pl);
	CK(rc == -EINVAL, "band 2 -> %d", rc);
	rc = hi5622_vap_start_g0_plan(1u, 1u, 1u, 0x13u, &pl);
	CK(rc == -EINVAL, "module 0x13 -> %d", rc);
	CK(hi5622_vap_start_g0_plan(1u, 1u, 1u, 0x12u, &pl) == 0 &&
	   pl.module == 0x12u, "module 0x12 accepted");
	rc = hi5622_vap_start_g0_plan(1u, 1u, 1u, 0u, NULL);
	CK(rc == -EINVAL, "NULL out -> %d", rc);
}

static void test_build_matrix(void)
{
	struct hi5622_vap_start_g0_plan pl, off;
	u8 wire[64];
	size_t len = 0;
	int rc;

	CK(hi5622_vap_start_g0_plan(1u, 1u, 1u, 0u, &pl) == 0, "armed plan");
	CK(hi5622_vap_start_g0_plan(0u, 0u, 1u, 0u, &off) == 0, "off plan");
	rc = hi5622_vap_start_g0_build(NULL, sizeof(wire), &len, &pl);
	CK(rc == -EINVAL, "NULL wire -> %d", rc);
	rc = hi5622_vap_start_g0_build(wire, sizeof(wire), NULL, &pl);
	CK(rc == -EINVAL, "NULL out_len -> %d", rc);
	rc = hi5622_vap_start_g0_build(wire, sizeof(wire), &len, NULL);
	CK(rc == -EINVAL, "NULL plan -> %d", rc);
	rc = hi5622_vap_start_g0_build(wire, sizeof(wire), &len, &off);
	CK(rc == -EINVAL, "unarmed plan -> %d", rc);
	rc = hi5622_vap_start_g0_build(wire, 39u, &len, &pl);
	CK(rc == -ENOSPC, "cap 39 -> %d", rc);
	rc = hi5622_vap_start_g0_build(wire, 40u, &len, &pl);
	CK(rc == 0 && len == 40u, "cap 40 -> %d len %zu", rc, len);
	/* module drift inside the plan is refused by the builder too */
	{
		struct hi5622_vap_start_g0_plan bad = pl;

		bad.module = 0x13u;
		rc = hi5622_vap_start_g0_build(wire, sizeof(wire), &len, &bad);
		CK(rc == -EINVAL, "plan module 0x13 -> %d", rc);
	}
}

static void test_gates(void)
{
	struct hi5622_vap_start_g0_probe p;
	const char *v;
	int rc;

	probe_default(&p);
	rc = hi5622_vap_start_g0_precheck(&p);
	CK(rc == 0, "passing probe rejected: %d", rc);
	v = hi5622_vap_start_g0_verdict(&p);
	CK(strstr(v, "gates are open") != NULL, "passing verdict: %s", v);
	CK(hi5622_vap_start_g0_helper_ok(&p) == true, "G6 helper ok");

	CK(hi5622_vap_start_g0_precheck(NULL) == -EINVAL, "NULL probe");

	probe_default(&p); p.mctx = 0u;
	CK(hi5622_vap_start_g0_precheck(&p) == -ENODEV, "G1");
	CK(strstr(hi5622_vap_start_g0_verdict(&p), "G1") != NULL, "G1 text");

	probe_default(&p); p.bandobject = 0u;
	CK(hi5622_vap_start_g0_precheck(&p) == -ENOENT, "G2 bandobject");
	probe_default(&p); p.count = 2u;
	CK(hi5622_vap_start_g0_precheck(&p) == -ENOENT, "G2 count 2");
	probe_default(&p); p.count = 0u;
	CK(hi5622_vap_start_g0_precheck(&p) == -ENOENT, "G2 count 0");
	CK(strstr(hi5622_vap_start_g0_verdict(&p), "G2") != NULL, "G2 text");

	probe_default(&p); p.entry0 = 4u;
	CK(hi5622_vap_start_g0_precheck(&p) == -EEXIST, "G3 entry drift");
	CK(strstr(hi5622_vap_start_g0_verdict(&p), "G3") != NULL, "G3 text");

	probe_default(&p); p.published = 0u;
	CK(hi5622_vap_start_g0_precheck(&p) == -EAGAIN, "G4 published 0");
	CK(strstr(hi5622_vap_start_g0_verdict(&p), "G4") != NULL, "G4 text");

	probe_default(&p); p.b190 = 0u;
	CK(hi5622_vap_start_g0_precheck(&p) == -EFAULT, "G5 b190 0");
	v = hi5622_vap_start_g0_verdict(&p);
	CK(strstr(v, "G5") != NULL && strstr(v, "0x46470") != NULL,
	   "G5 text: %s", v);

	probe_default(&p); p.type = 3u;
	CK(hi5622_vap_start_g0_precheck(&p) == -EPROTO, "G7 type");
	CK(strstr(hi5622_vap_start_g0_verdict(&p), "G7") != NULL, "G7 text");

	probe_default(&p); p.band = 0u;
	CK(hi5622_vap_start_g0_precheck(&p) == -EINVAL, "G8 band");
	CK(strstr(hi5622_vap_start_g0_verdict(&p), "G8") != NULL, "G8 text");

	probe_default(&p); p.b440 = 1u;
	CK(hi5622_vap_start_g0_precheck(&p) == -EBUSY, "G9 b440");
	CK(strstr(hi5622_vap_start_g0_verdict(&p), "G9") != NULL, "G9 text");

	probe_default(&p); p.d6e = 1u;
	CK(hi5622_vap_start_g0_precheck(&p) == -EINVAL, "G10 d6e");
	CK(strstr(hi5622_vap_start_g0_verdict(&p), "G10") != NULL, "G10 text");

	probe_default(&p); p.ec8 = 1u;
	CK(hi5622_vap_start_g0_precheck(&p) == -EALREADY, "G11 ec8");
	CK(strstr(hi5622_vap_start_g0_verdict(&p), "G11") != NULL, "G11 text");

	probe_default(&p); p.state0 = 1u;
	CK(hi5622_vap_start_g0_precheck(&p) == -EBUSY, "G13 state0");
	probe_default(&p); p.state1 = 2u;
	CK(hi5622_vap_start_g0_precheck(&p) == -EBUSY, "G13 state1");
	probe_default(&p); p.state4 = 0x02u;
	CK(hi5622_vap_start_g0_precheck(&p) == -EBUSY, "G13 state4");
	CK(strstr(hi5622_vap_start_g0_verdict(&p), "G13") != NULL, "G13 text");
	probe_default(&p); p.state4 = 0xffu;
	CK(hi5622_vap_start_g0_precheck(&p) == 0, "G13 state4 0xFF accepted");

	/* G6 / G14 are observations only */
	probe_default(&p); p.b4ac = 0u;
	CK(hi5622_vap_start_g0_precheck(&p) == 0, "G6 does not gate");
	CK(hi5622_vap_start_g0_helper_ok(&p) == false, "G6 helper absent");
	probe_default(&p); p.plink6c6 = 1u;
	CK(hi5622_vap_start_g0_precheck(&p) == 0, "G14 does not gate");
}

static void test_g12(void)
{
	CK(hi5622_vap_start_g0_postcheck(1u) == 0, "G12 state 1 -> allow");
	CK(hi5622_vap_start_g0_postcheck(0u) == -EAGAIN, "G12 state 0");
	CK(hi5622_vap_start_g0_postcheck(2u) == -EAGAIN, "G12 state 2");
	CK(hi5622_vap_start_g0_postcheck(3u) == -EAGAIN, "G12 state 3");
	CK(hi5622_vap_start_g0_postcheck(4u) == -EAGAIN, "G12 state 4");
	CK(hi5622_vap_start_g0_postcheck(0xffu) == -EAGAIN, "G12 state 0xFF");
}

static void test_status_line(void)
{
	char buf[160];

	CK(hi5622_vap_start_g0_status_line(0u, 0u, -ENODATA, 0u, 0u,
					   buf, sizeof(buf)) > 0, "status off");
	CK(strstr(buf, "off") != NULL && strstr(buf, "posted=0") != NULL,
	   "status off text: %s", buf);
	CK(hi5622_vap_start_g0_status_line(1u, 1u, 0, 1u, 0u,
					   buf, sizeof(buf)) > 0, "status on");
	CK(strstr(buf, "band1-post") != NULL && strstr(buf, "posted=1") != NULL,
	   "status on text: %s", buf);
	CK(hi5622_vap_start_g0_status_line(2u, 1u, -EINVAL, 0u, 1u,
					   buf, sizeof(buf)) > 0, "status invalid");
	CK(strstr(buf, "invalid") != NULL && strstr(buf, "refused=1") != NULL,
	   "status invalid text: %s", buf);
	CK(hi5622_vap_start_g0_status_line(0u, 0u, 0, 0u, 0u, NULL, 0u) ==
	   -EINVAL, "status NULL");
}

/* the plan order contract: the real hi5622_beacon_plan() */
static void test_plan_order(void)
{
	static const char *want9[] = {
		"vap_add", "vap_start_g0", "desc_table", "dfs_mode",
		"band_start", "ap_chan", "ap_ssid", "ap_cfg", "ap_vap_start",
	};
	static const char *want8[] = {
		"vap_add", "desc_table", "dfs_mode", "band_start",
		"ap_chan", "ap_ssid", "ap_cfg", "ap_vap_start",
	};
	u8 plan[HI5622_BEACON_STEP_MAX];
	unsigned int n, i;

	n = hi5622_beacon_plan(1u, 1u, 1u, 1u, 1u, 1u, plan,
			       HI5622_BEACON_STEP_MAX);
	CK(n == 9u, "full plan n=%u != 9", n);
	CK(plan[1] == HI5622_BEACON_STEP_VAP_START_G0,
	   "plan[1]=0x%02x is not the 0x0065 step", plan[1]);
	CK(plan[0] == HI5622_BEACON_STEP_VAP_ADD, "plan[0] vap_add");
	CK(plan[2] == HI5622_BEACON_STEP_DESC_TABLE, "plan[2] desc_table");
	CK(HI5622_BEACON_STEP_VAP_START_G0 < HI5622_BEACON_STEP_DESC_TABLE,
	   "enum order vap_start_g0 < desc_table");
	for (i = 0; i < n && i < 9u; i++)
		CK(strcmp(hi5622_beacon_step_name(plan[i]), want9[i]) == 0,
		   "step %u name %s != %s", i,
		   hi5622_beacon_step_name(plan[i]), want9[i]);

	n = hi5622_beacon_plan(1u, 1u, 0u, 1u, 1u, 1u, plan,
			       HI5622_BEACON_STEP_MAX);
	CK(n == 8u, "no-0x0065 plan n=%u != 8", n);
	for (i = 0; i < n && i < 8u; i++)
		CK(strcmp(hi5622_beacon_step_name(plan[i]), want8[i]) == 0,
		   "no-0x0065 step %u name mismatch", i);

	n = hi5622_beacon_plan(1u, 0u, 0u, 0u, 0u, 0u, plan,
			       HI5622_BEACON_STEP_MAX);
	CK(n == 4u, "minimal plan n=%u != 4 (vap_start=0 drops the last)", n);
	n = hi5622_beacon_plan(1u, 1u, 0u, 0u, 0u, 0u, plan,
			       HI5622_BEACON_STEP_MAX);
	CK(n == 5u, "legacy-order plan n=%u != 5", n);
	CK(hi5622_beacon_plan(0u, 1u, 1u, 1u, 1u, 1u, plan,
			      HI5622_BEACON_STEP_MAX) == 0u, "disabled plan");
	CK(hi5622_beacon_plan(1u, 0u, 0u, 0u, 0u, 0u, NULL,
			      HI5622_BEACON_STEP_MAX) == 0u, "NULL out");

	n = hi5622_beacon_plan(1u, 1u, 1u, 1u, 1u, 1u, plan, 3u);
	CK(n == 3u, "max clamp n=%u", n);
	CK(hi5622_beacon_step_name(plan[1]) &&
	   strcmp(hi5622_beacon_step_name(plan[1]), "vap_start_g0") == 0,
	   "clamped plan keeps the 0x0065 step at index 1");
	CK(strcmp(hi5622_beacon_step_name(HI5622_BEACON_STEP_NONE),
		  "none") == 0, "unknown step name");

	/* the 0x0242 frame builder still agrees with the 0x0065 field B */
	{
		struct hi5622_band_start_plan bsp;
		u8 wire[64];
		size_t len = 0;

		CK(hi5622_band_start_plan(1u, 0u, 1u, 1u, 0u, &bsp) == 0,
		   "band_start plan");
		CK(bsp.post == true, "band_start armed");
		CK(hi5622_band_start_build(wire, sizeof(wire), &len, &bsp,
					   HI5622_BAND_START_FRAME_PRIMARY) == 0,
		   "0x0242 build");
		CK(len == 33u && wire[3] == 0u, "0x0242 field B = 0");
	}
}

int main(void)
{
	test_frame_kat();
	test_sibling_cross_check();
	test_plan_matrix();
	test_build_matrix();
	test_gates();
	test_g12();
	test_status_line();
	test_plan_order();
	printf("vap_start_g0_selftest: %d checks, %d failures\n", checks,
	       failures);
	return failures ? 1 : 0;
}
