/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_ap_frames.h - AP start frame builders (Hi5622V100 clean-room,
 * Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (wifi-ap-start lane, shared task task-121,
 * 2026-11-26).  Derived from static reversal of the local vendor host module
 * (sha256 de78ec07... 2.5.24) and the local vendor firmware blob
 * (sha256 7fc87e20...); only derived addresses/constants/field offsets and
 * original code appear here - no vendor source, object byte, firmware byte,
 * calibration value or secret is reproduced.  Evidence: ../REPORT.md,
 * ../TABLES.md.
 *
 * WHAT THIS BUILDS
 * ----------------
 * The vendor AP-start path (wal_cfg80211_start_ap, vendor .text 0x103634 /
 * 0x103548 in the 2.5.24 host module) is a SEQUENCE, not one frame:
 *
 *   1. WAL cfg cmd 0x0005  set SSID    body 36  {u8 len, u8 pad[2], ssid[33]}
 *   2. WAL cfg cmd 0x006D  auth algs   body  4  {u32 nl80211_auth_type}
 *   3. WAL cfg cmd 0x0127  AP config   body 156 (beacon/PHY/RSN/WMM blob)
 *   4. WAL cfg cmd 0x0065  VAP start   body 12  {u8 start=1, ..., u32 cookie}
 *
 * The firmware dispatches config bodies through its static config-key table
 * A (CA 0x10C23C, 217 x {u16 key; u16 pad; u32 worker}): key 0x0005 ->
 * worker 0x82878, key 0x0127 -> worker 0x84DA4.  The vendor host emits them
 * as HCC group-2 WAL cfg events (header id 0).  This library therefore emits
 * BOTH carriers:
 *
 *   HAF_CARRIER_G0_ID1  - group-0 id-1 config events keyed 0x0005 / 0x0127.
 *                         These hit table A statically (bench-proven carrier:
 *                         the C2 ctx-create / TXQ-sync frames ride it).
 *   HAF_CARRIER_WAL_G2  - the vendor exact group-2 WAL cfg-event envelope
 *                         (cmd 0x05 / 0x6D / 0x127 / 0x65).  Bench-proven
 *                         envelope (mode-0 VAP add/start were consumed), but
 *                         on this firmware build group-2 header id 0 has no
 *                         STATIC receive-table slot, so the G0 carrier is the
 *                         default ladder path.
 *
 * AP CONFIG BODY (156 B) - every offset is [V] firmware-verified against the
 * table-A worker 0x84DA4 and [V] host-verified against the vendor builder:
 *
 *    +0x00 u32 beacon_interval     -> VAP ctx +0x10 (worker 0x84DA4)
 *    +0x04 u32 dtim_period         -> VAP ctx +0x14
 *    +0x08 u8  rsn/wmm-present     (gates the two IE copies below)
 *    +0x09..0x48 (64 B) RSN IE (element 0x30) copy, id at +0x09
 *    +0x49..0x88 (64 B) vendor IE (OUI 00:50:f2, WMM) copy, id 0xDD at +0x49
 *    +0x89 u8  hidden_ssid         (1 = zero-length SSID) -> ctx +0x18D bit0
 *    +0x8a u8  HT cap payload[0] bit5 (greenfield)
 *    +0x8b u8  HT cap payload[0] bit6 (short GI 20) when the band hint is 5G
 *    +0x8c u8  VHT cap payload[0] bit5 (short GI 80)   (5G band hint only)
 *    +0x8d u8  VHT cap payload[0] bit6 (short GI 160)  (5G band hint only)
 *    +0x8e u8  wireless mode enum: 5 = HT, 6 = VHT, 10 = HE/ext, 0..4 legacy
 *    +0x8f..0x92 reserved (zero; no firmware reader)
 *    +0x93 u8  start flag: 1 = start_ap, 0 = change_beacon (vendor builder)
 *    +0x94 u16 MDID        (mobility-domain IE 0x36 payload[0..1])
 *    +0x96 u8  MD present  -> ctx +0x138 bit0
 *    +0x97 u8  MD payload[2] bit0 -> ctx +0x138 bit1
 *    +0x98 u8  MD payload[2] bit1 -> ctx +0x138 bit2
 *    +0x99..0x9B reserved (zero; no firmware reader)
 *
 * The SSID, auth algorithm and RSN/WMM IEs are NOT in the 156-byte body - the
 * vendor sends the first two as separate events (1./2.) and the IEs ride in
 * the RSN/vendor copies above.  This matters for hostapd: its WPA2-PSK
 * NL80211_CMD_NEW_BEACON yields auth_type = OPEN (0), privacy set, and the
 * RSN IE in the beacon tail.
 *
 * Pure C99: host tests and the 6.18 kernel module compile the same code.
 */
#ifndef HI5622_AP_FRAMES_H
#define HI5622_AP_FRAMES_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#ifndef HI5622_HOST_TYPES_DEFINED
#define HI5622_HOST_TYPES_DEFINED
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef uint64_t u64;
#endif
#endif

/* ---- wire/HCC bounds (inherited, [V]) ---- */
#define HAF_HCC_HDR_LEN		12u
#define HAF_HCC_TAG		0x5a5au
#define HAF_HCC_GROUP_CHIP	0u
#define HAF_HCC_GROUP_CORE	2u
#define HAF_HCC_ID_CFG_EVENT	1u
#define HAF_ALLOC_NIBBLE	0x01u

/* g0 id1 config event: hdr[12] token[8] key[2] len[2] body[len] pad[4] */
#define HAF_G0_OVERHEAD		28u
/* WAL group-2 cfg event: hdr[12] cookie[4] wal{id,pad,len,seq}[12]
 *                       {cmd,len}[4] body[n] */
#define HAF_WAL_OVERHEAD	32u
#define HAF_WAL_MSG_CFG		1u

/* WAL cfg command ids [V] vendor builder .text */
#define HAF_WAL_CMD_SSID	0x0005u
#define HAF_WAL_CMD_AUTH	0x006du
#define HAF_WAL_CMD_AP_CFG	0x0127u
#define HAF_WAL_CMD_VAP_START	0x0065u
/* static-dispatch config keys (firmware table A rows) [V] */
#define HAF_G0_KEY_SSID		0x0005u
#define HAF_G0_KEY_AP_CFG	0x0127u

/* body sizes [V] */
#define HAF_SSID_BODY_LEN	36u
#define HAF_AUTH_BODY_LEN	4u
#define HAF_AP_CFG_BODY_LEN	156u
#define HAF_VAP_START_BODY_LEN	12u

#define HAF_MAX_SSID		32u
#define HAF_MAX_HEAD		512u
#define HAF_MAX_TAIL		256u
#define HAF_MAX_FRAME		2048u
#define HAF_MAX_SEQ_FRAMES	4u

/* nl80211_auth_type values the vendor worker accepts [V]:
 * OPEN_SYSTEM=0, SHARED_KEY=1, SAE=4, AUTOMATIC=8. */
#define HAF_AUTH_OPEN		0u
#define HAF_AUTH_SHARED		1u
#define HAF_AUTH_SAE		4u
#define HAF_AUTH_AUTOMATIC	8u

/* 802.11 element ids the vendor builder parses [V] */
#define HAF_IE_SSID		0u
#define HAF_IE_RATES		1u
#define HAF_IE_MD		54u	/* 0x36 mobility domain */
#define HAF_IE_RSN		48u	/* 0x30 */
#define HAF_IE_HT_CAP		45u	/* 0x2d */
#define HAF_IE_VHT_CAP		191u	/* 0xbf */
#define HAF_IE_EXT		255u	/* 0xff */
#define HAF_IE_VENDOR		0xddu
#define HAF_WMM_OUI_LO16	0x50f2u	/* OUI 00:50:f2 */
#define HAF_IE_COPY_MAX		64u	/* body IE copy slots */

enum haf_carrier {
	HAF_CARRIER_G0_ID1 = 0,	/* group-0 id-1 config events (static table A) */
	HAF_CARRIER_WAL_G2 = 1,	/* vendor group-2 WAL cfg events */
};

enum haf_frame_id {
	HAF_FRAME_SSID = 0,
	HAF_FRAME_AUTH = 1,
	HAF_FRAME_AP_CFG = 2,
	HAF_FRAME_VAP_START = 3,
	HAF_FRAME_COUNT = 4,
};

/* One AP start request in the neutral form the builders need. */
struct haf_ap_params {
	u8	carrier;	/* enum haf_carrier */
	u8	dst;		/* HCC dst nibble, 0 for these frames [V*] */
	u8	module;		/* g0 hdr[3] / VAP module index (0..0x12) */
	u8	vap_id;		/* WAL hdr[3] VAP id for this frame class */
	u8	iftype;		/* 2 = AP for the WAL VAP-add family */
	u8	band;		/* 0 = 2.4 GHz, 1 = 5 GHz (vendor band hint) */
	u8	hidden_ssid;	/* 1 = hide SSID (zero-length SSID element) */
	u8	auth_algs;	/* nl80211_auth_type, allowlist above */
	u8	start;		/* 1 = start_ap, 0 = change_beacon body */
	u8	ssid_len;
	u8	ssid[HAF_MAX_SSID];
	u16	beacon_interval;	/* TU */
	u16	dtim_period;
	u16	center_freq1;	/* chandef center freq, MHz (info only) */
	u16	center_freq2;	/* 80+80 second segment, 0 otherwise */
	u8	width;		/* nl80211_chan_width (info only) */
	u16	head_len;
	u16	tail_len;
	u8	head[HAF_MAX_HEAD];	/* cfg80211 beacon head (fixed + IEs) */
	u8	tail[HAF_MAX_TAIL];	/* cfg80211 beacon tail (HT/VHT/RSN/WMM/MD) */
};

/* One built wire frame. */
struct haf_frame {
	u8	buf[HAF_MAX_FRAME];
	size_t	len;
	u8	id;		/* enum haf_frame_id */
};

/* 0 = parameter struct is structurally usable (bounds/lengths/values). */
int haf_ap_params_validate(const struct haf_ap_params *p);

/* Fill the 156-byte AP-config body.  The caller supplies a buffer of at
 * least HAF_AP_CFG_BODY_LEN bytes. */
int haf_build_body_ap_cfg(u8 *body, size_t cap,
			  const struct haf_ap_params *p);

/* Fill the 36-byte set-SSID body (len byte + 33-byte SSID slot). */
int haf_build_body_ssid(u8 *body, size_t cap, const struct haf_ap_params *p);

/* Fill the 4-byte auth body (u32 auth type, checked against the allowlist). */
int haf_build_body_auth(u8 *body, size_t cap,
			const struct haf_ap_params *p);

/* Fill the 12-byte VAP-start body: [0]=1 (start), [8..11]=cookie (0 here). */
int haf_build_body_vap_start(u8 *body, size_t cap,
			     const struct haf_ap_params *p);

/* Build one complete wire frame of the given family member.  Returns 0,
 * -EINVAL/-ENOSPC on bad input/short buffer, -EOPNOTSUPP when the member
 * does not exist for the selected carrier (e.g. auth has no static g0 key). */
int haf_build_frame(struct haf_frame *f, enum haf_frame_id id,
		    const struct haf_ap_params *p);

/* Sequence order per carrier -> frame id (0-based index).  Returns 0 with
 * *out_id set, or -ENODATA past the end of the sequence. */
int haf_sequence_at(unsigned int index, u8 carrier, enum haf_frame_id *out_id);

/* Build the full ordered sequence (up to HAF_MAX_SEQ_FRAMES).  *count is set
 * to the number of frames written.  Returns 0 on success. */
int haf_build_sequence(struct haf_frame *frames, unsigned int max,
		       const struct haf_ap_params *p, unsigned int *count);

/* Stable names ("ap_ssid", "ap_auth", "ap_cfg", "ap_vap_start"). */
const char *haf_frame_name(enum haf_frame_id id);
const char *haf_carrier_name(u8 carrier);

/* Structural validator for a built wire frame: header/tag/total/length
 * consistency and the per-carrier allowlist.  0 = OK. */
int haf_validate_wire(const u8 *wire, size_t len);

#endif /* HI5622_AP_FRAMES_H */
