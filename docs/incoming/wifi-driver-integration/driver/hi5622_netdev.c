// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_netdev.c - netdev registration skeleton + wiphy stub of the
 * clean-room Hi5622V100 host driver.
 *
 * ORIGINAL text written by the wifi-driver-integration lane (task-64).
 *
 * Scope note (deliberate): the project kernel config for this 6.18 luofu tree
 * has CONFIG_CFG80211=n (checked in the built tree's .config), so a real
 * wiphy cannot be registered with this build at all.  The wiphy is therefore
 * a documented stub; when cfg80211 is enabled the vendor map is
 * wifi-driver-re 2.2 (39/112 callbacks, cfg80211_ops at .data+0xeb0,
 * sizeof_priv 4, wiphy_new_nm + wiphy_apply_custom_regulatory), which is the
 * follow-up task, not this skeleton.
 *
 * The netdev is a real net_device so the interface lifecycle is exercised:
 * open/stop gate on the driver state, xmit feeds the hdpp TX entry point and
 * the MAC address is a locally-administered placeholder (the real source is
 * the hmac/bdinfo MAC path [U]; no per-unit value may ever be committed).
 */
#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <linux/string.h>

#include "hi5622_drv.h"

struct hi5622_netdev_priv {
	struct hi5622_chip *chip;
};

static int hi5622_netdev_open(struct net_device *dev)
{
	struct hi5622_netdev_priv *priv = netdev_priv(dev);
	struct hi5622_chip *chip = priv->chip;

	if (!(chip->state & HI5622_DRV_STATE_RUNNING)) {
		dev_info(chip->dev,
			 "netdev: %s open deferred - chip not RUNNING yet\n",
			 dev->name);
		return -ENETDOWN;
	}
	netif_carrier_on(dev);
	netif_start_queue(dev);
	return 0;
}

static int hi5622_netdev_stop(struct net_device *dev)
{
	netif_stop_queue(dev);
	netif_carrier_off(dev);
	return 0;
}

static netdev_tx_t hi5622_netdev_xmit(struct sk_buff *skb,
				      struct net_device *dev)
{
	struct hi5622_netdev_priv *priv = netdev_priv(dev);
	struct hi5622_chip *chip = priv->chip;

	if (!(chip->state & HI5622_DRV_STATE_RUNNING)) {
		chip->tx_dropped++;
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}
	/* The hdpp layer consumes the skb in every path (frees it). */
	hi5622_hdpp_tx_frame(chip, skb);
	return NETDEV_TX_OK;
}

static const struct net_device_ops hi5622_netdev_ops = {
	.ndo_open		= hi5622_netdev_open,
	.ndo_stop		= hi5622_netdev_stop,
	.ndo_start_xmit		= hi5622_netdev_xmit,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

int hi5622_netdev_register(struct hi5622_chip *chip)
{
	struct net_device *dev;
	struct hi5622_netdev_priv *priv;
	int ret;

	if (chip->netdev)
		return -EBUSY;

	dev = alloc_netdev(sizeof(*priv), "hiwlan%d", NET_NAME_ENUM,
			   ether_setup);
	if (!dev)
		return -ENOMEM;

	priv = netdev_priv(dev);
	priv->chip = chip;
	dev->netdev_ops = &hi5622_netdev_ops;
	SET_NETDEV_DEV(dev, chip->dev);
	/* [U] real MAC comes from the hmac/bdinfo provisioning path; this is a
	 * locally-administered placeholder so no per-unit value is ever used. */
	eth_hw_addr_random(dev);
	netif_carrier_off(dev);

	ret = register_netdev(dev);
	if (ret) {
		free_netdev(dev);
		return ret;
	}
	chip->netdev = dev;
	dev_info(chip->dev, "netdev: %s registered (MAC %pM, skeleton)\n",
		 dev->name, dev->dev_addr);
	return 0;
}

void hi5622_netdev_unregister(struct hi5622_chip *chip)
{
	struct net_device *dev = chip->netdev;

	if (!dev)
		return;
	chip->netdev = NULL;
	rtnl_lock();
	unregister_netdevice(dev);
	rtnl_unlock();
	free_netdev(dev);
	dev_info(chip->dev, "netdev: unregistered\n");
}

/* Reflect the chip state into the netdev carrier/queue (state notifier). */
void hi5622_netdev_sync_state(struct hi5622_chip *chip)
{
	struct net_device *dev = chip->netdev;

	if (!dev)
		return;
	rtnl_lock();
	if (chip->state & HI5622_DRV_STATE_RUNNING) {
		netif_carrier_on(dev);
		if (netif_running(dev))
			netif_start_queue(dev);
	} else {
		netif_carrier_off(dev);
		netif_stop_queue(dev);
	}
	rtnl_unlock();
}

/* Wiphy stub: CONFIG_CFG80211 is not set in the project 6.18 tree, so this is
 * an explicitly non-functional placeholder that records the planned surface. */
int hi5622_wiphy_stub_init(struct hi5622_chip *chip)
{
	struct hi5622_hdpp *hdpp = &chip->hdpp;

	chip->wiphy_stub.present = false;
	chip->wiphy_stub.bands = HI5622_HDPP_MAX_BAND;
	/* 39 populated cfg80211_ops callbacks in the vendor full-MAC map
	 * (wifi-driver-re 2.2 [V]); recorded for the follow-up task. */
	chip->wiphy_stub.ops_planned = 39;

	dev_info(chip->dev,
		 "wiphy: stub only (CONFIG_CFG80211=n on the 6.18 tree; %u bands, 39 cfg80211 ops planned for the follow-up)\n",
		 chip->wiphy_stub.bands);
	(void)hdpp;
	return 0;
}
