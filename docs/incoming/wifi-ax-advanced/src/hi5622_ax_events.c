// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_ax_events.c - portable model of the vendor 802.11ax config-event
 * frames (see hi5622_ax_events.h; lane wifi-ax-advanced, task-195).
 *
 * Every constant in this file is decoded in ../TABLES.md and re-asserted by
 * ../tools/fw_ax_advanced_re.py --selftest from the pinned local artifacts.
 * HOST-ONLY: no register, no firmware, no device.
 */
#include "hi5622_ax_events.h"

#define HI5622_AX_TOKEN_AREA	8	/* body+8 = first token */

static int put_u16(u8 *p, u16 v)
{
	p[0] = v & 0xFF;
	p[1] = (v >> 8) & 0xFF;
	return 0;
}

static int put_u32(u8 *p, u32 v)
{
	p[0] = v & 0xFF;
	p[1] = (v >> 8) & 0xFF;
	p[2] = (v >> 16) & 0xFF;
	p[3] = (v >> 24) & 0xFF;
	return 0;
}

/* The decoded key/length contract (TABLES T2/T3). */
static int hi5622_ax_key_len_ok(u16 key, u16 body_len)
{
	switch (key) {
	case HI5622_AX_KEY_TWT_ENABLE:
	case HI5622_AX_KEY_HE_GI:
	case HI5622_AX_KEY_BFEE:
	case HI5622_AX_KEY_BFER:
	case HI5622_AX_KEY_TRIG_SU_BF_FB:
	case HI5622_AX_KEY_CYC_SCHE:
	case HI5622_AX_KEY_SR_ENABLE:
		return body_len == 1 ? 0 : -EINVAL;
	case HI5622_AX_KEY_EXT_TWT_IE:
		return body_len == 4 ? 0 : -EINVAL;
	case HI5622_AX_KEY_MU_EDCA_BASE + 0:
	case HI5622_AX_KEY_MU_EDCA_BASE + 1:
	case HI5622_AX_KEY_MU_EDCA_BASE + 2:
	case HI5622_AX_KEY_MU_EDCA_BASE + 3:
	case HI5622_AX_KEY_MU_EDCA_BASE + 4:
		return body_len == HI5622_AX_LEN_MU_EDCA ? 0 : -EINVAL;
	case HI5622_AX_KEY_ALG:
		return (body_len >= HI5622_AX_ALG_MIN &&
			body_len <= HI5622_AX_ALG_MAX) ? 0 : -EINVAL;
	default:
		return -EOPNOTSUPP;	/* never guess a key */
	}
}

int hi5622_ax_event(u8 field_b, u16 key, const u8 *body, u16 body_len,
		    struct hi5622_ax_frame *out)
{
	u8 *b;
	u16 total;
	int ret;

	if (!out || (!body && body_len))
		return -EINVAL;
	ret = hi5622_ax_key_len_ok(key, body_len);
	if (ret)
		return ret;
	total = HI5622_AX_ENV_LEN + body_len;
	if (total > HI5622_AX_FRAME_MAX)
		return -EINVAL;
	b = out->buf;
	memset(b, 0, sizeof(out->buf));
	/* 12-B HCC header: g0 id1, dst 0, alloc 1, field B, total */
	b[0] = 0x00;
	b[1] = 0x01;
	b[3] = field_b;
	put_u16(b + 4, total);
	put_u16(b + 6, 1);
	put_u16(b + 10, 0x5A5A);	/* the fired envelope tag */
	/* 8-B token: the fired envelope is {00000000 01000000}; semantics [U] */
	put_u32(b + 12, 0);
	put_u32(b + 16, 1);
	/* key, length, body, 4x 0xFF pad */
	put_u16(b + 20, key);
	put_u16(b + 22, body_len);
	memcpy(b + 24, body, body_len);
	memset(b + 24 + body_len, 0xFF, 4);
	out->len = total;
	return 0;
}

int hi5622_ax_twt_enable(struct hi5622_ax_frame *out, u8 field_b, u8 flags)
{
	u8 body[1];

	/* vendor validator wal_hipriv_twt_enable accepts only {1,3} */
	if (flags != 1 && flags != 3)
		return -EINVAL;
	body[0] = flags;
	return hi5622_ax_event(field_b, HI5622_AX_KEY_TWT_ENABLE, body, 1, out);
}

int hi5622_ax_ext_twt_ie(struct hi5622_ax_frame *out, u8 field_b, bool on)
{
	u8 body[4];

	put_u32(body, on ? 1 : 0);
	return hi5622_ax_event(field_b, HI5622_AX_KEY_EXT_TWT_IE, body, 4, out);
}

int hi5622_ax_mu_edca_param(struct hi5622_ax_frame *out, u8 field_b,
			    u8 selector, u8 ac, u32 value)
{
	u8 body[HI5622_AX_LEN_MU_EDCA];

	if (selector < HI5622_AX_MU_EDCA_SEL_MIN ||
	    selector > HI5622_AX_MU_EDCA_SEL_MAX)
		return -EINVAL;
	if (ac > 3)			/* firmware worker: ac > 3 -> log, no write */
		return -EINVAL;
	memset(body, 0, sizeof(body));
	put_u16(body + 0, selector);
	put_u32(body + 4, ac);
	put_u32(body + 8, value);
	return hi5622_ax_event(field_b, selector, body, sizeof(body), out);
}

int hi5622_ax_he_gi(struct hi5622_ax_frame *out, u8 field_b, u8 gi)
{
	u8 body[1];

	/* host builder valid set {1,2,4}; 3 is refused */
	if (gi != 1 && gi != 2 && gi != 4)
		return -EINVAL;
	body[0] = gi;
	return hi5622_ax_event(field_b, HI5622_AX_KEY_HE_GI, body, 1, out);
}

int hi5622_ax_bfee(struct hi5622_ax_frame *out, u8 field_b, u8 mode)
{
	u8 body[1];

	if (mode > 1)
		return -EINVAL;
	body[0] = mode;
	return hi5622_ax_event(field_b, HI5622_AX_KEY_BFEE, body, 1, out);
}

int hi5622_ax_bfer(struct hi5622_ax_frame *out, u8 field_b, u8 mode)
{
	u8 body[1];

	if (mode > 3)			/* firmware tbb is 4-way, >3 -> log */
		return -EINVAL;
	body[0] = mode;
	return hi5622_ax_event(field_b, HI5622_AX_KEY_BFER, body, 1, out);
}

int hi5622_ax_trig_su_bf_fb(struct hi5622_ax_frame *out, u8 field_b, u8 on)
{
	u8 body[1];

	if (on > 1)
		return -EINVAL;
	body[0] = on;
	return hi5622_ax_event(field_b, HI5622_AX_KEY_TRIG_SU_BF_FB, body, 1,
			       out);
}

int hi5622_ax_cyc_sche(struct hi5622_ax_frame *out, u8 field_b, u8 on)
{
	u8 body[1];

	if (on > 1)			/* firmware worker: only 0/1 */
		return -EINVAL;
	body[0] = on;
	return hi5622_ax_event(field_b, HI5622_AX_KEY_CYC_SCHE, body, 1, out);
}

int hi5622_ax_sr_enable(struct hi5622_ax_frame *out, u8 field_b, u8 on)
{
	u8 body[1];

	if (on > 1)
		return -EINVAL;
	body[0] = on;
	return hi5622_ax_event(field_b, HI5622_AX_KEY_SR_ENABLE, body, 1, out);
}

static bool hi5622_ax_module_ok(const char *name)
{
	return !strcmp(name, "sch") || !strcmp(name, "cmn") ||
	       !strcmp(name, "err_name");
}

int hi5622_ax_alg_parse(const u8 *body, u16 body_len,
			const char **tokens, unsigned int max_tokens)
{
	unsigned int n, k;

	if (!body || body_len < HI5622_AX_ALG_MIN)
		return -EINVAL;
	n = body[0];
	if (n < 1 || 1 + n > body_len || n > max_tokens)
		return -EINVAL;
	for (k = 0; k < n; k++) {
		unsigned int off = body[1 + k];
		unsigned int pos = HI5622_AX_TOKEN_AREA + off;
		unsigned int end;

		if (pos >= body_len)
			return -EINVAL;
		for (end = pos; end < body_len; end++)
			if (body[end] == 0)
				break;
		if (end >= body_len)
			return -EINVAL;
		tokens[k] = (const char *)(body + pos);
	}
	return (int)n;
}

int hi5622_ax_alg(struct hi5622_ax_frame *out, u8 field_b,
		  const char *const *tokens, unsigned int n_tokens)
{
	u8 body[HI5622_AX_ALG_MAX];
	unsigned int total = HI5622_AX_TOKEN_AREA, k;
	int pos;

	if (!tokens || n_tokens < 1 || n_tokens > 127)
		return -EINVAL;
	if (!hi5622_ax_module_ok(tokens[0]))
		return -EINVAL;
	memset(body, 0, sizeof(body));
	body[0] = (u8)n_tokens;
	for (k = 0; k < n_tokens; k++) {
		unsigned int len = strlen(tokens[k]) + 1;

		if (total + len > HI5622_AX_ALG_MAX)
			return -EINVAL;
		if (total - HI5622_AX_TOKEN_AREA > 0xFF)
			return -EINVAL;
		body[1 + k] = (u8)(total - HI5622_AX_TOKEN_AREA);
		memcpy(body + total, tokens[k], len);
		total += len;
	}
	if (total < HI5622_AX_ALG_MIN)
		return -EINVAL;
	{
		const char *check[128];

		pos = hi5622_ax_alg_parse(body, (u16)total, check, 128);
		if (pos < 0)
			return pos;
	}
	return hi5622_ax_event(field_b, HI5622_AX_KEY_ALG, body, (u16)total,
			       out);
}
