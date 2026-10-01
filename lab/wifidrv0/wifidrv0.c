// SPDX-License-Identifier: GPL-2.0
/*
 * wifidrv0: register a cfg80211 wiphy *and* create a real wireless netdev on
 * the vendor 5.10.201 kernel, with no hardware behind it.
 *
 * The wiphy half is the phase-14 wifiskel path: build against the prepared
 * vanilla tree after lab/abi-pm-off.sh has forced CONFIG_PM/CONFIG_PM_SLEEP
 * off, so struct wiphy's bands[] offset matches the vendor's and our 2.4 GHz
 * band registers as "Band 1".
 *
 * The netdev half cannot use our compiled struct net_device offsets.  The
 * running device (WR3000 V2.0, fw 2.5.24) has a *backported* cfg80211
 * (modinfo: "backported from Linux v5.15.92") and its net_device carries the
 * CONFIG_WIRELESS_EXT pointer pair, so the three fields we own do not sit
 * where our multi_v7_defconfig (PM-off) headers put them:
 *
 *   field                              our PM-off    vendor (fw 2.5.24)
 *   offsetof(net_device, netdev_ops)      0x120           0x128
 *   offsetof(net_device, ieee80211_ptr)   0x1e8           0x1f0
 *   ALIGN(sizeof(net_device), 32)         0x4c0           0x540  (netdev_priv)
 *
 * The vendor values were measured from the vendor's own modules (see
 * docs/phase14/wifidrv0.md), not guessed:
 *   - mac80211.ko  ieee80211_if_setup : `str r1,[r4,#0x128]` (nd->netdev_ops)
 *   - mac80211.ko  ieee80211_if_add   : `add r4,r5,#0x540` then
 *                                       `str r3,[r5,#0x1f0]` (ieee80211_ptr)
 *   - cfg80211.ko  cfg80211_netdev_notifier_call : `ldr r4,[r8,#0x1f0]`
 *
 * If we wrote netdev_ops through the compiled offset, the vendor's
 * netdev_ops would stay NULL and register_netdevice() would dereference it
 * (net/core/dev.c: `if (dev->netdev_ops->ndo_init)`) -> oops.  So we address
 * exactly the three fields we own through the vendor's measured offsets,
 * keep the wireless_dev inside the netdev private area (at the vendor's
 * netdev_priv base), and leave every other net_device field to the vendor's
 * own code (ether_setup/eth_mac_addr/register_netdevice/...).
 *
 * There is deliberately NO hardware here: no PCI/MMIO/DMA/firmware, no data
 * path, no scan.  Everything logs its return code.
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
#include <net/cfg80211.h>

#define OMO_WIPHY_NAME	"omo-drv0"
#define OMO_IFNAME	"omowl0"

/*
 * Vendor struct net_device offsets for the running device (fw 2.5.24),
 * measured from the vendor's own mac80211.ko / cfg80211.ko.  The private
 * area is sized generously so the vendor's (larger, v5.15.92) wireless_dev
 * fits; alloc_netdev zeroes it.
 */
#define VND_ND_OPS_OFF		0x128u	/* offsetof(nd, netdev_ops) */
#define VND_ND_IEEE80211_OFF	0x1f0u	/* offsetof(nd, ieee80211_ptr) */
#define VND_ND_PRIV_OFF		0x540u	/* ALIGN(sizeof(nd), 32) */
#define VND_ND_PRIV_SIZE	4096u

static struct wiphy *omo_wiphy;
static struct net_device *omo_netdev;

/* valid locally-administered address (02:..), "omo0" */
static const u8 omo_mac[ETH_ALEN] = { 0x02, 0x00, 0x6f, 0x6d, 0x6f, 0x30 };

/*
 * The ops table only has to satisfy wiphy_new_nm()'s consistency WARN_ONs;
 * the callbacks below are the ones cfg80211 can actually reach.
 */
static struct wireless_dev *omo_add_virtual_intf(struct wiphy *wiphy,
						 const char *name,
						 unsigned char name_assign_type,
						 enum nl80211_iftype type,
						 struct vif_params *params);
static int omo_del_virtual_intf(struct wiphy *wiphy, struct wireless_dev *wdev);
static int omo_change_virtual_intf(struct wiphy *wiphy, struct net_device *dev,
				   enum nl80211_iftype type,
				   struct vif_params *params);

static const struct cfg80211_ops omo_ops = {
	.add_virtual_intf	= omo_add_virtual_intf,
	.del_virtual_intf	= omo_del_virtual_intf,
	.change_virtual_intf	= omo_change_virtual_intf,
};

/* net_device_ops is laid out unconditionally at the head of the struct, so
 * our ndo_open/ndo_stop slots line up with the vendor's. */
static int omo_ndo_open(struct net_device *dev)
{
	pr_info("omo-drv0: ndo_open %s rc=0 (no data path)\n", dev->name);
	return 0;
}

static int omo_ndo_stop(struct net_device *dev)
{
	pr_info("omo-drv0: ndo_stop %s rc=0 (no data path)\n", dev->name);
	return 0;
}

/*
 * A netdev that can be brought up must accept transmits: `ip link set up`
 * makes the stack add an IPv6 link-local address and the MLD timer then
 * queues a report.  Without ndo_start_xmit the core calls through a NULL
 * pointer in dev_hard_start_xmit() and oopses (seen on the first try, see
 * the report).  There is no hardware, so every packet is simply dropped;
 * this is not logged per-packet to avoid a dmesg flood.
 */
static netdev_tx_t omo_ndo_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

static const struct net_device_ops omo_netdev_ops = {
	.ndo_open	= omo_ndo_open,
	.ndo_stop	= omo_ndo_stop,
	.ndo_start_xmit	= omo_ndo_start_xmit,
};

/* channels 1..13, 20 MHz, nominal 20 dBm */
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

/* minimal, sane CCK + OFDM set (bitrate in 100 kbps), as wifiskel uses */
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

/* alloc_netdev setup callback: the vendor calls this on its own struct
 * net_device, so ether_setup() writes the right fields; we only add our
 * netdev_ops through the vendor's measured offset. */
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

	if (type != NL80211_IFTYPE_STATION) {
		pr_info("omo-drv0: add_virtual_intf type=%d refused (managed only)\n",
			type);
		return ERR_PTR(-EOPNOTSUPP);
	}
	if (omo_netdev) {
		pr_info("omo-drv0: add_virtual_intf refused: interface already exists\n");
		return ERR_PTR(-EBUSY);
	}

	dev = alloc_netdev(VND_ND_PRIV_SIZE, name, name_assign_type,
			   omo_netdev_setup);
	if (!dev) {
		pr_err("omo-drv0: alloc_netdev(%s) failed\n", name);
		return ERR_PTR(-ENOMEM);
	}

	/* wireless_dev lives in the netdev private area at the vendor's
	 * netdev_priv offset; the private area was zeroed by alloc_netdev. */
	wdev = (struct wireless_dev *)((char *)dev + VND_ND_PRIV_OFF);
	wdev->wiphy  = wiphy;
	wdev->iftype = type;
	*(struct wireless_dev **)((char *)dev + VND_ND_IEEE80211_OFF) = wdev;

	/* give the interface a valid MAC using the vendor's own helper (it
	 * writes dev->dev_addr through the vendor's layout). */
	memcpy(sa.sa_data, omo_mac, ETH_ALEN);
	rc = eth_mac_addr(dev, &sa);
	pr_info("omo-drv0: eth_mac_addr(rc=%d)\n", rc);

	rc = register_netdevice(dev);
	if (rc) {
		pr_err("omo-drv0: register_netdevice(%s) rc=%d\n", dev->name, rc);
		free_netdev(dev);
		return ERR_PTR(rc);
	}

	omo_netdev = dev;
	pr_info("omo-drv0: add_virtual_intf name=%s type=%d ifindex=%d rc=0\n",
		dev->name, type, dev->ifindex);
	return wdev;
}

static int omo_del_virtual_intf(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	struct net_device *dev = omo_netdev;

	if (!dev) {
		pr_info("omo-drv0: del_virtual_intf: no interface registered\n");
		return -ENODEV;
	}

	omo_netdev = NULL;
	pr_info("omo-drv0: del_virtual_intf name=%s\n", dev->name);
	unregister_netdevice(dev);
	free_netdev(dev);
	pr_info("omo-drv0: del_virtual_intf rc=0 (netdev removed and freed)\n");
	return 0;
}

static int omo_change_virtual_intf(struct wiphy *wiphy, struct net_device *dev,
				   enum nl80211_iftype type,
				   struct vif_params *params)
{
	struct wireless_dev *wdev =
		*(struct wireless_dev **)((char *)dev + VND_ND_IEEE80211_OFF);

	if (!wdev) {
		pr_err("omo-drv0: change_virtual_intf: no wdev\n");
		return -EINVAL;
	}
	if (type != wdev->iftype) {
		pr_info("omo-drv0: change_virtual_intf %s: %d -> %d refused (no-op only)\n",
			dev->name, wdev->iftype, type);
		return -EOPNOTSUPP;
	}
	pr_info("omo-drv0: change_virtual_intf %s: type %d unchanged, rc=0\n",
		dev->name, type);
	return 0;
}

static int __init omo_wifidrv0_init(void)
{
	struct wiphy *wiphy;
	struct wireless_dev *wdev;
	int rc;

	pr_info("omo-drv0: init: compiled offsets: netdev_ops=%zu ieee80211_ptr=%zu "
		"ALIGN(sizeof(net_device),32)=%zu; vendor offsets: %u/%u/%u\n",
		offsetof(struct net_device, netdev_ops),
		offsetof(struct net_device, ieee80211_ptr),
		ALIGN(sizeof(struct net_device), NETDEV_ALIGN),
		VND_ND_OPS_OFF, VND_ND_IEEE80211_OFF, VND_ND_PRIV_OFF);
	pr_info("omo-drv0: init: offsetof(wiphy.interface_modes)=%zu "
		"offsetof(wiphy.bands)=%zu\n",
		offsetof(struct wiphy, interface_modes),
		offsetof(struct wiphy, bands));

	wiphy = wiphy_new_nm(&omo_ops, 0, OMO_WIPHY_NAME);
	pr_info("omo-drv0: wiphy_new_nm(ops, sizeof_priv=0, name=\"%s\") rc=%d ptr=%px\n",
		OMO_WIPHY_NAME, wiphy ? 0 : -ENOMEM, wiphy);
	if (!wiphy)
		return -ENOMEM;
	omo_wiphy = wiphy;

	wiphy->bands[NL80211_BAND_2GHZ] = &omo_2ghz_band;
	pr_info("omo-drv0: set bands[2GHZ] rc=0 (n_channels=%d n_bitrates=%d "
		"ht_supported=%d first_ch=%u max_power=%d)\n",
		omo_2ghz_band.n_channels, omo_2ghz_band.n_bitrates,
		omo_2ghz_band.ht_cap.ht_supported,
		omo_2ghz_channels[0].center_freq,
		omo_2ghz_channels[0].max_power);

	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);
	memcpy(wiphy->perm_addr, omo_mac, ETH_ALEN);
	pr_info("omo-drv0: set interface_modes=0x%x perm_addr=%pM rc=0\n",
		wiphy->interface_modes, wiphy->perm_addr);

	rc = wiphy_register(wiphy);
	pr_info("omo-drv0: wiphy_register() rc=%d\n", rc);
	if (rc) {
		wiphy_free(wiphy);
		omo_wiphy = NULL;
		pr_err("omo-drv0: registration rejected (rc=%d), wiphy freed\n", rc);
		return rc;
	}

	/* nl80211 calls add_virtual_intf with RTNL held; do the same here so
	 * the interface also exists automatically at load. */
	rtnl_lock();
	wdev = omo_add_virtual_intf(wiphy, OMO_IFNAME, NET_NAME_UNKNOWN,
				    NL80211_IFTYPE_STATION, NULL);
	rtnl_unlock();
	if (IS_ERR(wdev)) {
		rc = PTR_ERR(wdev);
		pr_err("omo-drv0: interface %s create failed rc=%d\n",
		       OMO_IFNAME, rc);
		wiphy_unregister(wiphy);
		wiphy_free(wiphy);
		omo_wiphy = NULL;
		return rc;
	}

	pr_info("omo-drv0: ready: phy \"%s\", interface \"%s\" (no hardware, no data path)\n",
		OMO_WIPHY_NAME, OMO_IFNAME);
	return 0;
}

static void __exit omo_wifidrv0_exit(void)
{
	if (!omo_wiphy) {
		pr_info("omo-drv0: unload: nothing was registered\n");
		return;
	}

	/* wiphy_unregister() WARNs if wdev_list is non-empty, so remove the
	 * interface first. */
	rtnl_lock();
	if (omo_netdev)
		omo_del_virtual_intf(omo_wiphy, NULL);
	rtnl_unlock();

	wiphy_unregister(omo_wiphy);
	pr_info("omo-drv0: wiphy_unregister() rc=0 (void)\n");
	wiphy_free(omo_wiphy);
	pr_info("omo-drv0: wiphy_free() rc=0 (void)\n");

	omo_wiphy = NULL;
	pr_info("omo-drv0: unloaded, phy and interface removed\n");
}

module_init(omo_wifidrv0_init);
module_exit(omo_wifidrv0_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("WR3000 V2.0 cfg80211 wiphy + netdev interface (no hardware)");
