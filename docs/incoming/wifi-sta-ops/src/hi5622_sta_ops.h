/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_sta_ops.h - kernel glue for cfg80211 station operations and the
 * EAPOL/data RX delivery path (Hi5622V100 clean-room driver).
 *
 * ORIGINAL clean-room code (lane wifi-sta-ops, shared task task-149,
 * 2026-11-24).  Pure builders / parsing live in hi5622_sta_ops_logic.h/.c
 * (host-tested); this file is the kernel-only half.
 *
 * Fail-closed policy:
 *   sta_ops=0 (default)  every station op returns -EOPNOTSUPP (hostapd logs
 *                        "Could not add STA to kernel driver") and no frame
 *                        is built or posted;
 *   sta_ops=1            .add_station posts the decoded per-STA firmware
 *                        context (g0 id7 user_add + g0 id1 cmd 0x07d2
 *                        association sync), .del_station posts the vendor
 *                        WAL cfg-event kick (opcode 0x00d0) and optionally
 *                        the g0 id9 user_del (sta_user_del=1);
 *   eapol_rx=0 (default) received data frames are NOT delivered to the
 *                        netdev (the RX lane keeps its capture-only
 *                        behaviour);
 *   eapol_rx=1           dp_rx_drain delivers received 802.11 data frames
 *                        to the AP VAP netdev after 802.11->Ethernet
 *                        conversion: EAPOL always, other ethertypes only
 *                        for an authorized station.
 */
#ifndef HI5622_STA_OPS_H
#define HI5622_STA_OPS_H

#include <linux/skbuff.h>
#include <linux/types.h>
#include <net/cfg80211.h>

#include "hi5622_sta_ops_logic.h"

struct hi5622_cfg80211;
struct hi5622_pci;
struct net_device;

/* Module parameters (defined in hi5622_sta_ops.c). */
extern unsigned int hi5622_sta_ops_enable;	/* sta_ops */
extern unsigned int hi5622_eapol_rx;		/* eapol_rx */
extern unsigned int hi5622_sta_user_del;	/* sta_user_del */

void hi5622_sta_ops_init(struct hi5622_cfg80211 *c);
void hi5622_sta_ops_remove(struct hi5622_cfg80211 *c);
int hi5622_sta_ops_status(struct hi5622_cfg80211 *c, char *buf, size_t len);

/* cfg80211 ops. */
int hi5622_sta_ops_add(struct hi5622_cfg80211 *c, struct net_device *dev,
		       const u8 *mac, struct station_parameters *params);
int hi5622_sta_ops_change(struct hi5622_cfg80211 *c, struct net_device *dev,
			  const u8 *mac, struct station_parameters *params);
int hi5622_sta_ops_del(struct hi5622_cfg80211 *c, struct net_device *dev,
		       struct station_del_parameters *params);
int hi5622_sta_ops_get(struct hi5622_cfg80211 *c, struct net_device *dev,
		       const u8 *mac, struct station_info *sinfo);
int hi5622_sta_ops_dump(struct hi5622_cfg80211 *c, struct net_device *dev,
			int idx, u8 *mac, struct station_info *sinfo);

/* RX delivery from the dp_txrx completion drain (eapol_rx gate inside). */
void hi5622_sta_ops_rx_deliver(struct hi5622_pci *ctx, const u8 *mpdu,
			       u32 len);

/* Account a host -> netdev frame the data path cannot transmit yet (the
 * netdev xmit still drains the skb; this makes the drop visible). */
void hi5622_sta_ops_xmit_drop(struct net_device *dev);

#endif /* HI5622_STA_OPS_H */
