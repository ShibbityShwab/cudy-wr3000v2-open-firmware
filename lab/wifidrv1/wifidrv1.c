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
#include <linux/fs.h>		/* filp_open / kernel_read (the firmware loader) */
#include <linux/vmalloc.h>	/* vmalloc/vfree for the firmware image */
#include <linux/uaccess.h>	/* memcpy_toio */
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
/*
 * MESSAGE window: the six mailbox CAs.  
 *
 * THE BASE IS 0x3f1000, NOT 0x3f0000.  This was wrong for the whole phase-23 investigation and it
 * explains every "the firmware produced nothing" result: the mailbox was being watched one page
 * below where it lives, so the firmware's words landed outside the poll.
 *
 * The vendor's own table (docs/phase20/runtime-msg.md) gives CA -> BAR0 offset, and the region-3
 * translation (host 0x403b8000 -> dev CA 0x40000000, docs/phase18/inbound-map.md row 3) is
 * offset = 0x3b8000 + (CA - 0x40000000):
 *   out[0] CA 0x40039010 -> 0x3f1010     out[1] CA 0x40039014 -> 0x3f1014
 *   out[2] CA 0x400392d4 -> 0x3f12d4     chn_res CA 0x400392e8 -> 0x3f12e8
 *   glue status +0x2ec   -> 0x3f12ec     out[5] CA 0x400392f0 -> 0x3f12f0
 *   ETE intr CA 0x40039508 -> 0x3f1508 (= this base + 0x508)
 * Independent confirmation on a live vendor boot: 0x403f12e8 reads 0x00000020, matching phase 15's
 * captured chn_res, while 0x403f02e8 (the address an earlier revision read) reads 0.
 *
 * Sizing: the highest offset used here is 0x508 (+4), so a 0x1000 window at 0x3f1000 covers all of
 * them.  (An earlier revision also mapped 0x2000 from 0x3f0000 and used offset 0x1508 for the
 * interrupt register, which happened to land on the right address - masking the base error.)
 */
#define OMO_MSG_WIN	0x3f1000UL
#define OMO_MSG_BYTES	0x1000UL
#define OMO_ETE_WIN	0x3f2000UL
#define OMO_WIN_BYTES	0x1000UL	/* the ETE block: +0x408..+0x6e8 */
/* Region 0 (ROM_WRAM) holds the release register at BAR0+0x3b8108, which is
 * OUTSIDE both windows above.  It gets its own mapping: one page suffices for
 * the single register.  (Writing it through another window would fault exactly
 * like the +0x1508 access did - checked before writing the code this time.) */
/* Region 3 (SHUANGTA_REGION_IO) as ONE window: host 0x403b8000..0x404d7fff -> dev CA
 * 0x40000000, size 0x120000 (docs/phase18/inbound-map.md row 3).  It carries the release
 * register (0x3b8108), the analog/clock status words phase 19 used as the CPU-start
 * signature (0x3bd00c, 0x4b9230) and more.  An earlier revision mapped only 0x1000 here
 * and the signature registers fell outside it - caught by the offset-fits check, which is
 * exactly why that check exists (the +0x1508 access faulted once already). */
#define OMO_IO_WIN	0x3b8000UL
#define OMO_IO_BYTES	0x120000UL
#define OMO_REL_WIN	0x3b8000UL	/* kept for the release offset arithmetic */

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
#define OMO_SR_PAYLOAD	512	/* per-node host->device message buffer (fwaccept: ETE_SR_PAYLOAD) */
#define OMO_SR_MSG_LEN	0x48	/* the vendor's first SR message, live capture (72 B) */
#define OMO_SR_ALG_LEN	0x12a	/* alg get_2g_power_param H2D frame, live capture (298 B) */
#define OMO_SR_FLAG	0x6d2b	/* shuangta_ete_sr_dscr_fill @0x17858: word1 = (len<<16)|0x6d2b */
#define OMO_SR_EN0	0x000	/* per-channel enable (the vendor's ENABLE SR chN +0x00) */
#define OMO_SR_EN1	0x048	/* SR-side enable (ENABLE SR chN +0x48) */
#define ETE_SR_RPTR	0x01c
#define ETE_DR_BASEREG	0x030
#define ETE_DR_DEPTH	0x034
#define ETE_DR_WPTR	0x038
#define ETE_DR_RPTR	0x03c
#define OMO_DR_PAYLOAD	2048	/* per-node device->host receive buffer (fwaccept: ETE_DR_PAYLOAD) */

/* ---- parameters --------------------------------------------------------- */
static unsigned int omo_hw;		/* 0 = registration only (safe default) */
module_param_named(hw, omo_hw, uint, 0444);
MODULE_PARM_DESC(hw, "1 = claim EP0, program the inbound viewports, decode (read-only); 0 = no hardware access");

static unsigned int omo_wr_en;
module_param_named(wr, omo_wr_en, uint, 0444);
MODULE_PARM_DESC(wr, "1 = program the ETE rings (the module's first writes to the endpoint); requires hw=1 program=1");

static unsigned int omo_fw_en;
module_param_named(fw, omo_fw_en, uint, 0444);
MODULE_PARM_DESC(fw, "1 = load FIRMWARE.bin into the chip before the release (REQUIRED for the firmware to run)");

static char *omo_fwpath = "/lib/firmware/hi_wifi/FIRMWARE.bin";
module_param_named(fwpath, omo_fwpath, charp, 0444);
MODULE_PARM_DESC(fwpath, "firmware image path (default /lib/firmware/hi_wifi/FIRMWARE.bin)");

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

/*
 * The value the host writes to the re-arm register (out[4], CA 0x40101414).
 *
 * The HOST-side disassembly (plat.ko, docs/phase20/msg-host-half.md) writes 1 here, and the port has
 * written 1 since phase 24c.  The FIRMWARE's own pcie_msg_handle (file 0x818a8, ARM Thumb) writes
 * **8** to its third register - `movs r1, #8; str r1, [r2]` - so the two sides of the same protocol do
 * not use the same value.  Whether the difference is meaningful (a different register, or a bitmask
 * where the host only needs bit 0) is unresolved, and this parameter makes it testable without a
 * rebuild per value.
 */
static unsigned int omo_rearm_val = 1;
module_param_named(rearm_val, omo_rearm_val, uint, 0444);
MODULE_PARM_DESC(rearm_val, "value written to the re-arm register out[4] (host disasm says 1, the firmware's own handler writes 8); default 1");

static unsigned int omo_verbose = 1;
module_param_named(verbose, omo_verbose, uint, 0444);
MODULE_PARM_DESC(verbose, "1 = log each decoded register");

/* ---- state ------------------------------------------------------------- */
static struct wiphy *omo_wiphy;
static struct net_device *omo_netdev;

static struct pci_dev *omo_pdev;
static void __iomem *omo_msg;		/* message/channel window  BAR0+0x3f0000 */
static void __iomem *omo_ete;		/* ETE ring window        BAR0+0x3f2000 */
static void __iomem *omo_rel;		/* region 3 IO (release + status words) */
static void __iomem *omo_fwmap;		/* region 5 / firmware   BAR0+0x6f8000 */static void __iomem *omo_iatu;		/* BAR2: the inbound viewport window */
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
/* PHASE 34 - a spare inbound viewport onto the device's interrupt block.
 * Phase 32 (verified) located the firmware's interrupt pending (CA 0x4016010C) and enable
 * (0x40161100) registers but proved no existing host window reaches the 0x4016xxxx range.
 * The endpoint's iATU has 16 inbound viewports and 6 are used; viewport 6 can target device
 * 0x40160000 at an unused BAR0 offset, making the whole block host-visible. */
static unsigned int omo_irqwin_en;
module_param_named(irqwin, omo_irqwin_en, uint, 0444);
MODULE_PARM_DESC(irqwin,
	"1 = add a spare inbound viewport onto device CA 0x40160000 (phase 34)");

#define OMO_IRQWIN_OFF	0x8d8000UL	/* unused BAR0 offset above the six named regions */
#define OMO_IRQWIN_SIZE	0x002000UL	/* covers 0x40160000..0x40161fff */
#define OMO_IRQWIN_TGT	0x40160000UL

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

	if (omo_irqwin_en) {
		u64 base = bar0_base + OMO_IRQWIN_OFF;
		u64 limit = base + OMO_IRQWIN_SIZE - 1;
		unsigned long c = OMO_IATU_CTRL2 + OMO_IATU_STRIDE * 6;

		pr_info("omo-drv1: %s region 6 IRQ         host 0x%llx..0x%llx -> dev 0x%llx\n",
			who, (unsigned long long)base, (unsigned long long)limit,
			(unsigned long long)OMO_IRQWIN_TGT);
		omo_iatu_wr(iatu, c, 0, who, "ctrl2=0");
		omo_iatu_wr(iatu, c, 0x80000000U, who, "ctrl2=ena");
		omo_iatu_wr(iatu, c + 4, (u32)base, who, "base_lo");
		omo_iatu_wr(iatu, c + 8, (u32)(base >> 32), who, "base_hi");
		omo_iatu_wr(iatu, c + 12, (u32)limit, who, "limit");
		omo_iatu_wr(iatu, c + 16, (u32)OMO_IRQWIN_TGT, who, "target_lo");
		omo_iatu_wr(iatu, c + 20, (u32)((u64)OMO_IRQWIN_TGT >> 32), who, "target_hi");
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
#define OMO_ETE_INTR_OFF	0x508UL	/* within the message window -> BAR0 0x3f1508 */
#define OMO_ETE_INTR_MASK	0xffe0f8f8U
#define OMO_GLUE_CHN_RES_MASK	0xfffffc20U
#define OMO_ETE_DEPTH		32
#define OMO_ETE_SR_N		3
#define OMO_ETE_DR_N		4

static const unsigned long omo_sr_block[OMO_ETE_SR_N] = { 0x400, 0x450, 0x4a0 };
static const unsigned long omo_dr_block[OMO_ETE_DR_N] = { 0x590, 0x5e0, 0x630, 0x680 };

static void *omo_sr_va[OMO_ETE_SR_N];
static dma_addr_t omo_sr_dma[OMO_ETE_SR_N];
static void *omo_sr_pay[OMO_ETE_SR_N];	/* per-node H2D message buffers */
static dma_addr_t omo_sr_pay_dma[OMO_ETE_SR_N];
static void *omo_dr_va[OMO_ETE_DR_N];
static dma_addr_t omo_dr_dma[OMO_ETE_DR_N];
static void *omo_dr_pay[OMO_ETE_DR_N];	/* per-node D2H receive buffers */
static dma_addr_t omo_dr_pay_dma[OMO_ETE_DR_N];
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
	for (i = 0; i < OMO_ETE_SR_N; i++) {
		omo_sr_pay[i] = dma_alloc_coherent(&omo_pdev->dev,
						  OMO_ETE_DEPTH * OMO_SR_PAYLOAD,
						  &omo_sr_pay_dma[i], GFP_KERNEL);
		if (!omo_sr_pay[i]) {
			pr_err("omo-drv1: SR ch%u payload dma_alloc_coherent FAILED\n", i);
			return -ENOMEM;
		}
		pr_info("omo-drv1: SR ch%u payload %u bytes @ %pad (devva 0x%08x)\n",
			i, OMO_ETE_DEPTH * OMO_SR_PAYLOAD, &omo_sr_pay_dma[i],
			omo_hostca_to_devva(omo_sr_pay_dma[i]));
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
	for (i = 0; i < OMO_ETE_DR_N; i++) {
		omo_dr_pay[i] = dma_alloc_coherent(&omo_pdev->dev,
						  OMO_ETE_DEPTH * OMO_DR_PAYLOAD,
						  &omo_dr_pay_dma[i], GFP_KERNEL);
		if (!omo_dr_pay[i]) {
			pr_err("omo-drv1: DR ch%u payload dma_alloc_coherent FAILED\n", i);
			return -ENOMEM;
		}
		pr_info("omo-drv1: DR ch%u payload %u bytes @ %pad (devva 0x%08x)\n",
			i, OMO_ETE_DEPTH * OMO_DR_PAYLOAD, &omo_dr_pay_dma[i],
			omo_hostca_to_devva(omo_dr_pay_dma[i]));
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
		if (omo_sr_pay[i]) {
			dma_free_coherent(&omo_pdev->dev,
					  OMO_ETE_DEPTH * OMO_SR_PAYLOAD,
					  omo_sr_pay[i], omo_sr_pay_dma[i]);
			omo_sr_pay[i] = NULL;
		}
	}
	for (i = 0; i < OMO_ETE_DR_N; i++) {
		if (omo_dr_va[i]) {
			dma_free_coherent(&omo_pdev->dev, OMO_ETE_DEPTH * 8,
					  omo_dr_va[i], omo_dr_dma[i]);
			omo_dr_va[i] = NULL;
		}
		if (omo_dr_pay[i]) {
			dma_free_coherent(&omo_pdev->dev,
					  OMO_ETE_DEPTH * OMO_DR_PAYLOAD,
					  omo_dr_pay[i], omo_dr_pay_dma[i]);
			omo_dr_pay[i] = NULL;
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

/* PHASE 38 - the D2H service routine, from the verified map (phase37 sections 1.2/3.1): when
 * out[1] is nonzero, ack 0x40101438 = 1, read+clear out[1] 0x40039014, re-arm 0x40101414 = 1,
 * then dispatch the lowest set bit among the registered ids {1,3,6,7} - none of which owes a
 * device reply.  The forbidden CA 0x400392f0 is not touched (that is the DEVICE's ack). */
static unsigned int omo_d2hsvc_en;
module_param_named(d2hsvc, omo_d2hsvc_en, uint, 0444);
MODULE_PARM_DESC(d2hsvc, "1 = service out[1] as the vendor's pcie_msg_handle does (phase 38)");

#define OMO_D2H_ACK	0x101438UL	/* CA 0x40101438 within the region-3 window */
#define OMO_D2H_REARM	0x101414UL	/* CA 0x40101414 */

static void omo_d2h_service(u32 pending)
{
	u32 id = 0;
	const char *what;

	while (pending && !(pending & 1)) {
		pending >>= 1;
		id++;
	}
	iowrite32(1U, omo_rel + OMO_D2H_ACK);
	iowrite32(0, omo_msg + OMO_MSG1);
	iowrite32(1U, omo_rel + OMO_D2H_REARM);
	pr_info("omo-drv1: [d2hsvc] ack=1 clear re-arm=1 for id %u\n", id);

	switch (id) {
	case 1:
		what = "empty stub (device-ready is the HCC group-4 id 1) - no reply";
		break;
	case 3:
		what = "ETE transfer done - drain DR channels 0..4 - no reply";
		break;
	case 6:
	case 7:
		what = "trigger ETE sending - wake the SR/TX pump - no reply";
		break;
	default:
		what = "UNREGISTERED - the vendor's 0x1739c path drops it";
		break;
	}
	pr_info("omo-drv1: [d2hsvc] id %u: %s\n", id, what);
}

static void omo_poll_mailbox(void)
{
	unsigned long elapsed = 0;
	u32 p0 = omo_rd(omo_msg, OMO_MSG0);
	u32 p1 = omo_rd(omo_msg, OMO_MSG1);
	u32 p5 = omo_rd(omo_msg, OMO_MSG5);
	unsigned int seen = 0;

	pr_info("omo-drv1: ---- post-release mailbox poll (%u ms interval, %u ms total) ----\n",
		omo_pollms, omo_polldur);
	pr_info("omo-drv1:   t=0  out[0]=0x%08x out[1]=0x%08x out[5]=0x%08x (baseline)\n",
		p0, p1, p5);

	while (elapsed < omo_polldur) {
		u32 n0, n1, n5;
		int bit;

		msleep(omo_pollms);
		elapsed += omo_pollms;

		n0 = omo_rd(omo_msg, OMO_MSG0);
		n1 = omo_rd(omo_msg, OMO_MSG1);
		n5 = omo_rd(omo_msg, OMO_MSG5);
		if (n0 == p0 && n1 == p1 && n5 == p5)
			continue;

		if (omo_d2hsvc_en && n1 && n1 != p1)
			omo_d2h_service(n1 & ~p1);

		pr_info("omo-drv1:   t=%lums out[0] 0x%08x -> 0x%08x, out[1] 0x%08x -> 0x%08x, out[5] 0x%08x -> 0x%08x\n",
			elapsed, p0, n0, p1, n1, p5, n5);
		if (n5 != p5)
			pr_info("omo-drv1:     out[5] CA 0x400392f0 changed %s (the firmware's own ack register)\n",
				n5 ? "ASSERTED" : "cleared");
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
		p5 = n5;
		seen++;
	}

	pr_info("omo-drv1:   poll done: %u transitions in %lu ms; final out[0]=0x%08x out[1]=0x%08x\n",
		seen, elapsed, omo_rd(omo_msg, OMO_MSG0), omo_rd(omo_msg, OMO_MSG1));
	if (!seen)
		pr_info("omo-drv1:   NOTE no mailbox transition in this window - the firmware produced nothing\n");
}

/* PHASE 35 - is the ack a pulse the 25ms poll keeps missing?
 * The firmware provably consumes the announce word out[0] at t=25ms after the release.  If
 * that consumption is the dispatcher running, its ack write to out[5] should appear at the
 * same moment - and every poll so far sampled at 25ms, which would miss a us-scale pulse.
 * Sample out[5] and out[0] at 100us for 500ms right after the release. */
static unsigned int omo_ackfast_en;
module_param_named(ackfast, omo_ackfast_en, uint, 0444);
MODULE_PARM_DESC(ackfast,
	"1 = fast-sample out[5]/out[0] at 100us for 500ms right after the release");

static void omo_ackfast(void)
{
	u32 a0 = omo_rd(omo_msg, OMO_MSG5);
	u32 o0 = omo_rd(omo_msg, OMO_MSG0);
	int j;
	int ch = 0;

	pr_info("omo-drv1: [ackfast] start: out5=0x%08x out0=0x%08x\n", a0, o0);
	for (j = 0; j < 5000; j++) {
		u32 a = omo_rd(omo_msg, OMO_MSG5);
		u32 o = omo_rd(omo_msg, OMO_MSG0);

		udelay(100);
		if (a != a0 || o != o0) {
			pr_info("omo-drv1: [ackfast] CHANGE iter %u (t~%u.%02ums): out5 %08x -> %08x, out0 %08x -> %08x\n",
				j, j / 10, (j % 10) * 10, a0, a, o0, o);
			a0 = a;
			o0 = o;
			ch++;
		}
	}
	pr_info("omo-drv1: [ackfast] done: out5=0x%08x out0=0x%08x changes=%d\n", a0, o0, ch);
}

/*
 * Load FIRMWARE.bin into the chip.  THIS MUST HAPPEN BEFORE THE RELEASE.
 *
 * The 2026-10-02 poll run released the CPU with no firmware in place and the poll produced
 * NOTHING in 8 s, while phase 19 saw words at +1.85 s - the difference is exactly that phase 19's
 * sequence is "place the firmware, THEN release" (docs/phase19/release-attempts.md A.1: the
 * 0x5a5a write sits in firmware_download AFTER the image is written).  Releasing an empty chip
 * starts a CPU with nothing to run.
 *
 * Path and target are the phase-18 verified ones: FIRMWARE.bin -> BAR0+0x6f8000 (device CA
 * 0x01240000), written through region 5 (ACP-fw), verified there with diffs=0.
 */
#define OMO_FW_TARGET	0x6f8000UL	/* device CA 0x01240000 */
#define OMO_FW_CHUNK	0x80000UL

/* its own mapping: the firmware target is outside every window above */
#define OMO_FW_WIN	0x6f8000UL
#define OMO_FW_BYTES	0x100000UL	/* 1 MiB covers the 928,920-byte image */

static void *omo_fw;
static size_t omo_fw_len;

static int omo_load_fw(void)
{
	struct file *f;
	loff_t pos = 0;
	ssize_t n;
	size_t done = 0;

	f = filp_open(omo_fwpath, O_RDONLY, 0);
	if (IS_ERR(f)) {
		pr_err("omo-drv1: filp_open(%s) failed %ld\n", omo_fwpath, PTR_ERR(f));
		return PTR_ERR(f);
	}
	omo_fw_len = i_size_read(file_inode(f));
	if (!omo_fw_len || omo_fw_len > 16UL * 1024 * 1024) {
		pr_err("omo-drv1: bad firmware size %zu\n", omo_fw_len);
		filp_close(f, NULL);
		return -EINVAL;
	}
	omo_fw = vmalloc(omo_fw_len);
	if (!omo_fw) {
		filp_close(f, NULL);
		return -ENOMEM;
	}
	while (done < omo_fw_len) {
		n = kernel_read(f, omo_fw + done, omo_fw_len - done, &pos);
		if (n <= 0) {
			pr_err("omo-drv1: kernel_read stopped at %zu/%zu (n=%zd)\n",
			       done, omo_fw_len, n);
			filp_close(f, NULL);
			vfree(omo_fw);
			omo_fw = NULL;
			return n ? (int)n : -EIO;
		}
		done += n;
	}
	filp_close(f, NULL);
	pr_info("omo-drv1: firmware file %s size=%zu bytes\n", omo_fwpath, omo_fw_len);
	return 0;
}

/* Write the loaded image, then read it back and report the diff count (phase 18: diffs=0). */
static int omo_write_fw(void)
{
	size_t off = 0;
	unsigned int diffs = 0;

	if (!omo_fw || !omo_fw_len) {
		pr_err("omo-drv1: no firmware loaded\n");
		return -EINVAL;
	}
	while (off < omo_fw_len) {
		size_t n = omo_fw_len - off;

		if (n > OMO_FW_CHUNK)
			n = OMO_FW_CHUNK;
		memcpy_toio(omo_fwmap + off, omo_fw + off, n);
		off += n;
	}
	pr_info("omo-drv1: firmware written to BAR0+0x%lx (%zu bytes)\n",
		(unsigned long)OMO_FW_TARGET, omo_fw_len);
	for (off = 0; off < omo_fw_len; off++) {
		if (ioread8(omo_fwmap + off) != ((u8 *)omo_fw)[off])
			diffs++;
	}
	pr_info("omo-drv1: firmware readback diffs=%u %s\n", diffs,
		diffs == 0 ? "match=YES" : "match=NO");
	return diffs == 0 ? 0 : -EIO;
}

/*
 * The glue-status service loop - the route phases 20 and 22 both name as the point a raw takeover
 * never reaches, reproduced WITHOUT an interrupt line (the takeover has none: PCI_INTERRUPT_LINE is
 * 0xff and the vendor's action that hosts it does not exist here).
 *
 * Shape, from docs/phase20/message-service.md A.2 [proven]:
 *   oal_pcie_transfer_done @0x83e4 reads the PCIe glue status, masks off the non-device bits
 *   (0xff000000 / 0xe00000 / 0xf800 / 0xf8), ORs the remainder into the bridge status register
 *   (that write-back IS the clear), then fans out to pcie_ete_h2d_isr_handle,
 *   pcie_ete_d2h_isr_handle and pcie_intr_handle.
 *   pcie_intr_handle @0x82e4 then reads the SAME status masked to 0x3d8 (bits 3,4,6,7,8,9) and calls
 *   handler[lowest set bit] from the table at comm+0x48 (arg at comm+0x4c).
 *
 * Our takeover has no ISR, so this polls that status in a thread at the same cadence an ISR would be
 * entered, and performs the same clear + dispatch. It is a faithful reproduction of the DECISION
 * LOGIC, not of the interrupt: if the firmware needs a real IRQ edge to make progress, this will not
 * supply it - and the run will say so, which is itself the answer.
 *
 * Read-mostly: the only write is the masked status write-back that the vendor performs to clear it.
 */
#define OMO_GLUE_STAT	0x2ec		/* within the message window -> BAR0 0x3f12ec */
#define OMO_GLUE_MASK	0x3d8U		/* bits 3,4,6,7,8,9 - pcie_intr_handle's mask */
#define OMO_STAT_MASK1	0xff000000U
#define OMO_STAT_MASK2	0x00e00000U
#define OMO_STAT_MASK3	0x0000f800U
#define OMO_STAT_MASK4	0x000000f8U

static unsigned int omo_svc;
module_param_named(svc, omo_svc, uint, 0444);
MODULE_PARM_DESC(svc, "1 = poll the PCIe glue status and dispatch, standing in for the missing ISR");

static unsigned int omo_svcms = 200;
module_param_named(svcms, omo_svcms, uint, 0444);
MODULE_PARM_DESC(svcms, "glue-status poll interval in ms (default 200)");

static unsigned int omo_svcdur = 8000;
module_param_named(svcdur, omo_svcdur, uint, 0444);
MODULE_PARM_DESC(svcdur, "glue-status service duration in ms (default 8000)");

static void omo_glue_service(void)
{
	unsigned long elapsed = 0;
	unsigned int events = 0;
	u32 first = omo_rd(omo_msg, OMO_GLUE_STAT);

	pr_info("omo-drv1: ---- glue-status service (%u ms interval, %u ms total) ----\n",
		omo_svcms, omo_svcdur);
	pr_info("omo-drv1:   t=0  glue status BAR0+0x%05lx = 0x%08x (masked 0x%08x)\n",
		(unsigned long)(OMO_MSG_WIN + OMO_GLUE_STAT), first, first & OMO_GLUE_MASK);

	while (elapsed < omo_svcdur) {
		u32 st;

		msleep(omo_svcms);
		elapsed += omo_svcms;
		st = omo_rd(omo_msg, OMO_GLUE_STAT);

		if (st & OMO_GLUE_MASK) {
			u32 keep = st;
			int bit;

			pr_info("omo-drv1:   t=%lums status 0x%08x -> masked 0x%08x PENDING\n",
				elapsed, st, st & OMO_GLUE_MASK);
			for (bit = 0; bit < 32; bit++)
				if ((st & OMO_GLUE_MASK) & (1U << bit))
					pr_info("omo-drv1:     status bit %d set -> dispatch handler[%d]\n",
						bit, bit);

			/* the vendor's clear: mask off non-device bits, write the remainder back */
			keep &= ~OMO_STAT_MASK1;
			keep &= ~OMO_STAT_MASK2;
			keep &= ~OMO_STAT_MASK3;
			keep &= ~OMO_STAT_MASK4;
			pr_info("omo-drv1:     write-back 0x%08x (the vendor's clear)\n", keep);
			iowrite32(keep, omo_msg + OMO_GLUE_STAT);
			pr_info("omo-drv1:     after clear: 0x%08x\n",
				omo_rd(omo_msg, OMO_GLUE_STAT));
			events++;
			first = omo_rd(omo_msg, OMO_GLUE_STAT);
		}
	}

	pr_info("omo-drv1:   service done: %u pending events in %lu ms; final status=0x%08x\n",
		events, elapsed, omo_rd(omo_msg, OMO_GLUE_STAT));
	pr_info("omo-drv1:   (glue status read at BAR0+0x%05lx = CA 0x400392ec)\n",
		(unsigned long)(OMO_MSG_WIN + OMO_GLUE_STAT));
	if (!events)
		pr_info("omo-drv1:   NOTE the glue status never asserted in this window\n");
}

/*
 * The release SIGNATURE - whether the chip actually left ROM state.
 *
 * Phase 19 (docs/phase19/release-attempts.md A.2/A.3) measured the difference between a frozen chip
 * and a running one.  A readback match on the release register only proves the register accepted the
 * value; it does NOT prove the CPU started.  This reads the same signature phase 19 used, before and
 * after the release, so "the release worked" becomes an observation instead of an assumption.
 *
 * Register addresses follow the region mapping: for a device CA in 0x40000000..0x4011ffff the BAR0
 * offset is 0x3b8000 + (CA - 0x40000000) (region 3); the firmware/stack words live in region 5
 * (dev 0x01200000 -> host 0x406b8000).
 */
struct omo_sigreg {
	const char *name;
	unsigned long off;	/* BAR0 offset */
	int window;		/* 0 = omo_rel, 1 = omo_msg, 2 = fw window */
};

static const struct omo_sigreg omo_sig[] = {
	{ "fw BSS +0x00",	0x7eac18UL, 2 },
	{ "fw BSS +0x04",	0x7eac1cUL, 2 },
	{ "fw BSS +0x08",	0x7eac20UL, 2 },
	{ "fw BSS +0x0c",	0x7eac24UL, 2 },
	{ "dcoldo_vset",	0x3bd00cUL, 0 },
	{ "pbank_code",		0x3bd05cUL, 0 },
	{ "abank_code",		0x3bd060UL, 0 },
	{ "tcxo_pll_mux",	0x4b9230UL, 0 },
	{ "tcxo_pll_stat",	0x4b9234UL, 0 },
};

static void __iomem *omo_sigwin(int w)
{
	switch (w) {
	case 0:	return omo_rel;
	case 1:	return omo_msg;
	case 2:	return omo_fwmap;
	default:	return NULL;
	}
}

static void omo_sig_read(const char *tag, u32 *out)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(omo_sig); i++) {
		void __iomem *w = omo_sigwin(omo_sig[i].window);
		unsigned long base = (omo_sig[i].window == 2) ? OMO_FW_WIN
				   : (omo_sig[i].window == 1) ? OMO_MSG_WIN : OMO_IO_WIN;

		out[i] = w ? omo_rd(w, omo_sig[i].off - base) : 0;
	}
	pr_info("omo-drv1: [sig %s] BSS=%08x/%08x/%08x/%08x dcoldo=%08x pbank=%08x abank=%08x tcxo=%08x/%08x\n",
		tag, out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7], out[8]);
}

/*
 * The recovered host half of the message service, run against the CORRECT registers.
 *
 * docs/phase20/msg-host-half.md recovered the contract [proven]: pcie_msg_handle reads the pending
 * mask, writes 1 to the ack, clears the pending word, writes 1 to the re-arm, then dispatches the
 * lowest set bit through the handler table.  The three registers, by the region-3 translation
 * (offset = 0x3b8000 + (CA - 0x40000000)):
 *
 *   pending out[1] CA 0x40039014 -> 0x3f1014   (the message window, +0x014)
 *   ack     out[3] CA 0x40101438 -> 0x4b9438   (region 3, well above the message window)
 *   re-arm  out[4] CA 0x40101414 -> 0x4b9414   (region 3)
 *   out[5]  CA 0x400392f0 -> 0x3f12f0          NEVER WRITTEN by this module (project rule)
 *
 * Until phase 23x the module read the pending word one page low, so this sequence was never
 * actually performed against the real registers.  Now that the addresses are right, running it is a
 * real test of whether the dialogue advances past the firmware's single ready word.
 */
#define OMO_ACK_OFF	0x4b9438UL	/* BAR0: out[3], CA 0x40101438 */
#define OMO_REARM_OFF	0x4b9414UL	/* BAR0: out[4], CA 0x40101414 */

static unsigned int omo_msgsvc;
module_param_named(msgsvc, omo_msgsvc, uint, 0444);
MODULE_PARM_DESC(msgsvc, "1 = service the mailbox (ack + clear + re-arm + dispatch) instead of only observing");

static unsigned int omo_svcdur2 = 10000;
module_param_named(msgsvc_dur, omo_svcdur2, uint, 0444);
MODULE_PARM_DESC(msgsvc_dur, "mailbox service duration in ms (default 10000)");

static void omo_msg_service(void)
{
	unsigned long elapsed = 0;
	unsigned int handled = 0;
	u32 prev = omo_rd(omo_msg, OMO_MSG1);

	pr_info("omo-drv1: ---- mailbox service: ack/clear/re-arm + dispatch (%u ms) ----\n",
		omo_svcdur2);
	pr_info("omo-drv1:   pending out[1] BAR0+0x%05lx = 0x%08x (baseline)\n",
		(unsigned long)(OMO_MSG_WIN + OMO_MSG1), prev);

	while (elapsed < omo_svcdur2) {
		u32 st = omo_rd(omo_msg, OMO_MSG1);
		int bit;

		/* The vendor's pcie_msg_handle runs whenever the pending mask is NON-ZERO - not on a
		 * transition.  An earlier revision keyed on a change and therefore serviced nothing when
		 * the word was already pending at entry (which it is: the firmware asserts it ~500 ms
		 * after the release and it stays set).  Service on the pending STATE. */
		if (st) {
			pr_info("omo-drv1:   t=%lums pending 0x%08x (was 0x%08x)\n", elapsed, st, prev);
			for (bit = 0; bit < 32; bit++)
				if (st & (1U << bit))
					pr_info("omo-drv1:     bit %d pending -> dispatch handler[%d]\n",
						bit, bit);

			/* the vendor's pcie_msg_handle, in its order */
			iowrite32(1, omo_rel + (OMO_ACK_OFF - OMO_IO_WIN));
			pr_info("omo-drv1:     ack   out[3] 0x%05lx <= 0x00000001 readback=0x%08x\n",
				(unsigned long)OMO_ACK_OFF,
				omo_rd(omo_rel, OMO_ACK_OFF - OMO_IO_WIN));
			iowrite32(0, omo_msg + OMO_MSG1);
			pr_info("omo-drv1:     clear out[1] 0x%05lx <= 0 readback=0x%08x\n",
				(unsigned long)(OMO_MSG_WIN + OMO_MSG1),
				omo_rd(omo_msg, OMO_MSG1));
		iowrite32(omo_rearm_val, omo_rel + (OMO_REARM_OFF - OMO_IO_WIN));
		pr_info("omo-drv1:     rearm out[4] 0x%05lx <= 0x%08x readback=0x%08x\n",
			(unsigned long)OMO_REARM_OFF, omo_rearm_val,
			omo_rd(omo_rel, OMO_REARM_OFF - OMO_IO_WIN));

			prev = omo_rd(omo_msg, OMO_MSG1);
			pr_info("omo-drv1:     after service: out[0]=0x%08x out[1]=0x%08x glue=0x%08x\n",
				omo_rd(omo_msg, OMO_MSG0), prev,
				omo_rd(omo_msg, OMO_GLUE_STAT));
			handled++;
		}

		msleep(200);
		elapsed += 200;
	}

	pr_info("omo-drv1:   mailbox service done: %u words serviced in %lu ms; final out[1]=0x%08x\n",
		handled, elapsed, omo_rd(omo_msg, OMO_MSG1));
	if (!handled)
		pr_info("omo-drv1:   NOTE nothing was pending to service in this window\n");
}

/*
 * Host -> device send, in the vendor's pcie_msg_send form.
 *
 * docs/phase20/msg-host-half.md: pcie_msg_send indexes the same context from +0x2c:
 *   out[0] (CA 0x40039010 -> BAR0 0x3f1010, offset +0x010) receives the message-id BITMAP
 *   out[2] (CA 0x400392d4 -> BAR0 0x3f12d4, offset +0x2d4) is the doorbell; bit 0 is ORed in
 *
 * DELIBERATE OMISSION: the vendor also has pcie_msg_send_irq, which additionally writes 8 to
 * out[5] (CA 0x400392f0).  That register is on this project's forbidden list (writing it hangs the
 * chip - phase 20 BOOT B), so this module performs the out[0]+out[2] form ONLY.  If a send needs the
 * out[5] arm to be seen, that will show up as the device ignoring it, and it must be solved another
 * way rather than by breaking the rule.
 */
#define OMO_MSG_DOORBELL	0x2d4	/* within the message window -> BAR0 0x3f12d4 (out[2]) */

static int omo_send = -1;
module_param_named(send, omo_send, int, 0444);
MODULE_PARM_DESC(send, "message id bit to send to the device (0-31); -1 = do not send (default)");

static unsigned int omo_senddur = 6000;
module_param_named(senddur, omo_senddur, uint, 0444);
MODULE_PARM_DESC(senddur, "post-send observation window in ms (default 6000)");

static void omo_h2d_send(void)
{
	u32 mask = 1U << omo_send;
	u32 m0, m2, rb0, rb2;
	unsigned long elapsed = 0;
	u32 l0, l1, lg;

	pr_info("omo-drv1: ---- H2D send: id %d (bitmap 0x%08x) via out[0] + doorbell out[2] ----\n",
		omo_send, mask);
	pr_info("omo-drv1:   NOTE out[5] (CA 0x400392f0) is deliberately NOT written (project rule)\n");

	m0 = omo_rd(omo_msg, OMO_MSG0);
	iowrite32(m0 | mask, omo_msg + OMO_MSG0);
	rb0 = omo_rd(omo_msg, OMO_MSG0);
	pr_info("omo-drv1:   out[0] 0x%05lx 0x%08x -> 0x%08x readback=0x%08x match=%s\n",
		(unsigned long)(OMO_MSG_WIN + OMO_MSG0), m0, m0 | mask, rb0,
		rb0 == (m0 | mask) ? "YES" : "NO");

	m2 = omo_rd(omo_msg, OMO_MSG_DOORBELL);
	iowrite32(m2 | 1U, omo_msg + OMO_MSG_DOORBELL);
	rb2 = omo_rd(omo_msg, OMO_MSG_DOORBELL);
	pr_info("omo-drv1:   out[2] 0x%05lx 0x%08x -> 0x%08x readback=0x%08x match=%s (doorbell)\n",
		(unsigned long)(OMO_MSG_WIN + OMO_MSG_DOORBELL), m2, m2 | 1U, rb2,
		rb2 == (m2 | 1U) ? "YES" : "NO");

	l0 = omo_rd(omo_msg, OMO_MSG0);
	l1 = omo_rd(omo_msg, OMO_MSG1);
	lg = omo_rd(omo_msg, OMO_GLUE_STAT);
	pr_info("omo-drv1:   observing %u ms for a response...\n", omo_senddur);

	while (elapsed < omo_senddur) {
		u32 n0 = omo_rd(omo_msg, OMO_MSG0);
		u32 n1 = omo_rd(omo_msg, OMO_MSG1);
		u32 ng = omo_rd(omo_msg, OMO_GLUE_STAT);
		int bit;

		if (n0 != l0 || n1 != l1 || ng != lg) {
			pr_info("omo-drv1:   t=%lums out[0] 0x%08x->0x%08x out[1] 0x%08x->0x%08x glue 0x%08x->0x%08x\n",
				elapsed, l0, n0, l1, n1, lg, ng);
			for (bit = 0; bit < 32; bit++) {
				if ((n0 & (1U << bit)) != (l0 & (1U << bit)))
					pr_info("omo-drv1:     out[0] bit %d %s by the DEVICE\n", bit,
						(n0 & (1U << bit)) ? "SET" : "CLEARED");
				if ((n1 & (1U << bit)) != (l1 & (1U << bit)))
					pr_info("omo-drv1:     out[1] bit %d %s by the DEVICE\n", bit,
						(n1 & (1U << bit)) ? "SET" : "CLEARED");
			}
			l0 = n0;
			l1 = n1;
			lg = ng;
		}
		msleep(200);
		elapsed += 200;
	}

	pr_info("omo-drv1:   send observation done: final out[0]=0x%08x out[1]=0x%08x glue=0x%08x\n",
		omo_rd(omo_msg, OMO_MSG0), omo_rd(omo_msg, OMO_MSG1),
		omo_rd(omo_msg, OMO_GLUE_STAT));
	if (omo_rd(omo_msg, OMO_MSG0) & mask)
		pr_info("omo-drv1:   NOTE the sent bit is STILL SET - the device did not consume it\n");
	else
		pr_info("omo-drv1:   NOTE the sent bit was CLEARED - the device consumed the message\n");
}

/*
 * The vendor's first host->device SR frame, captured live by phase 20 (fwaccept: omo_sr_msg).
 *
 * PHASE 25o - THE ONE-VARIABLE TEST.  A write-atomic capture (write index + all descriptors in one ssh
 * call, then the just-published slot's own buffer in the next) gave a BOUND message of the port's own
 * type: '04000100 001d0048 5a5a0000' - i.e. type 0x04000100, +4..5 = 0x0048 (72), +6..7 = 0x001d, magic
 * 0x5a5a at +0xa.  Against this frame that is every field equal EXCEPT +0x06 (vendor 0x001d, port 0x0001).
 * Phase 25i had set +6 = 0x001d but in the SAME edit as +0, moving the type to 0x01000100; phase 25k
 * restored both together - so this combination was never actually run.  Only +0x06 changes here.
 *
 * PHASE 25w - REPLICATE MESSAGE B EXACTLY.  Two clean single-variable replicas were defined when the
 * port's frame was found to be a MIX of two real vendor messages (docs/phase25/frame-is-a-chimera.md).
 * Replica A (len 0x0048 id 0x001d, zero body) ran and was SILENT - identical to every mix.  This is
 * replica B, the vendor's FIRST frame on a fresh SR ring, byte-for-byte from docs/phase20/tx-path.md A.5:
 * '+00 0x04000100  +04 0x00010030  +08 0x5A5A0000  +0c 0  +10 1  +14 0x001400D8  +18 1  +1c..+28 0
 *  +2c..+44 0xFFFFFFFF x7'.  Read as fields: +0x04 is a u16 TOTAL LENGTH = 0x0030 (48) and +0x06 is
 * the u16 id = 0x0001 - so length and id travel together (48 with 1, 72 with 29) and this is the (48,1)
 * member of the pair.  The SR descriptor still announces 72, which is exactly the vendor's own
 * arrangement (its descriptor says 0x48 while the message inside says 0x30).
 */
static const u8 omo_sr_msg[OMO_SR_MSG_LEN] = {
	0x00, 0x01, 0x00, 0x04, 0x30, 0x00, 0x01, 0x00,
	0x00, 0x00, 0x5a, 0x5a, 0x00, 0x00, 0x00, 0x00,
	0x01, 0x00, 0x00, 0x00, 0xd8, 0x00, 0x14, 0x00,
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

/*
 * PHASE 25y - THE OFFSET BECOMES AN EXPLICIT VARIABLE.  Phase 25 established that an announce is only
 * read if it is PENDING AT RELEASE, because the firmware clears the message registers during its own
 * boot.  Every run since has satisfied that ordering - but only as an ACCIDENT of code layout: the
 * init path is sr_post (announce) -> load_fw -> write_fw -> release, so the gap between the announce and
 * the release is whatever the firmware load happens to take, and it has never been chosen or swept.
 * With the payload question closed (replicas A and B, both real vendor messages, both silent), the
 * remaining host-controlled variable is that gap.  This delay re-posts the SAME announce after the
 * firmware write and immediately before the release; 0 leaves the historical behaviour untouched.
 */
static unsigned int omo_reannounce_ms;
module_param_named(reannounce, omo_reannounce_ms, uint, 0444);
MODULE_PARM_DESC(reannounce,
	"ms to wait after the firmware write before re-posting the SAME announce and releasing; 0 = historical (announce inside omo_sr_post only)");

/* Re-post the identical announce the SR fill makes, so the message is pending as close to the release
 * as the chosen delay allows.  Same registers, same bits - only the MOMENT differs. */
static void omo_reannounce(void)
{
	u32 a0, a2;

	a0 = omo_rd(omo_msg, OMO_MSG0);
	iowrite32(a0 | 8U, omo_msg + OMO_MSG0);
	pr_info("omo-drv1: RE-ANNOUNCE out[0] <= 0x%08x readback=0x%08x\n",
		a0 | 8U, omo_rd(omo_msg, OMO_MSG0));
	a2 = omo_rd(omo_msg, OMO_MSG_DOORBELL);
	iowrite32(a2 | 1U, omo_msg + OMO_MSG_DOORBELL);
	pr_info("omo-drv1: RE-ANNOUNCE out[2] <= 0x%08x readback=0x%08x (doorbell)\n",
		a2 | 1U, omo_rd(omo_msg, OMO_MSG_DOORBELL));
}

/*
 * PHASE 30 - READ THE FIRMWARE'S LIVE MESSAGE CONTEXT THROUGH THE ACP-FW WINDOW.
 *
 * The mapping is pinned by phase-17 A.6 and the phase-23 signature reads: the firmware image
 * loads at device 0x01240000 (= 0x01200000 + 0x40000) and a firmware runtime address X is
 * visible at BAR0 0x6b8000 + X (region 5, ACP-fw, size 0x218000) - the phase-23 BSS signature
 * read at BAR0 0x7eac18 is firmware addr 0x132c18, which confirms it.  The firmware's
 * pcie_msg_init stores its ctx object pointer in the GLOBAL at runtime 0x172130 (file 0x9706),
 * and the record (phase20 A.1, phase22 fw-hostmem) says the ctx fields at [obj]+0xd0..0xe4 hold
 * the six mailbox CAs with the handler table at [obj]+0xbc.  Reading them LIVE shows the
 * vendor's own binding - the thing phase 22 named as "reproducing the vendor's message-
 * context/ISR binding", which no register poke could see.
 */
static unsigned int omo_fwctx_en;
module_param_named(fwctx, omo_fwctx_en, uint, 0444);
MODULE_PARM_DESC(fwctx,
	"1 = read the firmware's live pcie_msg ctx through the ACP-fw window (phase 30)");

#define OMO_ACP_BAR0	0x6b8000UL	/* region 5 BAR0 base; firmware addr X -> BAR0 0x6b8000 + X */
#define OMO_ACP_BYTES	0x218000UL	/* region 5 size */
#define OMO_FWCTX_GLOBAL 0x172130UL	/* firmware runtime addr of the ctx pointer global */

static void __iomem *omo_acp;
static void __iomem *omo_irqwin;

static void omo_fwctx_read(void)
{
	u32 g, v[80];
	int i;

	if (!omo_acp)
		return;
	g = readl(omo_acp + OMO_FWCTX_GLOBAL);
	pr_info("omo-drv1: [fwctx] global @firmware 0x%x (BAR0 0x%lx) = 0x%08x\n",
		OMO_FWCTX_GLOBAL, (unsigned long)(OMO_ACP_BAR0 + OMO_FWCTX_GLOBAL), g);
	if (g < 0x40000 || g >= OMO_ACP_BYTES) {
		pr_info("omo-drv1: [fwctx] ctx pointer 0x%08x outside the ACP window - cannot dump\n",
			g);
		return;
	}
	for (i = 0; i < 80; i++)
		v[i] = readl(omo_acp + (g - 0x10) + i * 4);
	pr_info("omo-drv1: [fwctx] ctx object @firmware 0x%08x, dump (offset relative to it):\n", g);
	for (i = 0; i < 80; i += 4)
		pr_info("omo-drv1: [fwctx]   %+4ld: %08x %08x %08x %08x\n",
			(long)(i * 4) - 0x10, v[i], v[i + 1], v[i + 2], v[i + 3]);

	/* In NORMAL operation the firmware's H2D handler table lives at firmware 0x118d68
	 * (4 handlers registered: ids 1/3/5/6) and its INTERRUPT fn array at 0x17d430
	 * (base 0x17d398, fn for id N at +N*4+0x98): id 0x2d = 0x62f9, id 0x2e = 0x624d,
	 * flags 0x05/0x01 in the byte table at 0x105f8c.  Dump the interrupt fn array and
	 * the flags in the takeover to see whether the interrupt service is armed. */
	{
		static const struct { u32 a; u32 n; } tabs[] = {
			{ 0x118d68, 20 }, { 0x17d430, 48 }, { 0x105f80, 40 },
		};
		int t, i;

		for (t = 0; t < 3; t++) {
			u32 w[48];

			for (i = 0; i < (int)tabs[t].n; i++)
				w[i] = readl(omo_acp + tabs[t].a + i * 4);
			pr_info("omo-drv1: [fwctx] table @firmware 0x%x:\n", tabs[t].a);
			for (i = 0; i < (int)tabs[t].n; i += 4)
				pr_info("omo-drv1: [fwctx]   +0x%02x: %08x %08x %08x %08x\n",
					i * 4, w[i], w[i + 1], w[i + 2], w[i + 3]);
		}
	}
}

/*
 * PHASE 31 - RING THE H2D DOORBELL POST-RELEASE AND SAMPLE THE INTERRUPT BLOCK + ACK.
 *
 * The gate narrowed (phase 30) to one link: the host's doorbell write (out[2] |= 1 to CA
 * 0x400392d4) never reaches the firmware as an interrupt, so its dispatcher never runs
 * (the ack out[5] CA 0x400392f0 never reads 1).  The firmware's interrupt enable bitmap
 * is at CA 0x40161100 (region 3 -> BAR0 0x3b8000 + 0x161100 = 0x519100).  This samples
 * the block and the ack at high rate across a POST-RELEASE doorbell ring, to see whether
 * the doorbell latches as a pending interrupt at all.  out[5] is only ever READ.
 */
static unsigned int omo_intrsamp_en;
module_param_named(intrsamp, omo_intrsamp_en, uint, 0444);
MODULE_PARM_DESC(intrsamp,
	"1 = ring the doorbell post-release and sample the interrupt block + ack at high rate (phase 31)");

#define OMO_IRQ_BLOCK	0x161100UL	/* region-3 offset of CA 0x40161100 */

static struct pci_dev *omo_ep1_dev;
static void __iomem *omo_ep1_msg;
static void __iomem *omo_ep1_iatu;

static void omo_intrsamp(void)
{
	u32 pre[8], cur[8], ack_pre, ack_cur;
	u32 db;
	unsigned int i, j;

	for (i = 0; i < 8; i++)
		pre[i] = omo_rd(omo_rel, OMO_IRQ_BLOCK + i * 4);
	ack_pre = omo_rd(omo_msg, OMO_MSG5);
	pr_info("omo-drv1: [intrsamp] pre: irqblock=%08x %08x %08x %08x %08x %08x %08x %08x ack(out5)=%08x\n",
		pre[0], pre[1], pre[2], pre[3], pre[4], pre[5], pre[6], pre[7], ack_pre);

	/* the firmware's own re-arm constant for this doorbell is 8 (its dispatcher writes 8 to
	 * 0x400392d4); the port has always rung 1.  Ring BOTH values and sample each. */
	{
		static const u32 vals[2] = { 1U, 8U };
		int v;

		for (v = 0; v < 2; v++) {
			db = omo_rd(omo_msg, OMO_MSG_DOORBELL);
			iowrite32(db | vals[v], omo_msg + OMO_MSG_DOORBELL);
			pr_info("omo-drv1: [intrsamp] doorbell out[2] <= 0x%08x (bit %u) readback=0x%08x\n",
				db | vals[v], v == 0 ? 0 : 3, omo_rd(omo_msg, OMO_MSG_DOORBELL));

			for (j = 0; j < 1000; j++) {
				int any = 0;

				udelay(100);
				for (i = 0; i < 8; i++) {
					cur[i] = omo_rd(omo_rel, OMO_IRQ_BLOCK + i * 4);
					if (cur[i] != pre[i])
						any = 1;
				}
				ack_cur = omo_rd(omo_msg, OMO_MSG5);
				if (ack_cur != ack_pre)
					any = 1;
				if (any) {
					pr_info("omo-drv1: [intrsamp] CHANGE val %u iter %u: irqblock=%08x %08x %08x %08x ack %08x -> %08x\n",
						vals[v], j, cur[0], cur[1], cur[2], cur[3], ack_pre, ack_cur);
					for (i = 0; i < 8; i++)
						pre[i] = cur[i];
					ack_pre = ack_cur;
				}
			}
		}
	}
	pr_info("omo-drv1: [intrsamp] done: final irqblock=%08x %08x %08x %08x ack=%08x\n",
		cur[0], cur[1], cur[2], cur[3], ack_pre);

	/* PHASE 33 - the EP1 doorbell: phase 32 (verified) showed the vendor rings the H2D
	 * doorbell through the SIBLING endpoint's window (chip->dev[0] = EP1 by phy_devid), while
	 * the port rings EP0.  Same device register CA 0x400392d4, different root complex. */
	if (omo_ep1_msg) {
		u32 d1 = omo_rd(omo_ep1_msg, OMO_MSG_DOORBELL);

		iowrite32(d1 | 8U, omo_ep1_msg + OMO_MSG_DOORBELL);
		pr_info("omo-drv1: [intrsamp] EP1 doorbell (sibling window) <= 0x%08x readback=0x%08x\n",
			d1 | 8U, omo_rd(omo_ep1_msg, OMO_MSG_DOORBELL));
		for (j = 0; j < 1000; j++) {
			int any = 0;

			udelay(100);
			for (i = 0; i < 8; i++) {
				cur[i] = omo_rd(omo_rel, OMO_IRQ_BLOCK + i * 4);
				if (cur[i] != pre[i])
					any = 1;
			}
			ack_cur = omo_rd(omo_msg, OMO_MSG5);
			if (ack_cur != ack_pre)
				any = 1;
			if (any) {
				pr_info("omo-drv1: [intrsamp] CHANGE (EP1 ring) iter %u: irqblock=%08x %08x %08x %08x ack %08x -> %08x\n",
					j, cur[0], cur[1], cur[2], cur[3], ack_pre, ack_cur);
				for (i = 0; i < 8; i++)
					pre[i] = cur[i];
				ack_pre = ack_cur;
			}
		}
	}

	/* The spare viewport (phase 34) makes the interrupt block host-visible: sample the
	 * pending register (CA 0x4016010C = win+0x10c) and the enables (0x40161100/0x40161108)
	 * across a doorbell ring, looking for the latch phase 31 could not see. */
	if (omo_irqwin) {
		u32 p0 = readl(omo_irqwin + 0x10c);
		u32 e00 = readl(omo_irqwin + 0x1100);
		u32 e10 = readl(omo_irqwin + 0x1108);

		pr_info("omo-drv1: [intrsamp] irqwin pre: pending=0x%08x en0=0x%08x en1=0x%08x\n",
			p0, e00, e10);
		iowrite32(8U, omo_msg + OMO_MSG_DOORBELL);
		for (j = 0; j < 1000; j++) {
			u32 p = readl(omo_irqwin + 0x10c);
			u32 e0 = readl(omo_irqwin + 0x1100);
			u32 e1 = readl(omo_irqwin + 0x1108);

			udelay(100);
			if (p != p0 || e0 != e00 || e1 != e10) {
				pr_info("omo-drv1: [intrsamp] irqwin CHANGE iter %u: pending %08x -> %08x en0 %08x -> %08x en1 %08x -> %08x\n",
					j, p0, p, e00, e0, e10, e1);
				p0 = p;
				e00 = e0;
				e10 = e1;
			}
		}
		pr_info("omo-drv1: [intrsamp] irqwin done: pending=0x%08x en0=0x%08x en1=0x%08x\n",
			p0, e00, e10);
	}
}

/* PHASE 33 - claim and map the SIBLING endpoint (0001:00:00.0, EP1) so its window can ring
 * the H2D doorbell, exactly as the vendor's chip->dev[0] does.  Only the doorbell CA 0x400392d4
 * is written through it; out[5] CA 0x400392f0 is never written. */
static unsigned int omo_ep1db_en;
module_param_named(ep1db, omo_ep1db_en, uint, 0444);
MODULE_PARM_DESC(ep1db,
	"1 = also claim 0001:00:00.0 and ring the H2D doorbell through its window (phase 33)");

static int omo_ep1_init(void)
{
	resource_size_t lo;
	int rc;

	omo_ep1_dev = pci_get_domain_bus_and_slot(1, 0, PCI_DEVFN(0, 0));
	if (!omo_ep1_dev) {
		pr_err("omo-drv1: [ep1db] sibling 0001:00:00.0 not found\n");
		return -ENODEV;
	}
	rc = pci_enable_device(omo_ep1_dev);
	if (rc) {
		pr_err("omo-drv1: [ep1db] pci_enable_device rc=%d\n", rc);
		return rc;
	}
	rc = pci_request_mem_regions(omo_ep1_dev, "omo-ep1");
	if (rc) {
		pr_err("omo-drv1: [ep1db] request regions rc=%d\n", rc);
		return rc;
	}
	pci_read_config_dword(omo_ep1_dev, PCI_BASE_ADDRESS_0, (u32 *)&lo);
	lo &= PCI_BASE_ADDRESS_MEM_MASK;
	pr_info("omo-drv1: [ep1db] sibling BAR0 = 0x%llx\n", (unsigned long long)lo);

	/* The first run proved the bare claim is not enough: EP1's readback was 0xffffffff
	 * (PCIe no-decode) because the takeover only programs EP0's inbound viewports.  Program
	 * the SAME six inbound regions on the sibling, shifted by its BAR0, exactly as the vendor
	 * does for both functions. */
	omo_ep1_iatu = pci_iomap(omo_ep1_dev, OMO_IATU_BAR, 0);
	if (!omo_ep1_iatu) {
		pr_err("omo-drv1: [ep1db] iomap sibling iATU failed\n");
		return -ENOMEM;
	}
	omo_program_inbound(omo_ep1_iatu, lo, "ep1-inbound");

	omo_ep1_msg = ioremap(lo + OMO_MSG_WIN, OMO_MSG_BYTES);
	if (!omo_ep1_msg) {
		pr_err("omo-drv1: [ep1db] ioremap sibling window failed\n");
		return -ENOMEM;
	}
	return 0;
}

/*
 * Host -> device: post SR nodes and commit the producer index.
 *
 * Phase 24i proved this is the trigger for the firmware's id-6 word
 * (pcie_trigger_ete_sending_handle): fwaccept, which posts, sees id 6 -> id 2 -> clear; wifidrv1,
 * which writes the ring configuration only, sees id 2 alone.  Until this was added the port had
 * removed the trigger for the very word the later phases were trying to elicit.
 *
 * word0 = the payload buffer's device VA (devva_base == hostca_base == 0x80000000 here, so the
 * mapping is the identity); word1 = (len << 16) | 0x6d2b.  The producer index is committed to
 * SR+0x18 as the vendor's packed index (index[9:0] | phase[10]) - 32 nodes at depth 32 wrap the
 * index and flip the phase, which is where the recorded 0x400 comes from.
 */
static u32 omo_ring_ptr_plus(u32 idx, u32 depth)
{
	u32 i = (idx & 0x3ffU) + 1;
	u32 ph = (idx >> 10) & 1U;

	if (i >= depth) {
		i = 0;
		ph ^= 1U;
	}
	return (ph << 10) | i;
}

static void omo_program_outbound(void)
{
	pr_info("omo-drv1: outbound viewport0: devva 0x%08lx..0x%08lx -> host 0x%08lx (oal_pcie_set_outbound_by_membar @0x9a38)\n",
		OMO_DEVVA_BASE, 0xffffffffUL, OMO_HOSTCA_BASE);
	omo_iatu_wr(omo_iatu, 0x000, 0, "out", "ctrl1=0");
	omo_iatu_wr(omo_iatu, 0x004, 0x80000000U, "out", "ctrl2=ena|bar0");
	omo_iatu_wr(omo_iatu, 0x008, (u32)OMO_DEVVA_BASE, "out", "base_lo");
	omo_iatu_wr(omo_iatu, 0x00c, 0, "out", "base_hi");
	omo_iatu_wr(omo_iatu, 0x010, 0xffffffffU, "out", "limit");
	omo_iatu_wr(omo_iatu, 0x014, (u32)OMO_HOSTCA_BASE, "out", "target_lo");
	omo_iatu_wr(omo_iatu, 0x018, 0, "out", "target_hi");
}

static unsigned int omo_srpost_en;
module_param_named(srpost, omo_srpost_en, uint, 0444);
MODULE_PARM_DESC(srpost, "1 = post SR descriptor nodes + commit the producer index + enable the channel (the phase-24i-proven trigger for the firmware's id-6 word); requires wr=1");

/*
 * The descriptor length the SR nodes announce for the vendor's first frame.
 *
 * The frame is 72 bytes and its OWN header at +4 says 48, so the two length fields disagree (slot 1's
 * alg frame is self-consistent at 298/298, see docs/phase24/frame-field-check.md).  This parameter
 * makes the announced length testable without a rebuild per value: 0 keeps the captured 0x48, and
 * 0x30 matches the message's own header.
 */
static unsigned int omo_sr_desclen;
module_param_named(sr_desclen, omo_sr_desclen, uint, 0444);
MODULE_PARM_DESC(sr_desclen, "SR slot-0 descriptor length override (0 = the captured 0x48; 0x30 matches the frame's own header field)");

/*
 * Announce the SR post the way the vendor's own fill routine does.
 *
 * shuangta_ete_sr_dscr_fill @0x17858 ends with `mov r1, #3; bl pcie_msg_send` - i.e. it announces
 * `out[0] |= 8` (id 3) and the `out[2]` doorbell as the LAST act of filling, per descriptor, BEFORE
 * anything else runs.  The port has announced id 3 too, but once and much later (after the poll and
 * the service).  Same id, same registers - different moment.  Default 1: do it as the vendor does.
 */
static unsigned int omo_sr_announce = 1;
module_param_named(sr_announce, omo_sr_announce, uint, 0444);
MODULE_PARM_DESC(sr_announce, "1 = announce id 3 (out[0] bitmap + out[2] doorbell) inside omo_sr_post, as shuangta_ete_sr_dscr_fill does; 0 = only the later omo_h2d_send");

/* PHASE 38 - the HCC frame builder, from the verified protocol map (phase37 section 2.2).
 * The SR node's buffer is an HCC message: a 12-byte header + payload.  Header fields:
 * +0x00 group/type (low nibble), +0x01 alloc-state (low nibble, 1 = allocated) + resource-group
 * index (high nibble), +0x02/+0x03 field A/B, +0x04 u16 total length (payload + 12), +0x06 u16
 * message id, +0x08 field C, +0x09 retry, +0x0a the SR-layer tag 0x5a5a, payload at +0x0c. */
static unsigned int omo_hccpost_en;
module_param_named(hccpost, omo_hccpost_en, uint, 0444);
MODULE_PARM_DESC(hccpost, "1 = build the SR node body as a real HCC frame (phase 38)");
static unsigned int omo_hcc_group;
module_param_named(hccgroup, omo_hcc_group, uint, 0444);
MODULE_PARM_DESC(hccgroup, "HCC group/type nibble for the posted frame (default 0)");
static unsigned int omo_hcc_id;
module_param_named(hccid, omo_hcc_id, uint, 0444);
MODULE_PARM_DESC(hccid, "HCC message id for the posted frame (default 0)");
static unsigned int omo_hcc_len = 16;
module_param_named(hcclen, omo_hcc_len, uint, 0444);
MODULE_PARM_DESC(hcclen, "HCC payload length for the posted frame (default 16)");
static unsigned int omo_hcc_res;
module_param_named(hccres, omo_hcc_res, uint, 0444);
MODULE_PARM_DESC(hccres,
	"HCC resource-group index (high nibble of header byte 1; default 0 - the announce-body lane names the per-message value)");

static void omo_hcc_build(void *buf, unsigned int group, unsigned int id,
			  unsigned int payload_len)
{
	u8 *h = buf;
	unsigned int total = payload_len + 12;

	memset(buf, 0, total);
	h[0x00] = (u8)(group & 0xf);
	h[0x01] = (u8)(0x1 | ((omo_hcc_res & 0xf) << 4));
	h[0x04] = (u8)(total & 0xff);
	h[0x05] = (u8)((total >> 8) & 0xff);
	h[0x06] = (u8)(id & 0xff);
	h[0x07] = (u8)((id >> 8) & 0xff);
	h[0x0a] = 0x5a;
	h[0x0b] = 0x5a;
}

/* message B - the verified boot announce body (phase38/announce-body-bytes.md): HCC group 0 /
 * id 1, header total 0x30, 72-byte descriptor - the vendor's first SR post after boot. */
static unsigned int omo_msgb_en;
module_param_named(msgb, omo_msgb_en, uint, 0444);
MODULE_PARM_DESC(msgb, "1 = post the literal message-B bytes instead of the builder frame (phase 38)");

static const u8 omo_msg_b[72] = {
	0x00, 0x01, 0x00, 0x04,  0x30, 0x00, 0x01, 0x00,  0x00, 0x00, 0x5a, 0x5a,
	0x00, 0x00, 0x00, 0x00,  0x01, 0x00, 0x00, 0x00,  0xd8, 0x00, 0x14, 0x00,
	0x01, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,
	0xff, 0xff, 0xff, 0xff,  0xff, 0xff, 0xff, 0xff,  0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff,  0xff, 0xff, 0xff, 0xff,  0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff,
};

static void omo_sr_post(void)
{
	unsigned int i, j;

	if (!omo_rings_ready) {
		pr_err("omo-drv1: srpost requested but the rings are not allocated\n");
		return;
	}

	omo_program_outbound();

	memset(omo_sr_pay[0], 0, OMO_ETE_DEPTH * OMO_SR_PAYLOAD);
	if (omo_hccpost_en) {
		if (omo_msgb_en) {
			memcpy(omo_sr_pay[0], omo_msg_b, sizeof(omo_msg_b));
			pr_info("omo-drv1: message B (verified boot announce, HCC group 0 id 1, 72 B) posted\n");
		} else {
			omo_hcc_build(omo_sr_pay[0], omo_hcc_group, omo_hcc_id, omo_hcc_len);
			pr_info("omo-drv1: HCC frame built: group=%u id=%u len=%u (12-byte header + payload)\n",
				omo_hcc_group, omo_hcc_id, omo_hcc_len);
		}
	} else {
		memcpy(omo_sr_pay[0], omo_sr_msg, sizeof(omo_sr_msg));
	}
	{
		u32 *w = (u32 *)((u8 *)omo_sr_pay[0] + OMO_SR_PAYLOAD);

		memset(w, 0, OMO_SR_PAYLOAD);
		w[0] = 0x01200101;
		w[1] = 0x0003012a;
		w[2] = 0x5a5a0000;
		w[5] = 0x010e0101;
		w[6] = 0x0d010dae;
		w[8] = 0x00000001;
	}

	pr_info("omo-drv1: ---- SR post: %u nodes + producer commit + enable (fwaccept's proven sequence) ----\n",
		OMO_ETE_DEPTH);
	for (i = 0; i < OMO_ETE_SR_N; i++) {
		u32 devva = omo_hostca_to_devva(omo_sr_dma[i]);
		u64 *n = omo_sr_va[i];
		unsigned long b = omo_sr_block[i];
		u32 idx = 0, rb, en;

		for (j = 0; j < OMO_ETE_DEPTH; j++) {
			u32 ln = (i == 0 && j == 1) ? OMO_SR_ALG_LEN :
				(omo_sr_desclen ? omo_sr_desclen : OMO_SR_MSG_LEN);
			u64 w1 = (u64)(u32)((ln << 16) | OMO_SR_FLAG);

			n[j] = (w1 << 32) | (devva + j * OMO_SR_PAYLOAD);
			idx = omo_ring_ptr_plus(idx, OMO_ETE_DEPTH);
		}

		iowrite32(idx, omo_ete + b + ETE_SR_WPTR);
		rb = omo_rd(omo_ete, b + ETE_SR_WPTR);
		pr_info("omo-drv1: SR ch%u posted %u nodes word0=0x%08x; commit SR+0x%02x <= 0x%08x readback=0x%08x\n",
			i, OMO_ETE_DEPTH, devva, (unsigned)ETE_SR_WPTR, idx, rb);

		en = omo_rd(omo_ete, b + OMO_SR_EN0);
		iowrite32(en | 1U, omo_ete + b + OMO_SR_EN0);
		pr_info("omo-drv1: ENABLE SR ch%u +0x00 0x%08x -> 0x%08x readback=0x%08x\n",
			i, en, en | 1U, omo_rd(omo_ete, b + OMO_SR_EN0));
		en = omo_rd(omo_ete, b + OMO_SR_EN1);
		iowrite32(1U, omo_ete + b + OMO_SR_EN1);
		pr_info("omo-drv1: ENABLE SR ch%u +0x48 0x%08x -> 0x00000001 readback=0x%08x\n",
			i, en, omo_rd(omo_ete, b + OMO_SR_EN1));

		/* the vendor's own last act of the fill: pcie_msg_send(chip, 3) */
		if (omo_sr_announce) {
			u32 a0 = omo_rd(omo_msg, OMO_MSG0);
			u32 a2;

			iowrite32(a0 | 8U, omo_msg + OMO_MSG0);
			pr_info("omo-drv1: ANNOUNCE (as sr_dscr_fill does) out[0] <= 0x%08x readback=0x%08x\n",
				a0 | 8U, omo_rd(omo_msg, OMO_MSG0));
			a2 = omo_rd(omo_msg, OMO_MSG_DOORBELL);
			iowrite32(a2 | 1U, omo_msg + OMO_MSG_DOORBELL);
			pr_info("omo-drv1: ANNOUNCE out[2] <= 0x%08x readback=0x%08x (doorbell)\n",
				a2 | 1U, omo_rd(omo_msg, OMO_MSG_DOORBELL));
		}
	}
}

/*
 * Device -> host: post DR receive buffers and commit the producer index.
 *
 * Phase 20's own conclusion named this the most likely next thing to try: the posted DR nodes
 * (word0 = payload device VA, word1 = 0) "were never touched", and whether the engine needs the
 * host producer index advanced - or an explicit "buffers available" write - before it will DMA a
 * receive was left undetermined.  lab/fwaccept DID commit that index (DR+0x38, the same packed form)
 * and recorded dr_events=4; wifidrv1 wrote DR wptr = 0.  This closes that gap.
 */
static void omo_dr_post(void)
{
	unsigned int i, j;

	if (!omo_rings_ready) {
		pr_err("omo-drv1: drpost requested but the rings are not allocated\n");
		return;
	}

	pr_info("omo-drv1: ---- DR post: %u nodes + producer commit (the phase-20 open item) ----\n",
		OMO_ETE_DEPTH);
	for (i = 0; i < OMO_ETE_DR_N; i++) {
		u32 devva = omo_hostca_to_devva(omo_dr_pay_dma[i]);
		u64 *nodes = omo_dr_va[i];
		unsigned long b = omo_dr_block[i];
		u32 idx = 0, rb, rp;

		memset(omo_dr_pay[i], 0, OMO_ETE_DEPTH * OMO_DR_PAYLOAD);
		for (j = 0; j < OMO_ETE_DEPTH; j++) {
			nodes[j] = devva + j * OMO_DR_PAYLOAD;
			idx = omo_ring_ptr_plus(idx, OMO_ETE_DEPTH);
		}

		iowrite32(idx, omo_ete + b + ETE_DR_WPTR);
		rb = omo_rd(omo_ete, b + ETE_DR_WPTR);
		rp = omo_rd(omo_ete, b + ETE_DR_RPTR);
		pr_info("omo-drv1: DR ch%u posted %u nodes word0=0x%08x; commit DR+0x%02x <= 0x%08x readback=0x%08x rptr=0x%08x\n",
			i + 3, OMO_ETE_DEPTH, devva, (unsigned)ETE_DR_WPTR, idx, rb, rp);
	}
}

/*
 * Watch the DR rings for a device deposit: the device index (+0x3c) advancing, or a node word
 * changing under us.  Read-only on the device.  A deposit is the first evidence of data flow.
 */
/* PHASE 40 - parse the device's D2H payload as an HCC frame (the verified 12-byte header):
 * group = h[0] & 0xf, id = u16 @ +6, total len = u16 @ +4, SR tag = u16 @ +0xa. */
static unsigned int omo_drparse_en;
module_param_named(drparse, omo_drparse_en, uint, 0444);
MODULE_PARM_DESC(drparse, "1 = parse the DR payload as an HCC frame when the device writes it (phase 40)");

static void omo_dr_watch(void)
{
	unsigned int i;
	u32 rptr_last[OMO_ETE_DR_N];
	u32 snap[OMO_ETE_DR_N][8];
	u32 srr_last[OMO_ETE_SR_N];
	unsigned long elapsed = 0;
	unsigned int events = 0, sr_events = 0;

	for (i = 0; i < OMO_ETE_DR_N; i++) {
		rptr_last[i] = omo_rd(omo_ete, omo_dr_block[i] + ETE_DR_RPTR);
		memcpy(snap[i], omo_dr_va[i], sizeof(snap[i]));
	}
	/*
	 * The SR device index is the flow-control signal that matters most here: the vendor refuses to
	 * transmit unless the device has granted TX buffers, and the buffers it returns are exactly the
	 * SR descriptors it has consumed.  If this index never advances after the post, the device never
	 * took our descriptors - which is the observable form of a zero credit, and the measurement the
	 * credit chain (docs/phase24/tx-credit-source.md) could not produce statically.
	 */
	for (i = 0; i < OMO_ETE_SR_N; i++)
		srr_last[i] = omo_rd(omo_ete, omo_sr_block[i] + ETE_SR_RPTR);

	pr_info("omo-drv1: ---- ring watch (%u ms): DR device index + node words, SR device index ----\n",
		omo_polldur);
	for (i = 0; i < OMO_ETE_SR_N; i++)
		pr_info("omo-drv1:   SR ch%u baseline: wptr(+0x18)=0x%08x rptr(+0x1c)=0x%08x\n",
			i, omo_rd(omo_ete, omo_sr_block[i] + ETE_SR_WPTR), srr_last[i]);

	while (elapsed < omo_polldur) {
		for (i = 0; i < OMO_ETE_SR_N; i++) {
			u32 rp = omo_rd(omo_ete, omo_sr_block[i] + ETE_SR_RPTR);

			if (rp != srr_last[i]) {
				pr_info("omo-drv1: SR ch%u DEVICE INDEX 0x%08x -> 0x%08x (host wptr=0x%08x, delta=%u) = THE DEVICE CONSUMED OUR DESCRIPTORS\n",
					i, srr_last[i], rp,
					omo_rd(omo_ete, omo_sr_block[i] + ETE_SR_WPTR),
					(rp - srr_last[i]) & 0x3ffU);
				srr_last[i] = rp;
				sr_events++;
			}
		}
		for (i = 0; i < OMO_ETE_DR_N; i++) {
			unsigned long b = omo_dr_block[i];
			u32 rp = omo_rd(omo_ete, b + ETE_DR_RPTR);
			u32 wp = omo_rd(omo_ete, b + ETE_DR_WPTR);

			if (rp != rptr_last[i]) {
				pr_info("omo-drv1: DR ch%u DEVICE INDEX 0x%08x -> 0x%08x (host wptr=0x%08x, delta=%u) = A DEPOSIT\n",
					i + 3, rptr_last[i], rp, wp,
					(rp - rptr_last[i]) & 0x3ffU);
				rptr_last[i] = rp;
				events++;
			}
			if (memcmp(omo_dr_va[i], snap[i], sizeof(snap[i])) != 0) {
				u32 *n = omo_dr_va[i];

				pr_info("omo-drv1: DR ch%u node[0] CHANGED word0=0x%08x word1=0x%08x (the device wrote our buffer)\n",
					i + 3, n[0], n[1]);
				if (omo_drparse_en && omo_dr_pay[i]) {
					u8 *h = omo_dr_pay[i];

					pr_info("omo-drv1: [drparse] ch%u HCC group=%u id=%u len=%u tag=%02x%02x hdr=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
						i + 3, h[0] & 0xf, h[6] | (h[7] << 8), h[4] | (h[5] << 8),
						h[0xa], h[0xb], h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7], h[8], h[9], h[10], h[11]);
				}
				memcpy(snap[i], omo_dr_va[i], sizeof(snap[i]));
				events++;
			}
		}
		msleep(omo_pollms);
		elapsed += omo_pollms;
	}

	pr_info("omo-drv1: ring watch done: %u DR deposit events, %u SR consumption events in %lu ms\n",
		events, sr_events, elapsed);
	if (!events)
		pr_info("omo-drv1: NOTE no DR deposit - the device did not write our receive buffers\n");
	if (!sr_events)
		pr_info("omo-drv1: NOTE the SR device index did not MOVE during this window. That is NOT evidence the\n");
	pr_info("omo-drv1:      device ignored our descriptors - check rptr against wptr above: ch0 read\n");
	pr_info("omo-drv1:      rptr == wptr, i.e. already caught up at baseline (docs/phase24/sr-consumption-measured.md)\n");
}

static unsigned int omo_drpost_en;
module_param_named(drpost, omo_drpost_en, uint, 0444);
MODULE_PARM_DESC(drpost, "1 = post DR receive buffers + commit the producer index AND watch for a device deposit (the phase-20 open item); requires wr=1");

static int omo_do_release(void)
{
	u32 rb;

	pr_info("omo-drv1: RELEASE write CA 0x40000108 <- 0x%08x (BAR0+0x%lx)\n",
		OMO_RELEASE_VAL, (unsigned long)OMO_RELEASE_OFF);
	iowrite32(OMO_RELEASE_VAL, omo_rel + (OMO_RELEASE_OFF - OMO_IO_WIN));
	rb = ioread32(omo_rel + (OMO_RELEASE_OFF - OMO_IO_WIN));
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

	omo_rel = ioremap(omo_bar0_base + OMO_IO_WIN, OMO_IO_BYTES);
	if (!omo_rel) {
		pr_err("omo-drv1: ioremap region-3 IO (BAR0+0x%lx) FAILED\n",
		       (unsigned long)OMO_IO_WIN);
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
		if (omo_srpost_en)
			omo_sr_post();
		if (omo_drpost_en)
			omo_dr_post();
	}

	if (omo_fw_en) {
		rc = omo_load_fw();
		if (rc)
			goto err_iatu;
		omo_fwmap = ioremap(omo_bar0_base + OMO_FW_WIN, OMO_FW_BYTES);
		if (!omo_fwmap) {
			pr_err("omo-drv1: ioremap firmware window FAILED\n");
			rc = -ENOMEM;
			goto err_iatu;
		}
		if (omo_fwctx_en) {
			omo_acp = ioremap(omo_bar0_base + OMO_ACP_BAR0, OMO_ACP_BYTES);
			if (!omo_acp) {
				pr_err("omo-drv1: ioremap ACP window FAILED\n");
				rc = -ENOMEM;
				goto err_iatu;
			}
		}
		if (omo_irqwin_en) {
			omo_irqwin = ioremap(omo_bar0_base + OMO_IRQWIN_OFF, OMO_IRQWIN_SIZE);
			if (!omo_irqwin) {
				pr_err("omo-drv1: ioremap IRQ window FAILED\n");
				rc = -ENOMEM;
				goto err_iatu;
			}
		}
		omo_write_fw();
	}

/* Release the Wi-Fi CPU - the act phase 19 found, gated on its own param - then OBSERVE
	 * what the released firmware emits instead of assuming it said nothing. */
	if (omo_release_en) {
		u32 before[ARRAY_SIZE(omo_sig)], after[ARRAY_SIZE(omo_sig)];
		unsigned int i, changed = 0;

		/* Phase 25y: make the announce-to-release gap an explicit, swept variable instead of
		 * whatever the firmware load happens to cost.  Same announce, same registers - only the
		 * moment moves.  0 leaves every earlier result reproducible. */
		if (omo_reannounce_ms) {
			pr_info("omo-drv1: waiting %u ms after the firmware write before re-announcing\n",
				omo_reannounce_ms);
			msleep(omo_reannounce_ms);
			omo_reannounce();
		}

		omo_sig_read("pre ", before);
		omo_do_release();

		if (omo_ackfast_en)
			omo_ackfast();

		/* POLL FIRST, IMMEDIATELY. An earlier revision slept 500 ms and then did the signature
		 * read (~40 ms) before polling, so the mailbox was unobserved for the first ~540 ms
		 * after the release - which is exactly where phase 20 recorded the id-6 word
		 * (out[1] 0 -> 0x40 = pcie_trigger_ete_sending_handle at ~+910 ms).  That ordering
		 * could have hidden the first word of the dialogue, and nothing about the poll needs
		 * the signature to run first.  The signature is CPU state, not a transient, so
		 * reading it after the poll window is just as valid. */
		omo_poll_mailbox();

		if (omo_drpost_en)
			omo_dr_watch();

		omo_sig_read("post", after);
		for (i = 0; i < ARRAY_SIZE(omo_sig); i++) {
			if (before[i] != after[i]) {
				pr_info("omo-drv1: [sig]   %-14s 0x%08x -> 0x%08x CHANGED\n",
					omo_sig[i].name, before[i], after[i]);
				changed++;
			}
		}
		pr_info("omo-drv1: [sig] %u/%zu signature registers changed -> %s\n",
			changed, ARRAY_SIZE(omo_sig),
			changed ? "THE CHIP LEFT ROM STATE" : "the chip did NOT start");
	}

	if (omo_fwctx_en)
		omo_fwctx_read();

	if (omo_ep1db_en) {
		rc = omo_ep1_init();
		if (rc)
			pr_err("omo-drv1: [ep1db] init failed rc=%d - EP1 doorbell skipped\n", rc);
	}

	if (omo_intrsamp_en)
		omo_intrsamp();

	/* The recovered host half, against the corrected registers: ack, clear, re-arm, dispatch. */
	if (omo_msgsvc)
		omo_msg_service();

	/* Host -> device send, then observe whether the device reacts. */
	if (omo_send >= 0 && omo_send < 32)
		omo_h2d_send();

	/* Stand in for the missing ISR: poll the glue status and dispatch, the route
	 * phases 20/22 both name as never entered by a takeover. */
	if (omo_svc)
		omo_glue_service();

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
	if (omo_ep1_msg) {
		iounmap(omo_ep1_msg);
		omo_ep1_msg = NULL;
	}
	if (omo_ep1_iatu) {
		pci_iounmap(omo_ep1_dev, omo_ep1_iatu);
		omo_ep1_iatu = NULL;
	}
	if (omo_ep1_dev) {
		pci_release_mem_regions(omo_ep1_dev);
		omo_ep1_dev = NULL;
	}
	if (omo_acp) {
		iounmap(omo_acp);
		omo_acp = NULL;
	}
	if (omo_irqwin) {
		iounmap(omo_irqwin);
		omo_irqwin = NULL;
	}
	if (omo_fwmap) {
		iounmap(omo_fwmap);
		omo_fwmap = NULL;
	}
	if (omo_fw) {
		vfree(omo_fw);
		omo_fw = NULL;
		omo_fw_len = 0;
	}
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
