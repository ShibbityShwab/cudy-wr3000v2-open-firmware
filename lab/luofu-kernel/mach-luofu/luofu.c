// SPDX-License-Identifier: GPL-2.0
/*
 * Machine descriptor for the Hisilicon Hi5671Y "luofu" SoC
 * (board R116, the Cudy WR3000 v2.0).
 *
 * This is the machine hook of the first own-kernel image: match our
 * devicetree (opensource/docs/soc/luofu-r116.dts) and let the generic arm
 * code populate the platform devices the DT declares. There is deliberately
 * no .init_machine, no map_io and no register access here - the vendor's
 * hi_* modules are what touched the SoC's CRG / pinctrl / PCIe windows, and
 * none of them are in this tree. A boot of this image therefore proves the
 * image shape (zImage + our DTB in the kernelb tail) and the console, not
 * the bring-up of any block that still needs a ported driver.
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
 */
#include <linux/init.h>
#include <asm/mach/arch.h>

static const char *const luofu_dt_compat[] __initconst = {
	"hisilicon,luofu-r116",
	"hisilicon,luofu",
	NULL,
};

DT_MACHINE_START(LUOFU, "HiSilicon Hi5671Y (luofu)")
	.dt_compat	= luofu_dt_compat,
	.l2c_aux_val	= 0x430001,
	.l2c_aux_mask	= ~0,
MACHINE_END
