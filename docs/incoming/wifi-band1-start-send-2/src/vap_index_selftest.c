// SPDX-License-Identifier: GPL-2.0
/*
 * vap_index_selftest.c - host tests for the VAP/module-index contract of the
 * structural H2D g0-id1 key-0x0064 VAP add (lane wifi-band1-start-send-2,
 * task-207).  Host-only: no device, no firmware byte, no vendor object.
 *
 * It compiles the REAL portable sources from the committed sibling lanes:
 *   - src/hi5622_vap_index.{c,h}          (this lane)
 *   - ../wifi-vap-start/src/vapstart_frames.c      (the key-0x0064 builder)
 *   - ../wifi-band1-start-send/src/hi5622_band_start.c (the key-0x0242 builder)
 * and asserts: the gate/plan matrix, the byte-exact 0x0064 wire frame (body
 * data[4], data[1], data[0x12] and header field B) for index 0 and 4, the
 * frame through the real builder validator, the 0x0242 frame's field B ==
 * the same index, and the read-only post-add verdict strings.
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_vap_index.h"
#include "vapstart_frames.h"
#include "hi5622_band_start.h"

static int checks, fails;

#define CHECK(cond, msg) do { \
	checks++; \
	if (!(cond)) { fails++; printf("FAIL: %s\n", msg); } \
} while (0)

#define CHECK_EQ(a, b, msg) do { \
	long long _a = (long long)(a), _b = (long long)(b); \
	checks++; \
	if (_a != _b) { fails++; printf("FAIL: %s (%lld != %lld)\n", msg, _a, _b); } \
} while (0)

static const char *hex(const u8 *b, size_t n, char *buf)
{
	size_t i;
	static const char d[] = "0123456789abcdef";

	for (i = 0; i < n; i++) {
		buf[i * 2] = d[b[i] >> 4];
		buf[i * 2 + 1] = d[b[i] & 0x0f];
	}
	buf[n * 2] = 0;
	return buf;
}

static void test_gate(void)
{
	CHECK_EQ(hi5622_vap_add_gate(0u, 0u), 0, "gate index 0 / start 0");
	CHECK_EQ(hi5622_vap_add_gate(4u, 4u), 0, "gate index 4 / start 4");
	CHECK_EQ(hi5622_vap_add_gate(0x12u, 0x12u), 0, "gate index 18 / start 18");
	CHECK_EQ(hi5622_vap_add_gate(0x13u, 0x13u), -EINVAL,
		 "gate index 19 out of the module table");
	CHECK_EQ(hi5622_vap_add_gate(0u, 4u), -EINVAL,
		 "gate index 0 != start module 4 (bind/start ctx divergence)");
	CHECK_EQ(hi5622_vap_add_gate(4u, 0u), -EINVAL,
		 "gate index 4 != start module 0");
}

static void test_plan(void)
{
	struct hi5622_vap_add_plan p;

	memset(&p, 0, sizeof(p));
	CHECK_EQ(hi5622_vap_add_plan_build(0u, 0u, 1u, HI5622_VAP_IFTYPE_AP, &p),
		 0, "plan index 0 band 1 AP");
	CHECK(p.post, "plan posts");
	CHECK_EQ(p.index, 0, "plan index");
	CHECK_EQ(p.parent, 0, "plan parent = self");
	CHECK_EQ(p.field_b, 0, "plan field B = index");
	CHECK_EQ(p.band, 1, "plan band");
	CHECK_EQ(p.create_type, 2, "plan create type AP (2)");
	CHECK_EQ(hi5622_vap_add_plan_build(4u, 4u, 1u, HI5622_VAP_IFTYPE_AP, &p),
		 0, "plan index 4 band 1 AP");
	CHECK_EQ(p.index, 4, "plan index 4");
	CHECK_EQ(p.parent, 4, "plan parent 4 = self");
	CHECK_EQ(p.field_b, 4, "plan field B 4 = index");
	CHECK_EQ(hi5622_vap_add_plan_build(0u, 0u, 2u, HI5622_VAP_IFTYPE_AP, &p),
		 -EINVAL, "plan band 2 refused");
	CHECK_EQ(hi5622_vap_add_plan_build(0u, 0u, 0u, 0u, &p), -EINVAL,
		 "plan unknown iftype refused");
	CHECK_EQ(hi5622_vap_add_plan_build(0x13u, 0x13u, 1u, HI5622_VAP_IFTYPE_AP,
					   &p), -EINVAL, "plan index 19 refused");
	CHECK_EQ(hi5622_vap_add_plan_build(0u, 4u, 1u, HI5622_VAP_IFTYPE_AP, &p),
		 -EINVAL, "plan index/start mismatch refused");
	CHECK_EQ(hi5622_vap_add_plan_build(0u, 0u, 1u, HI5622_VAP_IFTYPE_AP, NULL),
		 -EINVAL, "plan NULL out refused");
}

/* The key-0x0064 frame with data[4] = index, data[1] = parent, data[0x12] =
 * band and hdr[3] = field_b.  This is the exact frame our provider posts. */
static int build_vap_add(struct vsf_frame *f, const struct hi5622_vap_add_plan *p)
{
	struct vsf_vap_add gp;

	vsf_vap_add_defaults(&gp);
	gp.iftype = p->create_type;
	gp.module = p->index;
	gp.parent = p->parent;
	gp.band = p->band;
	gp.field_b = p->field_b;
	return vsf_build_vap_add(f, &gp);
}

static void test_vap_add_frame(void)
{
	struct hi5622_vap_add_plan p;
	struct vsf_frame f;
	struct vsf_info info;
	char buf[256];
	const char *want0 =
		"000100003000010000005a5a0000000001000000640014000200000000000000000000000000000000000100ffffffff";
	const char *want4 =
		"000100043000010000005a5a0000000001000000640014000204000004000000000000000000000000000100ffffffff";

	CHECK_EQ(hi5622_vap_add_plan_build(0u, 0u, 1u, HI5622_VAP_IFTYPE_AP, &p),
		 0, "frame plan index 0");
	CHECK_EQ(build_vap_add(&f, &p), 0, "build 0x0064 frame index 0");
	CHECK_EQ(f.len, 48, "0x0064 frame length 48");
	CHECK_EQ(f.buf[4] | (f.buf[5] << 8), 0x30, "0x0064 total field 0x30");
	CHECK_EQ(f.buf[6] | (f.buf[7] << 8), 1, "0x0064 g0 id1");
	CHECK_EQ(f.buf[3], 0, "0x0064 hdr[3] field B = 0");
	CHECK_EQ(f.buf[20] | (f.buf[21] << 8), 0x0064, "0x0064 key");
	CHECK_EQ(f.buf[22] | (f.buf[23] << 8), 20, "0x0064 body length 20");
	CHECK_EQ(f.buf[24 + HI5622_VAP_ADD_DATA_TYPE], 2, "data[0] create type AP");
	CHECK_EQ(f.buf[24 + HI5622_VAP_ADD_DATA_PARENT], 0, "data[1] parent 0");
	CHECK_EQ(f.buf[24 + HI5622_VAP_ADD_DATA_INDEX], 0, "data[4] VAP index 0 SET");
	CHECK_EQ(f.buf[24 + HI5622_VAP_ADD_DATA_BAND], 1, "data[0x12] band 1");
	CHECK(strcmp(hex(f.buf, f.len, buf), want0) == 0,
	      "0x0064 index0/band1 wire bytes");
	printf("frame index=0 band=1  %s\n", hex(f.buf, f.len, buf));
	CHECK_EQ(vsf_frame_validate(&f), 0, "0x0064 frame validates");
	CHECK_EQ(vsf_frame_decode(&f, &info), 0, "0x0064 frame decodes");
	CHECK_EQ(info.key, 0x0064, "decoded key 0x0064");

	CHECK_EQ(hi5622_vap_add_plan_build(4u, 4u, 1u, HI5622_VAP_IFTYPE_AP, &p),
		 0, "frame plan index 4");
	CHECK_EQ(build_vap_add(&f, &p), 0, "build 0x0064 frame index 4");
	CHECK_EQ(f.buf[3], 4, "0x0064 hdr[3] field B = 4");
	CHECK_EQ(f.buf[24 + HI5622_VAP_ADD_DATA_INDEX], 4, "data[4] VAP index 4");
	CHECK_EQ(f.buf[24 + HI5622_VAP_ADD_DATA_PARENT], 4, "data[1] parent 4");
	CHECK(strcmp(hex(f.buf, f.len, buf), want4) == 0,
	      "0x0064 index4/band1 wire bytes");
	printf("frame index=4 band=1  %s\n", hex(f.buf, f.len, buf));
}

/* The 0x0242 band-start frame's field B (hdr[3]) must resolve the SAME ctx
 * the 0x0064 body data[4] created: CA 0x4C524 takes ctx = 0xFE578(0, hdr[3])
 * and bandobj = ctx+0x4B8. */
static void test_band_start_field_b(void)
{
	unsigned int index;
	u8 wire[64];
	size_t len;
	int rc;

	for (index = 0; index <= 0x12u; index++) {
		struct hi5622_band_start_plan plan;
		char msg[64];

		rc = hi5622_band_start_plan(1u, 0u, 1u, 1u, index, &plan);
		CHECK_EQ(rc, 0, "0x0242 plan accepts a valid module index");
		rc = hi5622_band_start_build(wire, sizeof(wire), &len, &plan,
					     HI5622_BAND_START_FRAME_PRIMARY);
		snprintf(msg, sizeof(msg), "0x0242 build index %u", index);
		CHECK_EQ(rc, 0, msg);
		CHECK_EQ(len, 33, "0x0242 frame length 33");
		CHECK_EQ(wire[3], index, "0x0242 field B == the 0x0064 data[4]");
		CHECK_EQ(wire[20] | (wire[21] << 8), 0x0242, "0x0242 key");
		CHECK_EQ(wire[22] | (wire[23] << 8), 5, "0x0242 body length 5");
		CHECK_EQ(wire[24], 1, "0x0242 body[0] subcmd 1 (sync start)");
		CHECK_EQ(wire[25], 0, "0x0242 body[1] 0");
		CHECK_EQ(wire[26], 1, "0x0242 body[2] arm flag 1");
		CHECK_EQ(wire[27], 0, "0x0242 body[3] 0");
		CHECK_EQ(wire[28], 0, "0x0242 body[4] 0");
	}
	rc = hi5622_band_start_plan(1u, 0u, 1u, 1u, 0x13u, NULL);
	CHECK_EQ(rc, -EINVAL, "0x0242 plan refuses a NULL out");
}

static void test_verdict(void)
{
	const char *v;

	v = hi5622_vap_add_verdict(false, 0u, 0u);
	CHECK(strstr(v, "module ctx unresolved") != NULL, "verdict ctx unresolved");
	v = hi5622_vap_add_verdict(true, 0u, 0u);
	CHECK(strstr(v, "ctx+0x4B8 is ZERO") != NULL, "verdict bind did not run");
	v = hi5622_vap_add_verdict(true, 0x15e570u, 0u);
	CHECK(strstr(v, "band VAP list EMPTY") != NULL, "verdict list empty");
	v = hi5622_vap_add_verdict(true, 0x15e570u, 1u);
	CHECK(strstr(v, "count=1") != NULL, "verdict bound count=1");
	CHECK(strstr(v, "NOT a VAP-add failure") != NULL,
	      "verdict count=1 clears the VAP add");
	v = hi5622_vap_add_verdict(true, 0x15e570u, 2u);
	CHECK(strstr(v, "count>1") != NULL, "verdict count>1");
}

int main(void)
{
	test_gate();
	test_plan();
	test_vap_add_frame();
	test_band_start_field_b();
	test_verdict();
	printf("vap_index_selftest: %d checks, %d failures\n", checks, fails);
	return fails ? 1 : 0;
}
