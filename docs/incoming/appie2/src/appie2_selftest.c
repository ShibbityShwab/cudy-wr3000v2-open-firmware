/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * appie2_selftest.c - host KAT for the WEXT set_vendor_ie call-order replica.
 * Lane appie2 (task-244), host-only. No kernel, no device, no vendor bytes.
 */
#include "appie2_replica.h"

#include <stdio.h>
#include <string.h>

static int g_pass;
static int g_fail;

#define CHECK(cond, name)                                                          \
	do {                                                                        \
		if (cond) {                                                         \
			g_pass++;                                                   \
			printf("PASS %2d %s\n", g_pass + g_fail, name);            \
		} else {                                                            \
			g_fail++;                                                   \
			printf("FAIL %2d %s\n", g_pass + g_fail, name);            \
		}                                                                   \
	} while (0)

#define CHECK_EQ(a, b, name) CHECK((a) == (b), name)

/* userspace-visible app IE blob: one 0x7f IE followed by two 0xdd IEs */
static const uint8_t k_ie_blob[] = {
	0x7f, 0x03, 0xaa, 0xbb, 0xcc,
	0xdd, 0x04, 0x00, 0x11, 0x22, 0x33,
	0xdd, 0x02, 0x44, 0x55,
};
#define IE_BLOB_LEN (sizeof(k_ie_blob))

static size_t build_userbuf(uint8_t *buf, size_t cap, uint32_t cmd, uint32_t ie_len,
			    uint8_t type, const uint8_t *ie, size_t ie_avail)
{
	size_t need = 12u + (size_t)ie_len;

	if (need > cap)
		return 0;
	memset(buf, 0, need);
	buf[0] = (uint8_t)cmd;
	buf[1] = (uint8_t)(cmd >> 8);
	buf[2] = (uint8_t)(cmd >> 16);
	buf[3] = (uint8_t)(cmd >> 24);
	buf[4] = (uint8_t)ie_len;
	buf[5] = (uint8_t)(ie_len >> 8);
	buf[6] = (uint8_t)(ie_len >> 16);
	buf[7] = (uint8_t)(ie_len >> 24);
	buf[8] = type;
	if (ie && ie_len && ie_avail >= ie_len)
		memcpy(buf + 12, ie, ie_len);
	return need;
}

static void test_parse(void)
{
	uint8_t ub[1024];
	struct aiw_wext_req req;
	size_t n;
	enum aiw_err err;

	n = build_userbuf(ub, sizeof(ub), AIW_WEXT_CMD, (uint32_t)IE_BLOB_LEN, 4u,
			  k_ie_blob, sizeof(k_ie_blob));
	err = aiw_wext_parse(ub, n, &req);
	CHECK_EQ(err, AIW_OK, "parse: valid 0x8eec request accepted");
	CHECK_EQ(req.ie_len, IE_BLOB_LEN, "parse: ie_len carried");
	CHECK_EQ(req.type, 4u, "parse: type carried");
	CHECK_EQ(req.mode, 0u, "parse: mode is 0 on the WEXT path");

	n = build_userbuf(ub, sizeof(ub), 0x8ee3u, 0, 4u, NULL, 0);
	err = aiw_wext_parse(ub, n, &req);
	CHECK_EQ(err, AIW_EINVAL, "parse: wrong cmd 0x8ee3 refused");

	n = build_userbuf(ub, sizeof(ub), AIW_WEXT_CMD, 497u, 4u, NULL, 0);
	err = aiw_wext_parse(ub, n, &req);
	CHECK_EQ(err, AIW_ERANGE, "parse: ie_len 497 refused (496-byte field)");

	{
		uint8_t big[64];

		memset(big, 0xdd, sizeof(big));
		n = build_userbuf(ub, sizeof(ub), AIW_WEXT_CMD, 32u, 20u, big, sizeof(big));
	}
	err = aiw_wext_parse(ub, n, &req);
	CHECK_EQ(err, AIW_ERANGE, "parse: type 20 refused (slot table is 0..19)");

	err = aiw_wext_parse(ub, 11u, &req);
	CHECK_EQ(err, AIW_EINVAL, "parse: short userspace buffer refused");
}

static void test_strip_and_body(void)
{
	struct aiw_wext_req req;
	struct aiw_cfg_body body;
	size_t stripped = 0;
	enum aiw_err err;

	err = aiw_wext_parse(k_ie_blob, 0, &req);	/* bogus: just to prove refusal */
	CHECK(err != AIW_OK, "strip: empty userspace arg refused");

	{
		uint8_t ub[1024];
		size_t n = build_userbuf(ub, sizeof(ub), AIW_WEXT_CMD,
					 (uint32_t)IE_BLOB_LEN, 4u, k_ie_blob,
					 sizeof(k_ie_blob));

		err = aiw_wext_parse(ub, n, &req);
		CHECK_EQ(err, AIW_OK, "strip: request parsed for the stripped fixture");
	}
	err = aiw_strip_7f(&req, &stripped);
	CHECK_EQ(err, AIW_OK, "strip: 0x7f IE found and removed");
	CHECK_EQ(stripped, 5u, "strip: 0x7f IE length 5 (2 + 3)");
	CHECK_EQ(req.ie_len, IE_BLOB_LEN - 5u, "strip: ie_len reduced by the 0x7f IE");
	CHECK_EQ(req.ie[0], 0xddu, "strip: first remaining IE is the 0xdd IE");

	err = aiw_cfg_body_build(&req, &body);
	CHECK_EQ(err, AIW_OK, "body: built");
	CHECK_EQ((unsigned)body.raw[0] | ((unsigned)body.raw[1] << 8),
		 AIW_EVENT_SET_VENDOR_IE, "body: u16 event = 0x0276 SET_VENDOR_IE");
	CHECK_EQ((unsigned)body.raw[2] | ((unsigned)body.raw[3] << 8),
		 AIW_EVENT_PAYLOAD_LEN, "body: u16 len = 0x01f8");
	CHECK_EQ((unsigned)body.raw[4], req.ie_len & 0xffu, "body: ie_len low byte");
	CHECK_EQ(body.raw[8], 4u, "body: type carried");
	CHECK_EQ(body.raw[9], 0u, "body: mode 0");
	CHECK_EQ(body.raw[10], 0u, "body: pad byte zero");
	CHECK_EQ(body.raw[11], 0u, "body: pad byte zero");
	CHECK_EQ(body.raw[12], req.ie[0], "body: IE blob starts at offset 12");
}

static void test_envelope(void)
{
	struct aiw_wext_req req;
	struct aiw_cfg_body body;
	struct aiw_envelope env;
	struct aiw_ctx ctx = { { 0x11u, 0x22u, 0x33u } };
	uint8_t ub[1024];
	size_t n;
	enum aiw_err err;

	n = build_userbuf(ub, sizeof(ub), AIW_WEXT_CMD, (uint32_t)IE_BLOB_LEN, 4u,
			  k_ie_blob, sizeof(k_ie_blob));
	err = aiw_wext_parse(ub, n, &req);
	CHECK_EQ(err, AIW_OK, "envelope: request parsed");
	err = aiw_cfg_body_build(&req, &body);
	CHECK_EQ(err, AIW_OK, "envelope: body built without the 0x7f strip step");
	err = aiw_envelope_build(&body, &ctx, 0x11223344u, &env);
	CHECK_EQ(err, AIW_OK, "envelope: built");
	CHECK_EQ(env.raw[0] & 0x0fu, AIW_ENV_GROUP, "envelope: group 2 in the low nibble");
	CHECK_EQ(env.raw[6], 0u, "envelope: id low byte 0");
	CHECK_EQ(env.raw[7], 0u, "envelope: id high byte 0");
	CHECK_EQ((unsigned)env.raw[10] | ((unsigned)env.raw[11] << 8), AIW_ENV_TAG,
		 "envelope: 0x5a5a HCC tag");
	CHECK_EQ(env.raw[3], 0x11u, "envelope: hdr[3] = ctx[0]");
	CHECK_EQ(env.raw[2], 0x22u, "envelope: hdr[2] = ctx[1]");
	CHECK_EQ(env.raw[1] & 0xf0u, 0x30u, "envelope: hdr[1] high nibble = ctx[2] & 0xf");
	CHECK_EQ(env.raw[16], 0x01u, "envelope: payload rpc tag 1");
	CHECK_EQ((unsigned)env.raw[22] | ((unsigned)env.raw[23] << 8), AIW_CFG_BODY_LEN,
		 "envelope: payload msg_len 0x01fc");
	CHECK_EQ(env.raw[24], 0x44u, "envelope: sequence byte 0");
	CHECK_EQ(env.raw[27], 0x11u, "envelope: sequence byte 3");
	CHECK_EQ(memcmp(env.raw + AIW_ENV_HDR_LEN + AIW_ENV_RPC_LEN, body.raw,
			AIW_CFG_BODY_LEN), 0,
		 "envelope: 508-byte body carried at hdr+28");
	CHECK_EQ(sizeof(env.raw), AIW_ENV_TOTAL_LEN, "envelope: fixed total length 536");
}

static void test_dispatch(void)
{
	enum aiw_handler h = AIW_H_NONE;
	enum aiw_err err;

	err = aiw_dispatch(g_aiw_vendor_rows, g_aiw_vendor_rows_len, AIW_EVENT_SET_VENDOR_IE,
			   3u, &h);
	CHECK_EQ(err, AIW_OK, "dispatch: event 0x0276 resolves in mode 3");
	CHECK_EQ(h, AIW_H_SET_VENDOR_IE, "dispatch: mode 3 slot is set_vendor_ie");

	err = aiw_dispatch(g_aiw_vendor_rows, g_aiw_vendor_rows_len, AIW_EVENT_SET_VENDOR_IE,
			   4u, &h);
	CHECK_EQ(err, AIW_ENOENT, "dispatch: mode 4 slot is empty -> fail closed");

	err = aiw_dispatch(g_aiw_vendor_rows, g_aiw_vendor_rows_len, 0x0277u, 4u, &h);
	CHECK_EQ(err, AIW_OK, "dispatch: decoy row with both slots, mode 4 resolves");
	CHECK_EQ(h, AIW_H_DECOY_MODE4, "dispatch: mode 4 chooses the +4 slot");

	err = aiw_dispatch(g_aiw_vendor_rows, g_aiw_vendor_rows_len, 0x02deu, 3u, &h);
	CHECK_EQ(h, AIW_H_GET_STA_11H, "dispatch: neighbouring 0x02de row maps to its handler");

	err = aiw_dispatch(g_aiw_vendor_rows, g_aiw_vendor_rows_len, 0x0999u, 3u, &h);
	CHECK_EQ(err, AIW_ENOENT, "dispatch: unknown event id refused");

	err = aiw_dispatch(g_aiw_vendor_rows, g_aiw_vendor_rows_len, AIW_EVENT_SET_VENDOR_IE,
			   5u, &h);
	CHECK_EQ(err, AIW_EINVAL, "dispatch: unknown mode refused");
}

static void test_sequence(void)
{
	uint8_t ub[1024];
	size_t n;
	struct aiw_ctx ctx = { { 0x01u, 0x02u, 0x03u } };
	struct aiw_trace trace;
	enum aiw_handler handler = AIW_H_NONE;
	enum aiw_err err;

	n = build_userbuf(ub, sizeof(ub), AIW_WEXT_CMD, (uint32_t)IE_BLOB_LEN, 4u,
			  k_ie_blob, sizeof(k_ie_blob));
	err = aiw_run_vendor_sequence(ub, n, &ctx, 7u, &trace, &handler);
	CHECK_EQ(err, AIW_OK, "sequence: full vendor call order runs");
	CHECK_EQ(handler, AIW_H_SET_VENDOR_IE, "sequence: dispatched to set_vendor_ie");
	CHECK_EQ(trace.mirror_saves, 1u, "sequence: exactly one host-mirror save");
	CHECK_EQ(trace.h2d_posts, 1u, "sequence: exactly one H2D app-IE post");
	CHECK_EQ(trace.mirror_before_post, 1u, "sequence: mirror save precedes the H2D post");
	CHECK_EQ(trace.envelope_tx, 0u,
		 "sequence: the group-2/id-0 envelope is host-internal (never tx'd)");

	n = build_userbuf(ub, sizeof(ub), AIW_WEXT_CMD, 4u, 4u, k_ie_blob + 5, 4u);
	err = aiw_run_vendor_sequence(ub, n, &ctx, 8u, &trace, &handler);
	CHECK_EQ(err, AIW_EINVAL, "sequence: malformed IE blob refused before any effect");
	CHECK_EQ(trace.h2d_posts, 0u, "sequence: refusal leaves the H2D counter at 0");
}

static void test_determinism(void)
{
	uint8_t ub[1024];
	struct aiw_cfg_body b1;
	struct aiw_cfg_body b2;
	struct aiw_envelope e1;
	struct aiw_envelope e2;
	struct aiw_ctx ctx = { { 0x0au, 0x0bu, 0x0cu } };
	struct aiw_wext_req req;
	size_t n;
	enum aiw_err err;

	n = build_userbuf(ub, sizeof(ub), AIW_WEXT_CMD, (uint32_t)IE_BLOB_LEN, 9u,
			  k_ie_blob, sizeof(k_ie_blob));
	err = aiw_wext_parse(ub, n, &req);
	CHECK_EQ(err, AIW_OK, "determinism: request parsed");
	(void)aiw_cfg_body_build(&req, &b1);
	(void)aiw_cfg_body_build(&req, &b2);
	CHECK_EQ(memcmp(b1.raw, b2.raw, AIW_CFG_BODY_LEN), 0,
		 "determinism: identical body on repeat builds");
	(void)aiw_envelope_build(&b1, &ctx, 42u, &e1);
	(void)aiw_envelope_build(&b1, &ctx, 42u, &e2);
	CHECK_EQ(memcmp(e1.raw, e2.raw, AIW_ENV_TOTAL_LEN), 0,
		 "determinism: identical envelope on repeat builds");
	(void)aiw_envelope_build(&b1, &ctx, 43u, &e2);
	CHECK(memcmp(e1.raw, e2.raw, AIW_ENV_TOTAL_LEN) != 0,
	      "determinism: sequence change alters the envelope");
}

int main(void)
{
	printf("appie2 replica selftest (host-only, no kernel, no device)\n");
	printf("model: WEXT 0x8eec -> cfg event 0x0276 -> g2/id0 host envelope -> dispatch -> mirror+post\n");
	test_parse();
	test_strip_and_body();
	test_envelope();
	test_dispatch();
	test_sequence();
	test_determinism();
	printf("----\n%d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
