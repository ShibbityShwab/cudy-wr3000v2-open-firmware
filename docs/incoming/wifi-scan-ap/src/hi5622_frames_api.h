/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_frames_api.h - the narrow frame-provider contract between the
 * cfg80211 scan/AP layer and the H2D payload builders.
 *
 * ORIGINAL clean-room code (wifi-scan-ap lane, shared task task-113,
 * 2026-11-24).  No vendor source, object code, firmware byte, calibration
 * value or secret is reproduced here; the evidence markers and the open
 * payload gaps are recorded in ../REPORT.md.
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * The g0 id0xC scan request (508 B), the g0 id7/id8 user/vap-add (168 B) and
 * the WAL group-2 AP-start frames are still [U] payloads: they are the
 * deliverable of the wifi-vap-scan lane (task-110), not of this lane.  The
 * cfg80211 side must nevertheless be wired NOW so that the builders can be
 * dropped in without touching the cfg80211 code.  Therefore every byte that
 * the firmware will ever receive is produced by ONE provider object that
 * implements this struct; the cfg80211 layer only:
 *
 *   1. fills the neutral parameter structs below from the cfg80211 request,
 *   2. calls the provider builder to obtain a complete HCC wire frame
 *      (12-byte header + payload, tag 0x5A5A, total == len),
 *   3. validates it with the inherited h2d_frames policy library and posts it
 *      through the fail-closed hi5622_frame_post() gate.
 *
 * The provider object is registered at module init (or compiled into the
 * module as one extra object).  With no provider - the committed default -
 * every scan/AP/VAP message op returns -EOPNOTSUPP and never writes a device
 * register.  This is the same "never fake success" discipline as the M2
 * stubs of the inherited wifi-cfg80211 lane.
 *
 * This header is pure C99: it compiles unchanged host-side (tools/
 * scan_logic_selftest.c) and in the 6.18 kernel module.
 */
#ifndef HI5622_FRAMES_API_H
#define HI5622_FRAMES_API_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>	/* snprintf: the portable logic formats status lines */
#include <string.h>
/* Shared with h2d_frames.h (both headers are portable); the guard avoids a
 * C99-pedantic typedef redefinition when a host test includes both. */
#ifndef HI5622_HOST_TYPES_DEFINED
#define HI5622_HOST_TYPES_DEFINED
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int16_t  s16;	/* h2d_frames.h shares this type set */
typedef int32_t  s32;
typedef uint64_t u64;
#endif
#endif

/* ===================================================================
 * 1. Bounds (all values are wire/HCC limits, not kernel limits)
 * =================================================================== */
#define HI5622_FRAME_HDR_LEN		12u
#define HI5622_FRAME_MAX_PAYLOAD	0x674u	/* hcc_msg_alloc bound [V] */
#define HI5622_FRAME_MAX_TOTAL		(HI5622_FRAME_HDR_LEN + HI5622_FRAME_MAX_PAYLOAD)
#define HI5622_FRAME_TAG		0x5a5au	/* transport tag at +0x0A [V] */

#define HI5622_FRAME_MAX_SSIDS		12	/* wiphy->max_scan_ssids [V*] */
#define HI5622_FRAME_MAX_SSID_LEN	32
#define HI5622_FRAME_MAX_CHANNELS	32	/* a single-band wiphy: 13 / 19 */
#define HI5622_FRAME_MAX_SCAN_IE	128	/* cfg80211 max_scan_ie_len cap */
#define HI5622_FRAME_MAX_BCN_HEAD	512
#define HI5622_FRAME_MAX_BCN_TAIL	256
#define HI5622_SCAN_MAX_IE		512	/* IEs reported per BSS */
#define HI5622_FRAME_MAX_USER_ID	0x0ffeu	/* 0x0fff is the del-user wildcard */

/* Provider capability bits.  A builder callback is mandatory exactly when its
 * capability bit is set; hi5622_frame_ops_validate() enforces this. */
#define HI5622_FRAME_CAP_SCAN_REQ	(1u << 0)	/* g0 id0xC builder */
#define HI5622_FRAME_CAP_SCAN_PARSE	(1u << 1)	/* D2H scan result decoder */
#define HI5622_FRAME_CAP_USER_ADD	(1u << 2)	/* g0 id7 builder */
#define HI5622_FRAME_CAP_USER_ADD_NOTIFY (1u << 3)	/* g0 id8 builder */
#define HI5622_FRAME_CAP_USER_DEL	(1u << 4)	/* g0 id9 builder */
#define HI5622_FRAME_CAP_AP_START	(1u << 5)	/* WAL g2 start-AP builder */
#define HI5622_FRAME_CAP_AP_STOP	(1u << 6)	/* WAL g2 stop-AP builder */

#define HI5622_FRAME_CAPS_ALL						\
	(HI5622_FRAME_CAP_SCAN_REQ | HI5622_FRAME_CAP_SCAN_PARSE |	\
	 HI5622_FRAME_CAP_USER_ADD | HI5622_FRAME_CAP_USER_ADD_NOTIFY |	\
	 HI5622_FRAME_CAP_USER_DEL | HI5622_FRAME_CAP_AP_START |	\
	 HI5622_FRAME_CAP_AP_STOP)

/* ===================================================================
 * 2. Neutral parameter structs (cfg80211/netlink -> provider)
 * =================================================================== */

/* One scan request as the provider builder needs it.  n_ssids == 0 is a
 * passive scan; n_channels == 0 means "all supported channels". */
struct hi5622_scan_req_params {
	u8	user_id;	/* user/VAP id selected by the VAP layer [U] */
	u8	iftype;		/* nl80211 iftype of the requesting wdev */
	u32	flags;		/* cfg80211_scan_request.flags (opaque) */
	u32	n_ssids;
	u8	ssid_len[HI5622_FRAME_MAX_SSIDS];
	u8	ssid[HI5622_FRAME_MAX_SSIDS][HI5622_FRAME_MAX_SSID_LEN];
	u32	n_channels;
	u16	freq[HI5622_FRAME_MAX_CHANNELS];	/* MHz centre */
	u32	ie_len;
	u8	ie[HI5622_FRAME_MAX_SCAN_IE];
};

/* One VAP/user add (g0 id7 + g0 id8 notify-alg). */
struct hi5622_vap_add_params {
	char	name[16];	/* IFNAMSIZ-style netdev name */
	u8	mac[6];
	u8	iftype;		/* nl80211 iftype */
	u16	user_id;	/* host-chosen vendor user id, 12-bit [U] */
	u8	vap_id;		/* host-chosen vap id [U] */
};

/* One VAP/user delete (g0 id9 hmac_del_user_notify_dmac, exact 12 B body). */
struct hi5622_vap_del_params {
	u16	user_id;	/* 0x0fff = delete by address [V] */
	u8	mac[6];
};

/* One AP start (WAL cfg group-2 family; the exact message is gap G1). */
struct hi5622_ap_start_params {
	u8	ssid[HI5622_FRAME_MAX_SSID_LEN];
	u8	ssid_len;
	u8	iftype;
	u16	user_id;	/* VAP user id, 12-bit [U] */
	u16	center_freq;	/* chandef.chan->center_freq, MHz */
	u8	width;		/* nl80211_chan_width */
	u16	beacon_interval;	/* TU */
	u16	dtim_period;
	u8	privacy;
	u8	auth_type;
	u32	crypto;	/* cfg80211_crypto_settings bit summary, opaque */
	u16	head_len;
	u16	tail_len;
	u8	head[HI5622_FRAME_MAX_BCN_HEAD];
	u8	tail[HI5622_FRAME_MAX_BCN_TAIL];
};

/* AP stop: identify the VAP; the provider owns the message shape. */
struct hi5622_ap_stop_params {
	u8	iftype;
	u16	user_id;
	u16	center_freq;
};

/* ===================================================================
 * 3. Scan result (provider decoder -> cfg80211 BSS)
 * =================================================================== */
enum hi5622_scan_frame_type {
	HI5622_SCAN_FTYPE_UNKNOWN = 0,	/* driver does not know */
	HI5622_SCAN_FTYPE_BEACON,
	HI5622_SCAN_FTYPE_PRESP,
};

struct hi5622_scan_result {
	u8	bssid[6];
	u8	ssid[HI5622_FRAME_MAX_SSID_LEN];
	u8	ssid_len;
	u8	frame_type;	/* enum hi5622_scan_frame_type */
	u8	reserved[3];
	s32	signal_mbm;	/* dBm * 100, cfg80211 convention */
	u16	freq;		/* MHz centre; 0 = unknown/unusable */
	u16	capability;	/* 802.11 capability field */
	u16	beacon_interval;	/* TU */
	u64	tsf;
	u16	ie_len;
	u8	ie[HI5622_SCAN_MAX_IE];
};

/* Optional scan-complete detail (g1 id16 / g2 id2 family [I]). */
struct hi5622_scan_complete {
	u8	reason;		/* provider-defined completion reason */
	u8	aborted;	/* 1 = scan stopped early */
};

/* ===================================================================
 * 4. The provider object
 * ===================================================================
 *
 * Builder contract: fill *wire with a complete HCC frame (header + payload),
 * set *out_len, return 0.  Return -EOPNOTSUPP when the exact payload is not
 * yet decoded for this message (the honest state today for every builder in
 * this lane); return -EINVAL/-ENOSPC on malformed input/short buffer.  The
 * kernel layer validates the finished frame with the h2d_frames library
 * before any register write, so a builder bug cannot post a malformed frame.
 *
 * Decoder contract: inspect payload (the HCC payload, header stripped) and
 * fill *out; return 0 on success, -EOPNOTSUPP when the event payload layout is
 * not decoded yet, -EINVAL on a malformed body.  A decoder must never read
 * beyond payload+len and must bound every copied field.
 */
struct hi5622_frame_ops {
	const char	*name;		/* dmesg/status identity, never NULL */
	u32		caps;		/* HI5622_FRAME_CAP_* bitmask */

	int (*build_scan_req)(u8 *wire, size_t cap, size_t *out_len,
			      const struct hi5622_scan_req_params *p);
	int (*parse_scan_result)(const u8 *payload, u32 len,
				 struct hi5622_scan_result *out);
	int (*parse_scan_complete)(const u8 *payload, u32 len,
				   struct hi5622_scan_complete *out);
	int (*build_user_add)(u8 *wire, size_t cap, size_t *out_len,
			      const struct hi5622_vap_add_params *p);
	int (*build_user_add_notify)(u8 *wire, size_t cap, size_t *out_len,
				     const struct hi5622_vap_add_params *p);
	int (*build_user_del)(u8 *wire, size_t cap, size_t *out_len,
			      const struct hi5622_vap_del_params *p);
	int (*build_ap_start)(u8 *wire, size_t cap, size_t *out_len,
			      const struct hi5622_ap_start_params *p);
	int (*build_ap_stop)(u8 *wire, size_t cap, size_t *out_len,
			     const struct hi5622_ap_stop_params *p);
};

/* ===================================================================
 * 5. Portable validation helpers (implemented in hi5622_scan_logic.c)
 * =================================================================== */

/* 0 = the provider object is internally consistent and safe to register. */
int hi5622_frame_ops_validate(const struct hi5622_frame_ops *ops);

/* Human-readable capability summary ("scan_req,user_add,..."). */
const char *hi5622_frame_cap_name(u32 cap);
int hi5622_frame_caps_format(u32 caps, char *buf, size_t len);

/* 0 = the parameter struct is internally consistent (bounds, lengths). */
int hi5622_scan_req_params_validate(const struct hi5622_scan_req_params *p);
int hi5622_vap_add_params_validate(const struct hi5622_vap_add_params *p);
int hi5622_vap_del_params_validate(const struct hi5622_vap_del_params *p);
int hi5622_ap_start_params_validate(const struct hi5622_ap_start_params *p);
int hi5622_ap_stop_params_validate(const struct hi5622_ap_stop_params *p);

/* 0 = the decoded scan result is usable for cfg80211 (unicast BSSID, a
 * usable channel/frequency, bounded IEs, sane signal).  Rejects rather than
 * forwarding garbage: cfg80211 BSS entries are visible to every userspace
 * daemon. */
int hi5622_scan_result_validate(const struct hi5622_scan_result *r);

#ifdef __KERNEL__
/* Kernel-only provider registry (implemented in hi5622_frames_provider.c).
 * Exactly one provider object may be registered per module load; an empty
 * registry is the committed default and makes every scan/AP/VAP message op
 * fail closed with -EOPNOTSUPP.  Registration validates the caps/callback
 * pairing; the caller keeps ownership of the object (static storage). */
int hi5622_frame_ops_register(const struct hi5622_frame_ops *ops);
void hi5622_frame_ops_unregister(const struct hi5622_frame_ops *ops);
const struct hi5622_frame_ops *hi5622_frame_ops_get(void);
#endif /* __KERNEL__ */

#endif /* HI5622_FRAMES_API_H */
