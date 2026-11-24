/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_sta_logic.h - portable STA/band/HT capability logic for the
 * Hi5622V100 clean-room driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (wifi-sta-static lane, shared task task-141;
 * hardened by the wifi-sta-join lane, shared task task-146, 2026-11-24).
 * No vendor source text, object code, firmware byte or calibration value is
 * reproduced; every constant below is a decoded fact with the vendor-module
 * address recorded in ../REPORT.md / ../TABLES.md.
 *
 * This header is pure C99: it compiles unchanged in the 6.18 kernel module
 * and host-side (tools/sta_join_selftest.c).
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
typedef uint64_t u64;
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

/* ===================================================================
 * 4. Connect request bounds (wifi-sta-static, re-checked here)
 * =================================================================== */
#define HI5622_STA_MAX_SSID	32u
#define HI5622_STA_MAX_IE	512u
#define HI5622_STA_MAX_KEY	32u

struct hi5622_sta_connect_check {
	u32 ssid_len;
	const void *ssid;
	const void *bssid;	/* may be NULL for a wildcard connect */
	u32 ie_len;
	const void *ie;
	u32 key_len;
	const void *key;
	u32 auth_type;
	u32 key_idx;
};

int hi5622_sta_connect_check(const struct hi5622_sta_connect_check *p);

/* ===================================================================
 * 5. Join fire-path gates (wifi-sta-join lane, task-146)
 *
 * The vendor path is a single WAL cfg event whose body carries four
 * host-DRAM buffer ADDRESSES the FIRMWARE dereferences.  On the vendor
 * kernel (VMSPLIT_2G) a kernel kmalloc VA happens to equal the
 * device-visible host-DRAM address; on our VMSPLIT_3G kernel it does not,
 * so the driver passes dma_alloc_coherent device addresses (the same
 * 0x80000000..0x8fffffff identity window the bench-proven SR ring nodes
 * use) and refuses anything outside it.  The firmware's dereference
 * contract itself remains [U]; the post path is therefore module-parameter
 * gated, one-shot per module load, no retry, and stops on a firmware
 * exception.
 * =================================================================== */
#define HI5622_STA_JOIN_OFF	0u	/* fail closed (default)          */
#define HI5622_STA_JOIN_DRY	1u	/* build+validate, no post        */
#define HI5622_STA_JOIN_POST	2u	/* one single-shot post per load  */
#define HI5622_STA_JOIN_MAX	HI5622_STA_JOIN_POST

/* One-shot join gate.  sta_join = module parameter; allow_frames = the
 * inherited post path budget; already_attempted = this load already had a
 * posted join (no retry).  Returns 0 to proceed, negative errno to refuse. */
int hi5622_sta_join_gate(u32 sta_join, u32 allow_frames,
			 int already_attempted);

/* Health gate: stop on a firmware exception.  exc_mask is the latched
 * exception-seen halfword (firmware CPU 0x400008, "mask") and staged is the
 * number of captured vector frames (firmware CPU 0x40000c, "staging").
 * Nonzero mask or any staged vector = stop, no retry, power-cycle. */
int hi5622_sta_health_gate(u32 exc_mask, int exc_valid, u32 staged);

/* ===================================================================
 * 6. D2H assoc / disassoc completion decode (wifi-sta-join, task-146)
 *
 * The vendor wifi.ko registers the WAL chip-group-2 handler table with
 * hcc_msg_register_tab_chip(2, table, 0x13) (wal_main_init, 2.5.24):
 *   g2 id 3 -> wal_asoc_comp_proc_sta   (.text+0x125b6c) -> cfg80211
 *              connect result (calls oal_cfg80211_connect_result)
 *   g2 id 4 -> wal_disasoc_comp_proc_sta (.text+0x125e3c) -> cfg80211
 *              disconnected
 * Field extraction decoded from those two handlers; the payload starts at
 * wire offset 12 (after the 12-byte HCC header).  The assoc-completion
 * payload carries TWO FIRMWARE-SUPPLIED HOST-DRAM ADDRESSES (req_ie,
 * resp_ie): they are untrusted input and must be validated against the
 * host-DMA window before any translation/deref.  This decoder NEVER
 * dereferences them (log-only on our side).
 * =================================================================== */
#define HI5622_STA_D2H_WAL_GROUP	2u	/* HCC group nibble of WAL msgs   */
#define HI5622_STA_D2H_ID_ASSOC_COMP	3u	/* wal_asoc_comp_proc_sta   [V]   */
#define HI5622_STA_D2H_ID_DISASOC_COMP	4u	/* wal_disasoc_comp_proc_sta[V]   */
#define HI5622_STA_D2H_HDR_LEN		12u	/* HCC header                    */
#define HI5622_STA_D2H_TAG		0x5a5au

#define HI5622_STA_ASSOC_OFF_UNK0	0x00u	/* [U] not read by the handler   */
#define HI5622_STA_ASSOC_OFF_STATUS	0x02u	/* u16 status (0 = success)      */
#define HI5622_STA_ASSOC_OFF_BSSID	0x04u	/* u8 bssid[6]                   */
#define HI5622_STA_ASSOC_OFF_UNKA	0x0au	/* [U] not read by the handler   */
#define HI5622_STA_ASSOC_OFF_REQ_IE_LEN	0x0cu	/* u32                           */
#define HI5622_STA_ASSOC_OFF_RESP_IE_LEN 0x10u	/* u32                          */
#define HI5622_STA_ASSOC_OFF_REQ_IE_VA	0x14u	/* u32 host-DRAM device address  */
#define HI5622_STA_ASSOC_OFF_RESP_IE_VA	0x18u	/* u32 host-DRAM device address  */
#define HI5622_STA_ASSOC_MIN_LEN	0x1cu	/* 28 bytes                      */
#define HI5622_STA_D2H_IE_MAX		1024u	/* decode sanity bound [I]       */

#define HI5622_STA_DISASOC_OFF_REASON	0x00u	/* u16 cfg80211 reason code      */
#define HI5622_STA_DISASOC_MIN_LEN	2u

struct hi5622_sta_assoc_comp {
	u16 status;
	u16 unknown0;		/* payload +0x00 [U] */
	u16 unknown_a;		/* payload +0x0a [U] */
	u8  bssid[6];
	u32 req_ie_len;
	u32 resp_ie_len;
	u32 req_ie_va;
	u32 resp_ie_va;
};

struct hi5622_sta_disasoc_comp {
	u16 reason;
};

/* 0 = decoded and every pointer/length is inside the supplied host-DMA
 * window; negative errno otherwise.  hdr = the 12-byte HCC header,
 * payload = payload_len bytes after it (the reader hands them separately).
 * The IE addresses are returned, never read. */
int hi5622_sta_assoc_comp_decode(const u8 *hdr, size_t hdr_len,
				 const u8 *payload, size_t payload_len,
				 u32 dma_lo, u32 dma_hi,
				 struct hi5622_sta_assoc_comp *out);
int hi5622_sta_disasoc_comp_decode(const u8 *hdr, size_t hdr_len,
				   const u8 *payload, size_t payload_len,
				   struct hi5622_sta_disasoc_comp *out);

#endif /* HI5622_STA_LOGIC_H */
