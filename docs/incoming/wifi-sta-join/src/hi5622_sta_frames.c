// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_sta_frames.c - WAL cfg-event STA join/disconnect frame builders
 * (clean-room; hardened by the wifi-sta-join lane, task-146).
 * See hi5622_sta_frames.h and ../REPORT.md sec 3.
 */
#include "hi5622_sta_frames.h"

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

static void hst_put16(u8 *p, u16 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
}

static void hst_put32(u8 *p, u32 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
	p[2] = (u8)((v >> 16) & 0xffu);
	p[3] = (u8)((v >> 24) & 0xffu);
}

static u16 hst_get16(const u8 *p)
{
	return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static u32 hst_get32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
	       ((u32)p[3] << 24);
}

/* ---------------------------------------------------------------------
 * Body-pointer validation (task-146).  These addresses are handed to the
 * FIRMWARE; the firmware dereferences them, so any value that is not a
 * host-DRAM device address inside the proven identity window is a wild
 * pointer.  All checks are pure integer arithmetic.
 * -------------------------------------------------------------------*/
int hi5622_sta_join_bufs_check(const struct hi5622_sta_join_bufs *p,
			       u32 buf_va, u32 buf_size,
			       u32 dma_lo, u32 dma_hi)
{
	u64 buf_end, end;

	if (!p)
		return -EINVAL;
	if (buf_size < HI5622_STA_JOIN_BUF_SIZE)
		return -ENOSPC;
	if (buf_va < dma_lo || buf_va > dma_hi)
		return -ERANGE;
	buf_end = (u64)buf_va + (u64)buf_size;
	if (buf_end - 1u > (u64)dma_hi)
		return -ERANGE;	/* the whole buffer must stay in the window */
	if (p->ssid_len == 0u || p->ssid_len > HI5622_STA_MAX_SSID)
		return -EINVAL;
	if (p->ie_len > HI5622_STA_MAX_IE)
		return -E2BIG;
	if (p->key_len > HI5622_STA_MAX_KEY)
		return -E2BIG;
	if (p->key_idx > 3u)
		return -EINVAL;
	if (!hi5622_sta_auth_type_valid(p->auth_type))
		return -EINVAL;

	/* no-NULL policy: every field must be a real address */
	if (!p->bssid_va || !p->ssid_va || !p->ie_va || !p->key_va)
		return -EINVAL;
	/* each address: alignment + window (before the layout compare so a
	 * wild value is reported as ERANGE, not hidden as a layout error) */
	if (p->bssid_va & 0x3u || p->ssid_va & 0x3u ||
	    p->ie_va & 0x3u || p->key_va & 0x3u)
		return -EINVAL;
	if (p->bssid_va < dma_lo || p->bssid_va > dma_hi)
		return -ERANGE;
	if (p->ssid_va < dma_lo || p->ssid_va > dma_hi)
		return -ERANGE;
	if (p->ie_va < dma_lo || p->ie_va > dma_hi)
		return -ERANGE;
	if (p->key_va < dma_lo || p->key_va > dma_hi)
		return -ERANGE;
	/* the fixed layout is the only layout this driver builds */
	if (p->bssid_va != buf_va + HI5622_STA_JOIN_OFF_BSSID)
		return -EINVAL;
	if (p->ssid_va != buf_va + HI5622_STA_JOIN_OFF_SSID)
		return -EINVAL;
	if (p->ie_va != buf_va + HI5622_STA_JOIN_OFF_IE)
		return -EINVAL;
	if (p->key_va != buf_va + HI5622_STA_JOIN_OFF_KEY)
		return -EINVAL;
	end = (u64)p->bssid_va + 6u;
	if (end > buf_end)
		return -ERANGE;
	end = (u64)p->ssid_va + (u64)p->ssid_len;
	if (end > buf_end)
		return -ERANGE;
	end = (u64)p->ie_va + (u64)p->ie_len;
	if (end > buf_end)
		return -ERANGE;
	end = (u64)p->key_va + (u64)p->key_len;
	if (end > buf_end)
		return -ERANGE;
	return 0;
}

int hi5622_sta_connect_body(u8 *body, size_t cap,
			    const struct hi5622_sta_join_bufs *p)
{
	if (!body || !p)
		return -EINVAL;
	if (cap < HI5622_STA_CONNECT_BODY_LEN)
		return -ENOSPC;
	if (p->ssid_len == 0u || p->ssid_len > HI5622_STA_MAX_SSID)
		return -EINVAL;
	if (p->ie_len > HI5622_STA_MAX_IE)
		return -EINVAL;
	if (p->key_len > HI5622_STA_MAX_KEY)
		return -EINVAL;
	if (p->key_idx > 3u)
		return -EINVAL;
	if (!hi5622_sta_auth_type_valid(p->auth_type))
		return -EINVAL;
	/* a zero address is not a firmware-compatible "absent" sentinel */
	if (!p->bssid_va || !p->ssid_va || !p->ie_va || !p->key_va)
		return -EINVAL;

	memset(body, 0, HI5622_STA_CONNECT_BODY_LEN);
	/* +0x00 stays 0 (reserved [V]: the vendor struct is memset() to 0). */
	body[0x01] = p->ssid_len;
	body[0x02] = p->mfp;
	body[0x03] = 0u;
	hst_put32(body + 0x04, p->ie_va);
	hst_put32(body + 0x08, p->ssid_va);
	hst_put32(body + 0x0c, p->bssid_va);
	body[0x10] = p->privacy ? 1u : 0u;
	body[0x11] = p->auth_type;
	body[0x12] = p->key_len;
	body[0x13] = p->key_idx;
	hst_put32(body + 0x14, p->key_va);
	hst_put32(body + 0x68, p->ie_len);
	return 0;
}

int hi5622_sta_connect_body_check(const u8 *body, size_t body_len,
				  const struct hi5622_sta_join_bufs *p)
{
	if (!body || !p)
		return -EINVAL;
	if (body_len != HI5622_STA_CONNECT_BODY_LEN)
		return -EINVAL;
	if (body[0x00] != 0u || body[0x03] != 0u)
		return -EINVAL;
	if (body[0x01] != p->ssid_len || body[0x02] != p->mfp)
		return -EINVAL;
	if (body[0x10] != (p->privacy ? 1u : 0u) ||
	    body[0x11] != p->auth_type ||
	    body[0x12] != p->key_len || body[0x13] != p->key_idx)
		return -EINVAL;
	if (hst_get32(body + 0x04) != p->ie_va ||
	    hst_get32(body + 0x08) != p->ssid_va ||
	    hst_get32(body + 0x0c) != p->bssid_va ||
	    hst_get32(body + 0x14) != p->key_va ||
	    hst_get32(body + 0x68) != p->ie_len)
		return -EINVAL;
	return 0;
}

int hi5622_sta_disconnect_body(u8 *body, size_t cap,
			      const struct hi5622_sta_disconnect_params *p)
{
	if (!body || !p)
		return -EINVAL;
	if (cap < HI5622_STA_DISCONNECT_BODY_LEN)
		return -ENOSPC;
	memset(body, 0, HI5622_STA_DISCONNECT_BODY_LEN);
	memcpy(body, p->bssid, 6);
	hst_put16(body + 0x06, p->reason_code);
	body[0x0f] = 1u;	/* vendor constant [V] */
	return 0;
}

int hi5622_sta_wal_frame(u8 *out, size_t cap, size_t *out_len, u16 wal_cmd,
			 const u8 *body, u16 body_len, u32 seq, u8 dst)
{
	size_t total;
	u16 data_len;

	if (!out || (body_len && !body) || dst > 0x0fu)
		return -EINVAL;
	if (wal_cmd == 0u)
		return -EINVAL;
	if (body_len > (u16)(HI5622_STA_FRAME_MAX - HI5622_STA_WAL_HDR_LEN))
		return -E2BIG;
	total = (size_t)HI5622_STA_WAL_HDR_LEN + body_len;
	data_len = (u16)(4u + body_len);
	if (cap < total)
		return -ENOSPC;

	memset(out, 0, total);
	/* 12-byte HCC header: group 2 (core/band), HCC id 0 (the WAL carrier
	 * the bench-proven AP-start frames use), tag 0x5a5a. */
	out[0] = (u8)(HI5622_STA_HCC_GROUP_CORE & 0x0fu);
	out[1] = (u8)(0x01u | ((dst & 0x0fu) << 4));
	out[2] = 0u;
	out[3] = 0u;
	hst_put16(out + 0x04, (u16)total);
	hst_put16(out + 0x06, HI5622_STA_HCC_ID_WAL);
	out[8] = 0u;
	out[9] = (u8)(dst & 0x0fu);
	hst_put16(out + 0x0a, HI5622_STA_HCC_TAG);
	/* WAL envelope. */
	hst_put32(out + 0x0c, 0u);			/* cookie */
	hst_put32(out + 0x10, HI5622_STA_WAL_MSG_CFG);
	hst_put16(out + 0x14, 0u);			/* pad */
	hst_put16(out + 0x16, data_len);
	hst_put32(out + 0x18, seq);
	hst_put16(out + 0x1c, wal_cmd);
	hst_put16(out + 0x1e, body_len);
	if (body_len)
		memcpy(out + HI5622_STA_WAL_HDR_LEN, body, body_len);
	if (out_len)
		*out_len = total;
	return 0;
}

int hi5622_sta_frame_check(const u8 *frame, size_t len, u16 wal_cmd,
			   u16 body_len)
{
	size_t total = (size_t)HI5622_STA_WAL_HDR_LEN + body_len;

	if (!frame || len != total)
		return -EINVAL;
	if ((frame[0] & 0x0fu) != HI5622_STA_HCC_GROUP_CORE)
		return -EINVAL;
	if (((frame[1] >> 4) & 0x0fu) != (frame[9] & 0x0fu))
		return -EINVAL;
	if (hst_get16(frame + 0x04) != (u16)total)
		return -EINVAL;
	if (hst_get16(frame + 0x06) != HI5622_STA_HCC_ID_WAL)
		return -EINVAL;
	if (hst_get16(frame + 0x0a) != HI5622_STA_HCC_TAG)
		return -EINVAL;
	if (hst_get32(frame + 0x10) != HI5622_STA_WAL_MSG_CFG)
		return -EINVAL;
	if (hst_get16(frame + 0x16) != (u16)(4u + body_len))
		return -EINVAL;
	if (hst_get16(frame + 0x1c) != wal_cmd)
		return -EINVAL;
	if (hst_get16(frame + 0x1e) != body_len)
		return -EINVAL;
	return 0;
}
