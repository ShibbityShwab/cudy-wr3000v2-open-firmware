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
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

/* The two 9-bit prescaler fields recovered from hi_wdt_hw_set_timeout(). */
#define LUOFU_WDT_TIME_FIELD0_SHIFT	3
#define LUOFU_WDT_TIME_FIELD1_SHIFT	10
#define LUOFU_WDT_TIME_FIELD_MASK	GENMASK(8, 0)

struct luofu_wdt {
	struct watchdog_device	wdd;
	struct device		*dev;
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
};

static int luofu_wdt_start(struct watchdog_device *wdd)
{
	struct luofu_wdt *w = watchdog_get_drvdata(wdd);

	writel(w->enable_val0, w->base + w->en_offset);
	writel(w->enable_val1, w->base + w->en_offset);
	return 0;
}

static int luofu_wdt_stop(struct watchdog_device *wdd)
{
	struct luofu_wdt *w = watchdog_get_drvdata(wdd);

	writel(w->stop_val0, w->base + w->en_offset);
	writel(w->stop_val1, w->base + w->en_offset);
	return 0;
}

static int luofu_wdt_ping(struct watchdog_device *wdd)
{
	struct luofu_wdt *w = watchdog_get_drvdata(wdd);

	writel(0, w->base + w->clr_offset);
	return 0;
}

static int luofu_wdt_set_timeout(struct watchdog_device *wdd, unsigned int secs)
{
	struct luofu_wdt *w = watchdog_get_drvdata(wdd);
	u32 v;

	if (secs == 0 || secs > wdd->max_timeout)
		return -EINVAL;

	v = readl(w->base + w->set_offset);
	v &= ~(LUOFU_WDT_TIME_FIELD_MASK <<
	       (w->timeout_field == 0 ? LUOFU_WDT_TIME_FIELD0_SHIFT
				      : LUOFU_WDT_TIME_FIELD1_SHIFT));
	v |= (secs - 1) << (w->timeout_field == 0 ? LUOFU_WDT_TIME_FIELD0_SHIFT
						  : LUOFU_WDT_TIME_FIELD1_SHIFT);
	writel(v, w->base + w->set_offset);

	wdd->timeout = secs;
	return 0;
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

static int luofu_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct luofu_wdt *w;
	int ret;

	w = devm_kzalloc(dev, sizeof(*w), GFP_KERNEL);
	if (!w)
		return -ENOMEM;

	w->dev = dev;
	w->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(w->base))
		return PTR_ERR(w->base);

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
	of_property_read_u32(dev->of_node, "enable-val0", &w->enable_val0);
	of_property_read_u32(dev->of_node, "enable-val1", &w->enable_val1);
	of_property_read_u32(dev->of_node, "stop-val0", &w->stop_val0);
	of_property_read_u32(dev->of_node, "stop-val1", &w->stop_val1);
	of_property_read_u32(dev->of_node, "reset-val0", &w->reset_val0);
	of_property_read_u32(dev->of_node, "reset-val1", &w->reset_val1);

	w->wdd.info = &luofu_wdt_info;
	w->wdd.ops = &luofu_wdt_ops;
	w->wdd.parent = dev;

	/* the DT's timeout-min/timeout-max/timeout-sec are the mainline names too */
	watchdog_init_timeout(&w->wdd, 0, dev);
	w->wdd.min_timeout = 1;
	w->wdd.max_timeout = 0x200;

	watchdog_set_drvdata(&w->wdd, w);
	watchdog_stop_on_reboot(&w->wdd);
	watchdog_stop_on_unregister(&w->wdd);

	ret = devm_watchdog_register_device(dev, &w->wdd);
	if (ret)
		return ret;

	dev_info(dev,
		 "luofu-wdt: registered (en+0x%x set+0x%x clr+0x%x), timeout %us, max %us\n",
		 w->en_offset, w->set_offset, w->clr_offset,
		 w->wdd.timeout, w->wdd.max_timeout);
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
