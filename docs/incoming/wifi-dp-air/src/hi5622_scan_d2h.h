/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_scan_d2h.h - tolerant decoder for the Hi5622V100 firmware's
 * device->host scan-result events (Cudy WR3000 v2.0, luofu v2, Linux 6.18).
 *
 * ORIGINAL clean-room code (lane wifi-scan-d2h, shared task task-118,
 * 2026-11-26).  No vendor source, object code, firmware byte, calibration
 * value or secret is reproduced here; every constant/offset is a derived
 * protocol fact justified in ../REPORT.md with the address it was read from.
 *
 * WHAT THIS DECODES
 * -----------------
 * The firmware answers a scan request with HCC group-1 events (see REPORT.md
 * section 2 for the evidence):
 *
 *   g1 id 0x0F  hmac_scan_proc_scanned_bss            one BSS result
 *   g1 id 0x10  hmac_scan_proc_scan_comp_event        scan completion
 *   g1 id 0x11  hmac_scan_process_chan_result_event   per-channel result
 *   g2 id 0x02  wal_scan_comp_proc_sta                WAL/STA scan completion
 *
 * The driver's direct group-A D2H reader hands this decoder the HCC *payload*
 * (the 12-byte HCC header is stripped and available separately); the payload
 * of a BSS event is
 *
 *      802.11 beacon or probe-response frame  ||  20-byte trailing record
 *
 * The split is NOT assumed: the decoder walks the 802.11 information elements
 * and accepts the layout whose IE chain lands exactly on the end or exactly
 * 20 bytes before the end.  A bare frame (no trailer) is accepted too, and a
 * defensive 12-byte-prefix variant is probed last.  Every read is bounded and
 * every field is copied with an explicit length.
 *
 * The IE walk also produces the derived facts a cfg80211 BSS entry is built
 * from (scan-complete lane, task-123): SSID, DS/HT-operating channel, the
 * capability ESS/IBSS/privacy bits, and the presence of the HT cap / HT op /
 * VHT cap / VHT op / RSN / WMM / country / extended-capabilities elements,
 * plus the HT capabilities info field.  The raw IE chain is forwarded
 * verbatim; cfg80211 parses it and owns the actual entry.
 *
 * CONFIDENCE MARKERS (project convention)
 *   [V]  verified from a named artifact/decode (see REPORT.md)
 *   [I]  inference / unvalidated on the bench
 *   [U]  unknown: never guessed here, reported as unsupported
 *
 * The file is pure C99 with no kernel dependency beyond <linux/types.h> and
 * <linux/string.h> under __KERNEL__: it compiles unchanged as a userspace
 * unit (tools/scan_d2h_selftest.c) and inside the 6.18.60 module (patched
 * wifi-scan-ap provider, patches/0001-*.patch).
 */
#ifndef HI5622_SCAN_D2H_H
#define HI5622_SCAN_D2H_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
/* Portability edit (wifi-scan-ap integration): shared host type guard with
 * ../wifi-scan-ap/src/hi5622_frames_api.h / vapscan_frames.h so a host selftest
 * can include all portable headers under -pedantic.  No protocol content is
 * changed in this file. */
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

/* ===================================================================
 * 1. Message ids and envelope constants
 * =================================================================== */
#define HI5622_SCAN_D2H_GROUP_BSS	0x01u
#define HI5622_SCAN_D2H_ID_BSS		0x0fu	/* hmac_scan_proc_scanned_bss [V] */
#define HI5622_SCAN_D2H_ID_COMPLETE	0x10u	/* hmac_scan_proc_scan_comp_event [V] */
#define HI5622_SCAN_D2H_ID_CHANNEL	0x11u	/* hmac_scan_process_chan_result_event [V] */
#define HI5622_SCAN_D2H_GROUP_STA	0x02u
#define HI5622_SCAN_D2H_ID_STA_COMP	0x02u	/* wal_scan_comp_proc_sta [V] */

/* 20-byte trailer the vendor host subtracts before it feeds the 802.11 frame
 * to the management/HSAN path: hmac_scan_proc_scan_result_handle computes
 * frame_len = skb_len - 0x14 and hmac_fill_bss_dscr is handed the tail
 * pointer (wifi.ko .text+0x9d104 / +0x9cd6c..+0x9cdc0). [V] */
#define HI5622_SCAN_D2H_TRAILER_LEN	20u

/* 802.11 fixed sizes. */
#define HI5622_SCAN_D2H_MGMT_HDR	24u
#define HI5622_SCAN_D2H_BEACON_FIXED	12u
#define HI5622_SCAN_D2H_IE_HDR		2u

/* g1 id17 channel-result payload size: the firmware sender allocates 0x70
 * bytes (CA 0x642da, movs r0,#0x70) and copies 0x6C bytes from ctx+0x1688 to
 * payload+4. [V] */
#define HI5622_SCAN_D2H_CHAN_PAYLOAD	0x70u
#define HI5622_SCAN_D2H_CHAN_BLOCK	0x6cu

/* Capacity mirrors the wifi-scan-ap provider contract (512 B IEs, 32 B SSID). */
#define HI5622_SCAN_D2H_SSID_MAX	32u
#define HI5622_SCAN_D2H_IE_MAX		512u

/* Derived IEEE 802.11 capability/IE facts (scan-complete lane, task-123).
 * Bit positions are shared with the cfg80211-facing
 * hi5622_scan_result.cap_flags/ie_flags (hi5622_frames_api.h); they are
 * duplicated here so this decoder stays a standalone portable unit.  The
 * presence bits are what the per-BSS dmesg evidence line and the bench
 * decision table assert on; the raw IE chain is still forwarded verbatim. */
#define HI5622_SCAN_D2H_CAP_ESS		(1u << 0)	/* capability bit 0 */
#define HI5622_SCAN_D2H_CAP_IBSS	(1u << 1)	/* capability bit 1 */
#define HI5622_SCAN_D2H_CAP_PRIVACY	(1u << 2)	/* capability bit 4 */
#define HI5622_SCAN_D2H_IE_HT_CAP	(1u << 0)	/* IE 45, len 26 */
#define HI5622_SCAN_D2H_IE_HT_OP	(1u << 1)	/* IE 61, len 22 */
#define HI5622_SCAN_D2H_IE_VHT_CAP	(1u << 2)	/* IE 191 */
#define HI5622_SCAN_D2H_IE_VHT_OP	(1u << 3)	/* IE 192 */
#define HI5622_SCAN_D2H_IE_RSN		(1u << 4)	/* IE 48 */
#define HI5622_SCAN_D2H_IE_WMM		(1u << 5)	/* IE 221 00:50:f2 type 2 */
#define HI5622_SCAN_D2H_IE_EXT_CAP	(1u << 6)	/* IE 127 */
#define HI5622_SCAN_D2H_IE_COUNTRY	(1u << 7)	/* IE 7 */
/* HT operation secondary-channel offset values (IEEE 802.11 9.4.2.22). */
#define HI5622_SCAN_D2H_HT_SEC_NONE	0u
#define HI5622_SCAN_D2H_HT_SEC_ABOVE	1u
#define HI5622_SCAN_D2H_HT_SEC_BELOW	3u

/* ===================================================================
 * 2. Decoded BSS event (g1 id15) -> cfg80211 BSS data
 * =================================================================== */
enum hi5622_scan_d2h_ftype {
	HI5622_SCAN_D2H_FTYPE_UNKNOWN = 0,
	HI5622_SCAN_D2H_FTYPE_BEACON = 1,
	HI5622_SCAN_D2H_FTYPE_PRESP = 2,
};

/* Layout actually recognised (diagnostics / bench observability). */
#define HI5622_SCAN_D2H_LAYOUT_NONE		0x00u
#define HI5622_SCAN_D2H_LAYOUT_FRAME_TRAILER	0x01u	/* frame + 20 B tail [V] */
#define HI5622_SCAN_D2H_LAYOUT_FRAME_ONLY	0x02u	/* bare 802.11 frame */
#define HI5622_SCAN_D2H_LAYOUT_PREFIX12_TRAILER	0x04u	/* defensive variant */
#define HI5622_SCAN_D2H_LAYOUT_PREFIX12_ONLY	0x08u

/* PHY mode byte inside the 20-byte trailer (trailer[0x11]) [V offset, I naming]:
 * the vendor host maps 4 -> internal 0x0f, 8 -> 0x10, 0x0c -> 0x11 in
 * hmac_fill_bss_dscr, i.e. an HT/VHT/HE discriminator.  The names are the
 * project's reading of that enum; the raw value is always reported. */
enum hi5622_scan_d2h_phy {
	HI5622_SCAN_D2H_PHY_UNKNOWN = 0,
	HI5622_SCAN_D2H_PHY_RAW4 = 4,	/* [I] HT */
	HI5622_SCAN_D2H_PHY_RAW8 = 8,	/* [I] VHT */
	HI5622_SCAN_D2H_PHY_RAW12 = 12,	/* [I] HE */
};

struct hi5622_scan_d2h_bss {
	/* identity */
	u8	bssid[6];
	u8	ssid[HI5622_SCAN_D2H_SSID_MAX];
	u8	ssid_len;
	bool	ssid_hidden;		/* zero-length SSID IE or absent */
	u8	frame_type;		/* enum hi5622_scan_d2h_ftype */
	u8	phy_mode;		/* enum hi5622_scan_d2h_phy (raw) */

	/* beacon/probe-response fixed fields */
	u64	tsf;
	u16	beacon_interval;
	u16	capability;

	/* location */
	u8	chan_num;		/* 0 = not derivable */
	bool	is_5g;
	u16	freq;			/* MHz, 0 = unusable */

	/* derived IE facts (scan-complete lane): what the cfg80211 BSS entry was
	 * built from, without re-parsing the IE chain in the log path */
	u8	ds_chan;		/* DS parameter-set channel (IE 3), 0 = absent */
	u8	ie_count;		/* IEs walked in the validated chain */
	u8	cap_flags;		/* HI5622_SCAN_D2H_CAP_* */
	u8	ie_flags;		/* HI5622_SCAN_D2H_IE_* */
	u16	ht_cap_info;		/* HT capabilities info field (IE 45) */
	u8	ht_op_primary;		/* HT operation primary channel (IE 61) */
	u8	ht_op_sec_off;		/* HT operation secondary offset */

	/* signal: 0 unless opts->trust_trailer_signal and trailer present */
	s32	signal_mbm;
	bool	signal_from_trailer;

	/* the 20-byte trailing record (raw, always copied when present) [V raw] */
	u8	trailer_present;
	u8	trailer_len;
	u8	trailer[HI5622_SCAN_D2H_TRAILER_LEN];
	/* fields the vendor host consumes from the trailer [V offsets] */
	u32	tr_u32_0;		/* trailer+0x00 u32 */
	u8	tr_u8_4;		/* trailer+0x04 u8 */
	s8	tr_s8_5;		/* trailer+0x05 s8 */
	s8	tr_s8_6;		/* trailer+0x06 s8 */
	u32	tr_u32_8;		/* trailer+0x08 u32 */
	u32	tr_u32_12;		/* trailer+0x0c u32 */
	u8	tr_u8_16;		/* trailer+0x10 u8 */

	/* IE body handed to cfg80211_inform_bss() */
	u16	ie_len;
	u8	ie[HI5622_SCAN_D2H_IE_MAX];

	/* provenance */
	u8	layout;			/* HI5622_SCAN_D2H_LAYOUT_* bit */
	u16	raw_len;		/* payload length the decoder was handed */
	u16	frame_len;		/* 802.11 frame length inside the payload */
};

struct hi5622_scan_d2h_opts {
	/* Default false: report signal 0 rather than an unvalidated value.
	 * The vendor host reads two signed bytes from the trailer (+5/+6) into
	 * its BSS record and the trailer is where a MAC RX descriptor would
	 * live; until a bench correlation proves which byte is RSSI, keep the
	 * cfg80211 signal at 0 (fail closed, never invent a value). [I] */
	bool	trust_trailer_signal;
	/* Default true: require the IE chain to end exactly on the layout
	 * boundary.  False only for diagnostics. */
	bool	require_exact_ie;
};

/* ===================================================================
 * 3. Completion / channel-result / STA-completion events
 * =================================================================== */
struct hi5622_scan_d2h_complete {
	u8	status;		/* payload[0x00] [V: the vendor host tests it] */
	u8	raw[16];
	u16	raw_len;
	u16	payload_len;
};

struct hi5622_scan_d2h_chan {
	u8	slot;			/* payload[0x00], array slot index [V] */
	u8	chan_num;		/* block[0x00] = channel number [V] */
	bool	is_5g;
	u16	freq;			/* 0 = unusable */
	u16	block_len;		/* bytes copied (<= 0x6c) */
	u8	block[HI5622_SCAN_D2H_CHAN_BLOCK];
	u16	payload_len;
};

struct hi5622_scan_d2h_sta_comp {
	u8	status;		/* payload[0x01] [V: host reads frame+0xd] */
	u8	core_band;	/* payload[0x00] high nibble [V: frame+1>>4] */
	u8	chip_id;	/* payload[0x01]... vendor host uses frame+3 [V] */
	u8	raw[16];
	u16	raw_len;
	u16	payload_len;
};

/* ===================================================================
 * 4. API
 * =================================================================== */

/* Classify one (group,id) pair: returns the enum event kind, 0 = not ours. */
u8 hi5622_scan_d2h_event_kind(u8 group, u16 id);

#define HI5622_SCAN_D2H_EV_NONE		0x00u
#define HI5622_SCAN_D2H_EV_BSS		0x01u
#define HI5622_SCAN_D2H_EV_COMPLETE	0x02u
#define HI5622_SCAN_D2H_EV_CHANNEL	0x03u
#define HI5622_SCAN_D2H_EV_STA_COMP	0x04u

/* Decode one g1 id15 payload into cfg80211-ready BSS data.
 * opts == NULL uses the fail-closed defaults (exact IE, no signal).
 * Returns 0 on success, -EINVAL on NULL/short input, -EOPNOTSUPP when no
 * layout can be validated (the caller must count a decode gap, not a result).
 * On any return the output struct is bounded and safe to inspect. */
int hi5622_scan_d2h_parse_bss(const u8 *payload, u32 len,
			      const struct hi5622_scan_d2h_opts *opts,
			      struct hi5622_scan_d2h_bss *out);

/* Same but with explicit layout selection (bench/diagnostic use). */
int hi5622_scan_d2h_parse_bss_layout(const u8 *payload, u32 len,
				     u32 frame_off, u32 trailer_len,
				     const struct hi5622_scan_d2h_opts *opts,
				     struct hi5622_scan_d2h_bss *out);

/* g1 id16: payload[0] = completion status/result code. */
int hi5622_scan_d2h_parse_complete(const u8 *payload, u32 len,
				   struct hi5622_scan_d2h_complete *out);

/* g1 id17: 112-byte per-channel result (payload[0] slot, payload+4 block). */
int hi5622_scan_d2h_parse_chan(const u8 *payload, u32 len,
			       struct hi5622_scan_d2h_chan *out);

/* g2 id2: WAL/STA scan completion. */
int hi5622_scan_d2h_parse_sta_comp(const u8 *payload, u32 len,
				   struct hi5622_scan_d2h_sta_comp *out);

/* Human-readable names (dmesg / selftest / status). */
const char *hi5622_scan_d2h_layout_name(u8 layout);
const char *hi5622_scan_d2h_ftype_name(u8 ftype);
const char *hi5622_scan_d2h_phy_name(u8 raw_phy);
const char *hi5622_scan_d2h_event_name(u8 kind);

/* 802.11 channel number -> centre frequency (MHz); 0 when not derivable. */
u16 hi5622_scan_d2h_chan_to_freq(u8 chan, bool is_5g);

#endif /* HI5622_SCAN_D2H_H */
