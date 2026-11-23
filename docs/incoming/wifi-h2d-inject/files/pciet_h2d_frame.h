/* SPDX-License-Identifier: GPL-2.0 */
/*
 * pciet_h2d_frame.h - Hi5622V100 H2D HCC frame model: request parser, frame
 * builder, frame validator, hex helpers and the shared self-test.
 *
 * ORIGINAL code written by the wifi-h2d-inject lane (host-only, 2026-11-23)
 * from the project's decoded interface contract; no vendor source text or
 * object code is copied.  Evidence markers: [V] re-verified from a named
 * local artifact, [I] inference, [U] unknown / not yet pinned.
 *
 * CONTRACT SOURCE
 *   opensource/docs/incoming/wifi-hcc-re/REPORT.md
 *     sec 3.1  the 12-byte HCC header + the hcc_msg_alloc 0x674 bound
 *     sec 3.3  the 8-byte ETE SR node and the out[0]/out[2] announce
 *     sec 8    item 5, the driver-facing H2D send contract
 *
 * [V] 12-byte HCC header (little-endian), payload starts at +0x0c:
 *   +0x00 u8   group/type       low nibble = group index (dispatch does &0xf)
 *   +0x01 u8   low nibble = alloc state (1 = allocated);
 *              high nibble = resource-group / thread index (= dst)
 *   +0x02 u8   per-message field A
 *   +0x03 u8   per-message field B (chip id / vap id)
 *   +0x04 u16  total length = payload + 12
 *   +0x06 u16  message id (the dispatch key inside the group)
 *   +0x08 u8   per-message field C
 *   +0x09 u8   dst = hdr[1] >> 4 (resource/thread selector)
 *   +0x0a u16  transport tag 0x5a5a
 *   +0x0c ...  payload, max 0x674 (hcc_msg_alloc bound), total max 0x680
 *
 * The frame is carried by one 8-byte ETE SR node {devVA, (len << 16) | 0x6d2b}
 * where len is the TOTAL frame length [V], and the mailbox announce is
 * written by pciet_h2d.c as out[0] |= 1 << 3 then out[2] |= 1 (hcc-re
 * sec 3.3 / sec 8 item 5 [V]).
 *
 * WHY THIS HEADER IS PURE C
 *   The same text compiles into the kernel module (pciet_h2d.c) and into the
 *   userspace selftest/tool (h2d_selftest.c) with no kernel headers and no
 *   device access on the host side.  The parser/builder/validator the module
 *   runs at injection time are therefore the exact code the userspace
 *   selftest exercises - that is what makes the injector testable without the
 *   board.  Keep this file free of kernel-only calls.
 *
 * HARD REFUSALS (defence in depth; see ../REPORT.md sec 4):
 *   - HCC message id 5 is refused by the parser, the builder and the frame
 *     validator.  The device *mailbox* id 5 is the firmware BUG() trap
 *     (hcc-re sec 3.5/5.5); the injector's announce is hard-wired to mailbox
 *     id 3 and has no path that can post 5, and the frame id is refused too
 *     so that a typo can never build a frame whose dispatch key is 5.
 *   - payload > 0x674 / total > 0x680 is refused everywhere: hcc_msg_alloc
 *     returns NULL beyond its movw 0x674 bound [V].
 *   - group and dst are 4-bit header fields; values > 0x0f are refused
 *     instead of being silently masked.
 *   - the builder always sets the allocated bit (hdr[1] bit 0) and the
 *     hdr[9] = hdr[1] >> 4 invariant; the validator re-checks that invariant
 *     on raw frames.
 */
#ifndef _PCIET_H2D_FRAME_H
#define _PCIET_H2D_FRAME_H

#ifdef __KERNEL__
#include <linux/kernel.h>	/* u8/u16/u32, vsnprintf, strlen, memset */
#include <linux/string.h>
#include <linux/types.h>
#else
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
#endif

/* ---------------------------------------------------------------------
 * 1. Contract constants ([V] hcc-re sec 3.1 / sec 8)
 * ------------------------------------------------------------------- */
#define H2D_HCC_HDR_LEN		12u
#define H2D_HCC_TAG		0x5a5au
#define H2D_HCC_ALLOC_BIT	0x1u
#define H2D_HCC_MAX_PAYLOAD	0x674u
#define H2D_HCC_MAX_TOTAL	(H2D_HCC_HDR_LEN + H2D_HCC_MAX_PAYLOAD) /* 0x680 */
#define H2D_HCC_REFUSED_ID	5u	/* mailbox id 5 = firmware BUG() trap */
#define H2D_HCC_GROUP_MAX	0x0fu	/* 4-bit group index */
#define H2D_HCC_DST_MAX		0x0fu	/* 4-bit resource/thread index */
#define H2D_MAX_REQUEST_TEXT	4096u	/* sysfs store bound for builder text */
#define H2D_MAX_RAW_HEX		(2u * H2D_HCC_MAX_TOTAL)

/* Small status codes shared by the kernel wrapper and the userspace tool;
 * the kernel side maps them onto -errno in pciet_h2d.c. */
#define H2D_OK		0
#define H2D_EARG	(-1)	/* malformed request / frame */
#define H2D_EBOUNDS	(-2)	/* a contract bound is exceeded */
#define H2D_EREFUSED	(-3)	/* hard-refused value (message id 5) */
#define H2D_EBUFFER	(-4)	/* caller buffer too small */

/* ---------------------------------------------------------------------
 * 2. Request model + shared self-test workspace
 *
 * struct h2d_request is the userspace->kernel request: the parsed fields of
 * one "group=.. id=.. dst=.. payload=.." line.  It is deliberately NOT
 * packed (it never crosses a wire) and is always heap-allocated by the
 * kernel caller (it is >1.6 KiB, far beyond the CONFIG_FRAME_WARN stack
 * budget); userspace may keep it anywhere.
 * ------------------------------------------------------------------- */
struct h2d_request {
	u8	group;		/* header +0x00 low nibble */
	u16	id;		/* header +0x06 dispatch key */
	u8	dst;		/* header +0x09 and header +0x01 high nibble */
	u8	field_a;	/* header +0x02 */
	u8	field_b;	/* header +0x03 */
	u8	field_c;	/* header +0x08 */
	u16	payload_len;	/* 0 .. H2D_HCC_MAX_PAYLOAD */
	u8	payload[H2D_HCC_MAX_PAYLOAD];
};

struct h2d_selftest_buf {
	struct h2d_request	req;	/* parser scratch */
	u8			frame[H2D_HCC_MAX_TOTAL];	/* builder scratch */
	u8			frame2[H2D_HCC_MAX_TOTAL];	/* validator scratch */
	char			hex[H2D_MAX_RAW_HEX + 8];	/* hex scratch */
};

/* ---------------------------------------------------------------------
 * 3. Small helpers (no kernel-only calls)
 * ------------------------------------------------------------------- */
static int h2d_is_space(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
	       c == '\v' || c == '\f';
}

static int h2d_hexval(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* Format a message into err (never fatal if err is NULL/empty). */
static int h2d_fail(char *err, size_t errsz, int code, const char *fmt, ...)
{
	va_list ap;

	if (err && errsz) {
		va_start(ap, fmt);
		vsnprintf(err, errsz, fmt, ap);
		va_end(ap);
	}
	return code;
}

/* Append one formatted line to a NUL-terminated log buffer. */
static void h2d_log(char *log, size_t logsz, const char *fmt, ...)
{
	va_list ap;
	size_t used;

	if (!log || !logsz)
		return;
	used = strlen(log);
	if (used + 1 >= logsz)
		return;
	va_start(ap, fmt);
	vsnprintf(log + used, logsz - used, fmt, ap);
	va_end(ap);
}

static int h2d_key_eq(const char *s, size_t n, const char *key)
{
	size_t kl = strlen(key);

	return n == kl && memcmp(s, key, kl) == 0;
}

/* Parse an unsigned decimal or 0x-prefixed hexadecimal number.
 * Returns 0 on success, -1 for a malformed/overflowing number, -2 when the
 * value is valid but greater than max. */
static int h2d_parse_uint(const char *s, size_t n, u32 max, u32 *out)
{
	u32 base = 10, v = 0;
	size_t i = 0;

	if (!s || n == 0 || !out)
		return -1;
	if (n >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		base = 16;
		i = 2;
	}
	if (i >= n)
		return -1;
	for (; i < n; i++) {
		int d = h2d_hexval(s[i]);

		if (d < 0 || (u32)d >= base)
			return -1;
		if (v > (0xffffffffu - (u32)d) / base)
			return -1;
		v = v * base + (u32)d;
	}
	if (v > max)
		return -2;
	*out = v;
	return 0;
}

/* Parse an even-length hex string (optional 0x prefix) into bytes. */
static int h2d_parse_hex(const char *s, size_t n, u8 *out, u32 out_sz,
			 u32 *out_len, char *err, size_t errsz)
{
	size_t i = 0, o = 0;

	if (!s || !out || !out_len)
		return h2d_fail(err, errsz, H2D_EARG, "hex: null argument");
	if (n >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		s += 2;
		n -= 2;
	}
	if (n & 1)
		return h2d_fail(err, errsz, H2D_EARG,
				"hex payload: odd number of digits (%zu)", n);
	if (n / 2 > out_sz)
		return h2d_fail(err, errsz, H2D_EBOUNDS,
				"hex payload: %zu bytes exceed the %u-byte bound",
				n / 2, out_sz);
	for (i = 0; i < n; i += 2) {
		int hi = h2d_hexval(s[i]);
		int lo = h2d_hexval(s[i + 1]);

		if (hi < 0 || lo < 0)
			return h2d_fail(err, errsz, H2D_EARG,
					"hex payload: invalid digit at offset %zu",
					i);
		out[o++] = (u8)((hi << 4) | lo);
	}
	*out_len = (u32)o;
	return H2D_OK;
}

static const char *h2d_strerror(int code)
{
	switch (code) {
	case H2D_OK:		return "ok";
	case H2D_EARG:		return "malformed request/frame";
	case H2D_EBOUNDS:	return "contract bound exceeded";
	case H2D_EREFUSED:	return "hard-refused value (message id 5)";
	case H2D_EBUFFER:	return "caller buffer too small";
	default:		return "unknown error";
	}
}

/* ---------------------------------------------------------------------
 * 4. Builder-syntax parser: "group=N id=N dst=N [a=N] [b=N] [c=N]
 *    [payload=HEX]" - whitespace separated key=value tokens.
 *
 * Required: group (0..15), id (0..65535, never 5), dst (0..15).
 * Optional: a/b/c (0..255, default 0) and payload (hex, default empty).
 * Duplicate tokens, unknown keys and missing required keys are errors; the
 * parser never modifies the input text.
 * ------------------------------------------------------------------- */
static int h2d_parse_request(const char *text, size_t text_len,
			     struct h2d_request *req,
			     char *err, size_t errsz)
{
	size_t i = 0;
	int have_group = 0, have_id = 0, have_dst = 0;
	int have_a = 0, have_b = 0, have_c = 0, have_payload = 0;
	u32 v;
	int rc;

	if (!text || !req)
		return h2d_fail(err, errsz, H2D_EARG, "request: null argument");
	if (text_len > H2D_MAX_REQUEST_TEXT)
		return h2d_fail(err, errsz, H2D_EBOUNDS,
				"request: %zu bytes exceed the %u-byte bound",
				text_len, H2D_MAX_REQUEST_TEXT);
	memset(req, 0, sizeof(*req));

	while (i < text_len) {
		size_t start, toklen, eq;
		const char *tok;

		while (i < text_len && h2d_is_space(text[i]))
			i++;
		if (i >= text_len)
			break;
		start = i;
		while (i < text_len && !h2d_is_space(text[i]))
			i++;
		toklen = i - start;
		tok = text + start;

		eq = 0;
		while (eq < toklen && tok[eq] != '=')
			eq++;
		if (eq == toklen)
			return h2d_fail(err, errsz, H2D_EARG,
					"token '%.*s': expected key=value",
					(int)toklen, tok);
		if (eq == 0)
			return h2d_fail(err, errsz, H2D_EARG,
					"token '%.*s': empty key",
					(int)toklen, tok);
		if (h2d_key_eq(tok, eq, "group")) {
			if (have_group)
				return h2d_fail(err, errsz, H2D_EARG,
						"duplicate key 'group'");
			rc = h2d_parse_uint(tok + eq + 1, toklen - eq - 1,
					    H2D_HCC_GROUP_MAX, &v);
			if (rc == -2)
				return h2d_fail(err, errsz, H2D_EBOUNDS,
						"group: %.*s > 0x0f",
						(int)(toklen - eq - 1), tok + eq + 1);
			if (rc)
				return h2d_fail(err, errsz, H2D_EARG,
						"group: not a number");
			req->group = (u8)v;
			have_group = 1;
		} else if (h2d_key_eq(tok, eq, "id")) {
			if (have_id)
				return h2d_fail(err, errsz, H2D_EARG,
						"duplicate key 'id'");
			rc = h2d_parse_uint(tok + eq + 1, toklen - eq - 1,
					    0xffffu, &v);
			if (rc == -2)
				return h2d_fail(err, errsz, H2D_EBOUNDS,
						"id: %.*s > 0xffff",
						(int)(toklen - eq - 1), tok + eq + 1);
			if (rc)
				return h2d_fail(err, errsz, H2D_EARG,
						"id: not a number");
			if (v == H2D_HCC_REFUSED_ID)
				return h2d_fail(err, errsz, H2D_EREFUSED,
						"id=5 is hard-refused (device mailbox id 5 is the firmware BUG() trap)");
			req->id = (u16)v;
			have_id = 1;
		} else if (h2d_key_eq(tok, eq, "dst")) {
			if (have_dst)
				return h2d_fail(err, errsz, H2D_EARG,
						"duplicate key 'dst'");
			rc = h2d_parse_uint(tok + eq + 1, toklen - eq - 1,
					    H2D_HCC_DST_MAX, &v);
			if (rc == -2)
				return h2d_fail(err, errsz, H2D_EBOUNDS,
						"dst: %.*s > 0x0f",
						(int)(toklen - eq - 1), tok + eq + 1);
			if (rc)
				return h2d_fail(err, errsz, H2D_EARG,
						"dst: not a number");
			req->dst = (u8)v;
			have_dst = 1;
		} else if (h2d_key_eq(tok, eq, "a")) {
			if (have_a)
				return h2d_fail(err, errsz, H2D_EARG,
						"duplicate key 'a'");
			rc = h2d_parse_uint(tok + eq + 1, toklen - eq - 1,
					    0xffu, &v);
			if (rc)
				return h2d_fail(err, errsz, H2D_EARG,
						"a: not a 0..255 number");
			req->field_a = (u8)v;
			have_a = 1;
		} else if (h2d_key_eq(tok, eq, "b")) {
			if (have_b)
				return h2d_fail(err, errsz, H2D_EARG,
						"duplicate key 'b'");
			rc = h2d_parse_uint(tok + eq + 1, toklen - eq - 1,
					    0xffu, &v);
			if (rc)
				return h2d_fail(err, errsz, H2D_EARG,
						"b: not a 0..255 number");
			req->field_b = (u8)v;
			have_b = 1;
		} else if (h2d_key_eq(tok, eq, "c")) {
			if (have_c)
				return h2d_fail(err, errsz, H2D_EARG,
						"duplicate key 'c'");
			rc = h2d_parse_uint(tok + eq + 1, toklen - eq - 1,
					    0xffu, &v);
			if (rc)
				return h2d_fail(err, errsz, H2D_EARG,
						"c: not a 0..255 number");
			req->field_c = (u8)v;
			have_c = 1;
		} else if (h2d_key_eq(tok, eq, "payload")) {
			if (have_payload)
				return h2d_fail(err, errsz, H2D_EARG,
						"duplicate key 'payload'");
			u32 plen = 0;

			rc = h2d_parse_hex(tok + eq + 1, toklen - eq - 1,
					   req->payload, H2D_HCC_MAX_PAYLOAD,
					   &plen, err, errsz);
			if (rc)
				return rc;
			req->payload_len = (u16)plen;
			have_payload = 1;
		} else {
			return h2d_fail(err, errsz, H2D_EARG,
					"unknown key '%.*s'",
					(int)eq, tok);
		}
	}

	if (!have_group)
		return h2d_fail(err, errsz, H2D_EARG, "missing required key 'group'");
	if (!have_id)
		return h2d_fail(err, errsz, H2D_EARG, "missing required key 'id'");
	if (!have_dst)
		return h2d_fail(err, errsz, H2D_EARG, "missing required key 'dst'");
	return H2D_OK;
}

/* Re-check a parsed request against the contract (defence in depth: the
 * builder must not trust its caller). */
static int h2d_check_request(const struct h2d_request *req,
			     char *err, size_t errsz)
{
	if (!req)
		return h2d_fail(err, errsz, H2D_EARG, "request: null argument");
	if (req->group > H2D_HCC_GROUP_MAX)
		return h2d_fail(err, errsz, H2D_EBOUNDS,
				"group %u > 0x0f", req->group);
	if (req->dst > H2D_HCC_DST_MAX)
		return h2d_fail(err, errsz, H2D_EBOUNDS,
				"dst %u > 0x0f", req->dst);
	if (req->id == H2D_HCC_REFUSED_ID)
		return h2d_fail(err, errsz, H2D_EREFUSED,
				"id=5 is hard-refused (firmware BUG() trap)");
	if (req->payload_len > H2D_HCC_MAX_PAYLOAD)
		return h2d_fail(err, errsz, H2D_EBOUNDS,
				"payload %u > 0x674", req->payload_len);
	return H2D_OK;
}

/* ---------------------------------------------------------------------
 * 5. Frame builder: request -> 12-byte header + payload.
 *
 * Little-endian byte assembly is done explicitly (u8 stores) so the built
 * frame is identical on any host/target endianness.
 * ------------------------------------------------------------------- */
static int h2d_build_frame(const struct h2d_request *req, u8 *frame,
			   size_t frame_sz, size_t *frame_len,
			   char *err, size_t errsz)
{
	size_t total;
	int rc;

	if (!frame || !frame_len)
		return h2d_fail(err, errsz, H2D_EARG, "build: null argument");
	rc = h2d_check_request(req, err, errsz);
	if (rc)
		return rc;
	total = H2D_HCC_HDR_LEN + req->payload_len;
	if (frame_sz < total)
		return h2d_fail(err, errsz, H2D_EBUFFER,
				"build: frame %zu B does not fit the %zu-byte buffer",
				total, frame_sz);

	frame[0] = (u8)(req->group & H2D_HCC_GROUP_MAX);
	frame[1] = (u8)(((req->dst << 4) & 0xf0) | H2D_HCC_ALLOC_BIT);
	frame[2] = req->field_a;
	frame[3] = req->field_b;
	frame[4] = (u8)(total & 0xff);
	frame[5] = (u8)((total >> 8) & 0xff);
	frame[6] = (u8)(req->id & 0xff);
	frame[7] = (u8)((req->id >> 8) & 0xff);
	frame[8] = req->field_c;
	frame[9] = (u8)(req->dst & H2D_HCC_DST_MAX);
	frame[10] = (u8)(H2D_HCC_TAG & 0xff);
	frame[11] = (u8)((H2D_HCC_TAG >> 8) & 0xff);
	if (req->payload_len)
		memcpy(frame + H2D_HCC_HDR_LEN, req->payload,
		       req->payload_len);
	*frame_len = total;
	return H2D_OK;
}

/* ---------------------------------------------------------------------
 * 6. Frame validator: re-checks every hard invariant of a ready-to-post
 *    frame (used for the raw hex path and as a belt-and-braces check of the
 *    builder path right before delivery).
 * ------------------------------------------------------------------- */
static int h2d_validate_frame(const u8 *frame, size_t frame_len,
			      char *err, size_t errsz)
{
	u16 declared, tag, id;

	if (!frame)
		return h2d_fail(err, errsz, H2D_EARG, "frame: null argument");
	if (frame_len < H2D_HCC_HDR_LEN)
		return h2d_fail(err, errsz, H2D_EARG,
				"frame: %zu B shorter than the 12-byte HCC header",
				frame_len);
	if (frame_len > H2D_HCC_MAX_TOTAL)
		return h2d_fail(err, errsz, H2D_EBOUNDS,
				"frame: %zu B > 0x680 contract maximum",
				frame_len);
	declared = (u16)(frame[4] | ((u16)frame[5] << 8));
	if ((size_t)declared != frame_len)
		return h2d_fail(err, errsz, H2D_EARG,
				"frame: declared length %u != actual %zu",
				declared, frame_len);
	tag = (u16)(frame[10] | ((u16)frame[11] << 8));
	if (tag != H2D_HCC_TAG)
		return h2d_fail(err, errsz, H2D_EARG,
				"frame: transport tag 0x%04x != 0x5a5a", tag);
	id = (u16)(frame[6] | ((u16)frame[7] << 8));
	if (id == H2D_HCC_REFUSED_ID)
		return h2d_fail(err, errsz, H2D_EREFUSED,
				"frame: id=5 is hard-refused (firmware BUG() trap)");
	if ((u8)(frame[1] >> 4) != frame[9])
		return h2d_fail(err, errsz, H2D_EARG,
				"frame: hdr[9] dst %u != hdr[1] high nibble %u",
				frame[9], frame[1] >> 4);
	return H2D_OK;
}

/* ---------------------------------------------------------------------
 * 7. Raw-hex frame parser: arbitrary 12+ byte frame, whitespace tolerated,
 *    then the full validator.  This is the escape hatch for injecting an
 *    exact vendor-derived frame from a trace without a kernel rebuild.
 * ------------------------------------------------------------------- */
static int h2d_parse_raw_frame(const char *text, size_t text_len,
			       u8 *out, size_t out_sz, size_t *out_len,
			       char *err, size_t errsz)
{
	size_t i, n = 0;
	int hi = -1;

	if (!text || !out || !out_len)
		return h2d_fail(err, errsz, H2D_EARG, "raw: null argument");
	if (out_sz < H2D_HCC_HDR_LEN)
		return h2d_fail(err, errsz, H2D_EBUFFER,
				"raw: output buffer %zu B smaller than the header",
				out_sz);
	for (i = 0; i < text_len; i++) {
		char c = text[i];
		int d;

		if (h2d_is_space(c))
			continue;
		d = h2d_hexval(c);
		if (d < 0)
			return h2d_fail(err, errsz, H2D_EARG,
					"raw: invalid hex character '%c' at offset %zu",
					c, i);
		if (hi < 0) {
			hi = d;
		} else {
			if (n >= out_sz)
				return h2d_fail(err, errsz, H2D_EBOUNDS,
						"raw: more than %zu bytes", out_sz);
			out[n++] = (u8)((hi << 4) | d);
			hi = -1;
		}
	}
	if (hi >= 0)
		return h2d_fail(err, errsz, H2D_EARG,
				"raw: odd number of hex digits");
	if (n < H2D_HCC_HDR_LEN)
		return h2d_fail(err, errsz, H2D_EARG,
				"raw: %zu B shorter than the 12-byte HCC header", n);
	*out_len = n;
	return h2d_validate_frame(out, n, err, errsz);
}

/* Lowercase hex dump of up to (out_sz-1)/2 frame bytes, NUL terminated. */
static void h2d_frame_to_hex(const u8 *frame, size_t frame_len,
			     char *out, size_t out_sz)
{
	static const char digits[] = "0123456789abcdef";
	size_t i, o = 0;

	if (!out || !out_sz)
		return;
	for (i = 0; i < frame_len && o + 2 < out_sz; i++) {
		out[o++] = digits[frame[i] >> 4];
		out[o++] = digits[frame[i] & 0x0f];
	}
	out[o] = '\0';
}

/* ---------------------------------------------------------------------
 * 8. Shared self-test.  Deterministic, no device, no kernel API: the same
 *    function is run by the host tool (h2d_selftest.c) and by the module's
 *    h2d_selftest sysfs attribute on the target.
 *
 *    Returns H2D_OK when every case passed; *passed / *failed report the
 *    per-case counters.  log receives one "PASS <name>" / "FAIL <name>"
 *    line per case plus a final summary (log may be NULL).
 * ------------------------------------------------------------------- */
static void h2d_selftest_result(char *log, size_t logsz, u32 *counter,
				const char *name, const char *verdict)
{
	(*counter)++;
	h2d_log(log, logsz, "%s %s\n", verdict, name);
}

#define H2D_TEST(cond, name)						\
	do {								\
		if (cond)						\
			h2d_selftest_result(log, logsz, passed, (name), "PASS"); \
		else							\
			h2d_selftest_result(log, logsz, failed, (name), "FAIL"); \
	} while (0)

static int h2d_run_selftest(struct h2d_selftest_buf *b, char *log,
			    size_t logsz, u32 *passed, u32 *failed)
{
	struct h2d_request *r;
	char err[160];
	size_t flen = 0, flen2 = 0, hlen, i;
	int rc;

	if (!b || !passed || !failed)
		return H2D_EARG;
	if (log && logsz)
		log[0] = '\0';
	*passed = 0;
	*failed = 0;

	/* ---- 8.1 builder: minimal frame, every header field ---- */
	r = &b->req;
	memset(r, 0, sizeof(*r));
	r->group = 4;
	r->id = 1;
	r->dst = 0;
	memset(err, 0, sizeof(err));
	rc = h2d_build_frame(r, b->frame, sizeof(b->frame), &flen,
			     err, sizeof(err));
	H2D_TEST(rc == H2D_OK, "build: minimal frame returns OK");
	H2D_TEST(flen == 12, "build: minimal frame total = 12 B");
	H2D_TEST(err[0] == '\0', "build: success leaves err empty");
	H2D_TEST(b->frame[0] == 0x04, "build: hdr[0] = group 4");
	H2D_TEST(b->frame[1] == 0x01, "build: hdr[1] = alloc bit | dst 0");
	H2D_TEST((b->frame[4] | (b->frame[5] << 8)) == 12,
		 "build: hdr u16@4 = payload + 12");
	H2D_TEST((b->frame[6] | (b->frame[7] << 8)) == 1,
		 "build: hdr u16@6 = id 1 (LE)");
	H2D_TEST(b->frame[8] == 0x00, "build: hdr[8] field c default 0");
	H2D_TEST(b->frame[9] == 0x00, "build: hdr[9] dst 0");
	H2D_TEST((b->frame[10] | (b->frame[11] << 8)) == 0x5a5a,
		 "build: hdr u16@0xa = tag 0x5a5a");

	/* ---- 8.2 builder: dst nibble duplication + a/b/c fields ---- */
	memset(r, 0, sizeof(*r));
	r->group = 0x0f;
	r->id = 0x1234;
	r->dst = 0x0f;
	r->field_a = 0xaa;
	r->field_b = 0xbb;
	r->field_c = 0xcc;
	r->payload_len = 4;
	r->payload[0] = 0xde;
	r->payload[1] = 0xad;
	r->payload[2] = 0xbe;
	r->payload[3] = 0xef;
	rc = h2d_build_frame(r, b->frame, sizeof(b->frame), &flen,
			     err, sizeof(err));
	H2D_TEST(rc == H2D_OK && flen == 16, "build: 4-byte payload total = 16 B");
	H2D_TEST(b->frame[1] == 0xf1, "build: dst 0x0f -> hdr[1] = 0xf1");
	H2D_TEST(b->frame[9] == 0x0f, "build: dst 0x0f -> hdr[9] = 0x0f");
	H2D_TEST(b->frame[2] == 0xaa && b->frame[3] == 0xbb && b->frame[8] == 0xcc,
		 "build: fields a/b/c at hdr[2]/[3]/[8]");
	H2D_TEST(memcmp(b->frame + 12, r->payload, 4) == 0,
		 "build: payload copied at hdr+0x0c");
	H2D_TEST((b->frame[4] | (b->frame[5] << 8)) == 16,
		 "build: hdr u16@4 = 16");
	H2D_TEST((b->frame[6] | (b->frame[7] << 8)) == 0x1234,
		 "build: hdr u16@6 = id 0x1234 (LE)");

	/* ---- 8.3 builder: bounds and refusals ---- */
	memset(r, 0, sizeof(*r));
	r->group = 4;
	r->id = 1;
	r->dst = 0;
	r->payload_len = H2D_HCC_MAX_PAYLOAD;
	for (i = 0; i < H2D_HCC_MAX_PAYLOAD; i++)
		r->payload[i] = (u8)(i & 0xff);
	rc = h2d_build_frame(r, b->frame, sizeof(b->frame), &flen,
			     err, sizeof(err));
	H2D_TEST(rc == H2D_OK && flen == H2D_HCC_MAX_TOTAL,
		 "build: max payload 0x674 -> total 0x680");
	H2D_TEST((b->frame[4] | (b->frame[5] << 8)) == 0x680,
		 "build: max frame hdr u16@4 = 0x680");
	H2D_TEST(b->frame[H2D_HCC_MAX_TOTAL - 1] ==
		 (u8)((H2D_HCC_MAX_PAYLOAD - 1) & 0xff),
		 "build: max payload last byte copied");

	r->payload_len = H2D_HCC_MAX_PAYLOAD + 1;
	rc = h2d_build_frame(r, b->frame2, sizeof(b->frame2), &flen2,
			     err, sizeof(err));
	H2D_TEST(rc == H2D_EBOUNDS, "build: payload 0x675 refused (EBOUNDS)");

	memset(r, 0, sizeof(*r));
	r->group = 4;
	r->id = H2D_HCC_REFUSED_ID;
	r->dst = 0;
	rc = h2d_build_frame(r, b->frame2, sizeof(b->frame2), &flen2,
			     err, sizeof(err));
	H2D_TEST(rc == H2D_EREFUSED, "build: id 5 refused (EREFUSED)");

	r->id = 1;
	r->group = H2D_HCC_GROUP_MAX + 1;
	rc = h2d_build_frame(r, b->frame2, sizeof(b->frame2), &flen2,
			     err, sizeof(err));
	H2D_TEST(rc == H2D_EBOUNDS, "build: group 16 refused (EBOUNDS)");

	r->group = 4;
	r->dst = H2D_HCC_DST_MAX + 1;
	rc = h2d_build_frame(r, b->frame2, sizeof(b->frame2), &flen2,
			     err, sizeof(err));
	H2D_TEST(rc == H2D_EBOUNDS, "build: dst 16 refused (EBOUNDS)");

	/* capacity check + no write past the frame */
	memset(b->frame, 0xaa, sizeof(b->frame));
	memset(r, 0, sizeof(*r));
	r->group = 1;
	r->id = 2;
	r->dst = 3;
	rc = h2d_build_frame(r, b->frame, H2D_HCC_HDR_LEN - 1, &flen,
			     err, sizeof(err));
	H2D_TEST(rc == H2D_EBUFFER, "build: buffer too small refused (EBUFFER)");
	H2D_TEST(b->frame[0] == 0xaa, "build: buffer-too-small leaves output untouched");

	memset(b->frame2, 0xaa, sizeof(b->frame2));
	rc = h2d_build_frame(r, b->frame2, sizeof(b->frame2), &flen,
			     err, sizeof(err));
	H2D_TEST(rc == H2D_OK && b->frame2[12] == 0xaa,
		 "build: writes exactly total bytes (no padding)");

	/* ---- 8.4 validator ---- */
	H2D_TEST(h2d_validate_frame(b->frame2, flen, err, sizeof(err)) == H2D_OK,
		 "validate: canonical frame accepted");
	H2D_TEST(h2d_validate_frame(b->frame2, H2D_HCC_HDR_LEN - 1,
				    err, sizeof(err)) == H2D_EARG,
		 "validate: short frame refused");
	H2D_TEST(h2d_validate_frame(b->frame2, H2D_HCC_MAX_TOTAL + 1,
				    err, sizeof(err)) == H2D_EBOUNDS,
		 "validate: total > 0x680 refused");

	memcpy(b->frame, b->frame2, flen);
	b->frame[4] = (u8)((flen + 1) & 0xff);
	H2D_TEST(h2d_validate_frame(b->frame, flen, err, sizeof(err)) == H2D_EARG,
		 "validate: declared length mismatch refused");

	memcpy(b->frame, b->frame2, flen);
	b->frame[10] ^= 0xff;
	H2D_TEST(h2d_validate_frame(b->frame, flen, err, sizeof(err)) == H2D_EARG,
		 "validate: bad transport tag refused");

	memcpy(b->frame, b->frame2, flen);
	b->frame[6] = H2D_HCC_REFUSED_ID;
	b->frame[7] = 0;
	H2D_TEST(h2d_validate_frame(b->frame, flen, err, sizeof(err)) ==
		 H2D_EREFUSED, "validate: raw id 5 refused (EREFUSED)");

	memcpy(b->frame, b->frame2, flen);
	b->frame[9] = 0x0e;	/* hdr[1] high nibble is 3 for this frame */
	H2D_TEST(h2d_validate_frame(b->frame, flen, err, sizeof(err)) == H2D_EARG,
		 "validate: hdr[9] != hdr[1]>>4 refused");

	memcpy(b->frame, b->frame2, flen);
	b->frame[1] = 0x00;	/* alloc bit cleared: allowed (host bookkeeping) */
	b->frame[9] = 0x00;
	H2D_TEST(h2d_validate_frame(b->frame, flen, err, sizeof(err)) == H2D_OK,
		 "validate: alloc bit not required on a raw frame");

	/* ---- 8.5 builder-syntax parser ---- */
	memset(err, 0, sizeof(err));
	rc = h2d_parse_request("group=4 id=1 dst=0 payload=00\n",
			       strlen("group=4 id=1 dst=0 payload=00\n"),
			       r, err, sizeof(err));
	H2D_TEST(rc == H2D_OK && r->group == 4 && r->id == 1 && r->dst == 0,
		 "parse: group/id/dst");
	H2D_TEST(rc == H2D_OK && r->payload_len == 1 && r->payload[0] == 0x00,
		 "parse: payload hex");
	H2D_TEST(rc == H2D_OK && r->field_a == 0 && r->field_b == 0 &&
		 r->field_c == 0, "parse: fields a/b/c default 0");

	rc = h2d_parse_request("  group=0x0f\tid=0x1234\ndst=0x0f "
			      "a=170 b=0xbb c=204 payload=DEadBEEF \n",
			      strlen("  group=0x0f\tid=0x1234\ndst=0x0f "
				     "a=170 b=0xbb c=204 payload=DEadBEEF \n"),
			      r, err, sizeof(err));
	H2D_TEST(rc == H2D_OK && r->group == 0x0f && r->id == 0x1234 &&
		 r->dst == 0x0f, "parse: whitespace + 0x-prefixed numbers");
	H2D_TEST(r->field_a == 170 && r->field_b == 0xbb && r->field_c == 204,
		 "parse: fields a/b/c overrides");
	H2D_TEST(r->payload_len == 4 && r->payload[0] == 0xde &&
		 r->payload[1] == 0xad && r->payload[2] == 0xbe &&
		 r->payload[3] == 0xef, "parse: mixed-case hex payload");

	H2D_TEST(h2d_parse_request("id=1 dst=0", strlen("id=1 dst=0"), r,
				   err, sizeof(err)) !=
		 H2D_OK, "parse: missing group refused");
	H2D_TEST(h2d_parse_request("group=4 group=4 id=1 dst=0",
				   strlen("group=4 group=4 id=1 dst=0"), r,
				   err, sizeof(err)) != H2D_OK,
		 "parse: duplicate key refused");
	H2D_TEST(h2d_parse_request("group=4 id=1 dst=0 zz=1",
				   strlen("group=4 id=1 dst=0 zz=1"), r,
				   err, sizeof(err)) != H2D_OK,
		 "parse: unknown key refused");
	H2D_TEST(h2d_parse_request("group=4 id=5 dst=0",
				   strlen("group=4 id=5 dst=0"), r, err,
				   sizeof(err)) == H2D_EREFUSED,
		 "parse: id 5 refused (EREFUSED)");
	H2D_TEST(h2d_parse_request("group=4 id=1 dst=0 payload=abc",
				   strlen("group=4 id=1 dst=0 payload=abc"), r,
				   err, sizeof(err)) != H2D_OK,
		 "parse: odd payload hex refused");
	H2D_TEST(h2d_parse_request("group=4 id=1 dst=0 payload=zz",
				   strlen("group=4 id=1 dst=0 payload=zz"), r,
				   err, sizeof(err)) != H2D_OK,
		 "parse: non-hex payload refused");
	H2D_TEST(h2d_parse_request("group=16 id=1 dst=0",
				   strlen("group=16 id=1 dst=0"), r, err,
				   sizeof(err)) != H2D_OK,
		 "parse: group 16 refused");
	H2D_TEST(h2d_parse_request("", 0, r, err, sizeof(err)) != H2D_OK,
		 "parse: empty request refused");

	/* max payload accepted / 0x675 refused through the parser */
	memset(b->hex, 0, sizeof(b->hex));
	memcpy(b->hex, "group=4 id=1 dst=0 payload=",
	       strlen("group=4 id=1 dst=0 payload="));
	hlen = strlen(b->hex);
	for (i = 0; i < H2D_HCC_MAX_PAYLOAD * 2; i++)
		b->hex[hlen + i] = 'a';
	b->hex[hlen + H2D_HCC_MAX_PAYLOAD * 2] = '\0';
	rc = h2d_parse_request(b->hex, strlen(b->hex), r, err, sizeof(err));
	H2D_TEST(rc == H2D_OK && r->payload_len == H2D_HCC_MAX_PAYLOAD,
		 "parse: payload 0x674 hex accepted");
	/* append two more digits -> 0x675 bytes */
	b->hex[hlen + H2D_HCC_MAX_PAYLOAD * 2] = 'a';
	b->hex[hlen + H2D_HCC_MAX_PAYLOAD * 2 + 1] = 'a';
	b->hex[hlen + H2D_HCC_MAX_PAYLOAD * 2 + 2] = '\0';
	rc = h2d_parse_request(b->hex, strlen(b->hex), r, err, sizeof(err));
	H2D_TEST(rc == H2D_EBOUNDS, "parse: payload 0x675 refused (EBOUNDS)");

	/* ---- 8.6 parse -> build -> validate round trip ---- */
	rc = h2d_parse_request("group=0x03 id=0x1f dst=0x02 a=1 b=2 c=3 "
			      "payload=00112233445566778899aabbccddeeff",
			      strlen("group=0x03 id=0x1f dst=0x02 a=1 b=2 c=3 "
				     "payload=00112233445566778899aabbccddeeff"),
			      r, err, sizeof(err));
	H2D_TEST(rc == H2D_OK, "roundtrip: request parsed");
	rc = h2d_build_frame(r, b->frame, sizeof(b->frame), &flen,
			     err, sizeof(err));
	H2D_TEST(rc == H2D_OK && flen == 28, "roundtrip: 16-B payload -> 28 B frame");
	H2D_TEST(h2d_validate_frame(b->frame, flen, err, sizeof(err)) == H2D_OK,
		 "roundtrip: built frame validates");
	H2D_TEST(b->frame[0] == 3 && b->frame[2] == 1 && b->frame[3] == 2 &&
		 b->frame[8] == 3 && b->frame[9] == 2,
		 "roundtrip: fields land in the header");

	/* ---- 8.7 raw-hex path ---- */
	h2d_frame_to_hex(b->frame, flen, b->hex, sizeof(b->hex));
	H2D_TEST(strlen(b->hex) == flen * 2, "raw: frame-to-hex length");
	memset(b->frame2, 0, sizeof(b->frame2));
	rc = h2d_parse_raw_frame(b->hex, strlen(b->hex), b->frame2,
				 sizeof(b->frame2), &flen2, err, sizeof(err));
	H2D_TEST(rc == H2D_OK && flen2 == flen &&
		 memcmp(b->frame2, b->frame, flen) == 0,
		 "raw: canonical hex round trips");

	hlen = strlen(b->hex);
	memmove(b->hex + 2, b->hex, hlen + 1);
	b->hex[0] = ' ';
	b->hex[1] = '\t';
	b->hex[hlen + 2] = '\n';
	b->hex[hlen + 3] = '\0';
	rc = h2d_parse_raw_frame(b->hex, hlen + 3, b->frame2,
				 sizeof(b->frame2), &flen2, err, sizeof(err));
	H2D_TEST(rc == H2D_OK && flen2 == flen,
		 "raw: whitespace tolerated");

	H2D_TEST(h2d_parse_raw_frame("zz", 2, b->frame2, sizeof(b->frame2),
				     &flen2, err, sizeof(err)) != H2D_OK,
		 "raw: non-hex refused");
	H2D_TEST(h2d_parse_raw_frame("abc", 3, b->frame2, sizeof(b->frame2),
				     &flen2, err, sizeof(err)) != H2D_OK,
		 "raw: odd digit count refused");
	H2D_TEST(h2d_parse_raw_frame("00112233", 8, b->frame2,
				     sizeof(b->frame2), &flen2, err,
				     sizeof(err)) != H2D_OK,
		 "raw: shorter than 12 B refused");

	/* raw frame with a wrong declared length and with id 5 */
	memcpy(b->frame2, b->frame, flen);
	b->frame2[4] = (u8)((flen + 1) & 0xff);
	memset(b->hex, 0, sizeof(b->hex));
	h2d_frame_to_hex(b->frame2, flen, b->hex, sizeof(b->hex));
	H2D_TEST(h2d_parse_raw_frame(b->hex, strlen(b->hex), b->frame2,
				     sizeof(b->frame2), &flen2, err,
				     sizeof(err)) == H2D_EARG,
		 "raw: declared length mismatch refused");
	memcpy(b->frame2, b->frame, flen);
	b->frame2[6] = H2D_HCC_REFUSED_ID;
	b->frame2[7] = 0;
	h2d_frame_to_hex(b->frame2, flen, b->hex, sizeof(b->hex));
	H2D_TEST(h2d_parse_raw_frame(b->hex, strlen(b->hex), b->frame2,
				     sizeof(b->frame2), &flen2, err,
				     sizeof(err)) == H2D_EREFUSED,
		 "raw: id 5 refused");

	/* ---- 8.8 summary ---- */
	h2d_log(log, logsz, "SUMMARY: %u passed, %u failed\n", *passed, *failed);
	return *failed ? H2D_EARG : H2D_OK;
}

#endif /* _PCIET_H2D_FRAME_H */
