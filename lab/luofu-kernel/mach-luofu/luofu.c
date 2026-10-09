// SPDX-License-Identifier: GPL-2.0
/*
 * Machine descriptor for the Hisilicon Hi5671Y "luofu" SoC
 * (board R116, the Cudy WR3000 v2.0).
 *
 * This is the machine hook of the own-kernel image: match our devicetree
 * (opensource/docs/soc/luofu-r116.dts) and let the generic arm code populate
 * the platform devices the DT declares.
 *
 * .l2c_aux_val / .l2c_aux_mask: replay the AUX-control value the vendor
 * writes (opensource/docs/soc/luofu-r116-pinned.dts:1043, l2c_aux_val =
 * 0x430001) instead of letting the PL310 be reprogrammed with something
 * else. arch/arm/kernel/irq.c:88 only calls l2x0_of_init() when one of the
 * two fields is non-zero, so these two fields are also what turns the L2
 * cache on at all; with no mach match the generic-DT fallback in
 * arch/arm/kernel/devtree.c:216 would program AUX = 0 (mask ~0) instead.
 * The DT's l2-cache node carries the matching reg/cache-level/cache-unified
 * triple, so l2x0_of_init() binds to it.
 *
 * SMP: the DT enable-method is the vendor's "hisilicon,hsan_smp" (pen/smc
 * path) and has no mainline smp_ops yet, so a UP boot with a warning is the
 * expected first-boot posture.
 *
 * dt_compat carries "hsan-luofu": the vendor u-boot's bootfip hands the
 * kernel ITS OWN DT (root compatible "hsan-luofu", /chosen bootargs =
 * "noinitrd cma=0 console=ttyS0,115200 earlycon ..."), not the appended one;
 * without this entry setup_machine_fdt finds no machine and the boot panics
 * before init_early ever runs (observed: the 0xc18 breadcrumb untouched).
 *
 * Breadcrumbs: the sysctrl register 0x10100c18 is inert, writable and
 * survives warm resets (verified live).  Each stage below stamps a distinct
 * value there so a later boot of the vendor system (or a devmem read) shows
 * exactly how far this kernel got:
 *   0xC0DE0001  reach setup_arch / init_early
 *   0xC0DE0002  reach machine init (init_machine)
 *   0xC0DE0003  machine init returned (the watchdog disarm below ran)
 *   0xC0DE0010  early_initcall ran (core init done)
 *   0xC0DE0020  late_initcall ran (all initcalls done, rootfs mount next)
 * A byte-identical value from a previous boot means the kernel died before
 * that stage; the pre-boot sentinel 0xFEEDFACE means no stage ran at all.
 *
 * .restart: the SoC reset magic from the vendor DT (reset-val0 0x55aa5a5a /
 * reset-val1 0xaa55a5a5 at the clr-offset pair 0x6c/0x70).  Without it a
 * panic with panic=5 would hang instead of rebooting, because mainline has
 * no restart driver for this CRG yet.
 *
 * init_machine: disarm the SoC watchdog the vendor u-boot arms ~1 s into
 * every boot.  Nothing in this tree pets it, so a boot is reset after the
 * 30 s timeout; the stop magic pair comes from the vendor DT
 * (luofu-r116-pinned.dts hsan-watchdog) - enable pair at [0x60]=val0 and
 * [0x64]=val1, the stop pair is written to the same two slots.
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of_fdt.h>
#include <linux/printk.h>
#include <linux/reboot.h>
#include <linux/string.h>
#include <asm/byteorder.h>
#include <asm/mach/arch.h>

#define LUOFU_SYSCTRL_BASE	0x10100000
#define LUOFU_CRUMB_OFFSET	0xc18
#define LUOFU_CRUMB_EARLY	0xc0de0001
#define LUOFU_CRUMB_MACH	0xc0de0002
#define LUOFU_CRUMB_DONE	0xc0de0003

#define LUOFU_CRG_BASE		0x14880000
#define HSAN_WDT_EN_OFFSET	0x64	/* vendor DT en-offset "d" (val1/commit word) */
#define HSAN_WDT_STOP0		0xabcd5116
#define HSAN_WDT_STOP1		0xed574447
#define HSAN_WDT_CLR_OFFSET	0x70	/* vendor DT clr-offset "p" (reset val1) */
#define HSAN_WDT_RESET0		0x55aa5a5a
#define HSAN_WDT_RESET1		0xaa55a5a5

static void __init luofu_crumb(u32 value)
{
	void __iomem *sysctrl = ioremap(LUOFU_SYSCTRL_BASE, 0x1000);

	if (!sysctrl)
		return;

	writel(value, sysctrl + LUOFU_CRUMB_OFFSET);
	(void)readl(sysctrl + LUOFU_CRUMB_OFFSET);
	iounmap(sysctrl);
}

static void luofu_restart(enum reboot_mode mode, const char *cmd)
{
	void __iomem *crg = ioremap(LUOFU_CRG_BASE, 0x1000);

	if (crg) {
		writel(HSAN_WDT_RESET0, crg + HSAN_WDT_CLR_OFFSET - 4);
		writel(HSAN_WDT_RESET1, crg + HSAN_WDT_CLR_OFFSET);
		(void)readl(crg + HSAN_WDT_CLR_OFFSET);
		iounmap(crg);
	}

	/* if the reset magic did not take, park until the watchdog or a
	 * power cycle
	 */
	for (;;)
		;
}

static int __init luofu_early_crumb(void)
{
	luofu_crumb(0xc0de0010);
	return 0;
}
early_initcall(luofu_early_crumb);

static int __init luofu_late_crumb(void)
{
	luofu_crumb(0xc0de0020);
	return 0;
}
late_initcall(luofu_late_crumb);

/*
 * The BSP's prebuilt NAND stack binds to compatible = "tri,fmc" and
 * "tri,flashinfo_reserved".  The vendor DT we boot with says "hsan,..." for
 * the same hardware; both rewrites are byte-for-byte equal length, so patch
 * the live blob in place from init_early.  Deliberately minimal code (no
 * string helpers, fixed stores) - the previous libc-flavoured version's
 * linked footprint was large enough to shift the kernel into the
 * layout-dependent death at the MMU-enable (0xC0DE0017).
 */
extern void *initial_boot_params;

static void __init luofu_dt_rewrite(void)
{
	u8 *blob = initial_boot_params;
	u32 totalsize;
	u8 *p, *end;

	if (!blob)
		return;
	if (be32_to_cpup((__be32 *)blob) != 0xd00dfeed)
		return;
	totalsize = be32_to_cpup((__be32 *)(blob + 4));
	end = blob + totalsize - 24;

	for (p = blob; p <= end; p++) {
		if (!(p[0] == 'h' && p[1] == 's' && p[2] == 'a' && p[3] == 'n'))
			continue;
		if (p[4] == ',' && p[5] == 'f' && p[6] == 'm' && p[7] == 'c') {
			/* "hsan,fmc" (8 incl NUL) -> "tri,fmc" (8 incl NUL) */
			p[0] = 't'; p[1] = 'r'; p[2] = 'i'; p[3] = ',';
			p[4] = 'f'; p[5] = 'm'; p[6] = 'c'; p[7] = 0;
			continue;
		}
		if (p[4] == ',' && p[5] == 'f' && p[6] == 'l' && p[7] == 'a' &&
		    p[8] == 's' && p[9] == 'h' && p[10] == 'i' && p[11] == 'n' &&
		    p[12] == 'f' && p[13] == 'o' && p[14] == '_') {
			/* "hsan,flashinfo_reserved" (24) -> "tri,flashinfo_reserved" (23);
			 * the old NUL at p[23] still terminates the shorter string. */
			p[0] = 't'; p[1] = 'r'; p[2] = 'i'; p[3] = ',';
			p[4] = 'f'; p[5] = 'l'; p[6] = 'a'; p[7] = 's';
			p[8] = 'h'; p[9] = 'i'; p[10] = 'n'; p[11] = 'f';
			p[12] = 'o'; p[13] = '_'; p[14] = 'r'; p[15] = 'e';
			p[16] = 's'; p[17] = 'e'; p[18] = 'r'; p[19] = 'v';
			p[20] = 'e'; p[21] = 'd'; p[22] = 0;
		}
	}
}

static void __init luofu_early(void)
{
	luofu_dt_rewrite();
	luofu_crumb(LUOFU_CRUMB_EARLY);
}

static void __init luofu_init_machine(void)
{
	void __iomem *crg;

	luofu_crumb(LUOFU_CRUMB_MACH);

	crg = ioremap(LUOFU_CRG_BASE, 0x1000);
	if (crg) {
		writel(HSAN_WDT_STOP0, crg + HSAN_WDT_EN_OFFSET - 4);
		writel(HSAN_WDT_STOP1, crg + HSAN_WDT_EN_OFFSET);
		(void)readl(crg + HSAN_WDT_EN_OFFSET);	/* flush the write pair */
		iounmap(crg);
		pr_info("luofu: hsan watchdog disarmed (stop magic %08x %08x)\n",
			HSAN_WDT_STOP0, HSAN_WDT_STOP1);
	}

	luofu_crumb(LUOFU_CRUMB_DONE);
}

/* layout-perturbation probe (part of the mcr-lottery experiment) */
static const char luofu_layout_probe[] __used =
	"perturb-0123456789-0123456789-0123456789-0123456789-0123456789" "pad1";

static const char *const luofu_dt_compat[] __initconst = {
	"hisilicon,luofu-r116",
	"hisilicon,luofu",
	"hsan-luofu",
	NULL,
};

DT_MACHINE_START(LUOFU, "HiSilicon Hi5671Y (luofu)")
	.dt_compat	= luofu_dt_compat,
	.init_early	= luofu_early,
	.init_machine	= luofu_init_machine,
	.restart	= luofu_restart,
	.l2c_aux_val	= 0x430001,
	.l2c_aux_mask	= ~0,
MACHINE_END
