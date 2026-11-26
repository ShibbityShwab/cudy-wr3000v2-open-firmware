// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_ba.c - fail-closed TX-direction block-ack core for the Hi5622V100
 * clean-room Wi-Fi driver (lane wifi-ba-tx-impl, shared task-217).
 *
 * Ported from ../wifi-ba-agg/src/hi5622_ba.c (task-178) and extended with
 * the TX-direction cut (see hi5622_ba.h).  The core owns no device access:
 * every wire goes through the two callbacks (post_h2d / tx_action), so the
 * same file is unit-tested on the host with -DHI5622_BA_HOST_TEST.
 */
#include "hi5622_ba.h"

#ifdef HI5622_BA_HOST_TEST
#include <stdio.h>
#include <string.h>
#define ba_memset	memset
#define ba_memcmp	memcmp
#else
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/printk.h>
#include <linux/string.h>
#define ba_memset	memset
#define ba_memcmp	memcmp
#endif

unsigned int hi5622_ba_enable;
#ifndef HI5622_BA_HOST_TEST
module_param_named(ba_enable, hi5622_ba_enable, uint, 0444);
MODULE_PARM_DESC(ba_enable,
	"fail-closed TX-direction block-ack: 0=off (default; no frame, no H2D message, no state), 1=armed (in-kernel ADDBA originator + g0 0x0A/0x0B/0x1C), >1=invalid");
#endif

/* fail-closed gate: 0 ok, -EOPNOTSUPP off, -EINVAL invalid value */
int hi5622_ba_check_enabled(void)
{
	if (hi5622_ba_enable == 1)
		return HI5622_BA_OK;
	if (hi5622_ba_enable == 0)
		return HI5622_BA_EOPNOTSUPP;
	return HI5622_BA_EINVAL;
}

/* ---- small helpers ----------------------------------------------------- */

static void ba_put_le16(u8 *p, u16 v)
{
	p[0] = (u8)(v & 0xff);
	p[1] = (u8)(v >> 8);
}

static u16 ba_get_le16(const u8 *p)
{
	return (u16)(p[0] | ((u16)p[1] << 8));
}

static void ba_copy_addr(u8 *dst, const u8 *src)
{
	int i;

	if (!src) {
		ba_memset(dst, 0, 6);
		return;
	}
	for (i = 0; i < 6; i++)
		dst[i] = src[i];
}

static u16 ba_clamp_baw(u16 baw)
{
	if (baw > HI5622_BA_BAW_MAX)
		return HI5622_BA_BAW_MAX;
	if (baw < HI5622_BA_BAW_MIN)
		return HI5622_BA_BAW_MIN;
	return baw;
}

/* BA parameter set: | baw[9:0] << 6 | policy<<1 | amsdu | tid<<2 | */
static u16 ba_param_set(const struct hi5622_ba_params *p)
{
	u16 v = 0;

	v |= (u16)((p->baw & 0x3ff) << 6);
	v |= (u16)((p->policy & 1) << 1);
	v |= (u16)(p->amsdu & 1);
	v |= (u16)((p->tid & 0xf) << 2);
	return v;
}

/* 24-byte 802.11 mgmt header for the three action frames */
static void ba_fill_hdr(u8 *frame, const u8 *da, const u8 *sa,
			const u8 *bssid)
{
	frame[0] = 0xd0;	/* FC: mgmt, action */
	frame[1] = 0x00;
	frame[2] = 0x00;	/* duration */
	frame[3] = 0x00;
	ba_copy_addr(frame + 4, da);
	ba_copy_addr(frame + 10, sa);
	ba_copy_addr(frame + 16, bssid);
	/* frame[22..23] sequence control = 0 */
}

/* ---- complete HCC wire ------------------------------------------------ */

/* 12-byte HCC header, verified this lane from the pinned vendor module:
 *   [0] group nibble, [1] high nibble = dst (== [9]), low nibble = alloc 1,
 *   [4..5] total length, [6..7] id, [10..11] tag 0x5A5A.  The vendor BA
 *   builders take the hmac-dev triple bytes at [1].high/[2]/[3]; for the
 *   single chip / single AP VAP bench these are 0 (chip id 0 both bands,
 *   TABLES T3), so only the alloc nibble is set here. */
void hi5622_ba_wrap_h2d(u8 *wire, size_t payload_len, u8 group, u8 id)
{
	if (!wire)
		return;
	ba_memset(wire, 0, HI5622_BA_HDR_LEN + payload_len);
	wire[0] = (u8)(group & 0x0fu);
	wire[1] = (u8)HI5622_BA_ALLOC_NIBBLE;
	wire[2] = 0;
	wire[3] = 0;
	ba_put_le16(wire + 4, (u16)(HI5622_BA_HDR_LEN + payload_len));
	ba_put_le16(wire + 6, (u16)id);
	wire[8] = 0;
	wire[9] = 0;
	ba_put_le16(wire + 10, (u16)HI5622_BA_HCC_TAG);
}

static size_t ba_wire_compose(u8 *wire, size_t cap, u8 group, u8 id,
			      const u8 *payload, size_t payload_len)
{
	size_t total = HI5622_BA_HDR_LEN + payload_len;
	size_t i;

	if (!wire || !payload || cap < total)
		return 0;
	hi5622_ba_wrap_h2d(wire, payload_len, group, id);
	for (i = 0; i < payload_len; i++)
		wire[HI5622_BA_HDR_LEN + i] = payload[i];
	return total;
}

/* ---- byte-exact action-frame builders ---------------------------------- */

size_t hi5622_ba_build_addba_req(u8 *frame, size_t cap,
				 const struct hi5622_ba_params *p,
				 const u8 *da, const u8 *sa, const u8 *bssid)
{
	if (hi5622_ba_check_enabled())
		return 0;
	if (!frame || !p || !da || cap < HI5622_BA_FRAME_ADDBA_REQ_LEN)
		return 0;
	if (p->tid >= HI5622_BA_MAX_TID)
		return 0;
	if (p->baw < HI5622_BA_BAW_MIN || p->baw > HI5622_BA_BAW_MAX)
		return 0;
	ba_memset(frame, 0, HI5622_BA_FRAME_ADDBA_REQ_LEN);
	ba_fill_hdr(frame, da, sa, bssid);
	frame[24] = HI5622_BA_CATEGORY;
	frame[25] = HI5622_BA_ACTION_ADDBA_REQ;
	frame[26] = p->dialog;
	ba_put_le16(frame + 27, ba_param_set(p));
	ba_put_le16(frame + 29, p->timeout_tu);
	ba_put_le16(frame + 31, p->start_seq);
	return HI5622_BA_FRAME_ADDBA_REQ_LEN;
}

size_t hi5622_ba_build_addba_rsp(u8 *frame, size_t cap,
				 const struct hi5622_ba_params *p,
				 const u8 *da, const u8 *sa, const u8 *bssid)
{
	if (hi5622_ba_check_enabled())
		return 0;
	if (!frame || !p || !da || cap < HI5622_BA_FRAME_ADDBA_RSP_LEN)
		return 0;
	if (p->tid >= HI5622_BA_MAX_TID)
		return 0;
	if (p->baw < HI5622_BA_BAW_MIN || p->baw > HI5622_BA_BAW_MAX)
		return 0;
	ba_memset(frame, 0, HI5622_BA_FRAME_ADDBA_RSP_LEN);
	ba_fill_hdr(frame, da, sa, bssid);
	frame[24] = HI5622_BA_CATEGORY;
	frame[25] = HI5622_BA_ACTION_ADDBA_RSP;
	frame[26] = p->dialog;
	ba_put_le16(frame + 27, p->status);	/* status code */
	ba_put_le16(frame + 29, ba_param_set(p));
	/* vendor parity: the response builder leaves the BA timeout 0 */
	ba_put_le16(frame + 31, 0);
	return HI5622_BA_FRAME_ADDBA_RSP_LEN;
}

size_t hi5622_ba_build_delba(u8 *frame, size_t cap, u8 tid, u8 initiator,
			     u16 reason, const u8 *da, const u8 *sa,
			     const u8 *bssid)
{
	if (hi5622_ba_check_enabled())
		return 0;
	if (!frame || !da || cap < HI5622_BA_FRAME_DELBA_LEN)
		return 0;
	if (tid >= HI5622_BA_MAX_TID)
		return 0;
	ba_memset(frame, 0, HI5622_BA_FRAME_DELBA_LEN);
	ba_fill_hdr(frame, da, sa, bssid);
	frame[24] = HI5622_BA_CATEGORY;
	frame[25] = HI5622_BA_ACTION_DELBA;
	/* DELBA parameter: | tid<<12 | initiator<<11 | aid[10:0]=0 */
	frame[27] = (u8)(((tid & 0xf) << 4) | ((initiator & 1) << 3));
	ba_put_le16(frame + 28, reason);
	return HI5622_BA_FRAME_DELBA_LEN;
}

/* ---- byte-exact payload builders (vendor shapes T3-T5) ----------------- */

size_t hi5622_ba_build_relay_addba_rsp(u8 *payload, size_t cap, u16 uid,
				       const struct hi5622_ba_params *p)
{
	if (hi5622_ba_check_enabled())
		return 0;
	if (!payload || !p || cap < HI5622_BA_RELAY_PAYLOAD_LEN)
		return 0;
	if (p->tid >= HI5622_BA_MAX_TID)
		return 0;
	/* zero the WHOLE payload: the fw reads fields we do not set */
	ba_memset(payload, 0, HI5622_BA_RELAY_PAYLOAD_LEN);
	payload[0] = HI5622_BA_RELAY_TYPE;	/* 3 */
	payload[1] = HI5622_BA_RELAY_ADDBA_RSP;	/* 1 */
	ba_put_le16(payload + 2, uid);		/* user index (obj key) */
	payload[7] = p->tid;
	payload[9] = (u8)(p->status & 0xff);	/* ADDBA status, 0 = success */
	ba_put_le16(payload + 0x0c, ba_clamp_baw(p->baw));
	ba_put_le16(payload + 0x10, p->timeout_tu);
	payload[0x12] = (u8)(p->amsdu & 1);
	payload[0x14] = p->dialog;
	payload[0x15] = (u8)(p->policy & 1);
	return HI5622_BA_RELAY_PAYLOAD_LEN;
}

size_t hi5622_ba_build_relay_delba(u8 *payload, size_t cap, u16 uid, u8 tid,
				   u8 initiator)
{
	if (hi5622_ba_check_enabled())
		return 0;
	if (!payload || cap < HI5622_BA_RELAY_PAYLOAD_LEN)
		return 0;
	if (tid >= HI5622_BA_MAX_TID)
		return 0;
	ba_memset(payload, 0, HI5622_BA_RELAY_PAYLOAD_LEN);
	payload[0] = HI5622_BA_RELAY_TYPE;
	payload[1] = HI5622_BA_RELAY_DELBA;	/* 2 */
	ba_put_le16(payload + 2, uid);
	payload[7] = tid;
	payload[8] = (u8)(initiator & 1);
	payload[0x14] = 0;			/* clear flag, explicit */
	return HI5622_BA_RELAY_PAYLOAD_LEN;
}

size_t hi5622_ba_build_ampdu_start(u8 *payload, size_t cap, u8 tid, u16 seq,
				   u16 uid, u8 flag)
{
	if (hi5622_ba_check_enabled())
		return 0;
	if (!payload || cap < HI5622_BA_AMPDU_PAYLOAD_LEN)
		return 0;
	if (tid >= HI5622_BA_MAX_TID)
		return 0;
	ba_memset(payload, 0, HI5622_BA_AMPDU_PAYLOAD_LEN);
	payload[0] = 0;			/* start */
	payload[1] = tid;
	ba_put_le16(payload + 2, seq);
	ba_put_le16(payload + 4, uid);
	payload[6] = flag;
	return HI5622_BA_AMPDU_PAYLOAD_LEN;
}

size_t hi5622_ba_build_ampdu_end(u8 *payload, size_t cap, u8 tid, u16 uid)
{
	if (hi5622_ba_check_enabled())
		return 0;
	if (!payload || cap < HI5622_BA_AMPDU_PAYLOAD_LEN)
		return 0;
	if (tid >= HI5622_BA_MAX_TID)
		return 0;
	ba_memset(payload, 0, HI5622_BA_AMPDU_PAYLOAD_LEN);
	payload[0] = 1;			/* end */
	payload[1] = tid;
	ba_put_le16(payload + 4, uid);
	return HI5622_BA_AMPDU_PAYLOAD_LEN;
}

size_t hi5622_ba_build_tx_notify(u8 *payload, size_t cap, u16 uid, u8 tid,
				 u8 value)
{
	if (hi5622_ba_check_enabled())
		return 0;
	if (!payload || cap < HI5622_BA_NOTIFY_PAYLOAD_LEN)
		return 0;
	if (tid >= HI5622_BA_MAX_TID)
		return 0;
	ba_memset(payload, 0, HI5622_BA_NOTIFY_PAYLOAD_LEN);
	ba_put_le16(payload, uid);
	payload[2] = tid;
	payload[3] = value;
	return HI5622_BA_NOTIFY_PAYLOAD_LEN;
}

/* ---- complete wire builders ------------------------------------------- */

size_t hi5622_ba_wire_relay_addba_rsp(u8 *wire, size_t cap, u16 uid,
				      const struct hi5622_ba_params *p)
{
	u8 payload[HI5622_BA_RELAY_PAYLOAD_LEN];

	if (hi5622_ba_build_relay_addba_rsp(payload, sizeof(payload), uid, p) !=
	    HI5622_BA_RELAY_PAYLOAD_LEN)
		return 0;
	return ba_wire_compose(wire, cap, HI5622_BA_H2D_GROUP_RELAY,
			       HI5622_BA_H2D_BA_RELAY, payload,
			       sizeof(payload));
}

size_t hi5622_ba_wire_relay_delba(u8 *wire, size_t cap, u16 uid, u8 tid,
				  u8 initiator)
{
	u8 payload[HI5622_BA_RELAY_PAYLOAD_LEN];

	if (hi5622_ba_build_relay_delba(payload, sizeof(payload), uid, tid,
					initiator) !=
	    HI5622_BA_RELAY_PAYLOAD_LEN)
		return 0;
	return ba_wire_compose(wire, cap, HI5622_BA_H2D_GROUP_RELAY,
			       HI5622_BA_H2D_BA_RELAY, payload,
			       sizeof(payload));
}

size_t hi5622_ba_wire_ampdu_start(u8 *wire, size_t cap, u8 tid, u16 seq,
				  u16 uid, u8 flag)
{
	u8 payload[HI5622_BA_AMPDU_PAYLOAD_LEN];

	if (hi5622_ba_build_ampdu_start(payload, sizeof(payload), tid, seq, uid,
					flag) != HI5622_BA_AMPDU_PAYLOAD_LEN)
		return 0;
	return ba_wire_compose(wire, cap, HI5622_BA_H2D_GROUP_AMPDU,
			       HI5622_BA_H2D_AMPDU_TX, payload,
			       sizeof(payload));
}

size_t hi5622_ba_wire_ampdu_end(u8 *wire, size_t cap, u8 tid, u16 uid)
{
	u8 payload[HI5622_BA_AMPDU_PAYLOAD_LEN];

	if (hi5622_ba_build_ampdu_end(payload, sizeof(payload), tid, uid) !=
	    HI5622_BA_AMPDU_PAYLOAD_LEN)
		return 0;
	return ba_wire_compose(wire, cap, HI5622_BA_H2D_GROUP_AMPDU,
			       HI5622_BA_H2D_AMPDU_TX, payload,
			       sizeof(payload));
}

size_t hi5622_ba_wire_tx_notify(u8 *wire, size_t cap, u16 uid, u8 tid,
				u8 value)
{
	u8 payload[HI5622_BA_NOTIFY_PAYLOAD_LEN];

	if (hi5622_ba_build_tx_notify(payload, sizeof(payload), uid, tid,
				      value) != HI5622_BA_NOTIFY_PAYLOAD_LEN)
		return 0;
	return ba_wire_compose(wire, cap, HI5622_BA_H2D_GROUP_NOTIFY,
			       HI5622_BA_H2D_TX_BA_NOTIFY, payload,
			       sizeof(payload));
}

/* ---- parser ------------------------------------------------------------ */

/* Accept either a full 802.11 action frame (24-byte MAC header) or the
 * category-first body the firmware embeds (e.g. D2H 0x1D action 1 at hdr+20);
 * the body layout is identical in both cases.
 *
 * Body layouts (vendor bytes, TABLES T8):
 *   ADDBA req: 3,0,dialog,param[2],timeout[2],start-seq[2]
 *   ADDBA rsp: 3,1,dialog,status[2],param[2],timeout[2]
 *   DELBA:     3,2,param[2],reason[2]
 */
static int ba_parse_body(const u8 *b, size_t blen,
			 struct hi5622_ba_action *out)
{
	u16 param;

	if (!b || !out || blen < 3)
		return HI5622_BA_EINVAL;
	if (b[0] != HI5622_BA_CATEGORY)
		return HI5622_BA_EINVAL;
	ba_memset(out, 0, sizeof(*out));
	out->action = b[1];
	if (out->action == HI5622_BA_ACTION_ADDBA_REQ ||
	    out->action == HI5622_BA_ACTION_ADDBA_RSP) {
		if (blen < HI5622_BA_ACTION_MIN_LEN)
			return HI5622_BA_EINVAL;
		out->dialog = b[2];
		if (out->action == HI5622_BA_ACTION_ADDBA_RSP) {
			out->status = ba_get_le16(b + 3);
			param = ba_get_le16(b + 5);
			out->timeout_tu = ba_get_le16(b + 7);
		} else {
			param = ba_get_le16(b + 3);
			out->timeout_tu = ba_get_le16(b + 5);
			out->start_seq = ba_get_le16(b + 7);
		}
		out->tid = (u8)((param >> 2) & 0xf);
		out->policy = (u8)((param >> 1) & 1);
		out->amsdu = (u8)(param & 1);
		out->baw = (u16)(param >> 6);
		if (out->tid >= HI5622_BA_MAX_TID)
			return HI5622_BA_EINVAL;
		if (out->baw < HI5622_BA_BAW_MIN ||
		    out->baw > HI5622_BA_BAW_MAX)
			return HI5622_BA_EINVAL;
		return HI5622_BA_OK;
	}
	if (out->action == HI5622_BA_ACTION_DELBA) {
		if (blen < 6)
			return HI5622_BA_EINVAL;
		param = ba_get_le16(b + 2);
		out->tid = (u8)((param >> 12) & 0xf);
		out->initiator = (u8)((param >> 11) & 1);
		out->reason = ba_get_le16(b + 4);
		if (out->tid >= HI5622_BA_MAX_TID)
			return HI5622_BA_EINVAL;
		return HI5622_BA_OK;
	}
	return HI5622_BA_EINVAL;
}

int hi5622_ba_parse_action(const u8 *frame, size_t len,
			   struct hi5622_ba_action *out)
{
	if (!frame || !out || len < 3)
		return HI5622_BA_EINVAL;
	/* full 802.11 action frame (24-byte header, category at [24])? */
	if (len >= 25 && frame[0] == 0xd0 && frame[1] == 0x00 &&
	    frame[24] == HI5622_BA_CATEGORY)
		return ba_parse_body(frame + 24, len - 24, out);
	return ba_parse_body(frame, len, out);
}

/* ---- core / peers ------------------------------------------------------ */

void hi5622_ba_core_init(struct hi5622_ba_core *core)
{
	if (!core)
		return;
	ba_memset(core, 0, sizeof(*core));
}

void hi5622_ba_core_set_addrs(struct hi5622_ba_core *core, const u8 *own_mac,
			      const u8 *bssid)
{
	if (!core)
		return;
	ba_copy_addr(core->own_mac, own_mac);
	ba_copy_addr(core->bssid, bssid);
}

struct hi5622_ba_peer *hi5622_ba_peer_get(struct hi5622_ba_core *core,
					  const u8 *mac)
{
	int i;

	if (!core || !mac)
		return NULL;
	for (i = 0; i < HI5622_BA_MAX_PEERS; i++) {
		if (core->peers[i].used &&
		    ba_memcmp(core->peers[i].mac, mac, 6) == 0)
			return &core->peers[i];
	}
	for (i = 0; i < HI5622_BA_MAX_PEERS; i++) {
		if (!core->peers[i].used) {
			ba_memset(&core->peers[i], 0,
				  sizeof(core->peers[i]));
			core->peers[i].used = 1;
			ba_copy_addr(core->peers[i].mac, mac);
			return &core->peers[i];
		}
	}
	return NULL;
}

struct hi5622_ba_peer *hi5622_ba_peer_get_by_uid(struct hi5622_ba_core *core,
						 u16 uid)
{
	int i;

	if (!core)
		return NULL;
	for (i = 0; i < HI5622_BA_MAX_PEERS; i++) {
		if (core->peers[i].used && core->peers[i].uid == uid)
			return &core->peers[i];
	}
	return NULL;
}

struct hi5622_ba_peer *hi5622_ba_peer_bind(struct hi5622_ba_core *core,
					   const u8 *mac, u16 uid)
{
	struct hi5622_ba_peer *peer;

	if (!core || !mac)
		return NULL;
	peer = hi5622_ba_peer_get(core, mac);
	if (!peer)
		return NULL;
	peer->uid = uid;
	return peer;
}

void hi5622_ba_peer_drop(struct hi5622_ba_core *core,
			 struct hi5622_ba_peer *peer)
{
	if (!core || !peer || peer < core->peers ||
	    peer >= core->peers + HI5622_BA_MAX_PEERS)
		return;
	ba_memset(peer, 0, sizeof(*peer));
}

u32 hi5622_ba_active_sessions(const struct hi5622_ba_core *core)
{
	u32 n = 0;
	int i, t;

	if (!core)
		return 0;
	for (i = 0; i < HI5622_BA_MAX_PEERS; i++) {
		if (!core->peers[i].used)
			continue;
		for (t = 0; t < HI5622_BA_MAX_TID; t++)
			if (core->peers[i].tx[t].used &&
			    core->peers[i].tx[t].state == 2)
				n++;
	}
	return n;
}

/* ---- H2D post helpers -------------------------------------------------- */

static int ba_post_h2d(const struct hi5622_ba_ops *ops, const u8 *wire,
		       size_t len)
{
	if (!ops || !ops->post_h2d || !wire || !len)
		return HI5622_BA_EINVAL;
	return ops->post_h2d(ops->ctx, wire, len);
}

static int ba_post_relay_addba_rsp(struct hi5622_ba_core *core,
				   const struct hi5622_ba_ops *ops,
				   const struct hi5622_ba_peer *peer,
				   const struct hi5622_ba_params *p)
{
	u8 wire[HI5622_BA_WIRE_MAX];
	size_t len;
	int rc;

	len = hi5622_ba_wire_relay_addba_rsp(wire, sizeof(wire), peer->uid, p);
	if (len != HI5622_BA_RELAY_WIRE_LEN)
		return HI5622_BA_EINVAL;
	rc = ba_post_h2d(ops, wire, len);
	core->stats.last_rc = rc;
	if (!rc)
		core->stats.relay_rsp++;
	else
		core->stats.refused++;
	return rc;
}

static int ba_post_relay_delba(struct hi5622_ba_core *core,
			       const struct hi5622_ba_ops *ops,
			       const struct hi5622_ba_peer *peer, u8 tid,
			       u8 initiator)
{
	u8 wire[HI5622_BA_WIRE_MAX];
	size_t len;
	int rc;

	len = hi5622_ba_wire_relay_delba(wire, sizeof(wire), peer->uid, tid,
					 initiator);
	if (len != HI5622_BA_RELAY_WIRE_LEN)
		return HI5622_BA_EINVAL;
	rc = ba_post_h2d(ops, wire, len);
	core->stats.last_rc = rc;
	if (!rc)
		core->stats.relay_delba++;
	else
		core->stats.refused++;
	return rc;
}

/* The g0 0x1C TX BA notify: informational (the decoded vendor firmware
 * handler builds a follow-up MAC-core message); a post failure is recorded
 * but never blocks the session state - the decoded contract does not make
 * the notify a precondition of aggregation. */
static void ba_post_notify(struct hi5622_ba_core *core,
			   const struct hi5622_ba_ops *ops,
			   const struct hi5622_ba_peer *peer, u8 tid, u8 value)
{
	u8 wire[HI5622_BA_WIRE_MAX];
	size_t len;
	int rc;

	len = hi5622_ba_wire_tx_notify(wire, sizeof(wire), peer->uid, tid,
				       value);
	if (len != HI5622_BA_NOTIFY_WIRE_LEN)
		return;
	rc = ba_post_h2d(ops, wire, len);
	core->stats.last_rc = rc;
	if (!rc)
		core->stats.notify++;
	if (ops && ops->log && value == HI5622_BA_NOTIFY_TEARDOWN)
		ops->log(ops->ctx, "ba: tx notify (teardown)");
}

/* ---- TX data path: aggregation window boundaries ----------------------- */

int hi5622_ba_tx_mpdu(struct hi5622_ba_core *core,
		      const struct hi5622_ba_ops *ops, const u8 *mac, u8 tid,
		      u16 seq)
{
	struct hi5622_ba_peer *peer;
	struct hi5622_ba_session *s;
	u8 wire[HI5622_BA_WIRE_MAX];
	size_t len;
	int ret = hi5622_ba_check_enabled();

	if (ret)
		return ret;
	if (!core || !mac || tid >= HI5622_BA_MAX_TID)
		return HI5622_BA_EINVAL;
	peer = hi5622_ba_peer_get(core, mac);
	if (!peer)
		return HI5622_BA_ENOSPC;
	s = &peer->tx[tid];
	if (!s->used || s->state != 2)
		return HI5622_BA_EOPNOTSUPP;	/* no agreement: never aggregate */
	s->last_seq = seq;
	if (!s->ampdu_started) {
		len = hi5622_ba_wire_ampdu_start(wire, sizeof(wire), tid, seq,
						 peer->uid, 0);
		if (len != HI5622_BA_AMPDU_WIRE_LEN)
			return HI5622_BA_EINVAL;
		ret = ba_post_h2d(ops, wire, len);
		core->stats.last_rc = ret;
		if (ret) {
			core->stats.refused++;
			return ret;
		}
		core->stats.ampdu_start++;
		s->ampdu_started = 1;
		s->burst_count = 1;
		if (ops && ops->log)
			ops->log(ops->ctx, "ba: ampdu start");
		return HI5622_BA_OK;
	}
	if (s->burst_count >= HI5622_BA_BURST_MAX) {
		ret = hi5622_ba_tx_burst_end(core, ops, mac, tid);
		if (ret)
			return ret;
		return hi5622_ba_tx_mpdu(core, ops, mac, tid, seq);
	}
	s->burst_count++;
	return HI5622_BA_OK;
}

int hi5622_ba_tx_burst_end(struct hi5622_ba_core *core,
			   const struct hi5622_ba_ops *ops, const u8 *mac,
			   u8 tid)
{
	struct hi5622_ba_peer *peer;
	struct hi5622_ba_session *s;
	u8 wire[HI5622_BA_WIRE_MAX];
	size_t len;
	int ret = hi5622_ba_check_enabled();

	if (ret)
		return ret;
	if (!core || !mac || tid >= HI5622_BA_MAX_TID)
		return HI5622_BA_EINVAL;
	peer = hi5622_ba_peer_get(core, mac);
	if (!peer)
		return HI5622_BA_ENOSPC;
	s = &peer->tx[tid];
	if (!s->used || !s->ampdu_started)
		return HI5622_BA_OK;
	len = hi5622_ba_wire_ampdu_end(wire, sizeof(wire), tid, peer->uid);
	if (len != HI5622_BA_AMPDU_WIRE_LEN)
		return HI5622_BA_EINVAL;
	ret = ba_post_h2d(ops, wire, len);
	core->stats.last_rc = ret;
	if (ret) {
		core->stats.refused++;
		return ret;
	}
	core->stats.ampdu_end++;
	s->ampdu_started = 0;
	s->burst_count = 0;
	if (ops && ops->log)
		ops->log(ops->ctx, "ba: ampdu end");
	return HI5622_BA_OK;
}

/* ---- host-initiated TX BA session (the in-kernel originator) ----------- */

int hi5622_ba_tx_start(struct hi5622_ba_core *core,
		       const struct hi5622_ba_ops *ops, const u8 *mac,
		       const struct hi5622_ba_params *p)
{
	struct hi5622_ba_peer *peer;
	struct hi5622_ba_session *s;
	u8 frame[HI5622_BA_FRAME_ADDBA_REQ_LEN];
	struct hi5622_ba_params fp;
	size_t flen;
	int ret = hi5622_ba_check_enabled();

	if (ret)
		return ret;
	if (!core || !ops || !ops->tx_action || !mac || !p ||
	    p->tid >= HI5622_BA_MAX_TID)
		return HI5622_BA_EINVAL;
	if (p->baw < HI5622_BA_BAW_MIN || p->baw > HI5622_BA_BAW_MAX)
		return HI5622_BA_EINVAL;
	peer = hi5622_ba_peer_get(core, mac);
	if (!peer)
		return HI5622_BA_ENOSPC;
	s = &peer->tx[p->tid];
	if (s->used && s->state == 2)
		return HI5622_BA_OK;	/* already active: nothing to negotiate */
	/* a retry of a pending request increments the dialog token (the
	 * vendor increments a per-user counter) */
	s->dialog = (u8)((s->used && s->state == 1) ? (s->dialog + 1) : 1);
	s->used = 1;
	s->state = 1;			/* ADDBA pending */
	s->tid = p->tid;
	s->policy = (u8)(p->policy & 1);
	s->amsdu = (u8)(p->amsdu & 1);
	s->baw = ba_clamp_baw(p->baw);
	s->timeout_tu = p->timeout_tu;
	s->ampdu_started = 0;
	s->burst_count = 0;
	fp = *p;
	fp.dialog = s->dialog;
	fp.baw = s->baw;
	flen = hi5622_ba_build_addba_req(frame, sizeof(frame), &fp, peer->mac,
					 core->own_mac, core->bssid);
	if (flen != HI5622_BA_FRAME_ADDBA_REQ_LEN) {
		s->state = 0;
		s->used = 0;
		return HI5622_BA_EINVAL;
	}
	ret = ops->tx_action(ops->ctx, frame, flen);
	core->stats.last_rc = ret;
	if (ret) {
		core->stats.refused++;
		s->state = 0;
		s->used = 0;
		return ret;
	}
	core->stats.addba_req++;
	if (ops->log)
		ops->log(ops->ctx, "ba: addba request tx");
	return HI5622_BA_OK;
}

/* The TX-path hook: the in-kernel ADDBA negotiation decision for one QoS
 * data MPDU.  This is what makes the AP the TX originator without any
 * userspace help (stock hostapd has none of the vendor WAL commands). */
int hi5622_ba_tx_data(struct hi5622_ba_core *core,
		      const struct hi5622_ba_ops *ops, const u8 *mac, u8 tid,
		      u16 seq)
{
	struct hi5622_ba_peer *peer;
	struct hi5622_ba_session *s;
	struct hi5622_ba_params p;
	int ret = hi5622_ba_check_enabled();

	if (ret)
		return ret;
	if (!core || !mac || tid >= HI5622_BA_MAX_TID)
		return HI5622_BA_EINVAL;
	peer = hi5622_ba_peer_get(core, mac);
	if (!peer)
		return HI5622_BA_ENOSPC;
	s = &peer->tx[tid];
	if (!s->used || s->state == 0) {
		ba_memset(&p, 0, sizeof(p));
		p.tid = tid;
		p.baw = HI5622_BA_BAW_DEFAULT;
		p.timeout_tu = HI5622_BA_TIMEOUT_DEFAULT;
		p.policy = 1;	/* immediate BA (vendor default) */
		p.amsdu = 0;
		return hi5622_ba_tx_start(core, ops, mac, &p);
	}
	if (s->state == 1)
		return HI5622_BA_EAGAIN;	/* request outstanding: wait */
	return hi5622_ba_tx_mpdu(core, ops, mac, tid, seq);
}

/* ---- received action frame --------------------------------------------- */

int hi5622_ba_mgmt_rx(struct hi5622_ba_core *core,
		      const struct hi5622_ba_ops *ops, u16 uid, const u8 *mac,
		      const u8 *frame, size_t len)
{
	struct hi5622_ba_action a;
	struct hi5622_ba_peer *peer;
	struct hi5622_ba_session *s;
	int ret = hi5622_ba_check_enabled();

	if (ret)
		return ret;
	if (!core || !ops || !mac || !frame)
		return HI5622_BA_EINVAL;
	if (hi5622_ba_parse_action(frame, len, &a) != HI5622_BA_OK) {
		core->stats.refused++;
		return HI5622_BA_EINVAL;
	}
	peer = hi5622_ba_peer_get(core, mac);
	if (!peer)
		return HI5622_BA_ENOSPC;
	if (uid)
		peer->uid = uid;
	s = &peer->tx[a.tid];

	if (a.action == HI5622_BA_ACTION_ADDBA_REQ) {
		/* peer is the originator: AP answers with the response frame
		 * and mirrors the RX session.  The vendor's per-TID RX sync to
		 * the firmware uses the user-sync writer (item 7/8), whose
		 * wire encoding is parked [U] - that is the RX direction, not
		 * this lane's cut. */
		u8 rsp[HI5622_BA_FRAME_ADDBA_RSP_LEN];
		struct hi5622_ba_params rp;
		size_t rlen;

		s->used = 1;
		s->state = 2;
		s->tid = a.tid;
		s->dialog = a.dialog;
		s->policy = a.policy;
		s->amsdu = a.amsdu;
		s->baw = ba_clamp_baw(a.baw);
		s->timeout_tu = a.timeout_tu;
		s->start_seq = a.start_seq;
		ba_memset(&rp, 0, sizeof(rp));
		rp.tid = a.tid;
		rp.dialog = a.dialog;
		rp.policy = a.policy;
		rp.amsdu = a.amsdu;
		rp.baw = s->baw;
		rp.status = 0;
		rlen = hi5622_ba_build_addba_rsp(rsp, sizeof(rsp), &rp,
						 peer->mac, core->own_mac,
						 core->bssid);
		if (rlen != HI5622_BA_FRAME_ADDBA_RSP_LEN)
			return HI5622_BA_EINVAL;
		if (!ops->tx_action)
			return HI5622_BA_EINVAL;
		ret = ops->tx_action(ops->ctx, rsp, rlen);
		core->stats.last_rc = ret;
		if (ret) {
			core->stats.refused++;
			return ret;
		}
		core->stats.addba_rsp++;
		if (ops->log)
			ops->log(ops->ctx, "ba: addba response tx");
		return HI5622_BA_OK;
	}

	if (a.action == HI5622_BA_ACTION_ADDBA_RSP) {
		struct hi5622_ba_params rp;

		if (!s->used || s->state != 1) {
			core->stats.refused++;
			return HI5622_BA_EINVAL;	/* no pending request */
		}
		if (a.dialog != s->dialog) {
			core->stats.refused++;
			return HI5622_BA_EINVAL;	/* stale response */
		}
		ba_memset(&rp, 0, sizeof(rp));
		rp.tid = a.tid;
		rp.dialog = a.dialog;
		rp.policy = a.policy;
		rp.amsdu = a.amsdu;
		rp.baw = ba_clamp_baw(a.baw);
		rp.timeout_tu = a.timeout_tu;
		rp.status = a.status;
		ret = ba_post_relay_addba_rsp(core, ops, peer, &rp);
		if (ret)
			return ret;
		if (a.status == 0) {
			s->state = 2;	/* active: aggregation allowed */
			s->baw = rp.baw;
			s->timeout_tu = a.timeout_tu;
			s->policy = a.policy;
			s->amsdu = a.amsdu;
			core->stats.sessions++;
			/* vendor state machine: notify on pending -> active */
			ba_post_notify(core, ops, peer, a.tid,
				       HI5622_BA_NOTIFY_ACTIVE);
			if (ops->log)
				ops->log(ops->ctx, "ba: relay addba rsp (active)");
		} else {
			s->used = 0;
			s->state = 0;
			if (ops->log)
				ops->log(ops->ctx, "ba: relay addba rsp (failed)");
		}
		return HI5622_BA_OK;
	}

	/* DELBA */
	ret = ba_post_relay_delba(core, ops, peer, a.tid, a.initiator);
	if (ret)
		return ret;
	if (s->used && s->state == 2)
		ba_post_notify(core, ops, peer, a.tid,
			       HI5622_BA_NOTIFY_TEARDOWN);
	s->used = 0;
	s->state = 0;
	s->ampdu_started = 0;
	if (ops->log)
		ops->log(ops->ctx, "ba: relay delba");
	return HI5622_BA_OK;
}

/* ---- firmware D2H events ----------------------------------------------- */

int hi5622_ba_d2h(struct hi5622_ba_core *core,
		  const struct hi5622_ba_ops *ops, const u8 *hdr, size_t len)
{
	struct hi5622_ba_peer *peer;
	struct hi5622_ba_params p;
	u16 uid;
	u8 id, tid, action = 0;
	int ret = hi5622_ba_check_enabled();

	if (ret)
		return ret;
	if (!core || !ops || !hdr || len < HI5622_BA_HDR_LEN + 4)
		return HI5622_BA_EINVAL;
	id = hdr[6];
	/* Not a BA event: ignore before any peer lookup (an unrelated D2H id
	 * must never be misread as a BA session request). */
	if (id != HI5622_BA_D2H_CREATE_BA && id != HI5622_BA_D2H_DEL_BA &&
	    id != HI5622_BA_D2H_RX_DELBA_EVENT && id != HI5622_BA_D2H_BA_ACTION)
		return HI5622_BA_OK;
	uid = ba_get_le16(hdr + HI5622_BA_HDR_LEN);
	if (id == HI5622_BA_D2H_BA_ACTION)
		action = hdr[HI5622_BA_HDR_LEN + 3];
	/* D2H 0x0F carries the TID at hdr[19] (the vendor handler reads
	 * hdr[19..21]); ids 2/3/0x1D use hdr[14]. */
	tid = (id == HI5622_BA_D2H_RX_DELBA_EVENT) ? hdr[19]
						    : hdr[HI5622_BA_HDR_LEN + 2];
	if (tid >= HI5622_BA_MAX_TID)
		return HI5622_BA_EINVAL;
	peer = hi5622_ba_peer_get_by_uid(core, uid);
	if (!peer) {
		/* no host-side user yet: fail closed, never guess a peer */
		core->stats.refused++;
		return HI5622_BA_EOPNOTSUPP;
	}
	core->stats.d2h_events++;

	switch (id) {
	case HI5622_BA_D2H_CREATE_BA:
	case HI5622_BA_D2H_BA_ACTION:
		if (id == HI5622_BA_D2H_BA_ACTION && action == 1) {
			/* firmware relayed a received ADDBA response: the frame
			 * bytes start at hdr+20 (TABLES T7) */
			if (len < HI5622_BA_HDR_LEN + 8)
				return HI5622_BA_EINVAL;
			return hi5622_ba_mgmt_rx(core, ops, uid, peer->mac,
						 hdr + HI5622_BA_HDR_LEN + 8,
						 len - HI5622_BA_HDR_LEN - 8);
		}
		if (id == HI5622_BA_D2H_BA_ACTION && action == 2) {
			/* firmware asks for a DELBA: initiator=1, reason=1 */
			u8 frame[HI5622_BA_FRAME_DELBA_LEN];
			size_t flen;

			flen = hi5622_ba_build_delba(frame, sizeof(frame), tid,
						     1, 1, peer->mac,
						     core->own_mac, core->bssid);
			if (flen != HI5622_BA_FRAME_DELBA_LEN)
				return HI5622_BA_EINVAL;
			if (!ops->tx_action)
				return HI5622_BA_EINVAL;
			ret = ops->tx_action(ops->ctx, frame, flen);
			core->stats.last_rc = ret;
			if (ret)
				return ret;
			core->stats.delba++;
			return HI5622_BA_OK;
		}
		/* action 0 (or the old D2H 2): start a TX BA session */
		ba_memset(&p, 0, sizeof(p));
		p.tid = tid;
		p.baw = HI5622_BA_BAW_DEFAULT;
		p.dialog = (u8)(peer->tx[tid].dialog + 1);
		p.policy = 1;	/* immediate BA (vendor default) */
		p.amsdu = 0;
		return hi5622_ba_tx_start(core, ops, peer->mac, &p);

	case HI5622_BA_D2H_DEL_BA:
	case HI5622_BA_D2H_RX_DELBA_EVENT: {
		u8 frame[HI5622_BA_FRAME_DELBA_LEN];
		size_t flen;
		struct hi5622_ba_session *s = &peer->tx[tid];

		if (!s->used || s->state != 2)
			return HI5622_BA_OK;	/* nothing to tear down */
		flen = hi5622_ba_build_delba(frame, sizeof(frame), tid, 1, 1,
					     peer->mac, core->own_mac,
					     core->bssid);
		if (flen != HI5622_BA_FRAME_DELBA_LEN)
			return HI5622_BA_EINVAL;
		if (ops->tx_action) {
			ret = ops->tx_action(ops->ctx, frame, flen);
			core->stats.last_rc = ret;
			if (ret)
				return ret;
		}
		core->stats.delba++;
		ba_post_notify(core, ops, peer, tid,
			       HI5622_BA_NOTIFY_TEARDOWN);
		s->used = 0;
		s->state = 0;
		s->ampdu_started = 0;
		if (ops->log)
			ops->log(ops->ctx, "ba: d2h delba -> delba tx");
		return HI5622_BA_OK;
	}
	default:
		return HI5622_BA_OK;	/* not a BA event: ignore */
	}
}

/* ---- read-only status -------------------------------------------------- */

int hi5622_ba_status_line(const struct hi5622_ba_core *core,
			  unsigned int ba_enable, char *buf, size_t len)
{
	const struct hi5622_ba_stats *st;
	unsigned int peers = 0, active = 0, pending = 0;
	const char *mode;
	int i, t, n;

	if (!buf || !len)
		return HI5622_BA_EINVAL;
	if (ba_enable == 0u)
		mode = "off";
	else if (ba_enable == 1u)
		mode = "tx-addba";
	else
		mode = "invalid";
	if (core) {
		for (i = 0; i < HI5622_BA_MAX_PEERS; i++) {
			if (!core->peers[i].used)
				continue;
			peers++;
			for (t = 0; t < HI5622_BA_MAX_TID; t++) {
				if (!core->peers[i].tx[t].used)
					continue;
				if (core->peers[i].tx[t].state == 2)
					active++;
				else if (core->peers[i].tx[t].state == 1)
					pending++;
			}
		}
	}
	st = core ? &core->stats : NULL;
	n = snprintf(buf, len,
		     "ba: ba_enable=%u (%s) peers=%u active=%u pending=%u req=%u rsp=%u delba=%u relay=%u/%u ampdu=%u/%u notify=%u events=%u refused=%u last_rc=%d\n",
		     ba_enable, mode, peers, active, pending,
		     st ? st->addba_req : 0u, st ? st->addba_rsp : 0u,
		     st ? st->delba : 0u, st ? st->relay_rsp : 0u,
		     st ? st->relay_delba : 0u, st ? st->ampdu_start : 0u,
		     st ? st->ampdu_end : 0u, st ? st->notify : 0u,
		     st ? st->d2h_events : 0u, st ? st->refused : 0u,
		     st ? (int)st->last_rc : 0);
	if (n < 0)
		return (int)n;
	return (int)((size_t)n < len ? (size_t)n : len - 1);
}
