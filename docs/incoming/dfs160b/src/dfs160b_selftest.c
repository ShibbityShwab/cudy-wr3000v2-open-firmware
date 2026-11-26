/* SPDX-License-Identifier: GPL-2.0 */
/*
 * dfs160b_selftest.c - host selftest of the portable 160 MHz width selector +
 * DFS/CAC/NOL model (lane dfs160b / task-245).  Userspace only: no kernel
 * headers, no device, no vendor artifact.  Build + run:
 *   ./tools/run-host-tests.sh
 *
 * Every check is a protocol decision taken from the committed decode lanes
 * (../wifi-5g-160-dfs, ../wifi-5g-dfs-set, ../wifi-5g-dfs-order).  The
 * nl80211 width pins are the Linux 6.18.60 UAPI values - a drift here is the
 * exact regression class that broke the sibling lane, so it is checked
 * first.
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_dfsb.h"

static unsigned int g_checks;
static unsigned int g_fails;

static void ck(const char *what, int cond)
{
	g_checks++;
	if (!cond) {
		g_fails++;
		printf("FAIL: %s\n", what);
	}
}

static void ck_rc(const char *what, int got, int want)
{
	g_checks++;
	if (got != want) {
		g_fails++;
		printf("FAIL: %s (got %d, want %d)\n", what, got, want);
	}
}

/* ---- 0. the nl80211 UAPI pins (6.18.60) ------------------------------ */
static void test_width_enum_pins(void)
{
	ck("pin 20_NOHT=0", HI5622_DFSB_WIDTH_20NOHT == 0u);
	ck("pin 20=1", HI5622_DFSB_WIDTH_20 == 1u);
	ck("pin 40=2", HI5622_DFSB_WIDTH_40 == 2u);
	ck("pin 80=3", HI5622_DFSB_WIDTH_80 == 3u);
	ck("pin 80P80=4 (6.18; NOT 5)", HI5622_DFSB_WIDTH_80P80 == 4u);
	ck("pin 160=5 (6.18; NOT 4)", HI5622_DFSB_WIDTH_160 == 5u);
	ck("pin 5MHz=6", HI5622_DFSB_WIDTH_5 == 6u);
	ck("pin 10MHz=7", HI5622_DFSB_WIDTH_10 == 7u);
	ck("pin 320=13", HI5622_DFSB_WIDTH_320 == 13u);
}

/* ---- 1. the 0x0124 body[1] width selector --------------------------- */
static void test_width_plan(void)
{
	struct hi5622_dfsb_width_plan p;
	int rc;

	/* legacy: ht_bw=0 keeps the old body[1] byte 0 for every width */
	memset(&p, 0xaa, sizeof(p));
	rc = hi5622_dfsb_width_plan(0u, 1u, 36u, HI5622_DFSB_WIDTH_160,
				    5250u, 0u, &p);
	ck_rc("legacy 160 plan rc", rc, 0);
	ck("legacy 160 not enabled", p.enabled == false);
	ck("legacy 160 sel 0", p.sel == 0u);
	ck_rc("legacy unknown width ignored", hi5622_dfsb_width_plan(
		0u, 0u, 1u, 999u, 0u, 0u, &p), 0);

	ck_rc("ht_bw=2 refused", hi5622_dfsb_width_plan(2u, 1u, 36u,
		HI5622_DFSB_WIDTH_20, 0u, 0u, &p), -EINVAL);
	ck_rc("null out", hi5622_dfsb_width_plan(1u, 1u, 36u,
		HI5622_DFSB_WIDTH_20, 0u, 0u, NULL), -EINVAL);

	ck_rc("20NOHT -> 0", hi5622_dfsb_width_plan(1u, 1u, 36u,
		HI5622_DFSB_WIDTH_20NOHT, 0u, 0u, &p), 0);
	ck("20NOHT sel 0", p.enabled && p.sel == HI5622_DFSB_BW_20);
	ck_rc("20 -> 0", hi5622_dfsb_width_plan(1u, 1u, 36u,
		HI5622_DFSB_WIDTH_20, 0u, 0u, &p), 0);
	ck("20 sel 0", p.sel == HI5622_DFSB_BW_20);
	ck_rc("40 -> 1", hi5622_dfsb_width_plan(1u, 1u, 36u,
		HI5622_DFSB_WIDTH_40, 0u, 0u, &p), 0);
	ck("40 sel 1", p.sel == HI5622_DFSB_BW_40);
	ck_rc("80 ch36 ccfs1 5210 -> 2", hi5622_dfsb_width_plan(1u, 1u, 36u,
		HI5622_DFSB_WIDTH_80, 5210u, 0u, &p), 0);
	ck("80 sel 2", p.sel == HI5622_DFSB_BW_80);
	ck_rc("80 wrong centre", hi5622_dfsb_width_plan(1u, 1u, 36u,
		HI5622_DFSB_WIDTH_80, 5290u, 0u, &p), -EINVAL);
	ck_rc("80 with ccfs2", hi5622_dfsb_width_plan(1u, 1u, 36u,
		HI5622_DFSB_WIDTH_80, 5210u, 5290u, &p), -EINVAL);
	ck_rc("80 on 2.4 GHz", hi5622_dfsb_width_plan(1u, 0u, 1u,
		HI5622_DFSB_WIDTH_80, 0u, 0u, &p), -EOPNOTSUPP);

	/* the 160 MHz path: a real 6.18 width value 5 must be accepted */
	ck_rc("160 ch36 ccfs1 5250 -> 3", hi5622_dfsb_width_plan(1u, 1u, 36u,
		HI5622_DFSB_WIDTH_160, 5250u, 0u, &p), 0);
	ck("160 sel 3", p.enabled && p.sel == HI5622_DFSB_BW_160);
	ck("160 width stored", p.width == 5u);
	ck_rc("160 ch100 ccfs1 5570", hi5622_dfsb_width_plan(1u, 1u, 100u,
		HI5622_DFSB_WIDTH_160, 5570u, 0u, &p), 0);
	ck_rc("160 ch52 ccfs1 5250", hi5622_dfsb_width_plan(1u, 1u, 52u,
		HI5622_DFSB_WIDTH_160, 5250u, 0u, &p), 0);
	ck_rc("160 wrong centre", hi5622_dfsb_width_plan(1u, 1u, 36u,
		HI5622_DFSB_WIDTH_160, 5210u, 0u, &p), -EINVAL);
	ck_rc("160 ccfs2 nonzero", hi5622_dfsb_width_plan(1u, 1u, 36u,
		HI5622_DFSB_WIDTH_160, 5250u, 5290u, &p), -EINVAL);
	ck_rc("160 on 2.4 GHz", hi5622_dfsb_width_plan(1u, 0u, 1u,
		HI5622_DFSB_WIDTH_160, 0u, 0u, &p), -EOPNOTSUPP);
	ck_rc("160 ch140 not a segment", hi5622_dfsb_width_plan(1u, 1u, 140u,
		HI5622_DFSB_WIDTH_160, 5690u, 0u, &p), -EOPNOTSUPP);

	/* unselectable widths: 4 = 80+80 and 13 = 320 on the 6.18 UAPI */
	ck_rc("80P80 (4) refused", hi5622_dfsb_width_plan(1u, 1u, 36u,
		HI5622_DFSB_WIDTH_80P80, 5250u, 5210u, &p), -EOPNOTSUPP);
	ck_rc("320 (13) refused", hi5622_dfsb_width_plan(1u, 1u, 36u,
		HI5622_DFSB_WIDTH_320, 0u, 0u, &p), -EOPNOTSUPP);
	ck_rc("5 MHz refused", hi5622_dfsb_width_plan(1u, 1u, 36u,
		HI5622_DFSB_WIDTH_5, 0u, 0u, &p), -EOPNOTSUPP);
	ck_rc("10 MHz refused", hi5622_dfsb_width_plan(1u, 1u, 36u,
		HI5622_DFSB_WIDTH_10, 0u, 0u, &p), -EOPNOTSUPP);
	ck_rc("unknown width 99", hi5622_dfsb_width_plan(1u, 1u, 36u,
		99u, 0u, 0u, &p), -EINVAL);
	ck_rc("channel 0 refused", hi5622_dfsb_width_plan(1u, 1u, 0u,
		HI5622_DFSB_WIDTH_20, 0u, 0u, &p), -EINVAL);
	ck_rc("band 1 ch49 refused", hi5622_dfsb_width_plan(1u, 1u, 49u,
		HI5622_DFSB_WIDTH_20, 0u, 0u, &p), -EINVAL);
	ck_rc("band 2 refused", hi5622_dfsb_width_plan(1u, 2u, 36u,
		HI5622_DFSB_WIDTH_20, 0u, 0u, &p), -EINVAL);
}

/* ---- 2. the channel tables, the ch144 gap, the TDWR window ---------- */
static void test_channels(void)
{
	static const u8 list[29] = {
		36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116,
		120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165,
		184, 188, 192, 196
	};
	static const u16 freqs[29] = {
		5180, 5200, 5220, 5240, 5260, 5280, 5300, 5320, 5500, 5520,
		5540, 5560, 5580, 5600, 5620, 5640, 5660, 5680, 5700, 5720,
		5745, 5765, 5785, 5805, 5825, 4920, 4940, 4960, 4980
	};
	unsigned int i;
	u16 mhz = 0;
	int rc;

	for (i = 0; i < 29u; i++) {
		rc = hi5622_dfsb_chan_freq(list[i], &mhz);
		ck("band1 table lookup rc", rc == 0);
		ck("band1 table frequency", mhz == freqs[i]);
	}
	ck_rc("ch144 exists (the gap)", hi5622_dfsb_chan_freq(144u, &mhz), 0);
	ck("ch144 = 5720 MHz", mhz == 5720u);
	ck_rc("ch37 not in table", hi5622_dfsb_chan_freq(37u, &mhz), -EINVAL);
	ck_rc("ch0 not in table", hi5622_dfsb_chan_freq(0u, &mhz), -EINVAL);
	ck_rc("null freq out", hi5622_dfsb_chan_freq(36u, NULL), -EINVAL);

	ck("36 not DFS", hi5622_dfsb_is_dfs_chan(36u) == 0);
	ck("48 not DFS", hi5622_dfsb_is_dfs_chan(48u) == 0);
	ck("52 DFS", hi5622_dfsb_is_dfs_chan(52u) == 1);
	ck("64 DFS", hi5622_dfsb_is_dfs_chan(64u) == 1);
	ck("100 DFS", hi5622_dfsb_is_dfs_chan(100u) == 1);
	ck("144 DFS", hi5622_dfsb_is_dfs_chan(144u) == 1);
	ck("149 not DFS", hi5622_dfsb_is_dfs_chan(149u) == 0);
	ck("196 not DFS", hi5622_dfsb_is_dfs_chan(196u) == 0);
	ck("0 not DFS", hi5622_dfsb_is_dfs_chan(0u) == 0);

	ck("120 in TDWR", hi5622_dfsb_is_tdwr_chan(120u) == 1);
	ck("124 in TDWR", hi5622_dfsb_is_tdwr_chan(124u) == 1);
	ck("128 in TDWR", hi5622_dfsb_is_tdwr_chan(128u) == 1);
	ck("100 not TDWR", hi5622_dfsb_is_tdwr_chan(100u) == 0);
	ck("116 not TDWR", hi5622_dfsb_is_tdwr_chan(116u) == 0);
	ck("132 not TDWR (5660)", hi5622_dfsb_is_tdwr_chan(132u) == 0);
	ck("144 not TDWR", hi5622_dfsb_is_tdwr_chan(144u) == 0);
}

static void test_ccfs1(void)
{
	u32 c = 0;

	ck_rc("160 ccfs1 ch36", hi5622_dfsb_ccfs1_160(36u, &c), 0);
	ck("160 centre 36 = 5250", c == 5250u);
	ck_rc("160 ccfs1 ch64", hi5622_dfsb_ccfs1_160(64u, &c), 0);
	ck("160 centre 64 = 5250", c == 5250u);
	ck_rc("160 ccfs1 ch100", hi5622_dfsb_ccfs1_160(100u, &c), 0);
	ck("160 centre 100 = 5570", c == 5570u);
	ck_rc("160 ccfs1 ch128", hi5622_dfsb_ccfs1_160(128u, &c), 0);
	ck("160 centre 128 = 5570", c == 5570u);
	ck_rc("160 ccfs1 ch149", hi5622_dfsb_ccfs1_160(149u, &c), 0);
	ck("160 centre 149 = 5815", c == 5815u);
	ck_rc("160 ccfs1 ch140 none", hi5622_dfsb_ccfs1_160(140u, &c),
	      -EOPNOTSUPP);
	ck_rc("160 ccfs1 ch144 none", hi5622_dfsb_ccfs1_160(144u, &c),
	      -EOPNOTSUPP);
	ck_rc("160 ccfs1 null", hi5622_dfsb_ccfs1_160(36u, NULL), -EINVAL);

	ck_rc("80 ccfs1 ch36", hi5622_dfsb_ccfs1_80(36u, &c), 0);
	ck("80 centre 36 = 5210", c == 5210u);
	ck_rc("80 ccfs1 ch52", hi5622_dfsb_ccfs1_80(52u, &c), 0);
	ck("80 centre 52 = 5290", c == 5290u);
	ck_rc("80 ccfs1 ch100", hi5622_dfsb_ccfs1_80(100u, &c), 0);
	ck("80 centre 100 = 5530", c == 5530u);
	ck_rc("80 ccfs1 ch116", hi5622_dfsb_ccfs1_80(116u, &c), 0);
	ck("80 centre 116 = 5610", c == 5610u);
	ck_rc("80 ccfs1 ch132", hi5622_dfsb_ccfs1_80(132u, &c), 0);
	ck("80 centre 132 = 5690", c == 5690u);
	ck_rc("80 ccfs1 ch144", hi5622_dfsb_ccfs1_80(144u, &c), 0);
	ck("80 centre 144 = 5690", c == 5690u);
	ck_rc("80 ccfs1 ch149", hi5622_dfsb_ccfs1_80(149u, &c), 0);
	ck("80 centre 149 = 5775", c == 5775u);
	ck_rc("80 ccfs1 ch165", hi5622_dfsb_ccfs1_80(165u, &c), 0);
	ck("80 centre 165 = 5855", c == 5855u);
	ck_rc("80 ccfs1 ch145 none", hi5622_dfsb_ccfs1_80(145u, &c),
	      -EOPNOTSUPP);
}

/* ---- 3. the CAC / NOL state machine --------------------------------- */
static void test_cac(void)
{
	struct hi5622_dfsb_cac st;
	enum hi5622_dfsb_cac_step step = HI5622_DFSB_STEP_REARM;
	unsigned int i;
	int rc;

	memset(&st, 0, sizeof(st));
	ck_rc("CAC on non-DFS ch36", hi5622_dfsb_cac_begin(&st, 36u, 5180u,
		60000u), -EOPNOTSUPP);
	ck_rc("CAC ch not in table", hi5622_dfsb_cac_begin(&st, 49u, 0u,
		60000u), -EINVAL);
	ck_rc("CAC null state", hi5622_dfsb_cac_begin(NULL, 52u, 5260u,
		60000u), -EINVAL);
	ck_rc("CAC centre mismatch", hi5622_dfsb_cac_begin(&st, 52u, 5280u,
		60000u), -EINVAL);

	/* the 60 s class: one 63000 ms first timer, then FINISHED */
	ck_rc("CAC ch52 begin", hi5622_dfsb_cac_begin(&st, 52u, 5260u,
		60000u), 0);
	ck("ch52 ARMED", st.state == HI5622_DFSB_CAC_ARMED);
	ck("ch52 not TDWR", st.tdwr == 0u);
	ck("ch52 cac_ms 60000", st.cac_ms == 60000u);
	ck("ch52 ticks 1", st.ticks == 1u);
	ck("ch52 first 63000", st.first_ms == 63000u);
	ck("ch52 tick 60000", st.tick_ms == 60000u);
	ck("ch52 NOL 1800000", st.nol_ms == 1800000u);
	ck("ch52 delay first 63000",
	   hi5622_dfsb_cac_delay_ms(&st) == 63000u);
	ck_rc("second begin while armed", hi5622_dfsb_cac_begin(&st, 56u,
		5280u, 60000u), -EALREADY);
	ck_rc("ch52 tick", hi5622_dfsb_cac_tick(&st, 63000u, &step), 0);
	ck("ch52 tick FINISH", step == HI5622_DFSB_STEP_FINISH);
	ck("ch52 FINISHED", st.state == HI5622_DFSB_CAC_FINISHED);
	ck("ch52 finish_ms", st.finish_ms == 63000u);
	ck("ch52 idle delay 0", hi5622_dfsb_cac_delay_ms(&st) == 0u);
	ck_rc("ch52 gate same channel", hi5622_dfsb_cac_ap_gate(&st, 52u,
		63000u), 0);
	ck_rc("ch52 gate inside NOL", hi5622_dfsb_cac_ap_gate(&st, 52u,
		63000u + 1799999u), 0);
	ck_rc("ch52 gate NOL boundary", hi5622_dfsb_cac_ap_gate(&st, 52u,
		63000u + 1800000u), -EAGAIN);
	ck_rc("ch52 gate other channel", hi5622_dfsb_cac_ap_gate(&st, 56u,
		63000u), -EINVAL);
	ck_rc("gate channel 0", hi5622_dfsb_cac_ap_gate(&st, 0u, 0u), -EINVAL);
	ck_rc("tick after finish", hi5622_dfsb_cac_tick(&st, 70000u, &step),
	      -EPERM);
	ck_rc("tick null out", hi5622_dfsb_cac_tick(&st, 70000u, NULL),
	      -EINVAL);
	hi5622_dfsb_cac_abort(&st);
	ck("abort -> IDLE", st.state == HI5622_DFSB_CAC_IDLE);
	ck("abort clears channel", st.channel == 0u);
	ck_rc("gate while IDLE", hi5622_dfsb_cac_ap_gate(&st, 52u, 63000u),
	      -EAGAIN);

	/* the TDWR class: 600000 ms -> 10 ticks of the vendor arithmetic:
	 * 63000 + 9 x 60000 = 603000 ms. */
	ck_rc("CAC ch120 begin 600000", hi5622_dfsb_cac_begin(&st, 120u,
		5600u, 600000u), 0);
	ck("ch120 TDWR", st.tdwr == 1u);
	ck("ch120 cac_ms 600000", st.cac_ms == 600000u);
	ck("ch120 ticks 10", st.ticks == 10u);
	for (i = 1; i <= 10u; i++) {
		u32 now = 63000u + (i - 1u) * 60000u;

		rc = hi5622_dfsb_cac_tick(&st, now, &step);
		ck("ch120 tick rc", rc == 0);
		if (i < 10u) {
			ck("ch120 rearm", step == HI5622_DFSB_STEP_REARM);
			ck("ch120 tick delay 60000",
			   hi5622_dfsb_cac_delay_ms(&st) == 60000u);
		} else {
			ck("ch120 finish", step == HI5622_DFSB_STEP_FINISH);
			ck("ch120 finish time 603000",
			   st.finish_ms == 603000u);
		}
	}
	ck("ch120 FINISHED after 10 ticks",
	   st.state == HI5622_DFSB_CAC_FINISHED);
	ck_rc("ch120 gate", hi5622_dfsb_cac_ap_gate(&st, 120u, 604000u), 0);

	/* a larger cfg80211 time is never shortened below the report time */
	ck_rc("CAC ch100 begin passed 900000", hi5622_dfsb_cac_begin(&st,
		100u, 5500u, 900000u), 0);
	ck("ch100 cac_ms 900000", st.cac_ms == 900000u);
	ck("ch100 ticks 15", st.ticks == 15u);

	/* the wrap-safe NOL gate: finish near the u32 ms wrap */
	memset(&st, 0, sizeof(st));
	ck_rc("CAC ch60 begin", hi5622_dfsb_cac_begin(&st, 60u, 5300u,
		60000u), 0);
	ck_rc("CAC ch60 finish", hi5622_dfsb_cac_tick(&st, 0xffffff00u,
		&step), 0);
	ck("ch60 wrap gate", hi5622_dfsb_cac_ap_gate(&st, 60u, 0x00000100u) ==
	   0);
	ck("ch60 wrap NOL", hi5622_dfsb_cac_ap_gate(&st, 60u,
	   0xffffff00u + 1800000u) == -EAGAIN);
}

/* ---- 4. the 0x07E7 frame KAT ---------------------------------------- */
static void test_set_frame(void)
{
	static const u8 arm_kat[HI5622_DFSB_SET_WIRE_LEN] = {
		0x00, 0x01, 0x00, 0x00, 0x1d, 0x00, 0x01, 0x00,
		0x00, 0x00, 0x5a, 0x5a, 0x00, 0x00, 0x00, 0x00,
		0x01, 0x00, 0x00, 0x00, 0xe7, 0x07, 0x01, 0x00,
		0x01, 0xff, 0xff, 0xff, 0xff,
	};
	u8 wire[HI5622_DFSB_SET_WIRE_LEN + 8];
	size_t len = 0;
	int rc;

	memset(wire, 0xcc, sizeof(wire));
	rc = hi5622_dfsb_set_build(wire, sizeof(wire), &len,
				   HI5622_DFSB_SET_BODY_ARM);
	ck_rc("set ARM build rc", rc, 0);
	ck("set ARM len 29", len == HI5622_DFSB_SET_WIRE_LEN);
	ck("set ARM byte-exact KAT", memcmp(wire, arm_kat, len) == 0);
	ck("set ARM body byte", wire[HI5622_DFSB_SET_BODY_OFF] == 0x01u);
	ck("set ARM key", wire[20] == 0xe7u && wire[21] == 0x07u);
	ck("set ARM tag", wire[10] == 0x5au && wire[11] == 0x5au);

	rc = hi5622_dfsb_set_build(wire, sizeof(wire), &len,
				   HI5622_DFSB_SET_BODY_CLEAR);
	ck_rc("set CLEAR build rc", rc, 0);
	ck("set CLEAR body byte", wire[HI5622_DFSB_SET_BODY_OFF] == 0x00u);
	ck("set CLEAR differs only in the body",
	   memcmp(wire, arm_kat, HI5622_DFSB_SET_BODY_OFF) == 0 &&
	   memcmp(wire + HI5622_DFSB_SET_BODY_OFF + 1,
		  arm_kat + HI5622_DFSB_SET_BODY_OFF + 1,
		  HI5622_DFSB_SET_WIRE_LEN -
			  HI5622_DFSB_SET_BODY_OFF - 1) == 0);

	ck_rc("set bad body", hi5622_dfsb_set_build(wire, sizeof(wire),
		&len, 0x02u), -EINVAL);
	ck_rc("set NULL wire", hi5622_dfsb_set_build(NULL, 0u, &len,
		HI5622_DFSB_SET_BODY_ARM), -EINVAL);
	ck_rc("set NULL len", hi5622_dfsb_set_build(wire, sizeof(wire), NULL,
		HI5622_DFSB_SET_BODY_ARM), -EINVAL);
	ck_rc("set short cap", hi5622_dfsb_set_build(wire, 28u, &len,
		HI5622_DFSB_SET_BODY_ARM), -ENOSPC);
}

/* ---- 5. the ladder order contract ----------------------------------- */
static void test_order(void)
{
	ck_rc("vap_start < dfs_set", hi5622_dfsb_order_before(
		HI5622_DFSB_OP_VAP_START, HI5622_DFSB_OP_DFS_SET), 1);
	ck_rc("dfs_set < dfs_mode", hi5622_dfsb_order_before(
		HI5622_DFSB_OP_DFS_SET, HI5622_DFSB_OP_DFS_MODE), 1);
	ck_rc("dfs_mode < band_start", hi5622_dfsb_order_before(
		HI5622_DFSB_OP_DFS_MODE, HI5622_DFSB_OP_BAND_START), 1);
	ck_rc("desc_table < ap_chan", hi5622_dfsb_order_before(
		HI5622_DFSB_OP_DESC_TABLE, HI5622_DFSB_OP_AP_CHAN), 1);
	ck_rc("ap_chan < vap_start", hi5622_dfsb_order_before(
		HI5622_DFSB_OP_AP_CHAN, HI5622_DFSB_OP_VAP_START), 1);
	ck_rc("ap_cfg < vap_start", hi5622_dfsb_order_before(
		HI5622_DFSB_OP_AP_CFG, HI5622_DFSB_OP_VAP_START), 1);
	ck_rc("band_start < dfs_set is false", hi5622_dfsb_order_before(
		HI5622_DFSB_OP_BAND_START, HI5622_DFSB_OP_DFS_SET), 0);
	ck_rc("same op is not before", hi5622_dfsb_order_before(
		HI5622_DFSB_OP_DFS_SET, HI5622_DFSB_OP_DFS_SET), 0);
	ck_rc("op max refused", hi5622_dfsb_order_before(
		HI5622_DFSB_OP_MAX, HI5622_DFSB_OP_DFS_SET), -EINVAL);
}

/* ---- 6. the start_ap feature plan ----------------------------------- */
static void test_ap_plan(void)
{
	struct hi5622_dfsb_ap_plan plan;
	struct hi5622_dfsb_cac st;
	enum hi5622_dfsb_cac_step step;

	memset(&plan, 0xaa, sizeof(plan));
	ck_rc("plan legacy", hi5622_dfsb_ap_plan(0u, 0u, 0u, 1u, 36u,
		HI5622_DFSB_WIDTH_160, 5250u, 0u, NULL, 0u, &plan), 0);
	ck("plan legacy no width", plan.width_enabled == false &&
	   plan.width_sel == 0u && plan.cac_required == false);
	ck_rc("plan ht_bw=2 refused", hi5622_dfsb_ap_plan(2u, 0u, 0u, 1u,
		36u, HI5622_DFSB_WIDTH_20, 0u, 0u, NULL, 0u, &plan),
	      -EINVAL);
	ck_rc("plan dfs_cac=2 refused", hi5622_dfsb_ap_plan(0u, 2u, 0u, 1u,
		36u, HI5622_DFSB_WIDTH_20, 0u, 0u, NULL, 0u, &plan),
	      -EINVAL);
	ck_rc("plan null out", hi5622_dfsb_ap_plan(0u, 0u, 0u, 1u, 36u,
		HI5622_DFSB_WIDTH_20, 0u, 0u, NULL, 0u, NULL), -EINVAL);
	ck_rc("plan conflict dfs_cac+dfs_set", hi5622_dfsb_ap_plan(0u, 1u,
		1u, 1u, 36u, HI5622_DFSB_WIDTH_20, 0u, 0u, NULL, 0u, &plan),
	      -EINVAL);

	ck_rc("plan ht_bw=1 160 ch36", hi5622_dfsb_ap_plan(1u, 0u, 0u, 1u,
		36u, HI5622_DFSB_WIDTH_160, 5250u, 0u, NULL, 0u, &plan), 0);
	ck("plan 160 enabled sel 3", plan.width_enabled &&
	   plan.width_sel == HI5622_DFSB_BW_160);
	ck_rc("plan ht_bw=1 80P80", hi5622_dfsb_ap_plan(1u, 0u, 0u, 1u, 36u,
		HI5622_DFSB_WIDTH_80P80, 5250u, 5210u, NULL, 0u, &plan),
	      -EOPNOTSUPP);

	/* the DFS gate: a DFS channel without a finished CAC is refused */
	memset(&st, 0, sizeof(st));
	ck_rc("plan DFS ch52 no CAC is EAGAIN", hi5622_dfsb_ap_plan(1u, 1u,
		0u, 1u, 52u, HI5622_DFSB_WIDTH_80, 5290u, 0u, &st, 0u,
		&plan), -EAGAIN);
	ck("plan DFS ch52 cac_required", plan.cac_required == true);
	ck("plan DFS ch52 gate failed", plan.cac_gate == false);
	ck_rc("plan DFS ch52 null cac", hi5622_dfsb_ap_plan(1u, 1u, 0u, 1u,
		52u, HI5622_DFSB_WIDTH_80, 5290u, 0u, NULL, 0u, &plan),
	      -EINVAL);

	ck_rc("CAC ch52 begin", hi5622_dfsb_cac_begin(&st, 52u, 5260u,
		60000u), 0);
	ck_rc("plan DFS ch52 CAC armed (not finished)",
		hi5622_dfsb_ap_plan(1u, 1u, 0u, 1u, 52u,
		HI5622_DFSB_WIDTH_80, 5290u, 0u, &st, 30000u, &plan),
	      -EAGAIN);
	ck_rc("CAC ch52 tick", hi5622_dfsb_cac_tick(&st, 63000u, &step), 0);
	ck_rc("plan DFS ch52 CAC finished", hi5622_dfsb_ap_plan(1u, 1u, 0u,
		1u, 52u, HI5622_DFSB_WIDTH_80, 5290u, 0u, &st, 70000u,
		&plan), 0);
	ck("plan DFS ch52 gate passed", plan.cac_required == true &&
	   plan.cac_gate == true);
	ck("plan DFS ch52 width sel 2",
	   plan.width_enabled && plan.width_sel == HI5622_DFSB_BW_80);

	/* non-DFS 5 GHz and 2.4 GHz channels need no CAC gate */
	ck_rc("plan DFS-mode ch36 no CAC needed", hi5622_dfsb_ap_plan(0u,
		1u, 0u, 1u, 36u, HI5622_DFSB_WIDTH_20, 0u, 0u, &st,
		70000u, &plan), 0);
	ck("plan ch36 no cac", plan.cac_required == false);
	ck_rc("plan DFS-mode 2.4 GHz", hi5622_dfsb_ap_plan(0u, 1u, 0u, 0u,
		1u, HI5622_DFSB_WIDTH_20, 0u, 0u, &st, 70000u, &plan), 0);
	ck("plan 2.4 no cac", plan.cac_required == false);
}

int main(void)
{
	printf("dfs160b host selftest - the 160 MHz width selector + DFS/CAC/NOL model (task-245)\n");
	test_width_enum_pins();
	test_width_plan();
	test_channels();
	test_ccfs1();
	test_cac();
	test_set_frame();
	test_order();
	test_ap_plan();
	printf("RESULT: %u checks, %u failure(s)\n", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
