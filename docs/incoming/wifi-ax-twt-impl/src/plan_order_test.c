// SPDX-License-Identifier: GPL-2.0
/*
 * plan_order_test.c - host test for the AP-start plan ordering contract with
 * the TWT-enable step (lane wifi-ax-twt-impl, task-199).  Compiles the
 * portable model hi5622_scan_logic.c on the host (no kernel, no device) and
 * asserts exactly where the twt step appears:
 *
 *   vap_add [desc_table] [dfs_mode] ap_chan ap_ssid ap_cfg ap_vap_start [twt]
 *
 * and that twt=0 / twt=2 / vap_start=0 never add it (the kernel layer refuses
 * twt=2 before the plan is built; the plan itself must stay inert).
 */
#include "hi5622_scan_logic.h"
#include "hi5622_ax_twt.h"

#include <stdio.h>
#include <string.h>

static int checks;
static int fails;

static void ck(const char *what, int ok)
{
	checks++;
	if (!ok) {
		fails++;
		printf("FAIL %s\n", what);
	}
}

static int plan_is(const u8 *plan, unsigned int n, const char *want)
{
	char got[256];
	unsigned int i;
	size_t pos = 0;

	for (i = 0; i < n; i++) {
		int w = snprintf(got + pos, sizeof(got) - pos, "%s%s",
				 i ? " " : "", hi5622_beacon_step_name(plan[i]));

		if (w < 0 || (size_t)w >= sizeof(got) - pos)
			return 0;
		pos += (size_t)w;
	}
	return !strcmp(got, want);
}

int main(void)
{
	u8 plan[HI5622_BEACON_STEP_MAX];
	unsigned int n;

	/* twt=0 (default): the plan is byte-identical to the sibling lanes. */
	n = hi5622_beacon_plan(1u, 1u, 0u, 0u, 0u, plan, sizeof(plan));
	ck("no-fix plan", n == 5u &&
	   plan_is(plan, n, "vap_add ap_chan ap_ssid ap_cfg ap_vap_start"));
	ck("no twt name", !strcmp(hi5622_beacon_step_name(
		HI5622_BEACON_STEP_TWT_ENABLE), "twt"));

	/* 5 GHz/SR sibling steps still ordered around the new last step. */
	n = hi5622_beacon_plan(1u, 1u, 1u, 1u, 0u, plan, sizeof(plan));
	ck("desc+dfs plan", n == 7u &&
	   plan_is(plan, n,
		   "vap_add desc_table dfs_mode ap_chan ap_ssid ap_cfg ap_vap_start"));

	/* TWT enable / enable+required: LAST, after the VAP start. */
	n = hi5622_beacon_plan(1u, 1u, 0u, 0u, 1u, plan, sizeof(plan));
	ck("twt=1 plan", n == 6u &&
	   plan_is(plan, n,
		   "vap_add ap_chan ap_ssid ap_cfg ap_vap_start twt"));
	n = hi5622_beacon_plan(1u, 1u, 0u, 0u, 3u, plan, sizeof(plan));
	ck("twt=3 plan", n == 6u &&
	   plan_is(plan, n,
		   "vap_add ap_chan ap_ssid ap_cfg ap_vap_start twt"));
	n = hi5622_beacon_plan(1u, 1u, 1u, 1u, 1u, plan, sizeof(plan));
	ck("twt after all sibling steps", n == 8u &&
	   plan_is(plan, n,
		   "vap_add desc_table dfs_mode ap_chan ap_ssid ap_cfg ap_vap_start twt"));

	/* The plan model never accepts a non-{1,3} twt value, and a dropped
	 * VAP start drops the TWT post with it (the post needs the AP). */
	ck("twt=2 inert", (n = hi5622_beacon_plan(1u, 1u, 0u, 0u, 2u, plan,
						   sizeof(plan))) == 5u &&
	   plan_is(plan, n, "vap_add ap_chan ap_ssid ap_cfg ap_vap_start"));
	ck("twt=4 inert", (n = hi5622_beacon_plan(1u, 1u, 0u, 0u, 4u, plan,
						   sizeof(plan))) == 5u &&
	   plan_is(plan, n, "vap_add ap_chan ap_ssid ap_cfg ap_vap_start"));
	ck("no vap_start -> no twt",
	   (n = hi5622_beacon_plan(1u, 0u, 0u, 0u, 1u, plan, sizeof(plan))) == 4u &&
	   plan_is(plan, n, "vap_add ap_chan ap_ssid ap_cfg"));

	/* enabled=0 is the legacy path: empty plan, no TWT. */
	ck("disabled plan", hi5622_beacon_plan(0u, 1u, 1u, 1u, 3u, plan,
					       sizeof(plan)) == 0u);

	/* Truncation is safe (never writes past max). */
	memset(plan, 0xAA, sizeof(plan));
	ck("truncated", hi5622_beacon_plan(1u, 1u, 0u, 0u, 1u, plan, 3u) == 3u &&
	   plan_is(plan, 3u, "vap_add ap_chan ap_ssid") && plan[3] == 0xAAu);
	ck("null out", hi5622_beacon_plan(1u, 1u, 0u, 0u, 1u, NULL, 9u) == 0u);

	printf("plan-order selftest: %d/%d PASS\n", checks - fails, checks);
	return fails ? 1 : 0;
}
