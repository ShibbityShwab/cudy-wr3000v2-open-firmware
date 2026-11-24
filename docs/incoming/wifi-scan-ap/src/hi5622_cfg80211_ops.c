// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_cfg80211_ops.c - struct cfg80211_ops and netdev ndos for the
 * Hi5622V100 clean-room full-MAC driver, M0-M2 (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (wifi-cfg80211 lane, shared task task-104,
 * 2026-11-24).  No vendor source/object/firmware/calibration material is
 * reproduced; see ../REPORT.md for the op<->HCC mapping table and evidence.
 *
 * Implemented here (M0-M1):
 *   add_virtual_intf / del_virtual_intf / change_virtual_intf.
 *     The netdev VAP is real and visible to iw dev / netifd; the firmware
 *     user/vap H2D messages (g0 id7/id8 add, g0 id9 delete) are deliberately
 *     NOT sent yet - their builders are task-103's deliverable.  The model
 *     records each VAP as "hcc add pending" so the status file tells the
 *     operator exactly which messages are missing.
 * M2/M3 wiring added by the wifi-scan-ap lane (task-113):
 *   scan / abort_scan -> hi5622_scan.c (provider-built g0 id0xC request +
 *   D2H g1 15/16/17 / g2 id2 decode; the provider is absent by default so
 *   the op still returns -EOPNOTSUPP and never fakes a scan);
 *   start_ap / stop_ap -> WAL group-2 design + stub (same provider rule);
 *   add/del_virtual_intf now also attempt the firmware g0 id7/id8/id9
 *   messages through the provider; a missing builder leaves the netdev model
 *   intact and records the pending message.
 * Still clean -EOPNOTSUPP stubs (M2):
 *   get_station / dump_station - station-event D2H frames are [U].
 * Deferred (not registered => cfg80211 returns -EOPNOTSUPP by itself):
 *   every M3+ op; the full 39-op vendor set with milestones and HCC messages
 *   is the portable map in hi5622_cfg80211_logic.c (host-tested).
 */
#include <linux/device.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/ieee80211.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/string.h>
#include <linux/types.h>
#include <net/cfg80211.h>

#include "hi5622_cfg80211.h"
#include "hi5622_scan.h"

/* ---------------------------------------------------------------------
 * nl80211 iftype -> portable model iftype
 * -------------------------------------------------------------------*/
static u16 hi5622_cfg11_model_iftype(enum nl80211_iftype type)
{
	switch (type) {
	case NL80211_IFTYPE_STATION:	return HI5622_IFTYPE_STATION;
	case NL80211_IFTYPE_AP:		return HI5622_IFTYPE_AP;
	case NL80211_IFTYPE_MONITOR:	return HI5622_IFTYPE_MONITOR;
	default:			return 0;
	}
}

/* ---------------------------------------------------------------------
 * netdev plumbing (M0 VAP model; no data path yet - M4)
 * -------------------------------------------------------------------*/
static int hi5622_vap_open(struct net_device *dev)
{
	(void)dev;
	return 0;
}

static int hi5622_vap_stop(struct net_device *dev)
{
	(void)dev;
	return 0;
}

/* Drop-stub TX: the hdpp data path is M4.  Without ndo_start_xmit an
 * interface bring-up would oops in the IPv6 stack (phase14 wifidrv0 lesson). */
static netdev_tx_t hi5622_vap_xmit(struct sk_buff *skb, struct net_device *dev)
{
	(void)dev;
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static const struct net_device_ops hi5622_vap_netdev_ops = {
	.ndo_open		= hi5622_vap_open,
	.ndo_stop		= hi5622_vap_stop,
	.ndo_start_xmit		= hi5622_vap_xmit,
};

static void hi5622_vap_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->netdev_ops = &hi5622_vap_netdev_ops;
	dev->needs_free_netdev = true;
	dev->tx_queue_len = 0;
}

/* ---------------------------------------------------------------------
 * cfg80211 ops - implemented (M0-M1)
 * -------------------------------------------------------------------*/
static struct wireless_dev *
hi5622_cfg11_add_virtual_intf(struct wiphy *wiphy, const char *name,
			      unsigned char name_assign_type,
			      enum nl80211_iftype type,
			      struct vif_params *params)
{
	struct hi5622_cfg80211 *c = wiphy_priv(wiphy);
	struct net_device *dev;
	struct hi5622_vap *vap;
	struct wireless_dev *wdev;
	u8 addr[ETH_ALEN];
	u16 slot = 0xffff, iftype;
	int err, hcc_rc;

	(void)params;
	iftype = hi5622_cfg11_model_iftype(type);
	if (!iftype)
		return ERR_PTR(-EOPNOTSUPP);

	mutex_lock(&c->lock);
	if (!c->model.wiphy_registered) {
		mutex_unlock(&c->lock);
		return ERR_PTR(-ENODEV);
	}
	err = hi5622_cfg_vap_alloc(&c->model, name, iftype, &slot);
	mutex_unlock(&c->lock);
	if (err)
		return ERR_PTR(err);

	dev = alloc_netdev(sizeof(*vap), name, name_assign_type,
			   hi5622_vap_setup);
	if (!dev) {
		err = -ENOMEM;
		goto drop_model;
	}
	vap = netdev_priv(dev);
	wdev = &vap->wdev;

	/* Derive the VAP address from wiphy->perm_addr (documentation prefix),
	 * keeping the locally-administered unicast bits and a unique last byte.
	 * Never a per-unit MAC: this is the M0 model address. */
	memcpy(addr, wiphy->perm_addr, ETH_ALEN);
	addr[0] |= 0x02;
	addr[0] &= ~0x01;
	addr[5] = (u8)(wiphy->perm_addr[5] ^ (u8)(slot + 1));
	eth_hw_addr_set(dev, addr);
	ether_addr_copy(wdev->address, addr);

	wdev->wiphy = wiphy;
	wdev->iftype = type;
	wdev->netdev = dev;
	dev->ieee80211_ptr = wdev;
	SET_NETDEV_DEV(dev, wiphy_dev(wiphy));
	if (type == NL80211_IFTYPE_MONITOR)
		dev->type = ARPHRD_IEEE80211_RADIOTAP;

	mutex_lock(&c->lock);
	if (c->removing) {
		mutex_unlock(&c->lock);
		err = -ESHUTDOWN;
		goto free_netdev;
	}
	memcpy(c->model.vap[slot].addr, addr, ETH_ALEN);
	c->vap_dev[slot] = dev;
	c->vap_wdev[slot] = wdev;
	mutex_unlock(&c->lock);

	/* RTNL + wiphy mutex are held by cfg80211 here; this registers the
	 * netdev and the wdev in one step (M0 gate: iw dev enumerates). */
	err = cfg80211_register_netdevice(dev);
	if (err) {
		mutex_lock(&c->lock);
		c->model.netdev_register_failures++;
		c->vap_dev[slot] = NULL;
		c->vap_wdev[slot] = NULL;
		hi5622_cfg_vap_drop(&c->model, slot);
		mutex_unlock(&c->lock);
		goto free_netdev;
	}

	mutex_lock(&c->lock);
	(void)hi5622_cfg_vap_mark_registered(&c->model, slot);
	mutex_unlock(&c->lock);

	/* Firmware user/vap add (g0 id7 + optional id8 notify) through the
	 * frame provider.  A missing builder must NOT break the validated M0
	 * netdev model: the failure is recorded and the status file/bench gate
	 * turns it into an assertion.  With no provider (default) this is a
	 * no-op -EOPNOTSUPP with a dmesg note. */
	hcc_rc = hi5622_vap_hcc_add(c, slot);
	dev_info(wiphy_dev(wiphy),
		 "cfg80211: VAP '%s' added (type %s, addr %pM) - netdev model live; firmware user_add (g0 id7/id8) %s\n",
		 name, hi5622_cfg_iftype_name(iftype), addr,
		 hcc_rc == 0 ? "committed via the frame provider" :
		 "pending (no frame provider / payload still [U])");
	return wdev;

free_netdev:
	free_netdev(dev);
drop_model:
	mutex_lock(&c->lock);
	hi5622_cfg_vap_drop(&c->model, slot);
	mutex_unlock(&c->lock);
	return ERR_PTR(err);
}

static int hi5622_cfg11_del_virtual_intf(struct wiphy *wiphy,
					 struct wireless_dev *wdev)
{
	struct hi5622_cfg80211 *c = wiphy_priv(wiphy);
	struct net_device *dev;
	u16 user_id = 0;
	u8 addr[ETH_ALEN] = {};
	unsigned int i, slot = HI5622_CFG_MAX_VAPS;

	mutex_lock(&c->lock);
	for (i = 0; i < HI5622_CFG_MAX_VAPS; i++) {
		if (c->vap_wdev[i] == wdev) {
			slot = i;
			break;
		}
	}
	if (slot == HI5622_CFG_MAX_VAPS) {
		mutex_unlock(&c->lock);
		return -ENOENT;
	}
	dev = c->vap_dev[slot];
	user_id = c->model.vap[slot].hcc_user_id;
	memcpy(addr, c->model.vap[slot].addr, ETH_ALEN);
	c->vap_dev[slot] = NULL;
	c->vap_wdev[slot] = NULL;
	hi5622_cfg_vap_drop(&c->model, slot);
	mutex_unlock(&c->lock);

	/* Firmware user delete (g0 id9) before the netdev goes away; same rule
	 * as add - a missing provider is recorded, the teardown always runs. */
	if (hi5622_vap_hcc_del(c, user_id, addr, c->band, (u8)slot))
		dev_info(wiphy_dev(wiphy),
			 "cfg80211: VAP %u firmware user_del (g0 id9) pending (no provider / payload [U]); netdev teardown continues\n",
			 slot);
	else
		dev_info(wiphy_dev(wiphy),
			 "cfg80211: VAP %u firmware user_del committed (g0 id9); netdev teardown\n",
			 slot);
	/* Unregisters the netdev too; the netdev (and the wdev inside its
	 * private area) is freed by netdev_run_todo at rtnl_unlock
	 * (needs_free_netdev).  Do NOT kfree(wdev) here: cfg80211's
	 * post-doit still reads wdev->netdev. */
	cfg80211_unregister_wdev(wdev);
	return 0;
}

static int hi5622_cfg11_change_virtual_intf(struct wiphy *wiphy,
					    struct net_device *dev,
					    enum nl80211_iftype type,
					    struct vif_params *params)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	if (!wdev)
		return -EINVAL;
	if (type == wdev->iftype) {
		if (params && params->use_4addr != wdev->use_4addr) {
			dev_warn(wiphy_dev(wiphy),
				 "cfg80211: 4-address change deferred to M5 (VAP delete/add needed)\n");
			return -EOPNOTSUPP;
		}
		return 0;
	}
	dev_warn(wiphy_dev(wiphy),
		 "cfg80211: type change %d -> %d not supported by the M1 VAP model (delete + add the interface)\n",
		 (int)wdev->iftype, (int)type);
	return -EOPNOTSUPP;
}

/* ---------------------------------------------------------------------
 * cfg80211 ops - registered -EOPNOTSUPP stubs (M2)
 *
 * These must exist so cfg80211 knows the capability is "not yet", but they
 * never pretend success: hostapd/netifd/iw get a clean -EOPNOTSUPP until the
 * corresponding HCC messages/frames exist (scan g0 id0xC / D2H station
 * events; task-103).
 * -------------------------------------------------------------------*/
static void hi5622_cfg11_stub_hit(struct wiphy *wiphy)
{
	struct hi5622_cfg80211 *c = wiphy_priv(wiphy);

	mutex_lock(&c->lock);
	c->model.op_stub_hits++;
	mutex_unlock(&c->lock);
}

static int hi5622_cfg11_get_station(struct wiphy *wiphy, struct net_device *dev,
				    const u8 *mac, struct station_info *sinfo)
{
	(void)dev;
	(void)mac;
	(void)sinfo;
	hi5622_cfg11_stub_hit(wiphy);
	return -EOPNOTSUPP;
}

static int hi5622_cfg11_dump_station(struct wiphy *wiphy, struct net_device *dev,
				     int idx, u8 *mac, struct station_info *sinfo)
{
	(void)dev;
	(void)idx;
	(void)mac;
	(void)sinfo;
	hi5622_cfg11_stub_hit(wiphy);
	return -EOPNOTSUPP;
}

/* scan / abort_scan / AP start-stop are implemented in hi5622_scan.c; the
 * provider-absent path returns -EOPNOTSUPP from there (never a fake scan). */

/* ---------------------------------------------------------------------
 * The M0-M2 ops table (M2 scan/AP entries route to hi5622_scan.c, which is
 * still provider-gated and returns -EOPNOTSUPP when no builder exists).
 * Everything else stays NULL: cfg80211 returns -EOPNOTSUPP for the deferred
 * M3+ ops without involving the driver (add_key etc. are M3).
 * -------------------------------------------------------------------*/
const struct cfg80211_ops hi5622_cfg11_ops = {
	.add_virtual_intf	= hi5622_cfg11_add_virtual_intf,
	.del_virtual_intf	= hi5622_cfg11_del_virtual_intf,
	.change_virtual_intf	= hi5622_cfg11_change_virtual_intf,
	.get_station		= hi5622_cfg11_get_station,
	.dump_station		= hi5622_cfg11_dump_station,
	.scan			= hi5622_scan_op_scan,
	.abort_scan		= hi5622_scan_op_abort,
	.start_ap		= hi5622_scan_op_start_ap,
	.stop_ap		= hi5622_scan_op_stop_ap,
};
