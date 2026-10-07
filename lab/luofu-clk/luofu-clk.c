// SPDX-License-Identifier: GPL-2.0
/*
 * luofu-clk: the Hi5671Y "luofu" CRG clock + reset controller driver
 * structure (DT compatible "hisilicon,luofu-crg").
 *
 * ================  STAGE-2 DRIVER STRUCTURE  ================
 * Evolved from the stage-1 design skeleton (which carried only the shape):
 * the pinned geometry is now the live register model (16 gates + 2 muxes +
 * 2 PLLs in structs, "THE CRG MODEL") and a real clk_ops set (gate/mux/PLL,
 * "THE DRIVER OPS") registers a DT onecell clock tree through the generic
 * clk-provider API.  The forced-probe smoke path and the bounded write
 * instruments (write_test/write_flip/rst_flip) are UNCHANGED below.
 *
 * ==========================  SKELETON  ==========================
 * A DESIGN SKELETON: it carries the shape (of_match_table + probe/remove +
 * the regmap plan) that the stage-1 bring-up fills in; the hisi_clk_* / reset
 * helpers and the real clock-data wiring stay TODO.  It COMPILES against the
 * vanilla 5.10.201 arm headers in the CI cross-build
 * (.github/workflows/lab-module-build.yml -> lab/luofu-clk, plus the
 * build-load-test-module.yml lane) and performs NO register writes: probe
 * only maps the page and logs the transcribed table sizes.
 * ================================================================
 *
 * Spec: build/tmp/inta-spec/clocks2.md sec 3.  The reference implementation is
 * drivers/clk/hisilicon/crg-hi3798cv200.c (one node = one clock+reset
 * controller, shared hisi_clock_data/hisi_crg_dev infra in clk.h/crg.h, reset
 * helper in reset.c).  We clone that file and swap the register tables, because
 * its per-SoC data is hardcoded and a new compatible is required (there is no
 * mainline "luofu"/"hsan"/Hi5671 entry under drivers/clk/hisilicon).
 *
 * The DT node it binds (opensource/docs/soc/luofu-r116.dts):
 *
 *   crg: clock-reset-controller@14880000 {
 *       compatible = "hisilicon,luofu-crg", "syscon", "simple-mfd";
 *       reg = <0x14880000 0x1000>;   // pinned clk@14880000 :201 (reg :205)
 *       #clock-cells = <1>;          // LUOFU_CLK_* index namespace
 *       #reset-cells = <2>;          // (reg-offset, bit), pinned reset0 :1395
 *   };
 *
 * STAGE-1 GOALS (clocks2.md sec 3):
 *   1. gate providers so every consumer `clocks = <&crg IDX>` resolves at probe
 *      time (SPI-NAND rootfs, 8250_dw console, gpio-dwapb, i2c-designware);
 *   2. a reset provider so every `resets = <&crg off bit>` resolves and
 *      deasserts at driver probe;
 *   3. preserve-bootloader-state: the gates are already enabled by U-Boot, so
 *      tag them CLK_IGNORE_UNUSED (or snapshot-and-restore) so
 *      clk_disable_unused() cannot kill the live console/NAND;
 *   4. no PLL/mux rate changes -- PLLs register read-only fixed-rate;
 *   5. timers need nothing here (TWD/SP804 run off the free fixed clocks).
 *
 * Regmap plan (clocks2.md sec 3): NO regmap for stage 1.  One
 * devm_platform_ioremap_resource(pdev, 0) maps the 0x1000 CRG page, shared by
 * the clock tables and the reset controller.  Keep "syscon" in the compatible
 * so later blocks (reboot/watchdog children) can
 * syscon_regmap_lookup_by_phandle() this page without a second mapping; only
 * switch to devm_regmap_init_mmio() if a child node actually needs a regmap.
 *
 * force_probe=1: the vendor kernel's live DT carries "hsan,clk", not this
 * driver's compatible, so probe never fires (smoke.md).  force_probe=1
 * registers a name-matched platform_device (no of_node) that binds through
 * platform_match()'s name compare only, and the probe maps the pinned CRG page
 * with devm_ioremap() and runs a READ-ONLY status inventory (crgbind.md).  It
 * is a no-op the day a luofu DT node exists.
 *
 * write_test=1 / write_flip=1 (wrspec.md / wrdesign.md / flip.md, "THE WRITE
 * PATH" below): the forced path can additionally exercise the CRG's WRITE side
 * as a bounded, self-terminating probe.  Stage 1 is a no-op flip (set -> clear
 * -> restore-pre) of every gate in luofu_gates[] with a read-back per step;
 * stage 2 (compile-gated behind LUOFU_CRG_FLIP) is the ONE ranked flip of
 * flip.md -- gate offset 0x14 bit 0x18 (i2c0_clk), launched in the CLEAR
 * direction (read -> clear -> observe -> set-back, PASS iff the read-back ==
 * pre), because that is the only direction whose undo (the SET) is a store this
 * latch class is measured to honour.  rst_flip=1 adds crgnext.md sec 3's
 * rank-1 STAGE-2b flip: the RESET class, offset 0x2c bit 0x18 (i2c0_rst), in
 * its OWN 2-store instrument (assert -> observe -> deassert, PASS iff
 * post == pre), which answers whether the reset class honours the CLEAR
 * direction at all -- the class the RC's own 0x34 resets belong to.  All three
 * knobs default 0, so a bare insmod stays read-only forever.
 */

#include <linux/bitops.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include <linux/types.h>

/* CRG MMIO page.  reg comes from the DT node; the constants document the
 * pinned address (pinned clk@14880000 reg :205, node :201). */
#define LUOFU_CRG_BASE		0x14880000UL
#define LUOFU_CRG_SIZE		0x1000UL

/* softrst values for the reset/reboot path (pinned crg@14880000 :479/:480). */
#define LUOFU_SOFTRST_VAL0	0x51162100u
#define LUOFU_SOFTRST_VAL1	0xaee9deffu

/*
 * #clock-cells=<1> index namespace -- the seed for <dt-bindings/clock/luofu.h>
 * (clocks2.md sec 2).  The first seven are the IDs the DTS consumers use
 * today; the rest are pinned-only gates that complete the table.  Keep the
 * values in sync with the DTS placeholder #defines (luofu-r116.dts) and with
 * the header once it lands.
 */
enum {
	LUOFU_CLK_SFC = 0,
	LUOFU_CLK_GPIO0,
	LUOFU_CLK_GPIO1,
	LUOFU_CLK_I2C0,
	LUOFU_CLK_PCIE0,
	LUOFU_CLK_PCIE1,
	LUOFU_CLK_GMAC0,
	LUOFU_CLK_LED_PWM,
	LUOFU_CLK_LSW_DP,
	LUOFU_CLK_LSW_PFE,
	LUOFU_CLK_MDIO0,
	LUOFU_CLK_GMAC1,
	LUOFU_CLK_GMAC2,
	LUOFU_CLK_GMAC3,
	LUOFU_CLK_GMAC4,
	LUOFU_CLK_PIE,
	LUOFU_CLK_NR_CLKS,
};

/*
 * Gate table transcribed from the pinned clk@14880000 children ((reg-offset,
 * bit), pinned line).  reg-offset 0x14 is intact in the pinned body; each
 * 0x20-group offset is reconstructed from the clk_gate@0020NN unit address
 * (the reg-offset property renders as " "), so treat those as PROVISIONAL
 * until bench-confirmed (clocks2.md sec 2 caveat).
 */
struct luofu_gate {
	unsigned int id;
	u16 offset;
	u8 bit;
	const char *name;
	const char *parent;	/* pinned `clocks = <phandle>` -> output name */
};

/*
 * parent = the fixed input clock each gate fans out from, transcribed from the
 * pinned `clocks = <phandle>` cell (luofu-r116-pinned.dts): <0x6>=apb_clk,
 * <0x7>=ahb_clk, <0x8>=gemacN_clk, <0x9>=lsw_dp_clk, <0xa>=lsw_pfe_clk.  The
 * <0x8>/<0x9>/<0xa> parents are FIXED clocks, not other gates -- the gate
 * output names differ (gemac_clk0 vs gemacN_clk; lsw_dp_clk0 vs lsw_dp_clk).
 */
static const struct luofu_gate luofu_gates[] = {
	{ LUOFU_CLK_SFC,     0x14, 0x00, "sfc_clk",      "apb_clk"     },	/* :236 */
	{ LUOFU_CLK_GPIO0,   0x14, 0x14, "gpio0_clk",    "apb_clk"     },	/* :206 */
	{ LUOFU_CLK_GPIO1,   0x14, 0x15, "gpio1_clk",    "apb_clk"     },	/* :216 */
	{ LUOFU_CLK_I2C0,    0x14, 0x18, "i2c0_clk",     "apb_clk"     },	/* :226 */
	{ LUOFU_CLK_LED_PWM, 0x14, 0x0e, "led_pwm",      "apb_clk"     },	/* :246 */
	{ LUOFU_CLK_PCIE0,   0x20, 0x0c, "pcie0_clk",    "apb_clk"     },	/* :286 */
	{ LUOFU_CLK_PCIE1,   0x20, 0x0d, "pcie1_clk",    "apb_clk"     },	/* :296 */
	{ LUOFU_CLK_GMAC0,   0x20, 0x13, "gemac_clk0",   "gemacN_clk"  },	/* :306 */
	{ LUOFU_CLK_GMAC1,   0x20, 0x14, "gemac_clk1",   "gemacN_clk"  },	/* :316 */
	{ LUOFU_CLK_GMAC2,   0x20, 0x15, "gemac_clk2",   "gemacN_clk"  },	/* :326 */
	{ LUOFU_CLK_GMAC3,   0x20, 0x16, "gemac_clk3",   "gemacN_clk"  },	/* :336 */
	{ LUOFU_CLK_GMAC4,   0x20, 0x17, "gemac_clk4",   "gemacN_clk"  },	/* :346 */
	{ LUOFU_CLK_MDIO0,   0x20, 0x03, "mdio_clk0",    "gemacN_clk"  },	/* :276 */
	{ LUOFU_CLK_LSW_DP,  0x20, 0x00, "lsw_dp_clk0",  "lsw_dp_clk"  },	/* :256 */
	{ LUOFU_CLK_LSW_PFE, 0x20, 0x01, "lsw_pfe_clk0", "lsw_pfe_clk" },	/* :266 */
	{ LUOFU_CLK_PIE,     0x20, 0x19, "pie_clk0",     "ahb_clk"     },	/* :356 */
};

/*
 * Muxes (pinned clk@14880000 children): (reg-offset, mask).
 */
struct luofu_mux {
	const char *name;
	u16 offset;
	u8 mask;
	const char *parents[2];	/* pinned `clocks = <ph0 ph1>` -> output names */
};

static const struct luofu_mux luofu_muxes[] = {
	/* cpu_mux: <0x4 0x5> = clk_pll1 "cpu-clk" + clk_pll2 "lsw-clk". */
	{ "cpu_mux",   0x138, 0x8, { "cpu-clk", "lsw-clk" } },	/* :390 */
	/* efuse_mux: <0xb 0x3> = efuse_ring_clk + osc. */
	{ "efuse_mux", 0x138, 0x2, { "efuse_ring_clk", "osc" } },	/* :401 */
};

/*
 * PLLs (pinned clk_pll* children): ctrl-offset, status-offset, status-bit.
 * Stage 1 registers these READ-ONLY (fixed-rate) -- never reprogram the CPU
 * PLL (clocks2.md sec 3).
 */
struct luofu_pll {
	const char *name;
	u16 ctrl_offset;
	u16 status_offset;
	u8 status_bit;
	const char *parent;	/* pinned `clocks = <0x3>` = osc */
};

static const struct luofu_pll luofu_plls[] = {
	{ "cpu-clk", 0x198, 0x90, 0x1e, "osc" },	/* clk_pll1@0198 :366 */
	{ "lsw-clk", 0x1e0, 0x90, 0x1b, "osc" },	/* clk_pll2@01e0 :378 */
};

/*
 * force_probe: run the probe body against the hardcoded pinned CRG view
 * (read-only) with no DT match.  The live vendor DT carries "hsan,clk"
 * (pinned:201), not this driver's "hisilicon,luofu-crg", so without the knob
 * the probe never fires (smoke.md).  force_probe=1 synthesizes a name-matched
 * platform_device so the REAL probe runs; it is a no-op the day a luofu DT
 * node exists.
 */
static int force_probe;
module_param(force_probe, int, 0444);
MODULE_PARM_DESC(force_probe,
	"run the probe body against the hardcoded CRG view (read-only)");

/*
 * The write-path knobs (wrspec.md sec 2).  Both default 0: a bare insmod has
 * no store to reach.  write_flip is only meaningful together with write_test=1
 * on the forced path, and only in a build that defines LUOFU_CRG_FLIP.
 */
static int write_test;
module_param(write_test, int, 0444);
MODULE_PARM_DESC(write_test,
	"stage-1 gate no-op write sequence (set->clear->restore-pre, 0 net change); 0 = read-only");

static int write_flip;
module_param(write_flip, int, 0444);
MODULE_PARM_DESC(write_flip,
	"with write_test=1 on the forced path, flip i2c0_clk (0x14 bit 0x18) by ONE slow bit: clear -> observe -> set-back, PASS iff post == pre; needs the flip compiled in (LUOFU_CRG_FLIP)");

/*
 * rst_flip (crgnext.md sec 3): the STAGE-2b flip -- the RESET class, offset 0x2c
 * bit 0x18 (i2c0_rst), the class the RC's own 0x34 resets belong to.  It is its
 * OWN instrument (budget 2 stores, no gate sweep), reachable with force_probe=1
 * rst_flip=1 ALONE, so the design's "exactly two stores" bound holds without the
 * stage-1 sweep.  Compiled with the rest of the write path (LUOFU_CRG_FLIP).
 */
static int rst_flip;
module_param(rst_flip, int, 0444);
MODULE_PARM_DESC(rst_flip,
	"the crgnext rank-1 reset-class flip: with force_probe=1, assert (clear) -> observe -> deassert (set-back) of i2c0_rst (0x2c bit 0x18), PASS iff post == pre; needs the flip compiled in (LUOFU_CRG_FLIP)");

/*
 * The store counter that enforces THE BOUND (see "THE WRITE PATH" below):
 * declared here because the read-only inventory's summary line reports it, so
 * a bare insmod prints "... 0 writes" and a write_test run ends on the real,
 * budget-limited count.  LUOFU_WRITE_BUDGET is beside luofu_crg_rmw().
 */
static unsigned int luofu_write_count;

/*
 * Read-only status inventory table: pinned CRG page 0x14880000 + offset.
 * lock_mask = bits that must read 1 for the line to be a clean PASS; 0 = log
 * only.  Only pure-read status words live here: every gate/mux/PLL/misc word
 * is deliberately excluded so no accidental write is reachable (crgbind.md
 * sec 4).
 */
struct luofu_crg_reg {
	u16 offset;
	const char *name;
	u32 lock_mask;
};

static const struct luofu_crg_reg luofu_crg_safe[] = {
	/* clk_pll1 status-offset :376 + status-bit :375; clk_pll2 :388/:387;
	 * the same word is hsan,rstinfo reg-offset :1406 (rstinfo-mask :1408,
	 * rstinfo-offset :1409).  bit30 = CPU PLL, bit27 = LSW PLL,
	 * bits 14..16 = reset reason. */
	{ 0x090, "CRG_STATUS",  0x48000000u },
	/* watchdog int-status-offset :1519 -- benign, log only. */
	{ 0x100, "WDT_ISTATUS", 0x00000000u },
};

/*
 * Per-instance clock wrappers: embed the generic clk_hw and point back at the
 * owning CRG instance and the transcribed table row (id).  This is the
 * standalone-module stand-in for the hisi_clock_data infra (crg.h), which a
 * loadable .ko cannot link: the hisilicon clk helpers are in-tree objects,
 * not EXPORT_SYMBOL_GPL.
 */
struct luofu_gate_clk {
	struct clk_hw hw;
	struct luofu_crg *crg;
	unsigned int id;
};

struct luofu_pll_clk {
	struct clk_hw hw;
	struct luofu_crg *crg;
	unsigned int id;
};

struct luofu_mux_clk {
	struct clk_hw hw;
	struct luofu_crg *crg;
	unsigned int id;
};

/* Per-instance state. */
struct luofu_crg {
	void __iomem *base;
	struct luofu_gate_clk *gates;
	struct luofu_pll_clk *plls;
	struct luofu_mux_clk *muxes;
	struct clk_hw_onecell_data *clk_data;	/* DT onecell provider payload */
	/* TODO: struct reset_controller_dev rcdev;       (reset.c) */
};

static const struct of_device_id luofu_crg_match_table[] = {
	{ .compatible = "hisilicon,luofu-crg" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, luofu_crg_match_table);

/*
 * Read-only status inventory: walk luofu_crg_safe[] and readl() each word.
 * The only MMIO op reachable through the forced view is readl(); the table
 * holds offsets only, so a write cannot be expressed here.
 */
static void luofu_crg_inventory(struct device *dev, void __iomem *base,
				bool forced)
{
	unsigned int i, ok = 0;

	for (i = 0; i < ARRAY_SIZE(luofu_crg_safe); i++) {
		const struct luofu_crg_reg *r = &luofu_crg_safe[i];
		u32 v = readl(base + r->offset);

		if (!r->lock_mask)
			dev_info(dev, "[0x%03x] %s = 0x%08x (benign, log only)\n",
				 r->offset, r->name, v);
		else
			dev_info(dev, "[0x%03x] %s = 0x%08x (PLL cpu-lock=%u lsw-lock=%u, rst_reason=%u)\n",
				 r->offset, r->name, v,
				 !!(v & 0x40000000u), !!(v & 0x08000000u),
				 (v & 0x1c000u) >> 14);
		if (r->lock_mask && (v & r->lock_mask) != r->lock_mask)
			dev_warn(dev, "[0x%03x] %s lock mask 0x%08x not fully set (a status-bit-polarity finding, not a probe failure)\n",
				 r->offset, r->name, r->lock_mask);
		ok++;
	}
	dev_info(dev, "%s probe PASS: %u/%zu status regs read, %u writes\n",
		 forced ? "FORCED" : "DT", ok, ARRAY_SIZE(luofu_crg_safe),
		 luofu_write_count);
}

/*
 * ======================= THE WRITE PATH (wrspec.md) =======================
 * THE BOUND (wrspec.md sec 7 / wrdesign.md sec 6): every store goes through
 * luofu_crg_rmw(), which counts and REFUSES any store past a fixed budget.
 * The sequence is finite by construction; no input can make it a sweep.
 *
 * DELIBERATELY NOT IMPLEMENTED: wrspec.md sec 1's unconditional
 * "softrst deassert" writel(0x19f, base + 0x084).  wrdesign.md sec 7 could
 * not find that constant (HS_CRG_SOFTRST_WAIT / clk_init:7) anywhere in the
 * repo or in any vendor module, and the LIVE read (crgprobe.md) already
 * showed the CRG page fully readable with ZERO writes -- the block is not
 * held in reset for this probe.  A wrong value there costs the boot, not a
 * register, so an unverified magic is left out until a verified instrument
 * exists.  The offsets reachable from here are gate/reset latches only
 * (0x14 / 0x20); no reboot (0x00), resume (0x38), watchdog (0x50/0x64/0x70),
 * softrst (0x084), mux (0x138) or PLL (0x198/0x1e0) register is ever touched.
 */
#define LUOFU_GATE_WRITES	(ARRAY_SIZE(luofu_gates) * 3u)
#define LUOFU_FLIP_WRITES	2u
#define LUOFU_WRITE_BUDGET	(LUOFU_GATE_WRITES + LUOFU_FLIP_WRITES)

/*
 * luofu_crg_rmw - the ONLY write primitive: a dword-aligned read-modify-write
 * of one bit of a CRG latch, mirroring the vendor (hi_crg_enable/disable,
 * crgbind.md sec 0).  A 32-bit access is mandatory: a sub-word or misaligned
 * access on this SoC external-aborts (readw.md), so an unaligned offset is
 * refused rather than executed.  Returns the value read back after the store
 * -- the caller's measurement, and the FAIL signal when the store did not
 * take.
 */
static u32 luofu_crg_rmw(void __iomem *base, u16 off, u8 bit, u8 set)
{
	u32 v;

	if (off & 3u) {		/* readw.md: misaligned -> external abort */
		pr_err("luofu-crg: refusing unaligned CRG RMW at +0x%03x\n", off);
		return readl(base + off);
	}
	if (luofu_write_count >= LUOFU_WRITE_BUDGET) {
		pr_err("luofu-crg: write budget %u exhausted, refusing RMW at +0x%03x\n",
		       LUOFU_WRITE_BUDGET, off);
		return readl(base + off);
	}

	v = readl(base + off);
	v = set ? (v | BIT(bit)) : (v & ~BIT(bit));
	writel(v, base + off);
	luofu_write_count++;

	return readl(base + off);	/* settle-time read-back, one dword */
}

/*
 * Stage 1 -- the no-op sequence.  pre is read WITHOUT a store and the sequence
 * is set -> clear -> restore-pre, so each register ends byte-identical (0 net
 * change) while the store bus and the gate latch are exercised.  A read-back
 * that differs from pre is a flip: reported per step and as WRITE_TEST FAIL.
 */
static unsigned int luofu_crg_write_test(struct device *dev, void __iomem *base)
{
	unsigned int i, flips = 0;

	for (i = 0; i < ARRAY_SIZE(luofu_gates); i++) {
		const struct luofu_gate *g = &luofu_gates[i];
		u32 pre = readl(base + g->offset);
		u32 set, clr, back;

		set  = luofu_crg_rmw(base, g->offset, g->bit, 1);
		clr  = luofu_crg_rmw(base, g->offset, g->bit, 0);
		back = luofu_crg_rmw(base, g->offset, g->bit,
				     !!(pre & BIT(g->bit)));

		if (back != pre)
			flips++;
		dev_info(dev, "[STEP %2u] %-12s off=0x%02x bit=0x%02x pre=0x%08x -> set=0x%08x -> clear=0x%08x -> back=0x%08x (%s)\n",
			 i, g->name, g->offset, g->bit, pre, set, clr, back,
			 back == pre ? "0 flips" : "FLIP");
	}

	dev_info(dev, "WRITE_TEST %s %zu/%zu gates no-op (%u flips), %u stores\n",
		 flips ? "FAIL" : "PASS", ARRAY_SIZE(luofu_gates) - flips,
		 ARRAY_SIZE(luofu_gates), flips, luofu_write_count);
	return flips;
}

/*
 * ================ STAGE 2 -- THE ARMED FLIP (flip.md) ================
 * THE COMPILE GATE.  The stage-2 store is a build-time property, never a
 * runtime accident: everything below sits inside `#if LUOFU_CRG_FLIP`, and a
 * build line can always turn it off with -DLUOFU_CRG_FLIP=0.  Task st_01a11447
 * ("arm the flip") sets the default to 1 so the CI lane
 * (lab-module-build -> luofu-clk-ko) ships the instrument.  Arming is NOT
 * reachability: the store still needs force_probe=1 AND write_test=1 AND
 * write_flip=1, so a bare insmod of this same artifact performs ZERO stores.
 *
 * THE TARGET (flip.md sec 3 rank 1, sec 4): 0x14 bit 0x18 `i2c0_clk`.  The
 * i2c0 controller at 0x10111000 has no client node in the pinned tree and no
 * `i2c` line in any boot log, so nothing behind the gate moves (no bus, no
 * periodic MMIO); it is also the one gate register whose reg-offset the pinned
 * DTS states explicitly (`reg-offset = <0x14>`, clk@14880000 :205) instead of
 * being reconstructed from the unit address.  The gate table entry already
 * exists (luofu_gates[] LUOFU_CLK_I2C0).
 *
 * THE DIRECTION (flip.md sec 1/2) -- the previous shape was WRONG in this
 * respect.  The crgwrite smoke measured this latch class write-1-SETABLE but
 * NOT write-0-CLEARABLE (0x14 bit 0x0e: the set took, the clear and its
 * write-0 "restore" were both ignored), so a SET of an already-0 bit is
 * one-way here and the old `set -> clear-back` could never come back.  This
 * instrument therefore launches the CLEAR of an already-set bit:
 *
 *   pre = readl(0x14)                       (no store)
 *   refuse with ZERO stores unless pre bit 0x18 is SET   (the one-way SET
 *                                                        direction is never
 *                                                        entered)
 *   flip = rmw(0x14, 0x18, clear)           store 1
 *   observe readl(0x90), readl(0x100)       (read-only status words only)
 *   post = rmw(0x14, 0x18, set)             store 2 -- the undo IS the
 *                                           direction this latch is measured
 *                                           to honour
 *   PASS iff post == pre
 *
 * Fails closed: if the clear does not take (the measured expectation) nothing
 * changed at all.  Budget LUOFU_FLIP_WRITES = 2; a third store is refused.
 */
#ifndef LUOFU_CRG_FLIP
#define LUOFU_CRG_FLIP 1	/* armed; build with -DLUOFU_CRG_FLIP=0 to drop it */
#endif

#if LUOFU_CRG_FLIP

#define LUOFU_FLIP_OFF	0x14u	/* clk@14880000 gate 0, pinned :205 */
#define LUOFU_FLIP_BIT	0x18	/* i2c0_clk, pinned :226 */
#define LUOFU_FLIP_NAME	"i2c0_clk"

static int luofu_crg_write_flip(struct device *dev, void __iomem *base)
{
	u32 pre = readl(base + LUOFU_FLIP_OFF);
	u32 flip, post;
	u32 obs[ARRAY_SIZE(luofu_crg_safe)];
	unsigned int i;
	bool took;

	if (!(pre & BIT(LUOFU_FLIP_BIT))) {
		dev_warn(dev, "WRITE_FLIP refused: %s off=0x%02x bit=0x%02x reads 0 (pre=0x%08x) - only the CLEAR of an already-set bit is a permitted flip on this latch class (a SET of a 0 bit is one-way), 0 stores\n",
			 LUOFU_FLIP_NAME, LUOFU_FLIP_OFF, LUOFU_FLIP_BIT, pre);
		return -EBUSY;
	}

	dev_info(dev, "WRITE_FLIP %s off=0x%02x bit=0x%02x pre=0x%08x store 1/2 = the clear\n",
		 LUOFU_FLIP_NAME, LUOFU_FLIP_OFF, LUOFU_FLIP_BIT, pre);
	flip = luofu_crg_rmw(base, LUOFU_FLIP_OFF, LUOFU_FLIP_BIT, 0);
	took = !(flip & BIT(LUOFU_FLIP_BIT));

	/* OBSERVE.  These are the only two words the pinned tree licenses as
	 * pure reads (CRG_STATUS :375/:376, WDT_ISTATUS :1519).  They are logged
	 * and NOT adjudicated: 0x90 bit 0x13 moves between boots with no store
	 * at all (crgprobe.md sec 3).  The gate's own block (0x10111000) is never
	 * read while cleared -- that is the external-abort class. */
	for (i = 0; i < ARRAY_SIZE(luofu_crg_safe); i++)
		obs[i] = readl(base + luofu_crg_safe[i].offset);
	dev_info(dev, "WRITE_FLIP observe [0x%03x]=0x%08x [0x%03x]=0x%08x (dynamic - NOT flip evidence)\n",
		 luofu_crg_safe[0].offset, obs[0], luofu_crg_safe[1].offset, obs[1]);
	dev_info(dev, "WRITE_FLIP clear read-back=0x%08x i2c0 bit=%u (%s)\n",
		 flip, took ? 0u : 1u,
		 took ? "the clear TOOK - a plain RMW in the clear direction"
		      : "the clear was IGNORED - write-1-set latch confirmed on a second gate");

	dev_info(dev, "WRITE_FLIP %s store 2/2 = the set-back\n", LUOFU_FLIP_NAME);
	post = luofu_crg_rmw(base, LUOFU_FLIP_OFF, LUOFU_FLIP_BIT, 1);

	dev_info(dev, "WRITE_FLIP %s: pre=0x%08x -> clear=0x%08x -> set-back=0x%08x: %s (%u stores total)\n",
		 LUOFU_FLIP_NAME, pre, flip, post,
		 post == pre ? "PASS post == pre" : "FAIL post != pre",
		 luofu_write_count);
	if (post != pre)
		dev_err(dev, "WRITE_FLIP FAIL: %s left at 0x%08x, not pre 0x%08x - the gate may be left CLEARED; recover = the SoC watchdog/reset\n",
			LUOFU_FLIP_NAME, post, pre);
	return post == pre ? 0 : -EIO;
}

/*
 * ===== STAGE 2b -- THE RESET-CLASS FLIP (crgnext.md sec 3, rank 1) =====
 * The gate class above is write-1-SET-only (the CLEAR was IGNORED on 0x14 b0x18
 * and on 16/16 bits, flip.md sec 1), so a clock gate is NOT flippable in
 * software; crgnext.md sec 3 takes the ranked alternative -- exercise the RESET
 * class on a zero-consumer block.  The reset class is the one the vendor drives
 * in BOTH directions (`hi_reset_assert` = CLEAR, `hi_reset_deassert` = SET,
 * crgnext.md sec 1) and the one the RC's dependency lives on
 * (`resets = <0xf 0x34 0xc..0xf>`, pinned :1226).
 *
 * TARGET: CRG offset 0x2c bit 0x18 (abs 0x1488002c) = i2c0_rst, the reset of the
 * i2c0@0x10111000 controller (pinned `resets = <0xf 0x2c 0x18>` :1021; 1 = out
 * of reset, the same polarity as the RC's 0x34 bits).  Blast radius: i2c0 has no
 * client node in the pinned tree and no `i2c` line in any boot log (crgnext.md
 * sec 3), so nothing behind it moves.
 *
 * UNLOCK KEY: NONE.  crgnext.md sec 2 scanned every vendor KO (movw/movt + raw
 * bytes) and found no key or lock behind the gate/reset store path -- the
 * vendor's hi_crg_enable/disable are a bare one-bit RMW through the DT-mapped
 * page -- so no key handling exists here (and none is needed).
 *
 * THE SEQUENCE (crgnext.md sec 3, verbatim -- TWO stores, no sweep):
 *
 *   pre = readl(0x2c);                          the FIRST read of a reset word
 *   refuse (0 stores) unless pre bit 0x18 == 1     (out of reset)
 *   inv = rmw(0x2c, 0x18, clear)                store 1/2 = ASSERT
 *   observe readl(0x90), readl(0x100)            logged, NOT adjudicated
 *   post = rmw(0x2c, 0x18, set)                 store 2/2 = DEASSERT, the
 *                                               restore (the vendor-proven dir)
 *   PASS iff post == pre
 *
 * Fails closed: a CLEAR that is ignored (the class matching the gate class)
 * changes nothing; a CLEAR that takes followed by a failed set-back leaves i2c0
 * held in reset -- harmless (no client, no i2c MMIO) and cleared by the next SoC
 * reset.  Both outcomes are answers: (i) inv == pre -> the CRG latch class is
 * SET-only and the RC bring-up must be gate/reset SET-only (crgnext.md sec 4.3);
 * (ii) inv bit 0x18 cleared -> the reset class honours BOTH directions, and the
 * RC's own 0x34 bits become an admissible target on a QUIESCED RC.  Budget: 2
 * stores (LUOFU_WRITE_BUDGET keeps the shared bound); a third store is refused.
 */
#define LUOFU_RSTFLIP_OFF	0x2cu	/* reset class word, pinned :1021 */
#define LUOFU_RSTFLIP_BIT	0x18	/* i2c0_rst */
#define LUOFU_RSTFLIP_NAME	"i2c0_rst"

static int luofu_crg_write_rstflip(struct device *dev, void __iomem *base)
{
	u32 pre = readl(base + LUOFU_RSTFLIP_OFF);
	u32 inv, post;
	u32 obs[ARRAY_SIZE(luofu_crg_safe)];
	unsigned int i;
	bool took;

	if (!(pre & BIT(LUOFU_RSTFLIP_BIT))) {
		dev_warn(dev, "WRITE_RSTFLIP refused: %s off=0x%02x bit=0x%02x reads 0 (pre=0x%08x) - i2c0 already reads IN reset, so rank 1 has no out-of-reset bit to assert; 0 stores (crgnext.md sec 3 rank-1 precondition; the read-only reset inventory answers instead)\n",
			 LUOFU_RSTFLIP_NAME, LUOFU_RSTFLIP_OFF, LUOFU_RSTFLIP_BIT,
			 pre);
		return -EBUSY;
	}

	dev_info(dev, "WRITE_RSTFLIP %s off=0x%02x bit=0x%02x pre=0x%08x store 1/2 = the assert (clear)\n",
		 LUOFU_RSTFLIP_NAME, LUOFU_RSTFLIP_OFF, LUOFU_RSTFLIP_BIT, pre);
	inv = luofu_crg_rmw(base, LUOFU_RSTFLIP_OFF, LUOFU_RSTFLIP_BIT, 0);
	took = !(inv & BIT(LUOFU_RSTFLIP_BIT));

	/* OBSERVE.  These are the only two words the pinned tree licenses as
	 * pure reads (CRG_STATUS :375/:376, WDT_ISTATUS :1519).  Logged and NOT
	 * adjudicated: both are dynamic.  The i2c0 block (0x10111000) is never
	 * read, cleared or not -- that is the external-abort class. */
	for (i = 0; i < ARRAY_SIZE(luofu_crg_safe); i++)
		obs[i] = readl(base + luofu_crg_safe[i].offset);
	dev_info(dev, "WRITE_RSTFLIP observe [0x%03x]=0x%08x [0x%03x]=0x%08x (dynamic - NOT flip evidence)\n",
		 luofu_crg_safe[0].offset, obs[0], luofu_crg_safe[1].offset, obs[1]);
	dev_info(dev, "WRITE_RSTFLIP assert read-back=0x%08x i2c0_rst bit=%u (%s)\n",
		 inv, took ? 0u : 1u,
		 took ? "the CLEAR TOOK - the reset class honours the ASSERT direction"
		      : "the CLEAR was IGNORED - the reset class matches the gate class (write-1-set-only)");

	dev_info(dev, "WRITE_RSTFLIP %s store 2/2 = the deassert (set-back, the vendor-proven direction)\n",
		 LUOFU_RSTFLIP_NAME);
	post = luofu_crg_rmw(base, LUOFU_RSTFLIP_OFF, LUOFU_RSTFLIP_BIT, 1);

	dev_info(dev, "WRITE_RSTFLIP %s: pre=0x%08x -> assert=0x%08x -> deassert=0x%08x: %s (%u stores total)\n",
		 LUOFU_RSTFLIP_NAME, pre, inv, post,
		 post == pre ? "PASS post == pre" : "FAIL post != pre",
		 luofu_write_count);
	if (post != pre)
		dev_err(dev, "WRITE_RSTFLIP FAIL: %s left at 0x%08x, not pre 0x%08x - i2c0 may be held in reset (harmless: no client node, no i2c MMIO); recover = the next SoC reset\n",
			LUOFU_RSTFLIP_NAME, post, pre);
	return post == pre ? 0 : -EIO;
}
#endif /* LUOFU_CRG_FLIP */

/* ======================================================================
 * THE DRIVER OPS (stage-2 structure; clocks2.md sec 3 goals 1/2/5)
 * ======================================================================
 * A loadable .ko cannot call the hisilicon helpers (hisi_clk_register_gate /
 * hisi_mux_clock live in drivers/clk/hisilicon, in-tree objects that are not
 * EXPORT_SYMBOL_GPL), so the CRG geometry is wired through the generic
 * clk-provider API with OUR OWN clk_ops.  Every store stays on the proven
 * write discipline: dword-aligned one-bit RMW through luofu_crg_rmw() only
 * (read -> modify -> writel -> read-back); the out-of-bounds write classes
 * (mux 0x138, PLL ctrl 0x198/0x1e0, softrst/wdog/reboot) are refused or simply
 * not expressed.  Every read is a 4-byte-aligned readl() on the CRG page,
 * which crgprobe.md proved fully readable with ZERO writes.
 */

#define to_luofu_gate(_hw) container_of(_hw, struct luofu_gate_clk, hw)
#define to_luofu_pll(_hw)  container_of(_hw, struct luofu_pll_clk, hw)
#define to_luofu_mux(_hw)  container_of(_hw, struct luofu_mux_clk, hw)

/*
 * Gate ops: enable/disable are the proven RMW discipline (set/clear ONE bit of
 * a dword, never a blind writel, always the read-back).  is_enabled is a pure
 * readl -- it must not consume the write budget.
 */
static int luofu_gate_enable(struct clk_hw *hw)
{
	struct luofu_gate_clk *g = to_luofu_gate(hw);
	const struct luofu_gate *d = &luofu_gates[g->id];

	luofu_crg_rmw(g->crg->base, d->offset, d->bit, 1);
	return 0;
}

static void luofu_gate_disable(struct clk_hw *hw)
{
	struct luofu_gate_clk *g = to_luofu_gate(hw);
	const struct luofu_gate *d = &luofu_gates[g->id];

	luofu_crg_rmw(g->crg->base, d->offset, d->bit, 0);
}

static int luofu_gate_is_enabled(struct clk_hw *hw)
{
	struct luofu_gate_clk *g = to_luofu_gate(hw);
	const struct luofu_gate *d = &luofu_gates[g->id];

	return !!(readl(g->crg->base + d->offset) & BIT(d->bit));
}

static const struct clk_ops luofu_gate_ops = {
	.enable		= luofu_gate_enable,
	.disable	= luofu_gate_disable,
	.is_enabled	= luofu_gate_is_enabled,
};

/*
 * PLL ops -- READ-ONLY (clocks2.md sec 3 goal 5: never reprogram a PLL).
 * recalc_rate returns the ref as a documented passthrough: the boot-configured
 * CPU/LSW multiplier is not yet transcribed, and the ctrl words (0x198/0x1e0)
 * are out of bounds for this stage, so no rate field is read.  is_enabled
 * reads ONLY the pinned status word 0x90 (CRG_STATUS, a licensed pure read) at
 * the transcribed status_bit.
 */
static unsigned long luofu_pll_recalc_rate(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	return parent_rate;
}

static int luofu_pll_is_enabled(struct clk_hw *hw)
{
	struct luofu_pll_clk *p = to_luofu_pll(hw);
	const struct luofu_pll *d = &luofu_plls[p->id];

	return !!(readl(p->crg->base + d->status_offset) & BIT(d->status_bit));
}

static const struct clk_ops luofu_pll_ops = {
	.recalc_rate	= luofu_pll_recalc_rate,
	.is_enabled	= luofu_pll_is_enabled,
};

/*
 * Mux ops: get_parent is a pure read of 0x138 (the page is readable,
 * crgprobe.md); set_parent REFUSES -- 0x138 is out of bounds for writes
 * (wrdesign.md sec 1), because a store there switches the CPU PLL source.
 * Stage 1 never programs a mux.
 */
static u8 luofu_mux_get_parent(struct clk_hw *hw)
{
	struct luofu_mux_clk *m = to_luofu_mux(hw);
	const struct luofu_mux *d = &luofu_muxes[m->id];
	u32 v;

	v = readl(m->crg->base + d->offset);
	return (v & d->mask) >> __ffs(d->mask);
}

static int luofu_mux_set_parent(struct clk_hw *hw, u8 index)
{
	return -EPERM;	/* mux writes are out of bounds (wrdesign.md sec 1) */
}

static const struct clk_ops luofu_mux_ops = {
	.get_parent	= luofu_mux_get_parent,
	.set_parent	= luofu_mux_set_parent,
};

/*
 * luofu_register_clks: build the CRG clock tree on the DT path only.
 *
 * Order matters: PLLs register first (they are the muxes' parents), then the
 * muxes, then the gates; each gate's hw is stored into a onecell provider so a
 * DT `clocks = <&crg LUOFU_CLK_*>` resolves by index.  Every clock is tagged
 * CLK_IGNORE_UNUSED so clk_disable_unused() cannot kill a U-Boot-enabled gate
 * (clocks2.md sec 3 goal 3).  The forced (no-DT) path never reaches here: it
 * has no of_node, so nothing registers and the smoke path stays read-only
 * until its 0-default knobs turn.
 */
static int luofu_register_clks(struct luofu_crg *crg,
			       struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	unsigned int i;
	int ret;

	crg->plls = devm_kcalloc(dev, ARRAY_SIZE(luofu_plls),
				 sizeof(*crg->plls), GFP_KERNEL);
	crg->muxes = devm_kcalloc(dev, ARRAY_SIZE(luofu_muxes),
				  sizeof(*crg->muxes), GFP_KERNEL);
	crg->gates = devm_kcalloc(dev, ARRAY_SIZE(luofu_gates),
				  sizeof(*crg->gates), GFP_KERNEL);
	crg->clk_data = devm_kzalloc(dev, sizeof(*crg->clk_data) +
				     sizeof(struct clk_hw *) * LUOFU_CLK_NR_CLKS,
				     GFP_KERNEL);
	if (!crg->plls || !crg->muxes || !crg->gates || !crg->clk_data)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(luofu_plls); i++) {
		struct luofu_pll_clk *p = &crg->plls[i];
		struct clk_init_data init = {
			.name		= luofu_plls[i].name,
			.ops		= &luofu_pll_ops,
			.parent_names	= &luofu_plls[i].parent,
			.num_parents	= 1,
			.flags		= CLK_IGNORE_UNUSED,
		};

		p->crg = crg;
		p->id = i;
		p->hw.init = &init;
		ret = devm_clk_hw_register(dev, &p->hw);
		if (ret) {
			dev_err(dev, "luofu-crg: pll %s register failed: %d\n",
				luofu_plls[i].name, ret);
			return ret;
		}
	}

	for (i = 0; i < ARRAY_SIZE(luofu_muxes); i++) {
		struct luofu_mux_clk *m = &crg->muxes[i];
		struct clk_init_data init = {
			.name		= luofu_muxes[i].name,
			.ops		= &luofu_mux_ops,
			.parent_names	= luofu_muxes[i].parents,
			.num_parents	= ARRAY_SIZE(luofu_muxes[i].parents),
			.flags		= CLK_IGNORE_UNUSED,
		};

		m->crg = crg;
		m->id = i;
		m->hw.init = &init;
		ret = devm_clk_hw_register(dev, &m->hw);
		if (ret) {
			dev_err(dev, "luofu-crg: mux %s register failed: %d\n",
				luofu_muxes[i].name, ret);
			return ret;
		}
	}

	crg->clk_data->num = LUOFU_CLK_NR_CLKS;
	for (i = 0; i < ARRAY_SIZE(luofu_gates); i++) {
		struct luofu_gate_clk *g = &crg->gates[i];
		const struct luofu_gate *d = &luofu_gates[i];
		struct clk_init_data init = {
			.name		= d->name,
			.ops		= &luofu_gate_ops,
			.parent_names	= &d->parent,
			.num_parents	= 1,
			.flags		= CLK_IGNORE_UNUSED,
		};

		g->crg = crg;
		g->id = i;
		g->hw.init = &init;
		ret = devm_clk_hw_register(dev, &g->hw);
		if (ret) {
			dev_err(dev, "luofu-crg: gate %s register failed: %d\n",
				d->name, ret);
			return ret;
		}
		crg->clk_data->hws[d->id] = &g->hw;
	}

	return of_clk_add_hw_provider(dev->of_node, of_clk_hw_onecell_get,
				      crg->clk_data);
}

static int luofu_crg_probe(struct platform_device *pdev)
{
	struct luofu_crg *crg;

	crg = devm_kzalloc(&pdev->dev, sizeof(*crg), GFP_KERNEL);
	if (!crg)
		return -ENOMEM;

	/* Regmap plan step 1: one shared mapping of the 0x1000 CRG page,
	 * reused by the clock tables and the reset controller.
	 *
	 * The forced path has no DT resource: the region is already requested
	 * by the vendor hi_crg / watchdog drivers, so request_mem_region would
	 * return -EBUSY.  devm_ioremap() takes no reservation and makes a
	 * second read-only kernel VA alias to the same page (crgbind.md). */
	if (pdev->dev.of_node) {
		crg->base = devm_platform_ioremap_resource(pdev, 0);
	} else {
		dev_info(&pdev->dev,
			 "FORCED probe (no DT match) base=0x%lx size=0x%lx write_test=%d write_flip=%d rst_flip=%d%s\n",
			 (unsigned long)LUOFU_CRG_BASE,
			 (unsigned long)LUOFU_CRG_SIZE,
			 write_test, write_flip, rst_flip,
			 (write_test || rst_flip) ? "" : " read-only");
		crg->base = devm_ioremap(&pdev->dev, LUOFU_CRG_BASE,
					 LUOFU_CRG_SIZE);
	}
	if (IS_ERR(crg->base))
		return PTR_ERR(crg->base);

	/* Register the CRG clock tree (gates + muxes + PLLs) on the DT path.
	 * The forced path has no of_node and skips this: no DT clock consumer
	 * can resolve a synthetic device, and the smoke path must stay write-free
	 * until its 0-default knobs turn. */
	if (pdev->dev.of_node) {
		int ret = luofu_register_clks(crg, pdev);

		if (ret) {
			dev_err(&pdev->dev,
				"luofu-crg: clock registration failed: %d\n", ret);
			return ret;
		}
	}

	/* TODO: register the reset controller -- hisi_reset_init(pdev) (reset.c)
	 *       on the same MMIO page; #reset-cells=<2> => args[0]=reg-offset,
	 *       args[1]=bit.  Reproduce LUOFU_SOFTRST_VAL0/1 in the reboot path. */

	/* Prove the tables are wired: the sizes, plus whether the real tree
	 * registered (DT) or the forced smoke probe is running (skeleton). */
	dev_info(&pdev->dev, "luofu-crg: %zu gates, %zu muxes, %zu plls%s\n",
		 ARRAY_SIZE(luofu_gates), ARRAY_SIZE(luofu_muxes),
		 ARRAY_SIZE(luofu_plls),
		 pdev->dev.of_node ? " (CRG clock tree registered)" : " (skeleton)");

	/* Read-only status inventory, both paths: walks luofu_crg_safe[] with
	 * readl() only -- the forced path's evidence and the DT path's first
	 * hardware access.  No clock/mux/PLL/reset registration runs here. */
	luofu_crg_inventory(&pdev->dev, crg->base, !pdev->dev.of_node);

	/* THE WRITE PATH (wrspec.md): stage 1 (write_test) and, in a build with
	 * -DLUOFU_CRG_FLIP, the stage-2 flip.  Both run ONLY on the forced
	 * (no-DT) path and ONLY behind their 0-default knobs, so an in-tree
	 * bind -- or a bare insmod -- stays read-only.  The status inventory is
	 * re-run LAST: the CRG's first and last touch is a read, and the lock
	 * mask must still hold after the stores. */
	if (write_test && !pdev->dev.of_node) {
		luofu_crg_write_test(&pdev->dev, crg->base);
#if LUOFU_CRG_FLIP
		if (write_flip) {
			int rc;

			rc = luofu_crg_write_flip(&pdev->dev, crg->base);
			dev_info(&pdev->dev, "WRITE_FLIP rc=%d\n", rc);
		}
#else
		if (write_flip)
			dev_warn(&pdev->dev, "write_flip=1 ignored: the flip is not compiled in (LUOFU_CRG_FLIP=0)\n");
#endif
		luofu_crg_inventory(&pdev->dev, crg->base, true);
	} else if (write_test) {
		dev_warn(&pdev->dev, "write_test=1 ignored: the write path runs only on the forced (no-DT) probe\n");
	}

#if LUOFU_CRG_FLIP
	/* STAGE 2b (crgnext.md sec 3): the reset-class flip is its OWN 2-store
	 * instrument, so rst_flip=1 alone reaches it -- no stage-1 sweep, honouring
	 * the design's "exactly two stores" bound.  Forced (no-DT) path only. */
	if (rst_flip && !pdev->dev.of_node) {
		int rc;

		rc = luofu_crg_write_rstflip(&pdev->dev, crg->base);
		dev_info(&pdev->dev, "WRITE_RSTFLIP rc=%d\n", rc);
		luofu_crg_inventory(&pdev->dev, crg->base, true);
	} else if (rst_flip) {
		dev_warn(&pdev->dev, "rst_flip=1 ignored: the reset-class flip runs only on the forced (no-DT) probe\n");
	}
#else
	if (rst_flip)
		dev_warn(&pdev->dev, "rst_flip=1 ignored: the reset-class flip is not compiled in (LUOFU_CRG_FLIP=0)\n");
#endif

	platform_set_drvdata(pdev, crg);
	/* Clock registration (DT path) already returned its result above; the
	 * forced smoke path reports 0 after its bounded, self-terminating
	 * instruments. */
	return 0;
}

static int luofu_crg_remove(struct platform_device *pdev)
{
	struct luofu_crg *crg = platform_get_drvdata(pdev);

	/* Drop the DT onecell provider FIRST (reverse order of probe); the
	 * devm_clk_hw_register'd clocks are then unregistered automatically by
	 * device-managed teardown. */
	if (crg && pdev->dev.of_node)
		of_clk_del_provider(pdev->dev.of_node);
	/* TODO: unregister the reset controller (reverse order of probe). */
	return 0;
}

static struct platform_driver luofu_crg_driver = {
	.probe		= luofu_crg_probe,
	.remove		= luofu_crg_remove,
	.driver		= {
		.name		= "luofu-crg",
		.of_match_table	= luofu_crg_match_table,
	},
};

/*
 * The force_probe device: name-matched to the driver ("luofu-crg") with id -1
 * (dev_name "luofu-crg.0"), carrying only the pinned memory resource and no
 * of_node, so platform_match()'s name compare binds it where
 * of_driver_match_device() cannot.
 */
static struct resource luofu_crg_res =
	DEFINE_RES_MEM(LUOFU_CRG_BASE, LUOFU_CRG_SIZE);

static struct platform_device luofu_crg_fdev = {
	.name		= "luofu-crg",
	.id		= -1,
	.num_resources	= 1,
	.resource	= &luofu_crg_res,
};

static bool luofu_crg_fdev_live;

static int __init luofu_crg_init(void)
{
	int ret;

	/* TODO (clocks2.md sec 3): in-tree this becomes core_initcall() so the
	 * CRG is up before 8250_dw/gpio/i2c/mtd probe.  module_init is fine for
	 * the loadable lab bring-up. */
	ret = platform_driver_register(&luofu_crg_driver);
	if (ret)
		return ret;

	/* The DT-presence guard makes "without a DT match" literal: the forced
	 * path can never co-exist with a real bind. */
	if (force_probe) {
		if (of_find_compatible_node(NULL, NULL, "hisilicon,luofu-crg")) {
			pr_warn("luofu-crg: force_probe ignored, DT node present (would double-bind)\n");
		} else if (platform_device_register(&luofu_crg_fdev) == 0) {
			luofu_crg_fdev_live = true;
		} else {
			pr_warn("luofu-crg: force_probe device registration failed\n");
		}
	}
	return 0;
}

static void __exit luofu_crg_exit(void)
{
	if (luofu_crg_fdev_live)
		platform_device_unregister(&luofu_crg_fdev);
	platform_driver_unregister(&luofu_crg_driver);
}

module_init(luofu_crg_init);
module_exit(luofu_crg_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hi5671Y luofu CRG clock + reset controller (gate/mux/PLL clk_ops + DT onecell provider + forced probe + the bounded write path + the stage-2b reset-class flip)");
