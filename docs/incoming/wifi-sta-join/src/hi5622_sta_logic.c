// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_sta_logic.c - portable STA/band/HT capability + join-gate +
 * D2H completion logic (clean-room).
 *
 * ORIGINAL clean-room code (wifi-sta-static task-141; hardened by the
 * wifi-sta-join lane, task-146).  See hi5622_sta_logic.h and ../REPORT.md.
 */
#include "hi5622_sta_logic.h"

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#else
#include <errno.h>
#include <string.h>
#endif

/* Vendor 2.4 GHz channel table (.data+0x16e4, 14 x 56 B): ch1..13 + ch14. */
static const struct hi5622_sta_chan hi5622_sta_chan_2g[] = {
	{ 2412, 1,  0 }, { 2417, 2,  0 }, { 2422, 3,  0 }, { 2427, 4,  0 },
	{ 2432, 5,  0 }, { 2437, 6,  0 }, { 2442, 7,  0 }, { 2447, 8,  0 },
	{ 2452, 9,  0 }, { 2457, 10, 0 }, { 2462, 11, 0 }, { 2467, 12, 0 },
	{ 2472, 13, 0 }, { 2484, 14, 0 },
};

/* Vendor 5 GHz channel table (.data+0x108c, 29 x 56 B). */
static const struct hi5622_sta_chan hi5622_sta_chan_5g[] = {
	{ 5180,  36,  0 }, { 5200,  40,  0 }, { 5220,  44,  0 },
	{ 5240,  48,  0 }, { 5260,  52,  HI5622_STA_CHAN_RADAR },
	{ 5280,  56,  HI5622_STA_CHAN_RADAR }, { 5300,  60, HI5622_STA_CHAN_RADAR },
	{ 5320,  64,  HI5622_STA_CHAN_RADAR }, { 5500, 100, HI5622_STA_CHAN_RADAR },
	{ 5520, 104, HI5622_STA_CHAN_RADAR }, { 5540, 108, HI5622_STA_CHAN_RADAR },
	{ 5560, 112, HI5622_STA_CHAN_RADAR }, { 5580, 116, HI5622_STA_CHAN_RADAR },
	{ 5600, 120, HI5622_STA_CHAN_RADAR }, { 5620, 124, HI5622_STA_CHAN_RADAR },
	{ 5640, 128, HI5622_STA_CHAN_RADAR }, { 5660, 132, HI5622_STA_CHAN_RADAR },
	{ 5680, 136, HI5622_STA_CHAN_RADAR }, { 5700, 140, HI5622_STA_CHAN_RADAR },
	{ 5720, 144, HI5622_STA_CHAN_RADAR }, { 5745, 149, 0 },
	{ 5765, 153, 0 }, { 5785, 157, 0 }, { 5805, 161, 0 }, { 5825, 165, 0 },
	{ 4920, 184, HI5622_STA_CHAN_NO_IR }, { 4940, 188, HI5622_STA_CHAN_NO_IR },
	{ 4960, 192, HI5622_STA_CHAN_NO_IR }, { 4980, 196, HI5622_STA_CHAN_NO_IR },
};

const struct hi5622_sta_chan *hi5622_sta_band_channels(u8 band, u16 *count)
{
	if (band == 0u) {
		if (count)
			*count = (u16)(sizeof(hi5622_sta_chan_2g) /
				       sizeof(hi5622_sta_chan_2g[0]));
		return hi5622_sta_chan_2g;
	}
	if (band == 1u) {
		if (count)
			*count = (u16)(sizeof(hi5622_sta_chan_5g) /
				       sizeof(hi5622_sta_chan_5g[0]));
		return hi5622_sta_chan_5g;
	}
	if (count)
		*count = 0;
	return (const struct hi5622_sta_chan *)0;
}

u16 hi5622_sta_ht_cap_word(u32 ht_mode)
{
	switch (ht_mode) {
	case HI5622_STA_HT_MODE_BASELINE:
		return HI5622_STA_HT_CAP_BASELINE;
	case HI5622_STA_HT_MODE_VENDOR20:
		return HI5622_STA_HT_CAP_SGI20;
	case HI5622_STA_HT_MODE_VENDOR40:
		return HI5622_STA_HT_CAP_VENDOR;
	default:
		return 0xffffu;
	}
}

int hi5622_sta_ht_mode_is_40(u32 ht_mode)
{
	return ht_mode == HI5622_STA_HT_MODE_VENDOR40 ? 1 : 0;
}

u32 hi5622_sta_vht_cap_word(u8 band)
{
	if (band == 0u)
		return HI5622_STA_VHT_CAP_2G;
	if (band == 1u)
		return HI5622_STA_VHT_CAP_5G;
	return 0xffffffffu;
}

int hi5622_sta_auth_type_valid(u32 auth_type)
{
	/* nl80211_auth_type: OPEN=0 .. AUTOMATIC=8 (vendor passes the value
	 * straight into the connect body byte +17, wal_cfg80211_connect [V]). */
	return auth_type <= 8u ? 1 : 0;
}

int hi5622_sta_connect_check(const struct hi5622_sta_connect_check *p)
{
	if (!p)
		return -EINVAL;
	if (!p->ssid || p->ssid_len == 0u || p->ssid_len > HI5622_STA_MAX_SSID)
		return -EINVAL;
	if (p->ie_len > HI5622_STA_MAX_IE)
		return -E2BIG;
	if (p->ie_len && !p->ie)
		return -EINVAL;
	if (p->key_len > HI5622_STA_MAX_KEY)
		return -E2BIG;
	if (p->key_len && !p->key)
		return -EINVAL;
	if (p->key_idx > 3u)
		return -EINVAL;
	if (!hi5622_sta_auth_type_valid(p->auth_type))
		return -EINVAL;
	return 0;
}

/* ---------------------------------------------------------------------
 * Join fire-path gates (task-146)
 * -------------------------------------------------------------------*/
int hi5622_sta_join_gate(u32 sta_join, u32 allow_frames,
			 int already_attempted)
{
	if (sta_join > HI5622_STA_JOIN_MAX)
		return -EINVAL;
	if (sta_join == HI5622_STA_JOIN_OFF)
		return -EOPNOTSUPP;
	if (sta_join == HI5622_STA_JOIN_DRY)
		return 0;	/* build+validate only; no device write */
	/* single-shot post */
	if (!allow_frames)
		return -EPERM;	/* inherited post path is fail-closed */
	if (already_attempted)
		return -EALREADY;	/* one join per module load, no retry */
	return 0;
}

int hi5622_sta_health_gate(u32 exc_mask, int exc_valid, u32 staged)
{
	/* "mask" = firmware CPU 0x400008 exception-seen bitmap (HCC read);
	 * "staging" = firmware CPU 0x40000c vector context frame captured.
	 * Either is a stop condition: no retry, power-cycle, analyse. */
	if (exc_valid && exc_mask)
		return -EIO;
	if (staged)
		return -EIO;
	return 0;
}

/* ---------------------------------------------------------------------
 * D2H completion decode (task-146).  Fields decoded from the vendor host
 * handlers wal_asoc_comp_proc_sta / wal_disasoc_comp_proc_sta; the two IE
 * addresses are firmware-supplied and are only range-checked here.
 * -------------------------------------------------------------------*/
static u16 sta_get16(const u8 *p)
{
	return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static u32 sta_get32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
	       ((u32)p[3] << 24);
}

static int sta_d2h_hdr_check(const u8 *hdr, size_t hdr_len, u16 want_id,
			     size_t payload_len)
{
	if (!hdr || hdr_len < HI5622_STA_D2H_HDR_LEN)
		return -EINVAL;
	if (hdr_len != HI5622_STA_D2H_HDR_LEN)
		return -EINVAL;
	if ((hdr[0] & 0x0fu) != HI5622_STA_D2H_WAL_GROUP)
		return -EINVAL;
	if (sta_get16(hdr + 0x06) != want_id)
		return -EINVAL;
	if (sta_get16(hdr + 0x0a) != HI5622_STA_D2H_TAG)
		return -EINVAL;
	if (sta_get16(hdr + 0x04) !=
	    (u16)(HI5622_STA_D2H_HDR_LEN + payload_len))
		return -EINVAL;
	return 0;
}

/* A firmware-supplied host-DRAM address is usable only inside the identity
 * host-DMA window; a value outside it is untrusted input, not a pointer. */
static int sta_devva_ok(u32 va, u32 len, u32 dma_lo, u32 dma_hi)
{
	u64 end;

	if (va < dma_lo || va > dma_hi)
		return 0;
	if (va & 0x3u)
		return 0;
	end = (u64)va + (u64)len;
	if (end > (u64)dma_hi + 1u)
		return 0;
	return 1;
}

int hi5622_sta_assoc_comp_decode(const u8 *hdr, size_t hdr_len,
				 const u8 *payload, size_t payload_len,
				 u32 dma_lo, u32 dma_hi,
				 struct hi5622_sta_assoc_comp *out)
{
	u32 req_len, resp_len, req_va, resp_va;
	int rc;

	if (!payload || !out)
		return -EINVAL;
	if (payload_len < HI5622_STA_ASSOC_MIN_LEN)
		return -ENOSPC;
	rc = sta_d2h_hdr_check(hdr, hdr_len, HI5622_STA_D2H_ID_ASSOC_COMP,
			       payload_len);
	if (rc)
		return rc;
	req_len = sta_get32(payload + HI5622_STA_ASSOC_OFF_REQ_IE_LEN);
	resp_len = sta_get32(payload + HI5622_STA_ASSOC_OFF_RESP_IE_LEN);
	req_va = sta_get32(payload + HI5622_STA_ASSOC_OFF_REQ_IE_VA);
	resp_va = sta_get32(payload + HI5622_STA_ASSOC_OFF_RESP_IE_VA);
	if (req_len > HI5622_STA_D2H_IE_MAX || resp_len > HI5622_STA_D2H_IE_MAX)
		return -E2BIG;
	if (req_len && !sta_devva_ok(req_va, req_len, dma_lo, dma_hi))
		return -ERANGE;
	if (resp_len && !sta_devva_ok(resp_va, resp_len, dma_lo, dma_hi))
		return -ERANGE;
	memset(out, 0, sizeof(*out));
	out->status = sta_get16(payload + HI5622_STA_ASSOC_OFF_STATUS);
	out->unknown0 = sta_get16(payload + HI5622_STA_ASSOC_OFF_UNK0);
	out->unknown_a = sta_get16(payload + HI5622_STA_ASSOC_OFF_UNKA);
	memcpy(out->bssid, payload + HI5622_STA_ASSOC_OFF_BSSID, 6);
	out->req_ie_len = req_len;
	out->resp_ie_len = resp_len;
	out->req_ie_va = req_va;
	out->resp_ie_va = resp_va;
	return 0;
}

int hi5622_sta_disasoc_comp_decode(const u8 *hdr, size_t hdr_len,
				   const u8 *payload, size_t payload_len,
				   struct hi5622_sta_disasoc_comp *out)
{
	int rc;

	if (!payload || !out)
		return -EINVAL;
	if (payload_len < HI5622_STA_DISASOC_MIN_LEN)
		return -ENOSPC;
	rc = sta_d2h_hdr_check(hdr, hdr_len, HI5622_STA_D2H_ID_DISASOC_COMP,
			       payload_len);
	if (rc)
		return rc;
	memset(out, 0, sizeof(*out));
	out->reason = sta_get16(payload + HI5622_STA_DISASOC_OFF_REASON);
	return 0;
}
