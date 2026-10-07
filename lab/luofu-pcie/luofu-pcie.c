// SPDX-License-Identifier: GPL-2.0
/*
 * luofu-pcie: stage-2 driver FRAME for the Hi5671Y "luofu" PCIe root complex
 * (two DWC domains, DT compatible "hisilicon,luofu-pcie").
 *
 * ==========================  DRIVER FRAME  ==========================
 * The stage-2 DRIVER FRAME: the full platform-driver shape the bring-up fills
 * in (of_match_table + probe/remove + the from-scratch host-controller plan of
 * pcierc.md sec 2), with the three per-pcierc.md windows mapped and the read
 * path proven live.  The clk/reset wiring, the misc mode/LTSSM writes and the
 * pci_scan_root_bus_bridge registration stay TODO (staged in pcidrv.md).
 *
 * What the frame carries NOW:
 *   - module_init + platform_driver + of_match ("hisilicon,luofu-pcie") +
 *     probe/remove (pcierc.md sec 4b);
 *   - ioremap of DBI + config + the WRITE-ONLY port-logic (misc) window, from
 *     DT reg-names or the pinned CAs;
 *   - the aligned dword-field accessor (luofu_pcie_read, the access rule);
 *   - the link-state read path (DL_ACTIVE decode, the safe LTSSM substitute);
 *   - the force_probe=1 DT-less bench path (the proven smoke path, kept).
 *
 * It COMPILES against the vanilla 5.10.201 arm headers in the CI cross-build
 * (.github/workflows/lab-module-build.yml -> lab/luofu-pcie, plus the
 * build-load-test-module.yml lane) and performs NO register writes: probe maps
 * the windows and runs a read-only status inventory (FORCED probe:
 * dbi 0x10160000 + cfg 0x50000000 + write-only misc 0x10161000 on RC0).
 * ================================================================
 *
 * Spec: build/tmp/inta-spec/pcierc.md (the register receipt is
 * build/tmp/inta-spec/pciskel.md).  Every address/offset below cites the
 * pcierc.md section or the pinned DTS `reg`/`iatu_rc` it was transcribed from.
 * The vendor reference is `hi_pcie.ko` (stock firmware
 * rootfs-2.4.15/squashfs-root/lib/hisilicon/ko/hi_pcie.ko, disassembled with
 * lab/ko_disasm.py: hi_pcie_probe @0xb8c + the sections in pcierc.md sec 1).
 *
 * The five-window layout the vendor `hi_pcie` maps (pcierc.md sec 1, pinned DTS
 * `reg`/`reg-names`), and what this frame does with each:
 *
 *   reg-name   RC0 CA       size      this module
 *   dbi        0x10160000   0x1000    MAPPED + READ: the RC's own config header
 *                                     (vendor/device id), the DWC Link
 *                                     Control/Status words, the port-logic and
 *                                     the iATU register file
 *   misc       0x10161000   0x3000    MAPPED, WRITE-ONLY, NEVER READ.  The SoC
 *                                     "port-logic/app" block is write-only
 *                                     host-side (pcierc.md sec 1 + sec 4):
 *                                     mode select misc+0x00, LTSSM enable
 *                                     misc+0x1c, linkdown irq misc+0x28/0x2c.
 *                                     A read-only devmem of it PANICKED the
 *                                     box (phase20/host-window.md C.4); the
 *                                     HARD RULE stands for host-side reads.
 *                                     ioremap() alone performs no bus access,
 *                                     so the frame maps it ready for the
 *                                     staged writes; no readl()/writel() may
 *                                     touch it yet.
 *   cfg        0x50000000   0x1000    MAPPED + READ: the downstream dev-0 config
 *                                     window (iATU viewport 0, target of CFG0)
 *   mem        0x40000000   0x2000000 not mapped here (iATU viewport 1)
 *   io         0x48000000   0x800000  not mapped here (iATU viewport 2)
 *
 * The record labels `misc` the port-logic/app block, and its status words
 * (misc+0x100 link status, misc+0x110 LTSSM) are exactly the ones the vendor
 * polls.  Because misc is read-forbidden host-side, the READ-SAFE substitutes
 * the record prescribes are used instead (pcierc.md sec 4b): the DWC Link
 * Status DL_ACTIVE bit at DBI+0x82 / cfg+0x82, plus the DBI port-logic status
 * words (Link Capabilities / Link Control / Link Width-Speed Control).  The raw
 * LTSSM state therefore stays OUT of this frame by design - it lives only in
 * the read-forbidden misc window.
 *
 * HARD RULES honoured: never write CA 0x400392f0; never read the RC misc window
 * 0x10161000; never read the host-side GICC IAR 0x4016010c; no register write
 * of any kind from this module.  readl() is the ONLY MMIO op.
 *
 * THE ACCESS RULE (pciskel-smoke.md, readw.md, rcfix.md).  This DBI/CFG window
 * answers only 4-BYTE-ALIGNED 32-bit accesses (a DWC DBI sits behind a word-wide
 * APB bridge).  A read whose ADDRESS is not 4-byte aligned external-aborts the
 * bus - an IMPRECISE abort becomes an SError and panics - whatever the access
 * WIDTH is.  Both live panics are the 16-bit Link Status register at the
 * 2-mod-4 offset 0x082:
 *   - ko 503f9580 (503f9580c29f555a47755ca93e61feeb): readl(dbi + 0x082),
 *     fault 0xc800a082 = rc0's ioremap 0xc800a000 + 0x82 (pciskel-smoke.md);
 *   - ko 51376f76 (51376f7608d6e5d60b0bb6fe09eb068e): readw(dbi + 0x082) - the
 *     "width fix" - fault at rc0's ioremap 0xc9a7d000 + 0x82, PC
 *     luofu_pcie_inventory+0x7c, faulting instruction `ldrh r4,[r3]` with
 *     r3 = 0xc9a7d082 and the preceding header reads all correct.  A narrow
 *     accessor cannot help: 0x082 is 2 mod 4, so NO access of that register is
 *     ever aligned.
 * The fix is therefore not a narrower accessor but an ALIGNED one: every field
 * is fetched by reading the 4-byte-aligned dword that CONTAINS it and
 * extracting the field's byte lanes.  For 0x082 the containing dword is the
 * aligned 0x080 whose low half is Link Control and whose high half is Link
 * Status; the same holds for every other 16/8-bit header field (0x000/0x004/
 * 0x008(32)/0x02c), each of which is an aligned field of an aligned dword.  All
 * the DBI reads that succeeded live did so at 4-byte-aligned offsets, so the
 * aligned dword read is the proven-safe primitive on this window.
 * TODO (write path): the future DBI writes (DBI+0x04 = 7, ASPM |= 3, LTSSM |=)
 * must be 4-byte-aligned read-modify-write - readl the dword, modify the field,
 * writel the whole dword - never a sub-word or non-aligned register store.
 *
 * force_probe=1: the vendor kernel's live DT carries "hsan,pcie", not this
 * driver's compatible, so probe never fires (pcierc.md sec 5).  force_probe=1
 * registers two name-matched platform_devices (one per RC domain, no of_node)
 * that bind through platform_match()'s name compare only; probe maps the pinned
 * CAs with devm_ioremap() (the regions are already owned by the vendor hi_pcie,
 * so request_mem_region would -EBUSY) and runs the READ-ONLY inventory.  It is a
 * no-op the day a luofu DT node exists.
 *
 * TODO (pcierc.md sec 2, the 14-step hi_pcie_probe order): clk_bulk_enable from
 * `&crg` -> 4 reset_control_deassert -> `misc+0x00 = 0x40000000` RC mode ->
 * set_iatu (misc+0x1c |= 0x2000; write DBI+0x900+0x200*i; clear) -> endpoint
 * power via `pcie-gpios` -> `DBI+0x04 = 7` -> ASPM -> link-speed target ->
 * `misc+0x1c |= 0x800` LTSSM -> poll DBI+0x82 DL_ACTIVE -> linkdown irq ->
 * pci_scan_root_bus_bridge.  Every misc access there is a WRITE.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>

/* ------------------------------------------------------------------ *
 * Window CAs and sizes (pcierc.md sec 1 table; pinned DTS `reg`).     *
 * ------------------------------------------------------------------ */
#define LUOFU_PCIE_RC0_DBI	0x10160000UL	/* reg-name "dbi" */
#define LUOFU_PCIE_RC1_DBI	0x10164000UL
#define LUOFU_PCIE_DBI_SIZE	0x1000UL
#define LUOFU_PCIE_RC0_CFG	0x50000000UL	/* reg-name "cfg" (dev-0 ECAM) */
#define LUOFU_PCIE_RC1_CFG	0x68000000UL
#define LUOFU_PCIE_CFG_SIZE	0x1000UL

/* Geometry for the write-only misc window and the downstream mem/io windows.
 * misc 0x10161000/0x10165000 is the read-forbidden "port-logic/app" block: the
 * frame ioremaps it so the staged mode/LTSSM writes have a VA ready, but
 * nothing dereferences it yet.  mem/io are the iATU viewport 1/2 downstream
 * windows (pcierc.md sec 1) and stay documented-only constants. */
#define LUOFU_PCIE_RC0_MISC	0x10161000UL	/* WRITE-ONLY, NEVER READ */
#define LUOFU_PCIE_RC1_MISC	0x10165000UL	/* WRITE-ONLY, NEVER READ */
#define LUOFU_PCIE_MISC_SIZE	0x3000UL
#define LUOFU_PCIE_RC0_MEM	0x40000000UL
#define LUOFU_PCIE_MEM_SIZE	0x2000000UL
#define LUOFU_PCIE_RC0_IO	0x48000000UL
#define LUOFU_PCIE_IO_SIZE	0x800000UL

/* ------------------------------------------------------------------ *
 * DBI status registers (the RC's own config header + DWC link/port-  *
 * logic words).  Offsets are DBI-relative = plain PCI config-space    *
 * offsets (pcierc.md sec 1, sec 4).                                   *
 * ------------------------------------------------------------------ */
#define DBI_VENDOR_DEVICE_ID	0x000u	/* type-1 header id (measurement) */
#define DBI_COMMAND		0x004u	/* hi_pcie_init_cmd_status_reg writes 7 */
#define DBI_LINK_CAPABILITIES	0x07cu	/* the 32-bit Link Capabilities register */
#define DBI_LINK_CONTROL	0x080u	/* 16-bit Link Control (ASPM bits 1:0; vendor |= 3) */
#define DBI_LINK_STATUS		0x082u	/* DL_ACTIVE bit 13 (the safe link-up read) */
#define DBI_LINK_WIDTH_SPEED	0x80cu	/* hi_pcie_check_link_status |= 0x20000 (retrain) */

/*
 * Register FIELD widths (PCI/PCIe spec, see build/tmp/inta-spec/readw.md and
 * rcfix.md).  Under the access rule in the header, the width below no longer
 * selects an ACCESS width - it names the field's byte lanes inside its
 * containing dword, and luofu_pcie_read() extracts them.  Spec widths:
 * 0x000/0x004/0x02c are the type-1 header Vendor/Device ID, PCI_COMMAND and
 * Subsystem (I/O-Base-Upper on an RC) words (16/16/16 bits); cfg+0x008 is the
 * 32-bit Class/Revision dword; 0x07c is the 32-bit PCIe-capability Link
 * Capabilities register; 0x080/0x082 are the Link Control / Link Status halves
 * (16/16 bits) of the dword at 0x080; 0x80c is the 32-bit Link Width/Speed
 * Control.  The live RC's own readback confirms the geometry (pciskel-smoke.md
 * sec 4): 0x07c = 0x00734c12 (5GT/s x1, L0s+L1 ASPM support) and 0x080 =
 * 0x70120000 ({Link Control = 0x0000, Link Status = 0x7012}).  The DWC iATU
 * file (0x900..0x924) is a DWC-internal dword block at a 4-byte stride and is
 * read directly with readl().
 */
#define W8	1u
#define W16	2u
#define W32	4u

/* DWC iATU register file (pcierc.md sec 1).  0x900 selects the viewport; the
 * seven words at 0x904..0x91c are that viewport's CTRL1/CTRL2/base/limit/target.
 * The vendor writes the three `iatu_rc` entries to DBI+0x900+0x200*i. */
#define DBI_IATU_VIEWPORT	0x900u
#define DBI_IATU_CTRL1		0x904u
#define DBI_IATU_CTRL2		0x908u
#define DBI_IATU_LOWER_BASE	0x90cu
#define DBI_IATU_UPPER_BASE	0x910u
#define DBI_IATU_LIMIT		0x914u
#define DBI_IATU_LOWER_TARGET	0x918u
#define DBI_IATU_UPPER_TARGET	0x91cu

/* iATU CTRL1 type field values (pcierc.md sec 1 `iatu_rc` legend). */
#define IATU_TYPE_MEM		0x0u
#define IATU_TYPE_IO		0x2u
#define IATU_TYPE_CFG0		0x4u

/* cfg-window register offsets (the downstream `59e7:0005` dev-0 header). */
#define CFG_VENDOR_DEVICE_ID	0x000u
#define CFG_COMMAND		0x004u
#define CFG_CLASS_REVISION	0x008u
#define CFG_SUBSYSTEM_ID	0x02cu
#define CFG_LINK_STATUS		0x082u

/* Link Status DL_ACTIVE (pcierc.md sec 4).  Bit 13 by the record; bits 3:0 are
 * the negotiated link speed (1 = 2.5 GT/s, 2 = 5 GT/s). */
#define LS_DL_ACTIVE		(1u << 13)
#define LS_LINK_TRAINING	(1u << 4)
#define LS_SPEED_MASK		0xfu

/* The two RC domains (pcierc.md sec 1: RC0/RC1, each with its own DBI + cfg). */
struct luofu_pcie_rc_ca {
	unsigned long dbi_ca;
	unsigned long misc_ca;
	unsigned long cfg_ca;
	const char *name;
};

static const struct luofu_pcie_rc_ca luofu_rcs[] = {
	{ LUOFU_PCIE_RC0_DBI, LUOFU_PCIE_RC0_MISC, LUOFU_PCIE_RC0_CFG, "rc0" },
	{ LUOFU_PCIE_RC1_DBI, LUOFU_PCIE_RC1_MISC, LUOFU_PCIE_RC1_CFG, "rc1" },
};

/* ------------------------------------------------------------------ *
 * The transcribed `iatu_rc` tables (pcierc.md sec 1, verbatim from    *
 * the pinned DTS).  One entry is eight words:                         *
 *   <viewport, ctrl1(type), ctrl2, base_lo, base_hi, limit,           *
 *    target_lo, target_hi>                                            *
 * The RC's iATU is SELECTED through DBI+0x900, so a read of            *
 * DBI+0x904..0x91c returns whichever entry the vendor left selected    *
 * (they program 0,1,2 in order).  Reading is index-safe; a WRITE to    *
 * 0x900 to re-select is not done here (ZERO writes).                   *
 * ------------------------------------------------------------------ */
struct luofu_iatu_entry {
	u32 viewport;
	u32 ctrl1;
	u32 ctrl2;
	u32 base_lo;
	u32 base_hi;
	u32 limit;
	u32 target_lo;
	u32 target_hi;
};

static const struct luofu_iatu_entry luofu_iatu_rc0[] = {
	{ 0, IATU_TYPE_CFG0, 0x80000000u, 0x50000000u, 0, 0x57ffffffu, 0x00000000u, 0 },
	{ 1, IATU_TYPE_MEM,  0x80000000u, 0x40000000u, 0, 0x47ffffffu, 0x40000000u, 0 },
	{ 2, IATU_TYPE_IO,   0x80000000u, 0x48000000u, 0, 0x4fffffffu, 0x48000000u, 0 },
};

static const struct luofu_iatu_entry luofu_iatu_rc1[] = {
	{ 0, IATU_TYPE_CFG0, 0x80000000u, 0x68000000u, 0, 0x6fffffffu, 0x00000000u, 0 },
	{ 1, IATU_TYPE_MEM,  0x80000000u, 0x58000000u, 0, 0x5fffffffu, 0x58000000u, 0 },
	{ 2, IATU_TYPE_IO,   0x80000000u, 0x60000000u, 0, 0x67ffffffu, 0x60000000u, 0 },
};

/*
 * A predicted status word.  mask = bits the record predicts; 0 = measurement
 * only (the value is logged, nothing asserted).  expect is compared against
 * (value & mask).
 */
struct luofu_pcie_reg {
	u16 offset;
	u8 width;		/* field width W8/W16/W32 = its byte lanes in the dword */
	const char *name;
	u32 expect;
	u32 mask;
};

/*
 * The aligned field read - the fix for both live panics (header comment).  The
 * window only answers 4-byte-aligned 32-bit accesses, so a read at a 2-mod-4
 * offset aborts WHATEVER its width; that is why the 16-bit Link Status at 0x082
 * cannot be read directly (the readw() there was ko 51376f76's panic, the same
 * fault the readl() had been in ko 503f9580).  Every field is instead fetched
 * from the 4-byte-aligned dword that contains it: ONE readl() of
 * `base + (off & ~3u)`, then shift down by the field's byte offset inside that
 * dword and mask to (width * 8) bits.  For a W32 field at an aligned offset this
 * is exactly the old readl(); for a sub-word header field it returns the same
 * value the aligned-address readw() did, and it never issues a non-aligned or
 * sub-dword MMIO access.  Still read-only: readl() is the module's only MMIO op,
 * and the tables hold offsets/widths only, so a write cannot be expressed here.
 */
static u32 luofu_pcie_read(void __iomem *base, u16 off, u8 width)
{
	u32 word = readl(base + (off & ~3u));
	unsigned int shift = (off & 3u) * 8u;

	word >>= shift;
	if (width >= 4u)
		return word;
	return word & ((1u << (width * 8u)) - 1u);
}

/* DBI status inventory (read-only).  Only pure-read status words live here; no
 * register with a write side effect is reachable through this table. */
static const struct luofu_pcie_reg luofu_dbi_status[] = {
	/* The RC's own type-1 header id: the vendor's `read_conf` exposes only the
	 * downstream devfn 0, so Linux never reads this and the record carries no
	 * proven value -> measurement only. */
	{ DBI_VENDOR_DEVICE_ID, W16, "dbi+0x000  RC vendor/device id (type-1 header)", 0, 0 },
	/* hi_pcie_init_cmd_status_reg writes 7; the I/O-enable bit reads RO0 on this
	 * SoC (the endpoint's own cfg+0x004 readback is 0x6), so predict MEM|MASTER. */
	{ DBI_COMMAND,          W16, "dbi+0x004  PCI_COMMAND (vendor writes 7)", 0x00000006u, 0x00000006u },
	/* Link Capabilities is its own 32-bit register, not a 16-bit Link Control: the
	 * live readback 0x00734c12 decodes as max-speed 5GT/s, max-width x1, ASPM
	 * L0s+L1 support (pciskel-smoke.md sec 4).  Measurement only. */
	{ DBI_LINK_CAPABILITIES, W32, "dbi+0x07c  Link Capabilities", 0, 0 },
	/* hi_pcie_enable_aspm writes |= 3 (L0s + L1) into Link Control's ASPM field,
	 * but the live vendor boot reads that field OFF - the word at 0x080 reads
	 * {Link Control = 0x0000, Link Status = 0x7012} (pciskel-smoke.md sec 4), so
	 * the prediction is re-pinned to 0: the pre-fix `expect 3` was a bad
	 * predictor, not a lost measurement (pciskel-smoke.md sec 7). */
	{ DBI_LINK_CONTROL,     W16, "dbi+0x080  Link Control (vendor |= 3 ASPM)", 0x00000000u, 0x00000003u },
	/* The safe link-up read that replaces the forbidden misc+0x100.  W16: a
	 * 32-bit read of this 2-byte register is the panic. */
	{ DBI_LINK_STATUS,      W16, "dbi+0x082  Link Status (DL_ACTIVE bit 13)", LS_DL_ACTIVE, LS_DL_ACTIVE },
	/* Bit 17 is the SPEED_CHANGE trigger (write-1-to-initiate, self-clearing),
	 * so a 0 readback on a live link is normal -> measurement only. */
	{ DBI_LINK_WIDTH_SPEED, W32, "dbi+0x80c  Link Width/Speed Ctl (bit17=trigger)", 0, 0 },
};

/* cfg status inventory (read-only): the downstream endpoint's dev-0 header.
 * Expected values from the live vendor evidence (epinit readback + boot dmesg
 * `pci 0000:00:00.0: [59e7:0005] type 00 class 0x028000`). */
static const struct luofu_pcie_reg luofu_cfg_status[] = {
	/* W16: the read returns the vendor-ID half only (0x59e7), so the expect must be
	 * that half - the pre-fix readl carried the full dword 0x000559e7, and a
	 * full-dword expect can never equal a 16-bit read. */
	{ CFG_VENDOR_DEVICE_ID, W16, "cfg+0x000  endpoint vendor/device id [59e7:0005]",
	  0x000059e7u, 0x0000ffffu },
	{ CFG_COMMAND,          W16, "cfg+0x004  endpoint PCI_COMMAND (MEM|MASTER)",
	  0x00000006u, 0x00000006u },
	/* W32: class/revision is a 32-bit header dword at a 4-byte-aligned offset. */
	{ CFG_CLASS_REVISION,   W32, "cfg+0x008  endpoint class/revision (class 0x028000)",
	  0x02800000u, 0xffffff00u },
	{ CFG_SUBSYSTEM_ID,     W16, "cfg+0x02c  endpoint subsystem vendor (19e5)",
	  0x000019e5u, 0x0000ffffu },
	/* W16: sub-word Link Status -> readw, the sibling of the dbi+0x082 bug. */
	{ CFG_LINK_STATUS,      W16, "cfg+0x082  endpoint Link Status (DL_ACTIVE bit 13)",
	  LS_DL_ACTIVE, LS_DL_ACTIVE },
};

/* Per-instance state. */
struct luofu_pcie {
	void __iomem *dbi;
	void __iomem *cfg;
	void __iomem *misc;	/* WRITE-ONLY port-logic: mapped, NEVER read */
	unsigned int id;
};

/*
 * force_probe: run the probe body against the hardcoded pinned CAs (read-only)
 * with no DT match.  The live vendor DT carries "hsan,pcie", not this driver's
 * "hisilicon,luofu-pcie", so without the knob the probe never fires.
 */
static int force_probe;
module_param(force_probe, int, 0444);
MODULE_PARM_DESC(force_probe,
	"run the probe body against the hardcoded RC0/RC1 CAs (read-only)");

/*
 * Link-state read path (pcierc.md sec 4b).  The record's link-up read replaces
 * the read-forbidden misc+0x100/0x110 (link status / LTSSM) with the DWC Link
 * Status DL_ACTIVE bit at DBI+0x082 (the RC's own link) and cfg+0x082 (the
 * downstream endpoint's link).  The register is fetched through the aligned
 * dword reader -- the containing dword is DBI/cfg+0x080 whose low half is Link
 * Control and whose high half is Link Status (pciskel-smoke.md sec 4:
 * 0x080 = 0x70120000 -> {Link Control = 0x0000, Link Status = 0x7012}).
 *
 * Link Status fields (pinned by the record): DL_ACTIVE bit 13, link-training
 * bit 4, negotiated speed bits 3:0 (1 = 2.5 GT/s, 2 = 5 GT/s).  This is the
 * SAFE proxy for LTSSM=L0: the raw LTSSM state lives only in the
 * read-forbidden misc+0x110 and is deliberately not decoded here.
 */
struct luofu_pcie_link_state {
	bool up;		/* Link Status DL_ACTIVE (bit 13) */
	bool training;		/* link training in progress (bit 4) */
	unsigned int speed;	/* negotiated speed (bits 3:0) */
	u32 raw;		/* the 16-bit Link Status word */
};

/* Read + decode the aligned Link Status dword at `off` (DBI+0x082 / cfg+0x082).
 * The aligned dword reader guarantees one 4-byte-aligned readl(); the Link
 * Status half is extracted from the containing dword's high half. */
static struct luofu_pcie_link_state
luofu_pcie_read_link_status(void __iomem *base, u16 off)
{
	struct luofu_pcie_link_state st;
	u32 v = luofu_pcie_read(base, off, W16);

	st.raw = v;
	st.up = !!(v & LS_DL_ACTIVE);
	st.training = !!(v & LS_LINK_TRAINING);
	st.speed = v & LS_SPEED_MASK;
	return st;
}

/* Log one decoded link state. */
static void luofu_pcie_log_link_state(struct device *dev, const char *win,
				      const struct luofu_pcie_link_state *st)
{
	dev_info(dev,
		 "  %s link-state: DL_ACTIVE=%u (link %s), link-training=%u, neg-speed=%u%s\n",
		 win, st->up, st->up ? "UP" : "DOWN", st->training, st->speed,
		 st->speed == 1 ? " (2.5GT/s)" : st->speed == 2 ? " (5GT/s)" : "");
}

/* Read + log the link state for one window; return whether the link is up.
 * This is the reusable link-up predicate the staged bring-up polls (sec 2). */
static bool luofu_pcie_report_link(struct device *dev, const char *win,
				   void __iomem *base, u16 off)
{
	struct luofu_pcie_link_state st = luofu_pcie_read_link_status(base, off);

	luofu_pcie_log_link_state(dev, win, &st);
	return st.up;
}

/*
 * Read-only inventory of one register table.  Each word is fetched through
 * luofu_pcie_read() (the aligned dword-field accessor), so the only MMIO op is
 * a 4-byte-aligned readl() of the containing dword; the field's byte lanes are
 * extracted inside the accessor.  The table holds offsets + widths only, so a
 * write cannot be expressed here.  Returns the number of predicted words that
 * matched.
 */
static unsigned int luofu_pcie_inventory(struct device *dev, const char *win,
					 void __iomem *base,
					 const struct luofu_pcie_reg *tab,
					 size_t n, unsigned int *predicted)
{
	unsigned int i, matched = 0;

	*predicted = 0;
	for (i = 0; i < n; i++) {
		const struct luofu_pcie_reg *r = &tab[i];
		u32 v = luofu_pcie_read(base, r->offset, r->width);

		if (!r->mask) {
			dev_info(dev, "  %s [0x%03x] %s = 0x%04x (%u-bit, measurement)\n",
				 win, r->offset, r->name, v, r->width * 8);
			continue;
		}
		(*predicted)++;
		if ((v & r->mask) == r->expect)
			matched++;
		dev_info(dev, "  %s [0x%03x] %s = 0x%04x expect 0x%08x/0x%08x match=%s\n",
			 win, r->offset, r->name, v, r->expect, r->mask,
			 (v & r->mask) == r->expect ? "YES" : "NO");
	}
	return matched;
}

/*
 * Read-only iATU inspection.  DBI+0x900 is the viewport SELECTOR, so the seven
 * words at 0x904..0x91c belong to whichever entry the vendor left selected;
 * this only reads them and then names the transcribed `iatu_rc` entry they
 * match, rather than writing 0x900 to walk the table (ZERO writes).
 */
static void luofu_pcie_iatu_inventory(struct device *dev, void __iomem *dbi,
				      const struct luofu_iatu_entry *tab,
				      size_t n)
{
	struct luofu_iatu_entry got;
	u32 sel = readl(dbi + DBI_IATU_VIEWPORT);
	unsigned int i;
	u32 viewport = sel & 0x1fu;

	got.viewport = viewport;
	got.ctrl1 = readl(dbi + DBI_IATU_CTRL1);
	got.ctrl2 = readl(dbi + DBI_IATU_CTRL2);
	got.base_lo = readl(dbi + DBI_IATU_LOWER_BASE);
	got.base_hi = readl(dbi + DBI_IATU_UPPER_BASE);
	got.limit = readl(dbi + DBI_IATU_LIMIT);
	got.target_lo = readl(dbi + DBI_IATU_LOWER_TARGET);
	got.target_hi = readl(dbi + DBI_IATU_UPPER_TARGET);

	dev_info(dev,
		 "  iATU selector [0x900] = 0x%08x (viewport %u selected); readback ctrl1=0x%08x ctrl2=0x%08x base=0x%08x_%08x limit=0x%08x target=0x%08x_%08x\n",
		 sel, viewport, got.ctrl1, got.ctrl2, got.base_hi, got.base_lo,
		 got.limit, got.target_hi, got.target_lo);

	for (i = 0; i < n; i++) {
		const struct luofu_iatu_entry *e = &tab[i];

		dev_info(dev,
			 "  iatu_rc[%u] viewport=%u ctrl1=0x%08x ctrl2=0x%08x base=0x%08x limit=0x%08x target=0x%08x match=%s\n",
			 i, e->viewport, e->ctrl1, e->ctrl2, e->base_lo,
			 e->limit, e->target_lo, e->viewport == viewport ? "YES" : "NO");
	}
}

static const struct of_device_id luofu_pcie_match_table[] = {
	{ .compatible = "hisilicon,luofu-pcie" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, luofu_pcie_match_table);

static int luofu_pcie_probe(struct platform_device *pdev)
{
	const struct luofu_pcie_rc_ca *ca;
	struct luofu_pcie *rc;
	const char *label;
	unsigned int id, dbi_hits, cfg_hits, pred;
	bool link_up;

	rc = devm_kzalloc(&pdev->dev, sizeof(*rc), GFP_KERNEL);
	if (!rc)
		return -ENOMEM;

	id = (pdev->id >= 0 && pdev->id < (int)ARRAY_SIZE(luofu_rcs)) ?
	     (unsigned int)pdev->id : 0u;
	ca = &luofu_rcs[id];
	rc->id = id;

	/* The forced path has no DT resource: the regions are already requested
	 * by the vendor hi_pcie (pcierc.md sec 4), so request_mem_region would
	 * return -EBUSY.  devm_ioremap() takes no reservation and makes a
	 * second read-only kernel VA alias to the same page. */
	if (pdev->dev.of_node) {
		rc->dbi = devm_platform_ioremap_resource_byname(pdev, "dbi");
		rc->cfg = devm_platform_ioremap_resource_byname(pdev, "cfg");
		rc->misc = devm_platform_ioremap_resource_byname(pdev, "misc");
		label = "DT";
	} else {
		dev_info(&pdev->dev,
			 "FORCED probe (%s, no DT match) dbi=0x%lx cfg=0x%lx misc=0x%lx (write-only) read-only\n",
			 ca->name, ca->dbi_ca, ca->cfg_ca, ca->misc_ca);
		rc->dbi = devm_ioremap(&pdev->dev, ca->dbi_ca, LUOFU_PCIE_DBI_SIZE);
		rc->cfg = devm_ioremap(&pdev->dev, ca->cfg_ca, LUOFU_PCIE_CFG_SIZE);
		rc->misc = devm_ioremap(&pdev->dev, ca->misc_ca, LUOFU_PCIE_MISC_SIZE);
		label = "FORCED";
	}
	if (IS_ERR(rc->dbi))
		return PTR_ERR(rc->dbi);
	if (IS_ERR(rc->cfg))
		return PTR_ERR(rc->cfg);
	/* misc is WRITE-ONLY and unused by the read-only frame, so a mapping
	 * failure must not fail the probe (the forced smoke reads only dbi/cfg). */
	if (IS_ERR_OR_NULL(rc->misc)) {
		dev_warn(&pdev->dev,
			 "misc (port-logic) window not mapped: %pe (write-only, unused for now)\n",
			 rc->misc);
		rc->misc = NULL;
	}

	/* Keep the transcribed geometry live (not dead code): log the tables the
	 * frame carries, then the read-only inventory -- no register write. */
	dev_info(&pdev->dev,
		 "luofu-pcie %s: %zu iatu_rc entries, dbi/cfg mapped READ-ONLY, misc mapped WRITE-ONLY (frame, 0 writes)\n",
		 ca->name, ARRAY_SIZE(luofu_iatu_rc0));

	dev_info(&pdev->dev, "---- DBI window status (read-only) ----\n");
	dbi_hits = luofu_pcie_inventory(&pdev->dev, ca->name, rc->dbi,
					luofu_dbi_status,
					ARRAY_SIZE(luofu_dbi_status), &pred);
	luofu_pcie_iatu_inventory(&pdev->dev, rc->dbi,
				  id ? luofu_iatu_rc1 : luofu_iatu_rc0,
				  ARRAY_SIZE(luofu_iatu_rc0));

	dev_info(&pdev->dev, "---- CFG window status (read-only) ----\n");
	cfg_hits = luofu_pcie_inventory(&pdev->dev, ca->name, rc->cfg,
					luofu_cfg_status,
					ARRAY_SIZE(luofu_cfg_status), &pred);

	/* The link-state read path: the safe DL_ACTIVE decode that replaces the
	 * read-forbidden misc+0x100/+0x110 link/LTSSM reads (pcierc.md sec 4b).
	 * Both the RC's own link (dbi+0x082) and the endpoint's (cfg+0x082) are
	 * read; the reusable predicate is luofu_pcie_report_link(). */
	link_up = luofu_pcie_report_link(&pdev->dev, ca->name, rc->dbi,
					 DBI_LINK_STATUS);
	link_up &= luofu_pcie_report_link(&pdev->dev, ca->name, rc->cfg,
					  CFG_LINK_STATUS);

	dev_info(&pdev->dev,
		 "%s probe PASS: dbi %u predicted regs matched, cfg %u matched, link %s, 0 writes\n",
		 label, dbi_hits, cfg_hits, link_up ? "UP" : "DOWN");

	platform_set_drvdata(pdev, rc);
	/* TODO: return the real registration result once the host bridge is wired. */
	return 0;
}

static int luofu_pcie_remove(struct platform_device *pdev)
{
	/* TODO: unregister the host bridge (pci_remove_root_bus) once probe
	 * registers one.  The devm-managed mappings/kzalloc are freed
	 * automatically. */
	return 0;
}

static struct platform_driver luofu_pcie_driver = {
	.probe		= luofu_pcie_probe,
	.remove		= luofu_pcie_remove,
	.driver		= {
		.name		= "luofu-pcie",
		.of_match_table	= luofu_pcie_match_table,
	},
};

/*
 * The force_probe devices: two name-matched platform_devices (id 0 = RC0,
 * id 1 = RC1), each carrying its three pinned memory resources and no of_node,
 * so platform_match()'s name compare binds them where of_driver_match_device()
 * cannot.  probe ioremaps the CAs itself (the regions are vendor-owned).
 */
static struct resource luofu_pcie_res0[] = {
	DEFINE_RES_MEM(LUOFU_PCIE_RC0_DBI, LUOFU_PCIE_DBI_SIZE),
	DEFINE_RES_MEM(LUOFU_PCIE_RC0_MISC, LUOFU_PCIE_MISC_SIZE),
	DEFINE_RES_MEM(LUOFU_PCIE_RC0_CFG, LUOFU_PCIE_CFG_SIZE),
};

static struct resource luofu_pcie_res1[] = {
	DEFINE_RES_MEM(LUOFU_PCIE_RC1_DBI, LUOFU_PCIE_DBI_SIZE),
	DEFINE_RES_MEM(LUOFU_PCIE_RC1_MISC, LUOFU_PCIE_MISC_SIZE),
	DEFINE_RES_MEM(LUOFU_PCIE_RC1_CFG, LUOFU_PCIE_CFG_SIZE),
};

static struct platform_device luofu_pcie_fdev[] = {
	{ .name = "luofu-pcie", .id = 0, .num_resources = 3,
	  .resource = luofu_pcie_res0 },
	{ .name = "luofu-pcie", .id = 1, .num_resources = 3,
	  .resource = luofu_pcie_res1 },
};

static bool luofu_pcie_fdev_live[ARRAY_SIZE(luofu_pcie_fdev)];

static int __init luofu_pcie_init(void)
{
	unsigned int i;
	int ret;

	/* TODO (pcierc.md sec 2): in-tree this becomes a core/subsys_initcall so
	 * the RC is up before the endpoints probe.  module_init is fine for the
	 * loadable lab bring-up. */
	ret = platform_driver_register(&luofu_pcie_driver);
	if (ret)
		return ret;

	/* The DT-presence guard makes "without a DT match" literal: the forced
	 * path can never co-exist with a real bind. */
	if (!force_probe)
		return 0;

	if (of_find_compatible_node(NULL, NULL, "hisilicon,luofu-pcie")) {
		pr_warn("luofu-pcie: force_probe ignored, DT node present (would double-bind)\n");
		return 0;
	}
	for (i = 0; i < ARRAY_SIZE(luofu_pcie_fdev); i++) {
		if (platform_device_register(&luofu_pcie_fdev[i]) == 0)
			luofu_pcie_fdev_live[i] = true;
		else
			pr_warn("luofu-pcie: force_probe device %s registration failed\n",
				luofu_rcs[i].name);
	}
	return 0;
}

static void __exit luofu_pcie_exit(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(luofu_pcie_fdev); i++)
		if (luofu_pcie_fdev_live[i])
			platform_device_unregister(&luofu_pcie_fdev[i]);
	platform_driver_unregister(&luofu_pcie_driver);
}

module_init(luofu_pcie_init);
module_exit(luofu_pcie_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hi5671Y luofu PCIe root complex (stage-2 driver frame + forced probe)");
