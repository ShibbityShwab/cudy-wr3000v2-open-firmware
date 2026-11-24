// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_frame_post.c - the single, fail-closed wire path for provider-built
 * post-ready H2D frames (Hi5622V100 clean-room driver, Cudy WR3000 v2.0,
 * Linux 6.18).
 *
 * ORIGINAL clean-room code (wifi-scan-ap lane, shared task task-113,
 * 2026-11-24).  The post primitives and the vendor order are inherited from
 * the bench-validated wifi-h2d-send path (see ../REPORT.md sec 6); no vendor
 * source/object/firmware byte is reproduced.
 *
 * SAFETY: performs no probe-time device access.  Reads are limited to the
 * live out[0] pending word and writes only to the host-owned SR0 node array /
 * message buffer / producer register and the core's allowlisted mailbox
 * announce (hcc_core_sr_commit).  CA 0x400392F0 (out[5]) and the transport
 * release word are never writable from here.
 */
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <asm/barrier.h>

#include "hi5622_hcc.h"
#include "hi5622_driver.h"
#include "hi5622_frames_api.h"
#include "hi5622_scan_logic.h"
#include "hi5622_frame_post.h"
#include "h2d_frames.h"

#define HI5622_POST_POLL_MAX	200u	/* 200 x 5 ms = the core 1000 ms bound */
#define HI5622_POST_POLL_MS	5u

unsigned int hi5622_allow_frames;
module_param_named(allow_frames, hi5622_allow_frames, uint, 0644);
MODULE_PARM_DESC(allow_frames,
		 "post-ready provider-built H2D frames allowed per module load: 0 = fail-closed (default), N = budget");

unsigned int hi5622_frame_gap_ms = 20u;
module_param_named(frame_gap_ms, hi5622_frame_gap_ms, uint, 0644);
MODULE_PARM_DESC(frame_gap_ms,
		 "minimum milliseconds between two provider-built H2D frames (default 20; the post path also waits for the previous coalesced announce to clear, so back-to-back vendor-order frames such as user_add -> user_add_notify are not refused)");

static atomic_t hi5622_frames_posted = ATOMIC_INIT(0);
static atomic_t hi5622_frames_refused = ATOMIC_INIT(0);
static atomic_t hi5622_frames_timeout = ATOMIC_INIT(0);
static atomic_t hi5622_frames_bad = ATOMIC_INIT(0);
static u64 hi5622_last_post_ms;

static int hi5622_post_refuse(struct device *dev, const char *what,
			      const char *why, int err)
{
	atomic_inc(&hi5622_frames_refused);
	dev_warn(dev,
		 "frame post '%s': REFUSED (%s) - no device write performed\n",
		 what, why);
	return err;
}

/* Bounded wait for the wire to be ready for the NEXT provider frame.
 *
 * Vendor order sends user_add (g0 id7) immediately followed by
 * user_add_notify_alg (g0 id8): two frames, two SR commits, two coalesced
 * mailbox announces.  The first bench session showed the notify being refused
 * because the host-side coalescing flag was still set even though out[0] had
 * been consumed in 0 polls - that flag clears on the core's own service tick
 * (hcc_poll_ms default 5 ms), so a back-to-back frame needs a bounded wait,
 * not a refusal.  This waits for (a) core->mbox_inflight == 0, (b) live
 * out[0] == 0 and (c) the inter-frame gap, polling every 5 ms for at most
 * 200 ms.  Everything is read-only; on timeout the caller refuses without a
 * device write.  Caller must have verified windows_programmed + h->armed. */
#define HI5622_POST_WAIT_MS		200u
#define HI5622_POST_WAIT_POLL_MS	5u

static int hi5622_frame_wait_ready(struct hi5622_pci *ctx, u64 *now_out)
{
	struct hi5622_hcc_service *h = ctx->hcc;
	struct hcc_core *core = &h->hcc.core;
	struct device *dev = &ctx->pdev->dev;
	u32 waited = 0;
	u32 out0 = 0;

	for (;;) {
		u64 now = hcc_kernel_now_ms();
		bool gap_ok = !hi5622_last_post_ms ||
			now - hi5622_last_post_ms >=
				(u64)hi5622_frame_gap_ms;

		out0 = readl(ctx->bar0 +
			     hi5622_io_bar0(HI5622_CA_H2D_PENDING));
		if (!core->mbox_inflight && !out0 && gap_ok) {
			if (now_out)
				*now_out = now;
			return 0;
		}
		if (waited >= HI5622_POST_WAIT_MS) {
			dev_warn(dev,
				 "frame post: wire not ready within %u ms (core_inflight=0x%x out[0]=0x%08x gap_ok=%u) - next frame refused\n",
				 HI5622_POST_WAIT_MS, core->mbox_inflight, out0,
				 gap_ok ? 1u : 0u);
			return -ETIMEDOUT;
		}
		if (msleep_interruptible(HI5622_POST_WAIT_POLL_MS))
			return -EINTR;
		waited += HI5622_POST_WAIT_POLL_MS;
	}
}

int hi5622_frame_post(struct hi5622_pci *ctx, const u8 *wire, size_t len,
		      const char *what, u32 *out0_after)
{
	struct device *dev;
	struct hi5622_hcc_service *h;
	struct hcc_core *core;
	struct hi5622_ete_ring *r;
	struct hi5622_ete_node *nodes;
	struct h2d_frame *f = NULL;
	const struct h2d_fw_handler *handler;
	bool wal_cfg;
	enum h2d_err herr = H2D_OK;
	u32 producer = 0, slot = 0, devva = 0, out0 = 0;
	u32 poll;
	u64 now;
	int rc, ret = 0;

	if (out0_after)
		*out0_after = 0xffffffffu;
	if (!ctx || !ctx->hcc || !wire || !what)
		return -EINVAL;
	dev = &ctx->pdev->dev;
	h = ctx->hcc;
	core = &h->hcc.core;

	f = kzalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return -ENOMEM;

	/* 1. Structural + firmware-table validation.  Policy level is not used
	 * here: a provider frame is by definition not an EXACT builder of this
	 * tree, so we require transport structure, a real firmware handler and
	 * then refuse the known-unsafe ids explicitly. */
	if (len < HI5622_FRAME_HDR_LEN || len > HI5622_FRAME_MAX_TOTAL) {
		atomic_inc(&hi5622_frames_bad);
		ret = hi5622_post_refuse(dev, what, "wire length out of range",
					 -EINVAL);
		goto out;
	}
	if (h2d_frame_decode(wire, len, f,
			     H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
			     H2D_VF_HANDLER, &herr) != 0) {
		atomic_inc(&hi5622_frames_bad);
		dev_warn(dev,
			 "frame post '%s': frame rejected by the h2d_frames validator (%s)\n",
			 what, h2d_strerror(herr));
		ret = -EPERM;
		goto out;
	}
	if (f->group == H2D_GROUP_CORE && f->id == H2D_MBOX_ID_BUGON) {
		atomic_inc(&hi5622_frames_bad);
		ret = hi5622_post_refuse(dev, what,
					 "mailbox id 5 is the firmware BUG() trap",
					 -EPERM);
		goto out;
	}
	/* The WAL group-2 cfg-event family is consumed by the firmware's WAL/core
	 * module; the static H2D handler table has no id-0 slot, so the generic
	 * handler-slot gate below must not apply to it.  The frame was already
	 * validated by h2d_frame_decode(H2D_VF_HANDLER), which now includes the
	 * fail-closed h2d_check_wal_cfg() envelope/command allowlist - so the WAL
	 * family is handled by the WAL module without weakening any other id. */
	wal_cfg = h2d_frame_is_wal_cfg(f) != 0;
	handler = wal_cfg ? NULL : h2d_fw_handler_lookup(f->group, f->id);
	if (!wal_cfg && (!handler || !handler->handler_ca)) {
		atomic_inc(&hi5622_frames_bad);
		ret = hi5622_post_refuse(dev, what,
					 "no firmware handler for group/id",
					 -EOPNOTSUPP);
		goto out;
	}

	/* 2. Policy gates (all read-only; any refusal happens before a write). */
	if (!hi5622_allow_frames) {
		ret = hi5622_post_refuse(dev, what,
					 "allow_frames=0 (fail-closed default)",
					 -EPERM);
		goto out;
	}
	if (atomic_read(&hi5622_frames_posted) >= (int)hi5622_allow_frames) {
		ret = hi5622_post_refuse(dev, what,
					 "per-load frame budget exhausted",
					 -EPERM);
		goto out;
	}
	if (!ctx->windows_programmed) {
		ret = hi5622_post_refuse(dev, what,
					 "iATU windows not programmed", -EBUSY);
		goto out;
	}
	if (!h->armed) {
		ret = hi5622_post_refuse(dev, what,
					 "HCC SR/DR rings not armed", -EBUSY);
		goto out;
	}
	if (!core->boot_latched) {
		ret = hi5622_post_refuse(dev, what,
					 "golden boot not latched", -EBUSY);
		goto out;
	}
	if (!core->released || !ctx->cpu_released) {
		ret = hi5622_post_refuse(dev, what,
					 "0x5A5A release not observed", -EBUSY);
		goto out;
	}
	if (core->phase != HCC_PHASE_ACTIVE) {
		ret = hi5622_post_refuse(dev, what,
					 "core phase is not ACTIVE (needs ctx-create + TXQ sync)",
					 -EBUSY);
		goto out;
	}
	if (core->h2d_lane_closed) {
		ret = hi5622_post_refuse(dev, what,
					 "H2D lane closed by a previous timeout",
					 -EBUSY);
		goto out;
	}
	/* Back-to-back vendor-order frames (user_add -> user_add_notify) and
	 * any retry after a consumed announce: wait bounded for the core state
	 * and the live out[0] word, then require the inter-frame gap.  A
	 * timeout is a refusal with no device write (one idea, no retry). */
	rc = hi5622_frame_wait_ready(ctx, &now);
	if (rc) {
		ret = hi5622_post_refuse(dev, what,
					 "previous announce/gap did not clear within the bounded wait",
					 rc);
		goto out;
	}

	/* 3. SR commit + coalesced announce under the service mutex. */
	r = &h->sr[0].ete;
	nodes = r->node_arr;
	mutex_lock(&h->lock);
	out0 = readl(ctx->bar0 + hi5622_io_bar0(HI5622_CA_H2D_PENDING));
	if (out0) {
		mutex_unlock(&h->lock);
		ret = hi5622_post_refuse(dev, what,
					 "live out[0] pending word not consumed",
					 -EBUSY);
		goto out;
	}
	producer = core->sr[0].producer;
	slot = hcc_idx_slot(producer);
	if (slot >= r->depth || !nodes || !r->bufs) {
		mutex_unlock(&h->lock);
		atomic_inc(&hi5622_frames_bad);
		dev_err(dev, "frame post '%s': SR0 slot %u outside depth %u\n",
			what, slot, r->depth);
		ret = -EINVAL;
		goto out;
	}
	devva = (u32)r->buf_dma[slot];
	if (devva < HI5622_HOST_DMA_LO || devva > HI5622_HOST_DMA_HI) {
		mutex_unlock(&h->lock);
		atomic_inc(&hi5622_frames_bad);
		dev_err(dev,
			"frame post '%s': SR0 buffer 0x%08x outside the host DMA window\n",
			what, devva);
		ret = -ERANGE;
		goto out;
	}
	memcpy(r->bufs[slot], wire, len);
	nodes[slot].devva = devva;
	nodes[slot].word1 = ((u32)len << 16) | H2D_NODE_FLAGS;
	wmb();
	writel(hcc_idx_plus(producer, r->depth),
	       ctx->bar0 + HI5622_ETE_BAR0 + r->chan_off +
	       HI5622_ETE_CH_A_PRD);
	wmb();
	rc = hcc_core_sr_commit(core, 0, devva, (u8)f->group, (u16)f->id,
				(u16)len, now);
	mutex_unlock(&h->lock);
	if (rc) {
		atomic_inc(&hi5622_frames_bad);
		dev_err(dev,
			"frame post '%s': core refused the SR commit (%d) after the wire write - NO retry, record producer/out[0] and power-cycle\n",
			what, rc);
		ret = -EIO;
		goto out;
	}
	atomic_inc(&hi5622_frames_posted);
	hi5622_last_post_ms = now;

	/* 4. Bounded observation of the coalesced announce (no retry). */
	for (poll = 0; poll < HI5622_POST_POLL_MAX; poll++) {
		out0 = readl(ctx->bar0 + hi5622_io_bar0(HI5622_CA_H2D_PENDING));
		if (out0_after)
			*out0_after = out0;
		if (!out0)
			break;
		if (core->h2d_lane_closed)
			break;
		if (msleep_interruptible(HI5622_POST_POLL_MS))
			break;
	}
	if (out0) {
		atomic_inc(&hi5622_frames_timeout);
		dev_warn(dev,
			 "frame post '%s': announce NOT consumed (out[0]=0x%08x after %u polls); core closes the H2D lane on its own timeout - NO retry\n",
			 what, out0, poll);
		ret = -ETIMEDOUT;
		goto out;
	}
	dev_info(dev,
		 "frame post '%s': g%u id%u total %zu B in SR0 slot %u devva 0x%08x producer 0x%08x -> 0x%08x, coalesced announce consumed after %u polls; handler risk=%s; posted %d/%u\n",
		 what, f->group, f->id, len, slot, devva, producer,
		 hcc_idx_plus(producer, r->depth), poll,
		 wal_cfg ? "WAL_G2_CFG" : h2d_risk_name(handler->risk),
		 atomic_read(&hi5622_frames_posted), hi5622_allow_frames);
out:
	kfree(f);
	return ret;
}

int hi5622_frame_post_status_line(char *buf, size_t len)
{
	int n;

	if (!buf || !len)
		return -EINVAL;
	n = snprintf(buf, len,
		     "frame_post: allow_frames=%u gap_ms=%u posted=%d refused=%d timeout=%d bad=%d\n",
		     hi5622_allow_frames, hi5622_frame_gap_ms,
		     atomic_read(&hi5622_frames_posted),
		     atomic_read(&hi5622_frames_refused),
		     atomic_read(&hi5622_frames_timeout),
		     atomic_read(&hi5622_frames_bad));
	if (n < 0)
		return n;
	return (int)((size_t)n < len ? (size_t)n : len - 1);
}
