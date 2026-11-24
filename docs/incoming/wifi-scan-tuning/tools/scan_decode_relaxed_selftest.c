/* SPDX-License-Identifier: GPL-2.0 */
/* scan_decode_relaxed_selftest.c - wifi-scan-tuning lane (task-133 R2).
 * Device-free test for the relaxed g1 id15 layout probe added after the bench
 * showed parse_errors=6 with all four fixed layouts failing.  Builds a valid
 * 802.11 beacon inside a payload with a 4-byte unknown prefix and NO 20-byte
 * trailer, then requires hi5622_scan_d2h_parse_bss() to decode it with
 * layout == HI5622_SCAN_D2H_LAYOUT_RELAXED (and the DS channel/SSID correct).
 * Also asserts a malformed payload is still rejected (fail-closed). */
#include <stdio.h>
#include <string.h>
#include "hi5622_scan_d2h.h"

static int checks, fails;
#define CK(c, m) do { checks++; if (!(c)) { fails++; printf("FAIL %s\n", (m)); } } while (0)

int main(void)
{
	u8 p[160];
	u32 n = 0, i;
	struct hi5622_scan_d2h_bss bss;
	int rc;

	memset(p, 0, sizeof(p));
	/* 4-byte unknown prefix: the alignment the fixed candidates do not cover */
	p[n++] = 0xde; p[n++] = 0xad; p[n++] = 0xbe; p[n++] = 0xef;
	/* 802.11 beacon: fc = 0x0080 (version 0, type mgmt, subtype 8) */
	p[n++] = 0x80; p[n++] = 0x00;
	p[n++] = 0; p[n++] = 0;			/* duration */
	for (i = 0; i < 6; i++) p[n++] = 0x02;	/* addr1 */
	for (i = 0; i < 6; i++) p[n++] = 0x10 + i;	/* addr2 */
	for (i = 0; i < 6; i++) p[n++] = 0x20 + i;	/* addr3 (BSSID) */
	p[n++] = 0; p[n++] = 0;			/* seq */
	for (i = 0; i < 8; i++) p[n++] = 0;	/* tsf */
	p[n++] = 0x64; p[n++] = 0x00;		/* beacon interval 100 */
	p[n++] = 0x11; p[n++] = 0x00;		/* capab: ESS + privacy */
	/* IEs: SSID "auto", DS channel 6, HT cap (26 B) */
	p[n++] = 0; p[n++] = 4; p[n++] = 'a'; p[n++] = 'u'; p[n++] = 't'; p[n++] = 'o';
	p[n++] = 3; p[n++] = 1; p[n++] = 6;
	p[n++] = 45; p[n++] = 26;
	for (i = 0; i < 26; i++) p[n++] = (u8)i;

	rc = hi5622_scan_d2h_parse_bss(p, n, NULL, &bss);
	CK(rc == 0, "relaxed probe decodes prefixed beacon");
	CK(bss.layout == HI5622_SCAN_D2H_LAYOUT_RELAXED, "layout reported as relaxed");
	CK(bss.freq == 2437u, "DS channel 6 -> 2437 MHz");
	CK(bss.ssid_len == 4 && memcmp(bss.ssid, "auto", 4) == 0, "SSID decoded");
	CK(bss.cap_flags & HI5622_SCAN_D2H_CAP_ESS, "ESS capability");
	CK(bss.cap_flags & HI5622_SCAN_D2H_CAP_PRIVACY, "privacy capability");

	/* malformed: no valid 802.11 frame at any finite offset */
	memset(p, 0xAA, sizeof(p));
	rc = hi5622_scan_d2h_parse_bss(p, 64, NULL, &bss);
	CK(rc == -EOPNOTSUPP, "garbage payload still rejected");

	printf("scan_decode_relaxed_selftest: %d/%d checks %s\n",
	       checks - fails, checks, fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}

