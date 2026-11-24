/* SPDX-License-Identifier: GPL-2.0 */
/*
 * scan_tuning_selftest.c - device-free checks for the wifi-scan-tuning lane
 * (shared task task-133).  Covers the new portable policy surface:
 *   - retuned auto window (BASE + n*PER_CHAN, override per-channel cost,
 *     clamp raised to 120 s),
 *   - batch splitting helpers (size/count/len, 0 and >= n semantics,
 *     partial last batch, empty plan),
 *   - hi5622_scan_coverage_ready() (all channels seen + settle; never for
 *     abort/finished/empty plans),
 *   - the existing channel-coverage completion rule still fires.
 * Build/run: cc -std=c99 -Wall -Wextra -Werror -pedantic -I<src>
 *            scan_tuning_selftest.c <src>/hi5622_scan_logic.c
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_scan_logic.h"

static int checks;
static int fails;

#define CHECK(cond, msg) do { \
	checks++; \
	if (!(cond)) { \
		fails++; \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
	} \
} while (0)

#define CHECK_EQ(a, b, msg) do { \
	unsigned long long _a = (unsigned long long)(a); \
	unsigned long long _b = (unsigned long long)(b); \
	checks++; \
	if (_a != _b) { \
		fails++; \
		printf("FAIL %s:%d: %s (%llu != %llu)\n", __FILE__, __LINE__, \
		       (msg), _a, _b); \
	} \
} while (0)

static const u16 freqs13[13] = {
	2412, 2417, 2422, 2427, 2432, 2437, 2442,
	2447, 2452, 2457, 2462, 2467, 2472,
};

static void test_window(void)
{
	/* Retuned constants: base 1500, 250/channel, clamp 120 s. */
	CHECK_EQ(HI5622_SCAN_TIMEOUT_BASE_MS, 1500u, "retuned base constant");
	CHECK_EQ(HI5622_SCAN_TIMEOUT_PER_CHAN_MS, 250u, "retuned per-channel constant");
	CHECK_EQ(HI5622_SCAN_TIMEOUT_MAX_MS, 120000u, "45 s clamp raised to 120 s");
	CHECK_EQ(hi5622_scan_timeout_auto(0), 1500u + 13u * 250u,
		 "auto(0) sized as the 13-channel bench band");
	CHECK_EQ(hi5622_scan_timeout_auto(13), 1500u + 13u * 250u,
		 "auto(13)");
	CHECK_EQ(hi5622_scan_timeout_auto(6), 1500u + 6u * 250u,
		 "auto(6) for one batch");
	CHECK_EQ(hi5622_scan_timeout_for(13, 156u), 1500u + 13u * 156u,
		 "measured 156 ms/channel override");
	CHECK_EQ(hi5622_scan_timeout_for(6, 0u), 1500u + 6u * 250u,
		 "0 per-channel override = compiled default");
	/* Clamp: 42 channels * 2000 ms would exceed the cap -> MAX. */
	CHECK_EQ(hi5622_scan_timeout_for(500, 2000u), HI5622_SCAN_TIMEOUT_MAX_MS,
		 "auto window clamped to MAX");
}

static void test_batching(void)
{
	/* 13 channels with the compiled 6-channel firmware limit: 3 requests. */
	CHECK_EQ(HI5622_SCAN_DEFAULT_BATCH, 6u, "compiled batch default is the measured 6");
	CHECK_EQ(hi5622_scan_batch_size(13, 6), 6u, "batch size 6");
	CHECK_EQ(hi5622_scan_batch_count(13, 6), 3u, "13/6 -> 3 requests");
	CHECK_EQ(hi5622_scan_batch_len(13, 6, 0), 6u, "batch 0 = channels 1..6");
	CHECK_EQ(hi5622_scan_batch_len(13, 6, 1), 6u, "batch 1 = channels 7..12");
	CHECK_EQ(hi5622_scan_batch_len(13, 6, 2), 1u, "batch 2 = channel 13");
	CHECK_EQ(hi5622_scan_batch_len(13, 6, 3), 0u, "no batch past the end");
	/* Legacy / disabled forms. */
	CHECK_EQ(hi5622_scan_batch_size(13, 0), 13u, "param 0 -> one request");
	CHECK_EQ(hi5622_scan_batch_count(13, 0), 1u, "param 0 -> one request");
	CHECK_EQ(hi5622_scan_batch_len(13, 0, 0), 13u, "param 0 -> all channels");
	CHECK_EQ(hi5622_scan_batch_size(13, 13), 13u, "param == n -> one request");
	CHECK_EQ(hi5622_scan_batch_size(13, 42), 13u, "param > n -> one request");
	CHECK_EQ(hi5622_scan_batch_size(13, 43), 13u, "param beyond builder max -> n");
	CHECK_EQ(hi5622_scan_batch_count(1, 6), 1u, "single channel -> one request");
	CHECK_EQ(hi5622_scan_batch_len(1, 6, 0), 1u, "single channel length");
	CHECK_EQ(hi5622_scan_batch_count(0, 6), 0u, "empty plan -> no batch");
	CHECK_EQ(hi5622_scan_batch_len(0, 6, 0), 0u, "empty plan length");
	CHECK_EQ(hi5622_scan_batch_count(12, 6), 2u, "exact multiple -> 2");
	CHECK_EQ(hi5622_scan_batch_count(7, 4), 2u, "7/4 -> 2");
	CHECK_EQ(hi5622_scan_batch_len(7, 4, 1), 3u, "7/4 last batch = 3");
}

static void test_coverage_ready(void)
{
	struct hi5622_scan_model m;
	u64 t = 1000;
	u32 i;

	hi5622_scan_model_init(&m);
	CHECK_EQ(hi5622_scan_model_start(&m, t, 5000), 0, "model start");
	CHECK_EQ(hi5622_scan_model_set_channels(&m, freqs13, 6), 0,
		 "plan for one 6-channel batch");
	CHECK(!hi5622_scan_coverage_ready(&m, t), "not ready before any channel");
	for (i = 0; i < 6; i++)
		hi5622_scan_model_channel(&m, freqs13[i], t + 156u * (i + 1u));
	CHECK(!hi5622_scan_coverage_ready(&m, t + 156u * 6u),
	      "not ready inside the settle window");
	CHECK(hi5622_scan_coverage_ready(&m, t + 156u * 6u + HI5622_SCAN_SETTLE_MS),
	      "ready after all channels + settle");
	CHECK_EQ(hi5622_scan_model_tick(&m, t + 156u * 6u + HI5622_SCAN_SETTLE_MS),
		 HI5622_SCAN_FINISH_OK, "model tick also finishes on coverage");

	/* Abort request must suppress the batch advance predicate. */
	hi5622_scan_model_clear(&m);
	hi5622_scan_model_start(&m, t, 5000);
	hi5622_scan_model_set_channels(&m, freqs13, 6);
	for (i = 0; i < 6; i++)
		hi5622_scan_model_channel(&m, freqs13[i], t + 100u * (i + 1u));
	CHECK(hi5622_scan_coverage_ready(&m, t + 1000u + HI5622_SCAN_SETTLE_MS),
	      "ready on the plain path");
	hi5622_scan_model_abort(&m);
	CHECK(!hi5622_scan_coverage_ready(&m, t + 1000u + HI5622_SCAN_SETTLE_MS),
	      "abort suppresses next-batch advance");
}

int main(void)
{
	printf("scan_tuning_selftest (wifi-scan-tuning, task-133)\n");
	test_window();
	test_batching();
	test_coverage_ready();
	printf("%d/%d checks %s\n", checks - fails, checks,
	       fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}

