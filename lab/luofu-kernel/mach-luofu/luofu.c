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
#include <linux/printk.h>
#include <linux/reboot.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/string.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/mm.h>		/* high_memory, swapper_pg_dir */
#include <asm/pgtable.h>		/* pgd_offset_k, pmd_offset, pgd_val, pmd_val */
#include <asm/memory.h>		/* VMALLOC_START, VMALLOC_END */
#include <asm/mach/arch.h>

#define LUOFU_SYSCTRL_BASE	0x10100000
#define LUOFU_CRUMB_OFFSET	0xc18
#define LUOFU_BOOTREG_OFFSET	0xc08	/* slot selector: low byte 0x10 = A pair, 0x11 = B pair */
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
/* vendor DT set-offset "P" (0x50) and the ARM pair, enable-val0/1 */
#define HSAN_WDT_SET_OFFSET	0x50
#define HSAN_WDT_ARM0		0x4f4e5452	/* "ONTR" */
#define HSAN_WDT_ARM1		0x12345116

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

		/*
		 * BACKSTOP, and the reason this function used to be able to strand the board.
		 *
		 * The reset pair above is the vendor's own reset-val0/1 written to clr-offset, so it is the
		 * right thing to try first - but if it does not take, the old code parked in `for (;;) ;`
		 * with the SoC watchdog already DISARMED by init_machine. Nothing could then recover the
		 * box: no timer, no watchdog, no reset - only a hand on the power.
		 *
		 * So arm the watchdog before parking. Its own reset is a hardware event, independent of
		 * anything this kernel does from here on, and the boot it triggers runs u-boot again, which
		 * reads the slot selector the fallback arm has already flipped - i.e. it lands on the other
		 * pair. Values are the vendor DT's: enable-val0/1 at set-offset "P".
		 */
		writel(HSAN_WDT_ARM0, crg + HSAN_WDT_SET_OFFSET);
		writel(HSAN_WDT_ARM1, crg + HSAN_WDT_SET_OFFSET + 4);
		(void)readl(crg + HSAN_WDT_SET_OFFSET + 4);
		iounmap(crg);
	}

	pr_emerg("luofu: restart magic did not take - parking; the watchdog is armed and will do it\n");

	for (;;)
		;
}

static int __init luofu_early_crumb(void)
{
	luofu_crumb(0xc0de0010);
	return 0;
}
early_initcall(luofu_early_crumb);

/*
 * The ring at 0x80603000 is only 16 KiB (0x80603000-0x80607000) and a boot writes more than that,
 * so its visible window starts around 4.0 s - at the FMC's messages.  In link order the CRG's
 * initcall (c0e31e14) runs BEFORE the FMC's (c0e31e4c) and gmac/mdio sit just after, so anything
 * those drivers print is OLDER than the ring window and simply cannot be read back.  "No luofu-crg
 * line" is therefore not evidence the CRG did not probe; the instrument is lossy exactly where the
 * question is.
 *
 * A late_initcall runs late enough to land INSIDE the window (the FMC's late diag at ~4.0 s is
 * visible), so the binding state is reported from here instead.  Three answers, in the order they
 * discriminate:
 *   - crg driver registered at all?      driver_find on the platform bus
 *   - did the NIC reach userspace?       eth0 on init_net
 *   - did the clock provider resolve?    of_clk_get on the crg node
 */
static int __init luofu_late_crumb(void)
{
	struct device_driver *drv;
	struct net_device *nd;
	struct device_node *crg_np;
	struct clk *clk;
	u32 mask;
	const char *clkres = "n/a";
	void __iomem *sc;

	/*
	 * FIRST, before anything that can fail or fault: prove this initcall ran at all, into 0xC1C.
	 *
	 * 0xC18 is useless for that - the FMC's own late_initcall_sync deposits 0xc0de502d there and runs
	 * afterwards, so a crumb at 0xC18 is always overwritten and "did my code run" is unanswerable.
	 * 0xC1C is WORSE, and this cost several fires: it is the FMC's PAYLOAD cell - luofu_fmc_crumb()
	 * writes it on EVERY tick (every 5 s), so anything deposited there is overwritten within seconds
	 * and reads back 0.  A controlled test settled it: written from the VENDOR (which runs no FMC
	 * driver) 0xC1C held its value across a reboot, which is exactly why it looked usable.
	 *
	 * 0xC8C is neither: verified writable, verified sticky across a vendor reboot, and touched by
	 * nothing in this tree.  THIS is the cell to deposit answers in.
	 *
	 * 0xC0DE8C00 = reached this point.  The low bits are OR'd in below as each answer resolves.
	 */
	sc = ioremap(LUOFU_SYSCTRL_BASE, 0x1000);
	if (sc) {
		writel(0xc0de8c00, sc + 0xc8c);
		(void)readl(sc + 0xc8c);
	}

	drv = driver_find("luofu-crg", &platform_bus_type);

	nd = dev_get_by_name(&init_net, "eth0");

	crg_np = of_find_compatible_node(NULL, NULL, "hisilicon,luofu-crg");
	if (crg_np) {
		clk = of_clk_get(crg_np, 0);
		if (IS_ERR(clk))
			clkres = (PTR_ERR(clk) == -EPROBE_DEFER) ? "defer" : "err";
		else
			clkres = "ok";
	} else {
		clkres = "nonode";
	}

	/*
	 * bit8 = crg driver registered, bit4 = eth0 exists, bit0 = the crg clock provider answered.
	 */
	mask = 0xc0de8c00;
	if (drv)
		mask |= 0x100;
	if (nd)
		mask |= 0x010;
	if (!strcmp(clkres, "ok"))
		mask |= 0x001;

	if (sc) {
		writel(mask, sc + 0xc8c);
		(void)readl(sc + 0xc8c);
		iounmap(sc);
	}

	/*
	 * THE PAGE-TABLE PROBE.
	 *
	 * Measured 2026-10-13: our kernel Oopses at ~13 s inside console_on_rootfs() -> filp_open()
	 * -> tty_open -> n_tty_open -> vzalloc -> map_kernel_range_noflush, faulting on a PAGE-TABLE
	 * LOAD, and show_pte() then faults WALKING those same tables.  A faulting walker means the
	 * tables are wrong, not one address.  Two possibilities, and they need different fixes:
	 *
	 *   (a) the PGD entry for the vmalloc region is ABSENT  -> vmalloc's mapping was never set up
	 *   (b) it is PRESENT but the pmd/pte is bogus         -> the tables were written wrong
	 *
	 * This has to be reported from INSIDE our kernel: the vendor boot rebuilds the tables, so a
	 * post-mortem /dev/mem read of that page returns the VENDOR's, not ours (measured - 0x80604000
	 * read all zeros under the vendor while our log ring at 0x80603000 was still intact).
	 *
	 * It is printed rather than crumbed because the log RING survives the warm watchdog reset and
	 * the vendor kernel can read it back with `dd if=/dev/mem` - the free post-mortem channel.
	 * Keep it to ONE line: the ring is only ~16 KiB and the early lines are already being lost.
	 */
	{
		unsigned long vs = (unsigned long)VMALLOC_START;
		unsigned long fa = 0xc97e8000UL;	/* the address in the Oops registers */
		pgd_t *p_vs = pgd_offset_k(vs);
		pgd_t *p_fa = pgd_offset_k(fa);
		u32 e_vs = pgd_val(*p_vs);
		u32 e_fa = pgd_val(*p_fa);
		u32 enc;
		void __iomem *sc2;

		/*
		 * BOTH CHANNELS, BECAUSE THE RING EVICTS.
		 *
		 * The printk alone did NOT survive: the ring holds only the newest ~16 KiB and the Oops
		 * trace is longer than that, so the line was pushed out before the crash - measured, it
		 * was absent while the crash trace was present.  So the ANSWER also goes into a crumb cell,
		 * which is sticky and does not get evicted.
		 *
		 * A crumb is 32 bits, so it carries the discriminating facts, not prose:
		 *   bit0            pgd entry for VMALLOC_START is ZERO   (mapping never set up)
		 *   bit1            pgd entry for 0xc97e8000 is ZERO      (the faulting address)
		 *   bits 8..15      high_memory >> 24                    (0xC8 for 128 MiB at PAGE_OFFSET)
		 *   bits 16..31     VMALLOC_START >> 24
		 *
		 * Reading it: bit0=1 -> the vmalloc PGD was never populated (an init/bounds problem).
		 *             bit0=0 -> the table EXISTS and is wrong (a corruption problem).
		 * Those need different fixes, which is the whole point of measuring instead of guessing.
		 */
		enc = (e_vs ? 0u : 1u) | (e_fa ? 0u : 2u)
		    | (((u32)((unsigned long)high_memory >> 24)) & 0xffu) << 8
		    | (((u32)(vs >> 24)) & 0xffffu) << 16;

		sc2 = ioremap(LUOFU_SYSCTRL_BASE, 0x1000);
		if (sc2) {
			writel(enc, sc2 + 0xc9c);	/* stickier than the ring, and never evicted */
			(void)readl(sc2 + 0xc9c);
		}

		/*
		 * REPRODUCE THE ALLOCATION IN OUR OWN CODE.
		 *
		 * The root table is exonerated: a previous probe READ swapper_pg_dir successfully (marker
		 * 0xC0DE9A02), so init_mm.pgd is mapped and the fault is inside vmap_p4d_range - on ARM
		 * (folded p4d/pud) that is vmap_pmd_range -> pmd_alloc() -> pmd_alloc_one().
		 *
		 * Calling pmd_alloc() HERE buys one thing the trace cannot give: if it faults, the Oops
		 * names luofu_late_crumb, a function I control, instead of the generic mapper.  The stage is
		 * recorded as it goes so the last surviving value says how far it got.
		 *
		 * CELL CHOICE MATTERS AND COST A READING: the previous probe put its results in 0xC90/0xC94,
		 * and 0xC94 came back holding 0xC0DE6006 - the GMAC's crumb.  That is OUR OWN deferral fix
		 * at work: the gmac now defers and re-probes AFTER mdio0, and that retry can land after this
		 * late_initcall, so it overwrote the cell.  0xC8C is this mach's own cell and 0xC9C already
		 * survived a whole crash; nothing else writes either.
		 */
		if (sc2) {
			unsigned long a = 0xc97e8000UL;
			u32 st = 0xc0de8b00;

			writel(st | 1, sc2 + 0xc8c);	/* 1: about to call pud_offset/pmd_alloc */
			(void)readl(sc2 + 0xc8c);
			{
				p4d_t *q = p4d_offset(pgd_offset_k(a), a);
				pud_t *u;
				pmd_t *mm;

				writel(st | 2, sc2 + 0xc8c);	/* 2: p4d_offset returned */
				(void)readl(sc2 + 0xc8c);

				u = pud_offset(q, a);

				writel(st | 3, sc2 + 0xc8c);	/* 3: pud_offset returned */
				(void)readl(sc2 + 0xc8c);

				mm = pmd_alloc(&init_mm, u, a);

				writel(st | 4 | (mm ? 8u : 0u), sc2 + 0xc8c);
				if (mm) {
					writel((u32)pmd_val(*mm), sc2 + 0xc9c);
					(void)readl(sc2 + 0xc9c);
				}
			}
			writel(st | 0xf, sc2 + 0xc8c);	/* 0xF: the whole path completed */
			(void)readl(sc2 + 0xc8c);

			/*
			 * DOES THE KERNEL KNOW WHERE IT IS?
			 *
			 * The allocation above SUCCEEDED (stage 0xF, pmd non-NULL), so nothing is structurally
			 * broken - the same call faults only later, inside kernel_init_freeable.  The suspect is
			 * the physical<->virtual map being off by the difference between the kernel's LINK base
			 * and where it was LOADED:
			 *
			 *     DTB memory node 0x80500000 + TEXT_OFFSET 0x608000 = 0x80b08000 (expected)
			 *     the uImage loads at                                 0x80608000 (actual)
			 *     difference                                          0x500000
			 *
			 * If PHYS_OFFSET disagrees with reality, memblock reserves the wrong ranges, free_initmem()
			 * releases pages it does not own, and the allocator can then hand out a page overlapping the
			 * page tables - corrupting exactly those tables at the first vmalloc that needs a new pmd.
			 *
			 * virt_to_phys() answers it directly: swapper_pg_dir's virtual is 0xC0604000, so a correct
			 * map must return 0x80604000.  Anything else IS the bug.
			 */
			{
				phys_addr_t pg_phys = virt_to_phys((void *)swapper_pg_dir);
				phys_addr_t me_phys = virt_to_phys((void *)luofu_late_crumb);

				writel((u32)pg_phys, sc2 + 0xc8c);	/* expect 0x80604000 */
				(void)readl(sc2 + 0xc8c);
				writel((u32)me_phys, sc2 + 0xc9c);	/* our own phys, for cross-check */
				(void)readl(sc2 + 0xc9c);
			}
			iounmap(sc2);
		}

		pr_info("LUOFU-PG high=%08lx vs=%08lx ve=%08lx pgd=%08lx vs_e=%08x fa_e=%08x\n",
			(unsigned long)high_memory, vs,
			(unsigned long)VMALLOC_END, (unsigned long)swapper_pg_dir,
			e_vs, e_fa);
	}

	return 0;
}
late_initcall(luofu_late_crumb);

/*
 * ONE ioremap, CRUMB FIRST, fallback second.
 *
 * The crumb is the instrument that says how far this kernel got, so it is written before anything
 * that could fail - including the fallback below.  Ordering it the other way put the safety ahead of
 * the diagnostic and cost a fire: the board came back with the selector flipped (so this function ran)
 * but C18 still reading the poison, which cannot distinguish "died between the two writes" from
 * "the fallback itself faulted".  One mapping for both also removes a second ioremap/iounmap pair
 * from the earliest, most fragile code in the kernel.
 *
 * THE FALLBACK: u-boot selects the boot pair from the low byte of sysctrl 0x10100c08 and uses a
 * RECORDED value verbatim (0x10 -> A pair kernela+rootfsa, 0x11 -> B pair kernelb+rootfsb), only
 * re-validating when that byte reads 0.  That is why a kernel failing in the selected slot is retried
 * forever instead of falling through.  Flipping the low bit here makes the NEXT boot select the OTHER
 * pair, so a failing test kernel hands the board back to the vendor slot by itself.
 */
static void __init luofu_early(void)
{
	void __iomem *sysctrl = ioremap(LUOFU_SYSCTRL_BASE, 0x1000);
	u32 val;

	if (!sysctrl)
		return;

	writel(LUOFU_CRUMB_EARLY, sysctrl + LUOFU_CRUMB_OFFSET);
	(void)readl(sysctrl + LUOFU_CRUMB_OFFSET);

	val = readl(sysctrl + LUOFU_BOOTREG_OFFSET);
	if ((val & 0xff) == 0x10 || (val & 0xff) == 0x11) {
		writel(val ^ 1u, sysctrl + LUOFU_BOOTREG_OFFSET);
		(void)readl(sysctrl + LUOFU_BOOTREG_OFFSET);
	}

	iounmap(sysctrl);
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
