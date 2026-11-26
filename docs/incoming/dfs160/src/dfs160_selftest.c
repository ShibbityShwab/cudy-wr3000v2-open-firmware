// SPDX-License-Identifier: GPL-2.0
/*
 * dfs160_selftest.c - host selftest for the portable 160 MHz + DFS/CAC/NOL
 * model (lane dfs160 / task-240).  No kernel, no device, no vendor bytes:
 * every vector is derived from the committed decode tables of
 * ../wifi-5g-160-dfs/TABLES.md (protocol constants quoted as text).
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_dfs160.h"

static unsigned int checks, fails;

static void ok(int cond, const char *what)
{
	checks++;
	if (!cond) {
		fails++;
		printf("FAIL: %s\n", what);
	}
}

static void test_width_selector(void)
{
	u8 sel = 0xaa;
	int rc;

	rc = hi5622_dfs160_width_selector(HI5622_DFS160_WIDTH_20NOHT, &sel);
	ok(rc == 0 && sel == HI5622_DFS160_BW_20, "width 20_NOHT -> selector 0 (20M)");
	rc = hi5622_dfs160_width_selector(HI5622_DFS160_WIDTH_20, &sel);
	ok(rc == 0 && sel == HI5622_DFS160_BW_20, "width 20 -> selector 0 (20M)");
	rc = hi5622_dfs160_width_selector(HI5622_DFS160_WIDTH_40, &sel);
	ok(rc == 0 && sel == HI5622_DFS160_BW_40, "width 40 -> selector 1 (40M)");
	rc = hi5622_dfs160_width_selector(HI5622_DFS160_WIDTH_80, &sel);
	ok(rc == 0 && sel == HI5622_DFS160_BW_80, "width 80 -> selector 2 (80M)");
	rc = hi5622_dfs160_width_selector(HI5622_DFS160_WIDTH_160, &sel);
	ok(rc == 0 && sel == HI5622_DFS160_BW_160, "width 160 -> selector 3 (160M)");
	rc = hi5622_dfs160_width_selector(HI5622_DFS160_WIDTH_80P80, &sel);
	ok(rc == -EOPNOTSUPP, "width 80+80 refused (vendor cap has no 80+80 bit)");
	rc = hi5622_dfs160_width_selector(HI5622_DFS160_WIDTH_320, &sel);
	ok(rc == -EOPNOTSUPP, "width 320 refused (no 320 MHz)");
	rc = hi5622_dfs160_width_selector(99u, &sel);
	ok(rc == -EINVAL, "unknown width refused");
	rc = hi5622_dfs160_width_selector(HI5622_DFS160_WIDTH_160, NULL);
	ok(rc == -EINVAL, "NULL selector refused");
	ok(HI5622_DFS160_BW_LEGACY == 0xffu, "legacy sentinel outside the selector range");
}

static void test_ccfs1(void)
{
	u32 c = 0;
	int rc;

	rc = hi5622_dfs160_ccfs1_160(36u, &c);
	ok(rc == 0 && c == 50u, "160 MHz ch36 -> CCFS1 50");
	rc = hi5622_dfs160_ccfs1_160(64u, &c);
	ok(rc == 0 && c == 50u, "160 MHz ch64 -> CCFS1 50");
	rc = hi5622_dfs160_ccfs1_160(100u, &c);
	ok(rc == 0 && c == 114u, "160 MHz ch100 -> CCFS1 114");
	rc = hi5622_dfs160_ccfs1_160(128u, &c);
	ok(rc == 0 && c == 114u, "160 MHz ch128 -> CCFS1 114");
	rc = hi5622_dfs160_ccfs1_160(149u, &c);
	ok(rc == 0 && c == 163u, "160 MHz ch149 -> CCFS1 163");
	rc = hi5622_dfs160_ccfs1_160(140u, &c);
	ok(rc == -EOPNOTSUPP, "160 MHz ch140 not in a 160 MHz segment");
	rc = hi5622_dfs160_ccfs1_80(36u, &c);
	ok(rc == 0 && c == 42u, "80 MHz ch36 -> CCFS1 42");
	rc = hi5622_dfs160_ccfs1_80(52u, &c);
	ok(rc == 0 && c == 58u, "80 MHz ch52 -> CCFS1 58");
	rc = hi5622_dfs160_ccfs1_80(100u, &c);
	ok(rc == 0 && c == 106u, "80 MHz ch100 -> CCFS1 106");
	rc = hi5622_dfs160_ccfs1_80(132u, &c);
	ok(rc == 0 && c == 138u, "80 MHz ch132 -> CCFS1 138");
	rc = hi5622_dfs160_ccfs1_80(149u, &c);
	ok(rc == 0 && c == 155u, "80 MHz ch149 -> CCFS1 155");
}

static void test_width_check(void)
{
	int rc;

	rc = hi5622_dfs160_width_check(HI5622_DFS160_WIDTH_160, 36u, 50u, 0u);
	ok(rc == 0, "160: ch36/CCFS1 50 accepted");
	rc = hi5622_dfs160_width_check(HI5622_DFS160_WIDTH_160, 100u, 114u, 0u);
	ok(rc == 0, "160: ch100/CCFS1 114 accepted");
	rc = hi5622_dfs160_width_check(HI5622_DFS160_WIDTH_160, 36u, 42u, 0u);
	ok(rc == -EINVAL, "160: the 80 MHz centre 42 refused for 160");
	rc = hi5622_dfs160_width_check(HI5622_DFS160_WIDTH_160, 36u, 50u, 42u);
	ok(rc == -EINVAL, "160: CCFS2 != 0 refused (no 80+80)");
	rc = hi5622_dfs160_width_check(HI5622_DFS160_WIDTH_160, 140u, 138u, 0u);
	ok(rc == -EOPNOTSUPP, "160: ch140 refused (not a 160 segment)");
	rc = hi5622_dfs160_width_check(HI5622_DFS160_WIDTH_80, 52u, 58u, 0u);
	ok(rc == 0, "80: ch52/CCFS1 58 accepted");
	rc = hi5622_dfs160_width_check(HI5622_DFS160_WIDTH_80, 52u, 50u, 0u);
	ok(rc == -EINVAL, "80: wrong centre refused");
	rc = hi5622_dfs160_width_check(HI5622_DFS160_WIDTH_40, 40u, 0u, 0u);
	ok(rc == 0, "40: no centre required");
	rc = hi5622_dfs160_width_check(HI5622_DFS160_WIDTH_20, 1u, 0u, 0u);
	ok(rc == 0, "20: accepted");
	rc = hi5622_dfs160_width_check(HI5622_DFS160_WIDTH_80P80, 36u, 50u, 42u);
	ok(rc == -EOPNOTSUPP, "80+80 refused before any centre logic");
	rc = hi5622_dfs160_width_check(HI5622_DFS160_WIDTH_20, 0u, 0u, 0u);
	ok(rc == -EINVAL, "zero channel refused");
}

static void test_channels(void)
{
	u16 f = 0;

	ok(hi5622_dfs160_chan_freq(36u, &f) == 0 && f == 5180u, "ch36 = 5180 MHz");
	ok(hi5622_dfs160_chan_freq(52u, &f) == 0 && f == 5260u, "ch52 = 5260 MHz");
	ok(hi5622_dfs160_chan_freq(100u, &f) == 0 && f == 5500u, "ch100 = 5500 MHz");
	ok(hi5622_dfs160_chan_freq(144u, &f) == 0 && f == 5720u, "ch144 = 5720 MHz");
	ok(hi5622_dfs160_chan_freq(149u, &f) == 0 && f == 5745u, "ch149 = 5745 MHz");
	ok(hi5622_dfs160_chan_freq(184u, &f) == 0 && f == 4920u, "ch184 = 4920 MHz");
	ok(hi5622_dfs160_chan_freq(14u, &f) == 0 && f == 2484u, "ch14 = 2484 MHz");
	ok(hi5622_dfs160_chan_freq(1u, &f) == 0 && f == 2412u, "ch1 = 2412 MHz");
	ok(hi5622_dfs160_chan_freq(0u, &f) == -EINVAL, "ch0 refused");
	ok(hi5622_dfs160_chan_freq(35u, &f) == -EINVAL, "ch35 refused");

	ok(!hi5622_dfs160_is_dfs_chan(36u), "ch36 not DFS");
	ok(!hi5622_dfs160_is_dfs_chan(48u), "ch48 not DFS");
	ok(hi5622_dfs160_is_dfs_chan(52u), "ch52 DFS");
	ok(hi5622_dfs160_is_dfs_chan(64u), "ch64 DFS");
	ok(hi5622_dfs160_is_dfs_chan(100u), "ch100 DFS");
	ok(hi5622_dfs160_is_dfs_chan(140u), "ch140 DFS");
	ok(hi5622_dfs160_is_dfs_chan(144u), "ch144 DFS (the ch144 gap restored)");
	ok(!hi5622_dfs160_is_dfs_chan(149u), "ch149 not DFS");
	ok(!hi5622_dfs160_is_dfs_chan(165u), "ch165 not DFS");

	ok(hi5622_dfs160_is_tdwr_chan(100u), "ch100 TDWR window");
	ok(hi5622_dfs160_is_tdwr_chan(128u), "ch128 TDWR window");
	ok(!hi5622_dfs160_is_tdwr_chan(132u), "ch132 outside TDWR window");
	ok(!hi5622_dfs160_is_tdwr_chan(52u), "ch52 outside TDWR window");
	ok(hi5622_dfs160_is_tdwr_center(5600u), "5600 MHz TDWR");
	ok(hi5622_dfs160_is_tdwr_center(5650u), "5650 MHz TDWR");
	ok(!hi5622_dfs160_is_tdwr_center(5599u), "5599 MHz not TDWR");
	ok(!hi5622_dfs160_is_tdwr_center(5651u), "5651 MHz not TDWR");
}

static void test_cac_plan(void)
{
	struct hi5622_dfs160_cac_plan p;
	int rc;

	rc = hi5622_dfs160_cac_plan(52u, 5260u, 0u, &p);
	ok(rc == 0, "plan: ch52 ok");
	ok(p.cac_ms == 60000u, "plan: ch52 CAC 60000 ms (the default)");
	ok(p.ticks == 1u, "plan: ch52 one tick");
	ok(p.first_ms == 63000u, "plan: ch52 first timer 63000 ms");
	ok(p.tick_ms == 60000u, "plan: ch52 re-arm tick 60000 ms");
	ok(p.nol_ms == 1800000u, "plan: NOL 1800000 ms (30 min)");
	ok(p.tdwr == 0u, "plan: ch52 not TDWR");

	rc = hi5622_dfs160_cac_plan(100u, 5500u, 0u, &p);
	ok(rc == 0 && p.tdwr == 1u, "plan: ch100 TDWR");
	ok(p.cac_ms == 600000u, "plan: ch100 CAC 600000 ms (TDWR)");
	ok(p.ticks == 10u, "plan: ch100 ten ticks (600000/60000)");
	ok(63000u + 9u * 60000u >= 600000u, "plan: the vendor tick schedule covers the CAC");

	rc = hi5622_dfs160_cac_plan(124u, 5620u, 0u, &p);
	ok(rc == 0 && p.cac_ms == 600000u, "plan: ch124 TDWR 600000 ms");
	rc = hi5622_dfs160_cac_plan(144u, 5720u, 0u, &p);
	ok(rc == 0 && p.cac_ms == 60000u, "plan: ch144 (5720 MHz) 60000 ms");
	rc = hi5622_dfs160_cac_plan(52u, 5260u, 120000u, &p);
	ok(rc == 0 && p.cac_ms == 120000u && p.ticks == 2u,
	   "plan: a longer cfg80211 request is honoured");
	rc = hi5622_dfs160_cac_plan(100u, 5500u, 60000u, &p);
	ok(rc == 0 && p.cac_ms == 600000u, "plan: the driver TDWR floor wins over a short request");
	rc = hi5622_dfs160_cac_plan(36u, 5180u, 0u, &p);
	ok(rc == -EOPNOTSUPP, "plan: non-DFS channel refused");
	rc = hi5622_dfs160_cac_plan(0u, 0u, 0u, &p);
	ok(rc == -EINVAL, "plan: zero channel refused");
	rc = hi5622_dfs160_cac_plan(52u, 0u, 0u, &p);
	ok(rc == 0 && p.cac_ms == 60000u, "plan: centre frequency may be derived");
}

static void test_cac_machine(void)
{
	struct hi5622_dfs160_cac st;
	u32 elapsed;
	unsigned int i;
	int rc;

	hi5622_dfs160_cac_init(&st);
	ok(st.state == HI5622_DFS160_CAC_OFF, "machine: init OFF");
	ok(hi5622_dfs160_cac_delay_ms(&st) == 0u, "machine: OFF has no delay");
	ok(hi5622_dfs160_cac_tick(&st) == HI5622_DFS160_STEP_IDLE,
	   "machine: tick while OFF is IDLE");
	ok(hi5622_dfs160_cac_begin(&st, 36u, 5180u, 0u) == -EOPNOTSUPP,
	   "machine: CAC on a non-DFS channel refused");

	rc = hi5622_dfs160_cac_begin(&st, 52u, 5260u, 0u);
	ok(rc == 0, "machine: begin ch52");
	ok(st.state == HI5622_DFS160_CAC_ARMED, "machine: state ARMED");
	ok(hi5622_dfs160_cac_begin(&st, 52u, 5260u, 0u) == -EALREADY,
	   "machine: second begin refused (-EALREADY)");
	ok(hi5622_dfs160_cac_delay_ms(&st) == 63000u, "machine: first delay 63000 ms");
	ok(hi5622_dfs160_cac_tick(&st) == HI5622_DFS160_STEP_FINISH,
	   "machine: 60 s CAC finishes on the first tick");
	ok(hi5622_dfs160_cac_delay_ms(&st) == 0u, "machine: no delay after the finish tick");
	hi5622_dfs160_cac_finish(&st);
	ok(st.state == HI5622_DFS160_CAC_FINISHED, "machine: state FINISHED");
	ok(strcmp(hi5622_dfs160_cac_state_name(st.state), "finished") == 0,
	   "machine: state name finished");

	hi5622_dfs160_cac_init(&st);
	rc = hi5622_dfs160_cac_begin(&st, 100u, 5500u, 0u);
	ok(rc == 0 && st.ticks == 10u, "machine: begin ch100 (10 ticks)");
	elapsed = hi5622_dfs160_cac_delay_ms(&st);
	ok(elapsed == 63000u, "machine: TDWR first delay 63000 ms");
	for (i = 1; i <= 10u; i++) {
		enum hi5622_dfs160_cac_step s = hi5622_dfs160_cac_tick(&st);

		if (i < 10u) {
			ok(s == HI5622_DFS160_STEP_REARM, "machine: TDWR tick re-arms");
			ok(hi5622_dfs160_cac_delay_ms(&st) == 60000u,
			   "machine: TDWR re-arm delay 60000 ms");
			elapsed += 60000u;
		} else {
			ok(s == HI5622_DFS160_STEP_FINISH,
			   "machine: TDWR last tick finishes");
		}
	}
	ok(elapsed >= 600000u, "machine: TDWR schedule >= 600000 ms");
	ok(elapsed == 603000u, "machine: TDWR schedule 603000 ms (63 s + 9 x 60 s)");

	hi5622_dfs160_cac_finish(&st);
	ok(hi5622_dfs160_cac_ap_gate(&st, 100u, 5500u, 0u) == 0,
	   "gate: finished CAC on the same channel allows the AP");
	ok(hi5622_dfs160_cac_ap_gate(&st, 104u, 5520u, 0u) == -EAGAIN,
	   "gate: another DFS channel still blocked");
	ok(hi5622_dfs160_cac_ap_gate(&st, 36u, 5180u, 0u) == 0,
	   "gate: non-DFS channel not gated");

	hi5622_dfs160_cac_abort(&st);
	ok(st.state == HI5622_DFS160_CAC_ABORTED, "machine: abort");
	ok(strcmp(hi5622_dfs160_cac_state_name(st.state), "aborted") == 0,
	   "machine: state name aborted");
}

static void test_radar_nol(void)
{
	struct hi5622_dfs160_cac st;
	int rc;

	hi5622_dfs160_cac_init(&st);
	ok(hi5622_dfs160_cac_begin(&st, 100u, 5500u, 0u) == 0, "radar: CAC begins");
	rc = hi5622_dfs160_cac_radar(&st, 9u, 1000u);
	ok(rc == -EINVAL, "radar: type 9 refused (types 0..8)");
	rc = hi5622_dfs160_cac_radar(&st, 5u, 1000u);
	ok(rc == 0, "radar: type 5 accepted");
	ok(st.state == HI5622_DFS160_CAC_ABORTED, "radar: CAC aborted by the hit");
	ok(st.radar_count == 1u && st.radar_type == 5u, "radar: counter/type recorded");
	ok(hi5622_dfs160_nol_active(&st, 1000u), "NOL: active at detection time");
	ok(hi5622_dfs160_nol_active(&st, 1000u + 1799999u), "NOL: active just before 30 min");
	ok(!hi5622_dfs160_nol_active(&st, 1000u + 1800000u), "NOL: expired at 30 min");
	ok(hi5622_dfs160_cac_ap_gate(&st, 100u, 5500u, 2000u) == -EAGAIN,
	   "gate: a radar-aborted CAC blocks the AP");
}

static void test_status_line(void)
{
	struct hi5622_dfs160_cac st;
	char buf[512];
	int n;

	hi5622_dfs160_cac_init(&st);
	n = hi5622_dfs160_status_line(1u, 1u, &st, 0, 1u, 0u, 0u, 0u,
				      buf, sizeof(buf));
	ok(n > 0 && (size_t)n < sizeof(buf), "status: line written");
	ok(strstr(buf, "dfs_cac:") != NULL, "status: dfs_cac prefix");
	ok(strstr(buf, "ht_bw=1") != NULL, "status: ht_bw shown");
	ok(strstr(buf, "dfs_cac=1") != NULL, "status: dfs_cac shown");
	ok(strstr(buf, "state=off") != NULL, "status: state shown");
	n = hi5622_dfs160_status_line(0u, 0u, NULL, -ENODATA, 0u, 0u, 0u, 0u,
				      buf, 8u);
	ok(n == 7, "status: clamped to len-1 (8-byte buffer)");
	ok(hi5622_dfs160_status_line(0u, 0u, NULL, 0, 0u, 0u, 0u, 0u,
				     NULL, 0u) == -EINVAL, "status: NULL refused");
}

int main(void)
{
	test_width_selector();
	test_ccfs1();
	test_width_check();
	test_channels();
	test_cac_plan();
	test_cac_machine();
	test_radar_nol();
	test_status_line();
	printf("dfs160 selftest: %u checks, %u failed\n", checks, fails);
	return fails ? 1 : 0;
}
