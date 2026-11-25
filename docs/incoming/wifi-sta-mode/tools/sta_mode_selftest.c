// SPDX-License-Identifier: GPL-2.0
/*
 * sta_mode_selftest.c - host-only checks for the portable STA kernel-SME
 * logic (lane wifi-sta-mode, shared task task-155).
 *
 * Builds as strict C99 (-Wall -Wextra -Werror -pedantic) against
 * ../src/hi5622_sta_sme.c; no kernel, no device, no vendor bytes.
 */
#include <stdio.h>
#include <string.h>

#include "../src/hi5622_sta_sme.h"

static int checks;
static int fails;

#define CHECK(cond) do { \
	checks++; \
	if (!(cond)) { \
		fails++; \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	} \
} while (0)

static const u8 own[6]   = { 0x02, 0x00, 0x68, 0x69, 0x35, 0x01 };
static const u8 ap[6]    = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
static const u8 ssid[6]  = { 's', 'l', 'o', 'w', 'A', 'P' };
static const u8 ieblob[8] = { 0x00, 0x01, 'x', 0x01, 0x03, 0x82, 0x84, 0x96 };

static void test_state_machine(void)
{
	u32 st;

	CHECK(strcmp(hism_state_name(HISM_OFF), "off") == 0);
	CHECK(strcmp(hism_state_name(HISM_AUTH_SENT), "auth_sent") == 0);
	CHECK(strcmp(hism_state_name(HISM_CONNECTED), "connected") == 0);
	CHECK(strcmp(hism_state_name(HISM_FAILED), "failed") == 0);
	CHECK(strcmp(hism_state_name(99u), "invalid") == 0);

	CHECK(hism_state_pending(HISM_JOIN_POSTED) == 1);
	CHECK(hism_state_pending(HISM_AUTH_SENT) == 1);
	CHECK(hism_state_pending(HISM_AUTH_DONE) == 1);
	CHECK(hism_state_pending(HISM_ASSOC_SENT) == 1);
	CHECK(hism_state_pending(HISM_OFF) == 0);
	CHECK(hism_state_pending(HISM_CONNECTED) == 0);
	CHECK(hism_state_pending(HISM_FAILED) == 0);

	st = HISM_AUTH_SENT;
	CHECK(hism_auth_rsp_apply(&st, HISM_STATUS_SUCCESS) == 0);
	CHECK(st == HISM_AUTH_DONE);
	st = HISM_AUTH_SENT;
	CHECK(hism_auth_rsp_apply(&st, 17u) == 0);
	CHECK(st == HISM_FAILED);
	st = HISM_ASSOC_SENT;
	CHECK(hism_auth_rsp_apply(&st, HISM_STATUS_SUCCESS) != 0);
	CHECK(st == HISM_ASSOC_SENT);
	CHECK(hism_auth_rsp_apply(NULL, 0u) != 0);

	st = HISM_ASSOC_SENT;
	CHECK(hism_assoc_rsp_apply(&st, HISM_STATUS_SUCCESS) == 0);
	CHECK(st == HISM_CONNECTED);
	st = HISM_ASSOC_SENT;
	CHECK(hism_assoc_rsp_apply(&st, 1u) == 0);
	CHECK(st == HISM_FAILED);
	st = HISM_AUTH_SENT;
	CHECK(hism_assoc_rsp_apply(&st, 0u) != 0);
	CHECK(hism_assoc_rsp_apply(NULL, 0u) != 0);

	CHECK(hism_timeout_reason(HISM_AUTH_SENT) == HISM_TIMEOUT_AUTH);
	CHECK(hism_timeout_reason(HISM_ASSOC_SENT) == HISM_TIMEOUT_ASSOC);
	CHECK(hism_timeout_reason(HISM_AUTH_DONE) == HISM_TIMEOUT_ASSOC);
	CHECK(hism_timeout_reason(HISM_JOIN_POSTED) == HISM_TIMEOUT_UNSPECIFIED);
	CHECK(hism_timeout_reason(HISM_OFF) == HISM_TIMEOUT_UNSPECIFIED);
}

static void test_mac_ok(void)
{
	u8 z[6] = { 0, 0, 0, 0, 0, 0 };
	u8 m[6] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };

	CHECK(hism_mac_ok(own) == 1);
	CHECK(hism_mac_ok(z) == 0);
	CHECK(hism_mac_ok(m) == 0);
	CHECK(hism_mac_ok(NULL) == 0);
}

static void test_build_auth(void)
{
	struct hism_auth_req p;
	u8 frame[HISM_AUTH_FRAME_LEN + 4];
	size_t len = 0;

	memset(&p, 0, sizeof(p));
	p.own = own;
	p.bssid = ap;
	p.seq = 5;
	CHECK(hism_build_auth_req(frame, sizeof(frame), &len, &p) == 0);
	CHECK(len == HISM_AUTH_FRAME_LEN);
	CHECK(len == 30u);
	CHECK(frame[0] == 0xb0u);
	CHECK(frame[1] == 0x00u);
	CHECK(frame[2] == 0u && frame[3] == 0u);
	CHECK(memcmp(frame + 4, ap, 6) == 0);	/* addr1 = DA */
	CHECK(memcmp(frame + 10, own, 6) == 0);	/* addr2 = SA */
	CHECK(memcmp(frame + 16, ap, 6) == 0);	/* addr3 = BSSID */
	CHECK(frame[22] == (u8)(5u << 4));
	CHECK(frame[23] == 0u);
	CHECK(frame[24] == 0u && frame[25] == 0u);	/* algo 0 */
	CHECK(frame[26] == 1u && frame[27] == 0u);	/* seq 1 */
	CHECK(frame[28] == 0u && frame[29] == 0u);	/* status 0 */
	CHECK(hism_build_auth_req(frame, 29u, &len, &p) != 0);
	CHECK(hism_build_auth_req(NULL, sizeof(frame), &len, &p) != 0);
	CHECK(hism_build_auth_req(frame, sizeof(frame), NULL, &p) != 0);
	CHECK(hism_build_auth_req(frame, sizeof(frame), &len, NULL) != 0);
	p.own = NULL;
	CHECK(hism_build_auth_req(frame, sizeof(frame), &len, &p) != 0);
}

static void test_build_assoc(void)
{
	struct hism_assoc_req p;
	u8 frame[HISM_MAX_FRAME + 8];
	const u8 *ie = NULL;
	size_t ie_len = 0;
	size_t len = 0;
	size_t expect;

	memset(&p, 0, sizeof(p));
	p.own = own;
	p.bssid = ap;
	p.ssid = ssid;
	p.ssid_len = sizeof(ssid);
	p.privacy = 1;
	p.seq = 9;
	CHECK(hism_build_assoc_req(frame, sizeof(frame), &len, &ie, &ie_len, &p) == 0);
	expect = HISM_ASSOC_HDR_IE_OFF + 2u + sizeof(ssid) + 2u +
		 HISM_RATES_BASIC_LEN + 2u + HISM_RATES_EXT_LEN;
	CHECK(len == expect);
	CHECK(frame[0] == 0x00u);		/* assoc request subtype 0 */
	CHECK(memcmp(frame + 4, ap, 6) == 0);
	CHECK(memcmp(frame + 10, own, 6) == 0);
	CHECK(memcmp(frame + 16, ap, 6) == 0);
	CHECK(frame[22] == (u8)(9u << 4));
	CHECK(frame[24] == (u8)(HISM_CAP_ESS | HISM_CAP_SHORT_PREAMBLE | HISM_CAP_PRIVACY));
	CHECK(frame[25] == 0u);
	CHECK(frame[26] == (u8)HISM_LISTEN_INTERVAL);
	CHECK(frame[27] == 0u);
	CHECK(frame[28] == HISM_EID_SSID);
	CHECK(frame[29] == sizeof(ssid));
	CHECK(memcmp(frame + 30, ssid, sizeof(ssid)) == 0);
	CHECK(ie == frame + HISM_ASSOC_HDR_IE_OFF);
	CHECK(ie_len == len - HISM_ASSOC_HDR_IE_OFF);
	CHECK(ie_len >= 2u + sizeof(ssid));
	CHECK(ie[0] == HISM_EID_SSID);
	/* the IE list must itself parse (ei+2+len walk) */
	{
		size_t off = 0;
		int ok = 1;
		while (off < ie_len) {
			if (ie_len - off < 2u) { ok = 0; break; }
			off += 2u + ie[off + 1u];
		}
		CHECK(ok && off == ie_len);
	}
	p.privacy = 0;
	CHECK(hism_build_assoc_req(frame, sizeof(frame), &len, &ie, &ie_len, &p) == 0);
	CHECK(frame[24] == (u8)(HISM_CAP_ESS | HISM_CAP_SHORT_PREAMBLE));
	p.ssid_len = 0;
	CHECK(hism_build_assoc_req(frame, sizeof(frame), &len, NULL, NULL, &p) != 0);
	p.ssid_len = HISM_MAX_SSID + 1u;
	CHECK(hism_build_assoc_req(frame, sizeof(frame), &len, NULL, NULL, &p) != 0);
	p.ssid_len = sizeof(ssid);
	p.ssid = NULL;
	CHECK(hism_build_assoc_req(frame, sizeof(frame), &len, NULL, NULL, &p) != 0);
	p.ssid = ssid;
	p.bssid = NULL;
	CHECK(hism_build_assoc_req(frame, sizeof(frame), &len, NULL, NULL, &p) != 0);
	CHECK(hism_build_assoc_req(frame, 10u, &len, NULL, NULL, &p) != 0);
}

static size_t mk_auth_resp(u8 *f, u16 status)
{
	memset(f, 0, 30);
	f[0] = 0xb0u;				/* auth */
	memcpy(f + 4, own, 6);			/* addr1 = us */
	memcpy(f + 16, ap, 6);			/* addr3 = BSSID */
	f[24] = 0u; f[25] = 0u;			/* algo 0 */
	f[26] = 2u; f[27] = 0u;			/* seq 2 */
	f[28] = (u8)(status & 0xffu);
	f[29] = (u8)(status >> 8);
	return 30;
}

static size_t mk_assoc_resp(u8 *f, u16 status, const u8 *ies, size_t ies_len)
{
	size_t len = 30 + ies_len;

	memset(f, 0, len);
	f[0] = 0x10u;				/* assoc response */
	memcpy(f + 4, own, 6);
	memcpy(f + 16, ap, 6);
	f[24] = 0x01u; f[25] = 0x00u;		/* capability */
	f[26] = (u8)(status & 0xffu);
	f[27] = (u8)(status >> 8);
	f[28] = 0x01u; f[29] = 0xc0u;		/* AID 0xc001 -> masked */
	if (ies_len)
		memcpy(f + 30, ies, ies_len);
	return len;
}

static void test_decoders(void)
{
	u8 f[600];
	size_t len;
	struct hism_resp r;

	len = mk_auth_resp(f, 0);
	CHECK(hism_decode_auth_resp(f, len, &r) == 0);
	CHECK(r.status == 0u);
	CHECK(r.aid == 0u);
	CHECK(r.ies == NULL && r.ies_len == 0u);
	len = mk_auth_resp(f, 37u);
	CHECK(hism_decode_auth_resp(f, len, &r) == 0);
	CHECK(r.status == 37u);
	CHECK(hism_decode_auth_resp(f, 29u, &r) != 0);
	CHECK(hism_decode_auth_resp(f, len, NULL) != 0);
	f[0] = 0x10u;				/* wrong subtype */
	CHECK(hism_decode_auth_resp(f, len, &r) == -3);
	f[0] = 0xb1u;				/* protocol != 0 */
	CHECK(hism_decode_auth_resp(f, len, &r) != 0);
	f[0] = 0xb4u;				/* type != mgmt */
	CHECK(hism_decode_auth_resp(f, len, &r) != 0);
	CHECK(hism_decode_auth_resp(NULL, 30u, &r) != 0);

	len = mk_assoc_resp(f, 0, ieblob, sizeof(ieblob));
	CHECK(hism_decode_assoc_resp(f, len, &r) == 0);
	CHECK(r.status == 0u);
	CHECK(r.aid == 0x0001u);		/* 0xc001 & 0x3fff */
	CHECK(r.ies == f + 30);
	CHECK(r.ies_len == sizeof(ieblob));
	len = mk_assoc_resp(f, 12u, ieblob, sizeof(ieblob));
	CHECK(hism_decode_assoc_resp(f, len, &r) == 0);
	CHECK(r.status == 12u);
	/* truncated IE -> refused */
	f[30] = 0x01u; f[31] = 0x30u;
	CHECK(hism_decode_assoc_resp(f, 34u, &r) != 0);
	/* odd trailing byte -> refused */
	CHECK(hism_decode_assoc_resp(f, 31u, &r) != 0);
	/* no IEs is legal */
	CHECK(hism_decode_assoc_resp(f, 30u, &r) == 0);
	CHECK(r.ies_len == 0u);
	f[0] = 0xb0u;
	CHECK(hism_decode_assoc_resp(f, 30u, &r) != 0);
	f[0] = 0x10u;
	CHECK(hism_decode_assoc_resp(f, 29u, &r) != 0);
}

static void test_for_us(void)
{
	u8 f[600];
	u8 other[6] = { 0x02, 0x00, 0x68, 0x69, 0x35, 0x02 };
	size_t len = mk_assoc_resp(f, 0, ieblob, sizeof(ieblob));

	CHECK(hism_resp_for_us(f, len, own, ap) == 1);
	CHECK(hism_resp_for_us(f, len, other, ap) == 0);
	CHECK(hism_resp_for_us(f, len, own, own) == 0);
	CHECK(hism_resp_for_us(f, 10u, own, ap) == 0);
	CHECK(hism_resp_for_us(NULL, len, own, ap) == 0);
}

static void test_ie_copy(void)
{
	u8 dst[64];
	size_t n = 0;

	CHECK(hism_ie_copy(dst, sizeof(dst), ieblob, sizeof(ieblob), &n) == 0);
	CHECK(n == sizeof(ieblob));
	CHECK(memcmp(dst, ieblob, sizeof(ieblob)) == 0);
	CHECK(hism_ie_copy(dst, sizeof(dst), NULL, 0u, &n) == 0);
	CHECK(n == 0u);
	CHECK(hism_ie_copy(dst, 2u, ieblob, sizeof(ieblob), &n) != 0);
	CHECK(hism_ie_copy(NULL, 64u, ieblob, sizeof(ieblob), &n) != 0);
}

static void test_sync(void)
{
	struct hism_sync p;
	u8 f[HISM_SYNC_FRAME_LEN];
	size_t len = 0;

	memset(&p, 0, sizeof(p));
	p.status = 0;
	p.bssid = ap;
	p.req_ie_len = 8;
	p.resp_ie_len = 12;
	p.req_ie_va = 0x83187000u;
	p.resp_ie_va = 0x83187040u;
	p.dst = 0;
	p.vap_id = 0;
	CHECK(hism_build_assoc_sync(f, sizeof(f), &len, &p) == 0);
	CHECK(len == HISM_SYNC_FRAME_LEN);
	CHECK(len == 40u);
	CHECK(f[0] == HISM_SYNC_GROUP);
	CHECK(f[1] == 0x01u);			/* alloc nibble 1 */
	CHECK(f[3] == 0u);
	CHECK(f[4] == 40u && f[5] == 0u);	/* total */
	CHECK(f[6] == HISM_SYNC_ID && f[7] == 0u);
	CHECK(f[9] == 0u);
	CHECK(f[10] == 0x5au && f[11] == 0x5au);
	CHECK(f[12] == 0u && f[13] == 0u);	/* status */
	CHECK(memcmp(f + 16, ap, 6) == 0);	/* payload +4 = bssid */
	CHECK(f[24] == 8u);			/* req len @ +0x0c */
	CHECK(f[28] == 12u);			/* resp len @ +0x10 */
	CHECK(f[32] == 0x00u && f[33] == 0x70u && f[34] == 0x18u && f[35] == 0x83u);
	CHECK(f[36] == 0x40u && f[39] == 0x83u);
	p.status = 0;
	p.dst = 0x03u;
	CHECK(hism_build_assoc_sync(f, sizeof(f), &len, &p) == 0);
	CHECK(f[1] == 0x31u);
	CHECK(f[9] == 0x03u);
	p.dst = 0x10u;
	CHECK(hism_build_assoc_sync(f, sizeof(f), &len, &p) != 0);
	p.dst = 0;
	p.req_ie_va = 0u;
	CHECK(hism_build_assoc_sync(f, sizeof(f), &len, &p) != 0);
	p.req_ie_va = 0x83187002u;		/* unaligned */
	CHECK(hism_build_assoc_sync(f, sizeof(f), &len, &p) != 0);
	p.req_ie_va = 0x83187000u;
	p.bssid = NULL;
	CHECK(hism_build_assoc_sync(f, sizeof(f), &len, &p) != 0);
	p.bssid = ap;
	CHECK(hism_build_assoc_sync(f, 39u, &len, &p) != 0);
}

int main(void)
{
	printf("sta_mode_selftest: portable STA kernel-SME logic\n");
	test_state_machine();
	test_mac_ok();
	test_build_auth();
	test_build_assoc();
	test_decoders();
	test_for_us();
	test_ie_copy();
	test_sync();
	printf("checked %d, failed %d\n", checks, fails);
	return fails ? 1 : 0;
}
