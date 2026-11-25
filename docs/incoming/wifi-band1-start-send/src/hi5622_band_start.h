/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_band_start.h - H2D g0-id1 config-event key 0x0242 (the WAL
 * set_green_ap band-start command) for the Hi5622V100 clean-room full-MAC
 * driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (lane wifi-band1-start-send, shared task task-206,
 * 2026-11-25).  The frame and firmware facts are the committed decode of the
 * sibling lane wifi-band1-start (../wifi-band1-start/TABLES.md T1/T2/T5, the
 * per-band MAC start CA 0x4AF0C and its five direct call sites); the carrier
 * geometry is the committed g0-id1 config-event shape of
 * ../wifi-fw-desc-table/TABLES.md T2 (the same carrier the sibling 0x00F7 and
 * 0x01F7 sends use).  No vendor source, object code, firmware byte,
 * calibration value, MAC or secret is reproduced here.
 *
 * WHY THIS EXISTS
 * ---------------
 * The 5 GHz (band-1) MAC start is produced by CA 0x4AF0C.  The stock does not
 * arm it through the VAP-start hook G+0x370 (that slot has no writer); it arms
 * it by DIRECT calls to CA 0x4AF0C, and the host-reachable call sites are the
 * H2D config-event workers of key 0x0242 (the WAL "set_green_ap" command;
 * worker CA 0x4C524; body[0] = 1 sync/conditional start -> 0x4AF0C at
 * 0x4C612, body[0] = 2 forced start -> 0x4AF0C at 0x4C652) and key 0x0243
 * (worker CA 0x8419C -> helper 0x4BDCE -> 0x4AF0C at 0x4BEB0; arms only when
 * the per-band state marker state[1] == 3).  Our AP-start ladder never posts
 * either key, so band 1 never starts: the 0x4010C220 arm bits (1|8|0x20|0x1000
 * = 0x1029) are never set.  This module builds those frames and decides,
 * fail-closed, whether the current AP start may post them:
 *
 *   band_start=0 (default)   no 0x0242/0x0243 frame at all - current
 *                            behaviour, byte-identical to the F2 ladder;
 *   band_start=1             post the 33-byte 0x0242 sync-start frame on a
 *                            BAND-1 AP start with the beacon_static vendor
 *                            order, AFTER the 0x01F7 DFS/RF-mode step and
 *                            BEFORE the 0x0124 channel post;
 *   band_start>1             refused (-EINVAL) before any frame.
 *
 *   band_start_fallback=0 (default)  no fallback rung;
 *   band_start_fallback=1            run the decoded fallback ladder after
 *                            the primary frame: if the per-band start marker
 *                            state[1] is not 2 (the 0x4AF0C tail value), post
 *                            the forced-start 0x0242 (body[0]=2) and, if it
 *                            is still not 2, the 0x0243 nudge; requires
 *                            band_start=1 and the same band-1 gate.
 *
 * field B (hdr[3]) is the AP-start module/context index: the 0x0242 worker
 * resolves ctx = 0xFE578(0, hdr[3]) and takes the band object from ctx+0x4B8,
 * the same object the key-0x0064 VAP-add worker binds.  The sibling field-B
 * fix (../wifi-fw-dfs-worker/patches/0001-fieldb-context.patch) proved this
 * byte must match the AP's context; our ladder's AP context is module 0, the
 * default of vap_start_module, and this model derives field B from that same
 * index instead of hard-coding a vendor-wire value.
 *
 * Body of the sync-start frame (5 bytes, derived from the worker's reads at
 * CA 0x4C5E2..: body[0] selects the subcommand, body[2]==1 sets the run-state
 * bit before the 0x4C3FC gate): { 01, 00, 01, 00, 00 }.
 *
 * Pure C99: the same code compiles in the kernel module and in the host
 * selftest (src/band_start_selftest.c); tools/run-host-tests.sh compiles both.
 */
#ifndef HI5622_BAND_START_H
#define HI5622_BAND_START_H

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
/* The only accepted non-zero value of each parameter; anything above is
 * refused.  Both default to 0 (no frame, the current behaviour). */
#define HI5622_BAND_START_PARM_MAX		1u

/* ---- frame geometry [V] (wifi-fw-desc-table TABLES T2 carrier) --------- */
#define HI5622_BAND_START_HDR_LEN		12u	/* 12-byte HCC header */
#define HI5622_BAND_START_TOKEN_OFF		12u	/* 8-byte token/preamble */
#define HI5622_BAND_START_TOKEN_LEN		8u
#define HI5622_BAND_START_KEY_OFF		20u	/* key u16 at hdr+20 */
#define HI5622_BAND_START_LEN_OFF		22u	/* body length u16 at hdr+22 */
#define HI5622_BAND_START_BODY_OFF		24u	/* body at hdr+24 */
#define HI5622_BAND_START_TAG			0x5a5au	/* transport tag at hdr+10 */
#define HI5622_BAND_START_ID			1u	/* HCC message id 1 (g0 config event) */
#define HI5622_BAND_START_FA			0x00u	/* header byte 2 (inherited family) */
#define HI5622_BAND_START_MODULE_MAX		0x12u	/* module-table index bound [V] */

/* The primary frame: key 0x0242, body[0]=1 (sync/conditional start). */
#define HI5622_BAND_START_KEY			0x0242u	/* WAL set_green_ap, worker CA 0x4C524 */
#define HI5622_BAND_START_BODY_LEN		5u
#define HI5622_BAND_START_WIRE_LEN		33u	/* 12+8+2+2+5+4 */
#define HI5622_BAND_START_TOTAL_FIELD		0x0021u	/* total at hdr+4 = wire len */
#define HI5622_BAND_START_NODE_WORD1		0x00216d2bu
#define HI5622_BAND_START_SUBCMD		1u	/* body[0]: sync/conditional start */
#define HI5622_BAND_START_SUBCMD_FORCED	2u	/* body[0]: forced start */
#define HI5622_BAND_START_ARM_FLAG		1u	/* body[2]: set the run-state bit */

/* The fallback rung: key 0x0242 body[0]=2 (forced start), then key 0x0243
 * (the band-ready nudge, worker CA 0x8419C, body unread by the worker). */
#define HI5622_BAND_START_FALLBACK_KEY		0x0243u
#define HI5622_BAND_START_NUDGE_BODY_LEN		1u
#define HI5622_BAND_START_NUDGE_WIRE_LEN		29u	/* 12+8+2+2+1+4 */
#define HI5622_BAND_START_NUDGE_TOTAL_FIELD	0x001du
#define HI5622_BAND_START_NUDGE_NODE_WORD1	0x001d6d2bu

/* ---- firmware state the 0x0242 worker / CA 0x4AF0C require [V] -------- */
/* The worker CA 0x4C524 takes r5 = u32(ctx+0x4B8), the band object bound by
 * the key-0x0064 VAP-add worker; when it is 0 the worker returns without a
 * start.  CA 0x4AF0C then reads the per-band run-state at bandobj+0x14D0:
 * it returns early when state[1] == 2 ("already started", CA 0x4AF2C) and
 * when state[4] == 0xFF ("no bound VAP index", CA 0x4AF32); it writes
 * state[1] = 2 and increments the start counter u32 at state+0x64 on
 * success. */
#define HI5622_BAND_START_BAND_OBJ_OFF		0x4b8u	/* ctx+0x4B8 = band object */
#define HI5622_BAND_START_STATE_OFF		0x14d0u	/* bandobj+0x14D0 = run-state */
#define HI5622_BAND_START_STATE_STARTED	2u	/* state[1]: already started */
#define HI5622_BAND_START_STATE_READY		3u	/* state[1]: band ready (0x0243) */
#define HI5622_BAND_START_STATE_NO_VAP		0xffu	/* state[4]: no bound VAP */
#define HI5622_BAND_START_STATE_COUNTER_OFF	0x64u	/* u32 start counter */

/* ---- fail-closed AP-start decision ------------------------------------ */
/* Which frame of the decoded ladder a build call may synthesize. */
enum hi5622_band_start_frame {
	HI5622_BAND_START_FRAME_PRIMARY = 0,	/* 0x0242 body[0]=1 sync start */
	HI5622_BAND_START_FRAME_FORCED = 1,	/* 0x0242 body[0]=2 forced start */
	HI5622_BAND_START_FRAME_NUDGE = 2,	/* 0x0243 band-ready nudge */
};

/* What one band-1 AP start must do with the 0x0242/0x0243 frames. */
struct hi5622_band_start_plan {
	bool	post;		/* true = build + post the primary frame */
	bool	fallback;	/* true = run the forced/nudge rungs on a miss */
	u8	module;		/* field B = the AP-start module/context index */
};

/* Read-only snapshot of the firmware state the start guards test. */
struct hi5622_band_start_state {
	bool	has_bandobj;	/* the worker's ctx+0x4B8 dereference is non-zero */
	u8	started;	/* bandobj+0x14D0+1: 2 = already started */
	u8	vap_index;	/* bandobj+0x14D0+4: 0xFF = no bound VAP */
	u32	counter;	/* bandobj+0x14D0+0x64: start counter (increments) */
};

/* Fill *out with the band-start decision.
 *  - 0 on success;
 *  - -EINVAL for NULL out, band_start > HI5622_BAND_START_PARM_MAX,
 *    band_start_fallback > HI5622_BAND_START_PARM_MAX, module > the
 *    module-table bound, band > 1, band_start == 1 without beacon_static == 1,
 *    or band_start_fallback == 1 without band_start == 1 (fail closed: never
 *    silently ignore an explicit request, never guess a band).
 * band == 0 with a valid parameter is a clean no-post: the start is a band-1
 * AP-start step (the 0x4AF0C band-1 branch owns the 0x4010C220 0x1029 bits). */
int hi5622_band_start_plan(unsigned int band_start,
			   unsigned int band_start_fallback,
			   unsigned int beacon_static, unsigned int band,
			   unsigned int module, struct hi5622_band_start_plan *out);

/* Build one complete HCC wire frame into wire/cap.
 *  - frame PRIMARY: the 33-byte 0x0242 sync start; requires plan->post;
 *  - frame FORCED:  the 33-byte 0x0242 forced start; requires fallback;
 *  - frame NUDGE:   the 29-byte 0x0243 nudge; requires fallback.
 * Refuses (-EINVAL) NULL/bad arguments, a frame the plan does not arm, and
 * an unknown frame selector; -ENOSPC when cap is too small.  The caller
 * posts the result through hi5622_frame_post() (the only wire path); this
 * module never touches the device. */
int hi5622_band_start_build(u8 *wire, size_t cap, size_t *out_len,
			    const struct hi5622_band_start_plan *plan,
			    unsigned int frame);

/* Check the live firmware guards the 0x0242 worker and CA 0x4AF0C test.
 *  - 0 when the band object exists, the band is not already started and a
 *    VAP index is bound (the start can run);
 *  - -EALREADY when state[1] == 2 (CA 0x4AF0C returns at 0x4AF2C: the start
 *    marker means an earlier start ran; a re-post is a silent no-op);
 *  - -EAGAIN when the band object is absent (the 0x4C524 worker returns) or
 *    state[4] == 0xFF (CA 0x4AF0C refuses at 0x4AF32).
 * Read-only verdict for the dmesg evidence line; the caller logs it and does
 * not gate the frame on it (matching the sibling 0x00F7/0x01F7 readbacks). */
int hi5622_band_start_precheck(const struct hi5622_band_start_state *st);

/* One read-only sysfs status line (the driver appends it to cfg80211_status):
 *   band_start: param=N (mode) fallback=F beacon_static=M last_rc=R posted=P
 *               forced=FO nudge=NU refused=RF
 * mode is "off" (0), "band1-post" (1) or "invalid" (>1).  Returns the length,
 * clamped like hi5622_frame_post_status_line(). */
int hi5622_band_start_status_line(unsigned int band_start,
				  unsigned int band_start_fallback,
				  unsigned int beacon_static, int last_rc,
				  unsigned int posted, unsigned int forced,
				  unsigned int nudge, unsigned int refused,
				  char *buf, size_t len);

#endif /* HI5622_BAND_START_H */
