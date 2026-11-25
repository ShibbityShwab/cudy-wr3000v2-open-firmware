// SPDX-License-Identifier: GPL-2.0
/*
 * beacon5g_selftest.c - host selftest for the fail-closed 5 GHz AP-start
 * channel-activation planner (lane wifi-5ghz-beacon, task-175).
 *
 * No device, no kernel, no firmware: pure decision-table and address
 * arithmetic checks.  Build/run with ../tools/run-host-tests.sh.
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_beacon5g.h"

static int checks;
static int fails;

static void ck(int ok, const char *what, const char *detail)
{
	checks++;
	if (!ok) {
		fails++;
		if (detail)
			printf("FAIL %s [%s]\n", what, detail);
		else
			printf("FAIL %s\n", what);
	} else {
		printf("PASS %s\n", what);
	}
}

static void ck_u32(unsigned int got, unsigned int want, const char *what)
{
	char d[96];

	snprintf(d, sizeof(d), "got 0x%08x want 0x%08x", got, want);
	ck(got == want, what, d);
}

static void test_defaults(void)
{
	struct hi5622_beacon5g_plan p;

	/* Default (ap_chan_wait_ms=0) must be the bench-fired vap5g behaviour:
	 * no wait, no changed timing, whatever the band. */
	ck(hi5622_beacon5g_plan(0, 1, 1, 0x00119758, 0, &p) == 0,
	   "plan: default band-1 accepted", NULL);
	ck_u32(p.wait_ms, 0, "plan: default wait 0 (legacy, 2.4/5G)");
	ck(p.readback == true, "plan: default keeps the read-only gate pack", NULL);
	ck_u32(p.mctx, 0x00119758, "plan: default ctx from slot0/base");

	ck(hi5622_beacon5g_plan(0, 0, 1, 0, 0, &p) == 0,
	   "plan: beacon_static=0 accepted (legacy non-beacon path)", NULL);
	ck_u32(p.wait_ms, 0, "plan: beacon_static=0 wait 0");
	ck(p.readback == false, "plan: beacon_static=0 no readback work", NULL);

	ck(hi5622_beacon5g_plan(0, 1, 0, 0, 0, &p) == 0,
	   "plan: 2.4 GHz accepted", NULL);
	ck_u32(p.wait_ms, 0, "plan: 2.4 GHz wait 0 (byte/timing identical)");
}

static void test_matrix(void)
{
	struct hi5622_beacon5g_plan p;
	unsigned int w;

	for (w = 0; w <= HI5622_BEACON5G_WAIT_MAX_MS; w += 5) {
		ck(hi5622_beacon5g_plan(w, 1, 1, 0x00119758, 0x1000, &p) == 0,
		   "matrix: band-1 accepted", NULL);
		ck_u32(p.wait_ms, w, "matrix: band-1 wait == parameter");
	}
	/* The wait is a band-1 AP knob only. */
	ck(hi5622_beacon5g_plan(250, 1, 0, 0x00119758, 0, &p) == 0 &&
	   p.wait_ms == 0, "matrix: 2.4 GHz ignores the wait", NULL);
	ck(hi5622_beacon5g_plan(250, 0, 1, 0x00119758, 0, &p) == 0 &&
	   p.wait_ms == 0, "matrix: beacon_static=0 ignores the wait", NULL);
	ck(hi5622_beacon5g_plan(0, 1, 1, 0, 0, &p) == 0 && p.mctx == 0,
	   "matrix: unreadable module table -> ctx 0 (fail closed)", NULL);
}

static void test_fail_closed(void)
{
	struct hi5622_beacon5g_plan p;

	ck(hi5622_beacon5g_plan(0, 1, 1, 0, 0, NULL) == -EINVAL,
	   "refuse: NULL out", NULL);
	ck(hi5622_beacon5g_plan(HI5622_BEACON5G_WAIT_MAX_MS + 1, 1, 1,
				0, 0, &p) == -EINVAL,
	   "refuse: wait beyond the 1000 ms bound", NULL);
	ck(hi5622_beacon5g_plan(100, 1, 2, 0, 0, &p) == -EINVAL,
	   "refuse: band 2 (never a guessed band)", NULL);
	ck(hi5622_beacon5g_plan(100, 1, 0xffffffffu, 0, 0, &p) == -EINVAL,
	   "refuse: band 0xffffffff", NULL);
}

static void test_mctx(void)
{
	/* Committed bench recipe [V*]: slot0 BAR0 0x829458 = CPU 0x171458,
	 * base u16 BAR0 0x82a504 = CPU 0x172504, ctx0 = slot0 + u16(base). */
	ck_u32(HI5622_BEACON5G_MCTX_SLOT0_CPU + HI5622_BEACON5G_BAR0_BASE,
	       0x829458u, "addr: slot0 CPU -> committed BAR0 0x829458");
	ck_u32(HI5622_BEACON5G_MCTX_BASE_CPU + HI5622_BEACON5G_BAR0_BASE,
	       0x82a504u, "addr: base CPU -> committed BAR0 0x82a504");
	ck_u32(HI5622_BEACON5G_BAND0_PUBLISH_CPU + HI5622_BEACON5G_BAR0_BASE,
	       0x816408u, "addr: band-0 publish word -> BAR0 0x816408");
	ck_u32(HI5622_BEACON5G_BAND1_PUBLISH_CPU + HI5622_BEACON5G_BAR0_BASE,
	       0x817f70u, "addr: band-1 publish word -> BAR0 0x817f70 (0x15e570+0x1a00)");

	/* The old tree constants were the same BAR0 numbers minus the wrong
	 * window base (0x6b0000/0x6b4000 instead of 0x6b8000): prove they are
	 * NOT the committed CPU addresses, so the E2 readback was mis-addressed. */
	ck_u32(0x175458u, HI5622_BEACON5G_MCTX_SLOT0_CPU + 0x4000u,
	       "audit: old slot0 constant is the correct one + 0x4000");
	ck_u32(0x17a504u, HI5622_BEACON5G_MCTX_BASE_CPU + 0x8000u,
	       "audit: old base constant is the correct one + 0x8000");

	ck_u32(hi5622_beacon5g_mctx_ca(0x00119758u, 0u), 0x00119758u,
	       "mctx: slot0 with base 0");
	ck_u32(hi5622_beacon5g_mctx_ca(0x00119758u, 0x1234u), 0x0011a98cu,
	       "mctx: base is u16");
	ck_u32(hi5622_beacon5g_mctx_ca(0x00119758u, 0x00111234u), 0x0011a98cu,
	       "mctx: base high half ignored (u16 read)");
	ck_u32(hi5622_beacon5g_mctx_ca(0u, 0x1000u), 0u,
	       "mctx: slot0 0 -> 0 (fail closed)");
	ck_u32(hi5622_beacon5g_mctx_ca(0x02180000u, 0u), 0u,
	       "mctx: ctx above the firmware RAM window -> 0 (fail closed)");
}

static void test_chan_ready(void)
{
	/* Worker body map [V]: +0x10 channel, +0x11 band, +0x13 valid. */
	ck(hi5622_beacon5g_chan_ctx_ready(0x01000124u, 1u, 36u) == true,
	   "ctx: band 1 ch 36 valid -> ready", NULL);
	ck(hi5622_beacon5g_chan_ctx_ready(0x00000124u, 1u, 36u) == false,
	   "ctx: valid 0 -> not ready", NULL);
	ck(hi5622_beacon5g_chan_ctx_ready(0x01000124u, 0u, 36u) == false,
	   "ctx: band mismatch -> not ready", NULL);
	ck(hi5622_beacon5g_chan_ctx_ready(0x01000124u, 1u, 6u) == false,
	   "ctx: channel mismatch -> not ready", NULL);
	ck(hi5622_beacon5g_chan_ctx_ready(0x01000100u, 1u, 0u) == false,
	   "ctx: channel 0 -> not ready", NULL);
	ck(hi5622_beacon5g_chan_ctx_ready(0x01000006u, 0u, 6u) == true,
	   "ctx: 2.4 GHz ch 6 valid -> ready (generic check)", NULL);
	ck(hi5622_beacon5g_chan_ctx_ready(0x00175468u, 1u, 36u) == false,
	   "ctx: E2 logged word 0x00175468 is not a ready channel ctx", NULL);
	ck(hi5622_beacon5g_chan_ctx_ready(0x01000124u, 2u, 36u) == false,
	   "ctx: band 2 -> not ready", NULL);
}

int main(void)
{
	printf("beacon5g selftest: 5 GHz AP-start channel-activation planner\n");
	test_defaults();
	test_matrix();
	test_fail_closed();
	test_mctx();
	test_chan_ready();
	printf("beacon5g selftest: %d checks, %d failures\n", checks, fails);
	return fails ? 1 : 0;
}
