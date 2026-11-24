/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_frame_post.h - kernel-side, fail-closed post path for
 * provider-built post-ready H2D frames (Hi5622V100 clean-room driver).
 *
 * ORIGINAL clean-room code (wifi-scan-ap lane, shared task task-113,
 * 2026-11-24).  No vendor source/object/firmware byte is reproduced.
 *
 * This is the ONE place where a frame produced by the frame provider
 * (hi5622_frames_api.h) reaches the wire.  It re-uses the protocol core's
 * SR commit + coalesced mailbox announce (the same primitives the validated
 * h2d_send path uses) but adds the scan/AP-specific policy:
 *
 *   - compile/load-time fail-closed: module param allow_frames=0 means NO
 *     provider frame is ever posted; the benchmark sets it explicitly;
 *   - a per-load budget (the param value) so a scan/AP loop cannot flood the
 *     firmware;
 *   - a minimum inter-frame gap (frame_gap_ms) because the vendor host posts
 *     coalesced mailbox announces, not a burst;
 *   - the inherited h2d_frames structural + firmware-table validation
 *     (tag/length/group/dst/alloc, group 0/2 only, handler must exist);
 *   - the same gate set as h2d_send (armed, released, boot latched, phase
 *     ACTIVE, lane open, no inflight announce, live out[0] == 0) plus the
 *     host-DMA window check on the SR buffer;
 *   - bounded out[0] consumption poll with NO retry.
 *
 * The wire order is the validated vendor order: body -> node -> wmb ->
 * packed producer -> wmb -> coalesced mailbox id-3 announce.
 */
#ifndef HI5622_FRAME_POST_H
#define HI5622_FRAME_POST_H

#include <linux/device.h>
#include <linux/types.h>

struct hi5622_pci;

/* Post-ready provider frames allowed per module load; 0 = fail-closed
 * default (no device write from this path at all).  This is deliberately a
 * separate knob from h2d_send_budget: scan traffic is userspace-triggered and
 * repetitive, so it must never consume the curated bench experiment budget. */
extern unsigned int hi5622_allow_frames;
/* Minimum time between two provider frames, milliseconds. */
extern unsigned int hi5622_frame_gap_ms;

/* Validate + post one complete HCC wire frame (header + payload).
 * `what` is a short name for dmesg/status.  Returns 0 when the frame was
 * committed AND the firmware consumed the announce; -errno otherwise (never
 * retries).  `out0_after` may be NULL. */
int hi5622_frame_post(struct hi5622_pci *ctx, const u8 *wire, size_t len,
		      const char *what, u32 *out0_after);

/* Read-only status (sysfs). */
int hi5622_frame_post_status_line(char *buf, size_t len);

#endif /* HI5622_FRAME_POST_H */
