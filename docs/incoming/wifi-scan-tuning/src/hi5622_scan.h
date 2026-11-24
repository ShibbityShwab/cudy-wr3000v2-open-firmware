/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_scan.h - kernel side of the cfg80211 scan / AP / VAP-message wiring
 * for the Hi5622V100 clean-room full-MAC driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (wifi-scan-ap lane, shared task task-113,
 * 2026-11-24).  No vendor source/object/firmware byte is reproduced.
 *
 * This layer owns exactly four things:
 *   1. the cfg80211_scan_request lifetime: .scan validates + posts the request
 *      through the frame provider, .abort_scan asks for cancellation, and a
 *      delayed work item reports cfg80211_scan_done() exactly once;
 *   2. the D2H scan-event hook: g1 id15/16/17 and g2 id2 frames from BOTH
 *      receive paths are decoded by the provider and reported as cfg80211
 *      BSS entries (the payload layouts are [U], so a missing decoder is a
 *      counted error, never a guessed offset);
 *   3. the start_ap/stop_ap wiring to the WAL group-2 frame family (design +
 *      provider stub until the payload exists; the same fail-closed gate);
 *   4. the VAP add/delete firmware messages (g0 id7/id8/id9) called from the
 *      netdev model after/before cfg80211 registration.
 *
 * All decisions live in the portable model hi5622_scan_logic.h; this file is
 * plumbing + cfg80211 API use only.
 */
#ifndef _HI5622_SCAN_H
#define _HI5622_SCAN_H

#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <net/cfg80211.h>

#include "hi5622_scan_logic.h"

struct hi5622_pci;
struct hi5622_cfg80211;

struct hi5622_scan {
	struct hi5622_cfg80211	*cfg;
	struct mutex		lock;		/* model + request + work state */
	struct hi5622_scan_model	model;
	struct cfg80211_scan_request	*request;	/* valid until scan_done */
	struct delayed_work	work;
	bool			initialized;
	bool			posting;	/* .scan op still posting the frame */
	atomic_t		dropped_busy;	/* events lost to trylock */
	u32			scan_timeout_ms;
	u32			user_id;	/* user id used in builders [U] */
	/* task-133 (wifi-scan-tuning): one cfg80211 scan can be split into several
	 * g0 id0xC requests because the firmware was measured to stop after the
	 * first 6 requested channels.  The channel plan is kept here; the model's
	 * channel plan is always the CURRENT request (batch). */
	u16			freqs[HI5622_SCAN_MAX_CHANNELS];
	u32			n_freqs;
	u32			batch_param;	/* raw scan_req_batch parameter */
	u32			batch_first;	/* first channel index of the current batch */
	u32			batch_n;	/* channels in the current batch */
	u32			batches_sent;
	u32			batches_total;
	bool			batch_pending;	/* gap timer armed for the next batch */
	u64			batch_due_ms;
};

/* cfg80211 layer lifecycle (hi5622_cfg80211.c calls these). */
int  hi5622_scan_init(struct hi5622_cfg80211 *c);
void hi5622_scan_remove(struct hi5622_cfg80211 *c);

/* cfg80211 ops (hi5622_cfg80211_ops.c calls these). */
int  hi5622_scan_op_scan(struct wiphy *wiphy,
			 struct cfg80211_scan_request *request);
void hi5622_scan_op_abort(struct wiphy *wiphy, struct wireless_dev *wdev);
int  hi5622_scan_op_start_ap(struct wiphy *wiphy, struct net_device *dev,
			     struct cfg80211_ap_settings *settings);
int  hi5622_scan_op_stop_ap(struct wiphy *wiphy, struct net_device *dev,
			    unsigned int link_id);

/* Device->host delivery hook, called by hi5622_hcc_deliver() for EVERY frame
 * from both receive paths (before the core state machine, which only handles
 * the groups it knows).  hdr is the 12-byte HCC header, payload/avail the
 * header-stripped body as far as the receive path could snapshot it. */
void hi5622_scan_deliver(struct hi5622_pci *ctx, const u8 *hdr,
			 const u8 *payload, u32 avail);

/* VAP firmware messages: add (g0 id7 + optional id8 notify) and delete
 * (g0 id9).  Called by the netdev model; return 0 only when the frame was
 * committed and consumed.  A failure is recorded in the model and leaves the
 * netdev usable (the M0/M1 lifecycle must not break because a builder is
 * absent) - the status file/bench gate is what turns that into an assertion. */
int  hi5622_vap_hcc_add(struct hi5622_cfg80211 *c, u16 slot);
int  hi5622_vap_hcc_del(struct hi5622_cfg80211 *c, u16 user_id, const u8 *addr,
			 u8 band, u8 vap_id);

/* Read-only status (sysfs): provider, post gate and scan model. */
int  hi5622_scan_status(struct hi5622_cfg80211 *c, char *buf, size_t len);

#endif /* _HI5622_SCAN_H */
