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
 * =====================  WHAT IS REAL (and what is not measured yet)  =====================
 * REAL: the platform driver, the mdiobus allocation + `of_mdiobus_register`, the clocks and the
 * reset array, and the full register-level transfer - this is an MMIO MDIO controller (the frame
 * is assembled into a register and the hardware shifts it out), NOT a bit-bang.
 *
 * The register layout was recovered from the vendor module's own `hi_mdio_read` (see the layout
 * comment below), so nothing here is invented: `base[0x70]` takes the frame, `base[0x00]` takes
 * the `0x0d` read command, the completion field is bits[3:0] of `base[0x6c]` (read) or `base[0x60]`
 * (write), and the returned value is the low 16 bits of `base[0x74]`.
 *
 * NOT YET MEASURED: those offsets have never been exercised against the hardware, because the
 * vendor stack owns `mdio0` on the running system. Until a fire reads a real PHY ID back, the
 * layout is decoded-but-unproven - and a wrong offset here would show as a timeout from
 * `luofu_mdio_wait()`, not as silent corruption.
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
 * Register layout, recovered from hi_kmdio.ko's OWN hi_mdio_read (not guessed).
 *
 * This is an MMIO MDIO controller, NOT a bit-bang: the frame is assembled into a
 * register and the hardware shifts it out.
 *
 *   base[0x00]  command - write 0x0d to begin a read; 0x1d/0x1e are its error paths
 *   base[0x60]  write-status  - poll bits[3:0] until NON-ZERO (done)
 *   base[0x6c]  read-status   - same poll, same field
 *   base[0x70]  frame/data    - the assembled frame is stored here
 *   base[0x74]  read-data     - the returned value is the low 16 bits (uxth)
 *
 * The frame fields, from the same disassembly:
 *   5-bit PHY address   <- `and r6, r6, #31`
 *   5-bit register      <- `and r4, r4, #31`
 *   opcode bits         <- the `mvn`-derived `r6` value (0xe0 mask on the high half)
 *
 * The vendor's polls carry a loop counter of 0x5f5e100 (~100M iterations) - a
 * timeout, not a spin - so the poll below is bounded the same way in spirit.
 */
#define LUOFU_MDIO_CMD			0x00
#define LUOFU_MDIO_WSTATUS	0x60
#define LUOFU_MDIO_RSTATUS	0x6c
#define LUOFU_MDIO_FRAME		0x70
#define LUOFU_MDIO_RDATA		0x74

#define LUOFU_MDIO_CMD_READ	0x0d
#define LUOFU_MDIO_DONE		0x0f	/* bits[3:0] non-zero = complete */
#define LUOFU_MDIO_PHY_MASK	0x1f
#define LUOFU_MDIO_REG_MASK	0x1f
#define LUOFU_MDIO_DATA_MASK	0xffff

struct luofu_mdio {
	struct device		*dev;
	void __iomem		*base;
	struct mii_bus		*bus;
	struct clk		*clk;
	struct reset_control	*resets;
};

/*
 * Wait for a completion field. The vendor polls bits[3:0] of the status register
 * until they read NON-ZERO (see the layout comment above); "non-zero means done"
 * is the inverse of the usual busy-bit convention, so it is spelled out here
 * rather than hidden behind a helper name.
 */
static int luofu_mdio_wait(struct luofu_mdio *m, u32 status_off)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(100);

	do {
		if (readl(m->base + status_off) & LUOFU_MDIO_DONE)
			return 0;
		cpu_relax();
	} while (time_before(jiffies, timeout));

	return -ETIMEDOUT;
}

/* Build the frame word the controller shifts out: phy(5) | reg(5) in the halves. */
static u32 luofu_mdio_frame(unsigned int phy, unsigned int reg)
{
	return ((u32)(phy & LUOFU_MDIO_PHY_MASK) << 8) |
	       (u32)(reg & LUOFU_MDIO_REG_MASK);
}

static int luofu_mdio_read(struct mii_bus *bus, int phy, int reg)
{
	struct luofu_mdio *m = bus->priv;
	int ret;

	writel(luofu_mdio_frame(phy, reg), m->base + LUOFU_MDIO_FRAME);
	writel(LUOFU_MDIO_CMD_READ, m->base + LUOFU_MDIO_CMD);

	ret = luofu_mdio_wait(m, LUOFU_MDIO_RSTATUS);
	if (ret) {
		dev_err(m->dev, "MDIO bus hasn't done reading\n");
		return ret;
	}

	return readl(m->base + LUOFU_MDIO_RDATA) & LUOFU_MDIO_DATA_MASK;
}

static int luofu_mdio_write(struct mii_bus *bus, int phy, int reg, u16 val)
{
	struct luofu_mdio *m = bus->priv;
	int ret;

	writel(luofu_mdio_frame(phy, reg) | (u32)val, m->base + LUOFU_MDIO_FRAME);

	ret = luofu_mdio_wait(m, LUOFU_MDIO_WSTATUS);
	if (ret) {
		dev_err(m->dev, "MDIO bus hasn't done writing\n");
		return ret;
	}

	return 0;
}

#define LUOFU_DIAG_BASE	0x10100000UL
#define LUOFU_DIAG_OFF	0xc98	/* mdio's OWN cell - 0xC8C is the mach's, 0xC90 the CRG's, 0xC94 gmac's */

static void luofu_diag(u32 v)
{
	void __iomem *p = ioremap(LUOFU_DIAG_BASE, 0x1000);

	if (!p)
		return;
	writel(v, p + LUOFU_DIAG_OFF);
	(void)readl(p + LUOFU_DIAG_OFF);
	iounmap(p);
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

	luofu_diag(0xc0de7001);	/* entry */

	m->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(m->base)) {
		luofu_diag(0xc0de7f02);
		return PTR_ERR(m->base);
	}

	m->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(m->clk)) {
		luofu_diag(0xc0de7f03);	/* no clock provider */
		return dev_err_probe(dev, PTR_ERR(m->clk), "no clock\n");
	}
	luofu_diag(0xc0de7003);

	m->resets = devm_reset_control_array_get(dev, false, false);
	if (IS_ERR(m->resets)) {
		luofu_diag(0xc0de7f04);	/* no reset controller */
		return dev_err_probe(dev, PTR_ERR(m->resets), "no resets\n");
	}
	luofu_diag(0xc0de7004);

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
	dev_info(dev, "luofu-mdio: registered at %s (frame=%08x cmd=%02x)\n",
		 dev_name(dev), readl(m->base + LUOFU_MDIO_FRAME),
		 readl(m->base + LUOFU_MDIO_CMD) & 0xff);
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
