/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_data_tx_logic.c - portable data-TX logic (802.3 -> 802.11 data
 * MPDU, EAPOL-Key classification, peer-aware TX decision, completion
 * bookkeeping).  Host-tested by tools/data_tx_selftest.c; shared verbatim by
 * the kernel glue.  ORIGINAL clean-room code (lane wifi-data-tx, task-150).
 *
 * No Linux header and no device access: this file is compiled both by the
 * host selftest (C99) and as part of the kernel module.
 */
#include "hi5622_data_tx_logic.h"

static void hdtx_put_le16(u8 *p, u16 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
}

static void hdtx_put_le32(u8 *p, u32 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
	p[2] = (u8)((v >> 16) & 0xffu);
	p[3] = (u8)((v >> 24) & 0xffu);
}

static u16 hdtx_get_be16(const u8 *p)
{
	return (u16)(((u16)p[0] << 8) | (u16)p[1]);
}

static void hdtx_copy_addr(u8 *p, const u8 addr[6])
{
	p[0] = addr[0];
	p[1] = addr[1];
	p[2] = addr[2];
	p[3] = addr[3];
	p[4] = addr[4];
	p[5] = addr[5];
}

int hdtx_is_multicast(const u8 addr[6])
{
	if (addr == NULL)
		return 0;
	return (addr[0] & 0x01u) != 0u;
}

/* ------------------------------------------------------------------ */
/* 802.3 -> 802.11 data MPDU                                          */
/* ------------------------------------------------------------------ */
int hdtx_eth_parse(const u8 *frame, u32 len, struct hdtx_eth *out)
{
	if (out == NULL)
		return HDTX_EARG;
	memset(out, 0, sizeof(*out));
	if (frame == NULL)
		return HDTX_EARG;
	if (len < HDTX_ETH_HDR_LEN)
		return HDTX_ESHORT;
	out->da = frame;
	out->sa = frame + 6;
	out->ethertype = hdtx_get_be16(frame + 12);
	out->payload = frame + 12;
	out->payload_len = len - 12u;
	out->frame_len = len;
	out->ok = 1;
	if (out->ethertype == HDTX_ETH_P_VLAN)
		return HDTX_EFORMAT; /* 802.3ac tags are not supported (fail closed) */
	return HDTX_OK;
}

u16 hdtx_mpdu_fc(u8 qos, u8 protected_frame)
{
	u16 fc = qos ? HDTX_FC_QOS_DATA_FROMDS : HDTX_FC_DATA_FROMDS;

	if (protected_frame)
		fc |= HDTX_FC_PROTECTED;
	return fc;
}

int hdtx_build_data_mpdu(u8 *out, size_t cap, size_t *out_len,
			 const struct hdtx_mpdu_params *p)
{
	static const u8 llc_snap[6] = { 0xaau, 0xaau, 0x03u, 0x00u, 0x00u,
					0x00u };
	size_t hdr_len;
	size_t total;

	if (out == NULL || out_len == NULL || p == NULL)
		return HDTX_EARG;
	if (p->bssid == NULL || p->da == NULL || p->sa == NULL ||
	    p->payload == NULL)
		return HDTX_EARG;
	if (p->payload_len < 2u) /* the LLC/SNAP type must be present */
		return HDTX_ESHORT;
	if (p->qos > 1u || p->protected_frame > 1u || p->tid > 15u)
		return HDTX_EARG;
	hdr_len = p->qos ? HDTX_80211_QOS_HDR_LEN : HDTX_80211_HDR_LEN;
	/* payload starts with the 2-byte Ethernet type, so only the 6-byte LLC
	 * header is added in front of it (the full LLC/SNAP prefix is 8) */
	total = hdr_len + HDTX_LLC_HDR_LEN + (size_t)p->payload_len;
	if (total > cap)
		return HDTX_EBOUNDS;
	if (total > HDTX_MPDU_MAX)
		return HDTX_EBOUNDS;

	hdtx_put_le16(out + 0, hdtx_mpdu_fc(p->qos, p->protected_frame));
	hdtx_put_le16(out + 2, 0u);		/* duration */
	hdtx_copy_addr(out + 4, p->da);		/* addr1 = DA */
	hdtx_copy_addr(out + 10, p->bssid);	/* addr2 = BSSID/TA */
	hdtx_copy_addr(out + 16, p->sa);	/* addr3 = SA (FromDS=1) */
	hdtx_put_le16(out + 22,
		      (u16)(((u16)p->seq & (u16)HDTX_SEQ_MASK) <<
			    HDTX_80211_SEQ_SHIFT));
	if (p->qos)
		hdtx_put_le16(out + 24, (u16)(p->tid & 0x0fu));
	memcpy(out + hdr_len, llc_snap, sizeof(llc_snap));
	/* the caller's payload starts with the 2-byte big-endian Ethernet type,
	 * i.e. the last two bytes of the 8-byte LLC/SNAP prefix */
	memcpy(out + hdr_len + sizeof(llc_snap), p->payload,
	       (size_t)p->payload_len);
	*out_len = total;
	return HDTX_OK;
}

int hdtx_frame_write(u8 *buf, size_t cap, u32 owner, const u8 *mpdu,
		     u32 mpdu_len, u32 *devva_off)
{
	if (buf == NULL || mpdu == NULL || devva_off == NULL)
		return HDTX_EARG;
	if (mpdu_len == 0u || mpdu_len > HDTX_MPDU_MAX)
		return HDTX_EARG;
	if (cap < (size_t)HDTX_FRAME_MPDU_OFF + (size_t)mpdu_len)
		return HDTX_EBOUNDS;
	memset(buf, 0, HDTX_FRAME_MPDU_OFF);
	hdtx_put_le32(buf, owner);
	memcpy(buf + HDTX_FRAME_MPDU_OFF, mpdu, (size_t)mpdu_len);
	*devva_off = HDTX_FRAME_HOST_HDR;
	return HDTX_OK;
}

int hdtx_buffer_devva(u64 dma, u32 lo, u32 hi, u32 *devva)
{
	u64 end;

	if (devva == NULL || lo > hi)
		return HDTX_EARG;
	if (dma < (u64)lo || dma > (u64)hi)
		return HDTX_EBOUNDS;
	end = dma + HDTX_FRAME_AREA - 1u;
	if (end > (u64)hi)
		return HDTX_EBOUNDS;
	*devva = (u32)dma + HDTX_FRAME_HOST_HDR;
	return HDTX_OK;
}

/* ------------------------------------------------------------------ */
/* EAPOL / EAPOL-Key classification                                   */
/* ------------------------------------------------------------------ */
int hdtx_classify_eapol(const struct hdtx_eth *eth, struct hdtx_eapol *out)
{
	const u8 *eap;
	u32 avail;

	if (out == NULL)
		return HDTX_EARG;
	memset(out, 0, sizeof(*out));
	if (eth == NULL || !eth->ok || eth->payload == NULL)
		return HDTX_EARG;
	if (eth->ethertype != HDTX_ETH_P_PAE)
		return HDTX_OK; /* not EAPOL: a plain data frame */
	if (eth->payload_len < 2u + HDTX_EAPOL_HDR_LEN)
		return HDTX_ESHORT;

	eap = eth->payload + 2;			/* first EAPOL byte */
	avail = eth->payload_len - 2u;		/* bytes of EAPOL */
	out->is_eapol = 1;
	out->eapol_type = eap[1];
	out->eapol_len = hdtx_get_be16(eap + 2);
	if (out->eapol_type != HDTX_EAPOL_TYPE_KEY)
		return HDTX_OK;
	if (out->eapol_len < HDTX_EAPOL_KEY_MINLEN)
		return HDTX_OK;
	if (avail < HDTX_EAPOL_HDR_LEN + (u32)HDTX_EAPOL_KEY_MINLEN)
		return HDTX_ESHORT; /* header claims a key body beyond the frame */
	out->is_key = 1;
	if (avail >= HDTX_EAPOL_KEY_INFO_OFF + 2u) {
		u16 info = hdtx_get_be16(eap + HDTX_EAPOL_KEY_INFO_OFF);

		out->key_info = info;
		out->pairwise = (info & HDTX_KEY_INFO_PAIRWISE) != 0u;
	}
	if (out->pairwise && avail >= HDTX_EAPOL_KEYDLEN_OFF + 2u) {
		u16 dlen = hdtx_get_be16(eap + HDTX_EAPOL_KEYDLEN_OFF);

		out->key_datalen = dlen;
		/* vendor hmac_is_eapol_key_ptk_4_4: pairwise + key-data-len 0 */
		out->ptk_done_like = (dlen == 0u);
	}
	if (out->pairwise && (out->key_info & HDTX_KEY_INFO_ACK))
		out->msg = (out->key_info & HDTX_KEY_INFO_MIC) ?
			HDTX_EAPOL_MSG_3 : HDTX_EAPOL_MSG_1;
	else
		out->msg = HDTX_EAPOL_MSG_OTHER;
	return HDTX_OK;
}

/* ------------------------------------------------------------------ */
/* Peer-aware TX decision + Protected bit                             */
/* ------------------------------------------------------------------ */
int hdtx_decide_tx(unsigned int peer_required, int is_multicast,
		   int peer_found, int peer_authorized, int is_eapol,
		   unsigned int protect_mode, int group_keys,
		   struct hdtx_tx_decision *out)
{
	int protect = 0;

	if (out == NULL)
		return HDTX_EARG;
	if (protect_mode > HDTX_PROTECT_NEVER)
		return HDTX_EARG;
	memset(out, 0, sizeof(*out));

	if (protect_mode == HDTX_PROTECT_ALWAYS)
		protect = 1;
	else if (protect_mode == HDTX_PROTECT_NEVER)
		protect = 0;

	if (is_multicast) {
		if (protect_mode == HDTX_PROTECT_AUTO)
			protect = group_keys ? 1 : 0;
		out->allowed = 1;
		out->reason = HDTX_OK;
		out->protect = protect;
		return HDTX_OK;
	}

	if (!peer_found) {
		if (peer_required) {
			out->allowed = 0;
			out->reason = HDTX_EPEER;
			out->protect = 0;
			return HDTX_OK;
		}
		/* explicit diagnostic mode: unknown peer, never protect */
		out->allowed = 1;
		out->reason = HDTX_OK;
		out->protect = protect_mode == HDTX_PROTECT_ALWAYS ? 1 : 0;
		return HDTX_OK;
	}

	if (!peer_authorized && !is_eapol) {
		out->allowed = 0;
		out->reason = HDTX_EUNAUTH;
		out->protect = 0;
		return HDTX_OK;
	}

	if (protect_mode == HDTX_PROTECT_AUTO)
		protect = peer_authorized ? 1 : 0;
	out->allowed = 1;
	out->reason = HDTX_OK;
	out->protect = protect;
	return HDTX_OK;
}

/* ------------------------------------------------------------------ */
/* Completion bookkeeping                                             */
/* ------------------------------------------------------------------ */
int hdtx_ring_advance(u32 prev_rptr, u32 now_rptr, u32 depth, u32 *advance)
{
	u32 pi, ni, adv;

	if (advance == NULL || depth == 0u || depth > 0x7fffu)
		return HDTX_EARG;
	pi = prev_rptr & 0x7fffu;
	ni = now_rptr & 0x7fffu;
	if (pi >= depth || ni >= depth)
		return HDTX_EFORMAT;
	if ((prev_rptr ^ now_rptr) & 0x8000u) {
		/* the consumer wrapped (phase flipped): depth - prev + now */
		adv = depth - pi + ni;
	} else if (ni >= pi) {
		adv = ni - pi;
	} else {
		return HDTX_EFORMAT; /* consumer moved backwards */
	}
	if (adv > depth)
		return HDTX_EFORMAT;
	*advance = adv;
	return HDTX_OK;
}
