// SPDX-License-Identifier: GPL-2.0
/*
 * h2d_selftest.c - device-free known-answer + refusal test suite and a small
 * frame CLI for the wifi-h2d-frames library.
 *
 * ORIGINAL clean-room code by the wifi-h2d-frames lane (task-93, 2026-11-24).
 * Needs no device, no kernel tree and no vendor artifact: every vector is a
 * derived constant published in the committed lane reports.
 *
 *   cc -std=c99 -Wall -Wextra -Werror -pedantic -o h2d_selftest \
 *      h2d_selftest.c h2d_frames.c
 *   ./h2d_selftest            # run the suite
 *   ./h2d_selftest catalogue  # dump the generated catalogue
 *   ./h2d_selftest --help     # everything else
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "h2d_frames.h"

static int g_checks;
static int g_failures;

static void check(int cond, const char *name)
{
	g_checks++;
	if (!cond) {
		g_failures++;
		printf("FAIL  %s\n", name);
	}
}

static void check_eq_int(long got, long want, const char *name)
{
	g_checks++;
	if (got != want) {
		g_failures++;
		printf("FAIL  %s: got %ld want %ld\n", name, got, want);
	}
}

/* ---------------------------- hex helpers ---------------------------- */
static int hex_nibble(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int hex2bin(const char *hex, u8 *out, size_t cap, size_t *out_len)
{
	size_t n = 0;
	const char *p = hex;

	while (*p) {
		int hi, lo;

		if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' ||
		    *p == ',' || *p == ':') {
			p++;
			continue;
		}
		if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X') &&
		    hex_nibble((unsigned char)p[2]) >= 0) {
			p += 2;
			continue;
		}
		hi = hex_nibble((unsigned char)p[0]);
		lo = hex_nibble((unsigned char)p[1]);
		if (hi < 0 || lo < 0 || n >= cap)
			return -1;
		out[n++] = (u8)((hi << 4) | lo);
		p += 2;
	}
	*out_len = n;
	return 0;
}

static void print_hex(const u8 *buf, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++)
		printf("%02x", buf[i]);
}

static void check_hex(const u8 *got, size_t got_len, const char *want_hex,
		      const char *name)
{
	u8 want[4096];
	size_t want_len = 0;
	int ok;

	ok = hex2bin(want_hex, want, sizeof(want), &want_len) == 0 &&
	     want_len == got_len && memcmp(got, want, got_len) == 0;
	g_checks++;
	if (!ok) {
		g_failures++;
		printf("FAIL  %s: got ", name);
		print_hex(got, got_len);
		printf(" want %s\n", want_hex);
	}
}

/* ------------------------------ vectors ------------------------------ */
/* Group 0 id 0 payload-free stub, the only safe bare H2D probe. */
#define V_STUB		"000100000c00000000005a5a"

/* Group 0 id 0x1d hmac_edca_opt_timeout_fn: 12-byte header + 0x3c zeros
 * (the vendor live steady-state frame, phase38 sec 6 "message A"). */
#define V_MSG_A		"0001000448001d0000005a5a" \
			"0000000000000000000000000000000000000000" \
			"0000000000000000000000000000000000000000" \
			"0000000000000000000000000000000000000000"

/* Group 0 id 1 first post-ready TXQ config sync, 72-byte vendor post buffer:
 * declared total 0x30, buffer padded with 0xff past the payload
 * (phase38 sec 2 "message B"; token {0,0,0,0,1,0,0,0}, cmd 0x00d8, len 0x14,
 * 20-byte payload {01 00 00 00, 0...}). */
#define V_MSG_B		"000100043000010000005a5a" \
			"0000000001000000d8001400" \
			"0100000000000000000000000000000000000000" \
			"ffffffffffffffffffffffffffffffff" \
			"ffffffffffffffffffffffff"

/* D2H ready pair (wifi-ete-frames sec 2; d2h-direct bench ledger 2026-11-24). */
#define V_D2H_ID1	"040000000c00010000005a5a"
#define V_D2H_ID2	"040000001000020000 005a5a50080601"

static const u8 token_hdpp[8] = { 0x00, 0x00, 0x00, 0x00,
				  0x01, 0x00, 0x00, 0x00 };
static const u8 payload_hdpp[20] = { 0x01, 0x00, 0x00, 0x00 };

/* --------------------------- test sections --------------------------- */
static void test_constants(void)
{
	check_eq_int(H2D_HDR_LEN, 12, "header length 12");
	check_eq_int(H2D_TAG, 0x5a5a, "transport tag 0x5a5a");
	check_eq_int(H2D_MAX_PAYLOAD, 0x674, "payload bound 0x674");
	check_eq_int(H2D_MAX_TOTAL, 0x680, "total bound 0x680");
	check_eq_int(H2D_NODE_FLAGS, 0x6d2b, "node flags 0x6d2b");
	check_eq_int(H2D_RING_DEPTH, 32, "vendor ring depth 32");
	check_eq_int(H2D_MBOX_ID_ETE_ANNOUNCE, 3, "SR announce mailbox id 3");
	check_eq_int(H2D_MBOX_ID_BUGON, 5, "mailbox id 5 is the BUG trap");
	check(strcmp(h2d_strerror(H2D_ETAG), h2d_strerror(H2D_ELEN)) != 0,
	      "error names distinct");
	check(strcmp(h2d_risk_name(H2D_RISK_PTR_DEREF), "PTR_DEREF") == 0,
	      "risk name PTR_DEREF");
}

static void test_catalogue(void)
{
	const struct h2d_vendor_sender *vs;
	const struct h2d_fw_handler *fw;
	const struct h2d_fw_handler *h;
	size_t nv = 0, nf = 0, i, null_slots = 0, no_handler = 0, exact = 0;

	vs = h2d_vendor_senders_get(&nv);
	fw = h2d_fw_handlers_get(&nf);
	check(vs != NULL && nv == 48, "catalogue: 48 vendor senders");
	check(fw != NULL && nf == 60, "catalogue: 60 firmware receive slots");

	h = h2d_fw_handler_lookup(0, 0x00);
	check(h != NULL && h->handler_ca == 0x00081695u,
	      "catalogue: g0 id0 stub handler CA");
	h = h2d_fw_handler_lookup(0, 0x01);
	check(h != NULL && h->handler_ca == 0x00043d59u,
	      "catalogue: g0 id1 config handler CA");
	h = h2d_fw_handler_lookup(2, 0x2a);
	check(h != NULL && h->handler_ca == 0x0008f669u,
	      "catalogue: g2 id0x2a handler CA");
	check(h2d_fw_handler_lookup(1, 0x0c) == NULL,
	      "catalogue: no group-1 H2D table");
	check(h2d_fw_handler_lookup(4, 0x01) == NULL,
	      "catalogue: no group-4 H2D table");
	h = h2d_fw_handler_lookup(0, 0x14);
	check(h != NULL && h->handler_ca == 0, "catalogue: g0 id0x14 NULL slot");
	h = h2d_fw_handler_lookup(2, 0x0c);
	check(h != NULL && h->handler_ca == 0, "catalogue: g2 id0x0c NULL slot");

	for (i = 0; i < nf; i++) {
		if (fw[i].handler_ca == 0)
			null_slots++;
		else
			check(fw[i].risk != H2D_RISK_UNKNOWN,
			      "catalogue: every non-NULL handler has a risk");
	}
	check_eq_int((long)null_slots, 2, "catalogue: two NULL receive slots");

	for (i = 0; i < nv; i++) {
		if (vs[i].flags & H2D_SND_BUILDER_EXACT)
			exact++;
		if (!(vs[i].flags & H2D_SND_HAS_HANDLER))
			no_handler++;
	}
	check_eq_int((long)exact, 5, "catalogue: five exact-builder rows");
	check_eq_int((long)no_handler, 3,
		     "catalogue: three vendor senders without handler");
}

static void test_safe_probe_and_header(void)
{
	struct h2d_frame f;
	struct h2d_frame g;
	u8 buf[H2D_MAX_TOTAL];
	size_t n = 0;
	enum h2d_err err = H2D_EARG;

	check(h2d_build_safe_probe(&f, 0) == 0, "stub: builds");
	check_eq_int(f.group, 0, "stub: group 0");
	check_eq_int(f.id, 0, "stub: id 0");
	check_eq_int(f.payload_len, 0, "stub: no payload");
	check(h2d_frame_encode(&f, buf, sizeof(buf), &n, &err) == 0,
	      "stub: encodes");
	check_eq_int((long)n, 12, "stub: total 12");
	check_hex(buf, n, V_STUB, "stub: exact wire bytes");
	check_eq_int(buf[H2D_DST_OFF], 0, "stub: hdr[9] dst");
	check_eq_int(buf[H2D_BID_OFF] & 0x0fu, 1, "stub: alloc nibble 1");
	check_eq_int(buf[H2D_BID_OFF] >> 4, 0, "stub: hdr[1] high nibble");
	check_eq_int(buf[H2D_TAG_OFF] | (buf[H2D_TAG_OFF + 1] << 8), H2D_TAG,
		     "stub: tag bytes");
	check(h2d_frame_decode(buf, n, &g, H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
			       H2D_VF_HANDLER | H2D_VF_POLICY, &err) == 0,
	      "stub: policy decode");
	check_eq_int(g.group, f.group, "stub: decode group");
	check_eq_int(g.id, f.id, "stub: decode id");
	check_eq_int(g.payload_len, 0, "stub: decode payload len");
}

static void test_exact_builders(void)
{
	struct h2d_frame f, a;
	u8 buf[H2D_MAX_TOTAL];
	u8 cap[96];
	static const u8 addr[4] = { 0xaa, 0xbb, 0xcc, 0xdd };
	static const u8 tail[4] = { 0xff, 0xff, 0xff, 0xff };
	static const u8 data4[4] = { 0xde, 0xad, 0xbe, 0xef };
	static const u8 tok8[8] = { 0, 0, 0, 0, 1, 0, 0, 0 };
	size_t n = 0;
	enum h2d_err err = H2D_EARG;

	/* Message A: hmac_edca_opt_timeout_fn, zero 0x3c payload. */
	check(h2d_build_g0_id1d_edca_opt(&f, 0, 0, 4, 0, &err) == 0,
	      "msgA: builds");
	check(h2d_frame_encode(&f, buf, sizeof(buf), &n, &err) == 0,
	      "msgA: encodes");
	check_eq_int((long)n, 0x48, "msgA: total 0x48");
	check_hex(buf, n, V_MSG_A, "msgA: exact live capture bytes");

	/* Message B: the vendor's first post-ready g0 id1 frame, 72-byte post. */
	check(h2d_build_g0_id1_hdpp_txq_sync(&f, 0, 0, 4, token_hdpp,
					    0x00d8, payload_hdpp, 20, tail,
					    &err) == 0, "msgB: builds");
	check_eq_int(f.payload_len, 0x24, "msgB: payload 0x24");
	check(h2d_frame_export(&f, cap, sizeof(cap), 72, 0xff, &n, &err) == 0,
	      "msgB: exports 72-byte post buffer");
	check_eq_int((long)n, 72, "msgB: post buffer 72");
	check_hex(cap, n, V_MSG_B, "msgB: exact phase-38 capture bytes");

	/* msgB declared header is 0x30: node length uses the frame total. */
	{
		struct h2d_sr_plan p;

		check(h2d_sr_plan(&f, 0, H2D_RING_DEPTH, 0x84543000u, &p,
				  &err) == 0, "msgB: SR plan");
		check_eq_int(p.node_word1, (0x30 << 16) | 0x6d2b,
			     "msgB: node word1 (0x30<<16)|0x6d2b");
	}

	/* Group 0 id 2 shared config-event shape (hmac_config_alg_send_event). */
	check(h2d_build_g0_cfg_event(&f, 2, 0, 1, 2, 3, tok8, 0x0102,
				     data4, 4, NULL, &err) == 0,
	      "g0id2: builds");
	check(h2d_frame_encode(&f, buf, sizeof(buf), &n, &err) == 0,
	      "g0id2: encodes");
	check_hex(buf, n,
		  "000101022000020003 005a5a"
		  "000000000100000002010400deadbeef00000000"
		  , "g0id2: exact bytes");

	/* Group 0 id 9 hmac_del_user_notify_dmac: two decoded shapes. */
	check(h2d_build_g0_id09_del_user(&f, 0, 0x0fffu, addr, &err) == 0,
	      "deluser: builds with addr");
	check(h2d_frame_encode(&f, buf, sizeof(buf), &n, &err) == 0,
	      "deluser: encodes");
	check_hex(buf, n,
		  "000100001800090000005a5a"
		  "ff0faabbccdd000000000000", "deluser: exact bytes");
	check(h2d_build_g0_id09_del_user(&f, 0, 0x1234u, NULL, &err) == 0,
	      "deluser: builds without addr");
	check(h2d_frame_encode(&f, buf, sizeof(buf), &n, &err) == 0,
	      "deluser: encodes 2");
	check_hex(buf, n,
		  "000100001800090000005a5a"
		  "341200000000000000000000", "deluser: exact bytes 2");

	/* Import of the captured 72-byte msgB post buffer (declared total 48). */
	check_hex(cap, 72, V_MSG_B, "msgB: buffer kept for import");
	check(hex2bin(V_MSG_B, buf, sizeof(buf), &n) == 0, "msgB: vector decodes");
	check(h2d_frame_import(buf, n, &a, &err) == 0, "msgB: import");
	check_eq_int(a.payload_len, 0x24, "msgB: import payload 0x24");
	check_eq_int(a.id, 1, "msgB: import id 1");
	check_eq_int(a.policy, H2D_F_CAPTURED, "msgB: import captured policy");
}

static void test_refusals(void)
{
	struct h2d_frame f;
	u8 buf[H2D_MAX_TOTAL];
	static u8 big[H2D_MAX_PAYLOAD];
	size_t n = 0;
	enum h2d_err err = H2D_EARG;

	check(h2d_build_g0_cfg_event(&f, 3, 0, 0, 0, 0, NULL, 0, NULL, 0,
				     NULL, &err) != 0 && err == H2D_EARG,
	      "refuse: cfg-event id other than 1/2");

	check(h2d_build_raw(&f, 0, 0x07, 0, 0, 0, 0, big, 8, H2D_F_EXACT,
			    &err) != 0 && err == H2D_EARG,
	      "refuse: fixed-size vendor payload mismatch (g0 id7 needs 0xa8)");

	check(h2d_build_raw(&f, 1, 0x0c, 0, 0, 0, 0, NULL, 0, 0, &err) != 0 &&
	      err == H2D_EGROUP, "refuse: group 1 has no H2D table");
	check(h2d_build_raw(&f, 3, 0x00, 0, 0, 0, 0, NULL, 0, 0, &err) != 0 &&
	      err == H2D_EGROUP, "refuse: group 3 has no H2D table");
	check(h2d_build_raw(&f, 4, 0x01, 0, 0, 0, 0, NULL, 0, 0, &err) != 0 &&
	      err == H2D_EGROUP, "refuse: group 4 is device->host only");
	check(h2d_build_raw(&f, 0, 0x00, 0x10, 0, 0, 0, NULL, 0, 0,
			    &err) != 0 && err == H2D_EBOUNDS,
	      "refuse: dst > 0xf");
	check(h2d_build_raw(&f, 0, 0x01, 0, 0, 0, 0, big,
			    H2D_MAX_PAYLOAD + 1, H2D_F_CAPTURED, &err) != 0 &&
	      err == H2D_EBOUNDS, "refuse: payload 0x675");

	/* Boundary: payload exactly 0x674 is representable by a capture. */
	check(h2d_build_raw(&f, 0, 0x01, 0, 0, 0, 0, big, H2D_MAX_PAYLOAD,
			    H2D_F_CAPTURED, &err) == 0,
	      "accept: payload 0x674 (captured)");
	check_eq_int(f.payload_len, H2D_MAX_PAYLOAD, "accept: 0x674 length");
	check(h2d_frame_encode(&f, buf, sizeof(buf), &n, &err) == 0,
	      "accept: 0x674 encodes (total 0x680)");
	check_eq_int((long)n, 0x680, "accept: total 0x680");

	/* Policy: pointer-reading handlers need exact/captured bytes. */
	{
		struct h2d_frame p;

		memset(&p, 0, sizeof(p));
		p.group = 0;
		p.id = 1;
		p.payload_len = 4;
		check(h2d_frame_encode(&p, buf, sizeof(buf), &n, &err) != 0 &&
		      err == H2D_EPOLICY,
		      "refuse: synthesized payload to PTR_DEREF handler");
	}
	check(h2d_build_raw(&f, 0, 0x00, 0, 0, 0, 0, NULL, 0, 0, &err) == 0,
	      "accept: payload-free NO_ACCESS probe (g0 id0)");

	/* HCC group-2 id 5 exists in the vendor catalogue but is lane-refused. */
	check(h2d_build_raw(&f, 2, 0x05, 0, 0, 0, 0, big, 0x10, H2D_F_CAPTURED,
			    &err) != 0 && err == H2D_EPOLICY,
	      "refuse: HCC id5 without explicit override");
	check(h2d_build_raw(&f, 2, 0x05, 0, 0, 0, 0, big, 0x10,
			    H2D_F_CAPTURED | H2D_F_ALLOW_ID5_HCC, &err) == 0,
	      "accept: HCC id5 with explicit override");
	check(h2d_build_raw(&f, 0, 0x04, 0, 0, 0, 0, NULL, 0, 0, &err) != 0 &&
	      err == H2D_ENOHANDLER, "refuse: g0 id4 unregistered");
	check(h2d_build_raw(&f, 0, 0x14, 0, 0, 0, 0, NULL, 0, 0, &err) != 0 &&
	      err == H2D_ENOHANDLER, "refuse: g0 id0x14 NULL handler");
	check(h2d_build_raw(&f, 2, 0x0c, 0, 0, 0, 0, NULL, 0, 0, &err) != 0 &&
	      err == H2D_ENOHANDLER, "refuse: g2 id0x0c NULL handler");

	/* Encoder capacity refusal. */
	check(h2d_build_safe_probe(&f, 0) == 0 &&
	      h2d_frame_encode(&f, buf, 11, &n, &err) != 0 &&
	      err == H2D_EBUFFER, "refuse: caller buffer too small");

	/* del-user builder argument matrix. */
	check(h2d_build_g0_id09_del_user(&f, 0, 0x0fffu, NULL, &err) != 0 &&
	      err == H2D_EARG, "refuse: deluser 0xfff without addr");
	{
		static const u8 addr[4] = { 1, 2, 3, 4 };

		check(h2d_build_g0_id09_del_user(&f, 0, 0x0001u, addr,
						 &err) != 0 &&
		      err == H2D_EARG,
		      "refuse: deluser non-0xfff with addr");
	}
}

static void test_decode_refusals(void)
{
	struct h2d_frame f;
	u8 buf[H2D_MAX_TOTAL + 8];
	size_t n = 0;
	enum h2d_err err = H2D_EARG;

	/* Start from the known-good stub bytes, then corrupt one rule at a time. */
	check(hex2bin(V_STUB, buf, sizeof(buf), &n) == 0, "decode: vector");

	buf[H2D_TAG_OFF] ^= 0xffu;
	check(h2d_frame_decode(buf, n, &f, H2D_VF_TRANSPORT, &err) != 0 &&
	      err == H2D_ETAG, "decode: bad tag");
	check(hex2bin(V_STUB, buf, sizeof(buf), &n) == 0, "decode: vector 2");

	buf[H2D_LEN_OFF] = 0;
	buf[H2D_LEN_OFF + 1] = 0;
	check(h2d_frame_decode(buf, n, &f, H2D_VF_TRANSPORT, &err) != 0 &&
	      err == H2D_ELEN, "decode: zero total length");
	check(hex2bin(V_STUB, buf, sizeof(buf), &n) == 0, "decode: vector 3");

	buf[H2D_LEN_OFF] = 0x0d;
	check(h2d_frame_decode(buf, n, &f, H2D_VF_TRANSPORT, &err) != 0 &&
	      err == H2D_ELEN, "decode: declared length != buffer length");
	check(hex2bin(V_STUB, buf, sizeof(buf), &n) == 0, "decode: vector 4");

	check(h2d_frame_decode(buf, 11, &f, H2D_VF_TRANSPORT, &err) != 0 &&
	      err == H2D_ESHORT, "decode: short frame");
	check(hex2bin(V_STUB, buf, sizeof(buf), &n) == 0, "decode: vector 5");

	buf[H2D_BID_OFF] = 0x10;	/* alloc nibble 0, dst nibble 1 */
	check(h2d_frame_decode(buf, n, &f, H2D_VF_TRANSPORT, &err) != 0 &&
	      err == H2D_EALLOC, "decode: alloc nibble 0");
	check(hex2bin(V_STUB, buf, sizeof(buf), &n) == 0, "decode: vector 6");

	buf[H2D_BID_OFF] = 0x11;
	buf[H2D_DST_OFF] = 0x00;
	check(h2d_frame_decode(buf, n, &f, H2D_VF_TRANSPORT, &err) != 0 &&
	      err == H2D_EDST, "decode: hdr[9] != hdr[1]>>4");
	check(hex2bin(V_STUB, buf, sizeof(buf), &n) == 0, "decode: vector 7");

	buf[H2D_GROUP_OFF] = 0x04;
	check(h2d_frame_decode(buf, n, &f, H2D_VF_TRANSPORT | H2D_VF_FIRMWARE,
			       &err) != 0 && err == H2D_EGROUP,
	      "decode: group 4 rejected H2D");

	buf[H2D_GROUP_OFF] = 0x0f;
	check(h2d_frame_decode(buf, n, &f, H2D_VF_TRANSPORT | H2D_VF_FIRMWARE,
			       &err) != 0 && err == H2D_EGROUP,
	      "decode: group 0xf rejected H2D");
	check(hex2bin(V_STUB, buf, sizeof(buf), &n) == 0, "decode: vector 8");

	/* A 64-byte zero header is the historical invalid bench frame. */
	{
		u8 zero[64];

		memset(zero, 0, sizeof(zero));
		check(h2d_frame_import(zero, sizeof(zero), &f, &err) != 0 &&
		      err == H2D_ELEN,
		      "import: 64-byte zero header rejected (len)");
	}

	/* Payload bound on decode. */
	{
		u8 frame[H2D_MAX_TOTAL + 1];

		check(hex2bin(V_STUB, frame, sizeof(frame), &n) == 0,
		      "decode: vector 9");
		frame[H2D_LEN_OFF] = (u8)(0x681 & 0xff);
		frame[H2D_LEN_OFF + 1] = (u8)(0x681 >> 8);
		check(h2d_frame_decode(frame, 0x681, &f, H2D_VF_TRANSPORT,
				       &err) != 0 && err == H2D_EBOUNDS,
		      "decode: total 0x681 rejected");
	}

	/* Unknown handler ids at the handler validation level. */
	{
		static const u8 id4[12] = { 0x00, 0x01, 0, 0, 0x0c, 0x00,
					    0x04, 0x00, 0, 0, 0x5a, 0x5a };

		check(h2d_frame_decode(id4, sizeof(id4), &f,
				       H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
					       H2D_VF_HANDLER, &err) != 0 &&
		      err == H2D_ENOHANDLER, "decode: g0 id4 no handler");
	}
}

static void test_sr_and_index(void)
{
	u8 node[H2D_NODE_SIZE];
	struct h2d_frame f;
	struct h2d_sr_plan p;
	u32 devva = 0, len = 0, flags = 0, idx;
	enum h2d_err err = H2D_EARG;
	int i;

	check(h2d_sr_node_encode(node, 0x84543000u, 0x30u, &err) == 0,
	      "node: encodes");
	check_hex(node, H2D_NODE_SIZE, "003054842b6d3000", "node: exact bytes");
	check(h2d_sr_node_decode(node, &devva, &len, &flags, &err) == 0,
	      "node: decodes");
	check_eq_int((long)devva, 0x84543000L, "node: devva round-trip");
	check_eq_int((long)len, 0x30, "node: len round-trip");
	check_eq_int((long)flags, 0x6d2b, "node: flags round-trip");

	check(h2d_sr_node_encode(node, 0, 11, &err) != 0 &&
	      err == H2D_EBOUNDS, "node: total < 12 refused");
	node[4] = 0x2a;	/* flags != 0x6d2b */
	check(h2d_sr_node_decode(node, &devva, &len, &flags, &err) != 0 &&
	      err == H2D_EWIRE, "node: bad flags refused");

	check_eq_int(h2d_idx_plus(0, 32), 1, "idx: plus(0)=1");
	check_eq_int(h2d_idx_plus(31, 32), 0x400, "idx: plus(31) wraps phase");
	check_eq_int(h2d_idx_plus(0x400, 32), 0x401, "idx: plus(0x400)=0x401");
	check_eq_int(h2d_idx_plus(0x41f, 32), 0, "idx: plus(0x41f) wraps to 0");
	check_eq_int(h2d_idx_plus(0, 1), 0x400, "idx: depth-1 wrap toggles phase");
	check_eq_int(h2d_idx_count(0, 0x400, 32), 32, "idx: count 0->0x400");
	check_eq_int(h2d_idx_count(0, 1, 32), 1, "idx: count 0->1");
	check_eq_int(h2d_idx_count(0x400, 0x401, 32), 1, "idx: count wrap+1");
	check_eq_int(h2d_idx_count(1, 0, 32), 63, "idx: count wrap-back (2*depth cycle)");
	check_eq_int(h2d_idx_count(0x400, 0, 32), 32, "idx: count phase ->0");

	idx = 0;
	for (i = 0; i < 32; i++)
		idx = h2d_idx_plus(idx, 32);
	check_eq_int(idx, 0x400, "idx: 32 posts -> 0x400 (pre-post value)");

	/* SR posting plan for the bench-confirmed first post. */
	check(h2d_build_safe_probe(&f, 0) == 0, "srplan: stub");
	check(h2d_sr_plan(&f, 0, 32, 0x84543000u, &p, &err) == 0,
	      "srplan: builds");
	check_eq_int(p.node_index, 0, "srplan: node index 0");
	check_eq_int(p.node_word0, 0x84543000L, "srplan: word0 buffer devva");
	check_eq_int(p.node_word1, 0x000c6d2bL, "srplan: word1 0x000c6d2b");
	check_eq_int(p.producer_after, 1, "srplan: producer 0 -> 1");
	check_eq_int(p.frame_total, 12, "srplan: frame total");
	check_eq_int(h2d_idx_plus(0x1f, 32), 0x400, "srplan: last slot wraps");
}

static void test_mailbox_and_plan(void)
{
	struct h2d_mailbox_plan mb;
	struct h2d_post_plan pp;
	struct h2d_frame f;
	enum h2d_err err = H2D_EARG;

	check(h2d_mailbox_plan(3, 0, 0, &mb, &err) == 0, "mb: id3 plan");
	check_eq_int(mb.pending_ca, 0x40039010L, "mb: out[0] CA");
	check_eq_int(mb.pending_write, 0x8, "mb: id3 -> pending bit3");
	check_eq_int(mb.doorbell_ca, 0x400392d4L, "mb: out[2] CA");
	check_eq_int(mb.doorbell_write, 1, "mb: doorbell |= 1");
	check(h2d_mailbox_plan(3, 0, 0x2, &mb, &err) == 0, "mb: id3 doorbell 2");
	check_eq_int(mb.doorbell_write, 3, "mb: doorbell 2 -> 3 (OR, not set)");
	check(h2d_mailbox_plan(1, 0, 0, &mb, &err) == 0, "mb: id1 PM sleep");
	check_eq_int(mb.pending_write, 0x2, "mb: id1 -> pending bit1");

	check(h2d_mailbox_plan(5, 0, 0, &mb, &err) != 0 && err == H2D_EPOLICY,
	      "mb: id5 BUG trap refused");
	check(h2d_mailbox_plan(0, 0, 0, &mb, &err) != 0 && err == H2D_EPOLICY,
	      "mb: id0 refused (not a vendor post)");
	check(h2d_mailbox_plan(2, 0, 0, &mb, &err) != 0 && err == H2D_EPOLICY,
	      "mb: id2 refused (D2H handler only)");
	check(h2d_mailbox_plan(6, 0, 0, &mb, &err) != 0 && err == H2D_EPOLICY,
	      "mb: id6 refused (service semantics, not a host post)");
	check(h2d_mailbox_plan(10, 0, 0, &mb, &err) != 0 &&
	      err == H2D_EBOUNDS, "mb: id10 > send bound");
	check(h2d_mailbox_plan(3, 0x8, 0, &mb, &err) != 0 &&
	      err == H2D_ESTATE, "mb: pending out[0] not consumed -> no retry");

	check(h2d_build_safe_probe(&f, 0) == 0, "planpost: stub");
	check(h2d_plan_post(&f, 0, 32, 0x84543000u, 0, 0, &pp, &err) == 0,
	      "planpost: one frame plan");
	check_eq_int(pp.sr.producer_after, 1, "planpost: producer -> 1");
	check_eq_int(pp.mb.pending_write, 8, "planpost: announce bit3");
	check_eq_int(pp.frame_total, 12, "planpost: total");
	check(h2d_plan_post(&f, 0, 32, 0x84543000u, 8, 0, &pp, &err) != 0 &&
	      err == H2D_ESTATE, "planpost: refuses pending previous frame");
}

static void test_d2h_ready(void)
{
	struct h2d_ready r;
	u8 buf[H2D_MAX_TOTAL];
	size_t n = 0;
	enum h2d_err err = H2D_EARG;
	static const u8 id2[16] = { 0x04, 0x00, 0x00, 0x00, 0x10, 0x00, 0x02,
				    0x00, 0x00, 0x00, 0x5a, 0x5a,
				    0x50, 0x08, 0x06, 0x01 };
	static const u8 id1[12] = { 0x04, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x01,
				    0x00, 0x00, 0x00, 0x5a, 0x5a };

	check(h2d_ready_decode(id1, sizeof(id1), &r, &err) == 0,
	      "d2h: id1 decodes");
	check_eq_int(r.group, 4, "d2h: id1 group 4");
	check_eq_int(r.id, 1, "d2h: id1 id");
	check_eq_int(r.payload_len, 0, "d2h: id1 payload 0");
	check(h2d_ready_decode(id2, sizeof(id2), &r, &err) == 0,
	      "d2h: id2 decodes");
	check_eq_int(r.id, 2, "d2h: id2 id");
	check_eq_int(r.payload_len, 4, "d2h: id2 payload 4");
	check_eq_int((long)r.payload_dword0, 0x01060850L,
		     "d2h: id2 payload 0x01060850");
	check(hex2bin(V_D2H_ID2, buf, sizeof(buf), &n) == 0 &&
	      memcmp(buf, id2, 16) == 0, "d2h: id2 vector matches");

	buf[0] = 0x03;
	check(h2d_ready_decode(buf, sizeof(id2), &r, &err) != 0 &&
	      err == H2D_EGROUP, "d2h: group 3 refused");
	buf[0] = 0x04;
	buf[10] = 0x00;
	check(h2d_ready_decode(buf, sizeof(id2), &r, &err) != 0 &&
	      err == H2D_ETAG, "d2h: bad tag refused");
	buf[10] = 0x5a;
	buf[6] = 0x03;
	check(h2d_ready_decode(buf, sizeof(id2), &r, &err) != 0 &&
	      err == H2D_EID, "d2h: id3 is not a ready frame");
}

static void test_roundtrip(void)
{
	struct h2d_frame f, g;
	u8 buf[H2D_MAX_TOTAL];
	u8 cap[80];
	size_t n = 0;
	enum h2d_err err = H2D_EARG;

	check(h2d_build_g0_id1d_edca_opt(&f, 0, 0, 4, 0, &err) == 0,
	      "roundtrip: msgA build");
	check(h2d_frame_encode(&f, buf, sizeof(buf), &n, &err) == 0,
	      "roundtrip: msgA encode");
	check(h2d_frame_decode(buf, n, &g, H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
			       H2D_VF_HANDLER, &err) == 0,
	      "roundtrip: msgA decode (no policy)");
	check_eq_int(g.id, f.id, "roundtrip: id");
	check_eq_int(g.payload_len, f.payload_len, "roundtrip: payload len");
	check(memcmp(g.payload, f.payload, f.payload_len) == 0,
	      "roundtrip: payload bytes");

	/* Policy decode of a synthesized msgA payload must fail. */
	check(h2d_frame_decode(buf, n, &g, H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
			       H2D_VF_HANDLER | H2D_VF_POLICY, &err) != 0 &&
	      err == H2D_EPOLICY, "roundtrip: policy refuses synthesized msgA");
	/* ... but the captured frame imports and passes. */
	check(h2d_frame_import(buf, n, &g, &err) == 0,
	      "roundtrip: msgA import passes policy");
	check_eq_int(g.policy, H2D_F_CAPTURED, "roundtrip: msgA captured");

	/* Export with padding below the frame total is an argument error. */
	check(h2d_build_g0_id1d_edca_opt(&f, 0, 0, 4, 0, &err) == 0,
	      "roundtrip: msgA build 2");
	check(h2d_frame_export(&f, cap, sizeof(cap), 16, 0xff, &n, &err) != 0 &&
	      err == H2D_EARG, "roundtrip: pad < total refused");
}

static int run_suite(void)
{
	printf("wifi-h2d-frames selftest: HCC header, exact builders, refusals,\n"
	       "SR node/packed-index, mailbox plan, catalogue, D2H ready decode\n");
	test_constants();
	test_catalogue();
	test_safe_probe_and_header();
	test_exact_builders();
	test_refusals();
	test_decode_refusals();
	test_sr_and_index();
	test_mailbox_and_plan();
	test_d2h_ready();
	test_roundtrip();
	printf("SUMMARY: %d passed, %d failed\n", g_checks - g_failures,
	       g_failures);
	return g_failures ? 1 : 0;
}

/* ------------------------------- CLI ------------------------------- */
static unsigned long arg_ul(const char *s, unsigned long fallback)
{
	char *end = NULL;
	unsigned long v;

	if (!s)
		return fallback;
	v = strtoul(s, &end, 0);
	if (!end || *end != '\0')
		return fallback;
	return v;
}

static void print_frame(const struct h2d_frame *f, const char *tag)
{
	u8 buf[H2D_MAX_TOTAL];
	size_t n = 0;
	enum h2d_err err = H2D_EARG;

	if (h2d_frame_encode(f, buf, sizeof(buf), &n, &err) != 0) {
		printf("%s: ENCODE REFUSED: %s\n", tag, h2d_strerror(err));
		return;
	}
	printf("%s: group=%u id=0x%02x dst=%u fa=%u fb=%u fc=%u payload=%u total=%zu\n",
	       tag, f->group, f->id, f->dst, f->fa, f->fb, f->fc,
	       f->payload_len, n);
	printf("%s: ", tag);
	print_hex(buf, n);
	printf("\n");
}

static int cli(int argc, char **argv)
{
	struct h2d_frame f;
	u8 hexbuf[H2D_MAX_TOTAL];
	u8 tmp[H2D_MAX_PAYLOAD];
	size_t n = 0;
	enum h2d_err err = H2D_EARG;
	const char *cmd = argv[1];

	if (strcmp(cmd, "catalogue") == 0) {
		const struct h2d_fw_handler *fw;
		const struct h2d_vendor_sender *vs;
		size_t nf = 0, nv = 0, i;

		vs = h2d_vendor_senders_get(&nv);
		fw = h2d_fw_handlers_get(&nf);
		printf("vendor H2D senders: %zu\n", nv);
		for (i = 0; i < nv; i++)
			printf("  g%u id 0x%02x %-42s payload=%4d risk=%s\n",
			       vs[i].group, vs[i].id, vs[i].name,
			       vs[i].payload_len, h2d_risk_name(vs[i].risk));
		printf("firmware H2D receive slots: %zu\n", nf);
		for (i = 0; i < nf; i++)
			printf("  g%u id 0x%02x handler=%s risk=%s\n",
			       fw[i].group, fw[i].id,
			       fw[i].handler_ca ? "non-NULL" : "NULL",
			       h2d_risk_name(fw[i].risk));
		return 0;
	}
	if (strcmp(cmd, "stub") == 0) {
		if (h2d_build_safe_probe(&f,
					 (u8)arg_ul(argc > 2 ? argv[2] : NULL, 0)) != 0)
			return 2;
		print_frame(&f, "stub");
		return 0;
	}
	if (strcmp(cmd, "edca") == 0) {
		if (argc < 5)
			return 2;
		if (h2d_build_g0_id1d_edca_opt(&f,
					       (u8)arg_ul(argv[2], 0),
					       (u8)arg_ul(argv[3], 0),
					       (u8)arg_ul(argv[4], 0),
					       (u8)arg_ul(argc > 5 ? argv[5] : NULL,
							  0), &err) != 0)
			goto refused;
		print_frame(&f, "edca");
		return 0;
	}
	if (strcmp(cmd, "deluser") == 0) {
		u8 addr[4];

		if (argc < 4)
			return 2;
		if (argc > 4) {
			if (hex2bin(argv[4], addr, sizeof(addr), &n) != 0 || n != 4)
				return 2;
			if (h2d_build_g0_id09_del_user(&f, 0,
						       (u16)arg_ul(argv[3], 0),
						       addr, &err) != 0)
				goto refused;
		} else if (h2d_build_g0_id09_del_user(&f, 0,
						      (u16)arg_ul(argv[3], 0),
						      NULL, &err) != 0) {
			goto refused;
		}
		print_frame(&f, "deluser");
		return 0;
	}
	if (strcmp(cmd, "cfg") == 0 || strcmp(cmd, "hdpp") == 0) {
		u16 id = (strcmp(cmd, "hdpp") == 0) ? 1u :
			 (u16)arg_ul(argc > 2 ? argv[2] : NULL, 1);
		const char *p;
		int ai = (strcmp(cmd, "hdpp") == 0) ? 2 : 3;
		unsigned long dst, fa, fb, word8, ln;
		u8 token[8], tail[4];
		size_t tn = 0, tl = 0, dn = 0;

		if (argc < ai + 5)
			return 2;
		dst = arg_ul(argv[ai], 0);
		fa = arg_ul(argv[ai + 1], 0);
		fb = arg_ul(argv[ai + 2], 0);
		word8 = arg_ul(argv[ai + 3], 0);
		dn = 0;
		if (hex2bin(argv[ai + 4], tmp, sizeof(tmp), &dn) != 0)
			return 2;
		token[0] = token[1] = token[2] = token[3] = 0;
		token[4] = 1;
		token[5] = token[6] = token[7] = 0;
		if (argc > ai + 5 &&
		    (hex2bin(argv[ai + 5], token, sizeof(token), &tn) != 0 ||
		     tn != 8))
			return 2;
		memset(tail, 0, sizeof(tail));
		if (argc > ai + 6 &&
		    (hex2bin(argv[ai + 6], tail, sizeof(tail), &tl) != 0 ||
		     tl != 4))
			return 2;
		ln = dn;
		if (h2d_build_g0_cfg_event(&f, (u8)id, (u8)dst, (u8)fa, (u8)fb,
					   0, token, (u16)word8, tmp, (u16)ln,
					   (argc > ai + 6) ? tail : NULL,
					   &err) != 0)
			goto refused;
		(void)p;
		print_frame(&f, "cfg");
		return 0;
	}
	if (strcmp(cmd, "raw") == 0) {
		u32 policy = 0;

		if (argc < 9)
			return 2;
		if (hex2bin(argv[8], tmp, sizeof(tmp), &n) != 0)
			return 2;
		if (argc > 9 && strcmp(argv[9], "captured") == 0)
			policy = H2D_F_CAPTURED;
		if (h2d_build_raw(&f, (u8)arg_ul(argv[2], 0),
				  (u16)arg_ul(argv[3], 0),
				  (u8)arg_ul(argv[4], 0),
				  (u8)arg_ul(argv[5], 0),
				  (u8)arg_ul(argv[6], 0),
				  (u8)arg_ul(argv[7], 0), tmp, (u16)n,
				  policy, &err) != 0)
			goto refused;
		print_frame(&f, "raw");
		return 0;
	}
	if (strcmp(cmd, "decode") == 0 || strcmp(cmd, "import") == 0) {
		u32 flags = H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
			    H2D_VF_HANDLER | H2D_VF_POLICY;

		if (argc < 3 || hex2bin(argv[2], hexbuf, sizeof(hexbuf),
					&n) != 0)
			return 2;
		if (strcmp(cmd, "decode") == 0) {
			if (h2d_frame_decode(hexbuf, n, &f, flags, &err) != 0)
				goto refused;
		} else if (h2d_frame_import(hexbuf, n, &f, &err) != 0) {
			goto refused;
		}
		print_frame(&f, cmd);
		return 0;
	}
	if (strcmp(cmd, "d2h") == 0) {
		struct h2d_ready r;

		if (argc < 3 || hex2bin(argv[2], hexbuf, sizeof(hexbuf),
					&n) != 0)
			return 2;
		if (h2d_ready_decode(hexbuf, n, &r, &err) != 0)
			goto refused;
		printf("d2h: group=%u id=%u core=%u total=%u payload=%u dword0=0x%08x\n",
		       r.group, r.id, r.core, r.total, r.payload_len,
		       r.payload_dword0);
		return 0;
	}
	if (strcmp(cmd, "post") == 0) {
		struct h2d_post_plan pp;
		unsigned long producer = 0, depth = 32, devva = 0x84543000,
			      out0 = 0, out2 = 0;

		if (argc < 3 || hex2bin(argv[2], hexbuf, sizeof(hexbuf),
					&n) != 0)
			return 2;
		producer = arg_ul(argc > 3 ? argv[3] : NULL, 0);
		depth = arg_ul(argc > 4 ? argv[4] : NULL, 32);
		devva = arg_ul(argc > 5 ? argv[5] : NULL, 0x84543000);
		out0 = arg_ul(argc > 6 ? argv[6] : NULL, 0);
		out2 = arg_ul(argc > 7 ? argv[7] : NULL, 0);
		if (h2d_frame_import(hexbuf, n, &f, &err) != 0)
			goto refused;
		if (h2d_plan_post(&f, (u32)producer, (u32)depth, (u32)devva,
				  (u32)out0, (u32)out2, &pp, &err) != 0)
			goto refused;
		printf("post: frame total=%u node[idx=%u]={0x%08x,0x%08x} producer 0x%x->0x%x; mailbox id=%u out[0]=0x%x out[2]=0x%x|1\n",
		       pp.frame_total, pp.sr.node_index, pp.sr.node_word0,
		       pp.sr.node_word1, pp.sr.producer_before,
		       pp.sr.producer_after, pp.mb.id, pp.mb.pending_write,
		       pp.mb.doorbell_write & ~1u);
		return 0;
	}
	fprintf(stderr, "unknown command: %s\n", cmd);
	return 2;

refused:
	printf("REFUSED: %s (%s)\n", h2d_strerror(err),
	       (err == H2D_EPOLICY) ? "lane safety policy" : "validation");
	return 1;
}

int main(int argc, char **argv)
{
	if (argc < 2)
		return run_suite();
	if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
		printf("usage:\n"
		       "  %s                        run the self-test suite\n"
		       "  %s catalogue              dump the generated catalogue\n"
		       "  %s stub <dst>\n"
		       "  %s edca <dst> <fa> <fb> [fc]\n"
		       "  %s deluser <dst> <userid> [addr4hex]\n"
		       "  %s cfg <id> <dst> <fa> <fb> <word8> <datahex> [token8hex] [tail4hex]\n"
		       "  %s hdpp <dst> <fa> <fb> <cmd> <datahex> [token8hex] [tail4hex]\n"
		       "  %s raw <group> <id> <dst> <fa> <fb> <fc> <hex> [captured]\n"
		       "  %s decode|import <hex>\n"
		       "  %s d2h <hex>\n"
		       "  %s post <hex> [producer] [depth] [devva] [out0] [out2]\n"
		       "exit: 0 = pass / success, 1 = refused, 2 = command error\n",
		       argv[0], argv[0], argv[0], argv[0], argv[0], argv[0],
		       argv[0], argv[0], argv[0], argv[0], argv[0]);
		return 0;
	}
	return cli(argc, argv);
}
