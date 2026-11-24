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
#include "hi5622_driver.h"
#include "hi5622_scan.h"
#include "hi5622_keys.h"
#include "hi5622_sta.h"

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
	/* Stability hardening: a committed user_add re-creates the firmware user
	 * after a controlled stop_ap; the next start_ap only needs the base. */
	if (hcc_rc == 0 && c->ctx) {
		c->ctx->fw_vap_deleted = false;
		c->ctx->ap_stopped_hostside = false;
	}
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
	if (c->ctx->ap_stopped_hostside) {
		/* Bench E2: user_del (g0 id9) of an AP-started firmware VAP crashes the
		 * chip (dangling VAP context).  A netdev teardown after a host-side
		 * stop therefore performs NO firmware message: clear the host state and
		 * let the next boot own the firmware context. */
		c->ctx->ap_stopped_hostside = false;
		c->ctx->ap_started = false;
		c->ctx->ap_seq_dirty = false;
		c->ctx->ap_seq_frames = 0;
		c->ctx->vap_base_posted = false;
		c->ctx->vap_base_iftype = 0;
		dev_info(wiphy_dev(wiphy),
			 "cfg80211: VAP %u teardown after host-side stop: NO g0 id9 (bench E2 crash); host state cleared, power-cycle to reset the firmware AP\n",
			 slot);
	} else if (c->ctx->fw_vap_deleted) {
		/* A controlled stop_ap already posted the g0 id9 user_del: never send a
		 * second delete for a firmware user that no longer exists. */
		c->ctx->fw_vap_deleted = false;
		c->ctx->ap_stopped_hostside = false;
		c->ctx->ap_stopped_hostside = false;
		c->ctx->vap_base_posted = false;
		c->ctx->vap_base_iftype = 0;
		c->ctx->ap_started = false;
		c->ctx->ap_seq_dirty = false;
		c->ctx->ap_seq_frames = 0;
		dev_info(wiphy_dev(wiphy),
			 "cfg80211: VAP %u firmware user_del already committed by stop_ap; no second g0 id9; latches cleared\n",
			 slot);
	} else if (hi5622_vap_hcc_del(c, user_id, addr, c->band, (u8)slot)) {
		dev_info(wiphy_dev(wiphy),
			 "cfg80211: VAP %u firmware user_del (g0 id9) pending (no provider / payload [U]); netdev teardown continues; VAP-base/AP latches kept (fail-closed)\n",
			 slot);
	} else {
		/* The firmware VAP object is gone, so the per-load base latch is stale;
		 * the next scan/AP start must re-post the mode-1 VAP base.  This is the
		 * exact delete/re-add path that crashed the firmware in the
		 * wifi-stability boot A (ap_cfg against a dead VAP object). */
		c->ctx->vap_base_posted = false;
		c->ctx->vap_base_iftype = 0;
		c->ctx->ap_started = false;
		c->ctx->ap_seq_dirty = false;
		c->ctx->ap_seq_frames = 0;
		dev_info(wiphy_dev(wiphy),
			 "cfg80211: VAP %u firmware user_del committed (g0 id9); VAP-base/AP latches cleared (next scan/AP start re-posts the base)\n",
			 slot);
	}
	/* Unregisters the netdev too; the netdev (and the wdev inside its
	 * private area) is freed by netdev_run_todo at rtnl_unlock
	 * (needs_free_netdev).  Do NOT kfree(wdev) here: cfg80211's
	 * post-doit still reads wdev->netdev. */
	cfg80211_unregister_wdev(wdev);
	return 0;
}

/* wifi-ap-start lane (task-121, hostapd bring-up): the SET_INTERFACE path.
 *
 * hostapd configures the interface mode with NL80211_CMD_SET_INTERFACE.  When
 * the type actually changes (e.g. wlan0 created managed, hostapd wants AP),
 * cfg80211 calls cfg80211_change_iface() -> this op; cfg80211_change_iface()
 * then WARNs unless the driver has updated wdev->iftype, so the driver MUST
 * store the new type here before returning 0.  When the interface already has
 * the requested type, nl80211 does not call this op at all (no change), so
 * same-type SET_INTERFACE succeeds regardless.
 *
 * next member: nl80211_set_interface() leaves params->use_4addr = -1 when
 * NL80211_ATTR_4ADDR is absent, so -1 means "not specified" and must not be
 * compared against wdev->use_4addr (that bug made every SET_INTERFACE that
 * carries an explicit vif_params fail).  Only an explicit 4-address request
 * is refused (M5 work: the vendor VAP needs a delete/add).
 */
static int hi5622_cfg11_change_virtual_intf(struct wiphy *wiphy,
					    struct net_device *dev,
					    enum nl80211_iftype type,
					    struct vif_params *params)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	if (!wdev)
		return -EINVAL;
	dev_info(wiphy_dev(wiphy),
		 "cfg80211: SET_INTERFACE reached (iftype %d -> %d, use_4addr=%d)\n",
		 (int)wdev->iftype, (int)type,
		 params ? params->use_4addr : -1);
	if (hi5622_cfg11_model_iftype(type) == 0)
		return -EOPNOTSUPP;
	if (params && params->use_4addr != -1 &&
	    params->use_4addr != wdev->use_4addr) {
		dev_warn(wiphy_dev(wiphy),
			 "cfg80211: 4-address change deferred to M5 (VAP delete/add needed)\n");
		return -EOPNOTSUPP;
	}
	if (type == wdev->iftype)
		return 0;
	dev_warn(wiphy_dev(wiphy),
		 "cfg80211: type change %d -> %d accepted by the M1 netdev model (hostapd set_mode; firmware VAP iftype is fixed by the VAP base messages)\n",
		 (int)wdev->iftype, (int)type);
	if (type == NL80211_IFTYPE_MONITOR)
		dev->type = ARPHRD_IEEE80211_RADIOTAP;
	else if (wdev->iftype == NL80211_IFTYPE_MONITOR)
		dev->type = ARPHRD_ETHER;
	wdev->iftype = type;
	return 0;
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
/* hostapd's shared nl80211 init gate (driver_nl80211_capa.c) requires the
 * wiphy to advertise NL80211_CMD_AUTHENTICATE or NL80211_CMD_CONNECT:
 * cfg80211 lists CONNECT when .connect exists, and hostapd otherwise bails
 * out with "Driver does not support authentication/association or connect
 * commands" before any AP op.
 *
 * wifi-sta-static lane (task-141): the op now routes to the decoded vendor
 * STA path (hi5622_sta.c).  It stays fail-closed - with the shipped default
 * sta_join=0 the STA path returns -EOPNOTSUPP, builds no frame and never
 * writes a device register; the AP path never calls it. */
static int hi5622_cfg11_connect(struct wiphy *wiphy, struct net_device *dev,
				struct cfg80211_connect_params *params)
{
	return hi5622_sta_connect(wiphy_priv(wiphy), dev, params);
}

/* Image polish (wifi-image-polish, task-135): the one-shot mgmt_tx probe used
 * to dump a stack on every hostapd AP start, which dmesg reports as an
 * "Exception stack" during an otherwise clean AP bring-up.  Keep the probe
 * available for M4 work but off by default. */
unsigned int hi5622_mgmt_diag;
module_param_named(mgmt_diag, hi5622_mgmt_diag, uint, 0644);
MODULE_PARM_DESC(mgmt_diag,
		 "diagnostic: dump the caller stack from the fail-closed mgmt_tx op (0=off default, 1=on)");

/* hostapd AP-mode bring-up (task-121): cfg80211's NL80211_CMD_REGISTER_FRAME
 * handler refuses to install a management-frame filter when the driver has no
 * .mgmt_tx op ("not much point in registering if we can't reply",
 * net/wireless/nl80211.c nl80211_register_mgmt).  hostapd's
 * nl80211_setup_ap() subscribes to auth/assoc/reassoc/disassoc/deauth/
 * probe-request frames from its set_mode(AP) path; without the op the
 * subscription fails and hostapd prints "Could not configure driver mode"
 * even though SET_INTERFACE succeeded (and, for an interface already in AP
 * mode, no type-change message appears at all).
 * The op fails closed: management TX belongs to the data-plane/M4 work and is
 * never faked; this only advertises the capability the registration gate
 * needs. */
static int hi5622_cfg11_mgmt_tx(struct wiphy *wiphy,
				struct wireless_dev *wdev,
				struct cfg80211_mgmt_tx_params *params,
				u64 *cookie)
{
	(void)wiphy;
	(void)wdev;
	(void)params;
	(void)cookie;
	pr_info_once("hi5622: mgmt_tx op reached - fails closed (management TX is data-plane/M4 work)\n");
	if (hi5622_mgmt_diag)
		dump_stack();	/* caller stack only when explicitly requested */
	return -EOPNOTSUPP;
}

/* Paired with .connect: wiphy_new_nm() WARNs when a driver registers
 * .connect without .disconnect (net/wireless/core.c).  Routes to the vendor
 * WAL disconnect (opcode 0x00d0, 16-byte body) through the same fail-closed
 * STA path as connect. */
static int hi5622_cfg11_disconnect(struct wiphy *wiphy,
				   struct net_device *dev, u16 reason_code)
{
	return hi5622_sta_disconnect(wiphy_priv(wiphy), dev, reason_code);
}

/* SET_BEACON path (running-AP beacon update).  hostapd only reaches it after a
 * successful NEW_BEACON (cfg80211 also requires a non-zero AP beacon interval
 * and this op), so it is not on the initial AP-start path.  The firmware
 * beacon update (g0 id1 key 0x0127 with start flag 0) is not wired yet, so
 * this ACCEPTS the update host-side and logs the parameters: a bench
 * instrument with no firmware write (M3 work). */
static int hi5622_cfg11_change_beacon(struct wiphy *wiphy,
				      struct net_device *dev,
				      struct cfg80211_ap_update *info)
{
	dev_info(wiphy_dev(wiphy),
		 "cfg80211: SET_BEACON accepted host-side only (beacon head=%zu tail=%zu, no firmware write yet - M3)\n",
		 info ? info->beacon.head_len : 0,
		 info ? info->beacon.tail_len : 0);
	return 0;
}

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
	/* WPA2 key path (wifi-wpa-static lane, task-139): decoded firmware
	 * g0 id1 cmds 0x0078/0x007A/0x0079, fail-closed behind wpa_keys=0. */
	.add_key		= hi5622_keys_op_add,
	.del_key		= hi5622_keys_op_del,
	.set_default_key	= hi5622_keys_op_set_default,
	/* wifi-wpa3-sae lane (task-144): PMF group management key (IGTK)
	 * default-key selection.  hostapd installs the IGTK at AP startup
	 * whenever ieee80211w != 0 and then sends NL80211_CMD_SET_KEY with
	 * NL80211_KEY_DEFAULT_MGMT; cfg80211 returns -EOPNOTSUPP unless this
	 * op exists (net/wireless/nl80211.c nl80211_set_key default-mgmt
	 * branch), which failed wpa_init_keys() for every PMF profile. */
	.set_default_mgmt_key	= hi5622_keys_op_set_default_mgmt,
	/* Advertise NL80211_CMD_CONNECT so hostapd passes its driver init gate;
	 * fail-closed and never used by the AP path (task-121). */
	.connect		= hi5622_cfg11_connect,
	/* Needed so cfg80211 accepts hostapd's AP-mode management-frame
	 * subscriptions; fails closed (see above). */
	.mgmt_tx		= hi5622_cfg11_mgmt_tx,
	/* cfg80211 WARN pairing for .connect (core.c wiphy_new_nm). */
	.disconnect		= hi5622_cfg11_disconnect,
	/* Host-side-accepted SET_BEACON (see above; no firmware write yet). */
	.change_beacon		= hi5622_cfg11_change_beacon,
};
