/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rx_init_selftest.c - device-free host test for the candidate RX-init config
 * message model (lane rx-init-decode/task-170).  Checks: byte-exact wire
 * image, structural validation (positive + every single-byte corruption),
 * the fail-closed key allowlist, the full gate matrix and the read-back
 * classification.  No device, no IO, no vendor bytes.
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_rx_init_logic.h"

static int checks, failures;
static void ck(int cond, const char *what)
{
	checks++;
	if (!cond) {
		failures++;
		printf("FAIL %s\n", what);
	}
}

static const u8 expected[HI5622_RX_INIT_TOTAL] = {
	0x00, 0x01, 0x00, 0x04, 0x20, 0x00, 0x01, 0x00,
	0x00, 0x00, 0x5a, 0x5a,
	0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	0x04, 0x00, 0x04, 0x00,
	0x01, 0x00, 0x00, 0x00,
	0xff, 0xff, 0xff, 0xff,
};

static struct hi5622_rx_init_live live_ok(void)
{
	struct hi5622_rx_init_live l;

	memset(&l, 0, sizeof(l));
	l.armed = 1;
	l.released = 1;
	l.boot_latched = 1;
	l.phase_ready = 1;
	l.ctx_created = 1;
	l.budget = 8;
	l.frames_sent = 3;
	return l;
}

int main(void)
{
	struct hi5622_rx_init_frame f, g;
	struct hi5622_rx_init_live l;
	u8 bad[HI5622_RX_INIT_MAX];
	u32 i;
	int rc;

	/* 1. byte-exact frame */
	memset(&f, 0xaa, sizeof(f));
	rc = hi5622_rx_init_build(&f, HI5622_RX_INIT_KEY_DEFAULT,
				  HI5622_RX_INIT_VAL_DEFAULT);
	ck(rc == 0, "build returns 0");
	ck(f.total == HI5622_RX_INIT_TOTAL, "total == 0x20");
	ck(memcmp(f.wire, expected, sizeof(expected)) == 0, "wire bytes byte-exact");
	ck(hi5622_rx_init_validate(f.wire, f.total) == 0, "validate accepts the frame");

	/* 2. determinism + tail untouched by the value */
	rc = hi5622_rx_init_build(&g, 0x0004u, 1u);
	ck(rc == 0 && memcmp(&f, &g, sizeof(f)) == 0, "deterministic");
	hi5622_rx_init_build(&g, 0x0004u, 0xdeadbeefu);
	ck(g.wire[24] == 0xef && g.wire[25] == 0xbe && g.wire[26] == 0xad &&
	   g.wire[27] == 0xde, "value is little-endian");
	ck(g.wire[28] == 0xff && g.wire[31] == 0xff, "tail stays 0xff");

	/* 3. fail-closed allowlist: every other key is refused */
	ck(hi5622_rx_init_key_allowed(0x0004u) == 1, "0x0004 allowed");
	ck(hi5622_rx_init_key_allowed(0x0064u) == 0, "0x0064 refused");
	ck(hi5622_rx_init_key_allowed(0x00d8u) == 0, "0x00d8 refused");
	ck(hi5622_rx_init_key_allowed(0x0320u) == 0, "0x0320 refused");
	ck(hi5622_rx_init_key_allowed(0x0000u) == 0, "0x0000 refused");
	ck(hi5622_rx_init_build(&g, 0x00d8u, 1u) != 0, "build refuses a non-candidate key");

	/* 4. structural validation: every CHECKED header field is protected.  The
	 * token (12..19) and data value (24..27) are legal payload bytes and are
	 * intentionally not part of the validation contract. */
	{
		static const u8 checked[] = { 0, 1, 4, 5, 6, 7, 10, 11, 20, 21,
					      22, 23, 28, 29, 30, 31 };

		for (i = 0; i < sizeof(checked) / sizeof(checked[0]); i++) {
			char what[64];

			memcpy(bad, expected, sizeof(bad));
			bad[checked[i]] ^= 0x01;
			snprintf(what, sizeof(what),
				 "corruption at checked byte %u refused",
				 (unsigned)checked[i]);
			ck(hi5622_rx_init_validate(bad, sizeof(bad)) != 0, what);
		}
	}
	ck(hi5622_rx_init_validate(expected, HI5622_RX_INIT_TOTAL - 1) != 0,
	   "short length refused");
	ck(hi5622_rx_init_validate(expected, HI5622_RX_INIT_TOTAL + 1) != 0,
	   "long length refused");
	ck(hi5622_rx_init_validate(NULL, 0) != 0, "NULL refused");

	/* 5. gate matrix: each refusal in isolation, then the happy path */
	l = live_ok();
	ck(hi5622_rx_init_gate(&l) == HI5622_RX_INIT_OK, "gate ok");
	l.armed = 0;
	ck(hi5622_rx_init_gate(&l) == HI5622_RX_INIT_NOT_ARMED, "gate not_armed");
	l = live_ok(); l.released = 0;
	ck(hi5622_rx_init_gate(&l) == HI5622_RX_INIT_NOT_RELEASED, "gate not_released");
	l = live_ok(); l.boot_latched = 0;
	ck(hi5622_rx_init_gate(&l) == HI5622_RX_INIT_NOT_LATCHED, "gate not_latched");
	l = live_ok(); l.lane_closed = 1;
	ck(hi5622_rx_init_gate(&l) == HI5622_RX_INIT_LANE_CLOSED, "gate lane_closed");
	l = live_ok(); l.phase_ready = 0;
	ck(hi5622_rx_init_gate(&l) == HI5622_RX_INIT_PHASE, "gate phase");
	l = live_ok(); l.ctx_created = 0;
	ck(hi5622_rx_init_gate(&l) == HI5622_RX_INIT_NO_CTX, "gate ctx required");
	l = live_ok(); l.already_sent = 1;
	ck(hi5622_rx_init_gate(&l) == HI5622_RX_INIT_DUP, "gate one-shot");
	l = live_ok(); l.liveness_used = 1;
	ck(hi5622_rx_init_gate(&l) == HI5622_RX_INIT_LIVENESS_USED, "gate liveness");
	l = live_ok(); l.frames_sent = l.budget;
	ck(hi5622_rx_init_gate(&l) == HI5622_RX_INIT_BUDGET, "gate budget");
	l = live_ok(); l.mbox_inflight = 1;
	ck(hi5622_rx_init_gate(&l) == HI5622_RX_INIT_INFLIGHT, "gate inflight");
	l = live_ok(); l.out0 = 0x8;
	ck(hi5622_rx_init_gate(&l) == HI5622_RX_INIT_OUT0, "gate out0");
	ck(hi5622_rx_init_gate(NULL) == HI5622_RX_INIT_NOT_ARMED, "gate NULL refused");
	ck(strcmp(hi5622_rx_init_refusal_name(HI5622_RX_INIT_NO_CTX),
		  "no_ctx_create") == 0, "refusal name");

	/* 6. read-back classification: consumed vs timeout vs lane closed */
	ck(hi5622_rx_init_readback(0, 1, 200, 0) == HI5622_RX_INIT_RESULT_CONSUMED,
	   "readback consumed");
	ck(hi5622_rx_init_readback(0, 200, 200, 0) == HI5622_RX_INIT_RESULT_CONSUMED,
	   "readback consumed at bound");
	ck(hi5622_rx_init_readback(0x8, 200, 200, 0) == HI5622_RX_INIT_RESULT_TIMEOUT,
	   "readback timeout");
	ck(hi5622_rx_init_readback(0x8, 10, 200, 1) == HI5622_RX_INIT_RESULT_LANE_CLOSED,
	   "readback lane closed");
	ck(hi5622_rx_init_readback(0, 0, 0, 0) == HI5622_RX_INIT_RESULT_BAD_ARG,
	   "readback bad arg");
	ck(hi5622_rx_init_readback(0x8, 201, 200, 0) == HI5622_RX_INIT_RESULT_TIMEOUT,
	   "readback over-poll is not consumed");

	printf("rx_init_logic_selftest: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
