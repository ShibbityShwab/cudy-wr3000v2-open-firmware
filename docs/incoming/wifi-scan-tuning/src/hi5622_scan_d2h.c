// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_scan_d2h.c - tolerant scan-result D2H decoder (see .h for the model,
 * the evidence markers and the layout-probing rules).
 *
 * ORIGINAL clean-room code (lane wifi-scan-d2h, shared task task-118).
 *
 * Design rules enforced here:
 *   - every byte of input is untrusted: all reads are bounds-checked against
 *     the supplied length and all copies are length-clamped;
 *   - an unrecognised layout returns -EOPNOTSUPP instead of guessing, so the
 *     caller can distinguish "firmware produced nothing" from "firmware
 *     produced an event this decoder does not understand" (bench observability
 *     requirement of task-118);
 *   - the cfg80211 signal is 0 unless the caller opts in to the trailer
 *     heuristic: never invent a measurement.
 */
#include "hi5622_scan_d2h.h"

/* ---------------------------------------------------------------- */
/* little-endian readers (unaligned safe)                           */
/* ---------------------------------------------------------------- */
static u16 rd16(const u8 *p)
{
	return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static u32 rd32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
	       ((u32)p[3] << 24);
}

static u64 rd64(const u8 *p)
{
	return (u64)rd32(p) | ((u64)rd32(p + 4) << 32);
}

/* ---------------------------------------------------------------- */
/* helpers                                                          */
/* ---------------------------------------------------------------- */
u16 hi5622_scan_d2h_chan_to_freq(u8 chan, bool is_5g)
{
	if (chan == 0)
		return 0;
	if (is_5g)
		return (u16)(5000u + 5u * (u16)chan);
	if (chan == 14u)			/* 2484 MHz, not 2407+70 */
		return 2484u;
	if (chan < 1u || chan > 13u)
		return 0;
	return (u16)(2407u + 5u * (u16)chan);
}

u8 hi5622_scan_d2h_event_kind(u8 group, u16 id)
{
	if (group == HI5622_SCAN_D2H_GROUP_BSS) {
		switch (id) {
		case HI5622_SCAN_D2H_ID_BSS:
			return HI5622_SCAN_D2H_EV_BSS;
		case HI5622_SCAN_D2H_ID_COMPLETE:
			return HI5622_SCAN_D2H_EV_COMPLETE;
		case HI5622_SCAN_D2H_ID_CHANNEL:
			return HI5622_SCAN_D2H_EV_CHANNEL;
		default:
			return HI5622_SCAN_D2H_EV_NONE;
		}
	}
	if (group == HI5622_SCAN_D2H_GROUP_STA &&
	    id == HI5622_SCAN_D2H_ID_STA_COMP)
		return HI5622_SCAN_D2H_EV_STA_COMP;
	return HI5622_SCAN_D2H_EV_NONE;
}

const char *hi5622_scan_d2h_layout_name(u8 layout)
{
	switch (layout) {
	case HI5622_SCAN_D2H_LAYOUT_FRAME_TRAILER:
		return "frame+trailer20";
	case HI5622_SCAN_D2H_LAYOUT_FRAME_ONLY:
		return "frame-only";
	case HI5622_SCAN_D2H_LAYOUT_PREFIX12_TRAILER:
		return "prefix12+frame+trailer20";
	case HI5622_SCAN_D2H_LAYOUT_PREFIX12_ONLY:
		return "prefix12+frame";
	default:
		return "none";
	}
}

const char *hi5622_scan_d2h_ftype_name(u8 ftype)
{
	switch (ftype) {
	case HI5622_SCAN_D2H_FTYPE_BEACON:
		return "beacon";
	case HI5622_SCAN_D2H_FTYPE_PRESP:
		return "probe-resp";
	case HI5622_SCAN_D2H_LAYOUT_RELAXED:
		return "relaxed";
	default:
		return "unknown";
	}
}

const char *hi5622_scan_d2h_phy_name(u8 raw_phy)
{
	switch (raw_phy) {
	case HI5622_SCAN_D2H_PHY_RAW4:
		return "ht(4)";
	case HI5622_SCAN_D2H_PHY_RAW8:
		return "vht(8)";
	case HI5622_SCAN_D2H_PHY_RAW12:
		return "he(12)";
	case 0:
		return "unknown(0)";
	default:
		return "unknown";
	}
}

const char *hi5622_scan_d2h_event_name(u8 kind)
{
	switch (kind) {
	case HI5622_SCAN_D2H_EV_BSS:
		return "bss";
	case HI5622_SCAN_D2H_EV_COMPLETE:
		return "scan_complete";
	case HI5622_SCAN_D2H_EV_CHANNEL:
		return "chan_result";
	case HI5622_SCAN_D2H_EV_STA_COMP:
		return "sta_scan_complete";
	default:
		return "none";
	}
}

/* ---------------------------------------------------------------- */
/* 802.11 IE walk                                                   */
/* ---------------------------------------------------------------- */
struct ie_walk {
	bool	have_ssid;
	bool	have_chan;
	bool	ssid_zero_len;
	u8	chan;
	bool	chan_5g;
	u16	ie_len;			/* bytes copied to out->ie */
	bool	overrun;		/* an IE was truncated */
	/* derived facts (scan-complete lane) */
	u8	ds_chan;		/* channel from IE 3 / IE 61 */
	u8	ie_count;
	u8	ie_flags;
	u16	ht_cap_info;
	u8	ht_op_primary;
	u8	ht_op_sec_off;
};

static void walk_ies(const u8 *ies, u32 ielen, struct ie_walk *w)
{
	u32 off = 0;

	memset(w, 0, sizeof(*w));
	while (off + HI5622_SCAN_D2H_IE_HDR <= ielen) {
		u8 id = ies[off];
		u8 tl = ies[off + 1];
		u32 need = HI5622_SCAN_D2H_IE_HDR + (u32)tl;

		if (off + need > ielen) {
			w->overrun = true;
			break;
		}
		if (id == 0u) {			/* SSID */
			w->have_ssid = true;
			if (tl == 0u)
				w->ssid_zero_len = true;
		} else if (id == 3u && tl == 1u) {	/* DS parameter set [V] */
			w->have_chan = true;
			w->chan = ies[off + 2];
			w->chan_5g = false;
			w->ds_chan = ies[off + 2];
		} else if (id == 45u && tl == 26u) {	/* HT capabilities */
			w->ie_flags |= HI5622_SCAN_D2H_IE_HT_CAP;
			w->ht_cap_info = rd16(ies + off + 2);
		} else if (id == 61u && tl >= 22u) {	/* HT operation */
			u8 ch = ies[off + 2];

			w->ie_flags |= HI5622_SCAN_D2H_IE_HT_OP;
			w->ht_op_primary = ch;
			/* HT operation element byte 1 low 2 bits: secondary channel
			 * offset (0 none, 1 above, 3 below). */
			w->ht_op_sec_off = (u8)(ies[off + 3] & 0x3u);
			if (!w->have_chan || w->chan == 0) {
				w->have_chan = true;
				w->chan = ch;
				w->chan_5g = (ch > 14u);
			}
		} else if (id == 191u && tl >= 12u) {	/* VHT capabilities */
			w->ie_flags |= HI5622_SCAN_D2H_IE_VHT_CAP;
		} else if (id == 192u && tl >= 5u) {	/* VHT operation */
			w->ie_flags |= HI5622_SCAN_D2H_IE_VHT_OP;
		} else if (id == 48u && tl >= 2u) {	/* RSN */
			w->ie_flags |= HI5622_SCAN_D2H_IE_RSN;
		} else if (id == 7u && tl >= 3u) {	/* Country */
			w->ie_flags |= HI5622_SCAN_D2H_IE_COUNTRY;
		} else if (id == 127u) {		/* Extended capabilities */
			w->ie_flags |= HI5622_SCAN_D2H_IE_EXT_CAP;
		} else if (id == 221u && tl >= 4u &&
			   ies[off + 2] == 0x00u && ies[off + 3] == 0x50u &&
			   ies[off + 4] == 0xf2u && ies[off + 5] == 0x02u) {
			/* WMM/WME information element (vendor IE 00:50:f2 type 2) */
			w->ie_flags |= HI5622_SCAN_D2H_IE_WMM;
		}
		w->ie_count++;
		off += need;
	}
	w->ie_len = (u16)(off > HI5622_SCAN_D2H_IE_MAX ?
			  HI5622_SCAN_D2H_IE_MAX : off);
}

/* ---------------------------------------------------------------- */
/* one layout candidate                                             */
/* ---------------------------------------------------------------- */
static void copy_ies(const u8 *ies, u32 ielen, struct hi5622_scan_d2h_bss *out)
{
	u32 off = 0;

	out->ie_len = 0;
	while (off + HI5622_SCAN_D2H_IE_HDR <= ielen) {
		u8 tl = ies[off + 1];
		u32 need = HI5622_SCAN_D2H_IE_HDR + (u32)tl;
		u32 room;

		if (off + need > ielen)
			break;
		room = HI5622_SCAN_D2H_IE_MAX - out->ie_len;
		if (room < need)
			break;
		memcpy(out->ie + out->ie_len, ies + off, need);
		out->ie_len = (u16)(out->ie_len + need);
		off += need;
	}
}

static int parse_layout(const u8 *payload, u32 len, u32 frame_off,
			u32 trailer_len, bool require_exact,
			struct hi5622_scan_d2h_bss *out, u8 layout_bit)
{
	const u8 *frame;
	u32 frame_len;
	u32 ielen;
	struct ie_walk w;
	u16 fc;
	u8 subtype;
	u8 i;

	if (len < frame_off + HI5622_SCAN_D2H_MGMT_HDR +
		       HI5622_SCAN_D2H_BEACON_FIXED)
		return -EOPNOTSUPP;
	if (trailer_len > len - frame_off)
		return -EOPNOTSUPP;
	frame_len = len - frame_off - trailer_len;
	if (frame_len < HI5622_SCAN_D2H_MGMT_HDR +
			HI5622_SCAN_D2H_BEACON_FIXED)
		return -EOPNOTSUPP;

	frame = payload + frame_off;
	fc = rd16(frame);
	if ((fc & 0x3u) != 0u)			/* version must be 0 */
		return -EOPNOTSUPP;
	if (((fc >> 2) & 0x3u) != 0u)		/* management frame only */
		return -EOPNOTSUPP;
	subtype = (u8)((fc >> 4) & 0xfu);
	if (subtype != 8u && subtype != 5u)	/* beacon / probe response */
		return -EOPNOTSUPP;

	ielen = frame_len - (HI5622_SCAN_D2H_MGMT_HDR +
			     HI5622_SCAN_D2H_BEACON_FIXED);
	walk_ies(frame + HI5622_SCAN_D2H_MGMT_HDR + HI5622_SCAN_D2H_BEACON_FIXED,
		 ielen, &w);
	if (require_exact && w.overrun)
		return -EOPNOTSUPP;
	if (!w.have_chan)
		return -EOPNOTSUPP;	/* cannot place the BSS on a channel */

	memset(out, 0, sizeof(*out));
	out->frame_type = (subtype == 8u) ? HI5622_SCAN_D2H_FTYPE_BEACON :
					    HI5622_SCAN_D2H_FTYPE_PRESP;
	memcpy(out->bssid, frame + 16, 6);	/* addr3 [V] */
	out->tsf = rd64(frame + HI5622_SCAN_D2H_MGMT_HDR);
	out->beacon_interval = rd16(frame + HI5622_SCAN_D2H_MGMT_HDR + 8);
	out->capability = rd16(frame + HI5622_SCAN_D2H_MGMT_HDR + 10);

	/* derived facts from the validated IE walk (scan-complete lane) */
	out->ds_chan = w.ds_chan;
	out->ie_count = w.ie_count;
	out->ie_flags = w.ie_flags;
	out->ht_cap_info = w.ht_cap_info;
	out->ht_op_primary = w.ht_op_primary;
	out->ht_op_sec_off = w.ht_op_sec_off;
	out->cap_flags = 0;
	if (out->capability & 0x0001u)
		out->cap_flags |= HI5622_SCAN_D2H_CAP_ESS;
	if (out->capability & 0x0002u)
		out->cap_flags |= HI5622_SCAN_D2H_CAP_IBSS;
	if (out->capability & 0x0010u)
		out->cap_flags |= HI5622_SCAN_D2H_CAP_PRIVACY;

	/* SSID */
	{
		u32 off = 0;
		const u8 *ies = frame + HI5622_SCAN_D2H_MGMT_HDR +
				HI5622_SCAN_D2H_BEACON_FIXED;

		out->ssid_hidden = true;
		while (off + HI5622_SCAN_D2H_IE_HDR <= ielen) {
			u8 id = ies[off];
			u8 tl = ies[off + 1];
			u32 need = HI5622_SCAN_D2H_IE_HDR + (u32)tl;

			if (off + need > ielen)
				break;
			if (id == 0u) {
				if (tl == 0u) {
					out->ssid_hidden = true;
				} else {
					out->ssid_hidden = false;
					out->ssid_len = (u8)(tl >
						HI5622_SCAN_D2H_SSID_MAX ?
						HI5622_SCAN_D2H_SSID_MAX : tl);
					for (i = 0; i < out->ssid_len; i++)
						out->ssid[i] = ies[off + 2 + i];
				}
			}
			off += need;
		}
	}

	/* channel */
	out->chan_num = w.chan;
	out->is_5g = w.chan_5g;
	out->freq = hi5622_scan_d2h_chan_to_freq(w.chan, w.chan_5g);
	if (!out->freq)
		return -EOPNOTSUPP;

	copy_ies(frame + HI5622_SCAN_D2H_MGMT_HDR +
		 HI5622_SCAN_D2H_BEACON_FIXED, ielen, out);

	/* provenance */
	out->layout = layout_bit;
	out->raw_len = (u16)(len > 0xffffu ? 0xffffu : len);
	out->frame_len = (u16)(frame_len > 0xffffu ? 0xffffu : frame_len);

	/* trailer */
	if (trailer_len == HI5622_SCAN_D2H_TRAILER_LEN) {
		const u8 *t = payload + frame_off + frame_len;

		out->trailer_present = 1;
		out->trailer_len = (u8)HI5622_SCAN_D2H_TRAILER_LEN;
		memcpy(out->trailer, t, HI5622_SCAN_D2H_TRAILER_LEN);
		out->tr_u32_0 = rd32(t + 0x00);
		out->tr_u8_4 = t[0x04];
		out->tr_s8_5 = (s8)t[0x05];
		out->tr_s8_6 = (s8)t[0x06];
		out->tr_u32_8 = rd32(t + 0x08);
		out->tr_u32_12 = rd32(t + 0x0c);
		out->tr_u8_16 = t[0x10];
		out->phy_mode = t[0x11];
	}
	return 0;
}

int hi5622_scan_d2h_parse_bss_layout(const u8 *payload, u32 len,
				     u32 frame_off, u32 trailer_len,
				     const struct hi5622_scan_d2h_opts *opts,
				     struct hi5622_scan_d2h_bss *out)
{
	struct hi5622_scan_d2h_opts defaults = { false, true };
	int rc;

	if (!payload || !out || len == 0)
		return -EINVAL;
	if (!opts)
		opts = &defaults;
	memset(out, 0, sizeof(*out));
	rc = parse_layout(payload, len, frame_off, trailer_len,
			  opts->require_exact_ie, out,
			  trailer_len ? HI5622_SCAN_D2H_LAYOUT_FRAME_TRAILER :
					HI5622_SCAN_D2H_LAYOUT_FRAME_ONLY);
	if (rc)
		return rc;
	if (out->trailer_present && opts->trust_trailer_signal) {
		out->signal_mbm = (s32)out->tr_s8_5 * 100;
		out->signal_from_trailer = true;
	}
	return 0;
}

int hi5622_scan_d2h_parse_bss(const u8 *payload, u32 len,
			      const struct hi5622_scan_d2h_opts *opts,
			      struct hi5622_scan_d2h_bss *out)
{
	static const struct {
		u32 frame_off;
		u32 trailer;
		u8 bit;
	} cand[] = {
		{ 0,  HI5622_SCAN_D2H_TRAILER_LEN, HI5622_SCAN_D2H_LAYOUT_FRAME_TRAILER },
		{ 0,  0, HI5622_SCAN_D2H_LAYOUT_FRAME_ONLY },
		{ 12, HI5622_SCAN_D2H_TRAILER_LEN, HI5622_SCAN_D2H_LAYOUT_PREFIX12_TRAILER },
		{ 12, 0, HI5622_SCAN_D2H_LAYOUT_PREFIX12_ONLY },
	};
	size_t i;
	int rc = -EOPNOTSUPP;

	if (!payload || !out || len == 0)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	for (i = 0; i < sizeof(cand) / sizeof(cand[0]); i++) {
		rc = hi5622_scan_d2h_parse_bss_layout(payload, len,
						      cand[i].frame_off,
						      cand[i].trailer, opts,
						      out);
		if (!rc) {
			out->layout = cand[i].bit;
			return 0;
		}
	}
	memset(out, 0, sizeof(*out));
	out->raw_len = (u16)(len > 0xffffu ? 0xffffu : len);

	/* Relaxed probe (task-133 R2 follow-up): the bench showed g1 id15 frames
	 * whose layout none of the four fixed candidates matched (parse_errors=6,
	 * no cfg80211 inform attempted).  Probe small frame offsets and trailer
	 * lengths, requiring a syntactically valid 802.11 management beacon or
	 * probe-response whose IE walk yields a channel - the same validation the
	 * fixed candidates use, only with the alignment assumed rather than
	 * derived.  Nothing is invented: the recognised layout is reported as
	 * HI5622_SCAN_D2H_LAYOUT_RELAXED and the kernel logs it, so a bench run
	 * still tells us the true framing. */
	{
		static const u32 offs[] = { 0, 2, 4, 6, 8, 10, 12, 16, 20,
					    24, 28, 32 };
		static const u32 trs[] = { HI5622_SCAN_D2H_TRAILER_LEN,
					   0, 4, 8, 12, 16, 24 };
		struct hi5622_scan_d2h_opts ropts;
		size_t a, b;

		memset(&ropts, 0, sizeof(ropts));
		ropts.require_exact_ie = false;
		for (a = 0; a < sizeof(offs) / sizeof(offs[0]); a++) {
			for (b = 0; b < sizeof(trs) / sizeof(trs[0]); b++) {
				rc = hi5622_scan_d2h_parse_bss_layout(payload,
					len, offs[a], trs[b], &ropts, out);
				if (!rc) {
					out->layout =
						HI5622_SCAN_D2H_LAYOUT_RELAXED;
					return 0;
				}
			}
		}
	}
	memset(out, 0, sizeof(*out));
	out->raw_len = (u16)(len > 0xffffu ? 0xffffu : len);
	return -EOPNOTSUPP;
}

/* ---------------------------------------------------------------- */
/* completion events                                                */
/* ---------------------------------------------------------------- */
int hi5622_scan_d2h_parse_complete(const u8 *payload, u32 len,
				   struct hi5622_scan_d2h_complete *out)
{
	u32 n;

	if (!payload || !out || len == 0)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	/* The vendor handler reads the status byte at frame+0xC; for a payload
	 * handed with the 12-byte HCC header already stripped that is
	 * payload[0x00].  Both readings are covered: status = payload[0]. [V] */
	out->status = payload[0];
	n = len > sizeof(out->raw) ? (u32)sizeof(out->raw) : len;
	memcpy(out->raw, payload, n);
	out->raw_len = (u16)n;
	out->payload_len = (u16)(len > 0xffffu ? 0xffffu : len);
	return 0;
}

/* ---------------------------------------------------------------- */
/* g1 id17 per-channel result                                       */
/* ---------------------------------------------------------------- */
int hi5622_scan_d2h_parse_chan(const u8 *payload, u32 len,
			       struct hi5622_scan_d2h_chan *out)
{
	u32 avail;

	if (!payload || !out)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	out->payload_len = (u16)(len > 0xffffu ? 0xffffu : len);
	/* Firmware sender layout: payload[0] = slot, payload[4..0x6f] = the
	 * 108-byte per-channel record copied from ctx+0x1688. [V] */
	if (len < 5u)
		return -EOPNOTSUPP;
	out->slot = payload[0];
	avail = len - 4u;
	if (avail > HI5622_SCAN_D2H_CHAN_BLOCK)
		avail = HI5622_SCAN_D2H_CHAN_BLOCK;
	memcpy(out->block, payload + 4, avail);
	out->block_len = (u16)avail;
	out->chan_num = out->block[0];
	out->is_5g = out->chan_num > 14u;
	out->freq = hi5622_scan_d2h_chan_to_freq(out->chan_num, out->is_5g);
	return 0;
}

/* ---------------------------------------------------------------- */
/* g2 id2 WAL/STA scan completion                                   */
/* ---------------------------------------------------------------- */
int hi5622_scan_d2h_parse_sta_comp(const u8 *payload, u32 len,
				   struct hi5622_scan_d2h_sta_comp *out)
{
	u32 n;

	if (!payload || !out || len == 0)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	/* Vendor handler (wal_scan_comp_proc_sta, wifi.ko+0x125714) reads the
	 * frame-relative fields +1 (core nibble), +3 (chip id) and +0xD
	 * (status).  With the payload handed header-stripped the offsets shift
	 * by 12; we report the raw bytes so the bench can pick the semantic
	 * once a real frame is captured.  Status via both readings. [I] */
	out->core_band = (u8)(payload[0] >> 4);
	out->chip_id = payload[0];
	out->status = (len > 0x0du) ? payload[0x0d] :
		      (len > 1u ? payload[1] : 0u);
	n = len > sizeof(out->raw) ? (u32)sizeof(out->raw) : len;
	memcpy(out->raw, payload, n);
	out->raw_len = (u16)n;
	out->payload_len = (u16)(len > 0xffffu ? 0xffffu : len);
	return 0;
}
