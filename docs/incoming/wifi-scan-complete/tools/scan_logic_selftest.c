// SPDX-License-Identifier: GPL-2.0
/*
 * scan_logic_selftest.c - host-side (device-free, kernel-free) selftest for
 * the portable scan/AP frame-provider logic of the wifi-scan-ap lane
 * (shared task task-113).
 *
 * It exercises the EXACT decision surface the kernel layer calls:
 *   - the frame-provider contract validator and capability formatter,
 *   - every neutral parameter validator (bounds/policy, no device),
 *   - the D2H event classifier (g1 15/16/17, g2 2),
 *   - the scan model: start/busy, results/overflow, complete, abort,
 *     bounded timeout, finish/clear, counters and status line,
 *   - a host TEST-DOUBLE provider whose only real builder wraps the inherited
 *     EXACT g0 id9 builder, proving the provider -> wire -> h2d_frames
 *     round-trip without inventing any payload bytes.
 *
 * The test double is NOT built into the kernel module: the module's committed
 * provider registry is empty, which is what makes every message op fail
 * closed.  Build: tools/build.sh step 1.
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_scan_logic.h"
#include "hi5622_vapscan_map.h"
#include "h2d_frames.h"

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

/* ------------------------------------------------------------------ */
/* 1. frame-provider contract                                          */
/* ------------------------------------------------------------------ */
static int dummy_scan_req(u8 *wire, size_t cap, size_t *out_len,
			  const struct hi5622_scan_req_params *p)
{
	(void)wire; (void)cap; (void)out_len; (void)p;
	return -EOPNOTSUPP;
}
static int dummy_parse_result(const u8 *payload, u32 len,
			      struct hi5622_scan_result *out)
{
	(void)payload; (void)len; (void)out;
	return -EOPNOTSUPP;
}
static int dummy_parse_complete(const u8 *payload, u32 len,
				struct hi5622_scan_complete *out)
{
	(void)payload; (void)len; (void)out;
	return -EOPNOTSUPP;
}
static int dummy_user_add(u8 *wire, size_t cap, size_t *out_len,
			  const struct hi5622_vap_add_params *p)
{
	(void)wire; (void)cap; (void)out_len; (void)p;
	return -EOPNOTSUPP;
}
static int dummy_ap_start(u8 *wire, size_t cap, size_t *out_len,
			  const struct hi5622_ap_start_params *p)
{
	(void)wire; (void)cap; (void)out_len; (void)p;
	return -EOPNOTSUPP;
}
static int dummy_ap_stop(u8 *wire, size_t cap, size_t *out_len,
			 const struct hi5622_ap_stop_params *p)
{
	(void)wire; (void)cap; (void)out_len; (void)p;
	return -EOPNOTSUPP;
}
/* wifi-vap-start lane callbacks (task-117): the provider patch added the two
 * caps, so the full-ops contract test needs the matching dummies. */
static int dummy_vap_add(u8 *wire, size_t cap, size_t *out_len,
			 const struct hi5622_vap_start_params *p)
{
	(void)wire; (void)cap; (void)out_len; (void)p;
	return -EOPNOTSUPP;
}
static int dummy_vap_start(u8 *wire, size_t cap, size_t *out_len,
			   const struct hi5622_vap_start_params *p)
{
	(void)wire; (void)cap; (void)out_len; (void)p;
	return -EOPNOTSUPP;
}

/* the only provider with a REAL builder in this test: g0 id9 del_user, whose
 * exact builder is part of the inherited wifi-h2d-frames library */
static int test_build_user_del(u8 *wire, size_t cap, size_t *out_len,
			       const struct hi5622_vap_del_params *p)
{
	struct h2d_frame f;
	enum h2d_err err = H2D_OK;
	size_t len = 0;
	const u8 addr4[4] = { p->mac[0], p->mac[1], p->mac[2], p->mac[3] };
	int rc;

	rc = h2d_build_g0_id09_del_user(&f, 0, p->user_id,
					p->user_id == 0x0fffu ? addr4 : NULL,
					&err);
	if (rc)
		return err == H2D_OK ? -EIO : -EINVAL;
	rc = h2d_frame_export(&f, wire, cap, 0, 0x00, &len, &err);
	if (rc)
		return -EINVAL;
	*out_len = len;
	return 0;
}

static struct hi5622_frame_ops make_full_ops(void)
{
	struct hi5622_frame_ops ops;

	memset(&ops, 0, sizeof(ops));
	ops.name = "selftest-all";
	ops.caps = HI5622_FRAME_CAPS_ALL;
	ops.build_scan_req = dummy_scan_req;
	ops.parse_scan_result = dummy_parse_result;
	ops.parse_scan_complete = dummy_parse_complete;
	ops.build_user_add = dummy_user_add;
	ops.build_user_add_notify = dummy_user_add;
	ops.build_user_del = test_build_user_del;
	ops.build_ap_start = dummy_ap_start;
	ops.build_ap_stop = dummy_ap_stop;
	ops.build_vap_add = dummy_vap_add;
	ops.build_vap_start = dummy_vap_start;
	return ops;
}

static void test_provider_contract(void)
{
	struct hi5622_frame_ops ops = make_full_ops();
	char caps[128];
	u32 bit;

	CHECK_EQ(hi5622_frame_ops_validate(NULL), -EINVAL, "NULL ops refused");
	CHECK_EQ(hi5622_frame_ops_validate(&ops), 0, "full ops valid");

	ops.name = NULL;
	CHECK_EQ(hi5622_frame_ops_validate(&ops), -EINVAL, "NULL name refused");
	ops.name = "";
	CHECK_EQ(hi5622_frame_ops_validate(&ops), -EINVAL, "empty name refused");
	ops.name = "selftest-all";

	ops.caps = 0;
	CHECK_EQ(hi5622_frame_ops_validate(&ops), -EINVAL, "zero caps refused");
	ops.caps = HI5622_FRAME_CAPS_ALL | (1u << 20);
	CHECK_EQ(hi5622_frame_ops_validate(&ops), -EINVAL, "unknown cap refused");

	/* each capability requires its callback */
	for (bit = 1u; bit && bit <= HI5622_FRAME_CAP_VAP_START; bit <<= 1) {
		struct hi5622_frame_ops one = make_full_ops();

		one.caps = bit;
		CHECK_EQ(hi5622_frame_ops_validate(&one), 0,
			 "single-cap ops valid");
		one.caps = bit;
		switch (bit) {
		case HI5622_FRAME_CAP_SCAN_REQ:
			one.build_scan_req = NULL; break;
		case HI5622_FRAME_CAP_SCAN_PARSE:
			one.parse_scan_result = NULL; break;
		case HI5622_FRAME_CAP_USER_ADD:
			one.build_user_add = NULL; break;
		case HI5622_FRAME_CAP_USER_ADD_NOTIFY:
			one.build_user_add_notify = NULL; break;
		case HI5622_FRAME_CAP_USER_DEL:
			one.build_user_del = NULL; break;
		case HI5622_FRAME_CAP_AP_START:
			one.build_ap_start = NULL; break;
		case HI5622_FRAME_CAP_AP_STOP:
			one.build_ap_stop = NULL; break;
		case HI5622_FRAME_CAP_VAP_ADD:
			one.build_vap_add = NULL; break;
		default:
			one.build_vap_start = NULL; break;
		}
		CHECK_EQ(hi5622_frame_ops_validate(&one), -EINVAL,
			 "capability without callback refused");
	}

	CHECK(strcmp(hi5622_frame_cap_name(HI5622_FRAME_CAP_SCAN_REQ),
		     "scan_req") == 0, "cap name scan_req");
	CHECK(strcmp(hi5622_frame_cap_name(HI5622_FRAME_CAP_USER_DEL),
		     "user_del") == 0, "cap name user_del");
	CHECK(strcmp(hi5622_frame_cap_name(1u << 31), "?") == 0,
	      "unknown cap name");
	CHECK(hi5622_frame_caps_format(0, caps, sizeof(caps)) > 0, "caps none");
	CHECK(strcmp(caps, "none") == 0, "caps none text");
	CHECK(hi5622_frame_caps_format(HI5622_FRAME_CAPS_ALL, caps,
				       sizeof(caps)) > 0, "caps all");
	CHECK(strstr(caps, "scan_req") != NULL, "caps all has scan_req");
	CHECK(strstr(caps, "ap_stop") != NULL, "caps all has ap_stop");
	CHECK(strstr(caps, "vap_add") != NULL, "caps all has vap_add");
	CHECK(strstr(caps, "vap_start") != NULL, "caps all has vap_start");
	CHECK(strchr(caps, ' ') == NULL, "caps format has no spaces");
	{
		char tiny[8];
		int n = hi5622_frame_caps_format(HI5622_FRAME_CAPS_ALL, tiny,
						 sizeof(tiny));

		CHECK(n > 0 && (size_t)n < sizeof(tiny), "caps truncation bounded");
	}
}

/* ------------------------------------------------------------------ */
/* 2. parameter validators                                             */
/* ------------------------------------------------------------------ */
static void fill_scan_req(struct hi5622_scan_req_params *p)
{
	memset(p, 0, sizeof(*p));
	p->user_id = 0;
	p->iftype = 2;
	p->n_ssids = 1;
	p->ssid_len[0] = 4;
	memcpy(p->ssid[0], "test", 4);
	p->n_channels = 2;
	p->freq[0] = 2412;
	p->freq[1] = 2437;
}

static void test_validators(void)
{
	struct hi5622_scan_req_params sr;
	struct hi5622_vap_add_params va;
	struct hi5622_vap_del_params vd;
	struct hi5622_ap_start_params ap;
	struct hi5622_ap_stop_params as;
	struct hi5622_scan_result res;

	fill_scan_req(&sr);
	CHECK_EQ(hi5622_scan_req_params_validate(&sr), 0, "scan req valid");
	CHECK_EQ(hi5622_scan_req_params_validate(NULL), -EINVAL, "scan req NULL");
	sr.n_ssids = HI5622_FRAME_MAX_SSIDS + 1;
	CHECK_EQ(hi5622_scan_req_params_validate(&sr), -EINVAL,
		 "scan req too many ssids");
	fill_scan_req(&sr);
	sr.ssid_len[0] = 33;
	CHECK_EQ(hi5622_scan_req_params_validate(&sr), -EINVAL,
		 "scan req ssid too long");
	fill_scan_req(&sr);
	sr.n_channels = HI5622_FRAME_MAX_CHANNELS + 1;
	CHECK_EQ(hi5622_scan_req_params_validate(&sr), -EINVAL,
		 "scan req too many channels");
	fill_scan_req(&sr);
	sr.freq[1] = 2484;
	CHECK_EQ(hi5622_scan_req_params_validate(&sr), -EINVAL,
		 "scan req unsupported channel");
	fill_scan_req(&sr);
	sr.ie_len = HI5622_FRAME_MAX_SCAN_IE + 1;
	CHECK_EQ(hi5622_scan_req_params_validate(&sr), -EINVAL,
		 "scan req IE too long");
	fill_scan_req(&sr);
	sr.n_ssids = 0;
	sr.n_channels = 0;
	CHECK_EQ(hi5622_scan_req_params_validate(&sr), 0,
		 "passive/all-channel scan valid");

	memset(&va, 0, sizeof(va));
	strcpy(va.name, "wlan0");
	va.mac[0] = 0x02; va.mac[1] = 0x00; va.mac[5] = 0x01;
	va.iftype = 3;
	va.user_id = 1;
	va.vap_id = 0;
	CHECK_EQ(hi5622_vap_add_params_validate(&va), 0, "vap add valid");
	CHECK_EQ(hi5622_vap_add_params_validate(NULL), -EINVAL, "vap add NULL");
	memset(&va, 0, sizeof(va));
	CHECK_EQ(hi5622_vap_add_params_validate(&va), -EINVAL, "vap no name");
	strcpy(va.name, "wlan0");
	va.mac[0] = 0x01;
	CHECK_EQ(hi5622_vap_add_params_validate(&va), -EINVAL, "vap mcast mac");
	memset(&va, 0, sizeof(va));
	strcpy(va.name, "wlan0");
	CHECK_EQ(hi5622_vap_add_params_validate(&va), -EINVAL, "vap zero mac");
	va.mac[0] = 0x02; va.mac[5] = 1; va.iftype = 4;
	CHECK_EQ(hi5622_vap_add_params_validate(&va), -EINVAL, "vap AP_VLAN iftype");
	va.iftype = 3; va.user_id = 0x0fff;
	CHECK_EQ(hi5622_vap_add_params_validate(&va), -EINVAL, "vap user id range");
	{
		struct hi5622_vap_add_params adv = { 0 };

		memset(adv.name, 'x', sizeof(adv.name));
		adv.mac[0] = 0x02; adv.mac[5] = 1; adv.iftype = 3;
		CHECK_EQ(hi5622_vap_add_params_validate(&adv), -EINVAL,
			 "vap non-NUL-terminated name");
	}

	memset(&vd, 0, sizeof(vd));
	vd.user_id = 1;
	vd.mac[0] = 0x02; vd.mac[5] = 0x01;
	CHECK_EQ(hi5622_vap_del_params_validate(&vd), 0, "vap del valid");
	vd.user_id = 0x1000;
	CHECK_EQ(hi5622_vap_del_params_validate(&vd), -EINVAL,
		 "vap del user id range");
	memset(&vd, 0, sizeof(vd));
	vd.user_id = 0x0fff;
	CHECK_EQ(hi5622_vap_del_params_validate(&vd), 0,
		 "vap del wildcard by addr");

	memset(&ap, 0, sizeof(ap));
	ap.ssid_len = 4;
	memcpy(ap.ssid, "test", 4);
	ap.iftype = 3;
	ap.center_freq = 2437;
	ap.width = 1;
	ap.beacon_interval = 100;
	ap.dtim_period = 2;
	ap.head_len = 36;
	CHECK_EQ(hi5622_ap_start_params_validate(&ap), 0, "ap start valid");
	CHECK_EQ(hi5622_ap_start_params_validate(NULL), -EINVAL, "ap start NULL");
	ap.ssid_len = 0;
	CHECK_EQ(hi5622_ap_start_params_validate(&ap), -EINVAL, "ap no ssid");
	ap.ssid_len = 33;
	CHECK_EQ(hi5622_ap_start_params_validate(&ap), -EINVAL, "ap ssid too long");
	ap.ssid_len = 4;
	ap.iftype = 2;
	CHECK_EQ(hi5622_ap_start_params_validate(&ap), -EINVAL, "ap iftype STA");
	ap.iftype = 3;
	ap.center_freq = 2484;
	CHECK_EQ(hi5622_ap_start_params_validate(&ap), -EINVAL, "ap bad channel");
	ap.center_freq = 2437;
	ap.width = 9;
	CHECK_EQ(hi5622_ap_start_params_validate(&ap), -EINVAL, "ap bad width");
	ap.width = 1;
	ap.beacon_interval = 0;
	CHECK_EQ(hi5622_ap_start_params_validate(&ap), -EINVAL, "ap zero bcn int");
	ap.beacon_interval = 100;
	ap.dtim_period = 0;
	CHECK_EQ(hi5622_ap_start_params_validate(&ap), -EINVAL, "ap zero dtim");
	ap.dtim_period = 2;
	ap.head_len = 35;
	CHECK_EQ(hi5622_ap_start_params_validate(&ap), -EINVAL, "ap short head");
	ap.head_len = HI5622_FRAME_MAX_BCN_HEAD + 1;
	CHECK_EQ(hi5622_ap_start_params_validate(&ap), -EINVAL, "ap head too big");
	ap.head_len = 36;
	ap.tail_len = HI5622_FRAME_MAX_BCN_TAIL + 1;
	CHECK_EQ(hi5622_ap_start_params_validate(&ap), -EINVAL, "ap tail too big");
	ap.tail_len = 0;
	ap.privacy = 2;
	CHECK_EQ(hi5622_ap_start_params_validate(&ap), -EINVAL, "ap privacy range");
	ap.privacy = 1;
	ap.auth_type = 7;
	CHECK_EQ(hi5622_ap_start_params_validate(&ap), -EINVAL, "ap auth range");

	memset(&as, 0, sizeof(as));
	as.iftype = 3;
	as.user_id = 1;
	CHECK_EQ(hi5622_ap_stop_params_validate(&as), 0, "ap stop valid");
	as.center_freq = 2484;
	CHECK_EQ(hi5622_ap_stop_params_validate(&as), -EINVAL, "ap stop bad channel");
	as.center_freq = 0;
	as.iftype = 2;
	CHECK_EQ(hi5622_ap_stop_params_validate(&as), -EINVAL, "ap stop iftype");

	memset(&res, 0, sizeof(res));
	res.bssid[0] = 0x02; res.bssid[5] = 0xaa;
	res.freq = 2437;
	res.frame_type = HI5622_SCAN_FTYPE_BEACON;
	res.signal_mbm = -4200;
	res.ssid_len = 4;
	memcpy(res.ssid, "test", 4);
	CHECK_EQ(hi5622_scan_result_validate(&res), 0, "scan result valid");
	CHECK_EQ(hi5622_scan_result_validate(NULL), -EINVAL, "scan result NULL");
	memset(&res, 0, sizeof(res));
	res.freq = 2437;
	CHECK_EQ(hi5622_scan_result_validate(&res), -EINVAL, "result zero bssid");
	res.bssid[0] = 0x01;
	CHECK_EQ(hi5622_scan_result_validate(&res), -EINVAL, "result mcast bssid");
	memset(&res, 0, sizeof(res));
	res.bssid[0] = 0x02; res.bssid[5] = 1;
	CHECK_EQ(hi5622_scan_result_validate(&res), -EINVAL, "result no freq");
	res.freq = 2484;
	CHECK_EQ(hi5622_scan_result_validate(&res), -EINVAL, "result bad freq");
	res.freq = 2437;
	res.ie_len = HI5622_SCAN_MAX_IE + 1;
	CHECK_EQ(hi5622_scan_result_validate(&res), -EINVAL, "result IE too big");
	res.ie_len = 0;
	res.ssid_len = 33;
	CHECK_EQ(hi5622_scan_result_validate(&res), -EINVAL, "result ssid too long");
	res.ssid_len = 0;
	res.frame_type = HI5622_SCAN_FTYPE_PRESP + 1;
	CHECK_EQ(hi5622_scan_result_validate(&res), -EINVAL, "result ftype");
	res.frame_type = 0;
	res.signal_mbm = -15001;
	CHECK_EQ(hi5622_scan_result_validate(&res), -EINVAL, "result signal low");
	res.signal_mbm = 5001;
	CHECK_EQ(hi5622_scan_result_validate(&res), -EINVAL, "result signal high");
	res.signal_mbm = -4200;
	CHECK_EQ(hi5622_scan_result_validate(&res), 0, "result signal re-ok");
}

/* ------------------------------------------------------------------ */
/* 3. event classification                                             */
/* ------------------------------------------------------------------ */
static void test_classify(void)
{
	CHECK_EQ(hi5622_scan_classify(1, 0x0f), HI5622_SCAN_EV_BSS, "g1 15 bss");
	CHECK_EQ(hi5622_scan_classify(1, 0x10), HI5622_SCAN_EV_COMPLETE,
		 "g1 16 complete");
	CHECK_EQ(hi5622_scan_classify(1, 0x11), HI5622_SCAN_EV_CHANNEL,
		 "g1 17 channel");
	CHECK_EQ(hi5622_scan_classify(2, 0x02), HI5622_SCAN_EV_STA_RSP,
		 "g2 2 sta rsp");
	CHECK_EQ(hi5622_scan_classify(1, 0x12), HI5622_SCAN_EV_NONE,
		 "g1 0x12 not scan");
	CHECK_EQ(hi5622_scan_classify(4, 0x01), HI5622_SCAN_EV_NONE,
		 "g4 1 not scan");
	CHECK_EQ(hi5622_scan_classify(0, 0x0f), HI5622_SCAN_EV_NONE,
		 "g0 not scan");
	CHECK_EQ(hi5622_scan_classify(2, 0x03), HI5622_SCAN_EV_NONE,
		 "g2 3 station event not scan");
	CHECK(strcmp(hi5622_scan_event_name(HI5622_SCAN_EV_COMPLETE),
		     "complete") == 0, "event name complete");
	CHECK(strcmp(hi5622_scan_step_name(HI5622_SCAN_FINISH_ABORT),
		     "finish-abort") == 0, "step name abort");
	CHECK(strcmp(hi5622_scan_phase_name(HI5622_SCAN_FINISHED),
		     "finished") == 0, "phase name finished");
}

/* ------------------------------------------------------------------ */
/* 4. scan model                                                       */
/* ------------------------------------------------------------------ */
static void test_model(void)
{
	struct hi5622_scan_model m;
	char status[1024];
	u32 i;

	hi5622_scan_model_init(&m);
	CHECK_EQ(m.phase, HI5622_SCAN_IDLE, "model starts idle");
	CHECK_EQ(m.timeout_ms, HI5622_SCAN_TIMEOUT_MS, "default timeout");
	CHECK(!hi5622_scan_model_active(&m), "idle model not active");

	CHECK_EQ(hi5622_scan_model_start(&m, 1000, 500), 0, "scan start");
	CHECK(hi5622_scan_model_active(&m), "model active after start");
	CHECK_EQ(m.started_ms, 1000, "start time");
	CHECK_EQ(m.deadline_ms, 1500, "deadline");
	CHECK_EQ(m.starts, 1, "start counter");
	CHECK_EQ(hi5622_scan_model_start(&m, 1100, 500), -EBUSY,
		 "double start refused");
	CHECK_EQ(hi5622_scan_model_tick(&m, 1499), HI5622_SCAN_WAIT,
		 "before deadline waits");

	for (i = 0; i < HI5622_SCAN_MAX_RESULTS + 5; i++)
		(void)hi5622_scan_model_event(&m, HI5622_SCAN_EV_BSS, 1200);
	CHECK_EQ(m.results, HI5622_SCAN_MAX_RESULTS, "result cap");
	CHECK_EQ(m.result_overflow, 5, "overflow counted");
	CHECK_EQ(m.bss_events, HI5622_SCAN_MAX_RESULTS + 5, "bss event counter");
	(void)hi5622_scan_model_event(&m, HI5622_SCAN_EV_CHANNEL, 1200);
	CHECK_EQ(m.channel_events, 1, "channel counter");
	/* g2 id2 (STA completion) is checked in test_model_completion: it now
	 * completes the scan, so it must not be injected before this timeout. */
	CHECK_EQ(hi5622_scan_model_tick(&m, 1500), HI5622_SCAN_FINISH_ABORT,
		 "deadline aborts");

	/* complete event wins over a pending timeout */
	hi5622_scan_model_init(&m);
	CHECK_EQ(hi5622_scan_model_start(&m, 2000, 500), 0, "scan start 2");
	CHECK_EQ(hi5622_scan_model_event(&m, HI5622_SCAN_EV_COMPLETE, 2100),
		 HI5622_SCAN_FINISH_OK, "complete => finish ok");
	CHECK(m.complete_seen, "complete latched");
	CHECK_EQ(m.complete_events, 1, "complete counter");
	CHECK_EQ(hi5622_scan_model_tick(&m, 2100), HI5622_SCAN_FINISH_OK,
		 "tick sees complete");
	hi5622_scan_model_finish(&m, HI5622_SCAN_FINISH_OK);
	CHECK_EQ(m.phase, HI5622_SCAN_FINISHED, "finished phase");
	CHECK_EQ(m.finishes_ok, 1, "finish ok counter");
	CHECK_EQ(hi5622_scan_model_start(&m, 2200, 500), -EBUSY,
		 "start while finished refused");
	CHECK_EQ(hi5622_scan_model_event(&m, HI5622_SCAN_EV_BSS, 2200),
		 HI5622_SCAN_WAIT, "event while finished ignored");
	CHECK_EQ(m.events_when_idle, 1, "idle event counter");
	hi5622_scan_model_clear(&m);
	CHECK_EQ(m.phase, HI5622_SCAN_IDLE, "clear returns to idle");
	CHECK_EQ(hi5622_scan_model_start(&m, 2300, 500), 0, "scan after clear");
	hi5622_scan_model_finish(&m, HI5622_SCAN_FINISH_OK);
	hi5622_scan_model_clear(&m);

	/* abort path */
	hi5622_scan_model_init(&m);
	CHECK_EQ(hi5622_scan_model_abort(&m), 0, "abort idle tolerated");
	CHECK_EQ(hi5622_scan_model_start(&m, 3000, 500), 0, "scan start 3");
	CHECK_EQ(hi5622_scan_model_abort(&m), 0, "abort active");
	CHECK(m.abort_requested, "abort latched");
	CHECK_EQ(hi5622_scan_model_tick(&m, 3100), HI5622_SCAN_FINISH_ABORT,
		 "abort => finish abort");
	hi5622_scan_model_finish(&m, HI5622_SCAN_FINISH_ABORT);
	CHECK_EQ(m.finishes_abort, 1, "finish abort counter");
	CHECK_EQ(m.aborts_requested, 1, "aborts requested counter");
	hi5622_scan_model_clear(&m);

	/* timeout path */
	hi5622_scan_model_init(&m);
	CHECK_EQ(hi5622_scan_model_start(&m, 4000, 100), 0, "scan start 4");
	CHECK_EQ(hi5622_scan_model_tick(&m, 4099), HI5622_SCAN_WAIT,
		 "just before timeout waits");
	CHECK_EQ(hi5622_scan_model_tick(&m, 4100), HI5622_SCAN_FINISH_ABORT,
		 "timeout aborts");
	hi5622_scan_model_finish(&m, HI5622_SCAN_FINISH_ABORT);
	CHECK_EQ(m.timeouts, 1, "timeout counter");
	hi5622_scan_model_clear(&m);

	/* notes + status */
	hi5622_scan_note_dropped_busy(&m);
	hi5622_scan_note_parse_error(&m);
	hi5622_scan_note_invalid_result(&m);
	hi5622_scan_note_inform_failure(&m);
	hi5622_scan_note_error(&m, "scan post", -EBUSY);
	CHECK_EQ(m.dropped_busy, 1, "dropped_busy note");
	CHECK_EQ(m.parse_errors, 1, "parse error note");
	CHECK_EQ(m.invalid_results, 1, "invalid result note");
	CHECK_EQ(m.inform_failures, 1, "inform failure note");
	CHECK(strstr(m.last_error, "scan post") != NULL, "last error text");
	CHECK(hi5622_scan_status_line(&m, status, sizeof(status)) > 0,
	      "status line writes");
	CHECK(strstr(status, "scan:") != NULL, "status has scan tag");
	CHECK(strstr(status, "phase=") != NULL, "status has phase");
	CHECK(strstr(status, "dropped_busy=") != NULL, "status has drops");
	{
		char tiny[16];
		int n = hi5622_scan_status_line(&m, tiny, sizeof(tiny));

		CHECK(n > 0 && (size_t)n < sizeof(tiny), "status truncation bounded");
	}
}

/* ------------------------------------------------------------------ */
/* 5. test-double provider: g0 id9 del_user round-trip                 */
/* ------------------------------------------------------------------ */
static void test_provider_wire_roundtrip(void)
{
	struct hi5622_frame_ops ops = make_full_ops();
	struct hi5622_vap_del_params p;
	struct h2d_frame f;
	enum h2d_err err = H2D_OK;
	u8 wire[HI5622_FRAME_MAX_TOTAL];
	size_t len = 0;
	int rc;

	memset(&p, 0, sizeof(p));
	p.user_id = 1;
	p.mac[0] = 0x02; p.mac[1] = 0x00; p.mac[2] = 0x68; p.mac[3] = 0x69;
	rc = ops.build_user_del(wire, sizeof(wire), &len, &p);
	CHECK_EQ(rc, 0, "test builder builds g0 id9");
	CHECK(len >= HI5622_FRAME_HDR_LEN && len <= HI5622_FRAME_MAX_TOTAL,
	      "built wire length bounded");
	/* The policy assertion (H2D_F_EXACT) lives in the in-memory frame, not on
	 * the wire; the kernel post path validates structure + firmware handler
	 * exactly like this.  The builder is exact, so its frame carries the
	 * EXACT marker before export. */
	{
		struct h2d_frame fe;
		const u8 addr4[4] = { 0x02, 0x00, 0x68, 0x69 };

		memset(&fe, 0, sizeof(fe));
		/* non-wildcard user id deletes by id: the 12-byte body carries no
		 * address; the wildcard 0x0fff form carries the 4-byte address. */
		CHECK_EQ(h2d_build_g0_id09_del_user(&fe, 0, 1, NULL, NULL),
			 0, "exact g0 id9 builder accepts user id 1");
		CHECK((fe.policy & H2D_F_EXACT) != 0,
		      "exact builder marks the frame H2D_F_EXACT");
		CHECK_EQ(h2d_build_g0_id09_del_user(&fe, 0, 0x0fffu, addr4,
						    NULL),
			 0, "exact g0 id9 builder accepts wildcard+address");
	}
	rc = h2d_frame_decode(wire, len, &f,
			      H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
			      H2D_VF_HANDLER, &err);
	CHECK_EQ(rc, 0, "built wire passes inherited structural+handler validation");
	CHECK_EQ(f.group, 0, "built frame group 0");
	CHECK_EQ(f.id, 9, "built frame id 9");
	CHECK_EQ(f.payload_len, 12, "built frame 12-byte payload");
	CHECK(h2d_fw_handler_lookup(f.group, f.id) != NULL,
	      "built frame has a firmware handler");
	/* a zero-length buffer must be refused by the builder, never truncated */
	{
		u8 tiny[4];
		size_t n = 0;

		CHECK(ops.build_user_del(tiny, sizeof(tiny), &n, &p) != 0,
		      "short buffer refused by builder");
	}
}


/* ------------------------------------------------------------------ */
/* 6. task-110 wifi-vap-scan builders: mapping + tolerant D2H decode   */
/* ------------------------------------------------------------------ */
static void fill_scan_2g(struct hi5622_scan_req_params *p)
{
	memset(p, 0, sizeof(*p));
	p->band = 0;
	p->iftype = 2;
	p->n_ssids = 1;
	p->ssid_len[0] = 4;
	memcpy(p->ssid[0], "test", 4);
	p->n_channels = 3;
	p->freq[0] = 2412;
	p->freq[1] = 2437;
	p->freq[2] = 2462;
}

static void fill_vap_add(struct hi5622_vap_add_params *p, u8 band)
{
	memset(p, 0, sizeof(*p));
	strcpy(p->name, "wlan0");
	p->mac[0] = 0x02; p->mac[1] = 0x00; p->mac[2] = 0x68;
	p->mac[3] = 0x69; p->mac[4] = 0x35; p->mac[5] = 0x01;
	p->iftype = 3;
	p->band = band;
	p->user_id = 1;
	p->vap_id = 0;
}

static void test_vapscan_errno_and_guards(void)
{
	struct vs_frame f;
	struct hi5622_scan_req_params p;

	CHECK_EQ(hi5622_vapscan_errno(VS_OK), 0, "vs errno ok");
	CHECK_EQ(hi5622_vapscan_errno(VS_EARG), -EINVAL, "vs errno arg");
	CHECK_EQ(hi5622_vapscan_errno(VS_EBUFFER), -ENOSPC, "vs errno buffer");
	CHECK_EQ(hi5622_vapscan_errno(VS_EPOLICY), -EPERM, "vs errno policy");
	CHECK_EQ(hi5622_vapscan_errno(VS_EID), -EOPNOTSUPP, "vs errno id");

	memset(&f, 0, sizeof(f));
	CHECK_EQ(hi5622_vapscan_scan_req(NULL, &p), -EINVAL, "scan map NULL frame");
	CHECK_EQ(hi5622_vapscan_user_add(NULL, NULL), -EINVAL, "user add map NULL");
	fill_scan_2g(&p);
	p.band = 2;
	CHECK_EQ(hi5622_vapscan_scan_req(&f, &p), -EINVAL, "scan map bad band");
}

static void test_vapscan_scan_frames(void)
{
	static struct vs_frame f;
	struct hi5622_scan_req_params p;
	struct h2d_frame hf;
	enum h2d_err herr = H2D_OK;
	u8 wire[HI5622_FRAME_MAX_TOTAL];
	size_t len = 0;
	u8 family = 0, chip = 0xff, vap = 0xff;
	u16 plen = 0;
	int rc;

	/* 6.1 passive, whole-band 2.4 GHz scan (the conservative default) */
	memset(&p, 0, sizeof(p));
	p.band = 0;
	rc = hi5622_vapscan_scan_req(&f, &p);
	CHECK_EQ(rc, 0, "passive whole-band scan builds");
	CHECK_EQ(f.len, VS_HDR_LEN + VS_LEN_SCAN_REQ, "scan frame total 520");
	CHECK_EQ(vs_frame_validate(f.buf, f.len), VS_OK, "scan frame vs-valid");
	rc = vs_frame_decode(f.buf, f.len, &family, &chip, &vap, &plen);
	CHECK_EQ(rc, VS_OK, "scan frame decodes");
	CHECK_EQ(family, VS_ID_SCAN_REQ, "scan frame id 0xC");
	CHECK_EQ(chip, 0, "scan frame 2.4 GHz chip id");
	CHECK_EQ(plen, VS_LEN_SCAN_REQ, "scan payload 508 B");
	CHECK_EQ(f.buf[VS_HDR_LEN + VS_SC_OFF_N_SSIDS], 0, "passive no ssid");
	CHECK_EQ(f.buf[VS_HDR_LEN + VS_SC_OFF_CHAN_COUNT], 0,
		 "whole band no explicit channels");
	CHECK_EQ(f.buf[VS_HDR_LEN + VS_SC_OFF_DWELL], 60, "dwell 60 ms");
	CHECK_EQ(f.buf[VS_HDR_LEN + VS_SC_OFF_SCAN_TYPE], 0, "scan_type 0");

	/* 6.2 the completed frame passes the inherited post-gate validation */
	rc = vs_frame_export(&f, wire, sizeof(wire), &len);
	CHECK_EQ(rc, VS_OK, "scan frame exports to SR buffer");
	CHECK_EQ(len, f.len, "export length matches");
	rc = h2d_frame_decode(wire, len, &hf,
			      H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
			      H2D_VF_HANDLER, &herr);
	CHECK_EQ(rc, 0, "scan frame passes inherited structural validation");
	CHECK_EQ(hf.group, 0, "scan frame group 0");
	CHECK_EQ(hf.id, 0x0c, "scan frame id from header");
	CHECK_EQ(hf.payload_len, 508, "scan payload length from header");
	CHECK(h2d_fw_handler_lookup(0, 0x0c) != NULL,
	      "scan id 0xC has a firmware handler");

	/* 6.3 SSIDs + explicit 2.4 GHz channels -> vendor offsets */
	fill_scan_2g(&p);
	rc = hi5622_vapscan_scan_req(&f, &p);
	CHECK_EQ(rc, 0, "active scan with ssid/channels builds");
	CHECK_EQ(f.buf[VS_HDR_LEN + VS_SC_OFF_N_SSIDS], 1, "one ssid");
	CHECK_EQ(f.buf[VS_HDR_LEN + VS_SC_OFF_SSID_AREA], 't', "ssid string");
	CHECK_EQ(f.buf[VS_HDR_LEN + VS_SC_OFF_SSID_AREA + 4], 0, "ssid NUL");
	CHECK_EQ(f.buf[VS_HDR_LEN + VS_SC_OFF_CHAN_COUNT], 3, "three channels");
	CHECK_EQ(f.buf[VS_HDR_LEN + VS_SC_OFF_CHAN_AREA + 0], 1, "ch1 num");
	CHECK_EQ(f.buf[VS_HDR_LEN + VS_SC_OFF_CHAN_AREA + 3], 0, "ch1 idx");
	CHECK_EQ(f.buf[VS_HDR_LEN + VS_SC_OFF_CHAN_AREA + 4], 6, "ch6 num");
	CHECK_EQ(f.buf[VS_HDR_LEN + VS_SC_OFF_CHAN_AREA + 7], 5, "ch6 idx");
	CHECK_EQ(f.buf[VS_HDR_LEN + VS_SC_OFF_CHAN_AREA + 8], 11, "ch11 num");
	CHECK_EQ(f.buf[VS_HDR_LEN + VS_SC_OFF_CHAN_AREA + 11], 10, "ch11 idx");
	CHECK_EQ(f.buf[1] >> 4, 0, "broadcast dst 0");

	/* 6.4 5 GHz band: channel number and vendor index mapping [I] */
	memset(&p, 0, sizeof(p));
	p.band = 1;
	p.n_channels = 2;
	p.freq[0] = 5180;
	p.freq[1] = 5700;
	rc = hi5622_vapscan_scan_req(&f, &p);
	CHECK_EQ(rc, 0, "5 GHz scan builds");
	CHECK_EQ(f.buf[3], 1, "5 GHz chip id 1");
	CHECK_EQ(f.buf[VS_HDR_LEN + VS_SC_OFF_CHAN_AREA + 0], 36, "ch36 num");
	CHECK_EQ(f.buf[VS_HDR_LEN + VS_SC_OFF_CHAN_AREA + 1], 1, "ch36 band 1");
	CHECK_EQ(f.buf[VS_HDR_LEN + VS_SC_OFF_CHAN_AREA + 3], 0, "ch36 idx 0");
	CHECK_EQ(f.buf[VS_HDR_LEN + VS_SC_OFF_CHAN_AREA + 4], 140, "ch140 num");
	CHECK_EQ(f.buf[VS_HDR_LEN + VS_SC_OFF_CHAN_AREA + 7], 18, "ch140 idx 18");

	/* 6.5 bounds / unsupported inputs fail closed */
	fill_scan_2g(&p);
	p.ie_len = 1;
	CHECK_EQ(hi5622_vapscan_scan_req(&f, &p), -EOPNOTSUPP,
		 "scan with IE refused (builder has no IE field)");
	fill_scan_2g(&p);
	p.n_ssids = VS_SC_SSID_MAX + 1;
	CHECK_EQ(hi5622_vapscan_scan_req(&f, &p), -E2BIG, "too many ssids");
	fill_scan_2g(&p);
	p.n_channels = VS_SC_CHAN_MAX + 1;
	CHECK_EQ(hi5622_vapscan_scan_req(&f, &p), -E2BIG, "too many channels");
	fill_scan_2g(&p);
	p.freq[1] = 2413;
	CHECK_EQ(hi5622_vapscan_scan_req(&f, &p), -EINVAL, "off-grid channel");
	fill_scan_2g(&p);
	p.freq[1] = 5180;
	CHECK_EQ(hi5622_vapscan_scan_req(&f, &p), -EINVAL, "mixed band refused");
}

static void test_vapscan_user_frames(void)
{
	static struct vs_frame f;
	struct hi5622_vap_add_params va;
	struct hi5622_vap_del_params vd;
	struct h2d_frame hf;
	enum h2d_err herr = H2D_OK;
	u8 wire[HI5622_FRAME_MAX_TOTAL];
	size_t len = 0;
	u8 family = 0;
	int rc;

	/* 7.1 user add (g0 id7) */
	fill_vap_add(&va, 0);
	rc = hi5622_vapscan_user_add(&f, &va);
	CHECK_EQ(rc, 0, "user_add builds");
	CHECK_EQ(f.len, VS_HDR_LEN + VS_LEN_USER_ADD, "user_add total 180");
	CHECK_EQ(vs_frame_validate(f.buf, f.len), VS_OK, "user_add vs-valid");
	rc = vs_frame_decode(f.buf, f.len, &family, NULL, NULL, NULL);
	CHECK_EQ(rc, VS_OK, "user_add decodes");
	CHECK_EQ(family, VS_ID_USER_ADD, "user_add id 7");
	CHECK_EQ(f.buf[VS_HDR_LEN + VS_UA_OFF_UID], 1, "user_add uid");
	CHECK_EQ(f.buf[VS_HDR_LEN + VS_UA_OFF_MAC], 0x02, "user_add mac[0]");
	CHECK_EQ(f.buf[VS_HDR_LEN + VS_UA_OFF_TYPE], 2, "user_add type 2");
	CHECK_EQ((u8)f.buf[VS_HDR_LEN + VS_UA_OFF_RSSI], 100,
		 "user_add vendor rssi +100");
	rc = vs_frame_export(&f, wire, sizeof(wire), &len);
	CHECK_EQ(rc, VS_OK, "user_add exports");
	rc = h2d_frame_decode(wire, len, &hf,
			      H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
			      H2D_VF_HANDLER, &herr);
	CHECK_EQ(rc, 0, "user_add passes inherited structural validation");
	CHECK_EQ(hf.id, 7, "user_add header id 7");

	/* 7.2 user notify-alg (g0 id8): the task-110 name correction */
	fill_vap_add(&va, 1);
	rc = hi5622_vapscan_user_alg(&f, &va);
	CHECK_EQ(rc, 0, "user_add_notify_alg builds");
	CHECK_EQ(f.len, VS_HDR_LEN + VS_LEN_USER_ALG, "user_alg total 180");
	rc = vs_frame_decode(f.buf, f.len, &family, NULL, NULL, NULL);
	CHECK_EQ(rc, VS_OK, "user_alg decodes");
	CHECK_EQ(family, VS_ID_USER_ALG, "user_alg id 8");
	CHECK_EQ(f.buf[3], 1, "user_alg 5 GHz chip id");
	CHECK_EQ(f.buf[VS_HDR_LEN + VS_ALG_OFF_NSS], 0,
		 "user_alg zero caps/nss (not invented)");

	/* 7.3 delete by user id (g0 id9) */
	memset(&vd, 0, sizeof(vd));
	vd.user_id = 1;
	memcpy(vd.mac, va.mac, 6);
	vd.band = 0;
	vd.vap_id = 0;
	rc = hi5622_vapscan_user_del(&f, &vd);
	CHECK_EQ(rc, 0, "user_del builds");
	CHECK_EQ(f.len, VS_HDR_LEN + VS_LEN_USER_DEL, "user_del total 24");
	rc = vs_frame_decode(f.buf, f.len, &family, NULL, NULL, NULL);
	CHECK_EQ(rc, VS_OK, "user_del decodes");
	CHECK_EQ(family, VS_ID_USER_DEL, "user_del id 9");
	CHECK_EQ(f.buf[VS_HDR_LEN + VS_UD_OFF_UID], 1, "user_del uid");
	CHECK_EQ(f.buf[VS_HDR_LEN + VS_UD_OFF_FLAG], 0,
		 "user_del flag 0 (semantics [U])");

	/* 7.4 delete by MAC sentinel 0x0fff */
	vd.user_id = 0x0fff;
	rc = hi5622_vapscan_user_del(&f, &vd);
	CHECK_EQ(rc, 0, "user_del by MAC builds");
	CHECK_EQ(f.buf[VS_HDR_LEN + VS_UD_OFF_UID], 0xff, "wildcard uid low byte");
	CHECK_EQ(f.buf[VS_HDR_LEN + VS_UD_OFF_UID + 1], 0x0f,
		 "wildcard uid high nibble");

	/* 7.5 an invalid band is refused before the builder */
	fill_vap_add(&va, 0);
	va.band = 2;
	CHECK_EQ(hi5622_vapscan_user_add(&f, &va), -EINVAL, "bad band refused");
}

/* build one synthetic 802.11 beacon/probe-response body + 20 B trailer */
static u32 build_scan_body(u8 *body, u8 subtype, bool with_ds, bool with_ht)
{
	u32 o = 0;
	u8 i;

	memset(body, 0, 512);
	body[o++] = (u8)(subtype << 4);	/* frame control low: mgmt */
	body[o++] = 0x00;
	body[o++] = 0; body[o++] = 0;	/* duration */
	for (i = 0; i < 6; i++)		/* addr1 broadcast */
		body[o++] = 0xff;
	for (i = 0; i < 6; i++)		/* addr2 */
		body[o++] = (u8)(0x10 + i);
	for (i = 0; i < 6; i++)		/* addr3 = BSSID */
		body[o++] = (u8)(0x02 + i);
	body[o++] = 0; body[o++] = 0;	/* seq */
	for (i = 0; i < 8; i++)		/* timestamp */
		body[o++] = (u8)(0x10 + i);
	body[o++] = 100; body[o++] = 0;	/* beacon interval */
	body[o++] = 0x11; body[o++] = 0x04;	/* capability */
	body[o++] = 0; body[o++] = 4;	/* SSID IE */
	memcpy(body + o, "test", 4);
	o += 4;
	if (with_ds) {
		body[o++] = 3; body[o++] = 1; body[o++] = 6;	/* DS ch6 */
	}
	body[o++] = 221; body[o++] = 3;	/* vendor IE copied verbatim */
	body[o++] = 0xaa; body[o++] = 0xbb; body[o++] = 0xcc;
	if (with_ht) {
		body[o++] = 61; body[o++] = 22;	/* HT operation */
		body[o++] = 36;			/* primary channel (body[0]) */
		o += 21;
	}
	memset(body + o, 0xee, 20);		/* [U] trailing descriptor */
	o += 20;
	return o;
}

static void test_vapscan_scan_decode(void)
{
	struct hi5622_scan_result r;
	struct hi5622_scan_complete cc;
	struct hi5622_scan_channel ch;
	struct hi5622_scan_sta_complete sc;
	u8 body[512];
	u8 payload[128];
	u32 len;
	int rc;
	u16 i;

	/* 8.1 beacon + 20-byte trailer -> 2.4 GHz result (task-118 decoder) */
	len = build_scan_body(body, 8, true, false);
	rc = hi5622_vapscan_decode_scan_result(body, len, &r);
	CHECK_EQ(rc, 0, "beacon scan result decodes (task-118)");
	CHECK_EQ(r.frame_type, HI5622_SCAN_FTYPE_BEACON, "decoded ftype beacon");
	CHECK_EQ(r.freq, 2437, "decoded DS channel freq");
	CHECK_EQ(r.beacon_interval, 100, "decoded beacon interval");
	CHECK_EQ(r.capability, 0x0411, "decoded capability");
	CHECK_EQ(r.ssid_len, 4, "decoded SSID length");
	CHECK(memcmp(r.ssid, "test", 4) == 0, "decoded SSID");
	CHECK_EQ(r.bssid[0], 0x02, "decoded BSSID");
	CHECK_EQ(r.tsf, 0x1716151413121110ULL, "decoded TSF");
	CHECK_EQ(r.signal_mbm, 0, "signal reported UNKNOWN (0 mBm) [U]");
	CHECK(r.ie_len > 0, "IEs copied");
	CHECK_EQ(hi5622_scan_result_validate(&r), 0,
		 "decoded result passes cfg80211 validation");

	/* 8.2 probe response without DS but with HT operation body[0]=36 */
	len = build_scan_body(body, 5, false, true);
	rc = hi5622_vapscan_decode_scan_result(body, len, &r);
	CHECK_EQ(rc, 0, "probe response decodes via HT op primary channel");
	CHECK_EQ(r.frame_type, HI5622_SCAN_FTYPE_PRESP, "decoded ftype presp");
	CHECK_EQ(r.freq, 5180, "decoded HT primary channel freq");
	CHECK_EQ(hi5622_scan_result_validate(&r), 0,
		 "5 GHz decoded result passes cfg80211 validation");

	/* 8.3 a bare frame (no trailer) is also a valid layout */
	len = build_scan_body(body, 8, true, false) - 20u;
	rc = hi5622_vapscan_decode_scan_result(body, len, &r);
	CHECK_EQ(rc, 0, "bare frame layout accepted");
	CHECK_EQ(r.freq, 2437, "bare frame DS freq");

	/* 8.4 tolerant failures (never a fake BSS) */
	body[0] = 0x08;	/* data frame, not management */
	CHECK_EQ(hi5622_vapscan_decode_scan_result(body, 512, &r),
		 -EOPNOTSUPP, "non-management frame refused");
	len = build_scan_body(body, 8, false, false);	/* no DS/HT */
	CHECK_EQ(hi5622_vapscan_decode_scan_result(body, len, &r),
		 -EOPNOTSUPP, "no channel IE -> not decodable");
	CHECK_EQ(hi5622_vapscan_decode_scan_result(body, 20, &r),
		 -EOPNOTSUPP, "too short -> not decodable");
	CHECK_EQ(hi5622_vapscan_decode_scan_result(NULL, 100, &r), -EINVAL,
		 "NULL payload refused");
	len = build_scan_body(body, 8, true, false);
	body[24 + 12 + 1] = 0xfe;	/* SSID IE length overruns the frame */
	CHECK_EQ(hi5622_vapscan_decode_scan_result(body, len, &r),
		 -EOPNOTSUPP, "truncated IE chain -> layout refused");

	/* 8.5 g1 id16 completion: success == 1, anything else is an abort */
	memset(payload, 0, sizeof(payload));
	payload[0] = 1;
	CHECK_EQ(hi5622_vapscan_decode_scan_complete(payload, 16, &cc), 0,
		 "completion status decodes");
	CHECK_EQ(cc.reason, 1, "completion reason/status 1");
	CHECK_EQ(cc.aborted, 0, "status 1 is success");
	payload[0] = 2;
	CHECK_EQ(hi5622_vapscan_decode_scan_complete(payload, 16, &cc), 0,
		 "completion failure status decodes");
	CHECK_EQ(cc.aborted, 1, "non-1 status maps to aborted");

	/* 8.6 g1 id17 channel result: payload[0]=slot, payload+4 block */
	memset(payload, 0, sizeof(payload));
	payload[0] = 3;		/* slot */
	payload[4] = 6;		/* channel number in the record */
	CHECK_EQ(hi5622_vapscan_decode_scan_channel(payload, 124, &ch), 0,
		 "channel result decodes (fixed 124 B frame)");
	CHECK_EQ(ch.slot, 3, "channel result slot");
	CHECK_EQ(ch.chan_num, 6, "channel result channel");
	CHECK_EQ(ch.freq, 2437, "channel result freq");
	CHECK_EQ(ch.is_5g, 0, "channel result 2.4 GHz");

	/* 8.7 g2 id2 WAL/STA scan completion */
	memset(payload, 0, sizeof(payload));
	payload[0] = 0x01;
	payload[0x0d] = 1;
	CHECK_EQ(hi5622_vapscan_decode_scan_sta_complete(payload, 32, &sc), 0,
		 "STA completion decodes");
	CHECK_EQ(sc.status, 1, "STA completion status");
	CHECK_EQ(sc.core_band, 0, "STA completion core/band");
	(void)i;
}

/* ------------------------------------------------------------------ */
/* 5b. firmware completion semantics (task-118 status bytes)           */
/* ------------------------------------------------------------------ */
static void test_model_completion(void)
{
	struct hi5622_scan_model m;

	hi5622_scan_model_init(&m);
	CHECK_EQ(hi5622_scan_model_start(&m, 1000, 500), 0, "completion scan start");
	CHECK_EQ(hi5622_scan_model_complete(&m, 1, 0, 1100),
		 HI5622_SCAN_FINISH_OK, "status 1 => finish ok");
	CHECK(m.complete_seen, "completion latched");
	CHECK_EQ(m.complete_reason, 1, "completion reason stored");
	CHECK_EQ(m.complete_failures, 0, "no completion failure");
	CHECK_EQ(hi5622_scan_model_tick(&m, 1100), HI5622_SCAN_FINISH_OK,
		 "tick honours successful completion");
	hi5622_scan_model_finish(&m, HI5622_SCAN_FINISH_OK);
	CHECK_EQ(m.finishes_ok, 1, "finish ok counted");
	hi5622_scan_model_clear(&m);

	hi5622_scan_model_init(&m);
	CHECK_EQ(hi5622_scan_model_start(&m, 2000, 500), 0, "failure scan start");
	CHECK_EQ(hi5622_scan_model_complete(&m, 2, 1, 2100),
		 HI5622_SCAN_FINISH_ABORT, "failure status => finish abort");
	CHECK(m.complete_failed, "failed completion latched");
	CHECK_EQ(m.complete_failures, 1, "completion failure counted");
	CHECK_EQ(hi5622_scan_model_tick(&m, 2100), HI5622_SCAN_FINISH_ABORT,
		 "tick honours failed completion");
	hi5622_scan_model_finish(&m, HI5622_SCAN_FINISH_ABORT);
	CHECK_EQ(m.finishes_abort, 1, "abort finish counted");
	CHECK_EQ(m.timeouts, 0, "failed completion is not a timeout");
	hi5622_scan_model_clear(&m);
	CHECK(!m.complete_failed, "clear resets completion failure");

	/* g2 id2 wal_scan_comp_proc_sta is the vendor cfg80211 feed/completion
	 * point [V]: a completion candidate exactly like g1 id16. */
	hi5622_scan_model_init(&m);
	CHECK_EQ(hi5622_scan_model_start(&m, 3000, 500), 0, "sta comp scan start");
	CHECK_EQ(hi5622_scan_model_event(&m, HI5622_SCAN_EV_STA_RSP, 3100),
		 HI5622_SCAN_FINISH_OK, "g2 id2 completes the scan");
	CHECK(m.complete_seen, "g2 id2 latches completion");
	CHECK_EQ(m.sta_events, 1, "g2 id2 counted");
	CHECK_EQ(hi5622_scan_model_tick(&m, 3100), HI5622_SCAN_FINISH_OK,
		 "tick after g2 id2 reports finish");
	hi5622_scan_model_finish(&m, HI5622_SCAN_FINISH_OK);
	hi5622_scan_model_clear(&m);
	CHECK_EQ(hi5622_scan_model_complete(&m, 1, 0, 3200),
		 HI5622_SCAN_WAIT, "idle completion ignored");
	CHECK_EQ(m.events_when_idle, 1, "idle completion counted");
}

int main(void)
{
	printf("wifi-scan-ap scan logic selftest (task-113), device-free\n");
	test_provider_contract();
	test_validators();
	test_classify();
	test_model();
	test_model_completion();
	test_provider_wire_roundtrip();
	test_vapscan_errno_and_guards();
	test_vapscan_scan_frames();
	test_vapscan_user_frames();
	test_vapscan_scan_decode();
	printf("%d/%d checks passed\n", checks - failed, checks);
	if (failed) {
		printf("FAILED: %d check(s)\n", failed);
		return 1;
	}
	printf("PASS\n");
	return 0;
}
