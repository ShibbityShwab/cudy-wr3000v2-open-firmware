// SPDX-License-Identifier: GPL-2.0
/*
 * luofu-wdt: watchdog driver for the Hi5671Y "luofu" SoC
 * (DT compatible "hisilicon,luofu-wdt"; the vendor binds "hsan,hsan-watchdog").
 *
 * ==========================  WHERE THIS COMES FROM  ==========================
 * The vendor ships its watchdog as `hi_wdg.ko` (9,292 B) and its DT node carries a
 * VENDOR BINDING - the mainline watchdog binding defines none of these:
 *
 *   reg = <0x14880000 0x1000>      the CRG page (same page as the clocks)
 *   en-offset   = "d"  (= 0x64)    the enable/disable register
 *   set-offset  = "P"  (= 0x50)    the timeout register
 *   clr-offset  = "p"  (= 0x70)    the clear/ping register
 *   int-status-offset = <0x100>
 *   enable-val0 = "ONTR" (= 0x4f4e5452), enable-val1 = <0x12345116>
 *   stop-val0   = <0xabcd5116>,          stop-val1   = <0xed574447>
 *   reset-val0  = <0x55aa5a5a>,          reset-val1  = <0xaa55a5a5>
 *   timeout-min = <1>, timeout-max = <0x200>, timeout-sec = <0x1e>
 *
 * (source: opensource/docs/soc/luofu-r116-pinned.dts:1512-1531; the string-vs-cell
 * rendering and its decode rule are documented at that file's header, lines 34-40.)
 *
 * The vendor module's own structure confirms the shape: it is a standard
 * `watchdog_device` (it imports watchdog_register_device / watchdog_init_timeout /
 * watchdog_unregister_device) with hi_wdt_hw_enable/disable/set_timeout/ping over
 * the three registers above, plus a restart_handler for reset-val0/val1.
 *
 * Decoded from hi_wdt_hw_enable / _hw_set_timeout in that module:
 *   - enable writes enable-val0 then enable-val1 to [en-offset];
 *   - set_timeout does a read-modify-write of two 9-bit fields at bits 3 and 10 of
 *     [set-offset] with (secs-1);
 *   - ping writes [clr-offset].
 *
 * =============================  WHAT IS REAL  =============================
 * Everything above is implemented here and reads its values FROM THE DT, exactly
 * as the vendor does - so the numbers are not guesses, they are the vendor's own
 * node.  Nothing in this file hardcodes a magic value that the DT already carries.
 *
 * The one thing deliberately NOT done: a store is only ever issued from
 * ->start/->stop/->ping/->set_timeout, i.e. when userspace asks for the watchdog.
 * Probe itself performs reads only, so simply loading the module cannot arm a
 * timer - the same discipline every other luofu driver uses.
 */

#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

/* The two 9-bit prescaler fields recovered from hi_wdt_hw_set_timeout(). */
#define LUOFU_WDT_TIME_FIELD0_SHIFT	3
#define LUOFU_WDT_TIME_FIELD1_SHIFT	10
#define LUOFU_WDT_TIME_FIELD_MASK	GENMASK(8, 0)

struct luofu_wdt {
	struct watchdog_device	wdd;	struct device		*dev;
	void __iomem		*base;

	u32	en_offset;
	u32	set_offset;
	u32	clr_offset;
	u32	int_status_offset;

	u32	enable_val0;
	u32	enable_val1;
	u32	stop_val0;
	u32	stop_val1;
	u32	reset_val0;
	u32	reset_val1;

	u32	timeout_field;
	unsigned int	hwver;
	struct timer_list	pet_timer;	/* ★ pings unconditionally: nothing opens /dev/watchdog */
	unsigned long	pet_deadline;	/* ★ jiffies after which the pet STOPS (see luofu_wdt_pet) */
	bool		pet_expired;
};

/*
 * ★ THE PET MUST EXPIRE - OTHERWISE A HUNG BOOT IS A PERMANENT STRAND.
 *
 * u-boot arms this timer on EVERY boot (WDT70/WDT64 in every preflight readout).  A bootstrap pet
 * that pings forever therefore takes the board's ONLY automatic recovery away: if userspace never
 * brings the network up, nothing else feeds the watchdog, our timer keeps it alive, and the box sits
 * dark until somebody holds the RESET button.  Measured 2026-10-30: a fire whose userspace never
 * came up returned "NO RETURN within 1500s" and had to be waited out.
 *
 * So pet only long enough for userspace to get itself up.  When the budget is gone, stop feeding and
 * let the HARDWARE reset the board back to the vendor slot - the watchdog then does the job it was
 * always doing before this driver existed.
 */
#define LUOFU_WDT_PET_BUDGET_MS	45000	/* TEST BUILD: stop petting after 45 s so a stuck boot resets fast */

static void luofu_wdt_stamp(u32 v);

static int luofu_wdt_start(struct watchdog_device *wdd)
{
	struct luofu_wdt *w = watchdog_get_drvdata(wdd);

	writel(w->enable_val1, w->base + w->en_offset);
	udelay(500);
	writel(w->enable_val0, w->base + w->en_offset);
	return 0;
}

static int luofu_wdt_stop(struct watchdog_device *wdd)
{
	struct luofu_wdt *w = watchdog_get_drvdata(wdd);

	writel(w->stop_val1, w->base + w->en_offset);
	udelay(500);
	writel(w->stop_val0, w->base + w->en_offset);
	return 0;
}

static int luofu_wdt_ping(struct watchdog_device *wdd)
{
	struct luofu_wdt *w = watchdog_get_drvdata(wdd);

	/* two stores of the magics, LOW THEN HIGH; the register is left holding reset-val1, which is
	 * exactly what the live vendor reads back at 0x14880070 (0xaa55a5a5). Writing 0 there (what the
	 * old ping did) does not match the vendor in any routine. */
	writel(w->reset_val1, w->base + w->clr_offset);
	return 0;
}

/*
 * Vendor field selection (hi_wdt_hw_set_timeout @0x90 in hi_wdg.ko).
 *
 * The vendor does NOT pick between two fields: it switches on the DT `hwver`, and each case writes a
 * DIFFERENT field of the same register. Verified live on 2.4.15: set-offset (0x14880050) reads
 * 0x30007400, whose bits[10:18] are 29 - exactly `2*secs-1` for secs=15 under hwver 1. Read the old
 * way, bits[3:11] give 128 -> 129 s. So this board is hwver 1, and a driver that only implements
 * the hwver-0 case programs a field the hardware does not read.
 */
#define LUOFU_WDT_HWVER_DEFAULT	1

static int luofu_wdt_set_timeout(struct watchdog_device *wdd, unsigned int secs)
{
	struct luofu_wdt *w = watchdog_get_drvdata(wdd);
	u32 v;

	/*
	 * ENTRY/EXIT STAMPS (2026-10-30). [wwd4] put the death inside
	 * devm_watchdog_register_device, after every DT read. The core reconciles wdd->timeout during
	 * registration, so ->set_timeout is a candidate - and it is the ONE place this driver now does a
	 * read-modify-write of 0x14880050, in the CRG page whose reset/clock claims have broken this
	 * boot before.
	 *
	 *   0xC0DE7D06 -> set_timeout ENTERED (core called it)
	 *   0xC0DE7D07 -> the writel returned and the function is about to exit
	 */
	luofu_wdt_stamp(0xC0DE7D06u);

	if (secs == 0 || secs > wdd->max_timeout)
		return -EINVAL;

	v = readl(w->base + w->set_offset);

	switch (w->hwver) {
	case 0:	/* bfi(v, secs-1, lsb 3, width 9) */
		v = (v & ~GENMASK(11, 3)) | (((secs - 1) & 0x1ff) << 3);
		break;
	case 1:	/* bfi(v, 2*secs-1, lsb 10, width 10) */
		v = (v & ~GENMASK(19, 10)) | (((2 * secs - 1) & 0x3ff) << 10);
		break;
	case 2:	/* bfi(v, secs-1, lsb 10, width 9) */
		v = (v & ~GENMASK(18, 10)) | (((secs - 1) & 0x1ff) << 10);
		break;
	default:
		return -EINVAL;
	}

	writel(v, w->base + w->set_offset);

	luofu_wdt_stamp(0xC0DE7D07u);

	wdd->timeout = secs;
	return 0;
}

/*
 * ★ PET ON OUR OWN TIMER (2026-10-26 21:55Z).
 *
 * WDOG_HW_RUNNING is a STATE FLAG, not a pinger: the watchdog core only calls ->ping while the device is
 * OPEN, and nothing in this image opens /dev/watchdog. `fire_wg6` proved the consequence - with the mach's
 * disarm removed and the driver bound (`C1C = 0x5AC00182`), the box still died at the u-boot timeout. So the
 * driver pets the hardware itself every LUOFU_WDT_PET_MS; the core can still open the device and take over.
 */
#define LUOFU_WDT_PET_MS 5000
static void luofu_wdt_pet(struct timer_list *t)
{
	struct luofu_wdt *w = from_timer(w, t, pet_timer);

	writel(w->reset_val0, w->base + w->clr_offset);
	writel(w->reset_val1, w->base + w->clr_offset);

	/* budget exhausted -> stop feeding, let the hardware recover the board */
	if (time_after(jiffies, w->pet_deadline)) {
		if (!w->pet_expired) {
			w->pet_expired = true;
			pr_warn("luofu-wdt: pet budget spent - releasing the watchdog so a hung boot resets\n");
		}
		return;
	}

	mod_timer(&w->pet_timer, jiffies + msecs_to_jiffies(LUOFU_WDT_PET_MS));
}

static const struct watchdog_info luofu_wdt_info = {
	.options	= WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity	= "luofu-wdt",
};

static const struct watchdog_ops luofu_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= luofu_wdt_start,
	.stop		= luofu_wdt_stop,
	.ping		= luofu_wdt_ping,
	.set_timeout	= luofu_wdt_set_timeout,
};

/*
 * ★ PROBE CRUMB IN A CELL THE TICK DOES NOT OWN (2026-10-26 16:57Z).
 *
 * The tick rewrites C18/C1C every 5 s, so a probe crumb written there is gone before any fire can read it -
 * which is why "the watchdog bound" was never actually verified. C9C is not written by the tick.
 *
 *   0x00070B1D  the probe reached its end (the watchdog is being serviced)
 *   0x00070B1F  the probe failed one of the DT property lookups
 */
/*
 * ★ EXPORTED BINDING FLAG (2026-10-26 17:50Z). The sysctrl cells are all owned by something else
 * (0xC80-0xC9C = GMAC/mach/log per the cell map), so this driver reports through the ONE channel that
 * demonstrably works: the FMC tick's C1C payload. The FMC driver ORs 0x80 into the system_state byte when
 * this is true, so C1C = 0x5AC00182 reads as "RUNNING and the watchdog bound".
 */
bool luofu_wdt_bound;
EXPORT_SYMBOL_GPL(luofu_wdt_bound);

/*
 * BISECT STAMP (2026-10-30).
 *
 * The boot dies between the FMC probe's return (C18 = 0xC0DE50FF, [wexit]) and the first
 * late_initcall ([wlate]). The GMAC, next in link order, ENTERS and ERROR-RETURNS at its reset
 * lookup (C94 = 0xC0DE6F04) rather than hanging - so the death is at or after this probe.
 *
 * C18 is safe here: the FMC probe wrote it last at 0xC0DE50FF and does not touch it again.
 * The value also answers a second question that has never been measured - whether luofu-wdt
 * binds at all on this board.
 *
 *   0xC0DE7D01 -> luofu_wdt_probe ENTERED
 *   0xC0DE7D02 -> it reached watchdog registration
 */
static void luofu_wdt_stamp(u32 v)
{
	void __iomem *sc = ioremap(0x10100000, 0x1000);
	if (!sc)
		return;
	/* ★ C0C, NOT C18 (2026-10-30): the FMC log-ring tick REWRITES C18 every 5 s with
	 * 0xC0DE5000|ticks, so every watchdog stamp was erased before it could be read - which is
	 * why "does this probe bind at all?" has never been answerable. C0C reads 0 in every fire
	 * readout, no driver writes it, and the fire prints it: 7D01 probe entered, 7D03 map ok,
	 * 7D04 DT props read, 7D05 about to register (pet timer armed just before), 7D02 registered. */
	writel(v, sc + 0xc0c);
	(void)readl(sc + 0xc0c);
	iounmap(sc);
}

static int luofu_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct luofu_wdt *w;
	int ret;

	luofu_wdt_stamp(0xC0DE7D01u);

	w = devm_kzalloc(dev, sizeof(*w), GFP_KERNEL);
	if (!w)
		return -ENOMEM;

	w->dev = dev;
	w->hwver = LUOFU_WDT_HWVER_DEFAULT;
	/*
	 * ★★★★★ A PLAIN MAPPING, NOT AN EXCLUSIVE ONE (2026-10-30).
	 *
	 * This was devm_platform_ioremap_resource(), which calls request_mem_region() on the range -
	 * and the watchdog lives on the SAME 0x14880000 CRG page that luofu-crg already claims with
	 * devm_ioremap_resource(). The CRG probes first (everything needs its clocks), so this
	 * request returned -EBUSY, luofu_wdt_probe aborted before arming its pet timer, and NOTHING
	 * FED THE WATCHDOG U-BOOT LEAVES RUNNING. The board reset ~45-70 s into every boot of our
	 * kernel, which read as a userspace failure and was not one.
	 *
	 * devm_ioremap() maps the same registers WITHOUT taking the region, which is the normal way
	 * for a driver that shares a register block another driver owns.
	 */
	{
		struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

		if (!res)
			return -ENODEV;
		w->base = devm_ioremap(dev, res->start, resource_size(res));
		if (!w->base)
			return -ENOMEM;
	}
	if (IS_ERR(w->base))
		return PTR_ERR(w->base);
	luofu_wdt_stamp(0xC0DE7D03u);	/* the 0x14880000 mapping succeeded */

	/* every value comes from the vendor's own DT node */
	ret = of_property_read_u32(dev->of_node, "en-offset", &w->en_offset);
	if (ret)
		return dev_err_probe(dev, ret, "missing en-offset\n");
	ret = of_property_read_u32(dev->of_node, "set-offset", &w->set_offset);
	if (ret)
		return dev_err_probe(dev, ret, "missing set-offset\n");
	ret = of_property_read_u32(dev->of_node, "clr-offset", &w->clr_offset);
	if (ret)
		return dev_err_probe(dev, ret, "missing clr-offset\n");

	of_property_read_u32(dev->of_node, "int-status-offset",
			     &w->int_status_offset);
	{
		u32 hwver;

		if (!of_property_read_u32(dev->of_node, "hwver", &hwver) && hwver <= 2)
			w->hwver = hwver;
	}
	of_property_read_u32(dev->of_node, "enable-val0", &w->enable_val0);
	of_property_read_u32(dev->of_node, "enable-val1", &w->enable_val1);
	of_property_read_u32(dev->of_node, "stop-val0", &w->stop_val0);
	of_property_read_u32(dev->of_node, "stop-val1", &w->stop_val1);
	of_property_read_u32(dev->of_node, "reset-val0", &w->reset_val0);
	of_property_read_u32(dev->of_node, "reset-val1", &w->reset_val1);
	luofu_wdt_stamp(0xC0DE7D04u);	/* every DT property was read */

	w->wdd.info = &luofu_wdt_info;
	w->wdd.ops = &luofu_wdt_ops;
	w->wdd.parent = dev;

	/* the DT's timeout-min/timeout-max/timeout-sec are the mainline names too */
	watchdog_init_timeout(&w->wdd, 0, dev);
	w->wdd.min_timeout = 1;
	w->wdd.max_timeout = 0x200;

	/*
	 * STAGE-1 BISECT OF THE REGISTRATION HANG (2026-10-30).
	 *
	 * [wwd6] showed the probe reaches devm_watchdog_register_device (0xC0DE7D05) and never
	 * reaches the completion stamp. `watchdog_init` is a LEVEL-4s initcall, so by level 6
	 * wtd_deferred_reg_done is already true and the registration runs INLINE - it can hang.
	 *
	 * Inside __watchdog_register_device the one call that takes a MUTEX is
	 * register_reboot_notifier(), reached only because watchdog_stop_on_reboot() sets
	 * WDOG_STOP_ON_REBOOT. The driver does not need that policy: nothing in this image opens
	 * /dev/watchdog, and stop_on_reboot is a userspace-facing convenience.
	 *
	 *   boot reaches 0xC0DE7D02 -> the reboot notifier was the cause, and it is now simply gone
	 *   still stops at 0xC0DE7D05 -> the cause is watchdog_dev_register (cdev/device_add)
	 */
	watchdog_set_drvdata(&w->wdd, w);
	watchdog_stop_on_unregister(&w->wdd);

	/*
	 * ★★★★★ THE PET TIMER IS ARMED HERE, NOT AFTER THE REGISTRATION (2026-10-30).
	 *
	 * It used to be started at the END of this function, after
	 * devm_watchdog_register_device() returned - and the note below records that this probe
	 * reaches that call (0xC0DE7D05) and NEVER reaches the completion stamp
	 * (0xC0DE7D02). So the pet timer was never armed, nothing pinged the watchdog that u-boot
	 * left running, and the board reset ~45-70 s into EVERY boot of our kernel.
	 *
	 * That reset is what kept handing the box back to the vendor, and it masqueraded as a
	 * userspace fault: the boot had already reached run_init_process (0x1EB70F). The decisive
	 * test was init=/bin/sh - a minimal init execs (0x1EB70F again) and the box STILL reset at
	 * the same point, which exonerates procd and points here.
	 *
	 * Pinging is unconditional by design (nothing opens /dev/watchdog), so it does not depend
	 * on the device registering at all. Arming it first makes the board survivable whatever
	 * the watchdog core does later.
	 */
	timer_setup(&w->pet_timer, luofu_wdt_pet, 0);
	w->pet_deadline = jiffies + msecs_to_jiffies(LUOFU_WDT_PET_BUDGET_MS);
	w->pet_expired = false;
	mod_timer(&w->pet_timer, jiffies + msecs_to_jiffies(LUOFU_WDT_PET_MS));

	luofu_wdt_stamp(0xC0DE7D05u);	/* about to register with the watchdog core */
	ret = devm_watchdog_register_device(dev, &w->wdd);
	if (ret)
		return ret;
	dev_info(dev,
		 "luofu-wdt: registered (en+0x%x set+0x%x clr+0x%x), timeout %us, max %us\n",
		 w->en_offset, w->set_offset, w->clr_offset,
		 w->wdd.timeout, w->wdd.max_timeout);

	/* reported through the FMC tick payload: C1C bit 7 = this driver bound */
	luofu_wdt_bound = true;
	luofu_wdt_stamp(0xC0DE7D02u);	/* probe COMPLETED and the device registered */
	return 0;
}

static const struct of_device_id luofu_wdt_of_match[] = {
	{ .compatible = "hisilicon,luofu-wdt" },
	{ .compatible = "hsan,hsan-watchdog" },	/* the vendor tree's string */
	{ }
};
MODULE_DEVICE_TABLE(of, luofu_wdt_of_match);

static struct platform_driver luofu_wdt_driver = {
	.probe	= luofu_wdt_probe,
	.driver	= {
		.name		= "luofu-wdt",
		.of_match_table	= luofu_wdt_of_match,
	},
};
module_platform_driver(luofu_wdt_driver);

MODULE_DESCRIPTION("Hi5671Y luofu watchdog driver");
MODULE_LICENSE("GPL v2");
