/* SPDX-License-Identifier: GPL-2.0 */
/*
 * selftest-idx-arith.c - host-side unit test for the B3 packed ring-index
 * arithmetic used by pciet_b3.c.
 *
 * This file is ORIGINAL code by the b3-instrument lane (task-66, 2026-11-22).
 * It is NOT part of the kernel module: it is a plain userspace C program that
 * reproduces b3_idx_plus() and b3_idx_count() from pciet_b3.c and checks the
 * properties the DR drain loop depends on:
 *
 *   1. producer/published/fill pointer advance and wrap:
 *      low 10 bits = node index, bit 10 = phase; +1 each node, and when the low
 *      bits reach depth they are cleared and the phase bit toggles
 *      (hcc-re REPORT sec 4.2, pcie_ete_ring_ptr_plus [V]);
 *   2. newly-completed count = depth * phase_delta + (idx - last)
 *      (hcc-re REPORT sec 4.6, pcie_ete_get_rls_num [V]), and walking 'from'
 *      by that count with the advance function always lands exactly on 'to'
 *      for every reachable packed pair.
 *
 * Physical invariant checked implicitly: the device can never advance its DR
 * fill index by more than depth before the host returns buffers via +0x38, so
 * only deltas 0..depth are representable and the drain clamp
 * (n > depth -> do not consume, device state unknown) is correct.
 *
 * Build/run (host gcc, no kernel tree needed):
 *
 *     gcc -O2 -Wall -Wextra -o /tmp/idx_test selftest-idx-arith.c
 *     /tmp/idx_test
 *     -> ... "count+walk consistency over 64 start positions x 33 deltas: OK"
 *        "ALL OK"  (exit 0)
 */
#include <stdio.h>
#include <stdint.h>

/* Exact copies of pciet_b3.c's packed ring-pointer arithmetic (hcc-re
 * REPORT sec 4.2/4.6 [V]: low 10 bits = node index, bit 10 = wrap phase;
 * producer advance +1, wrap when low bits reach depth; newly completed =
 * depth * phase_delta + (idx - last)).  Physical invariant: the device can
 * never advance its fill index by more than depth before the host returns
 * buffers via +0x38, so only deltas 0..depth are representable. */
#define B3_IDX_LOW_MASK 0x3ffu
#define HI5622_ETE_IDX_PHASE_BIT 0x400u

static uint32_t b3_idx_plus(uint32_t idx, uint32_t depth)
{
	uint32_t next = idx + 1;

	if ((next & B3_IDX_LOW_MASK) == depth) {
		next &= ~B3_IDX_LOW_MASK;
		next ^= HI5622_ETE_IDX_PHASE_BIT;
	}
	return next;
}

static uint32_t b3_idx_count(uint32_t from, uint32_t to, uint32_t depth)
{
	int32_t phase = (int32_t)((to >> 10) & 1u) - (int32_t)((from >> 10) & 1u);
	int32_t delta = phase * (int32_t)depth +
			(int32_t)(to & B3_IDX_LOW_MASK) -
			(int32_t)(from & B3_IDX_LOW_MASK);

	if (delta < 0)
		delta += 2 * (int32_t)depth;
	return (uint32_t)delta;
}

static unsigned int fails;
#define CHECK(cond, label) do { \
	if (!(cond)) { printf("FAIL %s\n", label); fails++; } \
} while (0)

int main(void)
{
	const uint32_t depth = 32, mod = 2 * depth;
	uint32_t i, a, n;

	/* P0 pre-post of a full ring: 32 publishes -> 0x400 */
	{
		uint32_t idx = 0;

		for (i = 0; i < 32; i++)
			idx = b3_idx_plus(idx, depth);
		printf("prepost 32 publishes -> 0x%03x (expect 0x400)\n", idx);
		CHECK(idx == 0x400, "prepost-32 publish");
	}

	/* known pairs */
	printf("count(0x000,0x400)=%u (expect 32)\n", b3_idx_count(0, 0x400, depth));
	CHECK(b3_idx_count(0, 0x400, depth) == 32, "full ring");
	printf("count(0x400,0x000)=%u (expect 32)\n", b3_idx_count(0x400, 0, depth));
	CHECK(b3_idx_count(0x400, 0, depth) == 32, "full ring phase1");
	printf("count(0x01f,0x400)=%u (expect 1)\n", b3_idx_count(0x1f, 0x400, depth));
	CHECK(b3_idx_count(0x1f, 0x400, depth) == 1, "single step across wrap");
	printf("count(0x000,0x01f)=%u (expect 31)\n", b3_idx_count(0, 0x1f, depth));
	CHECK(b3_idx_count(0, 0x1f, depth) == 31, "31 of 32");
	printf("count(same,same)=0 (expect 0)\n");
	CHECK(b3_idx_count(0, 0, depth) == 0 && b3_idx_count(0x400, 0x400, depth) == 0,
	      "empty");

	/* reachability + walk consistency: for every start position a (mod 2*depth)
	 * and every delta n in 0..depth, the count must equal n and the walk must
	 * land exactly on the target.  This is what the DR drain relies on. */
	{
		uint32_t bad = 0;

		for (a = 0; a < mod; a++) {
			uint32_t pa = (a < depth) ? a : (0x400u | (a - depth));

			for (n = 0; n <= depth; n++) {
				uint32_t b = (a + n) % mod;
				uint32_t pb = (b < depth) ? b : (0x400u | (b - depth));
				uint32_t got = b3_idx_count(pa, pb, depth);
				uint32_t pos = pa, k;

				if (got != n) { bad++; continue; }
				for (k = 0; k < got; k++)
					pos = b3_idx_plus(pos, depth);
				if (pos != pb)
					bad++;
			}
		}
		printf("count+walk consistency over %u start positions x 33 deltas: %s\n",
		       mod, bad ? "FAIL" : "OK");
		if (bad)
			fails++;
	}

	printf(fails ? "FAILURES: %u\n" : "ALL OK\n", fails);
	return fails != 0;
}
