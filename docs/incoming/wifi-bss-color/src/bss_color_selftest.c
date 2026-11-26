// SPDX-License-Identifier: GPL-2.0
/*
 * bss_color_selftest.c - byte-exact host test for the g0 id36
 * hmac_scan_config_bss_color builder and the AP-colour policy (lane
 * wifi-bss-color, task-233). No device, no kernel: pure C99.
 *
 * KAT sources: TABLES.md T3 (K1 = the captured stock frame; K2-K4 = the
 * derived AP-colour vectors).
 */
#include <stdio.h>
#include <string.h>
#include "hi5622_bss_color.h"

static unsigned int checks;
static unsigned int failed;

static void check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		failed++;
		printf("FAIL %s\n", what);
	}
}

static void check_build(const char *name, u8 fa, u8 fb, u8 bid_hi, u64 mask,
			const u8 *expect, size_t expect_len)
{
	u8 wire[HI5622_BSS_COLOR_TOTAL_LEN];
	size_t len = 0;
	int rc = hi5622_bss_color_build(wire, sizeof(wire), fa, fb, bid_hi,
					mask, &len);

	checks++;
	if (rc || len != expect_len) {
		failed++;
		printf("FAIL %s: build rc=%d len=%zu\n", name, rc, len);
		return;
	}
	if (memcmp(wire, expect, expect_len) != 0) {
		size_t i;

		failed++;
		printf("FAIL %s: bytes differ\n  got   ", name);
		for (i = 0; i < len; i++)
			printf("%02x ", wire[i]);
		printf("\n  want  ");
		for (i = 0; i < expect_len; i++)
			printf("%02x ", expect[i]);
		printf("\n");
		return;
	}
	printf("PASS %s (%zu B byte-exact)\n", name, len);
}

int main(void)
{
	/* K1: the captured stock frame, mask 0x0400000002010009
	 * -> colours {0,3,16,25,58}. */
	static const u8 k1[20] = {
		0x00, 0x01, 0x00, 0x07, 0x14, 0x00, 0x24, 0x00,
		0x00, 0x00, 0x5a, 0x5a,
		0x09, 0x00, 0x01, 0x02, 0x00, 0x00, 0x00, 0x04
	};
	/* K2: our AP-colour posting for hostapd he_bss_color=25, chip 0. */
	static const u8 k2[20] = {
		0x00, 0x01, 0x00, 0x00, 0x14, 0x00, 0x24, 0x00,
		0x00, 0x00, 0x5a, 0x5a,
		0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
	};
	/* K3: colour 1, chip 4 -> mask 1<<1. */
	static const u8 k3[20] = {
		0x00, 0x01, 0x00, 0x04, 0x14, 0x00, 0x24, 0x00,
		0x00, 0x00, 0x5a, 0x5a,
		0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	/* K4: colour 63, chip 15 -> mask 1<<63. */
	static const u8 k4[20] = {
		0x00, 0x01, 0x00, 0x0f, 0x14, 0x00, 0x24, 0x00,
		0x00, 0x00, 0x5a, 0x5a,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80
	};
	u8 wire[HI5622_BSS_COLOR_TOTAL_LEN];
	u64 mask = 0;
	size_t len = 0;
	int rc;

	printf("hi5622_bss_color selftest (lane wifi-bss-color, task-233)\n");

	/* --- KATs (TABLES T3) --- */
	check_build("K1 captured stock frame", 0x00, 0x07, 0x00,
		    0x0400000002010009ull, k1, sizeof(k1));
	check_build("K2 he_bss_color=25 / chip 0", 0x00, 0x00, 0x00,
		    (u64)1u << 25, k2, sizeof(k2));
	check_build("K3 colour 1 / chip 4", 0x00, 0x04, 0x00,
		    (u64)1u << 1, k3, sizeof(k3));
	check_build("K4 colour 63 / chip 15", 0x00, 0x0f, 0x00,
		    (u64)1u << 63, k4, sizeof(k4));
	/* bid_hi is header byte 1's high nibble; fa is byte 2. */
	rc = hi5622_bss_color_build(wire, sizeof(wire), 0xab, 0x07, 0x0f,
				    0x0400000002010009ull, &len);
	check(rc == 0 && len == 20 &&
	      wire[HI5622_BSS_COLOR_OFF_BID] == 0xf1 &&
	      wire[HI5622_BSS_COLOR_OFF_FA] == 0xab,
	      "K1 variant: bid_hi/fa bytes land at offsets 1/2");

	/* --- validation of the built frames --- */
	rc = hi5622_bss_color_build(wire, sizeof(wire), 0, 0, 0,
				    (u64)1u << 25, &len);
	check(rc == 0 && len == 20, "build K2 for validation");
	rc = hi5622_bss_color_validate(wire, len, &mask);
	check(rc == 0 && mask == ((u64)1u << 25), "validate K2 mask");
	wire[HI5622_BSS_COLOR_OFF_ID] = 0x25;
	check(hi5622_bss_color_validate(wire, len, NULL) == -EINVAL,
	      "reject wrong id");
	wire[HI5622_BSS_COLOR_OFF_ID] = 0x24;
	wire[HI5622_BSS_COLOR_OFF_LEN] = 0x15;
	check(hi5622_bss_color_validate(wire, len, NULL) == -EINVAL,
	      "reject wrong total length");
	wire[HI5622_BSS_COLOR_OFF_LEN] = 0x14;
	wire[HI5622_BSS_COLOR_OFF_TAG] = 0x00;
	check(hi5622_bss_color_validate(wire, len, NULL) == -EINVAL,
	      "reject wrong tag");
	wire[HI5622_BSS_COLOR_OFF_TAG] = 0x5a;
	check(hi5622_bss_color_validate(wire, len - 1, NULL) == -EINVAL,
	      "reject truncated frame");
	check(hi5622_bss_color_validate(wire, len, NULL) == 0,
	      "accept repaired frame");
	check(hi5622_bss_color_build(wire, 19, 0, 0, 0, 1, &len) == -EINVAL,
	      "reject too-small buffer");
	check(hi5622_bss_color_build(wire, sizeof(wire), 0, 0, 0x10, 1, &len)
	      == -EINVAL, "reject bid_hi > 0xf");

	/* --- AP-colour -> bitmap --- */
	check(hi5622_bss_color_mask_from_color(0, &mask) == -EINVAL,
	      "colour 0 refused (cfg80211 policy: 1..63)");
	check(hi5622_bss_color_mask_from_color(64, &mask) == -EINVAL,
	      "colour 64 refused");
	check(hi5622_bss_color_mask_from_color(1, &mask) == 0 &&
	      mask == 2ull, "colour 1 -> bit 1");
	check(hi5622_bss_color_mask_from_color(25, &mask) == 0 &&
	      mask == 0x02000000ull, "colour 25 -> bit 25");
	check(hi5622_bss_color_mask_from_color(63, &mask) == 0 &&
	      mask == 0x8000000000000000ull, "colour 63 -> bit 63");
	/* the captured bitmap decodes to the exact colour set of TABLES T2 */
	mask = 0x0400000002010009ull;
	check(((mask >> 0) & 1u) == 1u, "capture colour 0 bit");
	check(((mask >> 3) & 1u) == 1u, "capture colour 3 bit");
	check(((mask >> 16) & 1u) == 1u, "capture colour 16 bit");
	check(((mask >> 25) & 1u) == 1u, "capture colour 25 bit");
	check(((mask >> 58) & 1u) == 1u, "capture colour 58 bit");
	check(__builtin_popcountll(mask) == 5, "capture bitmap has 5 colours");

	/* --- module-parameter policy --- */
	check(hi5622_bss_color_policy(0) == 0, "policy 0 = off");
	check(hi5622_bss_color_policy(1) == 1, "policy 1 = on");
	check(hi5622_bss_color_policy(2) == -EINVAL, "policy 2 refused");
	check(hi5622_bss_color_policy(100) == -EINVAL, "policy 100 refused");

	printf("%u checks, %u failed -> %s\n", checks, failed,
	       failed ? "FAIL" : "PASS");
	return failed ? 1 : 0;
}
