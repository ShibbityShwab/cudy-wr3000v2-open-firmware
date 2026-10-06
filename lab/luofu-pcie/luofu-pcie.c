// SPDX-License-Identifier: GPL-2.0
/*
 * luofu-pcie: stage-2 read-only skeleton for the Hi5671Y "luofu" PCIe root
 * complex (two DWC domains, DT compatible "hisilicon,luofu-pcie").
 *
 * ==========================  SKELETON  ==========================
 * A DESIGN SKELETON: it carries the shape (of_match_table + probe/remove + the
 * from-scratch host-controller plan) that the stage-2 bring-up fills in; the
 * clk/reset wiring, the misc mode/LTSSM writes and the pci_scan_root_bus_bridge
 * registration stay TODO.  It COMPILES against the vanilla 5.10.201 arm headers
 * in the CI cross-build (.github/workflows/lab-module-build.yml -> lab/luofu-pcie,
 * plus the build-load-test-module.yml lane) and performs NO register writes:
 * probe only maps the two READ-SAFE windows and runs a read-only status
 * inventory (FORCED probe: dbi 0x10160000 + cfg 0x50000000 on RC0).
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
 * `reg`/`reg-names`), and what this skeleton does with each:
 *
 *   reg-name   RC0 CA       size      this module
 *   dbi        0x10160000   0x1000    MAPPED + READ: the RC's own config header
 *                                     (vendor/device id), the DWC Link
 *                                     Control/Status words, the port-logic and
 *                                     the iATU register file
 *   misc       0x10161000   0x3000    NOT MAPPED, NEVER READ.  The SoC
 *                                     "port-logic/app" block is write-only
 *                                     host-side (pcierc.md sec 1 + sec 4):
 *                                     mode select misc+0x00, LTSSM enable
 *                                     misc+0x1c, linkdown irq misc+0x28/0x2c.
 *                                     A read-only devmem of it PANICKED the
 *                                     box (phase20/host-window.md C.4); the
 *                                     HARD RULE stands for host-side reads.
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
 * words (Link Control / Link Control 2 / Link Width-Speed Control).  The raw
 * LTSSM state therefore stays OUT of this skeleton by design - it lives only in
 * the read-forbidden misc window.
 *
 * HARD RULES honoured: never write CA 0x400392f0; never read the RC misc window
 * 0x10161000; never read the host-side GICC IAR 0x4016010c; no register write
 * of any kind from this module (readl() is the only MMIO op reachable).
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

/* Documented-only geometry for the windows this read-only skeleton never maps.
 * misc 0x10161000 is the read-forbidden "port-logic/app" block; mem/io are the
 * iATU viewport 1/2 downstream windows (pcierc.md sec 1).  These constants exist
 * so the DT/CC-port plan is transcribed in one place; nothing dereferences them. */
#define LUOFU_PCIE_RC0_MISC	0x10161000UL	/* WRITE-ONLY, NEVER READ */
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
#define DBI_LINK_CONTROL	0x07cu	/* hi_pcie_target_link_speed writes speed bits */
#define DBI_LINK_CONTROL2	0x080u	/* hi_pcie_enable_aspm writes |= 3 (L0s|L1) */
#define DBI_LINK_STATUS		0x082u	/* DL_ACTIVE bit 13 (the safe link-up read) */
#define DBI_LINK_WIDTH_SPEED	0x80cu	/* hi_pcie_check_link_status |= 0x20000 (retrain) */

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
	unsigned long cfg_ca;
	const char *name;
};

static const struct luofu_pcie_rc_ca luofu_rcs[] = {
	{ LUOFU_PCIE_RC0_DBI, LUOFU_PCIE_RC0_CFG, "rc0" },
	{ LUOFU_PCIE_RC1_DBI, LUOFU_PCIE_RC1_CFG, "rc1" },
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
	const char *name;
	u32 expect;
	u32 mask;
};

/* DBI status inventory (read-only).  Only pure-read status words live here; no
 * register with a write side effect is reachable through this table. */
static const struct luofu_pcie_reg luofu_dbi_status[] = {
	/* The RC's own type-1 header id: the vendor's `read_conf` exposes only the
	 * downstream devfn 0, so Linux never reads this and the record carries no
	 * proven value -> measurement only. */
	{ DBI_VENDOR_DEVICE_ID, "dbi+0x000  RC vendor/device id (type-1 header)", 0, 0 },
	/* hi_pcie_init_cmd_status_reg writes 7; the I/O-enable bit reads RO0 on this
	 * SoC (the endpoint's own cfg+0x004 readback is 0x6), so predict MEM|MASTER. */
	{ DBI_COMMAND,          "dbi+0x004  PCI_COMMAND (vendor writes 7)", 0x00000006u, 0x00000006u },
	{ DBI_LINK_CONTROL,     "dbi+0x07c  Link Control", 0, 0 },
	/* hi_pcie_enable_aspm writes |= 3 (L0s + L1). */
	{ DBI_LINK_CONTROL2,    "dbi+0x080  Link Control 2 (vendor |= 3 ASPM)", 0x00000003u, 0x00000003u },
	/* The safe link-up read that replaces the forbidden misc+0x100. */
	{ DBI_LINK_STATUS,      "dbi+0x082  Link Status (DL_ACTIVE bit 13)", LS_DL_ACTIVE, LS_DL_ACTIVE },
	/* Bit 17 is the SPEED_CHANGE trigger (write-1-to-initiate, self-clearing),
	 * so a 0 readback on a live link is normal -> measurement only. */
	{ DBI_LINK_WIDTH_SPEED, "dbi+0x80c  Link Width/Speed Ctl (bit17=trigger)", 0, 0 },
};

/* cfg status inventory (read-only): the downstream endpoint's dev-0 header.
 * Expected values from the live vendor evidence (epinit readback + boot dmesg
 * `pci 0000:00:00.0: [59e7:0005] type 00 class 0x028000`). */
static const struct luofu_pcie_reg luofu_cfg_status[] = {
	{ CFG_VENDOR_DEVICE_ID, "cfg+0x000  endpoint vendor/device id [59e7:0005]",
	  0x000559e7u, 0xffffffffu },
	{ CFG_COMMAND,          "cfg+0x004  endpoint PCI_COMMAND (MEM|MASTER)",
	  0x00000006u, 0x00000006u },
	{ CFG_CLASS_REVISION,   "cfg+0x008  endpoint class/revision (class 0x028000)",
	  0x02800000u, 0xffffff00u },
	{ CFG_SUBSYSTEM_ID,     "cfg+0x02c  endpoint subsystem vendor (19e5)",
	  0x000019e5u, 0x0000ffffu },
	{ CFG_LINK_STATUS,      "cfg+0x082  endpoint Link Status (DL_ACTIVE bit 13)",
	  LS_DL_ACTIVE, LS_DL_ACTIVE },
};

/* Per-instance state. */
struct luofu_pcie {
	void __iomem *dbi;
	void __iomem *cfg;
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

/* Decode a Link Status word (bits 3:0 speed, bit 4 training, bit 13 DL_ACTIVE). */
static void luofu_pcie_decode_link_status(struct device *dev, const char *win,
					  u32 v)
{
	unsigned int speed = v & LS_SPEED_MASK;

	dev_info(dev,
		 "  %s link: DL_ACTIVE=%u link-training=%u neg-speed=%u%s\n",
		 win, !!(v & LS_DL_ACTIVE), !!(v & LS_LINK_TRAINING), speed,
		 speed == 1 ? " (2.5GT/s)" : speed == 2 ? " (5GT/s)" : "");
}

/*
 * Read-only inventory of one register table.  readl() is the only MMIO op in
 * the whole module; the table holds offsets only, so a write cannot be
 * expressed here.  Returns the number of predicted words that matched.
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
		u32 v = readl(base + r->offset);

		if (!r->mask) {
			dev_info(dev, "  %s [0x%03x] %s = 0x%08x (measurement)\n",
				 win, r->offset, r->name, v);
			continue;
		}
		(*predicted)++;
		if ((v & r->mask) == r->expect)
			matched++;
		dev_info(dev, "  %s [0x%03x] %s = 0x%08x expect 0x%08x/0x%08x match=%s\n",
			 win, r->offset, r->name, v, r->expect, r->mask,
			 (v & r->mask) == r->expect ? "YES" : "NO");
		if (r->offset == DBI_LINK_STATUS || r->offset == CFG_LINK_STATUS)
			luofu_pcie_decode_link_status(dev, win, v);
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
		label = "DT";
	} else {
		dev_info(&pdev->dev,
			 "FORCED probe (%s, no DT match) dbi=0x%lx cfg=0x%lx read-only\n",
			 ca->name, ca->dbi_ca, ca->cfg_ca);
		rc->dbi = devm_ioremap(&pdev->dev, ca->dbi_ca, LUOFU_PCIE_DBI_SIZE);
		rc->cfg = devm_ioremap(&pdev->dev, ca->cfg_ca, LUOFU_PCIE_CFG_SIZE);
		label = "FORCED";
	}
	if (IS_ERR(rc->dbi))
		return PTR_ERR(rc->dbi);
	if (IS_ERR(rc->cfg))
		return PTR_ERR(rc->cfg);

	/* Keep the transcribed geometry live (not dead code): log the tables the
	 * skeleton carries, then the read-only inventory -- no register write. */
	dev_info(&pdev->dev,
		 "luofu-pcie %s: %zu iatu_rc entries, windows dbi/cfg mapped (skeleton, write-only misc 0x%lx NOT mapped)\n",
		 ca->name, ARRAY_SIZE(luofu_iatu_rc0), LUOFU_PCIE_RC0_MISC);

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

	dev_info(&pdev->dev,
		 "%s probe PASS: dbi %u predicted regs matched, cfg %u matched, 0 writes\n",
		 label, dbi_hits, cfg_hits);

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
 * id 1 = RC1), each carrying its two pinned memory resources and no of_node,
 * so platform_match()'s name compare binds them where of_driver_match_device()
 * cannot.  probe ioremaps the CAs itself (the regions are vendor-owned).
 */
static struct resource luofu_pcie_res0[] = {
	DEFINE_RES_MEM(LUOFU_PCIE_RC0_DBI, LUOFU_PCIE_DBI_SIZE),
	DEFINE_RES_MEM(LUOFU_PCIE_RC0_CFG, LUOFU_PCIE_CFG_SIZE),
};

static struct resource luofu_pcie_res1[] = {
	DEFINE_RES_MEM(LUOFU_PCIE_RC1_DBI, LUOFU_PCIE_DBI_SIZE),
	DEFINE_RES_MEM(LUOFU_PCIE_RC1_CFG, LUOFU_PCIE_CFG_SIZE),
};

static struct platform_device luofu_pcie_fdev[] = {
	{ .name = "luofu-pcie", .id = 0, .num_resources = 2,
	  .resource = luofu_pcie_res0 },
	{ .name = "luofu-pcie", .id = 1, .num_resources = 2,
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
MODULE_DESCRIPTION("Hi5671Y luofu PCIe root complex (stage-2 read-only skeleton + forced probe)");
