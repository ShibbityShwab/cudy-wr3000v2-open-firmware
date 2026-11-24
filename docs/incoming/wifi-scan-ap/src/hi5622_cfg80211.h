/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_cfg80211.h - cfg80211 full-MAC layer (M0-M2) for the Hi5622V100
 * clean-room Wi-Fi driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (wifi-cfg80211 lane, shared task task-104,
 * 2026-11-24).  No vendor source/object/firmware/calibration material is
 * reproduced.  See ../REPORT.md for the design, the op<->HCC message mapping
 * table and the evidence markers.
 *
 * Layering (vendor-parity, wifi-vendor-userspace/REPORT.md sec 8):
 *
 *   hi5622_cfg80211.c      wiphy lifecycle, band/channel/cipher capability,
 *                          readiness-gated registration work, RTNL teardown,
 *                          status/sysfs surface
 *   hi5622_cfg80211_ops.c  struct cfg80211_ops: add/del/change interface,
 *                          -EOPNOTSUPP stubs for the M2 ops, netdev ndos
 *   hi5622_cfg80211_logic.c portable VAP model / registration gate /
 *                          op<->HCC message map (host-tested)
 *
 * The cfg80211 layer performs NO device access and sends NO H2D message: the
 * wiphy is only registered after the HCC ready handshake has been decoded
 * (g4 id1 + g4 id2), and the VAP create/destroy firmware messages are
 * deferred until their builders exist (task-103), exactly as the vendor
 * orders wiphy registration after ready inside hmac/wal init.
 */
#ifndef _HI5622_CFG80211_H
#define _HI5622_CFG80211_H

#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <net/cfg80211.h>

#include "hi5622_cfg80211_logic.h"
#include "hi5622_scan.h"

struct hi5622_pci;

/* One VAP: the wireless_dev lives in the netdev private area so that it
 * outlives cfg80211's post-doit references and is freed with the netdev
 * (needs_free_netdev after unregister_netdevice).  This is the vendor/mac80211
 * pattern and the reason del_virtual_intf must NOT kfree the wdev itself. */
struct hi5622_vap {
	struct wireless_dev	wdev;
};

/* Per-endpoint cfg80211 state; embedded in the wiphy private area
 * (wiphy_new_nm(sizeof_priv = sizeof(struct hi5622_cfg80211))). */
struct hi5622_cfg80211 {
	struct hi5622_pci		*ctx;
	struct wiphy			*wiphy;
	struct mutex			lock;		/* model + registration state */
	struct work_struct		reg_work;
	struct hi5622_scan		scan;	/* M2/M3 scan + AP-start wiring */
	struct hi5622_cfg_model		model;
	struct net_device		*vap_dev[HI5622_CFG_MAX_VAPS];
	struct wireless_dev		*vap_wdev[HI5622_CFG_MAX_VAPS];
	u8				band;		/* enum hi5622_band */
	bool				wiphy_registered;
	bool				removing;
	bool				reg_scheduled;
	bool				ready_logged;
};

/* The op table (hi5622_cfg80211_ops.c); referenced by wiphy_new_nm(). */
extern const struct cfg80211_ops hi5622_cfg11_ops;

/* Probe/remove lifecycle and the service-loop readiness hook. */
int  hi5622_cfg80211_init(struct hi5622_pci *ctx);
void hi5622_cfg80211_remove(struct hi5622_pci *ctx);

/* Idempotent, cheap: called from the HCC service pass; schedules the wiphy
 * registration work once the ready handshake has completed. */
void hi5622_cfg80211_ready(struct hi5622_pci *ctx);

/* sysfs attribute group (read-only status). */
int  hi5622_cfg80211_add_attrs(struct device *dev);
void hi5622_cfg80211_del_attrs(struct device *dev);

#endif /* _HI5622_CFG80211_H */
