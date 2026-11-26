/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_monitor.h - local-mirror monitor mode integration for the Hi5622V100
 * clean-room driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (monitor2 lane, shared task task-246, 2026-11-26).
 * Materialises the cfg80211 monitor design documented by the monitor-mode lane
 * (task-241); no vendor source/object/firmware bytes are reproduced.
 *
 * It answers exactly the two cfg80211 gaps the IMAGE #3 driver had:
 *   1. .set_monitor_channel (metadata-only chandef store; NEVER a firmware
 *      tune), fail-closed at monitor=0;
 *   2. radiotap delivery of the frames that already reach the two existing RX
 *      sinks (mgmt D2H event and the completion-ring data path).
 * The capture is a *mirror of what the AP VAP receives*, not promiscuous
 * mode; the true sniffer needs a firmware switch the vendor driver does not
 * have.  The mirror is a no-op until the F1 RX gate (host-bound g1 id13 copy)
 * is fixed, because the sinks themselves are silent until then.
 */
#ifndef _HI5622_MONITOR_H
#define _HI5622_MONITOR_H

#include <linux/types.h>

struct cfg80211_chan_def;
struct hi5622_cfg80211;
struct hi5622_pci;
struct himg_rx_frame;
struct net_device;
struct wiphy;

/* cfg80211 ops implementation (registered as .set_monitor_channel). */
int hi5622_monitor_op_set_channel(struct wiphy *wiphy, struct net_device *dev,
				  struct cfg80211_chan_def *chandef);

/* One-shot status line when the monitor interface is created. */
void hi5622_monitor_iface_created(struct hi5622_cfg80211 *c);

/* Mirror hooks: called by the two existing RX sinks BEFORE their own policy
 * gates, so the mirror does not depend on mgmt_rx/eapol_rx.  Both take the cfg
 * lock with mutex_trylock only (HCC service thread; a busy lock drops the
 * mirror frame and counts it, never blocks the RX path). */
void hi5622_monitor_rx_mgmt(struct hi5622_pci *ctx,
			    const struct himg_rx_frame *rx);
void hi5622_monitor_rx_data(struct hi5622_pci *ctx, const u8 *mpdu, u32 len);

#endif /* _HI5622_MONITOR_H */
