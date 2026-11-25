/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_rx_format.h - the MAC RX ring entry + per-buffer 36-byte device
 * header format of the Hi5622V100 clean-room host (Cudy WR3000 v2.0, luofu
 * v2), as a device-free, fail-closed model.
 *
 * ORIGINAL clean-room code (lane wifi-rx-format, shared task task-163).  No
 * vendor source/object code, firmware byte, calibration value or per-unit
 * identity is reproduced: every constant below is a derived fact from the
 * vendor host module decode (function names + offsets are cited in
 * REPORT.md, evidence markers [V]/[V*]/[I]) and the routines are device-free
 * so the host selftest can pin them without a kernel or a bench.
 *
 * The contract in one paragraph.  The MAC ring entries are 4-byte little
 * endian *device virtual addresses*; a free-ring (SW2HW producer) entry is
 * the devVA of the buffer's 36-byte device-header start, i.e. buffer+4 (the
 * first 4 bytes of the allocation are a host-owned bookkeeping word the
 * device never touches).  The host pre-writes only: (a) the 4-byte host word
 * at buffer+0 (the vendor stores the skb pointer there; this driver stores a
 * cookie), and (b) a zeroed 36-byte header area at buffer+4..buffer+0x27.
 * Everything else in the header is written by the device before it echoes
 * the same devVA into the completion ring.  The frame payload starts at
 * buffer+0x28.  Buffer capacity = 4 + configured buffer length (0x640 norm /
 * 0xb4 small band 0); the device-writable area is exactly the configured
 * length (0x63c / 0xb0).
 */
#ifndef HI5622_RX_FORMAT_H
#define HI5622_RX_FORMAT_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef u8  hif_u8;
typedef u16 hif_u16;
typedef u32 hif_u32;
typedef s32 hif_s32;
#else
#include <stddef.h>
#include <stdint.h>
typedef uint8_t  hif_u8;
typedef uint16_t hif_u16;
typedef uint32_t hif_u32;
typedef int32_t  hif_s32;
#endif

/* -------------------------------------------------------------------
 * 1. Geometry (band 0; vendor shuangta_rx_host_init_dscr_queue)  [V]
 * ------------------------------------------------------------------- */
#define HIF_RX_ENTRY_BYTES      4u     /* every ring entry: 4-byte devVA */
#define HIF_RX_HOST_WORD        4u     /* host bookkeeping word at buffer+0 */
#define HIF_RX_NORM_DEPTH       256u
#define HIF_RX_SMALL_DEPTH      512u
#define HIF_RX_CMP_DEPTH        4095u
#define HIF_RX_NORM_BUFLEN      0x63cu  /* device-writable length, norm ring */
#define HIF_RX_SMALL_BUFLEN     0x00b0u /* device-writable length, small ring */
#define HIF_RX_NORM_ALLOC       (HIF_RX_HOST_WORD + HIF_RX_NORM_BUFLEN)  /* 0x640 */
#define HIF_RX_SMALL_ALLOC      (HIF_RX_HOST_WORD + HIF_RX_SMALL_BUFLEN)/* 0xb4 */
#define HIF_RX_HDR_BYTES        0x24u   /* 36-byte device header at buffer+4 */
#define HIF_RX_PAYLOAD_OFF      (HIF_RX_HOST_WORD + HIF_RX_HDR_BYTES)    /* 0x28 */
#define HIF_RX_NORM_MPDU_MAX    (HIF_RX_NORM_ALLOC - HIF_RX_PAYLOAD_OFF) /* 0x618 */
#define HIF_RX_SMALL_MPDU_MAX   (HIF_RX_SMALL_ALLOC - HIF_RX_PAYLOAD_OFF)/* 0x8c */
#define HIF_RX_IDX_MASK         0x7fffu
#define HIF_RX_PHASE_BIT        0x8000u

/* -------------------------------------------------------------------
 * 2. The 36-byte device-written header (offsets within the header; the
 *    absolute buffer address is header base + offset).  Every offset below
 *    is read by the vendor decoder shuangta_host_rx_get_msdu_info_dscr
 *    (0x3843c) unless marked [I].  The header base itself is buffer+4.
 * ------------------------------------------------------------------- */
#define HIF_RX_HDR_PPDU_ADDR    0x00u  /* u32 devVA of the PPDU info dscr [V] */
#define HIF_RX_HDR_W04          0x04u  /* u16 bits11:0 copied to rx_ctl+0x19 [V] */
#define HIF_RX_HDR_FLAGS05      0x05u  /* byte: bit4/bit6/bit7 are rx_ctl flags [V] */
#define HIF_RX_HDR_SUBMSDU_NUM  0x06u  /* u8 number of sub-MSDUs [V] */
#define HIF_RX_HDR_STATUS       0x07u  /* byte: bits7:4 = rx status nibble [V];
					* bit1 = a rx_ctl flag bit [V] */
#define HIF_RX_HDR_W08          0x08u  /* u16 bits2:0 -> rx_ctl flags; bits15:4
					* -> 12-bit field [V] */
#define HIF_RX_HDR_W0A          0x0au  /* u16 bits11:0 -> 12-bit field [V] */
#define HIF_RX_HDR_TS           0x0cu  /* u32 -> rx_ctl+0x21 (timestamp/TSF) [I] */
#define HIF_RX_HDR_W10          0x10u  /* u16 -> rx_ctl+0x25 [V] */
#define HIF_RX_HDR_FCTRL        0x12u  /* u16 copy of the frame-control word [V] */
#define HIF_RX_HDR_W14          0x14u  /* u16 bits11:0 [V] */
#define HIF_RX_HDR_FLAGS15      0x15u  /* byte bits5:4 [V] */
#define HIF_RX_HDR_B16          0x16u  /* byte -> rx_ctl+0x04 [V] */
#define HIF_RX_HDR_B17          0x17u  /* byte: low nibble/high nibble [V] */
#define HIF_RX_HDR_W18          0x18u  /* u16 bits15:4; low nibble tested [V] */
#define HIF_RX_HDR_LEN          0x1au  /* u16 payload length (MPDU bytes) [V] */
#define HIF_RX_HDR_W1C          0x1cu  /* u16 bits11:0 [V] */
#define HIF_RX_HDR_B1E          0x1eu  /* byte [V] */
#define HIF_RX_HDR_B1F          0x1fu  /* byte: low nibble + bit7 [V] */
#define HIF_RX_HDR_NEXT         0x20u  /* u32 next sub-MSDU devVA, 0 = last [V] */

/* Total device-writable area needed for a header+payload of len bytes:
 * 4 (host word) + 36 (header) + len.  The vendor bounds it as
 * len + 0x28 <= allocation (0x640 band 0). */
#define HIF_RX_FRAME_NEED(len)  ((hif_u32)(len) + HIF_RX_PAYLOAD_OFF)

/* -------------------------------------------------------------------
 * 3. Host-word cookie: replaces the vendor's skb pointer (this driver has no
 *    skb per RX buffer).  It proves the buffer is one we published and gives
 *    the device-write witness a clean baseline: before publishing, the host
 *    zeroes the 36-byte header and writes the cookie; therefore any nonzero
 *    header byte in a buffer the completion ring never mentioned means the
 *    MAC DMA'd a frame into it (see hif_rx_buffer_witness).
 * ------------------------------------------------------------------- */
#define HIF_RX_COOKIE_MAGIC     0x52580000u /* "RX" in the high half */
#define HIF_RX_COOKIE_KIND_BIT  0x00008000u /* bit15: 1 = small ring */
#define HIF_RX_COOKIE_SLOT_BITS 0x00007fffu

/* -------------------------------------------------------------------
 * 4. Error codes (negative; 0 = OK)
 * ------------------------------------------------------------------- */
#define HIF_RX_OK      0
#define HIF_RX_EARG    (-1)
#define HIF_RX_ERANGE  (-2)
#define HIF_RX_EFORMAT (-3)
#define HIF_RX_ESTALE  (-4)  /* host-word cookie does not match the slot */
#define HIF_RX_ERESET  (-5)  /* live consumer movement cannot be explained */

/* Decoded view of one device-written header. */
struct hif_rx_hdr {
    hif_u32 ppdu_devva;  /* +0x00 */
    hif_u16 payload_len; /* +0x1a */
    hif_u32 next_devva;  /* +0x20, 0 = last sub-MSDU */
    hif_u8  submsdu_num; /* +0x06 */
    hif_u8  status;      /* +0x07, status nibble in bits7:4 */
    hif_u8  flags05;     /* +0x05 */
    hif_u16 fctrl;       /* +0x12 copy of the frame-control word */
};

/* Witness classification of one published, not-yet-consumed buffer. */
#define HIF_RX_BUF_UNTOUCHED  0 /* cookie intact, header still all zero */
#define HIF_RX_BUF_WITNESS    1 /* cookie intact, header nonzero: MAC wrote */
#define HIF_RX_BUF_STALE     (-1) /* cookie mismatch: not our published buffer */
#define HIF_RX_BUF_BAD       (-2) /* allocation/layout argument refused */

/* -------------------------------------------------------------------
 * 5. Functions
 * ------------------------------------------------------------------- */

/* Encode the free-ring entry for a buffer: entry = buf_dma + 4.  Requires the
 * DMA address to be 4-byte aligned (vendor skb->data is 4-aligned and the
 * rejected-by-device values are the unaligned ones) and non-zero. */
int hif_rx_entry_encode(hif_u32 buf_dma, hif_u32 *entry);
/* Inverse: buffer DMA address from a completion/free-ring entry. */
int hif_rx_entry_decode(hif_u32 entry, hif_u32 *buf_dma);
/* Classify a completion entry against the two pool address windows. */
int hif_rx_entry_classify(hif_u32 entry, hif_u32 norm_lo, hif_u32 norm_hi,
                          hif_u32 small_lo, hif_u32 small_hi, hif_u8 *kind);

/* Host-word cookie pack/check (kind 0 = norm, 1 = small). */
int hif_rx_cookie_pack(hif_u8 kind, hif_u32 slot, hif_u32 *cookie);
int hif_rx_cookie_check(hif_u32 cookie, hif_u8 kind, hif_u32 slot);

/* Prepare a buffer for publication exactly as the vendor does on every
 * add_buff: zero the 36-byte header area (so stale length/next can never
 * survive a recycle) and write the host-word cookie.  alloc is the whole
 * allocation (4 + configured length). */
int hif_rx_buffer_prepare(hif_u8 *buf, hif_u32 alloc, hif_u8 kind,
                          hif_u32 slot);
/* Read-only witness classification (never writes). */
int hif_rx_buffer_witness(const hif_u8 *buf, hif_u32 alloc, hif_u8 kind,
                          hif_u32 slot);
/* True if the header area is all-zero (the post-prepare baseline). */
int hif_rx_header_is_zero(const hif_u8 *hdr);

/* Parse + validate a device-written header at header base hdr (buffer+4).
 * dma_len is the configured device-writable length (0x63c / 0xb0): the
 * payload check is len + 0x28 <= 4 + dma_len, the vendor's bound. */
int hif_rx_hdr_parse(const hif_u8 *hdr, hif_u32 dma_len,
                     struct hif_rx_hdr *out);

/* Free-ring refill budget, fail-closed.  posted = producer - live consumer
 * (15-bit index + phase, [0..depth]); delta = live consumer movement since
 * the last snapshot.  The device can only have consumed entries the host has
 * actually posted, so delta <= depth - posted; anything else means a live
 * pointer jumped (reset/desync) and the caller must not publish.  On success
 * *fillable = delta (bounded again by depth - posted). */
int hif_rx_refill_budget(hif_u32 depth, hif_u32 posted, hif_u32 delta,
                         hif_u32 *fillable);

/* Vendor stock mirror of the MAC host-interface format words [V*]:
 * 0x4004001c=0x00b0063c, +0x2c=0x100, +0x30=0x200, +0x34=0xfff,
 * +0x38=0x8000, +0x3c=0x8000, +0x40=0, +0x48=0xfffffcc1.
 * Returns 1 and fills *value when off is one of the format words, 0 when
 * the word is not part of the contract, HIF_RX_EARG on a bad argument. */
int hif_rx_cfg_expect(hif_u32 off, hif_u32 *value);
#define HIF_RX_CFG_WORDS 8u

#endif /* HI5622_RX_FORMAT_H */
