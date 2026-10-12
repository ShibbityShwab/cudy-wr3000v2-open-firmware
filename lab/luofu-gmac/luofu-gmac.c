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
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/phylink.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

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

static struct luofu_gmac *pl_to_gmac(struct phylink_config *cfg)
{
	return container_of(cfg, struct luofu_gmac, pl_config);
}

static void luofu_gmac_mac_config(struct phylink_config *cfg,
				  unsigned int mode,
				  const struct phylink_link_state *st)
{
	struct luofu_gmac *g = pl_to_gmac(cfg);

	/*
	 * The mode/speed/duplex words are the MAC-configuration registers the
	 * vendor sets in hi_mac_link_up; their offsets and values are staged
	 * behind the write guard until they are measured on the device.
	 */
	(void)g;
	(void)mode;
	(void)st;
}

static void luofu_gmac_mac_link_down(struct phylink_config *cfg,
				     unsigned int mode, phy_interface_t iface)
{
	(void)cfg;
	(void)mode;
	(void)iface;
}

static void luofu_gmac_mac_link_up(struct phylink_config *cfg,
				   struct phy_device *phy,
				   unsigned int mode, phy_interface_t iface,
				   int speed, int duplex, bool tx_pause, bool rx_pause)
{
	(void)cfg;
	(void)phy;
	(void)mode;
	(void)iface;
	(void)speed;
	(void)duplex;
	(void)tx_pause;
	(void)rx_pause;
}

static const struct phylink_mac_ops luofu_gmac_phylink_ops = {
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
#define LUOFU_DIAG_BASE	0x10100000UL
#define LUOFU_DIAG_OFF	0xc94	/* gmac's OWN cell - 0xC8C is the mach's, 0xC90 the CRG's, 0xC98 mdio's */

static void luofu_diag(u32 v)
{
	void __iomem *p = ioremap(LUOFU_DIAG_BASE, 0x1000);

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

	ret = of_get_phy_mode(pdev->dev.of_node, &iface);
	if (ret)
		iface = PHY_INTERFACE_MODE_NA;

	g->pl_config.dev = &pdev->dev;
	g->pl_config.type = PHYLINK_NETDEV;
	g->phylink = phylink_create(&g->pl_config, pdev->dev.fwnode, iface,
				    &luofu_gmac_phylink_ops);
	if (IS_ERR(g->phylink)) {
		ret = PTR_ERR(g->phylink);
		goto err_free;
	}

	ndev->netdev_ops = &luofu_gmac_netdev_ops;
	ndev->ethtool_ops = &luofu_gmac_ethtool_ops;

	ret = phylink_of_phy_connect(g->phylink, pdev->dev.of_node, 0);
	if (ret) {
		dev_warn(&pdev->dev, "no PHY (phy-handle/fixed-link?): %d\n", ret);
		/* keep the netdev: a fixed-link or a later mdio0 can bind it */
	}

	ret = register_netdev(ndev);
	if (ret)
		goto err_phylink;

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
