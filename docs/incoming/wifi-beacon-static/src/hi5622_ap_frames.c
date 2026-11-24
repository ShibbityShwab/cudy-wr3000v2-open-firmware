// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_ap_frames.c - AP start frame builders (see the header for the
 * decoded field tables and the evidence markers).
 *
 * ORIGINAL clean-room code (wifi-ap-start lane, task-121).  The AP-config
 * body layout, the WAL command ids/body sizes and the static-dispatch keys
 * are derived facts recorded in ../REPORT.md / ../TABLES.md; this file only
 * synthesizes frames from them and refuses malformed input.
 */
#include "hi5622_ap_frames.h"

/* ------------------------------------------------------------------ */
/* small helpers                                                      */
/* ------------------------------------------------------------------ */
static void haf_put16(u8 *p, u16 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
}

static void haf_put32(u8 *p, u32 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
	p[2] = (u8)((v >> 16) & 0xffu);
	p[3] = (u8)((v >> 24) & 0xffu);
}

/* 12-byte HCC header (same field map as the inherited h2d_frames builder):
 *  [0] group, [1] dst<<4 | alloc, [2] fa, [3] fb, [4..5] total,
 *  [6..7] id, [8] fc, [9] dst, [10..11] tag 0x5a5a. */
static void haf_hcc_hdr(u8 *buf, u8 group, u16 id, size_t total, u8 fb,
			u8 fc, u8 dst)
{
	buf[0] = (u8)(group & 0x0fu);
	buf[1] = (u8)(HAF_ALLOC_NIBBLE | ((dst & 0x0fu) << 4));
	buf[2] = 0u;	/* fa: not used by the AP families [V*] */
	buf[3] = fb;
	haf_put16(buf + 4, (u16)total);
	haf_put16(buf + 6, id);
	buf[8] = fc;
	buf[9] = (u8)(dst & 0x0fu);
	haf_put16(buf + 10, HAF_HCC_TAG);
}

/* Find one 802.11 information element by element id; returns the index of
 * the element header (id byte) or -1.  Mirrors the vendor mac_find_ie walk
 * (bounds: a length byte may not run past the buffer). */
static int haf_find_ie(u8 eid, const u8 *ies, size_t len)
{
	size_t i = 0;

	while (i + 2u <= len) {
		size_t eid_len = ies[i + 1u];

		if (ies[i] == eid)
			return (i + 2u + eid_len <= len) ? (int)i : -1;
		i += 2u + eid_len;
	}
	return -1;
}

/* Vendor-specific IE lookup: element 0xDD with OUI 00:50:f2.  The vendor
 * helper is mac_find_vendor_ie(0x50f2, type, ies, len); we accept any type
 * and let the caller copy the whole element (the firmware only checks the
 * element id at body+0x49 [V]). */
static int haf_find_vendor_ie(const u8 *ies, size_t len)
{
	size_t i = 0;

	while (i + 2u <= len) {
		size_t eid_len = ies[i + 1u];

		if (ies[i] == HAF_IE_VENDOR && eid_len >= 4u &&
		    i + 2u + eid_len <= len &&
		    ies[i + 2u] == 0x00u && ies[i + 3u] == 0x50u &&
		    ies[i + 4u] == 0xf2u)
			return (int)i;
		i += 2u + eid_len;
	}
	return -1;
}

/* Copy one complete IE (id + len + payload) into a fixed 64-byte body slot,
 * exactly like the vendor helper: reject when len+2 exceeds the slot. */
static int haf_copy_ie(u8 *slot, size_t slot_len, const u8 *ies, size_t len,
		       int ie_idx)
{
	size_t ielen;

	if (ie_idx < 0)
		return 0;	/* absent: the slot stays zero */
	ielen = (size_t)ies[ie_idx + 1u] + 2u;
	if (ielen > slot_len || (size_t)ie_idx + ielen > len)
		return -EOPNOTSUPP;
	memcpy(slot, ies + ie_idx, ielen);
	return 1;
}

/* ------------------------------------------------------------------ */
/* parameter validation                                               */
/* ------------------------------------------------------------------ */
static int haf_auth_ok(u8 auth)
{
	return auth == HAF_AUTH_OPEN || auth == HAF_AUTH_SHARED ||
	       auth == HAF_AUTH_SAE || auth == HAF_AUTH_AUTOMATIC;
}

int haf_ap_params_validate(const struct haf_ap_params *p)
{
	if (!p)
		return -EINVAL;
	if (p->carrier != HAF_CARRIER_G0_ID1 && p->carrier != HAF_CARRIER_WAL_G2)
		return -EINVAL;
	if (p->dst > 0x0fu)
		return -EINVAL;
	if (p->module > 0x12u)
		return -EINVAL;
	if (p->vap_id > 0x0fu)
		return -EINVAL;
	if (p->iftype != 2u && p->iftype != 3u)
		return -EINVAL;
	if (p->band > 1u)
		return -EINVAL;
	if (p->channel > 196u)
		return -EINVAL;
	if (p->hidden_ssid > 1u || p->start > 1u)
		return -EINVAL;
	if (!haf_auth_ok(p->auth_algs))
		return -EINVAL;
	if (p->ssid_len > HAF_MAX_SSID)
		return -EINVAL;
	if (p->ssid_len == 0u)
		return -EINVAL;
	if (p->head_len < 36u || p->head_len > HAF_MAX_HEAD)
		return -EINVAL;	/* cfg80211 beacon head >= 36-byte fixed part */
	if (p->tail_len > HAF_MAX_TAIL)
		return -EINVAL;
	if (p->beacon_interval == 0u || p->dtim_period == 0u)
		return -EINVAL;
	if (p->center_freq1 == 0u)
		return -EINVAL;
	return 0;
}

/* ------------------------------------------------------------------ */
/* 156-byte AP-config body                                            */
/* ------------------------------------------------------------------ */
int haf_build_body_ap_cfg(u8 *body, size_t cap,
			  const struct haf_ap_params *p)
{
	const u8 *tail;
	size_t tail_len;
	int rsn, wmm, ht, vht, ext, md;
	int rc, mode = 0;
	u16 md_id;
	u8 ht_b0, vht_b0, md_b2;

	if (!body || !p)
		return -EINVAL;
	if (cap < HAF_AP_CFG_BODY_LEN)
		return -ENOSPC;
	rc = haf_ap_params_validate(p);
	if (rc)
		return rc;

	memset(body, 0, HAF_AP_CFG_BODY_LEN);

	/* +0x00 / +0x04: the worker stores both u32 into VAP ctx +0x10/+0x14
	 * (firmware worker 0x84DA4) and the vendor debug log prints them as
	 * beacon_interval / dtim_period (.LC74) [V]. */
	haf_put32(body + 0x00, p->beacon_interval);
	haf_put32(body + 0x04, p->dtim_period);

	/* +0x89: hidden SSID (zero-length SSID element) [V]. */
	body[0x89] = p->hidden_ssid ? 1u : 0u;

	body[0x93] = p->start ? 1u : 0u;

	tail = p->tail;
	tail_len = p->tail_len;

	/* The vendor only copies the RSN/vendor IEs when the beacon capability
	 * privacy bit is set; body[8] then flags "both IE slots follow" [V].
	 * head layout: 0x22 = capability (802.11 fixed fields, 36 B) [V]. */
	if (p->head_len >= 36u && (p->head[0x22] & 0x10u) != 0u) {
		body[0x08] = 1u;
		rsn = haf_find_ie(HAF_IE_RSN, tail, tail_len);
		wmm = haf_find_vendor_ie(tail, tail_len);
		rc = haf_copy_ie(body + 0x09, HAF_IE_COPY_MAX, tail, tail_len,
				 rsn);
		if (rc < 0)
			return rc;
		rc = haf_copy_ie(body + 0x49, HAF_IE_COPY_MAX, tail, tail_len,
				 wmm);
		if (rc < 0)
			return rc;
	}

	/* HT capability IE (element 45): body 0x8a/0x8b carry payload byte 0
	 * bits 5/6 (vendor wal_parse_ht_vht_ie; firmware reads the byte) [V]. */
	ht = haf_find_ie(HAF_IE_HT_CAP, tail, tail_len);
	vht = haf_find_ie(HAF_IE_VHT_CAP, tail, tail_len);
	ext = haf_find_ie(HAF_IE_EXT, tail, tail_len);
	if (ht >= 0 && (size_t)ht + 3u <= tail_len) {
		ht_b0 = tail[(size_t)ht + 2u];
		body[0x8a] = (u8)((ht_b0 >> 5) & 1u);
		/* short-GI-20 bit is only copied for the non-2.4 band hint [V] */
		if (p->band != 0u)
			body[0x8b] = (u8)((ht_b0 >> 6) & 1u);
	}
	if (vht >= 0 && (size_t)vht + 3u <= tail_len) {
		vht_b0 = tail[(size_t)vht + 2u];
		/* 5 GHz band hint only (the vendor band gate clears both for
		 * band byte 0) [V] */
		if (p->band != 0u) {
			body[0x8c] = (u8)((vht_b0 >> 5) & 1u);
			body[0x8d] = (u8)((vht_b0 >> 6) & 1u);
		}
	}

	/* Wireless mode enum [V]:
	 *   extension element present -> 10 (HE),
	 *   VHT capability present    -> 6,
	 *   HT capability present     -> 5,
	 *   legacy otherwise: 0 (unknown), 2 (11g, 8 rates) or 1 (11b, 4 rates). */
	if (ext >= 0) {
		mode = 10;
	} else if (vht >= 0) {
		mode = 6;
	} else if (ht >= 0) {
		mode = 5;
	} else {
		int r1 = haf_find_ie(HAF_IE_RATES, p->head + 36u,
				     p->head_len >= 36u ? p->head_len - 36u : 0u);
		int r50 = haf_find_ie(50u, tail, tail_len);
		u32 n = 0;

		if (r1 >= 0)
			n += (u32)p->head[36u + (size_t)r1 + 1u];
		if (r50 >= 0)
			n += (u32)tail[(size_t)r50 + 1u];
		if (n == 4u)
			mode = 1;
		else if (n >= 8u)
			mode = 2;
	}
	body[0x8e] = (u8)mode;

	/* Mobility domain IE (0x36, len 3): MDID + capability bits.  Firmware
	 * packs these into VAP ctx +0x138/+0x13A [V]. */
	md = haf_find_ie(HAF_IE_MD, tail, tail_len);
	if (md >= 0) {
		if ((size_t)md + 5u > tail_len || tail[(size_t)md + 1u] != 3u)
			return -EOPNOTSUPP;
		md_id = (u16)(tail[(size_t)md + 2u] |
			      ((u16)tail[(size_t)md + 3u] << 8));
		md_b2 = tail[(size_t)md + 4u];
		haf_put16(body + 0x94, md_id);
		body[0x96] = 1u;
		body[0x97] = (u8)(md_b2 & 1u);
		body[0x98] = (u8)((md_b2 >> 1) & 1u);
	}

	return 0;
}

/* ------------------------------------------------------------------ */
/* other bodies                                                       */
/* ------------------------------------------------------------------ */
int haf_build_body_ssid(u8 *body, size_t cap, const struct haf_ap_params *p)
{
	if (!body || !p)
		return -EINVAL;
	if (cap < HAF_SSID_BODY_LEN)
		return -ENOSPC;
	if (p->ssid_len == 0u || p->ssid_len > HAF_MAX_SSID)
		return -EINVAL;
	memset(body, 0, HAF_SSID_BODY_LEN);
	body[0] = p->ssid_len;	/* vendor: u8 len, skip 2, ssid[33] [V] */
	memcpy(body + 3, p->ssid, p->ssid_len);
	return 0;
}

int haf_build_body_auth(u8 *body, size_t cap,
			const struct haf_ap_params *p)
{
	if (!body || !p)
		return -EINVAL;
	if (cap < HAF_AUTH_BODY_LEN)
		return -ENOSPC;
	if (!haf_auth_ok(p->auth_algs))
		return -EOPNOTSUPP;
	memset(body, 0, HAF_AUTH_BODY_LEN);
	haf_put32(body, (u32)p->auth_algs);
	return 0;
}

int haf_build_body_vap_start(u8 *body, size_t cap,
			     const struct haf_ap_params *p)
{
	if (!body || !p)
		return -EINVAL;
	if (cap < HAF_VAP_START_BODY_LEN)
		return -ENOSPC;
	memset(body, 0, HAF_VAP_START_BODY_LEN);
	body[0] = 1u;		/* vendor wal_start_vap body[0] = 1 [V] */
	/* body[8..11] = response cookie; the vendor passes a dev pointer and
	 * defaults are not written to the wire - zero is the bench default. */
	return 0;
}

/* ------------------------------------------------------------------ */
/* wire frames                                                        */
/* ------------------------------------------------------------------ */
static int haf_g0_frame(struct haf_frame *f, u16 key, const u8 *body,
			u16 body_len, const struct haf_ap_params *p)
{
	size_t total = (size_t)HAF_G0_OVERHEAD + body_len;
	u8 *b;

	if (!f || total > HAF_MAX_FRAME || total > sizeof(f->buf))
		return -ENOSPC;
	memset(f->buf, 0, sizeof(f->buf));
	b = f->buf;
	haf_hcc_hdr(b, HAF_HCC_GROUP_CHIP, HAF_HCC_ID_CFG_EVENT, total,
		    p->module, 0u, p->dst);
	/* token[8]: inherited g0 id1 token pattern from the bench-executed
	 * context-create frame; the AP workers do not read it [V]. */
	b[16] = 0x01u;
	haf_put16(b + 20, key);
	haf_put16(b + 22, body_len);
	if (body_len)
		memcpy(b + 24, body, body_len);
	memset(b + 24 + body_len, 0xff, 4);	/* inherited pad [V] */
	f->len = total;
	f->id = (u8)HAF_FRAME_COUNT;
	return 0;
}

static int haf_wal_frame(struct haf_frame *f, u16 cmd, const u8 *body,
			 u16 body_len, const struct haf_ap_params *p, u32 seq)
{
	size_t total = (size_t)HAF_WAL_OVERHEAD + body_len;
	u16 data_len = (u16)(4u + body_len);
	u8 *b;

	if (!f || total > HAF_MAX_FRAME || total > sizeof(f->buf))
		return -ENOSPC;
	memset(f->buf, 0, sizeof(f->buf));
	b = f->buf;
	haf_hcc_hdr(b, HAF_HCC_GROUP_CORE, 0u, total, 0u, 0u, p->dst);
	/* WAL envelope [V]: cookie, wal id 1, pad, data_len, seq, cmd, len */
	haf_put32(b + 12, 0u);		/* response cookie: default 0 */
	haf_put32(b + 16, HAF_WAL_MSG_CFG);
	haf_put16(b + 20, 0u);
	haf_put16(b + 22, data_len);
	haf_put32(b + 24, seq);
	haf_put16(b + 28, cmd);
	haf_put16(b + 30, body_len);
	if (body_len)
		memcpy(b + HAF_WAL_OVERHEAD, body, body_len);
	f->len = total;
	f->id = (u8)HAF_FRAME_COUNT;
	return 0;
}

int haf_sequence_at(unsigned int index, u8 carrier, enum haf_frame_id *out_id)
{
	if (!out_id)
		return -EINVAL;
	if (carrier == HAF_CARRIER_G0_ID1) {
		/* SSID first, then the AP config; there is no static-dispatch key
		 * for the auth command (firmware table A has no 0x006D row [V]),
		 * so the G0 carrier omits it and relies on the RSN IE. */
		if (index == 0u) {
			*out_id = HAF_FRAME_SSID;
			return 0;
		}
		if (index == 1u) {
			*out_id = HAF_FRAME_AP_CFG;
			return 0;
		}
		return -ENODATA;
	}
	if (carrier == HAF_CARRIER_WAL_G2) {
		if (index == 0u) {
			*out_id = HAF_FRAME_SSID;
			return 0;
		}
		if (index == 1u) {
			*out_id = HAF_FRAME_AUTH;
			return 0;
		}
		if (index == 2u) {
			*out_id = HAF_FRAME_AP_CFG;
			return 0;
		}
		if (index == 3u) {
			*out_id = HAF_FRAME_VAP_START;
			return 0;
		}
		return -ENODATA;
	}
	return -EINVAL;
}

/* 4-byte module-channel body (worker CA 0x841B8 [V]):
 *   [0] band (context +0x11), [1] vendor mac_vap+2 [I] (context +0x12),
 *   [2] channel number (context +0x10), [3] unused by the worker. */
static int haf_build_body_chan(u8 *body, size_t cap,
			     const struct haf_ap_params *p)
{
	if (!body || !p || cap < HAF_CHAN_BODY_LEN)
		return -ENOSPC;
	if (p->band > 1u || p->vap_id > 0x0fu || p->channel == 0u)
		return -EINVAL;
	body[0] = (u8)p->band;
	body[1] = p->vap_id;
	body[2] = p->channel;
	body[3] = 0u;
	return 0;
}

int haf_build_chan_frame(struct haf_frame *f, const struct haf_ap_params *p)
{
	u8 body[HAF_CHAN_BODY_LEN];
	int rc;

	if (!f || !p)
		return -EINVAL;
	if (p->carrier != HAF_CARRIER_G0_ID1 &&
	    p->carrier != HAF_CARRIER_WAL_G2)
		return -EINVAL;
	if (p->dst > 0x0fu || p->module > 0x12u)
		return -EINVAL;
	rc = haf_build_body_chan(body, sizeof(body), p);
	if (rc)
		return rc;
	if (p->carrier == HAF_CARRIER_G0_ID1)
		return haf_g0_frame(f, HAF_G0_KEY_CHAN, body,
				    HAF_CHAN_BODY_LEN, p);
	return haf_wal_frame(f, HAF_WAL_CMD_CHAN, body, HAF_CHAN_BODY_LEN,
			     p, 0u);
}

int haf_build_frame(struct haf_frame *f, enum haf_frame_id id,
		    const struct haf_ap_params *p)
{
	u8 body[HAF_AP_CFG_BODY_LEN];
	int rc;

	if (!f || !p)
		return -EINVAL;
	switch (id) {
	case HAF_FRAME_SSID:
		rc = haf_build_body_ssid(body, sizeof(body), p);
		if (rc)
			return rc;
		rc = (p->carrier == HAF_CARRIER_G0_ID1)
			     ? haf_g0_frame(f, HAF_G0_KEY_SSID, body,
					    HAF_SSID_BODY_LEN, p)
			     : haf_wal_frame(f, HAF_WAL_CMD_SSID, body,
					    HAF_SSID_BODY_LEN, p, 1u);
		break;
	case HAF_FRAME_AUTH:
		if (p->carrier != HAF_CARRIER_WAL_G2)
			return -EOPNOTSUPP;
		rc = haf_build_body_auth(body, sizeof(body), p);
		if (rc)
			return rc;
		rc = haf_wal_frame(f, HAF_WAL_CMD_AUTH, body, HAF_AUTH_BODY_LEN,
				   p, 2u);
		break;
	case HAF_FRAME_AP_CFG:
		rc = haf_build_body_ap_cfg(body, sizeof(body), p);
		if (rc)
			return rc;
		rc = (p->carrier == HAF_CARRIER_G0_ID1)
			     ? haf_g0_frame(f, HAF_G0_KEY_AP_CFG, body,
					    HAF_AP_CFG_BODY_LEN, p)
			     : haf_wal_frame(f, HAF_WAL_CMD_AP_CFG, body,
					    HAF_AP_CFG_BODY_LEN, p,
					    p->carrier == HAF_CARRIER_WAL_G2
						    ? 3u : 1u);
		break;
	case HAF_FRAME_VAP_START:
		if (p->carrier != HAF_CARRIER_WAL_G2)
			return -EOPNOTSUPP;
		rc = haf_build_body_vap_start(body, sizeof(body), p);
		if (rc)
			return rc;
		rc = haf_wal_frame(f, HAF_WAL_CMD_VAP_START, body,
				   HAF_VAP_START_BODY_LEN, p, 4u);
		break;
	default:
		return -EINVAL;
	}
	if (rc)
		return rc;
	f->id = (u8)id;
	return 0;
}

int haf_build_sequence(struct haf_frame *frames, unsigned int max,
		       const struct haf_ap_params *p, unsigned int *count)
{
	unsigned int n = 0;
	int rc;

	if (!frames || !p || !count)
		return -EINVAL;
	rc = haf_ap_params_validate(p);
	if (rc)
		return rc;
	for (;;) {
		enum haf_frame_id id;

		rc = haf_sequence_at(n, p->carrier, &id);
		if (rc == -ENODATA)
			break;
		if (rc)
			return rc;
		if (n >= max)
			return -ENOSPC;
		rc = haf_build_frame(&frames[n], id, p);
		if (rc){
			return rc;
		}
		n++;
	}
	if (n == 0u)
		return -EINVAL;
	*count = n;
	return 0;
}

/* ------------------------------------------------------------------ */
/* names + structural validation                                      */
/* ------------------------------------------------------------------ */
const char *haf_frame_name(enum haf_frame_id id)
{
	switch (id) {
	case HAF_FRAME_SSID:		return "ap_ssid";
	case HAF_FRAME_AUTH:		return "ap_auth";
	case HAF_FRAME_AP_CFG:		return "ap_cfg";
	case HAF_FRAME_VAP_START:	return "ap_vap_start";
	default:			return "?";
	}
}

const char *haf_carrier_name(u8 carrier)
{
	switch (carrier) {
	case HAF_CARRIER_G0_ID1:	return "g0_id1_static";
	case HAF_CARRIER_WAL_G2:	return "wal_g2_vendor";
	default:			return "?";
	}
}

int haf_validate_wire(const u8 *wire, size_t len)
{
	u16 total, id, tag, key, cmd, body_len, data_len;

	if (!wire || len < HAF_G0_OVERHEAD || len > HAF_MAX_FRAME)
		return -EINVAL;
	total = (u16)(wire[4] | ((u16)wire[5] << 8));
	id = (u16)(wire[6] | ((u16)wire[7] << 8));
	tag = (u16)(wire[10] | ((u16)wire[11] << 8));
	if ((size_t)total != len || tag != HAF_HCC_TAG)
		return -EINVAL;
	if ((wire[1] & 0x0fu) != HAF_ALLOC_NIBBLE)
		return -EINVAL;
	if ((wire[0] & 0x0fu) == HAF_HCC_GROUP_CHIP) {
		if (id != HAF_HCC_ID_CFG_EVENT)
			return -EINVAL;
		if (len < HAF_G0_OVERHEAD || ((size_t)wire[1] >> 4) != 0u)
			return -EINVAL;
		key = (u16)(wire[20] | ((u16)wire[21] << 8));
		body_len = (u16)(wire[22] | ((u16)wire[23] << 8));
		if (key != HAF_G0_KEY_SSID && key != HAF_G0_KEY_AP_CFG)
			return -EINVAL;
		if ((size_t)HAF_G0_OVERHEAD + body_len != len)
			return -EINVAL;
		/* the inherited 0xff pad occupies the last 4 bytes [V] */
		if (wire[len - 1u] != 0xffu || wire[len - 4u] != 0xffu)
			return -EINVAL;
		if (key == HAF_G0_KEY_SSID && body_len != HAF_SSID_BODY_LEN)
			return -EINVAL;
		if (key == HAF_G0_KEY_AP_CFG &&
		    body_len != HAF_AP_CFG_BODY_LEN)
			return -EINVAL;
		return 0;
	}
	if ((wire[0] & 0x0fu) == HAF_HCC_GROUP_CORE) {
		if (id != 0u)
			return -EINVAL;
		if (len < HAF_WAL_OVERHEAD)
			return -EINVAL;
		if ((u32)(wire[16] | ((u32)wire[17] << 8) |
			  ((u32)wire[18] << 16) | ((u32)wire[19] << 24)) !=
		    HAF_WAL_MSG_CFG)
			return -EINVAL;
		data_len = (u16)(wire[22] | ((u16)wire[23] << 8));
		cmd = (u16)(wire[28] | ((u16)wire[29] << 8));
		body_len = (u16)(wire[30] | ((u16)wire[31] << 8));
		if ((size_t)HAF_WAL_OVERHEAD + body_len != len)
			return -EINVAL;
		if ((u16)(4u + body_len) != data_len)
			return -EINVAL;
		switch (cmd) {
		case HAF_WAL_CMD_SSID:
			return body_len == HAF_SSID_BODY_LEN ? 0 : -EINVAL;
		case HAF_WAL_CMD_AUTH:
			return body_len == HAF_AUTH_BODY_LEN ? 0 : -EINVAL;
		case HAF_WAL_CMD_AP_CFG:
			return body_len == HAF_AP_CFG_BODY_LEN ? 0 : -EINVAL;
		case HAF_WAL_CMD_VAP_START:
			return body_len == HAF_VAP_START_BODY_LEN ? 0 : -EINVAL;
		default:
			return -EINVAL;
		}
	}
	return -EINVAL;
}
