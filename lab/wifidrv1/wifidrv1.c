// SPDX-License-Identifier: GPL-2.0
/*
 * wifidrv1: wifidrv0 (cfg80211 wiphy + real netdev) PLUS the endpoint.
 *
 * wifidrv0 proved registration and lifecycle: wiphy "omo-drv0" registers, the
 * managed netdev "omowl0" opens/closes, nl80211 add/del works, rmmod cleans
 * up, vendor radios untouched.  Its stated limit was "no hardware, no data
 * path" - this module is the first step past that limit, built entirely from
 * what the earlier phases already proved on this device:
 *
 *   1. claim EP0 exactly as lab/eteprobe does (pci_enable_device,
 *      pci_request_mem_regions, the documented PCI_COMMAND=0x0007 write with
 *      read-back), and REFUSE if the vendor stack still owns the BARs
 *      (rc != 0) - coexistence, never replacement (docs/phase15/bringup.md:
 *      the vendor stack cannot be rmmod'd; the first unload step panics).
 *   2. map BAR0's register windows read-only, using the same constants the
 *      vendor's own driver uses (docs/phase4/mmio-map.md: the 0x40000000
 *      device-CA window is the EP0 register origin; the ETE/glue block the
 *      engine programs is BAR0+0x39000 / +0x3a000, docs/phase17/ete-engine.md
 *      A.4).  Everything here is READ-ONLY: no write to any BAR, no descriptor
 *      submitted, no doorbell rung, no firmware window touched.
 *   3. decode the register block into the driver's own state so the netdev has
 *      something real behind it, and expose what was measured where a data
 *      path will need it:
 *        - SR/DR program registers (base/depth/wptr) -> the ring the engine uses
 *        - the message registers (out[0]/out[1]/out[2]/out[5]) -> the mailbox
 *        - the channel-res register -> per-channel reset/ownership
 *      The values are logged and cached; ndo_start_xmit still drops packets,
 *      because writing the rings without the message service is what phase 22
 *      showed does not work (h2d-accept: the device-side accept gate holds).
 *
 * HARD RULES honoured: never write CA 0x400392f0 (out[5] is only ever READ
 * here, and only by an explicit opt-in param); never read the RC misc window
 * 0x10161000; never rmmod a vendor module; measurement only through the
 * endpoint's BAR0/BAR2.
 *
 * SAFETY: the whole hardware section is gated on param `hw` (default 0 = off),
 * so the default load behaves exactly like wifidrv0 (registration only, no PCI
 * access at all).  With hw=1 the claim is read-mostly and refuses on conflict.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <net/cfg80211.h>

#define OMO_WIPHY_NAME	"omo-drv1"
#define OMO_IFNAME	"omowl1"

/* ---- vendor struct offsets (measured, docs/phase14/wifidrv0.md) --------- */
#define VND_ND_OPS_OFF		0x128u
#define VND_ND_IEEE80211_OFF	0x1f0u
#define VND_ND_PRIV_OFF		0x540u
#define VND_ND_PRIV_SIZE	4096u

/* ---- PCI identity and BARs (docs/phase4/mmio-map.md, phase18) ----------- */
#define OMO_PCI_DEV	PCI_DEVFN(0, 0)
#define OMO_CFG_BAR0	0x10
#define OMO_BAR0_WIN	0x40000000UL	/* region-3 viewport base (EP0) */

/* the ETE window and the message/channel block live in one BAR0 mapping:
 * 0x39000..0x3afff, so omo_bar0 covers the ETE block and omo_remap is the
 * same mapping offset to 0x3a000. */
#define OMO_ETE_WIN	0x39000UL
#define OMO_ETE_LEN	0x2000UL
#define OMO_REMAP_OFF	0x1000UL	/* 0x3a000 - the channel/message block within the mapping */
#define OMO_MSG0		0x010
#define OMO_MSG1		0x014
#define OMO_MSG2		0x2d4
#define OMO_CHN_RES	0x2e8
#define OMO_MSG5	0x2f0

/* Offsets inside the ETE block (docs/phase17/ete-engine.md A.4). */
#define ETE_SR_CTRL	0x008
#define ETE_SR_BASE	0x010
#define ETE_SR_DEPTH	0x014
#define ETE_SR_WPTR	0x018
#define ETE_DR_BASE	0x030
#define ETE_DR_DEPTH	0x034
#define ETE_DR_WPTR	0x038
#define ETE_MSG0	0x010
#define ETE_MSG1	0x014
#define ETE_MSG2	0x2d4
#define ETE_CHN_RES	0x2e8
#define ETE_MSG5	0x2f0

/* ---- parameters --------------------------------------------------------- */
static unsigned int omo_hw;		/* 0 = registration only (safe default) */
module_param_named(hw, omo_hw, uint, 0444);
MODULE_PARM_DESC(hw, "1 = claim EP0 and read the register block (read-only); 0 = no hardware access");

static unsigned int omo_domain;
module_param_named(domain, omo_domain, uint, 0444);
MODULE_PARM_DESC(domain, "PCI domain of the 59e7:0005 endpoint (default 0)");

static unsigned int omo_read_msg5;
module_param_named(read_msg5, omo_read_msg5, uint, 0444);
MODULE_PARM_DESC(read_msg5, "1 = also READ out[5] CA 0x400392f0 (never written); default 0");

static unsigned int omo_verbose = 1;
module_param_named(verbose, omo_verbose, uint, 0444);
MODULE_PARM_DESC(verbose, "1 = log each decoded register");

/* ---- state ------------------------------------------------------------- */
static struct wiphy *omo_wiphy;
static struct net_device *omo_netdev;

static struct pci_dev *omo_pdev;
static void __iomem *omo_bar0;		/* ETE/glue window */
static void __iomem *omo_remap;		/* channel-res / message window */
static resource_size_t omo_bar0_base;

struct omo_ring {
	u32 base;
	u32 depth;
	u32 wptr;
	u32 ctrl;
};

static struct omo_ring omo_sr;
static struct omo_ring omo_dr;
static u32 omo_msg0, omo_msg1, omo_msg2, omo_msg5, omo_chnres;
static bool omo_regs_valid;

static const u8 omo_mac[ETH_ALEN] = { 0x02, 0x00, 0x6f, 0x6d, 0x6f, 0x31 };

/* ---- register access --------------------------------------------------- */
static u32 omo_rd(void __iomem *win, unsigned long off)
{
	return ioread32(win + off);
}

static void omo_log_reg(const char *tag, void __iomem *win, unsigned long off, u32 v)
{
	if (omo_verbose)
		pr_info("omo-drv1: %-16s BAR0+0x%05lx = 0x%08x\n", tag, off, v);
}

/*
 * Read the ETE/glue block and decode it into driver state.  Read-only, and
 * only called when hw=1 and the BARs were successfully claimed.
 */
static void omo_read_ring_block(void)
{
	u32 before, after;

	/* bracket the block with one message register so a concurrent device
	 * write is visible as a changed value (the read discipline phase 17
	 * used: read-only, bracketed, no writes of any kind). */
	before = omo_rd(omo_bar0, ETE_MSG1);

	omo_sr.ctrl  = omo_rd(omo_bar0, ETE_SR_CTRL);
	omo_sr.base  = omo_rd(omo_bar0, ETE_SR_BASE);
	omo_sr.depth = omo_rd(omo_bar0, ETE_SR_DEPTH);
	omo_sr.wptr  = omo_rd(omo_bar0, ETE_SR_WPTR);

	omo_dr.base  = omo_rd(omo_bar0, ETE_DR_BASE);
	omo_dr.depth = omo_rd(omo_bar0, ETE_DR_DEPTH);
	omo_dr.wptr  = omo_rd(omo_bar0, ETE_DR_WPTR);

	after = omo_rd(omo_bar0, ETE_MSG1);

	omo_log_reg("SR_CTRL", omo_bar0, ETE_SR_CTRL, omo_sr.ctrl);
	omo_log_reg("SR_BASE", omo_bar0, ETE_SR_BASE, omo_sr.base);
	omo_log_reg("SR_DEPTH", omo_bar0, ETE_SR_DEPTH, omo_sr.depth);
	omo_log_reg("SR_WPTR", omo_bar0, ETE_SR_WPTR, omo_sr.wptr);
	omo_log_reg("DR_BASE", omo_bar0, ETE_DR_BASE, omo_dr.base);
	omo_log_reg("DR_DEPTH", omo_bar0, ETE_DR_DEPTH, omo_dr.depth);
	omo_log_reg("DR_WPTR", omo_bar0, ETE_DR_WPTR, omo_dr.wptr);

	pr_info("omo-drv1: SR: base=0x%08x depth_field=0x%08x wptr=0x%08x ctrl=0x%08x (depth-1=%u)\n",
		omo_sr.base, omo_sr.depth, omo_sr.wptr, omo_sr.ctrl,
		omo_sr.depth & 0x3ff);
	pr_info("omo-drv1: DR: base=0x%08x depth_field=0x%08x wptr=0x%08x (depth-1=%u)\n",
		omo_dr.base, omo_dr.depth, omo_dr.wptr, omo_dr.depth & 0x3ff);
	pr_info("omo-drv1: msg1 bracket before=0x%08x after=0x%08x %s\n",
		before, after, before == after ? "(stable)" : "(DEVICE CHANGED IT)");

	pr_info("omo-drv1: NOTE read-only decode; no ring write, no descriptor, no doorbell\n");
}

static void omo_read_msg_block(void)
{
	omo_msg0 = omo_rd(omo_remap, ETE_MSG0);
	omo_msg1 = omo_rd(omo_remap, ETE_MSG1);
	omo_msg2 = omo_rd(omo_remap, ETE_MSG2);
	omo_chnres = omo_rd(omo_remap, ETE_CHN_RES);

	omo_log_reg("MSG0 out[0]", omo_remap, ETE_MSG0, omo_msg0);
	omo_log_reg("MSG1 out[1]", omo_remap, ETE_MSG1, omo_msg1);
	omo_log_reg("MSG2 doorbell", omo_remap, ETE_MSG2, omo_msg2);
	omo_log_reg("CHN_RES", omo_remap, ETE_CHN_RES, omo_chnres);

	if (omo_read_msg5) {
		omo_msg5 = omo_rd(omo_remap, ETE_MSG5);
		pr_info("omo-drv1: DEBUG msg5 out[5] READ-ONLY = 0x%08x (never written here)\n",
			omo_msg5);
	}
}

/* ---- PCI bring-up (mirrors lab/eteprobe, read-mostly) ------------------ */
static int omo_hw_attach(void)
{
	u32 lo = 0, hi = 0;
	u16 rb = 0;
	int rc;

	omo_pdev = pci_get_domain_bus_and_slot(omo_domain, 0, OMO_PCI_DEV);
	if (!omo_pdev) {
		pr_err("omo-drv1: no 59e7:0005 endpoint in domain %u\n", omo_domain);
		return -ENODEV;
	}

	rc = pci_enable_device(omo_pdev);
	if (rc) {
		pr_err("omo-drv1: pci_enable_device rc=%d\n", rc);
		return rc;
	}

	/* REFUSE rather than fight: if the vendor stack owns the BARs this
	 * fails, and that is the correct outcome (coexistence rule). */
	rc = pci_request_mem_regions(omo_pdev, "omo-drv1");
	if (rc) {
		pr_err("omo-drv1: pci_request_mem_regions rc=%d (vendor stack loaded?) - refusing\n",
		       rc);
		pci_disable_device(omo_pdev);
		omo_pdev = NULL;
		return rc;
	}

	pci_read_config_dword(omo_pdev, PCI_BASE_ADDRESS_0, &lo);
	pci_read_config_dword(omo_pdev, PCI_BASE_ADDRESS_1, &hi);
	omo_bar0_base = (resource_size_t)(lo & PCI_BASE_ADDRESS_MEM_MASK);

	/* the documented viewport-enable write: MEM|MASTER */
	pci_write_config_word(omo_pdev, PCI_COMMAND, 0x0007);
	pci_read_config_word(omo_pdev, PCI_COMMAND, &rb);
	pr_info("omo-drv1: BAR0 base=0x%llx (config) BAR0_hi=0x%x cfg[0x004]=0x%04x MEM|MASTER=%s\n",
		(unsigned long long)omo_bar0_base, hi, rb,
		(rb & (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) ==
		(PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER) ? "set" : "MISSING");

	omo_bar0 = ioremap(omo_bar0_base + OMO_ETE_WIN, OMO_ETE_LEN);
	if (!omo_bar0) {
		pr_err("omo-drv1: ioremap ETE window FAILED\n");
		rc = -ENOMEM;
		goto err_regions;
	}
	omo_remap = omo_bar0 + OMO_REMAP_OFF;

	/* decode the blocks - reads only */
	omo_read_ring_block();
	omo_read_msg_block();
	omo_regs_valid = true;

	return 0;

err_bar0:
	iounmap(omo_bar0);
	omo_bar0 = NULL;
err_regions:
	pci_release_mem_regions(omo_pdev);
	pci_disable_device(omo_pdev);
	omo_pdev = NULL;
	return rc;
}

static void omo_hw_detach(void)
{
	omo_remap = NULL;	/* inside the omo_bar0 mapping */
	if (omo_bar0) {
		iounmap(omo_bar0);
		omo_bar0 = NULL;
	}
	if (omo_pdev) {
		pci_release_mem_regions(omo_pdev);
		pci_disable_device(omo_pdev);
		omo_pdev = NULL;
	}
	omo_regs_valid = false;
}

/* ---- netdev ------------------------------------------------------------ */
static struct wireless_dev *omo_add_virtual_intf(struct wiphy *wiphy,
						 const char *name,
						 unsigned char name_assign_type,
						 enum nl80211_iftype type,
						 struct vif_params *params);
static int omo_del_virtual_intf(struct wiphy *wiphy, struct wireless_dev *wdev);

static int omo_ndo_open(struct net_device *dev)
{
	pr_info("omo-drv1: ndo_open %s hw=%u regs=%s\n", dev->name, omo_hw,
		omo_regs_valid ? "decoded" : "absent");
	return 0;
}

static int omo_ndo_stop(struct net_device *dev)
{
	pr_info("omo-drv1: ndo_stop %s\n", dev->name);
	return 0;
}

/*
 * TX is still a drop: phase 22 measured that submitting descriptors without
 * the device-side accept gate produces nothing (docs/phase22/h2d-accept.md),
 * so the module does not pretend to transmit.  When a data path lands, this
 * is the hook that will use omo_sr/omo_dr.
 */
static netdev_tx_t omo_ndo_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	if (omo_regs_valid && omo_verbose > 1)
		pr_info("omo-drv1: xmit %u bytes dropped (no data path yet; SR base=0x%08x)\n",
			skb->len, omo_sr.base);
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

static const struct net_device_ops omo_netdev_ops = {
	.ndo_open	= omo_ndo_open,
	.ndo_stop	= omo_ndo_stop,
	.ndo_start_xmit	= omo_ndo_start_xmit,
};

#define OMO_CHAN(_ch) {				\
	.center_freq = 2407 + (_ch) * 5,	\
	.hw_value = (_ch),			\
	.max_power = 20,			\
}

static struct ieee80211_channel omo_2ghz_channels[] = {
	OMO_CHAN(1),  OMO_CHAN(2),  OMO_CHAN(3),  OMO_CHAN(4),
	OMO_CHAN(5),  OMO_CHAN(6),  OMO_CHAN(7),  OMO_CHAN(8),
	OMO_CHAN(9),  OMO_CHAN(10), OMO_CHAN(11), OMO_CHAN(12),
	OMO_CHAN(13),
};

static struct ieee80211_rate omo_2ghz_rates[] = {
	{ .bitrate = 10 },
	{ .bitrate = 20,  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 55,  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 110, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 60 },
	{ .bitrate = 90 },
	{ .bitrate = 120 },
	{ .bitrate = 180 },
	{ .bitrate = 240 },
	{ .bitrate = 360 },
	{ .bitrate = 480 },
	{ .bitrate = 540 },
};

static struct ieee80211_supported_band omo_2ghz_band = {
	.channels	= omo_2ghz_channels,
	.n_channels	= ARRAY_SIZE(omo_2ghz_channels),
	.bitrates	= omo_2ghz_rates,
	.n_bitrates	= ARRAY_SIZE(omo_2ghz_rates),
	.ht_cap		= {
		.ht_supported	= true,
		.cap		= IEEE80211_HT_CAP_SGI_20 |
				  IEEE80211_HT_CAP_MAX_AMSDU,
		.ampdu_factor	= IEEE80211_HT_MAX_AMPDU_64K,
		.ampdu_density	= IEEE80211_HT_MPDU_DENSITY_NONE,
		.mcs		= {
			.rx_mask	= { 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
			.tx_params	= IEEE80211_HT_MCS_TX_DEFINED,
		},
	},
};

static void omo_netdev_setup(struct net_device *dev)
{
	ether_setup(dev);
	*(const struct net_device_ops **)((char *)dev + VND_ND_OPS_OFF) =
		&omo_netdev_ops;
}

static struct wireless_dev *omo_add_virtual_intf(struct wiphy *wiphy,
						 const char *name,
						 unsigned char name_assign_type,
						 enum nl80211_iftype type,
						 struct vif_params *params)
{
	struct net_device *dev;
	struct wireless_dev *wdev;
	struct sockaddr sa = { .sa_family = ARPHRD_ETHER };
	int rc;

	if (type != NL80211_IFTYPE_STATION)
		return ERR_PTR(-EOPNOTSUPP);
	if (omo_netdev)
		return ERR_PTR(-EBUSY);

	dev = alloc_netdev(VND_ND_PRIV_SIZE, name, name_assign_type,
			   omo_netdev_setup);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	wdev = (struct wireless_dev *)((char *)dev + VND_ND_PRIV_OFF);
	wdev->wiphy  = wiphy;
	wdev->iftype = type;
	*(struct wireless_dev **)((char *)dev + VND_ND_IEEE80211_OFF) = wdev;

	memcpy(sa.sa_data, omo_mac, ETH_ALEN);
	rc = eth_mac_addr(dev, &sa);
	if (rc) {
		free_netdev(dev);
		return ERR_PTR(rc);
	}

	rc = register_netdevice(dev);
	if (rc) {
		free_netdev(dev);
		return ERR_PTR(rc);
	}

	omo_netdev = dev;
	pr_info("omo-drv1: add_virtual_intf name=%s ifindex=%d rc=0\n",
		dev->name, dev->ifindex);
	return wdev;
}

static int omo_del_virtual_intf(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	struct net_device *dev = omo_netdev;

	if (!dev)
		return -ENODEV;

	omo_netdev = NULL;
	unregister_netdevice(dev);
	free_netdev(dev);
	return 0;
}

static const struct cfg80211_ops omo_ops = {
	.add_virtual_intf	= omo_add_virtual_intf,
	.del_virtual_intf	= omo_del_virtual_intf,
};

static int __init omo_wifidrv1_init(void)
{
	struct wiphy *wiphy;
	int rc;

	pr_info("omo-drv1: init hw=%u read_msg5=%u (offsets: ops=%u ieee=%u priv=%u)\n",
		omo_hw, omo_read_msg5,
		VND_ND_OPS_OFF, VND_ND_IEEE80211_OFF, VND_ND_PRIV_OFF);

	if (omo_hw) {
		rc = omo_hw_attach();
		if (rc) {
			pr_err("omo-drv1: hardware attach failed rc=%d - continuing without it\n",
			       rc);
		}
	} else {
		pr_info("omo-drv1: hw=0 - registration-only load (no PCI access at all)\n");
	}

	wiphy = wiphy_new_nm(&omo_ops, 0, OMO_WIPHY_NAME);
	if (!wiphy) {
		omo_hw_detach();
		return -ENOMEM;
	}
	omo_wiphy = wiphy;

	wiphy->bands[NL80211_BAND_2GHZ] = &omo_2ghz_band;
	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);
	memcpy(wiphy->perm_addr, omo_mac, ETH_ALEN);

	rc = wiphy_register(wiphy);
	if (rc) {
		wiphy_free(wiphy);
		omo_wiphy = NULL;
		omo_hw_detach();
		return rc;
	}

	/* create the interface through cfg80211's own path so the notifier
	 * runs exactly as it does for a driver-created interface. */
	{
		struct wireless_dev *wdev;

		wdev = omo_add_virtual_intf(wiphy, OMO_IFNAME, NET_NAME_UNKNOWN,
					    NL80211_IFTYPE_STATION, NULL);
		if (IS_ERR(wdev))
			pr_err("omo-drv1: initial interface rc=%ld\n",
			       PTR_ERR(wdev));
	}

	pr_info("omo-drv1: init done wiphy=%s ifname=%s hw=%u regs=%s\n",
		OMO_WIPHY_NAME, OMO_IFNAME, omo_hw,
		omo_regs_valid ? "decoded" : "absent");
	return 0;
}

static void __exit omo_wifidrv1_exit(void)
{
	if (omo_netdev) {
		struct net_device *dev = omo_netdev;

		omo_netdev = NULL;
		unregister_netdevice(dev);
		free_netdev(dev);
	}
	if (omo_wiphy) {
		wiphy_unregister(omo_wiphy);
		wiphy_free(omo_wiphy);
		omo_wiphy = NULL;
	}
	omo_hw_detach();
	pr_info("omo-drv1: exit done\n");
}

module_init(omo_wifidrv1_init);
module_exit(omo_wifidrv1_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("WR3000 v2.0: cfg80211 wiphy + netdev + read-only endpoint decode (port step 2)");
