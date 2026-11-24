/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hardening_selftest.c - host test for the pure AP-start policy gate added by
 * the wifi-stability lane (patches/files/hi5622_stability.h).
 *
 * Build/run: ./tools/run-hardening-check.sh
 * Host-only: no kernel, no device, no vendor code.
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_stability.h"

static int failures;
static int checks;

static void expect_gate(const char *what, bool started, bool dirty,
			bool base, u8 iftype, enum hi5622_stab_ap_gate want)
{
	enum hi5622_stab_ap_gate got =
		hi5622_stab_ap_gate(started, dirty, base, iftype);

	checks++;
	if (got != want) {
		failures++;
		printf("FAIL %-42s got=%d want=%d\n", what, (int)got, (int)want);
	}
}

int main(void)
{
	/* Fresh module load: everything allowed. */
	expect_gate("fresh load", false, false, false, 0, HI5622_STAB_AP_OK);
	expect_gate("fresh load, stale iftype field", false, false, false, 2,
		    HI5622_STAB_AP_OK);

	/* One committed AP start per load (restart needs a power-cycle). */
	expect_gate("already started", true, false, false, 0,
		    HI5622_STAB_AP_ALREADY_STARTED);
	expect_gate("started + base AP", true, false, true, 3,
		    HI5622_STAB_AP_ALREADY_STARTED);
	expect_gate("started wins over dirty", true, true, true, 2,
		    HI5622_STAB_AP_ALREADY_STARTED);

	/* Aborted sequence that already posted frames: never re-post. */
	expect_gate("sequence dirty", false, true, false, 0,
		    HI5622_STAB_AP_SEQ_DIRTY);
	expect_gate("dirty wins over wrong-iftype base", false, true, true, 2,
		    HI5622_STAB_AP_SEQ_DIRTY);

	/* Base already posted by a scan: only the AP iftype may be reused. */
	expect_gate("base posted for AP", false, false, true, 3,
		    HI5622_STAB_AP_OK);
	expect_gate("base posted for managed/station", false, false, true, 2,
		    HI5622_STAB_AP_BASE_WRONG_IFTYPE);
	expect_gate("base posted, unknown iftype 0", false, false, true, 0,
		    HI5622_STAB_AP_BASE_WRONG_IFTYPE);

	/* After a validated VAP delete all latches are cleared (caller resets);
	 * the gate itself must accept the fresh combination again. */
	expect_gate("cleared latches", false, false, false, 0,
		    HI5622_STAB_AP_OK);

	/* Enum values are part of the documented dmesg/refusal contract. */
	checks++;
	if (HI5622_STAB_AP_OK != 0 || HI5622_STAB_AP_ALREADY_STARTED != 1 ||
	    HI5622_STAB_AP_SEQ_DIRTY != 2 ||
	    HI5622_STAB_AP_BASE_WRONG_IFTYPE != 3) {
		failures++;
		printf("FAIL enum values changed\n");
	}
	checks++;
	if (HI5622_STAB_IFTYPE_AP != 3u) {
		failures++;
		printf("FAIL HI5622_STAB_IFTYPE_AP != 3\n");
	}

	printf("hardening_selftest: %d passed, %d failed\n", checks - failures,
	       failures);
	return failures ? 1 : 0;
}
