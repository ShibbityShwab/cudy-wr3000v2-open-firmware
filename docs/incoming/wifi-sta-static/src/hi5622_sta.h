/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_sta.h - kernel-side cfg80211 STA connect/disconnect path for the
 * Hi5622V100 clean-room driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (wifi-sta-static lane, shared task task-141).
 * The wire frames are built by the portable hi5622_sta_frames.c; this file
 * only maps cfg80211 state to those frames and posts them through the
 * inherited fail-closed hi5622_frame_post() gate.
 */
#ifndef HI5622_STA_H
#define HI5622_STA_H

#include <linux/types.h>

struct cfg80211_connect_params;
struct hi5622_cfg80211;
struct net_device;

/* cfg80211 .connect / .disconnect.  Both are fail-closed: they return
 * -EOPNOTSUPP unless the hi5622_sta_join module parameter enables the
 * vendor WAL cfg-event join path (see hi5622_sta.c). */
int hi5622_sta_connect(struct hi5622_cfg80211 *c, struct net_device *dev,
		       struct cfg80211_connect_params *sme);
int hi5622_sta_disconnect(struct hi5622_cfg80211 *c, struct net_device *dev,
			  u16 reason_code);

/* Free the per-connect host buffer (called from hi5622_cfg80211_remove()). */
void hi5622_sta_remove(struct hi5622_cfg80211 *c);

#endif /* HI5622_STA_H */
