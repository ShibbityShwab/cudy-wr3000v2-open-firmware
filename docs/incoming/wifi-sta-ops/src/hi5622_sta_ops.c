// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_sta_ops.c - cfg80211 station operations + EAPOL/data RX delivery
 * (Hi5622V100 clean-room driver, Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (lane wifi-sta-ops, shared task task-149,
 * 2026-11-24).  Decoded facts and evidence markers: ../REPORT.md and
 * ../TABLES.md; the portable builders/parsers are in hi5622_sta_ops_logic.c.
 *
 * The firmware per-STA context installed by .add_station is the vendor
 * hmac AP-SME sequence (hmac_ap_assoc_check_chip_exist_user_proc):
 *   g0 id7  user_add   (180 B: u16 AID, MAC, type=2, RSSI)
 *   g0 id1  cfg event  (cmd 0x07d2: u16 AID, u8 asoc_state=1)
 * The firmware table A row 0x07d2 -> worker CA 0x83e25 was decoded from the
 * local PD4 image; it validates the user by AID and writes the state byte.
 * .del_station mirrors the vendor cfg80211 op (WAL cfg-event kick opcode
 * 0x00d0); the explicit g0 id9 user_del is decoded and host-tested but
 * deliberately only posted when the sta_user_del module parameter enables
 * it (deleting a firmware user twice is the documented E2 crash class).
 */
#include <linux/device.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/string.h>
#include <linux/types.h>
#include <net/cfg80211.h>

#include "hi5622_cfg80211.h"
#include "hi5622_driver.h"
#include "hi5622_frame_post.h"
#include "hi5622_sta_frames.h"
#include "hi5622_sta_ops.h"
#include "hi5622_sta_ops_logic.h"

/* 0 = fail closed (default), 1 = enable the cfg80211 station ops. */
unsigned int hi5622_sta_ops_enable;
module_param_named(sta_ops, hi5622_sta_ops_enable, uint, 0644);
MODULE_PARM_DESC(sta_ops,
		 "cfg80211 station ops: 0=fail closed (default; .add_station returns -EOPNOTSUPP), 1=post the decoded per-STA firmware context (g0 id7 user_add + g0 id1 cmd 0x07d2) and allow .del/.change/.get/.dump");

/* 0 = no data RX delivery (default), 1 = deliver received data frames to the
 * AP VAP netdev (EAPOL always, other ethertypes only for authorized STAs). */
unsigned int hi5622_eapol_rx;
module_param_named(eapol_rx, hi5622_eapol_rx, uint, 0644);
MODULE_PARM_DESC(eapol_rx,
		 "deliver received 802.11 data frames to the AP netdev (0=fail closed default, 1=EAPOL always + authorized-station data); used by dp_rx_drain");

/* 0 = only the vendor .del_station kick (default), 1 = also post the g0 id9
 * user_del for the station.  Double-deleting a firmware user is the E2 crash
 * class, so this stays off unless a ladder explicitly enables it. */
unsigned int hi5622_sta_user_del;
module_param_named(sta_user_del, hi5622_sta_user_del, uint, 0644);
MODULE_PARM_DESC(sta_user_del,
		 "also post the decoded g0 id9 user_del on .del_station (0=vendor kick only default, 1=experimental)");

#define HSOP_RX_ETH_MAX	(HSOP_ETH_HDR_LEN + 0x618u)	/* + max MPDU payload */

static unsigned int hsop_flag_authorized(u32 mask, u32 set)
{
	u32 bit = 1u << NL80211_STA_FLAG_AUTHORIZED;

	if (!(mask & bit))
		return 0;
	return (set & bit) ? 1u : 0u;
}

static unsigned int hsop_flag_authenticated(u32 mask, u32 set)
{
	u32 bit = 1u << NL80211_STA_FLAG_AUTHENTICATED;

	if (!(mask & bit))
		return 0;
	return (set & bit) ? 1u : 0u;
}

static unsigned int hsop_flag_associated(u32 mask, u32 set)
{
	u32 bit = 1u << NL80211_STA_FLAG_ASSOCIATED;

	if (!(mask & bit))
		return 0;
	return (set & bit) ? 1u : 0u;
}

void hi5622_sta_ops_init(struct hi5622_cfg80211 *c)
{
	if (!c)
		return;
	hsop_sta_table_init(&c->sta_table);
	c->sta_ops_ap_dev = NULL;
	c->sta_ops_seq = 0;
}

void hi5622_sta_ops_remove(struct hi5622_cfg80211 *c)
{
	if (!c)
		return;
	hsop_sta_table_init(&c->sta_table);
	c->sta_ops_ap_dev = NULL;
}

static int hsop_vap_slot(struct hi5622_cfg80211 *c, struct net_device *dev)
{
	unsigned int i;

	if (!c || !dev)
		return -EINVAL;
	for (i = 0; i < HI5622_CFG_MAX_VAPS; i++)
		if (c->vap_dev[i] == dev)
			return (int)i;
	return -ENODEV;
}

static int hsop_is_ap_dev(struct net_device *dev)
{
	return dev && dev->ieee80211_ptr &&
	       dev->ieee80211_ptr->iftype == NL80211_IFTYPE_AP;
}

static int hsop_post_user_add(struct hi5622_cfg80211 *c, u8 band, u8 vap_id,
			      u16 aid, const u8 *mac)
{
	struct hsop_user_add_params p;
	u8 wire[HSOP_USER_ADD_WIRE_LEN];
	size_t len = 0;
	int rc;

	memset(&p, 0, sizeof(p));
	p.band = band;
	p.vap_id = vap_id;
	p.hdr2 = 0;
	p.dst = 0;
	p.aid = aid;
	memcpy(p.mac, mac, 6);
	p.type = (u8)HSOP_USER_TYPE_AP;
	p.rssi = (s8)HSOP_DEFAULT_RSSI;
	rc = hsop_build_user_add(wire, sizeof(wire), &len, &p);
	if (rc)
		return rc;
	rc = hsop_check_user_add(wire, len);
	if (rc)
		return rc;
	rc = hi5622_frame_post(c->ctx, wire, len, "sta_user_add", NULL);
	if (!rc)
		c->sta_ops_user_add++;
	return rc;
}

static int hsop_post_asoc_syn(struct hi5622_cfg80211 *c, u8 band, u8 vap_id,
			      u16 aid, u8 state)
{
	struct hsop_asoc_syn_params p;
	u8 wire[HSOP_ASOC_SYN_WIRE_LEN];
	size_t len = 0;
	int rc;

	memset(&p, 0, sizeof(p));
	p.band = band;
	p.vap_id = vap_id;
	p.hdr2 = 0;
	p.dst = 0;
	p.module = vap_id;
	p.aid = aid;
	p.state = state;
	rc = hsop_build_asoc_syn(wire, sizeof(wire), &len, &p);
	if (rc)
		return rc;
	rc = hsop_check_asoc_syn(wire, len);
	if (rc)
		return rc;
	rc = hi5622_frame_post(c->ctx, wire, len, "sta_asoc_syn", NULL);
	if (!rc)
		c->sta_ops_asoc_syn++;
	return rc;
}

static int hsop_post_kick(struct hi5622_cfg80211 *c, const u8 *mac, u16 reason,
			  u8 subtype)
{
	struct hsop_kick_params p;
	u8 body[HSOP_KICK_BODY_LEN];
	u8 wire[HI5622_STA_FRAME_MAX];
	size_t len = 0;
	int rc;

	if (!mac)
		return -EINVAL;
	memset(&p, 0, sizeof(p));
	memcpy(p.mac, mac, 6);
	p.reason = reason;
	p.subtype = subtype;
	rc = hsop_build_kick_body(body, sizeof(body), &p);
	if (rc)
		return rc;
	rc = hi5622_sta_wal_frame(wire, sizeof(wire), &len,
				  HI5622_STA_WAL_CMD_DISCONNECT, body,
				  sizeof(body), c->sta_ops_seq++, 0u);
	if (rc)
		return rc;
	rc = hi5622_frame_post(c->ctx, wire, len, "sta_kick", NULL);
	if (!rc)
		c->sta_ops_kick++;
	return rc;
}

static int hsop_post_user_del(struct hi5622_cfg80211 *c, u8 band, u8 vap_id,
			      u16 aid, const u8 *mac)
{
	struct hsop_user_del_params p;
	u8 wire[HSOP_USER_DEL_WIRE_LEN];
	size_t len = 0;
	int rc;

	memset(&p, 0, sizeof(p));
	p.band = band;
	p.vap_id = vap_id;
	p.hdr2 = 0;
	p.dst = 0;
	p.uid = aid;
	memcpy(p.mac, mac, 6);
	p.flag = (u8)HSOP_USER_DEL_FLAG_AP;
	rc = hsop_build_user_del(wire, sizeof(wire), &len, &p);
	if (rc)
		return rc;
	rc = hsop_check_user_del(wire, len);
	if (rc)
		return rc;
	rc = hi5622_frame_post(c->ctx, wire, len, "sta_user_del", NULL);
	if (!rc)
		c->sta_ops_user_del++;
	return rc;
}

static void hsop_fill_sinfo(const struct hsop_sta *s, struct station_info *sinfo)
{
	memset(sinfo, 0, sizeof(*sinfo));
	sinfo->filled = BIT(NL80211_STA_INFO_STA_FLAGS);
	sinfo->sta_flags.mask = s->flags_mask;
	sinfo->sta_flags.set = s->flags_set;
}

int hi5622_sta_ops_add(struct hi5622_cfg80211 *c, struct net_device *dev,
		       const u8 *mac, struct station_parameters *params)
{
	struct hsop_sta *existing;
	u8 band, vap_id;
	int slot, rc;

	if (!hi5622_sta_ops_enable) {
		pr_info_once("hi5622: .add_station reached - fails closed (sta_ops=0; see docs/incoming/wifi-sta-ops/)\n");
		return -EOPNOTSUPP;
	}
	if (!c || !dev || !mac || !params)
		return -EINVAL;
	if (!is_valid_ether_addr(mac))
		return -EINVAL;
	if (!hsop_is_ap_dev(dev))
		return -EOPNOTSUPP;
	if (params->aid == 0u || params->aid > 0x0fffu)
		return -EINVAL;
	slot = hsop_vap_slot(c, dev);
	if (slot < 0)
		return slot;
	mutex_lock(&c->lock);
	existing = hsop_sta_find(&c->sta_table, mac);
	if (existing) {
		c->sta_table.stats.dup_updates++;
		mutex_unlock(&c->lock);
		return -EEXIST;
	}
	band = c->band;
	vap_id = (u8)slot;
	rc = hsop_post_user_add(c, band, vap_id, params->aid, mac);
	if (rc) {
		c->sta_table.stats.refused++;
		mutex_unlock(&c->lock);
		dev_warn(&c->ctx->pdev->dev,
			 "sta_ops: .add_station %pM AID %u: user_add (g0 id7) NOT posted (%d) - fail closed (allow_frames budget / HCC gate)\n",
			 mac, params->aid, rc);
		return rc;
	}
	rc = hsop_post_asoc_syn(c, band, vap_id, params->aid,
				(u8)HSOP_ASOC_STATE_ASSOC);
	if (rc) {
		c->sta_table.stats.refused++;
		mutex_unlock(&c->lock);
		dev_warn(&c->ctx->pdev->dev,
			 "sta_ops: .add_station %pM AID %u: association sync (g0 id1 cmd 0x%x) NOT posted (%d) - user_add already committed, station not recorded\n",
			 mac, params->aid, HSOP_CFG_CMD_ASOC_SYN, rc);
		return rc;
	}
	rc = hsop_sta_add(&c->sta_table, mac, params->aid,
			  params->sta_flags_mask, params->sta_flags_set);
	if (rc) {
		mutex_unlock(&c->lock);
		return rc;
	}
	existing = hsop_sta_find(&c->sta_table, mac);
	if (existing) {
		existing->authorized = (int)hsop_flag_authorized(
			params->sta_flags_mask, params->sta_flags_set);
		existing->authenticated = (int)hsop_flag_authenticated(
			params->sta_flags_mask, params->sta_flags_set);
		existing->associated = (int)hsop_flag_associated(
			params->sta_flags_mask, params->sta_flags_set);
	}
	c->sta_ops_ap_dev = dev;
	mutex_unlock(&c->lock);
	dev_info(&c->ctx->pdev->dev,
		 "sta_ops: STA %pM added (AID %u, vap %d, band %u): firmware context committed (g0 id7 user_add 180 B + g0 id1 cmd 0x%x asoc state %u); host station table=%u\n",
		 mac, params->aid, slot, band, HSOP_CFG_CMD_ASOC_SYN,
		 HSOP_ASOC_STATE_ASSOC, hsop_sta_count(&c->sta_table));
	return 0;
}

int hi5622_sta_ops_change(struct hi5622_cfg80211 *c, struct net_device *dev,
			  const u8 *mac, struct station_parameters *params)
{
	struct hsop_sta *s;
	int rc;

	if (!hi5622_sta_ops_enable)
		return -EOPNOTSUPP;
	if (!c || !dev || !mac || !params)
		return -EINVAL;
	if (!hsop_is_ap_dev(dev))
		return -EOPNOTSUPP;
	mutex_lock(&c->lock);
	s = hsop_sta_find(&c->sta_table, mac);
	if (!s) {
		c->sta_table.stats.refused++;
		mutex_unlock(&c->lock);
		return -ENOENT;
	}
	rc = hsop_sta_update(&c->sta_table, mac, params->sta_flags_mask,
			     params->sta_flags_set);
	if (rc) {
		mutex_unlock(&c->lock);
		return rc;
	}
	s->authorized = (int)hsop_flag_authorized(s->flags_mask, s->flags_set);
	s->authenticated = (int)hsop_flag_authenticated(s->flags_mask,
							s->flags_set);
	s->associated = (int)hsop_flag_associated(s->flags_mask, s->flags_set);
	dev_info(&c->ctx->pdev->dev,
		 "sta_ops: CHANGE_STATION %pM: authorized=%d authenticated=%d associated=%d (host-side flag mirror; no extra firmware message - [U])\n",
		 mac, s->authorized, s->authenticated, s->associated);
	mutex_unlock(&c->lock);
	return 0;
}

int hi5622_sta_ops_del(struct hi5622_cfg80211 *c, struct net_device *dev,
		       struct station_del_parameters *params)
{
	struct hsop_sta *s;
	u8 mac[6];
	u16 aid;
	u8 band, vap_id;
	int slot, rc = 0;

	if (!hi5622_sta_ops_enable)
		return -EOPNOTSUPP;
	if (!c || !dev || !params)
		return -EINVAL;
	if (!hsop_is_ap_dev(dev))
		return -EOPNOTSUPP;
	slot = hsop_vap_slot(c, dev);
	if (slot < 0)
		return slot;
	mutex_lock(&c->lock);
	band = c->band;
	vap_id = (u8)slot;
	if (!params->mac) {
		/* cfg80211 flush-all: host state only (firmware users are left
		 * for the stop_ap/power-cycle owner; a per-station kick storm
		 * would exceed the post budget). */
		c->sta_table.stats.flush++;
		hsop_sta_table_init(&c->sta_table);
		mutex_unlock(&c->lock);
		dev_info(&c->ctx->pdev->dev,
			 "sta_ops: DEL_STATION flush (host table cleared; no firmware message)\n");
		return 0;
	}
	s = hsop_sta_find(&c->sta_table, params->mac);
	if (!s) {
		c->sta_table.stats.unknown_del++;
		mutex_unlock(&c->lock);
		return -ENOENT;
	}
	memcpy(mac, s->mac, 6);
	aid = s->aid;
	if (hi5622_sta_user_del) {
		int drc = hsop_post_user_del(c, band, vap_id, aid, mac);

		if (drc) {
			dev_warn(&c->ctx->pdev->dev,
				 "sta_ops: .del_station %pM: g0 id9 user_del NOT posted (%d)\n",
				 mac, drc);
			rc = drc;
		}
	}
	{
		int krc = hsop_post_kick(c, mac, params->reason_code,
					 params->subtype);

		if (krc) {
			dev_warn(&c->ctx->pdev->dev,
				 "sta_ops: .del_station %pM: WAL cfg-event kick (0x%x) NOT posted (%d)\n",
				 mac, HSOP_WAL_KICK_CMD, krc);
			rc = rc ? rc : krc;
		}
	}
	(void)hsop_sta_del(&c->sta_table, mac);
	mutex_unlock(&c->lock);
	dev_info(&c->ctx->pdev->dev,
		 "sta_ops: DEL_STATION %pM (AID %u, subtype %u, reason %u): kick%s; host table=%u\n",
		 mac, aid, params->subtype, params->reason_code,
		 rc ? " incomplete" : " committed", hsop_sta_count(&c->sta_table));
	return rc;
}

int hi5622_sta_ops_get(struct hi5622_cfg80211 *c, struct net_device *dev,
		       const u8 *mac, struct station_info *sinfo)
{
	struct hsop_sta *s;

	if (!hi5622_sta_ops_enable)
		return -EOPNOTSUPP;
	if (!c || !dev || !mac || !sinfo)
		return -EINVAL;
	mutex_lock(&c->lock);
	s = hsop_sta_find(&c->sta_table, mac);
	if (!s) {
		mutex_unlock(&c->lock);
		return -ENOENT;
	}
	hsop_fill_sinfo(s, sinfo);
	mutex_unlock(&c->lock);
	return 0;
}

int hi5622_sta_ops_dump(struct hi5622_cfg80211 *c, struct net_device *dev,
			int idx, u8 *mac, struct station_info *sinfo)
{
	struct hsop_sta s;
	int rc;

	if (!hi5622_sta_ops_enable)
		return -EOPNOTSUPP;
	if (!c || !dev || !mac || !sinfo)
		return -EINVAL;
	if (idx < 0)
		return -EINVAL;
	mutex_lock(&c->lock);
	rc = hsop_sta_at(&c->sta_table, (u32)idx, &s);
	if (!rc) {
		memcpy(mac, s.mac, 6);
		hsop_fill_sinfo(&s, sinfo);
	}
	mutex_unlock(&c->lock);
	return rc;
}

void hi5622_sta_ops_rx_deliver(struct hi5622_pci *ctx, const u8 *mpdu,
			       u32 len)
{
	struct hi5622_cfg80211 *c;
	struct hsop_rx_info rx;
	struct hsop_sta *s;
	struct net_device *dev;
	struct sk_buff *skb;
	u8 *eth;
	size_t eth_len = 0;
	int rc;

	if (!ctx || !ctx->cfg || !hi5622_eapol_rx || !mpdu || len == 0u)
		return;
	c = ctx->cfg;
	if (!c->sta_ops_ap_dev)
		return;
	mutex_lock(&c->lock);
	c->sta_ops_rx_frames++;
	rc = hsop_rx_parse(mpdu, len, &rx);
	if (rc || !rx.is_data) {
		c->sta_ops_rx_bad++;
		mutex_unlock(&c->lock);
		return;
	}
	if (rx.is_eapol)
		c->sta_ops_rx_eapol++;
	s = hsop_sta_find(&c->sta_table, rx.src);
	if (!s) {
		c->sta_ops_rx_dropped++;
		mutex_unlock(&c->lock);
		return;
	}
	if (!hsop_rx_deliverable(&rx, s->authorized)) {
		c->sta_ops_rx_dropped++;
		mutex_unlock(&c->lock);
		return;
	}
	dev = c->sta_ops_ap_dev;
	if (!netif_running(dev)) {
		c->sta_ops_rx_dropped++;
		mutex_unlock(&c->lock);
		return;
	}
	eth = kmalloc(HSOP_RX_ETH_MAX, GFP_KERNEL);
	if (!eth) {
		c->sta_ops_rx_dropped++;
		mutex_unlock(&c->lock);
		return;
	}
	rc = hsop_rx_to_eth(mpdu, len, eth, HSOP_RX_ETH_MAX, &eth_len, &rx);
	if (rc) {
		kfree(eth);
		c->sta_ops_rx_bad++;
		mutex_unlock(&c->lock);
		return;
	}
	skb = netdev_alloc_skb(dev, (unsigned int)eth_len + 2u);
	if (!skb) {
		kfree(eth);
		c->sta_ops_rx_dropped++;
		mutex_unlock(&c->lock);
		return;
	}
	memcpy(skb_put(skb, eth_len), eth, eth_len);
	kfree(eth);
	skb->protocol = eth_type_trans(skb, dev);
	netif_rx(skb);
	c->sta_ops_rx_delivered++;
	mutex_unlock(&c->lock);
}

void hi5622_sta_ops_xmit_drop(struct net_device *dev)
{
	struct wireless_dev *wdev;
	struct hi5622_cfg80211 *c;

	if (!dev)
		return;
	wdev = dev->ieee80211_ptr;
	if (!wdev || !wdev->wiphy)
		return;
	c = wiphy_priv(wdev->wiphy);
	if (!c)
		return;
	c->sta_ops_xmit_dropped++;
}

int hi5622_sta_ops_status(struct hi5622_cfg80211 *c, char *buf, size_t len)
{
	int n;

	if (!c || !buf)
		return -EINVAL;
	n = scnprintf(buf, len,
		      "sta-ops: sta_ops=%u eapol_rx=%u sta_user_del=%u stations=%u added=%u updated=%u deleted=%u refused=%u\n",
		      hi5622_sta_ops_enable, hi5622_eapol_rx,
		      hi5622_sta_user_del, hsop_sta_count(&c->sta_table),
		      c->sta_table.stats.added, c->sta_table.stats.updated,
		      c->sta_table.stats.deleted, c->sta_table.stats.refused);
	if (n > 0 && (size_t)n < len) {
		int m = scnprintf(buf + n, len - (size_t)n,
				  "sta-ops wire: user_add=%u asoc_syn=%u kick=%u user_del=%u; rx frames=%u eapol=%u delivered=%u dropped=%u bad=%u; xmit_dropped=%u\n",
				  c->sta_ops_user_add, c->sta_ops_asoc_syn,
				  c->sta_ops_kick, c->sta_ops_user_del,
				  c->sta_ops_rx_frames, c->sta_ops_rx_eapol,
				  c->sta_ops_rx_delivered,
				  c->sta_ops_rx_dropped, c->sta_ops_rx_bad,
				  c->sta_ops_xmit_dropped);
		if (m > 0)
			n += m;
	}
	return n;
}
