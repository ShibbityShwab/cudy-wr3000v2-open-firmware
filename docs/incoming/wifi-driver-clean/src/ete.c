// SPDX-License-Identifier: GPL-2.0
/*
 * ete.c - ETE ring-engine helpers for the Hi5622V100 clean-room transport.
 *
 * ORIGINAL clean-room code built from the decoded contract (wifi-driver-re
 * sec 4.1, wifi-firmware-re sec 3.2, phase42/ete-registers-reconciled.md
 * E.2/E.3, wifi-ete-init).  No vendor code is reproduced.
 *
 * Model:
 *   - one channel = one 0x50-byte register block in the device ETE file
 *     (CA 0x4003a000 -> BAR0 0x3f2000); SR (host->device) uses program group A
 *     (+0x10..+0x1c), DR (device->host) uses group B (+0x30..+0x3c); each
 *     group holds {base, depth-1, producer/published, consumer/fill};
 *   - node = 8 B {device-visible buffer address, (len<<16)|0x6d2b};
 *   - ring node arrays and buffers are host DRAM allocated through the DMA API;
 *     the device-visible address must fall in the RC inbound window
 *     0x80000000..0x8fffffff (board DTS dma-ranges identity; the vendor's
 *     host-VA == device-address identity is intentionally NOT copied).
 *
 * This file owns the host-DRAM ring mirrors and the host-owned SR producer
 * post.  The vendor-exact channel arm sequence (intr RMW, per-channel group
 * registers, pre-posted DR free buffers) lives in the service layer, and the
 * device->host receive path is the direct reader in hi5622_d2h.c.
 */
#include <linux/types.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <asm/barrier.h>

#include "hi5622_driver.h"

int hi5622_ete_ring_alloc(struct hi5622_pci *ctx, struct hi5622_ete_ring *r,
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

	/* Pre-post the buffer device addresses; word1 is set per transfer. */
	nodes = r->node_arr;
	for (i = 0; i < r->depth; i++) {
		nodes[i].devva = cpu_to_le32((u32)r->buf_dma[i]);
		nodes[i].word1 = 0;
	}
	wmb();

	dev_info(dev, "ETE ring @ chan 0x%03x: nodes dma 0x%08x depth %u buflen %u\n",
		 chan_off, (u32)r->node_dma, r->depth, buf_len);
	return 0;

err_bufs:
	while (i-- > 0)
		dma_free_coherent(dev, buf_len, r->bufs[i], r->buf_dma[i]);
	kfree(r->bufs);
	kfree(r->buf_dma);
err_nodes:
	dma_free_coherent(dev, r->depth * sizeof(*nodes), r->node_arr,
			  r->node_dma);
	memset(r, 0, sizeof(*r));
	return -ENOMEM;
}

void hi5622_ete_ring_free(struct hi5622_pci *ctx, struct hi5622_ete_ring *r)
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
 * H2D post helper: copy the message into SR buffer (producer & DEPTH_MASK),
 * publish the node {devva, (len<<16)|0x6d2b} and advance the packed producer.
 * Returns 0 and the new packed producer through new_producer.  The caller then
 * kicks the device with the mailbox announce (id 3).
 *
 * The SR group-A base/depth/consumer registers are owned by the service arm
 * path; this helper only touches the host-DRAM node and the host-owned
 * producer register (+0x18).
 */
int hi5622_ete_sr_post(struct hi5622_pci *ctx, struct hi5622_ete_ring *r,
		       u32 producer, const void *data, u32 len, u32 *new_producer)
{
	void __iomem *ch = ctx->bar0 + HI5622_ETE_BAR0 + r->chan_off;
	struct hi5622_ete_node *nodes = r->node_arr;
	u32 idx, word1;

	if (len > r->buf_len) {
		dev_err(&ctx->pdev->dev, "SR post: len %u > buflen %u\n",
			len, r->buf_len);
		return -EINVAL;
	}
	if (!hi5622_idx_sane(producer, r->depth)) {
		dev_err(&ctx->pdev->dev, "SR post: implausible producer 0x%08x\n",
			producer);
		return -EINVAL;
	}
	idx = producer & HI5622_ETE_DEPTH_MASK;
	memcpy(r->bufs[idx], data, len);
	nodes[idx].devva = cpu_to_le32((u32)r->buf_dma[idx]);
	word1 = cpu_to_le32((len << HI5622_ETE_NODE_LEN_SHIFT) |
			    HI5622_ETE_NODE_FLAGS);
	nodes[idx].word1 = word1;
	wmb();
	writel(hi5622_idx_plus(producer, r->depth), ch + HI5622_ETE_CH_A_PRD);
	*new_producer = hi5622_idx_plus(producer, r->depth);
	return 0;
}
