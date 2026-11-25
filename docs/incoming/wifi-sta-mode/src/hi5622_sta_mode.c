// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_sta_mode.c - kernel-side STA (client) kernel-SME glue for the
 * Hi5622V100 clean-room driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (lane wifi-sta-mode, shared task task-155,
 * 2026-11-25).  The wire layouts and the state machine live in the portable
 * hi5622_sta_sme.c; this file only:
 *   - holds the per-connect context and the module parameters,
 *   - posts auth/assoc requests on the corrected device-facing management-TX
 *     carrier (raw HCC group 0 id 6, frame inline; sibling-gpl task-151),
 *   - consumes the CRX (D2H g1 id13) auth/assoc responses,
 *   - reports cfg80211_connect_done()/cfg80211_disconnected().
 *
 * WHY THE COMPLETION IS ON THE CRX PATH (not the data ring, not a firmware
 * completion record): the pinned vendor module is a split-MAC host SME - the
 * STA FSM table {scan_rsp, join_rsp, auth_rsp, asoc_rsp} at .rodata+0x8074 is
 * reached from hmac_rx_process_mgmt_event (D2H chip group 1 id 13), and
 * hmac_handle_asoc_rsp_sta parses the received association response, syncs
 * the association state to the device (H2D g2 id3) and reports the connect
 * result to cfg80211.  So the association response management frame is the
 * completion event; this file routes it into the kernel SME and calls
 * cfg80211_connect_done() with the locally-held req_ie/resp_ie - it never
 * dereferences a firmware-supplied IE address (the sta-join lane's log-only
 * g2 id3 decoder stays as a diagnostic for the full-MAC alternative).
 *
 * SAFETY: every post goes through hi5622_frame_post() (allow_frames budget,
 * inter-frame gap, armed/released/ACTIVE lane gates, structural + firmware
 * handler validation) and only when sta_connect=2; a failed post is final
 * (no retry).  No device register is written here.
 */
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <net/cfg80211.h>

#include "hi5622_cfg80211.h"
#include "hi5622_contract.h"
#include "hi5622_driver.h"
#include "hi5622_frame_post.h"
#include "hi5622_frames_api.h"
#include "hi5622_mgmt_logic.h"
#include "hi5622_sta_mode.h"
#include "hi5622_sta_sme.h"

/* STA connect mode: 0 = fail closed (default), 1 = build/validate only,
 * 2 = live host-SME handshake.  Read-only: the mode cannot be flipped on a
 * boot that already fired (one connect per load, no retry). */
unsigned int hi5622_sta_connect_mode;
module_param_named(sta_connect, hi5622_sta_connect_mode, uint, 0444);
MODULE_PARM_DESC(sta_connect,
		 "STA client connect flow: 0=fail-closed default (join-only path), 1=dry run (build+validate join/auth/assoc, post nothing), 2=live (join post + host-SME auth/assoc over the HCC g0 id6 mgmt carrier + CRX completion; needs sta_join=2 and allow_frames budget)");

#define HI5622_STA_MODE_TIMEOUT_MIN_MS	500u
#define HI5622_STA_MODE_TIMEOUT_MAX_MS	60000u
static unsigned int hi5622_sta_connect_timeout_ms = 4000u;
module_param_named(sta_connect_timeout_ms, hi5622_sta_connect_timeout_ms,
		   uint, 0644);
MODULE_PARM_DESC(sta_connect_timeout_ms,
		 "bounded auth/assoc response timeout for the STA host-SME connect (500..60000 ms, default 4000)");

#define HI5622_STA_MODE_DRY	1u
#define HI5622_STA_MODE_LIVE	2u

/* The join gate lives in hi5622_sta.c (wifi-sta-join lane); LIVE requires the
 * same mode there so one switch cannot silently post an un-gated join. */
extern unsigned int hi5622_sta_join;

struct hi5622_sta_mode_state {
	struct mutex		lock;
	struct delayed_work	timer;
	struct net_device	*dev;		/* connect owner while in flight */
	u8			own_mac[6];
	u8			bssid[6];
	u8			ssid[HISM_MAX_SSID];
	u8			ssid_len;
	u8			req_ie[HISM_MAX_IE];
	u16			req_ie_len;
	u8			resp_ie[HISM_MAX_IE];
	u16			resp_ie_len;
	u8			privacy;
	u8			channel;
	u32			seq;		/* mgmt-frame cookie counter */
	u32			state;		/* HISM_* */
	bool			armed;
	u32			auth_posts;
	u32			assoc_posts;
	u32			auth_rx;
	u32			assoc_rx;
	u32			completed_ok;
	u32			completed_fail;
	u32			timeouts;
	u32			refused;
	u32			rx_dropped;
	u32			rx_bad;
	u32			mlme_errors;
};

static struct hi5622_sta_mode_state sm = {
	.lock = __MUTEX_INITIALIZER(sm.lock),
	.state = HISM_OFF,
};

static void hi5622_sta_mode_timer_work(struct work_struct *work);

/* ---------------------------------------------------------------------
 * Management-carrier TX (corrected g0 id6 carrier, sibling-gpl task-151).
 * Caller must hold sm.lock (sequence/cookie/channel come from the connect
 * context).  Returns 0 only after hi5622_frame_post() accepted the frame and
 * the coalesced announce was consumed.
 * -------------------------------------------------------------------*/
static int hi5622_sta_mode_post_mgmt(struct hi5622_cfg80211 *c,
				     const u8 *frame, size_t frame_len,
				     const char *what)
{
	const struct hi5622_frame_ops *ops;
	struct hi5622_mgmt_tx_params p;
	u8 wire[HIMG_MGMT_TX_MAX_TOTAL];
	size_t wire_len = 0;
	int rc;

	if (!c || !c->ctx || !frame)
		return -EINVAL;
	if (frame_len < HISM_MGMT_HDR_LEN || frame_len > HIMG_MAX_FRAME_TX)
		return -EINVAL;
	if (himg_mgmt_frame_ok(frame, frame_len))
		return -EINVAL;
	ops = hi5622_frame_ops_get();
	if (!ops || !(ops->caps & HI5622_FRAME_CAP_MGMT_TX) ||
	    !ops->build_mgmt_tx)
		return -EOPNOTSUPP;
	memset(&p, 0, sizeof(p));
	p.channel = sm.channel;
	p.len = (u16)frame_len;
	p.seq = ++sm.seq;
	p.dst = 0u;			/* single STA VAP -> dst nibble 0 [V] */
	p.frame = frame;
	p.frame_len = (u16)frame_len;
	p.cookie = (u16)(p.seq & 0x0fu);
	p.user_id = 0u;			/* no station user -> 0x0fff    */
	p.vap_id = 0u;
	p.band = (sm.channel > 14u) ? 1u : 0u;
	p.hdr2 = 0u;
	rc = ops->build_mgmt_tx(wire, sizeof(wire), &wire_len, &p);
	if (rc)
		return rc;
	return hi5622_frame_post(c->ctx, wire, wire_len, what, NULL);
}

static int hi5622_sta_mode_send_auth_locked(struct hi5622_cfg80211 *c)
{
	struct hism_auth_req ar;
	u8 frame[HISM_AUTH_FRAME_LEN];
	size_t frame_len = 0;
	int rc;

	memset(&ar, 0, sizeof(ar));
	ar.own = sm.own_mac;
	ar.bssid = sm.bssid;
	ar.seq = (u16)(sm.seq & 0x0fu);
	rc = hism_build_auth_req(frame, sizeof(frame), &frame_len, &ar);
	if (rc) {
		sm.mlme_errors++;
		return -EINVAL;
	}
	rc = hi5622_sta_mode_post_mgmt(c, frame, frame_len, "sta_auth_req");
	if (rc) {
		sm.mlme_errors++;
		return rc;
	}
	sm.auth_posts++;
	sm.state = HISM_AUTH_SENT;
	return 0;
}

static int hi5622_sta_mode_send_assoc_locked(struct hi5622_cfg80211 *c)
{
	struct hism_assoc_req ar;
	const u8 *assoc_ie = NULL;
	size_t assoc_ie_len = 0;
	u8 frame[HISM_MAX_FRAME];
	size_t frame_len = 0;
	size_t copied = 0;
	int rc;

	memset(&ar, 0, sizeof(ar));
	ar.own = sm.own_mac;
	ar.bssid = sm.bssid;
	ar.ssid = sm.ssid;
	ar.ssid_len = sm.ssid_len;
	ar.privacy = sm.privacy ? 1 : 0;
	ar.seq = (u16)(sm.seq & 0x0fu);
	rc = hism_build_assoc_req(frame, sizeof(frame), &frame_len,
				  &assoc_ie, &assoc_ie_len, &ar);
	if (rc) {
		sm.mlme_errors++;
		return -EINVAL;
	}
	rc = hi5622_sta_mode_post_mgmt(c, frame, frame_len, "sta_assoc_req");
	if (rc) {
		sm.mlme_errors++;
		return rc;
	}
	sm.assoc_posts++;
	/* remember the association-request IE list for cfg80211_connect_done */
	if (assoc_ie_len && assoc_ie) {
		rc = hism_ie_copy(sm.req_ie, sizeof(sm.req_ie), assoc_ie,
				  assoc_ie_len, &copied);
		if (rc) {
			sm.mlme_errors++;
			return -E2BIG;
		}
		sm.req_ie_len = (u16)copied;
	}
	sm.state = HISM_ASSOC_SENT;
	return 0;
}

/* Finish the in-flight connect under sm.lock.  status 0 = success (802.11
 * status); positive 802.11 status = AP rejection; negative = local failure.
 * ies/ies_len = association response IEs (success only).  cfg80211 callbacks
 * run while the lock is held so remove/disconnect cannot clear sm.dev in
 * between (the service thread and the timer take the same lock). */
static void hi5622_sta_mode_finish_locked(int status, const u8 *ies,
					  size_t ies_len, u16 timeout_reason)
{
	struct cfg80211_connect_resp_params cp;
	struct net_device *dev = sm.dev;

	if (!dev) {
		sm.state = HISM_OFF;
		return;
	}
	if (sm.armed) {
		cancel_delayed_work(&sm.timer);
		sm.armed = false;
	}
	if (ies && ies_len) {
		size_t copied = 0;

		if (!hism_ie_copy(sm.resp_ie, sizeof(sm.resp_ie), ies,
				  ies_len, &copied))
			sm.resp_ie_len = (u16)copied;
		else
			sm.resp_ie_len = 0;
	} else {
		sm.resp_ie_len = 0;
	}
	memset(&cp, 0, sizeof(cp));
	cp.status = status;
	cp.timeout_reason = timeout_reason;
	cp.req_ie = sm.req_ie_len ? sm.req_ie : NULL;
	cp.req_ie_len = sm.req_ie_len;
	cp.resp_ie = sm.resp_ie_len ? sm.resp_ie : NULL;
	cp.resp_ie_len = sm.resp_ie_len;
	cp.links[0].bssid = sm.bssid;
	if (status == 0)
		sm.completed_ok++;
	else
		sm.completed_fail++;
	cfg80211_connect_done(dev, &cp, GFP_KERNEL);
}

static void hi5622_sta_mode_timer_work(struct work_struct *work)
{
	u16 reason;

	mutex_lock(&sm.lock);
	sm.armed = false;
	if (!sm.dev || !hism_state_pending(sm.state)) {
		mutex_unlock(&sm.lock);
		return;
	}
	sm.timeouts++;
	sm.state = HISM_FAILED;
	reason = hism_timeout_reason(sm.state);
	if (sm.dev)
		dev_warn(&sm.dev->dev,
			 "sta mode: connect timeout (state failed, timeout_reason=%u) - failing the connect, one shot no retry\n",
			 reason);
	hi5622_sta_mode_finish_locked(-1, NULL, 0, reason);
	mutex_unlock(&sm.lock);
}

/* ---------------------------------------------------------------------
 * Pre-.connect validation (module-parameter contract)
 * -------------------------------------------------------------------*/
int hi5622_sta_mode_precheck(struct net_device *dev,
			     struct cfg80211_connect_params *sme)
{
	if (hi5622_sta_connect_mode == 0u)
		return 0;		/* legacy join-only path (sta_join) */
	if (hi5622_sta_connect_mode > HI5622_STA_MODE_LIVE)
		return -EINVAL;
	if (hi5622_sta_connect_mode != hi5622_sta_join) {
		pr_warn("hi5622: sta_connect=%u needs the same sta_join=%u (the join post is the first frame of the flow) - fail closed\n",
			hi5622_sta_connect_mode, hi5622_sta_join);
		return -EINVAL;
	}
	if (!dev || !sme)
		return -EINVAL;
	if (hi5622_sta_connect_mode == HI5622_STA_MODE_LIVE) {
		if (!dev->dev_addr || !hism_mac_ok(dev->dev_addr)) {
			pr_warn("hi5622: sta mode: invalid own MAC - fail closed\n");
			return -EINVAL;
		}
		if (!sme->channel) {
			pr_warn("hi5622: sta mode: connect request has no BSS channel (needed for the device TX control block) - fail closed\n");
			return -EINVAL;
		}
		if (hi5622_sta_connect_timeout_ms <
				HI5622_STA_MODE_TIMEOUT_MIN_MS ||
		    hi5622_sta_connect_timeout_ms >
				HI5622_STA_MODE_TIMEOUT_MAX_MS) {
			pr_warn("hi5622: sta mode: sta_connect_timeout_ms=%u out of range %u..%u - fail closed\n",
				hi5622_sta_connect_timeout_ms,
				HI5622_STA_MODE_TIMEOUT_MIN_MS,
				HI5622_STA_MODE_TIMEOUT_MAX_MS);
			return -EINVAL;
		}
		/* join + auth + assoc = three provider frames of the budget */
		if (hi5622_allow_frames < 3u) {
			pr_warn("hi5622: sta mode LIVE needs allow_frames>=3 (join+auth+assoc), have %u - fail closed\n",
				hi5622_allow_frames);
			return -EPERM;
		}
	}
	return 0;
}

/* ---------------------------------------------------------------------
 * Begin the host-SME handshake after the WAL join was accepted.
 * -------------------------------------------------------------------*/
int hi5622_sta_mode_begin(struct hi5622_cfg80211 *c, struct net_device *dev,
			  struct cfg80211_connect_params *sme)
{
	struct device *d;
	size_t copied = 0;
	int rc;

	if (hi5622_sta_connect_mode == 0u)
		return 0;
	if (!c || !c->ctx || !dev || !sme)
		return -EINVAL;
	d = &c->ctx->pdev->dev;
	if (hi5622_sta_connect_mode != HI5622_STA_MODE_LIVE) {
		dev_info(d,
			 "sta mode DRY: join body built+validated; host-SME auth/assoc frames are dry-built too, nothing posted\n");
		return 0;
	}
	if (!sme->bssid || !sme->ssid || sme->ssid_len == 0 ||
	    sme->ssid_len > HISM_MAX_SSID)
		return -EINVAL;

	mutex_lock(&sm.lock);
	if (sm.state != HISM_OFF && sm.state != HISM_FAILED) {
		sm.refused++;
		rc = -EALREADY;
		goto out;
	}
	memset(&sm.own_mac, 0, sizeof(sm.own_mac));
	memcpy(sm.own_mac, dev->dev_addr, ETH_ALEN);
	memcpy(sm.bssid, sme->bssid, ETH_ALEN);
	sm.ssid_len = (u8)sme->ssid_len;
	memcpy(sm.ssid, sme->ssid, sme->ssid_len);
	sm.privacy = sme->privacy ? 1u : 0u;
	sm.channel = (u8)sme->channel->hw_value;
	sm.req_ie_len = 0;
	if (sme->ie_len) {
		rc = hism_ie_copy(sm.req_ie, sizeof(sm.req_ie), sme->ie,
				  sme->ie_len, &copied);
		if (rc) {
			sm.refused++;
			goto out;
		}
		sm.req_ie_len = (u16)copied;
	}
	sm.resp_ie_len = 0;
	sm.dev = dev;
	sm.state = HISM_JOIN_POSTED;
	sm.seq = 0;
	INIT_DELAYED_WORK(&sm.timer, hi5622_sta_mode_timer_work);
	rc = hi5622_sta_mode_send_auth_locked(c);
	if (rc) {
		sm.state = HISM_FAILED;
		sm.dev = NULL;
		goto out;
	}
	schedule_delayed_work(&sm.timer,
			      msecs_to_jiffies(hi5622_sta_connect_timeout_ms));
	sm.armed = true;
	dev_info(d,
		 "sta mode: host-SME connect started (bssid=%pM ch=%u auth=%zu B posted on g0 id6; waiting for the CRX auth response, timeout %u ms)\n",
		 sm.bssid, sm.channel, (size_t)HISM_AUTH_FRAME_LEN,
		 hi5622_sta_connect_timeout_ms);
out:
	mutex_unlock(&sm.lock);
	return rc;
}

/* ---------------------------------------------------------------------
 * DRY run (sta_connect=1): build+validate the whole handshake locally.
 * Nothing is posted and no state changes; returns 0 when every frame is
 * valid, negative errno on the first refusal.
 * -------------------------------------------------------------------*/
int hi5622_sta_mode_dry(struct hi5622_cfg80211 *c, struct net_device *dev,
			struct cfg80211_connect_params *sme)
{
	struct hism_auth_req ar;
	struct hism_assoc_req as;
	const u8 *assoc_ie = NULL;
	size_t assoc_ie_len = 0;
	u8 frame[HISM_MAX_FRAME];
	size_t frame_len = 0;
	int rc;

	if (hi5622_sta_connect_mode != HI5622_STA_MODE_DRY)
		return 0;		/* caller keeps the join-only behaviour */
	if (!c || !c->ctx || !dev || !sme)
		return -EINVAL;
	memset(&ar, 0, sizeof(ar));
	ar.own = dev->dev_addr;
	ar.bssid = sme->bssid;
	ar.seq = 0u;
	rc = hism_build_auth_req(frame, sizeof(frame), &frame_len, &ar);
	if (rc) {
		dev_warn(&c->ctx->pdev->dev,
			 "sta mode DRY: auth request refused (%d)\n", rc);
		return -EINVAL;
	}
	memset(&as, 0, sizeof(as));
	as.own = dev->dev_addr;
	as.bssid = sme->bssid;
	as.ssid = sme->ssid;
	as.ssid_len = (u8)sme->ssid_len;
	as.privacy = sme->privacy ? 1 : 0;
	as.seq = 0u;
	rc = hism_build_assoc_req(frame, sizeof(frame), &frame_len,
				  &assoc_ie, &assoc_ie_len, &as);
	if (rc) {
		dev_warn(&c->ctx->pdev->dev,
			 "sta mode DRY: assoc request refused (%d)\n", rc);
		return -EINVAL;
	}
	dev_info(&c->ctx->pdev->dev,
		 "sta mode DRY: auth request 30 B and assoc request %zu B (IE list %zu B) built+validated - nothing posted, no state change\n",
		 frame_len, assoc_ie_len);
	return 0;
}

/* ---------------------------------------------------------------------
 * Disconnect teardown (after the WAL 0x00d0 disconnect was posted).
 * -------------------------------------------------------------------*/
void hi5622_sta_mode_disconnect(struct hi5622_cfg80211 *c,
				struct net_device *dev, u16 reason)
{
	bool pending;

	if (hi5622_sta_connect_mode == 0u || !dev)
		return;
	mutex_lock(&sm.lock);
	if (!sm.dev) {
		mutex_unlock(&sm.lock);
		return;
	}
	if (sm.armed) {
		cancel_delayed_work(&sm.timer);
		sm.armed = false;
	}
	pending = hism_state_pending(sm.state);
	sm.state = HISM_OFF;
	sm.dev = NULL;
	if (pending) {
		struct cfg80211_connect_resp_params cp;

		memset(&cp, 0, sizeof(cp));
		cp.status = -1;
		cp.timeout_reason = HISM_TIMEOUT_UNSPECIFIED;
		cp.links[0].bssid = sm.bssid;
		cfg80211_connect_done(dev, &cp, GFP_KERNEL);
	} else {
		cfg80211_disconnected(dev, reason, NULL, 0, true, GFP_KERNEL);
	}
	mutex_unlock(&sm.lock);
	if (c && c->ctx)
		dev_info(&c->ctx->pdev->dev,
			 "sta mode: disconnect reported to cfg80211 (reason=%u, pending=%u); mode state off\n",
			 reason, pending ? 1u : 0u);
}

void hi5622_sta_mode_remove(struct hi5622_cfg80211 *c)
{
	mutex_lock(&sm.lock);
	if (sm.armed) {
		cancel_delayed_work(&sm.timer);
		sm.armed = false;
	}
	sm.dev = NULL;
	sm.state = HISM_OFF;
	mutex_unlock(&sm.lock);
}

/* ---------------------------------------------------------------------
 * CRX path: consume auth/assoc responses (D2H group 1 id 13).
 * Returns 1 when consumed.  Runs in the HCC service thread, so the lock is
 * trylock-only; a dropped frame is counted and never retried (fail-closed).
 * -------------------------------------------------------------------*/
int hi5622_sta_crx_deliver(struct hi5622_pci *ctx,
			   const struct himg_rx_frame *rx)
{
	struct hi5622_cfg80211 *c;
	struct hism_resp rsp;
	int handled = 0;
	int rc;

	if (!ctx || !rx || !ctx->cfg)
		return 0;
	if (hi5622_sta_connect_mode != HI5622_STA_MODE_LIVE)
		return 0;
	if (rx->subtype != HISM_SUBTYPE_AUTH &&
	    rx->subtype != HISM_SUBTYPE_ASSOC_RESP)
		return 0;
	c = ctx->cfg;
	if (!mutex_trylock(&sm.lock)) {
		sm.rx_dropped++;
		return 0;
	}
	if (!sm.dev)
		goto out;
	if (!hism_resp_for_us(rx->frame, rx->frame_len, sm.own_mac, sm.bssid)) {
		sm.rx_bad++;
		goto out;
	}
	if (rx->subtype == HISM_SUBTYPE_AUTH) {
		if (sm.state != HISM_AUTH_SENT) {
			sm.rx_bad++;
			goto out;
		}
		rc = hism_decode_auth_resp(rx->frame, rx->frame_len, &rsp);
		if (rc) {
			sm.rx_bad++;
			goto out;
		}
		handled = 1;
		sm.auth_rx++;
		if (hism_auth_rsp_apply(&sm.state, rsp.status)) {
			sm.state = HISM_FAILED;
			hi5622_sta_mode_finish_locked(-EIO, NULL, 0,
						      HISM_TIMEOUT_UNSPECIFIED);
			goto out;
		}
		if (rsp.status != HISM_STATUS_SUCCESS) {
			dev_warn(&ctx->pdev->dev,
				 "sta mode: auth response status=%u - connect failed (no retry)\n",
				 rsp.status);
			hi5622_sta_mode_finish_locked((int)rsp.status, NULL, 0,
						      HISM_TIMEOUT_UNSPECIFIED);
			goto out;
		}
		rc = hi5622_sta_mode_send_assoc_locked(c);
		if (rc) {
			dev_warn(&ctx->pdev->dev,
				 "sta mode: assoc request post refused (%d) - connect failed, no retry\n",
				 rc);
			hi5622_sta_mode_finish_locked(-EIO, NULL, 0,
						      HISM_TIMEOUT_UNSPECIFIED);
			goto out;
		}
		dev_info(&ctx->pdev->dev,
			 "sta mode: auth response OK -> assoc request posted (state %s)\n",
			 hism_state_name(sm.state));
	} else {
		if (sm.state != HISM_ASSOC_SENT) {
			sm.rx_bad++;
			goto out;
		}
		rc = hism_decode_assoc_resp(rx->frame, rx->frame_len, &rsp);
		if (rc) {
			sm.rx_bad++;
			goto out;
		}
		handled = 1;
		sm.assoc_rx++;
		if (hism_assoc_rsp_apply(&sm.state, rsp.status)) {
			sm.state = HISM_FAILED;
			hi5622_sta_mode_finish_locked(-EIO, NULL, 0,
						      HISM_TIMEOUT_UNSPECIFIED);
			goto out;
		}
		if (rsp.status != HISM_STATUS_SUCCESS) {
			dev_warn(&ctx->pdev->dev,
				 "sta mode: assoc response status=%u - connect failed (no retry)\n",
				 rsp.status);
			hi5622_sta_mode_finish_locked((int)rsp.status, NULL, 0,
						      HISM_TIMEOUT_UNSPECIFIED);
			goto out;
		}
		dev_info(&ctx->pdev->dev,
			 "sta mode: assoc response OK (aid=%u, resp IEs %zu B) -> cfg80211_connect_done(status=0) on the CRX path\n",
			 rsp.aid, rsp.ies_len);
		hi5622_sta_mode_finish_locked(0, rsp.ies, rsp.ies_len,
					      HISM_TIMEOUT_UNSPECIFIED);
	}
out:
	mutex_unlock(&sm.lock);
	return handled;
}
