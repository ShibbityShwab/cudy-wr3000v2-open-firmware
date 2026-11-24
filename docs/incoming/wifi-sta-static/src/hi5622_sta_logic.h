/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_sta_logic.h - portable STA/band/HT capability logic for the
 * Hi5622V100 clean-room driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (wifi-sta-static lane, shared task task-141,
 * 2026-11-24).  No vendor source text, object code, firmware byte or
 * calibration value is reproduced; every constant below is a decoded fact
 * with the vendor-module address recorded in ../REPORT.md / ../TABLES.md.
 *
 * This header is pure C99: it compiles unchanged in the 6.18 kernel module
 * and host-side (tools/sta_logic_selftest.c).
 */
#ifndef HI5622_STA_LOGIC_H
#define HI5622_STA_LOGIC_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stddef.h>
#include <stdint.h>
#ifndef HI5622_HOST_TYPES_DEFINED
#define HI5622_HOST_TYPES_DEFINED
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
#endif
#endif

/* ===================================================================
 * 1. WAL cfg-event opcodes (HCC group 2, WAL message id 1)
 *
 * The vendor cfg80211 .connect/.disconnect are NOT direct HCC messages:
 * both go through wal_send_cfg_event(r0=dev, r1=1, ...) as a WAL envelope
 * carrying {u16 opcode, u16 body_len, body}.  Recovered from
 *   wal_cfg80211_connect            wifi.ko 2.5.24 .text+0x100590
 *   wal_cfg80211_start_connect      .text+0x128310 -> wal_cfg80211_start_req
 *   wal_cfg80211_disconnect         .text+0x100844
 *   wal_cfg80211_start_disconnect   .text+0x128334
 *   wal_cfg80211_start_scan         .text+0x128094 (opcode 0x122 control)
 * Addresses are link-time .text offsets of the 2.5.24 vendor module
 * (md5 4737fcb2...); the 2.4.15 build carries the same constants.
 * =================================================================== */
#define HI5622_STA_WAL_CMD_CONNECT	0x0123u	/* [V] 108-byte body */
#define HI5622_STA_WAL_CMD_DISCONNECT	0x00d0u	/* [V] 16-byte body  */
#define HI5622_STA_WAL_CMD_SCAN		0x0122u	/* [V] context only  */

/* ===================================================================
 * 2. Vendor band capability words (2.5.24 static band tables)
 *
 * struct ieee80211_supported_band at .data+0xdf8 (2.4 GHz) and
 * .data+0xe54 (5 GHz); per-channel structs 56 B.  Decoded fields:
 *   ht_cap.cap      = 0x0062  (SUP_WIDTH_20_40|SGI_20|SGI_40)  2g and 5g
 *   ht_supported    = 1, ampdu_factor = 0, ampdu_density = 0
 *   ht_cap.mcs.rx_mask = 0 in the static table [U: see REPORT 4.3]
 *   vht_supported   = 1, vht cap = 0x00400000 (HTC_VHT)
 *   vht rx/tx mcs map = 0xfffa (2 spatial streams, MCS 0..9)
 * =================================================================== */
#define HI5622_STA_HT_CAP_VENDOR	0x0062u
#define HI5622_STA_HT_CAP_SGI20		0x0020u
#define HI5622_STA_HT_AMPDU_FACTOR	0u
#define HI5622_STA_HT_AMPDU_DENSITY	0u
#define HI5622_STA_VHT_CAP_2G	0x00400000u	/* HTC_VHT only, no width bits */
#define HI5622_STA_VHT_CAP_5G	0x00400064u	/* HTC_VHT|160 MHz|SGI80|SGI160 */
#define HI5622_STA_VHT_MCS_MAP	0xfffau		/* 2 SS, MCS 0..9 (rx and tx) */

/* ht_mode module-parameter values (hi5622_cfg80211.c). */
#define HI5622_STA_HT_MODE_BASELINE	0u	/* shipped default: SGI20+MAX_AMSDU */
#define HI5622_STA_HT_MODE_VENDOR20	1u	/* vendor HT20 word (SGI20)        */
#define HI5622_STA_HT_MODE_VENDOR40	2u	/* vendor HT20/40 word (0x62)      */
#define HI5622_STA_HT_MODE_MAX		2u

/* Baseline capability the bench-validated image already advertises
 * (IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_MAX_AMSDU). */
#define HI5622_STA_HT_CAP_BASELINE	0x0820u

/* ===================================================================
 * 3. Band channel tables (vendor static tables, 2.5.24)
 *
 * 2.4 GHz: 14 channels, 1..13 at 2412..2472 + ch14 at 2484.
 * 5 GHz:   29 channels: 36..64, 100..144, 149..165, and the 4.9 GHz
 *          public-safety channels 184..196 (4920..4980 MHz).
 * Static channel flags are 0 in the vendor table; wiphy_apply_custom_
 * regulatory (vendor) / the kernel regdb (our driver) restrict them.
 * =================================================================== */
#define HI5622_STA_CHAN_2G_COUNT	14u
#define HI5622_STA_CHAN_5G_COUNT	29u

struct hi5622_sta_chan {
	u16 freq;	/* MHz centre */
	u8  num;	/* IEEE channel number */
	u8  flags;	/* HI5622_STA_CHAN_* */
};

#define HI5622_STA_CHAN_NO_IR	0x01u
#define HI5622_STA_CHAN_RADAR	0x02u

const struct hi5622_sta_chan *hi5622_sta_band_channels(u8 band, u16 *count);

/* ht_mode -> vendor HT capability word; 0xffff on an invalid mode. */
u16 hi5622_sta_ht_cap_word(u32 ht_mode);

/* true when the mode advertises 20/40 MHz (vendor SUP_WIDTH_20_40). */
int hi5622_sta_ht_mode_is_40(u32 ht_mode);

/* Vendor VHT capability word per band: 0x00400000 on 2.4 GHz (HTC_VHT
 * only) and 0x00400064 on 5 GHz (HTC_VHT | SUPP_CHAN_WIDTH_160MHZ |
 * SHORT_GI_80 | SHORT_GI_160).  0xffffffff for an invalid band. */
u32 hi5622_sta_vht_cap_word(u8 band);

/* Auth-type bytes the vendor connect body carries (nl80211 values). */
int hi5622_sta_auth_type_valid(u32 auth_type);

/* Bounds-check one neutral connect request before any frame is built. */
struct hi5622_sta_connect_check {
	u32 ssid_len;
	const void *ssid;
	const void *bssid;	/* may be NULL for a wildcard connect */
	u32 ie_len;
	u32 key_len;
	u32 auth_type;
};

#define HI5622_STA_MAX_SSID	32u
#define HI5622_STA_MAX_IE	512u
#define HI5622_STA_MAX_KEY	32u

/* 0 = usable; negative errno otherwise.  Portable (no kernel calls). */
int hi5622_sta_connect_check(const struct hi5622_sta_connect_check *p);

#endif /* HI5622_STA_LOGIC_H */
