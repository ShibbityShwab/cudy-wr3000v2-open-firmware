// SPDX-License-Identifier: GPL-2.0
/*
 * Machine descriptor for the Hisilicon Hi5671Y "luofu" SoC
 * (board R116, the Cudy WR3000 v2.0).
 *
 * This is the machine hook of the first own-kernel image: match our
 * devicetree (opensource/docs/soc/luofu-r116.dts) and let the generic arm
 * code populate the platform devices the DT declares.
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
 * expected first-boot posture. The vendor bootargs already pass maxcpus=2
 * nr_cpus=2 - harmless until the SMP port lands (stage 1).
 *
 * init_machine: disarm the SoC watchdog the vendor u-boot arms ~1 s into
 * every boot.  Nothing in this tree pets it, so a boot is reset after the
 * 30 s timeout (the "all LEDs flash" loop); the stop magic pair below comes
 * from the vendor DT (luofu-r116-pinned.dts hsan-watchdog) and a surviving
 * boot is the first proof our kernel runs its own C past the loader.
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/printk.h>
#include <asm/mach/arch.h>

#define LUOFU_CRG_BASE		0x14880000
#define HSAN_WDT_EN_OFFSET	0x64	/* vendor DT en-offset "d" */
#define HSAN_WDT_STOP0		0xabcd5116
#define HSAN_WDT_STOP1		0xed574447

static void __init luofu_wdt_disarm(void)
{
	void __iomem *crg = ioremap(LUOFU_CRG_BASE, 0x1000);

	if (!crg)
		return;

	writel(HSAN_WDT_STOP0, crg + HSAN_WDT_EN_OFFSET);
	writel(HSAN_WDT_STOP1, crg + HSAN_WDT_EN_OFFSET + 4);
	(void)readl(crg + HSAN_WDT_EN_OFFSET);	/* flush the write pair */

	pr_info("luofu: hsan watchdog disarmed (stop magic %08x %08x)\n",
		HSAN_WDT_STOP0, HSAN_WDT_STOP1);

	iounmap(crg);
}

static const char *const luofu_dt_compat[] __initconst = {
	"hisilicon,luofu-r116",
	"hisilicon,luofu",
	NULL,
};

DT_MACHINE_START(LUOFU, "HiSilicon Hi5671Y (luofu)")
	.dt_compat	= luofu_dt_compat,
	.init_machine	= luofu_wdt_disarm,
	.l2c_aux_val	= 0x430001,
	.l2c_aux_mask	= ~0,
MACHINE_END
