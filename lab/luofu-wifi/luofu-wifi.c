// SPDX-License-Identifier: GPL-2.0
/*
 * luofu-wifi: the Wi-Fi service SKELETON - the first OUR-wifidrv code.
 *
 * The arc (phase20 -> phase49) localized the device's Wi-Fi service end to end
 * but never wrote it down as a driver: the vendor's host stack (hi5622v100_plat
 * / _wifi) owns the endpoint, and our experiments drove its registers with
 * one-shot instruments staged as wifidrv1.ko.  This module is the first code
 * that states the SERVICE as a structure: the windows, the announce state
 * machine and the rings are mapped and decoded in one place, so the next rung
 * has a frame to grow into instead of another probe.
 *
 * SCOPE - READ-ONLY.  This file performs no MMIO store of any kind: it holds
 * no MMIO store expression at all (the verification greps this file for the
 * store-token family and expects zero hits), the register tables carry offsets
 * only, and every accessor is built on readl().  The single non-MMIO side
 * effect is the kernel's own pci_enable_device(),
 * which sets the endpoint's command register so a memory BAR can be claimed -
 * the same call lab/eteprobe and lab/wifidrv1 make.  Nothing is rung, no
 * descriptor is posted, no firmware window is touched.
 *
 * THE HARD RULES, as the record states them (all held here):
 *   - never store to CA 0x400392f0 (nor the twin counterpart 0x40039af0);
 *     the skeleton does not even map that address;
 *   - never read the RC misc window 0x10161000;
 *   - never read the host-side ack IAR CA 0x4016010c (BAR0 0x51910c) nor the
 *     AIAR 0x40160120; the take is judged from the GLUE side only, which is
 *     what makes this skeleton safe to run while the vendor stack is loaded;
 *   - dword-aligned accesses everywhere: every field is fetched from the
 *     4-byte-aligned dword that contains it (lw_read()), never from a
 *     2-mod-4 address (the readw panic of rcfix.md / pciskel-smoke.md);
 *   - measurement only through the endpoint's own BAR0.
 *
 * THE WINDOWS (docs/phase18/inbound-map.md row 3; wifidrv1.c's constants).
 * Region 3 translates host 0x403b8000 -> dev CA 0x40000000, size 0x120000, so
 * a device CA 0x400XXXXX sits at BAR0 offset 0x3b8000 + (CA - 0x40000000).
 * Everything this skeleton reads lives in four pages of that one window:
 *
 *   LW_WIN_HS   0x3b8000  the bring-up handshake page      CA 0x40000108/0x4000010c
 *   LW_WIN_MSG  0x3f1000  the glue/mailbox + the twin copy CA 0x40039010 .. 0x40039aec
 *   LW_WIN_RING 0x3f2000  the ETE SR/DR ring program regs  CA 0x4003a400 .. 0x4003a6bc
 *   LW_WIN_D2H  0x4b9000  the device-local D2H + msg map   CA 0x40101414/0x40101434/0x40101438
 *
 * WHAT EACH READ MEANS (the arc's own findings; no value is invented here):
 *   - the ANNOUNCE handshake (h2d2.md sec 3): the device's ETE bring-up parks
 *     on *(CA 0x4000010c) == 0x0000cece; the low halves are zeroed at bring-up,
 *     the host completes it, and only then can the release run and the CPU mask
 *     lift.  out[1] (CA 0x40039014) carries the announce's pending-id bitmask
 *     (the bring-up posts 4 = id 2), and CA 0x40101434 is the device-local D2H
 *     assert its send path rings.
 *   - the GLUE/MAILBOX (twin.md, h2d2.md sec 1): out[0] CA 0x40039010 is the
 *     H2D pending bitmap the host publishes; the dispatcher's own gate is
 *     "out[0] != 0" and it CONSUMES by zeroing out[0].  CA 0x400392d4 is the
 *     HOST2DEVICE_INTR_SET doorbell (self-clearing: a non-zero read is a ring
 *     in flight), CA 0x400392e4 the RAW status, 0x400392e8 the MASK
 *     (0 = unmask) and 0x400392ec the post-mask STATUS.  Bit 3 of raw/status is
 *     device2host_rx_intr - the D2H-RX source the device raises when IT moves.
 *     The twin copy B repeats the same file at +0x800 (CA 0x40039800); its
 *     masked level is the discriminator (a copy-A ring with a still twin is
 *     device-originated, a twin ring is the host's own).
 *   - the RINGS (credit2.md; ete-registers-reconciled.md G): the ETE block's
 *     channel program registers.  SR ch0..2 at +{0x400,0x450,0x4a0}, DR ch0..3
 *     at +{0x590,0x5e0,0x630,0x680} - a 0x50 stride, the register blocks the
 *     vendor's own .rodata table names (ko file 0x20e5c).  Per channel:
 *     DR+0x30 node-array device VA, DR+0x34[9:0] depth-1, DR+0x38 the committed
 *     producer index (the host's GRANT: one unit = one buffer handed over),
 *     DR+0x3c the device's 16-bit consumer index; SR+0x08 ctrl, +0x10 base,
 *     +0x14 depth, +0x18 wptr, +0x1c rptr.  Credit = (producer - consumer) mod
 *     (2*depth), phase-aware on bit 10.
 *     NOTE the stride: wifidrv1.c indexes these channels with 0x114/0x6c, and
 *     credit2.md sec 5 retracts exactly that ("those are host struct strides");
 *     the register blocks are 0x50 apart, re-derived here from the ko's table
 *     and from pcie_ete_{sr,dr}_reg_init @0x14a48/0x1483c.  This skeleton uses
 *     the register stride.
 *   - the DR COMPLETION TEST (credit2.md sec 2.1): pcie_ete_rcv_buff_check
 *     @0x14d74 accepts a slot iff the *buffer's* 16-bit field at +0x0a reads
 *     0x5a5a and the 16-bit field at +0x04 is non-zero.  That buffer is host
 *     DRAM the vendor owns, so the skeleton carries the predicate
 *     (lw_dr_hdr_ok()) and only ever samples a node array that falls inside the
 *     mapped ETE block (a range guard, never an arbitrary device read).
 *   - the TAKE witness (realchain.md sec 5, h2d2.md sec 5), host-side only:
 *     out[0] == 0 after a send = the dispatcher CONSUMED the H2D bit (the
 *     device ran it), and a rising copy-A raw/status bit 3 = the device rang
 *     D2H.  Because this module stores nothing, any rise it observes across its
 *     sample window is device-originated by construction - the arc's own
 *     decision rule with the "host bit-3 store" leg removed.  The device-side
 *     per-entry id sensor (V2_ID) belongs to the firmware patch, not here.
 *
 * SAFETY: the hardware section is gated on param `hw` (default 0 = off), so a
 * default load performs no PCI access at all and proves only vermagic/ABI.  With
 * hw=1 the claim is read-mostly and REFUSES (rc != 0) rather than fight when the
 * vendor stack owns the BARs - coexistence, never replacement.
 */

#include <linux/build_bug.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/types.h>

#define LW_NAME		"luofu-wifi"

/* ---- PCI identity (the Wi-Fi endpoint on both RC domains) --------------- */
#define LW_PCI_VENDOR	0x59e7
#define LW_PCI_DEVICE	0x0005

/* ---- region 3 (SHUANGTA_REGION_IO) as one BAR0 window ------------------- */
#define LW_REGION3_BASE	0x3b8000UL	/* BAR0 offset of host 0x403b8000 */
#define LW_REGION3_SIZE	0x120000UL
/* device CA -> the BAR0 offset that reaches it (inbound-map.md row 3) */
#define LW_CA(ca)	(LW_REGION3_BASE + ((u32)(ca) - 0x40000000UL))

/* The four pages this skeleton maps.  Page-aligned bases; the register offsets
 * below are relative to them. */
#define LW_WIN_HS	0x3b8000UL	/* handshake/release page  (CA 0x40000000+) */
#define LW_WIN_MSG	0x3f1000UL	/* glue/mailbox + twin     (CA 0x40039000+) */
#define LW_WIN_RING	0x3f2000UL	/* ETE SR/DR program       (CA 0x4003a000+) */
#define LW_WIN_D2H	0x4b9000UL	/* device-local D2H/msgmap (CA 0x40101400+) */
#define LW_WIN_BYTES	0x1000UL

/* offsets within LW_WIN_HS (base CA 0x40000000) */
#define LW_HS_RELEASE	0x108U		/* CA 0x40000108: the CPU release word */
#define LW_HS_GATE	0x10cU		/* CA 0x4000010c: the park gate / handshake */
#define LW_HS_GATE_VAL	0x0000ceceU	/* the value the device's bring-up waits for */

/* offsets within LW_WIN_MSG (base CA 0x40039000 = copy A) */
#define LW_MSG_OUT0	0x010U		/* CA 0x40039010: the H2D pending bitmap */
#define LW_MSG_OUT1	0x014U		/* CA 0x40039014: the announce/pending-id mask */
#define LW_MSG_DOORBELL	0x2d4U		/* CA 0x400392d4: HOST2DEVICE_INTR_SET */
#define LW_MSG_RAW	0x2e4U		/* CA 0x400392e4: HOST_INTR_RAW_STATUS */
#define LW_MSG_MASK	0x2e8U		/* CA 0x400392e8: HOST_INTR_MASK (0 = unmask) */
#define LW_MSG_STATUS	0x2ecU		/* CA 0x400392ec: HOST_INTR_STATUS */
/* the twin copy B: the same register file at CA 0x40039800, stride 0x800 */
#define LW_TWIN_DOORBELL 0xad4U		/* CA 0x40039ad4 */
#define LW_TWIN_RAW	0xae4U		/* CA 0x40039ae4 */
#define LW_TWIN_MASK	0xae8U		/* CA 0x40039ae8 */
#define LW_TWIN_STATUS	0xaecU		/* CA 0x40039aec */
/* bit numbering inside a raw/status word (twin.md sec 4) */
#define LW_INTR_BIT_D2H_RX	3U	/* device2host_rx_intr - the take's own witness */
#define LW_INTR_BIT_D2H_MSG	4U	/* device2host_intr ("message/inbound pending") */
#define LW_INTR_DISPATCH_MASK	0x3d8U	/* pcie_intr_handle's dispatch mask */

/* offsets within LW_WIN_RING (base CA 0x4003a000) */
#define LW_SR0_BASE	0x400U
#define LW_SR_STRIDE	0x50U		/* the register blocks, not the host structs */
#define LW_SR_N		3U
#define LW_SR_CTRL	0x008U
#define LW_SR_BASE	0x010U
#define LW_SR_DEPTH	0x014U
#define LW_SR_WPTR	0x018U
#define LW_SR_RPTR	0x01cU
#define LW_DR0_BASE	0x590U
#define LW_DR_STRIDE	0x50U
#define LW_DR_N		4U
#define LW_DR_BASE	0x030U		/* the node-array device VA */
#define LW_DR_DEPTH	0x034U		/* [9:0] = depth-1 */
#define LW_DR_PROD	0x038U		/* the committed producer index (the GRANT) */
#define LW_DR_CONS	0x03cU		/* the device's 16-bit consumer index */
#define LW_RING_DEPTH_SHIFT 0U
#define LW_RING_DEPTH_MASK  0x3ffU
#define LW_IDX_MASK	0x3ffU		/* the index pair is 10-bit + phase bit 10 */
#define LW_DR_HDR_TAG	0x5a5aU		/* the buffer header's acceptance tag */

/* offsets within LW_WIN_D2H (base CA 0x40101400) */
#define LW_D2H_REARM	0x414U		/* CA 0x40101414: the host's re-arm word */
#define LW_D2H_ASSERT	0x434U		/* CA 0x40101434: the device-local D2H assert */
#define LW_D2H_ACK	0x438U		/* CA 0x40101438: the D2H ack word */

/* ---- the forbidden addresses, asserted out of every window ------------- */
#define LW_FORBIDDEN_IAR	LW_CA(0x4016010cUL)	/* the ack IAR CA (0x51910c) */
#define LW_FORBIDDEN_AIAR	LW_CA(0x40160120UL)	/* the NS-view IAR alias */
#define LW_FORBIDDEN_MISC	0x10161000UL		/* the RC misc window */
#define LW_FORBIDDEN_CLR	LW_CA(0x400392f0UL)	/* the W1C HOST_INTR_CLR */

/* Compile-time boundary receipt: the offset arithmetic above decodes as the
 * record says, and neither forbidden IAR word falls in a mapped window.  (A
 * mapped window is not a read - ioremap performs no bus access - but a mapped
 * forbidden page is a trap waiting for the next edit, so it is asserted out.) */
static_assert(LW_CA(0x4000010cUL) == LW_WIN_HS + LW_HS_GATE, "gate offset");
static_assert(LW_CA(0x40039010UL) == LW_WIN_MSG + LW_MSG_OUT0, "out[0] offset");
static_assert(LW_CA(0x400392e4UL) == LW_WIN_MSG + LW_MSG_RAW, "glue raw offset");
static_assert(LW_CA(0x40039ae4UL) == LW_WIN_MSG + LW_TWIN_RAW, "twin raw offset");
static_assert(LW_CA(0x4003a590UL) == LW_WIN_RING + LW_DR0_BASE, "DR ch0 offset");
static_assert(LW_CA(0x40101434UL) == LW_WIN_D2H + LW_D2H_ASSERT, "D2H assert offset");
static_assert(LW_FORBIDDEN_IAR != LW_WIN_HS + LW_HS_GATE, "IAR is not the gate CA");
static_assert(LW_FORBIDDEN_CLR != LW_WIN_MSG + LW_MSG_STATUS, "CLR is not the status CA");
/* the ack IAR (and the AIAR) must fall outside every mapped window - this is the
 * guard that keeps a future window growth from quietly mapping the forbidden
 * page.  If this ever fails, do not widen the assert: move the window. */
static_assert((LW_FORBIDDEN_IAR < LW_WIN_HS || LW_FORBIDDEN_IAR >= LW_WIN_HS + LW_WIN_BYTES) &&
	      (LW_FORBIDDEN_IAR < LW_WIN_MSG || LW_FORBIDDEN_IAR >= LW_WIN_MSG + LW_WIN_BYTES) &&
	      (LW_FORBIDDEN_IAR < LW_WIN_RING || LW_FORBIDDEN_IAR >= LW_WIN_RING + LW_WIN_BYTES) &&
	      (LW_FORBIDDEN_IAR < LW_WIN_D2H || LW_FORBIDDEN_IAR >= LW_WIN_D2H + LW_WIN_BYTES),
	      "the ack IAR must not lie inside a mapped window");
static_assert((LW_FORBIDDEN_AIAR < LW_WIN_HS || LW_FORBIDDEN_AIAR >= LW_WIN_HS + LW_WIN_BYTES) &&
	      (LW_FORBIDDEN_AIAR < LW_WIN_MSG || LW_FORBIDDEN_AIAR >= LW_WIN_MSG + LW_WIN_BYTES) &&
	      (LW_FORBIDDEN_AIAR < LW_WIN_RING || LW_FORBIDDEN_AIAR >= LW_WIN_RING + LW_WIN_BYTES) &&
	      (LW_FORBIDDEN_AIAR < LW_WIN_D2H || LW_FORBIDDEN_AIAR >= LW_WIN_D2H + LW_WIN_BYTES),
	      "the AIAR must not lie inside a mapped window");
static_assert(LW_FORBIDDEN_MISC != LW_WIN_HS && LW_FORBIDDEN_MISC != LW_WIN_MSG &&
	      LW_FORBIDDEN_MISC != LW_WIN_RING && LW_FORBIDDEN_MISC != LW_WIN_D2H,
	      "the RC misc window is never mapped");

/* ---- parameters -------------------------------------------------------- */
static unsigned int lw_hw;		/* 0 = registration/ABI only (safe default) */
module_param_named(hw, lw_hw, uint, 0444);
MODULE_PARM_DESC(hw, "1 = claim the endpoint and run the read-only decode; 0 = no PCI access");

static unsigned int lw_domain;
module_param_named(domain, lw_domain, uint, 0444);
MODULE_PARM_DESC(domain, "PCI domain of the 59e7:0005 endpoint to bind (0 = 0000:00:00.0)");

static unsigned int lw_samples = 1;
module_param_named(samples, lw_samples, uint, 0444);
MODULE_PARM_DESC(samples, "bounded take-status samples (1..64) taken across the sample window");

static unsigned int lw_delay_ms = 20;
module_param_named(delay_ms, lw_delay_ms, uint, 0444);
MODULE_PARM_DESC(delay_ms, "gap between take-status samples in ms (0..1000)");

/* ---- state ------------------------------------------------------------- */
struct lw_dev {
	struct pci_dev *pdev;
	void __iomem *hs;
	void __iomem *msg;
	void __iomem *ring;
	void __iomem *d2h;
	unsigned long bar0;
};
static struct lw_dev lw;

/* Status table: each field's containing register, its byte offset and the
 * expected value (0 = measurement only).  Offsets only - a store cannot be
 * expressed through this table. */
struct lw_field {
	unsigned long ca;	/* the device CA, for the log line */
	u32 expect;
	u32 mask;
	const char *name;
};

/* ---- the aligned-dword accessor --------------------------------------- */
/*
 * The window answers 4-byte-aligned 32-bit accesses only (rcfix.md: a read at a
 * 2-mod-4 offset external-aborts whatever its width).  Every field is therefore
 * fetched from the 4-byte-aligned dword that CONTAINS it: one readl(), then a
 * shift/mask to the field's byte lanes.  For a dword field at an aligned offset
 * this is exactly the plain readl(); for a sub-word field it returns what the
 * aligned readw() would, and it never issues a non-aligned access.
 */
static u32 lw_read(void __iomem *win, unsigned long off, u8 width)
{
	u32 word = readl(win + (off & ~3UL));
	unsigned int shift = (unsigned int)((off & 3UL) * 8U);

	word >>= shift;
	if (width >= 4U)
		return word;
	return word & ((1U << (width * 8U)) - 1U);
}

/* A named dword register read, with the record's prediction checked when one is
 * pinned (mask != 0). */
static u32 lw_rd_field(void __iomem *win, unsigned long off, const struct lw_field *f)
{
	u32 v = lw_read(win, off, 4U);

	if (f->mask) {
		u32 got = v & f->mask;

		pr_info(LW_NAME ":   CA 0x%08lx %-42s = 0x%08x %s\n", f->ca, f->name, v,
			got == f->expect ? "(as predicted)" : "(DIFFERS from the record)");
	} else {
		pr_info(LW_NAME ":   CA 0x%08lx %-42s = 0x%08x\n", f->ca, f->name, v);
	}
	return v;
}

static u32 lw_rd(void __iomem *win, unsigned long off)
{
	return lw_read(win, off, 4U);
}

/* ------------------------------------------------------------------------
 * 1. The announce / handshake state machine
 * --------------------------------------------------------------------- */
enum lw_announce_state {
	LW_ANN_IDLE,		/* both halves read 0: the bring-up has not run/cleared */
	LW_ANN_PARKED,		/* the gate holds something that is not 0xcece */
	LW_ANN_RELEASED,	/* the gate reads 0x0000cece: the park exited */
};

static const char *lw_ann_name(enum lw_announce_state s)
{
	switch (s) {
	case LW_ANN_RELEASED:
		return "RELEASED (gate == 0x0000cece)";
	case LW_ANN_PARKED:
		return "PARKED (gate != 0xcece; the CPU is inside the bring-up)";
	case LW_ANN_IDLE:
	default:
		return "IDLE (gate == 0; the announce has not started)";
	}
}

static void lw_decode_announce(void)
{
	static const struct lw_field gate = {
		0x4000010cUL, LW_HS_GATE_VAL, 0xffffffffU, "gate/handshake *(CA)==0xcece?" };
	static const struct lw_field release = {
		0x40000108UL, 0x00005a5aU, 0x0000ffffU, "release word (host writes 0x5a5a)" };
	static const struct lw_field out1 = {
		0x40039014UL, 0x00000000U, 0, "out[1] announce/pending-id bitmask" };
	static const struct lw_field assert = {
		0x40101434UL, 0x00000000U, 0, "device-local D2H assert (bit 0)" };
	static const struct lw_field rearm = {
		0x40101414UL, 0x00000000U, 0, "msg-map re-arm word" };
	static const struct lw_field ack = {
		0x40101438UL, 0x00000000U, 0, "msg-map ack word" };
	u32 g, o1;
	enum lw_announce_state st;
	unsigned int i;

	pr_info(LW_NAME ": ---- announce / handshake state machine ----\n");
	g = lw_rd_field(lw.hs, LW_HS_GATE, &gate);
	lw_rd_field(lw.hs, LW_HS_RELEASE, &release);
	o1 = lw_rd_field(lw.msg, LW_MSG_OUT1, &out1);
	lw_rd_field(lw.d2h, LW_D2H_ASSERT, &assert);
	lw_rd_field(lw.d2h, LW_D2H_REARM, &rearm);
	lw_rd_field(lw.d2h, LW_D2H_ACK, &ack);

	if (g == LW_HS_GATE_VAL)
		st = LW_ANN_RELEASED;
	else if (g == 0U)
		st = LW_ANN_IDLE;
	else
		st = LW_ANN_PARKED;

	pr_info(LW_NAME ":   ANNOUNCE STATE: %s\n", lw_ann_name(st));

	/* out[1] is a pending-id bitmask: bit i set = id i is posted (the vendor's
	 * bring-up posts out[1] = 4, i.e. id 2; the msg map only carries id <= 9). */
	if (o1) {
		for (i = 0; i < 10U; i++)
			if (o1 & (1U << i))
				pr_info(LW_NAME ":   announce id %u pending (out[1] bit %u)\n", i, i);
	} else {
		pr_info(LW_NAME ":   out[1] == 0 (no announce/message id posted)\n");
	}
}

/* ------------------------------------------------------------------------
 * 2. The glue / mailbox block (copy A and the twin copy B)
 * --------------------------------------------------------------------- */
struct lw_mailbox {
	u32 out0;
	u32 doorbell;
	u32 raw;
	u32 mask;
	u32 status;
	u32 twin_doorbell;
	u32 twin_raw;
	u32 twin_mask;
	u32 twin_status;
};

static void lw_decode_mailbox(struct lw_mailbox *mb)
{
	static const struct lw_field out0 = {
		0x40039010UL, 0, 0, "out[0] H2D pending bitmap (host publishes)" };
	static const struct lw_field door = {
		0x400392d4UL, 0, 0, "doorbell HOST2DEVICE_INTR_SET (self-clearing)" };
	static const struct lw_field raw = {
		0x400392e4UL, 0, 0, "copy A HOST_INTR_RAW_STATUS" };
	static const struct lw_field mask = {
		0x400392e8UL, 0, 0, "copy A HOST_INTR_MASK (0 = unmask)" };
	static const struct lw_field stat = {
		0x400392ecUL, 0, 0, "copy A HOST_INTR_STATUS (post-mask)" };
	static const struct lw_field tdoor = {
		0x40039ad4UL, 0, 0, "twin doorbell (copy B)" };
	static const struct lw_field traw = {
		0x40039ae4UL, 0, 0, "twin RAW status (copy B)" };
	static const struct lw_field tmask = {
		0x40039ae8UL, 0, 0, "twin MASK (copy B)" };
	static const struct lw_field tstat = {
		0x40039aecUL, 0, 0, "twin STATUS (copy B)" };

	pr_info(LW_NAME ": ---- glue / mailbox (copy A = CA 0x40039000, twin = 0x40039800) ----\n");
	mb->out0 = lw_rd_field(lw.msg, LW_MSG_OUT0, &out0);
	mb->doorbell = lw_rd_field(lw.msg, LW_MSG_DOORBELL, &door);
	mb->raw = lw_rd_field(lw.msg, LW_MSG_RAW, &raw);
	mb->mask = lw_rd_field(lw.msg, LW_MSG_MASK, &mask);
	mb->status = lw_rd_field(lw.msg, LW_MSG_STATUS, &stat);
	mb->twin_doorbell = lw_rd_field(lw.msg, LW_TWIN_DOORBELL, &tdoor);
	mb->twin_raw = lw_rd_field(lw.msg, LW_TWIN_RAW, &traw);
	mb->twin_mask = lw_rd_field(lw.msg, LW_TWIN_MASK, &tmask);
	mb->twin_status = lw_rd_field(lw.msg, LW_TWIN_STATUS, &tstat);

	pr_info(LW_NAME ":   copy A dispatch view: raw 0x%08x masked 0x%08x (& 0x%x -> 0x%08x), d2h_rx=%u d2h_msg=%u\n",
		mb->raw, mb->status, LW_INTR_DISPATCH_MASK,
		mb->status & LW_INTR_DISPATCH_MASK,
		!!(mb->status & (1U << LW_INTR_BIT_D2H_RX)),
		!!(mb->status & (1U << LW_INTR_BIT_D2H_MSG)));

	if (mb->out0)
		pr_info(LW_NAME ":   out[0] = 0x%08x -> the H2D queue is UNCONSUMED (lowest set id %u)\n",
			mb->out0, (unsigned int)__builtin_ctz(mb->out0));
	else
		pr_info(LW_NAME ":   out[0] = 0 -> nothing pending (the dispatcher consumed the last H2D bit)\n");

	if (mb->doorbell)
		pr_info(LW_NAME ":   doorbell reads 0x%08x - a ring is in flight (self-clearing word)\n",
			mb->doorbell);
}

/* ------------------------------------------------------------------------
 * 3. The SR/DR ring program registers and the credit bookkeeping
 * --------------------------------------------------------------------- */
struct lw_dr_channel {
	u32 base;		/* +0x30: the node-array device VA */
	u32 depth;		/* +0x34[9:0] = depth-1 */
	u32 producer;		/* +0x38: the committed producer index (the GRANT) */
	u32 consumer;		/* +0x3c: the device's 16-bit read index */
	u32 outstanding;	/* (producer - consumer) mod 2*depth, 10-bit + phase */
};

struct lw_sr_channel {
	u32 ctrl;
	u32 base;
	u32 depth;
	u32 wptr;
	u32 rptr;
};

/* A guarded dword read: only addresses inside the mapped ETE ring page are
 * touched, so a stale node-array or buffer address can never become an
 * arbitrary device read (the misc-window panic is the precedent).  `dev_ca` is a
 * device address; `off` is relative to it and must keep the access in the page. */
static int lw_ring_dword(u32 dev_ca, unsigned long off, u32 *out)
{
	unsigned long o;

	if (dev_ca < LW_CA(0x4003a000UL) || dev_ca >= LW_CA(0x4003a000UL) + LW_WIN_BYTES)
		return -ERANGE;
	o = (unsigned long)(dev_ca - 0x4003a000UL) + off;
	if (o + 4UL > LW_WIN_BYTES)
		return -ERANGE;
	*out = lw_rd(lw.ring, o);
	return 0;
}

/*
 * The DR completion predicate, verbatim from pcie_ete_rcv_buff_check @0x14d74
 * (credit2.md sec 2.1): accept a slot iff the BUFFER header's 16-bit field at
 * +0x0a reads 0x5a5a and its 16-bit length field at +0x04 is non-zero.  Both are
 * passed as the aligned dwords that contain them: the dword at buf+0x08 (whose
 * high half is the tag) and the dword at buf+0x04 (whose low half is the len).
 */
static bool lw_dr_hdr_ok(u32 hdr_tag_word, u32 hdr_len_word)
{
	return (u16)(hdr_tag_word >> 16) == LW_DR_HDR_TAG && (u16)hdr_len_word != 0U;
}

static void lw_decode_rings(void)
{
	unsigned int i;

	pr_info(LW_NAME ": ---- ETE ring program registers (CA 0x4003a000, register blocks) ----\n");
	pr_info(LW_NAME ":   stride 0x%x between register blocks (credit2.md sec 5; NOT the host struct stride)\n",
		(unsigned int)LW_DR_STRIDE);

	for (i = 0; i < LW_SR_N; i++) {
		unsigned long b = LW_SR0_BASE + (unsigned long)i * LW_SR_STRIDE;
		struct lw_sr_channel sr;

		sr.ctrl = lw_rd(lw.ring, b + LW_SR_CTRL);
		sr.base = lw_rd(lw.ring, b + LW_SR_BASE);
		sr.depth = lw_rd(lw.ring, b + LW_SR_DEPTH);
		sr.wptr = lw_rd(lw.ring, b + LW_SR_WPTR);
		sr.rptr = lw_rd(lw.ring, b + LW_SR_RPTR);
		pr_info(LW_NAME ":   SR ch%u CA 0x%08lx ctrl=0x%08x base=0x%08x depth-1=%u wptr=0x%08x rptr=0x%08x\n",
			i, LW_CA(0x4003a000UL + b), sr.ctrl, sr.base,
			(sr.depth >> LW_RING_DEPTH_SHIFT) & LW_RING_DEPTH_MASK,
			sr.wptr, sr.rptr);
	}

	for (i = 0; i < LW_DR_N; i++) {
		unsigned long b = LW_DR0_BASE + (unsigned long)i * LW_DR_STRIDE;
		struct lw_dr_channel dr;
		u32 depth, delta;

		dr.base = lw_rd(lw.ring, b + LW_DR_BASE);
		dr.depth = (lw_rd(lw.ring, b + LW_DR_DEPTH) >> LW_RING_DEPTH_SHIFT) & LW_RING_DEPTH_MASK;
		dr.producer = lw_rd(lw.ring, b + LW_DR_PROD);
		dr.consumer = lw_read(lw.ring, b + LW_DR_CONS, 2U);	/* a 16-bit index */
		depth = dr.depth + 1U;
		/* credit = (producer - consumer) mod (2*depth); the index pair is 10-bit
		 * with a phase bit at 10, so the raw delta is masked to that width and
		 * wrapped when depth is known. */
		delta = (dr.producer - dr.consumer) & LW_IDX_MASK;
		dr.outstanding = (depth && delta >= 2U * depth) ? (delta & (2U * depth - 1U)) : delta;
		pr_info(LW_NAME ":   DR ch%u CA 0x%08lx base=0x%08x depth=%u prod(grant)=0x%08x cons=0x%08x outstanding=%u\n",
			i, LW_CA(0x4003a000UL + b), dr.base, depth, dr.producer, dr.consumer,
			dr.outstanding);
		if (dr.producer == dr.consumer)
			pr_info(LW_NAME ":     ch%u: the index pair is LOCKED (everything granted has retired)\n", i);

		/* the 0x5a5a header test, on the node the device would consume next:
		 * 8-byte nodes {word0 = payload-buffer device VA, word1}, then the
		 * buffer header's own dwords 1 and 2. */
		{
			u32 w0 = 0, w1 = 0;
			unsigned long noff = (unsigned long)(dr.consumer & LW_IDX_MASK) * 8UL;
			int rc = lw_ring_dword(dr.base, noff, &w0);

			if (rc == 0)
				rc = lw_ring_dword(dr.base, noff + 4UL, &w1);
			if (rc == 0) {
				u32 tagw = 0, lenw = 0;
				int rc2 = lw_ring_dword(w0, 4UL, &lenw);

				if (rc2 == 0)
					rc2 = lw_ring_dword(w0, 8UL, &tagw);
				pr_info(LW_NAME ":     ch%u node[%u] word0=0x%08x word1=0x%08x (w1=%s)\n",
					i, dr.consumer & LW_IDX_MASK, w0, w1,
					w1 == 0U ? "0, as the vendor's live ring reads" : "NON-ZERO");
				if (rc2 == 0)
					pr_info(LW_NAME ":     ch%u buffer header: 0x5a5a test %s (tag=0x%04x len=0x%04x)\n",
						i, lw_dr_hdr_ok(tagw, lenw) ? "PASSED (a deposit)" : "FAILED (no deposit)",
						(u16)(tagw >> 16), (u16)lenw);
				else
					pr_info(LW_NAME ":     ch%u buffer at 0x%08x is outside the mapped ring page - header test not applicable\n",
						i, w0);
			} else {
				pr_info(LW_NAME ":     ch%u node array at 0x%08x is outside the mapped ring page - the 0x5a5a header test is not applicable here (the vendor places it in host DRAM)\n",
					i, dr.base);
			}
		}
	}
}

/* ------------------------------------------------------------------------
 * 4. The take-related status reads (glue side ONLY - never the ack IAR)
 * --------------------------------------------------------------------- */
struct lw_take {
	u32 out0_first, out0_last;
	u32 raw_first, raw_last;
	u32 status_last;
	u32 twin_raw_first, twin_raw_last;
	u32 d2h_last;
	bool raw_rose;
	bool twin_rose;
	const char *verdict;
};

static void lw_sample_take(struct lw_take *t)
{
	unsigned int n, i;

	n = lw_samples;
	if (n < 1U)
		n = 1U;
	if (n > 64U)
		n = 64U;

	t->out0_first = lw_rd(lw.msg, LW_MSG_OUT0);
	t->raw_first = lw_rd(lw.msg, LW_MSG_RAW);
	t->twin_raw_first = lw_rd(lw.msg, LW_TWIN_RAW);
	t->out0_last = t->out0_first;
	t->raw_last = t->raw_first;
	t->twin_raw_last = t->twin_raw_first;

	pr_info(LW_NAME ": ---- take-status window (%u sample(s), %u ms apart; ZERO host stores) ----\n",
		n, lw_delay_ms);
	for (i = 0; i < n; i++) {
		if (i) {
			if (lw_delay_ms)
				msleep(min(lw_delay_ms, 1000U));
		}
		t->out0_last = lw_rd(lw.msg, LW_MSG_OUT0);
		t->raw_last = lw_rd(lw.msg, LW_MSG_RAW);
		t->twin_raw_last = lw_rd(lw.msg, LW_TWIN_RAW);
		t->status_last = lw_rd(lw.msg, LW_MSG_STATUS);
		t->d2h_last = lw_rd(lw.d2h, LW_D2H_ASSERT);
		pr_info(LW_NAME ":   t%u out[0]=0x%08x raw=0x%08x stat=0x%08x twin_raw=0x%08x d2h=0x%08x\n",
			i, t->out0_last, t->raw_last, t->status_last, t->twin_raw_last, t->d2h_last);
	}

	t->raw_rose = ((t->raw_first ^ t->raw_last) & (1U << LW_INTR_BIT_D2H_RX)) != 0U;
	t->twin_rose = ((t->twin_raw_first ^ t->twin_raw_last) & (1U << LW_INTR_BIT_D2H_RX)) != 0U;

	/*
	 * The arc's decision rule (realchain.md sec 5) with the host leg removed:
	 * this module stores nothing, so a copy-A bit-3 rise across the window is
	 * device-originated by construction.  out[0] -> 0 is the dispatcher's own
	 * consume witness (its store is *out[0] = 0 inside 0x818ac).
	 */
	if (t->out0_first != 0U && t->out0_last == 0U)
		t->verdict = "CONSUMED: out[0] fell to 0 (the device's dispatcher ran the H2D bit)";
	else if (t->raw_rose && !t->twin_rose)
		t->verdict = "DEVICE-ORIGINATED: copy-A D2H-RX (bit 3) rose with the twin unchanged and no host store";
	else if (t->raw_rose)
		t->verdict = "D2H-RX rose on BOTH copies - inspect (only a host store should move the twin)";
	else if (t->out0_last != 0U)
		t->verdict = "H2D PENDING: out[0] != 0 (the dispatcher has not consumed the posted id)";
	else
		t->verdict = "QUIET: no take observed in the window";

	pr_info(LW_NAME ":   raw bit3 %u -> %u (%s), twin bit3 %u -> %u (%s)\n",
		!!(t->raw_first & (1U << LW_INTR_BIT_D2H_RX)),
		!!(t->raw_last & (1U << LW_INTR_BIT_D2H_RX)), t->raw_rose ? "ROSE" : "held",
		!!(t->twin_raw_first & (1U << LW_INTR_BIT_D2H_RX)),
		!!(t->twin_raw_last & (1U << LW_INTR_BIT_D2H_RX)), t->twin_rose ? "ROSE" : "held");
	pr_info(LW_NAME ":   TAKE VERDICT: %s\n", t->verdict);
}

/* ------------------------------------------------------------------------
 * 5. The claim, the maps and the decode
 * --------------------------------------------------------------------- */
static int lw_attach(void)
{
	struct pci_dev *pdev = lw.pdev;
	int rc;

	if (!pdev) {
		pr_err(LW_NAME ": no 59e7:0005 endpoint bound in domain %u (pass domain=<n>)\n",
		       lw_domain);
		return -ENODEV;
	}

	rc = pci_enable_device(pdev);
	if (rc) {
		pr_err(LW_NAME ": pci_enable_device rc=%d\n", rc);
		return rc;
	}

	/* REFUSE rather than fight: if the vendor stack owns the BARs this fails, and
	 * that is the correct outcome (coexistence, never replacement). */
	rc = pci_request_mem_regions(pdev, LW_NAME);
	if (rc) {
		pr_err(LW_NAME ": pci_request_mem_regions rc=%d (the vendor stack owns the BARs?) - refusing\n",
		       rc);
		pci_disable_device(pdev);
		return rc;
	}

	lw.bar0 = pci_resource_start(pdev, 0);
	pr_info(LW_NAME ": BAR0 host base 0x%08lx, claim ok (read-only skeleton)\n", lw.bar0);

	lw.hs = pci_iomap_range(pdev, 0, LW_WIN_HS, LW_WIN_BYTES);
	lw.msg = pci_iomap_range(pdev, 0, LW_WIN_MSG, LW_WIN_BYTES);
	lw.ring = pci_iomap_range(pdev, 0, LW_WIN_RING, LW_WIN_BYTES);
	lw.d2h = pci_iomap_range(pdev, 0, LW_WIN_D2H, LW_WIN_BYTES);
	if (!lw.hs || !lw.msg || !lw.ring || !lw.d2h) {
		pr_err(LW_NAME ": pci_iomap_range failed (hs=%d msg=%d ring=%d d2h=%d)\n",
		       !!lw.hs, !!lw.msg, !!lw.ring, !!lw.d2h);
		rc = -ENOMEM;
		goto err;
	}
	pr_info(LW_NAME ": mapped BAR0+0x%lx (handshake), +0x%lx (glue/twin), +0x%lx (rings), +0x%lx (D2H)\n",
		(unsigned long)LW_WIN_HS, (unsigned long)LW_WIN_MSG,
		(unsigned long)LW_WIN_RING, (unsigned long)LW_WIN_D2H);

	/* decode - reads only, dword-aligned, bracket-safe */
	lw_decode_announce();
	{
		struct lw_mailbox mb;

		lw_decode_mailbox(&mb);
	}
	lw_decode_rings();
	{
		struct lw_take t;

		lw_sample_take(&t);
	}

	pr_info(LW_NAME ": decode complete - ZERO stores issued (no doorbell, no descriptor, no firmware window)\n");
	return 0;

err:
	if (lw.hs) {
		pci_iounmap(pdev, lw.hs);
		lw.hs = NULL;
	}
	if (lw.msg) {
		pci_iounmap(pdev, lw.msg);
		lw.msg = NULL;
	}
	if (lw.ring) {
		pci_iounmap(pdev, lw.ring);
		lw.ring = NULL;
	}
	if (lw.d2h) {
		pci_iounmap(pdev, lw.d2h);
		lw.d2h = NULL;
	}
	pci_release_mem_regions(pdev);
	pci_disable_device(pdev);
	return rc;
}

static void lw_detach(void)
{
	struct pci_dev *pdev = lw.pdev;

	if (!pdev)
		return;
	if (lw.hs) {
		pci_iounmap(pdev, lw.hs);
		lw.hs = NULL;
	}
	if (lw.msg) {
		pci_iounmap(pdev, lw.msg);
		lw.msg = NULL;
	}
	if (lw.ring) {
		pci_iounmap(pdev, lw.ring);
		lw.ring = NULL;
	}
	if (lw.d2h) {
		pci_iounmap(pdev, lw.d2h);
		lw.d2h = NULL;
	}
	pci_release_mem_regions(pdev);
	pci_disable_device(pdev);
}

/* ---- the pci_driver (matches both domains; the domain param selects one) - */
static const struct pci_device_id lw_pci_ids[] = {
	{ PCI_DEVICE(LW_PCI_VENDOR, LW_PCI_DEVICE) },
	{ }
};
MODULE_DEVICE_TABLE(pci, lw_pci_ids);

static int lw_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct pci_dev *want;

	(void)id;
	if (!lw_hw)
		return -ENODEV;		/* hw=0: registration/ABI only, no claim */

	/* the domain selector, as wifidrv1's probe does it: the core hands us every
	 * 59e7:0005 endpoint, and only the one the domain param names is ours. */
	want = pci_get_domain_bus_and_slot(lw_domain, 0, PCI_DEVFN(0, 0));
	if (want != pdev) {
		if (want)
			pci_dev_put(want);
		return -ENODEV;
	}
	pci_dev_put(want);
	if (lw.pdev)
		return -EBUSY;
	lw.pdev = pdev;
	pr_info(LW_NAME ": bound bus %02x:%02x.%u (domain %u) for the read-only decode\n",
		pdev->bus->number, PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn),
		lw_domain);
	return 0;
}

static void lw_pci_remove(struct pci_dev *pdev)
{
	if (lw.pdev == pdev)
		lw.pdev = NULL;
}

static struct pci_driver lw_pci_driver = {
	.name		= LW_NAME,
	.id_table	= lw_pci_ids,
	.probe		= lw_pci_probe,
	.remove		= lw_pci_remove,
};

static int __init lw_init(void)
{
	int rc;
	unsigned int n = lw_samples;

	if (n < 1U)
		n = 1U;
	if (n > 64U)
		n = 64U;
	pr_info(LW_NAME ": the Wi-Fi service skeleton (read-only) hw=%u domain=%u samples=%u delay_ms=%u\n",
		lw_hw, lw_domain, n, lw_delay_ms);

	if (!lw_hw) {
		pr_info(LW_NAME ": hw=0 - registration/ABI only; pass hw=1 to claim and decode\n");
		return 0;
	}

	rc = pci_register_driver(&lw_pci_driver);
	if (rc) {
		pr_err(LW_NAME ": pci_register_driver rc=%d\n", rc);
		return rc;
	}
	rc = lw_attach();
	if (rc) {
		pci_unregister_driver(&lw_pci_driver);
		return rc;
	}
	return 0;
}

static void __exit lw_exit(void)
{
	if (lw_hw) {
		lw_detach();
		pci_unregister_driver(&lw_pci_driver);
	}
}

module_init(lw_init);
module_exit(lw_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hi5671Y luofu Wi-Fi service skeleton (read-only; the announce state machine + the glue/ring decode)");
MODULE_AUTHOR("omo");
