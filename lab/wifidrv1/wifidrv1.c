// SPDX-License-Identifier: GPL-2.0
/*
 * wifidrv1: wifidrv0 (cfg80211 wiphy + real netdev) PLUS the endpoint.
 *
 * wifidrv0 proved registration and lifecycle: wiphy "omo-drv0" registers, the
 * managed netdev "omowl0" opens/closes, nl80211 add/del works, rmmod cleans
 * up, vendor radios untouched.  Its stated limit was "no hardware, no data
 * path" - this module is the first step past that limit, built entirely from
 * what the earlier phases already proved on this device:
 *
 *   1. claim EP0 exactly as lab/eteprobe does (pci_enable_device,
 *      pci_request_mem_regions, the documented PCI_COMMAND=0x0007 write with
 *      read-back), and REFUSE if the vendor stack still owns the BARs
 *      (rc != 0) - coexistence, never replacement (docs/phase15/bringup.md:
 *      the vendor stack cannot be rmmod'd; the first unload step panics).
 *   2. map BAR0's register windows read-only, using the same constants the
 *      vendor's own driver uses (docs/phase4/mmio-map.md: the 0x40000000
 *      device-CA window is the EP0 register origin; the ETE/glue block the
 *      engine programs is BAR0+0x39000 / +0x3a000, docs/phase17/ete-engine.md
 *      A.4).  Everything here is READ-ONLY: no write to any BAR, no descriptor
 *      submitted, no doorbell rung, no firmware window touched.
 *   3. decode the register block into the driver's own state so the netdev has
 *      something real behind it, and expose what was measured where a data
 *      path will need it:
 *        - SR/DR program registers (base/depth/wptr) -> the ring the engine uses
 *        - the message registers (out[0]/out[1]/out[2]/out[5]) -> the mailbox
 *        - the channel-res register -> per-channel reset/ownership
 *      The values are logged and cached; ndo_start_xmit still drops packets,
 *      because writing the rings without the message service is what phase 22
 *      showed does not work (h2d-accept: the device-side accept gate holds).
 *
 * HARD RULES honoured: never write CA 0x400392f0 (out[5] is only ever READ
 * here, and only by an explicit opt-in param); never read the RC misc window
 * 0x10161000; never rmmod a vendor module; measurement only through the
 * endpoint's BAR0/BAR2.
 *
 * SAFETY: the whole hardware section is gated on param `hw` (default 0 = off),
 * so the default load behaves exactly like wifidrv0 (registration only, no PCI
 * access at all).  With hw=1 the claim is read-mostly and refuses on conflict.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <net/net_namespace.h>
#include <linux/skbuff.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <net/cfg80211.h>

#define OMO_WIPHY_NAME	"omo-drv1"
#define OMO_IFNAME	"omowl1"

/* ---- vendor struct offsets (measured, docs/phase14/wifidrv0.md) --------- */
#define VND_ND_OPS_OFF		0x128u
#define VND_ND_IEEE80211_OFF	0x1f0u
#define VND_ND_PRIV_OFF		0x540u
#define VND_ND_PRIV_SIZE	4096u

/* ---- PCI identity and BARs (docs/phase4/mmio-map.md, phase18) ----------- */
#define OMO_PCI_DEV	PCI_DEVFN(0, 0)
#define OMO_CFG_BAR0	0x10
#define OMO_IATU_BAR	2		/* BAR2 = the iATU viewport window */
#define OMO_IATU_CTRL2	0x104		/* per-viewport control word 2 */
#define OMO_IATU_STRIDE	0x200		/* viewport stride */

/* The six inbound regions the vendor's oal_pcie_set_inbound programs, from
 * docs/phase18/inbound-map.md (measured live).  Region 3 ("IO", dev CA
 * 0x40000000) is the one that makes the ETE block at CA 0x4003a000 appear at
 * BAR0+0x3f2000; without it that window reads 0xffffffff (the same state
 * phase 20 recorded before it programmed the viewports). */
struct omo_region {
	u32 off;		/* host offset within the BAR0 window */
	u32 size;
	u64 target;		/* device chip address */
	const char *name;
};

static const struct omo_region omo_regions[6] = {
	{ 0x000000, 0x1c0000, 0x00000000UL, "ROM_WRAM" },
	{ 0x1c0000, 0x018000, 0x00400000UL, "TCM_NOACP" },
	{ 0x1d8000, 0x1e0000, 0x01000000UL, "PKTRAM_NOACP" },
	{ 0x3b8000, 0x120000, 0x40000000UL, "IO" },
	{ 0x4d8000, 0x1e0000, 0x02000000UL, "ACP" },
	{ 0x6b8000, 0x218000, 0x01200000UL, "ACP-fw" },
};

/* Two DIFFERENT blocks, BOTH inside the region-3 IO window (host
 * 0x403b8000 -> dev CA 0x40000000, size 0x120000; docs/phase18/inbound-map.md
 * row 3).  Because the window base is 0x403b8000, a device CA 0x4003XXXX
 * appears at BAR0 offset 0x3XXXX:
 *
 * MESSAGE block: the six mailbox registers (docs/phase20/runtime-msg.md,
 * "slot | device CA | BAR0 off"):
 *   out[0] 0x40039010 -> 0x3f1010   out[1] 0x40039014 -> 0x3f1014
 *   out[2] 0x400392d4 -> 0x3f12d4   out[5] 0x400392f0 -> 0x3f12f0
 *
 * ETE block: the SR/DR ring program registers, device CA 0x4003a000 (static
 * resource .data+0x2944, docs/phase17/ete-engine.md A.1) -> BAR0 0x3f2000.
 * SR channels at +{0x400,0x450,0x4a0}, DR at +{0x590,0x5e0,0x630,0x680},
 * stride 0x114 / 0x6c.
 *
 * Getting these wrong is detectable in the readback: a mis-addressed window
 * reads 0xffffffff (measured both ways on this device, phase 23b/c). */
/*
 * Two windows, sized to what their users actually touch.
 *
 * MESSAGE window: the six mailbox CAs.  Device CAs 0x40039010 (out[0]),
 * 0x40039014 (out[1]), 0x400392d4 (out[2]), 0x400392e8 (glue chn_res),
 * 0x400392f0 (out[5]) and the ETE status/intr block at 0x40039508
 * (BAR0 0x3f1508).  That range spans 0x3f1010..0x3f1508, so the window is
 * 0x2000 bytes, NOT 0x1000: an earlier revision mapped 0x1000 and the write
 * path faulted at omo_msg+0x1508 (oops: paging request at c9045508,
 * PC omo_wifidrv1_init+0x98c - caught on hardware, phase 23k).
 *
 * ETE window: the SR/DR ring program registers, 0x3f2000 + up to ~0x6e8.
 */
#define OMO_MSG_WIN	0x3f0000UL
#define OMO_MSG_BYTES	0x2000UL	/* covers 0x3f1010..0x3f1508 inclusive-ish */
#define OMO_ETE_WIN	0x3f2000UL
#define OMO_WIN_BYTES	0x1000UL	/* the ETE block: +0x408..+0x6e8 */
/* Region 0 (ROM_WRAM) holds the release register at BAR0+0x3b8108, which is
 * OUTSIDE both windows above.  It gets its own mapping: one page suffices for
 * the single register.  (Writing it through another window would fault exactly
 * like the +0x1508 access did - checked before writing the code this time.) */
#define OMO_REL_WIN	0x3b8000UL
#define OMO_REL_BYTES	0x1000UL

/* message registers, offsets within the message window (0x3f0000 base) */
#define OMO_MSG0	0x010
#define OMO_MSG1	0x014
#define OMO_MSG2	0x2d4
#define OMO_CHN_RES	0x2e8
#define OMO_MSG5	0x2f0

/* ETE ring registers, offsets within the ETE window (SR ch0 base) */
#define ETE_SR0_BASE	0x400
#define ETE_SR_STRIDE	0x114
#define ETE_DR0_BASE	0x590
#define ETE_DR_STRIDE	0x6c
#define ETE_SR_CTRL	0x008
#define ETE_SR_BASEREG	0x010
#define ETE_SR_DEPTH	0x014
#define ETE_SR_WPTR	0x018
#define ETE_SR_RPTR	0x01c
#define ETE_DR_BASEREG	0x030
#define ETE_DR_DEPTH	0x034
#define ETE_DR_WPTR	0x038
#define ETE_DR_RPTR	0x03c

/* ---- parameters --------------------------------------------------------- */
static unsigned int omo_hw;		/* 0 = registration only (safe default) */
module_param_named(hw, omo_hw, uint, 0444);
MODULE_PARM_DESC(hw, "1 = claim EP0, program the inbound viewports, decode (read-only); 0 = no hardware access");

static unsigned int omo_wr_en;
module_param_named(wr, omo_wr_en, uint, 0444);
MODULE_PARM_DESC(wr, "1 = program the ETE rings (the module's first writes to the endpoint); requires hw=1 program=1");

static unsigned int omo_release_en;
module_param_named(release, omo_release_en, uint, 0444);
MODULE_PARM_DESC(release, "1 = write 0x5a5a to CA 0x40000108 (release the Wi-Fi CPU); requires hw=1");

static unsigned int omo_program_regions_en;
module_param_named(program, omo_program_regions_en, uint, 0444);
MODULE_PARM_DESC(program, "1 = program the six inbound iATU viewports (needed for the ETE/IO block to decode); 0 = decode as-is");

static unsigned int omo_domain;
module_param_named(domain, omo_domain, uint, 0444);
MODULE_PARM_DESC(domain, "PCI domain of the 59e7:0005 endpoint (default 0)");

static unsigned int omo_read_msg5;
module_param_named(read_msg5, omo_read_msg5, uint, 0444);
MODULE_PARM_DESC(read_msg5, "1 = also READ out[5] CA 0x400392f0 (never written); default 0");

static unsigned int omo_verbose = 1;
module_param_named(verbose, omo_verbose, uint, 0444);
MODULE_PARM_DESC(verbose, "1 = log each decoded register");

/* ---- state ------------------------------------------------------------- */
static struct wiphy *omo_wiphy;
static struct net_device *omo_netdev;

static struct pci_dev *omo_pdev;
static void __iomem *omo_msg;		/* message/channel window  BAR0+0x3f0000 */
static void __iomem *omo_ete;		/* ETE ring window        BAR0+0x3f2000 */
static void __iomem *omo_rel;		/* region 0 / release reg BAR0+0x3b8000 */static void __iomem *omo_iatu;		/* BAR2: the inbound viewport window */
static resource_size_t omo_bar0_base;

struct omo_ring {
	u32 base;
	u32 depth;
	u32 wptr;
	u32 rptr;
	u32 ctrl;
};

static struct omo_ring omo_sr[3];	/* 3 SR channels, stride 0x114 */
static struct omo_ring omo_dr[4];	/* 4 DR channels, stride 0x6c */
static u32 omo_msg0, omo_msg1, omo_msg2, omo_msg5, omo_chnres;
static bool omo_regs_valid;

static const u8 omo_mac[ETH_ALEN] = { 0x02, 0x00, 0x6f, 0x6d, 0x6f, 0x31 };

/* ---- iATU inbound programming (ported from lab/hccaccept, proven) ------- */
static void omo_iatu_wr(void __iomem *iatu, unsigned long off, u32 val,
			const char *who, const char *what)
{
	iowrite32(val, iatu + off);
	pr_info("omo-drv1: %s %-14s [0x%03lx] <= 0x%08x readback=0x%08x\n",
		who, what, off, val, ioread32(iatu + off));
}

/*
 * Program the six inbound viewports.  This is what makes the region-3 IO block
 * (and therefore the ETE register block at CA 0x4003a000, seen at BAR0
 * +0x3f2000) readable at all: an unprogrammed window reads 0xffffffff.
 * Values follow docs/phase18/inbound-map.md; the write order follows the
 * vendor's oal_pcie_set_inbound (ctrl2=0, ctrl2=enable, base_lo/hi, limit,
 * target_lo/hi) as reproduced in lab/hccaccept.
 */
static int omo_program_inbound(void __iomem *iatu, u64 bar0_base, const char *who)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(omo_regions); i++) {
		const struct omo_region *r = &omo_regions[i];
		u64 base = bar0_base + r->off;
		u64 limit = base + r->size - 1;
		unsigned long c = OMO_IATU_CTRL2 + OMO_IATU_STRIDE * i;

		pr_info("omo-drv1: %s region %u %-12s host 0x%llx..0x%llx -> dev 0x%llx\n",
			who, i, r->name, (unsigned long long)base,
			(unsigned long long)limit, (unsigned long long)r->target);

		omo_iatu_wr(iatu, c, 0, who, "ctrl2=0");
		omo_iatu_wr(iatu, c, 0x80000000U, who, "ctrl2=ena");
		omo_iatu_wr(iatu, c + 4, (u32)base, who, "base_lo");
		omo_iatu_wr(iatu, c + 8, (u32)(base >> 32), who, "base_hi");
		omo_iatu_wr(iatu, c + 12, (u32)limit, who, "limit");
		omo_iatu_wr(iatu, c + 16, (u32)r->target, who, "target_lo");
		omo_iatu_wr(iatu, c + 20, (u32)(r->target >> 32), who, "target_hi");
	}
	return i;
}

/* ---- ETE ring programming (ported from lab/hccaccept, proven) ---------- */
/* forward decl: defined with the other accessors below */
static u32 omo_rd(void __iomem *win, unsigned long off);

/* The two "binding" writes the vendor performs around the ring program:
 * pcie_ete_intr_init maps CA 0x40039508 -> BAR0 0x3f1508 and ANDs it with
 * 0xffe0f8f8, BEFORE the rings; pcie_ete_chn_res touches ONE register in the
 * message/glue block, CA 0x400392e8 -> BAR0 0x3f12e8, AND 0xfffffc20, AFTER
 * them.  (hccaccept's note: the earlier takeover applied that mask to the ETE
 * ring offsets instead and missed the real register.) */
#define OMO_ETE_INTR_OFF	0x1508UL	/* within the message window (0x3f0000) */
#define OMO_ETE_INTR_MASK	0xffe0f8f8U
#define OMO_GLUE_CHN_RES_MASK	0xfffffc20U
#define OMO_ETE_DEPTH		32
#define OMO_ETE_SR_N		3
#define OMO_ETE_DR_N		4

static const unsigned long omo_sr_block[OMO_ETE_SR_N] = { 0x400, 0x450, 0x4a0 };
static const unsigned long omo_dr_block[OMO_ETE_DR_N] = { 0x590, 0x5e0, 0x630, 0x680 };

static void *omo_sr_va[OMO_ETE_SR_N];
static dma_addr_t omo_sr_dma[OMO_ETE_SR_N];
static void *omo_dr_va[OMO_ETE_DR_N];
static dma_addr_t omo_dr_dma[OMO_ETE_DR_N];
static bool omo_rings_ready;
static unsigned int omo_wr_fail;

/* write + readback, the discipline rtmsg/hccaccept used and the spec mandates */
static void omo_wr(void __iomem *win, unsigned long off, u32 val, const char *name)
{
	u32 rb;

	iowrite32(val, win + off);
	rb = ioread32(win + off);
	pr_info("omo-drv1:   %-20s [0x%04lx] <= 0x%08x readback=0x%08x match=%s\n",
		name, off, val, rb, rb == val ? "YES" : "NO");
	if (rb != val)
		omo_wr_fail++;
}

/*
 * devva = devva_base + hostca - hostca_base (pcie_hostca_to_devva @0xaefc).
 * The window values the vendor boot reports are 0x80000000/0x80000000, which is
 * what the device->host iATU viewport 0 was programmed with (phase 20c).
 */
#define OMO_DEVVA_BASE	0x80000000UL
#define OMO_HOSTCA_BASE	0x80000000UL

static u32 omo_hostca_to_devva(dma_addr_t hostca)
{
	u64 h = (u64)hostca;

	if (h < (u64)OMO_HOSTCA_BASE)
		return 0xffffffffU;
	return (u32)((u64)OMO_DEVVA_BASE + (h - (u64)OMO_HOSTCA_BASE));
}

/* Allocate the ring node arrays: SR (depth+2)*8 per channel, DR depth*8. */
static int omo_rings_alloc(void)
{
	unsigned int i;
	size_t sr_sz = (OMO_ETE_DEPTH + 2) * 8;
	size_t dr_sz = OMO_ETE_DEPTH * 8;

	for (i = 0; i < OMO_ETE_SR_N; i++) {
		omo_sr_va[i] = dma_alloc_coherent(&omo_pdev->dev, sr_sz,
						  &omo_sr_dma[i], GFP_KERNEL);
		if (!omo_sr_va[i]) {
			pr_err("omo-drv1: SR ch%u dma_alloc_coherent(%zu) FAILED\n",
			       i, sr_sz);
			return -ENOMEM;
		}
		pr_info("omo-drv1: SR ch%u nodes %zu bytes @ %pad (devva 0x%08x)\n",
			i, sr_sz, &omo_sr_dma[i], omo_hostca_to_devva(omo_sr_dma[i]));
	}
	for (i = 0; i < OMO_ETE_DR_N; i++) {
		omo_dr_va[i] = dma_alloc_coherent(&omo_pdev->dev, dr_sz,
						  &omo_dr_dma[i], GFP_KERNEL);
		if (!omo_dr_va[i]) {
			pr_err("omo-drv1: DR ch%u dma_alloc_coherent(%zu) FAILED\n",
			       i, dr_sz);
			return -ENOMEM;
		}
		pr_info("omo-drv1: DR ch%u nodes %zu bytes @ %pad (devva 0x%08x)\n",
			i, dr_sz, &omo_dr_dma[i], omo_hostca_to_devva(omo_dr_dma[i]));
	}
	omo_rings_ready = true;
	return 0;
}

static void omo_rings_free(void)
{
	unsigned int i;

	if (!omo_rings_ready)
		return;
	for (i = 0; i < OMO_ETE_SR_N; i++) {
		if (omo_sr_va[i]) {
			dma_free_coherent(&omo_pdev->dev, (OMO_ETE_DEPTH + 2) * 8,
					  omo_sr_va[i], omo_sr_dma[i]);
			omo_sr_va[i] = NULL;
		}
	}
	for (i = 0; i < OMO_ETE_DR_N; i++) {
		if (omo_dr_va[i]) {
			dma_free_coherent(&omo_pdev->dev, OMO_ETE_DEPTH * 8,
					  omo_dr_va[i], omo_dr_dma[i]);
			omo_dr_va[i] = NULL;
		}
	}
	omo_rings_ready = false;
}

/*
 * Program the ETE rings - the port's FIRST WRITE to the endpoint.
 * Order and values per the write-path spec (docs/phase23/write-path-spec.md):
 * binding write #1 (intr mask), SR ch0..2, DR ch0..3, binding write #2
 * (glue chn_res RMW).  Readback after every write; a mismatch is counted, not
 * swallowed.  Bounded by design: NO descriptor is submitted and NO doorbell is
 * rung - phase 22 showed submitting without the device-side accept gate does
 * nothing, so this stops at ring ownership.
 */
static void omo_ete_program(void)
{
	unsigned int i;
	u32 v;

	pr_info("omo-drv1: ---- first write path: ETE ring programming ----\n");

	/* binding write #1: pcie_ete_intr_init, CA 0x40039508, before the rings */
	v = omo_rd(omo_msg, OMO_ETE_INTR_OFF);
	pr_info("omo-drv1:   intr pre=0x%08x mask=0x%08x\n", v, OMO_ETE_INTR_MASK);
	omo_wr(omo_msg, OMO_ETE_INTR_OFF, v & OMO_ETE_INTR_MASK, "ETE intr 0x40039508");

	for (i = 0; i < OMO_ETE_SR_N; i++) {
		unsigned long b = omo_sr_block[i];
		u32 devva = omo_hostca_to_devva(omo_sr_dma[i]);
		char t[40];

		scnprintf(t, sizeof(t), "SR ch%u base", i);
		omo_wr(omo_ete, b + ETE_SR_BASEREG, devva, t);
		scnprintf(t, sizeof(t), "SR ch%u depth-1", i);
		omo_wr(omo_ete, b + ETE_SR_DEPTH, OMO_ETE_DEPTH - 1, t);
		scnprintf(t, sizeof(t), "SR ch%u wptr", i);
		omo_wr(omo_ete, b + ETE_SR_WPTR, 0, t);
		scnprintf(t, sizeof(t), "SR ch%u ctrl", i);
		omo_wr(omo_ete, b + ETE_SR_CTRL, 0, t);
	}

	for (i = 0; i < OMO_ETE_DR_N; i++) {
		unsigned long b = omo_dr_block[i];
		u32 devva = omo_hostca_to_devva(omo_dr_dma[i]);
		char t[40];

		scnprintf(t, sizeof(t), "DR ch%u base", i + 3);
		omo_wr(omo_ete, b + ETE_DR_BASEREG, devva, t);
		scnprintf(t, sizeof(t), "DR ch%u depth-1", i + 3);
		omo_wr(omo_ete, b + ETE_DR_DEPTH, OMO_ETE_DEPTH - 1, t);
		scnprintf(t, sizeof(t), "DR ch%u wptr", i + 3);
		omo_wr(omo_ete, b + ETE_DR_WPTR, 0, t);
	}

	/* binding write #2: pcie_ete_chn_res on the glue/message block, after the rings */
	v = omo_rd(omo_msg, OMO_CHN_RES);
	pr_info("omo-drv1:   glue chn_res pre=0x%08x mask=0x%08x\n", v, OMO_GLUE_CHN_RES_MASK);
	omo_wr(omo_msg, OMO_CHN_RES, v & OMO_GLUE_CHN_RES_MASK, "glue chn_res 0x400392e8");

	pr_info("omo-drv1: ---- write path done: writes that failed readback = %u ----\n",
		omo_wr_fail);
	pr_info("omo-drv1: NOTE no descriptor submitted, no doorbell rung (bounded by design)\n");
}

/*
 * Read back the viewports we programmed, so they can be diffed against a live vendor boot.
 * Reference values (docs/phase18/inbound-map.md + build/register-dumps/bothep/001_live_vendor_bars2.txt):
 *   v0 [0x104]=0x80000000 [0x108]=0x40000000 [0x110]=0x401BFFFF [0x114]=0x00000000
 *   v3 [0x704]=0x80000000 [0x708]=0x403B8000 [0x710]=0x404D7FFF [0x714]=0x40000000
 * If ours differ, the region-3 decode difference has a name; if ours match, the difference is
 * elsewhere (and that is a finding too).
 */
static void omo_read_viewports(void)
{
	static const unsigned int ctrl1[6] = { 0x100, 0x300, 0x500, 0x700, 0x900, 0xb00 };
	unsigned int i;

	pr_info("omo-drv1: ---- inbound viewports as programmed (diff against the vendor ref) ----\n");
	for (i = 0; i < ARRAY_SIZE(ctrl1); i++) {
		unsigned long o = ctrl1[i];

		pr_info("omo-drv1:   v%u [0x%03lx]=0x%08x [0x%03lx]=0x%08x [0x%03lx]=0x%08x [0x%03lx]=0x%08x [0x%03lx]=0x%08x [0x%03lx]=0x%08x\n",
			i, o, omo_rd(omo_iatu, o),
			o + 4, omo_rd(omo_iatu, o + 4),
			o + 8, omo_rd(omo_iatu, o + 8),
			o + 12, omo_rd(omo_iatu, o + 12),
			o + 16, omo_rd(omo_iatu, o + 16),
			o + 20, omo_rd(omo_iatu, o + 20));
	}
}

/*
 * Release the Wi-Fi CPU: a single quoted 4-byte write, the act phase 19 found by
 * experiment (docs/phase19/release-attempts.md).  Without it the firmware image
 * sits in the chip and nothing runs.  Read back and report, per the discipline
 * used everywhere else in this module.
 *
 * CA 0x40000108 -> BAR0 0x3b8108 (region 3: host 0x403b8000 -> dev 0x40000000).
 */
#define OMO_RELEASE_OFF	0x3b8108UL
#define OMO_RELEASE_VAL	0x00005a5aU

/*
 * Poll the mailbox after the release, so the words the released firmware emits are actually
 * OBSERVED rather than assumed.  Phase 19/20 measured them at ~+1.85 s after the release write
 * (docs/phase20/runtime-msg.md B.1: out[1] 0 -> 0x40 at +1320 ms, then 0x40 -> 0x04 at +1860 ms), and
 * logged each TRANSITION rather than a sampled value.
 *
 * Reads only.  Every transition in out[0]/out[1] is reported with the bit decoded to its id, because
 * a single-bit mailbox is the vendor's per-message convention (docs/phase19/fw-handshake.md).
 */
static unsigned int omo_pollms = 500;
module_param_named(pollms, omo_pollms, uint, 0444);
MODULE_PARM_DESC(pollms, "post-release mailbox poll interval in ms (default 500)");

static unsigned int omo_polldur = 8000;
module_param_named(polldur, omo_polldur, uint, 0444);
MODULE_PARM_DESC(polldur, "post-release mailbox poll duration in ms (default 8000; phase 19 saw the first word at ~1.85 s)");

static void omo_poll_mailbox(void)
{
	unsigned long elapsed = 0;
	u32 p0 = omo_rd(omo_msg, OMO_MSG0);
	u32 p1 = omo_rd(omo_msg, OMO_MSG1);
	unsigned int seen = 0;

	pr_info("omo-drv1: ---- post-release mailbox poll (%u ms interval, %u ms total) ----\n",
		omo_pollms, omo_polldur);
	pr_info("omo-drv1:   t=0  out[0]=0x%08x out[1]=0x%08x (baseline)\n", p0, p1);

	while (elapsed < omo_polldur) {
		u32 n0, n1;
		int bit;

		msleep(omo_pollms);
		elapsed += omo_pollms;

		n0 = omo_rd(omo_msg, OMO_MSG0);
		n1 = omo_rd(omo_msg, OMO_MSG1);
		if (n0 == p0 && n1 == p1)
			continue;

		pr_info("omo-drv1:   t=%lums out[0] 0x%08x -> 0x%08x, out[1] 0x%08x -> 0x%08x\n",
			elapsed, p0, n0, p1, n1);
		for (bit = 0; bit < 32; bit++) {
			if ((n1 & (1U << bit)) && !(p1 & (1U << bit)))
				pr_info("omo-drv1:     out[1] bit %d set (id %d)\n", bit, bit);
			if (!(n1 & (1U << bit)) && (p1 & (1U << bit)))
				pr_info("omo-drv1:     out[1] bit %d cleared (id %d)\n", bit, bit);
		}
		for (bit = 0; bit < 32; bit++) {
			if ((n0 & (1U << bit)) && !(p0 & (1U << bit)))
				pr_info("omo-drv1:     out[0] bit %d set (H2D mask)\n", bit);
		}
		p0 = n0;
		p1 = n1;
		seen++;
	}

	pr_info("omo-drv1:   poll done: %u transitions in %lu ms; final out[0]=0x%08x out[1]=0x%08x\n",
		seen, elapsed, omo_rd(omo_msg, OMO_MSG0), omo_rd(omo_msg, OMO_MSG1));
	if (!seen)
		pr_info("omo-drv1:   NOTE no mailbox transition in this window - the firmware produced nothing\n");
}

static int omo_do_release(void)
{
	u32 rb;

	pr_info("omo-drv1: RELEASE write CA 0x40000108 <- 0x%08x (BAR0+0x%lx)\n",
		OMO_RELEASE_VAL, (unsigned long)OMO_RELEASE_OFF);
	iowrite32(OMO_RELEASE_VAL, omo_rel + (OMO_RELEASE_OFF - OMO_REL_WIN));
	rb = ioread32(omo_rel + (OMO_RELEASE_OFF - OMO_REL_WIN));
	pr_info("omo-drv1: release readback = 0x%08x %s\n", rb,
		rb == OMO_RELEASE_VAL ? "match=YES" : "match=NO");
	return rb == OMO_RELEASE_VAL ? 0 : -EIO;
}

/* ---- register access --------------------------------------------------- */
static u32 omo_rd(void __iomem *win, unsigned long off)
{
	return ioread32(win + off);
}

static void omo_log_reg(const char *tag, void __iomem *win, unsigned long off, u32 v)
{
	if (omo_verbose)
		pr_info("omo-drv1: %-16s BAR0+0x%05lx = 0x%08x\n", tag, off, v);
}

/*
 * Read the ETE/glue block and decode it into driver state.  Read-only, and
 * only called when hw=1 and the BARs were successfully claimed.
 */
static void omo_read_ring_block(void)
{
	u32 before, after;
	int i;

	/* bracket the block with a message register so a concurrent device
	 * write is visible as a changed value (the read discipline phase 17
	 * used: read-only, bracketed, no writes of any kind). */
	before = omo_rd(omo_msg, OMO_MSG1);

	/* 3 SR channels at 0x400 + i*0x114, 4 DR at 0x590 + i*0x6c */
	for (i = 0; i < 3; i++) {
		unsigned long b = ETE_SR0_BASE + i * ETE_SR_STRIDE;

		omo_sr[i].ctrl  = omo_rd(omo_ete, b + ETE_SR_CTRL);
		omo_sr[i].base  = omo_rd(omo_ete, b + ETE_SR_BASEREG);
		omo_sr[i].depth = omo_rd(omo_ete, b + ETE_SR_DEPTH);
		omo_sr[i].wptr  = omo_rd(omo_ete, b + ETE_SR_WPTR);
		omo_sr[i].rptr  = omo_rd(omo_ete, b + ETE_SR_RPTR);
		pr_info("omo-drv1: SR ch%d CA=0x%08x base=0x%08x depth-1=%u wptr=0x%08x rptr=0x%08x ctrl=0x%08x\n",
			i, (u32)(0x4003a000 + b), omo_sr[i].base,
			omo_sr[i].depth & 0x3ff, omo_sr[i].wptr, omo_sr[i].rptr,
			omo_sr[i].ctrl);
	}
	for (i = 0; i < 4; i++) {
		unsigned long b = ETE_DR0_BASE + i * ETE_DR_STRIDE;

		omo_dr[i].base  = omo_rd(omo_ete, b + ETE_DR_BASEREG);
		omo_dr[i].depth = omo_rd(omo_ete, b + ETE_DR_DEPTH);
		omo_dr[i].wptr  = omo_rd(omo_ete, b + ETE_DR_WPTR);
		omo_dr[i].rptr  = omo_rd(omo_ete, b + ETE_DR_RPTR);
		pr_info("omo-drv1: DR ch%d CA=0x%08x base=0x%08x depth-1=%u wptr=0x%08x rptr=0x%08x\n",
			i, (u32)(0x4003a000 + b), omo_dr[i].base,
			omo_dr[i].depth & 0x3ff, omo_dr[i].wptr, omo_dr[i].rptr);
	}

	after = omo_rd(omo_msg, OMO_MSG1);
	pr_info("omo-drv1: msg1 bracket before=0x%08x after=0x%08x %s\n",
		before, after, before == after ? "(stable)" : "(DEVICE CHANGED IT)");
	pr_info("omo-drv1: NOTE read-only decode; no ring write, no descriptor, no doorbell\n");
}

/*
 * Disambiguate out[0].  The phase-23f run read 0x40000004 at BAR0+0x3f1010, which is ALSO the
 * value of the endpoint's BAR0 config-space register - so the read could be a live mailbox word
 * or an aliasing artefact.  Several addresses are read in the same boot, plus the config word,
 * so the question is settled by measurement rather than by reasoning:
 *   0x3f1010  the vendor slot table's offset for out[0] (CA 0x40039010 through region 3)
 *   0x3f1014  out[1], the register the released chip is stated to write
 *   0x39010   the earlier (wrong) offset, for contrast
 *   0x3f2000  the ETE block's own base - a different, already-trusted decode
 *   0x3f1000  the message block's base word
 * and the PCI_BASE_ADDRESS_0 config word the driver already prints.  If the candidate reads differ
 * from each other, at most one is the real register; if one equals the config word bit-for-bit
 * while another differs, the matching one is aliasing rather than a mailbox value.
 */
static void omo_disambiguate_msg0(void)
{
	u32 cfg0 = 0;

	pci_read_config_dword(omo_pdev, PCI_BASE_ADDRESS_0, &cfg0);

	pr_info("omo-drv1: ---- out[0] disambiguation (labels are ABSOLUTE BAR0 offsets) ----\n");
	pr_info("omo-drv1:   cfg      PCI_BASE_ADDRESS_0   = 0x%08x\n", cfg0);
	pr_info("omo-drv1:   msg+000  BAR0+0x%05lx       = 0x%08x\n",
		(unsigned long)OMO_MSG_WIN + 0x000, omo_rd(omo_msg, 0x000));
	pr_info("omo-drv1:   out[0]   BAR0+0x%05lx       = 0x%08x\n",
		(unsigned long)OMO_MSG_WIN + OMO_MSG0, omo_rd(omo_msg, OMO_MSG0));
	pr_info("omo-drv1:   out[1]   BAR0+0x%05lx       = 0x%08x\n",
		(unsigned long)OMO_MSG_WIN + OMO_MSG1, omo_rd(omo_msg, OMO_MSG1));
	pr_info("omo-drv1:   ete+000  BAR0+0x%05lx       = 0x%08x  (IO ROM vector)\n",
		(unsigned long)OMO_ETE_WIN + 0x000, omo_rd(omo_ete, 0x000));
}

static void omo_read_msg_block(void)
{
	omo_msg0 = omo_rd(omo_msg, OMO_MSG0);
	omo_msg1 = omo_rd(omo_msg, OMO_MSG1);
	omo_msg2 = omo_rd(omo_msg, OMO_MSG2);
	omo_chnres = omo_rd(omo_msg, OMO_CHN_RES);

	omo_log_reg("MSG0 out[0]", omo_msg, OMO_MSG0, omo_msg0);
	omo_log_reg("MSG1 out[1]", omo_msg, OMO_MSG1, omo_msg1);
	omo_log_reg("MSG2 doorbell", omo_msg, OMO_MSG2, omo_msg2);
	omo_log_reg("CHN_RES", omo_msg, OMO_CHN_RES, omo_chnres);

	omo_disambiguate_msg0();

	if (omo_read_msg5) {
		omo_msg5 = omo_rd(omo_msg, OMO_MSG5);
		pr_info("omo-drv1: DEBUG msg5 out[5] READ-ONLY = 0x%08x (never written here)\n",
			omo_msg5);
	}
}

/* ---- PCI bring-up (mirrors lab/eteprobe, read-mostly) ------------------ */
static int omo_hw_attach(void)
{
	u32 lo = 0, hi = 0;
	u16 rb = 0;
	int rc;

	omo_pdev = pci_get_domain_bus_and_slot(omo_domain, 0, OMO_PCI_DEV);
	if (!omo_pdev) {
		pr_err("omo-drv1: no 59e7:0005 endpoint in domain %u\n", omo_domain);
		return -ENODEV;
	}

	rc = pci_enable_device(omo_pdev);
	if (rc) {
		pr_err("omo-drv1: pci_enable_device rc=%d\n", rc);
		return rc;
	}

	/* REFUSE rather than fight: if the vendor stack owns the BARs this
	 * fails, and that is the correct outcome (coexistence rule). */
	rc = pci_request_mem_regions(omo_pdev, "omo-drv1");
	if (rc) {
		pr_err("omo-drv1: pci_request_mem_regions rc=%d (vendor stack loaded?) - refusing\n",
		       rc);
		pci_disable_device(omo_pdev);
		omo_pdev = NULL;
		return rc;
	}

	pci_read_config_dword(omo_pdev, PCI_BASE_ADDRESS_0, &lo);
	pci_read_config_dword(omo_pdev, PCI_BASE_ADDRESS_1, &hi);
	omo_bar0_base = (resource_size_t)(lo & PCI_BASE_ADDRESS_MEM_MASK);

	/* the documented viewport-enable write: MEM|MASTER */
	pci_write_config_word(omo_pdev, PCI_COMMAND, 0x0007);
	pci_read_config_word(omo_pdev, PCI_COMMAND, &rb);
	pr_info("omo-drv1: BAR0 base=0x%llx (config) BAR0_hi=0x%x cfg[0x004]=0x%04x MEM|MASTER=%s\n",
		(unsigned long long)omo_bar0_base, hi, rb,
		(rb & (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) ==
		(PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER) ? "set" : "MISSING");

	omo_msg = ioremap(omo_bar0_base + OMO_MSG_WIN, OMO_MSG_BYTES);
	if (!omo_msg) {
		pr_err("omo-drv1: ioremap message window FAILED\n");
		rc = -ENOMEM;
		goto err_regions;
	}
	omo_ete = ioremap(omo_bar0_base + OMO_ETE_WIN, OMO_WIN_BYTES);
	if (!omo_ete) {
		pr_err("omo-drv1: ioremap ETE window FAILED\n");
		rc = -ENOMEM;
		goto err_msg;
	}
	pr_info("omo-drv1: mapped message BAR0+0x%lx and ETE BAR0+0x%lx (region-3 viewport at 0x40000000)\n",
		(unsigned long)OMO_MSG_WIN, (unsigned long)OMO_ETE_WIN);

	omo_rel = ioremap(omo_bar0_base + OMO_REL_WIN, OMO_REL_BYTES);
	if (!omo_rel) {
		pr_err("omo-drv1: ioremap release window (BAR0+0x%lx) FAILED\n",
		       (unsigned long)OMO_REL_WIN);
		rc = -ENOMEM;
		goto err_ete;	/* omo_rel is NULL; only ete/msg need unwinding */
	}

	omo_iatu = pci_iomap(omo_pdev, OMO_IATU_BAR, 0);
	if (!omo_iatu) {
		pr_err("omo-drv1: iomap BAR2 (iATU) FAILED\n");
		rc = -ENOMEM;
		goto err_rel;	/* unmap the release window, which IS mapped here */
	}
	if (omo_program_regions_en) {
		pr_info("omo-drv1: programming the six inbound viewports (region-3 IO required for the ETE block)\n");
		omo_program_inbound(omo_iatu, omo_bar0_base, "inbound");
		omo_read_viewports();
	}

	/* decode the blocks - reads only */
	omo_read_ring_block();
	omo_read_msg_block();
	omo_regs_valid = true;

	/* the first write path, only when explicitly requested and only after the
	 * decode, so the before/after pair is unambiguous in one boot. */
	if (omo_wr_en) {
		rc = omo_rings_alloc();
		if (rc)
			goto err_iatu;
		omo_ete_program();
	}

	/* Release the Wi-Fi CPU - the act phase 19 found, gated on its own param - then OBSERVE
	 * what the released firmware emits instead of assuming it said nothing. */
	if (omo_release_en) {
		omo_do_release();
		omo_poll_mailbox();
	}

	return 0;

err_iatu:
	pci_iounmap(omo_pdev, omo_iatu);
	omo_iatu = NULL;
err_rel:
	iounmap(omo_rel);
	omo_rel = NULL;
err_ete:
	iounmap(omo_ete);
	omo_ete = NULL;
err_msg:
	iounmap(omo_msg);
	omo_msg = NULL;
err_regions:
	pci_release_mem_regions(omo_pdev);
	pci_disable_device(omo_pdev);
	omo_pdev = NULL;
	return rc;
}

static void omo_hw_detach(void)
{
	if (omo_pdev)
		omo_rings_free();
	if (omo_rel) {
		iounmap(omo_rel);
		omo_rel = NULL;
	}
	if (omo_iatu) {
		pci_iounmap(omo_pdev, omo_iatu);
		omo_iatu = NULL;
	}
	if (omo_ete) {
		iounmap(omo_ete);
		omo_ete = NULL;
	}
	if (omo_msg) {
		iounmap(omo_msg);
		omo_msg = NULL;
	}
	if (omo_pdev) {
		pci_release_mem_regions(omo_pdev);
		pci_disable_device(omo_pdev);
		omo_pdev = NULL;
	}
	omo_regs_valid = false;
}

/* ---- netdev ------------------------------------------------------------ */
static struct wireless_dev *omo_add_virtual_intf(struct wiphy *wiphy,
						 const char *name,
						 unsigned char name_assign_type,
						 enum nl80211_iftype type,
						 struct vif_params *params);
static int omo_del_virtual_intf(struct wiphy *wiphy, struct wireless_dev *wdev);

static int omo_ndo_open(struct net_device *dev)
{
	pr_info("omo-drv1: ndo_open %s hw=%u regs=%s\n", dev->name, omo_hw,
		omo_regs_valid ? "decoded" : "absent");
	return 0;
}

static int omo_ndo_stop(struct net_device *dev)
{
	pr_info("omo-drv1: ndo_stop %s\n", dev->name);
	return 0;
}

/*
 * TX is still a drop: phase 22 measured that submitting descriptors without
 * the device-side accept gate produces nothing (docs/phase22/h2d-accept.md),
 * so the module does not pretend to transmit.  When a data path lands, this
 * is the hook that will use omo_sr/omo_dr.
 */
static netdev_tx_t omo_ndo_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	if (omo_regs_valid && omo_verbose > 1)
		pr_info("omo-drv1: xmit %u bytes dropped (no data path yet; SR0 base=0x%08x)\n",
			skb->len, omo_sr[0].base);
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

static const struct net_device_ops omo_netdev_ops = {
	.ndo_open	= omo_ndo_open,
	.ndo_stop	= omo_ndo_stop,
	.ndo_start_xmit	= omo_ndo_start_xmit,
};

#define OMO_CHAN(_ch) {				\
	.center_freq = 2407 + (_ch) * 5,	\
	.hw_value = (_ch),			\
	.max_power = 20,			\
}

static struct ieee80211_channel omo_2ghz_channels[] = {
	OMO_CHAN(1),  OMO_CHAN(2),  OMO_CHAN(3),  OMO_CHAN(4),
	OMO_CHAN(5),  OMO_CHAN(6),  OMO_CHAN(7),  OMO_CHAN(8),
	OMO_CHAN(9),  OMO_CHAN(10), OMO_CHAN(11), OMO_CHAN(12),
	OMO_CHAN(13),
};

static struct ieee80211_rate omo_2ghz_rates[] = {
	{ .bitrate = 10 },
	{ .bitrate = 20,  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 55,  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 110, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 60 },
	{ .bitrate = 90 },
	{ .bitrate = 120 },
	{ .bitrate = 180 },
	{ .bitrate = 240 },
	{ .bitrate = 360 },
	{ .bitrate = 480 },
	{ .bitrate = 540 },
};

static struct ieee80211_supported_band omo_2ghz_band = {
	.channels	= omo_2ghz_channels,
	.n_channels	= ARRAY_SIZE(omo_2ghz_channels),
	.bitrates	= omo_2ghz_rates,
	.n_bitrates	= ARRAY_SIZE(omo_2ghz_rates),
	.ht_cap		= {
		.ht_supported	= true,
		.cap		= IEEE80211_HT_CAP_SGI_20 |
				  IEEE80211_HT_CAP_MAX_AMSDU,
		.ampdu_factor	= IEEE80211_HT_MAX_AMPDU_64K,
		.ampdu_density	= IEEE80211_HT_MPDU_DENSITY_NONE,
		.mcs		= {
			.rx_mask	= { 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
			.tx_params	= IEEE80211_HT_MCS_TX_DEFINED,
		},
	},
};

static void omo_netdev_setup(struct net_device *dev)
{
	ether_setup(dev);
	*(const struct net_device_ops **)((char *)dev + VND_ND_OPS_OFF) =
		&omo_netdev_ops;
}

static struct wireless_dev *omo_add_virtual_intf(struct wiphy *wiphy,
						 const char *name,
						 unsigned char name_assign_type,
						 enum nl80211_iftype type,
						 struct vif_params *params)
{
	struct net_device *dev;
	struct wireless_dev *wdev;
	struct sockaddr sa = { .sa_family = ARPHRD_ETHER };
	int rc;

	if (type != NL80211_IFTYPE_STATION)
		return ERR_PTR(-EOPNOTSUPP);
	if (omo_netdev)
		return ERR_PTR(-EBUSY);

	dev = alloc_netdev(VND_ND_PRIV_SIZE, name, name_assign_type,
			   omo_netdev_setup);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	wdev = (struct wireless_dev *)((char *)dev + VND_ND_PRIV_OFF);
	wdev->wiphy  = wiphy;
	wdev->iftype = type;
	*(struct wireless_dev **)((char *)dev + VND_ND_IEEE80211_OFF) = wdev;

	memcpy(sa.sa_data, omo_mac, ETH_ALEN);
	rc = eth_mac_addr(dev, &sa);
	if (rc) {
		free_netdev(dev);
		return ERR_PTR(rc);
	}

	rc = register_netdevice(dev);
	if (rc) {
		free_netdev(dev);
		return ERR_PTR(rc);
	}

	omo_netdev = dev;
	pr_info("omo-drv1: add_virtual_intf name=%s ifindex=%d rc=0\n",
		dev->name, dev->ifindex);
	return wdev;
}

static int omo_del_virtual_intf(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	struct net_device *dev = omo_netdev;

	if (!dev)
		return -ENODEV;

	omo_netdev = NULL;
	rtnl_lock();
	unregister_netdevice(dev);
	rtnl_unlock();
	free_netdev(dev);
	return 0;
}

static const struct cfg80211_ops omo_ops = {
	.add_virtual_intf	= omo_add_virtual_intf,
	.del_virtual_intf	= omo_del_virtual_intf,
};

static int __init omo_wifidrv1_init(void)
{
	struct wiphy *wiphy;
	int rc;

	pr_info("omo-drv1: init hw=%u read_msg5=%u (offsets: ops=%u ieee=%u priv=%u)\n",
		omo_hw, omo_read_msg5,
		VND_ND_OPS_OFF, VND_ND_IEEE80211_OFF, VND_ND_PRIV_OFF);

	if (omo_hw) {
		rc = omo_hw_attach();
		if (rc) {
			pr_err("omo-drv1: hardware attach failed rc=%d - continuing without it\n",
			       rc);
		}
	} else {
		pr_info("omo-drv1: hw=0 - registration-only load (no PCI access at all)\n");
	}

	wiphy = wiphy_new_nm(&omo_ops, 0, OMO_WIPHY_NAME);
	if (!wiphy) {
		omo_hw_detach();
		return -ENOMEM;
	}
	omo_wiphy = wiphy;

	wiphy->bands[NL80211_BAND_2GHZ] = &omo_2ghz_band;
	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);
	memcpy(wiphy->perm_addr, omo_mac, ETH_ALEN);

	rc = wiphy_register(wiphy);
	if (rc) {
		wiphy_free(wiphy);
		omo_wiphy = NULL;
		omo_hw_detach();
		return rc;
	}

	/* create the interface through cfg80211's own path so the notifier
	 * runs exactly as it does for a driver-created interface. */
	{
		struct wireless_dev *wdev;

		wdev = omo_add_virtual_intf(wiphy, OMO_IFNAME, NET_NAME_UNKNOWN,
					    NL80211_IFTYPE_STATION, NULL);
		if (IS_ERR(wdev))
			pr_err("omo-drv1: initial interface rc=%ld\n",
			       PTR_ERR(wdev));
	}

	pr_info("omo-drv1: init done wiphy=%s ifname=%s hw=%u regs=%s\n",
		OMO_WIPHY_NAME, OMO_IFNAME, omo_hw,
		omo_regs_valid ? "decoded" : "absent");
	return 0;
}

static void __exit omo_wifidrv1_exit(void)
{
	if (omo_netdev) {
		struct net_device *dev = omo_netdev;

		omo_netdev = NULL;
		rtnl_lock();
		unregister_netdevice(dev);
		rtnl_unlock();
		free_netdev(dev);
	}
	if (omo_wiphy) {
		wiphy_unregister(omo_wiphy);
		wiphy_free(omo_wiphy);
		omo_wiphy = NULL;
	}
	omo_hw_detach();
	pr_info("omo-drv1: exit done\n");
}

module_init(omo_wifidrv1_init);
module_exit(omo_wifidrv1_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("WR3000 v2.0: cfg80211 wiphy + netdev + read-only endpoint decode (port step 2)");
