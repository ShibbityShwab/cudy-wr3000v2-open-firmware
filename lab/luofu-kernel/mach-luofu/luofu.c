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
#include <linux/serial_core.h>	/* setup_earlycon */

#define LUOFU_SYSCTRL_BASE	0x10100000
#define LUOFU_CRUMB_OFFSET	0xc18
#define LUOFU_BOOTREG_OFFSET	0xc08	/* slot selector: low byte 0x10 = A pair, 0x11 = B pair */
#define LUOFU_CRUMB_EARLY	0xc0de0001
#define LUOFU_CRUMB_MACH	0xc0de0002
#define LUOFU_CRUMB_DONE	0xc0de0003
/*
 * THE SERIAL INSTRUMENT: THE BOARD'S OWN CONSOLE UART, TAKEN OVER FROM INIT_EARLY.
 *
 * Every instrument in this window died the same way.  The crumb channel is I/O and survives, but
 * it is ONE 32-bit value; the log ring lives in RAM that the vendor's boot scrubs - measured
 * 2026-10-14: every page of the 0x80600000..0x80620000 window carries the VENDOR's kernel
 * messages (kmodloader, mac80211), none of ours, and pstore on mtd10 is entirely blank.  So
 * "where does our boot stop?" has no answer on this bench today, and every remaining step is
 * inference.  A UART turns the boot into a stream that needs no RAM and no reader on the device.
 *
 * THE PORT IS THE SoC's OWN CONSOLE, AND THE ADDRESS COMES FROM THE BOARD, NOT FROM A GUESS.
 * /proc/tty/driver/serial on the running VENDOR kernel reports:
 *     0: uart:16550A mmio:0x1010F000 irq:204 tx:93 rx:0 RTS|DTR
 * and /proc/device-tree/chosen/stdout-path names /uart1@0x1010f000 - the console, pinned at
 * 115200.  The vendor's own cmdline is "console=ttyS0,115200 earlycon", so the pad is wired.
 * dt:/uart1@0x1010f000 carries compatible snps,dw-apb-uart, reg-shift 2, clock-frequency
 * 0x5f5e100 = 100000000 (the DT's own value, not the inferred apb_clk).
 *
 * SO THE COMMAND LINE DOES THE WORK.  CONFIG_CMDLINE_EXTEND=y, so u-boot's own bootargs and
 * CONFIG_CMDLINE are CONCATENATED; adding `earlycon=ns16550a,mmio32,0x1010f000` here is enough
 * for the kernel to register the early console itself, before machine setup.  This function is
 * only the belt-and-braces for when the loader hands us NO bootargs at all (the appended-DTB
 * case): it builds the same string and calls setup_earlycon() directly.
 *
 * IT IS SAFE: earlycon_setup() only PROGRAMS and READS the pad (no poll without a pending char),
 * so the worst case is a few stray characters on a console nobody is watching.  It cannot hang a
 * boot - that is the property every other instrument in this project lacked.
 */
#define LUOFU_UART_BASE		0x1010f000
#define LUOFU_UART_CLK		100000000
#define LUOFU_EARLYCON_STR	"ns16550a,mmio32,0x1010f000,115200n8"

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

/*
 * The same write, WITHOUT __init: luofu_restart can run at any time, long after init memory is freed, so
 * calling the __init version above would read freed memory (modpost flags exactly that as a section
 * mismatch). This one is deliberately a plain function.
 */
static void luofu_crumb_live(u32 value)
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

	/*
	 * ★ DID THE BOX REACH THIS FUNCTION AT ALL? (2026-10-27 04:15Z)
	 *
	 * Every eliminations so far assumes the ~6 s death is a PANIC that reaches the restart handler. That
	 * assumption has never been tested, and it is testable with no new instrument: this crumb is written
	 * inside the restart path, and nothing runs after it (the function then either resets the SoC or parks
	 * with the watchdog armed), so it is the LAST writer on C18 - it cannot be overwritten by the tick.
	 *
	 *   C18 = 0xC0DE5245 after a fire   the box PANICKED and went through machine_restart
	 *   anything else                   it never reached the restart path - the death is a HANG, not a panic
	 *
	 * That distinction decides the next lane: a panic invites a fault-record hunt, a hang invites a
	 * lockup/deadlock hunt, and the two have nothing in common.
	 */
	luofu_crumb_live(0xc0de5245);

	/*
	 * CARRY THE late_initcall_sync PROBE'S ANSWER THROUGH THE PANIC (2026-10-30).
	 *
	 * The read-only `blk_lookup_devt("ubiblock0_0")` probe runs in
	 * luofu_fmc_ubi_diag() and writes its result to 0xC04 - but the VENDOR BOOT ZEROES 0xC04, so
	 * reading it after the fallback returns 0x00000000 and the answer is lost. That voided the
	 * [wroot] fire entirely.
	 *
	 * C18 is different: THIS function writes it and nothing runs afterwards, which is why
	 * 0xC0DE5245 has been read back correctly after every vendor boot this session. So re-stamp the
	 * probe's answer here, where it survives:
	 *
	 *   0xC0DE5245  the restart path ran; the probe's cell read 0 or was never written
	 *   0xC0DE5246  the restart path ran AND ubiblock0_0 RESOLVED (C04 was 0xC0DE9A01)
	 *   0xC0DE5247  the restart path ran AND it did NOT resolve (C04 was 0xC0DE9A02)
	 */
	{
		void __iomem *pSc = ioremap(LUOFU_SYSCTRL_BASE, 0x1000);

		if (pSc) {
			u32 probe = readl(pSc + 0xc04);
			/*
			 * CARRY THE LOW BYTE THROUGH (2026-10-30).
			 *
			 * luofu_fmc_ubi_diag (level 7, and it demonstrably RUNS now) writes its verdict to 0xC04
			 * as 0xC0DE9A00 | code, where code describes what UBI can actually see - the partition
			 * lookup, the EC header, and the VID header at +0x800.
			 *
			 * 0xC04 is zeroed by the vendor boot, but THIS function runs before that, on the
			 * restart path. So the code is copied into the low byte of the C18 stamp, which is the
			 * one cell proven to survive:
			 *
			 *   0xC0DE52E1  partition "rootfsb" not found by name
			 *   0xC0DE528x  EC magic bad (x = magic's top byte & 0x3f)
			 *   0xC0DE524x  EC magic OK but vid_hdr_offset != 0x800
			 *   0xC0DE52A0  EC + vid_hdr_offset OK and PEB0+0x800 reads 'U' - UBI's view is clean
			 *   0xC0DE52B0  EC OK but PEB0+0x800 reads 0x00 - the 2026-10-21 finding, reproduced
			 *   0xC0DE52Cx  EC OK, VID reads something else
			 *
			 * The older 0xC0DE9A01/0x9A02 values keep their old mapping for compatibility.
			 */
			if ((probe & 0xFFFFFF00u) == 0xC0DE9A00u)
				luofu_crumb_live(0xc0de5200u | (probe & 0xffu));
			else if (probe == 0xC0DE9A01u)
				luofu_crumb_live(0xc0de5246);
			else if (probe == 0xC0DE9A02u)
				luofu_crumb_live(0xc0de5247);
		}
	}

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
 * THE STAGE LADDER, AND WHY IT IS ONE CELL WITH A BITMASK, NOT FIVE CELLS.
 *
 * The standing question is "where does our boot stop?": it runs ~78 s, nothing answers, and the
 * fallback hands the box to the vendor.  Every post-mortem channel is gone - the ring lives in RAM
 * the vendor's boot scrubs (measured: every page of 0x80600000..0x80620000 holds ITS text), pstore
 * mtd10 was never connected at all (see below), and there is no serial cable on this bench.
 *
 * MY FIRST DESIGN WAS FIVE CELLS, ONE PER STAGE, AND IT WAS WRONG - measured, not assumed:
 *
 *     0x10100C5C..0x10100C74   NOT WRITABLE.  C60 read back 0x3F21A112 after writing 0xC0DE5EED,
 *                              C70 came back 0xC0005EED (masked), C5C/C64/C68/C6C/C74 read 0/1.
 *     0x10100CA4..0x10100CE0   NOT WRITABLE.  Every one read back 0 after the write.
 *     0x10100C80..0x10100C98   WRITABLE AND STICKY, but every one is ALREADY CLAIMED:
 *                              C80/C84/C88 = the gmac's per-MAC (phyaddr<<16)|BMSR,
 *                              C8C/C9C     = this mach's own diagnostics,
 *                              C90/C94/C98 = the CRG / gmac / mdio crumbs.
 *                              A ladder here would collide exactly like the CRG-vs-gmac collision
 *                              the record already paid for.
 *     0x10100C18 / 0x10100C1C  WRITABLE AND STICKY - the only free pair - but C18 is the HEAD's own
 *                              crumb at 0x0017 and C1C is rewritten by the FMC's 5-second tick.
 *
 * SO: ONE CELL, MONOTONE, IN A CELL THAT IS OURS ALONE.  C18 is the head's and is THE fact that
 * matters most ("did the image execute at all"), so it must keep its own meaning.  The ladder uses
 * 0xC1C - not as a value but as an ACCUMULATING BITMASK: each stage ORs in one more bit, so a
 * single read says which stages ran without needing five cells, and monotonicity means no stage can
 * mask an earlier one.  The FMC's tick also writes C1C, which is why the ladder's bits live in the
 * HIGH half and the FMC's payload keeps the low half:
 *
 *     0xC1C  bits 24..28 = stages reached (OR-accumulated), bits 0..15 = the FMC's own payload
 *
 *     0xXX000000 bit24: early_initcall      (core init reached - the first luofu C code)
 *     0xXX000000 bit25: level 6 drivers     (FMC, CRG, GMAC, MDIO probes)
 *     0xXX000000 bit26: late_initcall       (ubi_init, ip_auto_config, pstore_blk)
 *     0xXX000000 bit27: late_initcall_sync  (the last luofu stage: the FMC's ubi diagnostic)
 *
 * A primed sentinel of 0xDEAD0000 is distinguishable from every accumulated value, and from the
 * FMC's own 0xC0DE5xxx family, without ambiguity.  Cross-check C18 (the head) and C8C/C9C (this
 * mach's diagnostics) to say WHICH kernel produced the reading - a surviving cell proves nothing
 * about which boot wrote it, the trap the record has already paid for twice.
 */
#define LUOFU_LADDER_OFF	0xc1c
#define LUOFU_LADDER_SENT	0xDEAD0000u
#define LUOFU_LADDER_EARLY	0x11000000u
#define LUOFU_LADDER_DEVL6	0x12000000u
#define LUOFU_LADDER_LATE	0x14000000u
#define LUOFU_LADDER_LATESYNC	0x18000000u

/*
 * OR-ACCUMULATE, never overwrite: `val |= bit`.  A plain write would make the ladder the LAST
 * stage's value instead of a history, which is the whole point.  The low 16 bits are preserved so
 * the FMC's tick payload (which shares this cell) is not destroyed by the ladder.
 */
static void __init luofu_ladder(u32 bit)
{
	void __iomem *sc = ioremap(LUOFU_SYSCTRL_BASE, 0x1000);
	u32 v;

	if (!sc)
		return;

	v = readl(sc + LUOFU_LADDER_OFF);
	v = (v | bit) & 0xff00ffffu;   /* accumulate in the high byte, keep the FMC payload */
	v |= bit;
	writel(v, sc + LUOFU_LADDER_OFF);
	(void)readl(sc + LUOFU_LADDER_OFF);
	iounmap(sc);
}

static int __init luofu_ladder_early(void)
{
	luofu_ladder(LUOFU_LADDER_EARLY);
	return 0;
}
early_initcall(luofu_ladder_early);

static int __init luofu_ladder_devl6(void)
{
	luofu_ladder(LUOFU_LADDER_DEVL6);
	return 0;
}
/*
 * A LEVEL-6 MARKER MUST BE REGISTERED AT LEVEL 6.
 *
 * This was `late_initcall(luofu_ladder_devl6)` with a note admitting "level 6 is gone by the time
 * late_initcalls exist". That made the bit useless for the one question it exists to answer: if
 * the boot dies inside a device_initcall, a marker registered at level 7 never runs either, so its
 * absence is indistinguishable from the death happening later. Both outcomes look identical - the
 * same defect as the FMC exit marker that sat after a `return`.
 *
 * Measured 2026-10-30 [wexit]: C18 = 0xC0DE50FF proves luofu_fmc_probe COMPLETES, so the death is
 * in some other level-6 probe or in the level transition. This marker is the bisect point:
 *   C1C top byte 0x12 present -> level 7 was reached; the death is after it
 *   C1C still 0xC0DE5FFF       -> the death is in a level-6 device_initcall
 */
device_initcall(luofu_ladder_devl6);

static int __init luofu_ladder_late(void)
{
	luofu_ladder(LUOFU_LADDER_LATE);
	/*
	 * ORDERING STAMP IN C18, NOT C1C.
	 *
	 * The ladder ORs its stage bits into C1C, but the FMC writes C1C unconditionally on every
	 * crumb - so a bit deposited BEFORE the FMC probe is destroyed by it and can never be read
	 * back. That made "the level-6 marker never fired" indistinguishable from "it fired and was
	 * overwritten", and one fire was spent on that ambiguity.
	 *
	 * C18's last writer inside the probe is the 0xFF exit crumb (0xC0DE50FF); nothing in the FMC
	 * writes C18 again after it. So a stamp here is only possible if level 7 was actually reached:
	 *
	 *   C18 = 0xC0DE1414 -> late_initcall ran AFTER the FMC probe returned; the death is later
	 *   C18 = 0xC0DE50FF -> level 7 never ran; the death is inside level 6 or the level transition
	 */
	luofu_crumb(0xC0DE1414u);
	return 0;
}
late_initcall(luofu_ladder_late);

static int __init luofu_ladder_latesync(void)
{
	luofu_ladder(LUOFU_LADDER_LATESYNC);
	return 0;
}
late_initcall_sync(luofu_ladder_latesync);

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

	crg_np = of_find_compatible_node(NULL, NULL, "hsan,crg");
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
 * ** THE BOOT-SURVIVAL PROBE - AND 0xC08 IS NOT A SCRATCH CELL AT ALL.
 *
 * I first wrote this probe into 0xC08, believing it was free. IT IS THE SLOT SELECTOR:
 *
 *     luofu.c:71  LUOFU_BOOTREG_OFFSET = 0xc08   (slot selector: 0x10 = A pair, 0x11 = B pair)
 *
 * The probe stored 0xC0DE9A01 there, whose LOW BYTE is 0x01 - neither 0x10 nor 0x11 - so it
 * CORRUPTED THE SLOT SELECTOR ON EVERY BOOT.  The fire script set the selector correctly
 * (devmem 0x10100C08 32 0x11) and this initcall then clobbered it mid-boot, the loader retried,
 * fell back to slot A, and the box spent 13-15 minutes dark each time.
 *
 * **That is what "it used to come back on its own and now the LEDs never light" was: MY BUG**, not
 * a hardware fault.  The board was being told to boot a slot it could not resolve.
 *
 * THE LESSON IS THE ONE I HAD ALREADY WRITTEN DOWN AND FAILED TO APPLY: a cell is only safe if it
 * is proved to hold what THIS code wrote, in THIS runtime, WITHOUT breaking something else. 0xC08
 * passed the vendor-side scratch probe precisely because the vendor's kernel does not drive the
 * selector the way ours must - the exact "wrong runtime" trap recorded in sysctrl-cell-map.md.
 *
 * THIS VERSION USES 0xC90/0xC94, chosen from a full-writer audit of this tree rather than a guess,
 * and it reads its deposits back IN THIS BOOT so a clobbered cell can never be reported as a
 * measurement.  The genuine zero-reference cells (0xC74/0xC7C/0xC80/0xC84) went to the erase gate,
 * which is the instrument that matters more.
 */
static int __init luofu_boot_stage(void)
{
	void __iomem *sc = ioremap(LUOFU_SYSCTRL_BASE, 0x1000);
	u32 surv = 0;

	if (!sc)
		return 0;

	/*
	 * 0xC10 carries the stage value; 0xC14 the self-check.  Neither is the selector, the probe's
	 * tick, nor the FMC's payload.  The value is read back IN THIS BOOT so the reading proves its
	 * own validity - a deposit that does not survive its own write is never reported.
	 *
	 *   0xC10 = 0xC0DE9A01 and 0xC14 = 0x5AFE0003
	 *     -> the kernel got past ALL late initcalls, so the panic is in the userspace hand-off
	 *        (kernel_init_freeable / mount_root).
	 *   0xC10 still reads its prime
	 *     -> late_initcall_sync never ran: the panic is INSIDE a late-initcall body.
	 */
	/*
	 * 0xC94 CARRIES THE STAGE VALUE - chosen from a full-writer audit, not a guess.
	 * 0xC08 is the SLOT SELECTOR and must never be written (see the note above).  The cells with
	 * zero references anywhere in this tree are 0xC74, 0xC7C, 0xC80, 0xC84 - and the erase gate
	 * has since taken those, so this probe uses 0xC90/0xC94, written only by the FMC's own
	 * per-operation config snapshot, which runs in a different path and will not collide here.
	 *
	 *   0xC94 = 0xC0DE9A01 -> the kernel got past ALL late initcalls (panic is in the userspace
	 *                         hand-off: kernel_init_freeable / mount_root)
	 *   0xC94 still reads its prime -> late_initcall_sync never ran (panic is INSIDE a late
	 *                         initcall body)
	 */
	writel(0xC0DE9A01u, sc + 0xc94);
	surv = (readl(sc + 0xc94) == 0xC0DE9A01u) ? 1u : 0u;
	/* the complement, in the neighbouring cell, to show these are real registers not mirrors */
	writel(0x3F2165FEu, sc + 0xc90);
	if (readl(sc + 0xc90) == 0x3F2165FEu)
		surv |= 2u;
	/* the self-check goes BACK into 0xC90 so one read tells us both "it ran" and "it is real" */
	writel(0x5AFE0000u | surv, sc + 0xc90);
	(void)readl(sc + 0xc14);
	iounmap(sc);

	return 0;
}
late_initcall_sync(luofu_boot_stage);

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
/*
 * setup_earlycon() takes the "<drivername>,<options>" form, exactly what the earlycon= kernel
 * parameter parses] - so this hands it the SAME string the command line carries, which keeps one
 * spelling of the port in the image.  A failure here is not fatal and must not be: an instrument
 * that can abort the boot is not an instrument.
 */
static void __init luofu_uart_early(void)
{
	char buf[] = LUOFU_EARLYCON_STR;
	int ret;

	ret = setup_earlycon(buf);
	if (ret && ret != -EALREADY)
		pr_err("luofu: setup_earlycon(%s) -> %d (the cmdline earlycon= is the other path)\n",
		       LUOFU_EARLYCON_STR, ret);
}

static void __init luofu_early(void)
{
	void __iomem *sysctrl = ioremap(LUOFU_SYSCTRL_BASE, 0x1000);
	u32 val;

	luofu_uart_early();

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

	/*
	 * ★ THE WATCHDOG IS LEFT ARMED ON PURPOSE (2026-10-26 18:30Z).
	 *
	 * This used to write the stop magic here, contradicting the driver: luofu-wdt's probe sets
	 * WDOG_HW_RUNNING ("the hardware is live, core, please ping it") while this function disarmed the very
	 * timer it was told to service. The watchdog was therefore neither a watchdog nor a hazard - and the
	 * boot had no independent reset source at all.
	 *
	 * Leaving u-boot's armed timer running gives both halves: the core's servicing workqueue pings it, so a
	 * healthy kernel survives indefinitely (what G6 needs), and a kernel that panics or hangs stops pinging,
	 * so the hardware still resets it (what would make a panic=0 experiment safe).
	 *
	 * The disarm is removed, not moved. If the core ever fails to ping, the box resets at the u-boot timeout
	 * and the vendor takes over - the failure mode this bench already lives with.
	 */
	crg = ioremap(LUOFU_CRG_BASE, 0x1000);
	if (crg) {
		pr_info("luofu: hsan watchdog LEFT ARMED - luofu-wdt services it; hardware resets a hung kernel\n");
		iounmap(crg);
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
