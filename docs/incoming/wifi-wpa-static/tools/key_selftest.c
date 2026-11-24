/* SPDX-License-Identifier: GPL-2.0 */
/*
 * key_selftest.c - host selftest for the WPA2 key-install frame builders
 * (Hi5622V100 clean-room driver, Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (wifi-wpa-static lane, shared task-139).
 * Links ONLY hi5622_key_frames.c (portable C99); no kernel, no device.
 *
 * Run:
 *   cc -std=c99 -Wall -Wextra -Werror -I<module src> key_selftest.c \
 *      <module src>/hi5622_key_frames.c -o key_selftest && ./key_selftest
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_key_frames.h"

static int failures;

#define CHECK(cond) do {						\
	if (!(cond)) {							\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
		failures++;						\
	}								\
} while (0)

static u32 get32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
	       ((u32)p[3] << 24);
}

static u16 get16(const u8 *p)
{
	return (u16)p[0] | ((u16)p[1] << 8);
}

static struct hkf_key_params mk_ccmp_pairwise(void)
{
	struct hkf_key_params p;
	int i;

	memset(&p, 0, sizeof(p));
	p.key_index = 0;
	p.pairwise = 1;
	p.mac[0] = 0x02; p.mac[1] = 0x11; p.mac[2] = 0x22;
	p.mac[3] = 0x33; p.mac[4] = 0x44; p.mac[5] = 0x55;
	p.cipher = 0x000fac04u;		/* WLAN_CIPHER_SUITE_CCMP */
	p.key_len = 16;
	p.seq_len = 6;
	for (i = 0; i < 16; i++)
		p.key[i] = (u8)(0xa0 + i);
	for (i = 0; i < 6; i++)
		p.seq[i] = (u8)(0x10 + i);
	return p;
}

static struct hkf_key_params mk_ccmp_group(void)
{
	struct hkf_key_params p = mk_ccmp_pairwise();

	p.pairwise = 0;
	memset(p.mac, 0, sizeof(p.mac));
	p.key_index = 1;
	return p;
}

static void test_cipher_classify(void)
{
	CHECK(hkf_cipher_classify(0x000fac02u) == HKF_CIPHER_DATA);	/* TKIP */
	CHECK(hkf_cipher_classify(0x000fac04u) == HKF_CIPHER_DATA);	/* CCMP */
	CHECK(hkf_cipher_classify(0x000fac08u) == HKF_CIPHER_DATA);	/* GCMP */
	CHECK(hkf_cipher_classify(0x000fac09u) == HKF_CIPHER_DATA);	/* GCMP-256 */
	CHECK(hkf_cipher_classify(0x000fac0au) == HKF_CIPHER_DATA);	/* CCMP-256 */
	CHECK(hkf_cipher_classify(0x000fac01u) == HKF_CIPHER_WEP);	/* WEP40 */
	CHECK(hkf_cipher_classify(0x000fac05u) == HKF_CIPHER_WEP);	/* WEP104 */
	CHECK(hkf_cipher_classify(0x000fac06u) == HKF_CIPHER_MGMT);	/* AES-CMAC */
	CHECK(hkf_cipher_classify(0x000fac0bu) == HKF_CIPHER_MGMT);	/* BIP-GMAC-128 */
	CHECK(hkf_cipher_classify(0x000fac0cu) == HKF_CIPHER_MGMT);	/* BIP-GMAC-256 */
	CHECK(hkf_cipher_classify(0x000fac0du) == HKF_CIPHER_MGMT);	/* BIP-CMAC-256 */
	CHECK(hkf_cipher_classify(0x000fac00u) == HKF_CIPHER_UNKNOWN);
	CHECK(hkf_cipher_classify(0x000fac03u) == HKF_CIPHER_UNKNOWN);
	CHECK(hkf_cipher_classify(0x000fac07u) == HKF_CIPHER_UNKNOWN);
	CHECK(hkf_cipher_classify(0x000fac0eu) == HKF_CIPHER_UNKNOWN);
	CHECK(hkf_cipher_classify(0x00000000u) == HKF_CIPHER_UNKNOWN);
	CHECK(strcmp(hkf_cipher_class_name(HKF_CIPHER_DATA), "data") == 0);
	CHECK(strcmp(hkf_cipher_class_name(HKF_CIPHER_MGMT), "mgmt") == 0);
	CHECK(strcmp(hkf_cipher_class_name(HKF_CIPHER_WEP), "wep") == 0);
	CHECK(strcmp(hkf_cipher_class_name(HKF_CIPHER_UNKNOWN), "unknown") == 0);
}

static void test_add_key_body(void)
{
	struct hkf_key_params p = mk_ccmp_pairwise();
	u8 body[HKF_ADD_KEY_BODY_LEN];

	memset(body, 0xee, sizeof(body));
	CHECK(hkf_build_body_add_key(body, sizeof(body), &p) == 0);
	CHECK(body[0x00] == 0);
	CHECK(body[0x01] == 1);
	CHECK(memcmp(body + 0x02, p.mac, 6) == 0);
	CHECK(memcmp(body + 0x08, p.key, 16) == 0);
	/* key tail (16..32) must be zero */
	{
		int i, zero = 1;

		for (i = 16; i < 32; i++)
			if (body[0x08 + i] != 0)
				zero = 0;
		CHECK(zero);
	}
	CHECK(memcmp(body + 0x28, p.seq, 6) == 0);
	CHECK(get32(body + 0x38) == 16);
	CHECK(get32(body + 0x3c) == 6);
	CHECK(get32(body + 0x40) == 0x000fac04u);
	/* no uninitialised bytes: everything after seq data to the length
	 * fields must be zero */
	{
		int i, zero = 1;

		for (i = 0x2e; i < 0x38; i++)
			if (body[i] != 0)
				zero = 0;
		CHECK(zero);
	}
}

static void test_add_key_group_body(void)
{
	struct hkf_key_params p = mk_ccmp_group();
	u8 body[HKF_ADD_KEY_BODY_LEN];

	CHECK(hkf_build_body_add_key(body, sizeof(body), &p) == 0);
	CHECK(body[0x00] == 1);
	CHECK(body[0x01] == 0);
	CHECK(body[0x02] == 0 && body[0x07] == 0);
	CHECK(get32(body + 0x38) == 16);
}

static void test_del_and_default_bodies(void)
{
	struct hkf_key_params p = mk_ccmp_pairwise();
	struct hkf_default_key_params d;
	u8 body[HKF_DEL_KEY_BODY_LEN];
	u8 dbody[HKF_DEFAULT_KEY_BODY_LEN];

	CHECK(hkf_build_body_del_key(body, sizeof(body), &p) == 0);
	CHECK(body[0] == 0 && body[1] == 1);
	CHECK(memcmp(body + 2, p.mac, 6) == 0);

	memset(&d, 0, sizeof(d));
	d.key_index = 1;
	d.multicast = 1;
	CHECK(hkf_build_body_default_key(dbody, sizeof(dbody), &d) == 0);
	CHECK(dbody[0] == 1 && dbody[1] == 0 && dbody[2] == 1);
	{
		int i, zero = 1;

		for (i = 3; i < (int)sizeof(dbody); i++)
			if (dbody[i] != 0)
				zero = 0;
		CHECK(zero);
	}
}

static void test_add_key_frame(void)
{
	struct hkf_key_params p = mk_ccmp_pairwise();
	struct hkf_frame f;
	size_t total = HKF_G0_OVERHEAD + HKF_ADD_KEY_BODY_LEN;
	int i;

	memset(&f, 0xee, sizeof(f));
	CHECK(hkf_build_add_key(&f, 3, 0, &p) == 0);
	CHECK(f.len == total);
	CHECK(f.cmd == HKF_CMD_ADD_KEY);
	CHECK(f.buf[0] == 0x00);			/* group 0 */
	CHECK(f.buf[1] == 0x01);			/* alloc 1, dst 0 */
	CHECK(f.buf[2] == 0x00);
	CHECK(f.buf[3] == 0x03);			/* module */
	CHECK(get16(f.buf + 4) == total);
	CHECK(get16(f.buf + 6) == 1);			/* id 1 */
	CHECK(f.buf[8] == 0x00);
	CHECK(f.buf[9] == 0x00);			/* dst */
	CHECK(get16(f.buf + 10) == 0x5a5a);
	CHECK(f.buf[16] == 0x01);			/* inherited token */
	CHECK(get16(f.buf + 20) == 0x0078);
	CHECK(get16(f.buf + 22) == HKF_ADD_KEY_BODY_LEN);
	CHECK(memcmp(f.buf + 24 + 0x08, p.key, 16) == 0);	/* body key at +0x08 */
	CHECK(f.buf[24 + 0] == 0);
	CHECK(f.buf[24 + 1] == 1);
	CHECK(get32(f.buf + 24 + 0x40) == 0x000fac04u);
	for (i = 0; i < 4; i++)
		CHECK(f.buf[total - 4 + i] == 0xff);	/* inherited pad */
	/* total frame must fit the declared g0 limit and the HCC header total
	 * field must be inside the class bound */
	CHECK(total <= HKF_MAX_FRAME);
}

static void test_del_default_frames(void)
{
	struct hkf_key_params p = mk_ccmp_pairwise();
	struct hkf_default_key_params d;
	struct hkf_frame f;

	memset(&d, 0, sizeof(d));
	d.key_index = 1;
	d.multicast = 1;
	CHECK(hkf_build_del_key(&f, 0, 0, &p) == 0);
	CHECK(f.len == HKF_G0_OVERHEAD + HKF_DEL_KEY_BODY_LEN);
	CHECK(f.cmd == HKF_CMD_DEL_KEY);
	CHECK(get16(f.buf + 20) == 0x007a);
	CHECK(get16(f.buf + 22) == HKF_DEL_KEY_BODY_LEN);
	CHECK(hkf_build_default_key(&f, 0, 0, &d) == 0);
	CHECK(f.len == HKF_G0_OVERHEAD + HKF_DEFAULT_KEY_BODY_LEN);
	CHECK(f.cmd == HKF_CMD_SET_DEFAULT_KEY);
	CHECK(get16(f.buf + 20) == 0x0079);
	CHECK(get16(f.buf + 22) == HKF_DEFAULT_KEY_BODY_LEN);
}

static void test_validation_rules(void)
{
	struct hkf_key_params p;
	struct hkf_default_key_params d;
	struct hkf_frame f;
	u8 body[HKF_ADD_KEY_BODY_LEN];

	p = mk_ccmp_pairwise();
	CHECK(hkf_key_params_validate(&p) == 0);

	p = mk_ccmp_pairwise();
	p.key_len = HKF_KEY_MAX + 1;
	CHECK(hkf_key_params_validate(&p) != 0);
	p = mk_ccmp_pairwise();
	p.seq_len = HKF_SEQ_MAX + 1;
	CHECK(hkf_key_params_validate(&p) != 0);
	p = mk_ccmp_pairwise();
	p.key_index = 6;
	CHECK(hkf_key_params_validate(&p) != 0);
	/* data cipher requires index <= 3 (firmware worker 0xA5825) */
	p = mk_ccmp_group();
	p.key_index = 4;
	CHECK(hkf_key_params_validate(&p) != 0);
	/* mgmt cipher requires index 4..5 */
	p = mk_ccmp_group();
	p.cipher = 0x000fac06u;		/* AES-CMAC (BIP) */
	p.key_index = 3;
	CHECK(hkf_key_params_validate(&p) != 0);
	p.key_index = 4;
	CHECK(hkf_key_params_validate(&p) == 0);	/* mgmt group key 4 */
	p.key_index = 5;
	CHECK(hkf_key_params_validate(&p) == 0);
	/* unknown cipher is a hard -EOPNOTSUPP (never a guessed enum) */
	p = mk_ccmp_group();
	p.cipher = 0x000fac03u;
	CHECK(hkf_key_params_validate(&p) == -EOPNOTSUPP);
	CHECK(hkf_build_body_add_key(body, sizeof(body), &p) == -EOPNOTSUPP);
	/* WEP class accepts index 0..5 */
	p = mk_ccmp_group();
	p.cipher = 0x000fac01u;		/* WEP40 */
	p.key_len = 5;
	p.key_index = 2;
	CHECK(hkf_key_params_validate(&p) == 0);
	/* pairwise MAC must be unicast and nonzero */
	p = mk_ccmp_pairwise();
	memset(p.mac, 0, 6);
	CHECK(hkf_key_params_validate(&p) != 0);
	p = mk_ccmp_pairwise();
	p.mac[0] = 0x01;		/* multicast bit */
	CHECK(hkf_key_params_validate(&p) != 0);
	/* group key must not carry a unicast MAC */
	p = mk_ccmp_group();
	p.mac[0] = 0x02;
	CHECK(hkf_key_params_validate(&p) != 0);
	/* empty key refused */
	p = mk_ccmp_group();
	p.key_len = 0;
	CHECK(hkf_key_params_validate(&p) != 0);
	/* NULL / short-buffer */
	CHECK(hkf_key_params_validate(NULL) != 0);
	p = mk_ccmp_group();
	CHECK(hkf_build_body_add_key(NULL, sizeof(body), &p) != 0);
	CHECK(hkf_build_body_add_key(body, sizeof(body) - 1, &p) != 0);
	CHECK(hkf_build_add_key(&f, 0, 0, NULL) != 0);
	CHECK(hkf_build_del_key(&f, 0, 0, NULL) != 0);
	CHECK(hkf_build_default_key(&f, 0, 0, NULL) != 0);
	CHECK(hkf_build_add_key(NULL, 0, 0, NULL) != 0);
	/* module/dst bounds */
	p = mk_ccmp_group();
	CHECK(hkf_build_add_key(&f, HKF_MAX_MODULE + 1, 0, &p) != 0);
	CHECK(hkf_build_add_key(&f, 0, HKF_MAX_DST + 1, &p) != 0);
	/* default key: no class flag -> invalid; index > 3 -> invalid */
	memset(&d, 0, sizeof(d));
	CHECK(hkf_default_key_params_validate(&d) != 0);
	d.key_index = 4;
	d.multicast = 1;
	CHECK(hkf_default_key_params_validate(&d) != 0);
	d.key_index = 3;
	d.unicast = 1;
	CHECK(hkf_default_key_params_validate(&d) == 0);
}

int main(void)
{
	test_cipher_classify();
	test_add_key_body();
	test_add_key_group_body();
	test_del_and_default_bodies();
	test_add_key_frame();
	test_del_default_frames();
	test_validation_rules();
	if (failures) {
		printf("key_selftest: %d FAILED\n", failures);
		return 1;
	}
	printf("key_selftest: all checks passed (cipher class, bodies, g0 frames, bounds)\n");
	return 0;
}
