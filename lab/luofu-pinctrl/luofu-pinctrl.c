// SPDX-License-Identifier: GPL-2.0
/*
 * luofu-pinctrl: mapping-table driver for the Hi5671Y "luofu" peripheral IOMUX
 * (DT compatible "hsan,luofu-peri-pinctrl"; the vendor's hi_kpinctrl module).
 *
 * =====================  MAPPING-TABLE DRIVER  =====================
 * The stage-2 skeleton has been filled in from the vendor mapping: the 37-pin
 * table, the 24-group table with the REAL per-group pin lists (replacing the
 * old shared placeholder), the 24-function table, and the per-pin mux
 * drv_data ({reg_off, shift, func_name} x5 + cfg_off) are all transcribed
 * below from the shipped module and cited to their .ko offsets. The ops
 * tables keep the mainline pinctrl_ops/pinmux_ops/pinconf_ops shape; set_mux
 * and the pinconf setters remain NO-OP stubs (no register writes), so the
 * bootloader/vendor mux state is preserved -- the transcribed tables are data
 * for the next fill-in, not yet executed.
 *
 * It COMPILES against the vanilla 5.10.201 arm headers in the CI cross-build
 * (.github/workflows/lab-module-build.yml matrix entry lab/luofu-pinctrl) and
 * performs NO register writes.
 * ================================================================
 *
 * Vendor evidence source (authoritative copy):
 *   rootfs-2.4.15/squashfs-root/lib/hisilicon/ko/hi_kpinctrl.ko
 *   (vermagic 5.10.201 SMP mod_unload ARMv7; 17,636 B). Table offsets below
 *   are file offsets into that ELF's .data / .rodata sections, read out with
 *   pyelftools + capstone (see build/tmp/inta-spec/drvpin.md for the map).
 *
 * DT binding (the pinned base DTB node, opensource/docs/soc/luofu-r116-pinned.dts:1270;
 * also rendered in build/tmp/dt-spec/luofu-r116-pinned-body.dts):
 *
 *   pinctrl@0x14900000 {
 *       compatible = "hsan,luofu-peri-pinctrl";
 *       reg = <0x14900100 0x3c 0x14940000 0x100>;
 *       reg-names = "mux" "cfg";
 *       // pinctrl-0/1 consumer subnodes use the generic "function"/"groups"
 *       // string form, parsed by pinconf_generic_dt_node_to_map():
 *       jtag_gpio2_default_state { jtag_gpio2_pmux {
 *           function = "jtag_gpio2"; groups = "jtag_gpio2_grp"; }; };
 *       led0_1_default_state { led0_0_pmux {
 *           function = "led0_1"; groups = "led0_1_grp"; }; };
 *       led1_1_default_state { led1_1_pmux {
 *           function = "led1_1"; groups = "led1_1_grp"; }; };
 *       pmupwm_default_state  { pmupwm_pmux {
 *           function = "avs"; groups = "avs_grp"; }; };
 *       spi { spi_default_state { spi_pmux { function = "spi"; groups = "spi_grp"; }; };
 *             spi_sleep_state   { spi_pmux { function = "spi"; groups = "spi_grp"; }; }; };
 *   };
 *
 *   - "mux" window: 0x14900100..0x1490013c, 15 x 32-bit registers. set_mux
 *     (vendor hi_pinmux_set_mux @ .text+0x348) walk a group's pins, then each
 *     pin's drv_data entries {reg_off, shift, func_name}: read mux_base[reg_off],
 *     clear bit (1<<shift), and set it only when func_name == the requested
 *     function; a group with a mux_cfg (jtag_gpio2..5) additionally does a
 *     mask/value RMW of mux_base[reg_off]. dword-aligned read-modify-write.
 *   - "cfg" window: 0x14940000..0x149400ff, 64 x 32-bit registers. The cfg
 *     register for pin i is cfg_base[i] (vendor drv_data+0x3c holds cfg_off =
 *     4*i); luofu_pin_config_set (@ .text+0xa8) dispatches on config&0xff
 *     (types 3..8 -> bias/drive/slew bitfield-inserts).
 *   - probe also does devm_reset_control_get + reset_control_deassert on the
 *     shared hsan,hsan-reset; kept TODO here (a reset write, not a pinctrl
 *     register write).
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/slab.h>

/* MMIO windows (reg-names "mux"/"cfg"; luofu-r116-pinned.dts:1270). */
#define LUOFU_MUX_BASE	0x14900100UL
#define LUOFU_MUX_SIZE	0x3cUL		/* 15 x 32-bit mux registers */
#define LUOFU_CFG_BASE	0x14940000UL
#define LUOFU_CFG_SIZE	0x100UL		/* 64 x 32-bit config registers */

/* ---- Pin table ---- */
/*
 * 37 pins, "pin0".."pin36".
 * src: .rodata.str1.4 pin names @ 0x254..0x374; pin desc array @ .data+0x428
 * (12-byte entries {number, name, drv_data}; npins = 0x25 in the soc_data).
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

/* ---- Group table ---- */
/*
 * 24 groups. src: .data+0x68, 40-byte entries {name, pins, npins, mux_flag,
 * mux_reg_off, mux_mask, mux_val, ...}; the pin-number words live in one
 * concatenated .rodata array @ 0x32c..0x3e4, one pointer per group (reverse
 * order). Only jtag_gpio2..5 carry a group-level mux RMW (mask/value of bit 0
 * of mux register 8).
 */
struct luofu_pingroup {
	const char *name;
	const unsigned int *pins;
	unsigned int npins;
	unsigned int mux_reg;	/* group mux RMW: byte offset into "mux" (0 = none) */
	unsigned int mux_mask;	/* 0 = no group mux RMW */
	unsigned int mux_val;
};

static const unsigned int luofu_sfc_pins[] = { 30, 33 };
static const unsigned int luofu_uart1_pins[] = { 35, 36 };
static const unsigned int luofu_uart0_pins[] = { 16, 18, 19, 22 };
static const unsigned int luofu_mdio1_pins[] = { 10, 11 };
static const unsigned int luofu_spi_pins[] = { 13, 14, 15, 23 };
static const unsigned int luofu_i2c_pins[] = { 24, 25 };
static const unsigned int luofu_rgmii0_pins[] = {
	12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
};
static const unsigned int luofu_eth_act_led0_pins[] = { 2 };
static const unsigned int luofu_eth_act_led1_pins[] = { 3 };
static const unsigned int luofu_eth_act_led2_pins[] = { 4 };
static const unsigned int luofu_eth_act_led3_pins[] = { 5 };
static const unsigned int luofu_eth_act_led4_pins[] = { 7 };
static const unsigned int luofu_led0_0_pins[] = { 1 };
static const unsigned int luofu_led1_0_pins[] = { 0 };
static const unsigned int luofu_led0_1_pins[] = { 21 };
static const unsigned int luofu_led1_1_pins[] = { 20 };
static const unsigned int luofu_clk_test_pins[] = { 26 };
static const unsigned int luofu_avs_pins[] = { 8 };
static const unsigned int luofu_phy_clk_pins[] = { 9 };
static const unsigned int luofu_usb_pins[] = { 27, 28 };
static const unsigned int luofu_jtag_gpio2_pins[] = { 2 };
static const unsigned int luofu_jtag_gpio3_pins[] = { 3 };
static const unsigned int luofu_jtag_gpio4_pins[] = { 4 };
static const unsigned int luofu_jtag_gpio5_pins[] = { 5 };

#define LUOFU_GROUP(_n, _p, _mreg, _mmask, _mval) \
	{ .name = (_n), .pins = (_p), .npins = ARRAY_SIZE(_p), \
	  .mux_reg = (_mreg), .mux_mask = (_mmask), .mux_val = (_mval) }

static const struct luofu_pingroup luofu_groups[] = {
	LUOFU_GROUP("sfc_grp",           luofu_sfc_pins,           0, 0, 0),
	LUOFU_GROUP("uart1_grp",         luofu_uart1_pins,         0, 0, 0),
	LUOFU_GROUP("uart0_grp",         luofu_uart0_pins,         0, 0, 0),
	LUOFU_GROUP("mdio1_grp",         luofu_mdio1_pins,         0, 0, 0),
	LUOFU_GROUP("spi_grp",           luofu_spi_pins,           0, 0, 0),
	LUOFU_GROUP("i2c_grp",           luofu_i2c_pins,           0, 0, 0),
	LUOFU_GROUP("rgmii0_grp",        luofu_rgmii0_pins,        0, 0, 0),
	LUOFU_GROUP("eth_act_led0_grp",  luofu_eth_act_led0_pins,  0, 0, 0),
	LUOFU_GROUP("eth_act_led1_grp",  luofu_eth_act_led1_pins,  0, 0, 0),
	LUOFU_GROUP("eth_act_led2_grp",  luofu_eth_act_led2_pins,  0, 0, 0),
	LUOFU_GROUP("eth_act_led3_grp",  luofu_eth_act_led3_pins,  0, 0, 0),
	LUOFU_GROUP("eth_act_led4_grp",  luofu_eth_act_led4_pins,  0, 0, 0),
	LUOFU_GROUP("led0_0_grp",        luofu_led0_0_pins,        0, 0, 0),
	LUOFU_GROUP("led1_0_grp",        luofu_led1_0_pins,        0, 0, 0),
	LUOFU_GROUP("led0_1_grp",        luofu_led0_1_pins,        0, 0, 0),
	LUOFU_GROUP("led1_1_grp",        luofu_led1_1_pins,        0, 0, 0),
	LUOFU_GROUP("clk_test_grp",      luofu_clk_test_pins,      0, 0, 0),
	LUOFU_GROUP("avs_grp",           luofu_avs_pins,           0, 0, 0),
	LUOFU_GROUP("phy_clk_grp",       luofu_phy_clk_pins,       0, 0, 0),
	LUOFU_GROUP("usb_grp",           luofu_usb_pins,           0, 0, 0),
	LUOFU_GROUP("jtag_gpio2_grp",    luofu_jtag_gpio2_pins,    8, 0x1, 0x1),
	LUOFU_GROUP("jtag_gpio3_grp",    luofu_jtag_gpio3_pins,    8, 0x1, 0x1),
	LUOFU_GROUP("jtag_gpio4_grp",    luofu_jtag_gpio4_pins,    8, 0x1, 0x1),
	LUOFU_GROUP("jtag_gpio5_grp",    luofu_jtag_gpio5_pins,    8, 0x1, 0x1),
};

#undef LUOFU_GROUP

/* Group names, same order as luofu_groups[]. */
static const char * const luofu_group_names[] = {
	"sfc_grp", "uart1_grp", "uart0_grp", "mdio1_grp", "spi_grp", "i2c_grp",
	"rgmii0_grp",
	"eth_act_led0_grp", "eth_act_led1_grp", "eth_act_led2_grp",
	"eth_act_led3_grp", "eth_act_led4_grp",
	"led0_0_grp", "led1_0_grp", "led0_1_grp", "led1_1_grp",
	"clk_test_grp", "avs_grp", "phy_clk_grp", "usb_grp",
	"jtag_gpio2_grp", "jtag_gpio3_grp", "jtag_gpio4_grp", "jtag_gpio5_grp",
};

/* Function names (24), 1:1 with the groups (function i selects group i).
 * src: .rodata+0x20c function entries (12 B) + the reverse-ordered group-name
 * array @ .data+0x5e4..0x640 that each function's groups[0] points at. */
static const char * const luofu_function_names[] = {
	"sfc", "uart1", "uart0", "mdio1", "spi", "i2c", "rgmii0",
	"eth_act_led0", "eth_act_led1", "eth_act_led2", "eth_act_led3",
	"eth_act_led4",
	"led0_0", "led1_0", "led0_1", "led1_1",
	"clk_test", "avs", "phy_clk", "usb",
	"jtag_gpio2", "jtag_gpio3", "jtag_gpio4", "jtag_gpio5",
};

/* ---- Per-pin mux drv_data ---- */
/*
 * One 64-byte drv_data per pin at .data+0x644..0xf84 (pin i @ 0xf44 - i*0x40):
 * 5 x 12-byte entries {reg_off, shift, func_name} + a 4-byte cfg_off (= 4*i).
 * reg_off is a byte offset into the "mux" window, shift is the bit (1 << shift)
 * to toggle. Selecting a function sets the entry whose func_name matches and
 * clears the rest; "null" entries are bits kept clear, "gpio" is the GPIO
 * mux position. src: hi_pinmux_set_mux @ .text+0x348 reads 5 entries per pin
 * (add r4,#0xc, bound r4+0x3c).
 */
struct luofu_mux_entry {
	unsigned int reg;	/* byte offset into "mux" */
	unsigned int shift;	/* bit position */
	const char *func;	/* function name this position selects */
};

#define LUOFU_MUX(_r, _s, _f)	{ .reg = (_r), .shift = (_s), .func = (_f) }

static const struct luofu_mux_entry luofu_pin_mux[37][5] = {
	/* pin0  (drv @ .data+0xf44) */
	{ LUOFU_MUX(0,  0,  "gpio"), LUOFU_MUX(16, 7,  "led1_0"),
	  LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin1  (drv @ .data+0xf04) */
	{ LUOFU_MUX(0,  1,  "gpio"), LUOFU_MUX(16, 6,  "led0_0"),
	  LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin2  (drv @ .data+0xec4) */
	{ LUOFU_MUX(0,  2,  "jtag_gpio2"), LUOFU_MUX(16, 8, "eth_act_led0"),
	  LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin3  (drv @ .data+0xe84) */
	{ LUOFU_MUX(0,  3,  "jtag_gpio3"), LUOFU_MUX(16, 9, "eth_act_led1"),
	  LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin4  (drv @ .data+0xe44) */
	{ LUOFU_MUX(0,  4,  "jtag_gpio4"), LUOFU_MUX(16, 10, "eth_act_led2"),
	  LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin5  (drv @ .data+0xe04) */
	{ LUOFU_MUX(0,  5,  "jtag_gpio5"), LUOFU_MUX(16, 11, "eth_act_led3"),
	  LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin6  (drv @ .data+0xdc4) */
	{ LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin7  (drv @ .data+0xd84) */
	{ LUOFU_MUX(0,  7,  "gpio"), LUOFU_MUX(16, 12, "eth_act_led4"),
	  LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin8  (drv @ .data+0xd44) */
	{ LUOFU_MUX(0,  8,  "gpio"), LUOFU_MUX(12, 0,  "avs"),
	  LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin9  (drv @ .data+0xd04) */
	{ LUOFU_MUX(0,  9,  "gpio"), LUOFU_MUX(12, 24, "phy_clk"),
	  LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin10 (drv @ .data+0xcc4) */
	{ LUOFU_MUX(0,  10, "gpio"), LUOFU_MUX(16, 21, "mdio1"),
	  LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin11 (drv @ .data+0xc84) */
	{ LUOFU_MUX(0,  11, "gpio"), LUOFU_MUX(16, 21, "mdio1"),
	  LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin12 (drv @ .data+0xc44) */
	{ LUOFU_MUX(0,  12, "gpio"), LUOFU_MUX(12, 11, "rgmii0"),
	  LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin13 (drv @ .data+0xc04) */
	{ LUOFU_MUX(0,  13, "gpio"), LUOFU_MUX(12, 11, "rgmii0"),
	  LUOFU_MUX(12, 23, "spi"),   LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin14 (drv @ .data+0xbc4) */
	{ LUOFU_MUX(0,  14, "gpio"), LUOFU_MUX(12, 11, "rgmii0"),
	  LUOFU_MUX(12, 23, "spi"),   LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin15 (drv @ .data+0xb84) */
	{ LUOFU_MUX(0,  15, "gpio"), LUOFU_MUX(12, 11, "rgmii0"),
	  LUOFU_MUX(12, 23, "spi"),   LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin16 (drv @ .data+0xb44) */
	{ LUOFU_MUX(0,  16, "gpio"), LUOFU_MUX(12, 11, "rgmii0"),
	  LUOFU_MUX(12, 9,  "uart0"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin17 (drv @ .data+0xb04) */
	{ LUOFU_MUX(0,  17, "gpio"), LUOFU_MUX(12, 11, "rgmii0"),
	  LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin18 (drv @ .data+0xac4) */
	{ LUOFU_MUX(0,  18, "gpio"), LUOFU_MUX(12, 11, "rgmii0"),
	  LUOFU_MUX(12, 10, "uart0"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin19 (drv @ .data+0xa84) */
	{ LUOFU_MUX(0,  19, "gpio"), LUOFU_MUX(12, 11, "rgmii0"),
	  LUOFU_MUX(12, 10, "uart0"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin20 (drv @ .data+0xa44) */
	{ LUOFU_MUX(0,  20, "gpio"), LUOFU_MUX(12, 11, "rgmii0"),
	  LUOFU_MUX(16, 14, "led1_1"), LUOFU_MUX(12, 1, "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin21 (drv @ .data+0xa04) */
	{ LUOFU_MUX(0,  21, "gpio"), LUOFU_MUX(12, 11, "rgmii0"),
	  LUOFU_MUX(16, 13, "led0_1"), LUOFU_MUX(12, 1, "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin22 (drv @ .data+0x9c4) */
	{ LUOFU_MUX(0,  22, "gpio"), LUOFU_MUX(12, 11, "rgmii0"),
	  LUOFU_MUX(12, 9,  "uart0"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin23 (drv @ .data+0x984) */
	{ LUOFU_MUX(0,  23, "gpio"), LUOFU_MUX(12, 11, "rgmii0"),
	  LUOFU_MUX(12, 22, "spi"),   LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin24 (drv @ .data+0x944) */
	{ LUOFU_MUX(0,  24, "gpio"), LUOFU_MUX(12, 15, "i2c"),
	  LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin25 (drv @ .data+0x904) */
	{ LUOFU_MUX(0,  25, "gpio"), LUOFU_MUX(12, 15, "i2c"),
	  LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin26 (drv @ .data+0x8c4) */
	{ LUOFU_MUX(0,  26, "gpio"), LUOFU_MUX(12, 14, "clk_test"),
	  LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin27 (drv @ .data+0x884) */
	{ LUOFU_MUX(0,  27, "gpio"), LUOFU_MUX(16, 17, "usb"),
	  LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin28 (drv @ .data+0x844) */
	{ LUOFU_MUX(0,  28, "gpio"), LUOFU_MUX(16, 16, "usb"),
	  LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin29 (drv @ .data+0x804) */
	{ LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin30 (drv @ .data+0x7c4) */
	{ LUOFU_MUX(0,  30, "gpio"), LUOFU_MUX(12, 2,  "sfc"),
	  LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin31 (drv @ .data+0x784) */
	{ LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin32 (drv @ .data+0x744) */
	{ LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin33 (drv @ .data+0x704) */
	{ LUOFU_MUX(0,  1,  "gpio"), LUOFU_MUX(12, 3,  "sfc"),
	  LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin34 (drv @ .data+0x6c4) */
	{ LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin35 (drv @ .data+0x684) */
	{ LUOFU_MUX(0,  3,  "gpio"), LUOFU_MUX(12, 7,  "uart1"),
	  LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
	/* pin36 (drv @ .data+0x644) */
	{ LUOFU_MUX(4,  3,  "gpio"), LUOFU_MUX(12, 7,  "uart1"),
	  LUOFU_MUX(12, 1,  "null"), LUOFU_MUX(12, 1,  "null"),
	  LUOFU_MUX(12, 1,  "null") },
};

#undef LUOFU_MUX

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

	*pins = luofu_groups[selector].pins;
	*num_pins = luofu_groups[selector].npins;
	return 0;
}

/* Vendor hi_pinctrl_dt_node_to_map (@ .text+0x564) is a thin wrapper that
 * pushes a 5th stack arg 0 and tail-calls pinconf_generic_dt_node_to_map().
 * Vanilla 5.10.201's signature is pinconf_generic_dt_node_to_map(pctldev,
 * np_config, map, num_maps, type), and type 0 is PIN_MAP_TYPE_INVALID, so the
 * equivalent is pinconf_generic_dt_node_to_map_all(). dt_free_map is
 * pinconf_generic_dt_free_map() (the vendor's pinctrl_utils_free_map is not
 * exported to modules in vanilla 5.10). */
static int luofu_dt_node_to_map(struct pinctrl_dev *pctldev,
				struct device_node *np_config,
				struct pinctrl_map **map,
				unsigned int *num_maps)
{
	return pinconf_generic_dt_node_to_map_all(pctldev, np_config, map,
						  num_maps);
}

static const struct pinctrl_ops luofu_pctl_ops = {
	.get_groups_count = luofu_get_groups_count,
	.get_group_name = luofu_get_group_name,
	.get_group_pins = luofu_get_group_pins,
	.dt_node_to_map = luofu_dt_node_to_map,
	.dt_free_map = pinconf_generic_dt_free_map,
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
	 * NO-OP STUB (no register writes): the board is already muxed by the
	 * vendor/U-Boot, so preserving bootloader state is deliberate. When the
	 * fill-in lands, the vendor algorithm (hi_pinmux_set_mux @ .text+0x348)
	 * is, for every pin in luofu_groups[group_selector]:
	 *   for (i = 0; i < 5; i++) {
	 *       e = luofu_pin_mux[pin][i];
	 *       v  = readl(mux_base + e->reg);            // dword-aligned RMW
	 *       v &= ~(1 << e->shift);
	 *       if (!strcmp(e->func, luofu_function_names[func_selector]))
	 *           v |= 1 << e->shift;
	 *       writel(v, mux_base + e->reg);
	 *   }
	 * and, if the group carries mux_mask (jtag_gpio2..5), a mask/value RMW of
	 * mux_base[mux_reg]. All accesses are 32-bit aligned.
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
	/*
	 * NO-OP STUB. Vendor luofu_pin_config_set (@ .text+0xa8) does a dword
	 * RMW bitfield-insert on cfg_base[pin] (cfg_off = 4*pin), dispatching on
	 * config & 0xff (param types 3..8 = bias/drive/slew).
	 */
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
	/* NO-OP STUB. */
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

	dev_info(&pdev->dev,
		 "luofu-pinctrl: %zu pins, %zu groups, %zu functions, %zu mux entries (mapping-table)\n",
		 ARRAY_SIZE(luofu_pins), ARRAY_SIZE(luofu_groups),
		 ARRAY_SIZE(luofu_function_names),
		 ARRAY_SIZE(luofu_pin_mux) * ARRAY_SIZE(luofu_pin_mux[0]));

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
MODULE_DESCRIPTION("Hi5671Y luofu peripheral IOMUX pinctrl (mapping-table)");
