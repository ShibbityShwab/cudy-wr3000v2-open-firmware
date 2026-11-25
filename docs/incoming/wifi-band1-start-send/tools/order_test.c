// SPDX-License-Identifier: GPL-2.0
/*
 * order_test.c - host-only test of the AP-start ordering contract in the
 * patched driver model (lane wifi-band1-start-send, task-206).
 *
 * HOST-ONLY: compiled against the patched driver tree's portable
 * hi5622_scan_logic.c (no kernel, no device).  It asserts the required order
 *
 *   vap_add (0x0064) -> [desc_table 0x00F7] -> [dfs_mode 0x01F7] ->
 *   [band_start 0x0242] -> ap_chan (0x0124) -> ap_ssid -> ap_cfg (0x0127) ->
 *   ap_vap_start (0x0065)
 *
 * i.e. the 0x0242 band-start step is AFTER the 0x01F7 DFS-mode step and
 * BEFORE the 0x0124 channel post when the parameters are 1, and the step
 * exists only for its own fail-closed parameter == 1.  The optional
 * band_start_fallback rungs run INSIDE the band_start step (same plan slot),
 * so the plan length never depends on them.  Run from
 * tools/run-host-tests.sh with -I<patched tree>.
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_scan_logic.h"

static int checks;
static int fails;

static void check(int cond, const char *what)
{
	checks++;
	if (!cond) {
		fails++;
		printf("FAIL %s\n", what);
	}
}

static void check_steps(const u8 *got, unsigned int n, const u8 *want,
			unsigned int want_n, const char *what)
{
	unsigned int i;
	int ok = (n == want_n);

	for (i = 0; ok && i < n; i++)
		if (got[i] != want[i])
			ok = 0;
	checks++;
	if (!ok) {
		fails++;
		printf("FAIL %s: n=%u want %u [", what, n, want_n);
		for (i = 0; i < n; i++)
			printf("%s%s", i ? "," : "",
			       hi5622_beacon_step_name(got[i]));
		printf("]\n");
	}
}

int main(void)
{
	static const u8 base_plan[] = {
		HI5622_BEACON_STEP_VAP_ADD, HI5622_BEACON_STEP_CHAN,
		HI5622_BEACON_STEP_SSID, HI5622_BEACON_STEP_AP_CFG,
		HI5622_BEACON_STEP_VAP_START,
	};
	static const u8 desc_plan[] = {
		HI5622_BEACON_STEP_VAP_ADD, HI5622_BEACON_STEP_DESC_TABLE,
		HI5622_BEACON_STEP_CHAN, HI5622_BEACON_STEP_SSID,
		HI5622_BEACON_STEP_AP_CFG, HI5622_BEACON_STEP_VAP_START,
	};
	static const u8 dfs_plan[] = {
		HI5622_BEACON_STEP_VAP_ADD, HI5622_BEACON_STEP_DFS_MODE,
		HI5622_BEACON_STEP_CHAN, HI5622_BEACON_STEP_SSID,
		HI5622_BEACON_STEP_AP_CFG, HI5622_BEACON_STEP_VAP_START,
	};
	/* band_start only: exactly between vap_add and ap_chan. */
	static const u8 bs_plan[] = {
		HI5622_BEACON_STEP_VAP_ADD, HI5622_BEACON_STEP_BAND_START,
		HI5622_BEACON_STEP_CHAN, HI5622_BEACON_STEP_SSID,
		HI5622_BEACON_STEP_AP_CFG, HI5622_BEACON_STEP_VAP_START,
	};
	/* The FIRE-RECIPE ladder: desc_table=1 dfs_mode=1 band_start=1. */
	static const u8 full_plan[] = {
		HI5622_BEACON_STEP_VAP_ADD, HI5622_BEACON_STEP_DESC_TABLE,
		HI5622_BEACON_STEP_DFS_MODE, HI5622_BEACON_STEP_BAND_START,
		HI5622_BEACON_STEP_CHAN, HI5622_BEACON_STEP_SSID,
		HI5622_BEACON_STEP_AP_CFG, HI5622_BEACON_STEP_VAP_START,
	};
	static const u8 full_no_vap_start[] = {
		HI5622_BEACON_STEP_VAP_ADD, HI5622_BEACON_STEP_DESC_TABLE,
		HI5622_BEACON_STEP_DFS_MODE, HI5622_BEACON_STEP_BAND_START,
		HI5622_BEACON_STEP_CHAN, HI5622_BEACON_STEP_SSID,
		HI5622_BEACON_STEP_AP_CFG,
	};
	u8 out[HI5622_BEACON_STEP_MAX];
	unsigned int n;

	/* 0 (default): the bench-fired F2 ladder, byte-for-byte unchanged. */
	n = hi5622_beacon_plan(1u, 1u, 0u, 0u, 0u, out,
			       HI5622_BEACON_STEP_MAX);
	check_steps(out, n, base_plan, 5u,
		    "plan all knobs 0 is the base ladder");

	/* desc_table only: the 0x00F7 step exactly between vap_add and ap_chan. */
	n = hi5622_beacon_plan(1u, 1u, 1u, 0u, 0u, out,
			       HI5622_BEACON_STEP_MAX);
	check_steps(out, n, desc_plan, 6u,
		    "plan desc_table=1: vap_add -> desc_table -> ap_chan");

	/* dfs_mode only: the 0x01F7 step exactly between vap_add and ap_chan. */
	n = hi5622_beacon_plan(1u, 1u, 0u, 1u, 0u, out,
			       HI5622_BEACON_STEP_MAX);
	check_steps(out, n, dfs_plan, 6u,
		    "plan dfs_mode=1: vap_add -> dfs_mode -> ap_chan");

	/* band_start only: the 0x0242 step exactly between vap_add and
	 * ap_chan (after the others when they are also armed). */
	n = hi5622_beacon_plan(1u, 1u, 0u, 0u, 1u, out,
			       HI5622_BEACON_STEP_MAX);
	check_steps(out, n, bs_plan, 6u,
		    "plan band_start=1: vap_add -> band_start -> ap_chan");

	/* The FIRE-RECIPE order: 0x00F7 table, 0x01F7 mode, 0x0242 start,
	 * then the 0x0124 channel post. */
	n = hi5622_beacon_plan(1u, 1u, 1u, 1u, 1u, out,
			       HI5622_BEACON_STEP_MAX);
	check_steps(out, n, full_plan, 8u,
		    "plan full=1: vap_add -> desc_table -> dfs_mode -> band_start -> ap_chan");

	/* The ladder-isolation rung drops only the final VAP-start step. */
	n = hi5622_beacon_plan(1u, 0u, 1u, 1u, 1u, out,
			       HI5622_BEACON_STEP_MAX);
	check_steps(out, n, full_no_vap_start, 7u,
		    "plan full=1 vap_start=0 drops only ap_vap_start");

	/* Truncation: never overflow, always a prefix of the full plan. */
	n = hi5622_beacon_plan(1u, 1u, 1u, 1u, 1u, out, 4u);
	check(n == 4u && out[0] == HI5622_BEACON_STEP_VAP_ADD &&
	      out[1] == HI5622_BEACON_STEP_DESC_TABLE &&
	      out[2] == HI5622_BEACON_STEP_DFS_MODE &&
	      out[3] == HI5622_BEACON_STEP_BAND_START,
	      "plan max=4 truncates to vap_add,desc_table,dfs_mode,band_start");
	n = hi5622_beacon_plan(1u, 1u, 1u, 1u, 1u, out, 0u);
	check(n == 0u, "plan max=0 -> 0 steps");
	n = hi5622_beacon_plan(0u, 1u, 1u, 1u, 1u, out,
			       HI5622_BEACON_STEP_MAX);
	check(n == 0u, "plan enabled=0 -> 0 steps (legacy path)");
	n = hi5622_beacon_plan(1u, 1u, 1u, 1u, 1u, NULL,
			       HI5622_BEACON_STEP_MAX);
	check(n == 0u, "plan NULL out -> 0 steps");

	/* Enum order is the contract: the 0x0242 step sits strictly between
	 * the 0x01F7 step and the 0x0124 channel step. */
	check(HI5622_BEACON_STEP_VAP_ADD < HI5622_BEACON_STEP_DESC_TABLE &&
	      HI5622_BEACON_STEP_DESC_TABLE < HI5622_BEACON_STEP_DFS_MODE &&
	      HI5622_BEACON_STEP_DFS_MODE < HI5622_BEACON_STEP_BAND_START &&
	      HI5622_BEACON_STEP_BAND_START < HI5622_BEACON_STEP_CHAN &&
	      HI5622_BEACON_STEP_CHAN < HI5622_BEACON_STEP_SSID &&
	      HI5622_BEACON_STEP_SSID < HI5622_BEACON_STEP_AP_CFG &&
	      HI5622_BEACON_STEP_AP_CFG < HI5622_BEACON_STEP_VAP_START &&
	      HI5622_BEACON_STEP_VAP_START < HI5622_BEACON_STEP_MAX,
	      "enum order vap_add < desc_table < dfs_mode < band_start < ap_chan < ...");

	/* The step names the dmesg plan line prints. */
	check(strcmp(hi5622_beacon_step_name(HI5622_BEACON_STEP_BAND_START),
		     "band_start") == 0, "step name band_start");
	check(strcmp(hi5622_beacon_step_name(HI5622_BEACON_STEP_DFS_MODE),
		     "dfs_mode") == 0, "step name dfs_mode");
	check(strcmp(hi5622_beacon_step_name(HI5622_BEACON_STEP_CHAN),
		     "ap_chan") == 0, "step name ap_chan");
	check(strcmp(hi5622_beacon_step_name(0xfeu), "none") == 0,
	      "step name unknown -> none");

	printf("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
