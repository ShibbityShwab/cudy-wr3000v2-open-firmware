// SPDX-License-Identifier: GPL-2.0
/*
 * luofu-pinctrl: stage-2 skeleton for the Hi5671Y "luofu" peripheral IOMUX
 * (DT compatible "hsan,luofu-peri-pinctrl"; the vendor's hi_kpinctrl module).
 *
 * ==========================  SKELETON  ==========================
 * A DESIGN SKELETON: it carries the shape (of_match_table + probe/remove +
 * pinctrl_register with a transcribed pin/group/function table) that the
 * stage-2 bring-up fills in; the mux/config register programming stays TODO.
 * It COMPILES against the vanilla 5.10.201 arm headers in the CI cross-build
 * (.github/workflows/lab-module-build.yml -> lab/luofu-pinctrl, plus the
 * build-load-test-module.yml lane) and performs NO register writes: probe
 * maps the "mux"/"cfg" windows, registers the pinctrl device and logs the
 * transcribed geometry.
 * ================================================================
 *
 * Spec: build/tmp/inta-spec/stage2.md (row #2: pinctrl + IOMUX -> hi_kpinctrl).
 * The vendor evidence is disassembled from the shipped module
 *   rootfs-2.4.15/squashfs-root/lib/hisilicon/ko/hi_kpinctrl.ko
 * (vermagic 5.10.201 SMP mod_unload ARMv7): 37 pins (pin0..pin36), 24 groups,
 * 24 functions (1:1 with the groups), and a probe that ioremaps two windows by
 * reg-name then pinctrl_register()s.  The DT node it binds
 * (opensource/docs/soc/luofu-r116-pinned.dts:1270):
 *
 *   pinctrl@0x14900000 {
 *       compatible = "hsan,luofu-peri-pinctrl";
 *       reg = <0x14900100 0x3c 0x14940000 0x100>;
 *       reg-names = "mux" "cfg";
 *       ... function/groups strings: jtag_gpio2..5, led0_1, led1_1, avs, spi ...
 *   };
 *
 * Register plan (vendor disassembly, hi_pinctrl_probe / hi_pinmux_set_mux /
 * luofu_pin_config_set):
 *   - "mux" window @ 0x14900100, 0x3c (15 x 32-bit registers).  set_mux does a
 *     read-modify-write of mux_base[reg_off], toggling bit (1 << shift) where
 *     (reg_off, shift, func_name) come from each pin's drv_data table.
 *   - "cfg"  window @ 0x14940000, 0x100 (64 x 32-bit registers).  pin_config_set
 *     does read-modify-write bitfield-inserts on cfg_base[pin_cfg_off].
 *   - probe also devm_reset_control_get() + reset_control_deassert()s the
 *     shared reset (hsan,hsan-reset); kept TODO here (it is a reset write, not
 *     a pinctrl register write).
 * This skeleton maps both windows but writes nothing.
 *
 * STAGE-2 GOALS (stage2.md sec 2, dep #2):
 *   1. provide a pin controller so every consumer's pinctrl-0/1 reference
 *      (ledpwm, pmupwm, pcie "pcie-gpios", uart, gpio-dwapb) resolves;
 *   2. carry the IOMUX shape (pinmux_ops.set_mux) so the mux tables can be
 *      transcribed in-place later;
 *   3. no hardware writes -- the board is already muxed by the vendor/U-Boot,
 *      so a no-op set_mux preserves bootloader state.
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/slab.h>

/* MMIO windows (from the DT node; the constants document the pinned addresses).
 * reg-names "mux"/"cfg", luofu-r116-pinned.dts:1270. */
#define LUOFU_MUX_BASE	0x14900100UL
#define LUOFU_MUX_SIZE	0x3cUL		/* 15 x 32-bit mux registers */
#define LUOFU_CFG_BASE	0x14940000UL
#define LUOFU_CFG_SIZE	0x100UL		/* 64 x 32-bit config registers */

/* Largest transcribed group pin count (rgmii0_grp = 12).  Used only for the
 * placeholder pin list below; the real per-group pin lists are the stage-2
 * TODO (recoverable from the vendor per-pin drv_data tables). */
#define LUOFU_MAX_GROUP_PINS	12

/*
 * Pin table (37 pins) transcribed from the vendor module's .rodata.str1.4 /
 * pin descriptor array: pin0..pin36.
 */
#define LUOFU_PIN(_id)	{ .number = (_id), .name = "pin" #_id }

static const struct pinctrl_pin_desc luofu_pins[] = {
	LUOFU_PIN(0),  LUOFU_PIN(1),  LUOFU_PIN(2),  LUOFU_PIN(3),
	LUOFU_PIN(4),  LUOFU_PIN(5),  LUOFU_PIN(6),  LUOFU_PIN(7),
	LUOFU_PIN(8),  LUOFU_PIN(9),  LUOFU_PIN(10), LUOFU_PIN(11),
	LUOFU_PIN(12), LUOFU_PIN(13), LUOFU_PIN(14), LUOFU_PIN(15),
	LUOFU_PIN(16), LUOFU_PIN(17), LUOFU_PIN(18), LUOFU_PIN(19),
	LUOFU_PIN(20), LUOFU_PIN(21), LUOFU_PIN(22), LUOFU_PIN(23),
	LUOFU_PIN(24), LUOFU_PIN(25), LUOFU_PIN(26), LUOFU_PIN(27),
	LUOFU_PIN(28), LUOFU_PIN(29), LUOFU_PIN(30), LUOFU_PIN(31),
	LUOFU_PIN(32), LUOFU_PIN(33), LUOFU_PIN(34), LUOFU_PIN(35),
	LUOFU_PIN(36),
};

#undef LUOFU_PIN

/*
 * Group table (24 groups) transcribed from the vendor module: name + npins.
 * The pin membership is NOT yet transcribed, so every group points at the same
 * placeholder list for now -- get_group_pins() therefore returns pin 0..npins-1.
 * Transcribing the real per-group pin numbers (from the vendor .data group
 * struct, offset 0x04 -> pins[]) is the first stage-2 fill-in.
 */
struct luofu_pingroup {
	const char *name;
	unsigned int npins;
	const unsigned int *pins;
};

static const unsigned int luofu_group_pins_placeholder[LUOFU_MAX_GROUP_PINS] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
};

static const struct luofu_pingroup luofu_groups[] = {
	{ "sfc_grp",           2, luofu_group_pins_placeholder },
	{ "uart1_grp",         2, luofu_group_pins_placeholder },
	{ "uart0_grp",         4, luofu_group_pins_placeholder },
	{ "mdio1_grp",         2, luofu_group_pins_placeholder },
	{ "spi_grp",           4, luofu_group_pins_placeholder },
	{ "i2c_grp",           2, luofu_group_pins_placeholder },
	{ "rgmii0_grp",       12, luofu_group_pins_placeholder },
	{ "eth_act_led0_grp",  1, luofu_group_pins_placeholder },
	{ "eth_act_led1_grp",  1, luofu_group_pins_placeholder },
	{ "eth_act_led2_grp",  1, luofu_group_pins_placeholder },
	{ "eth_act_led3_grp",  1, luofu_group_pins_placeholder },
	{ "eth_act_led4_grp",  1, luofu_group_pins_placeholder },
	{ "led0_0_grp",        1, luofu_group_pins_placeholder },
	{ "led1_0_grp",        1, luofu_group_pins_placeholder },
	{ "led0_1_grp",        1, luofu_group_pins_placeholder },
	{ "led1_1_grp",        1, luofu_group_pins_placeholder },
	{ "clk_test_grp",      1, luofu_group_pins_placeholder },
	{ "avs_grp",           1, luofu_group_pins_placeholder },
	{ "phy_clk_grp",       1, luofu_group_pins_placeholder },
	{ "usb_grp",           2, luofu_group_pins_placeholder },
	{ "jtag_gpio2_grp",    1, luofu_group_pins_placeholder },
	{ "jtag_gpio3_grp",    1, luofu_group_pins_placeholder },
	{ "jtag_gpio4_grp",    1, luofu_group_pins_placeholder },
	{ "jtag_gpio5_grp",    1, luofu_group_pins_placeholder },
};

/* Group names, same order as luofu_groups[] -- the 1:1 function<->group map
 * (function i selects group i) is expressed by index into this array. */
static const char * const luofu_group_names[] = {
	"sfc_grp", "uart1_grp", "uart0_grp", "mdio1_grp", "spi_grp", "i2c_grp",
	"rgmii0_grp",
	"eth_act_led0_grp", "eth_act_led1_grp", "eth_act_led2_grp",
	"eth_act_led3_grp", "eth_act_led4_grp",
	"led0_0_grp", "led1_0_grp", "led0_1_grp", "led1_1_grp",
	"clk_test_grp", "avs_grp", "phy_clk_grp", "usb_grp",
	"jtag_gpio2_grp", "jtag_gpio3_grp", "jtag_gpio4_grp", "jtag_gpio5_grp",
};

/* Function names (24), same order -- the group name with "_grp" stripped. */
static const char * const luofu_function_names[] = {
	"sfc", "uart1", "uart0", "mdio1", "spi", "i2c", "rgmii0",
	"eth_act_led0", "eth_act_led1", "eth_act_led2", "eth_act_led3",
	"eth_act_led4",
	"led0_0", "led1_0", "led0_1", "led1_1",
	"clk_test", "avs", "phy_clk", "usb",
	"jtag_gpio2", "jtag_gpio3", "jtag_gpio4", "jtag_gpio5",
};

/* Per-instance state. */
struct luofu_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctldev;
	void __iomem *mux_base;	/* "mux" window, no writes */
	void __iomem *cfg_base;	/* "cfg"  window, no writes */
};

/* ---- pinctrl ops (pin/group enumeration) ---- */

static int luofu_get_groups_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(luofu_groups);
}

static const char *luofu_get_group_name(struct pinctrl_dev *pctldev,
					unsigned int selector)
{
	if (selector >= ARRAY_SIZE(luofu_groups))
		return NULL;
	return luofu_groups[selector].name;
}

static int luofu_get_group_pins(struct pinctrl_dev *pctldev,
				unsigned int selector,
				const unsigned int **pins,
				unsigned int *num_pins)
{
	if (selector >= ARRAY_SIZE(luofu_groups))
		return -EINVAL;

	/* TODO: return the real per-group pin list once transcribed. */
	*pins = luofu_groups[selector].pins;
	*num_pins = luofu_groups[selector].npins;
	return 0;
}

static const struct pinctrl_ops luofu_pctl_ops = {
	.get_groups_count = luofu_get_groups_count,
	.get_group_name = luofu_get_group_name,
	.get_group_pins = luofu_get_group_pins,
	/* .dt_node_to_map left NULL: the pinctrl core then falls back to
	 * pinconf_generic_dt_node_to_map(), which parses the vendor's
	 * "function"/"groups" strings on pinctrl-0/1. */
};

/* ---- pinmux ops (function enumeration + mux programming) ---- */

static int luofu_get_functions_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(luofu_function_names);
}

static const char *luofu_get_function_name(struct pinctrl_dev *pctldev,
					   unsigned int selector)
{
	if (selector >= ARRAY_SIZE(luofu_function_names))
		return NULL;
	return luofu_function_names[selector];
}

static int luofu_get_function_groups(struct pinctrl_dev *pctldev,
				     unsigned int selector,
				     const char * const **groups,
				     unsigned int *num_groups)
{
	if (selector >= ARRAY_SIZE(luofu_function_names))
		return -EINVAL;

	/* 1:1 function<->group mapping, same index order (vendor evidence). */
	*groups = &luofu_group_names[selector];
	*num_groups = 1;
	return 0;
}

static int luofu_set_mux(struct pinctrl_dev *pctldev,
			 unsigned int func_selector,
			 unsigned int group_selector)
{
	/*
	 * SKELETON: no write.  The vendor path (hi_pinmux_set_mux) does a
	 * read-modify-write of mux_base[reg_off], toggling bit (1 << shift)
	 * per (reg_off, shift, func_name) entry in the selected pin's drv_data.
	 * Transcribing those per-pin tables is the stage-2 fill-in; until then
	 * selecting a function is a no-op so bootloader mux state is preserved.
	 */
	return 0;
}

static const struct pinmux_ops luofu_pmx_ops = {
	.get_functions_count = luofu_get_functions_count,
	.get_function_name = luofu_get_function_name,
	.get_function_groups = luofu_get_function_groups,
	.set_mux = luofu_set_mux,
};

/* ---- pinconf ops (stubs -- no writes) ---- */

static int luofu_pin_config_set(struct pinctrl_dev *pctldev, unsigned int pin,
				unsigned long *configs, unsigned int num_configs)
{
	/* SKELETON: no write.  TODO: cfg_base[pin_cfg_off] bitfield-insert. */
	return 0;
}

static int luofu_pin_config_get(struct pinctrl_dev *pctldev, unsigned int pin,
				unsigned long *config)
{
	*config = 0;
	return -ENOTSUPP;
}

static int luofu_pin_config_group_set(struct pinctrl_dev *pctldev,
				      unsigned int selector,
				      unsigned long *configs,
				      unsigned int num_configs)
{
	/* SKELETON: no write. */
	return 0;
}

static int luofu_pin_config_group_get(struct pinctrl_dev *pctldev,
				      unsigned int selector,
				      unsigned long *config)
{
	*config = 0;
	return -ENOTSUPP;
}

static const struct pinconf_ops luofu_conf_ops = {
	.pin_config_set = luofu_pin_config_set,
	.pin_config_get = luofu_pin_config_get,
	.pin_config_group_set = luofu_pin_config_group_set,
	.pin_config_group_get = luofu_pin_config_group_get,
};

static struct pinctrl_desc luofu_pinctrl_desc = {
	.name = "luofu-pinctrl",
	.pins = luofu_pins,
	.npins = ARRAY_SIZE(luofu_pins),
	.pctlops = &luofu_pctl_ops,
	.pmxops = &luofu_pmx_ops,
	.confops = &luofu_conf_ops,
	.owner = THIS_MODULE,
};

static const struct of_device_id luofu_pinctrl_match[] = {
	{ .compatible = "hsan,luofu-peri-pinctrl" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, luofu_pinctrl_match);

static int luofu_pinctrl_probe(struct platform_device *pdev)
{
	struct luofu_pinctrl *priv;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = &pdev->dev;

	/* Map both windows (no writes).  reg-names "mux"/"cfg" per
	 * luofu-r116-pinned.dts:1270. */
	priv->mux_base = devm_platform_ioremap_resource_byname(pdev, "mux");
	if (IS_ERR(priv->mux_base))
		return PTR_ERR(priv->mux_base);
	priv->cfg_base = devm_platform_ioremap_resource_byname(pdev, "cfg");
	if (IS_ERR(priv->cfg_base))
		return PTR_ERR(priv->cfg_base);

	/* TODO: devm_reset_control_get(&pdev->dev, NULL) +
	 *       reset_control_deassert() (vendor probe shape) once the reset
	 *       controller is up.  Kept out: it is a reset write. */

	platform_set_drvdata(pdev, priv);

	priv->pctldev = pinctrl_register(&luofu_pinctrl_desc, &pdev->dev, priv);
	if (IS_ERR(priv->pctldev))
		return PTR_ERR(priv->pctldev);

	/* Prove the transcribed geometry is wired (no register access). */
	dev_info(&pdev->dev,
		 "luofu-pinctrl: %zu pins, %zu groups, %zu functions (skeleton)\n",
		 ARRAY_SIZE(luofu_pins), ARRAY_SIZE(luofu_groups),
		 ARRAY_SIZE(luofu_function_names));

	return 0;
}

static int luofu_pinctrl_remove(struct platform_device *pdev)
{
	struct luofu_pinctrl *priv = platform_get_drvdata(pdev);

	pinctrl_unregister(priv->pctldev);
	return 0;
}

static struct platform_driver luofu_pinctrl_driver = {
	.probe = luofu_pinctrl_probe,
	.remove = luofu_pinctrl_remove,
	.driver = {
		.name = "luofu-pinctrl",
		.of_match_table = luofu_pinctrl_match,
	},
};

module_platform_driver(luofu_pinctrl_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hi5671Y luofu peripheral IOMUX pinctrl (stage-2 skeleton)");
