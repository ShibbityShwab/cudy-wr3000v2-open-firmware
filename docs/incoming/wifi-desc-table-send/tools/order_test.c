// SPDX-License-Identifier: GPL-2.0
/*
 * order_test.c - host-only test of the AP-start ordering contract in the
 * patched driver model (lane wifi-desc-table-send, task-188).
 *
 * HOST-ONLY: compiled against the patched driver tree's portable
 * hi5622_scan_logic.c (no kernel, no device).  It asserts the required order
 *   vap_add (0x0064) -> desc_table (0x00F7) -> ap_chan (0x0124) -> ...
 * and that the 0x00F7 step exists only for desc_table == 1.  Run from
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
	static const u8 no_vap_start[] = {
		HI5622_BEACON_STEP_VAP_ADD, HI5622_BEACON_STEP_CHAN,
		HI5622_BEACON_STEP_SSID, HI5622_BEACON_STEP_AP_CFG,
	};
	static const u8 fix_plan[] = {
		HI5622_BEACON_STEP_VAP_ADD, HI5622_BEACON_STEP_DESC_TABLE,
		HI5622_BEACON_STEP_CHAN, HI5622_BEACON_STEP_SSID,
		HI5622_BEACON_STEP_AP_CFG, HI5622_BEACON_STEP_VAP_START,
	};
	static const u8 fix_no_vap_start[] = {
		HI5622_BEACON_STEP_VAP_ADD, HI5622_BEACON_STEP_DESC_TABLE,
		HI5622_BEACON_STEP_CHAN, HI5622_BEACON_STEP_SSID,
		HI5622_BEACON_STEP_AP_CFG,
	};
	u8 out[HI5622_BEACON_STEP_MAX];
	unsigned int n;

	/* 0 (default): the bench-fired ladder, byte-for-byte unchanged. */
	n = hi5622_beacon_plan(1u, 1u, 0u, out, HI5622_BEACON_STEP_MAX);
	check_steps(out, n, base_plan, 5u, "plan desc_table=0 is the base ladder");

	/* 1: the 0x00F7 step exactly between vap_add and ap_chan. */
	n = hi5622_beacon_plan(1u, 1u, 1u, out, HI5622_BEACON_STEP_MAX);
	check_steps(out, n, fix_plan, 6u,
		    "plan desc_table=1: vap_add -> desc_table -> ap_chan");
	check(n >= 3u && out[0] == HI5622_BEACON_STEP_VAP_ADD &&
		      out[1] == HI5622_BEACON_STEP_DESC_TABLE &&
		      out[2] == HI5622_BEACON_STEP_CHAN,
	      "0x00F7 step index 1 (after 0x0064, before 0x0124)");

	/* Any other value is not a step (the kernel refuses it before use). */
	n = hi5622_beacon_plan(1u, 1u, 2u, out, HI5622_BEACON_STEP_MAX);
	check_steps(out, n, base_plan, 5u, "plan desc_table=2 keeps the base ladder");

	/* Ladder isolation rung and truncation still behave. */
	n = hi5622_beacon_plan(1u, 0u, 1u, out, HI5622_BEACON_STEP_MAX);
	check_steps(out, n, fix_no_vap_start, 5u,
		    "plan desc_table=1 with vap_start=0 drops only vap_start");
	n = hi5622_beacon_plan(1u, 0u, 0u, out, HI5622_BEACON_STEP_MAX);
	check_steps(out, n, no_vap_start, 4u, "plan vap_start=0 base ladder");
	n = hi5622_beacon_plan(1u, 1u, 1u, out, 2u);
	check(n == 2u && out[0] == HI5622_BEACON_STEP_VAP_ADD &&
		      out[1] == HI5622_BEACON_STEP_DESC_TABLE,
	      "plan truncates safely at max=2");
	n = hi5622_beacon_plan(0u, 1u, 1u, out, HI5622_BEACON_STEP_MAX);
	check(n == 0u, "plan disabled returns 0 steps");
	n = hi5622_beacon_plan(1u, 1u, 1u, NULL, HI5622_BEACON_STEP_MAX);
	check(n == 0u, "plan NULL out returns 0 steps");

	/* Step ids are ordered like the wire order, and named. */
	check(HI5622_BEACON_STEP_VAP_ADD < HI5622_BEACON_STEP_DESC_TABLE &&
		      HI5622_BEACON_STEP_DESC_TABLE < HI5622_BEACON_STEP_CHAN &&
		      HI5622_BEACON_STEP_CHAN < HI5622_BEACON_STEP_SSID &&
		      HI5622_BEACON_STEP_SSID < HI5622_BEACON_STEP_AP_CFG &&
		      HI5622_BEACON_STEP_AP_CFG < HI5622_BEACON_STEP_VAP_START,
	      "step enum values follow the wire order");
	check(strcmp(hi5622_beacon_step_name(HI5622_BEACON_STEP_DESC_TABLE),
		     "desc_table") == 0, "step name desc_table");
	check(strcmp(hi5622_beacon_step_name(HI5622_BEACON_STEP_CHAN),
		     "ap_chan") == 0, "step name ap_chan");
	check(strcmp(hi5622_beacon_step_name(0xffu), "none") == 0,
	      "unknown step name");

	printf("== ordering: %d checks, %d failures\n", checks, fails);
	if (fails) {
		printf("FAIL\n");
		return 1;
	}
	printf("PASS\n");
	return 0;
}
