/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_scan_logic.h - portable (host + kernel) scan / AP-start state model
 * for the Hi5622V100 clean-room full-MAC driver, M2/M3 wiring of the
 * wifi-cfg80211 lane (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (wifi-scan-ap lane, shared task task-113,
 * 2026-11-24).  No vendor source, object code, firmware byte, calibration
 * value or secret is reproduced here; every D2H id / evidence marker is
 * justified in ../REPORT.md.
 *
 * The model is deliberately free of kernel and cfg80211 dependencies so the
 * complete decision surface can be host-tested (tools/scan_logic_selftest.c):
 *   - when a scan may start (no scan in flight),
 *   - how a device->host frame is classified into "BSS result / scan complete /
 *     channel report / station response / not ours",
 *   - when the scan is finished or must be aborted (completion event,
 *     cfg80211 abort, bounded timeout),
 *   - the counters the sysfs status file and the bench ladder assert on.
 *
 * The kernel file hi5622_scan.c owns the cfg80211_scan_request lifetime, the
 * BSS reporting and the work queue; it never re-implements the decisions of
 * this model.
 */
#ifndef HI5622_SCAN_LOGIC_H
#define HI5622_SCAN_LOGIC_H

#include "hi5622_frames_api.h"

/* Bounded scan: the firmware gets one request; a scan that produces no
 * completion event within this window is reported ABORTED to cfg80211 so that
 * iw/netifd never hang.
 *
 * History: the wifi-scan-ap lane used a fixed 5 s bound and saw 6 of 13
 * channels; the wifi-scan-complete lane then sized the window from the channel
 * count using a ~830 ms/channel figure derived from that same 5 s run.  The
 * wifi-scan-tuning lane re-measured the actual firmware cadence on the
 * self-contained image (combined module, 2026-11-27): six channels are
 * reported in ~0.94 s (~156 ms/channel) and then the firmware idles, so the
 * "830 ms/channel" figure was an artefact of the host bound - the live
 * per-channel cost is ~156 ms.  The auto window is therefore now
 * BASE + n*PER_CHAN with a per-channel term of 250 ms (measured 156 ms + 60%
 * margin); it stays deliberately generous because a batch also has to wait
 * for the firmware to flush late BSS frames.
 *
 * A single request can in practice complete only the first N channels
 * (observed N=6), so hi5622_scan.c can split one cfg80211 scan into several
 * g0 id0xC requests ("batches"); the window below applies to one request /
 * one batch.  The scan_timeout_ms module parameter overrides the per-request
 * window; 0 keeps the auto policy. */
#define HI5622_SCAN_TIMEOUT_MS		5000u	/* legacy fixed value (kept) */
#define HI5622_SCAN_TIMEOUT_BASE_MS	1500u
#define HI5622_SCAN_TIMEOUT_PER_CHAN_MS	250u	/* measured ~156 + margin */
#define HI5622_SCAN_TIMEOUT_MAX_MS	120000u	/* was 45000 (task-133) */
/* Per-request channel batching: the firmware scan engine was measured to stop
 * after the first 6 requested channels (bench 2026-11-27, slots 0..5 ->
 * channels 1..6, then no frame for 14 s).  The host can chain several g0 id0xC
 * requests so that a 13-channel cfg80211 scan is covered by ceil(13/6)=3
 * requests.  DEFAULT_BATCH is the compiled default of the scan_req_batch
 * module parameter; 0 = one request (legacy behaviour). */
#define HI5622_SCAN_DEFAULT_BATCH	6u
#define HI5622_SCAN_MAX_BATCH		42u	/* builder channel slots */
#define HI5622_SCAN_BATCH_GAP_MS	300u	/* default inter-batch pause */
/* After every requested channel has been reported, keep collecting late BSS
 * events for this long before finishing the scan successfully (a firmware
 * completion event, when it exists, wins immediately). */
#define HI5622_SCAN_SETTLE_MS		500u
/* Channel bookkeeping bound (a single-band request: 13 / 19 channels; the
 * builder allows 42).  Not part of the wire format. */
#define HI5622_SCAN_MAX_CHANNELS	32u
/* Work-queue re-check period while a scan is in flight. */
#define HI5622_SCAN_POLL_MS		100u
/* Refuse unbounded accumulation (a runaway firmware must not grow state). */
#define HI5622_SCAN_MAX_RESULTS		256u

/* D2H frames the vendor scan implementation uses [I]:
 *   wifi-cfg80211 REPORT.md sec 5 frame table and hi5622_cfg80211_logic.c
 *   hi5622_msg_refs[]: g1 id15 = scan bss, g1 id16 = scan complete,
 *   g1 id17 = scan channel, g2 id2 = scan rsp (sta).  The payload layouts are
 *   [U] - this is exactly why the provider decoder exists and why a missing
 *   decoder fails closed instead of guessing offsets. */
#define HI5622_SCAN_D2H_GROUP_BSS	0x01u
#define HI5622_SCAN_D2H_ID_BSS		0x0fu
#define HI5622_SCAN_D2H_ID_COMPLETE	0x10u
#define HI5622_SCAN_D2H_ID_CHANNEL	0x11u
#define HI5622_SCAN_D2H_GROUP_STA	0x02u
#define HI5622_SCAN_D2H_ID_STA_RSP	0x02u

enum hi5622_scan_phase {
	HI5622_SCAN_IDLE = 0,	/* no request owned by the driver */
	HI5622_SCAN_ACTIVE,	/* request posted, collecting events */
	HI5622_SCAN_FINISHED,	/* scan_done reported; waiting for clear */
};

enum hi5622_scan_event_class {
	HI5622_SCAN_EV_NONE = 0,
	HI5622_SCAN_EV_BSS,	/* g1 id15: one BSS result */
	HI5622_SCAN_EV_COMPLETE,	/* g1 id16: scan finished */
	HI5622_SCAN_EV_CHANNEL,	/* g1 id17: per-channel report */
	HI5622_SCAN_EV_STA_RSP,	/* g2 id2: station scan response */
};

/* Decision returned by event/tick: keep waiting, or finish the cfg80211 scan
 * as success / abort.  FINISH_* also latches the model phase. */
enum hi5622_scan_step {
	HI5622_SCAN_WAIT = 0,
	HI5622_SCAN_FINISH_OK,
	HI5622_SCAN_FINISH_ABORT,
};

struct hi5622_scan_model {
	enum hi5622_scan_phase	phase;
	bool			abort_requested;
	bool			complete_seen;
	bool			complete_failed;	/* completion reported failure */
	bool			finish_reported;	/* kernel called scan_done */
	u64			started_ms;
	u64			deadline_ms;
	u32			timeout_ms;
	u32			starts;
	u32			finishes_ok;
	u32			finishes_abort;
	u32			aborts_requested;
	u32			timeouts;
	u32			results;		/* BSS events accepted */
	u32			result_overflow;
	u32			bss_events;
	u32			complete_events;
	u32			complete_failures;
	u32			channel_events;
	u32			sta_events;
	u32			unknown_events;
	u32			events_when_idle;
	u32			dropped_busy;		/* delivery lost to trylock */
	u32			parse_errors;
	u32			invalid_results;
	u32			inform_failures;
	u8			complete_reason;
	char			last_error[64];
	/* scan-complete lane (2026-11-26): per-channel progress.  A firmware scan
	 * reports the channels it scanned as g1 id17 events; once every requested
	 * channel has been reported the scan is logically complete even if the
	 * firmware completion event (g1 id16 / g2 id2) never arrives. */
	u16			chan_freq[HI5622_SCAN_MAX_CHANNELS];
	u32			chan_total;
	u64			chan_seen_mask;
	u32			chan_seen;
	u64			last_chan_ms;
	u64			last_bss_ms;	/* last BSS event (late-BSS settle) */
	u64			coverage_ms;	/* all requested channels seen at */
	u32			coverage_completes;	/* finishes_ok via channel coverage */
	char			finish_reason[24];	/* event / coverage / timeout / ... */
};

void hi5622_scan_model_init(struct hi5622_scan_model *m);

/* Begin a scan.  0 = accepted; -EBUSY = a scan is already in flight / not
 * cleared; the caller must then NOT call cfg80211_scan_done(). */
int hi5622_scan_model_start(struct hi5622_scan_model *m, u64 now_ms,
			    u32 timeout_ms);

/* cfg80211 abort_scan: request/observe a cancellation (idempotent). */
int hi5622_scan_model_abort(struct hi5622_scan_model *m);

/* Latch the finish (called once, under the scan lock). */
void hi5622_scan_model_finish(struct hi5622_scan_model *m,
			      enum hi5622_scan_step step);

/* Clear a finished scan back to IDLE (after scan_done was reported). */
void hi5622_scan_model_clear(struct hi5622_scan_model *m);

bool hi5622_scan_model_active(const struct hi5622_scan_model *m);

/* Auto scan window for one request: BASE + n_channels * PER_CHAN, clamped to
 * MAX.  n_channels == 0 (unknown) is treated as a 13-channel 2.4 GHz scan
 * (the bench band).  Pure function; host-tested. */
u32 hi5622_scan_timeout_auto(u32 n_channels);

/* Same auto window with an optional per-channel cost override in ms
 * (0 = the compiled HI5622_SCAN_TIMEOUT_PER_CHAN_MS default).  The kernel
 * layer feeds the scan_chan_time_ms module parameter here; the portable
 * arithmetic stays host-testable. */
u32 hi5622_scan_timeout_for(u32 n_channels, u32 chan_time_ms);

/* ---- per-request channel batching (task-133) ----------------------------
 * The firmware scan engine was measured to stop after the first 6 requested
 * channels (see REPORT.md sec 2/3).  The host therefore may split one
 * cfg80211 scan into several g0 id0xC requests.  These three helpers are the
 * complete, pure policy surface:
 *   hi5622_scan_batch_size(n, param)  - effective batch size (0/overflow -> n)
 *   hi5622_scan_batch_count(n, size)  - ceil(n / size), 0 for an empty request
 *   hi5622_scan_batch_len(n, size, i) - number of channels in batch i
 * and hi5622_scan_coverage_ready() is the host-side "this request is done"
 * predicate (every channel of the request reported, then a settle window
 * after the LAST channel or BSS event so late g1 id15 frames are collected). */
u32 hi5622_scan_batch_size(u32 n_channels, u32 batch_param);
u32 hi5622_scan_batch_count(u32 n_channels, u32 batch_param);
u32 hi5622_scan_batch_len(u32 n_channels, u32 batch_param, u32 batch_index);

/* True when the active request has reported every channel it asked for and
 * the settle window has elapsed - i.e. the kernel layer may post the next
 * batch instead of waiting for the model tick to finish the scan.  Never
 * returns true for a finished/abort-requested model or an empty plan. */
bool hi5622_scan_coverage_ready(const struct hi5622_scan_model *m, u64 now_ms);

/* Record the requested channel/frequency set of the active scan (called by the
 * kernel layer right after hi5622_scan_model_start(), under the same lock).
 * Storing the plan is what turns "some channel events arrived" into the
 * well-defined completion rule "every requested channel was reported".
 * Returns 0, -EINVAL when n > HI5622_SCAN_MAX_CHANNELS, -EINVAL for a NULL/
 * zero freq entry.  Never changes the wire request. */
int hi5622_scan_model_set_channels(struct hi5622_scan_model *m,
				   const u16 *freqs, u32 n);

/* Apply one g1 id17 per-channel report (freq = 0 when the decoder could not
 * derive the frequency: it still counts as a channel event).  Returns
 * HI5622_SCAN_WAIT or HI5622_SCAN_FINISH_ABORT on an abort request; the
 * success decision is the bounded tick's job (coverage + settle window). */
enum hi5622_scan_step hi5622_scan_model_channel(struct hi5622_scan_model *m,
						u16 freq, u64 now_ms);

/* Classify one device->host frame.  Pure function; never touches state. */
enum hi5622_scan_event_class hi5622_scan_classify(u8 group, u16 id);

/* Apply one classified event; returns the step decision. */
enum hi5622_scan_step hi5622_scan_model_event(struct hi5622_scan_model *m,
					      enum hi5622_scan_event_class cls,
					      u64 now_ms);

/* Firmware completion (g1 id16 hmac_scan_proc_scan_comp_event, and g2 id2
 * wal_scan_comp_proc_sta as the vendor's cfg80211 feed/completion path [V]).
 * Records the status byte and decides success (FINISH_OK) vs failure
 * (FINISH_ABORT, counted in complete_failures - not a timeout). */
enum hi5622_scan_step hi5622_scan_model_complete(struct hi5622_scan_model *m,
						u8 status, u8 aborted,
						u64 now_ms);

/* Bounded timeout / abort check; returns the step decision. */
enum hi5622_scan_step hi5622_scan_model_tick(struct hi5622_scan_model *m,
					     u64 now_ms);

/* Counter/error notes used by the kernel layer (all bounded, no IO). */
void hi5622_scan_note_dropped_busy(struct hi5622_scan_model *m);
void hi5622_scan_note_parse_error(struct hi5622_scan_model *m);
void hi5622_scan_note_invalid_result(struct hi5622_scan_model *m);
void hi5622_scan_note_inform_failure(struct hi5622_scan_model *m);
void hi5622_scan_note_error(struct hi5622_scan_model *m, const char *what,
			    int err);

/* Human-readable one-screen status (sysfs): returns the byte count written. */
int hi5622_scan_status_line(const struct hi5622_scan_model *m, char *buf,
			    size_t len);

/* Name of the phase/step (dmesg/selftest). */
const char *hi5622_scan_phase_name(enum hi5622_scan_phase p);
const char *hi5622_scan_step_name(enum hi5622_scan_step s);
const char *hi5622_scan_event_name(enum hi5622_scan_event_class c);

#endif /* HI5622_SCAN_LOGIC_H */
