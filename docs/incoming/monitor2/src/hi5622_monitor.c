// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_monitor.c - local-mirror monitor mode (cfg80211 monitor iface +
 * radiotap delivery) for the Hi5622V100 clean-room driver.
 *
 * ORIGINAL clean-room code (monitor2 lane, shared task task-246, 2026-11-26).
 * See hi5622_monitor.h for the semantics; the portable byte-vector logic is
 * hi5622_monitor_logic.c (host-tested by tools/monitor_selftest.c).
 *
 * Hard rules honoured here:
 *   - no firmware write from any monitor path (channel set is metadata only);
 *   - monitor>1 is refused, monitor=0 (default) is inert;
 *   - never blocks the RX path (mutex_trylock + bounded copy only).
 */
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <net/cfg80211.h>

#include "hi5622_cfg80211.h"
#include "hi5622_driver.h"
#include "hi5622_mgmt_logic.h"
#include "hi5622_monitor.h"
#include "hi5622_monitor_logic.h"

/* monitor mode: 0 = off (default, fail-closed), 1 = local mirror.
 * This is the only knob; there is deliberately no "promiscuous" value. */
static unsigned int hi5622_monitor_mode;
module_param_named(monitor, hi5622_monitor_mode, uint, 0644);
MODULE_PARM_DESC(monitor,
		 "monitor mode: 0=off (default), 1=local mirror of received frames (no firmware write); >1 refused");

int hi5622_monitor_op_set_channel(struct wiphy *wiphy, struct net_device *dev,
				  struct cfg80211_chan_def *chandef)
{
	struct hi5622_cfg80211 *c;
	unsigned int m = READ_ONCE(hi5622_monitor_mode);

	(void)dev;
	if (!wiphy || !chandef || !chandef->chan)
		return -EINVAL;
	c = wiphy_priv(wiphy);
	if (!c)
		return -EINVAL;
	/* monitor=0 keeps the old honest behaviour; monitor>1 is refused. */
	if (!himon_mode_ok(m) || m != HIMON_LOCAL_MIRROR)
		return -EOPNOTSUPP;
	if (!c->wiphy_registered)
		return -ENODEV;

	mutex_lock(&c->lock);
	c->mon_chandef = *chandef;
	c->mon_valid = true;
	mutex_unlock(&c->lock);

	dev_info(wiphy_dev(wiphy),
		 "monitor: channel set %u MHz width %d (metadata only - no firmware tune)\n",
		 chandef->chan->center_freq, (int)chandef->width);
	return 0;
}

void hi5622_monitor_iface_created(struct hi5622_cfg80211 *c)
{
	unsigned int m = READ_ONCE(hi5622_monitor_mode);

	if (!c || !c->ctx || !c->ctx->pdev)
		return;
	dev_info(&c->ctx->pdev->dev,
		 "monitor: monitor iface created (mode=%u %s; mirror=%s, capture needs the F1)\n",
		 m, himon_mode_name(m),
		 m == HIMON_LOCAL_MIRROR && himon_mode_ok(m) ? "enabled" : "disabled");
}

/* Copy one 802.11 MPDU to the first monitor netdev as radiotap + MPDU.
 * Runs in the HCC service thread; trylock only, no retry, no firmware I/O. */
static void hi5622_monitor_mirror(struct hi5622_pci *ctx, const u8 *frame,
				  u32 len, u16 freq, int have_chan, int rssi,
				  int have_rssi, bool is_mgmt)
{
	struct hi5622_cfg80211 *c;
	struct wireless_dev *wdev = NULL;
	struct net_device *dev;
	struct sk_buff *skb;
	u8 rt[HIMON_RT_MAX];
	size_t rt_len;
	unsigned int i;
	unsigned int m = READ_ONCE(hi5622_monitor_mode);

	if (!ctx || !frame || len == 0u)
		return;
	if (!himon_mode_ok(m) || m != HIMON_LOCAL_MIRROR)
		return;
	c = READ_ONCE(ctx->cfg);
	if (!c || !c->wiphy_registered)
		return;
	if (!mutex_trylock(&c->lock)) {
		c->mon_drop_busy++;
		return;
	}
	for (i = 0; i < HI5622_CFG_MAX_VAPS; i++) {
		if (c->vap_wdev[i] &&
		    c->vap_wdev[i]->iftype == NL80211_IFTYPE_MONITOR) {
			wdev = c->vap_wdev[i];
			break;
		}
	}
	if (!wdev) {
		c->mon_drop_no_dev++;
		goto out_unlock;
	}
	dev = wdev->netdev;
	if (!dev || !netif_running(dev)) {
		c->mon_drop_down++;
		goto out_unlock;
	}
	if (len > HIMON_MAX_FRAME) {
		c->mon_drop_bad_len++;
		goto out_unlock;
	}
	/* The data sink carries the MPDU only: fall back to the chandef the
	 * operator stored with SET_CHANNEL (metadata), never a guess. */
	if (!have_chan && c->mon_valid && c->mon_chandef.chan) {
		freq = (u16)c->mon_chandef.chan->center_freq;
		have_chan = 1;
	}
	rt_len = himon_rt_build(rt, sizeof(rt), freq, have_chan, rssi,
				have_rssi);
	if (!rt_len) {
		c->mon_rt_fail++;
		goto out_unlock;
	}
	skb = dev_alloc_skb((unsigned int)(rt_len + len));
	if (!skb) {
		c->mon_drop_nomem++;
		goto out_unlock;
	}
	skb_put_data(skb, rt, rt_len);
	skb_put_data(skb, frame, len);
	if (is_mgmt)
		c->mon_rx_mgmt++;
	else
		c->mon_rx_data++;
	dev_hold(dev);
	mutex_unlock(&c->lock);

	skb->dev = dev;
	skb->protocol = htons(ETH_P_802_2);
	skb->pkt_type = PACKET_OTHERHOST;
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb_reset_mac_header(skb);
	netif_rx(skb);
	dev_put(dev);
	dev_info_ratelimited(&ctx->pdev->dev,
			     "monitor: mirrored %s frame (%u MHz, len %u, rssi %d)\n",
			     is_mgmt ? "mgmt" : "data",
			     have_chan ? (unsigned int)freq : 0u, len, rssi);
	return;

out_unlock:
	mutex_unlock(&c->lock);
}

void hi5622_monitor_rx_mgmt(struct hi5622_pci *ctx,
			    const struct himg_rx_frame *rx)
{
	if (!rx || !rx->frame || rx->frame_len == 0u)
		return;
	/* The decoded mgmt event carries the channel frequency and the vendor
	 * RSSI byte; forward them as radiotap metadata when plausible. */
	hi5622_monitor_mirror(ctx, rx->frame, rx->frame_len, rx->freq, 1,
			      (rx->rssi <= 0 && rx->rssi >= -120) ?
			      (int)rx->rssi : 0,
			      (rx->rssi <= 0 && rx->rssi >= -120), 1);
}

void hi5622_monitor_rx_data(struct hi5622_pci *ctx, const u8 *mpdu, u32 len)
{
	/* No per-frame metadata on this sink: radiotap channel comes from the
	 * stored chandef when present (R1), else the 8-byte R0 header. */
	hi5622_monitor_mirror(ctx, mpdu, len, 0, 0, 0, 0, 0);
}
