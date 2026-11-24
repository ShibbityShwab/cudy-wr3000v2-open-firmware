// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_scan_logic.c - portable scan/AP-start state model and frame-provider
 * contract validation for the Hi5622V100 clean-room full-MAC driver
 * (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (wifi-scan-ap lane, shared task task-113,
 * 2026-11-24).  No vendor source, object code, firmware byte, calibration
 * value or secret is reproduced.  Host-tested by tools/scan_logic_selftest.c;
 * the kernel layer (hi5622_scan.c) contains no duplicated decision logic.
 */
#include "hi5622_scan_logic.h"

#ifdef __KERNEL__
#include <linux/kernel.h>
#endif

/* =====================================================================
 * Frame-provider contract validation
 * ===================================================================*/

const char *hi5622_frame_cap_name(u32 cap)
{
	switch (cap) {
	case HI5622_FRAME_CAP_SCAN_REQ:		return "scan_req";
	case HI5622_FRAME_CAP_SCAN_PARSE:	return "scan_parse";
	case HI5622_FRAME_CAP_USER_ADD:		return "user_add";
	case HI5622_FRAME_CAP_USER_ADD_NOTIFY:	return "user_add_notify";
	case HI5622_FRAME_CAP_USER_DEL:		return "user_del";
	case HI5622_FRAME_CAP_AP_START:		return "ap_start";
	case HI5622_FRAME_CAP_AP_STOP:		return "ap_stop";
	case HI5622_FRAME_CAP_VAP_ADD:		return "vap_add";
	case HI5622_FRAME_CAP_VAP_START:	return "vap_start";
	case HI5622_FRAME_CAP_CHAN_CFG:		return "chan_cfg";
	case HI5622_FRAME_CAP_KEY_ADD:		return "key_add";
	case HI5622_FRAME_CAP_KEY_DEL:		return "key_del";
	case HI5622_FRAME_CAP_KEY_DEFAULT:	return "key_default";
	default:				return "?";
	}
}

int hi5622_frame_caps_format(u32 caps, char *buf, size_t len)
{
	size_t pos = 0;
	u32 bit;
	int n;

	if (!buf || !len)
		return -EINVAL;
	buf[0] = '\0';
	if (!caps) {
		n = snprintf(buf, len, "none");
		return n < 0 ? n : (int)((size_t)n < len ? (size_t)n : len - 1);
	}
	for (bit = 1u; bit && bit <= HI5622_FRAME_CAP_KEY_DEFAULT; bit <<= 1) {
		const char *name;

		if (!(caps & bit))
			continue;
		name = hi5622_frame_cap_name(bit);
		n = snprintf(buf + pos, len - pos, "%s%s",
			     pos ? "," : "", name);
		if (n < 0)
			return n;
		pos += (size_t)n;
		if (pos >= len)
			return (int)(len - 1);
	}
	return (int)pos;
}

int hi5622_frame_ops_validate(const struct hi5622_frame_ops *ops)
{
	if (!ops || !ops->name || !ops->name[0])
		return -EINVAL;
	if (!ops->caps || (ops->caps & ~HI5622_FRAME_CAPS_ALL))
		return -EINVAL;
	if ((ops->caps & HI5622_FRAME_CAP_SCAN_REQ) && !ops->build_scan_req)
		return -EINVAL;
	if ((ops->caps & HI5622_FRAME_CAP_SCAN_PARSE) && !ops->parse_scan_result)
		return -EINVAL;
	if ((ops->caps & HI5622_FRAME_CAP_USER_ADD) && !ops->build_user_add)
		return -EINVAL;
	if ((ops->caps & HI5622_FRAME_CAP_USER_ADD_NOTIFY) &&
	    !ops->build_user_add_notify)
		return -EINVAL;
	if ((ops->caps & HI5622_FRAME_CAP_USER_DEL) && !ops->build_user_del)
		return -EINVAL;
	if ((ops->caps & HI5622_FRAME_CAP_AP_START) && !ops->build_ap_start)
		return -EINVAL;
	if ((ops->caps & HI5622_FRAME_CAP_AP_STOP) && !ops->build_ap_stop)
		return -EINVAL;
	if ((ops->caps & HI5622_FRAME_CAP_VAP_ADD) && !ops->build_vap_add)
		return -EINVAL;
	if ((ops->caps & HI5622_FRAME_CAP_VAP_START) && !ops->build_vap_start)
		return -EINVAL;
	if ((ops->caps & HI5622_FRAME_CAP_CHAN_CFG) && !ops->build_chan_cfg)
		return -EINVAL;
	if ((ops->caps & HI5622_FRAME_CAP_KEY_ADD) && !ops->build_key_add)
		return -EINVAL;
	if ((ops->caps & HI5622_FRAME_CAP_KEY_DEL) && !ops->build_key_del)
		return -EINVAL;
	if ((ops->caps & HI5622_FRAME_CAP_KEY_DEFAULT) && !ops->build_key_default)
		return -EINVAL;
	return 0;
}

/* =====================================================================
 * Neutral parameter validation (bounds only; no policy guesses)
 * ===================================================================*/

static bool hi5622_mac_is_unicast(const u8 *mac)
{
	return mac && !(mac[0] & 0x01u) && (mac[0] | mac[1] | mac[2] |
					    mac[3] | mac[4] | mac[5]);
}

static bool hi5622_freq_supported(u16 freq)
{
	/* The two endpoints advertise 2.4 GHz ch 1..13 and the LU-plan 5 GHz
	 * channels (hi5622_cfg80211.c band tables); accept exactly those
	 * centre frequencies and reject everything else instead of letting a
	 * bogus channel reach the firmware. */
	const u16 g2[] = { 2412, 2417, 2422, 2427, 2432, 2437, 2442, 2447,
			   2452, 2457, 2462, 2467, 2472 };
	const u16 g5[] = { 5180, 5200, 5220, 5240, 5260, 5280, 5300, 5320,
			   5500, 5520, 5540, 5560, 5580, 5600, 5620, 5640,
			   5660, 5680, 5700 };
	size_t i;

	for (i = 0; i < sizeof(g2) / sizeof(g2[0]); i++)
		if (freq == g2[i])
			return true;
	for (i = 0; i < sizeof(g5) / sizeof(g5[0]); i++)
		if (freq == g5[i])
			return true;
	return false;
}

int hi5622_scan_req_params_validate(const struct hi5622_scan_req_params *p)
{
	u32 i;

	if (!p)
		return -EINVAL;
	if (p->band > 1u)
		return -EINVAL;
	/* dwell 0 = provider/vendor default (60); otherwise stay inside the
	 * vendor's decoded 20/40/60 family so a bench sweep cannot put an
	 * unvalidated value on the wire. */
	if (p->dwell_ms && (p->dwell_ms < 20u || p->dwell_ms > 200u))
		return -EINVAL;
	if (p->n_ssids > HI5622_FRAME_MAX_SSIDS)
		return -EINVAL;
	if (p->n_channels > HI5622_FRAME_MAX_CHANNELS)
		return -EINVAL;
	if (p->ie_len > HI5622_FRAME_MAX_SCAN_IE)
		return -EINVAL;
	for (i = 0; i < p->n_ssids; i++)
		if (p->ssid_len[i] > HI5622_FRAME_MAX_SSID_LEN)
			return -EINVAL;
	for (i = 0; i < p->n_channels; i++)
		if (!hi5622_freq_supported(p->freq[i]))
			return -EINVAL;
	/* A hidden-SSID scan may carry a zero-length SSID, but never a
	 * partially filled one. */
	return 0;
}

int hi5622_vap_add_params_validate(const struct hi5622_vap_add_params *p)
{
	if (!p)
		return -EINVAL;
	if (!p->name[0] || memchr(p->name, '\0', sizeof(p->name)) == NULL)
		return -EINVAL;
	if (!hi5622_mac_is_unicast(p->mac))
		return -EINVAL;
	if (p->user_id > HI5622_FRAME_MAX_USER_ID)
		return -EINVAL;
	if (p->band > 1u)
		return -EINVAL;	/* only the two PCIe-endpoint bands exist */
	if (p->iftype != 2u && p->iftype != 3u && p->iftype != 6u)
		return -EINVAL;	/* STA / AP / MONITOR: the advertised modes */
	return 0;
}

int hi5622_vap_del_params_validate(const struct hi5622_vap_del_params *p)
{
	if (!p)
		return -EINVAL;
	if (p->user_id > 0x0fffu)
		return -EINVAL;
	if (p->band > 1u)
		return -EINVAL;
	if (p->user_id != 0x0fffu && !hi5622_mac_is_unicast(p->mac))
		return -EINVAL;
	return 0;
}

int hi5622_ap_start_params_validate(const struct hi5622_ap_start_params *p)
{
	if (!p)
		return -EINVAL;
	if (!p->ssid_len || p->ssid_len > HI5622_FRAME_MAX_SSID_LEN)
		return -EINVAL;
	if (p->iftype != 3u)
		return -EINVAL;	/* AP only (hostapd path) */
	if (p->user_id > HI5622_FRAME_MAX_USER_ID)
		return -EINVAL;
	if (!hi5622_freq_supported(p->center_freq))
		return -EINVAL;
	if (p->width > 8u)
		return -EINVAL;
	if (!p->beacon_interval)
		return -EINVAL;
	if (!p->dtim_period)
		return -EINVAL;
	if (p->privacy > 1u)
		return -EINVAL;
	if (p->auth_type > 6u)
		return -EINVAL;
	if (p->head_len > HI5622_FRAME_MAX_BCN_HEAD ||
	    p->tail_len > HI5622_FRAME_MAX_BCN_TAIL)
		return -EINVAL;
	if (p->channel > 196u)
		return -EINVAL;	/* highest 5 GHz channel number */
	/* A beacon head shorter than the 24-byte 802.11 MAC header plus the
	 * 12-byte beacon fixed fields is not a beacon. */
	if (p->head_len && p->head_len < 36u)
		return -EINVAL;
	return 0;
}

int hi5622_ap_stop_params_validate(const struct hi5622_ap_stop_params *p)
{
	if (!p)
		return -EINVAL;
	if (p->iftype != 3u)
		return -EINVAL;
	if (p->user_id > HI5622_FRAME_MAX_USER_ID)
		return -EINVAL;
	if (p->center_freq && !hi5622_freq_supported(p->center_freq))
		return -EINVAL;
	return 0;
}

int hi5622_scan_result_validate(const struct hi5622_scan_result *r)
{
	if (!r)
		return -EINVAL;
	if (!hi5622_mac_is_unicast(r->bssid))
		return -EINVAL;
	if (r->freq == 0)
		return -EINVAL;	/* cfg80211 needs a channel pointer */
	if (!hi5622_freq_supported(r->freq))
		return -EINVAL;
	if (r->ie_len > HI5622_SCAN_MAX_IE)
		return -EINVAL;
	if (r->ssid_len > HI5622_FRAME_MAX_SSID_LEN)
		return -EINVAL;
	if (r->frame_type > HI5622_SCAN_FTYPE_PRESP)
		return -EINVAL;
	/* cfg80211 mBm convention; anything outside -150..+5 dBm is garbage. */
	if (r->signal_mbm < -15000 || r->signal_mbm > 5000)
		return -EINVAL;
	return 0;
}

/* =====================================================================
 * Scan model
 * ===================================================================*/

void hi5622_scan_model_init(struct hi5622_scan_model *m)
{
	if (!m)
		return;
	memset(m, 0, sizeof(*m));
	m->phase = HI5622_SCAN_IDLE;
	m->timeout_ms = HI5622_SCAN_TIMEOUT_MS;
}

bool hi5622_scan_model_active(const struct hi5622_scan_model *m)
{
	return m && m->phase == HI5622_SCAN_ACTIVE;
}

int hi5622_scan_model_start(struct hi5622_scan_model *m, u64 now_ms,
			    u32 timeout_ms)
{
	if (!m)
		return -EINVAL;
	if (m->phase == HI5622_SCAN_ACTIVE)
		return -EBUSY;
	/* A FINISHED model must be cleared by the caller after scan_done; a new
	 * request before then would race the previous request pointer. */
	if (m->phase != HI5622_SCAN_IDLE)
		return -EBUSY;
	m->phase = HI5622_SCAN_ACTIVE;
	m->abort_requested = false;
	m->complete_seen = false;
	m->complete_failed = false;
	m->finish_reported = false;
	m->complete_reason = 0;
	m->started_ms = now_ms;
	m->timeout_ms = timeout_ms ? timeout_ms : HI5622_SCAN_TIMEOUT_MS;
	m->deadline_ms = now_ms + m->timeout_ms;
	m->chan_total = 0;
	m->chan_seen_mask = 0;
	m->chan_seen = 0;
	m->last_chan_ms = 0;
	m->last_bss_ms = 0;
	m->coverage_ms = 0;
	m->finish_reason[0] = '\0';
	m->starts++;
	m->last_error[0] = '\0';
	return 0;
}

u32 hi5622_scan_timeout_auto(u32 n_channels)
{
	return hi5622_scan_timeout_for(n_channels, 0u);
}

u32 hi5622_scan_timeout_for(u32 n_channels, u32 chan_time_ms)
{
	u64 t;

	if (!n_channels)
		n_channels = 13u;	/* the bench band: 2.4 GHz channels 1..13 */
	if (!chan_time_ms)
		chan_time_ms = HI5622_SCAN_TIMEOUT_PER_CHAN_MS;
	t = (u64)HI5622_SCAN_TIMEOUT_BASE_MS +
	    (u64)n_channels * (u64)chan_time_ms;
	if (t < HI5622_SCAN_TIMEOUT_BASE_MS)
		t = HI5622_SCAN_TIMEOUT_BASE_MS;
	if (t > HI5622_SCAN_TIMEOUT_MAX_MS)
		t = HI5622_SCAN_TIMEOUT_MAX_MS;
	return (u32)t;
}

/* ---- per-request channel batching (task-133) --------------------------- */
u32 hi5622_scan_batch_size(u32 n_channels, u32 batch_param)
{
	if (!batch_param || batch_param >= n_channels ||
	    batch_param > HI5622_SCAN_MAX_BATCH)
		return n_channels;
	return batch_param;
}

u32 hi5622_scan_batch_count(u32 n_channels, u32 batch_param)
{
	u32 size;

	if (!n_channels)
		return 0u;
	size = hi5622_scan_batch_size(n_channels, batch_param);
	if (!size)
		return 0u;
	return (n_channels + size - 1u) / size;
}

u32 hi5622_scan_batch_len(u32 n_channels, u32 batch_param, u32 batch_index)
{
	u32 size, count, first;

	count = hi5622_scan_batch_count(n_channels, batch_param);
	if (batch_index >= count)
		return 0u;
	size = hi5622_scan_batch_size(n_channels, batch_param);
	first = batch_index * size;
	if (first >= n_channels)
		return 0u;
	return (n_channels - first) < size ? (n_channels - first) : size;
}

bool hi5622_scan_coverage_ready(const struct hi5622_scan_model *m, u64 now_ms)
{
	if (!m || m->phase != HI5622_SCAN_ACTIVE || m->abort_requested)
		return false;
	u64 last;

	if (!m->chan_total || m->chan_seen < m->chan_total || !m->coverage_ms)
		return false;
	last = m->coverage_ms;
	if (m->last_bss_ms > last)
		last = m->last_bss_ms;
	return now_ms - last >= HI5622_SCAN_SETTLE_MS;
}

int hi5622_scan_model_set_channels(struct hi5622_scan_model *m,
				   const u16 *freqs, u32 n)
{
	u32 i;

	if (!m)
		return -EINVAL;
	if (n > HI5622_SCAN_MAX_CHANNELS)
		return -EINVAL;
	if (n && !freqs)
		return -EINVAL;
	if (m->phase != HI5622_SCAN_ACTIVE)
		return -EINVAL;
	/* validate the whole plan before touching state: a rejected plan must
	 * not silently drop the one already set for the active scan */
	for (i = 0; i < n; i++)
		if (!freqs[i])
			return -EINVAL;
	m->chan_total = 0;
	m->chan_seen = 0;
	m->chan_seen_mask = 0;
	m->coverage_ms = 0;
	for (i = 0; i < n; i++)
		m->chan_freq[i] = freqs[i];
	m->chan_total = n;
	return 0;
}

int hi5622_scan_model_abort(struct hi5622_scan_model *m)
{
	if (!m)
		return -EINVAL;
	if (m->phase != HI5622_SCAN_ACTIVE)
		return 0;	/* nothing to abort: cfg80211 tolerates this */
	m->abort_requested = true;
	m->last_error[0] = '\0';
	return 0;
}

void hi5622_scan_model_finish(struct hi5622_scan_model *m,
			      enum hi5622_scan_step step)
{
	if (!m)
		return;
	if (m->phase != HI5622_SCAN_ACTIVE)
		return;
	m->phase = HI5622_SCAN_FINISHED;
	m->finish_reason[0] = '\0';
	if (step == HI5622_SCAN_FINISH_OK) {
		m->finishes_ok++;
		if (m->complete_seen)
			snprintf(m->finish_reason, sizeof(m->finish_reason),
				 "complete-event");
		else {
			m->coverage_completes++;
			snprintf(m->finish_reason, sizeof(m->finish_reason),
				 "channel-coverage");
		}
	} else {
		m->finishes_abort++;
		if (m->abort_requested) {
			m->aborts_requested++;
			snprintf(m->finish_reason, sizeof(m->finish_reason),
				 "abort-request");
		} else if (m->complete_seen) {
			snprintf(m->finish_reason, sizeof(m->finish_reason),
				 "complete-failed");
		} else {
			m->timeouts++;
			snprintf(m->finish_reason, sizeof(m->finish_reason),
				 "host-timeout");
		}
	}
}

void hi5622_scan_model_clear(struct hi5622_scan_model *m)
{
	if (!m)
		return;
	if (m->phase == HI5622_SCAN_FINISHED) {
		m->phase = HI5622_SCAN_IDLE;
		m->abort_requested = false;
		m->complete_seen = false;
		m->complete_failed = false;
		m->finish_reported = false;
	}
}

enum hi5622_scan_event_class hi5622_scan_classify(u8 group, u16 id)
{
	if (group == HI5622_SCAN_D2H_GROUP_BSS) {
		switch (id) {
		case HI5622_SCAN_D2H_ID_BSS:		return HI5622_SCAN_EV_BSS;
		case HI5622_SCAN_D2H_ID_COMPLETE:	return HI5622_SCAN_EV_COMPLETE;
		case HI5622_SCAN_D2H_ID_CHANNEL:	return HI5622_SCAN_EV_CHANNEL;
		default:				return HI5622_SCAN_EV_NONE;
		}
	}
	if (group == HI5622_SCAN_D2H_GROUP_STA &&
	    id == HI5622_SCAN_D2H_ID_STA_RSP)
		return HI5622_SCAN_EV_STA_RSP;
	return HI5622_SCAN_EV_NONE;
}

enum hi5622_scan_step hi5622_scan_model_event(struct hi5622_scan_model *m,
					      enum hi5622_scan_event_class cls,
					      u64 now_ms)
{
	if (!m)
		return HI5622_SCAN_WAIT;
	if (m->phase != HI5622_SCAN_ACTIVE) {
		m->events_when_idle++;
		return HI5622_SCAN_WAIT;
	}
	if (m->abort_requested)
		return HI5622_SCAN_FINISH_ABORT;
	switch (cls) {
	case HI5622_SCAN_EV_BSS:
		m->bss_events++;
		m->last_bss_ms = now_ms;
		if (m->results < HI5622_SCAN_MAX_RESULTS)
			m->results++;
		else
			m->result_overflow++;
		return HI5622_SCAN_WAIT;
	case HI5622_SCAN_EV_COMPLETE:
		m->complete_events++;
		m->complete_seen = true;
		return HI5622_SCAN_FINISH_OK;
	case HI5622_SCAN_EV_CHANNEL:
		m->channel_events++;
		return HI5622_SCAN_WAIT;
	case HI5622_SCAN_EV_STA_RSP:
		m->sta_events++;
		/* g2 id2 wal_scan_comp_proc_sta is the vendor's WAL scan completion
		 * and cfg80211 feed point [V]: treat it as a completion candidate
		 * exactly like g1 id16. */
		m->complete_seen = true;
		return HI5622_SCAN_FINISH_OK;
	default:
		m->unknown_events++;
		return HI5622_SCAN_WAIT;
	}
	(void)now_ms;
}

enum hi5622_scan_step hi5622_scan_model_complete(struct hi5622_scan_model *m,
						u8 status, u8 aborted,
						u64 now_ms)
{
	if (!m)
		return HI5622_SCAN_WAIT;
	if (m->phase != HI5622_SCAN_ACTIVE) {
		m->events_when_idle++;
		return HI5622_SCAN_WAIT;
	}
	(void)now_ms;
	m->complete_events++;
	m->complete_seen = true;
	m->complete_reason = status;
	if (aborted) {
		m->complete_failed = true;
		m->complete_failures++;
		return HI5622_SCAN_FINISH_ABORT;
	}
	return HI5622_SCAN_FINISH_OK;
}

enum hi5622_scan_step hi5622_scan_model_tick(struct hi5622_scan_model *m,
					     u64 now_ms)
{
	if (!m || m->phase != HI5622_SCAN_ACTIVE)
		return HI5622_SCAN_WAIT;
	if (m->abort_requested)
		return HI5622_SCAN_FINISH_ABORT;
	/* An explicit firmware completion wins over every host-side heuristic:
	 * it is the only event that can report a firmware-side failure. */
	if (m->complete_seen)
		return m->complete_failed ? HI5622_SCAN_FINISH_ABORT :
					    HI5622_SCAN_FINISH_OK;
	/* Host-side completion: every requested channel was reported as scanned
	 * (g1 id17).  Wait a short settle window so BSS results that the firmware
	 * emits just after the last channel report are still collected, then
	 * finish successfully - without this the firmware's missing/unreached
	 * completion event would always turn a full 13-channel scan into
	 * scan_done(aborted=1).  For a multi-batch request the kernel layer checks
	 * hi5622_scan_coverage_ready() first and posts the next batch instead. */
	if (hi5622_scan_coverage_ready(m, now_ms))
		return HI5622_SCAN_FINISH_OK;
	/* Wrap-safe comparison: both values come from the same monotonic ms
	 * clock and a scan lives far below 2^32 ms. */
	if (now_ms >= m->deadline_ms)
		return HI5622_SCAN_FINISH_ABORT;
	return HI5622_SCAN_WAIT;
}

enum hi5622_scan_step hi5622_scan_model_channel(struct hi5622_scan_model *m,
						u16 freq, u64 now_ms)
{
	u32 i;

	if (!m)
		return HI5622_SCAN_WAIT;
	if (m->phase != HI5622_SCAN_ACTIVE) {
		m->events_when_idle++;
		return HI5622_SCAN_WAIT;
	}
	if (m->abort_requested)
		return HI5622_SCAN_FINISH_ABORT;
	m->channel_events++;
	m->last_chan_ms = now_ms;
	if (!freq)
		return HI5622_SCAN_WAIT;	/* event counted, channel not derivable */
	for (i = 0; i < m->chan_total; i++) {
		u64 bit;

		if (m->chan_freq[i] != freq)
			continue;
		bit = 1ull << i;
		if (!(m->chan_seen_mask & bit)) {
			m->chan_seen_mask |= bit;
			m->chan_seen++;
			if (m->chan_seen == m->chan_total && !m->coverage_ms)
				m->coverage_ms = now_ms;
		}
		break;
	}
	return HI5622_SCAN_WAIT;
}

void hi5622_scan_note_dropped_busy(struct hi5622_scan_model *m)
{
	if (m)
		m->dropped_busy++;
}

void hi5622_scan_note_parse_error(struct hi5622_scan_model *m)
{
	if (m)
		m->parse_errors++;
}

void hi5622_scan_note_invalid_result(struct hi5622_scan_model *m)
{
	if (m)
		m->invalid_results++;
}

void hi5622_scan_note_inform_failure(struct hi5622_scan_model *m)
{
	if (m)
		m->inform_failures++;
}

void hi5622_scan_note_error(struct hi5622_scan_model *m, const char *what,
			    int err)
{
	if (!m)
		return;
	snprintf(m->last_error, sizeof(m->last_error), "%s: %d",
		 what ? what : "error", err);
}

int hi5622_scan_status_line(const struct hi5622_scan_model *m, char *buf,
			    size_t len)
{
	int n;

	if (!m || !buf || !len)
		return -EINVAL;
	n = snprintf(buf, len,
		     "scan: phase=%s results=%u/%u bss=%u complete=%u channel=%u chans=%u/%u sta=%u unknown=%u idle_ev=%u overflow=%u\n"
		     "scan completions: reason=0x%02x failed=%u ; errors: dropped_busy=%u parse=%u invalid=%u inform=%u timeouts=%u aborts=%u last=%s\n"
		     "scan finish: last=%s ok=%u abort=%u coverage_ok=%u timeout_ms=%u\n",
		     hi5622_scan_phase_name(m->phase),
		     m->results, HI5622_SCAN_MAX_RESULTS,
		     m->bss_events, m->complete_events, m->channel_events,
		     m->chan_seen, m->chan_total,
		     m->sta_events, m->unknown_events, m->events_when_idle,
		     m->result_overflow, m->complete_reason,
		     m->complete_failures, m->dropped_busy, m->parse_errors,
		     m->invalid_results, m->inform_failures, m->timeouts,
		     m->finishes_abort,
		     m->last_error[0] ? m->last_error : "-",
		     m->finish_reason[0] ? m->finish_reason : "-",
		     m->finishes_ok, m->finishes_abort, m->coverage_completes,
		     m->timeout_ms);
	if (n < 0)
		return n;
	return (int)((size_t)n < len ? (size_t)n : len - 1);
}

const char *hi5622_scan_phase_name(enum hi5622_scan_phase p)
{
	switch (p) {
	case HI5622_SCAN_IDLE:		return "idle";
	case HI5622_SCAN_ACTIVE:	return "active";
	case HI5622_SCAN_FINISHED:	return "finished";
	default:			return "?";
	}
}

const char *hi5622_scan_step_name(enum hi5622_scan_step s)
{
	switch (s) {
	case HI5622_SCAN_WAIT:		return "wait";
	case HI5622_SCAN_FINISH_OK:	return "finish-ok";
	case HI5622_SCAN_FINISH_ABORT:	return "finish-abort";
	default:			return "?";
	}
}

const char *hi5622_scan_event_name(enum hi5622_scan_event_class c)
{
	switch (c) {
	case HI5622_SCAN_EV_NONE:	return "none";
	case HI5622_SCAN_EV_BSS:	return "bss";
	case HI5622_SCAN_EV_COMPLETE:	return "complete";
	case HI5622_SCAN_EV_CHANNEL:	return "channel";
	case HI5622_SCAN_EV_STA_RSP:	return "sta-rsp";
	default:			return "?";
	}
}

/* =====================================================================
 * AP-start ordering plan (wifi-beacon-static lane, task-140)
 * ===================================================================*/
unsigned int hi5622_beacon_plan(unsigned int enabled, unsigned int vap_start,
				u8 *out, unsigned int max)
{
	unsigned int n = 0;

	if (!enabled || !out || !max)
		return 0;
	if (n < max)
		out[n++] = HI5622_BEACON_STEP_VAP_ADD;
	if (n < max)
		out[n++] = HI5622_BEACON_STEP_CHAN;
	if (n < max)
		out[n++] = HI5622_BEACON_STEP_SSID;
	if (n < max)
		out[n++] = HI5622_BEACON_STEP_AP_CFG;
	if (vap_start && n < max)
		out[n++] = HI5622_BEACON_STEP_VAP_START;
	return n;
}

const char *hi5622_beacon_step_name(u8 step)
{
	switch (step) {
	case HI5622_BEACON_STEP_VAP_ADD:	return "vap_add";
	case HI5622_BEACON_STEP_CHAN:		return "ap_chan";
	case HI5622_BEACON_STEP_SSID:		return "ap_ssid";
	case HI5622_BEACON_STEP_AP_CFG:		return "ap_cfg";
	case HI5622_BEACON_STEP_VAP_START:	return "ap_vap_start";
	default:				return "none";
	}
}
