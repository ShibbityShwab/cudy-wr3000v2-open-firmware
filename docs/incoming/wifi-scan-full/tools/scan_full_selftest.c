/* SPDX-License-Identifier: GPL-2.0 */
/* scan_full_selftest.c - wifi-scan-full (task-138) device-free known-answer
 * test for the refined g0 id10 builder.
 *
 * Pins the vendor-shaped wire form (44 B = 12 hdr + 32 B payload), the
 * firmware field map from handler CA 0x5FF64 (subtype 1 = slot config,
 * subtype 2 = clear/remove with its flag at payload+0x14), the R4-compatible
 * parameter surface, and the fail-closed validation. */
#include <stdio.h>
#include <string.h>
#include "hi5622_band_cfg.h"

static int checks, fails;
#define CK(c, m) do { checks++; if (!(c)) { fails++; printf("FAIL %s\n", (m)); } } while (0)

/* payload bytes the firmware actually reads, per subtype */
static const int s1_reads[] = { 0x00, 0x01, 0x02, 0x03, 0x07, 0x09,
				0x0c, 0x0d, 0x10, 0x11, 0x12, 0x14, 0x15 };
static const int s2_reads[] = { 0x00, 0x01, 0x02, 0x03, 0x07, 0x14 };

static int in_set(int off, const int *set, int n)
{
	int i;

	for (i = 0; i < n; i++)
		if (set[i] == off)
			return 1;
	return 0;
}

int main(void)
{
	struct hi5622_band_cfg_params p;
	u8 f[HI5622_BAND_CFG_TOTAL];
	size_t len = 0;
	int i, rc;

	/* ---- 1. constants and vendor shape ---- */
	CK(HI5622_BAND_CFG_ID == 0x0a, "message id is g0 id 10");
	CK(HI5622_BAND_CFG_PAYLOAD_LEN == 0x20, "payload is 32 B (vendor shape)");
	CK(HI5622_BAND_CFG_TOTAL == 0x2c, "wire total is 44 B = 12 + 32");
	CK(HI5622_BAND_CFG_TOTAL == 12 + HI5622_BAND_CFG_PAYLOAD_LEN,
	   "header/payload split adds up");

	/* ---- 2. subtype 1 frame (the R10b baseline values) ---- */
	memset(&p, 0, sizeof(p));
	p.subtype = HI5622_BAND_CFG_SUB_SLOT;
	p.chan_id = 1;
	p.start = 12;
	p.field10 = 13;
	p.mode = 2;
	p.cmp = 0;
	rc = hi5622_band_cfg_build(f, sizeof(f), &len, &p);
	CK(rc == 0, "subtype1 builds");
	CK(len == 44, "subtype1 wire len 44");
	CK(f[0] == 0x00 && f[1] == 0x01, "hdr group 0 / alloc nibble 1");
	CK(f[2] == 0x00 && f[3] == 0x00, "hdr vap 0 / chip 0");
	CK(f[4] == 0x2c && f[5] == 0x00, "hdr total LE = 44");
	CK(f[6] == 0x0a && f[7] == 0x00, "hdr id = 10");
	CK(f[8] == 0x00 && f[9] == 0x00, "hdr[8]/hdr[9] zero");
	CK(f[10] == 0x5a && f[11] == 0x5a, "hdr tag 0x5A5A");
	CK(f[12 + 0x00] == 3, "payload +0x00 type = 3");
	CK(f[12 + 0x01] == 1, "payload +0x01 subtype = 1");
	CK(f[12 + 0x02] == 1 && f[12 + 0x03] == 0, "payload +0x02 key u16");
	CK(f[12 + 0x07] == 0, "payload +0x07 slot = 0");
	CK(f[12 + 0x09] == 0, "payload +0x09 action = 0 (configure path)");
	CK(f[12 + 0x0c] == 12 && f[12 + 0x0d] == 0, "payload +0x0C start u16 = 12");
	CK(f[12 + 0x10] == 13 && f[12 + 0x11] == 0, "payload +0x10 field10 u16 = 13");
	CK(f[12 + 0x12] == 2, "payload +0x12 mode = 2");
	CK(f[12 + 0x14] == 0 && f[12 + 0x15] == 0, "payload +0x14 cmp u16 = 0");
	for (i = 0; i < (int)HI5622_BAND_CFG_PAYLOAD_LEN; i++)
		if (!in_set(i, s1_reads, (int)(sizeof(s1_reads) / sizeof(s1_reads[0]))))
			CK(f[12 + i] == 0, "unused subtype1 payload byte is zero");
	/* payload bytes 0x16..0x1F exist only with the vendor shape */
	CK(len == 12 + 0x20, "vendor 32-byte payload present (bytes 0x16..0x1F)");

	/* ---- 3. subtype 2 (clear/remove) flag is payload+0x14 ---- */
	memset(&p, 0, sizeof(p));
	p.subtype = HI5622_BAND_CFG_SUB_CLEAR;
	p.chan_id = 6;
	p.slot = 3;
	p.sub2_flag = 0;
	rc = hi5622_band_cfg_build(f, sizeof(f), &len, &p);
	CK(rc == 0 && len == 44, "subtype2 builds 44 B");
	CK(f[12 + 0x01] == 2, "payload +0x01 subtype = 2");
	CK(f[12 + 0x02] == 6 && f[12 + 0x03] == 0, "subtype2 key u16");
	CK(f[12 + 0x07] == 3, "subtype2 slot = 3");
	CK(f[12 + 0x14] == 0, "subtype2 clear flag at payload+0x14 = 0");
	CK(f[12 + 0x08] == 0, "subtype2 payload+0x08 untouched (not the flag)");
	for (i = 0; i < (int)HI5622_BAND_CFG_PAYLOAD_LEN; i++)
		if (!in_set(i, s2_reads, (int)(sizeof(s2_reads) / sizeof(s2_reads[0]))))
			CK(f[12 + i] == 0, "unused subtype2 payload byte is zero");
	p.sub2_flag = 1;
	rc = hi5622_band_cfg_build(f, sizeof(f), &len, &p);
	CK(rc == 0 && f[12 + 0x14] == 1, "subtype2 arm flag = 1 at +0x14");
	CK(f[12 + 0x09] == 0, "subtype2 does not use payload+0x09");
	CK(f[12 + 0x0c] == 0 && f[12 + 0x10] == 0 && f[12 + 0x12] == 0,
	   "subtype2 leaves start/field10/mode zero");

	/* ---- 4. subtype 1 action / wide values ---- */
	memset(&p, 0, sizeof(p));
	p.subtype = HI5622_BAND_CFG_SUB_SLOT;
	p.action = 1;
	p.start = 0xabcd;
	p.field10 = 0xffff;
	p.mode = 0x7f;
	p.cmp = 0xbeef;
	rc = hi5622_band_cfg_build(f, sizeof(f), &len, &p);
	CK(rc == 0, "wide subtype1 values build");
	CK(f[12 + 0x09] == 1, "action -> payload+0x09 (firmware clear path)");
	CK(f[12 + 0x0c] == 0xcd && f[12 + 0x0d] == 0xab, "start LE bytes");
	CK(f[12 + 0x10] == 0xff && f[12 + 0x11] == 0xff, "field10 LE bytes");
	CK(f[12 + 0x12] == 0x7f, "mode byte");
	CK(f[12 + 0x14] == 0xef && f[12 + 0x15] == 0xbe, "cmp LE bytes");

	/* ---- 5. fail-closed validation ---- */
	memset(&p, 0, sizeof(p));
	p.subtype = HI5622_BAND_CFG_SUB_SLOT;
	p.slot = 8;
	CK(hi5622_band_cfg_build(f, sizeof(f), &len, &p) == -EINVAL,
	   "slot 8 refused (firmware limit 0..7)");
	p.slot = 0;
	p.subtype = 0;
	CK(hi5622_band_cfg_build(f, sizeof(f), &len, &p) == -EINVAL,
	   "subtype 0 refused");
	p.subtype = 3;
	CK(hi5622_band_cfg_build(f, sizeof(f), &len, &p) == -EINVAL,
	   "subtype 3 refused");
	p.subtype = HI5622_BAND_CFG_SUB_SLOT;
	CK(hi5622_band_cfg_build(f, 43, &len, &p) == -EINVAL,
	   "cap 43 refused (vendor frame needs 44)");
	CK(hi5622_band_cfg_build(f, 44, &len, &p) == 0, "cap 44 accepted");
	CK(hi5622_band_cfg_build(NULL, 44, &len, &p) == -EINVAL, "NULL out refused");
	CK(hi5622_band_cfg_build(f, 44, NULL, NULL) == -EINVAL, "NULL params refused");
	/* len is optional */
	memset(&p, 0, sizeof(p));
	p.subtype = HI5622_BAND_CFG_SUB_CLEAR;
	CK(hi5622_band_cfg_build(f, sizeof(f), NULL, &p) == 0, "NULL len accepted");

	printf("scan_full_selftest: %d/%d checks %s\n",
	       checks - fails, checks, fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}