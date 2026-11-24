// SPDX-License-Identifier: GPL-2.0
/*
 * h2d_send_logic.c - portable policy/emission logic for the first REAL
 * post-ready H2D message path (Hi5622V100 clean-room driver).
 *
 * ORIGINAL clean-room code by the wifi-h2d-send lane (task-98, extended by
 * task-102 with the ordered context-create precondition, 2026-11-24).  Every
 * constant is a derived value with an evidence marker in REPORT.md; no vendor
 * source/object/firmware byte is reproduced.  This file performs no IO and
 * includes no kernel header, so the same unit is linked into the kernel module
 * and into the host selftest.
 */
#include "h2d_send_logic.h"

/* -EINVAL / -ERANGE: kernel and host both provide these. */
#ifdef __KERNEL__
#include <linux/errno.h>
#else
#include <errno.h>
#endif

/* ------------------------------------------------------------------ */
/* the byte-exact first post-ready vendor frame (phase20 A.5/phase38)  */
/* ------------------------------------------------------------------ */

/* token[8] = {u32 0, u32 1} - inherited buffer bytes, not written by the
 * vendor builder [V]. */
static const u8 h2d_txq_token[8] = {
	0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
};

/* cmd/word8 = 0x00d8 (the TXQ-table address sync command) [V capture; the
 * producing call site has no match in the analysed vendor build - see
 * wifi-h2d-frames REPORT sec 8]. */
#define H2D_TXQ_CMD	0x00d8u

/* data[20] = {u32 1, 16 x 00} - the vendor sync payload struct [V]. */
static const u8 h2d_txq_data[20] = {
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
};

/* tail[4] = 0xffffffff - inherited (uninitialised) buffer bytes [V]. */
static const u8 h2d_txq_tail[4] = { 0xff, 0xff, 0xff, 0xff };

/* The vendor's first post-ready frame is a group-0 id-1 config event with
 * fa=0, fb=4, fc=0, dst=0 [V byte-exact capture]. */
#define H2D_TXQ_FA	0x00u
#define H2D_TXQ_FB	0x04u
#define H2D_TXQ_FC	0x00u

/* ------------------------------------------------------------------ */
/* context-create config event (g0 id1, key 0x0064) - task-102         */
/* ------------------------------------------------------------------ */

/* The g0 id1 config-event shape is identical to the TXQ sync frame: the same
 * inherited token[8] and 0xff tail.  Only the u16 at payload+8 (the key) and
 * the data block differ.  Key 0x0064 is table-A entry 1 -> worker CA 0x449E4,
 * the only host-reachable caller of the context-create helper CA 0xFE6BC
 * (../wifi-ctx-create/REPORT.md; firmware-side derived, SYNTHESIZED payload). */
static const u8 h2d_ctx_token[8] = {
	0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
};

/* data[20]: data[0] = type 3 (minimal create; 1/2 accepted by the helper),
 * data[1] = parent/vap module 4 (lookup gate; that module's byte 1 is 0),
 * data[4] = user id = the module index that gets created (4 = the TXQ field B),
 * data[0x11]/[0x12] = 0 (helper flag reads). */
static const u8 h2d_ctx_data[H2D_CTX_LEN] = {
	0x03, 0x04, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
};

/* tail[4] = 0xffffffff - inherited (uninitialised) buffer bytes [V]. */
static const u8 h2d_ctx_tail[4] = { 0xff, 0xff, 0xff, 0xff };

/* The create frame's header fields: g0 id1, fa=0, fb=4 (handler lookup and
 * the created module index), fc=0, dst 0. */
#define H2D_CTX_FA	0x00u
#define H2D_CTX_FC	0x00u

/* hmac_edca_opt_timeout_fn live steady-state frame: group-0 id 0x1d, fa=0,
 * fb=4, fc=0, 0x3c-byte zero payload (wifi-h2d-frames REPORT 4.2(d) [V]). */
#define H2D_EDCA_FA	0x00u
#define H2D_EDCA_FB	0x04u
#define H2D_EDCA_FC	0x00u

/* ------------------------------------------------------------------ */
/* names                                                               */
/* ------------------------------------------------------------------ */
const char *h2d_msg_name(enum h2d_msg msg)
{
	switch (msg) {
	case H2D_MSG_STUB:	return "stub";
	case H2D_MSG_TXQ_SYNC:	return "txq_sync";
	case H2D_MSG_TXQ_SYNC72:	return "txq_sync72";
	case H2D_MSG_EDCA_OPT:	return "edca";
	case H2D_MSG_DEL_USER:	return "del_user";
	case H2D_MSG_CTX_CREATE:	return "ctx_create";
	case H2D_MSG_CTX_CREATE_T1:	return "ctx_create_t1";
	case H2D_MSG_CTX_CREATE_T2:	return "ctx_create_t2";
	default:		return "?";
	}
}

const char *h2d_gate_name(enum h2d_gate gate)
{
	switch (gate) {
	case H2D_GATE_OK:		return "ok";
	case H2D_GATE_BAD_MSG:		return "bad_msg";
	case H2D_GATE_NOT_ARMED:	return "not_armed";
	case H2D_GATE_NOT_RELEASED:	return "not_released";
	case H2D_GATE_NOT_LATCHED:	return "not_latched";
	case H2D_GATE_PHASE:		return "phase_not_ready";
	case H2D_GATE_TXQ_PHASE:	return "txq_phase_not_ready";
	case H2D_GATE_LANE_CLOSED:	return "lane_closed";
	case H2D_GATE_LIVENESS_USED:	return "liveness_probe_used";
	case H2D_GATE_BUDGET:		return "budget_exhausted";
	case H2D_GATE_INFLIGHT:		return "announce_inflight";
	case H2D_GATE_OUT0_PENDING:	return "out0_not_consumed";
	case H2D_GATE_DUP_TXQ:		return "txq_already_sent";
	case H2D_GATE_DUP_CTX:		return "ctx_already_created";
	case H2D_GATE_CTX_PHASE:	return "ctx_phase_not_ready";
	case H2D_GATE_FRAME_INVALID:	return "frame_invalid";
	default:			return "?";
	}
}

bool h2d_msg_is_ctx(enum h2d_msg msg)
{
	return msg == H2D_MSG_CTX_CREATE || msg == H2D_MSG_CTX_CREATE_T1 ||
	       msg == H2D_MSG_CTX_CREATE_T2;
}

int h2d_msg_name_parse(const char *name, enum h2d_msg *msg)
{
	static const struct {
		const char	*name;
		enum h2d_msg	msg;
	} table[] = {
		{ "txq_sync",	H2D_MSG_TXQ_SYNC },
		{ "txq_sync72",	H2D_MSG_TXQ_SYNC72 },
		{ "ctx_create",	H2D_MSG_CTX_CREATE },
		{ "ctx_create_t1",	H2D_MSG_CTX_CREATE_T1 },
		{ "ctx_create_t2",	H2D_MSG_CTX_CREATE_T2 },
		{ "stub",	H2D_MSG_STUB },
		{ "edca",	H2D_MSG_EDCA_OPT },
		{ "del_user",	H2D_MSG_DEL_USER },
	};
	size_t i;

	if (!name || !msg)
		return -1;
	for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
		const char *a = table[i].name;
		const char *b = name;

		while (*a && *a == *b) {
			a++;
			b++;
		}
		if (!*a && !*b) {
			*msg = table[i].msg;
			return 0;
		}
	}
	return -1;
}

/* ------------------------------------------------------------------ */
/* gate                                                                */
/* ------------------------------------------------------------------ */
static bool h2d_phase_ok(enum hcc_phase phase)
{
	return phase == HCC_PHASE_READY_TXQ_SYNC ||
	       phase == HCC_PHASE_ACTIVE;
}

enum h2d_gate h2d_gate_check(const struct h2d_live *live, enum h2d_msg msg)
{
	if (!live || msg >= H2D_MSG_COUNT)
		return H2D_GATE_BAD_MSG;
	/* Boot-stage gates first: never post H2D during the ROM boot stage and
	 * never before the transport release (project hard rules). */
	if (!live->armed)
		return H2D_GATE_NOT_ARMED;
	if (!live->released)
		return H2D_GATE_NOT_RELEASED;
	if (!live->boot_latched)
		return H2D_GATE_NOT_LATCHED;
	if (live->lane_closed)
		return H2D_GATE_LANE_CLOSED;
	/* Core phase must be READY_TXQ_SYNC or later (FAILED is not "later"). */
	if (!h2d_phase_ok(live->phase))
		return H2D_GATE_PHASE;
	if (h2d_msg_is_ctx(msg)) {
		/* The context-create must precede the TXQ sync and allocates the
		 * single ctx+0x190 buffer, so it is READY-only and once per load.
		 * All three data[0] type variants share this one-shot rule. */
		if (live->ctx_created)
			return H2D_GATE_DUP_CTX;
		if (live->phase != HCC_PHASE_READY_TXQ_SYNC)
			return H2D_GATE_CTX_PHASE;
	}
	if (msg == H2D_MSG_TXQ_SYNC || msg == H2D_MSG_TXQ_SYNC72) {
		/* The vendor's first post-ready message is sent exactly once.  The
		 * ordinary path sends it from READY_TXQ_SYNC (the core flips to
		 * ACTIVE on commit).  ORDERED CREATE PATH: ctx_create is itself a
		 * group-0/id-1 frame, so the core's phase machine latches ACTIVE on
		 * it.  The driver keeps the semantic distinction (txq_submitted is
		 * only set by a real TXQ frame), so ACTIVE-after-create still allows
		 * exactly one TXQ frame - the ordered two-message ladder. */
		if (live->txq_submitted)
			return H2D_GATE_DUP_TXQ;
		/* READY_TXQ_SYNC is the ordinary path; ACTIVE is legal only after
		 * this load's ctx_create (the ordered two-message ladder). */
		if (!(live->phase == HCC_PHASE_READY_TXQ_SYNC ||
		      (live->phase == HCC_PHASE_ACTIVE && live->ctx_created)))
			return H2D_GATE_TXQ_PHASE;
	}
	/* One idea, one frame: the liveness stub and a real message are
	 * mutually exclusive in a module load. */
	if (live->liveness_sent)
		return H2D_GATE_LIVENESS_USED;
	if (live->frames_sent >= live->budget)
		return H2D_GATE_BUDGET;
	/* Coalescing preconditions: the device must have consumed the previous
	 * announce word before a new SR commit may publish another. */
	if (live->mbox_inflight)
		return H2D_GATE_INFLIGHT;
	if (live->out0)
		return H2D_GATE_OUT0_PENDING;
	return H2D_GATE_OK;
}

/* ------------------------------------------------------------------ */
/* frame emission                                                      */
/* ------------------------------------------------------------------ */

/* The context-create config event: group 0 / id 1, key 0x0064, len 0x14,
 * fb == user == the created module index.  Uses the shared byte-exact
 * config-event builder; the create helper only allocates ctx+0x190 for
 * data[0] in {1,2,3}, and the worker's lookup uses hdr[3] as the same module
 * index that data[4] then creates. */
int h2d_build_ctx_create(struct h2d_frame *f, u8 dst, u8 ctx_type, u8 vap,
			 u8 user, enum h2d_err *err)
{
	u8 data[H2D_CTX_LEN];

	/* only types 1/2/3 reach the +0x190 allocation path */
	if (ctx_type < 1u || ctx_type > 3u) {
		if (err)
			*err = H2D_EARG;
		return -1;
	}
	if (vap > H2D_CTX_MAX_MODULE || user > H2D_CTX_MAX_MODULE) {
		if (err)
			*err = H2D_EBOUNDS;
		return -1;
	}
	/* The handler looks up hdr[3]; the worker then creates data[4].  Both are
	 * the same module index (the frame's field B and the created id). */
	memcpy(data, h2d_ctx_data, sizeof(data));
	data[0] = ctx_type;
	data[1] = vap;
	data[4] = user;
	return h2d_build_g0_cfg_event(f, 1u, dst, H2D_CTX_FA, user,
				      H2D_CTX_FC, h2d_ctx_token, H2D_CTX_KEY,
				      data, (u16)sizeof(data), h2d_ctx_tail, err);
}

int h2d_msg_build(enum h2d_msg msg, u8 dst, u16 user_id, const u8 *addr,
		  struct h2d_frame *f, u8 *wire, u32 cap, u32 *total,
		  enum h2d_err *err)
{
	size_t wire_len = 0;
	enum h2d_err e = H2D_OK;
	int rc;

	if (!f || !wire || !total) {
		if (err)
			*err = H2D_EARG;
		return -1;
	}
	if (dst > H2D_MAX_DST) {
		if (err)
			*err = H2D_EBOUNDS;
		return -1;
	}

	switch (msg) {
	case H2D_MSG_STUB:
		/* The only safe bare probe: payload-free group-0 id-0 stub. */
		rc = h2d_build_safe_probe(f, dst);
		if (rc != 0)
			e = H2D_EPOLICY;
		break;
	case H2D_MSG_TXQ_SYNC:
	case H2D_MSG_TXQ_SYNC72:
		/* Byte-exact canned frame: only dst 0 is the vendor capture.  The
		 * 72 variant differs only in the SR node buffer length below. */
		if (dst != 0u) {
			if (err)
				*err = H2D_EPOLICY;
			return -1;
		}
		rc = h2d_build_g0_id1_hdpp_txq_sync(f, 0u, H2D_TXQ_FA,
						    H2D_TXQ_FB, h2d_txq_token,
						    H2D_TXQ_CMD, h2d_txq_data,
						    (u16)sizeof(h2d_txq_data),
						    h2d_txq_tail, &e);
		break;
	case H2D_MSG_CTX_CREATE:
	case H2D_MSG_CTX_CREATE_T1:
	case H2D_MSG_CTX_CREATE_T2: {
		/* Synthesized config event: only the canned dst-0 instance is
		 * allowed (the header fields are part of the derived frame).  The
		 * three variants differ ONLY in data[0] (wire byte 24): the create
		 * helper's type dispatch (1 = defaults + second 0xc alloc, 2 =
		 * defaults, 3 = straight to the shared 0x3a8 alloc). */
		u8 ctx_type = H2D_CTX_TYPE;

		if (dst != 0u) {
			if (err)
				*err = H2D_EPOLICY;
			return -1;
		}
		if (msg == H2D_MSG_CTX_CREATE_T1)
			ctx_type = 1u;
		else if (msg == H2D_MSG_CTX_CREATE_T2)
			ctx_type = 2u;
		rc = h2d_build_ctx_create(f, 0u, ctx_type, H2D_CTX_VAP,
					  H2D_CTX_USER, &e);
		break;
	}
	case H2D_MSG_EDCA_OPT:
		rc = h2d_build_g0_id1d_edca_opt(f, dst, H2D_EDCA_FA,
						H2D_EDCA_FB, H2D_EDCA_FC, &e);
		break;
	case H2D_MSG_DEL_USER:
		rc = h2d_build_g0_id09_del_user(f, dst, user_id, addr, &e);
		break;
	default:
		if (err)
			*err = H2D_EARG;
		return -1;
	}
	if (rc != 0) {
		if (err)
			*err = e;
		return -1;
	}
	/* Wire encode: re-runs the transport + firmware + handler + policy
	 * validation on the frame struct before a byte can reach the ring. */
	if (h2d_frame_encode(f, wire, (size_t)cap, &wire_len, &e) != 0) {
		if (err)
			*err = e;
		return -1;
	}
	if (wire_len > 0xffffu) {
		if (err)
			*err = H2D_EBOUNDS;
		return -1;
	}
	*total = (u32)wire_len;
	if (err)
		*err = H2D_OK;
	return 0;
}

/* Post-buffer length: default node_len = frame_total; txq_sync72 declares the
 * vendor's captured 72-byte live buffer for the same 48-byte frame. */
int h2d_msg_emit_plan(enum h2d_msg msg, u32 frame_total, u32 *node_len,
		      u8 *pad_byte)
{
	if (!node_len || !pad_byte || msg >= H2D_MSG_COUNT)
		return -EINVAL;
	if (msg == H2D_MSG_TXQ_SYNC72) {
		if (frame_total != H2D_TXQ_TOTAL)
			return -EINVAL;
		*node_len = H2D_TXQ_NODE_LEN;
		*pad_byte = H2D_TXQ_PAD_BYTE;
		return 0;
	}
	*node_len = frame_total;
	*pad_byte = 0u;
	return 0;
}

/* ------------------------------------------------------------------ */
/* strict sysfs grammar                                                */
/* ------------------------------------------------------------------ */
static bool h2d_sep(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',';
}

static void h2d_skip_sep(const char **pp)
{
	const char *p = *pp;

	while (h2d_sep(*p))
		p++;
	*pp = p;
}

static int h2d_parse_u32(const char **pp, u32 max, u32 *out)
{
	const char *p = *pp;
	u32 v = 0, base = 10;
	bool any = false;

	if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
		base = 16;
		p += 2;
	}
	while (*p) {
		u32 d;

		if (base == 16) {
			if (*p >= '0' && *p <= '9')
				d = (u32)(*p - '0');
			else if (*p >= 'a' && *p <= 'f')
				d = (u32)(*p - 'a') + 10u;
			else if (*p >= 'A' && *p <= 'F')
				d = (u32)(*p - 'A') + 10u;
			else
				break;
		} else {
			if (*p < '0' || *p > '9')
				break;
			d = (u32)(*p - '0');
		}
		if (v > (max - d) / base)
			return -ERANGE;
		v = v * base + d;
		p++;
		any = true;
	}
	if (!any)
		return -EINVAL;
	*out = v;
	*pp = p;
	return 0;
}

static int h2d_parse_hex_addr(const char **pp, u8 addr[4])
{
	const char *p = *pp;
	u8 out[4];
	int i;

	if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
		p += 2;
	for (i = 0; i < 4; i++) {
		int hi, lo;

		if (p[0] >= '0' && p[0] <= '9')
			hi = p[0] - '0';
		else if (p[0] >= 'a' && p[0] <= 'f')
			hi = p[0] - 'a' + 10;
		else if (p[0] >= 'A' && p[0] <= 'F')
			hi = p[0] - 'A' + 10;
		else
			return -EINVAL;
		if (p[1] >= '0' && p[1] <= '9')
			lo = p[1] - '0';
		else if (p[1] >= 'a' && p[1] <= 'f')
			lo = p[1] - 'a' + 10;
		else if (p[1] >= 'A' && p[1] <= 'F')
			lo = p[1] - 'A' + 10;
		else
			return -EINVAL;
		out[i] = (u8)((hi << 4) | lo);
		p += 2;
	}
	memcpy(addr, out, sizeof(out));
	*pp = p;
	return 0;
}

int h2d_req_parse(const char *buf, struct h2d_req *req)
{
	const char *p = buf, *name;
	enum h2d_msg msg;
	size_t nlen;
	int rc;

	if (!buf || !req)
		return -EINVAL;
	memset(req, 0, sizeof(*req));

	h2d_skip_sep(&p);
	if (!*p)
		return -EINVAL;
	name = p;
	while (*p && !h2d_sep(*p))
		p++;
	nlen = (size_t)(p - name);
	if (!nlen || nlen > 16u)
		return -EINVAL;
	{
		char namebuf[17];

		memcpy(namebuf, name, nlen);
		namebuf[nlen] = '\0';
		if (h2d_msg_name_parse(namebuf, &msg) != 0)
			return -EINVAL;
	}
	req->msg = msg;

	while (true) {
		u32 v;

		h2d_skip_sep(&p);
		if (!*p)
			break;
		if (!strncmp(p, "dst=", 4)) {
			p += 4;
			rc = h2d_parse_u32(&p, H2D_MAX_DST, &v);
			if (rc)
				return rc;
			req->dst = (u8)v;
		} else if (msg == H2D_MSG_DEL_USER &&
			   !strncmp(p, "user=", 5)) {
			p += 5;
			rc = h2d_parse_u32(&p, 0xffffu, &v);
			if (rc)
				return rc;
			req->has_user = true;
			req->user_id = (u16)v;
		} else if (msg == H2D_MSG_DEL_USER &&
			   !strncmp(p, "addr=", 5)) {
			p += 5;
			rc = h2d_parse_hex_addr(&p, req->addr);
			if (rc)
				return rc;
			req->has_addr = true;
		} else {
			return -EINVAL;
		}
		if (*p && !h2d_sep(*p))
			return -EINVAL;
	}
	if (msg == H2D_MSG_DEL_USER && !req->has_user)
		return -EINVAL;
	if ((msg == H2D_MSG_TXQ_SYNC || msg == H2D_MSG_TXQ_SYNC72 ||
	     h2d_msg_is_ctx(msg)) && req->dst != 0u)
		return -EINVAL;
	return 0;
}