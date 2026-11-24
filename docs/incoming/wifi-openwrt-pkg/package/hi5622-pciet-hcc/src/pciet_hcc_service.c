// SPDX-License-Identifier: GPL-2.0
/*
 * pciet_hcc_service.c - host-side HCC service counterpart for the Hi5622V100
 * clean-room PCIe/ETE transport, on the CORRECTED boot protocol (GP8).
 *
 * ORIGINAL code by the wifi-hcc-service lane (task-87, 2026-11-24) from the
 * project's decoded contract.  No vendor source text, object code, firmware
 * byte, calibration value or secret is reproduced; comments carry derived
 * addresses/values only.
 *
 * What this file is (and is not):
 *
 *   It is the host counterpart the vendor's plat.ko runs after the CPU
 *   release: (1) the pcie_msg_handle D2H service in the exact vendor order
 *   ack out[3]=1 -> read out[1] -> drain out[1]=0 -> re-arm out[4]=1 ->
 *   dispatch bitmap ids 0..10 lowest first, with the vendor handler
 *   semantics for ids 1/2/3/4/5/6/7; (2) the ETE SR announce carrier for a
 *   VALID 12-byte HCC frame (tag 0x5A5A at +0xA, total length > 0 at +4)
 *   followed by mailbox id 3, exactly as pcie_ete_tx_queue_handle /
 *   shuangta_ete_sr_dscr_fill / pcie_msg_send do; (3) the DR (group B)
 *   receive path: fill tracking at +0x3C, node decode, buffer return via the
 *   host-published free index +0x38, and the chip-group-4 id1/id2 ready
 *   decode with the vendor's 2.0 s / 20.0 s wait semantics; (4) the corrected
 *   CECE policy (the 16-bit CA 0x4000010C register is a dual-use gate: ROM
 *   S1 needs 0xCEED until the handshake page +8 reads 2; the image handler
 *   CPU 0xC6E14 zeroes it and spins for 32-bit 0x0000CECE - so a CECE write
 *   is only ever issued after boot completion and only on observed
 *   handler-spin evidence); (5) the read-only discriminators for the image
 *   IRQ/event layer, the ETE status/send/fill state and the D2H notify
 *   in-flight words, so one attended run can classify the next blocker.
 *
 *   It is not a Wi-Fi driver and it posts no H2D message on the boot path:
 *   the release->ready dialogue is device->host only (wifi-ete-frames,
 *   wifi-hcc-link, wifi-host-replica all agree).  The single H2D announce
 *   path here is the vendor-attested carrier for a liveness probe, bounded
 *   to one frame per stored request, with hard refusals of the known traps
 *   (HCC group-4 H2D has no firmware table; mailbox id 5 is the firmware
 *   BUG(); the 64-byte zero-header frame fails the firmware tag/length
 *   validation).
 *
 * SAFETY CONTRACT (project hard rules, encoded here):
 *   - CA 0x400392F0 (out[5]) is NEVER written: build-time asserts plus the
 *     runtime gate in hcc_writel()/hcc_ete_writel(); the service only READS it.
 *   - The RC misc window 0x10161000 is never touched; UART is never used.
 *   - Firmware-owned ETE fields are never written: channel enable +0x00,
 *     DR ctrl +0x08, group-A consumer +0x1C, group-B fill +0x3C, and the
 *     device-authored send words +0xC00/+0xC18.  They are only read.
 *   - Device RAM is never written (the boot-kick lane's g+0xB4 poke is NOT
 *     part of this service; it is an explicitly separate experiment).
 *   - Every device write is logged; probe() performs no device writes.
 */

#include <linux/types.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/printk.h>
#include <linux/processor.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <asm/barrier.h>

#include "hi5622_pciet.h"
#include "hi5622_hcc_service.h"
#include "hi5622_d2h_direct.h"

#define HCC_RESULT_LEN		4096
#define HCC_MAX_HIST		16
#define HCC_LOOP_MAX_S		3600
#define HCC_DMA_WIN_LO		0x80000000u	/* RC inbound window [V*] */
#define HCC_DMA_WIN_HI		0x8fffffffu	/* 256 MiB identity map [V*] */
#define HCC_IDX_LOW_MASK	0x3ffu
#define HCC_DRV_NAME		"hi5622_pciet_hcc"
#define HCC_SR_BUF_LEN		2048		/* >= max HCC frame (12 + 0x674) */
#define HCC_DR_BUF_LEN		2048

/* Per-endpoint service tuning.  hcc_poll_ms is the service cadence: the
 * vendor's D2H flush routine (CPU 0xC6108) spins for out[1]==0, so a tight
 * drain keeps that window open (boot-kick C1e lesson); default 5 ms. */
static unsigned int hcc_poll_ms = 5;
module_param_named(hcc_poll_ms, hcc_poll_ms, uint, 0644);
MODULE_PARM_DESC(hcc_poll_ms,
		 "HCC D2H service cadence in ms (1..1000, default 5)");

/* CECE policy mode: 0 = event-driven (boot-guarded, default), 1 = never
 * write (diagnostic negative control), 2 = allow the bounded manual kick
 * attribute in addition to the automatic evidence path. */
static unsigned int hcc_cece_mode;
module_param_named(hcc_cece_mode, hcc_cece_mode, uint, 0644);
MODULE_PARM_DESC(hcc_cece_mode,
		 "CECE gate policy: 0=event-driven boot-guarded (default), 1=never, 2=+manual kick");

MODULE_INFO(description,
	     "Hi5622V100 host-side HCC service counterpart on the corrected boot protocol (clean-room, Linux 6.18)");

/* ---------------------------------------------------------------------
 * Instrument state
 * ------------------------------------------------------------------- */
enum hcc_ready_state {
	HCC_RS_PROBE = 0,	/* before the firmware release */
	HCC_RS_WAIT_BOOT,	/* released; waiting for handshake page +8==2 */
	HCC_RS_WAIT_PLAT,	/* waiting for chip-group-4 id1 (2.0 s) */
	HCC_RS_WAIT_DEV,	/* waiting for chip-group-4 id2 (20.0 s) */
	HCC_RS_READY,		/* both ready messages decoded */
};

static const char *hcc_rs_name(int s)
{
	switch (s) {
	case HCC_RS_PROBE:	return "PROBE";
	case HCC_RS_WAIT_BOOT:	return "WAIT_BOOT";
	case HCC_RS_WAIT_PLAT:	return "WAIT_PLAT_READY";
	case HCC_RS_WAIT_DEV:	return "WAIT_DEVICE_READY";
	case HCC_RS_READY:	return "READY";
	default:		return "?";
	}
}

struct hcc_ring {
	struct hi5622_ete_ring	ete;
	u32			fill_last;	/* +0x3C baseline (packed) */
	u32			pub;		/* +0x38 host free index (packed) */
};

struct hi5622_hcc_service {
	struct hi5622_pci	*ctx;
	struct hcc_ring		sr[HI5622_HCC_SR_CH_COUNT];
	struct hcc_ring		dr[HI5622_HCC_DR_CH_COUNT];
	/* direct-read device group-A D2H receive (engine-independent) */
	struct hi5622_d2h_direct	rd;
	struct mutex		lock;
	bool			armed;
	u32			prepost;
	u32			sr_producer;	/* SR0 packed producer copy */
	struct task_struct	*loop;
	unsigned int		loop_ms;

	/* D2H mailbox telemetry */
	u64			events;
	u32			id_seen;
	u32			bit_count[HI5622_D2H_MAX_ID + 1];
	u32			hist[HCC_MAX_HIST];
	u32			hist_pos;
	u32			last_bitmap;

	/* ready exchange */
	enum hcc_ready_state	state;
	bool			boot_seen;
	unsigned long		boot_time;
	bool			plat_timeout;
	bool			dev_timeout;
	bool			g4_id1_seen;
	bool			g4_id2_seen;
	u32			g4_id1_count;
	u32			g4_id2_count;
	u16			g4_id1_len;
	u16			g4_id1_total;
	u16			g4_id2_len;
	u16			g4_id2_total;
	u16			g4_id2_plen;
	u8			g4_id2_payload[HI5622_HCC_DR_PAYLOAD_CAP];
	u16			last_hdr_group;
	u16			last_hdr_id;
	u16			last_hdr_total;
	u16			last_hdr_len;
	u16			last_hdr_tag;
	u32			hcc_rx_nodes;
	u32			dr_rx_nodes;

	/* CECE gate policy + exception watch */
	u64			cece_writes;
	u64			cece_blocked_boot;
	u64			cece_evidence_events;
	u32			cece_kicks;
	bool			cece_pending;
	bool			mask_valid;
	bool			mask_seen_nonzero;
	u32			last_mask;

	/* image IRQ/event layer watch */
	u64			eoi_events;
	u32			last_eoi;
	u32			last_vector;
	bool			eoi_seen;
	bool			vector_seen;

	/* H2D SR announce telemetry */
	u64			sr_announce_ok;
	u64			sr_announce_timeout;
	u64			sr_announce_refused;
	u32			last_sr_total;
	u32			last_sr_group;
	u32			last_sr_id;
	u32			last_sr_out0_before;
	u32			last_sr_out0_write;
	u32			last_sr_out0_after;
	u32			last_sr_out2_before;
	u32			last_sr_out2_after;
	u32			last_sr_out5_read;
	u32			last_sr_cns;
	u32			last_sr_polls;

	/* DR re-publish (host-owned field; S3-only experiment) */
	u64			dr_republish_count;

	/* one-shot boot-page log guard */
	bool			boot_logged;
};

/* ---------------------------------------------------------------------
 * Gated device writes (single helper pair, never-write CA enforced)
 * ------------------------------------------------------------------- */
static void hcc_writel(struct hi5622_pci *ctx, u32 bar0_off, u32 val)
{
	if (WARN_ON_ONCE(bar0_off == HI5622_HCC_BAR0_NEVER_WRITE))
		return;
	writel(val, ctx->bar0 + bar0_off);
}

static void hcc_ete_writel(struct hi5622_pci *ctx, u32 chan_off, u32 reg_off,
			   u32 val)
{
	u32 off = HI5622_BAR0_ETE_BASE + chan_off + reg_off;

	if (WARN_ON_ONCE(off == HI5622_HCC_BAR0_NEVER_WRITE))
		return;
	writel(val, ctx->bar0 + off);
}

/* ---------------------------------------------------------------------
 * Contract asserts (compile time)
 * ------------------------------------------------------------------- */
static void hcc_assert_contract(void)
{
	BUILD_BUG_ON(HI5622_HCC_BAR0_D2H_ACK == HI5622_HCC_BAR0_NEVER_WRITE);
	BUILD_BUG_ON(HI5622_HCC_BAR0_D2H_REARM == HI5622_HCC_BAR0_NEVER_WRITE);
	BUILD_BUG_ON(HI5622_BAR0_D2H_BITMAP == HI5622_HCC_BAR0_NEVER_WRITE);
	BUILD_BUG_ON(HI5622_BAR0_H2D_PENDING == HI5622_HCC_BAR0_NEVER_WRITE);
	BUILD_BUG_ON(HI5622_BAR0_H2D_DOORBELL == HI5622_HCC_BAR0_NEVER_WRITE);
	BUILD_BUG_ON(HI5622_HCC_BAR0_GATE == HI5622_HCC_BAR0_NEVER_WRITE);
	BUILD_BUG_ON(HI5622_HCC_BAR0_ETE_INTR == HI5622_HCC_BAR0_NEVER_WRITE);
	BUILD_BUG_ON(HI5622_HCC_BAR0_ETE_STATUS == HI5622_HCC_BAR0_NEVER_WRITE);
	BUILD_BUG_ON(HI5622_BAR0_IO(HI5622_HCC_CA_D2H_ACK) !=
		     HI5622_HCC_BAR0_D2H_ACK);
	BUILD_BUG_ON(HI5622_BAR0_IO(HI5622_HCC_CA_D2H_REARM) !=
		     HI5622_HCC_BAR0_D2H_REARM);
	BUILD_BUG_ON(sizeof(struct hi5622_ete_node) != 8);
	BUILD_BUG_ON(HI5622_ETE_DEPTH > (int)HCC_IDX_LOW_MASK + 1);
	BUILD_BUG_ON(HI5622_HCC_FRAME_MAX_TOTAL > HCC_SR_BUF_LEN);
	BUILD_BUG_ON(HI5622_HCC_DR_PAYLOAD_CAP > sizeof(((struct hi5622_hcc_service *)0)->g4_id2_payload));
}

/* ---------------------------------------------------------------------
 * Small formatting helper for the read-only attrs
 * ------------------------------------------------------------------- */
struct hcc_out {
	char	*buf;
	size_t	size;
	size_t	pos;
};

static void hcc_out_init(struct hcc_out *o, char *buf, size_t size)
{
	o->buf = buf;
	o->size = size;
	o->pos = 0;
	if (size)
		buf[0] = '\0';
}

static void hcc_printf(struct hcc_out *o, const char *fmt, ...)
{
	va_list args;
	int n;

	if (o->pos + 1 >= o->size)
		return;
	va_start(args, fmt);
	n = vsnprintf(o->buf + o->pos, o->size - o->pos, fmt, args);
	va_end(args);
	if (n < 0)
		return;
	if ((size_t)n >= o->size - o->pos)
		o->pos = o->size - 1;
	else
		o->pos += (size_t)n;
}

/* ---------------------------------------------------------------------
 * Packed ring-index arithmetic [V] (pcie_ete_ring_ptr_plus / get_rls_num)
 *   idx = (idx & 0x3ff) | phase << 10; advance +1, wrap at depth, toggle
 *   the 0x400 phase bit.  count = depth*phase_delta + (low_to - low_from).
 * ------------------------------------------------------------------- */
static u32 hcc_idx_plus(u32 idx, u32 depth)
{
	u32 next = idx + 1;

	if ((next & HCC_IDX_LOW_MASK) == depth) {
		next &= ~HCC_IDX_LOW_MASK;
		next ^= HI5622_ETE_IDX_PHASE_BIT;
	}
	return next;
}

static u32 hcc_idx_count(u32 from, u32 to, u32 depth)
{
	s32 phase = (s32)((to >> 10) & 1u) - (s32)((from >> 10) & 1u);
	s32 delta = phase * (s32)depth +
		    (s32)(to & HCC_IDX_LOW_MASK) -
		    (s32)(from & HCC_IDX_LOW_MASK);

	if (delta < 0)
		delta += 2 * (s32)depth;
	return (u32)delta;
}

static void *hcc_node_va(struct hi5622_ete_ring *r, u32 devva)
{
	u32 i;

	for (i = 0; i < r->depth; i++)
		if ((u32)r->buf_dma[i] == devva)
			return r->bufs[i];
	return NULL;
}

static bool hcc_dma_in_window(u32 dma)
{
	return dma >= HCC_DMA_WIN_LO && dma <= HCC_DMA_WIN_HI;
}

static struct hi5622_pci *hcc_ctx(struct device *dev)
{
	return pci_get_drvdata(to_pci_dev(dev));
}

/* Direct-read D2H accessor for pciet_d2h_direct.c (the hcc service
 * state is private to this file, so the direct path cannot dereference
 * ctx->hcc itself). */
struct hi5622_d2h_direct *pciet_hcc_rd(struct hi5622_pci *ctx)
{
	return ctx && ctx->hcc ? &ctx->hcc->rd : NULL;
}

/* Firmware-RAM and device-IO read helpers (read-only by construction). */
static u32 hcc_ram_read(struct hi5622_pci *ctx, u32 cpu_addr)
{
	return readl(ctx->bar0 + 0x6b8000u + cpu_addr);
}

static u32 hcc_io_read(struct hi5622_pci *ctx, u32 ca)
{
	return readl(ctx->bar0 + HI5622_BAR0_IO(ca));
}

/* Boot handshake completion: the ROM/CPU1 ack word page+0x8 reads 2 [V]. */
static bool hcc_boot_complete(struct hi5622_pci *ctx)
{
	if (!ctx->windows_programmed)
		return false;
	return readl(ctx->bar0 + HI5622_HCC_BAR0_HANDSHAKE_PAGE + 0x8) ==
	       HI5622_HCC_HANDSHAKE_ACK;
}

/* ---------------------------------------------------------------------
 * Vendor-exact ETE initialisation (wifi-ete-init REPORT sec 2) + channel arm
 *
 * Order: (1) host-facing intr word RMW &= 0xFFE0F8F8; (2) per SR channel
 * group-A base/depth-1/producer/ctrl; (3) per DR channel group-B base,
 * depth-1 and pre-posted published free index.  depth/ctrl are bfi-faithful
 * RMW (upper bits preserved).  Firmware-owned fields are never written.
 * ------------------------------------------------------------------- */
static void hcc_rmw_bfi(struct hi5622_pci *ctx, u32 chan_off, u32 reg_off,
			u32 val, u32 mask)
{
	u32 off = HI5622_BAR0_ETE_BASE + chan_off + reg_off;
	u32 old = readl(ctx->bar0 + off);

	hcc_ete_writel(ctx, chan_off, reg_off, (old & ~mask) | (val & mask));
}

static int hcc_ete_intr_init(struct hi5622_pci *ctx)
{
	struct device *dev = &ctx->pdev->dev;
	u32 before, after;

	if (!ctx->windows_programmed)
		return -EIO;
	before = readl(ctx->bar0 + HI5622_HCC_BAR0_ETE_INTR);
	hcc_writel(ctx, HI5622_HCC_BAR0_ETE_INTR,
		   before & HI5622_HCC_ETE_INTR_KEEP);
	after = readl(ctx->bar0 + HI5622_HCC_BAR0_ETE_INTR);
	dev_info(dev, "HCC ETE: intr CA 0x%08x RMW &= 0x%08x: 0x%08x -> 0x%08x (%s; vendor pcie_ete_intr_init parity, host-facing word - no firmware reader)\n",
		 HI5622_HCC_CA_ETE_INTR, HI5622_HCC_ETE_INTR_KEEP, before, after,
		 after == (before & HI5622_HCC_ETE_INTR_KEEP) ? "OK" : "MISMATCH");
	return after == (before & HI5622_HCC_ETE_INTR_KEEP) ? 0 : -EIO;
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
		    HCC_IDX_LOW_MASK);
	hcc_ete_writel(ctx, r->chan_off, HI5622_ETE_CH_A_PRD, 0);
	hcc_rmw_bfi(ctx, r->chan_off, HI5622_ETE_CH_CTRL, 0, 0x7);
	wmb();
	/* vendor pcie_ete_sr_reg_init reads the send word hi16 as an "already
	 * sending" warning only (never writes it) [V].  The send word lives in
	 * the ETE block (not the channel block): ETE + 0xC18 + 4*i. */
	send_word = readl(ctx->bar0 + HI5622_BAR0_ETE_BASE +
			  HI5622_HCC_ETE_SEND_SR0 + 4 * i);
	if (send_word >> 16)
		dev_warn(&ctx->pdev->dev, "HCC arm: SR%u send word 0x%08x has hi16=0x%04x (vendor logs 'already sending' and continues)\n",
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
	void __iomem *ch = ctx->bar0 + HI5622_BAR0_ETE_BASE + r->chan_off;
	u32 j, pub = 0;

	hcc_ete_writel(ctx, r->chan_off, HI5622_ETE_CH_B_BASE,
		       (u32)r->node_dma);
	hcc_rmw_bfi(ctx, r->chan_off, HI5622_ETE_CH_B_DEPTH, r->depth - 1,
		    HCC_IDX_LOW_MASK);
	/* pre-post the free buffers: node word0 = buffer dev VA, word1 = 0,
	 * then publish the packed free index (device never sees an invalid
	 * word0 for a published slot) [V b3 P0 / vendor group-B contract]. */
	for (j = 0; j < r->depth; j++) {
		nodes[j].devva = cpu_to_le32((u32)r->buf_dma[j]);
		nodes[j].word1 = 0;
	}
	wmb();
	for (j = 0; j < h->prepost; j++)
		pub = hcc_idx_plus(pub, r->depth);
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
		dev_warn(dev, "HCC arm: rings already armed in this module load; reload the module for a fresh arm\n");
		return -EBUSY;
	}
	if (!ctx->windows_programmed) {
		dev_err(dev, "HCC arm: run 'prepare' (endpoint iATU windows) first\n");
		return -EIO;
	}

	/* The coherent rings must sit in the RC inbound window the DWC host
	 * driver programmed from the board DTS dma-ranges. */
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

	dev_info(dev, "HCC arm: vendor-exact ETE init then SR0-2 (group A) + DR0-3 (group B), depth %u, pre-post %u free buffer(s)/channel; CPU state: released=%d\n",
		 HI5622_ETE_DEPTH, npre, ctx->cpu_released);

	/* vendor order: intr word first, before any ring write [V] */
	ret = hcc_ete_intr_init(ctx);
	if (ret)
		return ret;

	for (i = 0; i < HI5622_HCC_SR_CH_COUNT; i++) {
		struct hi5622_ete_ring *r = &h->sr[i].ete;
		void __iomem *ch = ctx->bar0 + HI5622_BAR0_ETE_BASE +
				   r->chan_off;
		u32 base, depth, prd, ctrl;

		hcc_arm_sr(ctx, i);
		rmb();
		base = readl(ch + HI5622_ETE_CH_A_BASE);
		depth = readl(ch + HI5622_ETE_CH_A_DEPTH) & HCC_IDX_LOW_MASK;
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
		void __iomem *ch = ctx->bar0 + HI5622_BAR0_ETE_BASE +
				   r->chan_off;
		u32 base, depth, pub, fill, enable;

		hcc_arm_dr(ctx, i);
		rmb();
		base = readl(ch + HI5622_ETE_CH_B_BASE);
		depth = readl(ch + HI5622_ETE_CH_B_DEPTH) & HCC_IDX_LOW_MASK;
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
	h->sr_producer = 0;
	dev_info(dev, "HCC arm: 7 channels armed, DR free idx=0x%08x/channel for %u buffers; firmware-owned enable still 0x0 (pcie_msg_init sets it later); device-authored send words +0xC00/+0xC18 are read-only and NOT written\n",
		 h->dr[0].pub, npre);
	return 0;
}

/* ---------------------------------------------------------------------
 * Chip-group-4 ready frames (device->host over DR)
 *
 *   id1 wire: 04 00 ?? 00 0c 00 01 00 ?? 00 ?? ??   (12 B, no payload)
 *   id2 wire: 04 00 ?? 00 10 00 02 00 ?? 00 ?? ?? 50 08 06 01
 *             (16 B, 4-byte payload = a device RAM address)
 * The host only receives; the vendor handlers complete two waits (200 and
 * 2000 jiffies) and send nothing back (wifi-ete-frames REPORT sec 2).
 * ------------------------------------------------------------------- */
static void hcc_g4_id1(struct hi5622_pci *ctx, int ch, u32 idx, const u8 *p,
		       u32 len, u16 total)
{
	struct hi5622_hcc_service *h = ctx->hcc;
	struct device *dev = &ctx->pdev->dev;
	u32 core = p[1] >> 4;

	h->g4_id1_count++;
	if (core != 0)
		dev_warn(dev, "HCC READY: g4 id1 src core %u != 0 (vendor device_plat_ready_msg_process rejects); recorded\n",
			 core);
	if (!h->g4_id1_seen) {
		h->g4_id1_seen = true;
		h->g4_id1_len = (u16)min_t(u32, len, 0xffffu);
		h->g4_id1_total = total;
		if (h->state == HCC_RS_WAIT_PLAT || h->state == HCC_RS_PROBE)
			h->state = HCC_RS_WAIT_DEV;
		dev_info(dev, "HCC READY: chip-group-4 id1 = device_plat_ready ('Device plat ready! chip id : %u'): dr%d[%u] len=%u total=0x%x - vendor wait #1 (200 jiffies / 2.0 s) RELEASED; no H2D reply\n",
			 core, ch, idx, len, total);
	}
}

static void hcc_g4_id2(struct hi5622_pci *ctx, int ch, u32 idx, const u8 *p,
		       u32 len, u16 total)
{
	struct hi5622_hcc_service *h = ctx->hcc;
	struct device *dev = &ctx->pdev->dev;
	u32 core = p[1] >> 4;
	u32 plen = total >= HI5622_HCC_HDR_LEN ?
		   total - HI5622_HCC_HDR_LEN : 0;

	h->g4_id2_count++;
	if (core != 0)
		dev_warn(dev, "HCC READY: g4 id2 dst/core %u != 0 (vendor host_ready_msg_process uses hdr[9]); recorded\n",
			 p[9] & 0xf);
	if (plen > HI5622_HCC_DR_PAYLOAD_CAP || plen > len - HI5622_HCC_HDR_LEN) {
		dev_warn(dev, "HCC READY: g4 id2 payload length %u B outside the vendor host_ready_msg_process cap (total 0x%x, len %u); recording raw only\n",
			 plen, total, len);
		plen = min_t(u32, plen, min_t(u32, len - HI5622_HCC_HDR_LEN,
					      HI5622_HCC_DR_PAYLOAD_CAP));
	}
	if (!h->g4_id2_seen) {
		h->g4_id2_seen = true;
		h->g4_id2_len = (u16)min_t(u32, len, 0xffffu);
		h->g4_id2_total = total;
		h->g4_id2_plen = (u16)plen;
		memset(h->g4_id2_payload, 0, sizeof(h->g4_id2_payload));
		memcpy(h->g4_id2_payload, p + HI5622_HCC_HDR_LEN,
		       min_t(u32, plen, HI5622_HCC_DR_PAYLOAD_CAP));
		/* vendor host_ready_msg_process copies the payload into a 100-byte
		 * per-core record; single core 0 on this board. */
		if (plen) {
			memcpy(ctx->read_msg[0], p + HI5622_HCC_HDR_LEN,
			       min_t(u32, plen, sizeof(ctx->read_msg[0])));
			dev_info(dev, "HCC READY: DEVICE READY (chip-group-4 id2 = host_ready_msg_process): dr%d[%u] total=0x%x payload=%u B = 0x%08x (device RAM address; vendor record g_dmac_to_hmac_read_msg[0]) - vendor wait #2 (2000 jiffies / 20.0 s) RELEASED\n",
				 ch, idx, total, plen,
				 plen >= 4 ? le32_to_cpup((const __le32 *)h->g4_id2_payload) : 0);
		} else {
			dev_info(dev, "HCC READY: DEVICE READY (chip-group-4 id2): dr%d[%u] total=0x%x payload=0 B (contract violation: vendor expects 4 B)\n",
				 ch, idx, total);
		}
		h->state = HCC_RS_READY;
	}
}

/* Decode one completed HCC frame from a DR node. */
static void hcc_hcc_rx(struct hi5622_pci *ctx, int ch, u32 idx, u32 len,
		       const u8 *p)
{
	struct hi5622_hcc_service *h = ctx->hcc;
	struct device *dev = &ctx->pdev->dev;
	u16 total, id, tag;
	u8 group, rsrc, dst;

	if (len < HI5622_HCC_HDR_LEN) {
		dev_warn(dev, "HCC RX dr%d[%u]: short frame (%u B), no 12-byte HCC header\n",
			 ch, idx, len);
		return;
	}
	group = p[0] & 0x0fu;
	rsrc = p[1] >> 4;
	total = (u16)(p[4] | ((u16)p[5] << 8));
	id = (u16)(p[6] | ((u16)p[7] << 8));
	dst = p[9];
	tag = (u16)(p[10] | ((u16)p[11] << 8));

	h->hcc_rx_nodes++;
	h->last_hdr_group = group;
	h->last_hdr_id = id;
	h->last_hdr_total = total;
	h->last_hdr_len = (u16)min_t(u32, len, 0xffffu);
	h->last_hdr_tag = tag;

	dev_info(dev, "HCC RX dr%d[%u]: len=%u total=0x%x group=%u id=%u rsrc=%u dst=%u tag=0x%04x tag=%s\n",
		 ch, idx, len, total, group, id, rsrc, dst, tag,
		 tag == HI5622_HCC_TAG ? "OK" : "MISMATCH");

	if (tag != HI5622_HCC_TAG) {
		dev_warn(dev, "HCC RX: transport tag != 0x5a5a (firmware-side validation would drop); recorded only\n");
		return;
	}
	if (group != HI5622_HCC_GROUP_PLAT) {
		dev_info(dev, "HCC RX: group %u (not the chip-group-4 ready pair) recorded only\n",
			 group);
		return;
	}
	switch (id) {
	case HI5622_HCC_GROUP_PLAT_ID1:
		hcc_g4_id1(ctx, ch, idx, p, len, total);
		break;
	case HI5622_HCC_GROUP_PLAT_ID2:
		hcc_g4_id2(ctx, ch, idx, p, len, total);
		break;
	default:
		dev_info(dev, "HCC RX: chip-group-4 id%u not in the boot catalogue (0-4) - recorded only\n",
			 id);
		break;
	}
}

/* ---------------------------------------------------------------------
 * DR completion walk (group B receive path, vendor get_rls_num arithmetic)
 *   from = last +0x3C baseline; fill = live +0x3C device service index.
 *   consume: decode each node, return the buffer by advancing the host
 *   free index +0x38 with the packed index (never touch +0x3C).
 * ------------------------------------------------------------------- */
static u32 hcc_walk_dr(struct hi5622_pci *ctx, int ch, bool consume)
{
	struct hi5622_hcc_service *h = ctx->hcc;
	struct hcc_ring *hr = &h->dr[ch];
	struct hi5622_ete_ring *r = &hr->ete;
	struct hi5622_ete_node *nodes = r->node_arr;
	void __iomem *chregs = ctx->bar0 + HI5622_BAR0_ETE_BASE + r->chan_off;
	u32 from = hr->fill_last;
	u32 fill = readl(chregs + HI5622_ETE_CH_B_CNS);
	u32 n = hcc_idx_count(from, fill, r->depth);
	u32 pos, i;

	if (n > r->depth) {
		dev_warn(&ctx->pdev->dev, "HCC DR%d: implausible completion count %u (from=0x%08x fill=0x%08x pub=0x%08x enable=0x%08x) - NOT consumed; device state unknown, stop\n",
			 ch, n, from, fill, hr->pub,
			 readl(chregs + HI5622_ETE_CH_ENABLE));
		return 0;
	}
	if (!n)
		return 0;
	if (!consume)
		return n;

	dev_info(&ctx->pdev->dev, "HCC DR%d: fill(+0x3c)=0x%08x last=0x%08x -> %u completed node(s) (pub=0x%08x enable=0x%02x)\n",
		 ch, fill, from, n, hr->pub,
		 readl(chregs + HI5622_ETE_CH_ENABLE));

	pos = from;
	for (i = 0; i < n; i++) {
		u32 low = pos & HCC_IDX_LOW_MASK;
		u32 word0, word1, len;
		void *va;

		rmb();
		word0 = le32_to_cpu(nodes[low].devva);
		word1 = le32_to_cpu(nodes[low].word1);
		rmb();
		len = word1 >> HI5622_ETE_NODE_LEN_SHIFT;
		va = hcc_node_va(r, word0);
		if (!va) {
			dev_warn(&ctx->pdev->dev, "HCC DR%d[%u]: node word0 0x%08x is not one of this ring's buffers (word1=0x%08x len=%u) - skipped\n",
				 ch, low, word0, word1, len);
		} else if (len >= HI5622_HCC_HDR_LEN && len <= r->buf_len) {
			hcc_hcc_rx(ctx, ch, low, len, va);
		} else {
			dev_warn(&ctx->pdev->dev, "HCC DR%d[%u]: node len=%u outside 12..%u - no decode\n",
				 ch, low, len, r->buf_len);
		}
		hr->pub = hcc_idx_plus(hr->pub, r->depth);
		pos = hcc_idx_plus(pos, r->depth);
		/* recycle the slot for the next device deposit (word1=0) */
		nodes[low].word1 = 0;
	}
	wmb();
	hcc_ete_writel(ctx, r->chan_off, HI5622_ETE_CH_B_PRD, hr->pub);
	wmb();
	hr->fill_last = fill;
	h->dr_rx_nodes += n;
	dev_info(&ctx->pdev->dev, "HCC DR%d: consumed %u node(s); host free idx (+0x38) now 0x%08x; fill baseline 0x%08x\n",
		 ch, n, hr->pub, fill);
	return n;
}

/* ---------------------------------------------------------------------
 * Boot handshake watch + ready waits
 * ------------------------------------------------------------------- */
static void hcc_boot_watch(struct hi5622_pci *ctx)
{
	struct hi5622_hcc_service *h = ctx->hcc;
	struct device *dev = &ctx->pdev->dev;
	u32 page0, page4, pagec, state, mask, ack;

	if (!ctx->windows_programmed)
		return;

	ack = readl(ctx->bar0 + HI5622_HCC_BAR0_HANDSHAKE_PAGE + 0x8);
	if (!h->boot_seen) {
		if (ack != HI5622_HCC_HANDSHAKE_ACK)
			return;
		h->boot_seen = true;
		h->boot_time = jiffies;
		h->state = HCC_RS_WAIT_PLAT;
		page0 = readl(ctx->bar0 + HI5622_HCC_BAR0_HANDSHAKE_PAGE + 0x0);
		page4 = readl(ctx->bar0 + HI5622_HCC_BAR0_HANDSHAKE_PAGE + 0x4);
		pagec = readl(ctx->bar0 + HI5622_HCC_BAR0_HANDSHAKE_PAGE + 0xc);
		state = hcc_ram_read(ctx, HI5622_HCC_CPU_BOOT_STATE);
		mask = readl(ctx->bar0 + HI5622_HCC_BAR0_EXCP_MASK);
		h->boot_logged = true;
		dev_info(dev, "HCC boot: handshake page CA 0x1bf000 = 0x%08x / 0x%08x / 0x%08x / 0x%08x (+8==2 -> CPU1 S1 complete, entry B dispatched); state CA 0x118b60=0x%08x mask CA 0x40008=0x%08x (GP8 golden expects 0x86807fef/7/2/0xabcd, state 1, mask 0)\n",
			 page0, page4, ack, pagec, state, mask);
		dev_info(dev, "HCC ready: starting vendor waits - chip-group-4 id1 within 2.0 s (200 jiffies), id2 within 20.0 s (2000 jiffies); both are device->host over DR, no H2D reply is sent on this path\n");
		return;
	}

	if (h->state == HCC_RS_WAIT_PLAT && !h->g4_id1_seen &&
	    time_after(jiffies, h->boot_time +
		       msecs_to_jiffies(HI5622_HCC_PLAT_READY_MS))) {
		h->plat_timeout = true;
		h->state = HCC_RS_WAIT_DEV;
		dev_warn(dev, "HCC ready: TIMEOUT - group-4 id1 (device_plat_ready) did not arrive within 2.0 s. Vendor multi_chip_loading would return OAL_FAIL 0x8b2d here; continuing to observe the 20.0 s id2 window and the DR/fill state (read-only, no H2D reply)\n");
	}
	if (!h->g4_id2_seen &&
	    time_after(jiffies, h->boot_time +
		       msecs_to_jiffies(HI5622_HCC_PLAT_READY_MS +
					HI5622_HCC_DEV_READY_MS))) {
		if (!h->dev_timeout) {
			h->dev_timeout = true;
			dev_warn(dev, "HCC ready: TIMEOUT - group-4 id2 (DEVICE READY) did not arrive within 20.0 s; device is not in its ready handshake. Record DR fills, ETE status/send, g+0xB4/+0xB8, mask and out[1] history, then stop per the bench stop rules\n");
		}
	}
}

/* ---------------------------------------------------------------------
 * Corrected CECE policy (the load-bearing new rule of this lane)
 *
 * The 16-bit CA 0x4000010C carries TWO handshakes:
 *   ROM boot stage (CPU0 -> CPU1 S1): 0xCEED must survive until page+8==2;
 *   image service/exception handler CPU 0xC6E14: zeroes the word, then spins
 *   for 32-bit 0x0000CECE.
 * Therefore: NEVER write CECE before the handshake page +8 reads 2 (GP8),
 * and afterwards write it only when a handler is demonstrably spinning:
 * out[1] bit2 (0x4 startup announce), a new/nonzero exception-seen mask, or
 * a D2H handler posting (any id other than the pure deposit notify bit6).
 * ------------------------------------------------------------------- */
static void hcc_cece_policy(struct hi5622_pci *ctx, u32 bitmap)
{
	struct hi5622_hcc_service *h = ctx->hcc;
	struct device *dev = &ctx->pdev->dev;
	u32 mask;
	bool boot, evidence = false, mask_now_nonzero;

	if (!ctx->windows_programmed)
		return;

	boot = hcc_boot_complete(ctx);
	mask = readl(ctx->bar0 + HI5622_HCC_BAR0_EXCP_MASK);
	mask_now_nonzero = mask != 0;

	if (!h->mask_valid) {
		h->mask_valid = true;
		h->last_mask = mask;
	} else if (mask != h->last_mask) {
		h->last_mask = mask;
		evidence = true;	/* new exception landed */
	}
	if (mask_now_nonzero && !h->mask_seen_nonzero)
		evidence = true;	/* first exception observed */
	h->mask_seen_nonzero |= mask_now_nonzero;

	if (bitmap & BIT(HI5622_HCC_D2H_BIT_EXCP_PANIC))
		evidence = true;	/* out[1]=0x4: the 0x86E14 announce */
	if (bitmap & ~(BIT(HI5622_HCC_D2H_BIT_ETE_NOTIFY) |
		       BIT(HI5622_HCC_D2H_BIT_EXCP_PANIC)))
		evidence = true;	/* a D2H handler posted (id != bit6/id6) */

	if (!boot) {
		if (evidence) {
			h->cece_blocked_boot++;
			dev_warn(dev, "HCC CECE: REFUSED - handshake page +8 != 2 (ROM boot stage owns 0xCEED; writing CECE here is the GP8 bug). evidence bitmap=0x%08x mask=0x%08x\n",
				 bitmap, mask);
		}
		return;
	}

	if (evidence) {
		h->cece_pending = true;
		h->cece_evidence_events++;
	}
	if (h->cece_pending && hcc_cece_mode != 1) {
		hcc_writel(ctx, HI5622_HCC_BAR0_GATE, HI5622_HCC_GATE_CECE);
		h->cece_pending = false;
		h->cece_writes++;
		dev_info(dev, "HCC CECE: 32-bit 0x0000CECE -> CA 0x4000010c (post-boot, event-driven; evidence bitmap=0x%08x mask=0x%08x); write #%llu\n",
			 bitmap, mask,
			 (unsigned long long)h->cece_writes);
	}
}

/* ---------------------------------------------------------------------
 * Image IRQ/event-layer watch (read-only discriminators)
 *   - EOI CA 0x40160110 nonzero => the image dispatcher 0xC2EFC has run;
 *   - current vector CA 0x4016010c nonzero => event mid-dispatch;
 *   - scheduler timer CA 0x40160600/08/0c and per-event enables show whether
 *     the scheduler (the only cpsie-i context besides the dispatcher) ran.
 * ------------------------------------------------------------------- */
static void hcc_irq_watch(struct hi5622_pci *ctx)
{
	struct hi5622_hcc_service *h = ctx->hcc;
	struct device *dev = &ctx->pdev->dev;
	u32 eoi, vec;

	if (!ctx->windows_programmed)
		return;

	eoi = hcc_io_read(ctx, HI5622_HCC_CA_EV_EOI);
	vec = hcc_io_read(ctx, HI5622_HCC_CA_EV_VECTOR);
	if (eoi != h->last_eoi) {
		dev_info(dev, "HCC IRQ: EOI CA 0x40160110 changed 0x%08x -> 0x%08x (image dispatcher activity), vector CA 0x4016010c=0x%08x\n",
			 h->last_eoi, eoi, vec);
		h->last_eoi = eoi;
		h->eoi_events++;
	}
	if (!h->eoi_seen && eoi != 0) {
		h->eoi_seen = true;
		dev_info(dev, "HCC IRQ: DELIVERY ALIVE - image IRQ dispatcher 0xC2EFC has run (EOI=0x%08x); the event layer is no longer the blocker\n",
			 eoi);
	}
	if (!h->vector_seen && vec != 0) {
		h->vector_seen = true;
		dev_info(dev, "HCC IRQ: current vector CA 0x4016010c=0x%08x (event id low10=%u) - an event is being dispatched\n",
			 vec, vec & 0x3ff);
	}
}

/* ---------------------------------------------------------------------
 * The vendor-exact D2H service pass
 *
 *   0. read out[1] (CA 0x40039014); a zero word performs no mailbox writes
 *   1. ack    out[3] (CA 0x40101438) = 1
 *   2. re-read out[1], write 0 (drain; device precondition)
 *   3. re-arm out[4] (CA 0x40101414) = 1
 *   4. dispatch saved bitmap lowest bit first, ids 0..10, id>10 stop
 *   5. id 3/6/7 drain DR0-3 (vendor channel mask 0x1f) and decode HCC
 *   6. boot watch + corrected CECE policy (never during the boot stage)
 * ------------------------------------------------------------------- */
static int hcc_service_pass(struct hi5622_pci *ctx, const char *why)
{
	struct hi5622_hcc_service *h = ctx->hcc;
	struct device *dev = &ctx->pdev->dev;
	u32 bitmap;
	bool need_dr = false;
	int serviced = 0;
	u32 id, ch;

	if (!ctx->windows_programmed) {
		dev_warn_ratelimited(dev, "HCC service: iATU windows not programmed (run 'prepare' first)\n");
		return 0;
	}

	mutex_lock(&h->lock);

	bitmap = readl(ctx->bar0 + HI5622_BAR0_D2H_BITMAP);
	if (bitmap) {
		hcc_writel(ctx, HI5622_HCC_BAR0_D2H_ACK, 1);
		bitmap |= readl(ctx->bar0 + HI5622_BAR0_D2H_BITMAP);
		hcc_writel(ctx, HI5622_BAR0_D2H_BITMAP, 0);
		hcc_writel(ctx, HI5622_HCC_BAR0_D2H_REARM, 1);

		h->events++;
		h->last_bitmap = bitmap;
		h->id_seen |= bitmap;
		h->hist[h->hist_pos % HCC_MAX_HIST] = bitmap;
		h->hist_pos++;
		for (id = 0; id <= HI5622_D2H_MAX_ID; id++)
			if (bitmap & BIT(id))
				h->bit_count[id]++;

		dev_info(dev, "HCC D2H[%s]: out[1]=0x%08x (vendor order: ack out[3]=1, re-read, drain out[1]=0, re-arm out[4]=1; NEVER writes out[5])\n",
			 why, bitmap);

		for (id = 0; id <= HI5622_D2H_MAX_ID; id++) {
			if (!(bitmap & BIT(id)))
				continue;
			serviced++;
			switch (id) {
			case HI5622_HCC_D2H_BIT_DEV_READY_STUB:
				dev_info(dev, "HCC D2H: id1 = pcie_dev_ready_msg_handle (vendor empty stub; no action)\n");
				break;
			case HI5622_HCC_D2H_BIT_EXCP_PANIC:
				dev_info(dev, "HCC D2H: id2 = startup/vector announce out[1]=0x4 (0x86E14 stores it; vendor table handler = exception_panic_msg_process). CECE evidence armed (post-boot only); never send H2D id5\n");
				break;
			case HI5622_HCC_D2H_BIT_ETE_DONE:
				dev_info(dev, "HCC D2H: id3 = ETE transfer done -> draining DR0-3 (vendor pcie_ete_d2h_isr_handle mask 0x1f)\n");
				need_dr = true;
				break;
			case HI5622_HCC_D2H_BIT_PM_SLEEP_DISALLOW:
				dev_info(dev, "HCC D2H: id4 = wlan_pm_slp_disallow (device refuses sleep; PM state only)\n");
				break;
			case HI5622_HCC_D2H_BIT_PM_WAKE_OK:
				dev_info(dev, "HCC D2H: id5 = wlan_pm_wkup_dev_ok (device wake-up is OK; PM state only)\n");
				break;
			case HI5622_HCC_D2H_BIT_ETE_NOTIFY:
				dev_info(dev, "HCC D2H: id6 = d2h_notify(6) from the deposit op -> draining DR0-3 (this is a device->host notification, NOT a request for H2D)\n");
				need_dr = true;
				break;
			case HI5622_HCC_D2H_BIT_ETE_SENDING:
				dev_info(dev, "HCC D2H: id7 = trigger-ete-sending variant (vendor handler wakes the chip TX/service thread) -> draining DR0-3\n");
				need_dr = true;
				break;
			default:
				dev_info(dev, "HCC D2H: id%u dropped (no vendor handler registered; ids 0,8,9,10 are dropped by pcie_msg_handle)\n",
					 id);
				break;
			}
		}

		if (need_dr) {
			int total = 0;

			for (ch = 0; ch < HI5622_HCC_DR_CH_COUNT; ch++)
				total += (int)hcc_walk_dr(ctx, ch, true);
			dev_info(dev, "HCC service: DR drain after id3/6/7: %d node(s) consumed across DR0-3\n",
				 total);
		}
	}

	hcc_boot_watch(ctx);

	/* Direct-read device group-A DR rings: the firmware stages HCC
	 * frames in PKTRAM before (and without) the ETE engine copy, so the
	 * host receive path reads them directly.  Read-only here. */
	pciet_d2h_poll(ctx, &h->rd, false);

	hcc_cece_policy(ctx, bitmap);

	mutex_unlock(&h->lock);
	return serviced;
}

/* Fill watchdog: a device deposit must publish even if the mailbox word was
 * lost; drain it and record the "deposit without notify" finding. */
static int hcc_watch_dr(struct hi5622_pci *ctx)
{
	struct hi5622_hcc_service *h = ctx->hcc;
	struct device *dev = &ctx->pdev->dev;
	int ch, found = 0;

	if (!ctx->windows_programmed || !h->armed)
		return 0;
	for (ch = 0; ch < HI5622_HCC_DR_CH_COUNT; ch++) {
		struct hi5622_ete_ring *r = &h->dr[ch].ete;
		void __iomem *regs = ctx->bar0 + HI5622_BAR0_ETE_BASE +
				     r->chan_off;
		u32 fill = readl(regs + HI5622_ETE_CH_B_CNS);

		if (fill == h->dr[ch].fill_last)
			continue;
		dev_info(dev, "HCC DR%d: fill(+0x3c) advanced 0x%08x -> 0x%08x WITHOUT a mailbox word; draining and returning the buffers (deposit-without-notify finding)\n",
			 ch, h->dr[ch].fill_last, fill);
		mutex_lock(&h->lock);
		hcc_walk_dr(ctx, ch, true);
		mutex_unlock(&h->lock);
		found++;
	}
	return found;
}

/* ---------------------------------------------------------------------
 * Service loop thread (kthread; start/stop via the hcc_loop attr)
 * ------------------------------------------------------------------- */
static int hcc_loop_thread(void *data)
{
	struct hi5622_hcc_service *h = data;
	struct hi5622_pci *ctx = h->ctx;
	struct device *dev = &ctx->pdev->dev;

	dev_info(dev, "HCC loop: service thread started (cadence %u ms): vendor D2H order, boot-guarded CECE, DR drain, ready waits 2.0/20.0 s; CECE mode %u\n",
		 hcc_poll_ms, hcc_cece_mode);
	while (!kthread_should_stop()) {
		hcc_service_pass(ctx, "loop");
		hcc_watch_dr(ctx);
		hcc_irq_watch(ctx);
		if (msleep_interruptible(hcc_poll_ms ? hcc_poll_ms : 1))
			break;
	}
	dev_info(dev, "HCC loop: service thread stopped: d2h_events=%llu bits_seen=0x%04x cece_writes=%llu cece_blocked_boot=%llu ready=%s g4_id1=%d g4_id2=%d plat_timeout=%d dev_timeout=%d\n",
		 (unsigned long long)h->events, h->id_seen,
		 (unsigned long long)h->cece_writes,
		 (unsigned long long)h->cece_blocked_boot,
		 hcc_rs_name(h->state), h->g4_id1_seen, h->g4_id2_seen,
		 h->plat_timeout, h->dev_timeout);
	return 0;
}


/* ---------------------------------------------------------------------
 * H2D SR announce: the vendor carrier for a VALID HCC frame
 *
 *   frame (12-byte header + payload; tag 0x5A5A at +0xA, total > 0 at +4)
 *     -> SR0 node {buffer dev VA, (total<<16)|0x6D2B}, packed producer
 *     -> mailbox id 3: out[0]=BIT(3), then out[2] |= 1 (vendor order)
 *
 * The firmware SR-receive drain validates both header fields and drops a
 * zero header; it dispatches H2D only through the group-0/group-2 tables,
 * and mailbox id 5 is its BUG() trap.  This path therefore allows only a
 * well-formed group-0 or group-2 frame, and the default is the vendor
 * payload-free stub frame 00 01 00 00 0c 00 00 00 00 00 5a 5a.
 * ------------------------------------------------------------------- */
static int hcc_parse_uint(const char **pp, unsigned int *out, unsigned int max)
{
	const char *p = *pp;
	unsigned int v = 0;
	unsigned int base = 10;
	bool any = false;

	if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
		base = 16;
		p += 2;
	}
	while (*p) {
		int d;

		if (base == 16) {
			d = hex_to_bin(*p);
			if (d < 0)
				break;
		} else {
			if (*p < '0' || *p > '9')
				break;
			d = *p - '0';
		}
		if (v > (max - (unsigned int)d) / base)
			return -ERANGE;
		v = v * base + (unsigned int)d;
		p++;
		any = true;
	}
	if (!any)
		return -EINVAL;
	*out = v;
	*pp = p;
	return 0;
}

static int hcc_parse_hex_payload(const char **pp, u8 *dst, size_t *plen)
{
	const char *p = *pp;
	size_t n = 0;

	if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
		p += 2;
	while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != ',') {
		int hi = hex_to_bin(p[0]);
		int lo;

		if (hi < 0)
			return -EINVAL;
		if (!p[1])
			return -EINVAL;
		lo = hex_to_bin(p[1]);
		if (lo < 0)
			return -EINVAL;
		if (n >= HI5622_HCC_FRAME_MAX_PAYLOAD)
			return -ERANGE;
		dst[n++] = (u8)((hi << 4) | lo);
		p += 2;
	}
	if (!n)
		return -EINVAL;
	*plen = n;
	*pp = p;
	return 0;
}

/* Build the 12-byte header + payload.  Returns the frame total length or a
 * negative errno; the header is always rebuilt so tag/length are correct. */
static int hcc_build_sr_frame(const char *buf, u8 *frame, u32 *total,
			      unsigned int *group_out, unsigned int *id_out,
			      unsigned int *dst_out)
{
	unsigned int g = HI5622_HCC_SR_GROUP_SAFE;
	unsigned int i = HI5622_HCC_SR_ID_SAFE;
	unsigned int d = 0;
	size_t plen = 0;
	const char *p = buf;
	bool seen = false;
	int ret;

	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;
	if (!*p)
		return -EINVAL;
	if (!(*p == '1' && !p[1])) {
		while (*p) {
			while (*p == ' ' || *p == '\t' || *p == '\n' ||
			       *p == '\r' || *p == ',')
				p++;
			if (!*p)
				break;
			if (!strncmp(p, "group=", 6)) {
				p += 6;
				ret = hcc_parse_uint(&p, &g, 0xffff);
			} else if (!strncmp(p, "id=", 3)) {
				p += 3;
				ret = hcc_parse_uint(&p, &i, 0xffff);
			} else if (!strncmp(p, "dst=", 4)) {
				p += 4;
				ret = hcc_parse_uint(&p, &d, 0xf);
			} else if (!strncmp(p, "payload=", 8)) {
				p += 8;
				ret = hcc_parse_hex_payload(&p, frame + HI5622_HCC_HDR_LEN,
							    &plen);
			} else {
				return -EINVAL;
			}
			if (ret)
				return ret;
			seen = true;
		}
		if (!seen)
			return -EINVAL;
	}

	/* Contract refusals encoded from the catalogue (never send these). */
	if (g > 0xf || d > 0xf)
		return -ERANGE;
	if (i == HI5622_HCC_MSG_H2D_BUGON)
		return -EACCES;		/* firmware BUG() trap [V] */
	if (g == HI5622_HCC_GROUP_PLAT)
		return -EACCES;		/* no group-4 H2D receive table [V] */
	if (g != 0 && g != 2)
		return -EINVAL;		/* firmware H2D tables are group 0 and 2 only [V] */
	if (plen > HI5622_HCC_FRAME_MAX_PAYLOAD)
		return -ERANGE;

	*total = (u32)plen + HI5622_HCC_HDR_LEN;
	memset(frame, 0, HI5622_HCC_HDR_LEN);
	frame[0] = (u8)(g & 0xf);
	frame[1] = (u8)(((d & 0xf) << 4) | 0x1);	/* low nibble = allocated [V] */
	frame[4] = (u8)(*total & 0xff);
	frame[5] = (u8)((*total >> 8) & 0xff);
	frame[6] = (u8)(i & 0xff);
	frame[7] = (u8)((i >> 8) & 0xff);
	frame[9] = (u8)(d & 0xf);
	frame[10] = (u8)(HI5622_HCC_TAG & 0xff);
	frame[11] = (u8)(HI5622_HCC_TAG >> 8);
	*group_out = g;
	*id_out = i;
	*dst_out = d;
	return 0;
}

static ssize_t hcc_sr_announce_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct hi5622_pci *ctx = hcc_ctx(dev);
	struct hi5622_hcc_service *h;
	struct hi5622_ete_ring *r;
	struct hi5622_ete_node *nodes;
	unsigned int g = 0, id = 0, dst = 0, polls;
	u32 total = 0, before, wb, o2, o2a, out0_after, out5, cns, idx, newprod;
	u8 *frame;
	int ret;

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	h = ctx->hcc;
	if (!ctx->windows_programmed) {
		h->sr_announce_refused++;
		return -EIO;
	}
	if (!h->armed) {
		h->sr_announce_refused++;
		dev_err(dev, "HCC H2D: arm first (hcc_arm) - the SR side must be programmed before a frame is posted\n");
		return -EBUSY;
	}
	if (!ctx->cpu_released) {
		h->sr_announce_refused++;
		dev_err(dev, "HCC H2D: refuse before the CPU release\n");
		return -EBUSY;
	}
	if (!hcc_boot_complete(ctx)) {
		h->sr_announce_refused++;
		dev_warn(dev, "HCC H2D: REFUSED - handshake page +8 != 2; never post H2D during the ROM boot stage\n");
		return -EBUSY;
	}

	frame = kmalloc(HI5622_HCC_FRAME_MAX_TOTAL, GFP_KERNEL);
	if (!frame)
		return -ENOMEM;

	mutex_lock(&h->lock);
	ret = hcc_build_sr_frame(buf, frame, &total, &g, &id, &dst);
	if (ret) {
		h->sr_announce_refused++;
		dev_warn(dev, "HCC H2D: frame request rejected (%d); only group=0|2, id!=5, dst<=0xf, payload<=0x674 hex bytes\n",
			 ret);
		goto out_unlock;
	}

	/* The device must have consumed the previous pending word; the vendor
	 * pcie_msg_send path is coalesced and our probe is one frame. */
	before = readl(ctx->bar0 + HI5622_BAR0_H2D_PENDING);
	if (before) {
		u32 spins;

		for (spins = 0; spins < 0xffff; spins++) {
			if (!readl(ctx->bar0 + HI5622_BAR0_H2D_PENDING))
				break;
			cpu_relax();
		}
		before = readl(ctx->bar0 + HI5622_BAR0_H2D_PENDING);
	}
	if (before) {
		h->sr_announce_refused++;
		dev_warn(dev, "HCC H2D: out[0] pending=0x%08x never consumed (H2D consumer dead) - not posting; record and stop the H2D lane (no retry in-session)\n",
			 before);
		ret = -EBUSY;
		goto out_unlock;
	}

	/* Vendor carrier: commit the SR node, then announce mailbox id 3. */
	r = &h->sr[0].ete;
	nodes = r->node_arr;
	idx = h->sr_producer & HCC_IDX_LOW_MASK;
	memcpy(r->bufs[idx], frame, total);
	nodes[idx].devva = cpu_to_le32((u32)r->buf_dma[idx]);
	nodes[idx].word1 = cpu_to_le32((total << HI5622_ETE_NODE_LEN_SHIFT) |
				       HI5622_ETE_NODE_FLAGS);
	wmb();
	newprod = hcc_idx_plus(h->sr_producer, r->depth);
	hcc_ete_writel(ctx, r->chan_off, HI5622_ETE_CH_A_PRD, newprod);
	h->sr_producer = newprod;
	wmb();

	hcc_writel(ctx, HI5622_BAR0_H2D_PENDING,
		   BIT(HI5622_HCC_MSG_H2D_ETE_ANNOUNCE));
	wb = readl(ctx->bar0 + HI5622_BAR0_H2D_PENDING);
	o2 = readl(ctx->bar0 + HI5622_BAR0_H2D_DOORBELL);
	hcc_writel(ctx, HI5622_BAR0_H2D_DOORBELL, o2 | 1);
	o2a = readl(ctx->bar0 + HI5622_BAR0_H2D_DOORBELL);

	for (polls = 0; polls < 1000; polls++) {
		if (!(readl(ctx->bar0 + HI5622_BAR0_H2D_PENDING) &
		      BIT(HI5622_HCC_MSG_H2D_ETE_ANNOUNCE)))
			break;
		udelay(100);	/* <= 100 ms bounded, vendor-style */
	}
	out0_after = readl(ctx->bar0 + HI5622_BAR0_H2D_PENDING);
	out5 = readl(ctx->bar0 + HI5622_HCC_BAR0_NEVER_WRITE);	/* READ-ONLY */
	cns = readl(ctx->bar0 + HI5622_BAR0_ETE_BASE + r->chan_off +
		    HI5622_ETE_CH_A_CNS);

	h->last_sr_total = total;
	h->last_sr_group = g;
	h->last_sr_id = id;
	h->last_sr_out0_before = before;
	h->last_sr_out0_write = wb;
	h->last_sr_out0_after = out0_after;
	h->last_sr_out2_before = o2;
	h->last_sr_out2_after = o2a;
	h->last_sr_out5_read = out5;
	h->last_sr_cns = cns;
	h->last_sr_polls = polls;

	dev_info(dev, "HCC H2D: posted VALID SR frame (group=%u id=%u dst=%u total=%u tag=0x%04x, node=(devva|0x%04x)) on SR%u; announce mailbox id%u: out[0] 0x%08x->0x%08x out[2] 0x%08x->0x%08x; polls=%u out[0]_after=0x%08x out[5](read)=0x%08x SR cns=%u\n",
		 g, id, dst, total, HI5622_HCC_TAG, HI5622_ETE_NODE_FLAGS,
		 0, HI5622_HCC_MSG_H2D_ETE_ANNOUNCE, before, wb, o2, o2a,
		 polls, out0_after, out5, cns);

	if (out0_after & BIT(HI5622_HCC_MSG_H2D_ETE_ANNOUNCE)) {
		h->sr_announce_timeout++;
		dev_warn(dev, "HCC H2D: announce NOT consumed (out[0] bit3 stuck after %u ms) -> the firmware H2D dispatcher is dead; the H2D lane is closed, no frame/payload can be the missing boot input (no retry in-session)\n",
			 polls / 10);
		ret = -ETIMEDOUT;
		goto out_unlock;
	}
	h->sr_announce_ok++;
	ret = (int)count;

out_unlock:
	mutex_unlock(&h->lock);
	kfree(frame);
	return ret;
}
static DEVICE_ATTR_WO(hcc_sr_announce);

/* Manual single CECE write (bounded; only after the boot handshake). */
static ssize_t hcc_cece_kick_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct hi5622_pci *ctx = hcc_ctx(dev);
	struct hi5622_hcc_service *h;

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	h = ctx->hcc;
	if (!sysfs_streq(buf, "1"))
		return -EINVAL;
	if (!ctx->windows_programmed)
		return -EIO;
	if (hcc_cece_mode == 1) {
		dev_warn(dev, "HCC CECE: manual kick refused (hcc_cece_mode=1, never)\n");
		return -EPERM;
	}
	if (!hcc_boot_complete(ctx)) {
		h->cece_blocked_boot++;
		dev_warn(dev, "HCC CECE: manual kick REFUSED - handshake page +8 != 2 (ROM boot stage); writing CECE here is the GP8 bug\n");
		return -EBUSY;
	}
	if (h->cece_kicks >= 3) {
		dev_warn(dev, "HCC CECE: manual kick limit reached (3 per module load)\n");
		return -EBUSY;
	}
	hcc_writel(ctx, HI5622_HCC_BAR0_GATE, HI5622_HCC_GATE_CECE);
	h->cece_kicks++;
	h->cece_writes++;
	dev_info(dev, "HCC CECE: manual kick %u/3 - 32-bit 0x0000CECE -> CA 0x4000010c (post-boot only)\n",
		 h->cece_kicks);
	return count;
}
static DEVICE_ATTR_WO(hcc_cece_kick);

/* Re-publish the host-owned DR free index (+0x38) with its current value.
 * This is the S3 decision-matrix experiment from wifi-ready-emit sec 7.2: the
 * vendor's sending trigger re-publishes +0x38 under lock, and a single
 * re-publish is the only benign host write class that could nudge a stalled
 * source->host copy.  It changes no state if the engine is idle; one per
 * session is the bench rule. */
static ssize_t hcc_dr_republish_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct hi5622_pci *ctx = hcc_ctx(dev);
	struct hi5622_hcc_service *h;
	int ch;

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	h = ctx->hcc;
	if (!sysfs_streq(buf, "1"))
		return -EINVAL;
	if (!ctx->windows_programmed || !h->armed)
		return -EBUSY;
	if (!hcc_boot_complete(ctx)) {
		dev_warn(dev, "HCC DR re-publish: refused before the boot handshake (one idea per session; no speculative writes during boot)\n");
		return -EBUSY;
	}
	for (ch = 0; ch < HI5622_HCC_DR_CH_COUNT; ch++) {
		struct hi5622_ete_ring *r = &h->dr[ch].ete;
		u32 cur = readl(ctx->bar0 + HI5622_BAR0_ETE_BASE + r->chan_off +
				HI5622_ETE_CH_B_PRD);

		hcc_ete_writel(ctx, r->chan_off, HI5622_ETE_CH_B_PRD, cur);
		wmb();
		dev_info(dev, "HCC DR%d: re-published host free idx (+0x38) = 0x%08x (S3 experiment; fill read-only %u)\n",
			 ch, cur,
			 readl(ctx->bar0 + HI5622_BAR0_ETE_BASE + r->chan_off +
			       HI5622_ETE_CH_B_CNS));
	}
	h->dr_republish_count++;
	return count;
}
static DEVICE_ATTR_WO(hcc_dr_republish);

/* ---------------------------------------------------------------------
 * Read-only attributes
 * ------------------------------------------------------------------- */
static void hcc_hex_bytes(char *dst, size_t dstsz, const u8 *src, u32 len)
{
	static const char hexchars[] = "0123456789abcdef";
	size_t pos = 0;
	u32 i;

	if (!dstsz)
		return;
	dst[0] = '\0';
	for (i = 0; i < len && pos + 4 < dstsz; i++) {
		dst[pos++] = hexchars[src[i] >> 4];
		dst[pos++] = hexchars[src[i] & 0xf];
		dst[pos++] = ' ';
		dst[pos] = '\0';
	}
}

static ssize_t hcc_status_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct hi5622_pci *ctx = hcc_ctx(dev);
	struct hi5622_hcc_service *h;
	struct hcc_out o;
	int ch;

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	h = ctx->hcc;
	hcc_out_init(&o, buf, PAGE_SIZE);

	hcc_printf(&o, "HCC status: state=%s armed=%d prepost=%u loop=%s poll_ms=%u cece_mode=%u\n",
		   hcc_rs_name(h->state), h->armed, h->prepost,
		   h->loop ? "running" : "stopped", hcc_poll_ms, hcc_cece_mode);
	hcc_printf(&o, "boot: fw_loaded=%d cpu_released=%d windows=%d handshake_page CA 0x1bf000=",
		   ctx->fw_loaded, ctx->cpu_released, ctx->windows_programmed);
	if (ctx->windows_programmed) {
		hcc_printf(&o, "0x%08x/0x%08x/0x%08x/0x%08x (+8==2:%d) state CA 0x118b60=0x%08x mask CA 0x40008=0x%08x boot_slot CA 0x17d42c=0x%08x\n",
			   readl(ctx->bar0 + HI5622_HCC_BAR0_HANDSHAKE_PAGE + 0x0),
			   readl(ctx->bar0 + HI5622_HCC_BAR0_HANDSHAKE_PAGE + 0x4),
			   readl(ctx->bar0 + HI5622_HCC_BAR0_HANDSHAKE_PAGE + 0x8),
			   readl(ctx->bar0 + HI5622_HCC_BAR0_HANDSHAKE_PAGE + 0xc),
			   hcc_boot_complete(ctx),
			   hcc_ram_read(ctx, HI5622_HCC_CPU_BOOT_STATE),
			   readl(ctx->bar0 + HI5622_HCC_BAR0_EXCP_MASK),
			   hcc_ram_read(ctx, HI5622_HCC_CPU_BOOT_SLOT));
		hcc_printf(&o, "mailbox: out0=0x%08x out1=0x%08x out2=0x%08x out3(ack,read)=0x%08x out4(rearm,read)=0x%08x out5(READ-ONLY)=0x%08x d2h_doorbell=0x%08x\n",
			   readl(ctx->bar0 + HI5622_BAR0_H2D_PENDING),
			   readl(ctx->bar0 + HI5622_BAR0_D2H_BITMAP),
			   readl(ctx->bar0 + HI5622_BAR0_H2D_DOORBELL),
			   readl(ctx->bar0 + HI5622_HCC_BAR0_D2H_ACK),
			   readl(ctx->bar0 + HI5622_HCC_BAR0_D2H_REARM),
			   readl(ctx->bar0 + HI5622_HCC_BAR0_NEVER_WRITE),
			   readl(ctx->bar0 + HI5622_HCC_BAR0_D2H_DOORBELL));
		hcc_printf(&o, "g: armed(g+0xb4 CA 0x10c1a8)=0x%08x pending(g+0xb8 CA 0x10c1ac)=0x%08x (both 0x40 = deferred bit6 D2H word: flush 0xC6108 not retired)\n",
			   hcc_ram_read(ctx, HI5622_HCC_CPU_G_ARMED),
			   hcc_ram_read(ctx, HI5622_HCC_CPU_G_PENDING));
		hcc_printf(&o, "ete: intr CA 0x40039508=0x%08x status CA 0x4003a874=0x%08x send words DR0-3=",
			   readl(ctx->bar0 + HI5622_HCC_BAR0_ETE_INTR),
			   readl(ctx->bar0 + HI5622_HCC_BAR0_ETE_STATUS));
		for (ch = 0; ch < HI5622_HCC_DR_CH_COUNT; ch++)
			hcc_printf(&o, "0x%08x ",
				   readl(ctx->bar0 + HI5622_BAR0_ETE_BASE +
					 HI5622_HCC_ETE_SEND_DR0 + 4 * ch));
		hcc_printf(&o, "\n");
		for (ch = 0; ch < HI5622_HCC_DR_CH_COUNT; ch++) {
			struct hi5622_ete_ring *r = &h->dr[ch].ete;

			hcc_printf(&o, "dr%d: pub(+0x38)=0x%08x fill(+0x3c)=0x%08x enable(+0x00)=0x%08x groupA base/prod/cons=",
				   ch,
				   readl(ctx->bar0 + HI5622_BAR0_ETE_BASE + r->chan_off + HI5622_ETE_CH_B_PRD),
				   readl(ctx->bar0 + HI5622_BAR0_ETE_BASE + r->chan_off + HI5622_ETE_CH_B_CNS),
				   readl(ctx->bar0 + HI5622_BAR0_ETE_BASE + r->chan_off + HI5622_ETE_CH_ENABLE));
			hcc_printf(&o, "0x%08x/0x%08x/0x%08x\n",
				   readl(ctx->bar0 + HI5622_BAR0_ETE_BASE + r->chan_off + HI5622_ETE_CH_A_BASE),
				   readl(ctx->bar0 + HI5622_BAR0_ETE_BASE + r->chan_off + HI5622_ETE_CH_A_PRD),
				   readl(ctx->bar0 + HI5622_BAR0_ETE_BASE + r->chan_off + HI5622_ETE_CH_A_CNS));
		}
	} else {
		hcc_printf(&o, "(not mapped: run 'prepare' first)\n");
	}
	hcc_printf(&o, "rx: hcc_nodes=%u dr_nodes=%u last hdr group=%u id=%u total=0x%x len=%u tag=0x%04x\n",
		   h->hcc_rx_nodes, h->dr_rx_nodes, h->last_hdr_group,
		   h->last_hdr_id, h->last_hdr_total, h->last_hdr_len,
		   h->last_hdr_tag);
	hcc_printf(&o, "cece policy (corrected): writes=%llu blocked_in_boot=%llu evidence_events=%llu kicks=%u mode=%u (CECE only when page+8==2 AND a handler is spinning)\n",
		   (unsigned long long)h->cece_writes,
		   (unsigned long long)h->cece_blocked_boot,
		   (unsigned long long)h->cece_evidence_events, h->cece_kicks,
		   hcc_cece_mode);
	return o.pos;
}
static DEVICE_ATTR_RO(hcc_status);

static ssize_t hcc_ready_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct hi5622_pci *ctx = hcc_ctx(dev);
	struct hi5622_hcc_service *h;
	struct hcc_out o;
	char hexbuf[3 * 32 + 1];

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	h = ctx->hcc;
	hcc_out_init(&o, buf, PAGE_SIZE);
	hcc_printf(&o, "HCC ready exchange: state=%s boot_seen=%d g4_id1_seen=%d g4_id2_seen=%d\n",
		   hcc_rs_name(h->state), h->boot_seen, h->g4_id1_seen,
		   h->g4_id2_seen);
	if (h->boot_seen)
		hcc_printf(&o, "elapsed_since_boot_ms=%u plat_timeout=%d dev_timeout=%d (vendor bounds: id1 <= 2.0 s, id2 <= 20.0 s, no retries, no host reply)\n",
			   jiffies_to_msecs(jiffies - h->boot_time),
			   h->plat_timeout, h->dev_timeout);
	else
		hcc_printf(&o, "boot handshake page +8 != 2 yet: release 0x5a5a only, NEVER write 0xCECE during the ROM boot stage\n");
	if (h->g4_id1_seen)
		hcc_printf(&o, "g4_id1: count=%u len=%u total=0x%x -> device_plat_ready ('Device plat ready!'); vendor wait #1 released\n",
			   h->g4_id1_count, h->g4_id1_len, h->g4_id1_total);
	if (h->g4_id2_seen) {
		hcc_hex_bytes(hexbuf, sizeof(hexbuf), h->g4_id2_payload,
			      min_t(u16, h->g4_id2_plen,
				    sizeof(h->g4_id2_payload)));
		hcc_printf(&o, "g4_id2: count=%u len=%u total=0x%x payload=%u B (cap 0x63: %s) = %s -> host_ready ('DEVICE READY'); vendor wait #2 released; record g_dmac_to_hmac_read_msg[0]\n",
			   h->g4_id2_count, h->g4_id2_len, h->g4_id2_total,
			   h->g4_id2_plen,
			   h->g4_id2_plen <= HI5622_HCC_DR_PAYLOAD_CAP ? "OK" : "VIOLATION",
			   hexbuf);
	}
	{
		char direct[256];

		if (pciet_d2h_ready_line(ctx, &h->rd, direct,
					  sizeof(direct)))
			hcc_printf(&o, "%s", direct);
	}
	if (h->state == HCC_RS_READY)
		hcc_printf(&o, "VERDICT: READY - the vendor host would resume multi_chip_loading and open the H2D lane. The vendor's first H2D send is the post-ready group-0 id1 TXQ config sync with a dynamic payload: do NOT synthesize it; this service's H2D path is a bounded liveness probe only.\n");
	else if (h->boot_seen && h->dev_timeout)
		hcc_printf(&o, "VERDICT: no ready handshake (2.0 s/20.0 s windows expired). Cross-check hcc_irq_probe: if EOI=0 and the scheduler timer is zero, the image IRQ/event layer never started - that is firmware/ROM-internal and NOT reachable by any H2D message (hcc-link negative). Record and stop; the next lane is firmware-side (ROM IRQ entry / entry-A post-handshake path), not more host writes.\n");
	else if (h->boot_seen)
		hcc_printf(&o, "VERDICT: waiting for the device->host ready pair; keep the service loop running and do not send H2D.\n");
	else
		hcc_printf(&o, "VERDICT: boot handshake not observed; verify the corrected boot (release-only, no CECE) before judging the ready path.\n");
	return o.pos;
}
static DEVICE_ATTR_RO(hcc_ready);

static ssize_t hcc_irq_probe_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct hi5622_pci *ctx = hcc_ctx(dev);
	struct hi5622_hcc_service *h;
	struct hcc_out o;
	u32 ctrl, maxpri, cfg, vec, eoi, en0, en1, en2, pend0, pend1;
	u32 t_reload, t_ctrl, t_start;

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	h = ctx->hcc;
	hcc_out_init(&o, buf, PAGE_SIZE);
	if (!ctx->windows_programmed) {
		hcc_printf(&o, "HCC IRQ probe: iATU windows not programmed (run 'prepare' first)\n");
		return o.pos;
	}
	ctrl = hcc_io_read(ctx, HI5622_HCC_CA_EV_CTRL);
	maxpri = hcc_io_read(ctx, HI5622_HCC_CA_EV_MAXPRI);
	cfg = hcc_io_read(ctx, HI5622_HCC_CA_EV_CFG);
	vec = hcc_io_read(ctx, HI5622_HCC_CA_EV_VECTOR);
	eoi = hcc_io_read(ctx, HI5622_HCC_CA_EV_EOI);
	en0 = hcc_io_read(ctx, HI5622_HCC_CA_EV_EN0);
	en1 = hcc_io_read(ctx, HI5622_HCC_CA_EV_EN1);
	en2 = hcc_io_read(ctx, HI5622_HCC_CA_EV_EN2);
	pend0 = hcc_io_read(ctx, HI5622_HCC_CA_EV_PEND0);
	pend1 = hcc_io_read(ctx, HI5622_HCC_CA_EV_PEND1);
	t_reload = hcc_io_read(ctx, HI5622_HCC_CA_TIMER_RELOAD);
	t_ctrl = hcc_io_read(ctx, HI5622_HCC_CA_TIMER_CTRL);
	t_start = hcc_io_read(ctx, HI5622_HCC_CA_TIMER_START);

	hcc_printf(&o, "HCC IRQ/event-layer probe (READ-ONLY; the image is the only writer of this block)\n");
	hcc_printf(&o, "controller: enable CA 0x40160100=0x%08x (expect 1) maxpri=0x%08x (expect 0xff) cfg=0x%08x (expect 3)\n",
		   ctrl, maxpri, cfg);
	hcc_printf(&o, "dispatch: current vector CA 0x4016010c=0x%08x (0 = no IRQ being taken) EOI CA 0x40160110=0x%08x (nonzero history => dispatcher 0xC2EFC ran)\n",
		   vec, eoi);
	hcc_printf(&o, "enables: CA 0x40161100=0x%08x 0x40161104=0x%08x 0x40161108=0x%08x (expect 0x2000 / 0xc0006000 / 0x5000 when the scheduler armed the timer + 0x2d/0x2e/0x3e/0x3f/0x4c/0x4e)\n",
		   en0, en1, en2);
	hcc_printf(&o, "latched raises: CA 0x40161188=0x%08x CA 0x40161288=0x%08x (0x5000 = events 0x4C+0x4E latched but never delivered)\n",
		   pend0, pend1);
	hcc_printf(&o, "scheduler timer: CA 0x40160600=0x%08x 0x40160608=0x%08x 0x4016060c=0x%08x (0x61a800/7/1 when the scheduler 0xC704C ran and IRQ was unmasked in idle)\n",
		   t_reload, t_ctrl, t_start);
	hcc_printf(&o, "image watch: eoi_events=%llu eoi_seen=%d vector_seen=%d last_eoi=0x%08x last_vector=0x%08x\n",
		   (unsigned long long)h->eoi_events, h->eoi_seen,
		   h->vector_seen, h->last_eoi, h->last_vector);
	if (!h->boot_seen)
		hcc_printf(&o, "VERDICT: boot handshake page +8 != 2 - the corrected boot did not complete; nothing here is meaningful yet (do not write CECE).\n");
	else if (eoi != 0)
		hcc_printf(&o, "VERDICT: DELIVERY ALIVE - the image dispatcher has run (EOI nonzero). The event layer is up; service the mailbox/DR and expect the g4 ready pair. No extra host action is needed for the IRQ layer.\n");
	else if (t_start == 0 && t_reload == 0)
		hcc_printf(&o, "VERDICT: scheduler fingerprint ABSENT - the image scheduler 0xC704C never armed the timer, so no context executed cpsie i (its only unmask sites are the dispatcher and scheduler idle). This is image/ROM-internal: entry A's post-handshake task-queue/scheduler path or the FIQ-armed reset slot. No mailbox/ETE/HCC message unblocks it (vendor sends nothing pre-ready; no H2D handler reaches the dispatcher). Record and stop the host side -> next lane: firmware/ROM IRQ-entry decode or vendor golden trace.\n");
	else
		hcc_printf(&o, "VERDICT: scheduler timer ARMED but EOI=0 and raises latched (0x%08x/0x%08x): the CPU/ROM IRQ delivery below the image dispatcher is dead (masked IRQ or ROM vector contract). Firmware/ROM-internal - not host-actionable; record and stop the host side.\n",
			   pend0, pend1);
	return o.pos;
}
static DEVICE_ATTR_RO(hcc_irq_probe);

static ssize_t hcc_events_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct hi5622_pci *ctx = hcc_ctx(dev);
	struct hi5622_hcc_service *h;
	struct hcc_out o;
	u32 i, start;

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	h = ctx->hcc;
	hcc_out_init(&o, buf, PAGE_SIZE);
	hcc_printf(&o, "HCC events: d2h_events=%llu last_out[1]=0x%08x bits_seen=0x%04x\n",
		   (unsigned long long)h->events, h->last_bitmap, h->id_seen);
	hcc_printf(&o, "per-id counts:");
	for (i = 0; i <= HI5622_D2H_MAX_ID; i++)
		hcc_printf(&o, " id%u=%u", i, h->bit_count[i]);
	hcc_printf(&o, "\ncallouts: bit2(startup 0x4)=%u bit3(ETE done)=%u bit6(deposit notify)=%u bit7(trigger)=%u\n",
		   h->bit_count[2], h->bit_count[3], h->bit_count[6],
		   h->bit_count[7]);
	start = h->hist_pos > HCC_MAX_HIST ? h->hist_pos - HCC_MAX_HIST : 0;
	hcc_printf(&o, "history[%u]:", h->hist_pos - start);
	for (i = 0; i < HCC_MAX_HIST && start + i < h->hist_pos; i++)
		hcc_printf(&o, " 0x%08x",
			   h->hist[(start + i) % HCC_MAX_HIST]);
	hcc_printf(&o, "\ncece: writes=%llu blocked_in_boot=%llu evidence_events=%llu kicks=%u pending=%d mode=%u\n",
		   (unsigned long long)h->cece_writes,
		   (unsigned long long)h->cece_blocked_boot,
		   (unsigned long long)h->cece_evidence_events, h->cece_kicks,
		   h->cece_pending, hcc_cece_mode);
	hcc_printf(&o, "h2d: sr_announce ok=%llu timeout=%llu refused=%llu last{group=%u id=%u total=%u out0_before=0x%08x out0_write=0x%08x out0_after=0x%08x out2_before=0x%08x out2_after=0x%08x out5_read=0x%08x sr_cns=%u polls=%u}\n",
		   (unsigned long long)h->sr_announce_ok,
		   (unsigned long long)h->sr_announce_timeout,
		   (unsigned long long)h->sr_announce_refused,
		   h->last_sr_group, h->last_sr_id, h->last_sr_total,
		   h->last_sr_out0_before, h->last_sr_out0_write,
		   h->last_sr_out0_after, h->last_sr_out2_before,
		   h->last_sr_out2_after, h->last_sr_out5_read,
		   h->last_sr_cns, h->last_sr_polls);
	hcc_printf(&o, "dr_republish_count=%llu hcc_rx_nodes=%u dr_rx_nodes=%u\n",
		   (unsigned long long)h->dr_republish_count,
		   h->hcc_rx_nodes, h->dr_rx_nodes);
	return o.pos;
}
static DEVICE_ATTR_RO(hcc_events);

static ssize_t hcc_rings_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct hi5622_pci *ctx = hcc_ctx(dev);
	struct hi5622_hcc_service *h;
	struct hcc_out o;
	int i;

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	h = ctx->hcc;
	hcc_out_init(&o, buf, PAGE_SIZE);
	hcc_printf(&o, "HCC rings: armed=%d prepost=%u depth=%u depth-1=0x%x\n",
		   h->armed, h->prepost, HI5622_ETE_DEPTH,
		   HI5622_ETE_DEPTH - 1);
	if (!ctx->windows_programmed) {
		hcc_printf(&o, "(not mapped: run 'prepare' first)\n");
		return o.pos;
	}
	for (i = 0; i < HI5622_HCC_SR_CH_COUNT; i++) {
		struct hi5622_ete_ring *r = &h->sr[i].ete;
		void __iomem *ch = ctx->bar0 + HI5622_BAR0_ETE_BASE +
				   r->chan_off;

		hcc_printf(&o, "SR%d ch=0x%03x nodes=0x%08x base=0x%08x depth-1=0x%02x prod=0x%08x consumer=0x%08x enable=0x%08x ctrl=0x%x sendword=0x%08x\n",
			   i, r->chan_off, (u32)r->node_dma,
			   readl(ch + HI5622_ETE_CH_A_BASE),
			   readl(ch + HI5622_ETE_CH_A_DEPTH) & HCC_IDX_LOW_MASK,
			   readl(ch + HI5622_ETE_CH_A_PRD),
			   readl(ch + HI5622_ETE_CH_A_CNS),
			   readl(ch + HI5622_ETE_CH_ENABLE),
			   readl(ch + HI5622_ETE_CH_CTRL) & 0x7,
			   readl(ctx->bar0 + HI5622_BAR0_ETE_BASE +
				 HI5622_HCC_ETE_SEND_SR0 + 4 * i));
	}
	for (i = 0; i < HI5622_HCC_DR_CH_COUNT; i++) {
		struct hi5622_ete_ring *r = &h->dr[i].ete;
		void __iomem *ch = ctx->bar0 + HI5622_BAR0_ETE_BASE +
				   r->chan_off;

		hcc_printf(&o, "DR%d ch=0x%03x nodes=0x%08x base=0x%08x depth-1=0x%02x pub(+0x38)=0x%08x fill(+0x3c)=0x%08x enable=0x%08x sendword=0x%08x\n",
			   i, r->chan_off, (u32)r->node_dma,
			   readl(ch + HI5622_ETE_CH_B_BASE),
			   readl(ch + HI5622_ETE_CH_B_DEPTH) & HCC_IDX_LOW_MASK,
			   readl(ch + HI5622_ETE_CH_B_PRD),
			   readl(ch + HI5622_ETE_CH_B_CNS),
			   readl(ch + HI5622_ETE_CH_ENABLE),
			   readl(ctx->bar0 + HI5622_BAR0_ETE_BASE +
				 HI5622_HCC_ETE_SEND_DR0 + 4 * i));
	}
	hcc_printf(&o, "NOTE: enable/ctrl/fill/+0xC00/+0xC18 are firmware or device authored and were never written by this host (vendor parity).\n");
	return o.pos;
}
static DEVICE_ATTR_RO(hcc_rings);

static ssize_t hcc_plan_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct hcc_out o;

	hcc_out_init(&o, buf, PAGE_SIZE);
	hcc_printf(&o, "HCC-SERVICE BENCH LADDER (attended; full text in the lane REPORT/BENCH-PROCEDURE)\n");
	hcc_printf(&o, "S1 boot: insmod hi5622_pciet_hcc.ko (base poll defaults OFF) -> prepare -> upload -> verify diffs=0\n");
	hcc_printf(&o, "S2 pre-release: echo 1 > hcc_arm (7 channels, intr RMW first); cat hcc_rings (pub=0x400). NEVER write CECE here.\n");
	hcc_printf(&o, "S3 release: echo 1 > release (0x5a5a ONLY). Then echo 1 > hcc_loop to start the vendor-order service BEFORE the first event.\n");
	hcc_printf(&o, "S4 watch: cat hcc_status hcc_ready hcc_irq_probe hcc_events; expected page +8=2, state=1, mask=0, boot=true.\n");
	hcc_printf(&o, "S5 ready gate: expect g4 id1 <= 2.0 s then g4 id2 <= 20.0 s (hcc_ready). No H2D reply on this path.\n");
	hcc_printf(&o, "S6 if no ready: cat hcc_irq_probe -> if EOI=0 and timer zero the image IRQ layer never started (firmware/ROM-internal; H2D messages cannot create it).\n");
	hcc_printf(&o, "S7 optional one-shot H2D liveness: echo 1 > hcc_sr_announce (valid group-0 id-0 frame + mailbox id3).\n");
	hcc_printf(&o, "    consumed => H2D consumer alive; out[0] bit3 stuck => H2D lane closed: stop, no retry, no payload experiment.\n");
	hcc_printf(&o, "S8 S3-only ETE nudge (one per session): echo 1 > hcc_dr_republish; re-take hcc_status/hcc_rings and stop if fills frozen.\n");
	hcc_printf(&o, "STOP RULES: no out[5] write; no device RAM write; no H2D id5; no group-4 H2D; no CECE before page+8==2; no retries in-session; any abort/SError -> capture + power-cycle.\n");
	return o.pos;
}
static DEVICE_ATTR_RO(hcc_plan);

/* ---------------------------------------------------------------------
 * Write attributes
 * ------------------------------------------------------------------- */
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
	int serviced;

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	if (!sysfs_streq(buf, "1"))
		return -EINVAL;
	serviced = hcc_service_pass(ctx, "manual");
	hcc_watch_dr(ctx);
	hcc_irq_watch(ctx);
	dev_info(dev, "HCC service: one manual pass done (%d bit(s) dispatched); out[1] now=0x%08x\n",
		 serviced, readl(ctx->bar0 + HI5622_BAR0_D2H_BITMAP));
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
			dev_err(dev, "HCC loop: run 'hcc_arm' first (vendor arms the ETE rings at probe, before the release)\n");
			return -EBUSY;
		}
		if (h->loop)
			return count;
		h->loop_ms = hcc_poll_ms;
		h->loop = kthread_run(hcc_loop_thread, h, "%s-svc", HCC_DRV_NAME);
		if (IS_ERR(h->loop)) {
			ret = PTR_ERR(h->loop);
			h->loop = NULL;
			return ret;
		}
	} else if (h->loop) {
		kthread_stop(h->loop);
		h->loop = NULL;
		dev_info(dev, "HCC loop: stopped by request; counters retained in hcc_events\n");
	}
	return count;
}
static DEVICE_ATTR_WO(hcc_loop);

static ssize_t hcc_clear_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct hi5622_pci *ctx = hcc_ctx(dev);
	struct hi5622_hcc_service *h;

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	h = ctx->hcc;
	if (!sysfs_streq(buf, "1"))
		return -EINVAL;
	h->events = 0;
	h->id_seen = 0;
	h->last_bitmap = 0;
	memset(h->bit_count, 0, sizeof(h->bit_count));
	memset(h->hist, 0, sizeof(h->hist));
	h->hist_pos = 0;
	h->hcc_rx_nodes = 0;
	h->dr_rx_nodes = 0;
	h->sr_announce_ok = 0;
	h->sr_announce_timeout = 0;
	h->sr_announce_refused = 0;
	h->dr_republish_count = 0;
	h->cece_evidence_events = 0;
	h->eoi_events = 0;
	dev_info(dev, "HCC: telemetry counters cleared (rings/armed/ready state unchanged)\n");
	return count;
}
static DEVICE_ATTR_WO(hcc_clear);

static struct attribute *hcc_attrs[] = {
	&dev_attr_hcc_status.attr,
	&dev_attr_hcc_ready.attr,
	&dev_attr_hcc_irq_probe.attr,
	&dev_attr_hcc_events.attr,
	&dev_attr_hcc_rings.attr,
	&dev_attr_hcc_plan.attr,
	&dev_attr_hcc_arm.attr,
	&dev_attr_hcc_loop.attr,
	&dev_attr_hcc_service.attr,
	&dev_attr_hcc_sr_announce.attr,
	&dev_attr_hcc_cece_kick.attr,
	&dev_attr_hcc_dr_republish.attr,
	&dev_attr_hcc_clear.attr,
	NULL,
};
ATTRIBUTE_GROUPS(hcc);

/* ---------------------------------------------------------------------
 * Probe / remove
 * ------------------------------------------------------------------- */
static void hcc_rings_free(struct hi5622_pci *ctx, struct hi5622_hcc_service *h)
{
	int i;

	for (i = 0; i < HI5622_HCC_DR_CH_COUNT; i++)
		pciet_ete_ring_free(ctx, &h->dr[i].ete);
	for (i = 0; i < HI5622_HCC_SR_CH_COUNT; i++)
		pciet_ete_ring_free(ctx, &h->sr[i].ete);
}

int pciet_hcc_probe(struct hi5622_pci *ctx)
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

	hcc_assert_contract();

	h = devm_kzalloc(dev, sizeof(*h), GFP_KERNEL);
	if (!h)
		return -ENOMEM;
	h->ctx = ctx;
	h->state = HCC_RS_PROBE;
	h->prepost = HI5622_ETE_DEPTH;
	mutex_init(&h->lock);
	ctx->hcc = h;
	pciet_d2h_init(ctx, &h->rd);

	/* Host-DRAM ring mirrors only - no device writes in probe. */
	for (i = 0; i < HI5622_HCC_SR_CH_COUNT; i++) {
		ret = pciet_ete_ring_alloc(ctx, &h->sr[i].ete, sr_off[i],
					   HCC_SR_BUF_LEN);
		if (ret)
			goto err_free;
	}
	for (i = 0; i < HI5622_HCC_DR_CH_COUNT; i++) {
		ret = pciet_ete_ring_alloc(ctx, &h->dr[i].ete, dr_off[i],
					   HCC_DR_BUF_LEN);
		if (ret)
			goto err_free;
	}

	ret = device_add_groups(dev, hcc_groups);
	if (ret) {
		dev_err(dev, "HCC: device_add_groups: %d\n", ret);
		goto err_free;
	}

	/* Direct-read D2H attributes (rd_poll/rd_advance/rd_status/
	 * rd_decode/rd_catalog).  Best-effort: a failure leaves the
	 * vendor-order service fully usable. */
	ret = pciet_d2h_add_attrs(dev);
	if (ret)
		dev_warn(dev, "D2H DIRECT: rd_* attributes unavailable (%d)\n",
			 ret);

	for (i = 0; i < HI5622_HCC_DR_CH_COUNT; i++)
		if (!hcc_dma_in_window((u32)h->dr[i].ete.node_dma))
			dev_warn(dev, "HCC: DR%u node dma 0x%08x outside the RC inbound window 0x80000000..0x8fffffff; hcc_arm will refuse\n",
				 i, (u32)h->dr[i].ete.node_dma);
	for (i = 0; i < HI5622_HCC_SR_CH_COUNT; i++)
		if (!hcc_dma_in_window((u32)h->sr[i].ete.node_dma))
			dev_warn(dev, "HCC: SR%u node dma 0x%08x outside the RC inbound window 0x80000000..0x8fffffff; hcc_arm will refuse\n",
				 i, (u32)h->sr[i].ete.node_dma);

	dev_info(dev, "HCC service ready: 3 SR + 4 DR ring mirrors in host DRAM (no device writes). Attrs: hcc_arm hcc_loop hcc_service hcc_sr_announce hcc_cece_kick hcc_dr_republish | RO: hcc_status hcc_ready hcc_irq_probe hcc_events hcc_rings hcc_plan. Corrected protocol: release-only; CECE only post-handshake on spin evidence\n");
	return 0;

err_free:
	hcc_rings_free(ctx, h);
	ctx->hcc = NULL;
	return ret;
}

void pciet_hcc_remove(struct hi5622_pci *ctx)
{
	struct hi5622_hcc_service *h = ctx->hcc;

	if (!h)
		return;
	if (h->loop) {
		kthread_stop(h->loop);
		h->loop = NULL;
	}
	pciet_d2h_del_attrs(&ctx->pdev->dev);
	device_remove_groups(&ctx->pdev->dev, hcc_groups);
	hcc_rings_free(ctx, h);
	ctx->hcc = NULL;
	dev_info(&ctx->pdev->dev, "HCC service removed\n");
}

