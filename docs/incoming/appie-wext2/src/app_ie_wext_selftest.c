/* SPDX-License-Identifier: GPL-2.0 */
/*
 * app_ie_wext_selftest.c - host KAT for the vendor WEXT set_vendor_ie model
 * (lane appie-wext2, task-239).  Host C compiler only: no kernel, no device,
 * no vendor artifact.  Exits non-zero on any failure.
 */
#include "hi5622_app_ie_wext.h"

#include <stdio.h>
#include <string.h>

static unsigned int checks, fails;

static void ck(int cond, const char *name)
{
	checks++;
	if (cond) {
		printf("PASS %s\n", name);
	} else {
		fails++;
		printf("FAIL %s\n", name);
	}
}

static void ck_u32(u32 got, u32 exp, const char *name)
{
	checks++;
	if (got == exp) {
		printf("PASS %s (= %u)\n", name, got);
	} else {
		fails++;
		printf("FAIL %s: got %u expected %u\n", name, got, exp);
	}
}

/* The WEXT userspace request of one app-IE set: cmd, ie_len, type, pad[3], ie */
static size_t make_wext(u8 *buf, u8 type, const u8 *ie, size_t ie_len)
{
	memset(buf, 0, 12u + ie_len);
	buf[0] = (u8)(HI5622_AIW_WEXT_CMD_SET_VENDOR_IE & 0xffu);
	buf[1] = (u8)((HI5622_AIW_WEXT_CMD_SET_VENDOR_IE >> 8) & 0xffu);
	buf[2] = (u8)((HI5622_AIW_WEXT_CMD_SET_VENDOR_IE >> 16) & 0xffu);
	buf[3] = (u8)((HI5622_AIW_WEXT_CMD_SET_VENDOR_IE >> 24) & 0xffu);
	buf[4] = (u8)(ie_len & 0xffu);
	buf[5] = (u8)((ie_len >> 8) & 0xffu);
	buf[6] = (u8)((ie_len >> 16) & 0xffu);
	buf[7] = (u8)((ie_len >> 24) & 0xffu);
	buf[8] = type;
	if (ie_len)
		memcpy(buf + 12u, ie, ie_len);
	return 12u + ie_len;
}

static const u8 k_ie[12] = {
	0xddu, 0x0au, 0x00u, 0x11u, 0x22u, 0x33u,
	0x61u, 0x70u, 0x70u, 0x69u, 0x65u, 0x21u
};

int main(void)
{
	u8 ubuf[12u + HI5622_AIW_WEXT_IE_MAX];
	struct hi5622_aiw_req req;
	struct hi5622_aiw_mirror mirror;
	struct hi5622_aiw_plan plan;
	u8 wire[HI5622_AIW_WIRE_LEN + 8u];
	size_t ulen, wire_len, ie_len;
	const u8 ctx[3] = { 4u, 0u, 0u };
	u8 strip[32];
	unsigned int mlen = 0xffffu;
	int rc;

	printf("appie-wext2 host KAT - the vendor WEXT set_vendor_ie model\n");

	/* ---- W1: constants / ABI ---------------------------------------- */
	ck_u32(HI5622_AIW_WEXT_CMD_SET_VENDOR_IE, 0x8eecu, "WEXT cmd is 0x8eec");
	ck_u32(HI5622_AIW_WIRE_LEN, 536u, "cfg-event frame is 536 B");
	ck_u32(HI5622_AIW_IE_OFF, 40u, "IE area starts at wire+40");
	ck_u32(HI5622_AIW_MIRROR_TYPES, 20u, "20 mirror classes");
	ck_u32(HI5622_AIW_EVENT_SET_VENDOR_IE, 0x276u, "event is 0x0276");

	/* ---- parse: the userspace request ------------------------------ */
	ulen = make_wext(ubuf, 4u, k_ie, sizeof(k_ie));
	rc = hi5622_aiw_wext_parse(ubuf, ulen, &req);
	ck(rc == 0 && req.valid, "parse: valid request accepted");
	ck_u32(req.ie_len, 12u, "parse: ie_len");
	ck_u32(req.type, 4u, "parse: type");
	ck_u32(req.mode, HI5622_AIW_MODE_SAVE, "parse: mode is 0 (path A)");
	ck(memcmp(req.ie, k_ie, sizeof(k_ie)) == 0, "parse: IE bytes copied");
	ck(hi5622_aiw_wext_parse(NULL, ulen, &req) == -EINVAL,
	   "parse: NULL buffer refused");
	ck(hi5622_aiw_wext_parse(ubuf, 8u, &req) == -EINVAL,
	   "parse: short header refused");
	ubuf[0] = 0xedu;	/* wrong cmd */
	ck(hi5622_aiw_wext_parse(ubuf, ulen, &req) == -EINVAL,
	   "parse: wrong cmd refused");
	ulen = make_wext(ubuf, 4u, k_ie, sizeof(k_ie));
	ubuf[4] = 0xf1u;	/* ie_len = 0x1f1 pad: 497 > 496 */
	ubuf[5] = 0x01u;
	ck(hi5622_aiw_wext_parse(ubuf, ulen, &req) == -EINVAL,
	   "parse: ie_len > 496 refused");
	ulen = make_wext(ubuf, 4u, k_ie, sizeof(k_ie));
	ubuf[8] = 20u;
	ck(hi5622_aiw_wext_parse(ubuf, ulen, &req) == -EINVAL,
	   "parse: type > 19 refused");
	ulen = make_wext(ubuf, 4u, k_ie, sizeof(k_ie));
	ck(hi5622_aiw_wext_parse(ubuf, ulen - 1u, &req) == -EINVAL,
	   "parse: truncated IE bytes refused");

	/* ---- the vendor 0x7f strip ------------------------------------- */
	memset(strip, 0, sizeof(strip));
	strip[0] = 0xddu; strip[1] = 0x01u; strip[2] = 0xaau;
	strip[3] = 0x7fu; strip[4] = 0x02u; strip[5] = 0x01u; strip[6] = 0x02u;
	strip[7] = 0xddu; strip[8] = 0x01u; strip[9] = 0xccu;
	ie_len = 10u;
	rc = hi5622_aiw_strip_7f(strip, &ie_len);
	ck(rc == 0 && ie_len == 6u, "strip: 0x7f element removed");
	ck(strip[0] == 0xddu && strip[1] == 0x01u && strip[2] == 0xaau &&
	   strip[3] == 0xddu && strip[4] == 0x01u && strip[5] == 0xccu,
	   "strip: surrounding elements preserved");
	ie_len = 4u;
	strip[0] = 0xddu; strip[1] = 0x02u; strip[2] = 0x01u; strip[3] = 0x02u;
	rc = hi5622_aiw_strip_7f(strip, &ie_len);
	ck(rc == 0 && ie_len == 4u, "strip: no 0x7f leaves the list unchanged");
	ie_len = 3u;
	strip[0] = 0xddu; strip[1] = 0x05u; strip[2] = 0x01u;
	ck(hi5622_aiw_strip_7f(strip, &ie_len) == -EINVAL,
	   "strip: malformed TLV list refused");

	/* ---- the host mirror (mac_vap_save_app_ie semantics) ----------- */
	memset(&mirror, 0, sizeof(mirror));
	rc = hi5622_aiw_mirror_save(&mirror, 4u, k_ie, sizeof(k_ie), &mlen);
	ck(rc == 0 && mlen == 12u && mirror.slot[4].used &&
	   mirror.slot[4].len == 12u && mirror.slot[4].cap >= 12u &&
	   memcmp(mirror.slot[4].ie, k_ie, sizeof(k_ie)) == 0,
	   "mirror: class 4 save (len/cap/payload)");
	rc = hi5622_aiw_mirror_save(&mirror, 4u, k_ie, 0u, &mlen);
	ck(rc == 0 && mlen == 0u && !mirror.slot[4].used &&
	   mirror.slot[4].len == 0u && mirror.slot[4].cap == 0u,
	   "mirror: ie_len 0 clears the class (free/len/cap)");
	rc = hi5622_aiw_mirror_save(&mirror, 9u, k_ie, 6u, NULL);
	rc = hi5622_aiw_mirror_save(&mirror, 9u, k_ie, sizeof(k_ie), &mlen);
	ck(rc == 0 && mlen == 12u && mirror.slot[9].len == 12u &&
	   mirror.slot[9].cap >= 12u, "mirror: growth keeps the new length");
	ck(hi5622_aiw_mirror_save(&mirror, 20u, k_ie, 1u, NULL) == -EINVAL,
	   "mirror: type > 19 refused");
	ck(hi5622_aiw_mirror_save(&mirror, 4u, k_ie,
				  HI5622_AIW_MIRROR_IE_MAX + 1u, NULL) == -EINVAL,
	   "mirror: ie_len > 608 refused");

	/* ---- the cfg-event plan / fail-closed gate --------------------- */
	ck(hi5622_aiw_plan(2u, 4u, ctx, 1u, k_ie, sizeof(k_ie), &plan) ==
	   -EINVAL, "plan: mode > 1 refused");
	ck(hi5622_aiw_plan(0u, 4u, ctx, 1u, k_ie, sizeof(k_ie), &plan) == 0 &&
	   !plan.post, "plan: mode 0 is the sibling direct frame (no envelope)");
	ck(hi5622_aiw_plan(1u, 20u, ctx, 1u, k_ie, sizeof(k_ie), &plan) ==
	   -EINVAL, "plan: type > 19 refused");
	ck(hi5622_aiw_plan(1u, 4u, NULL, 1u, k_ie, sizeof(k_ie), &plan) ==
	   -EINVAL, "plan: NULL ctx refused");
	{
		const u8 badctx[3] = { 4u, 0x10u, 0u };
		ck(hi5622_aiw_plan(1u, 4u, badctx, 1u, k_ie, sizeof(k_ie),
				   &plan) == -EINVAL,
		   "plan: ctx1 above 0xf refused");
	}
	rc = hi5622_aiw_plan(1u, 4u, ctx, 0x11223344u, k_ie, sizeof(k_ie),
			     &plan);
	ck(rc == 0 && plan.post && plan.ie_len == sizeof(k_ie) &&
	   plan.type == 4u, "plan: WEXT envelope plan accepted");

	/* ---- the builder: geometry and payload ------------------------ */
	ck(hi5622_aiw_cfg_event_build(wire, HI5622_AIW_WIRE_LEN - 1u,
				      &wire_len, &plan) == -ENOSPC,
	   "build: short buffer refused");
	{
		struct hi5622_aiw_plan off = plan;
		off.post = false;
		ck(hi5622_aiw_cfg_event_build(wire, sizeof(wire), &wire_len,
					      &off) == -EINVAL,
		   "build: unrequested plan refused");
	}
	rc = hi5622_aiw_cfg_event_build(wire, sizeof(wire), &wire_len, &plan);
	ck(rc == 0 && wire_len == HI5622_AIW_WIRE_LEN, "build: 536 B frame");
	ck_u32(wire[0], 0x02u, "hdr: group 2 in the b0 low nibble");
	ck_u32(wire[1], 0x01u, "hdr: b1 = alloc nibble (+ ctx2 high nibble 0)");
	ck_u32(wire[2], 0x00u, "hdr: b2 = ctx1");
	ck_u32(wire[3], 0x04u, "hdr: b3 = ctx0 (= the VAP index)");
	ck_u32((u32)wire[4] | ((u32)wire[5] << 8), 0x0218u, "hdr: total 0x0218");
	ck_u32((u32)wire[6] | ((u32)wire[7] << 8), 0u, "hdr: msg id 0 (g2/id0)");
	ck_u32((u32)wire[10] | ((u32)wire[11] << 8), 0x5a5au, "hdr: tag 0x5a5a");
	ck_u32((u32)wire[12] | ((u32)wire[13] << 8) |
	       ((u32)wire[14] << 16) | ((u32)wire[15] << 24), 0u,
	       "rec: transport cookie 0");
	ck_u32((u32)wire[16], HI5622_AIW_RPC_TAG, "rpc: tag 1");
	ck_u32((u32)wire[20] | ((u32)wire[21] << 8), 0u, "rpc: reserved 0");
	ck_u32((u32)wire[22] | ((u32)wire[23] << 8), 0x01fcu,
	       "rpc: cfg msg len 508");
	ck_u32((u32)wire[24] | ((u32)wire[25] << 8) |
	       ((u32)wire[26] << 16) | ((u32)wire[27] << 24), 0x11223344u,
	       "rpc: sequence");
	ck_u32((u32)wire[28] | ((u32)wire[29] << 8), 0x0276u,
	       "cfg: event SET_VENDOR_IE 0x0276");
	ck_u32((u32)wire[30] | ((u32)wire[31] << 8), 0x01f8u,
	       "cfg: payload len 504");
	ck_u32((u32)wire[32] | ((u32)wire[33] << 8) |
	       ((u32)wire[34] << 16) | ((u32)wire[35] << 24), 12u,
	       "req: ie_len");
	ck_u32(wire[36], 4u, "req: type");
	ck_u32(wire[37], 0u, "req: mode 0 (path A)");
	ck(memcmp(wire + HI5622_AIW_IE_OFF, k_ie, sizeof(k_ie)) == 0,
	   "req: IE bytes at wire+40");
	{
		size_t i;
		int zero = 1;
		for (i = HI5622_AIW_IE_OFF + sizeof(k_ie);
		     i < HI5622_AIW_WIRE_LEN; i++)
			if (wire[i])
				zero = 0;
		ck(zero, "req: IE area zero padded to 536 B");
	}

	/* ---- the byte-exact vendor vector ----------------------------- */
	{
		static const u8 exp[52] = {
			0x02u, 0x01u, 0x00u, 0x04u, 0x18u, 0x02u, 0x00u, 0x00u,
			0x00u, 0x00u, 0x5au, 0x5au, 0x00u, 0x00u, 0x00u, 0x00u,
			0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0xfcu, 0x01u,
			0x44u, 0x33u, 0x22u, 0x11u, 0x76u, 0x02u, 0xf8u, 0x01u,
			0x0cu, 0x00u, 0x00u, 0x00u, 0x04u, 0x00u, 0x00u, 0x00u,
			0xddu, 0x0au, 0x00u, 0x11u, 0x22u, 0x33u, 0x61u, 0x70u,
			0x70u, 0x69u, 0x65u, 0x21u
		};
		ck(memcmp(wire, exp, sizeof(exp)) == 0,
		   "build: byte-exact WEXT envelope vector");
	}
	/* sequence is carried, everything else byte-identical */
	{
		u8 wire2[HI5622_AIW_WIRE_LEN];
		u8 exp2[HI5622_AIW_WIRE_LEN];
		struct hi5622_aiw_plan p2;

		memcpy(exp2, wire, sizeof(exp2));
		exp2[24] = 0x46u;
		exp2[25] = 0x33u;
		ck(hi5622_aiw_plan(1u, 4u, ctx, 0x11223346u, k_ie,
				   sizeof(k_ie), &p2) == 0 &&
		   hi5622_aiw_cfg_event_build(wire2, sizeof(wire2), &wire_len,
					      &p2) == 0, "build: second plan");
		ck(memcmp(wire2, exp2, sizeof(exp2)) == 0,
		   "build: only the sequence field changes");
	}

	/* ---- the clear form (ie_len 0) and the 496 B maximum ---------- */
	{
		struct hi5622_aiw_plan clr;
		u8 w2[HI5622_AIW_WIRE_LEN];
		u8 big[HI5622_AIW_WEXT_IE_MAX];
		size_t n = 0, off = 0;

		while (off + 2u <= sizeof(big) && n < HI5622_AIW_WEXT_IE_MAX) {
			size_t el = sizeof(big) - off - 2u;
			if (el > 20u)
				el = 20u;
			big[off] = 0xddu;
			big[off + 1u] = (u8)el;
			memset(big + off + 2u, 0x5au, el);
			off += 2u + el;
		}
		n = off;
		ck(hi5622_aiw_plan(1u, 4u, ctx, 1u, NULL, 0u, &clr) == 0 &&
		   hi5622_aiw_cfg_event_build(w2, sizeof(w2), &wire_len,
					      &clr) == 0 &&
		   wire_len == HI5622_AIW_WIRE_LEN &&
		   w2[32] == 0u && w2[33] == 0u && w2[36] == 4u,
		   "build: clear form (ie_len 0) keeps the 536 B frame");
		ck(hi5622_aiw_plan(1u, 4u, NULL, 1u, big, n, &clr) == -EINVAL,
		   "plan: NULL ctx refused (max IE)");
		ck(hi5622_aiw_plan(1u, 4u, ctx, 1u, big, n, &clr) == 0 &&
		   clr.ie_len == n && n <= HI5622_AIW_WEXT_IE_MAX,
		   "plan: large well-formed IE list accepted");
		ck(hi5622_aiw_plan(1u, 4u, ctx, 1u, k_ie,
				   HI5622_AIW_WEXT_IE_MAX + 1u, &clr) ==
		   -EINVAL, "plan: ie_len > 496 refused");
	}

	/* ---- status line ---------------------------------------------- */
	{
		char line[256];
		rc = hi5622_aiw_status_line(1u, 1u, 4u, 0, 3u, 1u, 12u, 1u,
					    line, sizeof(line));
		ck(rc > 0 && strstr(line, "mode=1 (wext-event)") &&
		   strstr(line, "when=1 (post-enable)") &&
		   strstr(line, "mirror_len=12"), "status line reports the port");
	}

	printf("appie-wext2 host KAT: %u checks, %u failure(s)\n",
	       checks, fails);
	return fails ? 1 : 0;
}
