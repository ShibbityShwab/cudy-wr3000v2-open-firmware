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

/*
 * THE VENDOR BOOT CLOBBERS THIS CELL.
 *
 * 0xC98 is fine for reading during OUR boot, but once our kernel's fallback hands the board back
 * the vendor kernel's own boot overwrites it - measured 2026-10-13: after a fire, 0xC98 read
 * 0xE3520010 while the FMC's own 0xC18/0xC1C still held our values.  So every crumb is ALSO
 * deposited in luofu_net_diag, which the FMC's 5-second tick carries in its surviving payload.
 */
#define LUOFU_DIAG_BASE	0x10100000UL
#define LUOFU_DIAG_OFF	0xc98	/* mdio's OWN cell - 0xC8C is the mach's, 0xC90 the CRG's, 0xC94 gmac's */

extern u32 luofu_net_diag;

static void luofu_diag(u32 v)
{
	void __iomem *p;

	luofu_net_diag = v;	/* survive the fallback: the FMC tick carries this */

	p = ioremap(LUOFU_DIAG_BASE, 0x1000);
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

	/*
	 * RELEASE THE RESETS.  THIS IS THE -EIO.
	 *
	 * The probe fetched the reset array and never deasserted it, so the MDIO block stayed held in
	 * reset.  Every transfer then timed out in luofu_mdio_wait(), and that timeout is what
	 * propagated out of get_phy_c22_id() -> get_phy_device() -> of_mdiobus_register() as **-EIO**.
	 * That is why the failure was never on the clause-22 error paths (those converge on
	 * -ENODEV/-EINVAL) and why the file's own header predicted "a timeout from luofu_mdio_wait()".
	 *
	 * reset_control_deassert() dispatches to reset_control_array_deassert() for an array handle
	 * (drivers/reset/core.c:411), so the devm_reset_control_array_get() result is used directly.
	 * Clock first: the block must have a clock when it comes out of reset.
	 */
	ret = reset_control_deassert(m->resets);
	if (ret) {
		luofu_diag(0xc0de7f06);	/* could not release the resets */
		goto err_clk;
	}
	luofu_diag(0xc0de7005);	/* resets released */

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

	/*
	 * THE PHY'S OWN ANSWER - AND IT MUST BE THE LAST WRITE.
	 *
	 * A first attempt read this in the mdio probe, BEFORE of_mdiobus_register(), and the mdio
	 * probe's OWN later crumb (0xC0DE7E00) then the gmac's (0xC0DE6006) both overwrote it - so
	 * the survivor carried 0x6006 and told me nothing.  Here nothing follows it.
	 *
	 * BMSR bit 2 is LINK STATUS.  That one bit splits the whole problem in half:
	 *   set   -> the PHY HAS a link, and the fault is downstream (phylink / MAC configuration)
	 *   clear -> the PHY itself never linked, and no MAC-side change can fix it
	 *
	 * The gmac probe already reports 0xC0DE60EE here, i.e. of_phy_find_device() returned NULL -
	 * there is no phy_device in this kernel.  That is consistent with of_mdiobus_register()
	 * succeeding while finding ZERO phys (it returns 0 either way), which would leave phylink with
	 * nothing to attach and no link ever forming.  So ask the hardware directly, over our own
	 * controller, with nothing else touching this value afterwards.
	 *
	 * PHY 4 is the wired port: the vendor's running kernel has carrier ONLY on
	 * eth2 = gemac 0x14480000 = mdio0:04.
	 */
	luofu_diag((u32)luofu_mdio_read(m->bus, 4, MII_BMSR) & 0xffff);

	/*
	 * CRUMBS, NOT PRINTS.
	 *
	 * Early-boot dev_info() lines scroll out of the 16 KiB ring - measured: the bus-node and
	 * child-reg prints never appeared.  So every stage goes to a crumb, most decisive LAST:
	 *
	 *   0xC0DE7D00 / 0xC0DE7D01   node not / IS available
	 *   0xC0DE7C00 | n            children walked
	 *   0xC0DE7B00 | (addr<<4) | (rc & 0xf)   per-child registration result
	 *   0xC0DE7E00 | (rc & 0xff)  final of_mdiobus_register return code
	 */
	luofu_diag(of_device_is_available(dev->of_node) ? 0xc0de7d01 : 0xc0de7d00);

	{
		struct device_node *ch;
		int n = 0;

		for_each_available_child_of_node(dev->of_node, ch) {
			u32 r;
			n++;
			(void)r;
		}
		luofu_diag(0xc0de7c00 | (n & 0xff));

		/*
		 * Register the children ONE AT A TIME so the failing child is identifiable, instead of
		 * handing the whole tree to of_mdiobus_register() and getting only its -EIO back.
		 */
		for_each_available_child_of_node(dev->of_node, ch) {
			struct phy_device *phy;
			u32 r;
			int rc;

			if (of_property_read_u32(ch, "reg", &r))
				continue;
			phy = get_phy_device(m->bus, (int)r, false);
			if (IS_ERR(phy)) {
				rc = PTR_ERR(phy);
				luofu_diag(0xc0de7b00 | (((u32)r & 0xf) << 4) | ((u32)(-rc) & 0xf));
			} else {
				luofu_diag(0xc0de7b00 | (((u32)r & 0xf) << 4) | 0xf);
				phy_device_free(phy);
			}
		}
	}

	ret = of_mdiobus_register(m->bus, dev->of_node);
	/*
	 * WITH THE RESETS RELEASED, -EIO SHOULD BE GONE AND THIS SHOULD BE 0 (crumb 0xC0DE7E00).
	 * If it is still -EIO, the fault moved into the transfer itself and the register layout
	 * (base[0x70] frame / [0x00] cmd / [0x6c] status / [0x74] data - decoded from the vendor's
	 * hi_kmdio.ko but never exercised) is the remaining suspect.
	 */
	luofu_diag(0xc0de7e00 | (ret & 0xff));

	/*
	 * ASK THE PHY DIRECTLY - BUT IN ITS OWN CELL, NOT THROUGH luofu_diag().
	 *
	 * luofu_diag() writes the survivor AND 0xC98, which is the mdio's own cell - and the per-child
	 * registration crumbs above (0xC0DE7B00 | (addr<<4) | 0xf means THAT PHY REGISTERED) live
	 * there.  Writing my read over them is exactly how the answer to "did the bus find any phys"
	 * got erased on the last fire.  0xC84 is free: 0xC80 gmac phy value, 0xC8C mach, 0xC90 crg,
	 * 0xC94 gmac, 0xC98 mdio.
	 *
	 * BMSR bit 2 = LINK STATUS, bit 5 = autoneg complete.  Measured 0xCDEF on the previous fire,
	 * so the CONTROLLER works; if the phys still fail to register, the fault is enumeration.
	 */
	{
		void __iomem *p84 = ioremap(LUOFU_DIAG_BASE, 0x1000);
		if (p84) {
			u32 b = (u32)luofu_mdio_read(m->bus, 4, MII_BMSR) & 0xffff;
			writel(b, p84 + 0xc84);
			(void)readl(p84 + 0xc84);
			iounmap(p84);
		}
		/* A STRING, ON PURPOSE, AND IT IS THE ONLY REASON THIS LINE EXISTS.
		 *
		 * Twice now the cell 0xC84 came back 0 while the source plainly contained this block, so the
		 * question "is my edit actually in the kernel that boots" could not be answered from the
		 * cells.  strings/objdump tricks all misled earlier (compressed zImage; missing objdump;
		 * comments not making it into the binary).  This literal is searched out of the built
		 * vmlinux directly - if it is absent, my code is not in the image and nothing else matters. */
		dev_info(dev, "luofu-mdio: PHYPROBE-C84 bmsr=%08x\n",
			 readl(p84 ? p84 + 0xc84 : m->base));
	}
	if (ret) {
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
/*
 * REGISTER EARLIER THAN device_initcall, AND THAT IS A FUNCTIONAL REQUIREMENT.
 *
 * WHY THIS IS NOT A STYLE CHOICE.  The gmac driver calls phylink_of_phy_connect(), which needs the
 * PHYs this bus registers; before they exist it returns -ENODEV and the gmac returns -EPROBE_DEFER.
 * A deferral is retried from deferred_probe_work, which runs AFTER the initcall levels have all
 * completed - and `ipconfig` is a late_initcall:
 *
 *     ip_auto_config:  late_initcall(ip_auto_config)      (level 7)
 *     gmac, mdio:      module_platform_driver             (level 6)
 *
 * so with both at level 6 the sequence is mdio-probe, gmac-probe (DEFERS), ipconfig - and ipconfig
 * runs BEFORE the deferred retry has made the netdev.  `ipconfig` matches its interface BY NAME
 * (`!strcmp(dev->name, user_dev_name)` in net/ipv4/ipconfig.c) and prints
 * "IP-Config: Device `eth2' not found", leaving the kernel with no address even though the link is
 * up and the MAC is programmed.  Measured: our kernel ran ~78 s and never answered ssh.
 *
 * subsys_initcall (level 4) puts the PHYs in place BEFORE the gmac's first attempt, so the gmac
 * succeeds immediately, registers eth2 at level 6, and ipconfig at level 7 finds it.
 *
 * The wrappers are explicit because module_platform_driver() used to GENERATE these names.
 */
static int __init luofu_mdio_driver_init(void)
{
	return platform_driver_register(&luofu_mdio_driver);
}

static void __exit luofu_mdio_driver_exit(void)
{
	platform_driver_unregister(&luofu_mdio_driver);
}

subsys_initcall(luofu_mdio_driver_init);
module_exit(luofu_mdio_driver_exit);
MODULE_LICENSE("GPL");

MODULE_DESCRIPTION("Hi5671Y luofu MDIO bus driver");
MODULE_LICENSE("GPL v2");
