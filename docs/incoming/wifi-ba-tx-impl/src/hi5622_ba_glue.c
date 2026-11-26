// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_ba_glue.c - kernel binding for the fail-closed TX-direction BA core
 * (lane wifi-ba-tx-impl, shared task-217).
 *
 * See hi5622_ba_glue.h for the integration points and hi5622_ba.h for the
 * decoded wire contract.  All H2D/action posts are deferred to a workqueue
 * because the D2H/mgmt hooks run in the HCC service kthread and the data-TX
 * hook runs in ndo_start_xmit context; neither may run the sleeping post path
 * inline.
 */
#include <linux/etherdevice.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "hi5622_driver.h"
#include "hi5622_cfg80211.h"
#include "hi5622_frame_post.h"
#include "hi5622_sta_ops_logic.h"
#include "hi5622_mgmt_logic.h"
#include "hi5622_ba.h"
#include "hi5622_ba_glue.h"

/* Bound on one deferred D2H/mgmt payload (34 B relay action frame fits). */
#define HI5622_BA_GLUE_D2H_MAX		96u
#define HI5622_BA_GLUE_MGMT_MAX		64u
/* A burst window is closed when no new MPDU arrived for this long. */
#define HI5622_BA_GLUE_BURST_HOLD_MS	20u
/* AP data TX pacing: at most one negotiation/boundary work item at a time. */

struct hi5622_ba_glue {
	struct mutex		lock;		/* serializes the portable core */
	struct hi5622_pci	*ctx;
	struct hi5622_ba_core	core;
	bool			ap_up;
	u8			ap_band;
	u8			ap_channel;
	u8			ap_mac[6];
	u32			seq;		/* mgmt-TX cookie sequence */

	/* deferred D2H BA event (one slot; a newer event replaces it) */
	struct work_struct	d2h_work;
	spinlock_t		d2h_lock;
	u8			d2h_buf[HI5622_BA_GLUE_D2H_MAX];
	u32			d2h_len;
	bool			d2h_pending;

	/* deferred received action frame (one slot) */
	struct work_struct	mgmt_work;
	spinlock_t		mgmt_lock;
	u8			mgmt_buf[HI5622_BA_GLUE_MGMT_MAX];
	u32			mgmt_len;
	bool			mgmt_pending;

	/* deferred AP data-TX step + burst window close */
	struct work_struct	tx_work;
	struct delayed_work	end_work;
	spinlock_t		tx_lock;
	u8			tx_peer[6];
	u16			tx_seq;
	u8			tx_tid;
	u32			tx_count;
	bool			tx_pending;
	bool			tx_open;

	/* evidence counters */
	u32			posts;
	u32			refused;
	u32			refused_gate;
	u32			refused_no_ap;
	u32			refused_no_sta;
	u32			d2h_events;
	u32			d2h_dropped;
	u32			mgmt_events;
	u32			mgmt_dropped;
	u32			tx_events;
	int			last_rc;
};

static struct hi5622_ba_glue ba_glue = {
	.lock = __MUTEX_INITIALIZER(ba_glue.lock),
	.d2h_lock = __SPIN_LOCK_UNLOCKED(ba_glue.d2h_lock),
	.mgmt_lock = __SPIN_LOCK_UNLOCKED(ba_glue.mgmt_lock),
	.tx_lock = __SPIN_LOCK_UNLOCKED(ba_glue.tx_lock),
};

static const struct hi5622_ba_ops ba_glue_ops;

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static bool ba_glue_armed(void)
{
	return hi5622_ba_check_enabled() == HI5622_BA_OK;
}

/* The peer's firmware user index (the BA messages' object key) from the
 * cfg80211 station table; -ENOENT when the station is not known. */
static int ba_glue_uid(struct hi5622_pci *ctx, const u8 *mac, u16 *uid)
{
	struct hi5622_cfg80211 *c = ctx ? ctx->cfg : NULL;
	const struct hsop_sta *st;

	if (!c || !mac || !uid)
		return -ENODEV;
	st = hsop_sta_find_const(&c->sta_table, mac);
	if (!st)
		return -ENOENT;
	*uid = st->aid;
	return 0;
}

/* The single H2D wire path; the core built the complete header + payload. */
static int ba_glue_post_h2d(void *vctx, const u8 *wire, size_t len)
{
	struct hi5622_pci *ctx = vctx;
	const char *what = "ba_h2d";
	u16 id;
	int rc;

	if (!ctx || !wire || len < HI5622_BA_HDR_LEN)
		return -EINVAL;
	id = (u16)(wire[6] | ((u16)wire[7] << 8));
	if (id == HI5622_BA_H2D_BA_RELAY)
		what = "ba_relay";
	else if (id == HI5622_BA_H2D_AMPDU_TX)
		what = "ba_ampdu";
	else if (id == HI5622_BA_H2D_TX_BA_NOTIFY)
		what = "ba_notify";
	rc = hi5622_frame_post(ctx, wire, len, what, NULL);
	if (rc)
		ba_glue.refused++;
	else
		ba_glue.posts++;
	ba_glue.last_rc = rc;
	return rc;
}

/* The proven mgmt-TX carrier (HCC g0 id 6, 48 B device control + inline
 * 802.11 action frame) for the three BA action frames. */
static int ba_glue_tx_action(void *vctx, const u8 *frame, size_t len)
{
	struct hi5622_pci *ctx = vctx;
	struct himg_tx_params p;
	u8 wire[HIMG_MGMT_TX_MAX_TOTAL];
	size_t wlen = 0;
	u16 uid = 0;
	int rc;

	if (!ctx || !frame || !len)
		return -EINVAL;
	if (!ba_glue.ap_up || !ba_glue.ap_channel) {
		ba_glue.refused_no_ap++;
		return -ENOTCONN;
	}
	memset(&p, 0, sizeof(p));
	(void)ba_glue_uid(ctx, frame + 4, &uid);	/* addr1 = the peer */
	p.channel = ba_glue.ap_channel;
	p.len = (u16)len;
	p.seq = ++ba_glue.seq;
	p.dst = 0u;			/* single AP VAP -> dst nibble 0 */
	p.frame = frame;
	p.frame_len = (u16)len;
	p.band = ba_glue.ap_band;
	p.hdr2 = 0u;
	p.vap_id = 0u;
	p.cookie = (u16)(ba_glue.seq & 0x0fu);
	p.user_id = uid;
	rc = himg_build_hcc_mgmt_tx(wire, sizeof(wire), &wlen, &p);
	if (rc) {
		ba_glue.refused++;
		ba_glue.last_rc = rc;
		return rc;
	}
	rc = hi5622_frame_post(ctx, wire, wlen, "ba_mgmt_tx", NULL);
	if (rc)
		ba_glue.refused++;
	else
		ba_glue.posts++;
	ba_glue.last_rc = rc;
	return rc;
}

static void ba_glue_log(void *vctx, const char *msg)
{
	struct hi5622_pci *ctx = vctx;

	if (!ctx || !ctx->pdev || !msg)
		return;
	dev_info_ratelimited(&ctx->pdev->dev, "ba: %s\n", msg);
}

static const struct hi5622_ba_ops ba_glue_ops = {
	.post_h2d = ba_glue_post_h2d,
	.tx_action = ba_glue_tx_action,
	.log = ba_glue_log,
};

/* Bind the peer in the portable core (caller holds ba_glue.lock).  The uid
 * comes from the cfg80211 station table; an unknown station is refused (the
 * firmware BA messages are keyed by that user index - never guess it). */
static struct hi5622_ba_peer *ba_glue_bind(struct hi5622_pci *ctx,
					   const u8 *mac, u16 *uid_out)
{
	struct hi5622_ba_peer *peer;
	u16 uid = 0;

	if (ba_glue_uid(ctx, mac, &uid))
		return NULL;
	peer = hi5622_ba_peer_bind(&ba_glue.core, mac, uid);
	if (peer && uid_out)
		*uid_out = uid;
	return peer;
}

/* ------------------------------------------------------------------ */
/* deferred D2H BA events                                              */
/* ------------------------------------------------------------------ */

static void ba_glue_d2h_workfn(struct work_struct *work)
{
	struct hi5622_ba_glue *g = container_of(work, struct hi5622_ba_glue,
						d2h_work);
	struct hi5622_pci *ctx = g->ctx;
	u8 buf[HI5622_BA_GLUE_D2H_MAX];
	u32 len = 0;
	u16 uid;
	u8 id;
	int rc;

	spin_lock_bh(&g->d2h_lock);
	if (!g->d2h_pending) {
		spin_unlock_bh(&g->d2h_lock);
		return;
	}
	memcpy(buf, g->d2h_buf, g->d2h_len);
	len = g->d2h_len;
	g->d2h_pending = false;
	spin_unlock_bh(&g->d2h_lock);
	if (!ctx || len < HI5622_BA_HDR_LEN + 4)
		return;
	id = buf[6];
	uid = (u16)(buf[HI5622_BA_HDR_LEN] |
		    ((u16)buf[HI5622_BA_HDR_LEN + 1] << 8));
	mutex_lock(&g->lock);
	if (hi5622_ba_peer_get_by_uid(&g->core, uid) == NULL) {
		/* resolve the user index through the cfg80211 station table:
		 * the D2H event carries no MAC, the host side does */
		const struct hsop_sta *st;
		int i;

		st = NULL;
		for (i = 0; i < (int)HSOP_MAX_STA; i++) {
			if (ctx->cfg && ctx->cfg->sta_table.sta[i].used &&
			    ctx->cfg->sta_table.sta[i].aid == uid) {
				st = &ctx->cfg->sta_table.sta[i];
				break;
			}
		}
		if (!st) {
			g->refused++;
			g->last_rc = -ENOENT;
			mutex_unlock(&g->lock);
			return;
		}
		if (!hi5622_ba_peer_bind(&g->core, st->mac, uid)) {
			g->refused++;
			g->last_rc = -ENOSPC;
			mutex_unlock(&g->lock);
			return;
		}
	}
	rc = hi5622_ba_d2h(&g->core, &ba_glue_ops, buf, len);
	g->last_rc = rc;
	g->d2h_events++;
	dev_dbg(&ctx->pdev->dev, "ba: d2h id %u uid %u rc %d\n", id, uid, rc);
	mutex_unlock(&g->lock);
}

void hi5622_ba_glue_d2h(struct hi5622_pci *ctx, const u8 *hdr,
			const u8 *payload, u32 payload_avail, u32 wire_len)
{
	u32 n;

	if (!ctx || !hdr || !payload)
		return;
	if (!ba_glue_armed())
		return;
	/* Cheap filter first: the four decoded D2H BA event ids on the D2H
	 * event group (group 3).  Group 4 carries the boot/ready ids 1..4 and
	 * must never be read as a BA event. */
	if ((hdr[0] & 0x0fu) != 3u)
		return;
	if (hdr[6] != HI5622_BA_D2H_CREATE_BA &&
	    hdr[6] != HI5622_BA_D2H_DEL_BA &&
	    hdr[6] != HI5622_BA_D2H_RX_DELBA_EVENT &&
	    hdr[6] != HI5622_BA_D2H_BA_ACTION)
		return;
	spin_lock_bh(&ba_glue.d2h_lock);
	if (ba_glue.d2h_pending) {
		ba_glue.d2h_dropped++;
		spin_unlock_bh(&ba_glue.d2h_lock);
		return;
	}
	n = wire_len;
	if (n < HI5622_BA_HDR_LEN || n > HI5622_BA_GLUE_D2H_MAX) {
		ba_glue.d2h_dropped++;
		spin_unlock_bh(&ba_glue.d2h_lock);
		return;
	}
	memcpy(ba_glue.d2h_buf, hdr, HI5622_BA_HDR_LEN);
	n = min(n - HI5622_BA_HDR_LEN, payload_avail);
	if (n > HI5622_BA_GLUE_D2H_MAX - HI5622_BA_HDR_LEN)
		n = HI5622_BA_GLUE_D2H_MAX - HI5622_BA_HDR_LEN;
	memcpy(ba_glue.d2h_buf + HI5622_BA_HDR_LEN, payload, n);
	ba_glue.d2h_len = HI5622_BA_HDR_LEN + n;
	ba_glue.d2h_pending = true;
	spin_unlock_bh(&ba_glue.d2h_lock);
	schedule_work(&ba_glue.d2h_work);
}

/* ------------------------------------------------------------------ */
/* deferred received action frames                                     */
/* ------------------------------------------------------------------ */

static void ba_glue_mgmt_workfn(struct work_struct *work)
{
	struct hi5622_ba_glue *g = container_of(work, struct hi5622_ba_glue,
						mgmt_work);
	struct hi5622_pci *ctx = g->ctx;
	u8 frame[HI5622_BA_GLUE_MGMT_MAX];
	u8 peer[6];
	u32 len;
	u16 uid = 0;
	u8 tid;
	int rc;

	spin_lock_bh(&g->mgmt_lock);
	if (!g->mgmt_pending) {
		spin_unlock_bh(&g->mgmt_lock);
		return;
	}
	memcpy(frame, g->mgmt_buf, g->mgmt_len);
	len = g->mgmt_len;
	g->mgmt_pending = false;
	spin_unlock_bh(&g->mgmt_lock);
	if (!ctx || len < 25)
		return;
	memcpy(peer, frame + 10, 6);	/* addr2 = the transmitter/peer */
	mutex_lock(&g->lock);
	if (!ba_glue_bind(ctx, peer, &uid)) {
		g->refused_no_sta++;
		g->last_rc = -ENOENT;
		mutex_unlock(&g->lock);
		return;
	}
	rc = hi5622_ba_mgmt_rx(&g->core, &ba_glue_ops, uid, peer, frame, len);
	/* DELBA parameter byte at frame[27]: tid<<4 | initiator<<3 */
	tid = (u8)((frame[27] >> 4) & 0x0fu);
	g->last_rc = rc;
	g->mgmt_events++;
	dev_dbg(&ctx->pdev->dev,
		"ba: mgmt action 3/%u from %pM rc %d\n", frame[25], peer, rc);
	if (rc == 0 && frame[25] == HI5622_BA_ACTION_DELBA)
		hi5622_ba_tx_burst_end(&g->core, &ba_glue_ops, peer, tid);
	mutex_unlock(&g->lock);
}

void hi5622_ba_glue_mgmt_rx(struct hi5622_pci *ctx, const u8 *frame,
			    size_t len)
{
	if (!ctx || !frame)
		return;
	if (!ba_glue_armed())
		return;
	if (len < 26 || len > HI5622_BA_GLUE_MGMT_MAX)
		return;
	if (frame[0] != 0xd0 || frame[1] != 0x00)
		return;			/* management action only */
	if (frame[24] != HI5622_BA_CATEGORY)
		return;			/* block-ack category 3 only */
	spin_lock_bh(&ba_glue.mgmt_lock);
	if (ba_glue.mgmt_pending) {
		ba_glue.mgmt_dropped++;
		spin_unlock_bh(&ba_glue.mgmt_lock);
		return;
	}
	memcpy(ba_glue.mgmt_buf, frame, len);
	ba_glue.mgmt_len = (u32)len;
	ba_glue.mgmt_pending = true;
	spin_unlock_bh(&ba_glue.mgmt_lock);
	schedule_work(&ba_glue.mgmt_work);
}

/* ------------------------------------------------------------------ */
/* deferred AP data-TX step (the in-kernel ADDBA originator)           */
/* ------------------------------------------------------------------ */

static void ba_glue_end_workfn(struct work_struct *work)
{
	struct hi5622_ba_glue *g = container_of(work, struct hi5622_ba_glue,
						end_work.work);
	struct hi5622_pci *ctx = g->ctx;
	u8 peer[6];
	u8 tid;
	int rc;

	spin_lock_bh(&g->tx_lock);
	if (!g->tx_open) {
		spin_unlock_bh(&g->tx_lock);
		return;
	}
	memcpy(peer, g->tx_peer, 6);
	tid = g->tx_tid;
	g->tx_open = false;
	spin_unlock_bh(&g->tx_lock);
	if (!ctx)
		return;
	mutex_lock(&g->lock);
	rc = hi5622_ba_tx_burst_end(&g->core, &ba_glue_ops, peer, tid);
	g->last_rc = rc;
	mutex_unlock(&g->lock);
}

static void ba_glue_tx_workfn(struct work_struct *work)
{
	struct hi5622_ba_glue *g = container_of(work, struct hi5622_ba_glue,
						tx_work);
	struct hi5622_pci *ctx = g->ctx;
	u8 peer[6];
	u16 seq;
	u8 tid;
	int rc;

	spin_lock_bh(&g->tx_lock);
	if (!g->tx_pending) {
		spin_unlock_bh(&g->tx_lock);
		return;
	}
	memcpy(peer, g->tx_peer, 6);
	seq = g->tx_seq;
	tid = g->tx_tid;
	g->tx_pending = false;
	spin_unlock_bh(&g->tx_lock);
	if (!ctx)
		return;
	mutex_lock(&g->lock);
	if (!ba_glue_bind(ctx, peer, NULL)) {
		g->refused_no_sta++;
		g->last_rc = -ENOENT;
		mutex_unlock(&g->lock);
		return;
	}
	rc = hi5622_ba_tx_data(&g->core, &ba_glue_ops, peer, tid, seq);
	g->last_rc = rc;
	g->tx_events++;
	if (rc == 0) {
		spin_lock_bh(&g->tx_lock);
		g->tx_open = true;
		spin_unlock_bh(&g->tx_lock);
		/* close the A-MPDU window when the burst goes quiet */
		mod_delayed_work(system_wq, &g->end_work,
				 msecs_to_jiffies(HI5622_BA_GLUE_BURST_HOLD_MS));
	}
	mutex_unlock(&g->lock);
}

void hi5622_ba_glue_tx_data(struct hi5622_pci *ctx, const u8 *peer, u8 tid,
			    u16 seq)
{
	if (!ctx || !peer)
		return;
	if (!ba_glue_armed())
		return;
	if (tid >= HI5622_BA_MAX_TID)
		return;
	spin_lock_bh(&ba_glue.tx_lock);
	memcpy(ba_glue.tx_peer, peer, 6);
	ba_glue.tx_tid = tid;
	ba_glue.tx_seq = seq;
	ba_glue.tx_count++;
	ba_glue.tx_pending = true;
	spin_unlock_bh(&ba_glue.tx_lock);
	schedule_work(&ba_glue.tx_work);
}

/* ------------------------------------------------------------------ */
/* lifecycle                                                           */
/* ------------------------------------------------------------------ */

int hi5622_ba_glue_init(struct hi5622_pci *ctx)
{
	if (!ctx)
		return -EINVAL;
	memset(&ba_glue.core, 0, sizeof(ba_glue.core));
	hi5622_ba_core_init(&ba_glue.core);
	ba_glue.ctx = ctx;
	ba_glue.ap_up = false;
	ba_glue.ap_channel = 0;
	ba_glue.ap_band = 0;
	memset(ba_glue.ap_mac, 0, sizeof(ba_glue.ap_mac));
	INIT_WORK(&ba_glue.d2h_work, ba_glue_d2h_workfn);
	INIT_WORK(&ba_glue.mgmt_work, ba_glue_mgmt_workfn);
	INIT_WORK(&ba_glue.tx_work, ba_glue_tx_workfn);
	INIT_DELAYED_WORK(&ba_glue.end_work, ba_glue_end_workfn);
	if (hi5622_ba_enable == 1u)
		dev_info(&ctx->pdev->dev,
			 "ba: TX-direction block-ack ARMED (ba_enable=1): in-kernel ADDBA originator (AP-as-TX), g0 0x0A relay/g0 0x0B A-MPDU/g0 0x1C notify; no peer -> no frame (fail-closed)\n");
	else if (hi5622_ba_enable > 1u)
		dev_warn(&ctx->pdev->dev,
			 "ba: ba_enable=%u is invalid (>1) - every BA entry point refuses (-EINVAL), no frame, no H2D\n",
			 hi5622_ba_enable);
	else
		dev_info(&ctx->pdev->dev,
			 "ba: TX-direction block-ack OFF (ba_enable=0 default): inert, no frame, no H2D message\n");
	return 0;
}

void hi5622_ba_glue_remove(struct hi5622_pci *ctx)
{
	if (!ctx)
		return;
	cancel_work_sync(&ba_glue.d2h_work);
	cancel_work_sync(&ba_glue.mgmt_work);
	cancel_work_sync(&ba_glue.tx_work);
	cancel_delayed_work_sync(&ba_glue.end_work);
	ba_glue.ctx = NULL;
}

void hi5622_ba_glue_ap_up(struct hi5622_pci *ctx, u8 band, u8 channel,
			  const u8 *mac)
{
	if (!ctx)
		return;
	mutex_lock(&ba_glue.lock);
	ba_glue.ap_up = true;
	ba_glue.ap_band = (u8)(band & 0x0fu);
	ba_glue.ap_channel = channel;
	if (mac)
		memcpy(ba_glue.ap_mac, mac, 6);
	hi5622_ba_core_set_addrs(&ba_glue.core, ba_glue.ap_mac,
				 ba_glue.ap_mac);
	mutex_unlock(&ba_glue.lock);
	dev_info_ratelimited(&ctx->pdev->dev,
			     "ba: AP up recorded (band %u channel %u mac %pM) - action frames can be sent once a peer negotiates\n",
			     band, channel, ba_glue.ap_mac);
}

void hi5622_ba_glue_ap_down(struct hi5622_pci *ctx)
{
	if (!ctx)
		return;
	cancel_delayed_work_sync(&ba_glue.end_work);
	mutex_lock(&ba_glue.lock);
	ba_glue.ap_up = false;
	ba_glue.ap_channel = 0;
	memset(&ba_glue.core, 0, sizeof(ba_glue.core));
	hi5622_ba_core_init(&ba_glue.core);
	mutex_unlock(&ba_glue.lock);
	dev_info_ratelimited(&ctx->pdev->dev,
			     "ba: AP down - BA sessions cleared (no frame sent)\n");
}

int hi5622_ba_glue_status_line(struct hi5622_pci *ctx, char *buf, size_t len)
{
	char core_line[320];
	int n, m;

	if (!buf || !len)
		return -EINVAL;
	mutex_lock(&ba_glue.lock);
	n = hi5622_ba_status_line(&ba_glue.core, hi5622_ba_enable,
				  core_line, sizeof(core_line));
	mutex_unlock(&ba_glue.lock);
	if (n < 0)
		return n;
	m = snprintf(buf, len,
		     "%s ba_glue: ap_up=%u band=%u channel=%u posts=%u refused=%u(gate=%u no_ap=%u no_sta=%u) d2h=%u(drop=%u) mgmt=%u(drop=%u) tx=%u last_rc=%d\n",
		     core_line, ba_glue.ap_up ? 1u : 0u,
		     (unsigned)ba_glue.ap_band, (unsigned)ba_glue.ap_channel,
		     ba_glue.posts, ba_glue.refused, ba_glue.refused_gate,
		     ba_glue.refused_no_ap, ba_glue.refused_no_sta,
		     ba_glue.d2h_events, ba_glue.d2h_dropped,
		     ba_glue.mgmt_events, ba_glue.mgmt_dropped,
		     ba_glue.tx_events, ba_glue.last_rc);
	if (m < 0)
		return m;
	return (int)((size_t)m < len ? (size_t)m : len - 1);
}
