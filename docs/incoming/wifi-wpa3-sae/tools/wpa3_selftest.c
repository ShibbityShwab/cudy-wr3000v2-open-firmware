/* SPDX-License-Identifier: GPL-2.0 */
/*
 * wpa3_selftest.c - host selftest for the WPA3-SAE/PMF host-side bits
 * (wifi-wpa3-sae lane, shared task task-144).
 *
 * Exercises the portable frame builders that the kernel layer calls for the
 * PMF default-management-key path (IGTK default selection) and the unchanged
 * WPA2 data-key paths.  Builds with the system C compiler against the same
 * hi5622_key_frames.c the 6.18 module compiles; no kernel headers, no device.
 *
 * The key constants/CAs cited in comments are derived values from
 * ../TABLES.md; no vendor byte is reproduced.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "hi5622_key_frames.h"

static int checks;
static int fails;

#define CK(cond, name)							\
	do {								\
		checks++;						\
		if (!(cond)) {						\
			fails++;					\
			printf("FAIL %s (line %d)\n", name, __LINE__);	\
		} else {						\
			printf("ok   %s\n", name);			\
		}							\
	} while (0)

static unsigned u16le(const unsigned char *p)
{
	return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned u32le(const unsigned char *p)
{
	return (unsigned)p[0] | ((unsigned)p[1] << 8) |
	       ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

/* cfg80211 cipher suite selectors used by the fixtures (00-0f-ac:xx). */
#define SUITE(low)	(0x000fac00u | (unsigned)(low))
#define WEP40		SUITE(1)
#define TKIP		SUITE(2)
#define CCMP		SUITE(4)
#define AES_CMAC	SUITE(6)
#define BIP_GMAC_128	SUITE(0x0b)
#define BIP_GMAC_256	SUITE(0x0c)
#define BIP_CMAC_256	SUITE(0x0d)
#define BOGUS		SUITE(0xee)

static void test_classify(void)
{
	CK(hkf_cipher_classify(CCMP) == HKF_CIPHER_DATA, "classify CCMP = data");
	CK(hkf_cipher_classify(TKIP) == HKF_CIPHER_DATA, "classify TKIP = data");
	CK(hkf_cipher_classify(AES_CMAC) == HKF_CIPHER_MGMT, "classify AES-CMAC = mgmt");
	CK(hkf_cipher_classify(BIP_CMAC_256) == HKF_CIPHER_MGMT, "classify BIP-CMAC-256 = mgmt");
	CK(hkf_cipher_classify(BIP_GMAC_128) == HKF_CIPHER_MGMT, "classify BIP-GMAC-128 = mgmt");
	CK(hkf_cipher_classify(BIP_GMAC_256) == HKF_CIPHER_MGMT, "classify BIP-GMAC-256 = mgmt");
	CK(hkf_cipher_classify(WEP40) == HKF_CIPHER_WEP, "classify WEP40 = wep");
	CK(hkf_cipher_classify(BOGUS) == HKF_CIPHER_UNKNOWN, "classify unknown suite = unknown");
	CK(hkf_cipher_classify(0) == HKF_CIPHER_UNKNOWN, "classify cipher 0 = unknown");
}

static void test_data_default_body(void)
{
	struct hkf_default_key_params p;
	unsigned char body[HKF_DEFAULT_KEY_BODY_LEN];
	int rc;

	memset(&p, 0, sizeof(p));
	p.key_index = 1;
	p.multicast = 1;
	p.mgmt = 0;
	rc = hkf_build_body_default_key(body, sizeof(body), &p);
	CK(rc == 0, "data default-key body builds (idx 1, multicast)");
	CK(body[0x00] == 1 && body[0x01] == 0 && body[0x02] == 1,
	   "data default-key body fields at +0/+1/+2");
	CK(body[0x04] == 0, "data default-key body leaves the mgmt cipher byte +4 zero");
}

static void test_mgmt_default_body(void)
{
	struct hkf_default_key_params p;
	unsigned char body[HKF_DEFAULT_KEY_BODY_LEN];
	int rc;

	memset(&p, 0, sizeof(p));
	p.key_index = 4;		/* GN_igtk */
	p.multicast = 1;
	p.mgmt = 1;
	p.cipher = BIP_CMAC_256;
	rc = hkf_build_body_default_key(body, sizeof(body), &p);
	CK(rc == 0, "PMF default-mgmt body builds (idx 4, BIP-CMAC-256)");
	CK(body[0x00] == 4, "PMF default-mgmt body +0 = key_index 4");
	CK(body[0x01] == 0 && body[0x02] == 1,
	   "PMF default-mgmt body +1/+2 = unicast 0 / multicast class flag 1");
	CK(body[0x04] == 0x0d,
	   "PMF default-mgmt body +4 = BIP-CMAC-256 low byte 0x0d (worker requires a mgmt-class cipher)");

	p.cipher = BIP_GMAC_256;
	rc = hkf_build_body_default_key(body, sizeof(body), &p);
	CK(rc == 0 && body[0x04] == 0x0c,
	   "PMF default-mgmt body accepts BIP-GMAC-256 (low byte 0x0c)");

	p.cipher = BIP_GMAC_128;
	rc = hkf_build_body_default_key(body, sizeof(body), &p);
	CK(rc == 0 && body[0x04] == 0x0b,
	   "PMF default-mgmt body accepts BIP-GMAC-128 (low byte 0x0b)");

	p.cipher = AES_CMAC;
	rc = hkf_build_body_default_key(body, sizeof(body), &p);
	CK(rc == 0 && body[0x04] == 0x06,
	   "PMF default-mgmt body accepts AES-CMAC/BIP-CMAC-128 (low byte 0x06)");
}

static void test_mgmt_refusals(void)
{
	struct hkf_default_key_params p;

	/* wrong key index for the mgmt path */
	memset(&p, 0, sizeof(p));
	p.key_index = 3;
	p.multicast = 1;
	p.mgmt = 1;
	p.cipher = BIP_CMAC_256;
	CK(hkf_default_key_params_validate(&p) == -EINVAL,
	   "PMF default-mgmt refuses key index 3 (firmware mgmt range 4..5)");

	/* unicast flag set: worker 0xA5C9D expects unicast 0, multicast 1 */
	memset(&p, 0, sizeof(p));
	p.key_index = 4;
	p.unicast = 1;
	p.mgmt = 1;
	p.cipher = BIP_CMAC_256;
	CK(hkf_default_key_params_validate(&p) == -EINVAL,
	   "PMF default-mgmt refuses unicast flag (vendor/IGTK form is multicast)");

	/* no class flag at all */
	memset(&p, 0, sizeof(p));
	p.key_index = 4;
	p.mgmt = 1;
	p.cipher = BIP_CMAC_256;
	CK(hkf_default_key_params_validate(&p) == -EINVAL,
	   "PMF default-mgmt refuses zero class flags (worker error 0x545)");

	/* data cipher on the mgmt path: worker 0xA5C9D mgmt mask 0x3840 */
	memset(&p, 0, sizeof(p));
	p.key_index = 4;
	p.multicast = 1;
	p.mgmt = 1;
	p.cipher = CCMP;
	CK(hkf_default_key_params_validate(&p) == -EOPNOTSUPP,
	   "PMF default-mgmt refuses a data cipher (CCMP)");

	/* cipher 0: the firmware worker rejects it with 0x520b - never post it */
	memset(&p, 0, sizeof(p));
	p.key_index = 4;
	p.multicast = 1;
	p.mgmt = 1;
	p.cipher = 0;
	CK(hkf_default_key_params_validate(&p) == -EOPNOTSUPP,
	   "PMF default-mgmt refuses cipher byte 0 (the latched cipher is required)");

	/* data path still refuses the mgmt key indexes */
	memset(&p, 0, sizeof(p));
	p.key_index = 4;
	p.multicast = 1;
	p.mgmt = 0;
	CK(hkf_default_key_params_validate(&p) == -EINVAL,
	   "data default-key path still refuses key index 4");

	/* bad mgmt flag value */
	memset(&p, 0, sizeof(p));
	p.key_index = 4;
	p.multicast = 1;
	p.mgmt = 2;
	p.cipher = BIP_CMAC_256;
	CK(hkf_default_key_params_validate(&p) == -EINVAL,
	   "default-mgmt refuses a non-boolean mgmt flag");
}

static void test_mgmt_frame(void)
{
	struct hkf_default_key_params p;
	struct hkf_frame f;
	unsigned char *body;
	int rc;

	memset(&p, 0, sizeof(p));
	p.key_index = 4;
	p.multicast = 1;
	p.mgmt = 1;
	p.cipher = BIP_CMAC_256;
	rc = hkf_build_default_key(&f, 0, 0, &p);
	CK(rc == 0, "PMF default-mgmt frame builds");
	CK(f.len == HKF_G0_OVERHEAD + HKF_DEFAULT_KEY_BODY_LEN,
	   "PMF default-mgmt frame length = 28-byte g0 overhead + 0x28 body");
	CK(f.cmd == HKF_CMD_SET_DEFAULT_KEY, "PMF default-mgmt frame cmd = 0x0079");
	CK((f.buf[0] & 0x0fu) == HKF_HCC_GROUP_CHIP, "frame header group 0");
	CK(u16le(f.buf + 4) == f.len, "frame header total length");
	CK(u16le(f.buf + 6) == HKF_HCC_ID_CFG_EVENT, "frame header id 1 (cfg event)");
	CK(u16le(f.buf + 10) == HKF_HCC_TAG, "frame header tag 0x5a5a");
	CK(u16le(f.buf + 20) == HKF_CMD_SET_DEFAULT_KEY, "payload cmd 0x0079");
	CK(u16le(f.buf + 22) == HKF_DEFAULT_KEY_BODY_LEN, "payload body length 0x28");
	body = f.buf + 24;
	CK(body[0x00] == 4 && body[0x02] == 1 && body[0x04] == 0x0d,
	   "PMF default-mgmt frame body carries idx/class/cipher");
	CK(f.buf[f.len - 4] == 0xff && f.buf[f.len - 1] == 0xff,
	   "frame pad is 0xff (inherited g0 id1 pad)");
}

static void test_unchanged_data_frames(void)
{
	struct hkf_default_key_params dp;
	struct hkf_key_params kp;
	struct hkf_frame f;
	int rc;

	memset(&dp, 0, sizeof(dp));
	dp.key_index = 1;
	dp.multicast = 1;
	rc = hkf_build_default_key(&f, 0, 0, &dp);
	CK(rc == 0 && f.cmd == HKF_CMD_SET_DEFAULT_KEY &&
	   f.len == HKF_G0_OVERHEAD + HKF_DEFAULT_KEY_BODY_LEN,
	   "data default-key frame unchanged (cmd 0x0079, same length)");

	memset(&kp, 0, sizeof(kp));
	kp.key_index = 1;
	kp.cipher = CCMP;
	kp.key_len = 16;
	memset(kp.key, 0xa5, sizeof(kp.key));
	rc = hkf_build_add_key(&f, 0, 0, &kp);
	CK(rc == 0 && f.cmd == HKF_CMD_ADD_KEY,
	   "WPA2 add-key frame unchanged (cmd 0x0078)");
	CK(f.len == HKF_G0_OVERHEAD + HKF_ADD_KEY_BODY_LEN,
	   "WPA2 add-key frame length = 28 + 0x44");
	CK(f.buf[24 + 0x00] == 1 && u32le(f.buf + 24 + 0x38) == 16 &&
	   u32le(f.buf + 24 + 0x40) == CCMP,
	   "WPA2 add-key body layout unchanged (idx/key_len/cipher at +0/+0x38/+0x40)");
}

static void test_frame_bounds(void)
{
	struct hkf_default_key_params p;
	struct hkf_frame f;

	memset(&p, 0, sizeof(p));
	p.key_index = 4;
	p.multicast = 1;
	p.mgmt = 1;
	p.cipher = BIP_CMAC_256;
	CK(hkf_build_default_key(&f, HKF_MAX_MODULE + 1u, 0, &p) == -EINVAL,
	   "frame builder refuses module out of range");
	CK(hkf_build_default_key(&f, 0, HKF_MAX_DST + 1u, &p) == -EINVAL,
	   "frame builder refuses dst out of range");
	CK(hkf_build_default_key(&f, 0, 0, NULL) == -EINVAL,
	   "frame builder refuses NULL params");
}

int main(void)
{
	printf("wifi-wpa3-sae host selftest (portable key-frame builders)\n");
	test_classify();
	test_data_default_body();
	test_mgmt_default_body();
	test_mgmt_refusals();
	test_mgmt_frame();
	test_unchanged_data_frames();
	test_frame_bounds();
	printf("\n== wpa3_selftest: %d checks, %d failed ==\n", checks, fails);
	if (fails) {
		printf("RESULT: FAIL\n");
		return 1;
	}
	printf("RESULT: PASS\n");
	return 0;
}
