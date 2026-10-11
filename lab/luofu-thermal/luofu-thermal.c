// SPDX-License-Identifier: GPL-2.0
/*
 * luofu-thermal: temperature sensor driver for the Hi5671Y "luofu" SoC
 * (DT compatible "hisilicon,luofu-tvsensor"; the vendor binds "hsan,tvsensor").
 *
 * ==========================  WHERE THIS COMES FROM  ==========================
 * Our reconstructed device tree had NO thermal node at all; the vendor's own tree
 * carries both the sensor and the zone (opensource/docs/soc/luofu-r116-pinned.dts
 * :1432-1489), and that pair was restored into docs/soc/luofu-r116.dts. This is
 * the driver for the restored node.
 *
 * The vendor's `hi_tvsensor.ko` is a standard thermal driver - it imports
 * `devm_thermal_zone_of_sensor_register` and has `hi_tsensor_hw_get_temp`,
 * `set_th`, `set_trim`, `set_int_mask`, `clr_int`, in a v1 and a v4 variant.
 * The DT's `hwver = <3>` selects the v4 path.
 *
 * =====================  THE TEMPERATURE CONVERSION (decoded)  =====================
 * Recovered from `hi_tsensor_hw_v4_get_temp` and verified numerically:
 *
 *     raw = readl(base + 0x24) & 0xffff
 *     millidegC = ((165 * raw - 32835) / 100) * 1000 - 40000
 *
 * (the module computes `165*raw - 0x8000 - 0x43`, divides by 100 with a magic
 * multiply, multiplies by 1000 and subtracts 0x9c00 + 0x40 = 40000).  A raw of
 * 240 reads 27.0 C, and the slope is 1.65 C per LSB - a sane SoC curve.
 *
 * =============================  WHAT IS REAL VS STAGED  =============================
 * REAL: the platform driver, the thermal_zone_of_device_ops, the conversion above
 * (verified), and the zone's trip points, which come from the DT.
 *
 * STAGED: the threshold registers (`set_th`) and the trim/enable writes - their
 * values are recoverable from the same module but are NOT written here, because
 * no store to them has been measured on this bench. Reading the temperature is
 * store-free and is the part that is proven arithmetic.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>

/* v4 sensor register (from hi_tsensor_hw_v4_get_temp). */
#define LUOFU_TSENSOR_TEMP_REG		0x24
#define LUOFU_TSENSOR_RAW_MASK		0xffff

struct luofu_thermal {
	struct device		*dev;
	void __iomem		*base;
	struct thermal_zone_device *tzd;
};

static int luofu_thermal_get_temp(void *data, int *temp)
{
	void __iomem *base = data;
	u32 raw = readl(base + LUOFU_TSENSOR_TEMP_REG) & LUOFU_TSENSOR_RAW_MASK;
	s32 cdeg;

	cdeg = ((165 * (s32)raw - 32835) / 100) * 1000 - 40000;
	*temp = cdeg;
	return 0;
}

static const struct thermal_zone_of_device_ops luofu_thermal_ops = {
	.get_temp	= luofu_thermal_get_temp,
};

static int luofu_thermal_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct thermal_zone_device *tzd;
	void __iomem *base;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	tzd = devm_thermal_zone_of_sensor_register(dev, 0, base,
						   &luofu_thermal_ops);
	if (IS_ERR(tzd))
		return dev_err_probe(dev, PTR_ERR(tzd),
				     "cannot register thermal zone\n");

	dev_info(dev, "luofu-thermal: registered (temp reg +0x%x, raw %u)\n",
		 LUOFU_TSENSOR_TEMP_REG,
		 readl(base + LUOFU_TSENSOR_TEMP_REG) & LUOFU_TSENSOR_RAW_MASK);
	return 0;
}

static const struct of_device_id luofu_thermal_of_match[] = {
	{ .compatible = "hisilicon,luofu-tvsensor" },
	{ .compatible = "hsan,tvsensor" },	/* the vendor tree's string */
	{ }
};
MODULE_DEVICE_TABLE(of, luofu_thermal_of_match);

static struct platform_driver luofu_thermal_driver = {
	.probe	= luofu_thermal_probe,
	.driver	= {
		.name		= "luofu-thermal",
		.of_match_table	= luofu_thermal_of_match,
	},
};
module_platform_driver(luofu_thermal_driver);

MODULE_DESCRIPTION("Hi5671Y luofu temperature sensor driver");
MODULE_LICENSE("GPL v2");
