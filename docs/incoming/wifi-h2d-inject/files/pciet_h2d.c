// SPDX-License-Identifier: GPL-2.0
/*
 * pciet_h2d.c - Hi5622V100 H2D message injector (clean-room, bench
 * instrument).
 *
 * ORIGINAL code written by the wifi-h2d-inject lane (host-only, 2026-11-23)
 * from the project's decoded contract; no vendor source text or object code
 * is copied.  Evidence markers: [V] re-verified from a named local artifact,
 * [V*] device-measured by an earlier attended lane, [I] inference, [U]
 * unknown / not yet pinned.
 *
 * WHAT THIS FILE IS
 *
 *   A userspace-driven, hard-bounded H2D frame injector for the transport
 *   skeleton (../wifi-transport-port/build, or the B3 instrument build).  A
 *   single sysfs store builds one validated HCC frame, posts it on the SR0
 *   ring and rings the H2D doorbell, exactly as the decoded vendor send path
 *   does (wifi-hcc-re REPORT sec 3.1/3.3 and sec 8 item 5):
 *
 *     1. frame = 12-byte HCC header + payload (max payload 0x674, total
 *        0x680) - built/validated by files/pciet_h2d_frame.h, the same pure-C
 *        model the userspace selftest exercises;
 *     2. SR node[producer] = {buffer device VA, (total_len << 16) | 0x6d2b},
 *        packed producer advance (pcie_ete_ring_ptr_plus [V]); the base
 *        skeleton's pciet_ete_sr_send() advances the producer linearly, which
 *        is only correct for fewer than depth frames, so the injector does
 *        its own post here (the base function is left untouched - it is the
 *        bench-proven B2 smoke path);
 *     3. mailbox announce (pcie_msg_send [V]): out[0] (CA 0x40039010) |=
 *        1 << 3, then out[2] (CA 0x400392d4) |= 1;
 *     4. bounded drain-poll of out[0] (the contract's "wait for out[0] back
 *        to 0 before reusing the slot" [V]) and a read-only out[5] checkpoint
 *        (the device writes its ack there; the host NEVER writes it).
 *
 * WHY A SYSFS ATTRIBUTE
 *
 *   Every device write stays a single explicit attended step (the transport
 *   skeleton's safety model), on whichever endpoint's device directory the
 *   operator targets, with no rebuild for each experiment.  The userspace
 *   line is one of:
 *
 *     echo 'group=4 id=1 dst=0 payload=00000000' > <dev>/h2d_inject
 *     echo '12-byte-header+payload hex'           > <dev>/h2d_raw
 *
 *   plus h2d_arm (program/verify SR0), h2d_check (parse/build/validate only,
 *   no device write), h2d_clear (clear a stale host-set out[0] pending word),
 *   h2d_stats (read-only state) and h2d_selftest (the shared self-test on the
 *   target).  Bench procedure: ../REPORT.md sec 7.
 *
 * HARD SAFETY RULES ENCODED HERE
 *
 *   - mailbox id 5 is never posted: the announce is hard-wired to
 *     HI5622_MSG_ETE_DONE (3) and there is no user field that can change it
 *     (the device's id-5 handler is the firmware BUG() trap [V]);
 *   - HCC message id 5 is refused by the frame model before any device
 *     access (defence in depth, see pciet_h2d_frame.h);
 *   - CA 0x400392f0 (out[5]) is never written: every host write goes through
 *     pciet_writel_checked() (the skeleton's single gated writer, exported
 *     for this file) and read-only reads of out[5] are checkpointed;
 *   - payload/total sizes are bounded (0x674/0x680) and the frame is
 *     re-validated immediately before delivery;
 *   - a frame is only posted when out[0] reads 0 (nothing else in flight) and
 *     the SR0 ring is armed and inside the RC inbound DMA window; announce
 *     delivery is bounded (50 ms) and every refusal/timeout is logged and
 *     counted;
 *   - single-shot arming: the SR0 ring is programmed once per module load
 *     (reloading the module is safe only before the CPU release).
 *
 * Contract sources: opensource/docs/incoming/wifi-hcc-re/REPORT.md
 * (sec 3.1, 3.3, 3.5, 5.5, 8), SUMMARY.md; transport: ../wifi-transport-port/.
 */
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>
#include <asm/barrier.h>
#include <asm/page.h>

#include "hi5622_pciet.h"
#include "pciet_h2d_frame.h"

#define H2D_LOG_PREFIX		"hi5622_pciet h2d: "

/* Bounded announce drain-poll: the device should take the mailbox word in
 * microseconds; 500 x 100 us = 50 ms is a bench-safe ceiling. */
#define H2D_DRAIN_POLL_US	100u
#define H2D_DRAIN_POLLS		500u

/* [I] Reuse guard: the firmware's id-3 handler is expected to consume the
 * posted SR node when it consumes the announce; if announces are acknowledged
 * (drain poll succeeds) but no node was consumed we would be wrong, so the
 * guard only refuses when acknowledged frames reach the ring depth without a
 * single drain success - a parked device hits the out[0] check first. */
#define H2D_MAX_OUTSTANDING	((u32)HI5622_ETE_DEPTH)

/* ETE index/phase arithmetic (phase42 E.3 / pcie_ete_ring_ptr_plus [V]). */
#define H2D_ETE_IDX_MASK	0x3ffu

struct hi5622_h2d {
	struct list_head	node;
	struct hi5622_pci	*ctx;
	struct device		*dev;
	struct mutex		lock;		/* serialises one injection at a time */
	bool			armed;		/* SR0 programmed + verified */
	u32			sr_reg;		/* tracked packed SR0 producer */
	u32			sr_posted;	/* frames posted since arm */
	u32			drains_ok;	/* announces the device cleared */
	u32			drain_timeouts;
	u32			refused;
	u32			injected;	/* successful deliveries */
	ssize_t			last_result;	/* 0 or -errno of the last attempt */
	char			last_err[128];
	/* last request / frame */
	u8			last_group;
	u16			last_id;
	u8			last_dst;
	u8			last_a, last_b, last_c;
	u16			last_payload_len;
	u16			last_total;
	u8			last_frame[64];	/* head of the last posted frame */
	u16			last_frame_keep;
	/* last announce observables (all read-only) */
	u32			out0_before, out0_write, out0_after;
	u32			out2_before, out2_write;
	u32			out5_read;	/* [V] read-only device ack word */
	u32			cns_read;	/* SR0 consumer index (device-owned) */
	u32			drain_polls;
};

static LIST_HEAD(h2d_list);
static DEFINE_MUTEX(h2d_list_lock);

/* ---------------------------------------------------------------------
 * Instance lookup / error mapping
 * ------------------------------------------------------------------- */
static struct hi5622_h2d *h2d_find(struct hi5622_pci *ctx)
{
	struct hi5622_h2d *h;

	mutex_lock(&h2d_list_lock);
	list_for_each_entry(h, &h2d_list, node) {
		if (h->ctx == ctx) {
			mutex_unlock(&h2d_list_lock);
			return h;
		}
	}
	mutex_unlock(&h2d_list_lock);
	return NULL;
}

static struct hi5622_h2d *h2d_from_dev(struct device *dev)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));

	if (!ctx)
		return NULL;
	return h2d_find(ctx);
}

static int h2d_to_errno(int code)
{
	switch (code) {
	case H2D_OK:		return 0;
	case H2D_EARG:		return -EINVAL;
	case H2D_EBOUNDS:	return -ERANGE;
	case H2D_EREFUSED:	return -EACCES;
	case H2D_EBUFFER:	return -ENOBUFS;
	default:		return -EIO;
	}
}

static void h2d_set_error(struct hi5622_h2d *h, int code, const char *what)
{
	h->last_result = h2d_to_errno(code);
	h->refused++;
	dev_warn(h->dev, H2D_LOG_PREFIX "%s refused: %s (%s)\n",
		 what, h2d_strerror(code), h->last_err);
}

/* ---------------------------------------------------------------------
 * Compile-time contract asserts (also validates the shared frame model).
 * ------------------------------------------------------------------- */
static void h2d_assert_contract(void)
{
	BUILD_BUG_ON(H2D_HCC_HDR_LEN != 12);
	BUILD_BUG_ON(H2D_HCC_MAX_PAYLOAD != 0x674);
	BUILD_BUG_ON(H2D_HCC_MAX_TOTAL != 0x680);
	BUILD_BUG_ON(H2D_HCC_MAX_TOTAL > HI5622_ETE_BUF_LEN);
	BUILD_BUG_ON(H2D_HCC_MAX_TOTAL > 0xffff);	/* SR node len is u16 */
	BUILD_BUG_ON(H2D_HCC_REFUSED_ID != 5);
	BUILD_BUG_ON(HI5622_MSG_ETE_DONE == 5);
	BUILD_BUG_ON(HI5622_MSG_ETE_DONE > 31);
	BUILD_BUG_ON(HI5622_ETE_DEPTH != 32);
	BUILD_BUG_ON(H2D_HCC_TAG != 0x5a5a);
	BUILD_BUG_ON(HI5622_ETE_NODE_FLAGS != 0x6d2b);
	/* the only two mailbox registers this file writes must not be out[5] */
	BUILD_BUG_ON(HI5622_BAR0_H2D_PENDING ==
		     HI5622_BAR0_IO(HI5622_CA_NEVER_WRITE));
	BUILD_BUG_ON(HI5622_BAR0_H2D_DOORBELL ==
		     HI5622_BAR0_IO(HI5622_CA_NEVER_WRITE));
}

/* ---------------------------------------------------------------------
 * SR0 frame post: copy the frame into the next ring buffer, write the
 * 8-byte node, then commit with the packed producer index
 * (pcie_ete_ring_ptr_plus semantics [V]).
 * ------------------------------------------------------------------- */
static int h2d_sr_post(struct hi5622_h2d *h, const u8 *frame, size_t len)
{
	struct hi5622_pci *ctx = h->ctx;
	struct hi5622_ete_ring *r = &ctx->sr0;
	struct hi5622_ete_node *nodes = r->node_arr;
	u32 idx = h->sr_reg & H2D_ETE_IDX_MASK;
	u32 next;

	if (!r->node_arr || !r->bufs || !r->buf_dma)
		return -ENODEV;
	if (len > r->buf_len)
		return -EINVAL;
	if (idx >= r->depth)
		return -EIO;

	memcpy(r->bufs[idx], frame, len);
	nodes[idx].devva = cpu_to_le32((u32)r->buf_dma[idx]);
	WRITE_ONCE(nodes[idx].word1,
		   cpu_to_le32(((u32)len << HI5622_ETE_NODE_LEN_SHIFT) |
			       HI5622_ETE_NODE_FLAGS));
	wmb();

	next = h->sr_reg + 1;
	if ((next & H2D_ETE_IDX_MASK) == r->depth) {
		next &= ~H2D_ETE_IDX_MASK;
		next ^= HI5622_ETE_IDX_PHASE_BIT;
	}
	pciet_writel_checked(ctx, HI5622_BAR0_ETE_BASE + HI5622_ETE_SR0_OFF +
			     HI5622_ETE_CH_A_PRD, next);
	h->sr_reg = next;
	h->sr_posted++;
	return 0;
}

/* ---------------------------------------------------------------------
 * Mailbox announce + bounded drain-poll + read-only out[5] checkpoint.
 * pcie_msg_send [V]: out[0] |= 1<<3, then out[2] |= 1.
 * ------------------------------------------------------------------- */
static int h2d_announce(struct hi5622_h2d *h)
{
	struct hi5622_pci *ctx = h->ctx;
	unsigned long flags;
	u32 i;
	bool drained = false;

	spin_lock_irqsave(&ctx->doorbell_lock, flags);
	h->out0_before = readl(ctx->bar0 + HI5622_BAR0_H2D_PENDING);
	h->out2_before = readl(ctx->bar0 + HI5622_BAR0_H2D_DOORBELL);
	pciet_writel_checked(ctx, HI5622_BAR0_H2D_PENDING,
			     h->out0_before | BIT(HI5622_MSG_ETE_DONE));
	h->out0_write = readl(ctx->bar0 + HI5622_BAR0_H2D_PENDING);
	pciet_writel_checked(ctx, HI5622_BAR0_H2D_DOORBELL,
			     h->out2_before | 1u);
	h->out2_write = readl(ctx->bar0 + HI5622_BAR0_H2D_DOORBELL);
	/* The injector owns the announce, so keep the base module's deferred
	 * pending shadow clear; the base pciet_msg_send_irq() path stays
	 * serialised against this by the same doorbell_lock. */
	ctx->pending = 0;
	spin_unlock_irqrestore(&ctx->doorbell_lock, flags);

	for (i = 0; i < H2D_DRAIN_POLLS; i++) {
		if (!(readl(ctx->bar0 + HI5622_BAR0_H2D_PENDING) &
		      BIT(HI5622_MSG_ETE_DONE))) {
			drained = true;
			break;
		}
		udelay(H2D_DRAIN_POLL_US);
	}
	h->drain_polls = i;
	h->out0_after = readl(ctx->bar0 + HI5622_BAR0_H2D_PENDING);
	/* Read-only checkpoint: the device writes its ack to out[5]; the host
	 * NEVER writes this register (project hard rule). */
	h->out5_read = readl(ctx->bar0 + HI5622_BAR0_IO(HI5622_CA_NEVER_WRITE));

	if (!drained)
		return -ETIMEDOUT;
	return 0;
}

/* ---------------------------------------------------------------------
 * Deliver one already-built-and-validated frame.
 * ------------------------------------------------------------------- */
static int h2d_deliver(struct hi5622_h2d *h, const u8 *frame, size_t len)
{
	struct hi5622_pci *ctx = h->ctx;
	u32 pending, idx;
	int ret;

	if (!h->armed) {
		dev_warn(h->dev, H2D_LOG_PREFIX "not armed: write 1 to h2d_arm first\n");
		return -EBUSY;
	}
	if (h->sr_posted - h->drains_ok >= H2D_MAX_OUTSTANDING) {
		dev_warn(h->dev,
			 H2D_LOG_PREFIX "SR0 reuse guard: %u posted / %u acked (device not consuming?)\n",
			 h->sr_posted, h->drains_ok);
		return -EBUSY;
	}
	pending = readl(ctx->bar0 + HI5622_BAR0_H2D_PENDING);
	if (pending) {
		dev_warn(h->dev,
			 H2D_LOG_PREFIX "out[0] pending bitmap 0x%08x not drained; refusing to post (use h2d_clear only for a stale host-set word, see REPORT sec 7)\n",
			 pending);
		return -EBUSY;
	}
	h->cns_read = readl(ctx->bar0 + HI5622_BAR0_ETE_BASE +
			    HI5622_ETE_SR0_OFF + HI5622_ETE_CH_A_CNS);

	/* Self-healing guard: if anything else advanced the SR0 producer
	 * (e.g. the base module's B1/B2 send attr), resync to hardware before
	 * adding this frame; do not mix the two send paths in one session. */
	{
		u32 live = readl(ctx->bar0 + HI5622_BAR0_ETE_BASE +
				 HI5622_ETE_SR0_OFF + HI5622_ETE_CH_A_PRD);

		if (live != h->sr_reg) {
			dev_warn(h->dev,
				 H2D_LOG_PREFIX "SR0 producer drift: tracked 0x%08x, hardware 0x%08x - resyncing\n",
				 h->sr_reg, live);
			h->sr_reg = live;
		}
	}
	idx = h->sr_reg & H2D_ETE_IDX_MASK;
	ret = h2d_sr_post(h, frame, len);
	if (ret) {
		dev_err(h->dev, H2D_LOG_PREFIX "SR0 post failed: %d\n", ret);
		return ret;
	}
	ret = h2d_announce(h);
	if (ret) {
		h->drain_timeouts++;
		dev_warn(h->dev,
			 H2D_LOG_PREFIX "frame posted but announce NOT acked within %u us: out[0]=0x%08x doorbell=0x%08x out[5]=0x%08x (device parked/dead? ring slot state [I])\n",
			 H2D_DRAIN_POLLS * H2D_DRAIN_POLL_US, h->out0_after,
			 h->out2_write, h->out5_read);
		return ret;
	}
	h->drains_ok++;
	dev_info(h->dev,
		 H2D_LOG_PREFIX "inject ok: frame %zu B posted on SR0 (node idx %u, producer -> 0x%08x), announce out[0] 0x%08x->0x%08x doorbell 0x%08x, drained after %u us, out[5]=0x%08x (read-only)\n",
		 len, idx, h->sr_reg,
		 h->out0_before, h->out0_write, h->out2_write,
		 h->drain_polls * H2D_DRAIN_POLL_US, h->out5_read);
	return 0;
}

/* ---------------------------------------------------------------------
 * Common store body: parse/build/validate/deliver.  builder != 0 selects
 * the key=value syntax, otherwise the raw hex syntax.
 * ------------------------------------------------------------------- */
static ssize_t h2d_store(struct hi5622_h2d *h, const char *buf, size_t count,
			 bool builder)
{
	struct h2d_request *req = NULL;
	u8 *frame = NULL;
	size_t flen = 0;
	ssize_t err = 0;
	int rc;

	if (count > H2D_MAX_RAW_HEX + 64)
		return -EINVAL;
	req = kzalloc(sizeof(*req), GFP_KERNEL);
	frame = kmalloc(H2D_HCC_MAX_TOTAL, GFP_KERNEL);
	if (!req || !frame) {
		err = -ENOMEM;
		goto out;
	}
	mutex_lock(&h->lock);
	if (!h->armed) {
		dev_warn(h->dev, H2D_LOG_PREFIX "not armed: write 1 to h2d_arm first\n");
		err = -EBUSY;
		h->last_result = err;
		goto unlock;
	}
	h->last_err[0] = '\0';
	if (builder) {
		rc = h2d_parse_request(buf, count, req, h->last_err,
				       sizeof(h->last_err));
		if (rc) {
			h2d_set_error(h, rc, "h2d_inject");
			err = h2d_to_errno(rc);
			goto unlock;
		}
		rc = h2d_build_frame(req, frame, H2D_HCC_MAX_TOTAL, &flen,
				     h->last_err, sizeof(h->last_err));
		if (rc) {
			h2d_set_error(h, rc, "h2d_inject build");
			err = h2d_to_errno(rc);
			goto unlock;
		}
		h->last_group = req->group;
		h->last_id = req->id;
		h->last_dst = req->dst;
		h->last_a = req->field_a;
		h->last_b = req->field_b;
		h->last_c = req->field_c;
		h->last_payload_len = req->payload_len;
		dev_info(h->dev,
			 H2D_LOG_PREFIX "request: group=%u id=%u dst=%u a=0x%02x b=0x%02x c=0x%02x payload=%u B\n",
			 req->group, req->id, req->dst, req->field_a,
			 req->field_b, req->field_c, req->payload_len);
	} else {
		rc = h2d_parse_raw_frame(buf, count, frame, H2D_HCC_MAX_TOTAL,
					 &flen, h->last_err,
					 sizeof(h->last_err));
		if (rc) {
			h2d_set_error(h, rc, "h2d_raw");
			err = h2d_to_errno(rc);
			goto unlock;
		}
		h->last_group = frame[0] & H2D_HCC_GROUP_MAX;
		h->last_id = (u16)(frame[6] | ((u16)frame[7] << 8));
		h->last_dst = frame[9];
		h->last_a = frame[2];
		h->last_b = frame[3];
		h->last_c = frame[8];
		h->last_payload_len = (u16)(flen - H2D_HCC_HDR_LEN);
		dev_info(h->dev,
			 H2D_LOG_PREFIX "raw request: %zu B frame from hex\n", flen);
	}
	rc = h2d_validate_frame(frame, flen, h->last_err,
				sizeof(h->last_err));
	if (rc) {
		h2d_set_error(h, rc, "frame re-validation");
		err = h2d_to_errno(rc);
		goto unlock;
	}
	h->last_total = (u16)flen;
	h->last_frame_keep = (u16)min_t(size_t, flen, sizeof(h->last_frame));
	memset(h->last_frame, 0, sizeof(h->last_frame));
	memcpy(h->last_frame, frame, h->last_frame_keep);
	print_hex_dump(KERN_INFO, H2D_LOG_PREFIX "frame: ", DUMP_PREFIX_OFFSET,
		       16, 1, frame, min_t(size_t, flen, 64), true);
	rc = h2d_deliver(h, frame, flen);
	if (rc) {
		err = rc;
		h->last_result = rc;
		goto unlock;
	}
	h->injected++;
	h->last_result = 0;
	err = (ssize_t)count;
unlock:
	mutex_unlock(&h->lock);
out:
	kfree(req);
	kfree(frame);
	return err;
}

/* ---------------------------------------------------------------------
 * sysfs attributes
 * ------------------------------------------------------------------- */
static ssize_t h2d_inject_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct hi5622_h2d *h = h2d_from_dev(dev);

	if (!h)
		return -ENODEV;
	if (count > H2D_MAX_REQUEST_TEXT) {
		dev_warn(dev, H2D_LOG_PREFIX "request %zu B > %u B bound\n",
			 count, H2D_MAX_REQUEST_TEXT);
		return -E2BIG;
	}
	return h2d_store(h, buf, count, true);
}
static DEVICE_ATTR_WO(h2d_inject);

static ssize_t h2d_raw_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct hi5622_h2d *h = h2d_from_dev(dev);

	if (!h)
		return -ENODEV;
	if (count > H2D_MAX_RAW_HEX + 64) {
		dev_warn(dev, H2D_LOG_PREFIX "raw request %zu B > %u B bound\n",
			 count, H2D_MAX_RAW_HEX);
		return -E2BIG;
	}
	return h2d_store(h, buf, count, false);
}
static DEVICE_ATTR_WO(h2d_raw);

/* h2d_check: parse/build/validate only - no arming, no device write.  Safe
 * before the CPU release; lets the operator verify a frame line on target. */
static ssize_t h2d_check_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct hi5622_h2d *h = h2d_from_dev(dev);
	struct h2d_request *req;
	u8 *frame;
	size_t flen = 0;
	char err[160];
	int rc;

	if (!h)
		return -ENODEV;
	if (count > H2D_MAX_RAW_HEX + 64)
		return -E2BIG;
	req = kzalloc(sizeof(*req), GFP_KERNEL);
	frame = kmalloc(H2D_HCC_MAX_TOTAL, GFP_KERNEL);
	if (!req || !frame) {
		kfree(req);
		kfree(frame);
		return -ENOMEM;
	}
	err[0] = '\0';
	if (memchr(buf, '=', count)) {
		rc = h2d_parse_request(buf, count, req, err, sizeof(err));
		if (!rc)
			rc = h2d_build_frame(req, frame, H2D_HCC_MAX_TOTAL,
					     &flen, err, sizeof(err));
		if (!rc)
			dev_info(dev,
				 H2D_LOG_PREFIX "check OK: group=%u id=%u dst=%u a=0x%02x b=0x%02x c=0x%02x payload=%u B -> frame %zu B\n",
				 req->group, req->id, req->dst, req->field_a,
				 req->field_b, req->field_c, req->payload_len,
				 flen);
	} else {
		rc = h2d_parse_raw_frame(buf, count, frame, H2D_HCC_MAX_TOTAL,
					 &flen, err, sizeof(err));
		if (!rc)
			dev_info(dev,
				 H2D_LOG_PREFIX "check OK: raw frame %zu B (group=%u id=%u dst=%u)\n",
				 flen, frame[0] & H2D_HCC_GROUP_MAX,
				 frame[6] | ((u16)frame[7] << 8), frame[9]);
	}
	if (rc) {
		dev_warn(dev, H2D_LOG_PREFIX "check refused: %s (%s)\n",
			 h2d_strerror(rc), err);
		kfree(req);
		kfree(frame);
		return h2d_to_errno(rc);
	}
	if (!rc)
		rc = h2d_validate_frame(frame, flen, err, sizeof(err));
	kfree(req);
	kfree(frame);
	return rc ? h2d_to_errno(rc) : (ssize_t)count;
}
static DEVICE_ATTR_WO(h2d_check);

/* h2d_arm: single-shot SR0 programming (host-owned ETE fields only). */
static ssize_t h2d_arm_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct hi5622_h2d *h = h2d_from_dev(dev);
	struct hi5622_pci *ctx;
	void __iomem *ch;
	u32 base, depth, prd, cns, ctrl;
	int ret;

	if (!h)
		return -ENODEV;
	if (!sysfs_streq(buf, "1"))
		return -EINVAL;
	ctx = h->ctx;
	mutex_lock(&h->lock);
	if (h->armed) {
		dev_warn(dev, H2D_LOG_PREFIX "already armed (single-shot per module load; reload only before the CPU release)\n");
		ret = -EBUSY;
		goto out;
	}
	if (!ctx->windows_programmed) {
		dev_err(dev, H2D_LOG_PREFIX "refusing to arm SR0: endpoint iATU windows not programmed (write 1 to prepare first)\n");
		ret = -EIO;
		goto out;
	}
	if (!ctx->sr0.node_arr) {
		dev_err(dev, H2D_LOG_PREFIX "refusing to arm SR0: ring not allocated\n");
		ret = -ENODEV;
		goto out;
	}
	if (ctx->cpu_released)
		dev_warn(dev, H2D_LOG_PREFIX "warning: arming after the CPU release resets the SR0 indices; the bench procedure arms BEFORE release (see REPORT sec 7)\n");
	ret = pciet_ete_ring_init(ctx, &ctx->sr0, true);
	if (ret)
		goto out;
	ch = ctx->bar0 + HI5622_BAR0_ETE_BASE + HI5622_ETE_SR0_OFF;
	base = readl(ch + HI5622_ETE_CH_A_BASE);
	depth = readl(ch + HI5622_ETE_CH_A_DEPTH);
	prd = readl(ch + HI5622_ETE_CH_A_PRD);
	cns = readl(ch + HI5622_ETE_CH_A_CNS);
	ctrl = readl(ch + HI5622_ETE_CH_CTRL);
	if (base < 0x80000000u || base > 0x8fffffffu) {
		dev_err(dev, H2D_LOG_PREFIX "refusing to arm: SR0 base 0x%08x outside the RC inbound DMA window 0x80000000..0x8fffffff\n",
			base);
		ret = -EIO;
		goto out;
	}
	if (depth != (u32)(HI5622_ETE_DEPTH - 1)) {
		dev_err(dev, H2D_LOG_PREFIX "refusing to arm: SR0 depth-1 readback 0x%x != 0x%x\n",
			depth, HI5622_ETE_DEPTH - 1);
		ret = -EIO;
		goto out;
	}
	h->sr_reg = prd;
	h->armed = true;
	dev_info(dev,
		 H2D_LOG_PREFIX "armed: SR0 ch 0x%x base=0x%08x depth-1=0x%x prd=0x%08x cns=0x%08x ctrl=0x%x (read-only, firmware-owned enable +0x00 left untouched)\n",
		 HI5622_ETE_SR0_OFF, base, depth, prd, cns, ctrl);
	ret = (int)count;
out:
	mutex_unlock(&h->lock);
	return ret;
}
static DEVICE_ATTR_WO(h2d_arm);

/* h2d_clear: clear a stale *host-set* out[0] pending bitmap (host-owned
 * register, the device only reads+clears it).  Not a substitute for a real
 * device ack - the bench procedure only uses it after a documented timeout. */
static ssize_t h2d_clear_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct hi5622_h2d *h = h2d_from_dev(dev);
	struct hi5622_pci *ctx;
	u32 before, after;

	if (!h)
		return -ENODEV;
	if (!sysfs_streq(buf, "1"))
		return -EINVAL;
	ctx = h->ctx;
	mutex_lock(&h->lock);
	before = readl(ctx->bar0 + HI5622_BAR0_H2D_PENDING);
	pciet_writel_checked(ctx, HI5622_BAR0_H2D_PENDING, 0);
	after = readl(ctx->bar0 + HI5622_BAR0_H2D_PENDING);
	dev_warn(dev,
		 H2D_LOG_PREFIX "cleared out[0]: 0x%08x -> 0x%08x (stale host-set pending word; device ack state untouched)\n",
		 before, after);
	mutex_unlock(&h->lock);
	return count;
}
static DEVICE_ATTR_WO(h2d_clear);

/* h2d_selftest: run the shared frame-model self-test on the target. */
static ssize_t h2d_selftest_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct h2d_selftest_buf *b;
	char *log;
	u32 passed = 0, failed = 0;
	char *line, *nl;
	int rc;

	if (!sysfs_streq(buf, "1"))
		return -EINVAL;
	b = kzalloc(sizeof(*b), GFP_KERNEL);
	log = kzalloc(16 * 1024, GFP_KERNEL);
	if (!b || !log) {
		kfree(b);
		kfree(log);
		return -ENOMEM;
	}
	rc = h2d_run_selftest(b, log, 16 * 1024, &passed, &failed);
	line = log;
	while (line && *line) {
		nl = strchr(line, '\n');
		if (nl)
			*nl = '\0';
		if (*line)
			dev_info(dev, H2D_LOG_PREFIX "selftest: %s\n", line);
		if (!nl)
			break;
		line = nl + 1;
	}
	dev_info(dev, H2D_LOG_PREFIX "selftest result: %s (%u passed, %u failed)\n",
		 rc == H2D_OK ? "OK" : "FAILED", passed, failed);
	kfree(b);
	kfree(log);
	return rc ? -EIO : (ssize_t)count;
}
static DEVICE_ATTR_WO(h2d_selftest);

static ssize_t h2d_stats_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct hi5622_h2d *h = h2d_from_dev(dev);
	struct hi5622_pci *ctx;
	char hex[2 * sizeof(h->last_frame) + 1];
	int pos = 0;

	if (!h)
		return -ENODEV;
	ctx = h->ctx;
	h2d_frame_to_hex(h->last_frame, h->last_frame_keep, hex, sizeof(hex));

	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "h2d_inject: armed=%d sr_reg=0x%08x posted=%u acked=%u drain_timeouts=%u refused=%u injected=%u last_result=%zd last_err=%s\n",
			 h->armed, h->sr_reg, h->sr_posted, h->drains_ok,
			 h->drain_timeouts, h->refused, h->injected,
			 h->last_result, h->last_err[0] ? h->last_err : "-");
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "last request: group=%u id=%u dst=%u a=0x%02x b=0x%02x c=0x%02x payload=%u total=%u\n",
			 h->last_group, h->last_id, h->last_dst, h->last_a,
			 h->last_b, h->last_c, h->last_payload_len, h->last_total);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "last announce: out0 before=0x%08x write=0x%08x after=0x%08x out2 before=0x%08x write=0x%08x out5_read=0x%08x cns=0x%08x drain_polls=%u\n",
			 h->out0_before, h->out0_write, h->out0_after,
			 h->out2_before, h->out2_write, h->out5_read,
			 h->cns_read, h->drain_polls);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "live: out0=0x%08x out2=0x%08x out5=0x%08x(READ-ONLY) sr0_prd=0x%08x sr0_cns=0x%08x\n",
			 readl(ctx->bar0 + HI5622_BAR0_H2D_PENDING),
			 readl(ctx->bar0 + HI5622_BAR0_H2D_DOORBELL),
			 readl(ctx->bar0 + HI5622_BAR0_IO(HI5622_CA_NEVER_WRITE)),
			 readl(ctx->bar0 + HI5622_BAR0_ETE_BASE +
			       HI5622_ETE_SR0_OFF + HI5622_ETE_CH_A_PRD),
			 readl(ctx->bar0 + HI5622_BAR0_ETE_BASE +
			       HI5622_ETE_SR0_OFF + HI5622_ETE_CH_A_CNS));
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "boot: fw_loaded=%d cpu_released=%d gate_announced=%d windows=%d\n",
			 ctx->fw_loaded, ctx->cpu_released,
			 ctx->gate_announced, ctx->windows_programmed);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "contract: hdr=%u payload_max=0x%x total_max=0x%x tag=0x%04x mailbox_id=%u (out[0]|=1<<%u, out[2]|=1) refused_ids=%u never_write=0x%08x\n",
			 H2D_HCC_HDR_LEN, H2D_HCC_MAX_PAYLOAD,
			 H2D_HCC_MAX_TOTAL, H2D_HCC_TAG, HI5622_MSG_ETE_DONE,
			 HI5622_MSG_ETE_DONE, H2D_HCC_REFUSED_ID,
			 HI5622_CA_NEVER_WRITE);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "last_frame(%u B head): %s\n",
			 h->last_frame_keep, hex);
	return pos;
}
static DEVICE_ATTR_RO(h2d_stats);

static struct attribute *pciet_h2d_attrs[] = {
	&dev_attr_h2d_arm.attr,
	&dev_attr_h2d_inject.attr,
	&dev_attr_h2d_raw.attr,
	&dev_attr_h2d_check.attr,
	&dev_attr_h2d_clear.attr,
	&dev_attr_h2d_selftest.attr,
	&dev_attr_h2d_stats.attr,
	NULL,
};
ATTRIBUTE_GROUPS(pciet_h2d);

/* ---------------------------------------------------------------------
 * probe / remove hooks (called from pciet_probe/pciet_remove)
 * ------------------------------------------------------------------- */
int pciet_h2d_probe(struct hi5622_pci *ctx)
{
	struct device *dev = &ctx->pdev->dev;
	struct hi5622_h2d *h;
	int ret;

	h2d_assert_contract();
	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return -ENOMEM;
	h->ctx = ctx;
	h->dev = dev;
	mutex_init(&h->lock);
	INIT_LIST_HEAD(&h->node);
	ret = device_add_groups(dev, pciet_h2d_groups);
	if (ret) {
		dev_warn(dev, H2D_LOG_PREFIX "device_add_groups failed: %d\n", ret);
		mutex_destroy(&h->lock);
		kfree(h);
		return ret;
	}
	mutex_lock(&h2d_list_lock);
	list_add_tail(&h->node, &h2d_list);
	mutex_unlock(&h2d_list_lock);
	dev_info(dev,
		 H2D_LOG_PREFIX "injector ready (no device writes in probe): h2d_arm/inject/raw/check/clear/selftest/stats; contract payload max 0x%x, id 5 refused\n",
		 H2D_HCC_MAX_PAYLOAD);
	return 0;
}

void pciet_h2d_remove(struct hi5622_pci *ctx)
{
	struct device *dev = &ctx->pdev->dev;
	struct hi5622_h2d *h = h2d_find(ctx);

	if (!h)
		return;
	device_remove_groups(dev, pciet_h2d_groups);
	mutex_lock(&h2d_list_lock);
	list_del(&h->node);
	mutex_unlock(&h2d_list_lock);
	mutex_destroy(&h->lock);
	kfree(h);
	dev_info(dev, H2D_LOG_PREFIX "injector removed\n");
}
