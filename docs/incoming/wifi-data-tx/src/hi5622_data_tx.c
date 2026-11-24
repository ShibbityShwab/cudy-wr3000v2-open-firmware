/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_data_tx.c - kernel glue for the peer-aware host -> AP data TX path
 * (Hi5622V100 clean-room driver, Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (lane wifi-data-tx, shared task task-150,
 * 2026-11-29).  No vendor source/object/firmware byte is reproduced; the
 * wire offsets are the published decoded facts of the earlier lanes
 * (wifi-dp-txrx / wifi-dp-tuning / wifi-sta-ops), re-derived in
 * hi5622_data_tx_logic.c (host-tested by tools/data_tx_selftest.c).
 *
 * WHY: after wifi-sta-ops (task-149) the AP can start, hostapd reaches
 * AP-ENABLED, the WPA2 GTK is installed and the firmware per-STA context can
 * be posted - but hostapd's EAPOL-Key msg1/msg3 leave through the AP netdev
 * ndo_start_xmit, which was only an accounted drop.  This layer turns that
 * xmit into the proven dp TX sequence:
 *
 *   build 802.11 data MPDU (FromDS=1, LLC/SNAP)
 *     -> peer-aware decision (station table from sta-ops; EAPOL-only before
 *        the peer is authorized; Protected bit only for authorized peers)
 *     -> write the frame into a pre-allocated coherent pool buffer in the
 *        proven [owner][16 B zero][MPDU] layout
 *     -> hi5622_dp_tx_data_post(): 8-byte MSDU ring entry at the per-TID
 *        producer index, dma_wmb, packed-index publish into the device-RAM
 *        table entry, read-back, TXQ-table publish - the exact sequence the
 *        dp-tuning lane fired as TX COMPLETED
 *     -> bounded completion poll (delayed work) reading the device rptr;
 *        each completion frees the oldest skb, runs the vendor 4/4-like
 *        EAPOL-Key detector and wakes the netdev queue
 *     -> timeout: the lane closes for this module load (buffer parity is
 *        kept: the DMA buffers are never reused after a stall, the net
 *        stack's skbs are returned).
 *
 * WHAT IS DELIBERATELY NOT DONE: no key material is read or logged; no
 * encryption is performed in software (the 802.11 Protected bit is set and
 * the MAC/firmware per-user key table must do the encryption - whether it
 * does for host-originated data frames is the ladder's open question); the
 * "4/4-like" completion detector does NOT auto-authorize a station (in the
 * host-SME AP flow that shape also matches msg1, so doing so would be a
 * security bug - hostapd authorizes the peer after msg4 via .change_station).
 */
#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <net/cfg80211.h>

#include "hi5622_cfg80211.h"
#include "hi5622_data_tx.h"
#include "hi5622_data_tx_logic.h"
#include "hi5622_driver.h"
#include "hi5622_dp_txrx.h"
#include "hi5622_keys.h"
#include "hi5622_sta_ops.h"
#include "dp_rings.h"
#include "dp_txrx.h"

#define HDTX_OWNER		0x48584454u /* "HDTX" host bookkeeping word */
#define HDTX_POLL_MS		5u

/* Documentation-example STA address (locally administered); the probe frame
 * destination.  Never a per-unit address. */
static const u8 hdtx_doc_sta[ETH_ALEN] = { 0x02, 0x00, 0x68, 0x69, 0x35, 0x02 };

/* Module parameters - all fail-closed / tuning only. */
unsigned int hi5622_data_tx_enable;
module_param_named(data_tx, hi5622_data_tx_enable, uint, 0644);
MODULE_PARM_DESC(data_tx,
	"peer-aware host->AP data TX: 0 (default, fail closed: ndo_start_xmit keeps the accounted drop) / 1 (build the data MPDU and post through the proven dp TX ring)");

unsigned int hi5622_data_tx_peer = 1u;
module_param_named(data_tx_peer, hi5622_data_tx_peer, uint, 0644);
MODULE_PARM_DESC(data_tx_peer,
	"require a station-table entry for unicast data TX (default 1 = peer-aware; 0 = explicit bench diagnostic mode, unknown unicast peers allowed unprotected)");

unsigned int hi5622_data_tx_protect;
module_param_named(data_tx_protect, hi5622_data_tx_protect, uint, 0644);
MODULE_PARM_DESC(data_tx_protect,
	"802.11 Protected bit policy: 0 (default) = only data to authorized peers / group keys, 1 = force set, 2 = force clear");

unsigned int hi5622_data_tx_timeout_ms = 200u;
module_param_named(data_tx_timeout_ms, hi5622_data_tx_timeout_ms, uint, 0644);
MODULE_PARM_DESC(data_tx_timeout_ms,
	"bounded TX completion timeout in ms; on expiry the data-TX lane closes for this module load (no retry)");

struct hdtx_slot {
	bool		used;
	u8		protect;
	u8		eapol_msg;
	u8		ptk_done_like;
	u8		da[ETH_ALEN];
	u16		mpdu_len;
	u16		fc;
	u32		ring_index;
	u32		published_next;
	u32		rptr_at_post;
	void		*buf;		/* this slot's frame buffer */
	u32		devva;
	struct sk_buff	*skb;		/* NULL for the sysfs probe frame */
	unsigned long	posted;
};

struct hi5622_data_tx {
	struct hi5622_cfg80211	*c;
	struct device		*dev;
	spinlock_t		lock;
	struct delayed_work	poll;
	void			*pool;
	dma_addr_t		pool_dma;
	struct net_device	*netdev;	/* last netdev that posted */
	atomic_t		seq_ctr;
	struct hdtx_slot	slot[HDTX_POOL_DEPTH];
	u16			in_flight;
	u16			head;
	bool			rptr_valid;
	bool			stalled;
	bool			failed;
	u32			last_rptr;
	u32			last_wptr;
	int			last_err;
	/* counters */
	u32	posts;
	u32	completed;
	u32	dropped_off;
	u32	dropped_gate;
	u32	dropped_peer;
	u32	dropped_unauth;
	u32	dropped_big;
	u32	dropped_short;
	u32	dropped_nonlinear;
	u32	dropped_full;
	u32	timeouts;
	u32	stray_completions;
	u32	poll_errors;
	u32	probe_posts;
	u32	eapol_tx;
	u32	eapol_msg1;
	u32	eapol_msg3;
	u32	eapol_4_4_like;
	u32	protected_tx;
	u32	unprotected_tx;
	u32	poll_runs;
	/* last-frame evidence */
	u32	last_mpdu_len;
	u32	last_fc;
	u32	last_seq;
	u8	last_da[ETH_ALEN];
	u8	last_eapol_msg;
};

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */
static struct net_device *hdtx_ap_dev(struct hi5622_cfg80211 *c)
{
	unsigned int i;

	for (i = 0; i < HI5622_CFG_MAX_VAPS; i++) {
		struct net_device *dev = c->vap_dev[i];

		if (dev && dev->ieee80211_ptr &&
		    dev->ieee80211_ptr->iftype == NL80211_IFTYPE_AP)
			return dev;
	}
	return NULL;
}

/* Synthetic EAPOL-Key msg1-shaped 802.3 frame for the sysfs probe: a
 * structurally valid, all-zero-body frame to the documentation STA address.
 * It is NOT a handshake message (no real nonce/secret) - it exists to
 * exercise the same builder + MSDU ring + TXQ path before an association is
 * possible, with the on-air observer as witness. */
static size_t hdtx_build_probe_frame(u8 *frame, size_t cap, const u8 *bssid)
{
	u8 *eap;
	size_t len = HDTX_ETH_HDR_LEN + HDTX_EAPOL_HDR_LEN +
		     HDTX_EAPOL_KEY_MINLEN;

	if (cap < len)
		return 0;
	memset(frame, 0, len);
	ether_addr_copy(frame, hdtx_doc_sta);
	ether_addr_copy(frame + 6, bssid);
	frame[12] = 0x88;
	frame[13] = 0x8e;
	eap = frame + HDTX_ETH_HDR_LEN;
	eap[0] = 0x02;			/* EAPOL version (RSN) */
	eap[1] = HDTX_EAPOL_TYPE_KEY;	/* EAPOL-Key */
	eap[2] = 0x00;			/* EAPOL length 95 (0x005f) */
	eap[3] = (u8)HDTX_EAPOL_KEY_MINLEN;
	eap[4] = 0x02;			/* descriptor type RSN */
	eap[5] = 0x00;			/* key info 0x0088: pairwise + ACK */
	eap[6] = 0x88;
	eap[7] = 0x00;			/* key length 16 */
	eap[8] = 0x10;
	/* replay/nonce/IV/RSC/MIC and key-data-length stay zero (synthetic) */
	return len;
}

static void hdtx_free_skbs(struct sk_buff **skbs, unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++)
		dev_kfree_skb_any(skbs[i]);
}

/* ------------------------------------------------------------------ */
/* TX: build -> decide -> pool -> proven dp post                      */
/* ------------------------------------------------------------------ */
static int hdtx_send(struct hi5622_cfg80211 *c, struct hi5622_data_tx *d,
		     struct net_device *dev, const u8 *frame, u32 frame_len,
		     struct sk_buff *skb, int diagnostic)
{
	struct hdtx_eth eth;
	struct hdtx_eapol eapol;
	struct hdtx_tx_decision dec;
	struct hdtx_mpdu_params p;
	struct hdtx_slot *slot;
	const struct hsop_sta *csta = NULL;
	u8 mpdu[HDTX_MPDU_MAX];
	u8 bssid[ETH_ALEN];
	size_t mpdu_len = 0;
	u32 off = 0, published = 0, rptr = 0, seq;
	unsigned long flags;
	int rc, is_mcast, peer_found = 0, peer_authorized = 0;
	int is_eapol = 0, group_keys;

	rc = hdtx_eth_parse(frame, frame_len, &eth);
	if (rc) {
		d->dropped_short++;
		d->last_err = -EINVAL;
		return -EINVAL;
	}
	is_mcast = hdtx_is_multicast(eth.da);
	if (!is_mcast) {
		csta = hsop_sta_find_const(&c->sta_table, eth.da);
		peer_found = csta != NULL;
		peer_authorized = peer_found && csta->authorized != 0;
	}
	memset(&eapol, 0, sizeof(eapol));
	rc = hdtx_classify_eapol(&eth, &eapol);
	if (rc == HDTX_OK)
		is_eapol = eapol.is_eapol;
	/* A group key is installed when hostapd's WPA2 GTK path ran (wpa_keys
	 * enables that op); that is the only "group protected" hint available
	 * without a decoded key-state query. */
	group_keys = hi5622_wpa_keys != 0 ? 1 : 0;
	rc = hdtx_decide_tx(diagnostic ? 0u : hi5622_data_tx_peer, is_mcast,
			    peer_found, peer_authorized, is_eapol,
			    hi5622_data_tx_protect, group_keys, &dec);
	if (rc) {
		d->last_err = -EINVAL;
		return -EINVAL;
	}
	if (!dec.allowed) {
		if (dec.reason == HDTX_EPEER)
			d->dropped_peer++;
		else
			d->dropped_unauth++;
		d->last_err = (dec.reason == HDTX_EPEER) ? -ENOENT : -EPERM;
		return d->last_err;
	}

	ether_addr_copy(bssid, dev->dev_addr);
	seq = (u32)(atomic_inc_return(&d->seq_ctr) - 1) & HDTX_SEQ_MASK;
	memset(&p, 0, sizeof(p));
	p.bssid = bssid;
	p.da = eth.da;
	p.sa = eth.sa;
	p.payload = eth.payload;
	p.payload_len = eth.payload_len;
	p.seq = (u16)seq;
	p.qos = 0;		/* non-QoS data; TID 0 (EAPOL is best effort) */
	p.tid = 0;
	p.protected_frame = dec.protect ? 1u : 0u;
	rc = hdtx_build_data_mpdu(mpdu, sizeof(mpdu), &mpdu_len, &p);
	if (rc) {
		d->dropped_big++;
		d->last_err = -EMSGSIZE;
		return -EMSGSIZE;
	}

	spin_lock_irqsave(&d->lock, flags);
	if (d->failed || d->stalled) {
		rc = -EIO;
		goto out_unlock;
	}
	if (d->in_flight >= HDTX_POOL_DEPTH) {
		d->dropped_full++;
		rc = -ENOSPC;
		goto out_unlock;
	}
	slot = &d->slot[(d->head + d->in_flight) % HDTX_POOL_DEPTH];
	rc = hdtx_frame_write(slot->buf, HDTX_FRAME_AREA, HDTX_OWNER, mpdu,
			      (u32)mpdu_len, &off);
	if (rc) {
		rc = -EINVAL;
		goto out_unlock;
	}
	if (d->in_flight == 0) {
		/* refresh the completion baseline while the ring is idle */
		u32 w_idle = 0, r_idle = 0;

		if (hi5622_dp_tx_data_poll(c->ctx, 0u, &w_idle, &r_idle) == 0) {
			d->last_rptr = r_idle;
			d->last_wptr = w_idle;
			d->rptr_valid = true;
		}
	}
	rc = hi5622_dp_tx_data_post(c->ctx, slot->devva, (u16)mpdu_len,
				    DP_TXRX_VAP_TYPE_2, 0u, 0u,
				    &published, &rptr);
	if (rc) {
		d->dropped_gate++;
		d->last_err = rc;
		goto out_unlock;
	}
	slot->used = true;
	slot->protect = dec.protect ? 1u : 0u;
	slot->ring_index = published & 0x7fffu;
	slot->published_next = published;
	slot->rptr_at_post = rptr;
	slot->skb = skb;
	slot->posted = jiffies;
	slot->mpdu_len = (u16)mpdu_len;
	slot->fc = hdtx_mpdu_fc(0u, slot->protect);
	ether_addr_copy(slot->da, eth.da);
	slot->eapol_msg = is_eapol ? eapol.msg : HDTX_EAPOL_MSG_NONE;
	slot->ptk_done_like = (is_eapol && eapol.ptk_done_like) ? 1u : 0u;
	d->in_flight++;
	d->posts++;
	if (slot->protect)
		d->protected_tx++;
	else
		d->unprotected_tx++;
	ether_addr_copy(d->last_da, eth.da);
	d->last_mpdu_len = (u32)mpdu_len;
	d->last_fc = slot->fc;
	d->last_seq = seq;
	d->last_eapol_msg = slot->eapol_msg;
	d->netdev = dev;
	d->last_err = 0;
	rc = 0;
out_unlock:
	spin_unlock_irqrestore(&d->lock, flags);
	if (rc)
		return rc;
	queue_delayed_work(system_wq, &d->poll,
			   msecs_to_jiffies(HDTX_POLL_MS));
	return 0;
}

/* ------------------------------------------------------------------ */
/* TX completion: bounded poll, skb recycle, vendor 4/4-like detector */
/* ------------------------------------------------------------------ */
struct hdtx_done {
	u8	eapol_msg;
	u8	protect;
	u8	ptk_done_like;
	u8	da[ETH_ALEN];
	u16	mpdu_len;
};

static void hdtx_poll_work(struct work_struct *work)
{
	struct delayed_work *dw = to_delayed_work(work);
	struct hi5622_data_tx *d = container_of(dw, struct hi5622_data_tx,
						poll);
	struct hi5622_cfg80211 *c = d->c;
	struct sk_buff *freed[HDTX_POOL_DEPTH];
	struct hdtx_done done[HDTX_POOL_DEPTH];
	unsigned int nfreed = 0, ndone = 0, i;
	unsigned long flags;
	u32 wptr = 0, rptr = 0, adv = 0;
	bool rearm = false, stalled = false, poll_ok = false;
	int rc;

	d->poll_runs++;
	rc = hi5622_dp_tx_data_poll(c->ctx, 0u, &wptr, &rptr);
	poll_ok = (rc == 0);

	spin_lock_irqsave(&d->lock, flags);
	if (poll_ok) {
		if (!d->rptr_valid) {
			d->last_rptr = rptr;
			d->rptr_valid = true;
		} else if (hdtx_ring_advance(d->last_rptr, rptr,
					     DP_TXRX_TX_RING_DEPTH,
					     &adv) != HDTX_OK) {
			d->poll_errors++;
		} else {
			d->last_rptr = rptr;
			d->last_wptr = wptr;
			if (adv > d->in_flight) {
				d->stray_completions += adv - d->in_flight;
				adv = d->in_flight;
			}
			while (adv > 0u && d->in_flight > 0u) {
				struct hdtx_slot *slot = &d->slot[d->head];

				if (!slot->used)
					break; /* bookkeeping mismatch: stop */
				slot->used = false;
				if (slot->skb && nfreed < HDTX_POOL_DEPTH)
					freed[nfreed++] = slot->skb;
				slot->skb = NULL;
				d->completed++;
				if (slot->eapol_msg != HDTX_EAPOL_MSG_NONE) {
					d->eapol_tx++;
					if (slot->eapol_msg ==
					    HDTX_EAPOL_MSG_1)
						d->eapol_msg1++;
					else if (slot->eapol_msg ==
						 HDTX_EAPOL_MSG_3)
						d->eapol_msg3++;
					if (slot->ptk_done_like)
						d->eapol_4_4_like++;
					if (ndone < HDTX_POOL_DEPTH) {
						done[ndone].eapol_msg =
							slot->eapol_msg;
						done[ndone].protect =
							slot->protect;
						done[ndone].ptk_done_like =
							slot->ptk_done_like;
						done[ndone].mpdu_len =
							slot->mpdu_len;
						ether_addr_copy(
							done[ndone].da,
							slot->da);
						ndone++;
					}
				}
				d->head = (u16)((d->head + 1u) %
						HDTX_POOL_DEPTH);
				d->in_flight--;
				adv--;
			}
		}
	}
	if (d->in_flight > 0u && !d->stalled) {
		struct hdtx_slot *oldest = &d->slot[d->head];
		unsigned long limit =
			msecs_to_jiffies(hi5622_data_tx_timeout_ms);

		if (time_after(jiffies, oldest->posted + limit)) {
			/* bounded completion timeout: return the net stack's
			 * skbs, keep the DMA buffers (parity), close the lane */
			d->stalled = true;
			d->timeouts++;
			for (i = 0; i < d->in_flight; i++) {
				struct hdtx_slot *s =
					&d->slot[(d->head + i) %
						 HDTX_POOL_DEPTH];

				if (s->skb && nfreed < HDTX_POOL_DEPTH)
					freed[nfreed++] = s->skb;
				s->skb = NULL;
			}
			d->in_flight = 0;
			d->head = 0;
			stalled = true;
		}
	}
	if (d->stalled)
		rearm = false;
	else if (d->in_flight > 0u)
		rearm = true;
	spin_unlock_irqrestore(&d->lock, flags);

	hdtx_free_skbs(freed, nfreed);
	for (i = 0; i < ndone; i++) {
		const char *what = done[i].eapol_msg == HDTX_EAPOL_MSG_1 ?
			"msg1/4" :
			done[i].eapol_msg == HDTX_EAPOL_MSG_3 ? "msg3/4" :
			"other EAPOL-Key";

		dev_info(d->dev,
			 "data_tx: EAPOL-Key %s TX COMPLETED (peer %pM, %u B MPDU, protected=%u, key-data-len-zero=%u) - vendor hmac_is_eapol_key_ptk_4_4 shape=%u (no key material logged)\n",
			 what, done[i].da, done[i].mpdu_len,
			 done[i].protect, done[i].ptk_done_like,
			 done[i].ptk_done_like);
	}
	if (stalled)
		dev_err(d->dev,
			"data_tx: bounded completion timeout (%u ms): device rptr did not consume the posted frame - lane CLOSED for this module load (no retry; a power-cycle/module reload is the reset; the DMA buffers stay owned by the device)\n",
			hi5622_data_tx_timeout_ms);
	if (rearm)
		queue_delayed_work(system_wq, &d->poll,
				   msecs_to_jiffies(HDTX_POLL_MS));
}

/* ------------------------------------------------------------------ */
/* ndo_start_xmit                                                     */
/* ------------------------------------------------------------------ */
netdev_tx_t hi5622_data_tx_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct wireless_dev *wdev;
	struct hi5622_cfg80211 *c;
	struct hi5622_data_tx *d;

	if (!skb || !dev)
		return NETDEV_TX_OK;
	wdev = dev->ieee80211_ptr;
	if (!wdev || !wdev->wiphy)
		goto drop;
	c = wiphy_priv(wdev->wiphy);
	if (!c)
		goto drop;
	d = c->data_tx;
	if (!d) {
		hi5622_sta_ops_xmit_drop(dev);
		goto drop;
	}
	if (wdev->iftype != NL80211_IFTYPE_AP) {
		d->dropped_off++;
		goto drop;
	}
	if (!netif_running(dev)) {
		d->dropped_off++;
		goto drop;
	}
	if (skb->len < HDTX_ETH_HDR_LEN) {
		d->dropped_short++;
		goto drop;
	}
	if (skb_headlen(skb) < skb->len) {
		/* the builder needs a linear 802.3 frame and xmit is atomic */
		d->dropped_nonlinear++;
		goto drop;
	}
	if (hdtx_send(c, d, dev, skb->data, (u32)skb->len, skb, 0) != 0)
		goto drop;
	return NETDEV_TX_OK;
drop:
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

/* ------------------------------------------------------------------ */
/* lifecycle                                                          */
/* ------------------------------------------------------------------ */
int hi5622_data_tx_init(struct hi5622_cfg80211 *c)
{
	struct hi5622_data_tx *d;
	unsigned int i;
	int rc;

	if (!c || !c->ctx)
		return -EINVAL;
	if (!hi5622_data_tx_enable) {
		dev_info(&c->ctx->pdev->dev,
			 "data_tx: lane disabled (data_tx=0, fail-closed default) - ndo_start_xmit keeps the accounted drop; set data_tx=1 at insmod to enable the peer-aware data TX path\n");
		return 0;
	}
	BUILD_BUG_ON(HDTX_FRAME_AREA != DP_TXRX_FRAME_AREA);
	BUILD_BUG_ON(HDTX_MPDU_MAX != DP_TXRX_MAX_MPDU);
	BUILD_BUG_ON(HDTX_FRAME_HOST_HDR != DP_TXRX_FRAME_HOST_HDR);
	BUILD_BUG_ON(HDTX_FRAME_DMA_HDR != DP_TXRX_FRAME_DMA_HDR);
	BUILD_BUG_ON(DP_TXRX_VAP_TYPE_2 != 2);
	BUILD_BUG_ON(HDTX_POOL_DEPTH > DP_TXRX_TX_RING_DEPTH);

	d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;
	d->c = c;
	/* hi5622_data_tx_init runs from hi5622_cfg80211_init BEFORE
	 * set_wiphy_dev(), so root the device at the PCI endpoint directly. */
	d->dev = &c->ctx->pdev->dev;
	spin_lock_init(&d->lock);
	INIT_DELAYED_WORK(&d->poll, hdtx_poll_work);
	atomic_set(&d->seq_ctr, 0);
	d->pool = dma_alloc_coherent(d->dev, HDTX_POOL_BYTES, &d->pool_dma,
				     GFP_KERNEL);
	if (!d->pool) {
		dev_err(d->dev,
			"data_tx: dma_alloc_coherent(%u B) failed - lane disabled (fail closed)\n",
			HDTX_POOL_BYTES);
		kfree(d);
		return 0;
	}
	for (i = 0; i < HDTX_POOL_DEPTH; i++) {
		u64 dma = (u64)d->pool_dma + (u64)i * HDTX_FRAME_AREA;
		u32 devva = 0;

		rc = hdtx_buffer_devva(dma, DP_HOST_DMA_LO, DP_HOST_DMA_HI,
				       &devva);
		if (rc) {
			dev_err(d->dev,
				"data_tx: pool buffer %u DMA 0x%llx outside the device window 0x%08x..0x%08x - lane disabled\n",
				i, (unsigned long long)dma, DP_HOST_DMA_LO,
				DP_HOST_DMA_HI);
			dma_free_coherent(d->dev, HDTX_POOL_BYTES, d->pool,
					  d->pool_dma);
			kfree(d);
			return 0;
		}
		d->slot[i].buf = (u8 *)d->pool + (size_t)i * HDTX_FRAME_AREA;
		d->slot[i].devva = devva;
	}
	c->data_tx = d;
	dev_info(d->dev,
		 "data_tx: lane ENABLED (data_tx=1) - %u in-flight frames x %u B coherent at devva 0x%08x, peer_required=%u protect=%u timeout_ms=%u; AP ndo_start_xmit now builds the 802.11 data MPDU (FromDS=1, LLC/SNAP) and posts it through the proven MSDU ring/TXQ path\n",
		 HDTX_POOL_DEPTH, HDTX_FRAME_AREA, d->slot[0].devva,
		 hi5622_data_tx_peer, hi5622_data_tx_protect,
		 hi5622_data_tx_timeout_ms);
	return 0;
}

void hi5622_data_tx_remove(struct hi5622_cfg80211 *c)
{
	struct hi5622_data_tx *d;
	struct sk_buff *freed[HDTX_POOL_DEPTH];
	unsigned int nfreed = 0, i;
	unsigned long flags;

	if (!c)
		return;
	d = c->data_tx;
	if (!d)
		return;
	c->data_tx = NULL;
	cancel_delayed_work_sync(&d->poll);
	spin_lock_irqsave(&d->lock, flags);
	for (i = 0; i < HDTX_POOL_DEPTH; i++) {
		if (d->slot[i].skb && nfreed < HDTX_POOL_DEPTH)
			freed[nfreed++] = d->slot[i].skb;
		d->slot[i].skb = NULL;
		d->slot[i].used = false;
	}
	spin_unlock_irqrestore(&d->lock, flags);
	hdtx_free_skbs(freed, nfreed);
	if (d->pool)
		dma_free_coherent(d->dev, HDTX_POOL_BYTES, d->pool, d->pool_dma);
	kfree(d);
}

/* ------------------------------------------------------------------ */
/* sysfs: status + probe                                              */
/* ------------------------------------------------------------------ */
int hi5622_data_tx_status(struct hi5622_cfg80211 *c, char *buf, size_t len)
{
	struct hi5622_data_tx *d;
	unsigned long flags;
	int n = 0;

	if (!c || !buf)
		return -EINVAL;
	d = c->data_tx;
	if (!d)
		return scnprintf(buf, len,
				 "data-tx: data_tx=%u disabled (no pool; fail-closed default)\n",
				 hi5622_data_tx_enable);
	if (len == 0)
		return 0;
	spin_lock_irqsave(&d->lock, flags);
	if (n < (int)len)
		n += scnprintf(buf + n, len - (size_t)n,
			       "data-tx: data_tx=%u peer_required=%u protect=%u timeout_ms=%u stalled=%u failed=%u pool=%u in_flight=%u free=%u\n",
			       hi5622_data_tx_enable, hi5622_data_tx_peer,
			       hi5622_data_tx_protect,
			       hi5622_data_tx_timeout_ms,
			       d->stalled ? 1u : 0u, d->failed ? 1u : 0u,
			       HDTX_POOL_DEPTH, d->in_flight,
			       (unsigned int)(HDTX_POOL_DEPTH - d->in_flight));
	if (n < (int)len)
		n += scnprintf(buf + n, len - (size_t)n,
			       "data-tx tx: posts=%u completed=%u protected=%u unprotected=%u dropped(off=%u gate=%u peer=%u unauth=%u big=%u short=%u nonlin=%u full=%u) timeouts=%u stray=%u poll_errors=%u probes=%u\n",
			       d->posts, d->completed, d->protected_tx,
			       d->unprotected_tx, d->dropped_off,
			       d->dropped_gate, d->dropped_peer,
			       d->dropped_unauth, d->dropped_big,
			       d->dropped_short, d->dropped_nonlinear,
			       d->dropped_full, d->timeouts,
			       d->stray_completions, d->poll_errors,
			       d->probe_posts);
	if (n < (int)len)
		n += scnprintf(buf + n, len - (size_t)n,
			       "data-tx eapol: tx=%u msg1=%u msg3=%u 4_4_like=%u (vendor hmac_is_eapol_key_ptk_4_4 shape on TX completion; not used to authorize - hostapd .change_station owns AUTHORIZED)\n",
			       d->eapol_tx, d->eapol_msg1, d->eapol_msg3,
			       d->eapol_4_4_like);
	if (n < (int)len)
		n += scnprintf(buf + n, len - (size_t)n,
			       "data-tx last: mpdu=%u B fc=0x%04x seq=%u da=%pM eapol_msg=%u rptr=0x%04x wptr=0x%04x last_err=%d\n",
			       d->last_mpdu_len, d->last_fc, d->last_seq,
			       d->last_da, d->last_eapol_msg,
			       d->last_rptr, d->last_wptr, d->last_err);
	spin_unlock_irqrestore(&d->lock, flags);
	return n;
}

ssize_t hi5622_data_tx_probe_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct hi5622_pci *ctx = dev_get_drvdata(dev);
	struct hi5622_cfg80211 *c;
	struct hi5622_data_tx *d;
	struct net_device *vap;
	u8 frame[HDTX_ETH_HDR_LEN + HDTX_EAPOL_HDR_LEN +
		 HDTX_EAPOL_KEY_MINLEN];
	size_t frame_len;
	int rc;

	(void)attr;
	if (!ctx || !ctx->cfg)
		return -ENODEV;
	if (sysfs_streq(buf, "1") == 0)
		return -EINVAL;
	c = ctx->cfg;
	d = c->data_tx;
	if (!d)
		return -EOPNOTSUPP;
	vap = hdtx_ap_dev(c);
	if (!vap) {
		dev_warn(dev,
			 "data_tx_probe: no AP VAP netdev yet - start the AP first (hostapd NEW_BEACON/start_ap creates it); no frame posted\n");
		return -ENODEV;
	}
	frame_len = hdtx_build_probe_frame(frame, sizeof(frame), vap->dev_addr);
	if (frame_len == 0)
		return -ENOMEM;
	rc = hdtx_send(c, d, vap, frame, (u32)frame_len, NULL, 1);
	if (rc) {
		dev_warn(dev,
			 "data_tx_probe: probe frame refused (rc %d) at the dp TX gate/peer decision - no device write or no completion; check data_tx_status\n",
			 rc);
		return rc;
	}
	d->probe_posts++;
	dev_info(dev,
		 "data_tx_probe: synthetic EAPOL-Key msg1 shape (%zu B 802.3 -> data MPDU) posted through the same builder + MSDU ring + TXQ publish as ndo_start_xmit, dst 02:00:68:69:35:02; no key material (all-zero synthetic body). Observe data_tx_status (posts/completed), dp_tx_complete_poll and the on-air observer.\n",
		 frame_len);
	return (ssize_t)count;
}
