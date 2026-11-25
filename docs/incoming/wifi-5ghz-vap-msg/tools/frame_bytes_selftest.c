/* SPDX-License-Identifier: GPL-2.0 */
/*
 * frame_bytes_selftest.c - host byte checks for the two band-sensitive
 * messages of the 5 GHz AP path (lane wifi-5ghz-vap-msg, task-174):
 *
 *   1. the g0 id7 user_add / g0 id8 notify_alg the driver's provider builds
 *      (vapscan_frames.c): 180 B total, 12-byte header + 168-byte payload,
 *      header[3] = mac chip id (0 for both bands) - the vendor layout;
 *   2. the g0 id1 key 0x0064 VAP add (vapstart_frames.c): the band lives in
 *      body+0x12 and the chip (0) in body+0x13, so a band-1 AP VAP add is
 *      well formed while its user add stays on chip 0.
 *
 * Build+run: tools/run-host-tests.sh with the patched driver tree present
 * (built by tools/build-vapmsg5g-ready.sh).  No kernel, no device.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "vapscan_frames.h"
#include "vapstart_frames.h"

static int checks;
static int failures;

#define CHECK(cond) do { \
	checks++; \
	if (!(cond)) { \
		failures++; \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	} \
} while (0)

#define CHECK_EQ(a, b) CHECK((long)(a) == (long)(b))

static const u8 test_mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };

static void test_user_add_vendor_layout(void)
{
	struct vs_frame f;
	struct vs_user_add ua;
	const u8 *pl;

	vs_user_add_defaults(&ua);
	ua.user_id = 1;
	memcpy(ua.mac, test_mac, sizeof(ua.mac));
	ua.chip_id = 0;	/* vendor: mac chip id, both bands on chip 0 */
	ua.vap_id = 0;
	ua.dst = 0;
	CHECK_EQ(vs_build_user_add(&f, &ua), VS_OK);
	/* vendor: hcc_msg_alloc(168) payload -> 12 + 168 = 180 */
	CHECK_EQ(f.len, VS_HDR_LEN + VS_LEN_USER_ADD);
	CHECK_EQ(f.len, 180u);
	CHECK_EQ(vs_frame_validate(f.buf, f.len), VS_OK);
	CHECK_EQ(f.buf[3], 0u);	/* header chip id */
	CHECK_EQ(f.buf[6], 0x07u);	/* hmac_user_add */
	pl = f.buf + VS_HDR_LEN;
	CHECK_EQ(pl[VS_UA_OFF_UID], 1u);
	CHECK_EQ(pl[VS_UA_OFF_UID + 1u], 0u);
	CHECK(!memcmp(pl + VS_UA_OFF_MAC, test_mac, 6));
	CHECK_EQ(pl[VS_UA_OFF_TYPE], 2u);	/* normal user */
	CHECK_EQ((s8)pl[VS_UA_OFF_RSSI], 100);	/* vendor default +100 */
	/* nothing else written: no band/channel/rate field in this message */
	CHECK_EQ(pl[0x08], 0u);
	CHECK_EQ(pl[0xa5], 0u);

	/* The band-as-chip byte is the only difference between the legacy
	 * 5 GHz endpoint encoding (chip 1) and the vendor encoding (chip 0).
	 * Record it so the fire's dmesg/chip id assertion has a host twin. */
	ua.chip_id = 1;
	CHECK_EQ(vs_build_user_add(&f, &ua), VS_OK);
	CHECK_EQ(f.buf[3], 1u);
}

static void test_user_alg_vendor_layout(void)
{
	struct vs_frame f;
	struct vs_user_alg alg;
	const u8 *pl;
	unsigned int i;

	vs_user_alg_defaults(&alg);
	alg.user_id = 1;
	alg.chip_id = 0;
	alg.vap_id = 0;
	alg.dst = 0;
	CHECK_EQ(vs_build_user_alg(&f, &alg), VS_OK);
	CHECK_EQ(f.len, 180u);
	CHECK_EQ(vs_frame_validate(f.buf, f.len), VS_OK);
	CHECK_EQ(f.buf[3], 0u);
	CHECK_EQ(f.buf[6], 0x08u);	/* hmac_user_add_notify_alg */
	pl = f.buf + VS_HDR_LEN;
	CHECK_EQ(pl[VS_ALG_OFF_UID], 1u);
	/* our driver sends the capability blocks zeroed (2.4-proven); the
	 * layout offsets still are the vendor ones T3 records. */
	for (i = 0; i < VS_ALG_LEN_VHT; i++)
		CHECK_EQ(pl[VS_ALG_OFF_VHT + i], 0u);
	for (i = 0; i < VS_ALG_LEN_HT; i++)
		CHECK_EQ(pl[VS_ALG_OFF_HT + i], 0u);
	CHECK_EQ(pl[VS_ALG_OFF_NSS], 0u);
}

static void test_vap_add_band1(void)
{
	struct vsf_frame f;
	struct vsf_vap_add va;
	const u8 *body;

	vsf_vap_add_defaults(&va);
	va.iftype = VSF_IFTYPE_AP;
	va.band = 1;
	CHECK_EQ(vsf_build_vap_add(&f, &va), 0);
	/* g0 id1 config event: 12 hdr + 8 token + 2 key + 2 len + 20 body + 4
	 * pad = 48 total */
	CHECK_EQ(f.len, 48u);
	CHECK_EQ(vsf_frame_validate(&f), 0);
	CHECK_EQ(f.buf[6], 0x01u);	/* g0 id1 cfg event */
	CHECK_EQ(f.buf[20] | (f.buf[21] << 8), VSF_KEY_VAP_ADD);
	CHECK_EQ(f.buf[22] | (f.buf[23] << 8), 20u);
	body = f.buf + 24;
	CHECK_EQ(body[0], VSF_IFTYPE_AP);
	CHECK_EQ(body[0x12], 1u);	/* band id */
	CHECK_EQ(body[0x13], 0u);	/* chip id: both bands on chip 0 */

	/* band 0 keeps the same frame with body[0x12] = 0. */
	va.band = 0;
	CHECK_EQ(vsf_build_vap_add(&f, &va), 0);
	body = f.buf + 24;
	CHECK_EQ(body[0x12], 0u);
	CHECK_EQ(body[0x13], 0u);

	/* Out-of-range band is refused (fail closed). */
	va.band = 2;
	CHECK_EQ(vsf_build_vap_add(&f, &va), -EINVAL);
}

int main(void)
{
	test_user_add_vendor_layout();
	test_user_alg_vendor_layout();
	test_vap_add_band1();
	printf("%s: %d checks, %d failure(s)\n",
	       failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
