/* SPDX-License-Identifier: GPL-2.0 */
/*
 * band5g_selftest.c - host-side test for hi5622_band5g.c (wifi-5ghz lane).
 *
 * Pure C99, no kernel headers: checks the decoded vendor band tables, the
 * frequency<->channel<->index mapping (host .ko tables + firmware CA
 * 0x10172C semantics), the AP-start channel gate (band5g fail-closed) and
 * the H1 band-config channel derivation.
 *
 * Build+run: tools/selftest.sh
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_band5g.h"

static int checks;
static int failures;

static void check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		failures++;
		printf("FAIL %s\n", what);
	} else {
		printf("PASS %s\n", what);
	}
}

static void test_tables(void)
{
	const struct hi5622_band5g_chan *t2, *t5;
	u16 n2 = 0, n5 = 0;
	int i;

	t2 = hi5622_band5g_channels(HI5622_BAND5G_2GHZ, &n2);
	t5 = hi5622_band5g_channels(HI5622_BAND5G_5GHZ, &n5);
	check(t2 != NULL, "2.4 GHz table present");
	check(t5 != NULL, "5 GHz table present");
	check(n2 == HI5622_BAND5G_2G_COUNT, "2.4 GHz table has 14 entries");
	check(n5 == HI5622_BAND5G_5G_COUNT, "5 GHz table has 29 entries");
	check(hi5622_band5g_channels(2u, &n2) == NULL && n2 == 0,
	      "invalid band rejected by table lookup");

	/* 2.4 GHz: ch1..13 at 2412+5*(ch-1), ch14 at 2484. */
	for (i = 0; i < 14; i++) {
		u16 want = (i < 13) ? (u16)(2412 + 5 * i) : 2484u;

		check(t2[i].num == (u8)(i + 1) && t2[i].freq == want,
		      "2.4 GHz entry order/frequency");
	}
	/* 5 GHz: the exact vendor channel list, 36..196.  5 GHz channels are
	 * 5000 + 5*channel; the 4.9 GHz public-safety block (184..196) is
	 * 4000 + 5*channel = 4920..4980 MHz. */
	{
		static const u8 nums[29] = {
			36, 40, 44, 48, 52, 56, 60, 64,
			100, 104, 108, 112, 116, 120, 124, 128, 132, 136,
			140, 144, 149, 153, 157, 161, 165,
			184, 188, 192, 196,
		};

		for (i = 0; i < 29; i++) {
			u16 want = (u16)((nums[i] >= 184 ? 4000 : 5000) +
					 5 * nums[i]);

			check(t5[i].num == nums[i] && t5[i].freq == want,
			      "5 GHz entry order/frequency (36..196)");
		}
	}
	check(t5[19].num == 144 && t5[19].freq == 5720,
	      "5 GHz table includes ch144 (5720 MHz)");
	check(t5[25].num == 184 && t5[25].freq == 4920,
	      "5 GHz table includes the 4.9 GHz block");
	check((t5[4].flags & HI5622_BAND5G_CHAN_RADAR) != 0,
	      "ch52 marked DFS/radar");
	check(t5[20].flags == 0, "ch149 unrestricted by the vendor table");
	check((t5[28].flags & HI5622_BAND5G_CHAN_NO_IR) != 0,
	      "ch196 passive (NO_IR)");
}

static void test_mapping(void)
{
	const struct hi5622_band5g_chan *t2, *t5;
	u16 n2 = 0, n5 = 0;
	int i, bad = 0;

	t2 = hi5622_band5g_channels(HI5622_BAND5G_2GHZ, &n2);
	t5 = hi5622_band5g_channels(HI5622_BAND5G_5GHZ, &n5);

	for (i = 0; i < (int)n2; i++) {
		u8 ch = 0, idx = 0xff;
		u16 f = 0;

		if (hi5622_band5g_freq_to_chan(0, t2[i].freq, &ch) ||
		    ch != t2[i].num)
			bad++;
		if (hi5622_band5g_chan_to_freq(0, t2[i].num, &f) ||
		    f != t2[i].freq)
			bad++;
		if (hi5622_band5g_chan_index(0, t2[i].num, &idx) ||
		    idx != (u8)i)
			bad++;
	}
	check(bad == 0, "2.4 GHz freq<->channel<->index roundtrip (14)");

	bad = 0;
	for (i = 0; i < (int)n5; i++) {
		u8 ch = 0, idx = 0xff;
		u16 f = 0;

		if (hi5622_band5g_freq_to_chan(1, t5[i].freq, &ch) ||
		    ch != t5[i].num)
			bad++;
		if (hi5622_band5g_chan_to_freq(1, t5[i].num, &f) ||
		    f != t5[i].freq)
			bad++;
		/* The firmware index (CA 0x10172C) is the table position: this
		 * is the value the scan request entry+0x03 carries. */
		if (hi5622_band5g_chan_index(1, t5[i].num, &idx) ||
		    idx != (u8)i)
			bad++;
	}
	check(bad == 0, "5 GHz freq<->channel<->index roundtrip (29)");

	{
		u8 ch = 0, idx = 0;

		check(hi5622_band5g_freq_to_chan(0, 0, &ch) == -ENOENT,
		      "freq 0 not in 2.4 GHz table");
		check(hi5622_band5g_freq_to_chan(0, 2400, &ch) == -ENOENT,
		      "2400 MHz not in 2.4 GHz table");
		check(hi5622_band5g_freq_to_chan(1, 6000, &ch) == -ENOENT,
		      "6000 MHz not in 5 GHz table");
		check(hi5622_band5g_freq_to_chan(5, 5180, &ch) == -EINVAL,
		      "invalid band rejected by freq lookup");
		check(hi5622_band5g_chan_to_freq(0, 15, &(u16){0}) == -ENOENT,
		      "2.4 GHz channel 15 not in table");
		check(hi5622_band5g_chan_index(1, 145, &idx) == -ENOENT,
		      "5 GHz channel 145 not in table");
		check(hi5622_band5g_chan_valid(0, 14) == 1,
		      "channel 14 is in the vendor 2.4 GHz table");
		check(hi5622_band5g_chan_valid(0, 0) == 0,
		      "channel 0 invalid");
		check(hi5622_band5g_chan_valid(1, 36) == 1,
		      "channel 36 valid");
		check(hi5622_band5g_chan_valid(1, 196) == 1,
		      "channel 196 valid");
		check(hi5622_band5g_chan_valid(1, 197) == 0,
		      "channel 197 invalid");
		check(hi5622_band5g_chan_index(1, 149, &idx) == 0 && idx == 20,
		      "ch149 vendor index == 20 (not the 19-ch LU index)");
		check(hi5622_band5g_chan_index(1, 144, &idx) == 0 && idx == 19,
		      "ch144 vendor index == 19");
		check(hi5622_band5g_chan_count(0) == 14 &&
		      hi5622_band5g_chan_count(1) == 29 &&
		      hi5622_band5g_chan_count(9) == 0,
		      "channel counts 14/29/invalid");
	}
}

static void test_ap_gate(void)
{
	struct hi5622_band5g_ap_chan ac;

	/* 2.4 GHz: unchanged bench-validated behaviour, band5g open or shut. */
	memset(&ac, 0, sizeof(ac));
	check(hi5622_band5g_ap_chan_prepare(0, 0, 2437, 6, &ac) == 0 &&
	      ac.band == 0 && ac.channel == 6,
	      "2.4 GHz AP ch6 accepted with band5g=0");
	check(hi5622_band5g_ap_chan_prepare(1, 0, 2437, 6, &ac) == 0,
	      "2.4 GHz AP ch6 accepted with band5g=1");
	check(hi5622_band5g_ap_chan_prepare(0, 0, 2484, 14, &ac) == 0 &&
	      ac.channel == 14,
	      "2.4 GHz AP ch14 accepted (vendor table)");

	/* 5 GHz: fail-closed without band5g. */
	check(hi5622_band5g_ap_chan_prepare(0, 1, 5180, 36, &ac) ==
	      -EOPNOTSUPP, "5 GHz AP refused while band5g=0 (fail closed)");
	check(hi5622_band5g_ap_chan_prepare(1, 1, 5180, 36, &ac) == 0 &&
	      ac.band == 1 && ac.channel == 36,
	      "5 GHz AP ch36 accepted with band5g=1");
	check(hi5622_band5g_ap_chan_prepare(1, 1, 0, 0, &ac) == -EINVAL,
	      "5 GHz AP with no channel/frequency refused");
	check(hi5622_band5g_ap_chan_prepare(1, 1, 5180, 0, &ac) == 0 &&
	      ac.channel == 36, "5 GHz AP ch36 derived from 5180 MHz");
	check(hi5622_band5g_ap_chan_prepare(1, 1, 5720, 144, &ac) == 0 &&
	      ac.channel == 144, "5 GHz AP ch144 (DFS) resolved");
	check(hi5622_band5g_ap_chan_prepare(1, 1, 4920, 184, &ac) == 0 &&
	      ac.channel == 184, "5 GHz AP ch184 (4.9 GHz block) resolved");
	check(hi5622_band5g_ap_chan_prepare(1, 1, 5220, 36, &ac) == -EINVAL,
	      "5 GHz AP channel/frequency mismatch refused");
	check(hi5622_band5g_ap_chan_prepare(1, 1, 5200, 40, NULL) == -EINVAL,
	      "AP prepare NULL output refused");
	check(hi5622_band5g_ap_chan_prepare(1, 1, 5350, 68, &ac) != 0,
	      "5 GHz AP channel 68 (not in vendor table) refused");
	check(hi5622_band5g_ap_chan_prepare(1, 2, 5180, 36, &ac) == -EINVAL,
	      "AP prepare invalid band refused");
}

static void test_band_cfg_chan(void)
{
	u16 ch = 0;

	/* H1 g0 id10 channel field = IEEE channel number (firmware table
	 * lookup), never the raw center frequency. */
	check(hi5622_band5g_band_cfg_chan(0, 0, 2412, &ch) == 0 && ch == 1,
	      "H1 channel for 2412 MHz == 1");
	check(hi5622_band5g_band_cfg_chan(1, 0, 2484, &ch) == 0 && ch == 14,
	      "H1 channel for 2484 MHz == 14");
	check(hi5622_band5g_band_cfg_chan(1, 1, 5180, &ch) == 0 && ch == 36,
	      "H1 channel for 5180 MHz == 36 (not 5180)");
	check(hi5622_band5g_band_cfg_chan(1, 1, 5745, &ch) == 0 && ch == 149,
	      "H1 channel for 5745 MHz == 149");
	check(hi5622_band5g_band_cfg_chan(1, 1, 4920, &ch) == 0 && ch == 184,
	      "H1 channel for 4920 MHz == 184");
	check(hi5622_band5g_band_cfg_chan(0, 1, 5180, &ch) == -EOPNOTSUPP,
	      "H1 5 GHz config refused while band5g=0");
	check(hi5622_band5g_band_cfg_chan(1, 1, 6000, &ch) == -ENOENT,
	      "H1 unknown 5 GHz frequency refused");
	check(hi5622_band5g_band_cfg_chan(1, 0, 5180, &ch) == -ENOENT,
	      "H1 5 GHz frequency on the 2.4 GHz band refused");
	check(hi5622_band5g_band_cfg_chan(1, 1, 5180, NULL) == -EINVAL,
	      "H1 NULL output refused");
}

static void test_caps(void)
{
	check(hi5622_band5g_ht_cap_word(1) == 0x0062,
	      "5 GHz HT cap word 0x0062 (20/40+SGI20/40)");
	check(hi5622_band5g_vht_cap_word(1) == 0x00400064,
	      "5 GHz VHT cap word 0x00400064 (HTC_VHT|160MHz|SGI80/160)");
	check(hi5622_band5g_vht_cap_word(0) == 0x00400000,
	      "2.4 GHz VHT cap word 0x00400000 (HTC_VHT only)");
	check(hi5622_band5g_vht_mcs_map(1) == 0xfffa &&
	      hi5622_band5g_vht_mcs_map(0) == 0xfffa,
	      "VHT MCS map 0xfffa (2 SS MCS0-9)");
	check(hi5622_band5g_vht_mcs_map(7) == 0xffff,
	      "invalid band VHT MCS map sentinel");
	check(hi5622_band5g_max_power(0) == 30 &&
	      hi5622_band5g_max_power(1) == 23,
	      "max power 30 dBm 2.4 / 23 dBm 5");
	check(strcmp(hi5622_band5g_name(1), "5GHz") == 0 &&
	      strcmp(hi5622_band5g_name(0), "2.4GHz") == 0,
	      "band names");
}

/* ------------------------------------------------------------------
 * wifi-5ghz-bringup lane (task-154): the one-radio/two-band wiphy plan.
 *
 * The board's two PCI functions are two host views of ONE chip (both print
 * chip 0, the BAR0 windows alias the same register space, only instance 0 is
 * initialised).  band5g=1 therefore puts BOTH bands on the PRIMARY endpoint's
 * wiphy (domain 0) and gives the secondary host view no wiphy; band5g=0 keeps
 * the bench-validated per-endpoint single-band model.
 * ------------------------------------------------------------------ */
static void test_wiphy_bands(void)
{
	u8 plan[HI5622_BAND5G_MAX_WIPHY_BANDS] = { 0xff, 0xff };
	unsigned int n = 99;

	/* band5g=0: legacy per-endpoint model (domain 0 = 2.4, domain 1 = 5). */
	check(hi5622_band5g_wiphy_bands(0, 0, plan, 2, &n) == 0 && n == 1 &&
	      plan[0] == HI5622_BAND5G_2GHZ,
	      "band5g=0 domain0: one 2.4 GHz band (legacy model)");
	check(hi5622_band5g_wiphy_bands(0, 1, plan, 2, &n) == 0 && n == 1 &&
	      plan[0] == HI5622_BAND5G_5GHZ,
	      "band5g=0 domain1: one 5 GHz band (legacy model)");
	check(hi5622_band5g_wiphy_bands(0, 2, plan, 2, &n) == 0 && n == 1 &&
	      plan[0] == HI5622_BAND5G_2GHZ,
	      "band5g=0 domain2: auto rule keeps 2.4 GHz");

	/* band5g=1: primary carries both bands, secondary carries no wiphy. */
	check(hi5622_band5g_wiphy_bands(1, 0, plan, 2, &n) == 0 && n == 2 &&
	      plan[0] == HI5622_BAND5G_2GHZ &&
	      plan[1] == HI5622_BAND5G_5GHZ,
	      "band5g=1 domain0: BOTH bands on the primary wiphy");
	check(hi5622_band5g_wiphy_bands(1, 1, plan, 2, &n) == 0 && n == 0,
	      "band5g=1 domain1: secondary host view gets NO wiphy");
	check(hi5622_band5g_wiphy_bands(1, 2, plan, 2, &n) == 0 && n == 0,
	      "band5g=1 domain>1: no wiphy");
	check(hi5622_band5g_wiphy_bands(1, 0, plan, 1, &n) == -ENOSPC,
	      "band5g=1 plan refuses a too-small band array");
	check(hi5622_band5g_wiphy_bands(0, 0, plan, 0, &n) == -ENOSPC,
	      "band5g=0 plan refuses a zero-length array");
	check(hi5622_band5g_wiphy_bands(1, 0, NULL, 2, &n) == -EINVAL,
	      "band plan NULL bands refused");
	check(hi5622_band5g_wiphy_bands(1, 0, plan, 2, NULL) == -EINVAL,
	      "band plan NULL count refused");
	check(hi5622_band5g_wiphy_bands(1, -1, plan, 2, &n) == -EINVAL,
	      "band plan negative PCI domain refused");
	check(HI5622_BAND5G_MAX_WIPHY_BANDS == 2u,
	      "max wiphy bands is 2 (2.4 GHz + 5 GHz)");
}

static void test_freq_band(void)
{
	/* Both vendor tables on one wiphy: the scan-request band is derived from
	 * the requested centre frequency, never from the legacy wiphy band. */
	check(hi5622_band5g_freq_band(2412) == 0 &&
	      hi5622_band5g_freq_band(2437) == 0 &&
	      hi5622_band5g_freq_band(2472) == 0 &&
	      hi5622_band5g_freq_band(2484) == 0,
	      "freq->band: 2.4 GHz channels map to band 0");
	check(hi5622_band5g_freq_band(5180) == 1 &&
	      hi5622_band5g_freq_band(5240) == 1 &&
	      hi5622_band5g_freq_band(5500) == 1 &&
	      hi5622_band5g_freq_band(5720) == 1 &&
	      hi5622_band5g_freq_band(5745) == 1 &&
	      hi5622_band5g_freq_band(5825) == 1,
	      "freq->band: 5 GHz channels map to band 1");
	check(hi5622_band5g_freq_band(4920) == 1 &&
	      hi5622_band5g_freq_band(4980) == 1,
	      "freq->band: 4.9 GHz block maps to band 1");
	check(hi5622_band5g_freq_band(6000) == -ENOENT &&
	      hi5622_band5g_freq_band(0) == -ENOENT &&
	      hi5622_band5g_freq_band(2500) == -ENOENT,
	      "freq->band: unknown frequency refused");
}

int main(void)
{
	test_tables();
	test_mapping();
	test_ap_gate();
	test_band_cfg_chan();
	test_caps();
	test_wiphy_bands();
	test_freq_band();
	printf("\n%d checks, %d failure(s)\n", checks, failures);
	return failures ? 1 : 0;
}
