// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_vapscan_map.c - byte mapping between this lane's neutral cfg80211
 * parameters and the task-110 wifi-vap-scan builders, plus the tolerant D2H
 * scan-result decoder (Cudy WR3000 v2.0, Hi5622V100, Linux 6.18).
 *
 * ORIGINAL clean-room code (wifi-scan-ap lane, shared task task-113,
 * 2026-11-25).  Builders: wifi-vap-scan lane, task-110, opensource commit
 * 0e4ad45.  No vendor source/object/firmware byte is reproduced.
 *
 * Host- and kernel-compilable (no kernel API beyond the portable headers);
 * host-tested by tools/scan_logic_selftest.c.
 */
#include "hi5622_vapscan_map.h"

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>
#else
#include <errno.h>
#include <string.h>
#endif

/* g1 id15 host descriptor trailer length [U] (task-110 REPORT sec 6: the
 * vendor host trims 20 bytes before parsing IEs; exact layout unknown). */
#define HI5622_VAPSCAN_SCAN_FOOTER	20u

#define HI5622_MGMT_HDR_LEN		24u
#define HI5622_BEACON_FIXED_LEN		12u	/* timestamp+interval+cap */
#define HI5622_IE_HDR_LEN		2u

int hi5622_vapscan_errno(int vs_err)
{
	switch (vs_err) {
	case VS_OK:		return 0;
	case VS_EARG:
	case VS_EBOUNDS:	return -EINVAL;
	case VS_EBUFFER:	return -ENOSPC;
	case VS_EPOLICY:	return -EPERM;
	case VS_EID:
	case VS_EGROUP:		return -EOPNOTSUPP;
	default:		return -EIO;
	}
}

/* 2.4 GHz channel number of an advertised frequency (kHz-exact table). */
static int hi5622_band2g_chan(u16 freq)
{
	if (freq < 2412u || freq > 2472u || ((freq - 2412u) % 5u))
		return -1;
	return (int)((freq - 2412u) / 5u) + 1;
}

/* 5 GHz: our wiphy table order is also the vendor band channel index [I]. */
static const u16 hi5622_band5g_freq[] = {
	5180, 5200, 5220, 5240, 5260, 5280, 5300, 5320,
	5500, 5520, 5540, 5560, 5580, 5600, 5620, 5640,
	5660, 5680, 5700,
};

static int hi5622_band5g_index(u16 freq)
{
	size_t i;

	for (i = 0; i < sizeof(hi5622_band5g_freq) / sizeof(u16); i++)
		if (hi5622_band5g_freq[i] == freq)
			return (int)i;
	return -1;
}

int hi5622_vapscan_scan_req(struct vs_frame *f,
			    const struct hi5622_scan_req_params *p)
{
	struct vs_scan_params sp;
	u32 i;
	int rc;

	if (!f || !p || p->band > 1u)
		return -EINVAL;
	if (p->ie_len)
		return -EOPNOTSUPP;	/* builder has no IE field [V] */
	if (p->n_ssids > VS_SC_SSID_MAX)
		return -E2BIG;		/* 8 slots in the vendor layout [V] */
	if (p->n_channels > VS_SC_CHAN_MAX)
		return -E2BIG;		/* 42 entries [V] */

	vs_scan_defaults(&sp);
	sp.chip_id = p->band ? 1u : 0u;
	sp.vap_id = 0;			/* firmware scan handler hardcodes VAP 0 [V] */
	sp.dst = 0;
	/* A single-band endpoint: flags1 dual-band stays 0 [I]. */
	for (i = 0; i < p->n_ssids; i++) {
		if (p->ssid_len[i] > VS_SC_SSID_LEN_MAX)
			return -EINVAL;
		sp.ssids[i].len = p->ssid_len[i];
		memcpy(sp.ssids[i].str, p->ssid[i], p->ssid_len[i]);
	}
	sp.n_ssids = (u8)p->n_ssids;
	sp.n_chans = (u8)p->n_channels;
	for (i = 0; i < p->n_channels; i++) {
		u16 freq = p->freq[i];
		int ch;

		if (p->band == 0) {
			ch = hi5622_band2g_chan(freq);
			if (ch < 0)
				return -EINVAL;
			sp.chans[i].num = (u8)ch;
			sp.chans[i].band = 0;
			sp.chans[i].idx = (u8)(ch - 1);
		} else {
			ch = hi5622_band5g_index(freq);
			if (ch < 0)
				return -EINVAL;
			sp.chans[i].num = (u8)((freq - 5000u) / 5u);
			sp.chans[i].band = 1;
			sp.chans[i].idx = (u8)ch;
		}
		sp.chans[i].pad = 0;
	}
	rc = vs_build_scan_req(f, &sp);
	return hi5622_vapscan_errno(rc);
}

int hi5622_vapscan_user_add(struct vs_frame *f,
			    const struct hi5622_vap_add_params *p)
{
	struct vs_user_add ua;
	int rc;

	if (!f || !p || p->band > 1u)
		return -EINVAL;
	vs_user_add_defaults(&ua);
	ua.user_id = p->user_id;
	memcpy(ua.mac, p->mac, sizeof(ua.mac));
	ua.chip_id = p->band;
	ua.vap_id = p->vap_id;
	ua.dst = 0;
	/* type 2 (normal) and rssi +100 are the vendor defaults [V]. */
	rc = vs_build_user_add(f, &ua);
	return hi5622_vapscan_errno(rc);
}

int hi5622_vapscan_user_alg(struct vs_frame *f,
			    const struct hi5622_vap_add_params *p)
{
	struct vs_user_alg alg;
	int rc;

	if (!f || !p || p->band > 1u)
		return -EINVAL;
	vs_user_alg_defaults(&alg);
	alg.user_id = p->user_id;
	alg.chip_id = p->band;
	alg.vap_id = p->vap_id;
	alg.dst = 0;
	/* Capability blocks stay NULL = zeroed: the vendor values are not
	 * decoded yet ([I]/[U], task-110 REPORT item 4); the driver must not
	 * invent HT/VHT/HE capabilities. */
	rc = vs_build_user_alg(f, &alg);
	return hi5622_vapscan_errno(rc);
}

int hi5622_vapscan_user_del(struct vs_frame *f,
			    const struct hi5622_vap_del_params *p)
{
	int rc;

	if (!f || !p || p->band > 1u)
		return -EINVAL;
	/* flag byte: vendor 4th argument, semantic [U] - send 0. */
	rc = vs_build_user_del(f, p->user_id, p->mac, p->band, p->vap_id, 0);
	return hi5622_vapscan_errno(rc);
}

/* ------------------------------------------------------------------ */
/* Tolerant g1 id15 decoder                                           */
/* ------------------------------------------------------------------ */
static u16 hi5622_le16(const u8 *p)
{
	return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static u64 hi5622_le64(const u8 *p)
{
	u64 v = 0;
	int i;

	for (i = 7; i >= 0; i--)
		v = (v << 8) | p[i];
	return v;
}

static int hi5622_ie_channel(const u8 *ies, u32 len, u16 *freq)
{
	u32 off = 0;

	while (off + HI5622_IE_HDR_LEN <= len) {
		u8 id = ies[off];
		u8 tl = ies[off + 1];

		if (off + HI5622_IE_HDR_LEN + tl > len)
			return -EINVAL;
		if (id == 3u && tl == 1u) {	/* DS parameter set [V] */
			*freq = (u16)(2407u + 5u * ies[off + 2]);
			return 0;
		}
		if (id == 61u && tl >= 2u) {	/* HT operation: primary ch [I] */
			*freq = (u16)(5000u + 5u * ies[off + 3]);
			return 0;
		}
		off += HI5622_IE_HDR_LEN + tl;
	}
	return -EOPNOTSUPP;
}

int hi5622_vapscan_parse_scan_result(const u8 *payload, u32 len,
				     struct hi5622_scan_result *out)
{
	u32 flen;
	const u8 *frame;
	const u8 *ies;
	u32 ielen, off;
	u16 fc, cap, interval, freq = 0;
	u8 subtype;

	if (!payload || !out)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	/* Strip the host descriptor trailer [U]; the 802.11 frame must remain
	 * a complete management frame. */
	if (len <= HI5622_VAPSCAN_SCAN_FOOTER)
		return -EOPNOTSUPP;
	flen = len - HI5622_VAPSCAN_SCAN_FOOTER;
	if (flen < HI5622_MGMT_HDR_LEN + HI5622_BEACON_FIXED_LEN)
		return -EOPNOTSUPP;
	frame = payload;
	fc = hi5622_le16(frame);
	if (((fc >> 2) & 0x3u) != 0u)	/* management frame only */
		return -EOPNOTSUPP;
	subtype = (u8)((fc >> 4) & 0xfu);
	if (subtype == 8u)		/* beacon */
		out->frame_type = HI5622_SCAN_FTYPE_BEACON;
	else if (subtype == 5u)		/* probe response */
		out->frame_type = HI5622_SCAN_FTYPE_PRESP;
	else
		return -EOPNOTSUPP;
	memcpy(out->bssid, frame + 16, 6);	/* addr3 [V] */
	out->tsf = hi5622_le64(frame + HI5622_MGMT_HDR_LEN);
	interval = hi5622_le16(frame + HI5622_MGMT_HDR_LEN + 8);
	cap = hi5622_le16(frame + HI5622_MGMT_HDR_LEN + 10);
	out->beacon_interval = interval;
	out->capability = cap;
	ies = frame + HI5622_MGMT_HDR_LEN + HI5622_BEACON_FIXED_LEN;
	ielen = flen - (HI5622_MGMT_HDR_LEN + HI5622_BEACON_FIXED_LEN);
	off = 0;
	while (off + HI5622_IE_HDR_LEN <= ielen) {
		u8 id = ies[off];
		u8 tl = ies[off + 1];
		u32 need = HI5622_IE_HDR_LEN + tl;

		if (off + need > ielen)
			break;	/* stop at the first incomplete IE */
		if (id == 0u && tl <= HI5622_FRAME_MAX_SSID_LEN) {
			out->ssid_len = tl;
			memcpy(out->ssid, ies + off + HI5622_IE_HDR_LEN, tl);
		}
		if (out->ie_len + need <= HI5622_SCAN_MAX_IE) {
			memcpy(out->ie + out->ie_len, ies + off, need);
			out->ie_len = (u16)(out->ie_len + need);
		}
		off += need;
	}
	if (hi5622_ie_channel(ies, ielen, &freq)) {
		/* A 5 GHz probe response may carry only the HT operation IE,
		 * a 2.4 GHz beacon always carries the DS IE; a body with
		 * neither cannot be placed on a channel -> not decodable. */
		return -EOPNOTSUPP;
	}
	out->freq = freq;
	/* The RSSI lives in the [U] descriptor; report UNKNOWN (0 mBm) rather
	 * than inventing a value.  Callers must treat this as [U]. */
	out->signal_mbm = 0;
	return 0;
}
