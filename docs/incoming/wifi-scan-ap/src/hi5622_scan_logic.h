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
 * iw/netifd never hang.  5 s is longer than a 13-channel 2.4 GHz active scan
 * at the vendor's dwell time and shorter than the default cfg80211 caller
 * patience / hostapd bring-up timeout. */
#define HI5622_SCAN_TIMEOUT_MS		5000u
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

/* Classify one device->host frame.  Pure function; never touches state. */
enum hi5622_scan_event_class hi5622_scan_classify(u8 group, u16 id);

/* Apply one classified event; returns the step decision. */
enum hi5622_scan_step hi5622_scan_model_event(struct hi5622_scan_model *m,
					      enum hi5622_scan_event_class cls,
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
