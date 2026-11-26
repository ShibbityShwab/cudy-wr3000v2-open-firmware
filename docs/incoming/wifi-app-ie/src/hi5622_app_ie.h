/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_app_ie.h - H2D g0 id 0x20 vendor/app IE set (hmac_config_h2d_send_app_ie)
 * for the Hi5622V100 clean-room full-MAC driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (lane wifi-app-ie, shared task task-234, 2026-11-26).
 * The frame geometry and the firmware handler behaviour are the committed
 * decode of the first-ever stock H2D capture (sibling lane wifi-stock-h2d-census,
 * task-232, commit e977557) plus this lane's builder/firmware disassembly:
 * see ../REPORT.md and ../TABLES.md.  No vendor source, object code, firmware
 * byte, calibration value, MAC or secret is reproduced here.
 *
 * WHY THIS EXISTS
 * ---------------
 * The vendor host builds a group-0 / message-id 0x20 HCC frame whose payload is
 * a per-VAP IE table entry: {u32 ie_len; u8 type; u8 mode; u8 pad[2]; u8 ie[]}
 * plus a 3-byte zero tail (the vendor builder copies ie_len+11 bytes from its
 * caller struct; the stock capture shows the 11-byte payload geometry exactly).
 * The firmware handler (CA 0x85631) validates payload_len <= 616 and ie_len <=
 * 608, looks the VAP up by hdr[3], and stores the IE blob in the per-VAP slot
 * table chip_vap+0x24c+20*type (types 0..19; mode 0 = path A "save/clear").
 *
 * The driver half: cfg80211 hands the AP's cfg80211_beacon_data down in
 * start_ap() (beacon tail, beacon_ies, proberesp_ies, assocresp_ies - the
 * buffers hostapd fills from its vendor_elements= option, via
 * NL80211_ATTR_BEACON_TAIL / NL80211_ATTR_IE_PROBE_RESP /
 * NL80211_ATTR_IE_ASSOC_RESP).  This module extracts the well-formed
 * vendor-specific IEs (element id 0xDD) from those buffers and synthesizes the
 * 0x20 frame; the fail-closed app_ie parameter decides whether it may be
 * posted inside the beacon_static AP-start ladder (after the 0x0065 VAP start,
 * before the 0x0242 band start).
 *
 * Frame-type slot semantics (payload byte 4 = "type") are graded [I] in
 * TABLES.md: 0..19 are the vendor's per-VAP IE classes; the stock capture
 * cleared class 4, the vendor's AP association-response builder appends classes
 * 4 and 9, a scan path uses class 1, an internal WPS/P2P wrapper hardcodes
 * class 3, and classes 12/17/18 use a different (mode != 0) payload shape that
 * this lane does not ship.  The type is therefore an explicit module parameter
 * (default 4 = the stock-observed class), never a guess in code.
 *
 * Pure C99: the same code compiles in the kernel module and in the host
 * selftest (src/app_ie_selftest.c); tools/run-host-tests.sh compiles both.
 */
#ifndef HI5622_APP_IE_H
#define HI5622_APP_IE_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef HI5622_HOST_TYPES_DEFINED
#define HI5622_HOST_TYPES_DEFINED
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
#endif
#endif

/* ---- fail-closed module parameter contract ---------------------------- */
/* The only accepted non-zero value of app_ie; anything above is refused. */
#define HI5622_APP_IE_PARM_MAX		1u
/* The firmware's per-VAP IE slot count is 20 (index 0..19). */
#define HI5622_APP_IE_TYPE_MAX		19u
#define HI5622_APP_IE_VAP_MAX		255u

/* ---- frame geometry [V] (wifi-stock-h2d-census T6 + this lane's builder) */
#define HI5622_APP_IE_HDR_LEN		12u	/* HCC header */
#define HI5622_APP_IE_PAYLOAD_HDR_LEN	11u	/* 8 used bytes + 3 zero tail */
#define HI5622_APP_IE_DATA_OFF		8u	/* IE bytes start in the payload */
#define HI5622_APP_IE_TAIL_PAD		3u	/* vendor copies ie_len+11 bytes */
#define HI5622_APP_IE_MAX_BYTES		608u	/* firmware ie_len bound */
#define HI5622_APP_IE_MAX_PAYLOAD	(HI5622_APP_IE_DATA_OFF + HI5622_APP_IE_MAX_BYTES + HI5622_APP_IE_TAIL_PAD)
#define HI5622_APP_IE_WIRE_MAX		(HI5622_APP_IE_HDR_LEN + HI5622_APP_IE_MAX_PAYLOAD)
#define HI5622_APP_IE_MSG_ID		0x0020u	/* hdr[6] u16 LE */
#define HI5622_APP_IE_GROUP		0u	/* hdr[0] high nibble */
#define HI5622_APP_IE_TAG		0x5a5au	/* hdr[10] u16 LE */
#define HI5622_APP_IE_HDR_B1		0x01u	/* stock frame byte 1 */
#define HI5622_APP_IE_HDR_B2		0x00u	/* stock frame byte 2 */
#define HI5622_APP_IE_MODE_SAVE		0u	/* payload byte 5 = 0 (path A) */
#define HI5622_APP_IE_MAX_SOURCES	4u	/* tail/beacon/proberesp/assocresp */

/* The vendor-specific IE element id this lane injects. */
#define HI5622_APP_IE_ELEM_ID		0xddu

/* One prepared frame request (the plan) and the builder contract. */
struct hi5622_app_ie_plan {
	bool	 post;		/* 1 = build/post, 0 = parameter off */
	u32	 vap;		/* VAP/module index -> hdr[3] (0..255) */
	u8	 type;		/* firmware per-VAP IE class (0..19) */
	u8	 mode;		/* payload byte 5: 0 = path A save/clear */
	size_t	 ie_len;	/* bytes of ie[] (0 = clear the slot) */
	u8	 ie[HI5622_APP_IE_MAX_BYTES];
	unsigned int vendor_ies;/* count of 0xDD IEs inside ie[] */
};

/*
 * Decide, fail-closed, whether the current AP start may post one 0x20 frame.
 * Refusals (-EINVAL) happen before any wire byte:
 *   - app_ie > HI5622_APP_IE_PARM_MAX
 *   - app_ie == 1 without beacon_static == 1 (no ladder slot for the frame)
 *   - app_ie == 1 without vap_start_g0 == 1 (no started VAP to attach to)
 *   - band > 1, type > 19, vap > 255
 *   - ie_len > 608 or a malformed IE TLV list
 * app_ie == 0 returns a plan with post = false (the frame is never built).
 */
int hi5622_app_ie_plan(unsigned int app_ie,
		       unsigned int beacon_static,
		       unsigned int vap_start_g0,
		       unsigned int band,
		       unsigned int type,
		       unsigned int vap,
		       const u8 *ies, size_t ies_len,
		       struct hi5622_app_ie_plan *out);

/*
 * Build the 12-byte HCC header + 11-byte payload header + IE bytes + 3 zero
 * tail bytes.  Refuses a plan with post = false (never build an unrequested
 * frame), a bad IE length, or a short buffer.
 */
int hi5622_app_ie_build(u8 *wire, size_t cap, size_t *out_len,
			const struct hi5622_app_ie_plan *plan);

/*
 * Append every well-formed vendor-specific IE (element id 0xDD, whole TLV) of
 * one cfg80211 IE buffer to out[] (multiple sources are collected by calling
 * this repeatedly).  Malformed TLVs fail closed (-EBADMSG); an overflowing
 * destination fails closed (-ENOSPC).  Empty input (NULL/0) is a no-op.
 */
int hi5622_app_ie_collect(u8 *out, size_t cap, size_t *out_len,
			  unsigned int *vendor_ies,
			  const u8 *ies, size_t ies_len);

/* One read-only sysfs status line (the driver appends it to cfg80211_status). */
int hi5622_app_ie_status_line(unsigned int app_ie, unsigned int type,
			      unsigned int beacon_static,
			      unsigned int vap_start_g0,
			      int last_rc, unsigned int posted,
			      unsigned int refused, unsigned int ie_len,
			      unsigned int vendor_ies,
			      char *buf, size_t len);

#endif /* HI5622_APP_IE_H */
