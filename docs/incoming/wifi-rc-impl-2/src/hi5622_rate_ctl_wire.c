/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_rate_ctl_wire.c - the rate-control alg_cfg config-event wire builder
 * (Hi5622V100 clean-room driver, wifi-rc-impl-2 lane, RC-2).
 *
 * ORIGINAL clean-room code: the carrier shape is the committed calibration
 * contract (../wifi-rate-ctl/REPORT.md sec 4; ../wifi-proto-seq/recipes/
 * cal-alg-ladder.json P3) and h2d_frames' exact-frame library re-validates
 * every byte (transport + firmware table + handler) before the wire is
 * returned.  No vendor source/object/firmware byte is reproduced.
 */
#include "hi5622_rate_ctl_wire.h"

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/slab.h>
#else
#include <errno.h>
#include <stdlib.h>
#endif

static const uint8_t rc_wire_token[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static const uint8_t rc_wire_tail[4] = { 0, 0, 0, 0 };

/* struct h2d_frame embeds the maximum payload (0x674 B), so it must not live
 * on the kernel stack (the module builds with -Werror=frame-larger-than=1024).
 * Heap-allocate it in both worlds; the builder is otherwise pure. */
static struct h2d_frame *rc_wire_frame_alloc(void)
{
#ifdef __KERNEL__
	return kmalloc(sizeof(struct h2d_frame), GFP_KERNEL);
#else
	return malloc(sizeof(struct h2d_frame));
#endif
}

static void rc_wire_frame_free(struct h2d_frame *f)
{
#ifdef __KERNEL__
	kfree(f);
#else
	free(f);
#endif
}

int rc_wire_build(const uint8_t *blob, uint16_t blob_len, uint8_t fb,
		  uint8_t *wire, size_t cap, size_t *out_len)
{
	struct h2d_frame *f;
	enum h2d_err err = H2D_OK;
	size_t wire_len = 0;
	int rc;

	if (!blob || !wire || !out_len)
		return -EINVAL;
	/* The blob must be a complete alg_cfg header + value, and the carrier
	 * bound must hold (fail closed, never guess a length). */
	if (blob_len < RC_ALG_HDR_LEN || blob_len > RC_ALG_MAX_BLOB)
		return -EINVAL;
	if (fb > RC_CFG_FB_MAX)
		return -EINVAL;
	if (cap < (size_t)RC_WIRE_CARRIER_OVERHEAD + blob_len)
		return -ENOSPC;
	f = rc_wire_frame_alloc();
	if (!f)
		return -ENOMEM;
	/* Chunk 0: the shared config-event builder (group 0 / id 2 / key
	 * 0x0101 / 8-byte zero token / u16 len / blob / 4-byte zero tail), the
	 * exact shape the bench proved on the calibration ladder. */
	rc = h2d_build_g0_cfg_event(f, 2u, 0u, 0u, fb, 0u, rc_wire_token,
				    (u16)RC_CFG_KEY, blob, blob_len,
				    rc_wire_tail, &err);
	if (rc != 0) {
		rc_wire_frame_free(f);
		return -EPERM;
	}
	/* Chunk 1: wire encode re-runs the transport/firmware/handler/policy
	 * validation on the frame struct before a byte can reach the ring. */
	rc = h2d_frame_encode(f, wire, cap, &wire_len, &err);
	rc_wire_frame_free(f);
	if (rc != 0)
		return -EPERM;
	if (wire_len != (size_t)RC_WIRE_CARRIER_OVERHEAD + blob_len)
		return -EPERM;
	*out_len = wire_len;
	return 0;
}

int rc_wire_build_req(const struct rc_req *req, uint8_t fb,
		      uint8_t *wire, size_t cap, size_t *out_len)
{
	uint8_t blob[RC_ALG_MAX_BLOB];
	size_t blob_len = 0;

	if (!req)
		return -EINVAL;
	if (rc_blob_build(req, blob, sizeof(blob), &blob_len) != 0)
		return -EINVAL;
	return rc_wire_build(blob, (uint16_t)blob_len, fb, wire, cap, out_len);
}
