/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_data_tx.h - kernel glue for the peer-aware host -> AP data TX path
 * (Hi5622V100 clean-room driver, Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (lane wifi-data-tx, shared task task-150,
 * 2026-11-29).  The portable 802.3 -> 802.11 MPDU / EAPOL / decision logic
 * lives in hi5622_data_tx_logic.c (host-tested); this header is the kernel
 * binding used by:
 *   - the AP netdev's ndo_start_xmit (hostapd's EAPOL-Key msg1/msg3 and all
 *     bridged data frames),
 *   - the data_tx_probe sysfs action (the same builder/ring post without an
 *     association, for the attended ladder),
 *   - the read-only data_tx_status evidence line.
 *
 * The ring push + TXQ publish + completion read reuse the proven dp TX
 * sequence (wifi-dp-tuning TX COMPLETED) through two small helpers exported
 * by hi5622_dp_txrx.c; this layer owns the in-flight frame pool and the
 * bounded completion/timeout policy.
 *
 * Fail-closed: data_tx=0 (default) means the AP netdev keeps the old
 * accounted drop (hi5622_sta_ops_xmit_drop) and nothing is allocated.
 */
#ifndef HI5622_DATA_TX_H
#define HI5622_DATA_TX_H

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/types.h>

struct device;
struct device_attribute;
struct hi5622_cfg80211;

/* Module parameters (defined in hi5622_data_tx.c). */
extern unsigned int hi5622_data_tx_enable;	/* data_tx: 0 = fail closed */
extern unsigned int hi5622_data_tx_peer;	/* data_tx_peer: peer required */
extern unsigned int hi5622_data_tx_protect;	/* data_tx_protect: 0/1/2   */
extern unsigned int hi5622_data_tx_timeout_ms;	/* completion timeout      */

/* Probe the tx/rx object and allocate the in-flight pool (called from
 * hi5622_cfg80211_init; no device access).  Returns 0 even when the pool
 * cannot be allocated - the lane then stays disabled and accounts drops. */
int  hi5622_data_tx_init(struct hi5622_cfg80211 *c);
void hi5622_data_tx_remove(struct hi5622_cfg80211 *c);

/* ndo_start_xmit: consumes the skb in every path (NETDEV_TX_OK). */
netdev_tx_t hi5622_data_tx_xmit(struct sk_buff *skb, struct net_device *dev);

/* sysfs: data_tx_status (RO) and data_tx_probe (WO, write 1). */
int hi5622_data_tx_status(struct hi5622_cfg80211 *c, char *buf, size_t len);
ssize_t hi5622_data_tx_probe_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count);

#endif /* HI5622_DATA_TX_H */
