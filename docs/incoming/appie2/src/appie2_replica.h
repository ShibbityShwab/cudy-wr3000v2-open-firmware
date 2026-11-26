/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * appie2_replica.h - clean-room host model of the vendor WEXT set_vendor_ie call order
 *
 * Lane appie2 (task-244), host-only. This is original clean-room code written from
 * decoded call shapes (addresses/layouts only). It contains no vendor object code,
 * no firmware bytes and no vendor data. It models, and KAT-tests, the ordering:
 *
 *   WEXT private ioctl 0x8eec (nlwifi_set_app_ie)
 *     -> 508-byte cfg-event body {u16 event 0x0276; u16 len 0x01f8; ...}
 *     -> 536-byte group-2 / id-0 RPC envelope (host-internal container)
 *     -> host event dispatch by u16 id over 12-byte rows {id, pad, h4, h8}
 *     -> hmac chain: mirror save THEN H2D app-IE post (order matters)
 *
 * The model is deliberately fail-closed: every bound that the vendor side enforces
 * is enforced here too, and any missing handler is an error, never a silent no-op.
 */
#ifndef APPI2_REPLICA_H
#define APPI2_REPLICA_H

#include <stddef.h>
#include <stdint.h>

#define AIW_WEXT_CMD            0x8eecu
#define AIW_IE_MAX              496u
#define AIW_TYPE_MAX            19u
#define AIW_EVENT_SET_VENDOR_IE 0x0276u
#define AIW_EVENT_PAYLOAD_LEN   0x01f8u
#define AIW_CFG_BODY_LEN        508u
#define AIW_ENV_TOTAL_LEN       536u
#define AIW_ENV_HDR_LEN         16u
#define AIW_ENV_RPC_LEN         12u
#define AIW_ENV_GROUP           2u
#define AIW_ENV_ID              0u
#define AIW_ENV_TAG             0x5a5au

#define AIW_CTX_LEN 3u

enum aiw_handler {
	AIW_H_NONE = 0,
	AIW_H_SET_VENDOR_IE,
	AIW_H_GET_STA_11H,	/* decoy handler, the 0x02de row in the same table */
	AIW_H_DECOY_MODE4,	/* decoy handler, exercises the +4 slot */
};

struct aiw_wext_req {
	uint32_t cmd;
	uint32_t ie_len;
	uint8_t type;
	uint8_t mode;		/* always 0 on the WEXT path */
	uint8_t ie[AIW_IE_MAX];
};

struct aiw_cfg_body {
	uint8_t raw[AIW_CFG_BODY_LEN];
};

struct aiw_envelope {
	uint8_t raw[AIW_ENV_TOTAL_LEN];
};

struct aiw_ctx {
	uint8_t v[AIW_CTX_LEN];	/* the vendor netdev hmac ctx trio (dev+0x328) */
};

/* one 12-byte dispatch row: u16 id, u16 pad, u32 handler for mode 4, u32 handler for mode 3 */
struct aiw_row {
	uint16_t id;
	uint16_t pad;
	enum aiw_handler h_mode4;
	enum aiw_handler h_mode3;
};

enum aiw_err {
	AIW_OK = 0,
	AIW_EINVAL = -22,
	AIW_ERANGE = -34,
	AIW_ENOENT = -2,
};

/* WEXT userspace request parser, fail-closed (cmd / ie_len / type / mode). */
enum aiw_err aiw_wext_parse(const void *userbuf, size_t userlen, struct aiw_wext_req *out);

/* 508-byte cfg-event body for event 0x0276. */
enum aiw_err aiw_cfg_body_build(const struct aiw_wext_req *req, struct aiw_cfg_body *out);

/* The 0x7f IE strip the vendor applies before the mirror+post (in place). */
enum aiw_err aiw_strip_7f(struct aiw_wext_req *req, size_t *stripped_len);

/* 536-byte group-2/id-0 RPC envelope around the body. */
enum aiw_err aiw_envelope_build(const struct aiw_cfg_body *body, const struct aiw_ctx *ctx,
				uint32_t seq, struct aiw_envelope *out);

/* Host dispatch: find the row by u16 event id, choose the +4 (mode 4) or +8 (mode 3) slot. */
enum aiw_err aiw_dispatch(const struct aiw_row *rows, size_t nrows, uint16_t event_id,
			  unsigned mode, enum aiw_handler *handler);

/* The hmac consumer: mirror save first, H2D post second (vendor order). */
struct aiw_trace {
	unsigned mirror_saves;
	unsigned h2d_posts;
	unsigned envelope_tx;	/* must stay 0: the envelope is host-internal */
	unsigned mirror_before_post;	/* 1 when the last post followed a mirror save */
};

enum aiw_err aiw_hmac_consumer(const struct aiw_envelope *env, enum aiw_handler handler,
			       struct aiw_trace *trace);

/* Full modelled sequence: parse -> strip -> body -> envelope -> dispatch -> consumer. */
enum aiw_err aiw_run_vendor_sequence(const void *userbuf, size_t userlen, const struct aiw_ctx *ctx,
				     uint32_t seq, struct aiw_trace *trace,
				     enum aiw_handler *handler_out);

/* The two host dispatch rows decoded from the vendor driver (ids/order only, no bytes). */
extern const struct aiw_row g_aiw_vendor_rows[];
extern const size_t g_aiw_vendor_rows_len;

#endif /* APPI2_REPLICA_H */
