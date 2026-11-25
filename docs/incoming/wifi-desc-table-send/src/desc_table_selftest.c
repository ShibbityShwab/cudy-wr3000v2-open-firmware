// SPDX-License-Identifier: GPL-2.0
/*
 * desc_table_selftest.c - host-only tests for the portable H2D 0x00F7
 * descriptor-table model (lane wifi-desc-table-send, task-188).
 *
 * HOST-ONLY: no device, no kernel, no firmware, no vendor artifact.  It
 * compiles src/hi5622_desc_table.c with a plain C99 host compiler and checks:
 *   1. the fail-closed AP-start matrix (desc_table x beacon_static x band);
 *   2. the byte-exact 68-byte wire frame against the lane's spec bytes
 *      (wifi-fw-desc-table FIRE-RECIPE R1 + TABLES T2/T3), field by field;
 *   3. the builder's refusals (cc "99"/empty/non-printable, count, cap,
 *      unarmed plan, NULL);
 *   4. the sysfs status line.
 * Run with --dump to print the built frame as hex for the independent
 * tools/desc_table_send_test.py re-derivation.
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_desc_table.h"

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

/* The lane's byte-exact frame (wifi-fw-desc-table FIRE-RECIPE R1): 68 bytes,
 * header 000100044400010000005a5a, token 0000000001000000, key f700,
 * len 2800, blob 5553000002000000 + the two records, tail ffffffff. */
static const u8 golden[HI5622_DESC_TABLE_WIRE_LEN] = {
	0x00, 0x01, 0x00, 0x04, 0x44, 0x00, 0x01, 0x00,
	0x00, 0x00, 0x5a, 0x5a,
	0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	0xf7, 0x00,
	0x28, 0x00,
	0x55, 0x53, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x80, 0x00, 0x1e, 0x00, 0x2c, 0x01,
	0x00, 0x00, 0x00, 0x00, 0xff, 0x1f, 0x00, 0x00,
	0x01, 0x02, 0x80, 0x00, 0x1e, 0x00, 0x2c, 0x01,
	0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00,
	0xff, 0xff, 0xff, 0xff,
};

static void test_plan(void)
{
	struct hi5622_desc_table_plan p;
	int rc;

	/* Current behaviour: 0 = no frame, whatever the ladder/band. */
	rc = hi5622_desc_table_plan(0u, 0u, 0u, &p);
	check(rc == 0 && !p.post, "plan desc_table=0 band0 -> no post");
	rc = hi5622_desc_table_plan(0u, 1u, 1u, &p);
	check(rc == 0 && !p.post, "plan desc_table=0 band1 -> no post");

	/* The armed case: 1 + beacon_static + band 1 -> post. */
	rc = hi5622_desc_table_plan(1u, 1u, 1u, &p);
	check(rc == 0 && p.post, "plan desc_table=1 band1 beacon_static -> post");
	check(p.recs == 2u, "plan record count 2");
	check(p.cc[0] == 'U' && p.cc[1] == 'S', "plan cc US");

	/* Per-band invocation: band 0 never posts. */
	rc = hi5622_desc_table_plan(1u, 1u, 0u, &p);
	check(rc == 0 && !p.post, "plan desc_table=1 band0 -> no post (per-band gate)");

	/* Fail-closed parameter refusals. */
	rc = hi5622_desc_table_plan(1u, 0u, 1u, &p);
	check(rc == -EINVAL, "plan desc_table=1 without beacon_static -> refused");
	rc = hi5622_desc_table_plan(2u, 1u, 1u, &p);
	check(rc == -EINVAL, "plan desc_table=2 -> refused");
	rc = hi5622_desc_table_plan(0xffffffffu, 1u, 1u, &p);
	check(rc == -EINVAL, "plan desc_table=0xffffffff -> refused");
	rc = hi5622_desc_table_plan(1u, 1u, 2u, &p);
	check(rc == -EINVAL, "plan band=2 -> refused");
	rc = hi5622_desc_table_plan(0u, 0u, 0u, NULL);
	check(rc == -EINVAL, "plan NULL out -> refused");
}

static void dump_frame(const u8 *wire, size_t len)
{
	size_t i;

	printf("frame:");
	for (i = 0; i < len; i++)
		printf("%02x", wire[i]);
	printf("\n");
}

static void test_frame(void)
{
	struct hi5622_desc_table_plan p;
	u8 wire[HI5622_DESC_TABLE_WIRE_LEN];
	u8 big[HI5622_DESC_TABLE_WIRE_LEN + 16];
	size_t len = 0;
	const u8 *blob;
	int rc;

	rc = hi5622_desc_table_plan(1u, 1u, 1u, &p);
	check(rc == 0 && p.post, "frame: armed plan");
	rc = hi5622_desc_table_build(wire, sizeof(wire), &len, &p);
	check(rc == 0, "frame: build returns 0");
	check(len == HI5622_DESC_TABLE_WIRE_LEN, "frame: 68 bytes");
	check(memcmp(wire, golden, sizeof(golden)) == 0,
	      "frame: byte-exact 68-byte wire matches the lane spec");
	/* SR node word1 the existing h2d_post machinery derives. */
	check_eq_u32(((u32)len << 16) | 0x6d2bu, 0x00446d2bu,
		     "frame: SR node word1 0x00446D2B");

	/* Header field map (TABLES T2). */
	check_eq_u32(wire[0], 0x00u, "hdr group 0");
	check_eq_u32(wire[1], 0x01u, "hdr bid alloc=1 dst=0");
	check_eq_u32(wire[2], 0x00u, "hdr fa");
	check_eq_u32(wire[3], 0x04u, "hdr fb 0x04");
	check_eq_u32(get16(wire + 4), 0x0044u, "hdr total 0x0044");
	check_eq_u32(get16(wire + 6), 0x0001u, "hdr message id 1");
	check_eq_u32(wire[8], 0x00u, "hdr fc");
	check_eq_u32(wire[9], 0x00u, "hdr dst");
	check_eq_u32(get16(wire + 10), 0x5a5au, "hdr tag 0x5A5A");
	check_eq_u32(get32(wire + 12), 0x00000000u, "token lo word");
	check_eq_u32(get32(wire + 16), 0x00000001u, "token hi word");
	check_eq_u32(get16(wire + 20), 0x00f7u, "key 0x00F7 at hdr+20");
	check_eq_u32(get16(wire + 22), 0x0028u, "len 0x0028 at hdr+22");

	/* Blob (TABLES T3), blob at hdr+24. */
	blob = wire + 24;
	check(blob[0] == 'U' && blob[1] == 'S', "blob cc US");
	check_eq_u32(blob[2], 0u, "blob cc NUL");
	check_eq_u32(blob[3], 0u, "blob chip id 0");
	check_eq_u32(blob[4], 2u, "blob record count 2");
	check_eq_u32(get32(blob + 8), 0x00800000u, "rec0 word armed 2.4G");
	check_eq_u32(blob[12], 30u, "rec0 +4 30");
	check_eq_u32(get16(blob + 14), 300u, "rec0 +6 300");
	check_eq_u32(get32(blob + 20), 0x00001fffu, "rec0 bitmap 0x1FFF");
	check_eq_u32(get32(blob + 24), 0x00800201u, "rec1 word 0x00800201");
	check_eq_u32(get32(blob + 24) & 0x00800000u, 0x00800000u,
		     "rec1 word bit23 (armed) set");
	check_eq_u32(blob[28], 30u, "rec1 +4 30");
	check_eq_u32(get16(blob + 30), 300u, "rec1 +6 300");
	check_eq_u32(get32(blob + 36), 0x000000ffu, "rec1 bitmap ch36-64");
	check_eq_u32(get32(wire + 64), 0xffffffffu, "tail FF FF FF FF");

	/* Builder refusals. */
	{
		struct hi5622_desc_table_plan bad = p;

		bad.cc[0] = '9';
		bad.cc[1] = '9';
		rc = hi5622_desc_table_build(wire, sizeof(wire), &len, &bad);
		check(rc == -EINVAL, "build refuses cc \"99\" (gate dedupe)");
		bad = p;
		bad.cc[0] = 0u;
		bad.cc[1] = 0u;
		rc = hi5622_desc_table_build(wire, sizeof(wire), &len, &bad);
		check(rc == -EINVAL, "build refuses empty cc");
		bad = p;
		bad.cc[0] = 0x1fu;
		bad.cc[1] = 'S';
		rc = hi5622_desc_table_build(wire, sizeof(wire), &len, &bad);
		check(rc == -EINVAL, "build refuses non-printable cc");
		bad = p;
		bad.recs = 1u;
		rc = hi5622_desc_table_build(wire, sizeof(wire), &len, &bad);
		check(rc == -EINVAL, "build refuses count != 2");
		bad = p;
		bad.post = false;
		rc = hi5622_desc_table_build(wire, sizeof(wire), &len, &bad);
		check(rc == -EINVAL, "build refuses an unarmed plan");
		rc = hi5622_desc_table_build(big, HI5622_DESC_TABLE_WIRE_LEN - 1u,
					     &len, &p);
		check(rc == -ENOSPC, "build refuses a short buffer");
		rc = hi5622_desc_table_build(NULL, sizeof(wire), &len, &p);
		check(rc == -EINVAL, "build refuses NULL wire");
		rc = hi5622_desc_table_build(wire, sizeof(wire), NULL, &p);
		check(rc == -EINVAL, "build refuses NULL out_len");
		rc = hi5622_desc_table_build(wire, sizeof(wire), &len, NULL);
		check(rc == -EINVAL, "build refuses NULL plan");
	}

	/* Rebuild for the dump (the refusals above may have touched wire). */
	(void)hi5622_desc_table_build(wire, sizeof(wire), &len, &p);
	dump_frame(wire, len);
}

static void test_status(void)
{
	char buf[160];
	int n;

	n = hi5622_desc_table_status_line(1u, 1u, 0, 1u, 0u, buf, sizeof(buf));
	check(n > 0, "status: returns a positive length");
	check(strstr(buf, "desc_table: param=1") != NULL, "status: param");
	check(strstr(buf, "(band1-post)") != NULL, "status: mode");
	check(strstr(buf, "beacon_static=1") != NULL, "status: beacon_static");
	check(strstr(buf, "last_rc=0") != NULL, "status: last_rc");
	check(strstr(buf, "posted=1") != NULL, "status: posted");
	check(strstr(buf, "refused=0") != NULL, "status: refused");
	(void)hi5622_desc_table_status_line(0u, 0u, -ENODATA, 0u, 0u, buf,
					    sizeof(buf));
	check(strstr(buf, "(off)") != NULL, "status: off mode for 0");
	(void)hi5622_desc_table_status_line(2u, 1u, -EINVAL, 0u, 1u, buf,
					    sizeof(buf));
	check(strstr(buf, "(invalid)") != NULL, "status: invalid mode for >1");
	check(strstr(buf, "last_rc=-22") != NULL, "status: negative last_rc");
	check(hi5622_desc_table_status_line(0u, 0u, 0, 0u, 0u, NULL, 10u) ==
		      -EINVAL, "status: NULL buffer refused");
}

int main(int argc, char **argv)
{
	int dump = (argc > 1 && strcmp(argv[1], "--dump") == 0);

	if (!dump) {
		printf("== portable 0xF7 descriptor-table model\n");
		test_plan();
		test_frame();
		test_status();
		printf("== %d checks, %d failures\n", checks, fails);
		if (fails) {
			printf("FAIL\n");
			return 1;
		}
		printf("PASS\n");
		return 0;
	}
	test_frame();
	return 0;
}
