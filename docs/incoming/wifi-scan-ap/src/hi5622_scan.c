// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_scan.c - kernel side of the cfg80211 scan/AP wiring for the
 * Hi5622V100 clean-room full-MAC driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (wifi-scan-ap lane, shared task task-113,
 * 2026-11-24).  No vendor source, object code, firmware byte, calibration
 * value or secret is reproduced; see ../REPORT.md for the per-claim evidence
 * and ../TEST-LADDER.md for the userspace sequence this enables.
 *
 * The whole design is one sentence: cfg80211 asks, the portable model decides,
 * the frame provider builds bytes, hi5622_frame_post() is the only wire path,
 * and a missing/undecoded payload fails closed with -EOPNOTSUPP instead of
 * synthesising a firmware message.
 *
 * SAFETY: no device access here.  The only way to the wire is
 * hi5622_frame_post() (allow_frames=0 default) and the inherited core SR path.
 */
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/ieee80211.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <net/cfg80211.h>

#include "hi5622_driver.h"
#include "hi5622_hcc.h"
#include "hi5622_cfg80211.h"
#include "hi5622_frames_api.h"
#include "hi5622_frame_post.h"
#include "hi5622_scan.h"

/* =====================================================================
 * helpers
 * ===================================================================*/
static u64 hi5622_scan_now(void)
{
	return hcc_kernel_now_ms();
}

static struct hi5622_scan *hi5622_scan_of(struct hi5622_cfg80211 *c)
{
	return c ? &c->scan : NULL;
}

/* Provider lookup with the capability bits the caller needs.  Returns NULL
 * (and explains once per call site via ratelimited dmesg) when the provider is
 * absent or does not implement the message - the caller then returns
 * -EOPNOTSUPP and cfg80211 stays honest. */
static const struct hi5622_frame_ops *
hi5622_scan_need(struct wiphy *wiphy, u32 caps, const char *op)
{
	struct device *dev = wiphy_dev(wiphy);
	const struct hi5622_frame_ops *ops = hi5622_frame_ops_get();

	if (!ops) {
		dev_warn_ratelimited(dev,
				     "cfg80211: %s needs an H2D frame provider and none is registered - -EOPNOTSUPP (the g0 id0xC/id7/id8/WAL payloads are still [U]; see wifi-scan-ap/REPORT.md)\n",
				     op);
		return NULL;
	}
	if ((ops->caps & caps) != caps) {
		dev_warn_ratelimited(dev,
				     "cfg80211: %s needs provider caps 0x%x, '%s' has 0x%x - -EOPNOTSUPP (partial builder)\n",
				     op, caps, ops->name, ops->caps);
		return NULL;
	}
	return ops;
}

/* cfg80211 BSS reporting needs a channel pointer from OUR band tables.  A
 * frequency lookup avoids importing ieee80211_get_channel from the cfg80211
 * module (one less provider symbol) and makes the single-band model explicit. */
static struct ieee80211_channel *
hi5622_scan_chan(struct wiphy *wiphy, u16 freq)
{
	int b;

	for (b = 0; b < NUM_NL80211_BANDS; b++) {
		struct ieee80211_supported_band *band = wiphy->bands[b];
		int i;

		if (!band)
			continue;
		for (i = 0; i < band->n_channels; i++)
			if (band->channels[i].center_freq == freq)
				return &band->channels[i];
	}
	return NULL;
}

static void hi5622_scan_report_result(struct hi5622_cfg80211 *c,
				      const struct hi5622_scan_result *r)
{
	struct ieee80211_channel *chan;
	enum cfg80211_bss_frame_type ftype;
	struct cfg80211_bss *bss;

	chan = hi5622_scan_chan(c->wiphy, r->freq);
	if (!chan) {
		hi5622_scan_note_inform_failure(&c->scan.model);
		return;
	}
	switch (r->frame_type) {
	case HI5622_SCAN_FTYPE_BEACON:
		ftype = CFG80211_BSS_FTYPE_BEACON;
		break;
	case HI5622_SCAN_FTYPE_PRESP:
		ftype = CFG80211_BSS_FTYPE_PRESP;
		break;
	default:
		ftype = CFG80211_BSS_FTYPE_UNKNOWN;
		break;
	}
	bss = cfg80211_inform_bss(c->wiphy, chan, ftype, r->bssid, r->tsf,
				  r->capability, r->beacon_interval, r->ie,
				  r->ie_len, r->signal_mbm, GFP_KERNEL);
	if (!bss)
		hi5622_scan_note_inform_failure(&c->scan.model);
	else
		cfg80211_put_bss(c->wiphy, bss);
}

/* =====================================================================
 * scan completion work: reports cfg80211_scan_done() exactly once
 * ===================================================================*/
static void hi5622_scan_work(struct work_struct *work)
{
	struct hi5622_scan *s = container_of(work, struct hi5622_scan,
					     work.work);
	struct hi5622_cfg80211 *c = s->cfg;
	struct cfg80211_scan_request *req = NULL;
	enum hi5622_scan_step step;
	unsigned long delay;
	u64 now, left;

	if (!c)
		return;
	mutex_lock(&s->lock);
	if (!s->initialized || s->model.phase != HI5622_SCAN_ACTIVE) {
		mutex_unlock(&s->lock);
		return;
	}
	/* The .scan op may still be polling the announce consumption; never
	 * call scan_done before the op returned 0 to cfg80211. */
	if (s->posting) {
		mutex_unlock(&s->lock);
		schedule_delayed_work(&s->work, msecs_to_jiffies(20));
		return;
	}
	now = hi5622_scan_now();
	step = hi5622_scan_model_tick(&s->model, now);
	if (step == HI5622_SCAN_WAIT) {
		left = s->model.deadline_ms > now ?
			s->model.deadline_ms - now : 0;
		if (left > HI5622_SCAN_POLL_MS)
			left = HI5622_SCAN_POLL_MS;
		delay = msecs_to_jiffies((unsigned int)left);
		mutex_unlock(&s->lock);
		schedule_delayed_work(&s->work, delay ? delay : 1);
		return;
	}
	req = s->request;
	s->request = NULL;
	hi5622_scan_model_finish(&s->model, step);
	hi5622_scan_model_clear(&s->model);
	mutex_unlock(&s->lock);
	if (req) {
		struct cfg80211_scan_info info = {
			.aborted = (step == HI5622_SCAN_FINISH_ABORT),
		};

		dev_info(wiphy_dev(c->wiphy),
			 "cfg80211: scan DONE (%s, results=%u bss=%u complete=%u channel=%u sta=%u unknown=%u dropped_busy=%d timeout=%u) - reporting cfg80211_scan_done(aborted=%u)\n",
			 hi5622_scan_step_name(step), s->model.results,
			 s->model.bss_events, s->model.complete_events,
			 s->model.channel_events, s->model.sta_events,
			 s->model.unknown_events,
			 atomic_read(&s->dropped_busy), s->model.timeouts,
			 info.aborted ? 1u : 0u);
		cfg80211_scan_done(req, &info);
	}
}

/* =====================================================================
 * lifecycle
 * ===================================================================*/
int hi5622_scan_init(struct hi5622_cfg80211 *c)
{
	struct hi5622_scan *s;

	if (!c)
		return -EINVAL;
	s = &c->scan;
	memset(s, 0, sizeof(*s));
	s->cfg = c;
	mutex_init(&s->lock);
	INIT_DELAYED_WORK(&s->work, hi5622_scan_work);
	atomic_set(&s->dropped_busy, 0);
	s->scan_timeout_ms = HI5622_SCAN_TIMEOUT_MS;
	hi5622_scan_model_init(&s->model);
	s->initialized = true;
	return 0;
}

void hi5622_scan_remove(struct hi5622_cfg80211 *c)
{
	struct hi5622_scan *s;
	struct cfg80211_scan_request *req = NULL;

	if (!c)
		return;
	s = &c->scan;
	if (!s->initialized)
		return;
	mutex_lock(&s->lock);
	s->initialized = false;
	if (s->model.phase == HI5622_SCAN_ACTIVE && !s->posting) {
		req = s->request;
		s->request = NULL;
		hi5622_scan_model_finish(&s->model, HI5622_SCAN_FINISH_ABORT);
		hi5622_scan_model_clear(&s->model);
	}
	mutex_unlock(&s->lock);
	cancel_delayed_work_sync(&s->work);
	if (req) {
		struct cfg80211_scan_info info = { .aborted = true };

		dev_info(wiphy_dev(c->wiphy),
			 "cfg80211: driver removal aborted an in-flight scan - cfg80211_scan_done(aborted=1)\n");
		cfg80211_scan_done(req, &info);
	}
}

/* =====================================================================
 * D2H scan events (called from hi5622_hcc_deliver for both receive paths)
 * ===================================================================*/
void hi5622_scan_deliver(struct hi5622_pci *ctx, const u8 *hdr,
			 const u8 *payload, u32 avail)
{
	struct hi5622_cfg80211 *c;
	struct hi5622_scan *s;
	const struct hi5622_frame_ops *ops;
	enum hi5622_scan_event_class cls;
	enum hi5622_scan_step step = HI5622_SCAN_WAIT;
	u8 group;
	u16 id;

	if (!ctx || !hdr || !payload)
		return;
	c = ctx->cfg;
	if (!c)
		return;
	s = &c->scan;
	if (!s->initialized)
		return;
	group = hdr[0] & 0x0fu;
	id = (u16)(hdr[6] | ((u16)hdr[7] << 8));
	cls = hi5622_scan_classify(group, id);
	if (cls == HI5622_SCAN_EV_NONE)
		return;

	/* trylock: the receive path is the HCC service thread and must never
	 * block on a cfg80211 op; a lost event is counted, not retried (the
	 * firmware scan completes on its own and the bounded timeout closes). */
	if (!mutex_trylock(&s->lock)) {
		atomic_inc(&s->dropped_busy);
		return;
	}
	ops = hi5622_frame_ops_get();
	if (!hi5622_scan_model_active(&s->model)) {
		(void)hi5622_scan_model_event(&s->model, cls, hi5622_scan_now());
		mutex_unlock(&s->lock);
		return;
	}
	switch (cls) {
	case HI5622_SCAN_EV_BSS: {
		struct hi5622_scan_result *r;

		r = kzalloc(sizeof(*r), GFP_KERNEL);
		if (!r) {
			hi5622_scan_note_parse_error(&s->model);
			break;
		}
		if (!ops || !ops->parse_scan_result) {
			hi5622_scan_note_parse_error(&s->model);
		} else if (ops->parse_scan_result(payload, avail, r)) {
			hi5622_scan_note_parse_error(&s->model);
		} else if (hi5622_scan_result_validate(r)) {
			hi5622_scan_note_invalid_result(&s->model);
		} else {
			hi5622_scan_report_result(c, r);
		}
		kfree(r);
		step = hi5622_scan_model_event(&s->model, cls,
					       hi5622_scan_now());
		break;
	}
	case HI5622_SCAN_EV_COMPLETE: {
		struct hi5622_scan_complete cc = {};

		if (ops && ops->parse_scan_complete &&
		    !ops->parse_scan_complete(payload, avail, &cc))
			s->model.complete_reason = cc.reason;
		step = hi5622_scan_model_event(&s->model, cls,
					       hi5622_scan_now());
		break;
	}
	default:
		step = hi5622_scan_model_event(&s->model, cls,
					       hi5622_scan_now());
		break;
	}
	mutex_unlock(&s->lock);
	if (step != HI5622_SCAN_WAIT)
		mod_delayed_work(system_wq, &s->work, 0);
}

/* =====================================================================
 * cfg80211 ops: scan / abort_scan
 * ===================================================================*/
int hi5622_scan_op_scan(struct wiphy *wiphy,
			struct cfg80211_scan_request *request)
{
	struct hi5622_cfg80211 *c = wiphy_priv(wiphy);
	struct hi5622_scan *s = hi5622_scan_of(c);
	const struct hi5622_frame_ops *ops;
	struct hi5622_scan_req_params *p = NULL;
	struct device *dev = wiphy_dev(wiphy);
	u8 *wire = NULL;
	size_t len = 0;
	u32 i;
	int rc;

	if (!s || !s->initialized)
		return -ENODEV;
	ops = hi5622_scan_need(wiphy,
			       HI5622_FRAME_CAP_SCAN_REQ |
			       HI5622_FRAME_CAP_SCAN_PARSE, "scan");
	if (!ops)
		return -EOPNOTSUPP;
	if (!request || !request->wdev)
		return -EINVAL;
	if (request->n_ssids > HI5622_FRAME_MAX_SSIDS ||
	    request->n_channels > HI5622_FRAME_MAX_CHANNELS ||
	    request->ie_len > HI5622_FRAME_MAX_SCAN_IE) {
		dev_warn(dev,
			 "cfg80211: scan request out of bounds (ssids=%d ch=%u ie=%zu) - refused\n",
			 request->n_ssids, request->n_channels,
			 request->ie_len);
		return -EINVAL;
	}
	p = kzalloc(sizeof(*p), GFP_KERNEL);
	wire = kmalloc(HI5622_FRAME_MAX_TOTAL, GFP_KERNEL);
	if (!p || !wire) {
		rc = -ENOMEM;
		goto out;
	}
	p->user_id = (u8)s->user_id;
	p->iftype = (u8)request->wdev->iftype;
	p->flags = request->flags;
	p->n_ssids = (u32)request->n_ssids;
	for (i = 0; i < p->n_ssids; i++) {
		const struct cfg80211_ssid *ssid = &request->ssids[i];
		size_t n = ssid->ssid_len;

		if (n > HI5622_FRAME_MAX_SSID_LEN) {
			rc = -EINVAL;
			goto out;
		}
		p->ssid_len[i] = (u8)n;
		if (n)
			memcpy(p->ssid[i], ssid->ssid, n);
	}
	p->n_channels = request->n_channels;
	for (i = 0; i < p->n_channels; i++)
		p->freq[i] = request->channels[i]->center_freq;
	p->ie_len = (u32)request->ie_len;
	if (p->ie_len && request->ie)
		memcpy(p->ie, request->ie, p->ie_len);

	rc = hi5622_scan_req_params_validate(p);
	if (rc) {
		dev_warn(dev,
			 "cfg80211: scan request failed validation (%d) - refused (no firmware message)\n",
			 rc);
		goto out;
	}
	rc = ops->build_scan_req(wire, HI5622_FRAME_MAX_TOTAL, &len, p);
	if (rc) {
		dev_warn(dev,
			 "cfg80211: scan request builder '%s' returned %d - -EOPNOTSUPP (g0 id0xC payload still [U]; no speculative frame)\n",
			 ops->name, rc);
		rc = rc < 0 ? rc : -EIO;
		goto out;
	}
	if (len < HI5622_FRAME_HDR_LEN || len > HI5622_FRAME_MAX_TOTAL) {
		dev_err(dev, "cfg80211: scan builder produced invalid length %zu\n",
			len);
		rc = -EINVAL;
		goto out;
	}

	mutex_lock(&s->lock);
	rc = hi5622_scan_model_start(&s->model, hi5622_scan_now(),
				     s->scan_timeout_ms);
	if (rc) {
		mutex_unlock(&s->lock);
		dev_warn(dev, "cfg80211: scan refused by the model (%d) - a scan is already in flight or not cleared\n",
			 rc);
		goto out;
	}
	s->request = request;
	s->posting = true;
	mutex_unlock(&s->lock);

	rc = hi5622_frame_post(c->ctx, wire, len, "scan_req", NULL);

	mutex_lock(&s->lock);
	s->posting = false;
	if (rc) {
		s->request = NULL;
		hi5622_scan_model_finish(&s->model, HI5622_SCAN_FINISH_ABORT);
		hi5622_scan_model_clear(&s->model);
		hi5622_scan_note_error(&s->model, "scan post", rc);
		mutex_unlock(&s->lock);
		dev_warn(dev,
			 "cfg80211: scan request was NOT posted (%d) - returning the error to cfg80211, no scan_done follows\n",
			 rc);
		goto out;
	}
	mutex_unlock(&s->lock);
	dev_info(dev,
		 "cfg80211: scan request posted by provider '%s' (%u ssids, %u channels, %u IE bytes, total %zu B); waiting for g1 15/16/17 or g2 2, timeout %u ms\n",
		 ops->name, p->n_ssids, p->n_channels, p->ie_len, len,
		 s->scan_timeout_ms);
	schedule_delayed_work(&s->work, msecs_to_jiffies(HI5622_SCAN_POLL_MS));
out:
	kfree(p);
	kfree(wire);
	return rc;
}

void hi5622_scan_op_abort(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	struct hi5622_cfg80211 *c = wiphy_priv(wiphy);
	struct hi5622_scan *s = hi5622_scan_of(c);
	bool active;

	(void)wdev;
	if (!s || !s->initialized)
		return;
	mutex_lock(&s->lock);
	hi5622_scan_model_abort(&s->model);
	active = hi5622_scan_model_active(&s->model);
	mutex_unlock(&s->lock);
	if (!active)
		return;
	dev_info(wiphy_dev(wiphy),
		 "cfg80211: abort_scan requested - the bounded scan work will report cfg80211_scan_done(aborted=1)\n");
	schedule_delayed_work(&s->work, 0);
}

/* =====================================================================
 * cfg80211 ops: AP start/stop (WAL group-2 family; design + provider stub)
 * ===================================================================*/
static int hi5622_scan_fill_ap_start(struct hi5622_scan *s,
				     const struct cfg80211_ap_settings *set,
				     struct hi5622_ap_start_params *p)
{
	if (!set->chandef.chan)
		return -EINVAL;
	p->iftype = 3u;
	p->user_id = (u8)s->user_id;
	p->center_freq = set->chandef.chan->center_freq;
	p->width = (u8)set->chandef.width;
	p->beacon_interval = (u16)set->beacon_interval;
	p->dtim_period = (u16)set->dtim_period;
	p->privacy = set->privacy ? 1u : 0u;
	p->auth_type = (u8)set->auth_type;
	/* Opaque crypto summary: the WAL key/AP-start payload is gap G1, so we
	 * pass only the userspace requirement (which WPA versions) and let the
	 * provider decide.  Never a made-up key. */
	p->crypto = (u32)set->crypto.wpa_versions;
	if (set->ssid && set->ssid_len) {
		if (set->ssid_len > HI5622_FRAME_MAX_SSID_LEN)
			return -EINVAL;
		p->ssid_len = (u8)set->ssid_len;
		memcpy(p->ssid, set->ssid, p->ssid_len);
	}
	if (set->beacon.head_len) {
		if (set->beacon.head_len > HI5622_FRAME_MAX_BCN_HEAD)
			return -EINVAL;
		p->head_len = (u16)set->beacon.head_len;
		memcpy(p->head, set->beacon.head, p->head_len);
	}
	if (set->beacon.tail_len) {
		if (set->beacon.tail_len > HI5622_FRAME_MAX_BCN_TAIL)
			return -EINVAL;
		p->tail_len = (u16)set->beacon.tail_len;
		memcpy(p->tail, set->beacon.tail, p->tail_len);
	}
	return hi5622_ap_start_params_validate(p);
}

int hi5622_scan_op_start_ap(struct wiphy *wiphy, struct net_device *dev,
			    struct cfg80211_ap_settings *settings)
{
	struct hi5622_cfg80211 *c = wiphy_priv(wiphy);
	struct hi5622_scan *s = hi5622_scan_of(c);
	const struct hi5622_frame_ops *ops;
	struct hi5622_ap_start_params *p;
	struct device *wdev_dev = wiphy_dev(wiphy);
	u8 *wire = NULL;
	size_t len = 0;
	int rc;

	if (!s || !s->initialized || !settings || !dev)
		return -ENODEV;
	if (!dev->ieee80211_ptr ||
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP)
		return -EINVAL;
	ops = hi5622_scan_need(wiphy, HI5622_FRAME_CAP_AP_START,
			       "start_ap");
	if (!ops)
		return -EOPNOTSUPP;
	p = kzalloc(sizeof(*p), GFP_KERNEL);
	wire = kmalloc(HI5622_FRAME_MAX_TOTAL, GFP_KERNEL);
	if (!p || !wire) {
		rc = -ENOMEM;
		goto out;
	}
	rc = hi5622_scan_fill_ap_start(s, settings, p);
	if (rc) {
		dev_warn(wdev_dev,
			 "cfg80211: start_ap settings failed validation (%d) - refused\n",
			 rc);
		goto out;
	}
	rc = ops->build_ap_start(wire, HI5622_FRAME_MAX_TOTAL, &len, p);
	if (rc) {
		dev_warn(wdev_dev,
			 "cfg80211: start_ap builder '%s' returned %d - -EOPNOTSUPP (WAL group-2 AP-start payload is gap G1; no speculative frame)\n",
			 ops->name, rc);
		rc = rc < 0 ? rc : -EIO;
		goto out;
	}
	rc = hi5622_frame_post(c->ctx, wire, len, "ap_start", NULL);
	if (rc)
		goto out;
	dev_info(wdev_dev,
		 "cfg80211: start_ap handed to provider '%s' (ssid_len=%u freq=%u MHz width=%u beacon=%u dtim=%u head=%u tail=%u, %zu B frame) - EXPERIMENTAL: beacons/keys remain firmware/ M3 work, hostapd may stop at add_key\n",
		 ops->name, p->ssid_len, p->center_freq, p->width,
		 p->beacon_interval, p->dtim_period, p->head_len, p->tail_len,
		 len);
out:
	kfree(p);
	kfree(wire);
	return rc;
}

int hi5622_scan_op_stop_ap(struct wiphy *wiphy, struct net_device *dev,
			   unsigned int link_id)
{
	struct hi5622_cfg80211 *c = wiphy_priv(wiphy);
	struct hi5622_scan *s = hi5622_scan_of(c);
	const struct hi5622_frame_ops *ops;
	struct hi5622_ap_stop_params p;
	struct device *wdev_dev = wiphy_dev(wiphy);
	u8 *wire = NULL;
	size_t len = 0;
	int rc;

	(void)link_id;
	if (!s || !s->initialized || !dev)
		return -ENODEV;
	ops = hi5622_scan_need(wiphy, HI5622_FRAME_CAP_AP_STOP, "stop_ap");
	if (!ops)
		return -EOPNOTSUPP;
	memset(&p, 0, sizeof(p));
	p.iftype = 3u;
	p.user_id = (u8)s->user_id;
	rc = hi5622_ap_stop_params_validate(&p);
	if (rc)
		return rc;
	wire = kmalloc(HI5622_FRAME_MAX_TOTAL, GFP_KERNEL);
	if (!wire)
		return -ENOMEM;
	rc = ops->build_ap_stop(wire, HI5622_FRAME_MAX_TOTAL, &len, &p);
	if (rc) {
		dev_warn(wdev_dev,
			 "cfg80211: stop_ap builder '%s' returned %d - -EOPNOTSUPP (no frame sent)\n",
			 ops->name, rc);
		rc = rc < 0 ? rc : -EIO;
		goto out;
	}
	rc = hi5622_frame_post(c->ctx, wire, len, "ap_stop", NULL);
	if (!rc)
		dev_info(wdev_dev,
			 "cfg80211: stop_ap handed to provider '%s' (%zu B frame)\n",
			 ops->name, len);
out:
	kfree(wire);
	return rc;
}

/* =====================================================================
 * VAP firmware messages (g0 id7/id8 add, g0 id9 delete)
 * ===================================================================*/
static bool hi5622_vap_model_copy(struct hi5622_cfg80211 *c, u16 slot,
				  struct hi5622_vap_model *out)
{
	bool found = false;

	mutex_lock(&c->lock);
	if (slot < HI5622_CFG_MAX_VAPS &&
	    c->model.vap[slot].state != HI5622_VAP_FREE) {
		*out = c->model.vap[slot];
		found = true;
	}
	mutex_unlock(&c->lock);
	return found;
}

int hi5622_vap_hcc_add(struct hi5622_cfg80211 *c, u16 slot)
{
	struct hi5622_scan *s;
	const struct hi5622_frame_ops *ops;
	struct hi5622_vap_model vm;
	struct hi5622_vap_add_params p;
	struct device *dev;
	u8 *wire = NULL;
	size_t len = 0;
	int rc;

	if (!c)
		return -EINVAL;
	s = hi5622_scan_of(c);
	dev = wiphy_dev(c->wiphy);
	ops = hi5622_frame_ops_get();
	if (!hi5622_vap_model_copy(c, slot, &vm))
		return -ENOENT;
	if (!ops || !(ops->caps & HI5622_FRAME_CAP_USER_ADD)) {
		mutex_lock(&c->lock);
		c->model.vap[slot].hcc_add_pending = true;
		c->model.hcc_vap_msg_deferred++;
		snprintf(c->model.last_error, sizeof(c->model.last_error),
			 "user_add (g0 id7): no frame provider/capability");
		mutex_unlock(&c->lock);
		dev_info(dev,
			 "cfg80211: VAP '%s' netdev is live; firmware user_add (g0 id7) not sent: provider absent or partial (payload [U] - wifi-vap-scan lane)\n",
			 vm.name);
		return -EOPNOTSUPP;
	}
	memset(&p, 0, sizeof(p));
	memcpy(p.name, vm.name, sizeof(p.name));
	p.name[sizeof(p.name) - 1] = '\0';
	memcpy(p.mac, vm.addr, sizeof(p.mac));
	p.iftype = (u8)vm.iftype;
	p.user_id = (u8)(slot & 0xffu);
	p.vap_id = (u8)(slot & 0xffu);
	rc = hi5622_vap_add_params_validate(&p);
	if (rc) {
		dev_warn(dev, "cfg80211: user_add params invalid (%d) - not sent\n",
			 rc);
		return rc;
	}
	wire = kmalloc(HI5622_FRAME_MAX_TOTAL, GFP_KERNEL);
	if (!wire)
		return -ENOMEM;
	rc = ops->build_user_add(wire, HI5622_FRAME_MAX_TOTAL, &len, &p);
	if (rc) {
		dev_warn(dev,
			 "cfg80211: user_add builder '%s' returned %d - message not sent (netdev stays live)\n",
			 ops->name, rc);
		rc = rc < 0 ? rc : -EIO;
		goto out;
	}
	rc = hi5622_frame_post(c->ctx, wire, len, "user_add", NULL);
	if (rc)
		goto out;
	/* Vendor order: hmac_user_add first, then the notify-alg companion
	 * (g0 id8) when the provider has it.  A notify failure does not undo
	 * the add; it is recorded. */
	if (ops->caps & HI5622_FRAME_CAP_USER_ADD_NOTIFY) {
		int nrc = ops->build_user_add_notify(wire,
						     HI5622_FRAME_MAX_TOTAL,
						     &len, &p);

		if (!nrc)
			nrc = hi5622_frame_post(c->ctx, wire, len,
						"user_add_notify", NULL);
		if (nrc)
			dev_warn(dev,
				 "cfg80211: user_add_notify (g0 id8) failed (%d); user_add already committed\n",
				 nrc);
	}
	mutex_lock(&c->lock);
	c->model.vap[slot].hcc_add_pending = false;
	c->model.vap[slot].hcc_user_id = p.user_id;
	mutex_unlock(&c->lock);
	dev_info(dev,
		 "cfg80211: VAP '%s' firmware user_add committed (g0 id7, user_id=%u vap_id=%u)\n",
		 vm.name, p.user_id, p.vap_id);
out:
	kfree(wire);
	return rc;
}

int hi5622_vap_hcc_del(struct hi5622_cfg80211 *c, u16 user_id, const u8 *addr)
{
	struct hi5622_scan *s;
	const struct hi5622_frame_ops *ops;
	struct hi5622_vap_del_params p;
	struct device *dev;
	u8 *wire = NULL;
	size_t len = 0;
	int rc;

	if (!c)
		return -EINVAL;
	s = hi5622_scan_of(c);
	dev = wiphy_dev(c->wiphy);
	ops = hi5622_frame_ops_get();
	if (!ops || !(ops->caps & HI5622_FRAME_CAP_USER_DEL)) {
		mutex_lock(&c->lock);
		c->model.hcc_vap_msg_deferred++;
		snprintf(c->model.last_error, sizeof(c->model.last_error),
			 "user_del (g0 id9): no frame provider/capability");
		mutex_unlock(&c->lock);
		dev_info(dev,
			 "cfg80211: VAP firmware delete (g0 id9) not sent: provider absent or partial; netdev teardown continues\n");
		return -EOPNOTSUPP;
	}
	memset(&p, 0, sizeof(p));
	if (user_id && user_id != 0x0fffu) {
		p.user_id = user_id;
		if (addr)
			memcpy(p.mac, addr, sizeof(p.mac));
	} else {
		p.user_id = 0x0fffu;
		if (addr)
			memcpy(p.mac, addr, sizeof(p.mac));
	}
	rc = hi5622_vap_del_params_validate(&p);
	if (rc) {
		dev_warn(dev, "cfg80211: user_del params invalid (%d) - not sent\n",
			 rc);
		return rc;
	}
	wire = kmalloc(HI5622_FRAME_MAX_TOTAL, GFP_KERNEL);
	if (!wire)
		return -ENOMEM;
	rc = ops->build_user_del(wire, HI5622_FRAME_MAX_TOTAL, &len, &p);
	if (rc) {
		dev_warn(dev,
			 "cfg80211: user_del builder '%s' returned %d - message not sent\n",
			 ops->name, rc);
		rc = rc < 0 ? rc : -EIO;
		goto out;
	}
	rc = hi5622_frame_post(c->ctx, wire, len, "user_del", NULL);
	if (!rc)
		dev_info(dev,
			 "cfg80211: VAP firmware user_del committed (g0 id9, user_id=0x%x)\n",
			 p.user_id);
out:
	kfree(wire);
	return rc;
}

/* =====================================================================
 * sysfs status
 * ===================================================================*/
int hi5622_scan_status(struct hi5622_cfg80211 *c, char *buf, size_t len)
{
	struct hi5622_scan *s;
	const struct hi5622_frame_ops *ops;
	char caps[96];
	size_t pos = 0;
	int n, ret;

	if (!c || !buf || !len)
		return -EINVAL;
	s = &c->scan;
	ops = hi5622_frame_ops_get();
	if (ops)
		ret = hi5622_frame_caps_format(ops->caps, caps, sizeof(caps));
	else
		ret = hi5622_frame_caps_format(0, caps, sizeof(caps));
	if (ret < 0)
		caps[0] = '\0';
	n = scnprintf(buf, len,
		      "scan layer: initialized=%d provider=%s caps=%s user_id=%u timeout_ms=%u dropped_busy=%d\n",
		      s->initialized ? 1 : 0, ops ? ops->name : "-", caps,
		      s->user_id, s->scan_timeout_ms,
		      atomic_read(&s->dropped_busy));
	pos += (size_t)n;
	if (pos >= len)
		return (int)(len - 1);
	ret = hi5622_frame_post_status_line(buf + pos, len - pos);
	if (ret > 0)
		pos += (size_t)ret;
	if (pos >= len)
		return (int)(len - 1);
	mutex_lock(&s->lock);
	ret = hi5622_scan_status_line(&s->model, buf + pos, len - pos);
	mutex_unlock(&s->lock);
	if (ret > 0)
		pos += (size_t)ret;
	return (int)(pos < len ? pos : len - 1);
}
