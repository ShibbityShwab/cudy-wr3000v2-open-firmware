// SPDX-License-Identifier: GPL-2.0
/*
 * luofu-gmac: stage-3 GEMAC driver FRAME for the Hi5671Y "luofu" SoC
 * (DT compatible "hisilicon,luofu-gmac"; the vendor binds "hsan,mac").
 *
 * ==========================  WHAT THIS IS  ==========================
 * The DT has carried the five MAC nodes since the first device tree
 * (`gmac0..4`, luofu-r116.dts:408-468, all status="disabled"), each with its
 * reg, phy-mode, clocks and the two resets - and there was no driver.
 *
 * The vendor's own module (`hi_kmac.ko`, 75,116 B) was decoded before writing
 * this (build/tmp/lane-accounting.md, C7): it is NOT a proprietary MAC stack.
 * It is a **phylink** driver - phylink_create/start/of_phy_connect/mii_ioctl -
 * over clk_bulk_* and devm_reset_control_array_get, with plain `readl`-shaped
 * register access (`hi_reg_hw_read` is literally `ldr r3,[r0,r1]; str r3,[r2]`).
 * So the port is: a platform_driver + net_device + phylink whose register
 * accessors match the vendor's.  That is what this file is.
 *
 * =======================  WHAT IS REAL VS STAGED  =======================
 * REAL (compiles against vanilla 5.10.201 arm headers; no device needed):
 *   - module_init + platform_driver + of_match ("hisilicon,luofu-gmac" and
 *     the vendor's "hsan,mac" so the same driver binds either tree);
 *   - reg mapping; clk_bulk_get_all + reset_control_array_get (the DT already
 *     carries both, so probe fails cleanly on a tree that lacks them);
 *   - a full net_device: alloc_etherdev, ndo_open/stop, ethtool
 *     get_link/get_drvinfo, and netdev_ops;
 *   - the MAC register accessors, dword-only (the SoC's windows answer 32-bit
 *     aligned accesses; a sub-word access external-aborts - see the pcie and
 *     wifi drivers for the same rule);
 *   - phylink_create/phylink_of_phy_connect/phylink_start/stop wired to the
 *     DT's phy-mode and phy-handle, with the MAC-PCS link callback.
 *
 * STAGED (needs the device; named so the next step is unambiguous):
 *   - the MAC enable bit and the TX/RX config words: hi_gemac_probe's values
 *     are recoverable from hi_kmac.ko but are NOT reproduced here, because a
 *     write to them has never been measured on this bench.  They sit behind
 *     LUOFU_GMAC_STAGE_WRITES, off by default - the same discipline every other
 *     luofu driver uses: read-only until a measurement justifies a store.
 *   - the MDIO bus registration (hi_net_mii_register): the DT declares mdio0
 *     with 5 PHYs; until that driver exists, use a fixed-link or the
 *     phy-handle path.
 *
 * It performs NO register writes on the default path.
 */

#include <linux/clk.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_mdio.h>	/* of_phy_find_device - WITHOUT THIS THE FILE DOES NOT COMPILE */
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/phylink.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/rtnetlink.h>	/* rtnl_lock/rtnl_unlock - WITHOUT THIS THE FILE DOES NOT COMPILE */

/* GEMAC register window (dword-aligned; see the file header). */
#define LUOFU_GMAC_CTRL		0x00	/* MAC control: the enable bit lives here */
#define LUOFU_GMAC_STATUS	0x04	/* link/duplex/speed status */
#define LUOFU_GMAC_TX_IFG	0x08	/* inter-frame gap (hi_mac_hw_set_tx_ifg) */
#define LUOFU_GMAC_TX_PRE	0x0c	/* preamble (hi_mac_hw_set_tx_preamble) */

#define LUOFU_GMAC_MAC_EN	BIT(0)

struct luofu_gmac {
	struct net_device	*ndev;
	struct device		*dev;
	void __iomem		*base;
	struct clk_bulk_data	*clks;
	int			nclks;
	struct reset_control	*resets;
	struct phylink		*phylink;
	struct phylink_config	pl_config;
};

static u32 luofu_gmac_read(struct luofu_gmac *g, u32 off)
{
	return readl(g->base + off);
}

static void luofu_gmac_write(struct luofu_gmac *g, u32 off, u32 val)
{
	writel(val, g->base + off);
}

/*
 * luofu_gmac_write() is the store counterpart every luofu driver keeps ready for
 * the measured write path; this driver has none yet (see the file header), so
 * keep the compiler from calling it dead while the staged path lands.
 */
static void __maybe_unused
luofu_gmac_write_ready(struct luofu_gmac *g, u32 off, u32 val)
{
	luofu_gmac_write(g, off, val);
}

/* ---- phylink glue ---- */

/*
 * Defined further down; declared here because mac_link_up() uses it.
 * WITHOUT THIS THE BUILD FAILED and four fires in a row wrote a STALE image - the arm
 * toolchain rejects the implicit declaration, so nothing after this line ever shipped.
 */
static void luofu_diag(u32 v);

static struct luofu_gmac *pl_to_gmac(struct phylink_config *cfg)
{
	return container_of(cfg, struct luofu_gmac, pl_config);
}

/*
 * THE MAC ONLY PASSES TRAFFIC ONCE ITS PORT MODE IS PROGRAMMED.
 *
 * These were empty stubs: the netdev registered, a real PHY attached, and the MAC was never told
 * the link speed - so nothing flowed. That is exactly "looks healthy, carries nothing".
 *
 * The offsets and the algorithm come from the MAINLINE driver for this IP,
 * drivers/net/ethernet/hisilicon/hix5hd2_gmac.c (present in this kernel tree):
 *
 *     #define PORT_MODE		0x0040		<- matches the silicon
 *     #define MODE_CHANGE_EN	0x01b4
 *     #define BIT_MODE_CHANGE_EN	BIT(0)
 *
 * and its hix5hd2_config_port() writes PORT_MODE with the mode-change bit ENABLED around the
 * write:  mode_change = BIT(0) -> PORT_MODE = speed -> mode_change = 0.
 *
 * THE VALUES ARE MEASURED, NOT PORTED.  This silicon is a different revision from the mainline
 * one, and its encoding differs: mainline's GMAC_SPEED_1000 is 0x05, while a live 1 Gbps link on
 * THIS board reads
 *
 *     0x14380000 (eth0, carrier 0)  PORT_MODE = 0x000C0408
 *     0x14480000 (eth2, carrier 1)  PORT_MODE = 0x010C0008   <- 1 Gbps, link up
 *
 * read straight off the vendor's running kernel through /dev/mem.  So the link-up value below is
 * the one this hardware actually produced, not a copy of another revision's table.
 */
#define LUOFU_GMAC_PORT_MODE		0x0040
#define LUOFU_GMAC_MODE_CHANGE_EN	0x01b4
#define LUOFU_GMAC_MODE_CHANGE_BIT	BIT(0)

/* measured on the vendor's live 1 Gbps link (see above) */
#define LUOFU_PORT_MODE_1000		0x010c0008

static void luofu_gmac_mac_config(struct phylink_config *cfg,
				  unsigned int mode,
				  const struct phylink_link_state *st)
{
	struct luofu_gmac *g = pl_to_gmac(cfg);

	/*
	 * DELIBERATELY DOES NOT TOUCH THE MAC.
	 *
	 * phylink calls mac_config() during SETUP - phylink_create() calls phylink_validate(), and the
	 * first mac_config() can happen from the probe path, long before a PHY exists or the link has
	 * negotiated anything.  Writing the MAC there cost a boot: an access to this block before it
	 * is ready hangs the AHB bus, and the box never reached its first timer tick (crumb 0xC18
	 * still held the poison, no userspace, no ring lines).
	 *
	 * THE MODE CHANGE BELONGS IN mac_link_up(), which is what the mainline driver does too -
	 * hix5hd2_config_port() is called from hix5hd2_adjust_link(), the LINK callback, never from a
	 * config-time path.
	 *
	 * mac_config is also the right place for genuinely static configuration (RGMII delay, interface
	 * mode); none of that is measured for this silicon yet, so it stays a no-op rather than a guess.
	 */
	(void)g;
	(void)mode;
	(void)st;
}

static void luofu_gmac_mac_link_down(struct phylink_config *cfg,
				     unsigned int mode, phy_interface_t iface)
{
	struct luofu_gmac *g = pl_to_gmac(cfg);

	(void)mode;
	(void)iface;

	/* Clear the MAC enable: the mainline driver resets the interface on link down. */
	writel(0, g->base + LUOFU_GMAC_CTRL);
}

static void luofu_gmac_mac_link_up(struct phylink_config *cfg,
				   struct phy_device *phy,
				   unsigned int mode, phy_interface_t iface,
				   int speed, int duplex, bool tx_pause, bool rx_pause)
{
	struct luofu_gmac *g = pl_to_gmac(cfg);

	(void)phy;
	(void)mode;
	(void)iface;
	(void)tx_pause;
	(void)rx_pause;

	/*
	 * Order matters and follows the mainline driver: configure the PORT MODE first, then enable
	 * the MAC.  Enabling before the mode is right would let the MAC drive a mis-configured port.
	 */
	if (speed == SPEED_1000) {
		writel(LUOFU_GMAC_MODE_CHANGE_BIT, g->base + LUOFU_GMAC_MODE_CHANGE_EN);
		writel(LUOFU_PORT_MODE_1000, g->base + LUOFU_GMAC_PORT_MODE);
		writel(0, g->base + LUOFU_GMAC_MODE_CHANGE_EN);
	}

	/* Enable the MAC - the bit this driver already defines as living in LUOFU_GMAC_CTRL. */
	writel(LUOFU_GMAC_MAC_EN, g->base + LUOFU_GMAC_CTRL);

	/*
	 * AND LEAVE A MARK, so a fire says whether this path ran at all: 0xC94 is the gmac's own diag
	 * cell.  0xC0DE6100|speed means link_up executed; the speed nibble distinguishes 1G/100/10.
	 */
	luofu_diag(0xc0de6100 | ((speed == SPEED_1000) ? 1 : (speed == SPEED_100 ? 2 : 3)));
}

/*
 * THIS TREE'S phylink REQUIRES mac_ops->validate.
 *
 * drivers/net/phy/phylink.c here is the older API:
 *
 *     static int phylink_validate(struct phylink *pl, unsigned long *supported, ...)
 *     {
 *             pl->mac_ops->validate(pl->config, supported, state);
 *             return phylink_is_empty_linkmode(supported) ? -EINVAL : 0;
 *     }
 *
 * It calls the op unconditionally, and phylink_create() calls phylink_validate() before it returns.
 * Our ops table had only mac_config/mac_link_down/mac_link_up, so that call went through a NULL
 * pointer - the prefetch abort at phylink_validate+0x18 with the trace
 *
 *     __pabt_svc <- phylink_validate+0x18 <- phylink_create+0x134 <- luofu_gmac_probe+0x128
 *
 * Fill in the supported/advertising masks for an RGMII MAC behind a PHY, and respect the interface
 * the caller asks for.
 */
static void luofu_gmac_validate(struct phylink_config *cfg,
				unsigned long *supported,
				struct phylink_link_state *state)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask) = { 0, };

	switch (state->interface) {
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
	case PHY_INTERFACE_MODE_GMII:
		break;
	default:
		/* Unknown mode for this MAC: report nothing so phylink_validate() returns -EINVAL
		 * rather than proceeding on an empty mask. */
		linkmode_zero(supported);
		return;
	}

	phylink_set(mask, MII);
	phylink_set(mask, 10baseT_Half);
	phylink_set(mask, 10baseT_Full);
	phylink_set(mask, 100baseT_Half);
	phylink_set(mask, 100baseT_Full);
	phylink_set(mask, 1000baseT_Full);
	phylink_set(mask, Pause);
	phylink_set(mask, Asym_Pause);

	bitmap_and(supported, supported, mask, __ETHTOOL_LINK_MODE_MASK_NBITS);
	bitmap_and(state->advertising, state->advertising, mask,
		   __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static const struct phylink_mac_ops luofu_gmac_phylink_ops = {
	.validate	= luofu_gmac_validate,
	.mac_config	= luofu_gmac_mac_config,
	.mac_link_down	= luofu_gmac_mac_link_down,
	.mac_link_up	= luofu_gmac_mac_link_up,
};

/* ---- net_device ---- */

static int luofu_gmac_open(struct net_device *ndev)
{
	struct luofu_gmac *g = netdev_priv(ndev);
	int ret;

	ret = clk_bulk_prepare_enable(g->nclks, g->clks);
	if (ret)
		return ret;

	phylink_start(g->phylink);
	netif_start_queue(ndev);
	return 0;
}

static int luofu_gmac_stop(struct net_device *ndev)
{
	struct luofu_gmac *g = netdev_priv(ndev);

	netif_stop_queue(ndev);
	phylink_stop(g->phylink);
	clk_bulk_disable_unprepare(g->nclks, g->clks);
	return 0;
}

static netdev_tx_t luofu_gmac_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	/* No TX path yet: the rings are staged (see the DMA notes in the header). */
	dev_kfree_skb(skb);
	ndev->stats.tx_dropped++;
	return NETDEV_TX_OK;
}

static void luofu_gmac_get_stats64(struct net_device *ndev,
				   struct rtnl_link_stats64 *stats)
{
	/* No hardware counters mapped yet; report the software view. */
	(void)stats;
}

static const struct net_device_ops luofu_gmac_netdev_ops = {
	.ndo_open		= luofu_gmac_open,
	.ndo_stop		= luofu_gmac_stop,
	.ndo_start_xmit		= luofu_gmac_xmit,
	.ndo_get_stats64	= luofu_gmac_get_stats64,
};

static void luofu_gmac_get_drvinfo(struct net_device *ndev,
				   struct ethtool_drvinfo *info)
{
	strscpy(info->driver, "luofu-gmac", sizeof(info->driver));
}

static const struct ethtool_ops luofu_gmac_ethtool_ops = {
	.get_drvinfo	= luofu_gmac_get_drvinfo,
	.get_link	= ethtool_op_get_link,
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
};

/* ---- platform driver ---- */

/*
 * Driver-level diagnostic, deposited in sysctrl 0xC8C.
 *
 * 0xC18 and 0xC1C are NOT available: they are the FMC's crumb/payload pair and its tick rewrites
 * both every 5 s.  0xC8C is free, writable and sticky across a reboot.
 *
 * Last write wins, so reading it back says HOW FAR THE PROBE GOT:
 *   0xC0DE60NN  gmac reached step NN
 *   0xC0DE6FNN  gmac FAILED at step NN
 * (mdio uses 0xC0DE70NN / 0xC0DE7FNN.)  Without this the only visible symptom was "no eth0",
 * which cannot distinguish "never probed" from "probed and bailed at the reset lookup".
 */
/*
 * THE VENDOR BOOT CLOBBERS THIS CELL.
 *
 * 0xC94 is fine for reading during OUR boot, but once our kernel's fallback hands the board back
 * the vendor kernel's own boot overwrites it.  So every crumb is ALSO deposited in luofu_net_diag,
 * which the FMC's 5-second tick carries in its surviving payload (0xC1C) - the only channel that
 * outlives a fallback.
 */
#define LUOFU_DIAG_BASE	0x10100000UL
#define LUOFU_DIAG_OFF	0xc94	/* gmac's OWN cell - 0xC8C is the mach's, 0xC90 the CRG's, 0xC98 mdio's */

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

static int luofu_gmac_probe(struct platform_device *pdev)
{
	struct luofu_gmac *g;
	struct net_device *ndev;
	phy_interface_t iface;
	int ret;

	/*
	 * THE SWITCH, IF THIS IS ONE - make it alive, then stop.
	 *
	 * Enable its clock and release its resets; register no netdev (the MACs do that) and touch no
	 * register.  Without this the gmii PHYs behind the switch never negotiate.
	 */
	if (of_device_is_compatible(pdev->dev.of_node, "hisilicon,luofu-dp") ||
	    of_device_is_compatible(pdev->dev.of_node, "hisilicon,luofu-pfe")) {
		struct clk *ck = devm_clk_get(&pdev->dev, NULL);
		struct reset_control *rs;

		if (!IS_ERR(ck))
			clk_prepare_enable(ck);

		rs = devm_reset_control_array_get(&pdev->dev, false, true);
		if (!IS_ERR(rs))
			reset_control_deassert(rs);

		luofu_diag(of_device_is_compatible(pdev->dev.of_node,
						   "hisilicon,luofu-dp") ?
			   0xc0de6201 : 0xc0de6202);
		return 0;
	}

	ndev = alloc_etherdev(sizeof(*g));
	if (!ndev)
		return -ENOMEM;

	SET_NETDEV_DEV(ndev, &pdev->dev);
	g = netdev_priv(ndev);
	g->ndev = ndev;
	g->dev = &pdev->dev;
	platform_set_drvdata(pdev, g);

	luofu_diag(0xc0de6001);	/* entry: the probe was reached at all */

	g->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(g->base)) {
		ret = PTR_ERR(g->base);
		luofu_diag(0xc0de6f02);
		goto err_free;
	}
	luofu_diag(0xc0de6002);

	g->nclks = devm_clk_bulk_get_all(&pdev->dev, &g->clks);
	if (g->nclks < 0) {
		ret = g->nclks;
		luofu_diag(0xc0de6f03);	/* no clock provider to attach to */
		goto err_free;
	}
	luofu_diag(0xc0de6003 | (g->nclks & 0xf));

	g->resets = devm_reset_control_array_get(&pdev->dev, false, false);
	if (IS_ERR(g->resets)) {
		ret = PTR_ERR(g->resets);
		luofu_diag(0xc0de6f04);	/* no reset controller (optional=false -> fatal) */
		goto err_free;
	}
	luofu_diag(0xc0de6004);

	/*
	 * RELEASE THE RESETS - the same defect the mdio driver had.
	 *
	 * The resets were fetched and never deasserted, so the MAC stayed held in reset: no transfer,
	 * no PHY, no link.  The clocks are already enabled above (clk_bulk_prepare_enable), which is
	 * the order this needs - a clock before the block comes out of reset.
	 *
	 * reset_control_deassert() dispatches to reset_control_array_deassert() for an array handle
	 * (drivers/reset/core.c:411), so the devm_reset_control_array_get() result is used directly.
	 */
	ret = reset_control_deassert(g->resets);
	if (ret) {
		luofu_diag(0xc0de6f06);	/* could not release the resets */
		goto err_free;
	}
	luofu_diag(0xc0de6013);	/* resets released (gmac's own code - must not collide with mdio's 0xC0DE7005) */

	ret = of_get_phy_mode(pdev->dev.of_node, &iface);
	if (ret)
		iface = PHY_INTERFACE_MODE_NA;

	g->pl_config.dev = &pdev->dev;
	/*
	 * PHYLINK_DEV, NOT PHYLINK_NETDEV.
	 *
	 * This tree's phylink_create() does:
	 *
	 *     if (config->type == PHYLINK_NETDEV)
	 *             pl->netdev = to_net_dev(config->dev);   // container_of(dev, struct net_device, dev)
	 *
	 * and we hand it &pdev->dev - a PLATFORM device.  container_of() on that yields a bogus
	 * net_device *, which phylink_validate() then dereferences a few lines later:
	 *
	 *     BUG at phylink_validate+0x18/0x2c
	 *       phylink_validate <- phylink_create <- luofu_gmac_probe+0x128
	 *
	 * config->dev is the *device* form here, so declare it as such.
	 */
	g->pl_config.type = PHYLINK_DEV;
g->phylink = phylink_create(&g->pl_config, pdev->dev.fwnode, iface,
				    &luofu_gmac_phylink_ops);
	if (IS_ERR(g->phylink)) {
		ret = PTR_ERR(g->phylink);
		goto err_free;
	}

	ndev->netdev_ops = &luofu_gmac_netdev_ops;
	ndev->ethtool_ops = &luofu_gmac_ethtool_ops;

	ret = phylink_of_phy_connect(g->phylink, pdev->dev.of_node, 0);
	/*
	 * PUT THE ERROR WHERE IT CAN BE SEEN - AND IT IS THE WHOLE QUESTION NOW.
	 *
	 * This call DOES NOT DEFER, so it took the dev_warn path below: its return was neither
	 * -ENODEV nor -EPROBE_DEFER, then the probe continued to register_netdev with NO PHY.  The
	 * crumb for it (0xC0DE6F05|ret) is written BEFORE the value that later overwrites the cell,
	 * so its cause has never actually been visible.
	 *
	 * 0xC88 is written by nothing else, and unlike 0xC80 it demonstrably accepts writes (0xC84,
	 * written by the mdio driver, holds a live value where 0xC80 holds stale silicon state).
	 * Encoding: 0xE0000000 | (ret as two's-complement 28 bits) - any value starting 0xE is an
	 * error code; 0x00000000 means it succeeded.
	 */
	{
		void __iomem *pe = ioremap(LUOFU_DIAG_BASE, 0x1000);
		if (pe) {
			writel(0xe0000000u | ((u32)ret & 0x0fffffffu), pe + 0xc88);
			(void)readl(pe + 0xc88);
			iounmap(pe);
		}
	}
	if (ret) {
		luofu_diag(0xc0de6f05 | (ret & 0xf));	/* phy_connect FAILED */

		/*
		 * DEFER, DO NOT SETTLE FOR A PHY-LESS NETDEV.
		 *
		 * -ENODEV here is almost certainly NOT "this board has no PHY": the DT carries phy-handle
		 * and mdio0 is a SEPARATE DRIVER, so of_phy_find_device() returns NULL whenever the gmac is
		 * probed before mdio0 has registered its PHYs.  Measure: the mdio driver's crumb is the one
		 * that survives in 0xC1C, i.e. mdio probes LAST - so the gmac's connect ran while the PHY
		 * was still absent, failed with -ENODEV, and the old code kept a netdev with no PHY
		 * forever ("a later mdio0 can bind it" was never implemented - nothing re-connects).
		 *
		 * Returning -EPROBE_DEFER makes the driver core retry this probe after mdio0 is up, which
		 * is the standard way to express this dependency.
		 */
		if (ret == -ENODEV || ret == -EPROBE_DEFER) {
			luofu_diag(0xc0de6f07);	/* deferring: waiting for mdio0 to register the PHYs */
			ret = -EPROBE_DEFER;
			/*
			 * A DEFERRAL IS RETRIED, so release what this attempt enabled.  The clocks are
			 * refcounted in the CCF and err_free does not disable them, so without this every
			 * retry would leave another enable behind (harmless functionally - the clock just
			 * stays on - but wrong, and it would hide a genuine leak later).
			 */
			clk_bulk_disable_unprepare(g->nclks, g->clks);
			goto err_phylink;
		}
		dev_warn(&pdev->dev, "no PHY (phy-handle/fixed-link?): %d\n", ret);
	} else {
		luofu_diag(0xc0de6005);		/* phy_connect OK */
	}

	ret = register_netdev(ndev);
	if (ret)
		goto err_phylink;
	luofu_diag(0xc0de6006);		/* netdev registered */

	/*
	 * OPEN IT.  THIS IS WHY THE LINK NEVER CAME UP.
	 *
	 * MEASURED: phylink_of_phy_connect() returns 0 here (0xC88 = 0) - the PHY is found, attached,
	 * and the netdev registered.  But phylink only starts its PHY state machine from phylink_start(),
	 * which runs from ndo_open.  Nothing opened the interface, so the PHY was never polled, the
	 * link (which the PHY itself reports as up - BMSR 0xCDEF) was never noticed, mac_link_up never
	 * ran, and the MAC was never told its speed.  That is the whole remaining gap.
	 *
	 * The rootfs is the vendor's and its network config names the vendor's interfaces, so relying
	 * on userspace to bring these up is not something this bring-up can assume.  Opening it here
	 * makes the PHY start negotiating at probe time, which is what the bench needs.
	 */
	rtnl_lock();
	dev_open(ndev, NULL);
	rtnl_unlock();

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
	 * The PHY ADDRESS is carried alongside because FIVE macs probe and the last one wins: without
	 * it I cannot tell which PHY answered.  PHY 4 is the wired port (the vendor's running kernel
	 * has carrier ONLY on eth2 = gemac 0x14480000 = mdio0:04).
	 */
	{
		struct phy_device *pd = of_phy_find_device(pdev->dev.of_node);
		u32 _val = 0xc0de60ee;
		if (pd) {
			int bmsr = phy_read(pd, MII_BMSR);
			_val = ((u32)pd->mdio.addr << 16) |
			       (bmsr < 0 ? (0x8000 | ((-bmsr) & 0x7fff)) : (bmsr & 0xffff));
		}
		luofu_diag(_val);

		/*
		 * AND ONE CELL PER MAC, BECAUSE THE LAST WRITER WAS HIDING THE OTHERS.
		 *
		 * Four macs probe this driver and they all wrote the SAME cell, so a NULL from the last one
		 * made every earlier success invisible - which is exactly how "of_phy_find_device returns
		 * NULL" got read as "there is no phy anywhere" instead of "one mac cannot resolve its phy".
		 *
		 * Cell = 0xC80 + 4*(index), index by reg base: 14380000->0, 14400000->1, 14480000->2,
		 * 14500000->3.  0xC80/C84/C88/0xC8C are the free diagnostics; gmac0 (14300000) is disabled
		 * so it never indexes negatively.  A value of 0xC0DE60EE in a cell = that mac got NULL;
		 * anything else carries (phyaddr<<16)|BMSR for THAT mac.
		 */
		{
			/* UNCONDITIONAL, NO INDEX MATH.  An indexed version left all four cells holding values
			 * from ancient experiments, so the expression or its guard was the suspect.  This is the
			 * simplest thing that can work: if the block runs, 0xC80 changes. */
			void __iomem *pm = ioremap(LUOFU_DIAG_BASE, 0x1000);
			if (pm) {
				writel(_val, pm + 0xc80);
				(void)readl(pm + 0xc80);
				iounmap(pm);
			}
		}

		dev_info(&pdev->dev, "luofu-gmac: PHYLOOKUP base=%08x phy=%s val=%08x\n",
			 (unsigned)(pdev->resource[0].start),
			 pd ? dev_name(&pd->mdio.dev) : "NULL", _val);
	}

	dev_info(&pdev->dev,
		 "luofu-gmac: %s registered, iface=%s, %d clk(s), MAC ctrl=0x%08x\n",
		 ndev->name, phy_modes(iface), g->nclks, luofu_gmac_read(g, LUOFU_GMAC_CTRL));
	return 0;

err_phylink:
	phylink_disconnect_phy(g->phylink);
	phylink_destroy(g->phylink);
err_free:
	free_netdev(ndev);
	return ret;
}

static int luofu_gmac_remove(struct platform_device *pdev)
{
	struct luofu_gmac *g = platform_get_drvdata(pdev);

	unregister_netdev(g->ndev);
	phylink_disconnect_phy(g->phylink);
	phylink_destroy(g->phylink);
	free_netdev(g->ndev);
	return 0;
}

static const struct of_device_id luofu_gmac_of_match[] = {
	{ .compatible = "hisilicon,luofu-gmac" },
	{ .compatible = "hsan,mac" },	/* the vendor tree's string */
	/*
	 * THE INTERNAL SWITCH IS BOUND HERE TOO, AND ONLY TO BE MADE ALIVE.
	 *
	 * The wired path is gmac -> internal LSW -> PHY -> RJ45 (the DT's own note: "gemac1..4 are
	 * GMII into the internal LSW").  Both switch blocks sat `disabled` with nothing binding them,
	 * so their gate clocks were never enabled and their resets never released - and the gmii PHYs
	 * behind the switch never negotiated a link.  That is what "MAC registered, PHY attached, no
	 * carrier" looked like: measured on the vendor, exactly one MAC carries carrier and it is a
	 * gmii port into this switch.
	 *
	 * This driver does NOT configure the switch.  The vendor's own silicon comes up with usable
	 * defaults once the block is clocked and out of reset, and the goal here is reachability, not
	 * switch features.  Same minimal treatment that closed the MDIO -EIO.
	 */
	{ .compatible = "hisilicon,luofu-dp" },
	{ .compatible = "hisilicon,luofu-pfe" },
	{ }
};
MODULE_DEVICE_TABLE(of, luofu_gmac_of_match);

static struct platform_driver luofu_gmac_driver = {
	.probe	= luofu_gmac_probe,
	.remove	= luofu_gmac_remove,
	.driver	= {
		.name		= "luofu-gmac",
		.of_match_table	= luofu_gmac_of_match,
	},
};
module_platform_driver(luofu_gmac_driver);

MODULE_DESCRIPTION("Hi5671Y luofu GEMAC (phylink) driver frame");
MODULE_LICENSE("GPL v2");
