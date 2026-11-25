/* SPDX-License-Identifier: GPL-2.0 */
/*
 * pm_perf_selftest.c - host selftest for src/hi5622_pm_perf_logic.h
 * (wifi-power-perf lane, task-160).  Compiles with stock gcc, touches no
 * device, needs no kernel tree.  Every check names the policy it pins.
 *
 *   gcc -std=gnu99 -O2 -Wall -Wextra -Werror -I../src pm_perf_selftest.c -o /tmp/pm_perf_selftest
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_pm_perf_logic.h"

static unsigned int checks;
static unsigned int fails;

#define CHECK(cond, what) do {						\
	checks++;							\
	if (!(cond)) {							\
		fails++;						\
		printf("FAIL: %s (line %d)\n", (what), __LINE__);	\
	}								\
} while (0)

static void test_pm_gate(void)
{
	struct hspp_pm_input in;

	/* 0: default state = disabled, whatever else is true. */
	memset(&in, 0, sizeof(in));
	CHECK(hspp_pm_gate(&in) == HSPP_PM_REFUSE_DISABLED,
	      "PM: disabled refuses even before boot");
	in.enabled = 1;
	CHECK(hspp_pm_gate(&in) == HSPP_PM_REFUSE_NOT_READY,
	      "PM: enabled but not latched refuses");
	in.boot_latched = 1;
	CHECK(hspp_pm_gate(&in) == HSPP_PM_REFUSE_NOT_READY,
	      "PM: latched but not ACTIVE refuses");
	in.active_phase = 1;
	CHECK(hspp_pm_gate(&in) == HSPP_PM_OK,
	      "PM: enabled + latched + ACTIVE allows");
	in.pending = 1;
	CHECK(hspp_pm_gate(&in) == HSPP_PM_REFUSE_PENDING,
	      "PM: outstanding ack refuses a second request");
	in.pending = 0;
	in.enabled = 0;
	CHECK(hspp_pm_gate(&in) == HSPP_PM_REFUSE_DISABLED,
	      "PM: disabled wins over every other precondition");
	CHECK(hspp_pm_gate(NULL) == HSPP_PM_REFUSE_DISABLED,
	      "PM: NULL input refuses");
}

static void test_pm_ack_clamp(void)
{
	CHECK(hspp_pm_ack_ms_clamp(0) == HSPP_PM_ACK_MS_DEFAULT,
	      "PM ack: 0 -> 500 default");
	CHECK(hspp_pm_ack_ms_clamp(500) == 500, "PM ack: in range unchanged");
	CHECK(hspp_pm_ack_ms_clamp(49) == HSPP_PM_ACK_MS_MIN,
	      "PM ack: below min clamps up");
	CHECK(hspp_pm_ack_ms_clamp(50) == 50, "PM ack: min accepted");
	CHECK(hspp_pm_ack_ms_clamp(5001) == HSPP_PM_ACK_MS_MAX,
	      "PM ack: above max clamps down");
	CHECK(hspp_pm_ack_ms_clamp(5000) == 5000, "PM ack: max accepted");
}

static void test_poll_governor(void)
{
	struct hspp_poll_gov g;

	hspp_poll_gov_init(&g, 5, 20, 3);
	CHECK(g.active_ms == 5 && g.idle_ms == 20 && g.threshold == 3,
	      "poll: init keeps a sane 5/20/3 configuration");
	CHECK(hspp_poll_gov_step(&g, 0) == 5, "poll: first quiet pass = fast");
	CHECK(hspp_poll_gov_step(&g, 0) == 5, "poll: second quiet pass = fast");
	CHECK(hspp_poll_gov_step(&g, 0) == 20,
	      "poll: third quiet pass reaches the idle cadence");
	CHECK(hspp_poll_gov_interval(&g) == 20, "poll: interval reads back idle");
	CHECK(hspp_poll_gov_step(&g, 1) == 5,
	      "poll: one active pass snaps back to the fast cadence");
	CHECK(hspp_poll_gov_interval(&g) == 5, "poll: interval reads back fast");
	CHECK(hspp_poll_gov_step(&g, 0) == 5, "poll: streak restarted after activity");

	/* idle < active => backoff disabled, always the active cadence. */
	hspp_poll_gov_init(&g, 10, 5, 1);
	CHECK(g.idle_ms == g.active_ms, "poll: idle<active disables backoff");
	CHECK(hspp_poll_gov_step(&g, 0) == 10, "poll: disabled backoff stays fast");

	/* idle == 0 => disabled, same as above. */
	hspp_poll_gov_init(&g, 7, 0, 1);
	CHECK(hspp_poll_gov_step(&g, 0) == 7, "poll: idle=0 stays fast");

	/* threshold 0 is promoted to 1; empty config still sleeps >= 1 ms. */
	hspp_poll_gov_init(&g, 0, 0, 0);
	CHECK(g.active_ms == 1 && g.threshold == 1,
	      "poll: zero inputs become active=1/threshold=1");
	CHECK(hspp_poll_gov_step(NULL, 0) >= 1, "poll: NULL governor still sleeps");

	/* long quiet run does not overflow the streak. */
	hspp_poll_gov_init(&g, 5, 20, 250);
	{
		unsigned int i;
		unsigned int ms = 5;

		for (i = 0; i < 300; i++)
			ms = hspp_poll_gov_step(&g, 0);
		CHECK(ms == 20, "poll: streak saturates, idle cadence reached");
	}
}

static void test_aspm(void)
{
	struct hspp_aspm_state s;

	s = hspp_aspm_decode(0x0000);
	CHECK(!s.enabled && !s.l0s && !s.l1, "ASPM: 0 = off");
	s = hspp_aspm_decode(0x0001);
	CHECK(s.enabled && s.l0s && !s.l1, "ASPM: bit0 = L0s");
	s = hspp_aspm_decode(0x0002);
	CHECK(s.enabled && !s.l0s && s.l1, "ASPM: bit1 = L1");
	s = hspp_aspm_decode(0x0003);
	CHECK(s.enabled && s.l0s && s.l1, "ASPM: both = L0s+L1");
	s = hspp_aspm_decode(0x1433);
	CHECK(s.raw == 0x1433 && s.enabled && s.l0s && s.l1,
	      "ASPM: decode keeps the raw word");
	CHECK(hspp_aspm_disable_bits(0x0003) == 0x0000,
	      "ASPM: clear both bits");
	CHECK(hspp_aspm_disable_bits(0x1433) == 0x1430,
	      "ASPM: clear bits, preserve the rest");
	CHECK(hspp_aspm_disable_bits(0x0000) == 0x0000,
	      "ASPM: clearing an off link is a no-op");
}

static void test_tx_model(void)
{
	struct hspp_air_params p;
	struct hspp_air_model m;

	/* Host window at the shipped defaults: 16 x (1000/5 ms) = 3200 fps. */
	CHECK(hspp_tx_pps(16, 5) == 3200, "TX: pool=16 poll=5ms -> 3200 fps");
	CHECK(hspp_tx_pps(64, 5) == 12800, "TX: ring=64 poll=5ms -> 12800 fps");
	CHECK(hspp_tx_pps(16, 1) == 16000, "TX: pool=16 poll=1ms -> 16000 fps");
	CHECK(hspp_tx_pps(16, 0) == 0, "TX: poll=0 is refused (no divide)");
	CHECK(hspp_tx_pps(0, 5) == 0, "TX: pool=0 -> 0 fps");
	CHECK(hspp_tx_mpdu_bytes_per_s(16, 5, HSPP_TX_MPDU_MAX) ==
	      3200ull * 768ull, "TX: MPDU bytes/s at defaults");
	CHECK(hspp_tx_payload_bytes_per_s(16, 5, HSPP_TX_MPDU_MAX) ==
	      3200ull * 736ull, "TX: payload bytes/s strips 24+8 B");
	CHECK(hspp_tx_payload_bytes_per_s(16, 5, 20) == 0,
	      "TX: MPDU too small for headers -> 0 payload");
	CHECK(hspp_tx_payload_bytes_per_s(16, 5, 32) == 0,
	      "TX: exactly the headers -> 0 payload");

	/* 2.4 GHz HT20 1SS MCS7: 65 Mbps PHY, 768 B MPDU. */
	p = hspp_air_params_ht20_2g(65000);
	m = hspp_air_model_frame(&p, HSPP_TX_MPDU_MAX);
	CHECK(m.frame_us == 135, "air: 768 B at 65 Mbps = 135 us frame");
	CHECK(m.cycle_us == 291, "air: cycle = frame+10+40+34+72 = 291 us");
	CHECK(m.pps == 3436, "air: 1e6/291 = 3436 fps");
	CHECK(m.payload_bps == 3436ull * 736ull * 8ull,
	      "air: payload bps = 736 B x 8 x 3436");
	/* The shipped host window must not exceed the non-aggregated air
	 * ceiling at MCS7: 3200 <= 3436, i.e. the default is air-bound. */
	CHECK(hspp_tx_pps(16, 5) <= m.pps,
	      "air: shipped host window <= 65 Mbps air ceiling");
	/* Raising the pool to the per-TID ring depth is only useful when the
	 * PHY is faster; at 65 Mbps it stays air-bound. */
	m = hspp_air_model_frame(&p, 1500);	/* sanity: bigger MPDU slower */
	CHECK(m.cycle_us > 291 && m.pps < 3436,
	      "air: larger frames have a longer cycle");

	/* Zero PHY rate is refused, not divided. */
	p = hspp_air_params_ht20_2g(0);
	CHECK(p.phy_kbps == 65000, "air: zero PHY rate falls back to 65 Mbps");
}

static void test_scan_model(void)
{
	CHECK(hspp_scan_total_ms(13, HSPP_SCAN_PER_CH_MS) == 2028,
	      "scan: 13 x 156 ms = 2028 ms");
	CHECK(hspp_scan_total_ms(0, 156) == 0, "scan: no channels -> 0");
	CHECK(hspp_scan_requests(13, 6) == 3,
	      "scan: 13 channels / batch 6 = 3 requests");
	CHECK(hspp_scan_requests(13, 0) == 1,
	      "scan: batch 0 = the legacy single request");
	CHECK(hspp_scan_requests(13, 20) == 1,
	      "scan: batch >= n = the legacy single request");
	CHECK(hspp_scan_requests(6, 6) == 1, "scan: one full measurement batch");
	CHECK(hspp_scan_requests(10, 3) == 4, "scan: ceil() rounding");
	CHECK(hspp_scan_batched_ms(13, 6, HSPP_SCAN_PER_CH_MS,
				   HSPP_SCAN_BATCH_GAP_MS) == 2028 + 2 * 300,
	      "scan: batched 13ch = 2028 + 2 gaps");
	CHECK(hspp_scan_batched_ms(4, 6, HSPP_SCAN_PER_CH_MS,
				   HSPP_SCAN_BATCH_GAP_MS) == 4 * 156,
	      "scan: single batch adds no gap");
	CHECK(hspp_scan_batched_ms(13, 0, 156, 300) == 2028,
	      "scan: legacy single request adds no gap");
}

static void test_beacon(void)
{
	CHECK(hspp_beacon_ms(100) == 102, "beacon: 100 TU = 102 ms (integer)");
	CHECK(hspp_beacon_ms(0) == 102, "beacon: 0 TU uses the 100 TU default");
	CHECK(hspp_client_wakeups_per_min(100, 2) == 294,
	      "beacon: 100 TU / DTIM 2 = 294 client wakeups/min");
	CHECK(hspp_client_wakeups_per_min(100, 1) == 588,
	      "beacon: DTIM 1 doubles the wakeups");
	CHECK(hspp_client_wakeups_per_min(100, 10) == 58,
	      "beacon: DTIM 10 cuts wakeups by 10x");
	CHECK(hspp_client_wakeups_per_min(100, 0) == 294,
	      "beacon: DTIM 0 uses the vendor default 2");
	CHECK(hspp_client_wakeups_per_min(200, 2) == 147,
	      "beacon: 200 TU halves the wakeups");
}

static void test_data_tx_poll(void)
{
	CHECK(hspp_data_tx_poll_ms_clamp(0) == 5, "tx poll: 0 -> 5 default");
	CHECK(hspp_data_tx_poll_ms_clamp(1) == 1, "tx poll: 1 accepted");
	CHECK(hspp_data_tx_poll_ms_clamp(5) == 5, "tx poll: 5 unchanged");
	CHECK(hspp_data_tx_poll_ms_clamp(150) == 100, "tx poll: >100 clamps");
	/* Every accepted poll interval keeps the host ceiling above the
	 * non-aggregated MCS7 air ceiling only when the pool is large enough:
	 * this pins the tuning claim in REPORT.md sec 5. */
	CHECK(hspp_tx_pps(16, hspp_data_tx_poll_ms_clamp(1)) == 16000,
	      "tx poll: pool 16 at 1 ms = 16000 fps");
}

int main(void)
{
	test_pm_gate();
	test_pm_ack_clamp();
	test_poll_governor();
	test_aspm();
	test_tx_model();
	test_scan_model();
	test_beacon();
	test_data_tx_poll();

	printf("pm_perf_selftest: %u checks, %u failed\n", checks, fails);
	return fails ? 1 : 0;
}
