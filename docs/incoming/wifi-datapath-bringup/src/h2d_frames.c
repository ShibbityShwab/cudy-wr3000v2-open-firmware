// SPDX-License-Identifier: GPL-2.0
/*
 * h2d_frames.c - exact H2D frame catalogue, builders and validators for the
 * Hi5622V100 clean-room PCIe/ETE transport (Cudy WR3000 v2.0, luofu v2).
 *
 * ORIGINAL clean-room code written by the wifi-h2d-frames lane (task-93,
 * 2026-11-24) from the project's committed decoded contract; see REPORT.md for
 * the evidence behind every constant.  No vendor source, object code, firmware
 * byte, calibration value or secret is reproduced here.
 *
 * The only external dependency is memcpy()/memset(), so the same unit compiles
 * in a kernel module and in the userspace selftest.
 */
#include "h2d_frames.h"

/* Generated catalogue data (committed): vendor H2D senders + firmware tables. */
#include "h2d_catalogue.inc"

#define H2D_ARRAY_SIZE(a)	(sizeof(a) / sizeof((a)[0]))

/* ------------------------------------------------------------------ */
/* little-endian helpers (the HCC header is little-endian [V])         */
/* ------------------------------------------------------------------ */
static u16 h2d_get16(const u8 *p)
{
	return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static u32 h2d_get32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
	       ((u32)p[3] << 24);
}

static void h2d_put16(u8 *p, u16 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
}

static void h2d_put32(u8 *p, u32 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
	p[2] = (u8)((v >> 16) & 0xffu);
	p[3] = (u8)((v >> 24) & 0xffu);
}

static void h2d_frame_zero(struct h2d_frame *f)
{
	memset(f, 0, sizeof(*f));
}

/* ------------------------------------------------------------------ */
/* error / risk names                                                  */
/* ------------------------------------------------------------------ */
const char *h2d_strerror(enum h2d_err err)
{
	switch (err) {
	case H2D_OK:		return "ok";
	case H2D_EARG:		return "invalid argument/field";
	case H2D_ESHORT:	return "short frame (<12 bytes)";
	case H2D_ELEN:		return "bad declared total length";
	case H2D_ETAG:		return "transport tag != 0x5a5a";
	case H2D_EDST:		return "hdr[9] != hdr[1] >> 4";
	case H2D_EALLOC:	return "alloc nibble != 1";
	case H2D_EGROUP:	return "group has no H2D receive table";
	case H2D_EID:		return "unknown/unregistered message id";
	case H2D_EBOUNDS:	return "out of bounds (payload/dst/size)";
	case H2D_EBUFFER:	return "caller buffer too small";
	case H2D_ENOHANDLER:	return "firmware receive slot has no handler";
	case H2D_EPOLICY:	return "lane safety policy refused";
	case H2D_ESTATE:	return "mailbox/ring state precondition not met";
	case H2D_EWIRE:		return "malformed SR node";
	default:		return "unknown error";
	}
}

const char *h2d_risk_name(u8 risk)
{
	switch (risk) {
	case H2D_RISK_STUB:	 return "STUB";
	case H2D_RISK_NO_ACCESS: return "NO_ACCESS";
	case H2D_RISK_BYTE_READ: return "BYTE_READ";
	case H2D_RISK_PTR_DEREF: return "PTR_DEREF";
	case H2D_RISK_NO_HANDLER: return "NO_HANDLER";
	default:		 return "UNKNOWN";
	}
}

/* ------------------------------------------------------------------ */
/* catalogue accessors                                                 */
/* ------------------------------------------------------------------ */
const struct h2d_vendor_sender *h2d_vendor_senders_get(size_t *count)
{
	if (count)
		*count = H2D_ARRAY_SIZE(h2d_vendor_senders);
	return h2d_vendor_senders;
}

const struct h2d_fw_handler *h2d_fw_handlers_get(size_t *count)
{
	if (count)
		*count = H2D_ARRAY_SIZE(h2d_fw_handlers);
	return h2d_fw_handlers;
}

const struct h2d_fw_handler *h2d_fw_handler_lookup(u8 group, u16 id)
{
	size_t i;

	for (i = 0; i < H2D_ARRAY_SIZE(h2d_fw_handlers); i++) {
		if (h2d_fw_handlers[i].group == group &&
		    h2d_fw_handlers[i].id == (u8)id)
			return &h2d_fw_handlers[i];
	}
	return NULL;
}

static const struct h2d_vendor_sender *h2d_sender_lookup(u8 group, u16 id)
{
	size_t i;

	for (i = 0; i < H2D_ARRAY_SIZE(h2d_vendor_senders); i++) {
		if (h2d_vendor_senders[i].group == group &&
		    h2d_vendor_senders[i].id == (u8)id)
			return &h2d_vendor_senders[i];
	}
	return NULL;
}

/* ------------------------------------------------------------------ */
/* semantic validation (the frame struct is already well-formed)       */
/* ------------------------------------------------------------------ */
static enum h2d_err h2d_check_semantics(const struct h2d_frame *f, u32 vflags)
{
	const struct h2d_fw_handler *h;
	const struct h2d_vendor_sender *s;
	u32 total;

	if (f->group > 0x0fu || f->dst > H2D_MAX_DST)
		return H2D_EBOUNDS;
	if (f->payload_len > H2D_MAX_PAYLOAD)
		return H2D_EBOUNDS;

	total = (u32)f->payload_len + H2D_HDR_LEN;
	if (total > H2D_MAX_TOTAL || total > H2D_MAX_TOTAL_CLASS)
		return H2D_EBOUNDS;
	/* The firmware's header fetch also rejects class field > 3 [V]. */
	if (((total >> 9) & 0x7u) > 0x3u)
		return H2D_ELEN;

	if (!(vflags & H2D_VF_FIRMWARE))
		return H2D_OK;

	if (vflags & H2D_VF_D2H) {
		/* Device->host frames are chip-group 4 only; H2D rules do not apply. */
		if (f->group != H2D_GROUP_D2H_PLAT)
			return H2D_EGROUP;
		return H2D_OK;
	}

	if (!(vflags & H2D_VF_ANYGROUP)) {
		if (f->group == H2D_GROUP_D2H_PLAT)
			return H2D_EGROUP;	/* device->host only, no H2D table */
		if (f->group != H2D_GROUP_CHIP && f->group != H2D_GROUP_CORE)
			return H2D_EGROUP;
	}

	if (!(vflags & H2D_VF_HANDLER))
		return H2D_OK;

	h = h2d_fw_handler_lookup(f->group, f->id);
	if (!h || h->handler_ca == 0)
		return H2D_ENOHANDLER;

	if (!(vflags & H2D_VF_POLICY))
		return H2D_OK;

	s = h2d_sender_lookup(f->group, f->id);

	/* Conservative lane policy: HCC id 5 (group 2) is a real vendor message
	 * but its payload struct is not modelled; the mailbox id-5 trap makes the
	 * id an easy way to hurt the firmware, so require an explicit override. */
	if (f->id == 5u && !(f->policy & H2D_F_ALLOW_ID5_HCC))
		return H2D_EPOLICY;

	switch (h->risk) {
	case H2D_RISK_STUB:
		if (f->payload_len != 0)
			return H2D_EPOLICY;
		break;
	case H2D_RISK_NO_ACCESS:
		/* Payload-free is probe-safe; payload bytes need an exact builder
		 * (whose layout the vendor builder fixes) or a capture. */
		if (f->payload_len > 0 &&
		    !(f->policy & (H2D_F_EXACT | H2D_F_CAPTURED)))
			return H2D_EPOLICY;
		break;
	case H2D_RISK_BYTE_READ:
	case H2D_RISK_PTR_DEREF:
		/* The handler reads payload fields; never synthesize them. */
		if (!(f->policy & (H2D_F_EXACT | H2D_F_CAPTURED)))
			return H2D_EPOLICY;
		break;
	case H2D_RISK_NO_HANDLER:
		return H2D_ENOHANDLER;
	default:
		return H2D_EPOLICY;
	}

	/* A fixed-size vendor message must carry exactly that payload size
	 * (hcc_msg_alloc sizes it); a capture is evidence in its own right. */
	if (s && s->payload_len > 0 && !(f->policy & H2D_F_CAPTURED) &&
	    f->payload_len != (u16)s->payload_len)
		return H2D_EARG;

	return H2D_OK;
}

/* ------------------------------------------------------------------ */
/* frame encoding / decoding                                           */
/* ------------------------------------------------------------------ */
static int h2d_encode_checked(const struct h2d_frame *f, u8 *buf, size_t cap,
			      size_t *out_len, u32 vflags, enum h2d_err *err)
{
	u32 total = (u32)f->payload_len + H2D_HDR_LEN;
	enum h2d_err e;

	if (cap < (size_t)total) {
		if (err)
			*err = H2D_EBUFFER;
		return -1;
	}
	e = h2d_check_semantics(f, vflags);
	if (e != H2D_OK) {
		if (err)
			*err = e;
		return -1;
	}

	memset(buf, 0, (size_t)total);
	buf[H2D_GROUP_OFF] = (u8)(f->group & 0x0fu);
	buf[H2D_BID_OFF] = (u8)(((f->dst & 0x0fu) << 4) | H2D_ALLOC_NIBBLE);
	buf[H2D_FA_OFF] = f->fa;
	buf[H2D_FB_OFF] = f->fb;
	h2d_put16(buf + H2D_LEN_OFF, (u16)total);
	h2d_put16(buf + H2D_ID_OFF, f->id);
	buf[H2D_FC_OFF] = f->fc;
	buf[H2D_DST_OFF] = (u8)(f->dst & 0x0fu);
	h2d_put16(buf + H2D_TAG_OFF, H2D_TAG);
	if (f->payload_len)
		memcpy(buf + H2D_HDR_LEN, f->payload, (size_t)f->payload_len);
	if (out_len)
		*out_len = (size_t)total;
	if (err)
		*err = H2D_OK;
	return 0;
}

int h2d_frame_encode(const struct h2d_frame *f, u8 *buf, size_t cap,
		     size_t *out_len, enum h2d_err *err)
{
	return h2d_encode_checked(f, buf, cap, out_len,
				  H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
				  H2D_VF_HANDLER | H2D_VF_POLICY, err);
}

static int h2d_decode_checked(const u8 *buf, size_t len, struct h2d_frame *f,
			      u32 vflags, enum h2d_err *err)
{
	u16 total, id, tag;
	u8 alloc, dsth;
	enum h2d_err e;

	if (len < H2D_HDR_LEN) {
		if (err)
			*err = H2D_ESHORT;
		return -1;
	}
	tag = h2d_get16(buf + H2D_TAG_OFF);
	if (tag != H2D_TAG) {
		if (err)
			*err = H2D_ETAG;
		return -1;
	}
	total = h2d_get16(buf + H2D_LEN_OFF);
	if (total == 0) {
		/* Firmware SR-receive check: non-zero total length [V]. */
		if (err)
			*err = H2D_ELEN;
		return -1;
	}
	if (total < H2D_HDR_LEN || total > H2D_MAX_TOTAL ||
	    total > H2D_MAX_TOTAL_CLASS) {
		if (err)
			*err = H2D_EBOUNDS;
		return -1;
	}
	if ((size_t)total != len) {
		if (err)
			*err = H2D_ELEN;
		return -1;
	}
	alloc = (u8)(buf[H2D_BID_OFF] & 0x0fu);
	dsth = (u8)(buf[H2D_BID_OFF] >> 4);
	if (!(vflags & H2D_VF_D2H) && alloc != H2D_ALLOC_NIBBLE) {
		if (err)
			*err = H2D_EALLOC;
		return -1;
	}
	if (buf[H2D_DST_OFF] != dsth) {
		if (err)
			*err = H2D_EDST;
		return -1;
	}
	id = h2d_get16(buf + H2D_ID_OFF);

	h2d_frame_zero(f);
	f->group = (u8)(buf[H2D_GROUP_OFF] & 0x0fu);
	f->dst = buf[H2D_DST_OFF];
	f->fa = buf[H2D_FA_OFF];
	f->fb = buf[H2D_FB_OFF];
	f->fc = buf[H2D_FC_OFF];
	f->id = id;
	f->payload_len = (u16)(total - H2D_HDR_LEN);
	if (f->payload_len)
		memcpy(f->payload, buf + H2D_HDR_LEN, (size_t)f->payload_len);

	e = h2d_check_semantics(f, vflags);
	if (e != H2D_OK) {
		if (err)
			*err = e;
		return -1;
	}
	if (err)
		*err = H2D_OK;
	return 0;
}

int h2d_frame_decode(const u8 *buf, size_t len, struct h2d_frame *f,
		     u32 vflags, enum h2d_err *err)
{
	return h2d_decode_checked(buf, len, f, vflags, err);
}

int h2d_frame_import(const u8 *buf, size_t buf_len, struct h2d_frame *f,
		     enum h2d_err *err)
{
	u16 total;

	if (buf_len < H2D_HDR_LEN) {
		if (err)
			*err = H2D_ESHORT;
		return -1;
	}
	total = h2d_get16(buf + H2D_LEN_OFF);
	if (total < H2D_HDR_LEN || total > H2D_MAX_TOTAL ||
	    total > H2D_MAX_TOTAL_CLASS || (size_t)total > buf_len) {
		if (err)
			*err = (total == 0) ? H2D_ELEN : H2D_EBOUNDS;
		return -1;
	}
	if (h2d_decode_checked(buf, (size_t)total, f,
			       H2D_VF_TRANSPORT, err) != 0)
		return -1;
	f->policy = H2D_F_CAPTURED;
	{
		enum h2d_err e = h2d_check_semantics(
			f, H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
			   H2D_VF_HANDLER | H2D_VF_POLICY);

		if (e != H2D_OK) {
			if (err)
				*err = e;
			return -1;
		}
	}
	if (err)
		*err = H2D_OK;
	return 0;
}

int h2d_frame_export(const struct h2d_frame *f, u8 *dst, size_t cap,
		     size_t pad_to, u8 pad_byte, size_t *out_len,
		     enum h2d_err *err)
{
	size_t total = (size_t)f->payload_len + H2D_HDR_LEN;
	size_t want = total;

	if (pad_to) {
		if (pad_to < total) {
			if (err)
				*err = H2D_EARG;
			return -1;
		}
		want = pad_to;
	}
	if (cap < want) {
		if (err)
			*err = H2D_EBUFFER;
		return -1;
	}
	if (h2d_frame_encode(f, dst, cap, &total, err) != 0)
		return -1;
	if (want > total)
		memset(dst + total, pad_byte, want - total);
	if (out_len)
		*out_len = want;
	return 0;
}

/* ------------------------------------------------------------------ */
/* exact builders                                                      */
/* ------------------------------------------------------------------ */
int h2d_build_safe_probe(struct h2d_frame *f, u8 dst)
{
	h2d_frame_zero(f);
	f->group = H2D_GROUP_CHIP;
	f->id = 0u;
	f->dst = dst;
	f->policy = H2D_F_EXACT;
	return (h2d_check_semantics(f, H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
				       H2D_VF_HANDLER | H2D_VF_POLICY) == H2D_OK)
		       ? 0 : -1;
}

int h2d_build_g0_cfg_event(struct h2d_frame *f, u8 id, u8 dst, u8 fa, u8 fb,
			   u8 fc, const u8 token[8], u16 word8,
			   const u8 *data, u16 len, const u8 tail[4],
			   enum h2d_err *err)
{
	u32 total = (u32)len + 0x10u + H2D_HDR_LEN;
	enum h2d_err e;

	if (id != 1u && id != 2u) {
		if (err)
			*err = H2D_EARG;
		return -1;
	}
	if (total > H2D_MAX_TOTAL) {
		if (err)
			*err = H2D_EBOUNDS;
		return -1;
	}
	h2d_frame_zero(f);
	f->group = H2D_GROUP_CHIP;
	f->id = id;
	f->dst = dst;
	f->fa = fa;
	f->fb = fb;
	f->fc = fc;
	f->payload_len = (u16)(0x10u + len);
	if (token)
		memcpy(f->payload, token, 8u);
	h2d_put16(f->payload + 8u, word8);
	h2d_put16(f->payload + 10u, len);
	if (len && data)
		memcpy(f->payload + 12u, data, (size_t)len);
	if (tail)
		memcpy(f->payload + 12u + len, tail, 4u);
	f->policy = H2D_F_EXACT;
	e = h2d_check_semantics(f, H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
				  H2D_VF_HANDLER | H2D_VF_POLICY);
	if (e != H2D_OK) {
		if (err)
			*err = e;
		return -1;
	}
	if (err)
		*err = H2D_OK;
	return 0;
}

int h2d_build_g0_id1_hdpp_txq_sync(struct h2d_frame *f, u8 dst, u8 fa, u8 fb,
				   const u8 token[8], u16 cmd,
				   const u8 *data, u16 len,
				   const u8 tail[4], enum h2d_err *err)
{
	return h2d_build_g0_cfg_event(f, 1u, dst, fa, fb, 0u, token, cmd,
				      data, len, tail, err);
}

int h2d_build_g0_id1d_edca_opt(struct h2d_frame *f, u8 dst, u8 fa, u8 fb,
			       u8 fc, enum h2d_err *err)
{
	enum h2d_err e;

	h2d_frame_zero(f);
	f->group = H2D_GROUP_CHIP;
	f->id = 0x1du;
	f->dst = dst;
	f->fa = fa;
	f->fb = fb;
	f->fc = fc;
	f->payload_len = 0x3cu;
	f->policy = H2D_F_EXACT;
	e = h2d_check_semantics(f, H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
				  H2D_VF_HANDLER | H2D_VF_POLICY);
	if (e != H2D_OK) {
		if (err)
			*err = e;
		return -1;
	}
	if (err)
		*err = H2D_OK;
	return 0;
}

int h2d_build_g0_id09_del_user(struct h2d_frame *f, u8 dst, u16 user_id,
			       const u8 addr[4], enum h2d_err *err)
{
	enum h2d_err e;

	if (user_id == 0x0fffu && addr == NULL) {
		if (err)
			*err = H2D_EARG;
		return -1;
	}
	if (user_id != 0x0fffu && addr != NULL) {
		if (err)
			*err = H2D_EARG;
		return -1;
	}
	h2d_frame_zero(f);
	f->group = H2D_GROUP_CHIP;
	f->id = 0x09u;
	f->dst = dst;
	f->payload_len = 0x0cu;
	h2d_put16(f->payload, user_id);
	if (addr)
		memcpy(f->payload + 2u, addr, 4u);
	f->policy = H2D_F_EXACT;
	e = h2d_check_semantics(f, H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
				  H2D_VF_HANDLER | H2D_VF_POLICY);
	if (e != H2D_OK) {
		if (err)
			*err = e;
		return -1;
	}
	if (err)
		*err = H2D_OK;
	return 0;
}

int h2d_build_raw(struct h2d_frame *f, u8 group, u16 id, u8 dst, u8 fa,
		  u8 fb, u8 fc, const u8 *payload, u16 payload_len,
		  u32 policy, enum h2d_err *err)
{
	enum h2d_err e;

	if (payload_len > H2D_MAX_PAYLOAD || (payload_len && !payload)) {
		if (err)
			*err = H2D_EBOUNDS;
		return -1;
	}
	h2d_frame_zero(f);
	f->group = group;
	f->id = id;
	f->dst = dst;
	f->fa = fa;
	f->fb = fb;
	f->fc = fc;
	f->payload_len = payload_len;
	f->policy = policy;
	if (payload_len)
		memcpy(f->payload, payload, (size_t)payload_len);
	e = h2d_check_semantics(f, H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
				  H2D_VF_HANDLER | H2D_VF_POLICY);
	if (e != H2D_OK) {
		if (err)
			*err = e;
		return -1;
	}
	if (err)
		*err = H2D_OK;
	return 0;
}

/* ------------------------------------------------------------------ */
/* SR node / packed producer / plans                                   */
/* ------------------------------------------------------------------ */
int h2d_sr_node_encode(u8 node[H2D_NODE_SIZE], u32 devva, u32 frame_total,
		       enum h2d_err *err)
{
	if (frame_total < H2D_HDR_LEN || frame_total > H2D_MAX_TOTAL ||
	    frame_total > 0xffffu) {
		if (err)
			*err = H2D_EBOUNDS;
		return -1;
	}
	h2d_put32(node, devva);
	h2d_put32(node + 4u, ((frame_total & 0xffffu) << H2D_NODE_LEN_SHIFT) |
			     H2D_NODE_FLAGS);
	if (err)
		*err = H2D_OK;
	return 0;
}

int h2d_sr_node_decode(const u8 node[H2D_NODE_SIZE], u32 *devva, u32 *len,
		       u32 *flags, enum h2d_err *err)
{
	u32 w1 = h2d_get32(node + 4u);
	u32 nlen = w1 >> H2D_NODE_LEN_SHIFT;
	u32 nflags = w1 & 0xffffu;

	if (nflags != H2D_NODE_FLAGS) {
		if (err)
			*err = H2D_EWIRE;
		return -1;
	}
	if (nlen < H2D_HDR_LEN || nlen > H2D_MAX_TOTAL) {
		if (err)
			*err = H2D_EBOUNDS;
		return -1;
	}
	if (devva)
		*devva = h2d_get32(node);
	if (len)
		*len = nlen;
	if (flags)
		*flags = nflags;
	if (err)
		*err = H2D_OK;
	return 0;
}

u32 h2d_idx_plus(u32 idx, u32 depth)
{
	u32 next = idx + 1u;

	if (depth == 0u || depth > H2D_RING_MAX_DEPTH)
		return idx;
	if ((next & H2D_IDX_LOW_MASK) == depth) {
		next &= ~H2D_IDX_LOW_MASK;
		next ^= H2D_IDX_PHASE;
	}
	return next;
}

u32 h2d_idx_count(u32 from, u32 to, u32 depth)
{
	s32 phase, delta;

	if (depth == 0u || depth > H2D_RING_MAX_DEPTH)
		return 0;
	phase = (s32)((to >> 10) & 1u) - (s32)((from >> 10) & 1u);
	delta = phase * (s32)depth +
		(s32)(to & H2D_IDX_LOW_MASK) -
		(s32)(from & H2D_IDX_LOW_MASK);
	if (delta < 0)
		delta += 2 * (s32)depth;
	return (u32)delta;
}

int h2d_sr_plan(const struct h2d_frame *f, u32 producer, u32 depth,
		u32 buffer_devva, struct h2d_sr_plan *p, enum h2d_err *err)
{
	u32 total = (u32)f->payload_len + H2D_HDR_LEN;
	enum h2d_err e;
	u8 node[H2D_NODE_SIZE];

	if (depth == 0u || depth > H2D_RING_MAX_DEPTH) {
		if (err)
			*err = H2D_EARG;
		return -1;
	}
	if ((producer & ~(H2D_IDX_LOW_MASK | H2D_IDX_PHASE)) != 0u) {
		if (err)
			*err = H2D_EARG;
		return -1;
	}
	e = h2d_check_semantics(f, H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
				  H2D_VF_HANDLER | H2D_VF_POLICY);
	if (e != H2D_OK) {
		if (err)
			*err = e;
		return -1;
	}
	if (h2d_sr_node_encode(node, buffer_devva, total, err) != 0)
		return -1;

	p->node_index = producer & H2D_IDX_LOW_MASK;
	p->buffer_devva = buffer_devva;
	p->node_word0 = h2d_get32(node);
	p->node_word1 = h2d_get32(node + 4u);
	p->producer_before = producer;
	p->producer_after = h2d_idx_plus(producer, depth);
	p->frame_total = (u16)total;
	if (err)
		*err = H2D_OK;
	return 0;
}

int h2d_mailbox_plan(u16 id, u32 out0_pending, u32 out2_doorbell,
		     struct h2d_mailbox_plan *p, enum h2d_err *err)
{
	if (id > H2D_MBOX_SEND_MAX_ID) {
		if (err)
			*err = H2D_EBOUNDS;
		return -1;
	}
	if (id == H2D_MBOX_ID_BUGON) {
		/* The firmware id-5 mailbox handler is a BUG() trap [V]. */
		if (err)
			*err = H2D_EPOLICY;
		return -1;
	}
	if (id != H2D_MBOX_ID_PM_SLEEP && id != H2D_MBOX_ID_ETE_ANNOUNCE) {
		/* 0/2/4/8/9/10 have no vendor H2D post; 6/7 are service-only. */
		if (err)
			*err = H2D_EPOLICY;
		return -1;
	}
	if (out0_pending != 0u) {
		/* pcie_msg_send only writes out[0] when its coalesced shadow is
		 * empty; the device must have consumed the previous word. */
		if (err)
			*err = H2D_ESTATE;
		return -1;
	}
	p->id = id;
	p->pending_ca = H2D_CA_MBOX_PENDING;
	p->pending_write = H2D_MBOX_BIT(id);
	p->doorbell_ca = H2D_CA_MBOX_DOORBELL;
	p->doorbell_write = out2_doorbell | 1u;
	if (err)
		*err = H2D_OK;
	return 0;
}

int h2d_plan_post(const struct h2d_frame *f, u32 producer, u32 depth,
		  u32 buffer_devva, u32 out0_pending, u32 out2_doorbell,
		  struct h2d_post_plan *p, enum h2d_err *err)
{
	struct h2d_sr_plan sr;
	enum h2d_err e;

	e = h2d_sr_plan(f, producer, depth, buffer_devva, &sr, err);
	if (e != H2D_OK)
		return -1;
	e = h2d_mailbox_plan(H2D_MBOX_ID_ETE_ANNOUNCE, out0_pending,
			     out2_doorbell, &p->mb, err);
	if (e != H2D_OK)
		return -1;	/* err already set by h2d_mailbox_plan() */
	p->sr = sr;
	p->frame_total = sr.frame_total;
	if (err)
		*err = H2D_OK;
	return 0;
}

/* ------------------------------------------------------------------ */
/* device->host ready-frame validator                                  */
/* ------------------------------------------------------------------ */
int h2d_ready_decode(const u8 *buf, size_t len, struct h2d_ready *r,
		     enum h2d_err *err)
{
	u16 total, id;
	u8 payload_len;

	if (len < H2D_HDR_LEN) {
		if (err)
			*err = H2D_ESHORT;
		return -1;
	}
	if (h2d_get16(buf + H2D_TAG_OFF) != H2D_TAG) {
		if (err)
			*err = H2D_ETAG;
		return -1;
	}
	total = h2d_get16(buf + H2D_LEN_OFF);
	if (total < H2D_HDR_LEN || (size_t)total != len) {
		if (err)
			*err = H2D_ELEN;
		return -1;
	}
	if ((buf[H2D_GROUP_OFF] & 0x0fu) != H2D_GROUP_D2H_PLAT) {
		if (err)
			*err = H2D_EGROUP;
		return -1;
	}
	id = h2d_get16(buf + H2D_ID_OFF);
	payload_len = (u8)(total - H2D_HDR_LEN);
	if (id == 1u) {
		if (payload_len != 0u) {
			if (err)
				*err = H2D_ELEN;
			return -1;
		}
	} else if (id == 2u) {
		if (payload_len < 4u || payload_len > H2D_READY_MAX_PAYLOAD) {
			if (err)
				*err = H2D_ELEN;
			return -1;
		}
	} else {
		if (err)
			*err = H2D_EID;
		return -1;
	}
	memset(r, 0, sizeof(*r));
	r->group = (u8)(buf[H2D_GROUP_OFF] & 0x0fu);
	r->id = (u8)id;
	r->core = buf[H2D_DST_OFF];
	r->total = total;
	r->payload_len = payload_len;
	r->payload = buf + H2D_HDR_LEN;
	if (id == 2u)
		r->payload_dword0 = h2d_get32(buf + H2D_HDR_LEN);
	if (err)
		*err = H2D_OK;
	return 0;
}
