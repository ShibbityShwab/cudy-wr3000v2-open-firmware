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
#include "hi5622_vapscan_map.h"

/* =====================================================================
 * helpers
 * ===================================================================*/
/* wifi-vap-start lane (task-117): module params owned by the provider;
 * this file owns only the cfg80211 ordering (VAP add -> VAP start -> scan). */
extern unsigned int hi5622_vap_start_first;
extern unsigned int hi5622_vap_start_module;

/* scan-complete lane (task-123): the bench measured the firmware returning 6
 * of the 13 requested per-channel results within the wifi-scan-ap lane's 5 s
 * bound (~830 ms/channel, ledger 2026-11-26 14:00), so a 13-channel request
 * needs ~11 s.  Default 0 = auto (BASE + n*PER_CHAN, see
 * hi5622_scan_timeout_auto()); an explicit value is clamped to
 * [1000, HI5622_SCAN_TIMEOUT_MAX_MS].  Writable at runtime. */
static unsigned int hi5622_scan_timeout_ms;
module_param_named(scan_timeout_ms, hi5622_scan_timeout_ms, uint, 0644);
MODULE_PARM_DESC(scan_timeout_ms,
		 "scan completion bound in ms (0 = auto from the channel count)");

/* 0 = provider/vendor default dwell (60); 20..200 overrides the g0 id0xC dwell
 * field for a bench sweep.  Read at scan-op time; an invalid value is logged
 * and ignored. */
static unsigned int hi5622_scan_dwell_ms;
module_param_named(scan_dwell_ms, hi5622_scan_dwell_ms, uint, 0644);
MODULE_PARM_DESC(scan_dwell_ms,
		 "g0 id0xC scan dwell override in ms (0 = vendor default 60, 20..200)");

/* task-133 (wifi-scan-tuning): per-channel cost used by the auto scan window.
 * 0 = compiled default (250 ms; the bench measured ~156 ms/channel on the
 * self-contained image).  Out-of-range values are ignored, not clamped, so a
 * typo never changes the window silently. */
static unsigned int hi5622_scan_chan_time_ms;
module_param_named(scan_chan_time_ms, hi5622_scan_chan_time_ms, uint, 0644);
MODULE_PARM_DESC(scan_chan_time_ms,
		 "per-channel auto-window cost in ms (0 = compiled 250, 50..2000)");

/* task-133: the firmware scan engine was measured to stop after the first 6
 * requested channels (6 g1 id17 results then no frame for 14 s).  The driver
 * can chain ceil(n/batch) g0 id0xC requests so one cfg80211 scan still covers
 * every requested channel.  0 or >= n keeps the legacy single-request form;
 * the compiled default 6 matches the measured per-request firmware limit. */
static unsigned int hi5622_scan_req_batch = HI5622_SCAN_DEFAULT_BATCH;
module_param_named(scan_req_batch, hi5622_scan_req_batch, uint, 0644);
MODULE_PARM_DESC(scan_req_batch,
		 "channels per g0 id0xC scan request (0/>=n = one request, default 6)");

/* Pause between chained requests: the firmware must clear its scan-in-progress
 * state before the next request; until a completion event is proven the host
 * uses this fixed gap.  0 = post as soon as coverage is complete. */
static unsigned int hi5622_scan_batch_gap_ms = HI5622_SCAN_BATCH_GAP_MS;
module_param_named(scan_batch_gap_ms, hi5622_scan_batch_gap_ms, uint, 0644);
MODULE_PARM_DESC(scan_batch_gap_ms,
		 "pause between chained scan requests in ms (default 300, 0..5000)");

static u64 hi5622_scan_now(void)
{
	return hcc_kernel_now_ms();
}

static struct hi5622_scan *hi5622_scan_of(struct hi5622_cfg80211 *c)
{
	return c ? &c->scan : NULL;
}

/* Effective bounded-scan window for ONE request/batch (auto unless the
 * scan_timeout_ms module parameter overrides it).  The auto term uses the
 * measured per-channel cost, overridable through scan_chan_time_ms. */
static u32 hi5622_scan_pick_timeout(u32 n_channels)
{
	u32 ms = hi5622_scan_timeout_ms;
	u32 chan = hi5622_scan_chan_time_ms;

	if (!ms) {
		if (chan && (chan < 50u || chan > 2000u)) {
			pr_warn_ratelimited("hi5622_wifi: scan_chan_time_ms=%u out of range (50..2000) - using the compiled default\n",
					    chan);
			chan = 0;
		}
		return hi5622_scan_timeout_for(n_channels, chan);
	}
	if (ms < 1000u)
		ms = 1000u;
	if (ms > HI5622_SCAN_TIMEOUT_MAX_MS)
		ms = HI5622_SCAN_TIMEOUT_MAX_MS;
	return ms;
}

/* g0 id0xC dwell override: 0 keeps the vendor default (60). */
static u16 hi5622_scan_pick_dwell(struct device *dev)
{
	u32 d = hi5622_scan_dwell_ms;

	if (!d)
		return 0;
	if (d < 20u || d > 200u) {
		dev_warn_ratelimited(dev,
				     "cfg80211: scan_dwell_ms=%u out of range (20..200) - using the vendor default\n",
				     d);
		return 0;
	}
	return (u16)d;
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
	struct device *dev = wiphy_dev(c->wiphy);
	struct ieee80211_channel *chan;
	enum cfg80211_bss_frame_type ftype;
	struct cfg80211_bss *bss;

	chan = hi5622_scan_chan(c->wiphy, r->freq);
	if (!chan) {
		hi5622_scan_note_inform_failure(&c->scan.model);
		dev_warn_ratelimited(dev,
				     "cfg80211: decoded BSS %pM on %u MHz but this wiphy has no such channel - not reported (inform_failure)\n",
				     r->bssid, r->freq);
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
	/* cfg80211_inform_bss() takes the *additional IEs* (starting with the
	 * SSID element) plus the fixed fields; cfg80211 parses SSID/DS/HT/VHT
	 * itself and builds the scan-list entry.  The IE chain forwarded here is
	 * the decoder-validated one, so a malformed tail can never reach it. */
	bss = cfg80211_inform_bss(c->wiphy, chan, ftype, r->bssid, r->tsf,
				  r->capability, r->beacon_interval, r->ie,
				  r->ie_len, r->signal_mbm, GFP_KERNEL);
	if (!bss) {
		hi5622_scan_note_inform_failure(&c->scan.model);
		dev_warn_ratelimited(dev,
				     "cfg80211: cfg80211_inform_bss() refused BSS %pM '%*.s' (%u MHz, %u/%u IE bytes)\n",
				     r->bssid, (int)r->ssid_len, r->ssid, r->freq,
				     r->ie_count, r->ie_len);
		return;
	}
	cfg80211_put_bss(c->wiphy, bss);
	/* One line per decoded BSS: the bench evidence that gate "M4 scan lists
	 * BSS entries" asserts on (SSID and channel/DS/HT come from the IE chain
	 * that was just handed to cfg80211). */
	dev_info_ratelimited(dev,
			     "cfg80211: BSS %pM '%.*s' %u MHz (DS ch %u) %s cap=0x%04x ess=%u priv=%u bcn=%u TU ie=%u/%u ht_cap=%u ht_op=%u rsn=%u wmm=%u tsf=%llu signal=%d mBm\n",
			     r->bssid, (int)r->ssid_len, r->ssid,
			     r->freq, r->ds_chan,
			     r->frame_type == HI5622_SCAN_FTYPE_PRESP ?
					"probe-resp" : "beacon",
			     r->capability,
			     !!(r->cap_flags & HI5622_SCAN_CAP_ESS),
			     !!(r->cap_flags & HI5622_SCAN_CAP_PRIVACY),
			     r->beacon_interval, r->ie_count, r->ie_len,
			     !!(r->ie_flags & HI5622_SCAN_IE_HT_CAP),
			     !!(r->ie_flags & HI5622_SCAN_IE_HT_OP),
			     !!(r->ie_flags & HI5622_SCAN_IE_RSN),
			     !!(r->ie_flags & HI5622_SCAN_IE_WMM),
			     (unsigned long long)r->tsf, r->signal_mbm);
}

/* cfg80211 BSS-list readback (task-133 R1 follow-up): after the last batch is
 * complete, ask cfg80211 how many BSS entries the scan actually inserted and
 * how many of them pass the nl80211 "use for normal connection" dump filter
 * that iw relies on (iw does not request NL80211_ATTR_BSS_DUMP_INCLUDE_USE_DATA).
 * This turns "iw shows 0 BSS although results>0" into a precise observation:
 *   entries=0 -> cfg80211_inform_bss() never inserted (check inform_fail),
 *   entries>0 iw_visible=0 -> entries hidden by use_for/ABI mismatch,
 *   entries>0 iw_visible>0 -> results are visible; an empty iw dump then points
 *                              at the iw invocation/interface, not the driver.
 * cfg80211_bss_iter is a cfg80211 export (declared in cfg80211.h); the
 * cfg80211-provider.symvers file carries it for modpost. */
struct hi5622_scan_bss_diag {
	u32 total;
	u32 iw_visible;
};

static void hi5622_scan_bss_count(struct wiphy *wiphy,
				  struct cfg80211_bss *bss, void *data)
{
	struct hi5622_scan_bss_diag *d = data;

	(void)wiphy;
	d->total++;
	if (bss->use_for & (u8)NL80211_BSS_USE_FOR_NORMAL)
		d->iw_visible++;
}

/* Index of a requested frequency in the current cfg80211 scan (for the
 * accumulated all-batch coverage counter). */
static u32 hi5622_scan_freq_index(struct hi5622_scan *s, u16 freq)
{
	u32 i;

	for (i = 0; i < s->n_freqs && i < 64u; i++)
		if (s->freqs[i] == freq)
			return i;
	return 0xffffffffu;
}

/* task-133 forward declarations (the batch helpers live below the .scan op,
 * the work item above them needs them). */
static bool hi5622_scan_batch_done(const struct hi5622_scan_model *m, u64 now_ms);
static int hi5622_scan_send_batch(struct hi5622_scan *s, u32 first, u32 n);

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
	/* task-133 multi-request scan: when the CURRENT request is done (every
	 * batch channel reported + settle, or a firmware success completion for
	 * it) post the next batch instead of closing the cfg80211 scan.  The final
	 * batch falls through to the model tick, which reports the success. */
	if (s->batches_sent && s->batches_sent < s->batches_total &&
	    hi5622_scan_batch_done(&s->model, now)) {
		if (!s->batch_pending) {
			s->batch_pending = true;
			s->batch_due_ms = now + (u64)hi5622_scan_batch_gap_ms;
			dev_info(wiphy_dev(c->wiphy),
				 "cfg80211: scan batch %u/%u complete - next request in %u ms\n",
				 s->batches_sent, s->batches_total,
				 hi5622_scan_batch_gap_ms);
		}
	}
	if (s->batch_pending && !s->model.abort_requested) {
		if (now < s->batch_due_ms) {
			left = s->batch_due_ms - now;
			if (left > HI5622_SCAN_POLL_MS)
				left = HI5622_SCAN_POLL_MS;
			mutex_unlock(&s->lock);
			schedule_delayed_work(&s->work,
					      msecs_to_jiffies((unsigned int)left));
			return;
		}
		u32 next = s->batches_sent;
		u32 first = s->batch_first + s->batch_n;
		u32 len = hi5622_scan_batch_len(s->n_freqs, s->batch_param, next);
		struct cfg80211_scan_request *req = s->request;
		int brc;

		s->batch_pending = false;
		s->batch_due_ms = 0;
		/* Close the finished batch's model window but keep owning the
		 * cfg80211 request; the counters survive model_clear(). */
		hi5622_scan_model_finish(&s->model, HI5622_SCAN_FINISH_OK);
		hi5622_scan_model_clear(&s->model);
		mutex_unlock(&s->lock);
		brc = hi5622_scan_send_batch(s, first, len);
		if (brc) {
			/* A chained request failed after cfg80211 accepted the scan:
			 * report the scan as aborted (the first-batch failure path
			 * returns the error from .scan instead). */
			if (req) {
				struct cfg80211_scan_info info = { .aborted = true };

				dev_warn(wiphy_dev(c->wiphy),
					 "cfg80211: chained scan request %u/%u failed (%d) - reporting cfg80211_scan_done(aborted=1)\n",
					 s->batches_sent + 1u, s->batches_total, brc);
				cfg80211_scan_done(req, &info);
			}
			return;
		}
		schedule_delayed_work(&s->work, msecs_to_jiffies(HI5622_SCAN_POLL_MS));
		return;
	}
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

		{
			struct hi5622_scan_bss_diag diag = {};

			cfg80211_bss_iter(c->wiphy, NULL, hi5622_scan_bss_count,
					  &diag);
			dev_info(wiphy_dev(c->wiphy),
				 "cfg80211: scan BSS list check: entries=%u iw_visible=%u (reported=%u inform_fail=%u) - iw needs iw_visible>0\n",
				 diag.total, diag.iw_visible,
				 s->model.results, s->model.inform_failures);
		}
		dev_info(wiphy_dev(c->wiphy),
			 "cfg80211: scan DONE (%s[%s], results=%u bss=%u complete=%u channel=%u chans=%u/%u batches=%u/%u sta=%u unknown=%u dropped_busy=%d parse=%u invalid=%u inform_fail=%u timeout=%u) - reporting cfg80211_scan_done(aborted=%u)\n",
			 hi5622_scan_step_name(step),
			 s->model.finish_reason[0] ? s->model.finish_reason : "-",
			 s->model.results,
			 s->model.bss_events, s->model.complete_events,
			 s->model.channel_events,
			 s->total_seen, s->total_expected,
			 s->batches_sent, s->batches_total,
			 s->model.sta_events,
			 s->model.unknown_events,
			 atomic_read(&s->dropped_busy), s->model.parse_errors,
			 s->model.invalid_results, s->model.inform_failures,
			 s->model.timeouts,
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
	s->scan_timeout_ms = 0;	/* auto, sized per request (task-133) */
	hi5622_scan_model_init(&s->model);
	s->initialized = true;
	/* Idempotent global registration of the task-110 wifi-vap-scan builders
	 * as the module's H2D frame provider (registry accepts one provider).
	 * This sends nothing: the separate post gate still defaults to
	 * allow_frames=0.  Failure is non-fatal (the module just stays without a
	 * provider, i.e. the previous fail-closed behaviour). */
	{
		int prc = hi5622_vapscan_provider_register();

		if (prc)
			dev_warn(wiphy_dev(c->wiphy),
				 "cfg80211: frame provider registration failed (%d) - message ops stay -EOPNOTSUPP\n",
				 prc);
	}
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
		/* Driver teardown, not a firmware failure or a bounded timeout:
		 * report the finish reason as abort-request. */
		s->model.abort_requested = true;
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

		/* g1 id16: the decoder exposes the vendor status byte
		 * (wifi-scan-d2h, task-118) so a failed completion is reported
		 * to cfg80211 as an aborted scan instead of a fake success. */
		if (ops && ops->parse_scan_complete &&
		    !ops->parse_scan_complete(payload, avail, &cc))
			step = hi5622_scan_model_complete(&s->model, cc.reason,
							  cc.aborted,
							  hi5622_scan_now());
		else
			step = hi5622_scan_model_event(&s->model, cls,
						       hi5622_scan_now());
		break;
	}
	case HI5622_SCAN_EV_CHANNEL: {
		struct hi5622_scan_channel ch = {};
		u16 freq = 0;

		/* g1 id17 per-channel result (fully decoded by task-118): record the
		 * scanned channel in the model so "all requested channels reported"
		 * can close the scan without a firmware completion event. */
		if (ops && ops->parse_scan_channel &&
		    !ops->parse_scan_channel(payload, avail, &ch)) {
			u32 fi = hi5622_scan_freq_index(s, ch.freq);

			freq = ch.freq;
			if (fi < 64u && !(s->total_seen_mask & (1ULL << fi))) {
				s->total_seen_mask |= (1ULL << fi);
				s->total_seen++;
			}
			dev_info_ratelimited(&ctx->pdev->dev,
					     "cfg80211: scan channel result slot=%u ch=%u (%u MHz%s)\n",
					     ch.slot, ch.chan_num, ch.freq,
					     ch.is_5g ? ", 5GHz" : "");
		}
		step = hi5622_scan_model_channel(&s->model, freq,
						 hi5622_scan_now());
		break;
	}
	case HI5622_SCAN_EV_STA_RSP: {
		struct hi5622_scan_sta_complete sc = {};

		/* g2 id2 wal_scan_comp_proc_sta is the vendor's cfg80211 feed /
		 * scan-done path [V]; the model treats it as a completion
		 * candidate exactly like g1 id16.  Status semantics [I]/[U],
		 * so it is logged and recorded, never used as a false aborted. */
		if (ops && ops->parse_scan_sta_complete &&
		    !ops->parse_scan_sta_complete(payload, avail, &sc)) {
			s->model.complete_reason = sc.status;
			dev_info_ratelimited(&ctx->pdev->dev,
					     "cfg80211: scan STA completion status=0x%02x core_band=%u chip_id=%u\n",
					     sc.status, sc.core_band, sc.chip_id);
		}
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
/* Post the vendor VAP start order before the first scan request.  This is
 * OFF by default; the bench enables vap_start_first=1 and the provider
 * decides whether the wire family is WAL group 2 (vendor order) or the
 * g0 id1 config-event keys.  A builder/post failure aborts the scan: the
 * scan message is never sent for a VAP that was not started. */
/* wifi-ap-start lane (task-121, v7): the vendor VAP base must be posted
 * exactly once per module load before any frame that resolves the firmware VAP
 * object.  The AP set-SSID worker (0x82878) and the AP-config worker (0x84DA4)
 * both dereference module_ctx+0x190 WITHOUT a NULL check: posting an AP frame
 * before the mode-1 VAP base exists takes a firmware data abort (exception
 * mask 0x18, golden page lost). */
static bool hi5622_vap_base_posted;

static int hi5622_scan_post_vap_base_once(struct hi5622_pci *ctx,
					  const struct hi5622_frame_ops *ops,
					  u8 iftype, u8 band, u8 module)
{
	struct hi5622_vap_start_params vp;
	u8 *wire;
	size_t len = 0;
	int rc;

	if (hi5622_vap_base_posted)
		return 0;
	if ((ops->caps & (HI5622_FRAME_CAP_VAP_ADD |
			  HI5622_FRAME_CAP_VAP_START)) !=
	    (HI5622_FRAME_CAP_VAP_ADD | HI5622_FRAME_CAP_VAP_START))
		return -EOPNOTSUPP;
	if (!ops->build_vap_add || !ops->build_vap_start)
		return -EOPNOTSUPP;
	wire = kmalloc(HI5622_FRAME_MAX_TOTAL, GFP_KERNEL);
	if (!wire)
		return -ENOMEM;
	memset(&vp, 0, sizeof(vp));
	vp.iftype = iftype;
	vp.band = band;
	vp.module = module;
	vp.seq = 1;
	rc = ops->build_vap_add(wire, HI5622_FRAME_MAX_TOTAL, &len, &vp);
	if (rc)
		goto out;
	rc = hi5622_frame_post(ctx, wire, len, "vap_add", NULL);
	if (rc)
		goto out;
	len = 0;
	vp.seq = 2;
	rc = ops->build_vap_start(wire, HI5622_FRAME_MAX_TOTAL, &len, &vp);
	if (rc)
		goto out;
	rc = hi5622_frame_post(ctx, wire, len, "vap_start", NULL);
	if (!rc)
		hi5622_vap_base_posted = true;
out:
	kfree(wire);
	return rc;
}

/* Scan-path policy: the scan op posts the VAP base only when the bench
 * enabled vap_start_first; the AP path posts it unconditionally. */
static int hi5622_scan_post_vap_start(struct hi5622_pci *ctx,
				      const struct hi5622_frame_ops *ops,
				      u8 iftype, u8 band, u8 module)
{
	if (!hi5622_vap_start_first)
		return 0;
	return hi5622_scan_post_vap_base_once(ctx, ops, iftype, band,
					      module);
}

/* ---- task-133: multi-request ("batched") scan ------------------------- */

/* True when the CURRENT request/batch is done from the host's point of view:
 * a firmware success completion for it, or every channel it asked for was
 * reported plus the settle window.  A failed completion is not "done" so the
 * normal model tick can turn it into an honest abort. */
static bool hi5622_scan_batch_done(const struct hi5622_scan_model *m, u64 now_ms)
{
	if (m->phase != HI5622_SCAN_ACTIVE || m->abort_requested)
		return false;
	if (m->complete_seen && !m->complete_failed)
		return true;
	return hi5622_scan_coverage_ready(m, now_ms);
}

/* Post ONE g0 id0xC request for s->freqs[first .. first+n): start the scan
 * model for exactly that channel set and post the frame.  s->request must
 * already be owned by the scan.  On a post failure the request is released
 * (the caller does not call cfg80211_scan_done for the first batch, matching
 * the .scan op contract). */
static int hi5622_scan_send_batch(struct hi5622_scan *s, u32 first, u32 n)
{
	struct hi5622_cfg80211 *c = s->cfg;
	struct device *dev = wiphy_dev(c->wiphy);
	struct cfg80211_scan_request *req = s->request;
	const struct hi5622_frame_ops *ops;
	struct hi5622_scan_req_params *p = NULL;
	u8 *wire = NULL;
	size_t len = 0;
	u32 i, tmo;
	int rc;

	ops = hi5622_scan_need(c->wiphy, HI5622_FRAME_CAP_SCAN_REQ |
				       HI5622_FRAME_CAP_SCAN_PARSE, "scan");
	if (!ops)
		return -EOPNOTSUPP;
	if (!n || first + n > s->n_freqs)
		return -EINVAL;
	p = kzalloc(sizeof(*p), GFP_KERNEL);
	wire = kmalloc(HI5622_FRAME_MAX_TOTAL, GFP_KERNEL);
	if (!p || !wire) {
		rc = -ENOMEM;
		goto out;
	}
	p->user_id = (u8)s->user_id;
	p->iftype = (u8)(req && req->wdev ? req->wdev->iftype :
			 NL80211_IFTYPE_STATION);
	p->band = c->band;
	p->flags = req ? req->flags : 0;
	p->dwell_ms = hi5622_scan_pick_dwell(dev);
	p->n_ssids = req ? (u32)req->n_ssids : 0;
	for (i = 0; i < p->n_ssids; i++) {
		size_t l = req->ssids[i].ssid_len;

		if (l > HI5622_FRAME_MAX_SSID_LEN) {
			rc = -EINVAL;
			goto out;
		}
		p->ssid_len[i] = (u8)l;
		if (l)
			memcpy(p->ssid[i], req->ssids[i].ssid, l);
	}
	p->n_channels = n;
	for (i = 0; i < n; i++)
		p->freq[i] = s->freqs[first + i];
	p->ie_len = req ? (u32)req->ie_len : 0;
	if (p->ie_len && req->ie)
		memcpy(p->ie, req->ie, p->ie_len);
	rc = hi5622_scan_req_params_validate(p);
	if (rc) {
		dev_warn(dev,
			 "cfg80211: scan batch ch %u..%u failed validation (%d) - refused (no firmware message)\n",
			 first + 1u, first + n, rc);
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
	tmo = hi5622_scan_pick_timeout(n);
	mutex_lock(&s->lock);
	s->batch_first = first;
	s->batch_n = n;
	s->batch_pending = false;
	s->batch_due_ms = 0;
	rc = hi5622_scan_model_start(&s->model, hi5622_scan_now(), tmo);
	if (rc) {
		mutex_unlock(&s->lock);
		dev_warn(dev, "cfg80211: scan refused by the model (%d) - a scan is already in flight or not cleared\n",
			 rc);
		goto out;
	}
	rc = hi5622_scan_model_set_channels(&s->model, p->freq, n);
	if (rc) {
		hi5622_scan_model_finish(&s->model, HI5622_SCAN_FINISH_ABORT);
		hi5622_scan_model_clear(&s->model);
		mutex_unlock(&s->lock);
		dev_warn(dev, "cfg80211: scan channel plan rejected (%d) - request not posted\n",
			 rc);
		goto out;
	}
	s->scan_timeout_ms = tmo;
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
	s->batches_sent++;
	mutex_unlock(&s->lock);
	dev_info(dev,
		 "cfg80211: scan request batch %u/%u posted by provider '%s' (%u of %u channels, ch %u..%u, %u ssids, dwell %u ms, total %zu B); request window %u ms (%s), host completion on channel coverage%s\n",
		 s->batches_sent, s->batches_total, ops->name, n, s->n_freqs,
		 first + 1u, first + n, p->n_ssids,
		 p->dwell_ms ? p->dwell_ms : 60u, len, tmo,
		 hi5622_scan_timeout_ms ? "manual" : "auto",
		 s->batches_total > 1u ? ", chained multi-request scan" : "");
	rc = 0;
out:
	kfree(p);
	kfree(wire);
	return rc;
}

int hi5622_scan_op_scan(struct wiphy *wiphy,
			struct cfg80211_scan_request *request)
{
	struct hi5622_cfg80211 *c = wiphy_priv(wiphy);
	struct hi5622_scan *s = hi5622_scan_of(c);
	struct device *dev = wiphy_dev(wiphy);
	const struct hi5622_frame_ops *ops;
	u32 i, n, first_len;
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
	    request->n_channels > HI5622_SCAN_MAX_CHANNELS ||
	    request->ie_len > HI5622_FRAME_MAX_SCAN_IE) {
		dev_warn(dev,
			 "cfg80211: scan request out of bounds (ssids=%d ch=%u ie=%zu) - refused\n",
			 request->n_ssids, request->n_channels,
			 request->ie_len);
		return -EINVAL;
	}
	if (!request->n_channels) {
		dev_warn(dev, "cfg80211: scan request has no channels - refused\n");
		return -EINVAL;
	}
	if (request->ie_len) {
		/* g0 id0xC carries no IE field in the vendor STA builder ([V],
		 * wifi-vap-scan); stay fail-closed instead of dropping them. */
		dev_warn(dev,
			 "cfg80211: scan request carries %zu IE bytes but g0 id0xC has no IE field - -EOPNOTSUPP\n",
			 request->ie_len);
		return -EOPNOTSUPP;
	}
	mutex_lock(&s->lock);
	if (s->model.phase != HI5622_SCAN_IDLE) {
		mutex_unlock(&s->lock);
		dev_warn(dev, "cfg80211: scan refused - a scan is already in flight or not cleared\n");
		return -EBUSY;
	}
	n = request->n_channels;
	memset(s->freqs, 0, sizeof(s->freqs));
	for (i = 0; i < n; i++)
		s->freqs[i] = request->channels[i]->center_freq;
	s->n_freqs = n;
	/* Channel batching (task-133): the firmware was measured to stop after
	 * the first 6 requested channels, so one cfg80211 scan is covered by
	 * ceil(n / scan_req_batch) g0 id0xC requests.  scan_req_batch=0 (or any
	 * value >= n) keeps the legacy single-request form. */
	s->batch_param = hi5622_scan_req_batch;
	s->batches_total = hi5622_scan_batch_count(n, s->batch_param);
	s->total_seen = 0;
	s->total_seen_mask = 0;
	s->total_expected = n;
	s->batches_sent = 0;
	s->batch_pending = false;
	s->batch_due_ms = 0;
	s->batch_first = 0;
	s->batch_n = 0;
	s->request = request;
	first_len = hi5622_scan_batch_len(n, s->batch_param, 0u);
	mutex_unlock(&s->lock);
	/* The bench-validated VAP base ordering is kept: post the vendor VAP
	 * start order once before the first scan request (mode-1 ladder). */
	rc = hi5622_scan_post_vap_start(c->ctx, ops,
					(u8)request->wdev->iftype, c->band,
					(u8)hi5622_vap_start_module);
	if (rc) {
		mutex_lock(&s->lock);
		s->request = NULL;
		mutex_unlock(&s->lock);
		dev_warn(dev,
			 "cfg80211: VAP start sequence not posted (%d) - scan aborted, no scan message\n",
			 rc);
		return rc;
	}
	if (s->batches_total > 1u)
		dev_info(dev,
			 "cfg80211: scan is split into %u g0 id0xC requests of <=%u channels (chained multi-request scan, scan_req_batch=%u, gap %u ms)\n",
			 s->batches_total,
			 hi5622_scan_batch_size(n, s->batch_param),
			 s->batch_param, hi5622_scan_batch_gap_ms);
	rc = hi5622_scan_send_batch(s, 0u, first_len);
	if (rc) {
		mutex_lock(&s->lock);
		s->request = NULL;
		mutex_unlock(&s->lock);
		return rc;
	}
	schedule_delayed_work(&s->work, msecs_to_jiffies(HI5622_SCAN_POLL_MS));
	return 0;
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
	/* wifi-ap-start lane (task-121): provider hints from the cfg80211
	 * settings; the wire decision stays in the provider. */
	p->band = (u8)(set->chandef.chan->band == NL80211_BAND_5GHZ ? 1u : 0u);
	p->hidden_ssid = (u8)(set->hidden_ssid != NL80211_HIDDEN_SSID_NOT_IN_USE);
	p->auth_algs = (u8)set->auth_type;
	p->module = (u8)s->user_id;
	p->vap_id = 0u;
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
	dev_info(wdev_dev,
		 "cfg80211: start_ap reached (iftype=%d beacon_interval=%d dtim=%d head=%zu tail=%zu) - provider gate next\n",
		 (int)dev->ieee80211_ptr->iftype, settings->beacon_interval,
		 settings->dtim_period, settings->beacon.head_len,
		 settings->beacon.tail_len);
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
	/* wifi-ap-start lane (task-121, v7): the AP frames dereference the
	 * firmware VAP object at module_ctx+0x190.  The AP path ALWAYS posts the
	 * vendor mode-1 VAP base first (once per module load; the scan path
	 * still honours vap_start_first).  Without the VAP object the firmware
	 * worker NULL-derefs and the chip exception state is lost
	 * (power-cycle required). */
	rc = hi5622_scan_post_vap_base_once(c->ctx, ops,
					(u8)dev->ieee80211_ptr->iftype,
					c->band,
					(u8)hi5622_vap_start_module);
	if (rc) {
		dev_warn(wdev_dev,
			 "cfg80211: start_ap VAP base not posted (%d) - sequence aborted, no AP frame sent\n",
			 rc);
		goto out;
	}
	/* wifi-ap-start lane (task-121): the vendor AP start is a sequence.
	 * Post every frame the provider emits and stop on -ENODATA. */
	{
		static const char *const ap_seq_names[HI5622_AP_START_MAX_FRAMES] = {
			"ap_ssid", "ap_auth", "ap_cfg", "ap_vap_start",
		};
		unsigned int posted = 0;

		for (p->frame_index = 0;
		     p->frame_index < HI5622_AP_START_MAX_FRAMES;
		     p->frame_index++) {
			const char *label = ap_seq_names[p->frame_index];

			/* Carrier-aware label: index 1 is ap_cfg on the g0 carrier
			 * and ap_auth on the WAL carrier (v7 fixes the earlier
			 * mislabel that made dmesg say ap_auth for the 184-B
			 * AP-config frame). */
			if (ops->ap_frame_name) {
				const char *n = ops->ap_frame_name(
					(u8)p->frame_index);

				if (n)
					label = n;
			}

			len = 0;
			rc = ops->build_ap_start(wire, HI5622_FRAME_MAX_TOTAL,
						 &len, p);
			if (rc == -ENODATA) {
				rc = 0;
				break;
			}
			if (rc) {
				dev_warn(wdev_dev,
					 "cfg80211: start_ap builder '%s' refused sequence frame %u (rc=%d) - sequence aborted\n",
					 ops->name, p->frame_index, rc);
				rc = rc < 0 ? rc : -EIO;
				goto out;
			}
			if (!len) {
				rc = -EIO;
				goto out;
			}
			rc = hi5622_frame_post(c->ctx, wire, len, label, NULL);
			if (rc) {
				dev_warn(wdev_dev,
					 "cfg80211: start_ap sequence frame %u ('%s') not posted (%d) - sequence aborted\n",
					 p->frame_index, label, rc);
				goto out;
			}
			posted++;
			dev_info(wdev_dev,
				 "cfg80211: start_ap sequence frame %u/%u '%s' posted (%zu B, ssid_len=%u freq=%u MHz width=%u beacon=%u dtim=%u head=%u tail=%u)\n",
				 p->frame_index + 1u,
				 (unsigned int)HI5622_AP_START_MAX_FRAMES, label,
				 len, p->ssid_len, p->center_freq, p->width,
				 p->beacon_interval, p->dtim_period, p->head_len,
				 p->tail_len);
		}
		if (!posted) {
			dev_warn(wdev_dev,
				 "cfg80211: start_ap provider '%s' emitted no frame\n",
				 ops->name);
			rc = -EOPNOTSUPP;
			goto out;
		}
	}
	dev_info(wdev_dev,
		 "cfg80211: start_ap sequence posted to provider '%s' - EXPERIMENTAL: beaconing is a firmware effect; verify the VAP context readback\n",
		 ops->name);
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
	p.band = c->band;
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

int hi5622_vap_hcc_del(struct hi5622_cfg80211 *c, u16 user_id, const u8 *addr,
		       u8 band, u8 vap_id)
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
	p.band = band;
	p.vap_id = vap_id;
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
		      "scan layer: initialized=%d provider=%s caps=%s user_id=%u timeout_ms=%u (%s) dwell_ms=%u dropped_busy=%d\n",
		      s->initialized ? 1 : 0, ops ? ops->name : "-", caps,
		      s->user_id, s->scan_timeout_ms,
		      hi5622_scan_timeout_ms ? "manual" : "auto",
		      hi5622_scan_dwell_ms ? hi5622_scan_dwell_ms : 60u,
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
