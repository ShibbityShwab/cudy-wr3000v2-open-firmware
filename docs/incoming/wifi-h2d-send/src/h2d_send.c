// SPDX-License-Identifier: GPL-2.0
/*
 * h2d_send.c - the FIRST real post-ready H2D message path for the Hi5622V100
 * clean-room Wi-Fi driver (Cudy WR3000 v2.0, luofu v2).
 *
 * ORIGINAL clean-room code written by the wifi-h2d-send lane (shared task
 * task-98, 2026-11-24) from the project's committed decoded contract (see
 * REPORT.md for the evidence chain).  No vendor source, object code, firmware
 * byte, calibration value or secret is reproduced here.
 *
 * What this adds on top of the clean driver:
 *   - h2d_txq_sync: the vendor's FIRST post-ready message, byte-exact
 *     (group-0 id-1 hdpp_config_send_event, declared total 0x30, cmd 0x00d8,
 *     len 0x14, 0xff inherited tail).  The vendor's LIVE first post declared
 *     a 72-byte SR node buffer while the header total is 48; h2d_txq_sync
 *     keeps node_len = header total (48), h2d_txq_sync72 sends the same 48
 *     frame bytes in a 72-byte node buffer (48 + 24 x 0xff) with node word1
 *     = (72 << 16) | 0x6d2b - the one-variable transport experiment;
 *   - h2d_ctx_create: the ORDERED precondition of the TXQ sync - the
 *     synthesized group-0 id-1 config event with u16 key 0x0064 that makes the
 *     firmware allocate the per-module context (ctx+0x190) the key-0xd8 TXQ
 *     worker dereferences (../wifi-ctx-create/REPORT.md; payload synthesized
 *     from the firmware parser, not a vendor capture).  The write VALUE 1|2|3
 *     selects the create helper's data[0] type (ctx_create_t1/t2/t3 grammar
 *     aliases); all three share the same carrier/gates/one-frame rule.  The
 *     ordered ladder is create -> (read-only verification) -> txq_sync72, which
 *     needs h2d_send_budget=2 on the load;
 *   - h2d_send: the strict, name-based grammar for the six EXACT frames the
 *     library can synthesize (txq_sync / txq_sync72 / ctx_create / stub /
 *     edca / del_user).  It never accepts a synthetic payload - the firmware
 *     handlers dereference payload words (wifi-h2d-catalog Appendix B);
 *   - h2d_status (RO) and h2d_plan (RO read-only preflight) for the bench.
 *
 * The send path, in vendor order:
 *   1. build + validate the frame with the h2d_frames library (policy level:
 *      tag 0x5a5a, total == wire length, groups 0/2 only, id 5 refused,
 *      payload <= 0x674, handler must exist);
 *   2. gate: latched boot, released, armed, core phase READY_TXQ_SYNC or
 *      ACTIVE, H2D lane open, one-frame budget, liveness probe unused,
 *      previous announce consumed (core coalescing state AND live out[0]);
 *   3. commit the SR node {devva, (total<<16)|0x6d2b}, barrier, advance the
 *      packed producer (low10 | phase<<10, depth 32), barrier, then the
 *      coalesced mailbox id-3 announce through the core allowlist
 *      (hcc_core_sr_commit -> hcc_core_mbox_send: out[0] = 1<<3, out[2] |= 1);
 *   4. bounded out[0] poll: the device clears the pending word when it
 *      consumes the announce.  Scheduled sleeps (5 ms, <= 200 polls = the
 *      core's 1000 ms bound), no busy-wait, no retry: a timeout is recorded
 *      and the core closes the H2D lane on its own bounded timeout.
 *
 * SAFETY: every mailbox/gate write still goes through the core write
 * allowlist (out[0]/out[1]/out[2]/out[3]/out[4]/gate only; out[5] CA
 * 0x400392F0 and the transport release word are never writable).  This file
 * performs no probe-time device access; reads are limited to out[0]/out[2]
 * and the host-owned SR0 producer register, writes to the host-owned SR0
 * node array / message buffer / producer register (the same writes the
 * existing hcc_sr_announce liveness path performs).
 */
#include <linux/delay.h>
#include <linux/device.h>
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
#include "h2d_send.h"
#include "h2d_send_logic.h"

/* Post-ready H2D frames allowed per module load.  1 = the bench default
 * ("one idea, one frame"); 0 is the negative control (compile/load path
 * exercised but no real frame); raise only in a supervised multi-message
 * session. */
unsigned int hi5622_h2d_send_budget = 1;
module_param_named(h2d_send_budget, hi5622_h2d_send_budget, uint, 0644);
MODULE_PARM_DESC(h2d_send_budget,
		 "post-ready H2D frames per module load: 0=negative control, 1=one frame per idea (default), max 8");

#define HI5622_H2D_SEND_BUDGET_MAX	8u
#define HI5622_H2D_POLL_MAX		200u	/* 200 x 5 ms = core 1000 ms bound */
#define HI5622_H2D_POLL_MS		5u

/* ------------------------------------------------------------------ */
/* state snapshot -> portable gate                                     */
/* ------------------------------------------------------------------ */
static struct hi5622_pci *h2d_ctx(struct device *dev)
{
	return pci_get_drvdata(to_pci_dev(dev));
}

static u32 h2d_budget(void)
{
	return hi5622_h2d_send_budget > HI5622_H2D_SEND_BUDGET_MAX ?
		HI5622_H2D_SEND_BUDGET_MAX : hi5622_h2d_send_budget;
}

static void h2d_live_fill(struct hi5622_pci *ctx, struct h2d_live *live)
{
	struct hi5622_hcc_service *h = ctx->hcc;
	struct hcc_core *core;

	memset(live, 0, sizeof(*live));
	if (!h)
		return;
	core = &h->hcc.core;
	live->armed = h->armed && ctx->windows_programmed;
	live->released = core->released && ctx->cpu_released;
	live->boot_latched = core->boot_latched;
	live->lane_closed = core->h2d_lane_closed;
	/* The core's phase machine keys on group/id and therefore latches
	 * ACTIVE/txq_sync_submitted on ctx_create too (it is also g0/id1).  The
	 * driver's gate keeps the semantic distinction from what THIS load
	 * actually committed: only a real TXQ frame sets live->txq_submitted. */
	live->txq_submitted = h->h2d_txq_sync_sent > 0u;
	live->ctx_created = h->h2d_ctx_create_sent > 0u;
	live->phase = core->phase;
	live->frames_sent = h->h2d_frames_sent;
	live->budget = h2d_budget();
	live->liveness_sent = h->sr_announce_count;
	live->mbox_inflight = core->mbox_inflight;
	/* Region-3 window read: only when the iATU windows are programmed and
	 * the SR side is armed (the gate refuses earlier than this read). */
	if (live->armed)
		live->out0 = readl(ctx->bar0 +
				   hi5622_io_bar0(HI5622_CA_H2D_PENDING));
	else
		live->out0 = 0xffffffffu;
}

static int h2d_gate_errno(enum h2d_gate gate)
{
	switch (gate) {
	case H2D_GATE_OK:
		return 0;
	case H2D_GATE_BAD_MSG:
	case H2D_GATE_PHASE:
	case H2D_GATE_TXQ_PHASE:
	case H2D_GATE_DUP_TXQ:
	case H2D_GATE_FRAME_INVALID:
		return -EPERM;
	default:
		return -EBUSY;
	}
}

static void h2d_note_refused(struct hi5622_hcc_service *h, enum h2d_msg msg,
			     enum h2d_gate gate)
{
	if (!h)
		return;
	h->h2d_send_refused++;
	h->h2d_last_msg = (u32)msg;
	h->h2d_last_gate = (u32)gate;
}

static void h2d_hex(const u8 *p, u32 len, char *out, size_t cap)
{
	static const char digits[] = "0123456789abcdef";
	size_t i;

	if (!cap)
		return;
	for (i = 0; i < len && (i * 2 + 2) <= cap; i++) {
		out[i * 2] = digits[p[i] >> 4];
		out[i * 2 + 1] = digits[p[i] & 0x0fu];
	}
	out[i * 2] = '\0';
}

/* ------------------------------------------------------------------ */
/* the send path                                                       */
/* ------------------------------------------------------------------ */
static int h2d_post(struct hi5622_pci *ctx, enum h2d_msg msg, u8 dst,
		    u16 user_id, const u8 *addr, u16 cfg_id, u8 fb)
{
	struct hi5622_hcc_service *h;
	struct device *dev;
	struct hcc_core *core;
	struct hi5622_ete_ring *r;
	struct hi5622_ete_node *nodes;
	struct h2d_frame *f = NULL;
	struct h2d_post_plan plan;
	struct h2d_live live;
	enum h2d_gate gate;
	enum h2d_err herr = H2D_OK;
	u8 *wire = NULL;
	u32 total = 0, node_len = 0, producer, slot, devva, out2, out0 = 0;
	u32 polls = 0;
	u8 pad_byte = 0;
	int ret = 0;

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	h = ctx->hcc;
	dev = &ctx->pdev->dev;
	if (!ctx->windows_programmed || !h->armed) {
		h2d_note_refused(h, msg, H2D_GATE_NOT_ARMED);
		dev_warn(dev, "H2D %s: refused - run prepare + hcc_arm first (SR rings must be programmed and armed); no device write performed\n",
			 h2d_msg_name(msg));
		return -EBUSY;
	}
	core = &h->hcc.core;
	r = &h->sr[0].ete;
	nodes = r->node_arr;

	f = kmalloc(sizeof(*f), GFP_KERNEL);
	wire = kmalloc(HI5622_HCC_FRAME_MAX_TOTAL, GFP_KERNEL);
	if (!f || !wire) {
		ret = -ENOMEM;
		goto out;
	}

	/* 1. Exact frame build + full policy validation (no device access). */
	if (h2d_msg_build_ext(msg, dst, user_id, addr, cfg_id, fb, f, wire,
			  HI5622_HCC_FRAME_MAX_TOTAL, &total, &herr) != 0) {
		h2d_note_refused(h, msg, H2D_GATE_FRAME_INVALID);
		dev_warn(dev, "H2D %s: frame refused by the exact builder/validator (%s); no device write performed\n",
			 h2d_msg_name(msg), h2d_strerror(herr));
		ret = -EPERM;
		goto out;
	}

	/* Post-buffer length: node_len == frame total for every message except
	 * txq_sync72, which declares the vendor's 72-byte live node buffer. */
	if (h2d_msg_emit_plan(msg, total, &node_len, &pad_byte) != 0 ||
	    node_len < total || node_len > HI5622_HCC_SR_BUF_LEN ||
	    node_len > 0xffffu) {
		h2d_note_refused(h, msg, H2D_GATE_FRAME_INVALID);
		dev_warn(dev, "H2D %s: node length plan refused (frame %u B, node %u B); no device write performed\n",
			 h2d_msg_name(msg), total, node_len);
		ret = -EPERM;
		goto out;
	}

	/* 2. Gate + SR commit under the service mutex (sysfs ops serialised;
	 * the service-thread tick only reads/times the mailbox state). */
	mutex_lock(&h->lock);
	h2d_live_fill(ctx, &live);
	gate = h2d_gate_check(&live, msg);
	if (gate != H2D_GATE_OK) {
		mutex_unlock(&h->lock);
		h2d_note_refused(h, msg, gate);
		dev_warn(dev, "H2D %s: refused by the gate '%s' (phase=%s boot_latched=%u budget=%u/%u liveness=%u inflight=0x%08x out0=0x%08x); one frame per idea, no retry\n",
			 h2d_msg_name(msg), h2d_gate_name(gate),
			 hcc_phase_name(live.phase),
			 live.boot_latched ? 1u : 0u, live.frames_sent,
			 live.budget, live.liveness_sent, live.mbox_inflight,
			 live.out0);
		ret = h2d_gate_errno(gate);
		goto out;
	}

	producer = core->sr[0].producer;
	slot = hcc_idx_slot(producer);
	if (slot >= r->depth || !nodes || !r->bufs) {
		mutex_unlock(&h->lock);
		h2d_note_refused(h, msg, H2D_GATE_FRAME_INVALID);
		dev_err(dev, "H2D %s: SR0 slot %u outside depth %u - refused\n",
			h2d_msg_name(msg), slot, r->depth);
		ret = -EINVAL;
		goto out;
	}
	devva = (u32)r->buf_dma[slot];
	if (devva < HI5622_HOST_DMA_LO || devva > HI5622_HOST_DMA_HI) {
		mutex_unlock(&h->lock);
		h2d_note_refused(h, msg, H2D_GATE_FRAME_INVALID);
		dev_err(dev, "H2D %s: SR0 buffer 0x%08x outside the RC inbound window - refused\n",
			h2d_msg_name(msg), devva);
		ret = -ERANGE;
		goto out;
	}
	out2 = readl(ctx->bar0 + hi5622_io_bar0(HI5622_CA_H2D_DOORBELL));
	if (h2d_plan_post(f, producer, r->depth, devva, live.out0, out2,
			  &plan, &herr) != 0) {
		mutex_unlock(&h->lock);
		h2d_note_refused(h, msg, H2D_GATE_FRAME_INVALID);
		dev_warn(dev, "H2D %s: post plan refused (%s); the previous announce is not consumed (H2D_ESTATE) or the frame is invalid - no retry\n",
			 h2d_msg_name(msg), h2d_strerror(herr));
		ret = herr == H2D_ESTATE ? -EBUSY : -EPERM;
		goto out;
	}

	/* node word1 declares the POST-BUFFER length (the 72-byte variant) while
	 * the frame's declared header total stays 0x30.  For every other message
	 * node_len == total and this is h2d_plan_post's own value. */
	if (node_len != total)
		plan.sr.node_word1 = (node_len << 16) | H2D_NODE_FLAGS;

	/* 3. Vendor SR commit order: body -> node -> barrier -> producer
	 * register -> barrier -> coalesced mailbox id-3 announce (core). */
	memcpy(r->bufs[slot], wire, total);
	if (node_len > total)
		memset((u8 *)r->bufs[slot] + total, pad_byte, node_len - total);
	nodes[slot].devva = plan.sr.node_word0;
	nodes[slot].word1 = plan.sr.node_word1;
	wmb();
	writel(plan.sr.producer_after,
	       ctx->bar0 + HI5622_ETE_BAR0 + r->chan_off +
	       HI5622_ETE_CH_A_PRD);
	wmb();
	ret = hcc_core_sr_commit(core, 0, devva, (u8)f->group, (u16)f->id,
				 total, hcc_kernel_now_ms());
	if (ret) {
		h->h2d_desync++;
		h2d_note_refused(h, msg, H2D_GATE_FRAME_INVALID);
		mutex_unlock(&h->lock);
		dev_err(dev, "H2D %s: core refused the SR commit (%d) AFTER the wire write; one-frame rule: no retry - record SR0 producer/consumer and power-cycle\n",
			h2d_msg_name(msg), ret);
		goto out;
	}
	h->h2d_frames_sent++;
	if (msg == H2D_MSG_TXQ_SYNC || msg == H2D_MSG_TXQ_SYNC72)
		h->h2d_txq_sync_sent++;
	else if (h2d_msg_is_ctx(msg))
		h->h2d_ctx_create_sent++;
	else if (msg == H2D_MSG_CFG_CAL)
		h->h2d_cfg_cal_sent++;
	h->h2d_last_msg = (u32)msg;
	h->h2d_last_gate = (u32)H2D_GATE_OK;
	h->h2d_last_total = total;
	h->h2d_last_node_len = node_len;
	mutex_unlock(&h->lock);

	/* 4. Bounded consumption observation of the coalesced announce. */
	for (polls = 0; polls < HI5622_H2D_POLL_MAX; polls++) {
		out0 = readl(ctx->bar0 +
			     hi5622_io_bar0(HI5622_CA_H2D_PENDING));
		h->h2d_send_last_out0 = out0;
		if (!out0)
			break;
		if (core->h2d_lane_closed)
			break;
		if (msleep_interruptible(HI5622_H2D_POLL_MS))
			break;
	}
	if (polls > h->h2d_send_max_polls)
		h->h2d_send_max_polls = polls;
	if (!out0) {
		h->h2d_send_ok++;
		dev_info(dev, "H2D %s: frame %u B in a %u B node committed on SR0 slot %u devva 0x%08x node word1 0x%08x producer 0x%08x -> 0x%08x; coalesced announce id%u out[0]=0x%08x CONSUMED after %u polls; core phase=%s\n",
			 h2d_msg_name(msg), total, node_len, slot, devva,
			 plan.sr.node_word1, producer, plan.sr.producer_after,
			 HI5622_H2D_MSG_ETE_ANNOUNCE, out0, polls,
			 hcc_phase_name(core->phase));
	} else {
		h->h2d_send_timeout++;
		dev_warn(dev, "H2D %s: announce NOT consumed within the bounded window (out[0]=0x%08x after %u polls, lane_closed=%u); the core closes the H2D lane on its own 1000 ms timeout - NO RETRY in this session\n",
			 h2d_msg_name(msg), out0, polls,
			 core->h2d_lane_closed ? 1u : 0u);
		ret = -ETIMEDOUT;
	}

out:
	kfree(f);
	kfree(wire);
	return ret;
}

/* ------------------------------------------------------------------ */
/* sysfs: write attributes                                             */
/* ------------------------------------------------------------------ */
static ssize_t h2d_txq_sync_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct hi5622_pci *ctx = h2d_ctx(dev);
	int ret;

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	if (!sysfs_streq(buf, "1"))
		return -EINVAL;
	/* The canned vendor first post-ready frame is byte-exact with dst 0. */
	ret = h2d_post(ctx, H2D_MSG_TXQ_SYNC, 0u, 0u, NULL, 0u,
			H2D_CAL_FB_DEFAULT_UNSET);
	return ret ? ret : (ssize_t)count;
}
static DEVICE_ATTR_WO(h2d_txq_sync);

/* Same byte-exact frame, but the SR node declares the vendor's 72-byte live
 * buffer (48 frame bytes + 24 x 0xff): the one-variable node-length probe. */
static ssize_t h2d_txq_sync72_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct hi5622_pci *ctx = h2d_ctx(dev);
	int ret;

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	if (!sysfs_streq(buf, "1"))
		return -EINVAL;
	ret = h2d_post(ctx, H2D_MSG_TXQ_SYNC72, 0u, 0u, NULL, 0u,
			H2D_CAL_FB_DEFAULT_UNSET);
	return ret ? ret : (ssize_t)count;
}
static DEVICE_ATTR_WO(h2d_txq_sync72);

/* The ORDERED precondition of the TXQ sync: the synthesized group-0 id-1
 * config event with u16 key 0x0064 (hdpp_config_send_event shape).  It makes
 * the firmware allocate module 4's context (ctx+0x190); the key-0xd8 TXQ
 * worker dereferences that pointer, so a solo TXQ sync faults (PD4).
 * Write value = the create helper's data[0] TYPE: "1" (defaults block + the
 * extra 0xc-byte second allocation), "2" (defaults block), "3" (straight to
 * the shared 0x3a8 allocation).  The three selectors are otherwise identical
 * (same 48-byte frame, only wire byte 24 differs) and share the once-per-load
 * rule.  Type 1's post-allocation path can return 0x3e8 and make the worker
 * free the buffer, so its PACK interpretation differs (REPORT.md sec 12).
 * Same carrier + gates as txq_sync72; not the once-only TXQ idea. */
static ssize_t h2d_ctx_create_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct hi5622_pci *ctx = h2d_ctx(dev);
	enum h2d_msg msg;
	int ret;

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	if (sysfs_streq(buf, "1"))
		msg = H2D_MSG_CTX_CREATE_T1;
	else if (sysfs_streq(buf, "2"))
		msg = H2D_MSG_CTX_CREATE_T2;
	else if (sysfs_streq(buf, "3"))
		msg = H2D_MSG_CTX_CREATE;
	else
		return -EINVAL;
	ret = h2d_post(ctx, msg, 0u, 0u, NULL, 0u,
			H2D_CAL_FB_DEFAULT_UNSET);
	return ret ? ret : (ssize_t)count;
}
static DEVICE_ATTR_WO(h2d_ctx_create);

/* The calibration config event (g0 id2, key 0x0101; wifi-proto-seq lane,
 * task-103): the SIX vendor alg set_* messages.  One frame per load by
 * default (h2d_send_budget; raise only for a supervised multi-message run).
 * Write value: N | "N fb=M" where N = 1..6 in vendor script order
 * (1=2g power 3502, 2=2g low 3503, 3=5g power 3504, 4=5g low 3505,
 * 5=xo ppm cali 3506, 6=rssi 3507) or a literal cfg id 3502..3507, and M is
 * the header field B (module/vap selector, 0..0x12; default 4 = the module
 * the ordered type-1 context-create made; 0 and 0x12 are later-boot
 * fallbacks).  The frame is 298 B (270-byte alg_cfg blob, cfg_type SET,
 * dev_ret 0, need_w4 1, zero value area) in an SR node of the declared
 * total; the expected D2H answer is one 298-byte g1 id3 config-syn frame
 * with syn_id 0x0101 and the cfg id echoed.  Same gates as the other
 * frames: latched boot, released, armed, READY_TXQ_SYNC/ACTIVE, lane open,
 * previous announce consumed, per-load budget. */
static ssize_t h2d_cfg_cal_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct hi5622_pci *ctx = h2d_ctx(dev);
	u16 cfg_id = 0;
	u8 fb = H2D_CAL_FB_DEFAULT_UNSET;
	int ret;

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	ret = h2d_cfg_cal_parse(buf, &cfg_id, &fb);
	if (ret) {
		ctx->hcc->h2d_send_refused++;
		dev_warn(dev, "H2D cfg_cal: grammar rejected (%d); write N or N fb=M with N=1..6 (vendor order: 1=2g power 2=2g low 3=5g power 4=5g low 5=xo ppm 6=rssi) or 3502..3507, M=header field B 0..0x12 default 4\n", ret);
		return ret;
	}
	ret = h2d_post(ctx, H2D_MSG_CFG_CAL, 0u, 0u, NULL, cfg_id, fb);
	return ret ? ret : (ssize_t)count;
}
static DEVICE_ATTR_WO(h2d_cfg_cal);

static ssize_t h2d_send_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct hi5622_pci *ctx = h2d_ctx(dev);
	struct h2d_req req;
	int ret;

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	ret = h2d_req_parse(buf, &req);
	if (ret) {
		ctx->hcc->h2d_send_refused++;
		dev_warn(dev, "H2D send: grammar rejected (%d); use 'txq_sync', 'txq_sync72', 'ctx_create', 'stub [dst=N]', 'edca [dst=N]' or 'del_user user=U [addr=8hex] [dst=N]'\n",
			 ret);
		return ret;
	}
	ret = h2d_post(ctx, req.msg, req.dst, req.user_id,
		       req.has_addr ? req.addr : NULL,
		       req.has_cfg ? req.cfg_id : 0u,
		       req.has_fb ? req.fb : H2D_CAL_FB_DEFAULT_UNSET);
	return ret ? ret : (ssize_t)count;
}
static DEVICE_ATTR_WO(h2d_send);

/* ------------------------------------------------------------------ */
/* sysfs: read-only status and the read-only preflight plan            */
/* ------------------------------------------------------------------ */
static ssize_t h2d_status_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct hi5622_pci *ctx = h2d_ctx(dev);
	struct hi5622_hcc_service *h;
	struct hcc_core *core;
	struct h2d_live live;
	int pos = 0;

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	h = ctx->hcc;
	core = &h->hcc.core;
	h2d_live_fill(ctx, &live);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "H2D send path (real post-ready messages, one frame per idea): budget=%u frames_sent=%u txq_sent=%u ctx_sent=%u cfg_cal_sent=%u\n",
			 h2d_budget(), h->h2d_frames_sent, h->h2d_txq_sync_sent,
			 h->h2d_ctx_create_sent, h->h2d_cfg_cal_sent);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "state: armed=%u released=%u boot_latched=%u phase=%s lane_closed=%u liveness_used=%u ctx_created=%u\n",
			 live.armed ? 1u : 0u, live.released ? 1u : 0u,
			 live.boot_latched ? 1u : 0u,
			 hcc_phase_name(core->phase),
			 live.lane_closed ? 1u : 0u,
			 live.liveness_sent, live.ctx_created ? 1u : 0u);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "gates: txq_sync=%s txq_sync72=%s ctx_create(t1/t2/t3)=%s/%s/%s real_msg=%s cfg_cal=%s out[0]=0x%08x mbox_inflight=0x%08x\n",
			 h2d_gate_name(h2d_gate_check(&live, H2D_MSG_TXQ_SYNC)),
			 h2d_gate_name(h2d_gate_check(&live, H2D_MSG_TXQ_SYNC72)),
			 h2d_gate_name(h2d_gate_check(&live, H2D_MSG_CTX_CREATE_T1)),
			 h2d_gate_name(h2d_gate_check(&live, H2D_MSG_CTX_CREATE_T2)),
			 h2d_gate_name(h2d_gate_check(&live, H2D_MSG_CTX_CREATE)),
			 h2d_gate_name(h2d_gate_check(&live, H2D_MSG_EDCA_OPT)),
			 h2d_gate_name(h2d_gate_check(&live, H2D_MSG_CFG_CAL)),
			 live.out0, live.mbox_inflight);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "counters: ok=%llu timeout=%llu refused=%llu desync=%u last_out0=0x%08x max_polls=%u\n",
			 (unsigned long long)h->h2d_send_ok,
			 (unsigned long long)h->h2d_send_timeout,
			 (unsigned long long)h->h2d_send_refused,
			 h->h2d_desync, h->h2d_send_last_out0,
			 h->h2d_send_max_polls);
	if (!h->h2d_frames_sent && !h->h2d_send_refused &&
	    !h->h2d_send_timeout)
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "last attempt: none (no real H2D frame fired this load)\n");
	else
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "last attempt: msg=%s gate=%s wire_total=%u node_len=%u\n",
				 h2d_msg_name((enum h2d_msg)h->h2d_last_msg),
				 h2d_gate_name((enum h2d_gate)h->h2d_last_gate),
				 h->h2d_last_total, h->h2d_last_node_len);
	return pos;
}
static DEVICE_ATTR_RO(h2d_status);

static ssize_t h2d_plan_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct hi5622_pci *ctx = h2d_ctx(dev);
	struct hi5622_hcc_service *h;
	struct hcc_core *core;
	struct hi5622_ete_ring *r;
	struct h2d_frame *f;
	struct h2d_post_plan plan;
	struct h2d_live live;
	enum h2d_gate gate;
	enum h2d_err herr = H2D_OK;
	u8 *wire;
	char hex[2 * 96 + 1];
	u32 total = 0, producer, slot, devva, out2;
	int pos = 0, rc;

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	h = ctx->hcc;
	core = &h->hcc.core;
	f = kmalloc(sizeof(*f), GFP_KERNEL);
	wire = kmalloc(HI5622_HCC_FRAME_MAX_TOTAL, GFP_KERNEL);
	if (!f || !wire) {
		kfree(f);
		kfree(wire);
		return -ENOMEM;
	}
	rc = h2d_msg_build(H2D_MSG_TXQ_SYNC, 0u, 0u, NULL, f, wire,
			   HI5622_HCC_FRAME_MAX_TOTAL, &total, &herr);
	if (rc) {
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "h2d_plan: canned txq_sync frame build FAILED (%s)\n",
				 h2d_strerror(herr));
		goto pending;
	}
	h2d_hex(wire, total, hex, sizeof(hex));
	{
		u32 nlen = 0;
		u8 padb = 0;

		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "h2d_plan: canned first post-ready message (read-only preflight, no device write)\n"
				 "msg=txq_sync wire_total=%u wire=%s\n",
				 total, hex);
		if (h2d_msg_emit_plan(H2D_MSG_TXQ_SYNC, total, &nlen, &padb) == 0)
			pos += scnprintf(buf + pos, PAGE_SIZE - pos,
					 "msg=txq_sync node_len=%u node_word1=0x%08x pad=0x%02x\n",
					 nlen, (nlen << 16) | H2D_NODE_FLAGS, padb);
		if (h2d_msg_emit_plan(H2D_MSG_TXQ_SYNC72, total, &nlen,
				      &padb) == 0)
			pos += scnprintf(buf + pos, PAGE_SIZE - pos,
					 "msg=txq_sync72 node_len=%u node_word1=0x%08x pad=0x%02x (the %u pad bytes are not part of the declared 0x%x frame total)\n",
					 nlen, (nlen << 16) | H2D_NODE_FLAGS, padb,
					 nlen - total, total);
	}
	/* The ordered prerequisite: the synthesized g0 id1 key-0x64 context
	 * create.  Shown read-only for the bench preflight; no device write. */
	rc = h2d_msg_build(H2D_MSG_CTX_CREATE, 0u, 0u, NULL, f, wire,
			   HI5622_HCC_FRAME_MAX_TOTAL, &total, &herr);
	if (rc) {
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "h2d_plan: canned ctx_create frame build FAILED (%s)\n",
				 h2d_strerror(herr));
	} else {
		u32 nlen = 0;
		u8 padb = 0;

		h2d_hex(wire, total, hex, sizeof(hex));
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "msg=ctx_create wire_total=%u wire=%s\n",
				 total, hex);
		if (h2d_msg_emit_plan(H2D_MSG_CTX_CREATE, total, &nlen,
				      &padb) == 0)
			pos += scnprintf(buf + pos, PAGE_SIZE - pos,
					 "msg=ctx_create node_len=%u node_word1=0x%08x pad=0x%02x (ordered before the TXQ sync; one frame per idea)\n",
					 nlen,
					 (nlen << 16) | H2D_NODE_FLAGS, padb);
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "ctx_create variants: ctx_create_t1/t2 = the same 48-byte frame with data[0] (wire[24]) = 1 / 2; h2d_ctx_create write VALUE selects the type (1|2|3)\n");
	}
	/* The C1 calibration probe: one 298-byte g0 id2 key-0x0101 frame */
	rc = h2d_msg_build_ext(H2D_MSG_CFG_CAL, 0u, 0u, NULL, 0u,
			H2D_CAL_FB_DEFAULT_UNSET, f, wire,
			HI5622_HCC_FRAME_MAX_TOTAL, &total, &herr);
	if (rc) {
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "h2d_plan: canned cfg_cal frame build FAILED (%s)\n",
			 h2d_strerror(herr));
	} else {
		u32 nlen = 0;
		u8 padb = 0;
		u32 nword = 0u;

		if (h2d_msg_emit_plan(H2D_MSG_CFG_CAL, total, &nlen, &padb) == 0)
			nword = (nlen << 16) | H2D_NODE_FLAGS;
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "msg=cfg_cal wire_total=%u node_len=%u node_word1=0x%08x header=%02x%02x%02x%02x key=0x%04x blob_len=%u cfg_id=%u fb=%u\n",
			 total, nlen, nword, wire[0], wire[1], wire[2], wire[3],
			 (unsigned)(wire[20] | (wire[21] << 8)),
			 (unsigned)(wire[22] | (wire[23] << 8)),
			 (unsigned)(wire[24] | (wire[25] << 8)), (unsigned)wire[3]);
	}
	h2d_live_fill(ctx, &live);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "gate ctx_create(t1/t2/t3)=%s/%s/%s (READY-only, once per load; create -> read-only pack -> txq_sync72 needs h2d_send_budget=2 on the load)\n",
			 h2d_gate_name(h2d_gate_check(&live, H2D_MSG_CTX_CREATE_T1)),
			 h2d_gate_name(h2d_gate_check(&live, H2D_MSG_CTX_CREATE_T2)),
			 h2d_gate_name(h2d_gate_check(&live, H2D_MSG_CTX_CREATE)));
	gate = h2d_gate_check(&live, H2D_MSG_TXQ_SYNC);
	if (gate != H2D_GATE_OK) {
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "gate=%s - the send would be REFUSED now (phase=%s boot_latched=%u frames=%u/%u liveness=%u inflight=0x%08x out0=0x%08x)\n",
				 h2d_gate_name(gate),
				 hcc_phase_name(core->phase),
				 live.boot_latched ? 1u : 0u, live.frames_sent,
				 live.budget, live.liveness_sent,
				 live.mbox_inflight, live.out0);
		goto pending;
	}
	/* Rebuild the TXQ frame: the post plan below must describe the frame the
	 * next send would commit (the ctx_create display overwrote f/wire). */
	rc = h2d_msg_build(H2D_MSG_TXQ_SYNC, 0u, 0u, NULL, f, wire,
			   HI5622_HCC_FRAME_MAX_TOTAL, &total, &herr);
	if (rc) {
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "h2d_plan: canned txq_sync frame rebuild FAILED (%s)\n",
				 h2d_strerror(herr));
		goto pending;
	}
	r = &h->sr[0].ete;
	producer = core->sr[0].producer;
	slot = hcc_idx_slot(producer);
	if (slot >= r->depth || !r->buf_dma) {
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "gate=ok but SR0 slot %u is outside the ring - the send would be REFUSED\n",
				 slot);
		goto pending;
	}
	devva = (u32)r->buf_dma[slot];
	out2 = readl(ctx->bar0 + hi5622_io_bar0(HI5622_CA_H2D_DOORBELL));
	if (h2d_plan_post(f, producer, r->depth, devva, live.out0,
			  out2, &plan, &herr) != 0) {
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "gate=ok but post plan REFUSED (%s)\n",
				 h2d_strerror(herr));
		goto pending;
	}
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "gate=ok: phase=%s producer=0x%08x node_index=%u devva=0x%08x node_word1=0x%08x producer_after=0x%08x\n"
			 "announce: mailbox id=%u out[0]=0x%08x out[2]|=1 -> 0x%08x; out[0] poll bound %u x %u ms\n",
			 hcc_phase_name(core->phase), producer, plan.sr.node_index,
			 plan.sr.buffer_devva, plan.sr.node_word1,
			 plan.sr.producer_after, plan.mb.id,
			 plan.mb.pending_write, plan.mb.doorbell_write,
			 HI5622_H2D_POLL_MAX, HI5622_H2D_POLL_MS);
pending:
	kfree(f);
	kfree(wire);
	return pos;
}
static DEVICE_ATTR_RO(h2d_plan);

static struct attribute *hi5622_h2d_attrs[] = {
	&dev_attr_h2d_txq_sync.attr,
	&dev_attr_h2d_txq_sync72.attr,
	&dev_attr_h2d_ctx_create.attr,
	&dev_attr_h2d_cfg_cal.attr,
	&dev_attr_h2d_send.attr,
	&dev_attr_h2d_status.attr,
	&dev_attr_h2d_plan.attr,
	NULL,
};
ATTRIBUTE_GROUPS(hi5622_h2d);

int hi5622_h2d_add_attrs(struct device *dev)
{
	return device_add_groups(dev, hi5622_h2d_groups);
}

void hi5622_h2d_del_attrs(struct device *dev)
{
	device_remove_groups(dev, hi5622_h2d_groups);
}