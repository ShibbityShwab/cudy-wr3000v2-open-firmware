/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_desc_table.h - H2D 0x00F7 descriptor-table send (the C1 fix) for the
 * Hi5622V100 clean-room full-MAC driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (lane wifi-desc-table-send, shared task task-188,
 * 2026-11-25).  The frame and content facts are the committed decode of the
 * sibling lane wifi-fw-desc-table (../wifi-fw-desc-table/REPORT.md, TABLES.md,
 * FIRE-RECIPE.md); no vendor source, object code, firmware byte, calibration
 * value, MAC or secret is reproduced here.
 *
 * WHY THIS EXISTS
 * ---------------
 * The firmware keeps a 0x148-byte runtime "descriptor pool" (CA 0x1ADC98)
 * with N <= 20 records {word, +4, +6, channel bitmap}.  The boot builder
 * installs country "99" and a 5 GHz record word 0x00000201 whose byte2 (the
 * flags byte) is 0.  The band-1 descriptor predicate (CA 0x101890) returns 1
 * only when the resolved record's byte2 bit7 (0x00800000) is set, and the
 * 5 GHz-only RF programme (CA 0x40684) calls that predicate: with the boot
 * table the predicate is 0, the RF image is never written and the 5 GHz
 * beacon engine has no channel (the C1 symptom).
 *
 * The only firmware path that rewrites the pool from the host is an H2D HCC
 * group-0 / id-1 config event with key 0x00F7: the worker (CA 0x45A30)
 * strcmp-dedupes the pool country string against the blob, memcpy_s's
 * 8 + body[4]*16 bytes into the pool (destMax 0x148), runs the channel-map
 * populator and then programs the band feature bits.  This module builds
 * that frame (68 bytes on the wire including the inherited 4-byte tail) and
 * decides, fail-closed, whether the current AP start may post it:
 *
 *   desc_table=0 (default)  no 0xF7 frame at all - current behaviour;
 *   desc_table=1            post the 0xF7 table before the 0x0124 channel
 *                           post, on a band-1 AP start with the
 *                           beacon_static vendor order only;
 *   desc_table>1            refused (-EINVAL) before any frame.
 *
 * The blob is the pool image for the two-record fix table:
 *   cc "US" (MUST differ from the boot pool's "99": the install is gated by
 *   an ARM strcmp on the country string, and a "99" table is silently
 *   skipped), count 2,
 *   rec0 word 0x00800000 (2.4 GHz, armed), +4 30, +6 300, bitmap 0x1FFF;
 *   rec1 word 0x00800201 (5 GHz, 20 MHz, armed), +4 30, +6 300,
 *   bitmap 0x000000FF (ch36..64 = 5 GHz map rows 0..7).
 *
 * Pure C99: the same code compiles in the kernel module and in the host
 * selftest (src/desc_table_selftest.c); tools/run-host-tests.sh compiles both.
 */
#ifndef HI5622_DESC_TABLE_H
#define HI5622_DESC_TABLE_H

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
/* The only accepted non-zero value; anything above is refused. */
#define HI5622_DESC_TABLE_PARM_MAX	1u

/* ---- frame geometry [V] (wifi-fw-desc-table TABLES T2/T3) -------------- */
#define HI5622_DESC_TABLE_HDR_LEN	12u	/* 12-byte HCC header */
#define HI5622_DESC_TABLE_TOKEN_LEN	8u	/* preamble/token 00..00 01..00 */
#define HI5622_DESC_TABLE_KEY		0x00f7u	/* config key at hdr+20 */
#define HI5622_DESC_TABLE_BLOB_LEN	40u	/* 8 + 2*16 */
#define HI5622_DESC_TABLE_REC_LEN	16u
#define HI5622_DESC_TABLE_RECS		2u
#define HI5622_DESC_TABLE_MAX_RECS	20u	/* destMax 0x148 = 8+20*16 */
#define HI5622_DESC_TABLE_TAG		0x5a5au
/* 12 hdr + 8 token + 2 key + 2 len + 40 blob + 4 inherited tail. */
#define HI5622_DESC_TABLE_WIRE_LEN	68u

/* Fixed table values (the two-record fix of the lane's FIRE-RECIPE). */
#define HI5622_DESC_TABLE_CC0		'U'
#define HI5622_DESC_TABLE_CC1		'S'
#define HI5622_DESC_TABLE_WORD_2G	0x00800000u	/* band 0, byte2 bit7 armed */
#define HI5622_DESC_TABLE_WORD_5G	0x00800201u	/* band 1, bw 20, armed */
#define HI5622_DESC_TABLE_PLAN_4	30u		/* rec +4 (vendor 3000/100) */
#define HI5622_DESC_TABLE_PLAN_6	300u		/* rec +6 (vendor 3000/10) */
#define HI5622_DESC_TABLE_BITMAP_2G	0x00001fffu	/* 2.4G rows 0..12 */
#define HI5622_DESC_TABLE_BITMAP_5G	0x000000ffu	/* 5G rows 0..7 = ch36..64 */

/* ---- fail-closed AP-start decision ------------------------------------ */
/* What one band-1 AP start must do with the 0x00F7 frame. */
struct hi5622_desc_table_plan {
	bool	post;		/* true = build + post the frame */
	u8	cc[2];		/* country code in the blob ("US") */
	u8	recs;		/* record count (always 2 here) */
};

/* Fill *out with the descriptor-table decision.
 *  - 0 on success.  post = true only for desc_table == 1 on a band-1 AP with
 *    the beacon_static vendor order enabled;
 *  - -EINVAL for NULL out, desc_table > HI5622_DESC_TABLE_PARM_MAX, band > 1,
 *    or desc_table == 1 without beacon_static == 1 (fail closed: never
 *    silently ignore an explicit request, never guess a band).
 * band == 0 with a valid parameter is a clean no-post (the frame is a band-1
 * AP-start step only). */
int hi5622_desc_table_plan(unsigned int desc_table, unsigned int beacon_static,
			   unsigned int band, struct hi5622_desc_table_plan *out);

/* Build the complete 68-byte HCC wire frame into wire/cap.
 * Refuses (-EINVAL) NULL/bad arguments, a plan that is not armed, a country
 * code equal to the boot pool string "99", an empty or non-printable cc, and
 * a record count other than 2; -ENOSPC when cap < HI5622_DESC_TABLE_WIRE_LEN.
 * The caller posts the result through hi5622_frame_post() (the only wire
 * path); this module never touches the device. */
int hi5622_desc_table_build(u8 *wire, size_t cap, size_t *out_len,
			    const struct hi5622_desc_table_plan *plan);

/* One read-only sysfs status line (the driver appends it to cfg80211_status):
 *   desc_table: param=N (mode) beacon_static=M last_rc=R posted=P refused=F
 * mode is "off" (0), "band1-post" (1) or "invalid" (>1).  last_rc is
 * -ENODATA until the first attempt.  Returns the length, clamped like
 * hi5622_frame_post_status_line(). */
int hi5622_desc_table_status_line(unsigned int desc_table,
				  unsigned int beacon_static, int last_rc,
				  unsigned int posted, unsigned int refused,
				  char *buf, size_t len);

#endif /* HI5622_DESC_TABLE_H */
