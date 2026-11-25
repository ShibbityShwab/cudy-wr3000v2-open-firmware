// SPDX-License-Identifier: GPL-2.0
/*
 * band_start_selftest.c - host-only tests for the portable H2D 0x0242 band-1
 * MAC-start model (lane wifi-band1-start-send, task-206).
 *
 * HOST-ONLY: no device, no kernel, no firmware, no vendor artifact.  It
 * compiles src/hi5622_band_start.c with a plain C99 host compiler and checks:
 *   1. the fail-closed AP-start matrix (band_start x fallback x beacon_static
 *      x band x module);
 *   2. the byte-exact wire frames against this lane's spec bytes: the
 *      33-byte 0x0242 sync start (body {01,00,01,00,00}), the 33-byte
 *      0x0242 forced start (body {02,00,00,00,00}) and the 29-byte 0x0243
 *      nudge - each field by field;
 *   3. the builder's refusals and the firmware guard precheck;
 *   4. the sysfs status line.
 * Run with --dump to print the built frames as hex for the independent
 * tools/band_start_send_test.py re-derivation.
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_band_start.h"

static int checks;
static int fails;

static void check(int cond, const char *what)
{
	checks++;
	if (!cond) {
		fails++;
		printf("FAIL %s\n", what);
	}
}

static void check_eq_u32(u32 got, u32 want, const char *what)
{
	checks++;
	if (got != want) {
		fails++;
		printf("FAIL %s: got 0x%08x want 0x%08x\n", what, got, want);
	}
}

static u16 get16(const u8 *p)
{
	return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static u32 get32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
	       ((u32)p[3] << 24);
}

/* The lane's byte-exact frames: the g0-id1 config-event carrier
 * (wifi-fw-desc-table TABLES T2) with the band-start key/body.
 *   primary  0x0242 body {01,00,01,00,00} -> 33 B
 *   forced   0x0242 body {02,00,00,00,00} -> 33 B
 *   nudge    0x0243 body {00}             -> 29 B   */
static const u8 golden_primary[HI5622_BAND_START_WIRE_LEN] = {
	0x00, 0x01, 0x00, 0x00, 0x21, 0x00, 0x01, 0x00,
	0x00, 0x00, 0x5a, 0x5a,
	0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	0x42, 0x02,
	0x05, 0x00,
	0x01, 0x00, 0x01, 0x00, 0x00,
	0xff, 0xff, 0xff, 0xff,
};
static const u8 golden_forced[HI5622_BAND_START_WIRE_LEN] = {
	0x00, 0x01, 0x00, 0x00, 0x21, 0x00, 0x01, 0x00,
	0x00, 0x00, 0x5a, 0x5a,
	0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	0x42, 0x02,
	0x05, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x00,
	0xff, 0xff, 0xff, 0xff,
};
static const u8 golden_nudge[HI5622_BAND_START_NUDGE_WIRE_LEN] = {
	0x00, 0x01, 0x00, 0x00, 0x1d, 0x00, 0x01, 0x00,
	0x00, 0x00, 0x5a, 0x5a,
	0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	0x43, 0x02,
	0x01, 0x00,
	0x00,
	0xff, 0xff, 0xff, 0xff,
};

static void test_plan(void)
{
	struct hi5622_band_start_plan p;
	int rc;

	/* Current behaviour: 0 = no frame, whatever the ladder/band. */
	rc = hi5622_band_start_plan(0u, 0u, 0u, 0u, 0u, &p);
	check(rc == 0 && !p.post && !p.fallback, "plan off band0 -> no post");
	rc = hi5622_band_start_plan(0u, 0u, 1u, 1u, 0u, &p);
	check(rc == 0 && !p.post && !p.fallback, "plan off band1 -> no post");

	/* The armed case: 1 + beacon_static + band 1 -> post the 0x0242. */
	rc = hi5622_band_start_plan(1u, 0u, 1u, 1u, 0u, &p);
	check(rc == 0 && p.post && !p.fallback,
	      "plan band_start=1 band1 beacon_static -> post primary");
	check(p.module == 0u, "plan field B = module 0 (the ladder default)");

	/* Fallback rung: only as a continuation of the primary. */
	rc = hi5622_band_start_plan(1u, 1u, 1u, 1u, 0u, &p);
	check(rc == 0 && p.post && p.fallback,
	      "plan band_start=1 fallback=1 -> post primary + fallback rungs");
	rc = hi5622_band_start_plan(0u, 1u, 1u, 1u, 0u, &p);
	check(rc == -EINVAL, "plan fallback=1 without band_start -> refused");

	/* Per-band invocation: band 0 never posts. */
	rc = hi5622_band_start_plan(1u, 0u, 1u, 0u, 0u, &p);
	check(rc == 0 && !p.post, "plan band_start=1 band0 -> no post (band gate)");

	/* Fail-closed parameter refusals. */
	rc = hi5622_band_start_plan(1u, 0u, 0u, 1u, 0u, &p);
	check(rc == -EINVAL, "plan band_start=1 without beacon_static -> refused");
	rc = hi5622_band_start_plan(2u, 0u, 1u, 1u, 0u, &p);
	check(rc == -EINVAL, "plan band_start=2 -> refused");
	rc = hi5622_band_start_plan(0xffffffffu, 0u, 1u, 1u, 0u, &p);
	check(rc == -EINVAL, "plan band_start=0xffffffff -> refused");
	rc = hi5622_band_start_plan(1u, 2u, 1u, 1u, 0u, &p);
	check(rc == -EINVAL, "plan fallback=2 -> refused");
	rc = hi5622_band_start_plan(1u, 0u, 1u, 2u, 0u, &p);
	check(rc == -EINVAL, "plan band=2 -> refused");
	rc = hi5622_band_start_plan(1u, 0u, 1u, 1u, 0x13u, &p);
	check(rc == -EINVAL, "plan module 0x13 -> refused (module-table bound)");
	rc = hi5622_band_start_plan(1u, 0u, 1u, 1u, 0x12u, &p);
	check(rc == 0 && p.post && p.module == 0x12u,
	      "plan module 0x12 -> accepted into field B");
	rc = hi5622_band_start_plan(0u, 0u, 0u, 0u, 0u, NULL);
	check(rc == -EINVAL, "plan NULL out -> refused");
}

static void dump_frames(void)
{
	struct hi5622_band_start_plan p;
	u8 wire[HI5622_BAND_START_WIRE_LEN];
	size_t len, i;
	const char *name[3] = { "frame", "frame_forced", "frame_nudge" };
	unsigned int f;

	hi5622_band_start_plan(1u, 1u, 1u, 1u, 0u, &p);
	for (f = 0u; f < 3u; f++) {
		len = 0;
		if (hi5622_band_start_build(wire, sizeof(wire), &len, &p, f))
			continue;
		printf("%s:", name[f]);
		for (i = 0; i < len; i++)
			printf("%02x", wire[i]);
		printf("\n");
	}
}

static void test_frame(void)
{
	struct hi5622_band_start_plan p;
	u8 wire[HI5622_BAND_START_WIRE_LEN];
	u8 big[HI5622_BAND_START_WIRE_LEN + 16];
	u8 small[HI5622_BAND_START_WIRE_LEN - 1u];
	size_t len = 0;
	int rc;
	size_t i;

	hi5622_band_start_plan(1u, 1u, 1u, 1u, 0u, &p);
	memset(wire, 0xaa, sizeof(wire));
	rc = hi5622_band_start_build(wire, sizeof(wire), &len, &p,
				     HI5622_BAND_START_FRAME_PRIMARY);
	check(rc == 0, "build primary -> 0");
	check(len == HI5622_BAND_START_WIRE_LEN, "build primary wire len 33");
	check(len == 33u, "build primary wire len literal 33");
	check(memcmp(wire, golden_primary, sizeof(golden_primary)) == 0,
	      "build primary frame byte-exact (33 B golden)");

	/* Field by field (the header contract of the g0-id1 carrier). */
	check_eq_u32(wire[0], 0x00u, "hdr group 0");
	check_eq_u32(wire[1], 0x01u, "hdr bid alloc=1 dst=0");
	check_eq_u32(wire[2], 0x00u, "hdr fa 0");
	check_eq_u32(wire[3], 0x00u, "hdr field B = module 0");
	check_eq_u32(get16(wire + 4), 33u, "hdr total field = 33 (0x0021)");
	check_eq_u32(get16(wire + 6), 1u, "hdr message id 1");
	check_eq_u32(wire[8], 0x00u, "hdr fc 0");
	check_eq_u32(wire[9], 0x00u, "hdr dst 0");
	check_eq_u32(get16(wire + 10), 0x5a5au, "hdr tag 0x5A5A");
	for (i = 0; i < 8u; i++)
		check_eq_u32(wire[12u + i], (i == 4u) ? 0x01u : 0x00u,
			     "token byte");
	check_eq_u32(get16(wire + 20), 0x0242u, "key 0x0242 at hdr+20");
	check_eq_u32(get16(wire + 22), 5u, "body len 5 at hdr+22");
	check_eq_u32(wire[24], 0x01u, "body[0] subcmd 1 (sync start)");
	check_eq_u32(wire[25], 0x00u, "body[1] 0");
	check_eq_u32(wire[26], 0x01u, "body[2] arm flag 1");
	check_eq_u32(wire[27], 0x00u, "body[3] 0");
	check_eq_u32(wire[28], 0x00u, "body[4] 0");
	check_eq_u32(get32(wire + 29), 0xffffffffu, "inherited tail FFFFFFFF");
	check_eq_u32(HI5622_BAND_START_NODE_WORD1,
		     (u32)((HI5622_BAND_START_WIRE_LEN << 16) | 0x6d2bu),
		     "SR node word1 = (total<<16)|0x6D2B");

	/* Forced-start fallback frame (same carrier, body[0]=2). */
	memset(wire, 0xaa, sizeof(wire));
	rc = hi5622_band_start_build(wire, sizeof(wire), &len, &p,
				     HI5622_BAND_START_FRAME_FORCED);
	check(rc == 0, "build forced -> 0");
	check(len == 33u, "build forced wire len 33");
	check(memcmp(wire, golden_forced, sizeof(golden_forced)) == 0,
	      "build forced frame byte-exact (33 B golden)");
	check_eq_u32(wire[24], 0x02u, "forced body[0] subcmd 2");
	check_eq_u32(get16(wire + 20), 0x0242u, "forced key 0x0242");

	/* 0x0243 nudge fallback frame (29 B carrier). */
	memset(wire, 0xaa, sizeof(wire));
	rc = hi5622_band_start_build(wire, sizeof(wire), &len, &p,
				     HI5622_BAND_START_FRAME_NUDGE);
	check(rc == 0, "build nudge -> 0");
	check(len == HI5622_BAND_START_NUDGE_WIRE_LEN, "build nudge wire len 29");
	check(memcmp(wire, golden_nudge, sizeof(golden_nudge)) == 0,
	      "build nudge frame byte-exact (29 B golden)");
	check_eq_u32(wire[3], 0x00u, "nudge field B = module 0");
	check_eq_u32(get16(wire + 4), 29u, "nudge total field = 29 (0x001d)");
	check_eq_u32(get16(wire + 20), 0x0243u, "nudge key 0x0243");
	check_eq_u32(get16(wire + 22), 1u, "nudge body len 1");
	check_eq_u32(wire[24], 0x00u, "nudge body[0] 0");
	check_eq_u32(get32(wire + 25), 0xffffffffu, "nudge tail FFFFFFFF");
	check_eq_u32(HI5622_BAND_START_NUDGE_NODE_WORD1,
		     (u32)((HI5622_BAND_START_NUDGE_WIRE_LEN << 16) | 0x6d2bu),
		     "nudge SR node word1 = 0x001D6D2B");

	/* field B follows the module index the VAP add binds. */
	{
		struct hi5622_band_start_plan pm;

		hi5622_band_start_plan(1u, 0u, 1u, 1u, 5u, &pm);
		len = 0;
		rc = hi5622_band_start_build(wire, sizeof(wire), &len, &pm,
					     HI5622_BAND_START_FRAME_PRIMARY);
		check(rc == 0 && wire[3] == 5u,
		      "build field B = the AP-start module (5)");
	}

	/* Cap exactly the wire length works; one byte short is refused. */
	len = 0;
	rc = hi5622_band_start_build(big, HI5622_BAND_START_WIRE_LEN, &len, &p,
				     HI5622_BAND_START_FRAME_PRIMARY);
	check(rc == 0 && len == HI5622_BAND_START_WIRE_LEN,
	      "build cap == wire len accepted");
	rc = hi5622_band_start_build(small, sizeof(small), &len, &p,
				     HI5622_BAND_START_FRAME_PRIMARY);
	check(rc == -ENOSPC, "build cap wire_len-1 -> -ENOSPC");

	/* Builder refusals: an unarmed plan can never synthesize a frame. */
	{
		struct hi5622_band_start_plan off;
		struct hi5622_band_start_plan nofb;

		hi5622_band_start_plan(0u, 0u, 1u, 1u, 0u, &off);
		rc = hi5622_band_start_build(wire, sizeof(wire), &len, &off,
					     HI5622_BAND_START_FRAME_PRIMARY);
		check(rc == -EINVAL, "build unarmed plan -> -EINVAL");
		hi5622_band_start_plan(1u, 0u, 1u, 1u, 0u, &nofb);
		rc = hi5622_band_start_build(wire, sizeof(wire), &len, &nofb,
					     HI5622_BAND_START_FRAME_FORCED);
		check(rc == -EINVAL, "build forced without fallback -> -EINVAL");
		rc = hi5622_band_start_build(wire, sizeof(wire), &len, &nofb,
					     HI5622_BAND_START_FRAME_NUDGE);
		check(rc == -EINVAL, "build nudge without fallback -> -EINVAL");
		rc = hi5622_band_start_build(wire, sizeof(wire), &len, &p, 3u);
		check(rc == -EINVAL, "build unknown frame selector -> -EINVAL");
	}
	rc = hi5622_band_start_build(NULL, sizeof(wire), &len, &p,
				     HI5622_BAND_START_FRAME_PRIMARY);
	check(rc == -EINVAL, "build NULL wire -> -EINVAL");
	rc = hi5622_band_start_build(wire, sizeof(wire), NULL, &p,
				     HI5622_BAND_START_FRAME_PRIMARY);
	check(rc == -EINVAL, "build NULL out_len -> -EINVAL");
	rc = hi5622_band_start_build(wire, sizeof(wire), &len, NULL,
				     HI5622_BAND_START_FRAME_PRIMARY);
	check(rc == -EINVAL, "build NULL plan -> -EINVAL");

	dump_frames();
}

static void test_precheck_status(void)
{
	struct hi5622_band_start_state st;
	char buf[200];
	int rc;

	/* Worker/CA 0x4AF0C guards: band object, VAP index, start marker. */
	memset(&st, 0, sizeof(st));
	st.has_bandobj = true;
	st.started = 1u;
	st.vap_index = 3u;
	st.counter = 7u;
	check(hi5622_band_start_precheck(&st) == 0, "precheck open -> 0");
	st.started = 2u;
	check(hi5622_band_start_precheck(&st) == -EALREADY,
	      "precheck state[1]=2 started -> -EALREADY");
	st.started = 3u;
	st.vap_index = HI5622_BAND_START_STATE_NO_VAP;
	check(hi5622_band_start_precheck(&st) == -EAGAIN,
	      "precheck state[4]=0xFF no VAP -> -EAGAIN");
	st.has_bandobj = false;
	check(hi5622_band_start_precheck(&st) == -EAGAIN,
	      "precheck band object absent -> -EAGAIN");
	check(hi5622_band_start_precheck(NULL) == -EINVAL,
	      "precheck NULL -> -EINVAL");

	/* Status line: the three parameter modes and the counters. */
	rc = hi5622_band_start_status_line(0u, 0u, 0u, -ENODATA, 0u, 0u, 0u, 0u,
					   buf, sizeof(buf));
	check(rc > 0 && strstr(buf, "band_start: param=0 (off)") != NULL,
	      "status param=0 off");
	rc = hi5622_band_start_status_line(1u, 0u, 1u, 0, 1u, 0u, 0u, 0u,
					   buf, sizeof(buf));
	check(rc > 0 && strstr(buf, "param=1 (band1-post)") != NULL,
	      "status param=1 band1-post");
	check(strstr(buf, "last_rc=0 posted=1 forced=0 nudge=0 refused=0") != NULL,
	      "status counters");
	rc = hi5622_band_start_status_line(1u, 1u, 1u, 0, 1u, 1u, 1u, 0u,
					   buf, sizeof(buf));
	check(rc > 0 && strstr(buf, "fallback=1") != NULL,
	      "status fallback=1");
	check(strstr(buf, "posted=1 forced=1 nudge=1") != NULL,
	      "status fallback counters");
	rc = hi5622_band_start_status_line(2u, 0u, 1u, -EINVAL, 0u, 0u, 0u, 1u,
					   buf, sizeof(buf));
	check(rc > 0 && strstr(buf, "param=2 (invalid)") != NULL,
	      "status param=2 invalid");
	rc = hi5622_band_start_status_line(0u, 0u, 0u, -ENODATA, 0u, 0u, 0u, 0u,
					   buf, 1u);
	check(rc == 0, "status clamps to len-1");
	rc = hi5622_band_start_status_line(0u, 0u, 0u, -ENODATA, 0u, 0u, 0u, 0u,
					   NULL, 10u);
	check(rc == -EINVAL, "status NULL buf -> -EINVAL");
}

int main(int argc, char **argv)
{
	int dump = (argc > 1 && strcmp(argv[1], "--dump") == 0);

	test_plan();
	test_frame();
	test_precheck_status();

	if (!dump)
		printf("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
