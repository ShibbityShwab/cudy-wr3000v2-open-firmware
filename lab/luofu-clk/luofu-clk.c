// SPDX-License-Identifier: GPL-2.0
/*
 * luofu-clk: stage-1 skeleton for the Hi5671Y "luofu" CRG clock + reset
 * controller (DT compatible "hisilicon,luofu-crg").
 *
 * ======================  NOT-YET-COMPILED  ======================
 * This is a DESIGN SKELETON, not a buildable driver.  It carries the shape
 * (of_match_table + probe/remove + the regmap plan) that the stage-1 bring-up
 * will fill in; the hisi_clk_*/reset helpers and the real clock-data wiring
 * are left as TODO.  Do not build it against the stock tree as-is.
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
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include <linux/types.h>

/* CRG MMIO page.  reg comes from the DT node; the constant documents the
 * pinned address (pinned clk@14880000 reg :205, node :201). */
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
};

static const struct luofu_gate luofu_gates[] = {
	{ LUOFU_CLK_SFC,     0x14, 0x00, "sfc_clk"      },	/* :236 */
	{ LUOFU_CLK_GPIO0,   0x14, 0x14, "gpio0_clk"    },	/* :206 */
	{ LUOFU_CLK_GPIO1,   0x14, 0x15, "gpio1_clk"    },	/* :216 */
	{ LUOFU_CLK_I2C0,    0x14, 0x18, "i2c0_clk"     },	/* :226 */
	{ LUOFU_CLK_LED_PWM, 0x14, 0x0e, "led_pwm"      },	/* :246 */
	{ LUOFU_CLK_PCIE0,   0x20, 0x0c, "pcie0_clk"    },	/* :286 */
	{ LUOFU_CLK_PCIE1,   0x20, 0x0d, "pcie1_clk"    },	/* :296 */
	{ LUOFU_CLK_GMAC0,   0x20, 0x13, "gemac_clk0"   },	/* :306 */
	{ LUOFU_CLK_GMAC1,   0x20, 0x14, "gemac_clk1"   },	/* :316 */
	{ LUOFU_CLK_GMAC2,   0x20, 0x15, "gemac_clk2"   },	/* :326 */
	{ LUOFU_CLK_GMAC3,   0x20, 0x16, "gemac_clk3"   },	/* :336 */
	{ LUOFU_CLK_GMAC4,   0x20, 0x17, "gemac_clk4"   },	/* :346 */
	{ LUOFU_CLK_MDIO0,   0x20, 0x03, "mdio_clk0"    },	/* :276 */
	{ LUOFU_CLK_LSW_DP,  0x20, 0x00, "lsw_dp_clk0"  },	/* :256 */
	{ LUOFU_CLK_LSW_PFE, 0x20, 0x01, "lsw_pfe_clk0" },	/* :266 */
	{ LUOFU_CLK_PIE,     0x20, 0x19, "pie_clk0"     },	/* :356 */
};

/*
 * Muxes (pinned clk@14880000 children): (reg-offset, mask).
 */
struct luofu_mux {
	const char *name;
	u16 offset;
	u8 mask;
};

static const struct luofu_mux luofu_muxes[] = {
	{ "cpu_mux",   0x138, 0x8 },	/* cpu_mux@01388  :390 */
	{ "efuse_mux", 0x138, 0x2 },	/* efuse_mux@0138 :401 */
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
};

static const struct luofu_pll luofu_plls[] = {
	{ "cpu-clk", 0x198, 0x90, 0x1e },	/* clk_pll1@0198 :366 */
	{ "lsw-clk", 0x1e0, 0x90, 0x1b },	/* clk_pll2@01e0 :378 */
};

/* Per-instance state. */
struct luofu_crg {
	void __iomem *base;
	/* TODO: struct hisi_clock_data *clk_data;        (crg.h)
	 * TODO: struct reset_controller_dev rcdev;       (reset.c) */
};

static const struct of_device_id luofu_crg_match_table[] = {
	{ .compatible = "hisilicon,luofu-crg" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, luofu_crg_match_table);

static int luofu_crg_probe(struct platform_device *pdev)
{
	struct luofu_crg *crg;

	crg = devm_kzalloc(&pdev->dev, sizeof(*crg), GFP_KERNEL);
	if (!crg)
		return -ENOMEM;

	/* Regmap plan step 1: one shared mapping of the 0x1000 CRG page,
	 * reused by the clock tables and the reset controller. */
	crg->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(crg->base))
		return PTR_ERR(crg->base);

	/* TODO: register the gates -- hisi_clk_register_gate() per
	 *       luofu_gates[] (tagged CLK_IGNORE_UNUSED, goal 3). */
	/* TODO: register the two muxes -- hisi_mux_clock() per luofu_muxes[]. */
	/* TODO: register the two PLLs as read-only fixed-rate per luofu_plls[]. */
	/* TODO: register the reset controller -- hisi_reset_init(pdev) (reset.c)
	 *       on the same MMIO page; #reset-cells=<2> => args[0]=reg-offset,
	 *       args[1]=bit.  Reproduce LUOFU_SOFTRST_VAL0/1 in the reboot path. */

	platform_set_drvdata(pdev, crg);
	/* TODO: return the real registration result once the tables are wired. */
	return 0;
}

static int luofu_crg_remove(struct platform_device *pdev)
{
	struct luofu_crg *crg = platform_get_drvdata(pdev);

	/* TODO: unregister the reset controller, then the clock tree (reverse
	 * order of probe).  The devm-managed mapping/kzalloc are freed
	 * automatically. */
	(void)crg;
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

static int __init luofu_crg_init(void)
{
	/* TODO (clocks2.md sec 3): in-tree this becomes core_initcall() so the
	 * CRG is up before 8250_dw/gpio/i2c/mtd probe.  module_init is fine for
	 * the loadable lab bring-up. */
	return platform_driver_register(&luofu_crg_driver);
}

static void __exit luofu_crg_exit(void)
{
	platform_driver_unregister(&luofu_crg_driver);
}

module_init(luofu_crg_init);
module_exit(luofu_crg_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hi5671Y luofu CRG clock + reset controller (stage-1 skeleton, NOT-YET-COMPILED)");
