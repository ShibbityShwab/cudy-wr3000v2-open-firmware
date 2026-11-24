// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_sta_ops_logic.c - portable station-operations logic (clean-room).
 * See hi5622_sta_ops_logic.h for the decoded facts and their evidence.
 *
 * Pure C99, device-free: the 6.18 kernel module and tools/sta_ops_selftest.c
 * compile the same code.  No vendor source/object/firmware byte reproduced.
 */
#include "hi5622_sta_ops_logic.h"

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

/* ------------------------------------------------------------------ */
/* little-endian helpers                                              */
/* ------------------------------------------------------------------ */
static void hsop_put16(u8 *p, u16 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
}

static u16 hsop_get16(const u8 *p)
{
	return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static u16 hsop_getbe16(const u8 *p)
{
	return (u16)(((u16)p[0] << 8) | (u16)p[1]);
}

/* The two group-0 families share the first 12 bytes of the HCC header:
 *   [0] group nibble            [1] alloc nibble | band nibble<<4
 *   [2] vendor field [U]        [3] vap_id
 *   [4..5] total length         [6..7] HCC id
 *   [8] 0                       [9] destination nibble
 *   [10..11] tag 0x5a5a
 * (vendor vs_frame_open / hmac_user_add / hmac_del_user_notify_dmac). */
static int hsop_hdr(u8 *wire, size_t cap, size_t total, u8 id, u8 band,
		    u8 vap_id, u8 hdr2, u8 dst)
{
	if (!wire || cap < total || total > 0xffffu || dst > 0x0fu)
		return -EINVAL;
	memset(wire, 0, total);
	wire[0] = (u8)(HSOP_HCC_GROUP_CHIP & 0x0fu);
	wire[1] = (u8)(HSOP_ALLOC_NIBBLE | (u8)((band & 0x0fu) << 4));
	wire[2] = hdr2;
	wire[3] = vap_id;
	hsop_put16(wire + 4, (u16)total);
	hsop_put16(wire + 6, (u16)id);
	wire[8] = 0u;
	wire[9] = (u8)(dst & 0x0fu);
	hsop_put16(wire + 10, HSOP_HCC_TAG);
	return 0;
}

/* ------------------------------------------------------------------ */
/* g0 id7 user_add (180 B wire, 168 B body)                           */
/* ------------------------------------------------------------------ */
int hsop_build_user_add(u8 *wire, size_t cap, size_t *out_len,
			const struct hsop_user_add_params *p)
{
	u8 *body;
	int rc;

	if (!wire || !out_len || !p)
		return -EINVAL;
	if (p->type == 0u || p->aid == 0u || p->aid > 0x0fffu)
		return -EINVAL;	/* firmware user id space is 12-bit [V] */
	if (cap < HSOP_USER_ADD_WIRE_LEN)
		return -ENOSPC;
	rc = hsop_hdr(wire, cap, HSOP_USER_ADD_WIRE_LEN, HSOP_ID_USER_ADD,
		      p->band, p->vap_id, p->hdr2, p->dst);
	if (rc)
		return rc;
	body = wire + HSOP_HCC_HDR_LEN;
	hsop_put16(body + HSOP_UA_OFF_UID, p->aid);
	memcpy(body + HSOP_UA_OFF_MAC, p->mac, sizeof(p->mac));
	body[HSOP_UA_OFF_TYPE] = p->type;
	body[HSOP_UA_OFF_RSSI] = (u8)(s8)p->rssi;
	*out_len = HSOP_USER_ADD_WIRE_LEN;
	return 0;
}

int hsop_check_user_add(const u8 *wire, size_t len)
{
	u32 body_len;
	const u8 *body;
	u16 tag;

	if (!wire || len != HSOP_USER_ADD_WIRE_LEN)
		return -EINVAL;
	if ((wire[0] & 0x0fu) != HSOP_HCC_GROUP_CHIP)
		return -EINVAL;
	if ((wire[1] & 0x0fu) != HSOP_ALLOC_NIBBLE)
		return -EINVAL;
	if (hsop_get16(wire + 4) != (u16)len)
		return -EINVAL;
	if (hsop_get16(wire + 6) != HSOP_ID_USER_ADD)
		return -EINVAL;
	tag = hsop_get16(wire + 10);
	if (tag != HSOP_HCC_TAG)
		return -EINVAL;
	body = wire + HSOP_HCC_HDR_LEN;
	body_len = (u32)len - HSOP_HCC_HDR_LEN;
	if (body_len != HSOP_USER_ADD_BODY_LEN)
		return -EINVAL;
	if (hsop_get16(body + HSOP_UA_OFF_UID) == 0u)
		return -EINVAL;
	if (body[HSOP_UA_OFF_TYPE] == 0u)
		return -EINVAL;
	return 0;
}

/* ------------------------------------------------------------------ */
/* g0 id9 user_del (24 B wire, 12 B body)                             */
/* ------------------------------------------------------------------ */
int hsop_build_user_del(u8 *wire, size_t cap, size_t *out_len,
			const struct hsop_user_del_params *p)
{
	u8 *body;
	int rc;

	if (!wire || !out_len || !p)
		return -EINVAL;
	if (p->uid == 0u || p->uid > 0x0fffu)
		return -EINVAL;
	if (cap < HSOP_USER_DEL_WIRE_LEN)
		return -ENOSPC;
	rc = hsop_hdr(wire, cap, HSOP_USER_DEL_WIRE_LEN, HSOP_ID_USER_DEL,
		      p->band, p->vap_id, p->hdr2, p->dst);
	if (rc)
		return rc;
	body = wire + HSOP_HCC_HDR_LEN;
	hsop_put16(body + HSOP_UD_OFF_UID, p->uid);
	memcpy(body + HSOP_UD_OFF_MAC, p->mac, sizeof(p->mac));
	body[HSOP_UD_OFF_FLAG] = p->flag;
	*out_len = HSOP_USER_DEL_WIRE_LEN;
	return 0;
}

int hsop_check_user_del(const u8 *wire, size_t len)
{
	const u8 *body;

	if (!wire || len != HSOP_USER_DEL_WIRE_LEN)
		return -EINVAL;
	if (hsop_get16(wire + 4) != (u16)len)
		return -EINVAL;
	if (hsop_get16(wire + 6) != HSOP_ID_USER_DEL)
		return -EINVAL;
	if (hsop_get16(wire + 10) != HSOP_HCC_TAG)
		return -EINVAL;
	body = wire + HSOP_HCC_HDR_LEN;
	if (hsop_get16(body + HSOP_UD_OFF_UID) == 0u)
		return -EINVAL;
	return 0;
}

/* ------------------------------------------------------------------ */
/* g0 id1 config event cmd 0x07d2 (32 B wire, 4 B body)               */
/* ------------------------------------------------------------------ */
int hsop_build_asoc_syn(u8 *wire, size_t cap, size_t *out_len,
			const struct hsop_asoc_syn_params *p)
{
	u8 *token, *body;
	size_t total = HSOP_ASOC_SYN_WIRE_LEN;
	int rc;

	if (!wire || !out_len || !p)
		return -EINVAL;
	if (p->aid == 0u || p->aid > 0x0fffu)
		return -EINVAL;
	if (p->state == 0u)
		return -EINVAL;	/* 0 would mean "clear": not modelled */
	if (cap < total)
		return -ENOSPC;
	rc = hsop_hdr(wire, cap, total, HSOP_ID_CFG_EVENT, p->band, p->vap_id,
		      p->hdr2, p->dst);
	if (rc)
		return rc;
	/* Proven g0 id1 carrier shape: token[8] | cmd u16 | len u16 | body. */
	token = wire + HSOP_HCC_HDR_LEN;
	token[HSOP_CFG_TOKEN_BYTE] = HSOP_CFG_TOKEN_VALUE;
	hsop_put16(token + 8, HSOP_CFG_CMD_ASOC_SYN);
	hsop_put16(token + 10, HSOP_ASOC_SYN_BODY_LEN);
	body = token + 12;
	hsop_put16(body + HSOP_ASOC_OFF_UID, p->aid);
	body[HSOP_ASOC_OFF_STATE] = p->state;
	/* inherited 0xff pad after the body */
	memset(wire + total - 4u, HSOP_CFG_PAD_VALUE, 4u);
	*out_len = total;
	return 0;
}

int hsop_check_asoc_syn(const u8 *wire, size_t len)
{
	const u8 *token, *body;
	unsigned int i;

	if (!wire || len != HSOP_ASOC_SYN_WIRE_LEN)
		return -EINVAL;
	if ((wire[0] & 0x0fu) != HSOP_HCC_GROUP_CHIP)
		return -EINVAL;
	if (hsop_get16(wire + 4) != (u16)len)
		return -EINVAL;
	if (hsop_get16(wire + 6) != HSOP_ID_CFG_EVENT)
		return -EINVAL;
	if (hsop_get16(wire + 10) != HSOP_HCC_TAG)
		return -EINVAL;
	token = wire + HSOP_HCC_HDR_LEN;
	if (hsop_get16(token + 8) != HSOP_CFG_CMD_ASOC_SYN)
		return -EINVAL;
	if (hsop_get16(token + 10) != HSOP_ASOC_SYN_BODY_LEN)
		return -EINVAL;
	body = token + 12;
	if (hsop_get16(body + HSOP_ASOC_OFF_UID) == 0u)
		return -EINVAL;
	if (body[HSOP_ASOC_OFF_STATE] == 0u)
		return -EINVAL;
	for (i = 0; i < 4u; i++)
		if (wire[len - 4u + i] != HSOP_CFG_PAD_VALUE)
			return -EINVAL;
	return 0;
}

/* ------------------------------------------------------------------ */
/* WAL cfg-event kick body (16 B; the sta lane supplies the envelope) */
/* ------------------------------------------------------------------ */
int hsop_build_kick_body(u8 *body, size_t cap,
			 const struct hsop_kick_params *p)
{
	if (!body || !p)
		return -EINVAL;
	if (cap < HSOP_KICK_BODY_LEN)
		return -ENOSPC;
	memset(body, 0, HSOP_KICK_BODY_LEN);
	memcpy(body + HSOP_KICK_OFF_MAC, p->mac, sizeof(p->mac));
	hsop_put16(body + HSOP_KICK_OFF_REASON, p->reason);
	body[HSOP_KICK_OFF_SUBTYPE] =
		(p->subtype == HSOP_KICK_SUBTYPE_DEAUTH) ?
		HSOP_KICK_FLAG_DEAUTH : 0u;
	body[HSOP_KICK_OFF_LAST] = 1u;	/* vendor constant [V] */
	return 0;
}

/* ------------------------------------------------------------------ */
/* station table                                                      */
/* ------------------------------------------------------------------ */
static int hsop_mac_ok(const u8 *mac)
{
	return mac && !(mac[0] & 0x01);	/* unicast only */
}

void hsop_sta_table_init(struct hsop_sta_table *t)
{
	if (!t)
		return;
	memset(t, 0, sizeof(*t));
}

struct hsop_sta *hsop_sta_find(struct hsop_sta_table *t, const u8 *mac)
{
	unsigned int i;

	if (!t || !mac)
		return NULL;
	for (i = 0; i < HSOP_MAX_STA; i++) {
		if (t->sta[i].used && !memcmp(t->sta[i].mac, mac, 6))
			return &t->sta[i];
	}
	return NULL;
}

const struct hsop_sta *hsop_sta_find_const(const struct hsop_sta_table *t,
					   const u8 *mac)
{
	unsigned int i;

	if (!t || !mac)
		return NULL;
	for (i = 0; i < HSOP_MAX_STA; i++) {
		if (t->sta[i].used && !memcmp(t->sta[i].mac, mac, 6))
			return &t->sta[i];
	}
	return NULL;
}

int hsop_sta_add(struct hsop_sta_table *t, const u8 *mac, u16 aid,
		 u32 flags_mask, u32 flags_set)
{
	unsigned int i, slot = HSOP_MAX_STA;

	if (!t || !hsop_mac_ok(mac) || aid == 0u)
		return -EINVAL;
	if (hsop_sta_find(t, mac)) {
		t->stats.refused++;
		return -EEXIST;
	}
	for (i = 0; i < HSOP_MAX_STA; i++) {
		if (!t->sta[i].used) {
			slot = i;
			break;
		}
	}
	if (slot == HSOP_MAX_STA) {
		t->stats.refused++;
		return -ENOSPC;
	}
	memset(&t->sta[slot], 0, sizeof(t->sta[slot]));
	t->sta[slot].used = 1;
	memcpy(t->sta[slot].mac, mac, 6);
	t->sta[slot].aid = aid;
	t->sta[slot].flags_mask = flags_mask;
	t->sta[slot].flags_set = flags_set;
	t->stats.added++;
	return 0;
}

int hsop_sta_update(struct hsop_sta_table *t, const u8 *mac,
		    u32 flags_mask, u32 flags_set)
{
	struct hsop_sta *s = hsop_sta_find(t, mac);

	if (!s)
		return -ENOENT;
	s->flags_mask |= flags_mask;
	s->flags_set = (s->flags_set & ~flags_mask) | (flags_set & flags_mask);
	t->stats.updated++;
	return 0;
}

int hsop_sta_del(struct hsop_sta_table *t, const u8 *mac)
{
	struct hsop_sta *s = hsop_sta_find(t, mac);

	if (!s) {
		t->stats.unknown_del++;
		return -ENOENT;
	}
	memset(s, 0, sizeof(*s));
	t->stats.deleted++;
	return 0;
}

u32 hsop_sta_count(const struct hsop_sta_table *t)
{
	u32 i, n = 0;

	if (!t)
		return 0;
	for (i = 0; i < HSOP_MAX_STA; i++)
		if (t->sta[i].used)
			n++;
	return n;
}

int hsop_sta_at(const struct hsop_sta_table *t, u32 index, struct hsop_sta *out)
{
	u32 i, n = 0;

	if (!t || !out)
		return -EINVAL;
	for (i = 0; i < HSOP_MAX_STA; i++) {
		if (!t->sta[i].used)
			continue;
		if (n == index) {
			memcpy(out, &t->sta[i], sizeof(*out));
			return 0;
		}
		n++;
	}
	return -ENOENT;
}

/* ------------------------------------------------------------------ */
/* 802.11 data RX parse + Ethernet conversion                         */
/* ------------------------------------------------------------------ */

/* MAC header length from the frame control, identical in effect to the
 * vendor hmac_rx_get_mac_header_len: base 24, 4-address (ToDS+FromDS) 30,
 * +2 when a QoS control field is present (data subtype bit 3), +4 when the
 * HT-control/order bit is set. */
static u32 hsop_hdr_len(u16 fc)
{
	u32 len = ((fc & 0x0300u) == 0x0300u) ? 30u : 24u;

	if ((fc & 0x0080u) != 0u)
		len += 2u;	/* QoS control, data subtype bit 3 */
	if ((fc & 0x8000u) != 0u)
		len += 4u;	/* HT control / order bit */
	return len;
}

int hsop_rx_parse(const u8 *mpdu, u32 len, struct hsop_rx_info *out)
{
	u16 fc;
	u32 hdr_len, off;
	const u8 *h;

	if (!mpdu || !out)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	if (len < 24u)
		return -EINVAL;
	fc = hsop_get16(mpdu);
	out->fc = fc;
	out->type = (u8)((fc >> 2) & 0x3u);
	out->subtype = (u8)((fc >> 4) & 0x0fu);
	out->tods = (fc & 0x0100u) ? 1 : 0;
	out->fromds = (fc & 0x0200u) ? 1 : 0;
	out->is_data = (out->type == 2u);
	if (!out->is_data) {
		out->hdr_len = hsop_hdr_len(fc);
		return 0;
	}
	hdr_len = hsop_hdr_len(fc);
	if (hdr_len > len)
		return -EINVAL;
	out->hdr_len = hdr_len;
	h = mpdu;
	/* Address selection (vendor hmac_rx_ring_entry_proc_frame rules):
	 *   FromDS=0: src=addr2, dst = ToDS ? addr3 : addr1
	 *   FromDS=1: src = addr3 (addr4 when 4-address), dst = addr1 */
	if (!out->fromds) {
		memcpy(out->src, h + 10, 6);
		memcpy(out->dst, h + (out->tods ? 16 : 4), 6);
	} else {
		memcpy(out->dst, h + 4, 6);
		if (out->tods) {	/* both bits set = 4-address */
			if (len < 30u)
				return -EINVAL;
			memcpy(out->src, h + 24, 6);
		} else {
			memcpy(out->src, h + 16, 6);
		}
	}
	/* LLC/SNAP: AA AA 03 00 00 00 + ethertype */
	off = hdr_len;
	if (len < off + HSOP_SNAP_HDR_LEN)
		return 0;	/* data frame, but no LLC/SNAP payload */
	if (mpdu[off] != 0xaau || mpdu[off + 1] != 0xaau || mpdu[off + 2] != 0x03u)
		return 0;
	if (mpdu[off + 3] != 0x00u || mpdu[off + 4] != 0x00u ||
	    mpdu[off + 5] != 0x00u)
		return 0;
	out->has_snap = 1;
	out->payload_off = off + HSOP_SNAP_HDR_LEN;
	out->ethertype = hsop_getbe16(mpdu + off + 6);
	if (out->ethertype != HSOP_ETH_P_PAE)
		return 0;
	if (len < out->payload_off + HSOP_EAPOL_HDR_LEN)
		return 0;
	out->is_eapol = 1;
	out->eapol_type = mpdu[out->payload_off + 1u];
	out->eapol_len = hsop_getbe16(mpdu + out->payload_off + 2u);
	if (out->eapol_type != 3u)	/* 3 = EAPOL-Key */
		return 0;
	if (len < out->payload_off + HSOP_EAPOL_KEY_MIN)
		return 0;
	out->eapol_key = 1;
	{
		u16 ki = hsop_getbe16(mpdu + out->payload_off +
				      HSOP_EAPOL_KEY_INFO_OFF);

		out->eapol_key_datalen = hsop_getbe16(mpdu + out->payload_off +
						      HSOP_EAPOL_KEYDLEN_OFF);
		out->eapol_key_pairwise = (ki & HSOP_EAPOL_KEY_PAIRWISE) ? 1 : 0;
		/* vendor hmac_is_eapol_key_ptk_4_4 semantics: pairwise and,
		 * for the completion detection, zero key-data length. */
		out->eapol_ptk_done_like =
			(out->eapol_key_pairwise && out->eapol_key_datalen == 0u) ?
			1 : 0;
	}
	return 0;
}

int hsop_rx_to_eth(const u8 *mpdu, u32 len, u8 *eth, size_t cap,
		   size_t *eth_len, struct hsop_rx_info *info)
{
	struct hsop_rx_info local;
	struct hsop_rx_info *rx = info ? info : &local;
	u32 payload;
	size_t total;
	int rc;

	if (!mpdu || !eth || !eth_len)
		return -EINVAL;
	rc = hsop_rx_parse(mpdu, len, rx);
	if (rc)
		return rc;
	if (!rx->is_data || !rx->has_snap)
		return -EOPNOTSUPP;
	payload = rx->payload_off;
	if (payload >= len)
		return -EINVAL;
	total = (size_t)HSOP_ETH_HDR_LEN + (size_t)(len - payload);
	if (cap < total)
		return -ENOSPC;
	memcpy(eth, rx->dst, 6);
	memcpy(eth + 6, rx->src, 6);
	eth[12] = (u8)(rx->ethertype >> 8);
	eth[13] = (u8)(rx->ethertype & 0xffu);
	memcpy(eth + HSOP_ETH_HDR_LEN, mpdu + payload, len - payload);
	*eth_len = total;
	return 0;
}

int hsop_rx_deliverable(const struct hsop_rx_info *rx, int authorized)
{
	if (!rx || !rx->is_data)
		return 0;
	if (rx->is_eapol)
		return 1;	/* the 4-way handshake needs EAPOL before auth */
	return authorized ? 1 : 0;
}
