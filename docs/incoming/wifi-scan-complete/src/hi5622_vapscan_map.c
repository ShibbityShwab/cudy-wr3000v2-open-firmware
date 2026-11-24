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
#include "hi5622_scan_d2h.h"

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
	/* dwell override (scan-complete lane): 0 keeps the vendor default 60;
	 * a bench sweep may set 20/40 to shorten the per-channel cost. */
	if (p->dwell_ms)
		sp.dwell_ms = p->dwell_ms;
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
/* vendor-verified D2H decoders (wifi-scan-d2h lane, task-118)         */
/* ------------------------------------------------------------------ */
int hi5622_vapscan_decode_scan_result_opts(const u8 *payload, u32 len,
						   struct hi5622_scan_result *out,
						   bool trust_trailer_signal)
{
	struct hi5622_scan_d2h_bss b;
	struct hi5622_scan_d2h_opts opts = {
		.trust_trailer_signal	= trust_trailer_signal,
		.require_exact_ie	= true,
	};
	int rc;

	if (!out)
		return -EINVAL;
	/* Tolerant layout probe: 802.11 body + optional 20-byte trailer, plus
	 * bare-frame and prefix variants; returns -EOPNOTSUPP instead of
	 * guessing.  Signal stays 0 unless the caller opts in (trailer RSSI is
	 * [I]; bench correlation pending - scan-complete REPORT sec 4.4). */
	rc = hi5622_scan_d2h_parse_bss(payload, len, &opts, &b);
	if (rc)
		return rc;
	memset(out, 0, sizeof(*out));
	memcpy(out->bssid, b.bssid, sizeof(out->bssid));
	memcpy(out->ssid, b.ssid, b.ssid_len);
	out->ssid_len = b.ssid_len;
	out->frame_type = (b.frame_type == HI5622_SCAN_D2H_FTYPE_PRESP) ?
			  HI5622_SCAN_FTYPE_PRESP : HI5622_SCAN_FTYPE_BEACON;
	out->signal_mbm = b.signal_mbm;
	out->freq = b.freq;
	out->capability = b.capability;
	out->beacon_interval = b.beacon_interval;
	out->tsf = b.tsf;
	out->ds_chan = b.ds_chan;
	out->ie_count = b.ie_count;
	out->cap_flags = b.cap_flags;
	out->ie_flags = b.ie_flags;
	out->ht_cap_info = b.ht_cap_info;
	out->ie_len = b.ie_len;
	memcpy(out->ie, b.ie, b.ie_len);
	return 0;
}

int hi5622_vapscan_decode_scan_result(const u8 *payload, u32 len,
				      struct hi5622_scan_result *out)
{
	return hi5622_vapscan_decode_scan_result_opts(payload, len, out, false);
}

int hi5622_vapscan_decode_scan_complete(const u8 *payload, u32 len,
					struct hi5622_scan_complete *out)
{
	struct hi5622_scan_d2h_complete cc;
	int rc;

	if (!out)
		return -EINVAL;
	rc = hi5622_scan_d2h_parse_complete(payload, len, &cc);
	if (rc)
		return rc;
	out->reason = cc.status;
	/* Only 'success == 1' is proven [V]; everything else is reported as an
	 * abort so cfg80211 never sees a fake successful scan. */
	out->aborted = (cc.status != 1u) ? 1u : 0u;
	return 0;
}

int hi5622_vapscan_decode_scan_channel(const u8 *payload, u32 len,
				       struct hi5622_scan_channel *out)
{
	struct hi5622_scan_d2h_chan ch;
	int rc;

	if (!out)
		return -EINVAL;
	rc = hi5622_scan_d2h_parse_chan(payload, len, &ch);
	if (rc)
		return rc;
	memset(out, 0, sizeof(*out));
	out->slot = ch.slot;
	out->chan_num = ch.chan_num;
	out->is_5g = ch.is_5g ? 1u : 0u;
	out->freq = ch.freq;
	return 0;
}

int hi5622_vapscan_decode_scan_sta_complete(const u8 *payload, u32 len,
					    struct hi5622_scan_sta_complete *out)
{
	struct hi5622_scan_d2h_sta_comp sc;
	int rc;

	if (!out)
		return -EINVAL;
	rc = hi5622_scan_d2h_parse_sta_comp(payload, len, &sc);
	if (rc)
		return rc;
	memset(out, 0, sizeof(*out));
	out->status = sc.status;
	out->core_band = sc.core_band;
	out->chip_id = sc.chip_id;
	return 0;
}
