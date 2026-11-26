/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_dfs_cac.h - the kernel-side DFS/CAC runtime: the cfg80211
 * radar-detect ops (start_radar_detection/end_cac), the host CAC timer and
 * the fail-closed AP-start DFS gate, for the Hi5622V100 clean-room full-MAC
 * driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (lane dfs160 / shared task task-240, 2026-11-26).
 * The decisions (CAC 60000/600000 ms, the vendor 63 s/60 s tick arithmetic,
 * the 1800000 ms NOL, the DFS channel set incl. ch144) live in the portable
 * hi5622_dfs160.c; the 29-byte H2D g0-id1 key 0x07E7 ARM/CLEAR frame is
 * built by the sibling hi5622_dfs_set.c (lane wifi-5g-dfs-set) and posted
 * through the one fail-closed hi5622_frame_post() path.
 *
 * The runtime is fail closed at every gate:
 *   - dfs_cac=0 (default): no op, no frame, no AP-start gate - the
 *     byte-identical pre-feature behaviour;
 *   - dfs_cac=1: the ops are live; start_radar_detection probes the live
 *     firmware ARM prerequisites (the hi5622_dfs_set_precheck pack the
 *     AP-start dfs_set step uses) and returns -EAGAIN instead of posting a
 *     frame the firmware worker would refuse; the AP-start gate keeps a
 *     DFS AP from beaconing without a finished CAC on that channel.
 */
#ifndef HI5622_DFS_CAC_H
#define HI5622_DFS_CAC_H

#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <net/cfg80211.h>

#include "hi5622_dfs160.h"

struct hi5622_pci;
struct hi5622_cfg80211;
struct net_device;

struct hi5622_dfs_cac {
	struct hi5622_pci	*ctx;
	struct hi5622_dfs160_cac model;
	struct delayed_work	work;
	struct mutex		lock;		/* model + timer + op state */
	struct net_device	*dev;		/* CAC requester (cfg80211 op) */
	struct cfg80211_chan_def chandef;	/* the CAC chandef copy */
	unsigned int		link_id;
	unsigned int		requested;	/* the cfg80211 op owns the CAC */
	unsigned int		last_rc;	/* 0 or the last errno */
	unsigned int		arms;
	unsigned int		finishes;
	unsigned int		aborts;
	unsigned int		refusals;
};

/* dfs_cac module parameter (0 = off default, 1 = on, >1 refused). */
extern unsigned int hi5622_dfs_cac;

struct hi5622_dfs_cac *hi5622_dfs_cac_create(struct hi5622_pci *ctx);
void hi5622_dfs_cac_destroy(struct hi5622_dfs_cac *r);

/* cfg80211 ops (registered only in hi5622_cfg11_ops; both fail closed when
 * dfs_cac != 1). */
int hi5622_dfs_cac_op_start(struct wiphy *wiphy, struct net_device *dev,
			    struct cfg80211_chan_def *chandef,
			    u32 cac_time_ms, int link_id);
void hi5622_dfs_cac_op_end(struct wiphy *wiphy, struct net_device *dev,
			   unsigned int link_id);

/* start_ap fail-closed DFS gate: 0 when the feature is off, when the channel
 * is not a DFS channel, or when the CAC on that channel finished; -EAGAIN
 * when dfs_cac=1 and the CAC is not finished (no beacon on an unchecked DFS
 * channel); -EINVAL/-EIO on bad state. */
int hi5622_dfs_cac_ap_gate(struct hi5622_cfg80211 *c, u8 channel,
			   u16 center_freq);

/* The read-only sysfs line appended to cfg80211_status. */
int hi5622_dfs_cac_status_line(struct hi5622_cfg80211 *c, unsigned int ht_bw,
			       char *buf, size_t len);

#endif /* HI5622_DFS_CAC_H */
