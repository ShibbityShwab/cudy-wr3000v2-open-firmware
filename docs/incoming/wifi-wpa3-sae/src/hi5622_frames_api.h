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
/* extra caps (wifi-vap-start lane, task-117): the VAP start family that
 * must run before the firmware accepts a scan request / returns results. */
#define HI5622_FRAME_CAP_VAP_ADD	(1u << 7)	/* VAP add builder */
#define HI5622_FRAME_CAP_VAP_START	(1u << 8)	/* VAP start builder */
/* wifi-beacon-static lane (task-140): the vendor AP-start path configures the
 * module channel BEFORE the SSID/AP-config frames, so the firmware's module
 * channel context is programmed when the AP VAP starts beaconing. */
#define HI5622_FRAME_CAP_CHAN_CFG	(1u << 9)	/* channel config builder */
/* WPA2 key path (wifi-wpa-static lane, task-139): the three firmware g0 id1
 * key events decoded from the vendor module + firmware workers.  The cap bits
 * are renumbered above the beacon/scan bits (the lane patches used bit 9
 * independently; one bitmask must be shared, so the key family moves to
 * 12..14 - no on-wire or ABI meaning, internal provider capability only). */
#define HI5622_FRAME_CAP_KEY_ADD	(1u << 12)	/* cmd 0x0078 builder */
#define HI5622_FRAME_CAP_KEY_DEL	(1u << 13)	/* cmd 0x007A builder */
#define HI5622_FRAME_CAP_KEY_DEFAULT	(1u << 14)	/* cmd 0x0079 builder */

#define HI5622_FRAME_CAPS_ALL						\
	(HI5622_FRAME_CAP_SCAN_REQ | HI5622_FRAME_CAP_SCAN_PARSE |	\
	 HI5622_FRAME_CAP_USER_ADD | HI5622_FRAME_CAP_USER_ADD_NOTIFY |	\
	 HI5622_FRAME_CAP_USER_DEL | HI5622_FRAME_CAP_AP_START |	\
	 HI5622_FRAME_CAP_AP_STOP | HI5622_FRAME_CAP_VAP_ADD |	\
	 HI5622_FRAME_CAP_VAP_START | HI5622_FRAME_CAP_CHAN_CFG | \
	 HI5622_FRAME_CAP_KEY_ADD | HI5622_FRAME_CAP_KEY_DEL | \
	 HI5622_FRAME_CAP_KEY_DEFAULT)

/* ===================================================================
 * 2. Neutral parameter structs (cfg80211/netlink -> provider)
 * =================================================================== */

/* One scan request as the provider builder needs it.  n_ssids == 0 is a
 * passive scan; n_channels == 0 means "all supported channels". */
struct hi5622_scan_req_params {
	u8	user_id;	/* user/VAP id selected by the VAP layer [U] */
	u8	iftype;		/* nl80211 iftype of the requesting wdev */
	u8	band;		/* 0 = 2.4 GHz, 1 = 5 GHz (endpoint band) */
	u32	flags;		/* cfg80211_scan_request.flags (opaque) */
	u16	dwell_ms;	/* 0 = vendor default 60; 20..200 override [V] */
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
	u8	band;		/* 0 = 2.4 GHz, 1 = 5 GHz (chip_id [I]) */
	u16	user_id;	/* host-chosen vendor user id, 12-bit [U] */
	u8	vap_id;		/* host-chosen vap id [U] */
};

/* One VAP/user delete (g0 id9 hmac_del_user_notify_dmac, exact 12 B body). */
struct hi5622_vap_del_params {
	u16	user_id;	/* 0x0fff = delete by address [V] */
	u8	mac[6];
	u8	band;		/* 0 = 2.4 GHz, 1 = 5 GHz (chip_id [I]) */
	u8	vap_id;		/* vap id used in the add [U] */
};

/* One AP start (WAL cfg group-2 family; the exact message is gap G1). */
struct hi5622_ap_start_params {
	u8	ssid[HI5622_FRAME_MAX_SSID_LEN];
	u8	ssid_len;
	u8	iftype;
	u8	channel;	/* channel number (hw_value) for the beacon-static
				 * channel-config frame; 0 = not specified */
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
	/* wifi-ap-start lane (task-121): the vendor AP-start path is a multi-frame
	 * SEQUENCE (set SSID -> auth -> AP config -> VAP start, or the static
	 * g0 id1 subset).  The cfg80211 layer calls build_ap_start() once per
	 * sequence position and posts each returned frame until the builder
	 * returns -ENODATA.  These fields select the position and the carrier
	 * hints; the provider owns the wire family. */
	u8	frame_index;	/* 0-based sequence position */
	u8	band;		/* 0 = 2.4 GHz, 1 = 5 GHz band hint */
	u8	module;		/* module/VAP index (g0 hdr[3]) */
	u8	vap_id;		/* WAL header VAP id */
	u8	hidden_ssid;	/* 1 = zero-length SSID */
	u8	auth_algs;	/* nl80211_auth_type for the auth frame */
};

/* Upper bound on one AP-start sequence (vendor WAL order is 4 frames). */
#define HI5622_AP_START_MAX_FRAMES	4

/* AP stop: identify the VAP; the provider owns the message shape. */
struct hi5622_ap_stop_params {
	u8	iftype;
	u16	user_id;
	u16	center_freq;
};

/* One WPA2 key install (add_key / del_key).  The cipher is the cfg80211
 * WLAN_CIPHER_SUITE_* value; the provider/portable builder enforces the
 * firmware worker's low-byte cipher-class rules.  Key material is copied only
 * into the wire frame and is never logged. */
#define HI5622_KEY_MAX		32u
#define HI5622_KEY_SEQ_MAX	16u

struct hi5622_key_params {
	u8	key_index;	/* 0..5 */
	u8	pairwise;	/* 1 = per-STA key; 0 = group/VAP key */
	u8	mac[6];		/* pairwise peer address, zero for group keys */
	u8	module;		/* g0 header module/VAP index */
	u8	dst;		/* HCC destination nibble */
	u32	cipher;		/* cfg80211 cipher suite */
	u16	key_len;
	u16	seq_len;
	u8	key[HI5622_KEY_MAX];
	u8	seq[HI5622_KEY_SEQ_MAX];
};

/* One default-key selection.  mgmt = 0 is the data-key path (keys 0..3, the
 * wifi-wpa-static lane); mgmt = 1 is the PMF/default-management-key path
 * (keys 4..5, wifi-wpa3-sae lane task-144) whose wire body needs the mgmt
 * cipher learned from the preceding IGTK add_key. */
struct hi5622_key_default_params {
	u8	key_index;	/* 0..3 data; 4..5 mgmt */
	u8	unicast;
	u8	multicast;
	u8	mgmt;		/* 1 = default management key (PMF/IGTK) */
	u32	cipher;		/* mgmt path only: cfg80211 cipher suite */
	u8	module;
	u8	dst;
};

/* One VAP start sequence entry (wifi-vap-start lane): build the vendor's
 * VAP add followed by the VAP start for this wdev/band.  The provider
 * chooses the wire family (WAL group-2 vendor order, or the g0 id1
 * config-event keys); the cfg80211 layer only owns the ordering. */
struct hi5622_vap_start_params {
	u8	iftype;		/* nl80211 iftype */
	u8	band;		/* 0 = 2.4 GHz, 1 = 5 GHz */
	u8	channel;	/* VAP-start channel argument: the vendor
				 * hmac_config_do_start_vap channel byte [V];
				 * 0 = legacy bench-proven path (unchanged) */
	u8	module;		/* module/VAP index, <= 0x12 [I] */
	u8	dst;		/* HCC dst nibble, 0 = local endpoint */
	u32	seq;		/* WAL cfg-event sequence [I] */
};

/* One module-channel config (wifi-beacon-static lane, task-140): the vendor
 * AP-start path's FIRST message is a channel-info config (WAL cfg command
 * 0x0124) built from the cfg80211 chandef.  The firmware side is a static
 * g0 id1 config-event key as well (table A row 108 -> worker CA 0x841B8),
 * so both carriers are available; the provider owns the envelope.
 *
 * Body (4 B, [V] firmware worker 0x841B8):  [0] band (0=2.4G, 1=5G -> module
 * ctx +0x11), [1] vendor mac_vap+2 [I] (-> ctx +0x12), [2] channel number
 * (-> ctx +0x10), [3] unused by the worker.  The worker then programs the
 * PHY/band channel (CA 0xFDCE4 family) - a zero module-channel context is
 * what the bench-proven clean-room AP path leaves behind. */
struct hi5622_chan_cfg_params {
	u8	band;		/* 0 = 2.4 GHz, 1 = 5 GHz */
	u8	channel;	/* channel number (hw_value), 1..196 */
	u8	vap_id;		/* body[1] = vendor mac_vap+2 [I] */
	u8	module;		/* g0 hdr[3] module index */
	u8	dst;		/* HCC dst nibble */
	u32	seq;		/* WAL cfg-event sequence [I] */
};

/* ===================================================================
 * 3. Scan result (provider decoder -> cfg80211 BSS)
 * =================================================================== */
enum hi5622_scan_frame_type {
	HI5622_SCAN_FTYPE_UNKNOWN = 0,	/* driver does not know */
	HI5622_SCAN_FTYPE_BEACON,
	HI5622_SCAN_FTYPE_PRESP,
};

/* Derived 802.11 facts (not wire fields of their own): the capability ESS/
 * IBSS/privacy bits and the IEs the cfg80211 BSS entry is built from.  The
 * raw IE chain is still forwarded verbatim - cfg80211 parses SSID/DS/HT/VHT
 * itself - these bits are for the per-BSS dmesg evidence line and for the
 * bench decision table (e.g. "decoded 6 BSS: 6 have SSID+DS, 4 have HT"). */
#define HI5622_SCAN_CAP_ESS		(1u << 0)	/* capability bit 0 [V] */
#define HI5622_SCAN_CAP_IBSS		(1u << 1)	/* capability bit 1 */
#define HI5622_SCAN_CAP_PRIVACY	(1u << 2)	/* capability bit 4 */
#define HI5622_SCAN_IE_HT_CAP		(1u << 0)	/* IE 45, len 26 */
#define HI5622_SCAN_IE_HT_OP		(1u << 1)	/* IE 61, len 22 */
#define HI5622_SCAN_IE_VHT_CAP		(1u << 2)	/* IE 191 */
#define HI5622_SCAN_IE_VHT_OP		(1u << 3)	/* IE 192 */
#define HI5622_SCAN_IE_RSN		(1u << 4)	/* IE 48 (WPA2/3) */
#define HI5622_SCAN_IE_WMM		(1u << 5)	/* vendor IE 221 00:50:f2/2 */
#define HI5622_SCAN_IE_EXT_CAP		(1u << 6)	/* IE 127 */
#define HI5622_SCAN_IE_COUNTRY		(1u << 7)	/* IE 7 */

struct hi5622_scan_result {
	u8	bssid[6];
	u8	ssid[HI5622_FRAME_MAX_SSID_LEN];
	u8	ssid_len;
	u8	frame_type;	/* enum hi5622_scan_frame_type */
	u8	ds_chan;	/* DS parameter-set channel (IE 3), 0 = absent [V] */
	u8	ie_count;	/* IEs walked by the decoder */
	u8	cap_flags;	/* HI5622_SCAN_CAP_* derived from capability */
	u8	ie_flags;	/* HI5622_SCAN_IE_* presence bits */
	u16	ht_cap_info;	/* HT capabilities info field (IE 45) [V] */
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
	u8	reason;		/* provider status/reason byte */
	u8	aborted;	/* 1 = completion reports failure/early stop */
};

/* Optional g1 id17 per-channel result (firmware sender 124 B frame [V]). */
struct hi5622_scan_channel {
	u8	slot;		/* firmware array slot [V] */
	u8	chan_num;	/* channel number [V] */
	u8	is_5g;		/* 1 = 5 GHz */
	u8	reserved;
	u16	freq;		/* MHz, 0 = not derivable */
};

/* Optional g2 id2 WAL/STA scan completion (the vendor cfg80211 feed point [V]);
 * status semantics are [I]/[U] - callers log it and treat the event as a
 * completion candidate. */
struct hi5622_scan_sta_complete {
	u8	status;
	u8	core_band;
	u8	chip_id;
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
	/* Optional diagnostics/completion decoders (no capability bit needed):
	 * g1 id17 channel results and g2 id2 WAL/STA completion.  NULL is fine;
	 * the scan layer then only counts the event. */
	int (*parse_scan_channel)(const u8 *payload, u32 len,
				  struct hi5622_scan_channel *out);
	int (*parse_scan_sta_complete)(const u8 *payload, u32 len,
				       struct hi5622_scan_sta_complete *out);
	int (*build_user_add)(u8 *wire, size_t cap, size_t *out_len,
			      const struct hi5622_vap_add_params *p);
	int (*build_user_add_notify)(u8 *wire, size_t cap, size_t *out_len,
				     const struct hi5622_vap_add_params *p);
	int (*build_user_del)(u8 *wire, size_t cap, size_t *out_len,
			      const struct hi5622_vap_del_params *p);
	/* AP start SEQUENCE builder: emits the frame at p->frame_index, or
	 * -ENODATA once the sequence is complete.  The caller posts every frame
	 * through hi5622_frame_post() in order and stops on -ENODATA. */
	int (*build_ap_start)(u8 *wire, size_t cap, size_t *out_len,
			      const struct hi5622_ap_start_params *p);
	/* Optional: carrier-aware name of the sequence frame at frame_index
	 * (NULL = unknown/out of range).  The cfg80211 layer uses it for the
	 * post label; index 1 is ap_cfg on the g0 carrier but ap_auth on WAL. */
	const char *(*ap_frame_name)(u8 frame_index);
	int (*build_ap_stop)(u8 *wire, size_t cap, size_t *out_len,
			     const struct hi5622_ap_stop_params *p);
	int (*build_vap_add)(u8 *wire, size_t cap, size_t *out_len,
			     const struct hi5622_vap_start_params *p);
	int (*build_vap_start)(u8 *wire, size_t cap, size_t *out_len,
			       const struct hi5622_vap_start_params *p);
	/* wifi-beacon-static lane: module channel config (WAL cmd 0x0124 /
	 * g0 id1 key 0x0124), posted BEFORE the SSID/AP-config frames. */
	int (*build_chan_cfg)(u8 *wire, size_t cap, size_t *out_len,
			      const struct hi5622_chan_cfg_params *p);
	/* wifi-wpa-static lane: g0 id1 key events 0x0078/0x007A/0x0079. */
	int (*build_key_add)(u8 *wire, size_t cap, size_t *out_len,
			     const struct hi5622_key_params *p);
	int (*build_key_del)(u8 *wire, size_t cap, size_t *out_len,
			     const struct hi5622_key_params *p);
	int (*build_key_default)(u8 *wire, size_t cap, size_t *out_len,
				 const struct hi5622_key_default_params *p);
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
