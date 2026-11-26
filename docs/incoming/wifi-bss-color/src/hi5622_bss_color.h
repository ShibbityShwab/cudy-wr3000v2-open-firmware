/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_bss_color.h - the vendor g0 id36 "scan-config BSS color" frame
 * (hmac_scan_config_bss_color) and the fail-closed AP-colour posting policy.
 *
 * ORIGINAL clean-room code (lane wifi-bss-color, shared task-233, 2026-11-26).
 * No vendor source, object byte, firmware byte, calibration value or secret is
 * reproduced here; the derivation (vendor host .text 0x99C10, scan-result
 * producer 0x9D870, firmware g0 table handler file 0x755E1) is in
 * ../REPORT.md, the frame tables/KAT in ../TABLES.md.
 *
 * WHAT THE MESSAGE IS
 * -------------------
 * A DIRECT group-0 id-0x24 H2D message (12-byte HCC header + 8-byte payload,
 * total 20 B; hcc_msg_alloc(8)). The payload is a little-endian u64 bitmap:
 * bit i set = BSS color i (0..63). The vendor builder ORs 1 << entry[+0x85]
 * over the scan result list at scan completion; entry[+0x85] is the 6-bit BSS
 * color parsed from that BSS's HE Operation element (proven in REPORT sec. 1).
 *
 * OUR POLICY (fail closed)
 * ------------------------
 * Module parameter bss_color: 0 = off (default: no frame, no device write),
 * 1 = post the start_ap AP's cfg80211 HE BSS colour as a singleton bitmap
 * (NL80211_ATTR_HE_BSS_COLOR -> settings->beacon.he_bss_color), once per
 * start_ap, after the AP config. The on-air HE Operation IE is firmware-built,
 * so this posting is a differential probe, not an assumed colour setter.
 *
 * Pure C99: the same header/source compile in the 6.18 kernel module and in
 * the host selftest.
 */
#ifndef HI5622_BSS_COLOR_H
#define HI5622_BSS_COLOR_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/types.h>
#else
#include <errno.h>
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

/* ---- wire contract (TABLES.md T1/T2) ---------------------------------- */
#define HI5622_BSS_COLOR_GROUP		0u	/* group 0 [V] */
#define HI5622_BSS_COLOR_ID		0x24u	/* id 36 [V] */
#define HI5622_BSS_COLOR_HDR_LEN	12u	/* HCC header [V] */
#define HI5622_BSS_COLOR_PAYLOAD_LEN	8u	/* hcc_msg_alloc(8) [V] */
#define HI5622_BSS_COLOR_TOTAL_LEN 	(HI5622_BSS_COLOR_HDR_LEN + HI5622_BSS_COLOR_PAYLOAD_LEN)
#define HI5622_BSS_COLOR_TAG		0x5a5au	/* wire bytes 5a 5a at +0x0A [V] */
#define HI5622_BSS_COLOR_ALLOC_NIBBLE	0x01u	/* hcc_msg_alloc [V] */
#define HI5622_BSS_COLOR_MAX		63u	/* 6-bit colour domain [V] */

/* header byte offsets (same HCC layout as h2d_frames.h) */
#define HI5622_BSS_COLOR_OFF_GROUP	0u
#define HI5622_BSS_COLOR_OFF_BID	1u	/* alloc nibble + dst/band hi nibble */
#define HI5622_BSS_COLOR_OFF_FA		2u
#define HI5622_BSS_COLOR_OFF_FB		3u	/* destination chip id [V] */
#define HI5622_BSS_COLOR_OFF_LEN	4u	/* u16 LE total */
#define HI5622_BSS_COLOR_OFF_ID		6u	/* u16 LE = 0x0024 */
#define HI5622_BSS_COLOR_OFF_FC		8u
#define HI5622_BSS_COLOR_OFF_DST	9u
#define HI5622_BSS_COLOR_OFF_TAG	10u	/* u16 LE = 0x5A5A */
#define HI5622_BSS_COLOR_OFF_MASK	12u	/* u64 LE bitmap */

/* ---- module parameter policy (our design) ----------------------------- */
#define HI5622_BSS_COLOR_PARAM_OFF	0u
#define HI5622_BSS_COLOR_PARAM_ON	1u
#define HI5622_BSS_COLOR_PARAM_MAX	HI5622_BSS_COLOR_PARAM_ON

/* Build one complete 20-byte wire frame. fa/fb are the two sequence/address
 * bytes the vendor copies from its scan object (+0xFA9/+0xFA8); bid_hi is the
 * high nibble of header byte 1 (vendor scan object +0xFAA, 0 in the capture).
 * Return 0 and set *len_out = 20, or -EINVAL. */
int hi5622_bss_color_build(u8 *wire, size_t cap, u8 fa, u8 fb, u8 bid_hi,
			   u64 mask, size_t *len_out);

/* Structural validation of one complete wire frame; *mask_out (may be NULL)
 * receives the LE bitmap. Returns 0 or -EINVAL. */
int hi5622_bss_color_validate(const u8 *wire, size_t len, u64 *mask_out);

/* AP-colour -> singleton bitmap. cfg80211 policy is colour 1..63 (0 is
 * "disabled" and cannot be expressed); partial colouring is not part of the
 * 64-bit bitmap and is ignored (documented in REPORT sec. 4). */
int hi5622_bss_color_mask_from_color(u8 color, u64 *mask_out);

/* Module-parameter policy: OFF/ON pass through; anything else -EINVAL. */
int hi5622_bss_color_policy(unsigned int value);

#ifdef __KERNEL__
struct cfg80211_ap_settings;
struct device;
struct hi5622_pci;

/* Post the start_ap AP's cfg80211 HE BSS colour as one g0 id36 frame when
 * bss_color=1. Called from hi5622_scan_op_start_ap after the AP config; a
 * failure is logged and never aborts/rolls back a started AP. */
int hi5622_bss_color_post_ap(struct hi5622_pci *ctx, struct device *dev,
			     const struct cfg80211_ap_settings *settings,
			     u8 band);
#endif

#endif /* HI5622_BSS_COLOR_H */
