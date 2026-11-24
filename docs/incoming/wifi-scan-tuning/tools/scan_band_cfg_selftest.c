/* SPDX-License-Identifier: GPL-2.0 */
/* scan_band_cfg_selftest.c - wifi-scan-tuning lane R4 (task-133).
 * Device-free known-answer test for the optional H1 g0 id10 builder:
 * header shape (group 0, id 10, tag 0x5A5A, total 36), payload field map
 * derived from firmware handler CA 0x5FF64, subtype-1/2 forms, and the
 * fail-closed validation (slot > 7, bad subtype, short buffer). */
#include <stdio.h>
#include <string.h>
#include "hi5622_band_cfg.h"

static int checks, fails;
#define CK(c, m) do { checks++; if (!(c)) { fails++; printf("FAIL %s\n", (m)); } } while (0)

int main(void)
{
	struct hi5622_band_cfg_params p;
	u8 f[HI5622_BAND_CFG_TOTAL];
	size_t len = 0;
	int rc;

	memset(&p, 0, sizeof(p));
	p.subtype = HI5622_BAND_CFG_SUB_SLOT;
	p.chan_id = 1;
	p.start = 12;
	p.field10 = 13;
	p.mode = 2;
	rc = hi5622_band_cfg_build(f, sizeof(f), &len, &p);
	CK(rc == 0, "subtype1 builds");
	CK(len == HI5622_BAND_CFG_TOTAL, "wire total is 36 (12 + 0x18)");
	CK(f[0] == 0x00 && f[1] == 0x01 && f[2] == 0x00 && f[3] == 0x00,
	   "header group/alloc/vap/chip");
	CK(f[4] == 0x24 && f[5] == 0x00, "header total little-endian 36");
	CK(f[6] == HI5622_BAND_CFG_ID, "header family byte = g0 id 10");
	CK(f[10] == 0x5a && f[11] == 0x5a, "header tag 0x5A5A");
	CK(f[12 + 0x00] == 3, "payload +0x00 type = 3");
	CK(f[12 + 0x01] == HI5622_BAND_CFG_SUB_SLOT, "payload +0x01 subtype 1");
	CK(f[12 + 0x02] == 1 && f[12 + 0x03] == 0, "payload +0x02 chan id u16");
	CK(f[12 + 0x07] == 0, "payload +0x07 slot");
	CK(f[12 + 0x0c] == 12 && f[12 + 0x0d] == 0, "payload +0x0C start index");
	CK(f[12 + 0x10] == 13 && f[12 + 0x11] == 0, "payload +0x10 count field");
	CK(f[12 + 0x12] == 2, "payload +0x12 mode byte");

	memset(&p, 0, sizeof(p));
	p.subtype = HI5622_BAND_CFG_SUB_ARM;
	p.chan_id = 6;
	rc = hi5622_band_cfg_build(f, sizeof(f), &len, &p);
	CK(rc == 0, "subtype2 builds");
	CK(f[12 + 0x01] == HI5622_BAND_CFG_SUB_ARM, "payload subtype 2");

	memset(&p, 0, sizeof(p));
	p.subtype = HI5622_BAND_CFG_SUB_SLOT;
	p.slot = 8;
	CK(hi5622_band_cfg_build(f, sizeof(f), &len, &p) == -EINVAL,
	   "slot > 7 refused (firmware limit)");
	p.slot = 0;
	p.subtype = 7;
	CK(hi5622_band_cfg_build(f, sizeof(f), &len, &p) == -EINVAL,
	   "unknown subtype refused");
	p.subtype = HI5622_BAND_CFG_SUB_SLOT;
	CK(hi5622_band_cfg_build(f, 10, &len, &p) == -EINVAL,
	   "short buffer refused");

	printf("scan_band_cfg_selftest: %d/%d checks %s\n",
	       checks - fails, checks, fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}

