/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_keys.h - cfg80211 key-install path (add/del/set_default_key) for the
 * Hi5622V100 clean-room full-MAC driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (wifi-wpa-static lane, shared task task-139,
 * 2026-11-28).  No vendor source/object/firmware byte is reproduced; the
 * derived addresses/constants and the evidence markers are in ../REPORT.md.
 *
 * This layer owns exactly:
 *   1. the fail-closed policy: module param wpa_keys=0 (default) makes all
 *      three cfg80211 ops return -EOPNOTSUPP without a device write;
 *   2. the cfg80211 -> neutral-parameter mapping (key index, pairwise flag,
 *      MAC, cipher suite, key/seq bounds) with the firmware worker's
 *      index/cipher-class rules enforced before any frame is built;
 *   3. the post call through hi5622_frame_post() (the same allow_frames /
 *      gap / armed / phase gates as scan and AP start).
 *
 * The wire bytes are produced by the frame provider (hi5622_key_frames.c
 * builders, registered by hi5622_vapscan_provider.c with the KEY_* caps);
 * with no provider every op stays -EOPNOTSUPP.
 */
#ifndef _HI5622_KEYS_H
#define _HI5622_KEYS_H

#include <linux/errno.h>
#include <linux/types.h>

struct hi5622_cfg80211;
struct wiphy;
struct net_device;
struct key_params;

/* Module parameters (defined in hi5622_keys.c). */
extern unsigned int hi5622_wpa_keys;		/* 0 = fail-closed default */
extern unsigned int hi5622_wpa_key_module;	/* g0 hdr[3] module index */

/* struct cfg80211_ops key callbacks (hi5622_cfg80211_ops.c registers them). */
int hi5622_keys_op_add(struct wiphy *wiphy, struct net_device *netdev,
		       int link_id, u8 key_index, bool pairwise,
		       const u8 *mac_addr, struct key_params *params);
int hi5622_keys_op_del(struct wiphy *wiphy, struct net_device *netdev,
		       int link_id, u8 key_index, bool pairwise,
		       const u8 *mac_addr);
int hi5622_keys_op_set_default(struct wiphy *wiphy, struct net_device *netdev,
			       int link_id, u8 key_index, bool unicast,
			       bool multicast);
/* wifi-wpa3-sae lane (task-144): PMF default management key (IGTK, idx 4/5).
 * The firmware worker needs the mgmt cipher in the body, learned from the
 * preceding add_key; the op refuses when no IGTK add was seen. */
int hi5622_keys_op_set_default_mgmt(struct wiphy *wiphy,
				    struct net_device *netdev,
				    int link_id, u8 key_index);

/* Read-only status (sysfs): counters + last key operation. */
int hi5622_keys_status(struct hi5622_cfg80211 *c, char *buf, size_t len);

#endif /* _HI5622_KEYS_H */
