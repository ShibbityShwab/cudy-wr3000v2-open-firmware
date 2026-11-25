/* SPDX-License-Identifier: GPL-2.0 */
/*
 * chan_gate_selftest.c - host selftest for the decoded firmware channel
 * semantics (lane wifi-fw-chan-worker, task-180).
 *
 * HOST-ONLY, pure C99 with no kernel/header dependency beyond the two
 * committed sources in this directory.  Build+run with
 *   ../../tools/run-host-tests.sh
 * or directly:
 *   cc -std=c99 -Wall -Wextra -Werror -o /tmp/chan_gate_selftest \
 *      chan_gate_selftest.c hi5622_chan_gate.c && /tmp/chan_gate_selftest
 *
 * Every expectation is a decoded firmware fact from ../REPORT.md /
 * ../TABLES.md (firmware tables CA 0x117B48 / 0x117AD4, worker CA 0x841B8,
 * store map ctx+0x10..+0x13, gates CA 0x40684 / 0x944DE).
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_chan_gate.h"

static int checks;
static int failures;

static void ck(const char *name, int cond)
{
	checks++;
	if (!cond) {
		failures++;
		printf("FAIL  %s\n", name);
	} else {
		printf("PASS  %s\n", name);
	}
}

static void ck_word(const char *name, unsigned int band, unsigned int channel,
		    unsigned int field, u32 want)
{
	u32 got = 0;

	ck(name, hi5622_chan_gate_expected_word(band, channel, field, &got) &&
	       got == want);
	if (got != want && hi5622_chan_gate_expected_word(band, channel, field,
							  &got))
		printf("      got 0x%08x want 0x%08x\n",
		       (unsigned)got, (unsigned)want);
}

int main(void)
{
	u32 word = 0;

	/* ---- the firmware per-band channel tables ------------------------ */
	ck("index band0 ch1 = 0", hi5622_chan_gate_index(0, 1) == 0);
	ck("index band0 ch6 = 5", hi5622_chan_gate_index(0, 6) == 5);
	ck("index band0 ch14 = 13", hi5622_chan_gate_index(0, 14) == 13);
	ck("index band1 ch36 = 0", hi5622_chan_gate_index(1, 36) == 0);
	ck("index band1 ch40 = 1", hi5622_chan_gate_index(1, 40) == 1);
	ck("index band1 ch44 = 2", hi5622_chan_gate_index(1, 44) == 2);
	ck("index band1 ch64 = 7", hi5622_chan_gate_index(1, 64) == 7);
	ck("index band1 ch100 = 8", hi5622_chan_gate_index(1, 100) == 8);
	ck("index band1 ch149 = 20", hi5622_chan_gate_index(1, 149) == 20);
	ck("index band1 ch165 = 24", hi5622_chan_gate_index(1, 165) == 24);
	ck("index band1 ch196 = 28", hi5622_chan_gate_index(1, 196) == 28);
	ck("index rejects band1 ch37", hi5622_chan_gate_index(1, 37) < 0);
	ck("index rejects band1 ch2", hi5622_chan_gate_index(1, 2) < 0);
	ck("index rejects band0 ch15", hi5622_chan_gate_index(0, 15) < 0);
	ck("index rejects band2", hi5622_chan_gate_index(2, 36) < 0);

	/* ---- the exact stored word (the G6b proof) ----------------------- */
	ck_word("word band1 ch36 = 0x00000124", 1, 36, 0, 0x00000124u);
	ck_word("word band1 ch40 = 0x01000128", 1, 40, 0, 0x01000128u);
	ck_word("word band1 ch196 = 0x1c0001c4", 1, 196, 0, 0x1c0001c4u);
	ck_word("word band0 ch1 = 0x00000001", 0, 1, 0, 0x00000001u);
	ck_word("word band0 ch6 = 0x05000006", 0, 6, 0, 0x05000006u);
	ck_word("word band0 ch14 = 0x0d00000e", 0, 14, 0, 0x0d00000eu);
	ck_word("word field=3 band1 ch36 = 0x00030124", 1, 36, 3, 0x00030124u);
	ck("expected_word refuses an invalid channel",
	   !hi5622_chan_gate_expected_word(1, 37, 0, &word));
	ck("expected_word refuses a NULL out",
	   !hi5622_chan_gate_expected_word(1, 36, 0, NULL));

	/* ---- corrected readiness vs the old "index == 1" gate ------------ */
	ck("ready accepts the real ch36 store 0x00000124",
	   hi5622_chan_gate_word_ready(0x00000124u, 1, 36));
	ck("ready accepts the real ch40 store 0x01000128",
	   hi5622_chan_gate_word_ready(0x01000128u, 1, 40));
	ck("ready rejects the pre-store zero",
	   !hi5622_chan_gate_word_ready(0x00000000u, 1, 36));
	ck("ready rejects the previous channel's word",
	   !hi5622_chan_gate_word_ready(0x01000128u, 1, 36));
	ck("ready rejects an index mismatch",
	   !hi5622_chan_gate_word_ready(0x05000124u, 1, 36));
	ck("ready rejects a band mismatch",
	   !hi5622_chan_gate_word_ready(0x00000124u, 0, 36));
	ck("OLD gate (byte+0x13==1) would REFUSE ch36",
	   ((0x00000124u >> 24) & 0xffu) != 1u);
	ck("OLD gate (byte+0x13==1) would PASS ch40",
	   ((0x01000128u >> 24) & 0xffu) == 1u);
	ck("OLD gate (byte+0x13==1) would REFUSE ch196",
	   ((0x1c0001c4u >> 24) & 0xffu) != 1u);

	/* ---- the silent 5 GHz RF-programme gate -------------------------- */
	ck("rf_ran true for type2 + both gates",
	   hi5622_chan_gate_rf_ran(2u, 1u, 1u));
	ck("rf_ran false for a non-AP create type",
	   !hi5622_chan_gate_rf_ran(3u, 1u, 1u));
	ck("rf_ran false when module+0x440 is not 1",
	   !hi5622_chan_gate_rf_ran(2u, 0u, 1u));
	ck("rf_ran false when bandobj+0xEC8 is not 1",
	   !hi5622_chan_gate_rf_ran(2u, 1u, 0u));
	ck("rf_ran false for the all-zero boot state",
	   !hi5622_chan_gate_rf_ran(0u, 0u, 0u));

	printf("\nchan_gate_selftest: %d checks, %d failures\n",
	       checks, failures);
	return failures ? 1 : 0;
}
