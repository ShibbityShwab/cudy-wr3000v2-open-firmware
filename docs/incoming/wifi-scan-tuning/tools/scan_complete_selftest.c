// SPDX-License-Identifier: GPL-2.0
/*
 * scan_complete_selftest.c - host-side (device-free, kernel-free) selftest for
 * the scan-complete lane (shared task task-123): the auto scan window, the
 * channel-coverage completion rule, and the beacon/probe-response IE decode
 * into cfg80211 BSS fields.
 *
 * ORIGINAL clean-room code.  All frames below are synthetic: the test builds
 * 802.11 beacon/probe-response bodies from the IEEE 802.11 element ids and the
 * protocol facts derived in ../REPORT.md; no vendor byte is used.
 *
 * Build/run: tools/build.sh step 1b (host cc, -Werror).
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_scan_logic.h"
#include "hi5622_scan_d2h.h"
#include "hi5622_vapscan_map.h"

static int checks;
static int failed;

#define CHECK(cond, msg)						\
	do {								\
		checks++;						\
		if (!(cond)) {						\
			failed++;					\
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__,	\
			       (msg));					\
		}							\
	} while (0)

#define CHECK_EQ(a, b, msg)						\
	do {								\
		long long _a = (long long)(a);				\
		long long _b = (long long)(b);				\
		checks++;						\
		if (_a != _b) {						\
			failed++;					\
			printf("FAIL %s:%d: %s (%lld != %lld)\n",	\
			       __FILE__, __LINE__, (msg), _a, _b);	\
		}							\
	} while (0)

/* ==================================================================
 * 1. synthetic 802.11 frame builder
 * ================================================================== */
static size_t put_u16(u8 *b, size_t o, u16 v)
{
	b[o] = (u8)(v & 0xffu);
	b[o + 1] = (u8)(v >> 8);
	return o + 2;
}

static size_t put_u64(u8 *b, size_t o, u64 v)
{
	o = put_u16(b, o, (u16)(v & 0xffffu));
	o = put_u16(b, o, (u16)((v >> 16) & 0xffffu));
	o = put_u16(b, o, (u16)((v >> 32) & 0xffffu));
	return put_u16(b, o, (u16)((v >> 48) & 0xffffu));
}

static size_t add_ie(u8 *b, size_t o, u8 id, u8 len)
{
	b[o] = id;
	b[o + 1] = len;
	return o + 2;
}

/* SSID element; ssid == NULL builds the hidden-SSID element (length 0). */
static size_t add_ssid(u8 *b, size_t o, const char *ssid)
{
	size_t n = ssid ? strlen(ssid) : 0;

	o = add_ie(b, o, 0, (u8)n);
	if (n)
		memcpy(b + o, ssid, n);
	return o + n;
}

/* DS parameter set (id 3). */
static size_t add_ds(u8 *b, size_t o, u8 chan)
{
	o = add_ie(b, o, 3, 1);
	b[o++] = chan;
	return o;
}

/* HT capabilities (id 45, exact 26-byte body; only the info field is read). */
static size_t add_ht_cap(u8 *b, size_t o, u16 info)
{
	size_t i;

	o = add_ie(b, o, 45, 26);
	o = put_u16(b, o, info);
	for (i = 0; i < 24; i++)
		b[o++] = 0;
	return o;
}

/* HT operation (id 61, 22-byte body): primary channel, HT param with the
 * secondary-channel offset in bits 0-1, then zero MCS/beamforming fields. */
static size_t add_ht_op(u8 *b, size_t o, u8 primary, u8 sec_off)
{
	size_t i;

	o = add_ie(b, o, 61, 22);
	b[o++] = primary;
	b[o++] = (u8)(sec_off & 0x3u);
	for (i = 0; i < 20; i++)
		b[o++] = 0;
	return o;
}

/* Minimal RSN element (version 1 only): enough for IE presence detection. */
static size_t add_rsn(u8 *b, size_t o)
{
	o = add_ie(b, o, 48, 2);
	return put_u16(b, o, 1);
}

/* WMM/WME vendor element 221: OUI 00:50:f2, type 2, one OUI subtype byte. */
static size_t add_wmm(u8 *b, size_t o)
{
	o = add_ie(b, o, 221, 4);
	b[o++] = 0x00;
	b[o++] = 0x50;
	b[o++] = 0xf2;
	b[o++] = 0x02;
	return o;
}

static size_t add_country(u8 *b, size_t o)
{
	o = add_ie(b, o, 7, 3);
	b[o++] = 'T';
	b[o++] = 'H';
	b[o++] = ' ';
	return o;
}

static size_t add_ext_cap(u8 *b, size_t o)
{
	o = add_ie(b, o, 127, 1);
	b[o++] = 0x00;
	return o;
}

#define TEST_BSSID_OCTET 0x02u

struct beacon_opts {
	bool	presp;		/* probe response instead of beacon */
	const char *ssid;	/* NULL = hidden (zero-length SSID element) */
	u8	ds_chan;	/* 0 = omit the DS element */
	bool	ht;		/* add HT cap + HT op */
	u8	ht_op_primary;
	u8	ht_sec_off;
	bool	rsn;
	bool	wmm;
	bool	country;
	bool	ext_cap;
	bool	trailer;
	u32	trailer_seed;
};

static size_t build_beacon(u8 *out, const struct beacon_opts *o)
{
	size_t n = 0;
	size_t i;

	memset(out, 0, 600);
	out[n++] = o->presp ? 0x50u : 0x80u;	/* fc[0]: version0, mgmt */
	out[n++] = 0x00u;			/* fc[1]: no flags */
	out[n++] = 0x00u;			/* duration: 2 bytes */
	out[n++] = 0x00u;
	/* addr1 (DA) broadcast, addr2/addr3 the BSSID */
	for (i = 0; i < 6; i++)
		out[n++] = 0xffu;
	for (i = 0; i < 6; i++)
		out[n++] = TEST_BSSID_OCTET;
	for (i = 0; i < 6; i++)
		out[n++] = TEST_BSSID_OCTET;
	n += 2;					/* sequence control */
	n = put_u64(out, n, 0x1122334455667788ull);	/* timestamp */
	n = put_u16(out, n, 100);			/* beacon interval */
	n = put_u16(out, n, 0x0411u);			/* capability: ESS+priv */
	n = add_ssid(out, n, o->ssid);
	if (o->ds_chan)
		n = add_ds(out, n, o->ds_chan);
	if (o->ht) {
		n = add_ht_cap(out, n, 0x0142u);
		n = add_ht_op(out, n, o->ht_op_primary, o->ht_sec_off);
	}
	if (o->rsn)
		n = add_rsn(out, n);
	if (o->wmm)
		n = add_wmm(out, n);
	if (o->country)
		n = add_country(out, n);
	if (o->ext_cap)
		n = add_ext_cap(out, n);
	if (o->trailer) {
		u32 k;

		for (k = 0; k < HI5622_SCAN_D2H_TRAILER_LEN; k++)
			out[n + k] = (u8)(k * 3u + 1u);
		out[n + 5] = 0xc0u;	/* trailer +5: signed -64 [I] */
		out[n + 6] = 0xdeu;
		out[n + 0x11] = 4u;	/* phy mode: HT [V offset] */
		n += HI5622_SCAN_D2H_TRAILER_LEN;
	}
	return n;
}

/* ==================================================================
 * 2. auto scan window
 * ================================================================== */
static void test_timeout_auto(void)
{
	CHECK_EQ(hi5622_scan_timeout_auto(13),
		 HI5622_SCAN_TIMEOUT_BASE_MS + 13u * HI5622_SCAN_TIMEOUT_PER_CHAN_MS,
		 "13 channel auto window");
	CHECK_EQ(hi5622_scan_timeout_auto(1),
		 HI5622_SCAN_TIMEOUT_BASE_MS + HI5622_SCAN_TIMEOUT_PER_CHAN_MS,
		 "1 channel auto window");
	CHECK_EQ(hi5622_scan_timeout_auto(0), hi5622_scan_timeout_auto(13),
		 "unknown channel count == 13-channel bench band");
	CHECK_EQ(hi5622_scan_timeout_auto(32),
		 HI5622_SCAN_TIMEOUT_BASE_MS + 32u * HI5622_SCAN_TIMEOUT_PER_CHAN_MS,
		 "32 channel auto window");
	CHECK(hi5622_scan_timeout_auto(42) <= HI5622_SCAN_TIMEOUT_MAX_MS,
	      "auto window clamped to MAX");
	/* task-133: the retired window no longer has to exceed 5 s - the measured
	 * cadence is ~156 ms/channel, so the tuned window only has to cover it. */
	CHECK(hi5622_scan_timeout_auto(13) > 13u * 156u,
	      "13-channel window covers the measured channel cadence");
	/* wifi-scan-tuning (task-133): the window was re-measured - the firmware
	 * reports ~156 ms/channel, not ~830 ms (that figure came from a host bound).
	 * The auto term stays generous (250 ms/ch) but is far tighter than the old
	 * 900 ms/ch formula. */
	CHECK(hi5622_scan_timeout_auto(13) >= 1500u + 13u * 156u,
	      "13 channels at least the measured 156 ms/channel + base");
	CHECK(hi5622_scan_timeout_auto(13) < 3000u + 13u * 900u,
	      "13-channel window tighter than the retired 830 ms/ch formula");
}

/* ==================================================================
 * 3. channel-coverage scan model
 * ================================================================== */
static void fill_13ch(u16 *freqs)
{
	u32 i;

	for (i = 0; i < 13u; i++)
		freqs[i] = (u16)(2412u + 5u * i);
}

static void test_coverage_completion(void)
{
	struct hi5622_scan_model m;
	u16 freqs[13];
	u64 t = 1000;
	u32 i;

	fill_13ch(freqs);
	hi5622_scan_model_init(&m);
	CHECK_EQ(hi5622_scan_model_start(&m, t, hi5622_scan_timeout_auto(13)), 0,
		 "start 13-channel scan");
	CHECK_EQ(hi5622_scan_model_set_channels(&m, freqs, 13), 0,
		 "set channel plan");
	CHECK_EQ(m.chan_total, 13, "channel plan stored");
	CHECK(hi5622_scan_model_active(&m), "model active");
	CHECK_EQ(hi5622_scan_model_set_channels(&m, freqs, 13), 0,
		 "channel plan is idempotent");
	/* feed the 13 channel results (out of order on purpose) */
	{
		static const u8 order[13] = { 6, 0, 12, 3, 9, 1, 11, 4, 8, 2,
					      10, 5, 7 };

		for (i = 0; i < 13u; i++) {
			enum hi5622_scan_step step;

			t += 156;
			step = hi5622_scan_model_channel(&m,
					freqs[order[i]], t);
			CHECK_EQ(step, HI5622_SCAN_WAIT,
				 "channel event does not finish the scan");
			if (i < 12u)
				CHECK(m.chan_seen < 13u,
				      "coverage incomplete before the last channel");
		}
	}
	CHECK_EQ(m.channel_events, 13, "13 channel events counted");
	CHECK_EQ(m.chan_seen, 13, "13 distinct requested channels seen");
	CHECK_EQ(m.coverage_ms, t, "coverage timestamp latched on the last one");
	CHECK_EQ(hi5622_scan_model_tick(&m, t + HI5622_SCAN_SETTLE_MS - 1),
		 HI5622_SCAN_WAIT, "settle window still open");
	CHECK_EQ(hi5622_scan_model_tick(&m, t + HI5622_SCAN_SETTLE_MS),
		 HI5622_SCAN_FINISH_OK, "all requested channels -> finish OK");
	hi5622_scan_model_finish(&m, HI5622_SCAN_FINISH_OK);
	CHECK_EQ(m.phase, HI5622_SCAN_FINISHED, "model finished");
	CHECK_EQ(strcmp(m.finish_reason, "channel-coverage"), 0,
		 "finish reason channel-coverage");
	CHECK_EQ(m.coverage_completes, 1, "coverage finish counted");
	CHECK_EQ(m.finishes_ok, 1, "one successful finish");
	CHECK_EQ(m.timeouts, 0, "coverage completion is not a timeout");
	CHECK_EQ(hi5622_scan_model_tick(&m, t + 100000),
		 HI5622_SCAN_WAIT, "no decision after finish");
	hi5622_scan_model_clear(&m);
	CHECK_EQ(m.phase, HI5622_SCAN_IDLE, "clear -> idle");
	CHECK(hi5622_scan_model_active(&m) == false, "not active after clear");
}

static void test_coverage_duplicates_and_foreign(void)
{
	struct hi5622_scan_model m;
	u16 freqs[2] = { 2412u, 2417u };
	u64 t = 500;

	hi5622_scan_model_init(&m);
	CHECK_EQ(hi5622_scan_model_start(&m, t, 20000u), 0, "start 2-channel");
	CHECK_EQ(hi5622_scan_model_set_channels(&m, freqs, 2), 0, "2-channel plan");
	CHECK_EQ(hi5622_scan_model_channel(&m, 2412u, t + 10),
		 HI5622_SCAN_WAIT, "first channel event");
	CHECK_EQ(m.chan_seen, 1, "one channel seen");
	CHECK_EQ(hi5622_scan_model_channel(&m, 2412u, t + 20),
		 HI5622_SCAN_WAIT, "duplicate channel event");
	CHECK_EQ(m.chan_seen, 1, "duplicate does not count twice");
	CHECK_EQ(m.channel_events, 2, "duplicate still counted as event");
	CHECK_EQ(hi5622_scan_model_channel(&m, 2432u, t + 30),
		 HI5622_SCAN_WAIT, "foreign channel event");
	CHECK_EQ(m.chan_seen, 1, "foreign channel not in plan");
	CHECK_EQ(m.coverage_ms, 0, "no coverage yet");
	CHECK_EQ(hi5622_scan_model_channel(&m, 2417u, t + 40),
		 HI5622_SCAN_WAIT, "second requested channel");
	CHECK_EQ(m.chan_seen, 2, "coverage reached");
	CHECK_EQ(m.coverage_ms, t + 40, "coverage timestamp");
	CHECK_EQ(hi5622_scan_model_tick(&m, t + 40 + HI5622_SCAN_SETTLE_MS),
		 HI5622_SCAN_FINISH_OK, "coverage finish");
	hi5622_scan_model_finish(&m, HI5622_SCAN_FINISH_OK);
	hi5622_scan_model_clear(&m);
}

static void test_completion_event_wins(void)
{
	struct hi5622_scan_model m;
	u16 freqs[13];
	u64 t = 1000;
	u32 i;

	fill_13ch(freqs);
	hi5622_scan_model_init(&m);
	CHECK_EQ(hi5622_scan_model_start(&m, t, 20000u), 0, "start");
	CHECK_EQ(hi5622_scan_model_set_channels(&m, freqs, 13), 0, "plan");
	for (i = 0; i < 13u; i++)
		hi5622_scan_model_channel(&m, freqs[i], t + 156u * (i + 1u));
	CHECK_EQ(hi5622_scan_model_complete(&m, 1u, 0u, t + 12000u),
		 HI5622_SCAN_FINISH_OK, "g1 id16 status 1 -> success");
	CHECK_EQ(m.complete_seen, true, "completion latched");
	CHECK_EQ(m.complete_reason, 1, "completion status stored");
	CHECK_EQ(hi5622_scan_model_tick(&m, t + 12000u),
		 HI5622_SCAN_FINISH_OK, "tick agrees with the completion");
	hi5622_scan_model_finish(&m, HI5622_SCAN_FINISH_OK);
	CHECK_EQ(strcmp(m.finish_reason, "complete-event"), 0,
		 "finish reason complete-event");
	CHECK_EQ(m.coverage_completes, 0, "no coverage fallback used");
	hi5622_scan_model_clear(&m);
}

static void test_completion_failure(void)
{
	struct hi5622_scan_model m;
	u64 t = 1000;

	hi5622_scan_model_init(&m);
	CHECK_EQ(hi5622_scan_model_start(&m, t, 20000u), 0, "start");
	CHECK_EQ(hi5622_scan_model_complete(&m, 0x64u, 1u, t + 100u),
		 HI5622_SCAN_FINISH_ABORT, "g1 id16 status != 1 -> abort");
	CHECK_EQ(m.complete_failed, true, "completion failure latched");
	CHECK_EQ(m.complete_failures, 1, "failure counted");
	CHECK_EQ(hi5622_scan_model_tick(&m, t + 100u),
		 HI5622_SCAN_FINISH_ABORT, "tick reports the firmware abort");
	hi5622_scan_model_finish(&m, HI5622_SCAN_FINISH_ABORT);
	CHECK_EQ(strcmp(m.finish_reason, "complete-failed"), 0,
		 "finish reason complete-failed");
	CHECK_EQ(m.timeouts, 0, "firmware failure is not a host timeout");
	hi5622_scan_model_clear(&m);
}

static void test_host_timeout(void)
{
	struct hi5622_scan_model m;
	u16 freqs[13];
	u64 t = 1000;
	u32 i;

	fill_13ch(freqs);
	hi5622_scan_model_init(&m);
	CHECK_EQ(hi5622_scan_model_start(&m, t, 10000u), 0, "start");
	CHECK_EQ(hi5622_scan_model_set_channels(&m, freqs, 13), 0, "plan");
	for (i = 0; i < 6u; i++)	/* the 2026-11-26 5 s outcome shape */
		hi5622_scan_model_channel(&m, freqs[i], t + 156u * (i + 1u));
	CHECK_EQ(m.chan_seen, 6, "6 of 13 channels before the bound");
	CHECK_EQ(hi5622_scan_model_tick(&m, t + 9999u),
		 HI5622_SCAN_WAIT, "bound not reached yet");
	CHECK_EQ(hi5622_scan_model_tick(&m, t + 10000u),
		 HI5622_SCAN_FINISH_ABORT, "deadline -> abort");
	hi5622_scan_model_finish(&m, HI5622_SCAN_FINISH_ABORT);
	CHECK_EQ(strcmp(m.finish_reason, "host-timeout"), 0,
		 "finish reason host-timeout");
	CHECK_EQ(m.timeouts, 1, "host timeout counted");
	CHECK_EQ(m.finishes_abort, 1, "abort finish counted");
	hi5622_scan_model_clear(&m);
}

static void test_abort_and_plan_bounds(void)
{
	struct hi5622_scan_model m;
	u16 freqs[3] = { 2412u, 2417u, 2422u };
	u16 bad[33];
	u64 t = 1000;
	u32 i;

	for (i = 0; i < 33u; i++)
		bad[i] = 2412u;
	hi5622_scan_model_init(&m);
	CHECK_EQ(hi5622_scan_model_start(&m, t, 10000u), 0, "start");
	CHECK_EQ(hi5622_scan_model_set_channels(&m, bad, 33), -EINVAL,
		 "33 channels rejected");
	CHECK_EQ(hi5622_scan_model_set_channels(&m, NULL, 1), -EINVAL,
		 "NULL plan rejected");
	CHECK_EQ(hi5622_scan_model_set_channels(&m, freqs, 2), 0, "plan set");
	{
		u16 zero[2] = { 2412u, 0u };

		CHECK_EQ(hi5622_scan_model_set_channels(&m, zero, 2), -EINVAL,
			 "zero frequency rejected");
		CHECK_EQ(m.chan_total, 2, "a rejected plan keeps the old one");
	}
	CHECK_EQ(hi5622_scan_model_abort(&m), 0, "abort request");
	CHECK_EQ(hi5622_scan_model_channel(&m, 2412u, t + 5),
		 HI5622_SCAN_FINISH_ABORT, "channel event after abort -> abort");
	CHECK_EQ(hi5622_scan_model_tick(&m, t + 6),
		 HI5622_SCAN_FINISH_ABORT, "tick after abort -> abort");
	hi5622_scan_model_finish(&m, HI5622_SCAN_FINISH_ABORT);
	CHECK_EQ(strcmp(m.finish_reason, "abort-request"), 0,
		 "finish reason abort-request");
	CHECK_EQ(m.aborts_requested, 1, "abort request counted");
	hi5622_scan_model_clear(&m);
	/* channel events outside a scan are counted as idle events */
	CHECK_EQ(hi5622_scan_model_channel(&m, 2412u, t + 7),
		 HI5622_SCAN_WAIT, "channel event while idle");
	CHECK_EQ(m.events_when_idle, 1, "idle event counted");
	/* a plan cannot be attached to an idle model */
	CHECK_EQ(hi5622_scan_model_set_channels(&m, freqs, 2), -EINVAL,
		 "set_channels requires an active scan");
}

static void test_status_line(void)
{
	struct hi5622_scan_model m;
	char buf[1024];

	hi5622_scan_model_init(&m);
	CHECK(hi5622_scan_status_line(&m, buf, sizeof(buf)) > 0,
	      "status line renders");
	CHECK(strstr(buf, "chans=0/0") != NULL, "status shows channel coverage");
	CHECK(strstr(buf, "scan finish:") != NULL, "status shows finish block");
	CHECK(strstr(buf, "timeout_ms=5000") != NULL,
	      "status shows the legacy default timeout");
}

/* ==================================================================
 * 4. g1 id15 beacon IE decode
 * ================================================================== */
static void test_beacon_decode_full(void)
{
	struct beacon_opts o;
	struct hi5622_scan_d2h_bss b;
	u8 frame[600];
	size_t n;
	int rc;

	memset(&o, 0, sizeof(o));
	o.ssid = "bench-ap";
	o.ds_chan = 6;
	o.ht = true;
	o.ht_op_primary = 6;
	o.ht_sec_off = HI5622_SCAN_D2H_HT_SEC_ABOVE;
	o.rsn = true;
	o.wmm = true;
	o.country = true;
	o.ext_cap = true;
	o.trailer = true;
	n = build_beacon(frame, &o);

	rc = hi5622_scan_d2h_parse_bss(frame, (u32)n, NULL, &b);
	CHECK_EQ(rc, 0, "beacon with trailer decodes");
	CHECK_EQ(b.layout, HI5622_SCAN_D2H_LAYOUT_FRAME_TRAILER,
		 "frame+trailer20 layout");
	CHECK_EQ(b.frame_type, HI5622_SCAN_D2H_FTYPE_BEACON, "beacon subtype");
	CHECK_EQ(b.freq, 2437, "channel 6 frequency");
	CHECK_EQ(b.chan_num, 6, "channel number");
	CHECK_EQ(b.is_5g, false, "2.4 GHz channel");
	CHECK_EQ(b.ds_chan, 6, "DS parameter-set channel");
	CHECK_EQ(strcmp((const char *)b.ssid, "bench-ap"), 0, "SSID decoded");
	CHECK_EQ(b.ssid_len, 8, "SSID length");
	CHECK_EQ(b.ssid_hidden, false, "SSID not hidden");
	CHECK_EQ(b.capability, 0x0411, "capability field");
	CHECK_EQ(b.cap_flags,
		 HI5622_SCAN_D2H_CAP_ESS | HI5622_SCAN_D2H_CAP_PRIVACY,
		 "ESS+privacy capability flags");
	CHECK_EQ(b.beacon_interval, 100, "beacon interval TU");
	CHECK_EQ(b.tsf, 0x1122334455667788ull, "TSF decoded");
	CHECK_EQ(b.ie_flags,
		 HI5622_SCAN_D2H_IE_HT_CAP | HI5622_SCAN_D2H_IE_HT_OP |
		 HI5622_SCAN_D2H_IE_RSN | HI5622_SCAN_D2H_IE_WMM |
		 HI5622_SCAN_D2H_IE_COUNTRY | HI5622_SCAN_D2H_IE_EXT_CAP,
		 "IE presence flags");
	CHECK_EQ(b.ht_cap_info, 0x0142, "HT capabilities info field");
	CHECK_EQ(b.ht_op_primary, 6, "HT operation primary channel");
	CHECK_EQ(b.ht_op_sec_off, HI5622_SCAN_D2H_HT_SEC_ABOVE,
		 "HT secondary channel above");
	CHECK(b.ie_count >= 8, "all IEs walked");
	CHECK(b.ie_len > 40, "IE chain forwarded for cfg80211");
	CHECK_EQ(b.trailer_present, 1, "trailer present");
	CHECK_EQ(b.tr_s8_5, -64, "trailer +5 signed byte");
	CHECK_EQ(b.phy_mode, 4, "trailer PHY mode HT");
	CHECK_EQ(b.signal_mbm, 0, "signal stays 0 by default");
	CHECK_EQ(b.signal_from_trailer, false, "signal not from trailer");
	/* the same input with the bench opt-in reports the trailer heuristic */
	{
		struct hi5622_scan_d2h_opts opts = {
			.trust_trailer_signal = true,
			.require_exact_ie = true,
		};

		rc = hi5622_scan_d2h_parse_bss(frame, (u32)n, &opts, &b);
		CHECK_EQ(rc, 0, "beacon decodes with signal opt-in");
		CHECK_EQ(b.signal_mbm, -6400, "trailer RSSI heuristic mBm");
		CHECK_EQ(b.signal_from_trailer, true, "signal provenance");
	}
}

static void test_beacon_decode_variants(void)
{
	struct beacon_opts o;
	struct hi5622_scan_d2h_bss b;
	u8 frame[600];
	u8 prefixed[620];
	size_t n;
	int rc;

	/* probe response */
	memset(&o, 0, sizeof(o));
	o.presp = true;
	o.ssid = "pr";
	o.ds_chan = 1;
	o.trailer = true;
	n = build_beacon(frame, &o);
	rc = hi5622_scan_d2h_parse_bss(frame, (u32)n, NULL, &b);
	CHECK_EQ(rc, 0, "probe response decodes");
	CHECK_EQ(b.frame_type, HI5622_SCAN_D2H_FTYPE_PRESP, "probe-resp subtype");
	CHECK_EQ(b.freq, 2412, "probe response channel");

	/* bare frame (no trailer) */
	memset(&o, 0, sizeof(o));
	o.ssid = "bare";
	o.ds_chan = 11;
	n = build_beacon(frame, &o);
	rc = hi5622_scan_d2h_parse_bss(frame, (u32)n, NULL, &b);
	CHECK_EQ(rc, 0, "bare frame decodes");
	CHECK_EQ(b.layout, HI5622_SCAN_D2H_LAYOUT_FRAME_ONLY,
		 "frame-only layout");
	CHECK_EQ(b.freq, 2462, "bare frame channel 11");

	/* hidden SSID */
	memset(&o, 0, sizeof(o));
	o.ssid = NULL;
	o.ds_chan = 3;
	o.trailer = true;
	n = build_beacon(frame, &o);
	rc = hi5622_scan_d2h_parse_bss(frame, (u32)n, NULL, &b);
	CHECK_EQ(rc, 0, "hidden SSID decodes");
	CHECK_EQ(b.ssid_hidden, true, "hidden SSID flagged");
	CHECK_EQ(b.ssid_len, 0, "hidden SSID length 0");

	/* 5 GHz probe response, no DS element: channel from HT operation */
	memset(&o, 0, sizeof(o));
	o.presp = true;
	o.ssid = "five";
	o.ht = true;
	o.ht_op_primary = 36;
	o.trailer = true;
	n = build_beacon(frame, &o);
	rc = hi5622_scan_d2h_parse_bss(frame, (u32)n, NULL, &b);
	CHECK_EQ(rc, 0, "5 GHz probe response decodes");
	CHECK_EQ(b.freq, 5180, "5 GHz channel 36 frequency");
	CHECK_EQ(b.is_5g, true, "5 GHz flagged");
	CHECK_EQ(b.ds_chan, 0, "no DS element");
	CHECK_EQ(b.ht_op_primary, 36, "HT op primary channel");

	/* ch14 special case */
	memset(&o, 0, sizeof(o));
	o.ssid = "ch14";
	o.ds_chan = 14;
	o.trailer = true;
	n = build_beacon(frame, &o);
	rc = hi5622_scan_d2h_parse_bss(frame, (u32)n, NULL, &b);
	CHECK_EQ(rc, 0, "channel 14 decodes");
	CHECK_EQ(b.freq, 2484, "channel 14 = 2484 MHz");

	/* defensive 12-byte prefix variant */
	memset(&o, 0, sizeof(o));
	o.ssid = "pref";
	o.ds_chan = 6;
	o.trailer = true;
	n = build_beacon(frame, &o);
	memset(prefixed, 0, sizeof(prefixed));
	prefixed[0] = 0x01u;	/* group nibble 1 */
	prefixed[2] = 0x11u;
	prefixed[6] = 0x0fu;
	prefixed[10] = 0x5au;
	prefixed[11] = 0x5au;
	memcpy(prefixed + 12, frame, n);
	rc = hi5622_scan_d2h_parse_bss(prefixed, (u32)(n + 12u), NULL, &b);
	CHECK_EQ(rc, 0, "12-byte-prefixed frame decodes (defensive variant)");
	CHECK_EQ(b.layout, HI5622_SCAN_D2H_LAYOUT_PREFIX12_TRAILER,
		 "prefix12+trailer layout");
	CHECK_EQ(b.freq, 2437, "prefixed frame channel");
}

static void test_beacon_decode_negative(void)
{
	struct beacon_opts o;
	struct hi5622_scan_d2h_bss b;
	u8 frame[600];
	size_t n;
	int rc;

	CHECK_EQ(hi5622_scan_d2h_parse_bss(NULL, 0, NULL, &b), -EINVAL,
		 "NULL payload rejected");
	memset(&o, 0, sizeof(o));
	o.ssid = "x";
	o.ds_chan = 6;
	n = build_beacon(frame, &o);
	CHECK_EQ(hi5622_scan_d2h_parse_bss(frame, 20, NULL, &b),
		 -EOPNOTSUPP, "short payload rejected");
	/* a data frame is not a scan result */
	frame[0] = 0x08u;
	CHECK_EQ(hi5622_scan_d2h_parse_bss(frame, (u32)n, NULL, &b),
		 -EOPNOTSUPP, "non-management frame rejected");
	frame[0] = 0x80u;
	/* truncated IE: claim 200 bytes with none following */
	{
		u8 bad[64];
		size_t i;

		memset(bad, 0, sizeof(bad));
		bad[0] = 0x80u;
		for (i = 0; i < 6; i++)
			bad[4 + i] = bad[10 + i] = bad[16 + i] = TEST_BSSID_OCTET;
		bad[36] = 0;	/* SSID IE with length 200 but no bytes */
		bad[37] = 200u;
		CHECK_EQ(hi5622_scan_d2h_parse_bss(bad, sizeof(bad), NULL, &b),
			 -EOPNOTSUPP, "truncated IE chain rejected");
	}
	/* channel-less frame: no DS and no usable HT operation */
	memset(&o, 0, sizeof(o));
	o.ssid = "nochan";
	o.trailer = true;
	rc = hi5622_scan_d2h_parse_bss(frame, (u32)n, NULL, &b);
	CHECK_EQ(rc, 0, "frame with channel still decodes after reset");
}

static void test_complete_channel_sta_decode(void)
{
	struct hi5622_scan_d2h_complete cc;
	struct hi5622_scan_d2h_chan ch;
	struct hi5622_scan_d2h_sta_comp sc;
	u8 payload[0x70];
	u32 i;

	memset(payload, 0, sizeof(payload));
	payload[0] = 1u;
	CHECK_EQ(hi5622_scan_d2h_parse_complete(payload, 4, &cc), 0,
		 "g1 id16 decodes");
	CHECK_EQ(cc.status, 1, "g1 id16 success status");
	payload[0] = 0x64u;
	CHECK_EQ(hi5622_scan_d2h_parse_complete(payload, 4, &cc), 0,
		 "g1 id16 refusal decodes");
	CHECK_EQ(cc.status, 0x64, "g1 id16 refusal status");

	memset(payload, 0, sizeof(payload));
	payload[0] = 3u;	/* slot */
	for (i = 0; i < 0x6cu; i++)
		payload[4 + i] = (u8)i;
	payload[4] = 6u;	/* channel in the 108-byte record */
	CHECK_EQ(hi5622_scan_d2h_parse_chan(payload, 0x70u, &ch), 0,
		 "g1 id17 decodes");
	CHECK_EQ(ch.slot, 3, "g1 id17 slot");
	CHECK_EQ(ch.chan_num, 6, "g1 id17 channel number");
	CHECK_EQ(ch.freq, 2437, "g1 id17 frequency");
	CHECK_EQ(ch.block_len, 0x6cu, "g1 id17 108-byte record copied");

	memset(payload, 0, sizeof(payload));
	payload[0] = 0x30u;	/* core nibble 3 */
	payload[0x0d] = 1u;	/* status */
	CHECK_EQ(hi5622_scan_d2h_parse_sta_comp(payload, 0x20u, &sc), 0,
		 "g2 id2 decodes");
	CHECK_EQ(sc.status, 1, "g2 id2 status");
	CHECK_EQ(sc.core_band, 3, "g2 id2 core/band nibble");
}

/* ==================================================================
 * 5. provider map round-trip + cfg80211 result validation
 * ================================================================== */
static void test_map_roundtrip(void)
{
	struct beacon_opts o;
	struct hi5622_scan_result r;
	u8 frame[600];
	size_t n;
	int rc;

	memset(&o, 0, sizeof(o));
	o.ssid = "map-ap";
	o.ds_chan = 6;
	o.ht = true;
	o.ht_op_primary = 6;
	o.trailer = true;
	n = build_beacon(frame, &o);
	rc = hi5622_vapscan_decode_scan_result(frame, (u32)n, &r);
	CHECK_EQ(rc, 0, "map decode succeeds");
	CHECK_EQ(r.freq, 2437, "map frequency");
	CHECK_EQ(r.ds_chan, 6, "map DS channel");
	CHECK_EQ(r.ie_count, 4, "map IE count (SSID+DS+HT cap+HT op)");
	CHECK_EQ(r.cap_flags,
		 HI5622_SCAN_CAP_ESS | HI5622_SCAN_CAP_PRIVACY,
		 "map capability flags");
	CHECK_EQ(r.ie_flags,
		 HI5622_SCAN_IE_HT_CAP | HI5622_SCAN_IE_HT_OP,
		 "map IE presence flags");
	CHECK_EQ(r.ht_cap_info, 0x0142, "map HT capability info");
	CHECK((int)r.ssid_len == 6, "map SSID length");
	CHECK_EQ(memcmp(r.ssid, "map-ap", 6), 0, "map SSID bytes");
	CHECK_EQ(r.signal_mbm, 0, "map signal 0 by default");
	CHECK_EQ(hi5622_scan_result_validate(&r), 0,
		 "decoded result validates for cfg80211");
	CHECK(r.ie_len > 40, "map forwards the IE chain");
	/* the opt-in variant reports the trailer heuristic */
	rc = hi5622_vapscan_decode_scan_result_opts(frame, (u32)n, &r, true);
	CHECK_EQ(rc, 0, "map decode with signal opt-in");
	CHECK_EQ(r.signal_mbm, -6400, "map trailer RSSI opt-in");

	/* validation of hostile/partial results */
	memset(&r, 0, sizeof(r));
	CHECK_EQ(hi5622_scan_result_validate(&r), -EINVAL,
		 "all-zero result rejected");
	memcpy(r.bssid, "\x02\x00\x00\x00\x00\x01", 6);
	r.freq = 2437;
	CHECK_EQ(hi5622_scan_result_validate(&r), 0, "minimal result accepted");
	r.signal_mbm = 5001;
	CHECK_EQ(hi5622_scan_result_validate(&r), -EINVAL,
		 "out-of-range signal rejected");
	r.signal_mbm = 0;
	r.freq = 2400;
	CHECK_EQ(hi5622_scan_result_validate(&r), -EINVAL,
		 "unsupported frequency rejected");
	r.freq = 2437;
	r.bssid[0] = 0x01;
	CHECK_EQ(hi5622_scan_result_validate(&r), -EINVAL,
		 "multicast BSSID rejected");
}

static void test_scan_req_dwell_validation(void)
{
	struct hi5622_scan_req_params p;

	memset(&p, 0, sizeof(p));
	CHECK_EQ(hi5622_scan_req_params_validate(&p), 0, "empty request valid");
	p.n_channels = 1;
	p.freq[0] = 2437;
	p.dwell_ms = 0;
	CHECK_EQ(hi5622_scan_req_params_validate(&p), 0,
		 "dwell 0 = vendor default");
	p.dwell_ms = 60;
	CHECK_EQ(hi5622_scan_req_params_validate(&p), 0, "dwell 60 valid");
	p.dwell_ms = 20;
	CHECK_EQ(hi5622_scan_req_params_validate(&p), 0, "dwell 20 valid");
	p.dwell_ms = 200;
	CHECK_EQ(hi5622_scan_req_params_validate(&p), 0, "dwell 200 valid");
	p.dwell_ms = 19;
	CHECK_EQ(hi5622_scan_req_params_validate(&p), -EINVAL,
		 "dwell below range rejected");
	p.dwell_ms = 201;
	CHECK_EQ(hi5622_scan_req_params_validate(&p), -EINVAL,
		 "dwell above range rejected");
}

int main(void)
{
	printf("scan-complete selftest (task-123)\n");
	test_timeout_auto();
	test_coverage_completion();
	test_coverage_duplicates_and_foreign();
	test_completion_event_wins();
	test_completion_failure();
	test_host_timeout();
	test_abort_and_plan_bounds();
	test_status_line();
	test_beacon_decode_full();
	test_beacon_decode_variants();
	test_beacon_decode_negative();
	test_complete_channel_sta_decode();
	test_map_roundtrip();
	test_scan_req_dwell_validation();
	printf("%d/%d PASS\n", checks - failed, checks);
	return failed ? 1 : 0;
}
