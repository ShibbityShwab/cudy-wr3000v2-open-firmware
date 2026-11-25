// SPDX-License-Identifier: GPL-2.0
/*
 * ax_events_selftest.c - host selftest of the portable AX config-event model
 * (lane wifi-ax-advanced, task-195).  No device: compiles and runs on the
 * workstation.  The expected frames are the decoded envelope bytes from
 * ../TABLES.md T12 and are cross-checked against
 * ../tools/fw_ax_advanced_re.py --frames.
 */
#include <stdio.h>
#include <string.h>
#include "hi5622_ax_events.h"

static int checks;
static int fails;

static void ck(const char *name, int cond)
{
	checks++;
	if (!cond) {
		fails++;
		printf("FAIL: %s\n", name);
	}
}

static int hexeq(const struct hi5622_ax_frame *f, const char *hex)
{
	char got[2 * HI5622_AX_FRAME_MAX + 1];
	unsigned int i;

	if (strlen(hex) != 2u * f->len)
		return 0;
	for (i = 0; i < f->len; i++)
		snprintf(got + 2 * i, 3, "%02x", f->buf[i]);
	got[2 * f->len] = 0;
	return !strcmp(got, hex);
}

int main(void)
{
	struct hi5622_ax_frame f;
	const char *toks[] = { "sch", "usr_bw", "00:11:22:33:44:55", "1" };
	const char *bad_toks[] = { "zzz" };
	const char *parse[8];

	/* the fired envelope: TWT enable 1 and 3 */
	ck("twt en", hi5622_ax_twt_enable(&f, 0, 1) == 0 &&
	   hexeq(&f, "000100001d00010000005a5a00000000010000005d03010001"
		     "ffffffff"));
	ck("twt en+req", hi5622_ax_twt_enable(&f, 0, 3) == 0 &&
	   hexeq(&f, "000100001d00010000005a5a00000000010000005d03010003"
		     "ffffffff"));
	ck("ext twt", hi5622_ax_ext_twt_ie(&f, 0, true) == 0 &&
	   hexeq(&f, "000100002000010000005a5a0000000001000000e9000400010000"
		     "00ffffffff"));
	ck("he gi 1", hi5622_ax_he_gi(&f, 0, 1) == 0 &&
	   hexeq(&f, "000100001d00010000005a5a00000000010000006f00010001"
		     "ffffffff"));
	ck("mu edca", hi5622_ax_mu_edca_param(&f, 0, 0x5B, 0, 0x00F0F0) == 0 &&
	   hexeq(&f, "000100002800010000005a5a00000000010000005b000c005b0000"
		     "0000000000f0f00000ffffffff"));
	ck("bfee", hi5622_ax_bfee(&f, 0, 1) == 0 &&
	   hexeq(&f, "000100001d00010000005a5a00000000010000003503010001"
		     "ffffffff"));
	ck("bfer", hi5622_ax_bfer(&f, 0, 1) == 0 &&
	   hexeq(&f, "000100001d00010000005a5a0000000001000000c804010001"
		     "ffffffff"));
	ck("trig su bf fb", hi5622_ax_trig_su_bf_fb(&f, 0, 1) == 0 &&
	   hexeq(&f, "000100001d00010000005a5a00000000010000002e03010001"
		     "ffffffff"));
	ck("cyc sche", hi5622_ax_cyc_sche(&f, 0, 1) == 0 &&
	   hexeq(&f, "000100001d00010000005a5a00000000010000005303010001"
		     "ffffffff"));
	ck("sr enable", hi5622_ax_sr_enable(&f, 0, 1) == 0 &&
	   hexeq(&f, "000100001d00010000005a5a0000000001000000b604010001"
		     "ffffffff"));
	ck("alg sch", hi5622_ax_alg(&f, 0, toks, 4) == 0 &&
	   hexeq(&f, "000100004300010000005a5a0000000001000000280127000400040b"
		     "1d000000736368007573725f62770030303a31313a32323a33333a3434"
		     "3a3535003100ffffffff"));

	/* envelope contract (f still holds the ALG frame here) */
	ck("length arithmetic", f.len == 28 + 0x27);
	ck("alg key at 20", f.len > 22 && f.buf[20] == 0x28 && f.buf[21] == 0x01);
	ck("field B echoed", (hi5622_ax_twt_enable(&f, 4, 1) == 0 && f.buf[3] == 4));

	/* fail-closed cases */
	{
		u8 body[4] = { 0 };

		ck("unknown key refused",
		   hi5622_ax_event(0, 0x01F7, body, 1, &f) == -EOPNOTSUPP);
		ck("twt flags 0 refused", hi5622_ax_twt_enable(&f, 0, 0) == -EINVAL);
		ck("twt flags 2 refused", hi5622_ax_twt_enable(&f, 0, 2) == -EINVAL);
		ck("twt flags 4 refused", hi5622_ax_twt_enable(&f, 0, 4) == -EINVAL);
		ck("he gi 3 refused", hi5622_ax_he_gi(&f, 0, 3) == -EINVAL);
		ck("he gi 0 refused", hi5622_ax_he_gi(&f, 0, 0) == -EINVAL);
		ck("bfer 4 refused", hi5622_ax_bfer(&f, 0, 4) == -EINVAL);
		ck("bfee 2 refused", hi5622_ax_bfee(&f, 0, 2) == -EINVAL);
		ck("cyc 2 refused", hi5622_ax_cyc_sche(&f, 0, 2) == -EINVAL);
		ck("mu sel 0x5A refused",
		   hi5622_ax_mu_edca_param(&f, 0, 0x5A, 0, 0) == -EINVAL);
		ck("mu sel 0x60 refused",
		   hi5622_ax_mu_edca_param(&f, 0, 0x60, 0, 0) == -EINVAL);
		ck("mu ac 4 refused",
		   hi5622_ax_mu_edca_param(&f, 0, 0x5B, 4, 0) == -EINVAL);
		ck("alg bad module refused",
		   hi5622_ax_alg(&f, 0, bad_toks, 1) == -EINVAL);
		ck("alg no tokens refused",
		   hi5622_ax_alg(&f, 0, toks, 0) == -EINVAL);
		ck("ext twt bad len refused",
		   hi5622_ax_event(0, HI5622_AX_KEY_EXT_TWT_IE, body, 3, &f) ==
		   -EINVAL);
		ck("null out refused", hi5622_ax_twt_enable(NULL, 0, 1) == -EINVAL);
	}

	/* ALG parse roundtrip on a freshly generated frame */
	ck("alg parse", hi5622_ax_alg(&f, 0, toks, 4) == 0 &&
	   hi5622_ax_alg_parse(f.buf + 24, (u16)(f.len - 28), parse, 8) == 4 &&
	   !strcmp(parse[0], "sch") && !strcmp(parse[1], "usr_bw") &&
	   !strcmp(parse[2], "00:11:22:33:44:55") && !strcmp(parse[3], "1"));

	printf("ax-events selftest: %d/%d PASS\n", checks - fails, checks);
	return fails ? 1 : 0;
}
