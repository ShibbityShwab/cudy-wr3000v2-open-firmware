/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_sta_frames.h - WAL cfg-event STA join/disconnect frame builders
 * for the Hi5622V100 clean-room driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (wifi-sta-static lane, shared task task-141;
 * harden-by-re-derivation of the 108-byte body's four host-DRAM pointer
 * fields in the wifi-sta-join lane, shared task task-146, 2026-11-24).
 * Only decoded wire facts are encoded here; the vendor code itself is not
 * reproduced or linked.
 *
 * WIRE SHAPE (proven on the bench for the same WAL family by the AP-start
 * lane, whose 4 frames are consumed by the firmware):
 *
 *   12-byte HCC header (group 2 = core/band, HCC id 0, tag 0x5a5a)
 *   +0x0c u32 cookie        (0)
 *   +0x10 u32 wal message   (1 = WAL_MSG_CFG)
 *   +0x14 u16 pad           (0)
 *   +0x16 u16 data_len      (4 + body_len)
 *   +0x18 u32 sequence
 *   +0x1c u16 wal opcode    (0x123 connect / 0x0d0 disconnect)
 *   +0x1e u16 body_len
 *   +0x20 body[body_len]    (total = 32 + body_len)
 *
 * CONNECT BODY (108 B; wal_cfg80211_connect 2.5.24 .text+0x100590, the
 * 108-byte local struct it memcpy()s into wal_cfg80211_start_req):
 *   +0x01 u8  ssid_len      (cfg80211_connect_params.ssid_len)
 *   +0x02 u8  mfp
 *   +0x04 u32 ie buffer DEVICE ADDRESS
 *   +0x08 u32 ssid buffer DEVICE ADDRESS
 *   +0x0c u32 bssid buffer DEVICE ADDRESS
 *   +0x10 u8  privacy
 *   +0x11 u8  auth_type
 *   +0x12 u8  key_len
 *   +0x13 u8  key_idx
 *   +0x14 u32 key buffer DEVICE ADDRESS
 *   +0x18..+0x67 zero
 *   +0x68 u32 ie_len
 *
 * THE FOUR ADDRESS FIELDS (the [U] deref risk):
 *   The vendor stores the cfg80211 params POINTERS raw.  Its OpenWrt kernel
 *   uses VMSPLIT_2G, so a kmalloc VA (0x80000000+phys, PHYS_OFFSET
 *   0x80000000 on this SoC) numerically equals the device-visible host-DRAM
 *   address (DTS dma-ranges identity, HI5622_HOST_DMA_LO..HI).  Our luofu
 *   6.18 kernel is VMSPLIT_3G (PAGE_OFFSET 0xC0000000), so a kernel VA is
 *   NOT a device address; passing one would hand the firmware a wild
 *   pointer.  This builder therefore only ever encodes dma_alloc_coherent
 *   device addresses inside the identity window, requires all four to be
 *   non-zero (a zero/NULL field is not a provable "absent" sentinel in the
 *   firmware), and hi5622_sta_join_bufs_check() enforces:
 *     - each address is 4-byte aligned and inside [dma_lo, dma_hi];
 *     - each pointee region is inside the caller's buffer;
 *     - the addresses match the fixed buffer layout below;
 *     - ssid_len/ie_len/key_len fit their regions;
 *     - no 32-bit wrap of address+length.
 *   The firmware's actual dereference contract stays [U]: the post path is
 *   module-parameter gated, one-shot, no retry (ladder B2 is the experiment).
 *
 * FAIL CLOSED: these are pure builders; nothing posts unless the kernel
 * caller passes the hi5622_sta module-parameter gate.
 */
#ifndef HI5622_STA_FRAMES_H
#define HI5622_STA_FRAMES_H

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

#include "hi5622_sta_logic.h"

#define HI5622_STA_CONNECT_BODY_LEN	108u
#define HI5622_STA_DISCONNECT_BODY_LEN	16u
#define HI5622_STA_WAL_HDR_LEN		32u
#define HI5622_STA_HCC_HDR_LEN		12u
#define HI5622_STA_HCC_TAG		0x5a5au
#define HI5622_STA_HCC_GROUP_CORE	2u
#define HI5622_STA_HCC_ID_WAL		0u	/* bench-proven AP WAL carrier */
#define HI5622_STA_WAL_MSG_CFG		1u	/* WAL message class          */
#define HI5622_STA_FRAME_MAX								(HI5622_STA_WAL_HDR_LEN + HI5622_STA_CONNECT_BODY_LEN)

/* Fixed layout of the one coherent host-DRAM buffer the four body pointers
 * address: bssid[6], pad, ssid[32], ies[512], key[32]. */
#define HI5622_STA_JOIN_OFF_BSSID	0u
#define HI5622_STA_JOIN_OFF_SSID	8u
#define HI5622_STA_JOIN_OFF_IE		40u
#define HI5622_STA_JOIN_OFF_KEY		552u
#define HI5622_STA_JOIN_BUF_SIZE	(HI5622_STA_JOIN_OFF_KEY + 					 HI5622_STA_MAX_KEY)	/* 584 */

/* The four host-DRAM device addresses the body carries (0 = absent; the
 * hardened driver never emits 0 - see the header comment). */
struct hi5622_sta_join_bufs {
	u32 ssid_va;
	u32 bssid_va;
	u32 ie_va;
	u32 ie_len;
	u32 key_va;
	u8  ssid_len;
	u8  mfp;
	u8  privacy;
	u8  auth_type;
	u8  key_len;
	u8  key_idx;
};

struct hi5622_sta_disconnect_params {
	u8  bssid[6];
	u16 reason_code;
};

/* Fill the 108-byte connect body; 0 on success, -EINVAL/-ENOSPC otherwise.
 * All four address fields must be non-zero and lengths in bounds. */
int hi5622_sta_connect_body(u8 *body, size_t cap,
			    const struct hi5622_sta_join_bufs *p);

/* Independent re-parse + validation of a built 108-byte body: every
 * address is non-zero and equal to p's, lengths equal, no reserved byte
 * written.  Used as a post-build self-check before the frame is posted. */
int hi5622_sta_connect_body_check(const u8 *body, size_t body_len,
				  const struct hi5622_sta_join_bufs *p);

/* Bounds/window validation of the four addresses before any frame is
 * built:
 *   buf_va     device address of the coherent buffer holding all four
 *   buf_size   its size (>= HI5622_STA_JOIN_BUF_SIZE)
 *   dma_lo/hi  the proven device-visible host-DRAM window
 * Returns 0 only when every field is safe. */
int hi5622_sta_join_bufs_check(const struct hi5622_sta_join_bufs *p,
			       u32 buf_va, u32 buf_size,
			       u32 dma_lo, u32 dma_hi);

/* Fill the 16-byte disconnect body. */
int hi5622_sta_disconnect_body(u8 *body, size_t cap,
			      const struct hi5622_sta_disconnect_params *p);

/* Assemble one complete WAL cfg-event frame (HCC header + WAL envelope +
 * body).  out_len is written on success.  dst is the HCC destination
 * nibble (0 = local endpoint). */
int hi5622_sta_wal_frame(u8 *out, size_t cap, size_t *out_len, u16 wal_cmd,
			 const u8 *body, u16 body_len, u32 seq, u8 dst);

/* Structural self-check of a finished frame: header group/id/tag, lengths,
 * WAL envelope and opcode.  0 = acceptable. */
int hi5622_sta_frame_check(const u8 *frame, size_t len, u16 wal_cmd,
			   u16 body_len);

#endif /* HI5622_STA_FRAMES_H */
