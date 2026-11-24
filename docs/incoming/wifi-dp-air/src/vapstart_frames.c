// SPDX-License-Identifier: GPL-2.0
/*
 * vapstart_frames.c - byte-exact builders/validators for the Hi5622V100 VAP
 * start family.  Original clean-room code; see vapstart_frames.h for the
 * contract and ../REPORT.md for the per-byte derivation (firmware handler and
 * vendor-builder decode).  No vendor byte is reproduced.
 */
#include "vapstart_frames.h"

/* ------------------------------------------------------------------ */
/* little-endian field helpers                                         */
/* ------------------------------------------------------------------ */
static void vsf_put16(u8 *p, u16 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
}

static void vsf_put32(u8 *p, u32 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
	p[2] = (u8)((v >> 16) & 0xffu);
	p[3] = (u8)((v >> 24) & 0xffu);
}

static u16 vsf_get16(const u8 *p)
{
	return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static u32 vsf_get32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
	       ((u32)p[3] << 24);
}

static void vsf_zero(struct vsf_frame *f)
{
	memset(f->buf, 0, sizeof(f->buf));
	f->len = 0;
}

/* 12-byte HCC header.  alloc nibble is always 1 (hcc_msg_alloc), the dst
 * nibble lives in hdr[1] high and is mirrored to hdr[9] by the transport. */
static void vsf_hdr(struct vsf_frame *f, u8 group, u16 id, u16 total, u8 fa,
		   u8 fb, u8 fc, u8 dst)
{
	f->buf[0] = (u8)(group & 0x0fu);
	f->buf[1] = (u8)(0x01u | ((dst & 0x0fu) << 4));
	f->buf[2] = fa;
	f->buf[3] = fb;
	vsf_put16(f->buf + 4, total);
	vsf_put16(f->buf + 6, id);
	f->buf[8] = fc;
	f->buf[9] = (u8)(dst & 0x0fu);
	f->buf[10] = 0x5au;
	f->buf[11] = 0x5au;
}

/* ------------------------------------------------------------------ */
/* family A: g0 id1 config event (hdpp/hmac_config_send_event shape)   */
/*   hdr[12] | token[8] | key u16 | len u16 | data[len] | pad[4]      */
/*   total = 28 + len                                                  */
/* ------------------------------------------------------------------ */
static int vsf_build_cfg_event(struct vsf_frame *f, u16 key, const u8 *data,
			      u16 len, u8 fa, u8 fb, u8 fc, u8 dst)
{
	u16 total;

	if (!f || (len != 0u && data == NULL))
		return -EINVAL;
	if (len > (VSF_MAX_PAYLOAD - 16u))
		return -EINVAL;
	total = (u16)(28u + len);
	if (total > VSF_MAX_FRAME)
		return -EINVAL;

	vsf_zero(f);
	vsf_hdr(f, VSF_HCC_GROUP_CHIP, VSF_ID_CFG_EVENT, total, fa, fb, fc, dst);
	/* token[8]: the inherited capture value; the firmware does not parse
	 * token bytes 0..7 for the keys this lane sends [V]. */
	f->buf[12] = 0x00;
	f->buf[16] = 0x01;
	vsf_put16(f->buf + 20, key);
	vsf_put16(f->buf + 22, len);
	if (len != 0u)
		memcpy(f->buf + 24, data, len);
	memset(f->buf + 24 + len, 0xff, 4);	/* inherited pad, not parsed [V] */
	f->len = total;
	return 0;
}

void vsf_vap_add_defaults(struct vsf_vap_add *p)
{
	memset(p, 0, sizeof(*p));
	p->iftype = VSF_IFTYPE_AP;	/* vendor hipriv create type 2/3 [I] */
	p->parent = VSF_MODULE_MAX;	/* boot-task module 18 exists [V]; [I] */
	p->module = 0;			/* the first VAP: module/VAP 0 */
	p->band = 0;
	p->field_b = 0;			/* handler ctx lookup index, any 0..0x12 */
}

int vsf_build_vap_add(struct vsf_frame *f, const struct vsf_vap_add *p)
{
	u8 data[20];

	if (!f || !p)
		return -EINVAL;
	if (p->iftype != VSF_IFTYPE_MINIMAL && p->iftype != VSF_IFTYPE_AP &&
	    p->iftype != VSF_IFTYPE_STA)
		return -EINVAL;
	if (p->parent > VSF_MODULE_MAX || p->module > VSF_MODULE_MAX ||
	    p->field_b > VSF_MODULE_MAX)
		return -EINVAL;
	if (p->band > 1u || p->dst > 0x0fu)
		return -EINVAL;

	memset(data, 0, sizeof(data));
	data[0] = p->iftype;	/* create-helper type: 1/2/3 allocate [V] */
	data[1] = p->parent;	/* parent module lookup gate [V] */
	data[4] = p->module;	/* module/VAP index that gets created [V] */
	data[0x12] = p->band;	/* band selector [I] */
	return vsf_build_cfg_event(f, VSF_KEY_VAP_ADD, data, sizeof(data),
				  p->field_a, p->field_b, p->field_c, p->dst);
}

void vsf_vap_start_defaults(struct vsf_vap_start *p)
{
	memset(p, 0, sizeof(*p));
	p->state = 1;		/* vap+0x0f state byte: started/up [I] */
}

int vsf_build_vap_start(struct vsf_frame *f, const struct vsf_vap_start *p)
{
	u8 data[12];

	if (!f || !p)
		return -EINVAL;
	if (p->field_b > VSF_MODULE_MAX || p->dst > 0x0fu)
		return -EINVAL;

	memset(data, 0, sizeof(data));
	data[0] = p->channel;	/* do_start_vap channel argument [V] */
	data[1] = p->state;	/* vap+0x0f [V] */
	data[2] = p->cfg_a;	/* vap+0x11 [V] */
	data[3] = p->cfg_b;	/* vap+0x12 [V] */
	vsf_put32(data + 4, p->cfg_word);	/* vap+0x414 [V] */
	return vsf_build_cfg_event(f, VSF_KEY_VAP_START, data, sizeof(data),
				  p->field_a, p->field_b, p->field_c, p->dst);
}

/* ------------------------------------------------------------------ */
/* family B: g0 direct messages (hcc_msg_alloc(size) + explicit header) */
/* ------------------------------------------------------------------ */
static int vsf_build_direct(struct vsf_frame *f, u16 id, const u8 *payload,
			   u16 len, u8 fa, u8 fb, u8 fc, u8 dst)
{
	u16 total;

	if (!f || (len != 0u && payload == NULL))
		return -EINVAL;
	total = (u16)(VSF_HDR_LEN + len);
	if (total > VSF_MAX_FRAME)
		return -EINVAL;

	vsf_zero(f);
	vsf_hdr(f, VSF_HCC_GROUP_CHIP, id, total, fa, fb, fc, dst);
	if (len != 0u)
		memcpy(f->buf + VSF_HDR_LEN, payload, len);
	f->len = total;
	return 0;
}

void vsf_sta_join_start_defaults(struct vsf_sta_join_start *p)
{
	memset(p, 0, sizeof(*p));
	p->flag5 = 0;
	p->flag6 = 0;
	p->mac[0] = 0x02;	/* documentation MAC prefix (local example) */
}

int vsf_build_sta_join_start(struct vsf_frame *f, const struct vsf_sta_join_start *p)
{
	u8 pl[16];

	if (!f || !p || p->dst > 0x0fu)
		return -EINVAL;
	memset(pl, 0, sizeof(pl));
	pl[0] = p->flag5;	/* join +0x05 byte [V] */
	pl[4] = p->flag6;	/* join +0x06 byte [V] */
	memcpy(pl + 8, p->mac, VSF_MAC_LEN);	/* vap+0xF68 own MAC [V] */
	vsf_put16(pl + 14, 1);	/* vendor constant 1 [V] */
	return vsf_build_direct(f, VSF_ID_STA_JOIN_START, pl, sizeof(pl), 0,
			       p->dst, 0, p->dst);
}

void vsf_sta_join_params_defaults(struct vsf_sta_join_params *p)
{
	memset(p, 0, sizeof(*p));
	p->bssid[0] = 0x02;
}

int vsf_build_sta_join_params(struct vsf_frame *f,
			     const struct vsf_sta_join_params *p)
{
	u8 pl[64];

	if (!f || !p || p->dst > 0x0fu)
		return -EINVAL;
	memset(pl, 0, sizeof(pl));
	memcpy(pl, p->bssid, VSF_MAC_LEN);	/* join +0x36 BSSID [V] */
	vsf_put16(pl + 6, p->channel);		/* join +0x2c [V] */
	vsf_put32(pl + 8, p->band_info);		/* vap+0xFB8..FBB [V] */
	vsf_put32(pl + 16, 1);			/* vendor constant 1 [V] */
	memcpy(pl + 20, p->ie, VSF_STA_JOIN_IE_LEN);	/* assoc-req IE blob [V] */
	pl[52] = p->flag52;	/* vendor writes 0 [V] */
	pl[54] = p->flag54;	/* machine flag byte [V] */
	return vsf_build_direct(f, VSF_ID_STA_JOIN_PARAMS, pl, sizeof(pl), 0,
			       p->dst, 0, p->dst);
}

/* ------------------------------------------------------------------ */
/* family C: WAL group-2 cfg-event envelope                            */
/*   hdr[12] | cookie u32 | wal{id=1, pad, len, seq} | {cmd,len,body}  */
/*   total = 32 + body_len                                             */
/* ------------------------------------------------------------------ */
static int vsf_build_wal(struct vsf_frame *f, u16 cmd, const u8 *body,
			u16 body_len, u32 cookie, u32 seq, u8 fa, u8 fb,
			u8 fc, u8 dst)
{
	u16 data_len, total;

	if (!f)
		return -EINVAL;
	if (body_len != 0u && body == NULL)
		return -EINVAL;
	if (body_len > (VSF_MAX_PAYLOAD - 36u))
		return -EINVAL;
	data_len = (u16)(4u + body_len);
	total = (u16)(32u + body_len);
	if (total > VSF_MAX_FRAME)
		return -EINVAL;

	vsf_zero(f);
	vsf_hdr(f, VSF_HCC_GROUP_CORE, 0, total, fa, fb, fc, dst);
	vsf_put32(f->buf + 12, cookie);		/* response-matching cookie [V] */
	vsf_put32(f->buf + 16, VSF_WAL_MSG_CFG);	/* WAL cfg-event id 1 [V] */
	vsf_put16(f->buf + 20, 0);
	vsf_put16(f->buf + 22, data_len);	/* data length (hdr + body) [V] */
	vsf_put32(f->buf + 24, seq);
	vsf_put16(f->buf + 28, cmd);		/* WAL command [V] */
	vsf_put16(f->buf + 30, body_len);	/* body length [V] */
	if (body_len != 0u)
		memcpy(f->buf + 32, body, body_len);
	f->len = total;
	return 0;
}

void vsf_wal_vap_add_defaults(struct vsf_wal_vap_add *p)
{
	memset(p, 0, sizeof(*p));
	p->iftype = VSF_IFTYPE_AP;
	p->field_b = 0;
	p->seq = 1;
}

int vsf_build_wal_vap_add(struct vsf_frame *f, const struct vsf_wal_vap_add *p)
{
	u8 body[24];

	if (!f || !p)
		return -EINVAL;
	if (p->iftype != VSF_IFTYPE_AP && p->iftype != VSF_IFTYPE_STA)
		return -EINVAL;
	if (p->dst > 0x0fu)
		return -EINVAL;
	memset(body, 0, sizeof(body));
	body[0] = p->iftype;	/* vap mode: 2 = AP, 3 = STA [V] */
	body[1] = p->sub_type;
	body[6] = 0;
	body[7] = p->flag7;
	body[12] = p->band;
	body[17] = p->band_type;
	body[18] = p->band_id;
	body[19] = p->sub19;
	vsf_put32(body + 20, p->cookie);
	return vsf_build_wal(f, VSF_WAL_CMD_VAP_ADD, body, sizeof(body),
			    p->cookie, p->seq, p->field_a, p->field_b,
			    p->field_c, p->dst);
}

void vsf_wal_vap_start_defaults(struct vsf_wal_vap_start *p)
{
	memset(p, 0, sizeof(*p));
	p->start_flag = 1;	/* vendor builder writes body[0] = 1 [V] */
	p->seq = 2;
}

int vsf_build_wal_vap_start(struct vsf_frame *f,
			   const struct vsf_wal_vap_start *p)
{
	u8 body[12];

	if (!f || !p || p->dst > 0x0fu)
		return -EINVAL;
	memset(body, 0, sizeof(body));
	body[0] = p->start_flag;
	vsf_put32(body + 8, p->cookie);
	return vsf_build_wal(f, VSF_WAL_CMD_VAP_START, body, sizeof(body),
			    p->cookie, p->seq, p->field_a, p->field_b,
			    p->field_c, p->dst);
}

/* AP-start config event: the envelope is decoded (cmd 0x127, 156-byte body),
 * the body is the vendor VAP/beacon config blob - still [U] (gap G1).  The
 * builder therefore REFUSES unless the caller supplies the exact body. */
int vsf_build_wal_ap_start_cfg(struct vsf_frame *f,
			      const struct vsf_wal_ap_start_cfg *p)
{
	if (!f || !p || !p->body)
		return -EOPNOTSUPP;
	if (p->dst > 0x0fu)
		return -EINVAL;
	return vsf_build_wal(f, VSF_WAL_CMD_AP_START, p->body, 156u, p->cookie,
			    p->seq, p->field_a, p->field_b, p->field_c, p->dst);
}

/* ------------------------------------------------------------------ */
/* validator / decoder                                                 */
/* ------------------------------------------------------------------ */
static int vsf_check_common(const struct vsf_frame *f, u16 *total)
{
	if (!f)
		return -EINVAL;
	if (f->len < VSF_HDR_LEN || f->len > VSF_MAX_FRAME)
		return -EINVAL;
	*total = vsf_get16(f->buf + 4);
	if (*total != f->len)
		return -EINVAL;	/* total must equal the wire length [V] */
	if (vsf_get16(f->buf + 10) != VSF_TAG)
		return -EINVAL;
	if ((f->buf[1] & 0x0fu) != 0x01u)
		return -EINVAL;	/* alloc state 1 [V] */
	if ((f->buf[0] & 0x0fu) != VSF_HCC_GROUP_CHIP &&
	    (f->buf[0] & 0x0fu) != VSF_HCC_GROUP_CORE)
		return -EINVAL;	/* only groups 0/2 have H2D tables [V] */
	if ((f->buf[9] & 0x0fu) != (f->buf[1] >> 4))
		return -EINVAL;
	return 0;
}

int vsf_frame_validate(const struct vsf_frame *f)
{
	struct vsf_info info;

	return vsf_frame_decode(f, &info);
}

int vsf_frame_decode(const struct vsf_frame *f, struct vsf_info *out)
{
	u16 total, key, len, data_len, cmd, body_len;
	u8 group, id;

	if (!out)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	if (vsf_check_common(f, &total))
		return -EINVAL;
	group = (u8)(f->buf[0] & 0x0fu);
	id = vsf_get16(f->buf + 6);
	out->group = group;
	out->id = id;
	out->total = total;

	if (group == VSF_HCC_GROUP_CHIP && id == VSF_ID_CFG_EVENT) {
		if (total < 32u)
			return -EINVAL;
		key = vsf_get16(f->buf + 20);
		len = vsf_get16(f->buf + 22);
		if ((u32)total != 28u + (u32)len)
			return -EINVAL;
		out->family = VSF_FAMILY_CFG_EVENT;
		out->key = key;
		/* known config-event keys this lane builds */
		if (key == VSF_KEY_VAP_ADD && len != 20u)
			return -EINVAL;
		if (key == VSF_KEY_VAP_START && len != 12u)
			return -EINVAL;
		if (key != VSF_KEY_VAP_ADD && key != VSF_KEY_VAP_START)
			return -EOPNOTSUPP;	/* validator stays family-strict */
		return 0;
	}

	if (group == VSF_HCC_GROUP_CHIP && id == VSF_ID_STA_JOIN_PARAMS) {
		if (total != 76u)
			return -EINVAL;
		out->family = VSF_FAMILY_DIRECT_G0;
		return 0;
	}
	if (group == VSF_HCC_GROUP_CHIP && id == VSF_ID_STA_JOIN_START) {
		if (total != 28u)
			return -EINVAL;
		out->family = VSF_FAMILY_DIRECT_G0;
		return 0;
	}

	if (group == VSF_HCC_GROUP_CORE) {
		if (total < 36u)
			return -EINVAL;
		if (vsf_get32(f->buf + 16) != VSF_WAL_MSG_CFG)
			return -EINVAL;
		data_len = vsf_get16(f->buf + 22);
		if ((u32)total != 28u + (u32)data_len)
			return -EINVAL;
		cmd = vsf_get16(f->buf + 28);
		body_len = vsf_get16(f->buf + 30);
		if ((u32)data_len != 4u + (u32)body_len)
			return -EINVAL;
		if ((u32)total != 32u + (u32)body_len)
			return -EINVAL;
		out->family = VSF_FAMILY_WAL_CFG;
		out->wal_cmd = cmd;
		out->wal_len = body_len;
		if (cmd == VSF_WAL_CMD_VAP_ADD && body_len != 24u)
			return -EINVAL;
		if (cmd == VSF_WAL_CMD_VAP_START && body_len != 12u)
			return -EINVAL;
		if (cmd == VSF_WAL_CMD_AP_START && body_len != 156u)
			return -EINVAL;
		if (cmd != VSF_WAL_CMD_VAP_ADD && cmd != VSF_WAL_CMD_VAP_START &&
		    cmd != VSF_WAL_CMD_AP_START)
			return -EOPNOTSUPP;
		return 0;
	}

	return -EOPNOTSUPP;
}

int vsf_frame_export(const struct vsf_frame *f, u8 *dst, size_t cap,
		    size_t *out_len)
{
	if (!f || !dst || !out_len)
		return -EINVAL;
	if (f->len == 0u || f->len > cap)
		return -ENOSPC;
	memcpy(dst, f->buf, f->len);
	*out_len = f->len;
	return 0;
}
