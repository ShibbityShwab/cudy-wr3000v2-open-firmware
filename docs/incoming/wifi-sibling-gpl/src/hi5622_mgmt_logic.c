// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_mgmt_logic.c - portable management-frame TX/RX wire logic
 * (Hi5622V100 clean-room driver, Cudy WR3000 v2.0).
 *
 * ORIGINAL clean-room code (lane wifi-ap-mgmt, shared task task-143).
 * Host-tested by tools/mgmt_selftest.c; the kernel layer (hi5622_mgmt.c)
 * contains no duplicated decision logic.  See hi5622_mgmt_logic.h for the
 * decoded wire layouts and evidence markers.
 */
#include "hi5622_mgmt_logic.h"

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#else
#include <errno.h>
#include <string.h>
#endif

/* ------------------------------------------------------------------ */
/* little-endian wire helpers                                         */
/* ------------------------------------------------------------------ */
static himg_u16 hg_get16(const himg_u8 *p)
{
	return (himg_u16)((himg_u16)p[0] | ((himg_u16)p[1] << 8));
}

static void hg_put16(himg_u8 *p, himg_u16 v)
{
	p[0] = (himg_u8)(v & 0xffu);
	p[1] = (himg_u8)((v >> 8) & 0xffu);
}

static void hg_put32(himg_u8 *p, himg_u32 v)
{
	p[0] = (himg_u8)(v & 0xffu);
	p[1] = (himg_u8)((v >> 8) & 0xffu);
	p[2] = (himg_u8)((v >> 16) & 0xffu);
	p[3] = (himg_u8)((v >> 24) & 0xffu);
}

/* ------------------------------------------------------------------ */
/* 1. channel -> centre frequency                                     */
/* ------------------------------------------------------------------ */
/* Vendor hmac_rx_mgmt_send_to_host (2.4.15 0x4277c / 2.5.24 0x4288c) [V]:
 *   ch == 0            -> 0 (unset; caller refuses)
 *   ch <= 13           -> 2407 + 5*ch   (2.4 GHz, 1..13)
 *   ch == 14           -> 2484
 *   182 <= ch <= 196   -> 4000 + 5*ch   (4.9 GHz public-safety band)
 *   other ch > 14      -> 5000 + 5*ch   (5 GHz)
 * The vendor code has no upper bound (its loop can only produce channels the
 * scan layer returned); this portable decoder bounds the band plan because
 * the RX info byte is untrusted device input. */
int himg_chan_to_freq(himg_u8 channel, himg_u16 *freq)
{
	himg_u32 f;

	if (!freq)
		return -EINVAL;
	if (channel >= 1u && channel <= 13u)
		f = 2407u + 5u * (himg_u32)channel;
	else if (channel == 14u)
		f = 2484u;
	else if (channel >= 182u && channel <= 196u)
		f = 4000u + 5u * (himg_u32)channel;
	else if (channel >= 32u && channel <= 177u)
		f = 5000u + 5u * (himg_u32)channel;
	else
		return -EINVAL;
	*freq = (himg_u16)f;
	return 0;
}

/* ------------------------------------------------------------------ */
/* 2. mgmt-tx descriptor + WAL cfg-event wire frame                   */
/* ------------------------------------------------------------------ */
int himg_build_tx_desc(himg_u8 *desc, size_t cap,
		       const struct himg_tx_desc *d)
{
	himg_u16 freq;

	if (!desc || !d || cap < HIMG_WAL_CMD_MGMT_TX_BODY)
		return -EINVAL;
	if (d->channel > 0xffu || d->len < HIMG_MIN_MGMT_FRAME ||
	    d->len > HIMG_MAX_FRAME_TX || d->frame_addr == 0u)
		return -EINVAL;
	if (himg_chan_to_freq((himg_u8)d->channel, &freq))
		return -EINVAL;
	memset(desc, 0, HIMG_WAL_CMD_MGMT_TX_BODY);
	hg_put32(desc + 0, d->channel);
	desc[4] = d->cookie_idx;
	desc[5] = 0u;
	hg_put16(desc + 6, d->len);
	hg_put32(desc + 8, d->frame_addr);
	return 0;
}

int himg_build_wal_mgmt_tx(himg_u8 *wire, size_t cap, size_t *out_len,
			   const struct himg_tx_params *p)
{
	struct himg_tx_desc d;
	himg_u8 desc[HIMG_WAL_CMD_MGMT_TX_BODY];
	size_t total = (size_t)HIMG_WAL_OVERHEAD + HIMG_WAL_CMD_MGMT_TX_BODY;
	himg_u8 *b;

	if (!wire || !out_len || !p)
		return -EINVAL;
	if (total > cap)
		return -ENOSPC;
	if (p->dst > 0x0fu)
		return -EINVAL;
	d.channel = p->channel;
	d.cookie_idx = p->cookie_idx;
	d.pad = 0u;
	d.len = p->len;
	d.frame_addr = p->frame_addr;
	if (himg_build_tx_desc(desc, sizeof(desc), &d))
		return -EINVAL;

	memset(wire, 0, total);
	b = wire;
	/* 12-byte HCC header [V]: group 2, alloc nibble 0x1 + dst nibble,
	 * fa/fb 0 (single AP VAP, module 0), total, id 0 (WAL cfg family),
	 * fc 0, dst, tag 0x5a5a. */
	b[0] = HIMG_HCC_GROUP_WAL;
	b[1] = (himg_u8)(HIMG_ALLOC_NIBBLE | ((p->dst & 0x0fu) << 4));
	b[2] = 0u;
	b[3] = 0u;
	hg_put16(b + 4, (himg_u16)total);
	hg_put16(b + 6, HIMG_HCC_ID_WAL);
	b[8] = 0u;
	b[9] = (himg_u8)(p->dst & 0x0fu);
	hg_put16(b + 10, HIMG_HCC_TAG);
	/* WAL cfg-event envelope [V]: cookie 0, id 1, pad 0, data_len =
	 * 4 + body_len, seq, cmd 0x01F2, body_len 12. */
	hg_put32(b + 12, 0u);
	hg_put32(b + 16, HIMG_WAL_MSG_CFG);
	hg_put16(b + 20, 0u);
	hg_put16(b + 22, (himg_u16)(4u + HIMG_WAL_CMD_MGMT_TX_BODY));
	hg_put32(b + 24, p->seq);
	hg_put16(b + 28, HIMG_WAL_CMD_MGMT_TX);
	hg_put16(b + 30, HIMG_WAL_CMD_MGMT_TX_BODY);
	memcpy(b + HIMG_WAL_OVERHEAD, desc, sizeof(desc));
	*out_len = total;
	return 0;
}

/* ------------------------------------------------------------------ */
/* 2b. CORRECTED device-facing mgmt-TX carrier: g0 id 6 control msg  */
/* ------------------------------------------------------------------ */
/* Derived in the sibling-gpl lane (task-151) from our own vendor 2.5.24
 * module: hmac_wpas_mgmt_tx (0x4050c) -> helper (0x3fe68) -> hcc_msg_alloc /
 * hcc_msg_tx.  The helper writes the 12-byte HCC header in place and lays out
 * a 48-byte device TX-control block followed by the 802.11 frame.  See
 * hi5622_mgmt_logic.h section 1b for the byte map and the completion event. */
int himg_build_hcc_mgmt_tx(himg_u8 *wire, size_t cap, size_t *out_len,
			   const struct himg_tx_params *p)
{
	size_t total;
	himg_u8 *ctl;

	if (!wire || !out_len || !p)
		return -EINVAL;
	if (!p->frame || p->frame_len == 0u)
		return -EINVAL;
	if (p->dst > 0x0fu || p->vap_id > 0x0fu || p->band > 0x0fu)
		return -EINVAL;
	if (p->frame_len < HIMG_MGMT_TX_MIN_FRAME ||
	    p->frame_len > HIMG_MGMT_TX_MAX_FRAME)
		return -EINVAL;
	if (himg_mgmt_frame_ok(p->frame, p->frame_len))
		return -EINVAL;
	total = (size_t)HIMG_HCC_HDR_LEN + HIMG_MGMT_TX_CTL_LEN +
		(size_t)p->frame_len;
	if (total > cap || total > 0xffffu)
		return -ENOSPC;

	memset(wire, 0, total);
	/* 12-byte HCC header (same convention as the established g0 builders):
	 * group 0, alloc nibble 1 | band/chip<<4, hdr2, vap id, total, id 6,
	 * fc 0, dst, tag 0x5a5a. */
	wire[0] = (himg_u8)(HIMG_HCC_GROUP_CHIP & 0x0fu);
	wire[1] = (himg_u8)(HIMG_ALLOC_NIBBLE | ((p->band & 0x0fu) << 4));
	wire[2] = p->hdr2;
	wire[3] = (himg_u8)(p->vap_id & 0x0fu);
	hg_put16(wire + 4, (himg_u16)total);
	hg_put16(wire + 6, HIMG_HCC_ID_MGMT);
	wire[8] = 0u;
	wire[9] = (himg_u8)(p->dst & 0x0fu);
	hg_put16(wire + 10, HIMG_HCC_TAG);

	/* 48-byte device TX-control block; only the vendor-filled fields are
	 * set, everything else stays zero (the vendor copies a zeroed host
	 * cb prefix).  mpdu_len excludes the 24-byte 802.11 header; the user id
	 * is 0x0fff when no station context exists (auth/assoc responses). */
	ctl = wire + HIMG_HCC_HDR_LEN;
	hg_put16(ctl + HIMG_CTL_MPDU_LEN,
		 (himg_u16)(p->frame_len - HIMG_MGMT_TX_HDR_LEN));
	ctl[HIMG_CTL_HDR_LEN] = (himg_u8)HIMG_MGMT_TX_HDR_LEN;
	ctl[HIMG_CTL_FLAGS] = (himg_u8)HIMG_CTL_FLAGS_VAL;
	ctl[HIMG_CTL_COOKIE] = (himg_u8)((p->cookie & 0x0fu) << 4);
	hg_put16(ctl + HIMG_CTL_USER,
		 (himg_u16)(p->user_id ? p->user_id : HIMG_CTL_INVALID_USER));

	memcpy(wire + HIMG_MGMT_TX_FRAME_OFF, p->frame, (size_t)p->frame_len);
	*out_len = total;
	return 0;
}

/* ------------------------------------------------------------------ */
/* 3. 802.11 management-frame sanity                                  */
/* ------------------------------------------------------------------ */
int himg_mgmt_frame_ok(const himg_u8 *frame, size_t len)
{
	if (!frame)
		return -EINVAL;
	if (len < HIMG_MIN_MGMT_FRAME || len > HIMG_MAX_FRAME_TX)
		return -EINVAL;
	/* Frame control word [V 802.11-2020]: bits 0-1 protocol version (0),
	 * bits 2-3 type (0 = management).  Any management subtype is legal;
	 * the kernel layer decides which ones it is willing to post. */
	if ((frame[0] & 0x03u) != 0u)
		return -EINVAL;
	if (((frame[0] >> 2) & 0x03u) != 0u)
		return -EINVAL;
	return 0;
}

/* ------------------------------------------------------------------ */
/* 4. D2H management-frame RX decode                                  */
/* ------------------------------------------------------------------ */
/* Wire layout (vendor, [V]): hmac_rx_event_pre_process reads msg->len
 * (= HCC total) and msg->data (= HCC header start), and if total > 59 moves
 * the frame pointer to data+60 and the frame length to total-60; it copies
 * 48 bytes from data+12 to msg->cb.  hmac_rx_mgmt_send_to_host then derives
 * the frequency from msg->cb+7 (channel) and the frame length from
 * msg->cb+17, and hmac_send_mgmt_to_host copies the frame from msg->data and
 * takes the RSSI from msg->cb+39.  So with our payload pointer = data+12:
 * payload[0..47] = RX info, payload[48..] = 802.11 frame. */
int himg_parse_rx_mgmt(const himg_u8 *hdr, const himg_u8 *payload,
		       himg_u32 avail, himg_u32 wire_len,
		       struct himg_rx_frame *out)
{
	himg_u16 total, id, tag, info_len, freq;
	himg_u32 frame_len;
	himg_u8 channel, fc0;

	if (!hdr || !payload || !out)
		return -EINVAL;
	if ((hdr[0] & 0x0fu) != HIMG_RX_GROUP)
		return -ENODATA;
	id = hg_get16(hdr + 6);
	if (id != HIMG_RX_ID)
		return -ENODATA;
	tag = hg_get16(hdr + 10);
	if (tag != HIMG_HCC_TAG)
		return -EPROTO;
	total = hg_get16(hdr + 4);
	if ((himg_u32)total != wire_len)
		return -EPROTO;
	if ((himg_u32)total <= (himg_u32)HIMG_RX_FRAME_OFF)
		return -EPROTO;	/* no frame bytes: not a frame event */
	if (avail < HIMG_RX_INFO_LEN)
		return -EPROTO;
	frame_len = (himg_u32)total - (himg_u32)HIMG_RX_FRAME_OFF;
	if (frame_len < HIMG_MIN_MGMT_FRAME || frame_len > HIMG_MAX_FRAME_TX)
		return -EPROTO;
	if (avail < (himg_u32)HIMG_RX_INFO_LEN + frame_len)
		return -EPROTO;	/* snapshot truncated the frame */
	info_len = hg_get16(payload + HIMG_RX_INFO_FRAME_LEN);
	if ((himg_u32)info_len != frame_len)
		return -EPROTO;	/* vendor cross-check in the RX info */
	channel = payload[HIMG_RX_INFO_CHANNEL];
	if (himg_chan_to_freq(channel, &freq))
		return -EPROTO;
	/* payload already excludes the 12-byte HCC header, so the frame
	 * starts at payload+48 while the wire total measured 60 bytes earlier. */
	fc0 = payload[HIMG_RX_INFO_LEN];
	if ((fc0 & 0x03u) != 0u || ((fc0 >> 2) & 0x03u) != 0u)
		return -EPROTO;	/* only management frames belong to id 13 */
	memset(out, 0, sizeof(*out));
	out->frame = payload + HIMG_RX_INFO_LEN;
	out->frame_len = frame_len;
	out->freq = freq;
	out->rssi = (himg_s8)payload[HIMG_RX_INFO_RSSI];
	out->channel = channel;
	out->subtype = (himg_u8)(fc0 >> 4);
	return 0;
}
