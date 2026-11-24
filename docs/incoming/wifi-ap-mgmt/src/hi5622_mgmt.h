/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_mgmt.h - kernel-side management-frame TX/RX glue (Hi5622V100
 * clean-room driver, Cudy WR3000 v2.0).
 *
 * ORIGINAL clean-room code (lane wifi-ap-mgmt, shared task task-143).
 * The wire decisions live in the portable hi5622_mgmt_logic.c (host-tested);
 * this file only bridges cfg80211 <-> provider builder <-> the fail-closed
 * hi5622_frame_post() gate and the D2H direct reader.
 *
 * Fail-closed contract:
 *   mgmt_tx=0 (compiled default)  .mgmt_tx returns -EOPNOTSUPP, no DMA buffer
 *                                 is allocated and no frame is posted;
 *   mgmt_tx=1                     the decoded vendor WAL cfg-event mgmt-tx
 *                                 carrier is built and posted through the same
 *                                 allow_frames budget / gap / lane gates that
 *                                 every other provider frame uses.
 *   mgmt_rx=0 (compiled default)  D2H group-1 id-13 events are counted only;
 *   mgmt_rx=1                     decoded management frames are delivered to
 *                                 cfg80211_rx_mgmt() for the AP wdev.
 */
#ifndef _HI5622_MGMT_H
#define _HI5622_MGMT_H

#include <linux/types.h>

struct hi5622_pci;
struct wiphy;
struct wireless_dev;
struct cfg80211_mgmt_tx_params;

/* cfg80211_ops.mgmt_tx implementation (wired from hi5622_cfg80211_ops.c). */
int hi5622_mgmt_op_tx(struct wiphy *wiphy, struct wireless_dev *wdev,
		      struct cfg80211_mgmt_tx_params *params, u64 *cookie);

/* Device->host delivery hook, called by hi5622_hcc_deliver() for EVERY frame
 * on both receive paths.  Filters group/id itself, never blocks the service
 * thread (trylock), never writes a device register. */
void hi5622_mgmt_deliver(struct hi5622_pci *ctx, const u8 *hdr,
			 const u8 *payload, u32 avail, u32 wire_len);

/* Release the optional DMA frame buffer at cfg80211 teardown. */
void hi5622_mgmt_remove(struct hi5622_pci *ctx);

#endif /* _HI5622_MGMT_H */
