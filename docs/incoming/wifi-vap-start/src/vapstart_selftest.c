// SPDX-License-Identifier: GPL-2.0
/*
 * vapstart_selftest.c - device-free known-answer and refusal tests for the
 * Hi5622V100 VAP start family builders (lane wifi-vap-start).
 *
 * Every "known answer" below is the byte string derived from the decoded
 * firmware parser / vendor builder (see ../REPORT.md sections 4-7); it is
 * written out literally so a layout drift in vapstart_frames.c fails here.
 *
 *   ./build/vapstart_selftest             # run all checks, exit 0 on PASS
 *   ./build/vapstart_selftest --emit      # also print the bench frames (hex)
 *   ./build/vapstart_selftest --emit-raw  # hex only (for recipes/frames.md)
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "vapstart_frames.h"

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

static void eq_bytes(const struct vsf_frame *f, const unsigned char *want,
		     size_t n, const char *what)
{
	checks++;
	if (f->len != n || memcmp(f->buf, want, n) != 0) {
		size_t i;

		failures++;
		printf("FAIL: %s (len %u want %zu)\n", what, f->len, n);
		if (f->len == n) {
			for (i = 0; i < n; i++) {
				if (f->buf[i] != want[i])
					printf("  first diff at +%zu: got %02x want %02x\n",
					       i, f->buf[i], want[i]);
			}
		}
	}
}

static void dump_hex(const char *name, const struct vsf_frame *f)
{
	unsigned i;

	printf("%s total=%u: ", name, f->len);
	for (i = 0; i < f->len; i++)
		printf("%02x", f->buf[i]);
	printf("\n");
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

/* g0 id1 key 0x0064 VAP_ADD: 48 B */
static const unsigned char KAT_VAP_ADD[48] = {
	0x00, 0x01, 0x00, 0x00, 0x30, 0x00, 0x01, 0x00, 0x00, 0x00, 0x5a, 0x5a,
	0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	0x64, 0x00, 0x14, 0x00,
	0x02, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xff, 0xff, 0xff, 0xff,
};

/* g0 id1 key 0x0065 VAP_START: 40 B, data[1] = 1 */
static const unsigned char KAT_VAP_START[40] = {
	0x00, 0x01, 0x00, 0x00, 0x28, 0x00, 0x01, 0x00, 0x00, 0x00, 0x5a, 0x5a,
	0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	0x65, 0x00, 0x0c, 0x00,
	0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0xff, 0xff, 0xff, 0xff,
};

/* g0 id 0x0010 start_sta: 28 B */
static const unsigned char KAT_STA_START[28] = {
	0x00, 0x01, 0x00, 0x00, 0x1c, 0x00, 0x10, 0x00, 0x00, 0x00, 0x5a, 0x5a,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
};

/* g0 id 0x000F join params: 76 B (payload[16..19] = 01 00 00 00) */
static unsigned char KAT_STA_PARAMS[76];

/* WAL cfg-event cmd 0x0064 VAP add: 56 B */
static const unsigned char KAT_WAL_ADD[56] = {
	0x02, 0x01, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5a, 0x5a,
	0x00, 0x00, 0x00, 0x00,
	0x01, 0x00, 0x00, 0x00,
	0x00, 0x00,
	0x1c, 0x00,
	0x01, 0x00, 0x00, 0x00,
	0x64, 0x00, 0x18, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* WAL cfg-event cmd 0x0065 VAP start: 44 B */
static const unsigned char KAT_WAL_START[44] = {
	0x02, 0x01, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5a, 0x5a,
	0x00, 0x00, 0x00, 0x00,
	0x01, 0x00, 0x00, 0x00,
	0x00, 0x00,
	0x10, 0x00,
	0x02, 0x00, 0x00, 0x00,
	0x65, 0x00, 0x0c, 0x00,
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
};

int main(int argc, char **argv)
{
	int emit = 0, raw = 0, i, rc;
	struct vsf_frame f;
	struct vsf_info info;

	if (argc > 1 && strcmp(argv[1], "--emit") == 0)
		emit = 1;
	if (argc > 1 && strcmp(argv[1], "--emit-raw") == 0) {
		emit = 1;
		raw = 1;
	}

	/* g0 id 0xF frame: 12-byte header + 64-byte payload; the payload's
	 * first BSSID byte and the constant u32 at payload[16] are non-zero. */
	memset(KAT_STA_PARAMS, 0, sizeof(KAT_STA_PARAMS));
	KAT_STA_PARAMS[1] = 0x01;		/* alloc=1, dst=0 */
	KAT_STA_PARAMS[4] = 0x4c;		/* total 76 */
	KAT_STA_PARAMS[6] = 0x0f;		/* id 0x000F */
	KAT_STA_PARAMS[10] = 0x5a;
	KAT_STA_PARAMS[11] = 0x5a;
	KAT_STA_PARAMS[12] = 0x02;		/* payload[0] BSSID[0] */
	KAT_STA_PARAMS[12 + 16] = 0x01;		/* payload[16..19] = 1 */

	/* ---- known answers ---- */
	memset(&f, 0, sizeof(f));
	ok(b_vap_add(&f) == 0, "build vap_add rc");
	eq_bytes(&f, KAT_VAP_ADD, sizeof(KAT_VAP_ADD), "KAT g0 id1 key 0x64 VAP_ADD");
	ok(vsf_frame_validate(&f) == 0, "validate vap_add");

	ok(b_vap_start(&f) == 0, "build vap_start rc");
	eq_bytes(&f, KAT_VAP_START, sizeof(KAT_VAP_START), "KAT g0 id1 key 0x65 VAP_START");
	ok(vsf_frame_validate(&f) == 0, "validate vap_start");

	ok(b_sta_start(&f) == 0, "build sta_join_start rc");
	eq_bytes(&f, KAT_STA_START, sizeof(KAT_STA_START), "KAT g0 id 0x10 start_sta");
	ok(vsf_frame_validate(&f) == 0, "validate sta_join_start");

	ok(b_sta_params(&f) == 0, "build sta_join_params rc");
	eq_bytes(&f, KAT_STA_PARAMS, sizeof(KAT_STA_PARAMS), "KAT g0 id 0xF join params");
	ok(vsf_frame_validate(&f) == 0, "validate sta_join_params");

	ok(b_wal_add(&f) == 0, "build wal_vap_add rc");
	eq_bytes(&f, KAT_WAL_ADD, sizeof(KAT_WAL_ADD), "KAT WAL cmd 0x64 VAP add");
	ok(vsf_frame_validate(&f) == 0, "validate wal_vap_add");
	ok(vsf_frame_decode(&f, &info) == 0 && info.family == VSF_FAMILY_WAL_CFG &&
	   info.wal_cmd == VSF_WAL_CMD_VAP_ADD && info.wal_len == 24,
	   "decode wal_vap_add family/cmd/body");

	ok(b_wal_start(&f) == 0, "build wal_vap_start rc");
	eq_bytes(&f, KAT_WAL_START, sizeof(KAT_WAL_START), "KAT WAL cmd 0x65 VAP start");
	ok(vsf_frame_validate(&f) == 0, "validate wal_vap_start");
	ok(vsf_frame_decode(&f, &info) == 0 && info.wal_cmd == VSF_WAL_CMD_VAP_START &&
	   info.wal_len == 12, "decode wal_vap_start family/cmd/body");

	/* ---- nonzero field known answers ---- */
	{
		struct vsf_vap_add p;

		vsf_vap_add_defaults(&p);
		p.iftype = VSF_IFTYPE_STA;
		p.parent = 0x04;
		p.module = 0x0f;
		p.band = 1;
		p.field_b = 0x02;
		ok(vsf_build_vap_add(&f, &p) == 0, "build vap_add variant rc");
		ok(f.buf[3] == 0x02 && f.buf[24] == 0x03 && f.buf[25] == 0x04 &&
		   f.buf[28] == 0x0f && f.buf[42] == 0x01,
		   "vap_add field map (type/parent/module/band offsets)");
	}
	{
		struct vsf_vap_start p;

		vsf_vap_start_defaults(&p);
		p.channel = 6;
		p.state = 2;
		p.cfg_a = 3;
		p.cfg_b = 4;
		p.cfg_word = 0x11223344u;
		ok(vsf_build_vap_start(&f, &p) == 0, "build vap_start variant rc");
		ok(f.buf[24] == 6 && f.buf[25] == 2 && f.buf[26] == 3 &&
		   f.buf[27] == 4 && f.buf[28] == 0x44 && f.buf[29] == 0x33 &&
		   f.buf[30] == 0x22 && f.buf[31] == 0x11,
		   "vap_start field map (channel/state/cfg/u32)");
	}
	{
		struct vsf_wal_vap_start p;

		vsf_wal_vap_start_defaults(&p);
		p.cookie = 0xdeadbeefu;
		p.seq = 7;
		ok(vsf_build_wal_vap_start(&f, &p) == 0, "build wal start cookie rc");
		ok(f.buf[12] == 0xef && f.buf[15] == 0xde && f.buf[24] == 7 &&
		   f.buf[40] == 0xef && f.buf[43] == 0xde,
		   "wal cookie at +0xC and body[8..11], seq at +0x18");
	}

	/* ---- refusals: builders ---- */
	{
		struct vsf_vap_add p;

		vsf_vap_add_defaults(&p);
		p.iftype = 0;
		ok(vsf_build_vap_add(&f, &p) == -EINVAL, "refuse vap_add type 0");
		vsf_vap_add_defaults(&p);
		p.module = 0x13;
		ok(vsf_build_vap_add(&f, &p) == -EINVAL, "refuse vap_add module 0x13");
		vsf_vap_add_defaults(&p);
		p.band = 2;
		ok(vsf_build_vap_add(&f, &p) == -EINVAL, "refuse vap_add band 2");
		vsf_vap_add_defaults(&p);
		p.dst = 0x10;
		ok(vsf_build_vap_add(&f, &p) == -EINVAL, "refuse vap_add dst 0x10");
		ok(vsf_build_vap_add(NULL, &p) == -EINVAL, "refuse vap_add NULL frame");
	}
	{
		struct vsf_wal_vap_add p;

		vsf_wal_vap_add_defaults(&p);
		p.iftype = 1;
		ok(vsf_build_wal_vap_add(&f, &p) == -EINVAL, "refuse wal vap_add type 1");
		vsf_wal_vap_add_defaults(&p);
		p.dst = 0x20;
		ok(vsf_build_wal_vap_add(&f, &p) == -EINVAL, "refuse wal vap_add dst");
	}
	{
		struct vsf_wal_ap_start_cfg p;

		memset(&p, 0, sizeof(p));
		ok(vsf_build_wal_ap_start_cfg(&f, &p) == -EOPNOTSUPP,
		   "refuse AP-start cfg without the [U] 156-byte body");
	}

	/* ---- refusals: validator (mutate a good frame) ---- */
	ok(b_vap_add(&f) == 0, "rebuild good frame");
	f.buf[10] = 0x00;
	ok(vsf_frame_validate(&f) == -EINVAL, "validator refuses bad tag");
	ok(b_vap_add(&f) == 0, "rebuild good frame 2");
	f.buf[4] = 0x31;
	ok(vsf_frame_validate(&f) == -EINVAL, "validator refuses total mismatch");
	ok(b_vap_add(&f) == 0, "rebuild good frame 3");
	f.buf[0] = 0x01;
	ok(vsf_frame_validate(&f) == -EINVAL, "validator refuses group 1");
	ok(b_vap_add(&f) == 0, "rebuild good frame 4");
	f.buf[1] = 0x02;
	ok(vsf_frame_validate(&f) == -EINVAL, "validator refuses alloc nibble 2");
	ok(b_vap_add(&f) == 0, "rebuild good frame 5");
	f.buf[9] = 0x03;
	ok(vsf_frame_validate(&f) == -EINVAL, "validator refuses dst mismatch");
	ok(b_vap_add(&f) == 0, "rebuild good frame 6");
	f.buf[20] = 0x66;
	ok(vsf_frame_validate(&f) == -EOPNOTSUPP, "validator refuses unknown key");
	ok(b_vap_start(&f) == 0, "rebuild start frame");
	f.buf[22] = 0x10;
	ok(vsf_frame_validate(&f) == -EINVAL, "validator refuses cfgevent len");
	ok(b_wal_add(&f) == 0, "rebuild wal frame");
	f.buf[16] = 0x02;
	ok(vsf_frame_validate(&f) == -EINVAL, "validator refuses wal id 2");
	ok(b_wal_add(&f) == 0, "rebuild wal frame 2");
	f.buf[28] = 0x66;
	ok(vsf_frame_validate(&f) == -EOPNOTSUPP, "validator refuses unknown WAL cmd");
	ok(b_vap_add(&f) == 0, "rebuild good frame 7");
	f.len = (u16)(f.len - 4);
	ok(vsf_frame_validate(&f) == -EINVAL, "validator refuses short body");

	/* ---- export ---- */
	{
		u8 wire[VSF_MAX_FRAME];
		size_t n = 0;

		ok(b_vap_add(&f) == 0, "rebuild for export");
		ok(vsf_frame_export(&f, wire, sizeof(wire), &n) == 0 && n == f.len,
		   "export into wire buffer");
		ok(vsf_frame_export(&f, wire, 8, &n) == -ENOSPC,
		   "export refuses short cap");
	}

	/* ---- families ---- */
	ok(b_vap_add(&f) == 0 && vsf_frame_decode(&f, &info) == 0 &&
	   info.family == VSF_FAMILY_CFG_EVENT && info.key == VSF_KEY_VAP_ADD,
	   "decode vap_add family/key");
	ok(b_vap_start(&f) == 0 && vsf_frame_decode(&f, &info) == 0 &&
	   info.family == VSF_FAMILY_CFG_EVENT && info.key == VSF_KEY_VAP_START,
	   "decode vap_start family/key");
	ok(b_sta_start(&f) == 0 && vsf_frame_decode(&f, &info) == 0 &&
	   info.family == VSF_FAMILY_DIRECT_G0 && info.id == VSF_ID_STA_JOIN_START,
	   "decode start_sta family/id");
	ok(b_sta_params(&f) == 0 && vsf_frame_decode(&f, &info) == 0 &&
	   info.family == VSF_FAMILY_DIRECT_G0 && info.id == VSF_ID_STA_JOIN_PARAMS,
	   "decode join-params family/id");

	if (emit) {
		struct vsf_frame e[6];
		int (*builders[6])(struct vsf_frame *) = {
			b_vap_add, b_vap_start, b_sta_start, b_sta_params,
			b_wal_add, b_wal_start
		};
		const char *names[6] = {
			"vap_add_g0id1_key0x64", "vap_start_g0id1_key0x65",
			"sta_join_start_g0id0x10", "sta_join_params_g0id0x0F",
			"wal_vap_add_g2_cmd0x64", "wal_vap_start_g2_cmd0x65"
		};

		if (!raw)
			printf("\n== bench frames ==\n");
		for (i = 0; i < 6; i++) {
			memset(&e[i], 0, sizeof(e[i]));
			rc = builders[i](&e[i]);
			if (rc) {
				printf("emit: builder %d rc %d\n", i, rc);
				continue;
			}
			if (raw)
				dump_hex(names[i], &e[i]);
			else
				printf("%-26s %u B\n", names[i], e[i].len);
		}
	}

	printf("\n%u/%u checks passed\n", (unsigned)(checks - failures),
	       (unsigned)checks);
	if (failures)
		printf("RESULT: FAIL (%d)\n", failures);
	else
		printf("RESULT: PASS\n");
	return failures ? 1 : 0;
}