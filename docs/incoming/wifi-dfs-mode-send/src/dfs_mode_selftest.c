// SPDX-License-Identifier: GPL-2.0
/*
 * dfs_mode_selftest.c - host-only tests for the portable H2D 0x01F7 DFS-mode
 * model (lane wifi-dfs-mode-send, task-190).
 *
 * HOST-ONLY: no device, no kernel, no firmware, no vendor artifact.  It
 * compiles src/hi5622_dfs_mode.c with a plain C99 host compiler and checks:
 *   1. the fail-closed AP-start matrix (dfs_mode x beacon_static x band);
 *   2. the byte-exact 29-byte wire frame against this lane's spec bytes
 *      (the sibling 0x00F7 carrier with key 0x01F7, len 1, body {3}),
 *      field by field;
 *   3. the builder's refusals and the worker precheck;
 *   4. the sysfs status line.
 * Run with --dump to print the built frame as hex for the independent
 * tools/dfs_mode_send_test.py re-derivation.
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_dfs_mode.h"

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

/* The lane's byte-exact frame: the sibling 0x00F7 carrier (wifi-fw-desc-table
 * TABLES T2) with key 0x01F7, len 1 and body {0x03}.  Header
 * 000100041d00010000005a5a, token 0000000001000000, key f701, len 0100,
 * body 03, tail ffffffff -> 29 bytes. */
static const u8 golden[HI5622_DFS_MODE_WIRE_LEN] = {
	0x00, 0x01, 0x00, 0x04, 0x1d, 0x00, 0x01, 0x00,
	0x00, 0x00, 0x5a, 0x5a,
	0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	0xf7, 0x01,
	0x01, 0x00,
	0x03,
	0xff, 0xff, 0xff, 0xff,
};

static void test_plan(void)
{
	struct hi5622_dfs_mode_plan p;
	int rc;

	/* Current behaviour: 0 = no frame, whatever the ladder/band. */
	rc = hi5622_dfs_mode_plan(0u, 0u, 0u, &p);
	check(rc == 0 && !p.post, "plan dfs_mode=0 band0 -> no post");
	rc = hi5622_dfs_mode_plan(0u, 1u, 1u, &p);
	check(rc == 0 && !p.post, "plan dfs_mode=0 band1 -> no post");

	/* The armed case: 1 + beacon_static + band 1 -> post mode 3. */
	rc = hi5622_dfs_mode_plan(1u, 1u, 1u, &p);
	check(rc == 0 && p.post, "plan dfs_mode=1 band1 beacon_static -> post");
	check(p.mode == 3u, "plan mode is 3");

	/* Per-band invocation: band 0 never posts. */
	rc = hi5622_dfs_mode_plan(1u, 1u, 0u, &p);
	check(rc == 0 && !p.post, "plan dfs_mode=1 band0 -> no post (per-band gate)");

	/* Fail-closed parameter refusals. */
	rc = hi5622_dfs_mode_plan(1u, 0u, 1u, &p);
	check(rc == -EINVAL, "plan dfs_mode=1 without beacon_static -> refused");
	rc = hi5622_dfs_mode_plan(2u, 1u, 1u, &p);
	check(rc == -EINVAL, "plan dfs_mode=2 -> refused");
	rc = hi5622_dfs_mode_plan(3u, 1u, 1u, &p);
	check(rc == -EINVAL, "plan dfs_mode=3 -> refused");
	rc = hi5622_dfs_mode_plan(0xffffffffu, 1u, 1u, &p);
	check(rc == -EINVAL, "plan dfs_mode=0xffffffff -> refused");
	rc = hi5622_dfs_mode_plan(1u, 1u, 2u, &p);
	check(rc == -EINVAL, "plan band=2 -> refused");
	rc = hi5622_dfs_mode_plan(0u, 0u, 0u, NULL);
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
	struct hi5622_dfs_mode_plan p;
	u8 wire[HI5622_DFS_MODE_WIRE_LEN];
	u8 big[HI5622_DFS_MODE_WIRE_LEN + 16];
	u8 small[HI5622_DFS_MODE_WIRE_LEN - 1u];
	size_t len = 0;
	int rc;
	size_t i;

	hi5622_dfs_mode_plan(1u, 1u, 1u, &p);
	memset(wire, 0xaa, sizeof(wire));
	rc = hi5622_dfs_mode_build(wire, sizeof(wire), &len, &p);
	check(rc == 0, "build band1 plan -> 0");
	check(len == HI5622_DFS_MODE_WIRE_LEN, "build wire len 29");
	check(len == 29u, "build wire len literal 29");
	check(memcmp(wire, golden, sizeof(golden)) == 0,
	      "build frame byte-exact (29 B golden)");

	/* Field-by-field (the header contract of the g0-id1 carrier). */
	check_eq_u32(wire[0], 0x00u, "hdr group 0");
	check_eq_u32(wire[1], 0x01u, "hdr bid alloc=1 dst=0");
	check_eq_u32(wire[2], 0x00u, "hdr fa 0");
	check_eq_u32(wire[3], 0x04u, "hdr fb 4 (vendor-wire value)");
	check_eq_u32(get16(wire + 4), 29u, "hdr total field = 29 (0x001d)");
	check_eq_u32(get16(wire + 6), 1u, "hdr message id 1");
	check_eq_u32(wire[8], 0x00u, "hdr fc 0");
	check_eq_u32(wire[9], 0x00u, "hdr dst 0");
	check_eq_u32(get16(wire + 10), 0x5a5au, "hdr tag 0x5A5A");
	for (i = 0; i < 8u; i++)
		check_eq_u32(wire[12u + i], (i == 4u) ? 0x01u : 0x00u,
			     "token byte");
	check_eq_u32(get16(wire + 20), 0x01f7u, "key 0x01F7 at hdr+20");
	check_eq_u32(get16(wire + 22), 1u, "body len 1 at hdr+22");
	check_eq_u32(wire[24], 0x03u, "body[0] mode 3 at hdr+24");
	check_eq_u32(get32(wire + 25), 0xffffffffu, "inherited tail FFFFFFFF");
	check_eq_u32(HI5622_DFS_MODE_NODE_WORD1,
		     (u32)((HI5622_DFS_MODE_WIRE_LEN << 16) | 0x6d2bu),
		     "SR node word1 = (total<<16)|0x6D2B");

	/* Cap exactly the wire length works; one byte short is refused. */
	len = 0;
	rc = hi5622_dfs_mode_build(big, HI5622_DFS_MODE_WIRE_LEN, &len, &p);
	check(rc == 0 && len == HI5622_DFS_MODE_WIRE_LEN,
	      "build cap == wire len accepted");
	rc = hi5622_dfs_mode_build(small, sizeof(small), &len, &p);
	check(rc == -ENOSPC, "build cap wire_len-1 -> -ENOSPC");

	/* Builder refusals. */
	{
		struct hi5622_dfs_mode_plan off;
		struct hi5622_dfs_mode_plan badmode;

		hi5622_dfs_mode_plan(0u, 1u, 1u, &off);
		rc = hi5622_dfs_mode_build(wire, sizeof(wire), &len, &off);
		check(rc == -EINVAL, "build unarmed plan -> -EINVAL");
		badmode = p;
		badmode.mode = 4u;
		rc = hi5622_dfs_mode_build(wire, sizeof(wire), &len, &badmode);
		check(rc == -EINVAL, "build mode != 3 -> -EINVAL");
	}
	rc = hi5622_dfs_mode_build(NULL, sizeof(wire), &len, &p);
	check(rc == -EINVAL, "build NULL wire -> -EINVAL");
	rc = hi5622_dfs_mode_build(wire, sizeof(wire), NULL, &p);
	check(rc == -EINVAL, "build NULL out_len -> -EINVAL");
	rc = hi5622_dfs_mode_build(wire, sizeof(wire), &len, NULL);
	check(rc == -EINVAL, "build NULL plan -> -EINVAL");

	/* The frame emitted for --dump (and the Python cross-check). */
	hi5622_dfs_mode_plan(1u, 1u, 1u, &p);
	hi5622_dfs_mode_build(wire, sizeof(wire), &len, &p);
	dump_frame(wire, len);
}

static void test_precheck_status(void)
{
	struct hi5622_dfs_mode_plan p;
	char buf[160];
	int rc;

	/* Worker preconditions: published per-link non-zero, attach flag 1. */
	check(hi5622_dfs_mode_precheck(0x00175a18u, 1u) == 0,
	      "precheck published+attach -> 0");
	check(hi5622_dfs_mode_precheck(0u, 1u) == -EAGAIN,
	      "precheck published=0 -> -EAGAIN");
	check(hi5622_dfs_mode_precheck(0x00175a18u, 0u) == -EAGAIN,
	      "precheck attach=0 -> -EAGAIN");
	check(hi5622_dfs_mode_precheck(0u, 0u) == -EAGAIN,
	      "precheck both absent -> -EAGAIN");

	/* Status line, all three parameter modes. */
	rc = hi5622_dfs_mode_status_line(0u, 0u, -ENODATA, 0u, 0u,
				  buf, sizeof(buf));
	check(rc > 0 && strstr(buf, "dfs_mode: param=0 (off)") != NULL,
	      "status param=0 off");
	rc = hi5622_dfs_mode_status_line(1u, 1u, 0, 1u, 0u,
				  buf, sizeof(buf));
	check(rc > 0 && strstr(buf, "param=1 (band1-post)") != NULL,
	      "status param=1 band1-post");
	check(strstr(buf, "last_rc=0 posted=1 refused=0") != NULL,
	      "status counters");
	rc = hi5622_dfs_mode_status_line(2u, 1u, -EINVAL, 0u, 1u,
				  buf, sizeof(buf));
	check(rc > 0 && strstr(buf, "param=2 (invalid)") != NULL,
	      "status param=2 invalid");
	rc = hi5622_dfs_mode_status_line(0u, 0u, -ENODATA, 0u, 0u, buf, 1u);
	check(rc == 0, "status clamps to len-1");
	rc = hi5622_dfs_mode_status_line(0u, 0u, -ENODATA, 0u, 0u, NULL, 10u);
	check(rc == -EINVAL, "status NULL buf -> -EINVAL");
	(void)p;
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
