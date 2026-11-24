// SPDX-License-Identifier: GPL-2.0
/*
 * wal_validator_selftest.c - device-free test of the h2d_frames validator
 * extension for the WAL group-2 cfg-event family (lane wifi-vap-start,
 * task-117).
 *
 * The 2026-11-26 bench fire rejected the 56-byte WAL VAP-add frame inside
 * hi5622_frame_post() because h2d_frames' generic handler-slot lookup found no
 * group-2 id-0 entry.  The provider extension therefore teaches the validator
 * this one decoded family (envelope + command allowlist + body size) while
 * keeping every other frame on the generic rule.  This test proves both the
 * new accepted shapes and the still-refused ones, and re-runs the inherited
 * exact-builder regression.
 *
 * Build (tools/validator-check.sh does this against the PATCHED tree):
 *   cc -std=c99 -Wall -Wextra -Werror -pedantic -I<patched-src> \
 *      src/wal_validator_selftest.c src/vapstart_frames.c <patched-src>/h2d_frames.c
 */
#include <stdio.h>
#include <string.h>

#include "vapstart_frames.h"
#include "h2d_frames.h"

static int checks;
static int failures;

static void ok(int cond, const char *what)
{
	checks++;
	if (!cond) {
		failures++;
		printf("FAIL: %s\n", what);
	}
}

/* The exact call hi5622_frame_post() makes (frames_api / frame_post.c). */
static int validate_wire(const u8 *wire, size_t len, enum h2d_err *err)
{
	struct h2d_frame f;

	return h2d_frame_decode(wire, len, &f,
				H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
				H2D_VF_HANDLER, err);
}


/* Decode a wire frame and ask the new predicate whether the frame-post path
 * should treat it as handled by the firmware WAL module. */
static int wal_predicate(const u8 *wire, size_t len)
{
	struct h2d_frame f;

	if (h2d_frame_decode(wire, len, &f,
			     H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
			     H2D_VF_HANDLER, NULL) != 0)
		return -1;
	return h2d_frame_is_wal_cfg(&f) != 0;
}

static void expect_err(const struct vsf_frame *f, enum h2d_err want,
		       const char *what)
{
	enum h2d_err err = H2D_OK;
	int rc = validate_wire(f->buf, f->len, &err);

	checks++;
	if (rc != 0 && err == want) {
		return;
	}
	failures++;
	printf("FAIL: %s (rc=%d want_err=%s got_err=%s)\n", what, rc,
	       h2d_strerror(want), h2d_strerror(err));
}

static int build_and_validate(int (*build)(struct vsf_frame *),
			      const char *what)
{
	struct vsf_frame f;
	enum h2d_err err = H2D_OK;
	int rc = build(&f);
	int v;

	if (rc) {
		printf("FAIL: build %s rc=%d\n", what, rc);
		return -1;
	}
	v = validate_wire(f.buf, f.len, &err);
	ok(v == 0, what);
	if (v != 0)
		printf("  h2d validator refused (%s)\n", h2d_strerror(err));
	return v == 0 ? (int)f.len : -1;
}

static int b_vap_add(struct vsf_frame *f)
{
	struct vsf_vap_add p;

	vsf_vap_add_defaults(&p);
	return vsf_build_vap_add(f, &p);
}

static int b_vap_start(struct vsf_frame *f)
{
	struct vsf_vap_start p;

	vsf_vap_start_defaults(&p);
	return vsf_build_vap_start(f, &p);
}

static int b_sta_start(struct vsf_frame *f)
{
	struct vsf_sta_join_start p;

	vsf_sta_join_start_defaults(&p);
	return vsf_build_sta_join_start(f, &p);
}

static int b_sta_params(struct vsf_frame *f)
{
	struct vsf_sta_join_params p;

	vsf_sta_join_params_defaults(&p);
	return vsf_build_sta_join_params(f, &p);
}

static int b_wal_add(struct vsf_frame *f)
{
	struct vsf_wal_vap_add p;

	vsf_wal_vap_add_defaults(&p);
	return vsf_build_wal_vap_add(f, &p);
}

static int b_wal_start(struct vsf_frame *f)
{
	struct vsf_wal_vap_start p;

	vsf_wal_vap_start_defaults(&p);
	return vsf_build_wal_vap_start(f, &p);
}

int main(void)
{
	struct vsf_frame w;
	struct vsf_frame bad;
	struct vsf_wal_ap_start_cfg ap;
	u8 body[156];
	enum h2d_err err;
	u8 buf[H2D_MAX_TOTAL];
	size_t out_len = 0;
	int n;

	printf("== WAL cfg-event family accepted by the h2d_frames validator ==\n");
	n = build_and_validate(b_wal_add, "WAL VAP add 56 B accepted");
	ok(n == 56, "WAL VAP add total is 56");
	n = build_and_validate(b_wal_start, "WAL VAP start 44 B accepted");
	ok(n == 44, "WAL VAP start total is 44");

	/* Frame-post gate: the predicate must be true only for the validated WAL
	 * family (hi5622_frame_post.c skips the static handler-slot gate on it). */
	ok(b_wal_add(&w) == 0 && wal_predicate(w.buf, w.len) == 1,
	   "predicate: WAL VAP add handled by the WAL module");
	ok(b_wal_start(&w) == 0 && wal_predicate(w.buf, w.len) == 1,
	   "predicate: WAL VAP start handled by the WAL module");
	ok(b_vap_add(&w) == 0 && wal_predicate(w.buf, w.len) == 0,
	   "predicate: g0 VAP add is not the WAL family");
	ok(b_wal_add(&w) == 0, "rebuild WAL frame for predicate mutations");
	{
		struct vsf_frame pm = w;

		pm.buf[28] = 0x66;	/* unknown WAL cmd */
		ok(wal_predicate(pm.buf, pm.len) == -1,
		   "predicate: unknown WAL command refused by decode (H2D_EWALCMD)");
		pm = w;
		pm.len = 20;
		ok(wal_predicate(pm.buf, pm.len) == -1,
		   "predicate: malformed WAL frame rejected by decode");
	}

	/* The AP-config envelope is decoded (body still [U]); with a caller
	 * body the envelope must pass the same structural family check. */
	memset(body, 0, sizeof(body));
	memset(&ap, 0, sizeof(ap));
	ap.body = body;
	ok(vsf_build_wal_ap_start_cfg(&w, &ap) == 0, "build WAL AP cfg envelope");
	err = H2D_OK;
	ok(validate_wire(w.buf, w.len, &err) == 0 &&
	   w.len == 188, "WAL AP cfg 188 B accepted");

	/* Negative cases: the family stays fail-closed. */
	ok(b_wal_add(&w) == 0, "rebuild WAL VAP add");
	bad = w;
	bad.buf[28] = 0x66;	/* u16 cmd at payload+0x10 -> 0x0066 */
	expect_err(&bad, H2D_EWALCMD, "unknown WAL command 0x0066 refused");
	bad = w;
	bad.buf[29] = 0x01;	/* cmd 0x0164 */
	expect_err(&bad, H2D_EWALCMD, "unknown WAL command 0x0164 refused");
	bad = w;
	bad.buf[16] = 0x02;	/* WAL cfg-event id != 1 at payload+4 */
	expect_err(&bad, H2D_EWALCMD, "WAL cfg-event id != 1 refused");
	bad = w;
	bad.buf[22] = 0x20;	/* data_len at payload+0x0A */
	expect_err(&bad, H2D_EARG, "bad WAL data_len refused");
	bad = w;
	bad.buf[30] = 0x10;	/* body_len at payload+0x12 (24 -> 16) */
	ok(validate_wire(bad.buf, bad.len, &err) != 0,
	   "bad WAL body_len refused");
	bad = w;
	bad.len = (u16)(bad.len - 4);	/* length/header mismatch */
	ok(validate_wire(bad.buf, bad.len, &err) != 0,
	   "WAL frame length mismatch refused");
	bad = w;
	bad.len = 20;			/* shorter than the WAL header */
	expect_err(&bad, H2D_ELEN, "short WAL frame refused");

	printf("== inherited frames keep the generic handler-slot rule ==\n");
	n = build_and_validate(b_vap_add, "g0 id1 key 0x64 VAP add 48 B accepted");
	ok(n == 48, "g0 VAP add total is 48");
	n = build_and_validate(b_vap_start, "g0 id1 key 0x65 VAP start 40 B accepted");
	ok(n == 40, "g0 VAP start total is 40");
	n = build_and_validate(b_sta_start, "g0 id 0x10 start_sta 28 B accepted");
	ok(n == 28, "g0 0x10 total is 28");
	n = build_and_validate(b_sta_params, "g0 id 0xF join params 76 B accepted");
	ok(n == 76, "g0 0xF total is 76");

	/* Group 1 has no H2D receive table; group 4 is D2H only.  Build a
	 * transport-valid header first so the group rule is what refuses. */
	memset(&w, 0, sizeof(w));
	w.len = 12;
	w.buf[0] = 1;
	w.buf[1] = 1;
	w.buf[4] = 12;
	w.buf[6] = 0;
	w.buf[10] = 0x5a;
	w.buf[11] = 0x5a;
	expect_err(&w, H2D_EGROUP, "group 1 H2D refused");
	w.buf[0] = 4;
	expect_err(&w, H2D_EGROUP, "group 4 H2D refused");
	/* A group-2 frame with a non-zero id (thus not the WAL family) still
	 * goes through the generic slot lookup. */
	ok(b_wal_add(&w) == 0, "rebuild WAL VAP add for id test");
	w.buf[6] = 0x7f;
	w.buf[7] = 0x00;
	expect_err(&w, H2D_ENOHANDLER, "group-2 id 0x7f still refused");

	printf("== inherited exact-builders regression (h2d_frames) ==\n");
	{
		struct h2d_frame hf;
		const u8 token[8] = { 0, 0, 0, 0, 1, 0, 0, 0 };
		const u8 data[20] = { 1, 0, 0, 0 };
		const u8 tail[4] = { 0xff, 0xff, 0xff, 0xff };

		memset(&hf, 0, sizeof(hf));
		ok(h2d_build_g0_id1_hdpp_txq_sync(&hf, 0, 0, 4, token, 0x00d8,
						 data, sizeof(data), tail,
						 &err) == 0,
		   "inherited TXQ-sync builder still works");
		ok(h2d_frame_encode(&hf, buf, sizeof(buf), &out_len, &err) == 0,
		   "inherited TXQ-sync encode still validates");
		ok(validate_wire(buf, out_len, &err) == 0,
		   "inherited TXQ-sync frame accepted");
		memset(&hf, 0, sizeof(hf));
		ok(h2d_build_g0_id09_del_user(&hf, 0, 0x0fff,
					      (const u8 *)"\x02\x00\x68\x69",
					      &err) == 0,
		   "inherited del-user builder still works");
		ok(h2d_frame_encode(&hf, buf, sizeof(buf), &out_len, &err) == 0 &&
		   validate_wire(buf, out_len, &err) == 0,
		   "inherited del-user frame accepted");
	}

	printf("\n%u/%u checks passed\n", (unsigned)(checks - failures),
	       (unsigned)checks);
	if (failures)
		printf("RESULT: FAIL (%d)\n", failures);
	else
		printf("RESULT: PASS\n");
	return failures ? 1 : 0;
}
