// SPDX-License-Identifier: GPL-2.0
/*
 * he_caps_selftest.c - host selftest for the HE capability contract
 * (lane wifi-ax-he-caps, task-177).  Plain C99, no device, no network.
 *
 * The expected bytes/values below are the INDEPENDENT transcription of the
 * evidence tables (../TABLES.md T3/T10), not a re-use of the implementation
 * tables: a change in hi5622_he_caps.c that silently changes the advertised
 * capability fails here.
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_he_caps.h"

static unsigned int checks;
static unsigned int failures;

#define CHECK(cond, what)						\
	do {								\
		checks++;						\
		if (!(cond)) {						\
			failures++;					\
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, what); \
		}							\
	} while (0)

static void expect_bytes(const char *what, const u8 *got, const u8 *want,
			 size_t len)
{
	unsigned int i;
	int ok = 1;

	checks++;
	for (i = 0; i < len; i++) {
		if (got[i] != want[i]) {
			ok = 0;
			printf("FAIL %s: byte %u = 0x%02x, want 0x%02x\n",
			       what, i, got[i], want[i]);
		}
	}
	if (!ok)
		failures++;
}

static void test_policy(void)
{
	CHECK(hi5622_he_caps_policy(0u) == (int)HI5622_HE_CAPS_OFF,
	      "he_caps=0 is off");
	CHECK(hi5622_he_caps_policy(1u) == (int)HI5622_HE_CAPS_ON,
	      "he_caps=1 is on");
	CHECK(hi5622_he_caps_policy(2u) == -EINVAL, "he_caps=2 refused");
	CHECK(hi5622_he_caps_policy(3u) == -EINVAL, "he_caps=3 refused");
	CHECK(hi5622_he_caps_policy(0xffffffffu) == -EINVAL,
	      "he_caps=UINT_MAX refused");
}

static void test_blob_2g(void)
{
	struct hi5622_he_caps caps;
	static const u8 mac[6] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 };
	static const u8 phy[11] = { 0x02 };
	static const u16 mcs[6] = {
		0xfffa, 0xfffa, 0xffff, 0xffff, 0xffff, 0xffff,
	};
	unsigned int i;

	memset(&caps, 0xaa, sizeof(caps));
	CHECK(hi5622_he_caps_fill(0u, &caps) == 0, "fill 2.4 GHz");
	CHECK(caps.has_he == 1u, "2.4 GHz has_he");
	CHECK(caps.types_mask == HI5622_HE_TYPES_MASK_AP,
	      "2.4 GHz AP-only iftype mask");
	CHECK(caps.types_mask == (1u << 3), "AP iftype is bit 3");
	expect_bytes("2.4 GHz MAC caps", caps.mac_cap, mac, sizeof(mac));
	expect_bytes("2.4 GHz PHY caps", caps.phy_cap, phy, sizeof(phy));
	for (i = 0; i < 6u; i++)
		CHECK(caps.he_mcs[i] == mcs[i], "2.4 GHz MCS/NSS word");
	CHECK(hi5622_he_caps_mcs_len(0u) == 4u, "2.4 GHz MCS len 4");
	for (i = 0; i < HI5622_HE_PPE_LEN; i++)
		CHECK(caps.ppe_thres[i] == 0u, "2.4 GHz PPE empty");
	CHECK(caps.he_6ghz_capa == 0u, "2.4 GHz no 6 GHz capa");
}

static void test_blob_5g(void)
{
	struct hi5622_he_caps caps;
	static const u8 phy[11] = { 0x0c };
	static const u16 mcs[6] = {
		0xfffa, 0xfffa, 0xfffa, 0xfffa, 0xffff, 0xffff,
	};
	unsigned int i;

	CHECK(hi5622_he_caps_fill(1u, &caps) == 0, "fill 5 GHz");
	CHECK(caps.has_he == 1u, "5 GHz has_he");
	CHECK(caps.types_mask == HI5622_HE_TYPES_MASK_AP,
	      "5 GHz AP-only iftype mask");
	expect_bytes("5 GHz PHY caps", caps.phy_cap, phy, sizeof(phy));
	for (i = 0; i < 6u; i++)
		CHECK(caps.he_mcs[i] == mcs[i], "5 GHz MCS/NSS word");
	CHECK(hi5622_he_caps_mcs_len(1u) == 8u, "5 GHz MCS len 8");
	for (i = 0; i < HI5622_HE_PPE_LEN; i++)
		CHECK(caps.ppe_thres[i] == 0u, "5 GHz PPE empty");
	CHECK(caps.he_6ghz_capa == 0u, "5 GHz no 6 GHz capa");
}

static void test_refusals(void)
{
	struct hi5622_he_caps caps;

	memset(&caps, 0xaa, sizeof(caps));
	CHECK(hi5622_he_caps_fill(2u, &caps) == -EINVAL, "band 2 refused");
	CHECK(caps.has_he == 0u && caps.types_mask == 0u,
	      "refused band leaves a zeroed blob");
	CHECK(hi5622_he_caps_fill(0xffu, &caps) == -EINVAL,
	      "band 255 refused");
	CHECK(hi5622_he_caps_fill(0u, NULL) == -EINVAL, "NULL blob refused");
	CHECK(hi5622_he_caps_mcs_len(2u) == 0u, "band 2 MCS len 0");
	CHECK(hi5622_he_caps_ie_len(2u) == -EINVAL, "band 2 IE len refused");
	CHECK(hi5622_he_caps_build_ie(2u, (u8 *)&caps, sizeof(caps)) ==
	      -EINVAL, "band 2 IE refused");
	CHECK(hi5622_he_caps_build_ie(0u, NULL, 64u) == -EINVAL,
	      "NULL IE buffer refused");
}

static void test_ie_2g(void)
{
	/* independent transcription of TABLES.md T10 (2.4 GHz, 24 B) */
	static const u8 want[24] = {
		0xff, 0x16, 0x23,
		0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00,
		0xfa, 0xff, 0xfa, 0xff,
	};
	u8 buf[64];

	memset(buf, 0xaa, sizeof(buf));
	CHECK(hi5622_he_caps_ie_len(0u) == 24, "2.4 GHz IE len 24");
	CHECK(hi5622_he_caps_build_ie(0u, buf, sizeof(buf)) == 24,
	      "2.4 GHz IE built");
	expect_bytes("2.4 GHz HE Capabilities element", buf, want, 24);
	CHECK(buf[24] == 0xaa, "2.4 GHz IE did not overrun");
}

static void test_ie_5g(void)
{
	/* independent transcription of TABLES.md T10 (5 GHz, 28 B) */
	static const u8 want[28] = {
		0xff, 0x1a, 0x23,
		0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00,
		0xfa, 0xff, 0xfa, 0xff, 0xfa, 0xff, 0xfa, 0xff,
	};
	u8 buf[64];

	memset(buf, 0xaa, sizeof(buf));
	CHECK(hi5622_he_caps_ie_len(1u) == 28, "5 GHz IE len 28");
	CHECK(hi5622_he_caps_build_ie(1u, buf, sizeof(buf)) == 28,
	      "5 GHz IE built");
	expect_bytes("5 GHz HE Capabilities element", buf, want, 28);
	CHECK(buf[28] == 0xaa, "5 GHz IE did not overrun");
}

static void test_ie_short_buffer(void)
{
	u8 buf[64];
	unsigned int i;
	int ok = 1;

	memset(buf, 0xaa, sizeof(buf));
	CHECK(hi5622_he_caps_build_ie(0u, buf, 23u) == -ENOSPC,
	      "short buffer refused (2.4 GHz)");
	CHECK(hi5622_he_caps_build_ie(1u, buf, 27u) == -ENOSPC,
	      "short buffer refused (5 GHz)");
	/* No write may happen on refusal. */
	for (i = 0; i < sizeof(buf); i++)
		if (buf[i] != 0xaa)
			ok = 0;
	CHECK(ok, "refused IE build left the buffer untouched");
}

static void test_independence(void)
{
	struct hi5622_he_caps a, b, a2;

	CHECK(hi5622_he_caps_fill(0u, &a) == 0, "fill 2.4 A");
	CHECK(hi5622_he_caps_fill(1u, &b) == 0, "fill 5 A");
	CHECK(hi5622_he_caps_fill(0u, &a2) == 0, "fill 2.4 B");
	CHECK(memcmp(&a, &a2, sizeof(a)) == 0, "2.4 GHz fill deterministic");
	CHECK(a.phy_cap[0] != b.phy_cap[0], "bands differ in PHY width bits");
	CHECK(hi5622_he_caps_fill(0u, &b) == 0, "re-fill same blob");
	CHECK(memcmp(&a, &b, sizeof(a)) == 0, "re-fill identical");
	CHECK(strcmp(hi5622_he_caps_band_name(0u), "2.4GHz") == 0,
	      "2.4 GHz name");
	CHECK(strcmp(hi5622_he_caps_band_name(1u), "5GHz") == 0, "5 GHz name");
}

int main(void)
{
	test_policy();
	test_blob_2g();
	test_blob_5g();
	test_refusals();
	test_ie_2g();
	test_ie_5g();
	test_ie_short_buffer();
	test_independence();
	printf("he_caps_selftest: %u checks, %u failures\n", checks, failures);
	if (failures)
		return 1;
	printf("PASS\n");
	return 0;
}
