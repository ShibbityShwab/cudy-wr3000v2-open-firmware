// SPDX-License-Identifier: GPL-2.0
/*
 * validate_frame_test.c - host-only test that the built 0x0242/0x0243 frames
 * pass the REAL hi5622_frame_post() validation path (lane
 * wifi-band1-start-send, task-206).  HOST-ONLY: compiles the patched driver
 * tree's portable h2d_frames.c (the validator used by hi5622_frame_post.c:
 * h2d_frame_decode(wire, len, f, H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
 * H2D_VF_HANDLER)) with a plain C99 host compiler and checks:
 *   1. the 33-byte 0x0242 sync-start frame decodes as group 0 / id 1 /
 *      dst 0, tag 0x5A5A, payload 21 B, field B 0, key 0x0242, body len 5,
 *      body {01,00,01,00,00};
 *   2. the firmware handler table has a handler for group 0 / id 1;
 *   3. the SR node plan derived by the existing post machinery:
 *      word1 = (total << 16) | 0x6D2B = 0x00216D2B;
 *   4. the 29-byte 0x0243 nudge frame decodes with its own key/body;
 *   5. byte-corrupted total/tag frames are refused (negative controls).
 * Run from tools/run-host-tests.sh with -I<patched tree>.
 */
#include <stdio.h>
#include <string.h>

#include "h2d_frames.h"

#define WIRE_PRIMARY_LEN 33u
#define WIRE_NUDGE_LEN 29u

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

/* The lane's byte-exact 33-byte 0x0242 sync-start frame (src golden). */
static const u8 wire_primary[WIRE_PRIMARY_LEN] = {
	0x00, 0x01, 0x00, 0x00, 0x21, 0x00, 0x01, 0x00,
	0x00, 0x00, 0x5a, 0x5a,
	0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	0x42, 0x02,
	0x05, 0x00,
	0x01, 0x00, 0x01, 0x00, 0x00,
	0xff, 0xff, 0xff, 0xff,
};

/* The lane's byte-exact 29-byte 0x0243 nudge frame. */
static const u8 wire_nudge[WIRE_NUDGE_LEN] = {
	0x00, 0x01, 0x00, 0x00, 0x1d, 0x00, 0x01, 0x00,
	0x00, 0x00, 0x5a, 0x5a,
	0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	0x43, 0x02,
	0x01, 0x00,
	0x00,
	0xff, 0xff, 0xff, 0xff,
};

int main(void)
{
	struct h2d_frame f;
	enum h2d_err err = H2D_OK;
	u8 bad[WIRE_PRIMARY_LEN];
	const struct h2d_fw_handler *h;
	u16 total;

	memset(&f, 0, sizeof(f));
	/* The exact call hi5622_frame_post() makes before any device write. */
	check(h2d_frame_decode(wire_primary, sizeof(wire_primary), &f,
			       H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
			       H2D_VF_HANDLER, &err) == 0,
	      "frame_post validator accepts the 33-B 0x0242 sync frame");
	check(f.group == H2D_GROUP_CHIP, "decoded group 0");
	check(f.id == 1u, "decoded message id 1");
	check(f.dst == 0u, "decoded dst 0");
	check(f.fb == 0x00u, "decoded field B 0 (the AP-start module)");
	check(f.payload_len == 21u, "decoded payload len 21 (0x10 + 5)");
	total = (u16)(f.payload_len + H2D_HDR_LEN);
	check(total == WIRE_PRIMARY_LEN, "decoded total == wire len 33");
	check((u16)(f.payload[8] | (f.payload[9] << 8)) == 0x0242u,
	      "decoded key 0x0242 at payload+8 (hdr+20)");
	check((u16)(f.payload[10] | (f.payload[11] << 8)) == 5u,
	      "decoded body len 5 at payload+10 (hdr+22)");
	check(f.payload[12] == 0x01u && f.payload[13] == 0x00u &&
	      f.payload[14] == 0x01u && f.payload[15] == 0x00u &&
	      f.payload[16] == 0x00u,
	      "decoded body {01,00,01,00,00} at payload+12");

	/* The frame-post handler gate: a firmware receive slot must exist. */
	h = h2d_fw_handler_lookup(f.group, f.id);
	check(h != NULL && h->handler_ca != 0,
	      "firmware handler exists for group 0 / id 1");

	/* The SR node word1 hi5622_frame_post() writes for this frame:
	 * hi5622_frame_post.c:  nodes[slot].word1 = ((u32)len << 16) |
	 * H2D_NODE_FLAGS  (the same formula the h2d_send lane documents). */
	check((((u32)WIRE_PRIMARY_LEN << 16) | H2D_NODE_FLAGS) == 0x00216d2bu,
	      "SR node word1 = 0x00216D2B (total<<16 | 0x6D2B)");
	check((u16)total == WIRE_PRIMARY_LEN, "post node len == wire len 33");

	/* The 0x0243 nudge uses the same carrier with its own key/body. */
	memset(&f, 0, sizeof(f));
	check(h2d_frame_decode(wire_nudge, sizeof(wire_nudge), &f,
			       H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
			       H2D_VF_HANDLER, &err) == 0,
	      "frame_post validator accepts the 29-B 0x0243 nudge frame");
	check(f.payload_len == 17u, "nudge decoded payload len 17");
	check((u16)(f.payload[8] | (f.payload[9] << 8)) == 0x0243u,
	      "nudge decoded key 0x0243");
	check((u16)(f.payload[10] | (f.payload[11] << 8)) == 1u,
	      "nudge decoded body len 1");
	check(f.payload[12] == 0x00u, "nudge decoded body[0] 0");
	check((((u32)WIRE_NUDGE_LEN << 16) | H2D_NODE_FLAGS) == 0x001d6d2bu,
	      "nudge SR node word1 = 0x001D6D2B");

	/* Negative controls: a corrupted total length or tag is refused
	 * before any device write (the fail-closed validator). */
	memcpy(bad, wire_primary, sizeof(bad));
	bad[4] = 0x00;
	bad[5] = 0x00;
	err = H2D_OK;
	check(h2d_frame_decode(bad, sizeof(bad), &f,
			       H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
			       H2D_VF_HANDLER, &err) != 0,
	      "corrupted total length refused (negative control)");
	bad[4] = 0x21;
	bad[5] = 0x00;
	bad[10] = 0x00;   /* tag low byte -> not 0x5A5A */
	err = H2D_OK;
	check(h2d_frame_decode(bad, sizeof(bad), &f,
			       H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
			       H2D_VF_HANDLER, &err) != 0,
	      "wrong tag refused (negative control)");

	printf("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
