/* SPDX-License-Identifier: GPL-2.0 */
/*
 * monitor_selftest.c - host-only assertions for the portable local-mirror
 * monitor logic (monitor2 lane, task-246).  No kernel tree, no device.
 * Build: cc -std=c99 -Wall -Wextra -Werror -I src tools/monitor_selftest.c \
 *            src/hi5622_monitor_logic.c -o /tmp/monitor_selftest
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_monitor_logic.h"

static int failures;
static int checks;

#define CHECK(cond, fmt, ...) do {					\
	checks++;							\
	if (!(cond)) {							\
		failures++;						\
		printf("FAIL: " fmt "\n", ##__VA_ARGS__);		\
	}								\
} while (0)

static void check_bytes(const char *name, const unsigned char *got,
			const unsigned char *want, size_t n)
{
	if (memcmp(got, want, n) != 0) {
		size_t i;

		failures++;
		printf("FAIL: %s vector mismatch:", name);
		for (i = 0; i < n; i++)
			printf(" %02x", got[i]);
		printf("\n");
		return;
	}
	checks++;
}

int main(void)
{
	unsigned char rt[32];
	unsigned char r0[8] = { 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00 };
	unsigned char r1[12] = { 0x00, 0x00, 0x0c, 0x00, 0x08, 0x00, 0x00, 0x00,
				 0x6c, 0x09, 0x00, 0x00 };
	unsigned char r2[13] = { 0x00, 0x00, 0x0d, 0x00, 0x08, 0x08, 0x00, 0x00,
				 0x85, 0x09, 0x00, 0x00, 0xc2 };
	uint16_t f;
	size_t n;

	/* policy: only 0 and 1 are ever accepted */
	CHECK(himon_mode_ok(0) == 1, "mode 0 must be allowed");
	CHECK(himon_mode_ok(1) == 1, "mode 1 must be allowed");
	CHECK(himon_mode_ok(2) == 0, "mode 2 must be refused");
	CHECK(himon_mode_ok(0xffffffffu) == 0, "mode UINT_MAX must be refused");
	CHECK(strcmp(himon_mode_name(0), "off") == 0, "mode name 0");
	CHECK(strcmp(himon_mode_name(1), "local-mirror") == 0, "mode name 1");
	CHECK(strcmp(himon_mode_name(7), "invalid") == 0, "mode name 7");

	/* 2.4 GHz channel plan */
	CHECK(himon_chan_to_freq(0, 1, &f) == 0 && f == 2412, "ch1 = 2412 (got %u)", f);
	CHECK(himon_chan_to_freq(0, 6, &f) == 0 && f == 2437, "ch6 = 2437 (got %u)", f);
	CHECK(himon_chan_to_freq(0, 13, &f) == 0 && f == 2472, "ch13 = 2472 (got %u)", f);
	CHECK(himon_chan_to_freq(0, 14, &f) == 0 && f == 2484, "ch14 = 2484 (got %u)", f);
	CHECK(himon_chan_to_freq(0, 0, &f) == -1, "ch0 must be refused");
	CHECK(himon_chan_to_freq(0, 15, &f) == -1, "ch15 must be refused");
	CHECK(himon_chan_to_freq(0, 6, NULL) == -1, "NULL freq must be refused");

	/* 5 GHz channel plan (metadata map only; no regulatory decision) */
	CHECK(himon_chan_to_freq(1, 36, &f) == 0 && f == 5180, "ch36 = 5180 (got %u)", f);
	CHECK(himon_chan_to_freq(1, 100, &f) == 0 && f == 5500, "ch100 = 5500 (got %u)", f);
	CHECK(himon_chan_to_freq(1, 140, &f) == 0 && f == 5700, "ch140 = 5700 (got %u)", f);
	CHECK(himon_chan_to_freq(1, 165, &f) == 0 && f == 5825, "ch165 = 5825 (got %u)", f);
	CHECK(himon_chan_to_freq(1, 35, &f) == -1, "ch35 must be refused");
	CHECK(himon_chan_to_freq(1, 178, &f) == -1, "ch178 must be refused");
	CHECK(himon_chan_to_freq(2, 36, &f) == -1, "band 2 must be refused");

	/* radiotap pinned vectors */
	n = himon_rt_build(rt, sizeof(rt), 0, 0, 0, 0);
	CHECK(n == 8, "R0 length 8 (got %zu)", n);
	check_bytes("R0", rt, r0, 8);

	memset(rt, 0xaa, sizeof(rt));
	n = himon_rt_build(rt, sizeof(rt), 2412, 1, 0, 0);
	CHECK(n == 12, "R1 length 12 (got %zu)", n);
	check_bytes("R1", rt, r1, 12);

	memset(rt, 0xaa, sizeof(rt));
	n = himon_rt_build(rt, sizeof(rt), 2437, 1, -62, 1);
	CHECK(n == 13, "R2 length 13 (got %zu)", n);
	check_bytes("R2", rt, r2, 13);

	/* rssi without a channel degrades to R0, never a malformed header */
	memset(rt, 0xaa, sizeof(rt));
	n = himon_rt_build(rt, sizeof(rt), 0, 0, -62, 1);
	CHECK(n == 8, "rssi-only degrades to R0 (got %zu)", n);
	check_bytes("R0-degrade", rt, r0, 8);

	/* refusals: NULL buffer / short capacity / zero capacity */
	CHECK(himon_rt_build(NULL, 16, 2412, 1, 0, 0) == 0, "NULL buf refused");
	CHECK(himon_rt_build(rt, 7, 0, 0, 0, 0) == 0, "cap 7 refused for R0");
	CHECK(himon_rt_build(rt, 11, 2412, 1, 0, 0) == 0, "cap 11 refused for R1");
	CHECK(himon_rt_build(rt, 12, 2437, 1, -62, 1) == 0, "cap 12 refused for R2");
	CHECK(himon_rt_build(rt, 0, 0, 0, 0, 0) == 0, "cap 0 refused");

	printf("monitor_selftest: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
