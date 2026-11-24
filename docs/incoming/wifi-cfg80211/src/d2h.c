// SPDX-License-Identifier: GPL-2.0
/*
 * d2h.c - direct device group-A D2H receive service (engine-independent).
 *
 * ORIGINAL clean-room code, restructured from the wifi-d2h-direct lane
 * deliverable.  The firmware stages completed HCC frames as 8-byte nodes in
 * the DEVICE group-A DR rings (PKTRAM) and the frame bodies in device ACP
 * SRAM; the ETE copy engine stays idle in our reachable boot, so the host
 * reads both structures directly through the mapped inbound windows:
 *
 *   node array : group A base register (+0x10) in PKTRAM (region 2)
 *   frame body : node.word0, ACP alias 0x02xxxxxx (region 4)
 *   producer   : firmware per-channel record +0xc packed shadow (region 5)
 *
 * The direct path is the PRIMARY receive path of this driver: it feeds the
 * ready state machine (g4 id1/id2) and the frame catalogue without depending
 * on the ETE engine.  Consumer advance: the device +0x1C is engine-authored
 * and has no host write path in the vendor design; the only host-owned DR
 * index is the group-B published free index (+0x38), republished by explicit
 * sysfs action.  The +0x1C write is a default-OFF experiment.
 *
 * Safety: no firmware RAM / device RAM writes in the read path; only +0x38
 * (explicit store, boot-gated) and the default-OFF +0x1C experiment; the
 * PKTRAM control word node-base+0x100 is never touched; probe performs no
 * device writes.
 */

#include <linux/types.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <asm/barrier.h>

#include "hi5622_hcc.h"

#define RD_DR_CHAN_CA(ch)	(HI5622_D2H_DR_CHAN_CA0 + \
				 HI5622_ETE_CH_STRIDE * (ch))
#define RD_DR_SEND_CA(ch)	(HI5622_D2H_DR_SEND_CA0 + \
				 4u * (ch))
#define RD_DR_CHAN_OFF(ch)	(HI5622_ETE_DR0_OFF + HI5622_ETE_CH_STRIDE * (ch))

/* Experimental device group-A consumer (+0x1C) write.  Default 0 = this
 * service performs no device-ring consumer writes at all.  The bench
 * procedure treats a 1 only as a documented fallback experiment. */
/* chip-group-3 catalogue ids seen in the boot capture (hmac/hdpp tables) */
#define HI5622_G3_ID_SMPS_CAP	9u
#define HI5622_G3_ID_PFM_MSG	10u
#define HI5622_G3_ID_BSD_UPDATE	15u
#define HI5622_G3_ID_MULTIAP_11V 16u
#define HI5622_G3_ID_D2H_PM	17u
#define HI5622_G3_ID_TEMP_STATE	18u
#define HI5622_G3_ID_BEACON_REPROT 19u
#define HI5622_G3_ID_BEACON_FRAME 20u

/* The direct reader is embedded in the HCC service state; this driver always
 * has exactly one service instance per PCI function. */
static struct hi5622_d2h_direct *hi5622_d2h_of(struct hi5622_pci *ctx)
{
	return ctx && ctx->hcc ? &ctx->hcc->rd : NULL;
}

unsigned int hi5622_rd_ab_write;
module_param_named(rd_ab_write, hi5622_rd_ab_write, uint, 0644);
MODULE_PARM_DESC(rd_ab_write,
		 "D2H direct: allow the EXPERIMENTAL device ring consumer (+0x1C) write = 1 (default 0 = never)");

/* ---------------------------------------------------------------------
 * Small formatter (read-only attributes; same pattern as the hcc service)
 * ------------------------------------------------------------------- */
struct rd_out {
	char	*buf;
	size_t	size;
	size_t	pos;
};

static void rd_out_init(struct rd_out *o, char *buf, size_t size)
{
	o->buf = buf;
	o->size = size;
	o->pos = 0;
	if (size)
		buf[0] = '\0';
}

static void rd_printf(struct rd_out *o, const char *fmt, ...)
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

static void rd_hex(char *dst, size_t dstsz, const u8 *src, u32 len)
{
	u32 i;

	if (!dstsz)
		return;
	dst[0] = '\0';
	for (i = 0; i < len && (i + 1) * 2 + 1 <= dstsz; i++)
		snprintf(dst + i * 2, dstsz - i * 2, "%02x", src[i]);
}

static const char *rd_ascii(const u8 *p, u32 len, char *dst, size_t dstsz)
{
	u32 i;

	if (!dstsz)
		return "";
	for (i = 0; i + 1 < dstsz && i < len; i++)
		dst[i] = (p[i] >= 0x20 && p[i] < 0x7f) ? (char)p[i] : '.';
	dst[i] = '\0';
	return dst;
}

/* ---------------------------------------------------------------------
 * Address helpers: every read is bounds-checked against the mapped region.
 * ------------------------------------------------------------------- */

static u32 rd_chan_reg(struct hi5622_pci *ctx, int ch, u32 reg_off)
{
	return readl(ctx->bar0 + HI5622_ETE_BAR0 + RD_DR_CHAN_OFF(ch) +
		     reg_off);
}

/* firmware RAM (region 5): BAR0 = 0x6B8000 + CPU [V] */
static bool rd_fw_cpu_ok(u32 cpu)
{
	return cpu >= HI5622_FW_RAM_CPU_LO &&
	       cpu <= HI5622_FW_RAM_CPU_HI;
}

static u32 rd_fw_u32(struct hi5622_pci *ctx, u32 cpu)
{
	return readl(ctx->bar0 + HI5622_FW_RAM_BAR_BASE + cpu);
}

static u8 rd_fw_u8(struct hi5622_pci *ctx, u32 cpu)
{
	return readb(ctx->bar0 + HI5622_FW_RAM_BAR_BASE + cpu);
}

/* PKTRAM (region 2): BAR0 = 0x1D8000 + (CA - 0x01000000) [V] */
static void __iomem *rd_pktram_va(struct hi5622_pci *ctx, u32 ca)
{
	if (ca < HI5622_PKTRAM_CA_LO || ca > HI5622_PKTRAM_CA_HI ||
	    (u64)ca + 8 > (u64)HI5622_PKTRAM_CA_HI + 1)
		return NULL;
	return ctx->bar0 + HI5622_PKTRAM_BAR_BASE +
	       (ca - HI5622_PKTRAM_CA_LO);
}

static void __iomem *rd_acp_va(struct hi5622_pci *ctx, u32 ca)
{
	if (ca < HI5622_ACP_CA_LO || ca > HI5622_ACP_CA_HI)
		return NULL;
	return ctx->bar0 + HI5622_ACP_BAR_BASE + (ca - HI5622_ACP_CA_LO);
}

/* Bounded copy from a mapped device window.  Word reads are used only for a
 * 4-byte-aligned window; the byte fallback keeps a corrupt/short node pointer
 * from causing an unaligned MMIO access. */
static int rd_iomem_copy(void __iomem *va, u8 *dst, u32 len)
{
	u32 i = 0;

	if (!((unsigned long)va & 3)) {
		for (; i + 4 <= len; i += 4) {
			u32 w = readl(va + i);

			memcpy(dst + i, &w, 4);
		}
	}
	for (; i < len; i++)
		dst[i] = readb(va + i);
	return 0;
}

/* Copy up to len bytes of a device frame body from node.word0.
 * word0 is the ACP alias (dev CA 0x02xxxxxx) of the PKTRAM message pool; the
 * PKTRAM NOACP alias and the host-DRAM DMA window are accepted as fallbacks.
 * Returns 0 on success, -EFAULT for an unresolvable pointer. */
static int rd_copy_body(struct hi5622_pci *ctx, u32 ptr, u8 *dst, u32 len)
{
	void __iomem *va = rd_acp_va(ctx, ptr);

	if (va)
		return rd_iomem_copy(va, dst, len);

	va = rd_pktram_va(ctx, ptr);
	if (va)
		return rd_iomem_copy(va, dst, len);

	if (ptr >= HI5622_HOST_DMA_LO && ptr <= HI5622_HOST_DMA_HI &&
	    pfn_valid(ptr >> PAGE_SHIFT)) {
		memcpy(dst, phys_to_virt(ptr), len);
		return 0;
	}

	return -EFAULT;
}

/* ---------------------------------------------------------------------
 * Boot gates
 * ------------------------------------------------------------------- */
/* ---------------------------------------------------------------------
 * Message catalogue (wifi-hcc-re sec 5.5) [V]
 * ------------------------------------------------------------------- */
static const char *rd_msg_name(u8 group, u16 id)
{
	if (group == HI5622_HCC_GROUP_PLAT) {
		switch (id) {
		case HI5622_G4_ID_OAM_LOG:		return "g4_id0_oam_dev_log";
		case HI5622_G4_ID_PLAT_READY:	return "g4_id1_plat_ready";
		case HI5622_G4_ID_DEV_READY:	return "g4_id2_device_ready";
		case HI5622_G4_ID_EXCP_INFO:	return "g4_id3_excp_info";
		case HI5622_G4_ID_HEARTBEAT:	return "g4_id4_heartbeat";
		default:				return "g4_unknown";
		}
	}
	if (group == 3) {
		switch (id) {
		case HI5622_G3_ID_SMPS_CAP:		return "g3_id9_smps_cap";
		case HI5622_G3_ID_PFM_MSG:		return "g3_id10_pfm_msg";
		case HI5622_G3_ID_BSD_UPDATE:	return "g3_id15_bsd_update";
		case HI5622_G3_ID_MULTIAP_11V:	return "g3_id16_multiap_11v";
		case HI5622_G3_ID_D2H_PM:		return "g3_id17_d2h_pm";
		case HI5622_G3_ID_TEMP_STATE:	return "g3_id18_temp_state";
		case HI5622_G3_ID_BEACON_REPROT:	return "g3_id19_beacon_report";
		case HI5622_G3_ID_BEACON_FRAME:	return "g3_id20_beacon_frame";
		default:				return "g3_unknown";
		}
	}
	return NULL;
}

/* ---------------------------------------------------------------------
 * Frame decode + delivery (kernel-side: catalogue counters, ready flags,
 * bounded hexdump ring readable through the rd_decode attribute)
 * ------------------------------------------------------------------- */
static void rd_seen_update(struct hi5622_d2h_direct *rd, u8 group, u16 id,
			   u16 total, u16 len)
{
	u32 i;

	for (i = 0; i < rd->seen_count; i++) {
		if (rd->seen[i].group == group && rd->seen[i].id == id) {
			rd->seen[i].count++;
			rd->seen[i].last_total = total;
			rd->seen[i].last_len = len;
			return;
		}
	}
	if (rd->seen_count < HI5622_D2H_SEEN_ENTRIES) {
		rd->seen[rd->seen_count].group = group;
		rd->seen[rd->seen_count].id = id;
		rd->seen[rd->seen_count].count = 1;
		rd->seen[rd->seen_count].last_total = total;
		rd->seen[rd->seen_count].last_len = len;
		rd->seen_count++;
	} else {
		rd->seen_other++;
	}
}

static void rd_deliver(struct hi5622_pci *ctx, struct hi5622_d2h_direct *rd,
		       int ch, u32 idx, u32 node_ptr, u32 node_len,
		       const u8 *p, u32 snap_len)
{
	struct device *dev = &ctx->pdev->dev;
	struct hi5622_d2h_frame *fr;
	u8 group, rsrc, dst;
	u16 total, id, tag;
	u32 plen;
	char asc[49];

	group = p[0] & 0x0fu;
	rsrc = p[1] >> 4;
	total = (u16)(p[4] | ((u16)p[5] << 8));
	id = (u16)(p[6] | ((u16)p[7] << 8));
	dst = p[9];
	tag = (u16)(p[10] | ((u16)p[11] << 8));

	rd->frames++;
	fr = &rd->log[rd->log_pos];
	memset(fr, 0, sizeof(*fr));
	fr->jiffies64 = get_jiffies_64();
	fr->ch = (u8)ch;
	fr->group = group;
	fr->rsrc = rsrc;
	fr->dst = dst;
	memcpy(fr->hdr, p, HI5622_HCC_HDR_LEN);
	fr->id = id;
	fr->total = total;
	fr->node_len = (u16)node_len;
	fr->node_ptr = node_ptr;
	fr->tag_ok = tag == HI5622_HCC_TAG;
	fr->len_ok = total == (u16)node_len && total >= HI5622_HCC_HDR_LEN;
	plen = total >= HI5622_HCC_HDR_LEN ? total - HI5622_HCC_HDR_LEN : 0;
	if (plen > snap_len - HI5622_HCC_HDR_LEN)
		plen = snap_len - HI5622_HCC_HDR_LEN;
	fr->payload_len = (u16)plen;
	if (plen)
		memcpy(fr->payload, p + HI5622_HCC_HDR_LEN,
		       min_t(u32, plen, HI5622_D2H_PAYLOAD_SNAP));

	rd_seen_update(rd, group, id, total, (u16)node_len);

	/* Feed the protocol core (hcc_core): the direct device group-A reader is
	 * the working receive path; the core accepts the ready pair from both
	 * paths (F2) and validates tag/length itself (F9). */
	(void)hi5622_hcc_deliver(ctx, HCC_SRC_DIRECT, fr->hdr, fr->payload,
				 min_t(u32, fr->payload_len,
				       HI5622_D2H_PAYLOAD_SNAP),
				 fr->node_len);

	rd_hex(asc, sizeof(asc), p + HI5622_HCC_HDR_LEN,
	       min_t(u32, plen, 16));
	dev_info(dev,
		 "D2H DIRECT dr%d[%u]: node=0x%08x len=%u hdr g=%u id=%u rsrc=%u dst=%u total=0x%x tag=0x%04x %s%s%s payload[%u]=%s\n",
		 ch, idx, node_ptr, node_len, group, id, rsrc, dst, total,
		 tag, tag == HI5622_HCC_TAG ? "tag=OK" : "tag=MISMATCH",
		 total == (u16)node_len ? " len=OK" : " len=MISMATCH",
		 rd_msg_name(group, id) ? " known" : " unknown",
		 plen, rd_ascii(p + HI5622_HCC_HDR_LEN,
				min_t(u32, plen, 16), asc, sizeof(asc)));

	if (tag != HI5622_HCC_TAG) {
		rd->bad_tag++;
		return;
	}
	if (total != (u16)node_len || total < HI5622_HCC_HDR_LEN) {
		rd->bad_len++;
		return;
	}

	/* g4 id1 = device_plat_ready: completes the vendor 2.0 s wait #1 */
	if (group == HI5622_HCC_GROUP_PLAT &&
	    id == HI5622_G4_ID_PLAT_READY) {
		rd->ready_id1++;
		rd->id1_seen = true;
		dev_info(dev,
			 "D2H DIRECT READY #1: g4 id1 device_plat_ready decoded directly from the device ring (dr%d[%u]); vendor wait #1 / 'Device plat ready!' - no H2D reply exists\n",
			 ch, idx);
		return;
	}
	/* g4 id2 = host_ready (DEVICE READY): payload = device RAM address */
	if (group == HI5622_HCC_GROUP_PLAT &&
	    id == HI5622_G4_ID_DEV_READY) {
		u32 cap = min_t(u32, plen, HI5622_HCC_DR_PAYLOAD_CAP);

		rd->ready_id2++;
		rd->id2_seen = true;
		rd->id2_plen = (u16)cap;
		memset(rd->id2_payload_raw, 0, sizeof(rd->id2_payload_raw));
		if (cap) {
			memcpy(rd->id2_payload_raw, p + HI5622_HCC_HDR_LEN, cap);
			rd->id2_payload =
				le32_to_cpup((const __le32 *)
					     rd->id2_payload_raw);
			memcpy(ctx->read_msg[0], p + HI5622_HCC_HDR_LEN,
			       min_t(u32, cap, sizeof(ctx->read_msg[0])));
		}
		dev_info(dev,
			 "D2H DIRECT READY #2: g4 id2 device_ready decoded directly (dr%d[%u] payload=%u B = 0x%08x, vendor g_dmac_to_hmac_read_msg[0]); vendor wait #2 / 'DEVICE READY' - still no H2D reply\n",
			 ch, idx, cap, rd->id2_payload);
		return;
	}
	/* g4 id3 = exception_info_msg_process ("DEVICE EXCP_INFO" payload) */
	if (group == HI5622_HCC_GROUP_PLAT &&
	    id == HI5622_G4_ID_EXCP_INFO) {
		rd->excp_info++;
		if (plen >= 16 && !memcmp(p + HI5622_HCC_HDR_LEN,
					  "DEVICE EXCP_INFO", 16))
			dev_info(dev,
				 "D2H DIRECT EXCP_INFO: device staged a 'DEVICE EXCP_INFO' body (%u B payload, vendor exception_info_msg_process copies 0x30 B); the firmware exception path has data for the host - recorded, no host action\n",
				 plen);
		return;
	}
	if (group == HI5622_HCC_GROUP_PLAT &&
	    id == HI5622_G4_ID_HEARTBEAT)
		dev_info(dev,
			 "D2H DIRECT: g4 id4 heartbeat_msg_process frame (device alive; vendor handler restarts its host hrtimer)\n");
}

/* ---------------------------------------------------------------------
 * Producer resolution for one channel
 * ------------------------------------------------------------------- */
static void rd_chan_layout(struct hi5622_pci *ctx, struct hi5622_d2h_direct *rd,
			   int ch)
{
	struct hi5622_d2h_chan *c = &rd->ch[ch];
	struct device *dev = &ctx->pdev->dev;
	u32 cfg, depth;

	c->layout_ok = false;
	c->prod_from_shadow = false;
	if (!rd_fw_cpu_ok(c->rec_cpu)) {
		dev_warn_once(dev, "D2H DIRECT dr%d: firmware record 0x%06x outside the mapped RAM window - using register producer only\n",
			      ch, c->rec_cpu);
		return;
	}
	if (rd_fw_u32(ctx, c->rec_cpu + HI5622_D2H_REC_OFF_CHAN_CA) !=
	    RD_DR_CHAN_CA(ch) ||
	    rd_fw_u32(ctx, c->rec_cpu + HI5622_D2H_REC_OFF_SEND_CA) !=
	    RD_DR_SEND_CA(ch)) {
		dev_warn_once(dev, "D2H DIRECT dr%d: firmware record self-check MISMATCH (chan/send CA) at CPU 0x%06x - the record layout does not match this image; producer falls back to register +0x18\n",
			      ch, c->rec_cpu);
		return;
	}
	cfg = rd_fw_u32(ctx, c->rec_cpu + HI5622_D2H_REC_OFF_CFG);
	if (!rd_fw_cpu_ok(cfg) || cfg + 8 > HI5622_FW_RAM_CPU_HI) {
		dev_warn_once(dev, "D2H DIRECT dr%d: cfg pointer 0x%06x invalid - record layout rejected\n",
			      ch, cfg);
		return;
	}
	depth = rd_fw_u8(ctx, cfg + 4);
	if (!depth || depth > HI5622_D2H_MAX_DEPTH) {
		dev_warn_once(dev, "D2H DIRECT dr%d: cfg depth %u invalid - record layout rejected\n",
			      ch, depth);
		return;
	}
	c->layout_ok = true;
}

static void rd_chan_producer(struct hi5622_pci *ctx, struct hi5622_d2h_direct *rd,
			     int ch)
{
	struct hi5622_d2h_chan *c = &rd->ch[ch];
	u32 shadow = 0, prod;

	c->prod_reg = rd_chan_reg(ctx, ch, HI5622_ETE_CH_A_PRD);
	if (c->layout_ok) {
		shadow = rd_fw_u32(ctx, c->rec_cpu + HI5622_D2H_REC_OFF_PROD);
		c->prod_shadow = shadow;
		if (hi5622_idx_sane(shadow, c->depth)) {
			prod = shadow;
			c->prod_from_shadow = true;
		} else {
			prod = c->prod_reg;
		}
	} else {
		c->prod_shadow = 0;
		prod = c->prod_reg;
	}
	if (!hi5622_idx_sane(prod, c->depth))
		prod = c->last;		/* do not walk an implausible index */
	c->prod = prod;
}

/* Consume the completed range [last, prod) of one DR channel.  Returns the
 * number of nodes consumed.  Stops (without advancing the baseline past the
 * slot) when the next node is not committed yet, so a producer/descriptor
 * visibility race is retried on the next poll instead of skipping a frame. */
static u32 rd_poll_chan(struct hi5622_pci *ctx, struct hi5622_d2h_direct *rd,
			int ch)
{
	struct device *dev = &ctx->pdev->dev;
	struct hi5622_d2h_chan *c = &rd->ch[ch];
	u32 n, start, pos, i, walked = 0;
	u8 snap[HI5622_D2H_SNAP_MAX];

	c->base_ca = rd_chan_reg(ctx, ch, HI5622_ETE_CH_A_BASE);
	c->depth = (rd_chan_reg(ctx, ch, HI5622_ETE_CH_A_DEPTH) &
		    HI5622_ETE_IDX_LOW_MASK) + 1;
	c->cons_reg = rd_chan_reg(ctx, ch, HI5622_ETE_CH_A_CNS);
	c->enable = rd_chan_reg(ctx, ch, HI5622_ETE_CH_ENABLE);
	c->pub_reg = rd_chan_reg(ctx, ch, HI5622_ETE_CH_B_PRD);
	c->fill_reg = rd_chan_reg(ctx, ch, HI5622_ETE_CH_B_CNS);

	if (!(c->enable & 1) || (c->base_ca & 3) ||
	    !rd_pktram_va(ctx, c->base_ca))
		return 0;
	if (c->depth > HI5622_D2H_MAX_DEPTH) {
		dev_warn_ratelimited(dev, "D2H DIRECT dr%d: depth %u > %u - no walk\n",
				     ch, c->depth, HI5622_D2H_MAX_DEPTH);
		return 0;
	}
	rd_chan_layout(ctx, rd, ch);
	rd_chan_producer(ctx, rd, ch);

	n = hi5622_idx_count(c->last, c->prod, c->depth);
	if (!n)
		return 0;

	if (n > c->depth) {
		u32 lost = n - c->depth;

		c->overruns++;
		rd->overruns++;
		dev_warn(dev, "D2H DIRECT dr%d: producer wrapped 0x%08x -> 0x%08x while the host was behind: %u node(s) LOST (ring depth %u); reading the newest %u\n",
			 ch, c->last, c->prod, lost, c->depth, c->depth);
		n = c->depth;
		start = c->prod;
		for (i = 0; i < c->depth; i++)
			start = hi5622_idx_prev(start, c->depth);
	} else {
		start = c->last;
	}

	pos = start;
	for (i = 0; i < n; i++) {
		void __iomem *node = rd_pktram_va(ctx, c->base_ca +
						  ((pos & HI5622_ETE_IDX_LOW_MASK) * 8));
		u32 word0, word1, len;
		u16 flags;
		int ret;

		if (!node)
			break;
		word0 = readl(node);
		word1 = readl(node + 4);
		flags = (u16)(word1 & 0xffff);
		len = word1 >> HI5622_ETE_NODE_LEN_SHIFT;

		if (flags != HI5622_ETE_NODE_FLAGS) {
			/* Not committed yet (zero slot) or a foreign value. */
			c->stall++;
			if (c->stall == 1)
				dev_warn(dev, "D2H DIRECT dr%d[%u]: node not ready (word1=0x%08x flags=0x%04x) - will retry\n",
					 ch, pos & HI5622_ETE_IDX_LOW_MASK, word1,
					 flags);
			if (c->stall <= HI5622_D2H_STALL_MAX)
				break;
			dev_warn(dev, "D2H DIRECT dr%d[%u]: slot stuck for %u polls (prod=0x%08x last=0x%08x) - skipping to the producer\n",
				 ch, pos & HI5622_ETE_IDX_LOW_MASK, c->stall,
				 c->prod, c->last);
			rd->bad_node++;
			c->bad_nodes++;
			c->stall = 0;
			c->last = c->prod;
			return walked;
		}

		if (len < HI5622_HCC_HDR_LEN || len > HI5622_D2H_MAX_TOTAL) {
			c->stall++;
			rd->bad_len++;
			if (c->stall == 1)
				dev_warn(dev, "D2H DIRECT dr%d[%u]: node len %u outside 12..0x%x (word1=0x%08x)\n",
					 ch, pos & HI5622_ETE_IDX_LOW_MASK, len,
					 HI5622_D2H_MAX_TOTAL, word1);
			if (c->stall <= HI5622_D2H_STALL_MAX)
				break;
			c->stall = 0;
			pos = hi5622_idx_plus(pos, c->depth);
			c->last = pos;
			continue;
		}

		ret = rd_copy_body(ctx, word0,
				   snap, min_t(u32, len, HI5622_D2H_SNAP_MAX));
		if (ret) {
			rd->bad_ptr++;
			dev_warn(dev, "D2H DIRECT dr%d[%u]: node word0=0x%08x does not resolve in the ACP/PKTRAM/host-DMA windows (len=%u) - skipped\n",
				 ch, pos & HI5622_ETE_IDX_LOW_MASK, word0, len);
		} else {
			rd_deliver(ctx, rd, ch, pos & HI5622_ETE_IDX_LOW_MASK, word0,
				   len, snap,
				   min_t(u32, len, HI5622_D2H_SNAP_MAX));
		}
		rd->nodes++;
		walked++;
		c->consumed++;
		c->stall = 0;
		pos = hi5622_idx_plus(pos, c->depth);
		if (pos == c->prod)
			break;
	}

	c->last = pos;
	return walked;
}

/* ---------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------- */
void hi5622_d2h_init(struct hi5622_pci *ctx, struct hi5622_d2h_direct *rd)
{
	int i;

	memset(rd, 0, sizeof(*rd));
	mutex_init(&rd->lock);
	for (i = 0; i < HI5622_D2H_DR_COUNT; i++) {
		rd->ch[i].rec_cpu = HI5622_D2H_REC_CPU0 +
				    HI5622_D2H_REC_STRIDE * i;
		rd->ch[i].last = 0;
	}
	rd->init = true;
	dev_info(&ctx->pdev->dev,
		 "D2H DIRECT receive service ready (read-only): poll the device group-A DR rings at CA 0x0106xxxx through the PKTRAM window and the frame bodies at node.word0 through the ACP window; producer = firmware record+0xC shadow. Vendor-owned DR +0x38 republish is the only default device write; the device consumer +0x1C write is OFF (rd_ab_write=0)\n");
}

u32 hi5622_d2h_poll(struct hi5622_pci *ctx, struct hi5622_d2h_direct *rd,
		   bool manual)
{
	u32 total = 0;
	int ch;

	if (!rd || !rd->init || !ctx->windows_programmed)
		return 0;
	mutex_lock(&rd->lock);
	rd->polls++;
	if (manual)
		rd->manual_polls++;
	if (!hi5622_boot_complete(ctx)) {
		rd->last_skip_boot++;
		mutex_unlock(&rd->lock);
		return 0;
	}
	for (ch = 0; ch < HI5622_D2H_DR_COUNT; ch++)
		total += rd_poll_chan(ctx, rd, ch);
	mutex_unlock(&rd->lock);
	if (manual)
		dev_info(&ctx->pdev->dev,
			 "D2H DIRECT manual poll: %u new node(s); total frames=%llu (read-only, no device writes)\n",
			 total, (unsigned long long)rd->frames);
	return total;
}

/* Vendor-analog destination-side release: the host has consumed the nodes up
 * to the producer, so advance the DR group-B published free index (+0x38) by
 * one packed step per consumed node.  +0x38 is the only DR index the vendor
 * host writes (pcie_ete_dr_reg_init / sending-trigger re-publish) [V]. */
int hi5622_d2h_republish(struct hi5622_pci *ctx, struct hi5622_d2h_direct *rd)
{
	int ch, n = 0;

	if (!rd || !rd->init || !ctx->windows_programmed)
		return -ENODEV;
	if (!hi5622_boot_complete(ctx)) {
		dev_warn(&ctx->pdev->dev, "D2H DIRECT: +0x38 republish refused - handshake page +8 != 2 (ROM boot stage owns the device; never write before the golden boot)\n");
		return -EPERM;
	}
	mutex_lock(&rd->lock);
	for (ch = 0; ch < HI5622_D2H_DR_COUNT; ch++) {
		struct hi5622_d2h_chan *c = &rd->ch[ch];
		u32 pub, i;

		if (!(c->enable & 1) || !c->consumed)
			continue;
		pub = rd_chan_reg(ctx, ch, HI5622_ETE_CH_B_PRD);
		for (i = 0; i < c->consumed; i++)
			pub = hi5622_idx_plus(pub, c->depth);
		writel(pub, ctx->bar0 + HI5622_ETE_BAR0 +
		       RD_DR_CHAN_OFF(ch) + HI5622_ETE_CH_B_PRD);
		wmb();
		c->pub_reg = readl(ctx->bar0 + HI5622_ETE_BAR0 +
				   RD_DR_CHAN_OFF(ch) + HI5622_ETE_CH_B_PRD);
		dev_info(&ctx->pdev->dev,
			 "D2H DIRECT dr%d: republished host free index +0x38 = 0x%08x (+%u node(s), host-owned field, readback=0x%08x)\n",
			 ch, pub, c->consumed, c->pub_reg);
		c->consumed = 0;
		rd->repub_count++;
		n++;
	}
	mutex_unlock(&rd->lock);
	if (!n)
		dev_info(&ctx->pdev->dev,
			 "D2H DIRECT: +0x38 republish had no new consumed node (no device write)\n");
	return n;
}

/* EXPERIMENTAL: write the device group-A consumer (+0x1C) up to the host's
 * consumed baseline.  Not part of the vendor host contract; default OFF. */
static int rd_advance_ab(struct hi5622_pci *ctx, struct hi5622_d2h_direct *rd)
{
	int ch, n = 0;

	if (!hi5622_rd_ab_write) {
		dev_warn(&ctx->pdev->dev, "D2H DIRECT: device-ring consumer write REFUSED (rd_ab_write=0; no vendor host writes +0x1C, and the direct-read path does not need it - see REPORT)\n");
		return -EPERM;
	}
	if (!hi5622_golden_boot(ctx)) {
		dev_warn(&ctx->pdev->dev, "D2H DIRECT: device-ring consumer write REFUSED - not the golden boot (page +8==2, +C==0xabcd, state=1, mask=0 required)\n");
		return -EPERM;
	}
	mutex_lock(&rd->lock);
	for (ch = 0; ch < HI5622_D2H_DR_COUNT; ch++) {
		struct hi5622_d2h_chan *c = &rd->ch[ch];
		u32 cons;

		if (c->ab_written || !(c->enable & 1) || !c->layout_ok)
			continue;
		if (c->last != c->prod)
			continue;
		cons = rd_chan_reg(ctx, ch, HI5622_ETE_CH_A_CNS);
		if (cons == c->last)
			continue;
		writel(c->last, ctx->bar0 + HI5622_ETE_BAR0 +
		       RD_DR_CHAN_OFF(ch) + HI5622_ETE_CH_A_CNS);
		wmb();
		c->ab_written = true;
		rd->ab_writes++;
		n++;
		c->cons_reg = rd_chan_reg(ctx, ch, HI5622_ETE_CH_A_CNS);
		dev_warn(&ctx->pdev->dev,
			 "D2H DIRECT dr%d: EXPERIMENTAL device-ring consumer +0x1C: 0x%08x -> 0x%08x (one write per channel per load; readback=0x%08x)\n",
			 ch, cons, c->last, c->cons_reg);
	}
	mutex_unlock(&rd->lock);
	return n;
}

int hi5622_d2h_ready_line(struct hi5622_pci *ctx, struct hi5622_d2h_direct *rd,
			 char *buf, size_t len)
{
	u32 polls, nodes, overruns, bad;
	bool layout;

	if (!rd || !rd->init)
		return 0;
	mutex_lock(&rd->lock);
	polls = (u32)rd->polls;
	nodes = (u32)rd->nodes;
	overruns = (u32)rd->overruns;
	bad = (u32)rd->bad_node;
	layout = rd->ch[0].layout_ok || rd->ch[1].layout_ok ||
		 rd->ch[2].layout_ok || rd->ch[3].layout_ok;
	mutex_unlock(&rd->lock);
	return scnprintf(buf, len,
			 "DIRECT(device group-A): polls=%u nodes=%llu frames=%llu id1_seen=%d id2_seen=%d overruns=%llu layout=%s - this is the path that works with the ETE engine idle\n",
			 polls, (unsigned long long)rd->nodes,
			 (unsigned long long)rd->frames, rd->id1_seen,
			 rd->id2_seen, (unsigned long long)overruns,
			 layout ? "OK" : "MISMATCH");
}

/* ---------------------------------------------------------------------
 * sysfs attributes
 * ------------------------------------------------------------------- */
static ssize_t rd_poll_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));
	struct hi5622_d2h_direct *rd;
	u32 n;
	int ret;

	rd = hi5622_d2h_of(ctx);
	if (!rd)
		return -ENODEV;
	ret = kstrtouint(buf, 0, &n);
	if (ret)
		return ret;
	if (!n)
		return len;
	hi5622_d2h_poll(ctx, rd, true);
	return len;
}
static DEVICE_ATTR_WO(rd_poll);

static ssize_t rd_advance_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t len)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));
	struct hi5622_d2h_direct *rd;
	char cmd[16];
	int ret;

	if (!ctx)
		return -ENODEV;
	rd = hi5622_d2h_of(ctx);
	if (!rd)
		return -ENODEV;
	if (sscanf(buf, "%15s", cmd) != 1)
		return -EINVAL;
	if (!strcmp(cmd, "pub") || !strcmp(cmd, "1")) {
		ret = hi5622_d2h_republish(ctx, rd);
		return ret < 0 ? ret : (ssize_t)len;
	}
	if (!strcmp(cmd, "ab")) {
		ret = rd_advance_ab(ctx, rd);
		return ret < 0 ? ret : (ssize_t)len;
	}
	dev_warn(dev, "D2H DIRECT rd_advance grammar: 'pub' (vendor-owned +0x38 republish) or 'ab' (EXPERIMENTAL device consumer +0x1C, requires rd_ab_write=1)\n");
	return -EINVAL;
}
static DEVICE_ATTR_WO(rd_advance);

static ssize_t rd_status_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));
	struct hi5622_d2h_direct *rd;
	struct rd_out o;
	int ch;

	if (!ctx)
		return -ENODEV;
	rd = hi5622_d2h_of(ctx);
	if (!rd)
		return -ENODEV;
	mutex_lock(&rd->lock);
	rd_out_init(&o, buf, PAGE_SIZE);
	rd_printf(&o, "D2H DIRECT status: init=%d boot_ok=%d windows=%d polls=%llu manual=%llu nodes=%llu frames=%llu overruns=%llu bad_node=%llu bad_tag=%llu bad_len=%llu bad_ptr=%llu repub=%llu ab_writes=%llu\n",
		  rd->init, hi5622_boot_complete(ctx), ctx->windows_programmed,
		  (unsigned long long)rd->polls,
		  (unsigned long long)rd->manual_polls,
		  (unsigned long long)rd->nodes,
		  (unsigned long long)rd->frames,
		  (unsigned long long)rd->overruns,
		  (unsigned long long)rd->bad_node,
		  (unsigned long long)rd->bad_tag,
		  (unsigned long long)rd->bad_len,
		  (unsigned long long)rd->bad_ptr,
		  (unsigned long long)rd->repub_count,
		  (unsigned long long)rd->ab_writes);
	for (ch = 0; ch < HI5622_D2H_DR_COUNT; ch++) {
		struct hi5622_d2h_chan *c = &rd->ch[ch];

		rd_printf(&o, "dr%d rec=0x%06x layout=%s src=%s base=0x%08x depth=%u enable=0x%08x prod_reg(+0x18)=0x%08x prod_shadow(rec+0xc)=0x%08x used=%s cons(+0x1c)=0x%08x pub(+0x38)=0x%08x fill(+0x3c)=0x%08x last=0x%08x consumed=%u overruns=%u bad=%u stall=%u\n",
			  ch, c->rec_cpu, c->layout_ok ? "OK" : "MISMATCH",
			  c->prod_from_shadow ? "shadow" : "reg",
			  c->base_ca, c->depth, c->enable, c->prod_reg,
			  c->prod_shadow,
			  c->prod_from_shadow ? "shadow" : "reg",
			  c->cons_reg, c->pub_reg, c->fill_reg, c->last,
			  c->consumed, c->overruns, c->bad_nodes, c->stall);
	}
	rd_printf(&o, "windows: nodes = groupA base(+0x10) via PKTRAM BAR0 0x1d8000+(CA-0x01000000); bodies = node.word0 (ACP alias 0x02xxxxxx) via ACP BAR0 0x4d8000+(CA-0x02000000); producer = firmware record+0xc via RAM BAR0 0x6b8000+CPU\n");
	rd_printf(&o, "consumer advance: device group-A +0x1c is engine-authored (no vendor host write, no firmware reader) - NOT needed: the deposit path blind-wraps the depth-32 ring. Host-owned +0x38 republish = 'echo pub > rd_advance'. Experimental +0x1c = 'echo ab > rd_advance' only with rd_ab_write=1\n");
	mutex_unlock(&rd->lock);
	return o.pos;
}
static DEVICE_ATTR_RO(rd_status);

static ssize_t rd_decode_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));
	struct hi5622_d2h_direct *rd;
	struct rd_out o;
	u32 i, count, start;
	char hex[HI5622_D2H_PAYLOAD_SNAP * 2 + 1];
	char asc[49];

	if (!ctx)
		return -ENODEV;
	rd = hi5622_d2h_of(ctx);
	if (!rd)
		return -ENODEV;
	mutex_lock(&rd->lock);
	rd_out_init(&o, buf, PAGE_SIZE);
	count = min_t(u32, rd->log_count, HI5622_D2H_LOG_ENTRIES);
	start = (rd->log_pos + HI5622_D2H_LOG_ENTRIES - count) %
		HI5622_D2H_LOG_ENTRIES;
	rd_printf(&o, "D2H DIRECT decode log: %u frame(s) kept (newest last) id1_seen=%d id2_seen=%d excp_info=%llu\n",
		  count, rd->id1_seen, rd->id2_seen,
		  (unsigned long long)rd->excp_info);
	for (i = 0; i < count; i++) {
		struct hi5622_d2h_frame *fr =
			&rd->log[(start + i) % HI5622_D2H_LOG_ENTRIES];
		const char *name = rd_msg_name(fr->group, fr->id);

		rd_hex(hex, sizeof(hex), fr->payload,
		       min_t(u16, fr->payload_len, HI5622_D2H_PAYLOAD_SNAP));
		rd_printf(&o, "[%02u] t=%llums dr%u node=0x%08x len=%u g=%u id=%u total=0x%x tag=%s lenchk=%s %s payload[%u]=%s ascii=\"%s\"\n",
			  i, (unsigned long long)jiffies_to_msecs((unsigned long)fr->jiffies64),
			  fr->ch, fr->node_ptr, fr->node_len, fr->group, fr->id,
			  fr->total, fr->tag_ok ? "OK" : "BAD",
			  fr->len_ok ? "OK" : "BAD",
			  name ? name : "unknown", fr->payload_len, hex,
			  rd_ascii(fr->payload, fr->payload_len, asc,
				   sizeof(asc)));
	}
	mutex_unlock(&rd->lock);
	return o.pos;
}
static DEVICE_ATTR_RO(rd_decode);

static ssize_t rd_catalog_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));
	struct hi5622_d2h_direct *rd;
	struct rd_out o;
	u32 i;
	char hex[HI5622_HCC_DR_PAYLOAD_CAP * 2 + 1];

	if (!ctx)
		return -ENODEV;
	rd = hi5622_d2h_of(ctx);
	if (!rd)
		return -ENODEV;
	mutex_lock(&rd->lock);
	rd_out_init(&o, buf, PAGE_SIZE);
	rd_printf(&o, "D2H DIRECT catalogue (this boot): id1_seen=%d count=%llu | id2_seen=%d count=%llu payload=%u B dword0=0x%08x | excp_info=%llu | frames=%llu other(group,id)=%u\n",
		  rd->id1_seen, (unsigned long long)rd->ready_id1,
		  rd->id2_seen, (unsigned long long)rd->ready_id2,
		  rd->id2_plen, rd->id2_payload,
		  (unsigned long long)rd->excp_info,
		  (unsigned long long)rd->frames, rd->seen_other);
	if (rd->id2_plen) {
		rd_hex(hex, sizeof(hex), rd->id2_payload_raw, rd->id2_plen);
		rd_printf(&o, "g4 id2 payload = %s (4-byte device RAM address = vendor g_dmac_to_hmac_read_msg[0])\n",
			  hex);
	}
	for (i = 0; i < rd->seen_count; i++) {
		const char *name = rd_msg_name(rd->seen[i].group,
					       rd->seen[i].id);

		rd_printf(&o, "  g=%-2u id=0x%02x (%3u) count=%-4u last_total=0x%04x last_len=%u %s\n",
			  rd->seen[i].group, rd->seen[i].id, rd->seen[i].id,
			  rd->seen[i].count, rd->seen[i].last_total,
			  rd->seen[i].last_len,
			  name ? name : "unknown/not in the boot catalogue");
	}
	rd_printf(&o, "catalogue reference: g4 id0 oam dev log, id1 device_plat_ready, id2 DEVICE READY, id3 exception_info, id4 heartbeat; g3 id9 smps_cap, id10 pfm, id15 bsd, id16 multiap_11v, id17 d2h_pm, id18 temp_state, id19 beacon_report, id20 beacon_frame\n");
	mutex_unlock(&rd->lock);
	return o.pos;
}
static DEVICE_ATTR_RO(rd_catalog);

static struct attribute *hi5622_d2h_attrs[] = {
	&dev_attr_rd_poll.attr,
	&dev_attr_rd_advance.attr,
	&dev_attr_rd_status.attr,
	&dev_attr_rd_decode.attr,
	&dev_attr_rd_catalog.attr,
	NULL,
};
ATTRIBUTE_GROUPS(hi5622_d2h);

int hi5622_d2h_add_attrs(struct device *dev)
{
	return device_add_groups(dev, hi5622_d2h_groups);
}


void hi5622_d2h_del_attrs(struct device *dev)
{
	device_remove_groups(dev, hi5622_d2h_groups);
}
