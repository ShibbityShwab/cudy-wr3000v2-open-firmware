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
 * What the evolved driver carries NOW (pcidrv.md -> drvpcie.md):
 *   - module_init + platform_driver + of_match ("hisilicon,luofu-pcie") +
 *     probe/remove (pcierc.md sec 4b);
 *   - ioremap of DBI + config + the WRITE-ONLY port-logic (misc) window, from
 *     DT reg-names or the pinned CAs;
 *   - the DWC-style dword-aligned register accessors: luofu_pcie_read() (the
 *     field reader) + luofu_pcie_write() (the read-modify-write counterpart,
 *     staged behind the write path) - every MMIO op a 4-byte-aligned 32-bit
 *     access, compile-time-enforced by luofu_pcie_check_aligned();
 *   - the link-training/LTSSM read-state machine: the DWC PORT_LOGIC_DEBUG0/1
 *     words at DBI+0x728/0x72c (the RAW LTSSM state, decoded to a named state,
 *     plus link-up / link-in-training) - the read-SAFE substitute for the
 *     read-forbidden SoC misc+0x110;
 *   - the link-state read path (DL_ACTIVE decode at DBI/cfg+0x082) kept as the
 *     second, independent link-up predicate;
 *   - the host-bridge REGISTRATION outline + the reset/clock DEPENDENCY notes
 *     (the CRG handshake points) - a commented scaffold, not a working host;
 *   - the force_probe=1 DT-less bench path (the proven smoke path, kept).
 *
 * It COMPILES against the vanilla 5.10.201 arm headers in the CI cross-build
 * (.github/workflows/lab-module-build.yml -> lab/luofu-pcie, plus the
 * build-load-test-module.yml lane) and performs NO register writes: probe maps
 * the windows and runs a read-only status inventory (FORCED probe:
 * dbi 0x10160000 + cfg 0x50000000 + write-only misc 0x10161000 on RC0).
 *
 * =====================  THE RC WRITE PATH (rcwrite.md)  =====================
 * The default path above is unchanged and stays store-free.  On top of it sits
 * the bounded, compile-gated, forced-path-only RC WRITE PATH - the CRG half the
 * bring-up needs (the vendor order is clock gate ON -> reset deassert
 * apb->pcs->phy->ctrl, and a CRG reset bit is ACTIVE-LOW: 1 = out of reset):
 *
 *   rc_write_test=1  STAGE B - the safest first device step (rcwrite.md sec 3):
 *                    the no-op RMW census on the RC's OWN CRG words - the first
 *                    read of the reset word 0x34 and the gate word 0x20, each
 *                    written back UNCHANGED and read back (PASS iff
 *                    back == pre).  Idempotent on any latch, ZERO state change,
 *                    exactly 2 dword-aligned stores.  Admissible vendor-LIVE;
 *                    fails closed (0 stores) if the RC link already reads DOWN.
 *
 *   rc_write_flip=1  STAGE C - the meaningful write (rcwrite.md sec 3):
 *                    rank 1 SET(deassert) the RC0 reset bits 0x0c..0x0f (the
 *                    one-way, safe direction - a liveness check), then rank 2
 *                    the real flip: assert (CLEAR) apb_rst -> observe ->
 *                    deassert (SET) + read-back, PASS iff post == pre.  A
 *                    state-changing store on a LIVE RC wedges the DWC link, so
 *                    it is refused (0 stores) unless the RC is QUIESCED - the
 *                    RC's own link reads DOWN (endpoint disabled, link down).
 *
 * THE BOUND (MANDATORY): every store goes through luofu_rc_store(), the file's
 * ONLY writel() call site, which enforces the 4-byte alignment rule and REFUSES
 * any store past a fixed budget (2 + 3 = 5).  The sequences are finite by
 * construction: no input can make them a sweep.  Barred from every sequence:
 * CA 0x400392f0, the RC misc window 0x10161000 (never read), host-side IAR
 * 0x4016010c, and the PLL/mux/softrst/watchdog/reboot registers.
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

#include <linux/bitops.h>
#include <linux/build_bug.h>
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

/* ------------------------------------------------------------------ *
 * DWC port-logic DEBUG words (mainline pcie-designware.h): the DWC-    *
 * internal LTSSM state machine, readable through the SAME dword-       *
 * aligned DBI window as the iATU file.  These are the READ-SAFE raw    *
 * LTSSM state the SoC misc block (misc+0x110) mirrors read-forbidden   *
 * (pcierc.md sec 4b): the vendor polls misc+0x110, we poll DBI+0x728/  *
 * 0x72c instead - one 4-byte-aligned readl() each, never misc.         *
 * ------------------------------------------------------------------ */
#define DBI_PORT_DEBUG0		0x728u	/* LTSSM state in bits [4:0] */
#define DBI_PORT_DEBUG1		0x72cu	/* link-up bit 4, in-training bit 29 */
#define DWC_LTSSM_STATE_MASK	0x1fu
#define DWC_LTSSM_STATE_L0	0x11u
#define DWC_LINK_UP		(1u << 4)
#define DWC_LINK_IN_TRAINING	(1u << 29)

/*
 * The DWC LTSSM state encodings (Synopsys databook; the same 32 states the
 * mainline drivers decode).  PORT_LOGIC_DEBUG0 bits [4:0] hold this state;
 * L0 = 0x11.  This table turns the raw read into a NAMED state - the read-state
 * machine: the bring-up polls it (and DEBUG1's link-up / in-training bits)
 * instead of the forbidden misc+0x110.
 */
enum luofu_ltssm_state {
	LTSSM_DETECT_QUIET = 0x00,
	LTSSM_DETECT_ACT = 0x01,
	LTSSM_POLL_ACTIVE = 0x02,
	LTSSM_POLL_COMPLIANCE = 0x03,
	LTSSM_POLL_CONFIG = 0x04,
	LTSSM_PRE_DETECT_QUIET = 0x05,
	LTSSM_DETECT_WAIT = 0x06,
	LTSSM_CFG_LINKWD_START = 0x07,
	LTSSM_CFG_LINKWD_ACCEPT = 0x08,
	LTSSM_CFG_LANENUM_WAIT = 0x09,
	LTSSM_CFG_LANENUM_ACCEPT = 0x0a,
	LTSSM_CFG_COMPLETE = 0x0b,
	LTSSM_CFG_IDLE = 0x0c,
	LTSSM_RCVRY_LOCK = 0x0d,
	LTSSM_RCVRY_SPEED = 0x0e,
	LTSSM_RCVRY_RCVRCFG = 0x0f,
	LTSSM_RCVRY_IDLE = 0x10,
	LTSSM_L0 = 0x11,
	LTSSM_L0S = 0x12,
	LTSSM_L123_SEND_EIDLE = 0x13,
	LTSSM_L1_IDLE = 0x14,
	LTSSM_L2_IDLE = 0x15,
	LTSSM_L2_TRANSMIT_WAKE = 0x16,
	LTSSM_DISABLED_ENTRY = 0x17,
	LTSSM_DISABLED_IDLE = 0x18,
	LTSSM_DISABLED = 0x19,
	LTSSM_LPBK_ENTRY = 0x1a,
	LTSSM_LPBK_ACTIVE = 0x1b,
	LTSSM_LPBK_EXIT = 0x1c,
	LTSSM_LPBK_EXIT_TIMEOUT = 0x1d,
	LTSSM_HOT_RESET_ENTRY = 0x1e,
	LTSSM_HOT_RESET = 0x1f,
	LTSSM_NR_STATES = 0x20,
};

static const char * const luofu_ltssm_names[LTSSM_NR_STATES] = {
	[LTSSM_DETECT_QUIET]		= "DETECT.QUIET",
	[LTSSM_DETECT_ACT]		= "DETECT.ACT",
	[LTSSM_POLL_ACTIVE]		= "POLL.ACTIVE",
	[LTSSM_POLL_COMPLIANCE]		= "POLL.COMPLIANCE",
	[LTSSM_POLL_CONFIG]		= "POLL.CONFIG",
	[LTSSM_PRE_DETECT_QUIET]	= "PRE-DETECT.QUIET",
	[LTSSM_DETECT_WAIT]		= "DETECT.WAIT",
	[LTSSM_CFG_LINKWD_START]	= "CFG.LINKWD.START",
	[LTSSM_CFG_LINKWD_ACCEPT]	= "CFG.LINKWD.ACCEPT",
	[LTSSM_CFG_LANENUM_WAIT]	= "CFG.LANENUM.WAIT",
	[LTSSM_CFG_LANENUM_ACCEPT]	= "CFG.LANENUM.ACCEPT",
	[LTSSM_CFG_COMPLETE]		= "CFG.COMPLETE",
	[LTSSM_CFG_IDLE]		= "CFG.IDLE",
	[LTSSM_RCVRY_LOCK]		= "RECOVERY.LOCK",
	[LTSSM_RCVRY_SPEED]		= "RECOVERY.SPEED",
	[LTSSM_RCVRY_RCVRCFG]		= "RECOVERY.RCVR.CFG",
	[LTSSM_RCVRY_IDLE]		= "RECOVERY.IDLE",
	[LTSSM_L0]			= "L0",
	[LTSSM_L0S]			= "L0s",
	[LTSSM_L123_SEND_EIDLE]		= "L1/L2/L3.SEND.EIDLE",
	[LTSSM_L1_IDLE]			= "L1.IDLE",
	[LTSSM_L2_IDLE]			= "L2.IDLE",
	[LTSSM_L2_TRANSMIT_WAKE]	= "L2.TRANSMIT.WAKE",
	[LTSSM_DISABLED_ENTRY]		= "DISABLED.ENTRY",
	[LTSSM_DISABLED_IDLE]		= "DISABLED.IDLE",
	[LTSSM_DISABLED]		= "DISABLED",
	[LTSSM_LPBK_ENTRY]		= "LOOPBACK.ENTRY",
	[LTSSM_LPBK_ACTIVE]		= "LOOPBACK.ACTIVE",
	[LTSSM_LPBK_EXIT]		= "LOOPBACK.EXIT",
	[LTSSM_LPBK_EXIT_TIMEOUT]	= "LOOPBACK.EXIT.TIMEOUT",
	[LTSSM_HOT_RESET_ENTRY]		= "HOT.RESET.ENTRY",
	[LTSSM_HOT_RESET]		= "HOT.RESET",
};

/* misc (SoC port-logic/app) WRITE-ONLY offsets + the word values the staged
 * write path stores (pcierc.md sec 2).  The misc block is READ-FORBIDDEN
 * host-side, so every store is a full-word writel() with a host-side-computed
 * value - never a read-modify-write (which would read misc).  All offsets are
 * dword-aligned (checked by luofu_pcie_check_aligned()). */
#define MISC_RC_MODE		0x00u	/* writel(0x40000000) -> RC mode */
#define MISC_APP_CTRL		0x1cu	/* iATU-en bit 13, LTSSM-en bit 11 */
#define MISC_LINKDOWN_IRQ_MASK	0x28u	/* linkdown irq mask bit 12 */
#define MISC_LINKDOWN_IRQ_STAT	0x2cu	/* linkdown irq status/clear bit 12 */
#define MISC_MODE_RC		(4u << 28)	/* hi_pcie_set_mode */
#define MISC_APP_IATU_EN	(1u << 13)	/* 0x2000, hi_pcie_set_iatu */
#define MISC_APP_LTSSM_EN	(1u << 11)	/* 0x800, hi_pcie_enable_ltssm */
#define MISC_LINKDOWN_EN	(1u << 12)	/* 0x1000, hi_pcie_enable_linkdown_irq */

/* CRG handshake points (pcierc.md sec 2 + the stage-1 luofu-clk provider).
 * The RC must have its pcie_clk gate enabled AND its four resets deasserted - in
 * apb->pcs->phy->ctrl order, each with a delay - before ANY RC register write.
 * These are the consumer-side IDs the DT node declares (pinned DTS):
 *   clocks = <&crg LUOFU_CLK_PCIE0|1>           # gate reg 0x20, bit 0x0c|0x0d
 *   resets = <&crg 0x34 0x0c..0x0f|0x10..0x13> # apb, pcs, phy, ctrl
 * The stage-1 luofu-clk driver already transcribes LUOFU_CLK_PCIE0/1 and
 * #reset-cells=<2>; the staged bring-up consumes them (devm_clk_get /
 * devm_reset_control_get by name).  The RC WRITE PATH below is the ONE place
 * this module touches the CRG page itself: a bounded, forced-path-only
 * instrument over the reset word 0x34 and the gate word 0x20. */
#define LUOFU_CRG_BASE		0x14880000UL	/* the CRG page (pinned reg :205) */
#define LUOFU_CRG_SIZE		0x1000UL
#define LUOFU_RC_GATE_OFF	0x20u	/* peri gate-1: b0x0c pcie0_clk, b0x0d pcie1_clk */
#define LUOFU_RC_GATE_PCIE0	0x0cu
#define LUOFU_RC_GATE_PCIE1	0x0du
#define LUOFU_RST_PCIE_OFF	0x34u
#define LUOFU_RST_PCIE0_APB	0x0cu
#define LUOFU_RST_PCIE0_PCS	0x0du
#define LUOFU_RST_PCIE0_PHY	0x0eu
#define LUOFU_RST_PCIE0_CTRL	0x0fu
#define LUOFU_RST_PCIE1_APB	0x10u
#define LUOFU_RST_PCIE1_PCS	0x11u
#define LUOFU_RST_PCIE1_PHY	0x12u
#define LUOFU_RST_PCIE1_CTRL	0x13u

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

/* The DWC-style WRITE counterpart: the aligned dword read-modify-write.  Read
 * the 4-byte-aligned dword that contains the field, replace the field's byte
 * lanes, write the whole dword back with one writel() at the SAME aligned
 * address.  No sub-word, no non-aligned store - the only write primitive the
 * staged write path (pcierc.md sec 2) may use on DBI/cfg.  Staged: not called
 * while the module stays read-only, so __maybe_unused. */
static __maybe_unused void luofu_pcie_write(void __iomem *base, u16 off,
					    u8 width, u32 val)
{
	u32 word = readl(base + (off & ~3u));
	unsigned int shift = (off & 3u) * 8u;
	u32 mask = (width >= 4u) ? ~0u : ((1u << (width * 8u)) - 1u);

	word &= ~(mask << shift);
	word |= (val & mask) << shift;
	writel(word, base + (off & ~3u));
}

/*
 * THE LINK-TRAINING / LTSSM READ-STATE MACHINE (pcierc.md sec 4b evolved).
 * The DWC core exposes its own LTSSM state through the dword-aligned DBI
 * window: PORT_LOGIC_DEBUG0 (0x728) bits [4:0] = the raw LTSSM state, and
 * PORT_LOGIC_DEBUG1 (0x72c) bit 4 = link-up, bit 29 = link-in-training
 * (mainline pcie-designware.h).  This is the READ-SAFE substitute for the
 * read-forbidden SoC misc+0x110 LTSSM word the vendor polls: one 4-byte-aligned
 * readl() each, decoded to a named state, never touching misc.  The bring-up
 * polls this (a) to wait for L0 and (b) to name every intermediate state during
 * link training.
 */

/* Read the raw LTSSM state (PORT_LOGIC_DEBUG0 bits [4:0]) and name it. */
static u8 luofu_pcie_read_ltssm(void __iomem *dbi)
{
	return luofu_pcie_read(dbi, DBI_PORT_DEBUG0, W32) & DWC_LTSSM_STATE_MASK;
}

static const char *luofu_pcie_ltssm_name(u8 state)
{
	if (state >= LTSSM_NR_STATES || !luofu_ltssm_names[state])
		return "UNKNOWN";
	return luofu_ltssm_names[state];
}

/* The DWC link-up predicate (mainline dw_pcie_link_up shape): DEBUG1 link-up
 * bit set AND link-in-training bit clear.  Independent of the DL_ACTIVE read. */
static bool luofu_pcie_dwc_link_up(void __iomem *dbi)
{
	u32 v = luofu_pcie_read(dbi, DBI_PORT_DEBUG1, W32);

	return !!(v & DWC_LINK_UP) && !(v & DWC_LINK_IN_TRAINING);
}

/* Read + log the DWC LTSSM state machine for one RC domain. */
static void luofu_pcie_report_dwc_link(struct device *dev, const char *win,
				       void __iomem *dbi)
{
	u8 st = luofu_pcie_read_ltssm(dbi);
	u32 dbg1 = luofu_pcie_read(dbi, DBI_PORT_DEBUG1, W32);

	dev_info(dev,
		 "  %s DWC LTSSM [0x728] = 0x%02x (%s); DEBUG1 [0x72c] = 0x%08x: link %s%s\n",
		 win, st, luofu_pcie_ltssm_name(st), dbg1,
		 (dbg1 & DWC_LINK_UP) ? "UP" : "DOWN",
		 (dbg1 & DWC_LINK_IN_TRAINING) ? ", in-training" : "");
}

/*
 * THE ALIGNMENT RULE, compile-time enforced.  The DBI/cfg/misc windows answer
 * only 4-byte-aligned 32-bit accesses (header comment; pciskel-smoke.md).  The
 * sub-word header fields (0x082, ...) are reached ONLY through
 * luofu_pcie_read()/write(), which fetch the aligned containing dword; every
 * RAW readl()/writel() below targets a dword-aligned offset.  These
 * BUILD_BUG_ONs fail the build the moment an offset constant stops being
 * 4-byte aligned, so the CI cross-build is the alignment gate.
 */
static void luofu_pcie_check_aligned(void)
{
	/* iATU register file: a dword block at a 4-byte stride. */
	BUILD_BUG_ON(DBI_IATU_VIEWPORT & 3u);
	BUILD_BUG_ON(DBI_IATU_CTRL1 & 3u);
	BUILD_BUG_ON(DBI_IATU_CTRL2 & 3u);
	BUILD_BUG_ON(DBI_IATU_LOWER_BASE & 3u);
	BUILD_BUG_ON(DBI_IATU_UPPER_BASE & 3u);
	BUILD_BUG_ON(DBI_IATU_LIMIT & 3u);
	BUILD_BUG_ON(DBI_IATU_LOWER_TARGET & 3u);
	BUILD_BUG_ON(DBI_IATU_UPPER_TARGET & 3u);
	/* DWC port-logic DEBUG words (the LTSSM read-state machine). */
	BUILD_BUG_ON(DBI_PORT_DEBUG0 & 3u);
	BUILD_BUG_ON(DBI_PORT_DEBUG1 & 3u);
	/* the RC write path's CRG words + page (rcwrite.md sec 3). */
	BUILD_BUG_ON(LUOFU_CRG_BASE & 3u);
	BUILD_BUG_ON(LUOFU_CRG_SIZE & 3u);
	BUILD_BUG_ON(LUOFU_RC_GATE_OFF & 3u);
	BUILD_BUG_ON(LUOFU_RST_PCIE_OFF & 3u);
	/* misc write-only offsets (full-word stores, never read). */
	BUILD_BUG_ON(MISC_RC_MODE & 3u);
	BUILD_BUG_ON(MISC_APP_CTRL & 3u);
	BUILD_BUG_ON(MISC_LINKDOWN_IRQ_MASK & 3u);
	BUILD_BUG_ON(MISC_LINKDOWN_IRQ_STAT & 3u);
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
	void __iomem *crg;	/* the CRG page: the write-path instrument home */
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
 * The RC WRITE PATH knobs (rcwrite.md sec 3).  Both default 0, so a bare
 * insmod of this same artifact reaches no store, and both are reachable only
 * on the forced (no-DT) path.  Their preconditions differ by design: the no-op
 * census is the vendor-LIVE instrument (it refuses if the link is DOWN), while
 * the meaningful write is the QUIESCED one (it refuses if the link is UP).
 */
static int rc_write_test;
module_param(rc_write_test, int, 0444);
MODULE_PARM_DESC(rc_write_test,
	"STAGE B no-op RMW census of the RC's CRG reset word 0x34 + gate word 0x20 (write-same + read-back, 2 stores, 0 net change); forced path only; 0 = read-only");

static int rc_write_flip;
module_param(rc_write_flip, int, 0444);
MODULE_PARM_DESC(rc_write_flip,
	"STAGE C meaningful write: rank-1 SET(deassert) of the RC0 reset bits, then rank-2 assert/deassert of apb_rst 0x34 bit 0x0c; needs a QUIESCED RC (link down) and the write path compiled in; 0 = off");

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

/*
 * ===================== THE RC WRITE PATH (rcwrite.md) =====================
 * THE BOUND (rcwrite.md sec 3 / sec 5): every store goes through
 * luofu_rc_store(), which counts and REFUSES any store past a fixed budget.
 * The sequences are finite by construction - no input can make them a sweep.
 *
 * DELIBERATELY NOT IMPLEMENTED (rcwrite.md sec 5 bars): CA 0x400392f0; the RC
 * misc window 0x10161000/0x10165000 (never read); the host-side IAR 0x4016010c;
 * and the PLL (0x198/0x1e0), mux (0x138), softrst (0x84), watchdog
 * (0x50/0x64/0x70) and reboot (0x00) registers.  The only offsets reachable
 * from here are the CRG reset word 0x34 and the gate word 0x20, both words the
 * vendor's own hi_crg/hi_kreset/hi_clk RMW without a key (crgnext.md sec 0/2).
 *
 * THE RC RESET WORD IS ACTIVE-LOW (crgnext.md sec 1): hi_reset_assert CLEARs the
 * bit, hi_reset_deassert SETs it, so bit = 1 means OUT of reset.  The reset
 * CLASS was proven reversible in software by the crgstage2 smoke (the CLEAR of
 * 0x2c bit 0x18 TOOK, register restored exactly) - the Stage-A(ii) precondition
 * rcwrite.md sec 3 rank 2 requires before an RC-reset store may be read as a
 * meaningful write rather than an inert one.
 */
#define LUOFU_RC_TEST_WRITES	2u	/* the no-op census: one store per word */
#define LUOFU_RC_FLIP_WRITES	3u	/* rank 1 SET (1) + rank 2 assert/deassert (2) */
#define LUOFU_RC_WRITE_BUDGET	(LUOFU_RC_TEST_WRITES + LUOFU_RC_FLIP_WRITES)

/* The RC0 reset bits of the CRG reset word: apb/pcs/phy/ctrl (0x0c..0x0f) -
 * the deassert mask rank 1 SETs in one dword store. */
#define LUOFU_RC0_RST_MASK	((1u << LUOFU_RST_PCIE0_APB) | \
				 (1u << LUOFU_RST_PCIE0_PCS) | \
				 (1u << LUOFU_RST_PCIE0_PHY) | \
				 (1u << LUOFU_RST_PCIE0_CTRL))

/* The store counter that enforces THE BOUND.  It is reported by the frame's
 * final probe line, so a bare insmod prints "0 writes" and an instrument run
 * ends on the real, budget-limited count. */
static unsigned int luofu_rc_write_count;

/*
 * luofu_rc_store - the ONLY store primitive of this module.  It refuses a
 * non-4-byte-aligned offset (the readw.md external-abort class) and any store
 * past the fixed budget (THE BOUND), then does exactly one dword-aligned
 * writel() and returns the read-back.  Staged (__maybe_unused) because a build
 * with -DLUOFU_RC_WRITE=0 must drop the instruments and keep compiling.
 */
static __maybe_unused u32 luofu_rc_store(void __iomem *base, u16 off, u32 val)
{
	if (off & 3u) {
		pr_err("luofu-pcie: refusing unaligned CRG store at +0x%03x\n", off);
		return readl(base + (off & ~3u));
	}
	if (luofu_rc_write_count >= LUOFU_RC_WRITE_BUDGET) {
		pr_err("luofu-pcie: write budget %u exhausted, refusing store at +0x%03x\n",
		       LUOFU_RC_WRITE_BUDGET, off);
		return readl(base + off);
	}

	writel(val, base + off);
	luofu_rc_write_count++;

	return readl(base + off);	/* settle-time read-back, one clean dword */
}

/*
 * luofu_rc_rmw - a dword-aligned masked read-modify-write of one CRG word:
 * read the word, replace `mask`'s bits with `set_bits`, store the whole dword
 * through luofu_rc_store() (bound + alignment enforced), return the read-back.
 * The vendor's own hi_crg_enable/disable and hi_clk_gate_enable are exactly
 * this shape (crgnext.md sec 0); never a blind writel of a constant.
 */
static __maybe_unused u32 luofu_rc_rmw(void __iomem *base, u16 off, u32 mask,
				       u32 set_bits)
{
	u32 v = readl(base + off);

	v = (v & ~mask) | (set_bits & mask);
	return luofu_rc_store(base, off, v);
}

/*
 * luofu_rc_write_same - the no-op census store: read the word, write the SAME
 * dword back, read it back.  Idempotent on any latch (a plain R/W latch stores
 * the identical value; a write-1-SET-only latch sets bits already set and
 * leaves 0 bits 0) -> zero state change, exactly one store.  It proves the
 * store bus does not abort and yields the first-ever datum for the word.
 */
static __maybe_unused u32 luofu_rc_write_same(void __iomem *base, u16 off)
{
	return luofu_rc_store(base, off, readl(base + off));
}

/*
 * THE COMPILE GATE, in the LUOFU_CRG_FLIP pattern (wrdesign.md sec 6): the
 * store is a build-time property, never a runtime accident.  A build line can
 * always turn it off with -DLUOFU_RC_WRITE=0.  Arming is NOT reachability: the
 * stores still need force_probe=1 AND rc_write_test=1 (or rc_write_flip=1),
 * so a bare insmod of this same artifact performs ZERO stores.
 */
#ifndef LUOFU_RC_WRITE
#define LUOFU_RC_WRITE 1	/* armed; build with -DLUOFU_RC_WRITE=0 to drop it */
#endif

#if LUOFU_RC_WRITE

/*
 * STAGE B (rcwrite.md sec 3) - THE SAFEST FIRST DEVICE STEP: the no-op RMW
 * census on the RC's own CRG reset word 0x34 and gate word 0x20 (budget 2
 * stores + reads).  The reset word has never been read before: its value is a
 * datum in itself (a 0 bit would mean that RC is held in reset), and it becomes
 * the reference word for the ranked write.  Its pre-census gate word answers
 * the SET target for Stage C rank 3 without a store.
 *
 *   pre34 = readl(0x34); writel(pre34, 0x34); back34 = readl(0x34)
 *   pre20 = readl(0x20); writel(pre20, 0x20); back20 = readl(0x20)
 *   PASS iff no fault && back34 == pre34 && back20 == pre20
 *
 * Fail closed (0 stores) if the RC link already reads DOWN: this is the
 * vendor-LIVE instrument (rcwrite.md sec 2a), so an already-down link is the
 * unexpected state.
 */
static int luofu_pcie_rc_write_test(struct device *dev, void __iomem *crg,
				    void __iomem *dbi)
{
	struct luofu_pcie_link_state st =
		luofu_pcie_read_link_status(dbi, DBI_LINK_STATUS);
	u32 pre34, back34, pre20, back20;
	bool ok;

	if (!st.up) {
		dev_warn(dev, "RC_WRITE_TEST refused: the RC link reads DOWN (DL_ACTIVE=0) - the no-op census is the vendor-LIVE instrument; 0 stores\n");
		return -EBUSY;
	}

	pre34 = readl(crg + LUOFU_RST_PCIE_OFF);
	dev_info(dev, "RC_WRITE_TEST[1/2] off=0x%02x RC0 reset word pre=0x%08x (apb/pcs/phy/ctrl bits 0x%02x..0x%02x = %u%u%u%u) store 1/2 = write-same\n",
		 LUOFU_RST_PCIE_OFF, pre34,
		 LUOFU_RST_PCIE0_APB, LUOFU_RST_PCIE0_CTRL,
		 !!(pre34 & (1u << LUOFU_RST_PCIE0_APB)),
		 !!(pre34 & (1u << LUOFU_RST_PCIE0_PCS)),
		 !!(pre34 & (1u << LUOFU_RST_PCIE0_PHY)),
		 !!(pre34 & (1u << LUOFU_RST_PCIE0_CTRL)));
	back34 = luofu_rc_write_same(crg, LUOFU_RST_PCIE_OFF);
	dev_info(dev, "RC_WRITE_TEST[1/2] off=0x%02x back=0x%08x (%s)\n",
		 LUOFU_RST_PCIE_OFF, back34,
		 back34 == pre34 ? "no change" : "CHANGED");

	pre20 = readl(crg + LUOFU_RC_GATE_OFF);
	dev_info(dev, "RC_WRITE_TEST[2/2] off=0x%02x gate word pre=0x%08x (pcie0_clk b0x%02x=%u, pcie1_clk b0x%02x=%u) store 2/2 = write-same\n",
		 LUOFU_RC_GATE_OFF, pre20, LUOFU_RC_GATE_PCIE0,
		 !!(pre20 & (1u << LUOFU_RC_GATE_PCIE0)), LUOFU_RC_GATE_PCIE1,
		 !!(pre20 & (1u << LUOFU_RC_GATE_PCIE1)));
	back20 = luofu_rc_write_same(crg, LUOFU_RC_GATE_OFF);
	dev_info(dev, "RC_WRITE_TEST[2/2] off=0x%02x back=0x%08x (%s)\n",
		 LUOFU_RC_GATE_OFF, back20,
		 back20 == pre20 ? "no change" : "CHANGED");

	ok = (back34 == pre34) && (back20 == pre20);
	dev_info(dev, "RC_WRITE_TEST %s: pre34=0x%08x back34=0x%08x pre20=0x%08x back20=0x%08x, 2/2 no-op stores, %u stores total\n",
		 ok ? "PASS" : "FAIL", pre34, back34, pre20, back20,
		 luofu_rc_write_count);
	if (!ok)
		dev_err(dev, "RC_WRITE_TEST FAIL: a write-same store changed its word - the latch is not idempotent, do not proceed to a state-changing write\n");
	return ok ? 0 : -EIO;
}

/*
 * STAGE C (rcwrite.md sec 3) - THE MEANINGFUL WRITE: rank 1 SET(deassert) of
 * the RC0 reset bits (the one-way, safe direction: a no-op while they read 1,
 * so it is a liveness check) followed by rank 2, the real flip:
 *
 *   pre   = readl(0x34); refuse unless apb_rst (bit 0x0c) reads 1 (out of reset)
 *   set   = rmw(0x34, 0x0c..0x0f, live)     store 1/3 rank 1, the deassert
 *   obs   = readl(0x20)                    observe, logged only
 *   inv   = rmw(0x34, 0x0c, clear)         store 2/3 the ASSERT
 *   post  = rmw(0x34, 0x0c, set)           store 3/3 the DEASSERT (recovery)
 *   PASS iff post == pre
 *
 * QUIESCED PRECONDITION (rcwrite.md sec 2b + sec 3 Stage C): the only
 * state-changing direction on this word is a CLEAR (assert), and on a LIVE RC
 * that wedges the DWC link and drops the wiphy - the vendor's hi_clk/hi_kreset
 * refcounts cannot restore an out-of-band clear, so only the watchdog/reboot
 * recovers.  The write is therefore admitted ONLY once the endpoint is disabled
 * and the link is DOWN; under the live vendor stack the link reads UP and the
 * instrument refuses with ZERO stores.
 */
static int luofu_pcie_rc_write_flip(struct device *dev, void __iomem *crg,
				    void __iomem *dbi)
{
	struct luofu_pcie_link_state st =
		luofu_pcie_read_link_status(dbi, DBI_LINK_STATUS);
	u32 apb = 1u << LUOFU_RST_PCIE0_APB;
	u32 pre, rank1, inv, post;
	bool took;

	if (st.up) {
		dev_warn(dev, "RC_WRITE_FLIP refused: the RC link reads UP (DL_ACTIVE=1) - a state-changing store on the LIVE RC is not admissible (rcwrite.md sec 2b); the meaningful write needs the quiesced/takeover context; 0 stores\n");
		return -EBUSY;
	}

	pre = readl(crg + LUOFU_RST_PCIE_OFF);
	if (!(pre & apb)) {
		dev_warn(dev, "RC_WRITE_FLIP refused: apb_rst off=0x%02x bit=0x%02x reads 0 (pre=0x%08x) - the assert target must start deasserted (out of reset); 0 stores\n",
			 LUOFU_RST_PCIE_OFF, LUOFU_RST_PCIE0_APB, pre);
		return -EBUSY;
	}

	dev_info(dev, "RC_WRITE_FLIP rank1 off=0x%02x SET(deassert) RC0 bits 0x%02x..0x%02x: pre=0x%08x store 1/3\n",
		 LUOFU_RST_PCIE_OFF, LUOFU_RST_PCIE0_APB, LUOFU_RST_PCIE0_CTRL,
		 pre);
	rank1 = luofu_rc_rmw(crg, LUOFU_RST_PCIE_OFF, LUOFU_RC0_RST_MASK,
			     LUOFU_RC0_RST_MASK);
	dev_info(dev, "RC_WRITE_FLIP rank1 back=0x%08x (one-way: a no-op while the four bits read 1 - a liveness check, not a state change)\n",
		 rank1);

	/* OBSERVE: the gate word only - a licensed CRG read, and the Stage-C rank-3
	 * SET target.  Logged, NOT adjudicated (it is dynamic).  The gated blocks
	 * behind the RC are never read while a gate is cleared. */
	dev_info(dev, "RC_WRITE_FLIP observe [0x%02x]=0x%08x (dynamic - NOT flip evidence)\n",
		 LUOFU_RC_GATE_OFF, readl(crg + LUOFU_RC_GATE_OFF));

	dev_info(dev, "RC_WRITE_FLIP rank2 apb_rst off=0x%02x bit=0x%02x store 2/3 = the assert (clear)\n",
		 LUOFU_RST_PCIE_OFF, LUOFU_RST_PCIE0_APB);
	inv = luofu_rc_rmw(crg, LUOFU_RST_PCIE_OFF, apb, 0);
	took = !(inv & apb);
	dev_info(dev, "RC_WRITE_FLIP assert read-back=0x%08x apb_rst bit=%u (%s)\n",
		 inv, took ? 0u : 1u,
		 took ? "the CLEAR TOOK - the reset class honours the ASSERT direction"
		      : "the CLEAR was IGNORED - the reset class matches the gate class (write-1-set-only)");

	dev_info(dev, "RC_WRITE_FLIP rank2 apb_rst store 3/3 = the deassert (recovery, the vendor-proven direction)\n");
	post = luofu_rc_rmw(crg, LUOFU_RST_PCIE_OFF, apb, apb);

	dev_info(dev, "RC_WRITE_FLIP: pre=0x%08x -> rank1=0x%08x -> assert=0x%08x -> deassert=0x%08x: %s (%u stores total)\n",
		 pre, rank1, inv, post,
		 post == pre ? "PASS post == pre" : "FAIL post != pre",
		 luofu_rc_write_count);
	if (post != pre)
		dev_err(dev, "RC_WRITE_FLIP FAIL: the RC reset word is left at 0x%08x, not pre 0x%08x - recovery = re-deassert (SET) or the SoC watchdog/reset\n",
			post, pre);
	return post == pre ? 0 : -EIO;
}
#endif /* LUOFU_RC_WRITE */

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

	/* Compile-time alignment gate: fails the build if any raw-dword offset
	 * below stops being 4-byte aligned (the CI cross-build enforces it). */
	luofu_pcie_check_aligned();

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
			 "FORCED probe (%s, no DT match) dbi=0x%lx cfg=0x%lx misc=0x%lx (write-only) crg=0x%lx rc_write_test=%d rc_write_flip=%d\n",
			 ca->name, ca->dbi_ca, ca->cfg_ca, ca->misc_ca,
			 (unsigned long)LUOFU_CRG_BASE, rc_write_test, rc_write_flip);
		rc->dbi = devm_ioremap(&pdev->dev, ca->dbi_ca, LUOFU_PCIE_DBI_SIZE);
		rc->cfg = devm_ioremap(&pdev->dev, ca->cfg_ca, LUOFU_PCIE_CFG_SIZE);
		rc->misc = devm_ioremap(&pdev->dev, ca->misc_ca, LUOFU_PCIE_MISC_SIZE);
		/* The CRG page: the RC write path's instrument home (rcwrite.md).  The
		 * region is already owned by the vendor hi_crg/watchdog, so
		 * devm_ioremap() takes no reservation (crgbind.md); ioremap alone
		 * performs no bus access, so mapping it keeps the read-only default
		 * read-only.  A mapping failure is NOT fatal for the frame: the
		 * instrument refuses on a NULL mapping. */
		rc->crg = devm_ioremap(&pdev->dev, LUOFU_CRG_BASE, LUOFU_CRG_SIZE);
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
	if (IS_ERR_OR_NULL(rc->crg)) {
		dev_warn(&pdev->dev,
			 "CRG page not mapped: %pe (the RC write path will refuse)\n",
			 rc->crg);
		rc->crg = NULL;
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

	/* The DWC LTSSM read-state machine (DBI+0x728/0x72c) - the read-safe raw
	 * LTSSM state, which the SoC misc block mirrors read-forbidden (pcierc.md
	 * sec 4b).  This is the vendor's LTSSM read, relocated to the DWC DBI. */
	luofu_pcie_report_dwc_link(&pdev->dev, ca->name, rc->dbi);

	/* ---- THE RC WRITE PATH (rcwrite.md) ----
	 * Staged, bounded, forced-path-only, behind two 0-default knobs.  It runs on
	 * RC0 only: the two CRG words it touches carry BOTH domains' bits, so a
	 * second pass would only double the store count against the same bound.  A
	 * bare insmod, or any DT bind, reaches no store. */
	if ((rc_write_test || rc_write_flip) && !pdev->dev.of_node) {
		if (!rc->crg) {
			dev_warn(&pdev->dev, "the CRG page is not mapped - the RC write path refuses (0 stores)\n");
		} else if (id != 0) {
			dev_info(&pdev->dev, "luofu-pcie %s: the RC write path runs on RC0 only (the CRG words carry both domains' bits); skipped\n",
				 ca->name);
		} else if (rc_write_test && rc_write_flip) {
			dev_warn(&pdev->dev, "rc_write_test and rc_write_flip are mutually exclusive (the budget bounds one instrument): refusing both\n");
		} else if (rc_write_test) {
#if LUOFU_RC_WRITE
			int wrc = luofu_pcie_rc_write_test(&pdev->dev, rc->crg,
							   rc->dbi);

			dev_info(&pdev->dev, "RC_WRITE_TEST rc=%d\n", wrc);
#else
			dev_warn(&pdev->dev, "rc_write_test=1 ignored: the RC write path is not compiled in (LUOFU_RC_WRITE=0)\n");
#endif
		} else {
#if LUOFU_RC_WRITE
			int wrc = luofu_pcie_rc_write_flip(&pdev->dev, rc->crg,
							   rc->dbi);

			dev_info(&pdev->dev, "RC_WRITE_FLIP rc=%d\n", wrc);
#else
			dev_warn(&pdev->dev, "rc_write_flip=1 ignored: the RC write path is not compiled in (LUOFU_RC_WRITE=0)\n");
#endif
		}
		/* The CRG's last touch is a read (wrdesign.md sec 6): a read-only
		 * census of both words after the instrument, plus the store total. */
		if (rc->crg)
			dev_info(&pdev->dev, "RC_WRITE after: crg[0x%02x]=0x%08x crg[0x%02x]=0x%08x (%u stores total)\n",
				 LUOFU_RC_GATE_OFF,
				 readl(rc->crg + LUOFU_RC_GATE_OFF),
				 LUOFU_RST_PCIE_OFF,
				 readl(rc->crg + LUOFU_RST_PCIE_OFF),
				 luofu_rc_write_count);
	} else if (rc_write_test || rc_write_flip) {
		dev_warn(&pdev->dev, "rc_write_test/rc_write_flip ignored: the RC write path runs only on the forced (no-DT) probe\n");
	}

	dev_info(&pdev->dev,
		 "%s probe PASS: dbi %u predicted regs matched, cfg %u matched, link %s, %u writes\n",
		 label, dbi_hits, cfg_hits, link_up ? "UP" : "DOWN",
		 luofu_rc_write_count);

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

/* ------------------------------------------------------------------ *
 * HOST-BRIDGE REGISTRATION OUTLINE (staged - NOT compiled; the module *
 * stays read-only until the CRG/pinctrl providers are live and the    *
 * device action is serial + gate-checked).  This is the vendor         *
 * `hi_pcie_probe` 14-step order (pcierc.md sec 2) mapped onto the      *
 * modern pci_host_probe() API - a scaffold that names every call the   *
 * working host will make, with the CRG handshake points called out.    *
 * ------------------------------------------------------------------ */
/*
 * static int luofu_pcie_host_register(struct luofu_pcie *rc)
 * {
 *	struct pci_host_bridge *bridge;
 *	int ret, i;
 *
 *	// ---- 0. CRG handshake (DEPENDENCY notes, pcierc.md sec 2) ----
 *	// The stage-1 luofu-clk provider must already be live.  Before ANY RC
 *	// register write:
 *	//   rc->clk = devm_clk_get(dev, "pcie_clk");      // LUOFU_CLK_PCIE0|1
 *	//   ret = clk_prepare_enable(rc->clk);            // gate 0x20 bit 0x0c|0x0d
 *	//   rc->rst_apb  = devm_reset_control_get(dev, "apb_rst");  // 0x34 bit 0x0c|0x10
 *	//   rc->rst_pcs  = devm_reset_control_get(dev, "pcs_rst");  // 0x34 bit 0x0d|0x11
 *	//   rc->rst_phy  = devm_reset_control_get(dev, "phy_rst");  // 0x34 bit 0x0e|0x12
 *	//   rc->rst_ctrl = devm_reset_control_get(dev, "ctrl_rst"); // 0x34 bit 0x0f|0x13
 *	//   reset_control_deassert(rc->rst_apb);  udelay(50);
 *	//   reset_control_deassert(rc->rst_pcs);  udelay(50);
 *	//   reset_control_deassert(rc->rst_phy);  udelay(50);
 *	//   reset_control_deassert(rc->rst_ctrl); udelay(50);
 *	//   (apb -> pcs -> phy -> ctrl, in that order, each with a delay)
 *
 *	// ---- 1..3. RC mode + iATU (misc WRITES + DBI writes) ----
 *	//   writel(MISC_MODE_RC, rc->misc + MISC_RC_MODE);    // misc+0x00 = RC mode
 *	//   // misc+0x1c: iATU-en | LTSSM-en, full-word host-computed (no RMW - a
 *	//   // read-modify-write would READ the read-forbidden misc window):
 *	//   writel(MISC_APP_IATU_EN | MISC_APP_LTSSM_EN, rc->misc + MISC_APP_CTRL);
 *	//   for (i = 0; i < 3; i++) {               // the three iatu_rc entries
 *	//       writel(tab[i].viewport,   rc->dbi + DBI_IATU_VIEWPORT);
 *	//       writel(tab[i].ctrl1,      rc->dbi + DBI_IATU_CTRL1);
 *	//       writel(tab[i].ctrl2,      rc->dbi + DBI_IATU_CTRL2);
 *	//       writel(tab[i].base_lo,    rc->dbi + DBI_IATU_LOWER_BASE);
 *	//       writel(tab[i].base_hi,    rc->dbi + DBI_IATU_UPPER_BASE);
 *	//       writel(tab[i].limit,      rc->dbi + DBI_IATU_LIMIT);
 *	//       writel(tab[i].target_lo,  rc->dbi + DBI_IATU_LOWER_TARGET);
 *	//       writel(tab[i].target_hi,  rc->dbi + DBI_IATU_UPPER_TARGET);
 *	//       // DBI+0x900+0x200*i, all dword-aligned
 *	//   }
 *
 *	// ---- 4..6. endpoint power + command + ASPM + link enable ----
 *	//   luofu_pcie_gpio_power_on(dev);      // pcie-gpios out 0 -> delay -> out 1
 *	//   luofu_pcie_write(rc->dbi, DBI_COMMAND, W16, 0x7);         // DBI+0x04 = 7
 *	//   luofu_pcie_write(rc->dbi, DBI_LINK_CONTROL, W16, 0x3);    // ASPM L0s+L1
 *	//   writel(MISC_APP_IATU_EN | MISC_APP_LTSSM_EN, rc->misc + MISC_APP_CTRL);
 *
 *	// ---- 7. link training - the LTSSM READ-STATE MACHINE ----
 *	//   for (retries = 0; retries < LUOFU_PCIE_LINK_RETRIES; retries++) {
 *	//       u8 st = luofu_pcie_read_ltssm(rc->dbi);       // DBI+0x728 [4:0]
 *	//       if (luofu_pcie_dwc_link_up(rc->dbi))         // DBI+0x72c bits 4/29
 *	//           break;                                   // L0 reached
 *	//       dev_dbg(dev, "link training: LTSSM %s\n", luofu_pcie_ltssm_name(st));
 *	//       msleep(LUOFU_PCIE_LINK_POLL_MS);
 *	//   }
 *	//   // retrain if needed: DBI+0x80c |= 0x20000 (PORT_LOGIC_SPEED_CHANGE)
 *
 *	// ---- 8. linkdown irq (misc write + threaded irq) ----
 *	//   writel(~MISC_LINKDOWN_EN, rc->misc + MISC_LINKDOWN_IRQ_MASK); // &= ~0x1000
 *	//   devm_request_threaded_irq(dev, rc->linkdown_irq, NULL,
 *	//                             luofu_pcie_linkdown_irq, IRQF_ONESHOT, ...);
 *
 *	// ---- 9. the HOST-BRIDGE registration (pci_host_probe) ----
 *	//   bridge = devm_pci_alloc_host_bridge(dev, 0);
 *	//   if (!bridge)
 *	//       return -ENOMEM;
 *	//   bridge->ops = &luofu_pcie_ops;   // .read/.write = the dev-0 cfg-window
 *	//                                    // pci_ops (pcierc.md sec 1: cfg_base +
 *	//                                    // where, devfn 0 only)
 *	//   pci_add_resource(&bridge->windows, &rc->mem_res); // 0x40000000/0x58000000
 *	//   pci_add_resource(&bridge->windows, &rc->io_res);  // 0x48000000/0x60000000
 *	//   bridge->map_irq = luofu_pcie_map_irq;    // radm irq (SPI 59/63)
 *	//   bridge->swizzle_irq = pci_common_swizzle;
 *	//   ret = pci_host_probe(bridge);            // scan + assign + add devices
 *	//   // (the vendor's from-scratch shape instead calls
 *	//   //  pci_scan_root_bus_bridge + pci_bus_size_bridges +
 *	//   //  pci_bus_assign_resources + pcie_bus_configure_settings +
 *	//   //  pci_bus_add_devices - pci_host_probe() wraps the same path)
 *	//   return ret;
 * }
 */

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
MODULE_DESCRIPTION("Hi5671Y luofu PCIe root complex (DWC accessors + LTSSM read-state machine + host-bridge scaffold)");
