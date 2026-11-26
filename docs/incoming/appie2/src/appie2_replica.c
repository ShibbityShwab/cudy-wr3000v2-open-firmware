/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * appie2_replica.c - clean-room host model of the vendor WEXT set_vendor_ie call order.
 * Lane appie2 (task-244), host-only. No vendor code, no vendor bytes.
 */
#include "appie2_replica.h"

#include <string.h>

/* The two rows are the decoded host dispatch rows relevant to the app-IE path:
 *   0x0276 -> only the +8 (mode 3) slot is populated: wal_config_set_vendor_ie
 *   0x02de -> the neighbouring row that carries the +8 slot: get_sta_11h_ability
 *   0x0277 -> a fixture row with BOTH slots populated, used to prove slot selection
 * The addresses of the vendor functions are deliberately not reproduced here.
 */
const struct aiw_row g_aiw_vendor_rows[] = {
	{ AIW_EVENT_SET_VENDOR_IE, 0, AIW_H_NONE, AIW_H_SET_VENDOR_IE },
	{ 0x02de, 0, AIW_H_NONE, AIW_H_GET_STA_11H },
	{ 0x0277, 0, AIW_H_DECOY_MODE4, AIW_H_SET_VENDOR_IE },
};
const size_t g_aiw_vendor_rows_len = sizeof(g_aiw_vendor_rows) / sizeof(g_aiw_vendor_rows[0]);

static void wr16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v & 0xffu);
	p[1] = (uint8_t)(v >> 8);
}

static uint32_t rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

static void wr32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v & 0xffu);
	p[1] = (uint8_t)((v >> 8) & 0xffu);
	p[2] = (uint8_t)((v >> 16) & 0xffu);
	p[3] = (uint8_t)((v >> 24) & 0xffu);
}

enum aiw_err aiw_wext_parse(const void *userbuf, size_t userlen, struct aiw_wext_req *out)
{
	const uint8_t *u = (const uint8_t *)userbuf;
	uint32_t ie_len;

	if (!u || !out)
		return AIW_EINVAL;
	/* userspace ABI: {u32 cmd; u32 ie_len; u8 type; u8 pad[3]; u8 ie[]} */
	if (userlen < 12u)
		return AIW_EINVAL;
	if (rd32(u) != AIW_WEXT_CMD)
		return AIW_EINVAL;

	ie_len = rd32(u + 4);
	if (ie_len > AIW_IE_MAX)
		return AIW_ERANGE;	/* the vendor copy into the 496-byte field cannot fit */
	if (ie_len > userlen - 12u)
		return AIW_EINVAL;

	memset(out, 0, sizeof(*out));
	out->cmd = AIW_WEXT_CMD;
	out->ie_len = ie_len;
	out->type = u[8];
	out->mode = 0;			/* the WEXT path never sends mode != 0 */
	if (out->type > AIW_TYPE_MAX)
		return AIW_ERANGE;	/* firmware/mirror slot table is 0..19 */
	if (ie_len)
		memcpy(out->ie, u + 12, ie_len);
	return AIW_OK;
}

enum aiw_err aiw_cfg_body_build(const struct aiw_wext_req *req, struct aiw_cfg_body *out)
{
	if (!req || !out)
		return AIW_EINVAL;
	if (req->cmd != AIW_WEXT_CMD || req->ie_len > AIW_IE_MAX ||
	    req->type > AIW_TYPE_MAX || req->mode != 0)
		return AIW_EINVAL;

	memset(out, 0, sizeof(*out));
	wr16(out->raw + 0, AIW_EVENT_SET_VENDOR_IE);
	wr16(out->raw + 2, AIW_EVENT_PAYLOAD_LEN);	/* 0x01f8: 504 = ie_len + 8 */
	wr32(out->raw + 4, req->ie_len);
	out->raw[8] = req->type;
	out->raw[9] = 0;		/* mode: path A */
	out->raw[10] = 0;
	out->raw[11] = 0;
	if (req->ie_len)
		memcpy(out->raw + 12, req->ie, req->ie_len);
	return AIW_OK;
}

/* Remove the first 0x7f IE: the vendor runs mac_find_ie(0x7f) on the IE blob and
 * memmove()s the tail over it before the mirror save. */
enum aiw_err aiw_strip_7f(struct aiw_wext_req *req, size_t *stripped_len)
{
	size_t off = 0;

	if (!req || !stripped_len)
		return AIW_EINVAL;
	while (off + 2u <= req->ie_len) {
		size_t ie_len = 2u + req->ie[off + 1u];

		if (off + ie_len > req->ie_len)
			return AIW_EINVAL;	/* malformed IE blob: fail closed */
		if (req->ie[off] == 0x7fu) {
			size_t tail = req->ie_len - (off + ie_len);

			memmove(req->ie + off, req->ie + off + ie_len, tail);
			req->ie_len -= (uint32_t)ie_len;
			*stripped_len = ie_len;
			return AIW_OK;
		}
		off += ie_len;
	}
	*stripped_len = 0;
	return AIW_ENOENT;
}

enum aiw_err aiw_envelope_build(const struct aiw_cfg_body *body, const struct aiw_ctx *ctx,
				uint32_t seq, struct aiw_envelope *out)
{
	uint8_t *p;

	if (!body || !ctx || !out)
		return AIW_EINVAL;

	memset(out, 0, sizeof(*out));
	p = out->raw;
	/* vendor envelope: group 2 in the LOW nibble of byte 0, id 0 at bytes 6..7,
	 * context trio from the netdev hmac ctx, 0x5a5a HCC tag at bytes 10..11. */
	p[0] = (uint8_t)((p[0] & 0xf0u) | (AIW_ENV_GROUP & 0x0fu));
	p[1] = (uint8_t)((p[1] & 0x0fu) | ((ctx->v[2] & 0x0fu) << 4));
	p[2] = ctx->v[1];
	p[3] = ctx->v[0];
	wr16(p + 6, AIW_ENV_ID);
	wr16(p + 10, AIW_ENV_TAG);
	wr32(p + 12, 0u);		/* host-private word */
	/* payload at +16: 12-byte RPC prefix, then the 508-byte body */
	wr32(p + 16, 1u);		/* rpc tag */
	wr16(p + 20, 0u);
	wr16(p + 22, (uint16_t)AIW_CFG_BODY_LEN);	/* 0x01fc */
	wr32(p + 24, seq);
	memcpy(p + AIW_ENV_HDR_LEN + AIW_ENV_RPC_LEN, body->raw, AIW_CFG_BODY_LEN);
	return AIW_OK;
}

enum aiw_err aiw_dispatch(const struct aiw_row *rows, size_t nrows, uint16_t event_id,
			  unsigned mode, enum aiw_handler *handler)
{
	size_t i;

	if (!rows || !handler || !nrows)
		return AIW_EINVAL;
	for (i = 0; i < nrows; i++) {
		if (rows[i].id != event_id)
			continue;
		/* dispatch row: +4 handler is selected for mode 4, +8 handler for mode 3 */
		if (mode == 4u)
			*handler = rows[i].h_mode4;
		else if (mode == 3u)
			*handler = rows[i].h_mode3;
		else
			return AIW_EINVAL;
		if (*handler == AIW_H_NONE)
			return AIW_ENOENT;	/* vendor logs "not supported", no call */
		return AIW_OK;
	}
	return AIW_ENOENT;
}

enum aiw_err aiw_hmac_consumer(const struct aiw_envelope *env, enum aiw_handler handler,
			       struct aiw_trace *trace)
{
	if (!env || !trace)
		return AIW_EINVAL;
	if (handler != AIW_H_SET_VENDOR_IE)
		return AIW_ENOENT;
	/* vendor order: mac_vap_save_app_ie (host mirror) THEN
	 * hmac_config_h2d_send_app_ie (the g0 id 0x20 frame). */
	trace->mirror_saves++;
	trace->h2d_posts++;
	trace->mirror_before_post = 1;
	return AIW_OK;
}

enum aiw_err aiw_run_vendor_sequence(const void *userbuf, size_t userlen, const struct aiw_ctx *ctx,
				     uint32_t seq, struct aiw_trace *trace,
				     enum aiw_handler *handler_out)
{
	struct aiw_wext_req req;
	struct aiw_cfg_body body;
	struct aiw_envelope env;
	enum aiw_handler handler;
	size_t stripped = 0;
	enum aiw_err err;

	if (!trace || !handler_out || !ctx)
		return AIW_EINVAL;
	memset(trace, 0, sizeof(*trace));

	err = aiw_wext_parse(userbuf, userlen, &req);
	if (err)
		return err;
	err = aiw_strip_7f(&req, &stripped);
	if (err != AIW_OK && err != AIW_ENOENT)
		return err;
	err = aiw_cfg_body_build(&req, &body);
	if (err)
		return err;
	err = aiw_envelope_build(&body, ctx, seq, &env);
	if (err)
		return err;
	/* HOST-INTERNAL dispatch: the envelope is consumed locally, never sent over HCC. */
	err = aiw_dispatch(g_aiw_vendor_rows, g_aiw_vendor_rows_len,
			   AIW_EVENT_SET_VENDOR_IE, 3u, &handler);
	if (err)
		return err;
	if (trace->envelope_tx != 0u)
		return AIW_EINVAL;
	err = aiw_hmac_consumer(&env, handler, trace);
	if (err)
		return err;
	*handler_out = handler;
	return AIW_OK;
}
