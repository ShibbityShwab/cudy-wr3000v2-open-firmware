// SPDX-License-Identifier: GPL-2.0
/*
 * hcc.c - protocol-core glue for the Hi5622V100 clean-room transport driver.
 *
 * ORIGINAL clean-room code.  The post-release protocol state machine is the
 * portable hcc_core (baked into this tree; see hcc_core.h for the full
 * evidence ledger and the F1-F9 fix list).  This file owns only:
 *   - core initialisation (policy defaults: PM off, heartbeat off, CEED repair
 *     off, evidence-driven CECE) and the release latch hook;
 *   - the per-tick service call;
 *   - delivery of decoded device->host frames from BOTH receive paths into the
 *     core (the direct device group-A reader is the working path);
 *   - the H2D liveness carrier: frame grammar -> hcc_core_frame_build ->
 *     SR0 node placement -> hcc_core_sr_commit (packed producer + coalesced
 *     mailbox id-3 announce through the core's allowlisted write path);
 *   - the sysfs write attributes for the bounded H2D probe, the bounded CECE
 *     kick (mode 2 only) and the vendor-owned DR +0x38 republish.
 *
 * SAFETY: every device write is performed by the core's hcc_write() allowlist
 * (out[0]/out[1]/out[2]/out[3]/out[4]/gate only; out[5] and the transport's
 * release word are not writable).  probe performs no device writes.
 */
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/device.h>
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

int hi5622_hcc_init(struct hi5622_pci *ctx)
{
	struct hi5622_hcc_service *h = ctx->hcc;
	struct hcc_core_config cfg;

	hcc_core_config_default(&cfg);
	cfg.pm_enabled = false;		/* vendor PM layer is not on the boot path */
	cfg.heartbeat_armed = false;	/* vendor boot path leaves it off */
	cfg.allow_ceed_repair = false;	/* GP8 golden boot needs no repair */
	cfg.mbox_clear_timeout_ms = 1000;
	hcc_kernel_init(&h->hcc, &ctx->pdev->dev, ctx->bar0, &cfg);

	/* CECE policy parameter maps 1:1 to the core enum (0 evidence, 1 off,
	 * 2 evidence + bounded manual kick). */
	h->hcc.core.cece_mode = hi5622_cece_mode <= 2 ?
		(enum hcc_cece_mode)hi5622_cece_mode :
		HCC_CECE_MODE_EVIDENCE;

	dev_info(&ctx->pdev->dev,
		 "HCC core: portable hcc_core initialised (latched boot gate, F1-F9 fixes; cece_mode=%u, PM off, heartbeat off); boot_release=latch-on-0x5a5a\n",
		 (unsigned)h->hcc.core.cece_mode);
	return 0;
}

void hi5622_hcc_note_release(struct hi5622_pci *ctx)
{
	if (!ctx->hcc)
		return;
	hcc_kernel_release(&ctx->hcc->hcc);
	dev_info(&ctx->pdev->dev,
		 "HCC core: transport release recorded - boot watch armed (page CA 0x%08x +8 == 2 is the LATCHED gate)\n",
		 HI5622_HANDSHAKE_CPU);
}

void hi5622_hcc_tick(struct hi5622_pci *ctx)
{
	if (!ctx->hcc)
		return;
	hcc_kernel_tick(&ctx->hcc->hcc);
}

/* Deliver one decoded device->host frame into the core state machine. */
int hi5622_hcc_deliver(struct hi5622_pci *ctx, enum hcc_src src,
		       const u8 *hdr, const u8 *payload, u32 payload_avail,
		       u32 wire_len)
{
	if (!ctx->hcc)
		return -ENODEV;
	return hcc_core_deliver_hdr(&ctx->hcc->hcc.core, src, hdr, payload,
				    payload_avail, wire_len,
				    hcc_kernel_now_ms());
}

/* =====================================================================
 * H2D liveness carrier: sysfs grammar -> core frame build -> SR0 commit
 * ===================================================================*/
static int hi5622_parse_uint(const char **pp, unsigned int *out,
			     unsigned int max)
{
	const char *p = *pp;
	unsigned int v = 0, base = 10;
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

static int hi5622_parse_hex_payload(const char **pp, u8 *dst, size_t *plen)
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

static bool hi5622_rest_is_space(const char *p)
{
	while (*p) {
		if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
			return false;
		p++;
	}
	return true;
}

/* Parse the sysfs grammar.  "1" (with optional trailing whitespace, i.e. what
 * "echo 1" writes) selects the vendor payload-free stub.  The header itself is
 * always built by hcc_core_frame_build (tag/length guaranteed; id 5 and
 * group-4 H2D refused there). */
static int hi5622_parse_sr_request(const char *buf, unsigned int *group,
				   unsigned int *id, unsigned int *dst,
				   u8 *payload, size_t *plen)
{
	const char *p = buf;
	bool seen = false;
	int ret;

	*group = HI5622_HCC_SR_GROUP_SAFE;
	*id = HI5622_HCC_SR_ID_SAFE;
	*dst = 0;
	*plen = 0;
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;
	if (!*p)
		return -EINVAL;
	if (*p == '1' && hi5622_rest_is_space(p + 1))
		return 0;
	while (*p) {
		while (*p == ' ' || *p == '\t' || *p == '\n' ||
		       *p == '\r' || *p == ',')
			p++;
		if (!*p)
			break;
		if (!strncmp(p, "group=", 6)) {
			p += 6;
			ret = hi5622_parse_uint(&p, group, 0xffff);
		} else if (!strncmp(p, "id=", 3)) {
			p += 3;
			ret = hi5622_parse_uint(&p, id, 0xffff);
		} else if (!strncmp(p, "dst=", 4)) {
			p += 4;
			ret = hi5622_parse_uint(&p, dst, 0xf);
		} else if (!strncmp(p, "payload=", 8)) {
			p += 8;
			ret = hi5622_parse_hex_payload(&p, payload, plen);
		} else {
			return -EINVAL;
		}
		if (ret)
			return ret;
		seen = true;
	}
	return seen ? 0 : -EINVAL;
}

static int hi5622_hcc_sr_liveness(struct hi5622_pci *ctx, const char *buf)
{
	struct hi5622_hcc_service *h = ctx->hcc;
	struct device *dev = &ctx->pdev->dev;
	struct hcc_core *core = &h->hcc.core;
	struct hi5622_ete_ring *r = &h->sr[0].ete;
	struct hi5622_ete_node *nodes = r->node_arr;
	unsigned int group, id, dst, polls;
	size_t plen = 0;
	u32 total = 0, producer, slot, newprod, devva;
	u8 *payload;
	u8 *frame;
	u32 out0;
	int ret;

	if (!h->armed) {
		h->sr_announce_refused++;
		dev_err(dev, "HCC H2D: arm first (hcc_arm) - the SR side must be programmed before a frame is posted\n");
		return -EBUSY;
	}
	if (!ctx->cpu_released) {
		h->sr_announce_refused++;
		dev_err(dev, "HCC H2D: refused before the CPU release\n");
		return -EBUSY;
	}
	if (!core->boot_latched) {
		h->sr_announce_refused++;
		dev_warn(dev, "HCC H2D: refused - boot gate not latched yet (page+8 != 2); never post H2D during the ROM boot stage\n");
		return -EBUSY;
	}
	if (h->sr_announce_count >= HI5622_HCC_SR_ANNOUNCE_MAX) {
		h->sr_announce_refused++;
		dev_warn(dev, "HCC H2D: liveness probe already used this module load (1 frame max; no retries in session)\n");
		return -EBUSY;
	}
	if (h->h2d_frames_sent) {
		h->sr_announce_refused++;
		dev_warn(dev, "HCC H2D: refused - a REAL post-ready frame was already committed this load (h2d_txq_sync/h2d_send); liveness stub and real message are one idea: one frame per module load\n");
		return -EBUSY;
	}

	payload = kmalloc(HI5622_HCC_FRAME_MAX_PAYLOAD, GFP_KERNEL);
	frame = kmalloc(HI5622_HCC_FRAME_MAX_TOTAL, GFP_KERNEL);
	if (!payload || !frame) {
		ret = -ENOMEM;
		goto out;
	}
	ret = hi5622_parse_sr_request(buf, &group, &id, &dst, payload, &plen);
	if (ret) {
		h->sr_announce_refused++;
		dev_warn(dev, "HCC H2D: frame request rejected (%d); grammar: '1' or group=0|2 id=N dst=M payload=HEX (core refuses id5/group4)\n",
			 ret);
		goto out;
	}
	ret = hcc_core_frame_build(core, frame, HI5622_HCC_FRAME_MAX_TOTAL,
				   (u8)group, (u16)id, (u8)dst, payload,
				   (u32)plen, &total);
	if (ret) {
		h->sr_announce_refused++;
		dev_warn(dev, "HCC H2D: core refused the frame (%d): only group 0/2, id != 5, payload <= 0x674 hex bytes\n",
			 ret);
		goto out;
	}

	mutex_lock(&h->lock);
	if (!core->released || !core->boot_latched ||
	    core->phase == HCC_PHASE_FAILED) {
		mutex_unlock(&h->lock);
		h->sr_announce_refused++;
		ret = -EPERM;
		goto out;
	}
	producer = core->sr[0].producer;
	slot = hcc_idx_slot(producer);
	if (slot >= r->depth) {
		mutex_unlock(&h->lock);
		h->sr_announce_refused++;
		ret = -EINVAL;
		goto out;
	}
	devva = (u32)r->buf_dma[slot];
	if (devva < HI5622_HOST_DMA_LO || devva > HI5622_HOST_DMA_HI) {
		mutex_unlock(&h->lock);
		h->sr_announce_refused++;
		dev_err(dev, "HCC H2D: SR buffer 0x%08x outside the RC inbound window - refused\n",
			devva);
		ret = -ERANGE;
		goto out;
	}

	/* Vendor SR commit order: node data + word1, barrier, producer
	 * register, then the coalesced mailbox id-3 announce (core). */
	memcpy(r->bufs[slot], frame, total);
	nodes[slot].devva = devva;
	nodes[slot].word1 = hcc_sr_node_word1(total);
	wmb();
	newprod = hcc_idx_plus(producer, r->depth);
	writel(newprod, ctx->bar0 + HI5622_ETE_BAR0 + r->chan_off +
	       HI5622_ETE_CH_A_PRD);
	wmb();
	ret = hcc_core_sr_commit(core, 0, devva, (u8)group, (u16)id, total,
				 hcc_kernel_now_ms());
	mutex_unlock(&h->lock);
	if (ret) {
		h->sr_announce_refused++;
		dev_warn(dev, "HCC H2D: core refused the SR commit (%d)\n", ret);
		goto out;
	}

	/* Bounded observation of the coalesced announce (the core's mbox_tick
	 * owns completion/timeout semantics; this is the bench-facing report). */
	for (polls = 0; polls < 1000; polls++) {
		out0 = readl(ctx->bar0 + hi5622_io_bar0(HI5622_CA_H2D_PENDING));
		if (!(out0 & BIT(HI5622_H2D_MSG_ETE_ANNOUNCE)))
			break;
		udelay(100);
	}
	out0 = readl(ctx->bar0 + hi5622_io_bar0(HI5622_CA_H2D_PENDING));
	h->sr_announce_count++;
	dev_info(dev, "HCC H2D: posted VALID SR frame (group=%u id=%u dst=%u total=%u tag=0x%04x) on SR0 slot %u devva 0x%08x; coalesced announce id%u: out[0]=0x%08x after %u polls; H2D lane state logged by the core (hcc_events)\n",
		 group, id, dst, total, HI5622_HCC_TAG, slot, devva,
		 HI5622_H2D_MSG_ETE_ANNOUNCE, out0, polls);
	if (out0 & BIT(HI5622_H2D_MSG_ETE_ANNOUNCE)) {
		h->sr_announce_timeout++;
		dev_warn(dev, "HCC H2D: announce not consumed yet (out[0] bit3 set); the core will close the H2D lane on its bounded timeout - no retry in session\n");
		ret = -ETIMEDOUT;
	} else {
		h->sr_announce_ok++;
		ret = 0;
	}
out:
	kfree(payload);
	kfree(frame);
	return ret;
}

/* =====================================================================
 * DR published-free-index (+0x38) re-publish experiment (vendor-owned field)
 * ===================================================================*/
int hi5622_hcc_dr_republish(struct hi5622_pci *ctx)
{
	struct hi5622_hcc_service *h = ctx->hcc;
	struct device *dev = &ctx->pdev->dev;
	struct hcc_core *core = &h->hcc.core;
	int ch;

	if (!ctx->windows_programmed || !h->armed)
		return -EBUSY;
	if (!core->boot_latched) {
		dev_warn(dev, "HCC DR re-publish: refused before the boot latch (no speculative writes during boot)\n");
		return -EBUSY;
	}
	for (ch = 0; ch < HI5622_HCC_DR_CH_COUNT; ch++) {
		struct hi5622_ete_ring *r = &h->dr[ch].ete;
		u32 cur = readl(ctx->bar0 + HI5622_ETE_BAR0 + r->chan_off +
				HI5622_ETE_CH_B_PRD);

		writel(cur, ctx->bar0 + HI5622_ETE_BAR0 + r->chan_off +
		       HI5622_ETE_CH_B_PRD);
		wmb();
		dev_info(dev, "HCC DR%d: re-published host free idx (+0x38) = 0x%08x (fill read-only %u)\n",
			 ch, cur,
			 readl(ctx->bar0 + HI5622_ETE_BAR0 + r->chan_off +
			       HI5622_ETE_CH_B_CNS));
	}
	return 0;
}

/* =====================================================================
 * sysfs write attributes (protocol layer)
 * ===================================================================*/
static ssize_t hcc_sr_announce_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));
	int ret;

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	ret = hi5622_hcc_sr_liveness(ctx, buf);
	return ret ? ret : count;
}
static DEVICE_ATTR_WO(hcc_sr_announce);

static ssize_t hcc_cece_kick_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));
	struct hcc_core *core;
	int ret;

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	if (!sysfs_streq(buf, "1"))
		return -EINVAL;
	core = &ctx->hcc->hcc.core;
	ret = hcc_core_cece_kick(core);
	if (ret) {
		dev_warn(dev, "HCC CECE: kick refused (%d): needs cece_mode=2 (hcc_cece_mode=2), boot latched and <3 kicks; the evidence-driven policy is the normal path\n",
			 ret);
		return ret;
	}
	dev_info(dev, "HCC CECE: manual kick %u/3 - 32-bit 0x0000CECE -> CA 0x4000010c (post-latch only)\n",
		 core->cece_kicks);
	return count;
}
static DEVICE_ATTR_WO(hcc_cece_kick);

static ssize_t hcc_dr_republish_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));
	int ret;

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	if (!sysfs_streq(buf, "1"))
		return -EINVAL;
	ret = hi5622_hcc_dr_republish(ctx);
	return ret ? ret : count;
}
static DEVICE_ATTR_WO(hcc_dr_republish);

static struct attribute *hi5622_hcc_attrs[] = {
	&dev_attr_hcc_sr_announce.attr,
	&dev_attr_hcc_cece_kick.attr,
	&dev_attr_hcc_dr_republish.attr,
	NULL,
};
ATTRIBUTE_GROUPS(hi5622_hcc);

int hi5622_hcc_add_attrs(struct device *dev)
{
	return device_add_groups(dev, hi5622_hcc_groups);
}

void hi5622_hcc_del_attrs(struct device *dev)
{
	device_remove_groups(dev, hi5622_hcc_groups);
}
