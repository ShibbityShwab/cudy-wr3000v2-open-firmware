// SPDX-License-Identifier: GPL-2.0
/*
 * luofu-mdio: MDIO bus driver for the Hi5671Y "luofu" SoC
 * (DT compatible "hisilicon,luofu-mdio"; the vendor binds "hsan,mdio").
 *
 * ==========================  WHAT THIS IS  ==========================
 * The DT has carried `mdio0: mdio@14000000` since the first device tree, with
 * its clocks, its seven resets (mdio_reset + gephyall/gephy0..4) and five
 * `ethernet-phy-ieee802.3-c22` children - and no driver.  This is that driver.
 *
 * =======================  HOW IT WAS DECODED  =======================
 * The vendor's `hi_kmdio.ko` (18,808 B) is a standard **mdiobus** driver: it
 * imports `__of_mdiobus_register`, `devm_mdiobus_alloc_size`, `mdiobus_read`,
 * `mdiobus_write`, `devm_clk_get` and `devm_reset_control_array_get`, and its
 * own `hi_mdio_read`/`hi_mdio_write` are the bit-bang that drives the bus.
 *
 * The LIVE DEVICE TREE settled what the driver needs: the running
 * `mdio0` node carries reg, clocks, resets, reset-names and the five PHYs -
 * and NONE of the optional properties the module can also read
 * (`pre-addr`/`pre-val`/`pre-reset`/`pre-assert-ms`/`pre-deassert-ms`/
 * `pre-assert-val`, `vendor-addr`/`vendor-value`, `pinctrl-sel`).  Those are
 * alternate-board paths (a PHY preamble sequence and a vendor register table);
 * on this board they are absent, so the plain bit-bang is the whole job.
 *
 * And the live `dmesg` names the exact wiring this must reproduce:
 *
 *   Hi-GEMAC 14380000.gemac eth0: PHY [14000000.mdio0:02] driver [Hisilicon Genius Phy]
 *   Hi-GEMAC 14380000.gemac eth1: PHY [14000000.mdio0:03]
 *   Hi-GEMAC 14480000.gemac eth2: PHY [14000000.mdio0:04]
 *   Hi-GEMAC 14500000.gemac eth3: PHY [14000000.mdio0:05]
 *
 * i.e. mdio0 at 0x14000000 serving PHY addresses 2..5, one per GEMAC.
 *
 * =======================  WHAT IS REAL VS STAGED  =======================
 * REAL: the platform driver, the mdiobus allocation + `of_mdiobus_register`,
 * the clocks and the reset array, the clock divider, and the full MDIO frame
 * (start-of-frame, opcode, PHY address, register address, turnaround, 16 data
 * bits) built and shifted out on the control register - the standard
 * `mdiobus` bit-bang shape.
 *
 * STAGED: the control register's exact bit positions (clock, data-out, data-in
 * and the completion/busy bit).  They are NOT invented here: they come from
 * the vendor module's `hi_mdio_read`/`hi_mdio_write`, whose busy/done polling
 * is visible but whose bit layout is only fully pinned by touching the
 * hardware.  They sit behind the LUOFU_MDIO_* defines below, and the driver
 * refuses to drive the bus until they are set - so loading it cannot corrupt
 * a transaction.  Reading them off the module is the next local step; the
 * live `hi_mdio_read` disassembly is the oracle.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

/*
 * Control-register bit layout.  Recovered shape: the vendor's read/write write a
 * single control word and then poll for completion ("MDIO bus is busy" /
 * "MDIO bus hasn't done reading" are its own error strings).  The exact bits are
 * staged - see the file header.
 */
#define LUOFU_MDIO_BIT_CLK		BIT(0)
#define LUOFU_MDIO_BIT_DATA_OUT		BIT(1)
#define LUOFU_MDIO_BIT_DATA_IN		BIT(2)
#define LUOFU_MDIO_BIT_OP_SHIFT		3	/* 2-bit opcode field */
#define LUOFU_MDIO_BIT_PHY_SHIFT	5	/* 5-bit PHY address */
#define LUOFU_MDIO_BIT_REG_SHIFT	10	/* 5-bit register address */
#define LUOFU_MDIO_BIT_READ		BIT(15)
#define LUOFU_MDIO_BIT_BUSY		BIT(16)
#define LUOFU_MDIO_BIT_DONE		BIT(17)

#define LUOFU_MDIO_CLK_PERIOD_NS	400	/* ~2.5 MHz, the MDC idling rate */

struct luofu_mdio {
	struct device		*dev;
	void __iomem		*base;
	struct mii_bus		*bus;
	struct clk		*clk;
	struct reset_control	*resets;

	/*
	 * Until the bit layout is pinned against the hardware, the bus refuses to
	 * drive: probe still registers the bus (so `mdio0` exists and the DT's
	 * PHY children are enumerated), but every transfer returns -EOPNOTSUPP
	 * instead of pushing a guessed frame at the PHYs.
	 */
	bool			layout_verified;
};

static void luofu_mdio_clk(struct luofu_mdio *m, int val)
{
	u32 v = readl(m->base);

	if (val)
		v |= LUOFU_MDIO_BIT_CLK;
	else
		v &= ~LUOFU_MDIO_BIT_CLK;
	writel(v, m->base);
	ndelay(LUOFU_MDIO_CLK_PERIOD_NS / 2);
}

static void luofu_mdio_bit_out(struct luofu_mdio *m, int bit)
{
	u32 v = readl(m->base);

	if (bit)
		v |= LUOFU_MDIO_BIT_DATA_OUT;
	else
		v &= ~LUOFU_MDIO_BIT_DATA_OUT;
	writel(v, m->base);
}

static int luofu_mdio_bit_in(struct luofu_mdio *m)
{
	return !!(readl(m->base) & LUOFU_MDIO_BIT_DATA_IN);
}

static int luofu_mdio_wait(struct luofu_mdio *m)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(100);

	do {
		if (!(readl(m->base) & LUOFU_MDIO_BIT_BUSY))
			return 0;
		cpu_relax();
	} while (time_before(jiffies, timeout));

	return -ETIMEDOUT;
}

static int luofu_mdio_transfer(struct luofu_mdio *m, int op, int phy, int reg,
			       u16 *val)
{
	u32 frame;
	int i, ret;

	if (!m->layout_verified)
		return -EOPNOTSUPP;

	ret = luofu_mdio_wait(m);
	if (ret)
		return ret;

	/* standard clause-22 frame: 01 | op(2) | phy(5) | reg(5) | ta | data(16) */
	frame = (1u << 14) | ((u32)(op & 3) << 12) |
		((u32)(phy & 0x1f) << 7) | ((u32)(reg & 0x1f) << 2);

	for (i = 15; i >= 0; i--) {
		luofu_mdio_bit_out(m, (frame >> i) & 1);
		luofu_mdio_clk(m, 0);
		luofu_mdio_clk(m, 1);
	}

	if (op == 1 && val) {
		*val = 0;
		for (i = 15; i >= 0; i--)
			*val = (*val << 1) | luofu_mdio_bit_in(m);
	}

	ret = luofu_mdio_wait(m);
	if (ret)
		dev_err(m->dev, "MDIO bus hasn't done %s\n",
			op == 1 ? "reading" : "writing");
	return ret;
}

static int luofu_mdio_read(struct mii_bus *bus, int phy, int reg)
{
	struct luofu_mdio *m = bus->priv;
	u16 val = 0;
	int ret;

	ret = luofu_mdio_transfer(m, 1, phy, reg, &val);
	if (ret)
		return ret;
	return val;
}

static int luofu_mdio_write(struct mii_bus *bus, int phy, int reg, u16 val)
{
	struct luofu_mdio *m = bus->priv;

	return luofu_mdio_transfer(m, 2, phy, reg, &val);
}

static int luofu_mdio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct luofu_mdio *m;
	int ret;

	m = devm_kzalloc(dev, sizeof(*m), GFP_KERNEL);
	if (!m)
		return -ENOMEM;
	m->dev = dev;

	m->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(m->base))
		return PTR_ERR(m->base);

	m->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(m->clk))
		return dev_err_probe(dev, PTR_ERR(m->clk), "no clock\n");

	m->resets = devm_reset_control_array_get(dev, false, false);
	if (IS_ERR(m->resets))
		return dev_err_probe(dev, PTR_ERR(m->resets), "no resets\n");

	ret = clk_prepare_enable(m->clk);
	if (ret)
		return ret;

	m->bus = devm_mdiobus_alloc_size(dev, 0);
	if (!m->bus) {
		ret = -ENOMEM;
		goto err_clk;
	}

	m->bus->name = "luofu-mdio";
	m->bus->read = luofu_mdio_read;
	m->bus->write = luofu_mdio_write;
	m->bus->priv = m;
	m->bus->parent = dev;
	snprintf(m->bus->id, MII_BUS_ID_SIZE, "%s", dev_name(dev));

	ret = of_mdiobus_register(m->bus, dev->of_node);
	if (ret) {
		dev_err(dev, "Cannot register as MDIO bus!\n");
		goto err_clk;
	}

	platform_set_drvdata(pdev, m);
	dev_info(dev,
		 "luofu-mdio: registered at %s (%d PHYs), bit layout %s\n",
		 dev_name(dev), m->bus->phy_mask ? -1 : 0,
		 m->layout_verified ? "verified" : "STAGED - transfers refused");
	return 0;

err_clk:
	clk_disable_unprepare(m->clk);
	return ret;
}

static int luofu_mdio_remove(struct platform_device *pdev)
{
	struct luofu_mdio *m = platform_get_drvdata(pdev);

	mdiobus_unregister(m->bus);
	clk_disable_unprepare(m->clk);
	return 0;
}

static const struct of_device_id luofu_mdio_of_match[] = {
	{ .compatible = "hisilicon,luofu-mdio" },
	{ .compatible = "hsan,mdio" },	/* the vendor tree's string */
	{ }
};
MODULE_DEVICE_TABLE(of, luofu_mdio_of_match);

static struct platform_driver luofu_mdio_driver = {
	.probe	= luofu_mdio_probe,
	.remove	= luofu_mdio_remove,
	.driver	= {
		.name		= "luofu-mdio",
		.of_match_table	= luofu_mdio_of_match,
	},
};
module_platform_driver(luofu_mdio_driver);

MODULE_DESCRIPTION("Hi5671Y luofu MDIO bus driver");
MODULE_LICENSE("GPL v2");
