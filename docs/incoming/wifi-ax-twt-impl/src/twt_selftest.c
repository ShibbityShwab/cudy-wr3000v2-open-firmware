// SPDX-License-Identifier: GPL-2.0
/*
 * twt_selftest.c - host selftest for the portable TWT model
 * (lane wifi-ax-twt-impl, task-199).  No kernel, no device.
 *
 * Covers: the byte-exact 29-byte g0-id1 frame for twt 1 and 3, the field-B
 * echo and range refusal, the field-B derivation from the AP-start module
 * context CA (F2 lesson), the full fail-closed parameter/precondition matrix
 * and the sysfs status line.
 *
 * Build/run: tools/run-host-tests.sh (cc -std=c99 -Wall -Wextra -Werror).
 */
#include "hi5622_ax_twt.h"

#include <stdio.h>
#include <string.h>

static int checks;
static int fails;

static void ck(const char *what, int ok)
{
	checks++;
	if (!ok) {
		fails++;
		printf("FAIL %s\n", what);
	}
}

static int hexbyte(const char *hex)
{
	int hi = hex[0] <= '9' ? hex[0] - '0' : (hex[0] | 0x20) - 'a' + 10;
	int lo = hex[1] <= '9' ? hex[1] - '0' : (hex[1] | 0x20) - 'a' + 10;

	return (hi << 4) | lo;
}

static int hexeq(const u8 *buf, size_t len, const char *hex)
{
	size_t i;

	if (strlen(hex) != len * 2)
		return 0;
	for (i = 0; i < len; i++)
		if (buf[i] != (u8)hexbyte(hex + 2 * i))
			return 0;
	return 1;
}

/* the vendor/user-path plan for the fired 2.4 GHz HE AP shape */
static int good_plan(unsigned int twt, struct hi5622_ax_twt_plan *p,
		     u8 field_b)
{
	int rc = hi5622_ax_twt_precheck(twt, 1u, 1u, 1u, 0u, p);

	if (rc)
		return rc;
	p->field_b = field_b;
	return 0;
}

static void dump_frame(const char *label, const u8 *buf, size_t len)
{
	size_t i;

	printf("FRAME %s (%zu B): ", label, len);
	for (i = 0; i < len; i++)
		printf("%02x", buf[i]);
	printf("\n");
}

int main(int argc, char **argv)
{
	static const char *const frame_twt1 =
		"000100001d00010000005a5a00000000010000005d03010001"
		"ffffffff";
	static const char *const frame_twt3 =
		"000100001d00010000005a5a00000000010000005d03010003"
		"ffffffff";
	struct hi5622_ax_twt_plan p;
	u8 wire[64];
	size_t len;
	u8 fb;
	char line[256];
	int rc;

	/* ---- byte-exact frames [V] (TABLES T12 / FIRE-RECIPE R2) ---- */
	memset(wire, 0xAA, sizeof(wire));
	len = 0;
	ck("plan twt=1", good_plan(1u, &p, 0u) == 0 && p.post && p.flags == 1u &&
	   p.da_expected == 0x40u && p.da_mask == 0xc0u);
	ck("build twt=1", hi5622_ax_twt_build(wire, sizeof(wire), &len, &p) == 0 &&
	   len == HI5622_AX_TWT_WIRE_LEN && len == 29u);
	ck("frame twt=1 byte-exact", hexeq(wire, len, frame_twt1));
	ck("no overrun", wire[29] == 0xAAu);
	ck("plan twt=3", good_plan(3u, &p, 0u) == 0 && p.post && p.flags == 3u &&
	   p.da_expected == 0xc0u);
	ck("build twt=3", hi5622_ax_twt_build(wire, sizeof(wire), &len, &p) == 0 &&
	   hexeq(wire, len, frame_twt3));
	/* field B = the AP-start module context: echoed at hdr[3], everywhere
	 * else identical (the F2 lesson: field B is not a constant of the key). */
	ck("plan twt=1 fb=1", good_plan(1u, &p, 1u) == 0);
	ck("build fb=1", hi5622_ax_twt_build(wire, sizeof(wire), &len, &p) == 0 &&
	   wire[3] == 1u && len == 29u);
	ck("fb=1 differs only at hdr[3]",
	   wire[0] == 0x00u && wire[1] == 0x01u && wire[2] == 0x00u &&
	   hexeq(wire + 4, len - 4u, frame_twt1 + 8));
	ck("plan twt=3 fb=18", good_plan(3u, &p, 18u) == 0);
	ck("build fb=18", hi5622_ax_twt_build(wire, sizeof(wire), &len, &p) == 0 &&
	   wire[3] == 18u);

	/* ---- field-B derivation from the live AP-start module context ---- */
	fb = 0xAA;
	ck("fb ca=0 unresolved", hi5622_ax_twt_field_b(0u, &fb) == -EAGAIN &&
	   fb == 0xAA);
	ck("fb ctx0 -> 0",
	   hi5622_ax_twt_field_b(HI5622_AX_TWT_CTX0_CA, &fb) == 0 && fb == 0u);
	ck("fb ctx1 -> 1", hi5622_ax_twt_field_b(HI5622_AX_TWT_CTX0_CA +
		HI5622_AX_TWT_CTX_STRIDE, &fb) == 0 && fb == 1u);
	ck("fb ctx2 -> 2", hi5622_ax_twt_field_b(HI5622_AX_TWT_CTX0_CA +
		2u * HI5622_AX_TWT_CTX_STRIDE, &fb) == 0 && fb == 2u);
	ck("fb last ctx -> 18", hi5622_ax_twt_field_b(HI5622_AX_TWT_CTX0_CA +
		18u * HI5622_AX_TWT_CTX_STRIDE, &fb) == 0 && fb == 18u);
	ck("fb ctx19 refused", hi5622_ax_twt_field_b(HI5622_AX_TWT_CTX0_CA +
		19u * HI5622_AX_TWT_CTX_STRIDE, &fb) == -ERANGE);
	ck("fb misaligned refused", hi5622_ax_twt_field_b(
		HI5622_AX_TWT_CTX0_CA + 1u, &fb) == -ERANGE);
	ck("fb below ctx0 refused", hi5622_ax_twt_field_b(
		HI5622_AX_TWT_CTX0_CA - 1u, &fb) == -ERANGE);
	ck("fb unknown ca refused", hi5622_ax_twt_field_b(0x00200000u, &fb) ==
	   -ERANGE);
	ck("fb null out refused", hi5622_ax_twt_field_b(
		HI5622_AX_TWT_CTX0_CA, NULL) == -EINVAL);

	/* ---- fail-closed parameter matrix ---- */
	ck("off twt=0", hi5622_ax_twt_precheck(0u, 0u, 0u, 0u, 1u, &p) == 0 &&
	   !p.post);
	ck("off ignores AP shape", hi5622_ax_twt_precheck(0u, 0u, 0u, 0u, 7u,
							  &p) == 0 && !p.post);
	ck("twt=2 refused (not a {1,3} value)",
	   hi5622_ax_twt_precheck(2u, 1u, 1u, 1u, 0u, &p) == -EINVAL);
	ck("twt=4 refused", hi5622_ax_twt_precheck(4u, 1u, 1u, 1u, 0u, &p) ==
	   -EINVAL);
	ck("twt=-1 refused", hi5622_ax_twt_precheck(0xffffffffu, 1u, 1u, 1u,
						    0u, &p) == -EINVAL);
	ck("twt null out refused", hi5622_ax_twt_precheck(1u, 1u, 1u, 1u, 0u,
							  NULL) == -EINVAL);
	ck("twt=1 needs beacon_static",
	   hi5622_ax_twt_precheck(1u, 0u, 1u, 1u, 0u, &p) == -EINVAL);
	ck("twt=1 needs vap_start",
	   hi5622_ax_twt_precheck(1u, 1u, 0u, 1u, 0u, &p) == -EINVAL);
	ck("twt=1 needs he_caps",
	   hi5622_ax_twt_precheck(1u, 1u, 1u, 0u, 0u, &p) == -EINVAL);
	ck("twt=1 refuses he_caps=2",
	   hi5622_ax_twt_precheck(1u, 1u, 1u, 2u, 0u, &p) == -EINVAL);
	ck("twt=1 refuses 5 GHz AP",
	   hi5622_ax_twt_precheck(1u, 1u, 1u, 1u, 1u, &p) == -EOPNOTSUPP);
	ck("twt=3 refuses 5 GHz AP",
	   hi5622_ax_twt_precheck(3u, 1u, 1u, 1u, 1u, &p) == -EOPNOTSUPP);
	ck("failed precheck clears plan",
	   hi5622_ax_twt_precheck(2u, 1u, 1u, 1u, 0u, &p) == -EINVAL && !p.post);

	/* ---- builder refusals ---- */
	memset(&p, 0, sizeof(p));
	ck("unarmed plan refused", hi5622_ax_twt_build(wire, sizeof(wire), &len,
						       &p) == -EINVAL);
	ck("null wire refused", hi5622_ax_twt_build(NULL, sizeof(wire), &len,
						    &p) == -EINVAL);
	ck("null out_len refused", hi5622_ax_twt_build(wire, sizeof(wire), NULL,
						       &p) == -EINVAL);
	ck("null plan refused", hi5622_ax_twt_build(wire, sizeof(wire), &len,
						    NULL) == -EINVAL);
	ck("plan twt=1 for builder", good_plan(1u, &p, 0u) == 0);
	p.flags = 0u;
	ck("flags 0 refused", hi5622_ax_twt_build(wire, sizeof(wire), &len, &p) ==
	   -EINVAL);
	p.flags = 2u;
	ck("flags 2 refused", hi5622_ax_twt_build(wire, sizeof(wire), &len, &p) ==
	   -EINVAL);
	ck("plan twt=1 again", good_plan(1u, &p, 0u) == 0);
	p.field_b = 19u;
	ck("field_b 19 refused", hi5622_ax_twt_build(wire, sizeof(wire), &len,
						     &p) == -EINVAL);
	ck("plan twt=1 again 2", good_plan(1u, &p, 0u) == 0);
	p.da_mask = 0x3fu;
	ck("bad da_mask refused", hi5622_ax_twt_build(wire, sizeof(wire), &len,
						      &p) == -EINVAL);
	ck("plan twt=1 again 3", good_plan(1u, &p, 0u) == 0);
	p.da_expected = 0x41u;
	ck("bad da_expected refused", hi5622_ax_twt_build(wire, sizeof(wire),
							  &len, &p) == -EINVAL);
	ck("plan twt=1 again 4", good_plan(1u, &p, 0u) == 0);
	ck("small buffer refused", hi5622_ax_twt_build(wire,
		HI5622_AX_TWT_WIRE_LEN - 1u, &len, &p) == -ENOSPC && len == 0u);

	/* ---- sysfs status line ---- */
	memset(line, 0, sizeof(line));
	rc = hi5622_ax_twt_status_line(3u, 1u, 1u, -100, 2u, 1u, line,
				       sizeof(line));
	ck("status line length", rc > 0 && (size_t)rc == strlen(line));
	ck("status line text", strstr(line, "twt: param=3 (enable+required)") &&
	   strstr(line, "he_caps=1") && strstr(line, "beacon_static=1") &&
	   strstr(line, "last_rc=-100") && strstr(line, "posted=2") &&
	   strstr(line, "refused=1"));
	ck("status clamp", hi5622_ax_twt_status_line(0u, 0u, 0u, -ENODATA, 0u,
						     0u, line, 8u) == 7);
	ck("status null refused", hi5622_ax_twt_status_line(0u, 0u, 0u, 0, 0u,
							   0u, NULL, 8u) ==
	   -EINVAL);
	ck("mode names", !strcmp(hi5622_ax_twt_mode_name(0u), "off") &&
	   !strcmp(hi5622_ax_twt_mode_name(1u), "enable") &&
	   !strcmp(hi5622_ax_twt_mode_name(3u), "enable+required") &&
	   !strcmp(hi5622_ax_twt_mode_name(2u), "invalid"));

	if (argc > 1 && !strcmp(argv[1], "--frames")) {
		struct hi5622_ax_twt_plan fp;
		u8 fw[HI5622_AX_TWT_WIRE_LEN];
		size_t fl = 0;

		/* the exact wire bytes of the three shapes the fire recipe may
		 * see (field B 0 for the AP-start module context; 1 is shown to
		 * prove field B is not a constant of the key) */
		good_plan(1u, &fp, 0u);
		hi5622_ax_twt_build(fw, sizeof(fw), &fl, &fp);
		dump_frame("twt=1 field_b=0 (enable)", fw, fl);
		good_plan(3u, &fp, 0u);
		hi5622_ax_twt_build(fw, sizeof(fw), &fl, &fp);
		dump_frame("twt=3 field_b=0 (enable+required)", fw, fl);
		good_plan(1u, &fp, 1u);
		hi5622_ax_twt_build(fw, sizeof(fw), &fl, &fp);
		dump_frame("twt=1 field_b=1 (non-AP-start context)", fw, fl);
	}
	printf("twt selftest: %d/%d PASS\n", checks - fails, checks);
	return fails ? 1 : 0;
}
