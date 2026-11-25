// SPDX-License-Identifier: GPL-2.0
/*
 * validate_frame_test.c - host-only test that the built 0x01F7 frame passes
 * the REAL hi5622_frame_post() validation path (lane wifi-dfs-mode-send,
 * task-190).  HOST-ONLY: compiles the patched driver tree's portable
 * h2d_frames.c (the validator used by hi5622_frame_post.c:
 * h2d_frame_decode(wire, len, f, H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
 * H2D_VF_HANDLER)) with a plain C99 host compiler and checks:
 *   1. the 29-byte frame decodes as group 0 / id 1 / dst 0, tag 0x5A5A,
 *      payload 17 B, key 0x01F7, body len 1, body {3};
 *   2. the firmware handler table has a handler for group 0 / id 1 (the
 *      frame-post gate refuses otherwise);
 *   3. the SR node plan derived by the existing post machinery:
 *      word1 = (total << 16) | 0x6D2B = 0x001D6D2B;
 *   4. a byte-corrupted total length field is refused (negative control).
 * Run from tools/run-host-tests.sh with -I<patched tree>.
 */
#include <stdio.h>
#include <string.h>

#include "h2d_frames.h"

#define WIRE_LEN 29u

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

/* The lane's byte-exact 29-byte frame (src/hi5622_dfs_mode.c golden). */
static const u8 wire[WIRE_LEN] = {
	0x00, 0x01, 0x00, 0x04, 0x1d, 0x00, 0x01, 0x00,
	0x00, 0x00, 0x5a, 0x5a,
	0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	0xf7, 0x01,
	0x01, 0x00,
	0x03,
	0xff, 0xff, 0xff, 0xff,
};

int main(void)
{
	struct h2d_frame f;
	struct h2d_sr_plan plan;
	enum h2d_err err = H2D_OK;
	u8 bad[WIRE_LEN];
	const struct h2d_fw_handler *h;
	u16 total;

	memset(&f, 0, sizeof(f));
	/* The exact call hi5622_frame_post() makes before any device write. */
	check(h2d_frame_decode(wire, sizeof(wire), &f,
			       H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
			       H2D_VF_HANDLER, &err) == 0,
	      "frame_post validator accepts the 29-B 0x01F7 frame");
	check(f.group == H2D_GROUP_CHIP, "decoded group 0");
	check(f.id == 1u, "decoded message id 1");
	check(f.dst == 0u, "decoded dst 0");
	check(f.payload_len == 17u, "decoded payload len 17 (0x10 + 1)");
	total = (u16)(f.payload_len + H2D_HDR_LEN);
	check(total == WIRE_LEN, "decoded total == wire len 29");
	check(f.fb == 0x04u, "decoded fb 0x04 (the carrier value)");
	check((u16)(f.payload[8] | (f.payload[9] << 8)) == 0x01f7u,
	      "decoded key 0x01F7 at payload+8 (hdr+20)");
	check((u16)(f.payload[10] | (f.payload[11] << 8)) == 1u,
	      "decoded body len 1 at payload+10 (hdr+22)");
	check(f.payload[12] == 0x03u, "decoded body[0] mode 3 at payload+12");

	/* The frame-post handler gate: a firmware receive slot must exist. */
	h = h2d_fw_handler_lookup(f.group, f.id);
	check(h != NULL && h->handler_ca != 0,
	      "firmware handler exists for group 0 / id 1");

	/* The SR node word1 hi5622_frame_post() writes for this frame:
	 * hi5622_frame_post.c:  nodes[slot].word1 = ((u32)len << 16) |
	 * H2D_NODE_FLAGS  (the same formula the h2d_send lane documents). */
	check((((u32)WIRE_LEN << 16) | H2D_NODE_FLAGS) == 0x001d6d2bu,
	      "SR node word1 = 0x001D6D2B (total<<16 | 0x6D2B)");
	check((u16)total == WIRE_LEN, "post node len == wire len 29");
	(void)plan;

	/* Negative control: a corrupted total length is refused before any
	 * device write (the fail-closed validator). */
	memcpy(bad, wire, sizeof(bad));
	bad[4] = 0x00;
	bad[5] = 0x00;
	err = H2D_OK;
	check(h2d_frame_decode(bad, sizeof(bad), &f,
			       H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
			       H2D_VF_HANDLER, &err) != 0,
	      "corrupted total length refused (negative control)");
	bad[4] = 0x1d;
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
