/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_dfsb_k.h - kernel glue for the 160 MHz width selector + DFS/CAC/NOL
 * feature (lane dfs160b / shared task task-245, 2026-11-26).
 *
 * ORIGINAL clean-room code.  The portable decisions live in hi5622_dfsb.h;
 * this header exposes the module parameters, the two cfg80211 ops and the
 * start_ap plan to hi5622_scan.c / hi5622_cfg80211_ops.c.
 */
#ifndef HI5622_DFSB_K_H
#define HI5622_DFSB_K_H

#include <linux/types.h>
#include <net/cfg80211.h>

#include "hi5622_dfsb.h"

/* ht_bw: 0 = legacy 0x0124 body[1] byte (bench-validated wire), 1 = derive
 * the vendor bandwidth selector from the cfg80211 chandef width. */
extern unsigned int hi5622_ht_bw;
/* dfs_cac: 0 = no DFS service (default), 1 = cfg80211 start_radar_detection/
 * end_cac CAC runtime + the start_ap DFS gate.  One meaning per load: it is
 * mutually exclusive with the ladder dfs_set parameter (both own 0x07E7). */
extern unsigned int hi5622_dfs_cac;

/* Runtime lifecycle: hi5622_cfg80211_init/remove call these. */
void hi5622_dfsb_k_init(void);
void hi5622_dfsb_k_exit(void);

/* The start_ap feature plan (hi5622_scan.c): the width selector byte and the
 * live CAC gate for one start_ap.  Fail closed (-EINVAL for a bad parameter
 * combination, the width errors, -EAGAIN for the DFS gate). */
int hi5622_dfsb_k_ap_plan(u8 band, u8 channel, u32 width, u32 ccfs1,
			  u32 ccfs2, unsigned int dfs_set_on,
			  struct hi5622_dfsb_ap_plan *out);

/* struct cfg80211_ops hooks. */
int hi5622_dfsb_k_op_start_radar_detection(struct wiphy *wiphy,
					   struct net_device *dev,
					   struct cfg80211_chan_def *chandef,
					   u32 cac_time_ms, int link_id);
void hi5622_dfsb_k_op_end_cac(struct wiphy *wiphy, struct net_device *dev,
			      unsigned int link_id);

#endif /* HI5622_DFSB_K_H */
