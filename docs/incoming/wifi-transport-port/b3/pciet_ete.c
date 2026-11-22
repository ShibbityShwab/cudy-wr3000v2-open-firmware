// SPDX-License-Identifier: GPL-2.0
/*
 * pciet_ete.c - ETE ring-engine helpers for the Hi5622V100 PCIe transport
 * skeleton.  ORIGINAL code written by the wifi-transport-port lane from the
 * decoded contract (wifi-driver-re sec 4.1, wifi-firmware-re sec 3.2,
 * phase42/ete-registers-reconciled.md E.2/E.3).  No vendor code reproduced.
 *
 * Model implemented here (contract, not a re-implementation of vendor text):
 *
 *   - One channel = one register block in the device's ETE file
 *     (CA 0x4003a000 = BAR0+0x3f2000), 0x50 bytes.  SR (host->device) uses
 *     program group A (+0x10..+0x1c); DR (device->host) uses group B
 *     (+0x30..+0x3c).  Each group holds {base, depth-1, producer, consumer}.
 *
 *   - The ring node array and the message buffers live in host DRAM, made
 *     coherent with the device through the RC inbound window (device address
 *     space 0x80000000..0xffffffff).  On this board the window is
 *     PCI 0x80000000 -> CPU phys 0x80000000 (identity; DTS dma-ranges in
 *     luofu-host-618-delta.patch), so the DMA handle IS the device-side
 *     address.  The vendor's 5.10 tree used a 2 GiB kernel/userspace split
 *     where the host VA happened to equal the device address; that identity
 *     must NOT be copied - this code always goes through the DMA API.
 *
 *   - A ring node is 8 bytes: word0 = device-side address of the message
 *     buffer, word1 = (len << 16) | 0x6d2b.
 *
 *   - SR: the host is the producer (writes the +0x18 register), the device
 *     is the consumer (+0x1c).  DR: mirrored.
 *
 * The channel *enable* bits (+0x00 bit 0, +0x08 ctrl, +0x48 second enable)
 * are deliberately NOT touched by ring_init(): the vendor's exact enable
 * sequence is [U] and the firmware enables the DR side itself (device-side
 * precondition P1, [V]).  The bench plan enables channels explicitly after
 * the ready handshake.
 */
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/byteorder/generic.h>
#include <asm/barrier.h>

#include "hi5622_pciet.h"

/*
 * Allocate one ring: node array (depth x 8 B) + depth message buffers.
 * dma_alloc_coherent() returns the device-visible address in *node_dma;
 * with the dma-ranges of luofu-host-618-delta.patch that address lies in
 * the 0x8xxxxxxx window the ETE engine addresses (phase42 E.3 [V*]).
 */
int pciet_ete_ring_alloc(struct hi5622_pci *ctx, struct hi5622_ete_ring *r,
			 u32 chan_off, u32 buf_len)
{
	struct device *dev = &ctx->pdev->dev;
	struct hi5622_ete_node *nodes;
	u32 i;

	r->depth = HI5622_ETE_DEPTH;
	r->chan_off = chan_off;
	r->buf_len = buf_len;
	r->producer = 0;
	r->consumer = 0;
	r->enabled = false;

	r->node_arr = dma_alloc_coherent(dev, r->depth * sizeof(*nodes),
					 &r->node_dma, GFP_KERNEL);
	if (!r->node_arr)
		return -ENOMEM;

	r->bufs = kcalloc(r->depth, sizeof(*r->bufs), GFP_KERNEL);
	r->buf_dma = kcalloc(r->depth, sizeof(*r->buf_dma), GFP_KERNEL);
	if (!r->bufs || !r->buf_dma)
		goto err_nodes;

	for (i = 0; i < r->depth; i++) {
		r->bufs[i] = dma_alloc_coherent(dev, buf_len, &r->buf_dma[i],
						GFP_KERNEL);
		if (!r->bufs[i])
			goto err_bufs;
	}

	/* Pre-post the buffer devvas; word1 is set per transfer. */
	nodes = r->node_arr;
	for (i = 0; i < r->depth; i++) {
		nodes[i].devva = cpu_to_le32(r->buf_dma[i]);
		nodes[i].word1 = 0;
	}
	wmb();

	dev_info(dev, "ETE ring @ chan 0x%x: nodes dma 0x%08x depth %u buflen %u\n",
		 chan_off, r->node_dma, r->depth, buf_len);
	return 0;

err_bufs:
	while (i-- > 0)
		dma_free_coherent(dev, buf_len, r->bufs[i], r->buf_dma[i]);
	kfree(r->bufs);
	kfree(r->buf_dma);
err_nodes:
	dma_free_coherent(dev, r->depth * sizeof(*nodes), r->node_arr,
			  r->node_dma);
	return -ENOMEM;
}

void pciet_ete_ring_free(struct hi5622_pci *ctx, struct hi5622_ete_ring *r)
{
	struct device *dev = &ctx->pdev->dev;
	u32 i;

	if (!r->node_arr)
		return;
	for (i = 0; i < r->depth; i++)
		if (r->bufs && r->bufs[i])
			dma_free_coherent(dev, r->buf_len, r->bufs[i],
					  r->buf_dma[i]);
	kfree(r->bufs);
	kfree(r->buf_dma);
	dma_free_coherent(dev, r->depth * sizeof(struct hi5622_ete_node),
			  r->node_arr, r->node_dma);
	memset(r, 0, sizeof(*r));
}

/*
 * Program the channel register group and write the indices.
 *
 * use_grp_a: SR => group A (+0x10..+0x1c); DR => group B (+0x30..+0x3c).
 * This performs DEVICE WRITES (ETE register file) - called only from the
 * attended bench steps, never from probe.
 *
 * The measured vendor values the first bench session must compare against
 * (phase42 E.3, [V*]): base in 0x80000000..0xffffffff, depth-1 = 0x1f,
 * index pair <= 0x7ff with the 0x400 phase bit.
 */
int pciet_ete_ring_init(struct hi5622_pci *ctx, struct hi5622_ete_ring *r,
			bool use_grp_a)
{
	void __iomem *ch = ctx->bar0 + HI5622_BAR0_ETE_BASE + r->chan_off;
	u32 base_off, depth_off, prd_off, cns_off;

	if (use_grp_a) {
		base_off = HI5622_ETE_CH_A_BASE;
		depth_off = HI5622_ETE_CH_A_DEPTH;
		prd_off = HI5622_ETE_CH_A_PRD;
		cns_off = HI5622_ETE_CH_A_CNS;
	} else {
		base_off = HI5622_ETE_CH_B_BASE;
		depth_off = HI5622_ETE_CH_B_DEPTH;
		prd_off = HI5622_ETE_CH_B_PRD;
		cns_off = HI5622_ETE_CH_B_CNS;
	}

	writel(cpu_to_le32(r->node_dma), ch + base_off);
	writel(r->depth - 1, ch + depth_off);
	writel(0, ch + prd_off);
	writel(0, ch + cns_off);

	dev_info(&ctx->pdev->dev,
		 "ETE ch 0x%x initialized: base=0x%08x depth-1=0x%x (%s)\n",
		 r->chan_off, r->node_dma, r->depth - 1,
		 use_grp_a ? "group A (SR)" : "group B (DR)");

	/*
	 * The channel enable bits (+0x00 bit 0, +0x48) are intentionally left
	 * alone: the vendor's enable order is [U] and the device enables the
	 * DR channel itself (P1, [V]).  See BENCH-PLAN.md stage B2.
	 */
	return 0;
}

/*
 * H2D send: copy the message into the next free SR buffer, post the node,
 * advance the producer register.  The caller then kicks the device with
 * pciet_msg_send_irq() (id 3 = ETE transfer done trigger).
 *
 * Posting order follows shuangta_ete_sr_dscr_fill ([V]): word1 =
 * (len << 16) | 0x6d2b, node written last, producer register bumped after
 * the node is visible.
 */
int pciet_ete_sr_send(struct hi5622_pci *ctx, struct hi5622_ete_ring *r,
		      const void *data, u32 len)
{
	void __iomem *ch = ctx->bar0 + HI5622_BAR0_ETE_BASE + r->chan_off;
	struct hi5622_ete_node *nodes = r->node_arr;
	u32 idx, word1;

	if (len > r->buf_len) {
		dev_err(&ctx->pdev->dev, "SR send: len %u > buflen %u\n",
			len, r->buf_len);
		return -EINVAL;
	}

	idx = r->producer & HI5622_ETE_DEPTH_MASK;
	memcpy(r->bufs[idx], data, len);

	word1 = cpu_to_le32((len << HI5622_ETE_NODE_LEN_SHIFT) |
			    HI5622_ETE_NODE_FLAGS);
	nodes[idx].word1 = word1;
	wmb();

	writel(r->producer + 1, ch + HI5622_ETE_CH_A_PRD);
	r->producer++;
	return 0;
}

/*
 * D2H consume: read the producer register, walk the new nodes, hand each
 * buffer to the callback, then advance the consumer register.
 *
 * The device-side address in word0 is resolved against this ring's buffer
 * table (the device may only post buffers the host gave it).  A node whose
 * devva is unknown is logged and skipped - never dereferenced.
 */
int pciet_ete_dr_consume(struct hi5622_pci *ctx, struct hi5622_ete_ring *r,
			 void (*cb)(struct hi5622_pci *ctx, void *va, u32 len))
{
	void __iomem *ch = ctx->bar0 + HI5622_BAR0_ETE_BASE + r->chan_off;
	struct hi5622_ete_node *nodes = r->node_arr;
	u32 prd = readl(ch + HI5622_ETE_CH_B_PRD);
	u32 idx, devva, len;
	void *va;
	int found, n = 0;

	while (r->consumer != prd) {
		idx = r->consumer & HI5622_ETE_DEPTH_MASK;
		rmb();
		devva = le32_to_cpu(nodes[idx].devva);
		len = (le32_to_cpu(nodes[idx].word1) >> HI5622_ETE_NODE_LEN_SHIFT);

		va = NULL;
		for (found = 0; found < (int)r->depth; found++) {
			if (r->buf_dma[found] == devva) {
				va = r->bufs[found];
				break;
			}
		}
		if (va) {
			if (cb)
				cb(ctx, va, len);
			n++;
		} else {
			dev_warn(&ctx->pdev->dev,
				 "DR node 0x%08x: unknown devva, skipped\n",
				 devva);
		}
		nodes[idx].word1 = 0;	/* release the slot */
		wmb();
		r->consumer++;
	}

	if (n)
		writel(r->consumer, ch + HI5622_ETE_CH_B_CNS);
	return n;
}
