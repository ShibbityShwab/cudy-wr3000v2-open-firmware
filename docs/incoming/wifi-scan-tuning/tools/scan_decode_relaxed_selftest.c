/* SPDX-License-Identifier: GPL-2.0 */
/* scan_decode_relaxed_selftest.c - wifi-scan-tuning lane (task-133 R2/R3).
 * Device-free tests for the g1 id15 layout handling added after the bench:
 *  1. the R2 case: valid beacon inside a payload with a 4-byte unknown prefix
 *     and no trailer -> relaxed probe (layout 0x80) decodes it;
 *  2. the R3 case: bench-derived framing [72-byte vendor header][beacon
 *     (length = u16 at payload[0])][20-byte trailer] -> the derived-offset
 *     layout (LEN_PREFIX) decodes it and the frame offset is len-20-flen;
 *  3. garbage is still rejected (fail-closed). */
#include <stdio.h>
#include <string.h>
#include "hi5622_scan_d2h.h"

static int checks, fails;
#define CK(c, m) do { checks++; if (!(c)) { fails++; printf("FAIL %s\n", (m)); } } while (0)

static u32 put_beacon(u8 *p, u32 n, u8 ds_chan)
{
	u32 i;

	p[n++] = 0x80; p[n++] = 0x00;		/* fc: beacon */
	p[n++] = 0; p[n++] = 0;			/* duration */
	for (i = 0; i < 6; i++) p[n++] = 0x02;
	for (i = 0; i < 6; i++) p[n++] = 0x10 + (u8)i;
	for (i = 0; i < 6; i++) p[n++] = 0x20 + (u8)i;
	p[n++] = 0; p[n++] = 0;			/* seq */
	for (i = 0; i < 8; i++) p[n++] = 0;	/* tsf */
	p[n++] = 0x64; p[n++] = 0x00;		/* beacon interval */
	p[n++] = 0x11; p[n++] = 0x00;		/* capab: ESS + privacy */
	p[n++] = 0; p[n++] = 4; p[n++] = 'a'; p[n++] = 'u'; p[n++] = 't'; p[n++] = 'o';
	p[n++] = 3; p[n++] = 1; p[n++] = ds_chan;
	p[n++] = 45; p[n++] = 26;
	for (i = 0; i < 26; i++) p[n++] = (u8)i;
	return n;
}

int main(void)
{
	u8 p[240];
	u32 n, flen, i;
	struct hi5622_scan_d2h_bss bss;
	int rc;

	/* --- R2: 4-byte prefix, no trailer, beacon only --- */
	memset(p, 0, sizeof(p));
	p[0] = 0xde; p[1] = 0xad; p[2] = 0xbe; p[3] = 0xef;
	n = put_beacon(p, 4, 6);
	rc = hi5622_scan_d2h_parse_bss(p, n, NULL, &bss);
	CK(rc == 0, "relaxed probe decodes prefixed beacon");
	CK(bss.layout == HI5622_SCAN_D2H_LAYOUT_RELAXED, "R2 layout reported as relaxed");
	CK(bss.freq == 2437u, "R2 DS channel 6 -> 2437 MHz");
	CK(bss.ssid_len == 4 && memcmp(bss.ssid, "auto", 4) == 0, "R2 SSID decoded");
	CK(bss.cap_flags & HI5622_SCAN_D2H_CAP_ESS, "R2 ESS capability");
	CK(bss.cap_flags & HI5622_SCAN_D2H_CAP_PRIVACY, "R2 privacy capability");

	/* --- R3: bench framing [72-byte header][beacon][20-byte trailer] --- */
	memset(p, 0x5a, sizeof(p));
	n = 72u + 4u;				/* header + beacon start marker */
	n = put_beacon(p, 72u, 6);
	flen = n - 72u;				/* u16 at payload[0] = frame length */
	p[0] = (u8)(flen & 0xffu);
	p[1] = (u8)(flen >> 8);
	for (i = n; i < n + 20u; i++)		/* 20-byte trailer */
		p[i] = (u8)(i & 0xffu);
	n += 20u;
	rc = hi5622_scan_d2h_parse_bss(p, n, NULL, &bss);
	CK(rc == 0, "R3 bench framing decodes");
	CK(bss.layout == HI5622_SCAN_D2H_LAYOUT_LEN_PREFIX, "R3 length-prefix layout reported");
	CK(bss.freq == 2437u, "R3 channel 2437 MHz");
	CK(bss.ssid_len == 4 && memcmp(bss.ssid, "auto", 4) == 0, "R3 SSID decoded");
	CK(bss.trailer_present == 1u, "R3 20-byte trailer recorded");

	/* --- garbage still rejected --- */
	memset(p, 0xAA, sizeof(p));
	rc = hi5622_scan_d2h_parse_bss(p, 64, NULL, &bss);
	CK(rc == -EOPNOTSUPP, "garbage payload still rejected");

	printf("scan_decode_relaxed_selftest: %d/%d checks %s\n",
	       checks - fails, checks, fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}

