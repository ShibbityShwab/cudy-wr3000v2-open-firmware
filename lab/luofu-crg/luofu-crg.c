// SPDX-License-Identifier: GPL-2.0
/*
 * luofu-crg: minimal clock gate provider for the HiSilicon "luofu" CRG.
 *
 * Reversed from the vendor's hi_crg.ko (6,896 bytes), 2026-10-27:
 *
 *   hi_crg_plat_probe: for (i = 0; i < 15; i++) {
 *       res  = platform_get_resource(pdev, IORESOURCE_MEM, i);
 *       if (!res) break;
 *       bank[i] = devm_ioremap_resource(dev, res);
 *       bank[i].size = res->end - res->start + 1;
 *   }
 *
 *   hi_crg_enable(bank, reg, bit):
 *       if (bank > 14 || bit > 31) return -EPERM;
 *       if (reg > bank[bank].size - 4) return -EPERM;
 *       bank[bank][reg] |= (1 << bit);
 *
 * So a "bank" is simply one of the node's reg entries, and a gate is a single
 * bit in a 32-bit word of that bank. The DT describes each gate as
 * clk_gate@NNNNNN with reg-offset and bit, and clock-output-names.
 *
 * What this driver exists for: our trees reference <&crg ...> for the GMAC and
 * PIE clocks, but nothing bound that node, so every clk_get() in those drivers
 * failed and the engine's clock gate was never opened by anyone. This registers
 * the gates as real clk_hw so the normal clk API works.
 */
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>

#define LUOFU_CRG_MAX_BANKS	15

/*
 * The reset contract comes straight from the DT: `#reset-cells = <2>`, cells are
 * (reg-offset, bit). 0x30 is the only offset any consumer in this tree asks for, in bank 0.
 */
#define LUOFU_CRG_NR_RESETS	(256 * 32)

struct luofu_crg {
	void __iomem	*bank[LUOFU_CRG_MAX_BANKS];
	resource_size_t	 size[LUOFU_CRG_MAX_BANKS];
	unsigned int	 nbanks;	/* reg entries on this node - NOT the gate loop's index */
	struct clk_hw_onecell_data *data;
	struct reset_controller_dev rcdev;
};

static struct luofu_crg *luofu_crg_g;

/* One gate: bit `bit` of 32-bit word `off` inside bank `bank`. */
struct luofu_gate {
	struct clk_hw	hw;
	struct luofu_crg *crg;
	u32		bank;
	u32		off;
	u32		bit;
};

static void luofu_crg_stamp(u32 v);

static int luofu_gate_enable(struct clk_hw *hw)
{
	struct luofu_gate *g = container_of(hw, struct luofu_gate, hw);
	void __iomem *reg = g->crg->bank[g->bank] + g->off;

	writel(readl(reg) | BIT(g->bit), reg);
	return 0;
}

static void luofu_gate_disable(struct clk_hw *hw)
{
	struct luofu_gate *g = container_of(hw, struct luofu_gate, hw);
	void __iomem *reg = g->crg->bank[g->bank] + g->off;

	writel(readl(reg) & ~BIT(g->bit), reg);
}

static int luofu_gate_is_enabled(struct clk_hw *hw)
{
	struct luofu_gate *g = container_of(hw, struct luofu_gate, hw);

	return !!(readl(g->crg->bank[g->bank] + g->off) & BIT(g->bit));
}

static const struct clk_ops luofu_gate_ops = {	.enable		= luofu_gate_enable,
	.disable	= luofu_gate_disable,
	.is_enabled	= luofu_gate_is_enabled,
};

/*
 * RESET CONTROLLER.
 *
 * The DT contract is `#reset-cells = <2>` and its comments pin the cells as (reg-offset, bit) -
 * see the gmac nodes' `resets = <&crg 0x30 0x11>, <&crg 0x30 0x12>;`. Rather than an @of_xlate
 * (which cannot carry per-reset driver data), the two args are packed into the reset ID by the
 * core's default xlate: id = (args[0] << 32) | args[1] is NOT what the core does, so we supply
 * @of_reset_n_cells = 2 and decode the SAME packing the core uses: args[1] as the id when only
 * one cell is left. The core's default xlate takes the LAST cell as the id, so the offset
 * travels in args[0] and is ignored - which is why every consumer here uses offset 0x30, the
 * only one any node in this tree requests. That constant is what the ops below use.
 */
#define LUOFU_CRG_RESET_OFF_0X30	0x30

/*
 * THE PROVIDER MUST SUPPLY ITS OWN of_xlate (2026-10-30).
 *
 * drivers/reset/core.c:reset_controller_register() does:
 *
 *	if (!rcdev->of_xlate) {
 *		rcdev->of_reset_n_cells = 1;		 <- OVERWRITES this driver's 2
 *		rcdev->of_xlate = of_reset_simple_xlate;
 *	}
 *
 * So setting `of_reset_n_cells = 2` without an `of_xlate` silently left the controller
 * believing it takes ONE cell, while every consumer in this tree passes TWO
 * (`resets = <&crg 0x30 0x11>, <&crg 0x30 0x12>;`). The lookup then hits
 *
 *	WARN_ON(args.args_count != rcdev->of_reset_n_cells)		 <- 2 != 1
 *
 * for EVERY reset consumer and returns -EINVAL, which is why no network driver ever got its
 * resets and why the GMAC and MDIO probes both died at their reset fetch.
 *
 * The DTS contract is (reg-offset, bit). All consumers use offset 0x30, so the ops below read
 * that one word; the bit is the id, masked to 5 bits.
 */
static int luofu_crg_of_xlate(struct reset_controller_dev *rcdev,
			      const struct of_phandle_args *spec)
{
	if (spec->args_count != 2)
		return -EINVAL;

	return spec->args[1] & 31;
}

static int luofu_crg_reset(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct luofu_crg *crg = container_of(rcdev, struct luofu_crg, rcdev);
	u32 v;

	v = readl(crg->bank[0] + LUOFU_CRG_RESET_OFF_0X30);
	v |= BIT(id & 31);
	writel(v, crg->bank[0] + LUOFU_CRG_RESET_OFF_0X30);
	(void)readl(crg->bank[0] + LUOFU_CRG_RESET_OFF_0X30);
	return 0;
}

static int luofu_crg_reset_assert(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct luofu_crg *crg = container_of(rcdev, struct luofu_crg, rcdev);
	u32 v;

	v = readl(crg->bank[0] + LUOFU_CRG_RESET_OFF_0X30);
	v &= ~BIT(id & 31);
	writel(v, crg->bank[0] + LUOFU_CRG_RESET_OFF_0X30);
	(void)readl(crg->bank[0] + LUOFU_CRG_RESET_OFF_0X30);
	return 0;
}

static int luofu_crg_reset_deassert(struct reset_controller_dev *rcdev, unsigned long id)
{
	return luofu_crg_reset(rcdev, id);
}

static int luofu_crg_reset_status(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct luofu_crg *crg = container_of(rcdev, struct luofu_crg, rcdev);

	return !!(readl(crg->bank[0] + LUOFU_CRG_RESET_OFF_0X30) & BIT(id & 31));
}

static const struct reset_control_ops luofu_crg_reset_ops = {
	.reset		= luofu_crg_reset,
	.assert		= luofu_crg_reset_assert,
	.deassert	= luofu_crg_reset_deassert,
	.status		= luofu_crg_reset_status,
};

static void luofu_crg_stamp(u32 v)
{
	void __iomem *sc = ioremap(0x10100000, 0x1000);

	if (!sc)
		return;
	writel(v, sc + 0xc18);
	(void)readl(sc + 0xc18);
	iounmap(sc);
}

static int luofu_crg_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct luofu_crg *crg;
	struct clk_hw_onecell_data *data;
	struct device_node *child;
	int n = 0, i, ret;

	/*
	 * ENTRY/RESET STAMPS (2026-10-30). The compatible string is now "hsan,crg" in the compiled
	 * DTB and the driver matches it, yet the GMAC still fails at devm_reset_control_array_get
	 * (C94 = 0xC0DE6F04). Two candidates remain and they need different fixes:
	 *
	 *   0xC0DE8C01  probe ENTERED          -> the driver DID bind to the node
	 *   0xC0DE8C02  reached the reset registration call
	 *   0xC0DE8C03  devm_reset_controller_register returned OK
	 *   (none)      -> the driver never bound; something else claims the node
	 */
	luofu_crg_stamp(0xC0DE8C01u);

	crg = devm_kzalloc(dev, sizeof(*crg), GFP_KERNEL);
	if (!crg)
		return -ENOMEM;

	/* the banks ARE the node's reg entries - exactly what hi_crg_plat_probe does */
	for (i = 0; i < LUOFU_CRG_MAX_BANKS; i++) {
		struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, i);

		if (!res)
			break;
		crg->bank[i] = devm_ioremap_resource(dev, res);
		if (IS_ERR(crg->bank[i]))
			return PTR_ERR(crg->bank[i]);
		crg->size[i] = resource_size(res);
	}

	if (!i) {
		dev_err(dev, "no reg banks\n");
		return -ENODEV;
	}
	crg->nbanks = i;	/* the BANK COUNT - `i` is reused as the gate index below */

	for_each_available_child_of_node(dev->of_node, child)
		n++;
	if (!n) {
		dev_err(dev, "no clock gate children\n");
		return -ENODEV;
	}

	data = devm_kzalloc(dev, struct_size(data, hws, n), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->num = n;

	i = 0;
	for_each_available_child_of_node(dev->of_node, child) {
		struct luofu_gate *g;
		const char *name;
		u32 bank, off, bit;

		if (of_property_read_u32(child, "reg-offset", &off) ||
		    of_property_read_u32(child, "bit", &bit)) {
			dev_warn(dev, "%pOF: no reg-offset/bit, skipped\n", child);
			continue;
		}
		/* the vendor's gates live in bank 0 unless the DT says otherwise */
		if (of_property_read_u32(child, "bank", &bank))
			bank = 0;

		if (bank >= crg->nbanks || bit > 31) {
			dev_warn(dev, "%pOF: bank %u / bit %u out of range\n", child, bank, bit);
			continue;
		}

		if (of_property_read_string(child, "clock-output-names", &name) || !name)
			name = child->name;

		g = devm_kzalloc(dev, sizeof(*g), GFP_KERNEL);
		if (!g)
			return -ENOMEM;
		g->crg	= crg;
		g->bank	= bank;
		g->off	= off;
		g->bit	= bit;

		{
			struct clk_init_data init = {
				.name		= name,
				.ops		= &luofu_gate_ops,
				.flags		= CLK_IS_CRITICAL,
				.num_parents	= 0,
			};
			g->hw.init = &init;
			ret = devm_clk_hw_register(dev, &g->hw);
			if (ret)
				return ret;
		}
		data->hws[i] = &g->hw;
		i++;
	}
	data->num = i;

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, data);
	if (ret)
		return ret;

	/*
	 * THE RESET PROVIDER - it never existed, and its absence is why G4 has no driver.
	 *
	 * The DTS has advertised `#reset-cells = <2>` on this node from the start, and every gmac and
	 * mdio0 node asks for `resets = <&crg 0x30 0x11>` and friends. But this driver only ever registered a
	 * CLOCK provider - `reset_controller_register` appeared nowhere in the tree - so
	 * devm_reset_control_array_get(..., false, false) failed for every network driver. That is the
	 * crumb C94 = 0xC0DE6F04, and it explains every G4 hypothesis that was tested against a MAC that
	 * had never probed.
	 *
	 * The DT already fixes the contract: cells are (reg-offset, bit), and the vendor's own disassembly
	 * shows the shape - read the word at bank+offset, then set (deassert) or clear+set (assert) the
	 * bit. Reset offsets here are 0x30, which lands in bank 0 of the same 0x14880000 page.
	 */
	crg->rcdev.owner = THIS_MODULE;
	crg->rcdev.nr_resets = LUOFU_CRG_NR_RESETS;
	crg->rcdev.ops = &luofu_crg_reset_ops;
	crg->rcdev.of_node = dev->of_node;
	crg->rcdev.of_xlate = luofu_crg_of_xlate;	/* MUST be set: the core forces n_cells=1 otherwise */
	crg->rcdev.of_reset_n_cells = 2;
	luofu_crg_stamp(0xC0DE8C02u);
	ret = devm_reset_controller_register(dev, &crg->rcdev);
	if (ret)
		return ret;
	luofu_crg_stamp(0xC0DE8C03u);

	luofu_crg_g = crg;
	dev_info(dev, "registered %d clock gates across %d banks + reset provider\n", i, n);
	return 0;
}

static const struct of_device_id luofu_crg_of_match[] = {
	{ .compatible = "hsan,crg" },
	{ }
};
MODULE_DEVICE_TABLE(of, luofu_crg_of_match);

static struct platform_driver luofu_crg_driver = {
	.probe	= luofu_crg_probe,
	.driver	= {
		.name			= "luofu-crg",
		.of_match_table	= luofu_crg_of_match,
	},
};
module_platform_driver(luofu_crg_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HiSilicon luofu CRG clock gates (reversed from hi_crg.ko)");
