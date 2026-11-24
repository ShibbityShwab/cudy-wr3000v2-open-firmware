/* SPDX-License-Identifier: GPL-2.0 */
/*
 * vapscan_frames.c - HCC user/VAP-add (g0 id7/id8), user-del (g0 id9) and
 * scan-request (g0 id0xC) frame builders for the Hi5622V100 clean-room host.
 *
 * ORIGINAL clean-room code (lane wifi-vap-scan, task-110, 2026-11-25).
 * No vendor source, object code, firmware byte or secret is reproduced; the
 * constants are derived protocol values with the evidence markers documented in
 * REPORT.md and in vapscan_frames.h.
 *
 * Layout reminder (HCC header, shared with ../wifi-h2d-frames):
 *   hdr[0]    group (low nibble; 0 = chip)
 *   hdr[1]    alloc state (low nibble = 1) | destination nibble << 4
 *   hdr[2]    per-message field A  (user frames: vap id [I])
 *   hdr[3]    per-message field B  (user/scan frames: chip id [I])
 *   hdr[4..5] u16 total length = payload + 12
 *   hdr[6..7] u16 message id
 *   hdr[8]    per-message field C (0)
 *   hdr[9]    destination nibble = hdr[1] >> 4
 *   hdr[10..11] u16 transport tag 0x5a5a
 */
#include "vapscan_frames.h"

/* ------------------------------------------------------------------ */
/* small helpers                                                      */
/* ------------------------------------------------------------------ */
static void vs_put_le16(u8 *p, u16 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
}

static u16 vs_get_le16(const u8 *p)
{
	return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static int vs_valid_dst(u8 dst)
{
	return dst <= 0x0fu;
}

/* Build the shared 12-byte HCC header + zeroed payload. */
static int vs_frame_open(struct vs_frame *f, u8 id, u8 chip_id, u8 vap_id,
			 u8 dst, u16 payload_len)
{
	u8 *h;

	if (!f)
		return VS_EARG;
	if (!vs_valid_dst(dst))
		return VS_EDST;
	if ((u32)VS_HDR_LEN + payload_len > VS_MAX_FRAME)
		return VS_EBOUNDS;
	memset(f->buf, 0, sizeof(f->buf));
	f->len = (u16)(VS_HDR_LEN + payload_len);
	h = f->buf;
	h[0] = VS_GROUP_CHIP;
	h[1] = (u8)(VS_ALLOC_NIBBLE | (u8)((dst & 0x0fu) << 4));
	h[2] = vap_id;
	h[3] = chip_id;
	vs_put_le16(h + 4, f->len);
	vs_put_le16(h + 6, id);
	h[8] = 0;
	h[9] = (u8)(dst & 0x0fu);
	vs_put_le16(h + 10, VS_TAG);
	return VS_OK;
}

/* Header checks shared by the validator and decode. */
static int vs_header_check(const u8 *buf, size_t len, u16 *total, u8 *id)
{
	if (!buf || len < VS_HDR_LEN)
		return VS_ESHORT;
	if (vs_get_le16(buf + 10) != VS_TAG)
		return VS_ETAG;
	*total = vs_get_le16(buf + 4);
	if (*total == 0 || *total != len)
		return VS_ELEN;
	if ((buf[1] & 0x0fu) != VS_ALLOC_NIBBLE)
		return VS_EALLOC;
	if (buf[9] != (u8)(buf[1] >> 4))
		return VS_EDST;
	if ((buf[0] & 0x0fu) != VS_GROUP_CHIP)
		return VS_EGROUP;	/* no H2D table for other groups [V] */
	*id = vs_get_le16(buf + 6);
	return VS_OK;
}

/* declared payload length for a supported family, or -1 [V] */
static int vs_family_payload_len(u8 family)
{
	switch (family) {
	case VS_ID_USER_ADD:	return (int)VS_LEN_USER_ADD;
	case VS_ID_USER_ALG:	return (int)VS_LEN_USER_ALG;
	case VS_ID_USER_DEL:	return (int)VS_LEN_USER_DEL;
	case VS_ID_SCAN_REQ:	return (int)VS_LEN_SCAN_REQ;
	default:		return -1;
	}
}

/* ------------------------------------------------------------------ */
/* defaults                                                           */
/* ------------------------------------------------------------------ */
void vs_user_add_defaults(struct vs_user_add *p)
{
	if (!p)
		return;
	memset(p, 0, sizeof(*p));
	p->type = 2;		/* vendor normal user type [V] */
	p->rssi = 100;		/* vendor default when no BSS match [V] */
}

void vs_user_alg_defaults(struct vs_user_alg *p)
{
	if (!p)
		return;
	memset(p, 0, sizeof(*p));
}

void vs_scan_defaults(struct vs_scan_params *p)
{
	u8 i;

	if (!p)
		return;
	memset(p, 0, sizeof(*p));
	p->flags0 = 1;			/* vendor STA scan builder [V] */
	p->flags1 = 0;			/* single-band scan [I] */
	p->has_bssid = 1;		/* broadcast BSSID present [V] */
	for (i = 0; i < 6; i++)
		p->bssid[i] = 0xffu;	/* get_bcast_macaddr() [V] */
	p->type_b = 2;			/* active scan variant [V]/[I] */
	p->dwell_ms = 60;		/* vendor STA scan dwell [V] */
	p->scan_type = 0;		/* vendor builder leaves 0 [V] */
}

/* ------------------------------------------------------------------ */
/* builders                                                           */
/* ------------------------------------------------------------------ */
int vs_build_user_add(struct vs_frame *f, const struct vs_user_add *p)
{
	u8 *pl;
	int rc;

	if (!p || p->user_id >= 0x8000u)	/* firmware id space is < 0x8000 [V] */
		return VS_EARG;
	rc = vs_frame_open(f, VS_ID_USER_ADD, p->chip_id, p->vap_id, p->dst,
			   VS_LEN_USER_ADD);
	if (rc != VS_OK)
		return rc;
	pl = f->buf + VS_HDR_LEN;
	vs_put_le16(pl + VS_UA_OFF_UID, p->user_id);
	memcpy(pl + VS_UA_OFF_MAC, p->mac, 6);
	pl[VS_UA_OFF_TYPE] = p->type;
	pl[VS_UA_OFF_RSSI] = (u8)(s8)p->rssi;
	return VS_OK;
}

int vs_build_user_alg(struct vs_frame *f, const struct vs_user_alg *p)
{
	u8 *pl;
	int rc;

	if (!p || p->user_id >= 0x8000u)
		return VS_EARG;
	rc = vs_frame_open(f, VS_ID_USER_ALG, p->chip_id, p->vap_id, p->dst,
			   VS_LEN_USER_ALG);
	if (rc != VS_OK)
		return rc;
	pl = f->buf + VS_HDR_LEN;
	vs_put_le16(pl + VS_ALG_OFF_UID, p->user_id);
	vs_put_le16(pl + VS_ALG_OFF_ALG, p->alg);
	if (p->vht)
		memcpy(pl + VS_ALG_OFF_VHT, p->vht, VS_ALG_LEN_VHT);
	if (p->ht)
		memcpy(pl + VS_ALG_OFF_HT, p->ht, VS_ALG_LEN_HT);
	if (p->he)
		memcpy(pl + VS_ALG_OFF_HE, p->he, VS_ALG_LEN_HE);
	pl[VS_ALG_OFF_NSS] = p->nss;
	return VS_OK;
}

int vs_build_user_del(struct vs_frame *f, u16 user_id, const u8 mac[6],
		      u8 chip_id, u8 vap_id, u8 flag)
{
	u8 *pl;

	if (!mac)
		return VS_EARG;
	if (user_id != VS_UID_BY_MAC && user_id >= 0x8000u)
		return VS_EARG;
	if (vs_frame_open(f, VS_ID_USER_DEL, chip_id, vap_id, 0, VS_LEN_USER_DEL) != VS_OK)
		return VS_EARG;
	pl = f->buf + VS_HDR_LEN;
	vs_put_le16(pl + VS_UD_OFF_UID, user_id);
	memcpy(pl + VS_UD_OFF_MAC, mac, 6);
	pl[VS_UD_OFF_FLAG] = flag;
	return VS_OK;
}

int vs_build_scan_req(struct vs_frame *f, const struct vs_scan_params *p)
{
	u8 *pl;
	u8 i, j;
	int rc;

	if (!p)
		return VS_EARG;
	if (p->n_ssids > VS_SC_SSID_MAX || p->n_chans > VS_SC_CHAN_MAX)
		return VS_EBOUNDS;
	if (p->scan_type == 4u || p->scan_type == 8u)
		return VS_EPOLICY;	/* firmware handler rejects 4 and 8 [V] */
	for (i = 0; i < p->n_ssids; i++)
		if (p->ssids[i].len > VS_SC_SSID_LEN_MAX)
			return VS_EBOUNDS;
	for (i = 0; i < p->n_chans; i++)
		if (p->chans[i].band > 1u)
			return VS_EBOUNDS;	/* only two bands exist [V] */

	rc = vs_frame_open(f, VS_ID_SCAN_REQ, p->chip_id, p->vap_id, p->dst,
			   VS_LEN_SCAN_REQ);
	if (rc != VS_OK)
		return rc;
	pl = f->buf + VS_HDR_LEN;

	pl[VS_SC_OFF_FLAGS0] = p->flags0;
	pl[VS_SC_OFF_FLAGS1] = p->flags1;
	pl[VS_SC_OFF_HAS_BSSID] = p->has_bssid ? 1u : 0u;
	pl[VS_SC_OFF_N_SSIDS] = p->n_ssids;
	pl[VS_SC_OFF_TYPE_B] = p->type_b;
	memcpy(pl + VS_SC_OFF_BSSID, p->bssid, 6);

	/* SSID slots: bytes 0..len-1 = SSID, byte len = 0 (vendor builder) [V] */
	for (i = 0; i < p->n_ssids; i++) {
		u8 *slot = pl + VS_SC_OFF_SSID_AREA +
			   (u16)i * VS_SC_SSID_STRIDE;
		for (j = 0; j < p->ssids[i].len; j++)
			slot[j] = p->ssids[i].str[j];
		slot[p->ssids[i].len] = 0;
	}

	pl[VS_SC_OFF_COUNT_B] = p->n_ssids;

	/* channel entries: {u8 num; u8 band; u8 0; u8 idx} [V] */
	for (i = 0; i < p->n_chans; i++) {
		u8 *e = pl + VS_SC_OFF_CHAN_AREA +
			(u16)i * VS_SC_CHAN_STRIDE;
		e[0] = p->chans[i].num;
		e[1] = p->chans[i].band;
		e[2] = p->chans[i].pad;
		e[3] = p->chans[i].idx;
	}
	pl[VS_SC_OFF_CHAN_COUNT] = p->n_chans;
	vs_put_le16(pl + VS_SC_OFF_DWELL, p->dwell_ms);
	pl[VS_SC_OFF_SCAN_TYPE] = p->scan_type;
	pl[VS_SC_OFF_ONE] = 1;		/* vendor STA scan builder [V] */
	pl[VS_SC_OFF_TYPE_1F2] = 11;	/* vendor STA scan builder [V] */
	vs_put_le16(pl + VS_SC_OFF_TAIL32, 0);
	vs_put_le16(pl + VS_SC_OFF_TAIL32 + 2, 0);
	return VS_OK;
}

/* ------------------------------------------------------------------ */
/* validator / decode                                                 */
/* ------------------------------------------------------------------ */
int vs_frame_validate(const u8 *buf, size_t len)
{
	u16 total;
	u8 id;
	int rc, want;

	rc = vs_header_check(buf, len, &total, &id);
	if (rc != VS_OK)
		return rc;
	want = vs_family_payload_len(id);
	if (want < 0)
		return VS_EID;
	if ((int)total != (int)VS_HDR_LEN + want)
		return VS_ELEN;
	if (id == VS_ID_SCAN_REQ) {
		const u8 *pl = buf + VS_HDR_LEN;

		if (pl[VS_SC_OFF_SCAN_TYPE] == 4u ||
		    pl[VS_SC_OFF_SCAN_TYPE] == 8u)
			return VS_EPOLICY;
		if (pl[VS_SC_OFF_CHAN_COUNT] > VS_SC_CHAN_MAX)
			return VS_EBOUNDS;
		if (pl[VS_SC_OFF_N_SSIDS] > VS_SC_SSID_MAX)
			return VS_EBOUNDS;
	}
	if (id == VS_ID_USER_ADD || id == VS_ID_USER_ALG) {
		const u8 *pl = buf + VS_HDR_LEN;

		if (vs_get_le16(pl + VS_UA_OFF_UID) == VS_UID_BY_MAC)
			return VS_EPOLICY;	/* sentinel is del-only [V] */
	}
	return VS_OK;
}

int vs_frame_decode(const u8 *buf, size_t len, u8 *family, u8 *chip_id,
		    u8 *vap_id, u16 *payload_len)
{
	u16 total;
	u8 id;
	int rc, want;

	rc = vs_header_check(buf, len, &total, &id);
	if (rc != VS_OK)
		return rc;
	want = vs_family_payload_len(id);
	if (want < 0)
		return VS_EID;
	if (family)
		*family = id;
	if (chip_id)
		*chip_id = buf[3];
	if (vap_id)
		*vap_id = buf[2];
	if (payload_len)
		*payload_len = (u16)want;
	return VS_OK;
}

int vs_frame_export(const struct vs_frame *f, u8 *out, size_t cap,
		    size_t *out_len)
{
	if (!f || !out)
		return VS_EARG;
	if (f->len > cap)
		return VS_EBUFFER;
	memcpy(out, f->buf, f->len);
	if (out_len)
		*out_len = f->len;
	return VS_OK;
}

const char *vs_family_name(u8 family)
{
	switch (family) {
	case VS_ID_USER_ADD:	return "user_add";
	case VS_ID_USER_ALG:	return "user_add_notify_alg";
	case VS_ID_USER_DEL:	return "del_user";
	case VS_ID_SCAN_REQ:	return "scan_req";
	default:		return "unknown";
	}
}

const char *vs_strerror(int err)
{
	switch (err) {
	case VS_OK:		return "ok";
	case VS_EARG:		return "bad argument";
	case VS_ESHORT:		return "frame shorter than header";
	case VS_ETAG:		return "transport tag != 0x5a5a";
	case VS_ELEN:		return "declared total length inconsistent";
	case VS_EDST:		return "hdr[9] != hdr[1] >> 4";
	case VS_EALLOC:		return "alloc nibble != 1";
	case VS_EGROUP:		return "group has no firmware H2D table";
	case VS_EID:		return "unsupported message id";
	case VS_EBOUNDS:	return "field out of bounds";
	case VS_EBUFFER:	return "output buffer too small";
	case VS_EPOLICY:	return "policy refused";
	default:		return "unknown error";
	}
}

/* ------------------------------------------------------------------ */
/* carrier helpers                                                    */
/* ------------------------------------------------------------------ */
int vs_sr_node_encode(u8 node[VS_SR_NODE_LEN], u32 buf_devva, u16 total_len)
{
	if (!node || total_len == 0)
		return VS_EARG;
	node[0] = (u8)(buf_devva & 0xffu);
	node[1] = (u8)((buf_devva >> 8) & 0xffu);
	node[2] = (u8)((buf_devva >> 16) & 0xffu);
	node[3] = (u8)((buf_devva >> 24) & 0xffu);
	vs_put_le16(node + 4, total_len);
	vs_put_le16(node + 6, VS_SR_NODE_FLAGS);
	return VS_OK;
}

u32 vs_sr_producer_next(u32 producer, u32 depth)
{
	u32 idx, phase;

	if (depth == 0 || depth > (VS_SR_IDX_LOW_MASK + 1u))
		return producer;
	idx = producer & VS_SR_IDX_LOW_MASK;
	phase = producer & VS_SR_IDX_PHASE;
	idx++;
	if (idx >= depth) {
		idx = 0;
		phase ^= VS_SR_IDX_PHASE;
	}
	return (phase & VS_SR_IDX_PHASE) | (idx & VS_SR_IDX_LOW_MASK);
}
