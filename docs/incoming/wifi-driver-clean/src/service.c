// SPDX-License-Identifier: GPL-2.0
/*
 * service.c - kernel-side HCC service for the Hi5622V100 clean-room transport:
 * ETE ring mirrors + vendor-exact arm sequence, the engine group-B secondary
 * receive path, the service loop, probe/remove and the read-only sysfs surface.
 *
 * ORIGINAL clean-room code.  The protocol state machine (latched boot gate,
 * mailbox, ready pair, CECE policy, H2D coalescing, F1-F9 fixes) is hcc_core
 * (hcc.c glue); the primary receive path is the direct device group-A reader
 * (hi5622_d2h.c).  This file never writes a mailbox or gate register: it only
 * performs the vendor ETE arm writes (channel base/depth/ctrl/indices) and the
 * DR +0x38 republish is in hcc.c.
 */
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <asm/barrier.h>

#include "hi5622_hcc.h"

/* Per-endpoint service cadence: the vendor's D2H flush routine (CPU 0xC6108)
 * spins for out[1]==0, so a tight drain keeps that window open. */
unsigned int hi5622_hcc_poll_ms = 5;
module_param_named(hcc_poll_ms, hi5622_hcc_poll_ms, uint, 0644);
MODULE_PARM_DESC(hcc_poll_ms,
		 "HCC service cadence in ms (1..1000, default 5)");

/* CECE policy: 0 = evidence-driven (default), 1 = never write (negative
 * control), 2 = evidence-driven + bounded manual kick. */
unsigned int hi5622_cece_mode;
module_param_named(hcc_cece_mode, hi5622_cece_mode, uint, 0644);
MODULE_PARM_DESC(hcc_cece_mode,
		 "CECE policy: 0=evidence-driven (default), 1=never, 2=+manual kick");

#define HI5622_HCC_IDX_LOW_MASK	0x3ffu

/* ---------------------------------------------------------------------
 * Small helpers
 * ------------------------------------------------------------------- */
static struct hi5622_pci *hcc_ctx(struct device *dev)
{
	return pci_get_drvdata(to_pci_dev(dev));
}

static bool hcc_dma_in_window(u32 dma)
{
	return dma >= HI5622_HOST_DMA_LO && dma <= HI5622_HOST_DMA_HI;
}

static void *hcc_node_va(struct hi5622_ete_ring *r, u32 devva)
{
	u32 i;

	for (i = 0; i < r->depth; i++)
		if ((u32)r->buf_dma[i] == devva)
			return r->bufs[i];
	return NULL;
}

/* Gated ETE-arm write helpers (never-write CA enforced; mailbox writes are
 * the core's business and never go through here). */
static void hcc_writel(struct hi5622_pci *ctx, u32 bar0_off, u32 val)
{
	if (WARN_ON_ONCE(bar0_off == hi5622_io_bar0(HI5622_CA_NEVER_WRITE)))
		return;
	writel(val, ctx->bar0 + bar0_off);
}

static void hcc_ete_writel(struct hi5622_pci *ctx, u32 chan_off, u32 reg_off,
			   u32 val)
{
	u32 off = HI5622_ETE_BAR0 + chan_off + reg_off;

	if (WARN_ON_ONCE(off == hi5622_io_bar0(HI5622_CA_NEVER_WRITE)))
		return;
	writel(val, ctx->bar0 + off);
}

static void hcc_rmw_bfi(struct hi5622_pci *ctx, u32 chan_off, u32 reg_off,
			u32 val, u32 mask)
{
	u32 off = HI5622_ETE_BAR0 + chan_off + reg_off;
	u32 old = readl(ctx->bar0 + off);

	hcc_ete_writel(ctx, chan_off, reg_off, (old & ~mask) | (val & mask));
}

static void hi5622_assert_contract(void)
{
	BUILD_BUG_ON(HI5622_HCC_SR_CH_COUNT != 3);
	BUILD_BUG_ON(HI5622_HCC_DR_CH_COUNT != 4);
	BUILD_BUG_ON(HI5622_ETE_SR0_OFF + HI5622_ETE_CH_STRIDE * 2 +
		     HI5622_ETE_CH_A_CNS >= HI5622_ETE_DR0_OFF);
	BUILD_BUG_ON(HI5622_HCC_FRAME_MAX_TOTAL > HI5622_HCC_SR_BUF_LEN);
	BUILD_BUG_ON(HI5622_HCC_DR_PAYLOAD_CAP > 0x63);
}

/* ---------------------------------------------------------------------
 * Vendor-exact ETE initialisation + arm (wifi-ete-init REPORT sec 2)
 *
 * Order: (1) host-facing intr word RMW &= 0xFFE0F8F8; (2) per SR channel
 * group-A base/depth-1/producer/ctrl; (3) per DR channel group-B base,
 * depth-1 and pre-posted published free index.  depth/ctrl are bfi-faithful
 * RMW (upper bits preserved).  Firmware-owned fields are never written:
 * enable +0x00, ctrl ownership, group-A consumer +0x1C, group-B fill +0x3C
 * and the device-authored send words +0xC00/+0xC18.
 * -------------------------------------------------------------------*/
static int hcc_ete_intr_init(struct hi5622_pci *ctx)
{
	struct device *dev = &ctx->pdev->dev;
	u32 before, after;

	if (!ctx->windows_programmed)
		return -EIO;
	before = readl(ctx->bar0 + hi5622_io_bar0(HI5622_CA_ETE_INTR));
	hcc_writel(ctx, hi5622_io_bar0(HI5622_CA_ETE_INTR),
		   before & HI5622_ETE_INTR_KEEP);
	after = readl(ctx->bar0 + hi5622_io_bar0(HI5622_CA_ETE_INTR));
	dev_info(dev, "HCC ETE: intr CA 0x%08x RMW &= 0x%08x: 0x%08x -> 0x%08x (%s; host-facing word, no firmware reader)\n",
		 HI5622_CA_ETE_INTR, HI5622_ETE_INTR_KEEP, before, after,
		 after == (before & HI5622_ETE_INTR_KEEP) ? "OK" : "MISMATCH");
	return after == (before & HI5622_ETE_INTR_KEEP) ? 0 : -EIO;
}

static void hcc_arm_sr(struct hi5622_pci *ctx, int i)
{
	struct hi5622_hcc_service *h = ctx->hcc;
	struct hcc_ring *hr = &h->sr[i];
	struct hi5622_ete_ring *r = &hr->ete;
	u32 send_word;

	hcc_ete_writel(ctx, r->chan_off, HI5622_ETE_CH_A_BASE,
		       (u32)r->node_dma);
	hcc_rmw_bfi(ctx, r->chan_off, HI5622_ETE_CH_A_DEPTH, r->depth - 1,
		    HI5622_HCC_IDX_LOW_MASK);
	hcc_ete_writel(ctx, r->chan_off, HI5622_ETE_CH_A_PRD, 0);
	hcc_rmw_bfi(ctx, r->chan_off, HI5622_ETE_CH_CTRL, 0, 0x7);
	wmb();
	send_word = readl(ctx->bar0 + HI5622_ETE_BAR0 +
			  HI5622_ETE_SEND_SR0 + 4 * i);
	if (send_word >> 16)
		dev_warn(&ctx->pdev->dev, "HCC arm: SR%u send word 0x%08x has hi16=0x%04x (vendor logs 'already sending' and continues; read-only)\n",
			 i, send_word, send_word >> 16);
	r->producer = 0;
	r->consumer = 0;
}

static void hcc_arm_dr(struct hi5622_pci *ctx, int i)
{
	struct hi5622_hcc_service *h = ctx->hcc;
	struct hcc_ring *hr = &h->dr[i];
	struct hi5622_ete_ring *r = &hr->ete;
	struct hi5622_ete_node *nodes = r->node_arr;
	void __iomem *ch = ctx->bar0 + HI5622_ETE_BAR0 + r->chan_off;
	u32 j, pub = 0;

	hcc_ete_writel(ctx, r->chan_off, HI5622_ETE_CH_B_BASE,
		       (u32)r->node_dma);
	hcc_rmw_bfi(ctx, r->chan_off, HI5622_ETE_CH_B_DEPTH, r->depth - 1,
		    HI5622_HCC_IDX_LOW_MASK);
	/* Pre-post the free buffers: node word0 = buffer dev VA, word1 = 0,
	 * then publish the packed free index (the device never sees an invalid
	 * word0 for a published slot). */
	for (j = 0; j < r->depth; j++) {
		nodes[j].devva = (u32)r->buf_dma[j];
		nodes[j].word1 = 0;
	}
	wmb();
	for (j = 0; j < h->prepost; j++)
		pub = hi5622_idx_plus(pub, r->depth);
	hcc_ete_writel(ctx, r->chan_off, HI5622_ETE_CH_B_PRD, pub);
	wmb();
	hr->pub = pub;
	hr->fill_last = readl(ch + HI5622_ETE_CH_B_CNS);
}

static int hcc_arm_rings(struct hi5622_pci *ctx)
{
	struct hi5622_hcc_service *h = ctx->hcc;
	struct device *dev = &ctx->pdev->dev;
	u32 npre = clamp_t(u32, h->prepost, 1, HI5622_ETE_DEPTH);
	u32 i;
	int ret = 0;

	if (h->armed) {
		dev_warn(dev, "HCC arm: rings already armed in this module load; reload for a fresh arm\n");
		return -EBUSY;
	}
	if (!ctx->windows_programmed) {
		dev_err(dev, "HCC arm: run 'prepare' (endpoint iATU windows) first\n");
		return -EIO;
	}
	for (i = 0; i < HI5622_HCC_SR_CH_COUNT; i++) {
		u32 base = (u32)h->sr[i].ete.node_dma;

		if (!hcc_dma_in_window(base)) {
			dev_err(dev, "HCC arm: SR%u node dma 0x%08x outside the RC inbound window 0x80000000..0x8fffffff; refusing to arm\n",
				i, base);
			return -EIO;
		}
	}
	for (i = 0; i < HI5622_HCC_DR_CH_COUNT; i++) {
		u32 base = (u32)h->dr[i].ete.node_dma;

		if (!hcc_dma_in_window(base)) {
			dev_err(dev, "HCC arm: DR%u node dma 0x%08x outside the RC inbound window 0x80000000..0x8fffffff; refusing to arm\n",
				i, base);
			return -EIO;
		}
	}

	dev_info(dev, "HCC arm: vendor-exact ETE init then SR0-2 (group A) + DR0-3 (group B), depth %u, pre-post %u free buffer(s)/channel; released=%d\n",
		 HI5622_ETE_DEPTH, npre, ctx->cpu_released);

	ret = hcc_ete_intr_init(ctx);
	if (ret)
		return ret;

	for (i = 0; i < HI5622_HCC_SR_CH_COUNT; i++) {
		struct hi5622_ete_ring *r = &h->sr[i].ete;
		void __iomem *ch = ctx->bar0 + HI5622_ETE_BAR0 +
				   r->chan_off;
		u32 base, depth, prd, ctrl;

		hcc_arm_sr(ctx, i);
		rmb();
		base = readl(ch + HI5622_ETE_CH_A_BASE);
		depth = readl(ch + HI5622_ETE_CH_A_DEPTH) &
			HI5622_HCC_IDX_LOW_MASK;
		prd = readl(ch + HI5622_ETE_CH_A_PRD);
		ctrl = readl(ch + HI5622_ETE_CH_CTRL) & 0x7;
		dev_info(dev, "HCC arm: SR%u ch=0x%03x base=0x%08x depth-1=0x%02x prod=0x%08x ctrl=0x%x readback=%s (enable=0x%08x consumer=0x%08x read-only)\n",
			 i, r->chan_off, base, depth, prd, ctrl,
			 (base == (u32)r->node_dma && depth == r->depth - 1 &&
			  prd == 0 && ctrl == 0) ? "OK" : "MISMATCH",
			 readl(ch + HI5622_ETE_CH_ENABLE),
			 readl(ch + HI5622_ETE_CH_A_CNS));
		if (base != (u32)r->node_dma || depth != r->depth - 1 ||
		    prd != 0 || ctrl != 0)
			ret = -EIO;
	}

	for (i = 0; i < HI5622_HCC_DR_CH_COUNT; i++) {
		struct hi5622_ete_ring *r = &h->dr[i].ete;
		void __iomem *ch = ctx->bar0 + HI5622_ETE_BAR0 +
				   r->chan_off;
		u32 base, depth, pub, fill, enable;

		hcc_arm_dr(ctx, i);
		rmb();
		base = readl(ch + HI5622_ETE_CH_B_BASE);
		depth = readl(ch + HI5622_ETE_CH_B_DEPTH) &
			HI5622_HCC_IDX_LOW_MASK;
		pub = readl(ch + HI5622_ETE_CH_B_PRD);
		fill = readl(ch + HI5622_ETE_CH_B_CNS);
		enable = readl(ch + HI5622_ETE_CH_ENABLE);
		dev_info(dev, "HCC arm: DR%u ch=0x%03x base=0x%08x depth-1=0x%02x pub(free)=0x%08x fill(read-only)=0x%08x enable=0x%08x readback=%s\n",
			 i, r->chan_off, base, depth, pub, fill, enable,
			 (base == (u32)r->node_dma && depth == r->depth - 1 &&
			  pub == h->dr[i].pub) ? "OK" : "MISMATCH");
		if (base != (u32)r->node_dma || depth != r->depth - 1 ||
		    pub != h->dr[i].pub)
			ret = -EIO;
	}

	if (ret) {
		dev_err(dev, "HCC arm: register readback MISMATCH - device state not trusted, stop\n");
		return ret;
	}
	h->armed = true;
	h->prepost = npre;
	dev_info(dev, "HCC arm: 7 channels armed, DR free idx=0x%08x/channel for %u buffers; firmware-owned enable still 0x0 (pcie_msg_init sets it later); send words +0xC00/+0xC18 read-only\n",
		 h->dr[0].pub, npre);
	return 0;
}

/* ---------------------------------------------------------------------
 * Engine group-B (secondary) receive path
 *
 * The ETE engine is idle in our reachable boot, so the direct reader is the
 * primary receive path; this walk exists for vendor parity/diagnostics and
 * delivers any decoded frame into the core with HCC_SRC_ENGINE.
 * -------------------------------------------------------------------*/
static void hcc_rx_deliver(struct hi5622_pci *ctx, void *va, u32 len)
{
	if (len < HI5622_HCC_HDR_LEN)
		return;
	(void)hi5622_hcc_deliver(ctx, HCC_SRC_ENGINE, va,
				 (u8 *)va + HI5622_HCC_HDR_LEN,
				 len - HI5622_HCC_HDR_LEN, len);
}

static u32 hcc_walk_dr(struct hi5622_pci *ctx, int ch, bool consume)
{
	struct hi5622_hcc_service *h = ctx->hcc;
	struct hcc_ring *hr = &h->dr[ch];
	struct hi5622_ete_ring *r = &hr->ete;
	struct hi5622_ete_node *nodes = r->node_arr;
	void __iomem *chregs = ctx->bar0 + HI5622_ETE_BAR0 + r->chan_off;
	u32 from = hr->fill_last;
	u32 fill = readl(chregs + HI5622_ETE_CH_B_CNS);
	u32 n = hi5622_idx_count(from, fill, r->depth);
	u32 pos, i;

	if (n > r->depth) {
		dev_warn(&ctx->pdev->dev, "HCC DR%d: implausible completion count %u (from=0x%08x fill=0x%08x pub=0x%08x) - NOT consumed; stop\n",
			 ch, n, from, fill, hr->pub);
		return 0;
	}
	if (!n)
		return 0;
	if (!consume)
		return n;
	pos = from;
	for (i = 0; i < n; i++) {
		u32 low = pos & HI5622_HCC_IDX_LOW_MASK;
		u32 word0, word1, len;
		void *va;

		rmb();
		word0 = le32_to_cpu(nodes[low].devva);
		word1 = le32_to_cpu(nodes[low].word1);
		rmb();
		len = word1 >> HI5622_ETE_NODE_LEN_SHIFT;
		va = hcc_node_va(r, word0);
		if (va && len >= HI5622_HCC_HDR_LEN && len <= r->buf_len)
			hcc_rx_deliver(ctx, va, len);
		else
			dev_warn(&ctx->pdev->dev, "HCC DR%d[%u]: node word0 0x%08x len %u not decodable (va=%s) - skipped\n",
				 ch, low, word0, len, va ? "yes" : "no");
		hr->pub = hi5622_idx_plus(hr->pub, r->depth);
		pos = hi5622_idx_plus(pos, r->depth);
		nodes[low].word1 = 0;
	}
	wmb();
	hcc_ete_writel(ctx, r->chan_off, HI5622_ETE_CH_B_PRD, hr->pub);
	wmb();
	hr->fill_last = fill;
	dev_info(&ctx->pdev->dev, "HCC DR%d: engine path consumed %u node(s); host free idx (+0x38) now 0x%08x\n",
		 ch, n, hr->pub);
	return n;
}

/* Fill watchdog: a device deposit must publish even if the mailbox word was
 * lost; the engine path is secondary in this driver. */
static int hcc_watch_dr(struct hi5622_pci *ctx)
{
	struct hi5622_hcc_service *h = ctx->hcc;
	struct device *dev = &ctx->pdev->dev;
	int ch, found = 0;

	if (!ctx->windows_programmed || !h->armed)
		return 0;
	for (ch = 0; ch < HI5622_HCC_DR_CH_COUNT; ch++) {
		struct hi5622_ete_ring *r = &h->dr[ch].ete;
		void __iomem *regs = ctx->bar0 + HI5622_ETE_BAR0 + r->chan_off;
		u32 fill = readl(regs + HI5622_ETE_CH_B_CNS);

		if (fill == h->dr[ch].fill_last)
			continue;
		dev_info(dev, "HCC DR%d: engine fill(+0x3c) advanced 0x%08x -> 0x%08x WITHOUT a mailbox word; draining (direct reader is the primary path)\n",
			 ch, h->dr[ch].fill_last, fill);
		mutex_lock(&h->lock);
		hcc_walk_dr(ctx, ch, true);
		mutex_unlock(&h->lock);
		found++;
	}
	return found;
}

/* ---------------------------------------------------------------------
 * The service pass: core protocol tick + direct reader + engine watchdog
 * -------------------------------------------------------------------*/
static int hcc_service_pass(struct hi5622_pci *ctx, const char *why)
{
	struct hi5622_hcc_service *h = ctx->hcc;
	struct hcc_core *core = &h->hcc.core;
	int direct;

	if (!ctx->windows_programmed) {
		dev_warn_ratelimited(&ctx->pdev->dev, "HCC service: iATU windows not programmed (run 'prepare' first)\n");
		return 0;
	}

	mutex_lock(&h->lock);
	/* 1. portable protocol core: latched boot gate, mailbox service,
	 * ready deadlines, H2D coalescing, heartbeat/PM, CECE policy. */
	hi5622_hcc_tick(ctx);

	/* 2. primary receive path: direct device group-A rings (delivers into
	 * the core itself). */
	direct = (int)hi5622_d2h_poll(ctx, &h->rd, false);
	mutex_unlock(&h->lock);

	/* 3. secondary engine path watchdog. */
	hcc_watch_dr(ctx);

	if (direct && strcmp(why, "loop"))
		dev_info(&ctx->pdev->dev, "HCC service[%s]: direct reader decoded %d node(s); phase=%s frames=%u\n",
			 why, direct, hcc_phase_name(core->phase),
			 core->frames_seen);
	return direct;
}

static int hcc_loop_thread(void *data)
{
	struct hi5622_hcc_service *h = data;
	struct hi5622_pci *ctx = h->ctx;
	struct device *dev = &ctx->pdev->dev;

	dev_info(dev, "HCC loop: service thread started (cadence %u ms): core protocol tick + direct D2H reader (primary) + engine watchdog; cece_mode=%u\n",
		 hi5622_hcc_poll_ms, hi5622_cece_mode);
	while (!kthread_should_stop()) {
		hcc_service_pass(ctx, "loop");
		if (msleep_interruptible(hi5622_hcc_poll_ms ?
					 hi5622_hcc_poll_ms : 1))
			break;
	}
	dev_info(dev, "HCC loop: stopped; phase=%s d2h_words=%llu h2d[pub=%u done=%u closed=%u] cece[w=%u blocked=%u] direct frames=%llu\n",
		 hcc_phase_name(h->hcc.core.phase),
		 (unsigned long long)h->hcc.core.d2h_words,
		 h->hcc.core.mbox_publishes, h->hcc.core.mbox_completions,
		 h->hcc.core.h2d_lane_closed ? 1u : 0u,
		 h->hcc.core.cece_writes, h->hcc.core.cece_blocked_boot,
		 (unsigned long long)h->rd.frames);
	return 0;
}

/* =====================================================================
 * Read-only attributes
 * ===================================================================*/
static ssize_t hcc_status_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct hi5622_pci *ctx = hcc_ctx(dev);
	struct hi5622_hcc_service *h;
	struct hcc_core *core;
	int pos = 0, ch;
	char line[512];

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	h = ctx->hcc;
	core = &h->hcc.core;
	hcc_core_status(core, line, sizeof(line));
	pos += scnprintf(buf + pos, PAGE_SIZE - pos, "HCC %s\n", line);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "transport: windows=%d fw_loaded=%d fw_verified=%d blocks_seeded=%d cpu_released=%d cece_mode=%u\n",
			 ctx->windows_programmed, ctx->fw_loaded, ctx->fw_verified,
			 ctx->blocks_seeded, ctx->cpu_released, hi5622_cece_mode);
	if (ctx->windows_programmed) {
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "handshake CA 0x%08x = 0x%08x/0x%08x/0x%08x/0x%08x golden=%d latched=%d page_lost=%u\n",
				 HI5622_HANDSHAKE_CPU,
				 hi5622_transport_page(ctx, 0x0),
				 hi5622_transport_page(ctx, 0x4),
				 hi5622_transport_page(ctx, 0x8),
				 hi5622_transport_page(ctx, 0xc),
				 core->handshake_golden,
				 core->boot_latched,
				 core->page_lost_after_latch);
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "state CA 0x%08x=0x%08x mask CA 0x%08x=0x%08x gate(16-bit)=0x%04x\n",
				 HI5622_CPU_BOOT_STATE,
				 hi5622_fw_ram_read(ctx, HI5622_CPU_BOOT_STATE),
				 HI5622_CPU_EXCP_MASK,
				 hi5622_fw_ram_read(ctx, HI5622_CPU_EXCP_MASK),
				 readw(ctx->bar0 + hi5622_io_bar0(HI5622_CA_GATE)));
		for (ch = 0; ch < HI5622_HCC_DR_CH_COUNT; ch++) {
			struct hi5622_ete_ring *r = &h->dr[ch].ete;

			pos += scnprintf(buf + pos, PAGE_SIZE - pos,
					 "dr%d: pub(+0x38)=0x%08x fill(+0x3c)=0x%08x enable=0x%08x\n",
					 ch,
					 readl(ctx->bar0 + HI5622_ETE_BAR0 + r->chan_off + HI5622_ETE_CH_B_PRD),
					 readl(ctx->bar0 + HI5622_ETE_BAR0 + r->chan_off + HI5622_ETE_CH_B_CNS),
					 readl(ctx->bar0 + HI5622_ETE_BAR0 + r->chan_off + HI5622_ETE_CH_ENABLE));
		}
	}
	{
		char direct[256];

		if (hi5622_d2h_ready_line(ctx, &h->rd, direct,
					  sizeof(direct)))
			pos += scnprintf(buf + pos, PAGE_SIZE - pos, "%s\n",
					 direct);
	}
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "h2d probe: announces_ok=%llu timeout=%llu refused=%llu used=%u/%u\n",
			 (unsigned long long)h->sr_announce_ok,
			 (unsigned long long)h->sr_announce_timeout,
			 (unsigned long long)h->sr_announce_refused,
			 h->sr_announce_count, HI5622_HCC_SR_ANNOUNCE_MAX);
	return pos;
}
static DEVICE_ATTR_RO(hcc_status);

static ssize_t hcc_ready_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct hi5622_pci *ctx = hcc_ctx(dev);
	struct hi5622_hcc_service *h;
	struct hcc_core *core;
	char hex[READ_PAYLOAD_MAX * 2 + 1];
	int pos = 0, i;

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	h = ctx->hcc;
	core = &h->hcc.core;
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "HCC ready exchange: phase=%s fail=%s boot_latched=%d boot_age_ms=%llu ready={id1=%u id2=%u map=0x%x id2plen=%u}\n",
			 hcc_phase_name(core->phase),
			 core->fail_reason == HCC_FAIL_NONE ? "none" :
			 core->fail_reason == HCC_FAIL_BOOT_TIMEOUT ?
				"boot_timeout" :
			 core->fail_reason == HCC_FAIL_PLAT_READY_TIMEOUT ?
				"plat_ready_timeout" : "device_ready_timeout",
			 core->boot_latched,
			 (unsigned long long)(core->boot_latched ?
				hcc_kernel_now_ms() - core->boot_latched_ms : 0),
			 core->plat_ready_count, core->dev_ready_count,
			 core->ready_map, core->id2_plen);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "direct reader (primary): id1_seen=%d id2_seen=%d payload=0x%08x frames=%llu overruns=%llu layout=%s\n",
			 h->rd.id1_seen, h->rd.id2_seen, h->rd.id2_payload,
			 (unsigned long long)h->rd.frames,
			 (unsigned long long)h->rd.overruns,
			 (h->rd.ch[0].layout_ok || h->rd.ch[1].layout_ok ||
			  h->rd.ch[2].layout_ok || h->rd.ch[3].layout_ok) ?
				"OK" : "MISMATCH");
	if (core->id2_plen) {
		for (i = 0; i < core->id2_plen && i < READ_PAYLOAD_MAX; i++)
			snprintf(hex + i * 2, sizeof(hex) - i * 2, "%02x",
				 core->id2_payload[i]);
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "g4 id2 payload = %s (4-byte device RAM address = vendor g_dmac_to_hmac_read_msg[0])\n",
				 hex);
	}
	if (core->phase == HCC_PHASE_READY_TXQ_SYNC)
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "VERDICT: READY - the vendor host would resume multi_chip_loading. The first vendor H2D is the group-0 id-1 TXQ config sync with a dynamic payload: do NOT synthesize it. CLEAN if this boot also needs the liveness probe only, use hcc_sr_announce.\n");
	else if (core->phase == HCC_PHASE_ACTIVE)
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "VERDICT: ACTIVE - TXQ sync H2D was submitted (visible H2D lane open). Continue the vendor post-ready sequence.\n");
	else if (core->phase == HCC_PHASE_FAILED)
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "VERDICT: ready handshake TIMEOUT (%s); the D2H mailbox keeps being serviced per vendor S9. Record the direct catalogue and stop per the bench rules.\n",
				 core->fail_reason == HCC_FAIL_PLAT_READY_TIMEOUT ?
					"device_plat_ready 2.0 s" :
				 core->fail_reason == HCC_FAIL_DEVICE_READY_TIMEOUT ?
					"DEVICE READY 20.0 s" : "boot");
	else
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "VERDICT: waiting for the device->host ready pair via the direct reader; keep hcc_loop running, send no H2D before READY.\n");
	return pos;
}
static DEVICE_ATTR_RO(hcc_ready);

static ssize_t hcc_events_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct hi5622_pci *ctx = hcc_ctx(dev);
	struct hi5622_hcc_service *h;
	struct hcc_core *core;
	int pos = 0, i, start;
	char direct[256];

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	h = ctx->hcc;
	core = &h->hcc.core;
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "HCC events: d2h words=%llu last=0x%08x dropped=%u stop_gt10=%u need_dr=%u excp_announce=%d vector_mask=0x%08x\n",
			 (unsigned long long)core->d2h_words,
			 core->d2h_last_bitmap, core->d2h_dropped,
			 core->d2h_stop_gt10, core->d2h_need_dr_drain,
			 core->excp_announce, core->vector_mask);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos, "per-id counts:");
	for (i = 0; i <= (int)HCC_D2H_MAX_ID; i++)
		pos += scnprintf(buf + pos, PAGE_SIZE - pos, " id%d=%u", i,
				 core->d2h_id_count[i]);
	start = core->d2h_hist_pos > 16 ? (int)core->d2h_hist_pos - 16 : 0;
	pos += scnprintf(buf + pos, PAGE_SIZE - pos, "\nhistory[%u]:",
			 core->d2h_hist_pos - start);
	for (i = 0; i < 16 && start + i < (int)core->d2h_hist_pos; i++)
		pos += scnprintf(buf + pos, PAGE_SIZE - pos, " 0x%08x",
				 core->d2h_hist[(start + i) % 16]);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "\ncore: phase=%s frames=%u invalid=%u unknown=%u g4{excp=%u oam=%u hb=%u} g3=%u\n",
			 hcc_phase_name(core->phase), core->frames_seen,
			 core->frames_invalid, core->frames_unknown,
			 core->g4_excp_info, core->g4_oam_log, core->hb_count,
			 core->g3_frames);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "cece: writes=%u blocked_boot=%u blocked_mode=%u evidence=%u kicks=%u pending=%d mode=%u\n",
			 core->cece_writes, core->cece_blocked_boot,
			 core->cece_blocked_mode, core->cece_evidence_events,
			 core->cece_kicks, core->cece_pending,
			 (unsigned)core->cece_mode);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "h2d: pub=%u done=%u coal=%u to=%u closed=%u announces=%u pm_req=%u refused=%u last_out0=0x%08x\n",
			 core->mbox_publishes, core->mbox_completions,
			 core->mbox_coalesced_batches, core->mbox_timeouts,
			 core->h2d_lane_closed ? 1u : 0u, core->h2d_announces,
			 core->h2d_pm_requests, core->h2d_refused,
			 core->last_out0);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "heartbeat: armed=%u count=%u timeouts=%u pm{disallow=%u wake_ok=%u pending=%u timeouts=%u}\n",
			 core->hb_armed ? 1u : 0u, core->hb_count,
			 core->hb_timeouts, core->pm_sleep_disallow ? 1u : 0u,
			 core->pm_wake_ok ? 1u : 0u,
			 core->pm_sleep_pending ? 1u : 0u,
			 core->pm_sleep_timeouts);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "safety: core writes=%llu bad_writes=%u bad_reads=%u last_write_ca=0x%08x last_write_val=0x%08x\n",
			 (unsigned long long)core->writes, core->bad_writes,
			 core->bad_reads, core->last_write_ca,
			 core->last_write_val);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "s1 read_msg[0] dword0=0x%08x direct_repub=%llu ab_writes=%llu\n",
			 core->read_msg[0][0] | ((u32)core->read_msg[0][1] << 8) |
			 ((u32)core->read_msg[0][2] << 16) |
			 ((u32)core->read_msg[0][3] << 24),
			 (unsigned long long)h->rd.repub_count,
			 (unsigned long long)h->rd.ab_writes);
	if (hi5622_d2h_ready_line(ctx, &h->rd, direct, sizeof(direct)))
		pos += scnprintf(buf + pos, PAGE_SIZE - pos, "%s\n", direct);
	return pos;
}
static DEVICE_ATTR_RO(hcc_events);

static ssize_t hcc_rings_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct hi5622_pci *ctx = hcc_ctx(dev);
	struct hi5622_hcc_service *h;
	int pos = 0, i;

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	h = ctx->hcc;
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "HCC rings: armed=%d prepost=%u depth=%u depth-1=0x%x\n",
			 h->armed, h->prepost, HI5622_ETE_DEPTH,
			 HI5622_ETE_DEPTH - 1);
	if (!ctx->windows_programmed) {
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "(not mapped: run 'prepare' first)\n");
		return pos;
	}
	for (i = 0; i < HI5622_HCC_SR_CH_COUNT; i++) {
		struct hi5622_ete_ring *r = &h->sr[i].ete;
		void __iomem *ch = ctx->bar0 + HI5622_ETE_BAR0 + r->chan_off;

		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "SR%d ch=0x%03x nodes=0x%08x base=0x%08x depth-1=0x%02x prod=0x%08x consumer=0x%08x enable=0x%08x ctrl=0x%x sendword=0x%08x\n",
				 i, r->chan_off, (u32)r->node_dma,
				 readl(ch + HI5622_ETE_CH_A_BASE),
				 readl(ch + HI5622_ETE_CH_A_DEPTH) & HI5622_HCC_IDX_LOW_MASK,
				 readl(ch + HI5622_ETE_CH_A_PRD),
				 readl(ch + HI5622_ETE_CH_A_CNS),
				 readl(ch + HI5622_ETE_CH_ENABLE),
				 readl(ch + HI5622_ETE_CH_CTRL) & 0x7,
				 readl(ctx->bar0 + HI5622_ETE_BAR0 +
				       HI5622_ETE_SEND_SR0 + 4 * i));
	}
	for (i = 0; i < HI5622_HCC_DR_CH_COUNT; i++) {
		struct hi5622_ete_ring *r = &h->dr[i].ete;
		void __iomem *ch = ctx->bar0 + HI5622_ETE_BAR0 + r->chan_off;

		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "DR%d ch=0x%03x nodes=0x%08x base=0x%08x depth-1=0x%02x pub(+0x38)=0x%08x fill(+0x3c)=0x%08x enable=0x%08x sendword=0x%08x\n",
				 i, r->chan_off, (u32)r->node_dma,
				 readl(ch + HI5622_ETE_CH_B_BASE),
				 readl(ch + HI5622_ETE_CH_B_DEPTH) & HI5622_HCC_IDX_LOW_MASK,
				 readl(ch + HI5622_ETE_CH_B_PRD),
				 readl(ch + HI5622_ETE_CH_B_CNS),
				 readl(ch + HI5622_ETE_CH_ENABLE),
				 readl(ctx->bar0 + HI5622_ETE_BAR0 +
				       HI5622_ETE_SEND_DR0 + 4 * i));
	}
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "NOTE: enable/ctrl/fill/+0xC00/+0xC18 are firmware or device authored and are never written by this host. The direct reader reads the device group-A rings without touching the engine registers.\n");
	return pos;
}
static DEVICE_ATTR_RO(hcc_rings);

static ssize_t hcc_plan_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	int pos = 0;

	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "HCC-SERVICE BENCH LADDER (attended; see README/BENCH-PROCEDURE for the full text)\n");
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "S0 insmod hi5622_wifi.ko (probe: no device writes; ring mirrors only)\n");
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "S1 echo 1 > prepare (six endpoint-iATU windows, read-back verified)\n");
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "S2 echo 1 > upload ; echo 1 > verify (firmware loader name under /lib/firmware; diffs must be 0)\n");
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "S3 echo 1 > seed (platform descriptor built in + wifi-custom block from the local firmware dir; read-back verified)\n");
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "S4 echo 1 > hcc_arm (vendor-exact 7-channel ETE arm; NEVER write CECE here)\n");
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "S5 echo 1 > hcc_loop (start the service BEFORE the first event)\n");
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "S6 echo 1 > release (0x5a5a ONLY; the transport records the release with the protocol core; no CECE)\n");
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "S7 watch cat boot hcc_ready hcc_status hcc_events rd_status: LATCHED page 0x86807fef/7/2/abcd, state=1, mask=0; direct reader g4 id1 <=2.0 s then id2 <=20.0 s\n");
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "S8 optional liveness: printf 1 > hcc_sr_announce (valid group-0 id-0 frame + coalesced mailbox id3; one frame per load)\n");
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "S9 optional nudge: echo 1 > hcc_dr_republish (vendor-owned +0x38 only; one per session)\n");
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "STOP RULES: no out[5] write; no device RAM writes; never id5 (firmware BUG()); no group-4 H2D; no CECE before the boot latch; no retries in-session; abort/SError -> capture + power-cycle\n");
	return pos;
}
static DEVICE_ATTR_RO(hcc_plan);

/* =====================================================================
 * Write attributes
 * ===================================================================*/
static ssize_t hcc_arm_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct hi5622_pci *ctx = hcc_ctx(dev);
	int ret;

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	if (!sysfs_streq(buf, "1"))
		return -EINVAL;
	ret = hcc_arm_rings(ctx);
	return ret ? ret : count;
}
static DEVICE_ATTR_WO(hcc_arm);

static ssize_t hcc_service_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct hi5622_pci *ctx = hcc_ctx(dev);
	int direct;

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	if (!sysfs_streq(buf, "1"))
		return -EINVAL;
	direct = hcc_service_pass(ctx, "manual");
	dev_info(dev, "HCC service: one manual pass done (direct nodes=%d); out[1] now=0x%08x\n",
		 direct, readl(ctx->bar0 + hi5622_io_bar0(HI5622_CA_D2H_BITMAP)));
	return count;
}
static DEVICE_ATTR_WO(hcc_service);

static ssize_t hcc_loop_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct hi5622_pci *ctx = hcc_ctx(dev);
	struct hi5622_hcc_service *h;
	unsigned int on;
	int ret;

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	h = ctx->hcc;
	ret = kstrtouint(buf, 0, &on);
	if (ret)
		return ret;
	if (on) {
		if (!ctx->windows_programmed) {
			dev_err(dev, "HCC loop: run 'prepare' first\n");
			return -EIO;
		}
		if (!h->armed) {
			dev_err(dev, "HCC loop: run 'hcc_arm' first (the vendor arms the ETE rings before the release)\n");
			return -EBUSY;
		}
		if (h->loop)
			return count;
		h->loop_ms = hi5622_hcc_poll_ms;
		h->loop = kthread_run(hcc_loop_thread, h, "%s-svc",
				      HI5622_DRV_NAME);
		if (IS_ERR(h->loop)) {
			ret = PTR_ERR(h->loop);
			h->loop = NULL;
			return ret;
		}
	} else if (h->loop) {
		kthread_stop(h->loop);
		h->loop = NULL;
		dev_info(dev, "HCC loop: stopped by request; core counters retained in hcc_events\n");
	}
	return count;
}
static DEVICE_ATTR_WO(hcc_loop);

static ssize_t hcc_clear_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct hi5622_pci *ctx = hcc_ctx(dev);
	struct hcc_core *core;

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	core = &ctx->hcc->hcc.core;
	if (!sysfs_streq(buf, "1"))
		return -EINVAL;
	core->d2h_words = 0;
	core->d2h_last_bitmap = 0;
	core->d2h_coalesced_bits = 0;
	memset(core->d2h_id_count, 0, sizeof(core->d2h_id_count));
	memset(core->d2h_hist, 0, sizeof(core->d2h_hist));
	core->d2h_hist_pos = 0;
	core->frames_seen = 0;
	core->frames_invalid = 0;
	core->frames_unknown = 0;
	core->g4_excp_info = 0;
	core->g4_oam_log = 0;
	core->g3_frames = 0;
	ctx->hcc->sr_announce_ok = 0;
	ctx->hcc->sr_announce_timeout = 0;
	ctx->hcc->sr_announce_refused = 0;
	dev_info(dev, "HCC: telemetry counters cleared (rings/armed/core phase unchanged)\n");
	return count;
}
static DEVICE_ATTR_WO(hcc_clear);

static struct attribute *hi5622_service_attrs[] = {
	&dev_attr_hcc_status.attr,
	&dev_attr_hcc_ready.attr,
	&dev_attr_hcc_events.attr,
	&dev_attr_hcc_rings.attr,
	&dev_attr_hcc_plan.attr,
	&dev_attr_hcc_arm.attr,
	&dev_attr_hcc_loop.attr,
	&dev_attr_hcc_service.attr,
	&dev_attr_hcc_clear.attr,
	NULL,
};
ATTRIBUTE_GROUPS(hi5622_service);

int hi5622_service_add_attrs(struct device *dev)
{
	return device_add_groups(dev, hi5622_service_groups);
}

void hi5622_service_del_attrs(struct device *dev)
{
	device_remove_groups(dev, hi5622_service_groups);
}

/* =====================================================================
 * Probe / remove
 * ===================================================================*/
static void hcc_rings_free(struct hi5622_pci *ctx, struct hi5622_hcc_service *h)
{
	int i;

	for (i = 0; i < HI5622_HCC_DR_CH_COUNT; i++)
		hi5622_ete_ring_free(ctx, &h->dr[i].ete);
	for (i = 0; i < HI5622_HCC_SR_CH_COUNT; i++)
		hi5622_ete_ring_free(ctx, &h->sr[i].ete);
}

int hi5622_hcc_probe(struct hi5622_pci *ctx)
{
	static const u32 sr_off[HI5622_HCC_SR_CH_COUNT] = {
		HI5622_ETE_SR0_OFF, HI5622_ETE_SR1_OFF, HI5622_ETE_SR2_OFF,
	};
	static const u32 dr_off[HI5622_HCC_DR_CH_COUNT] = {
		HI5622_ETE_DR0_OFF, HI5622_ETE_DR1_OFF,
		HI5622_ETE_DR2_OFF, HI5622_ETE_DR3_OFF,
	};
	struct device *dev = &ctx->pdev->dev;
	struct hi5622_hcc_service *h;
	int i, ret;

	hi5622_assert_contract();
	h = devm_kzalloc(dev, sizeof(*h), GFP_KERNEL);
	if (!h)
		return -ENOMEM;
	h->ctx = ctx;
	h->prepost = HI5622_ETE_DEPTH;
	mutex_init(&h->lock);
	ctx->hcc = h;

	hi5622_d2h_init(ctx, &h->rd);
	ret = hi5622_hcc_init(ctx);
	if (ret)
		goto err_out;

	/* Host-DRAM ring mirrors only - no device writes in probe. */
	for (i = 0; i < HI5622_HCC_SR_CH_COUNT; i++) {
		ret = hi5622_ete_ring_alloc(ctx, &h->sr[i].ete, sr_off[i],
					    HI5622_HCC_SR_BUF_LEN);
		if (ret)
			goto err_free;
	}
	for (i = 0; i < HI5622_HCC_DR_CH_COUNT; i++) {
		ret = hi5622_ete_ring_alloc(ctx, &h->dr[i].ete, dr_off[i],
					    HI5622_HCC_DR_BUF_LEN);
		if (ret)
			goto err_free;
	}
	for (i = 0; i < HI5622_HCC_DR_CH_COUNT; i++)
		if (!hcc_dma_in_window((u32)h->dr[i].ete.node_dma))
			dev_warn(dev, "HCC: DR%u node dma 0x%08x outside the RC inbound window - hcc_arm will refuse\n",
				 i, (u32)h->dr[i].ete.node_dma);
	for (i = 0; i < HI5622_HCC_SR_CH_COUNT; i++)
		if (!hcc_dma_in_window((u32)h->sr[i].ete.node_dma))
			dev_warn(dev, "HCC: SR%u node dma 0x%08x outside the RC inbound window - hcc_arm will refuse\n",
				 i, (u32)h->sr[i].ete.node_dma);

	dev_info(dev, "HCC service ready: 3 SR + 4 DR ring mirrors in host DRAM (no device writes); direct D2H reader initialised (primary path); protocol core armed (release-only, CECE post-latch on evidence)\n");
	return 0;

err_free:
	hcc_rings_free(ctx, h);
err_out:
	ctx->hcc = NULL;
	return ret;
}

void hi5622_hcc_remove(struct hi5622_pci *ctx)
{
	struct hi5622_hcc_service *h = ctx->hcc;

	if (!h)
		return;
	if (h->loop) {
		kthread_stop(h->loop);
		h->loop = NULL;
	}
	hcc_rings_free(ctx, h);
	ctx->hcc = NULL;
	dev_info(&ctx->pdev->dev, "HCC service removed\n");
}
