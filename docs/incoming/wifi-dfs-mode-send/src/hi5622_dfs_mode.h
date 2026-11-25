/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_dfs_mode.h - H2D g0-id1 config-event key 0x01F7 (RF/DFS mode arm) for
 * the Hi5622V100 clean-room full-MAC driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (lane wifi-dfs-mode-send, shared task task-190,
 * 2026-11-25).  The frame and firmware facts are the committed decodes of the
 * sibling lanes wifi-fw-rf-descriptor (task-189: the one writer of the mode
 * byte P+0xF04, table-A row 93, the stock hmac_config_set_country_for_dfs
 * emitter) and wifi-fw-desc-table (task-187: the g0-id1 config-event wire
 * shape, key at hdr+20, len at hdr+22, body at hdr+24, the inherited
 * 4-byte FF tail, total = len + 28).  No vendor source, object code,
 * firmware byte, calibration value, MAC or secret is reproduced here.
 *
 * WHY THIS EXISTS
 * ---------------
 * The 5 GHz RF programme CA 0xA2FA2 (invoked from the band-1 branch of
 * CA 0x40684) returns silently unless the band object's RF mode byte
 * (P+0xF04, P = the band object) equals 3.  That byte has exactly one
 * writer in the image: CA 0xB5D1C (strb.w r5,[r4,#0xF04]) inside CA 0xB5B9C,
 * the worker for H2D group-0/id-1 config-event key 0x01F7 (firmware table A
 * row 93 at CA 0x10C524).  The worker takes the mode from body[0] (1..5 via
 * the tbb at CA 0xB5D72), writes the mode byte and the mode's whole RF
 * register image R+0x3A8..R+0x418.  The only shipped host emitter is
 * hmac_config_set_country_for_dfs (wifi.ko .text+0x5762C; WAL command
 * "set_country_for_dfs", WAL ops slot .data+0x890C):
 *
 *     .text+0x5763C  movw r1,#0x01F7
 *     .text+0x57644  bl   hmac_config_send_event(ctx, key, len, data)
 *
 * so the wire frame is the SAME carrier as our working 0x00F7 desc-table
 * frame, with key 0x01F7, len 1 and body {0x03} (mode 3 = the value the
 * ch36..64 programme requires):
 *
 *     12-B HCC header + 8-B token/preamble + key u16 + len u16 + body + tail
 *     total = 12 + 8 + 2 + 2 + 1 + 4 = 29 B  (SR node word1 = 0x001D6D2B)
 *
 * fail-closed module parameter:
 *   dfs_mode=0 (default)  no 0x01F7 frame at all - current behaviour;
 *   dfs_mode=1            post the 29-B frame on a band-1 AP start with the
 *                         beacon_static vendor order, AFTER the 0x00F7 table
 *                         step and BEFORE the 0x0124 channel post;
 *   dfs_mode>1            refused (-EINVAL) before any frame.
 *
 * Pure C99: the same code compiles in the kernel module and in the host
 * selftest (src/dfs_mode_selftest.c); tools/run-host-tests.sh compiles both.
 */
#ifndef HI5622_DFS_MODE_H
#define HI5622_DFS_MODE_H

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
#define HI5622_DFS_MODE_PARM_MAX	1u

/* ---- frame geometry [V] (wifi-fw-desc-table TABLES T2 + sibling 0x00F7) - */
#define HI5622_DFS_MODE_HDR_LEN		12u	/* 12-byte HCC header */
#define HI5622_DFS_MODE_TOKEN_OFF	12u	/* 8-byte token/preamble */
#define HI5622_DFS_MODE_TOKEN_LEN	8u
#define HI5622_DFS_MODE_KEY_OFF		20u	/* key u16 at hdr+20 */
#define HI5622_DFS_MODE_LEN_OFF		22u	/* body length u16 at hdr+22 */
#define HI5622_DFS_MODE_BODY_OFF	24u	/* body at hdr+24 */
#define HI5622_DFS_MODE_TAIL_OFF		25u	/* inherited 4-byte 0xFF tail */
#define HI5622_DFS_MODE_TAG		0x5a5au	/* transport tag at hdr+10 */
#define HI5622_DFS_MODE_ID		1u	/* HCC message id 1 (g0 config event) */
#define HI5622_DFS_MODE_KEY		0x01f7u	/* firmware table A row 93 */
#define HI5622_DFS_MODE_BODY_LEN	1u	/* body[0] = mode */
#define HI5622_DFS_MODE_MODE		3u	/* mode 3 = the ch36..64 RF image */
#define HI5622_DFS_MODE_FA		0x00u	/* header byte 2 (inherited family) */
#define HI5622_DFS_MODE_FB		0x04u	/* header byte 3 (vendor-wire value) */
#define HI5622_DFS_MODE_WIRE_LEN	29u	/* 12+8+2+2+1+4 */
#define HI5622_DFS_MODE_TOTAL_FIELD	0x001du	/* total at hdr+4 = wire len */
/* SR node word1 = (frame_total << 16) | 0x6D2B (h2d_frames convention). */
#define HI5622_DFS_MODE_NODE_WORD1	0x001d6d2bu

/* ---- firmware state the 0x01F7 worker requires [V] (task-189 sec 4.2) --- */
/* CA 0xB5B9C: published per-link u32(bandobj+0x1A00) must be non-zero (else
 * error log 1442) and the band attach flag u8(bandobj+0xD70) must be 1 (log
 * 1448).  Live reads only; never a wire input. */
#define HI5622_DFS_MODE_BANDOBJ_PUBLISHED_OFF	0x1a00u
#define HI5622_DFS_MODE_BANDOBJ_ATTACH_OFF	0x0d70u

/* ---- fail-closed AP-start decision ------------------------------------ */
/* What one band-1 AP start must do with the 0x01F7 frame. */
struct hi5622_dfs_mode_plan {
	bool	post;		/* true = build + post the frame */
	u8	mode;		/* body[0] mode value (always 3 here) */
};

/* Fill *out with the DFS-mode decision.
 *  - 0 on success.  post = true only for dfs_mode == 1 on a band-1 AP with
 *    the beacon_static vendor order enabled;
 *  - -EINVAL for NULL out, dfs_mode > HI5622_DFS_MODE_PARM_MAX, band > 1, or
 *    dfs_mode == 1 without beacon_static == 1 (fail closed: never silently
 *    ignore an explicit request, never guess a band).
 * band == 0 with a valid parameter is a clean no-post (the frame is a band-1
 * AP-start step only: the 0x01F7 worker arms the band-1 RF image and the
 * 5 GHz-only programme is what consumes mode 3). */
int hi5622_dfs_mode_plan(unsigned int dfs_mode, unsigned int beacon_static,
			 unsigned int band, struct hi5622_dfs_mode_plan *out);

/* Build the complete 29-byte HCC wire frame into wire/cap.
 * Refuses (-EINVAL) NULL/bad arguments, a plan that is not armed and a mode
 * other than 3; -ENOSPC when cap < HI5622_DFS_MODE_WIRE_LEN.  The caller
 * posts the result through hi5622_frame_post() (the only wire path); this
 * module never touches the device. */
int hi5622_dfs_mode_build(u8 *wire, size_t cap, size_t *out_len,
			  const struct hi5622_dfs_mode_plan *plan);

/* Check the two live firmware preconditions of the 0x01F7 worker.
 *  - 0 when published != 0 && attach == 1 (the worker can write the mode);
 *  - -EAGAIN otherwise (a read-only verdict for the dmesg evidence line; the
 *    caller does not gate on it, matching the sibling 0x00F7 readback). */
int hi5622_dfs_mode_precheck(u32 published_perlink, u8 attach_flag);

/* One read-only sysfs status line (the driver appends it to cfg80211_status):
 *   dfs_mode: param=N (mode) beacon_static=M last_rc=R posted=P refused=F
 * mode is "off" (0), "band1-post" (1) or "invalid" (>1).  last_rc is
 * -ENODATA until the first attempt.  Returns the length, clamped like
 * hi5622_frame_post_status_line(). */
int hi5622_dfs_mode_status_line(unsigned int dfs_mode,
				unsigned int beacon_static, int last_rc,
				unsigned int posted, unsigned int refused,
				char *buf, size_t len);

#endif /* HI5622_DFS_MODE_H */
