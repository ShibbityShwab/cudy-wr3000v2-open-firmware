/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_rx_live.h - portable MAC RX data-path model for the Hi5622V100
 * clean-room host (Cudy WR3000 v2.0, luofu v2).
 *
 * ORIGINAL clean-room code (lane wifi-rx-static, shared task task-137).  No
 * vendor source/object code, firmware byte, calibration value or per-unit
 * identity is reproduced: the constants are derived facts with [V]/[V*]
 * markers cited in REPORT.md, and the routines are device-free so the host
 * selftest can pin them without a kernel or a bench.
 *
 * Why this file exists (the RX gap): the MAC completion ring stayed at
 * zero captures because the driver polled config-bank +0x4c/+0x50/+0x54.
 * The vendor never reads those words: its live device-side ring pointers sit
 * in machw array word 8 = CA 0x40050000 (band 0), offsets +0x00 status,
 * +0x08 norm-free rptr, +0x0c small-free rptr, +0x10 cmp-ring wptr
 * [V wifi.ko 2.4.15: shuangta_get_host_mac_int_status 0x34af4 reads
 * *[machw+0x20]; shuangta_rx_host_init_dscr_queue 0x37840/0x37954/0x37adc
 * bind +8/+0xc/+0x10 of that word; rodata CA table at .rodata+0xb60; the
 * stock register dump carries 0x40050008 = 0x4005000c = 0x20 while
 * 0x4004004c/50/54 stay 0].  This header pins the live-bank decode, the
 * per-buffer completion header the MAC writes (36 bytes at buffer+4) and the
 * ring arithmetic, all fail-closed.
 *
 * Evidence markers: [V] decoded from the named vendor artifact; [V*] measured
 * by an attended bench lane and quoted from the project reports; [I]
 * inference; [U] unknown / not pinned.
 */
#ifndef HI5622_RX_LIVE_H
#define HI5622_RX_LIVE_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef u8  rx_u8;
typedef u16 rx_u16;
typedef u32 rx_u32;
typedef s32 rx_s32;
#else
#include <stddef.h>
#include <stdint.h>
typedef uint8_t  rx_u8;
typedef uint16_t rx_u16;
typedef uint32_t rx_u32;
typedef int32_t  rx_s32;
#endif

/* ===================================================================
 * 1. The live device-side ring block (machw array word 8) [V]
 *
 * The vendor machw object is 10 mapped register banks filled from the
 * .rodata CA table (band 0: word 5 = 0x40040000 host-interface config,
 * word 8 = 0x40050000 live interrupt/ring pointers).  The driver's inbound
 * region 3 covers CA 0x40000000..0x4011ffff, so the live block is reachable
 * through the transport's range-checked BAR0 translation.
 * =================================================================== */
#define HIL_RX_LIVE_CA_BAND0  0x40050000u  /* [V] rodata table word 8 */
#define HIL_RX_LIVE_CA_BAND1  0x40070000u  /* [V] band-1 analogue */
#define HIL_RX_LIVE_SPAN      0x1000u
#define HIL_RX_LIVE_STATUS    0x00u  /* host MAC interrupt status (live) */
#define HIL_RX_LIVE_NORM_RPTR 0x08u  /* norm free ring device consumer */
#define HIL_RX_LIVE_SMALL_RPTR 0x0cu /* small free ring device consumer */
#define HIL_RX_LIVE_CMP_WPTR  0x10u  /* completion ring device producer */

/* Host-interface config block, CA 0x40040000 band 0 (already programmed by
 * dp_setup's 14-write program; only the refill/publish words are rewritten by
 * the RX drain). */
#define HIL_RX_CFG_WPTR_NORM  0x38u  /* norm free ring producer (packed) */
#define HIL_RX_CFG_WPTR_SMALL 0x3cu
#define HIL_RX_CFG_CMP_RPTR   0x40u  /* completion ring host consumer (packed) */
#define HIL_RX_CFG_INT_STATUS 0x44u  /* W1C status, vendor clear path */
#define HIL_RX_CFG_INT_MASK   0x48u
#define HIL_RX_INT_MASK_INIT  0xfffffcc1u /* [V*] vendor live value + constant */
#define HIL_RX_INT_RX_BIT     2u     /* [V] MAC ISR RX branch tests status bit 2 */

/* ===================================================================
 * 2. Ring geometry [V] (unchanged from dp_rings.h; repeated here so the
 *    RX model is self-contained for the host test)
 * =================================================================== */
#define HIL_RX_NORM_DEPTH   256u
#define HIL_RX_SMALL_DEPTH  512u
#define HIL_RX_CMP_DEPTH    4095u
#define HIL_RX_NORM_BUFLEN  0x63cu /* DMA area after the 4-byte skb word */
#define HIL_RX_SMALL_BUFLEN 0x00b0u
#define HIL_RX_IDX_MASK     0x7fffu
#define HIL_RX_PHASE_BIT    0x8000u
#define HIL_RX_SKB_WORD     4u     /* host bookkeeping word at buffer+0 */

/* ===================================================================
 * 3. Device-written per-buffer completion header [V]
 *
 * shuangta_host_rx_get_msdu_info_dscr (0x3843c) decodes the MAC header the
 * device wrote at buffer+4 (36 bytes: oam_report_dscr copies 36 bytes from
 * skb->data+4), and shuangta_host_rx_get_msdu_info_dscr 0x385f4/
 * shuangta_rx_get_next_sub_msdu 0x3885c read the fields below.  The vendor
 * then does skb_put(len) and skb->data += 40 (0x28), i.e. the MPDU payload
 * starts at buffer+0x28.  The prior dp_txrx capture model treated the
 * descriptor as starting at buffer+0 and was therefore 4 bytes off; the
 * corrected layout is asserted by the host selftest with literal offsets.
 * =================================================================== */
#define HIL_RX_HDR_BYTES      0x24u  /* 36 bytes at buffer+4 */
#define HIL_RX_HDR_LEN_OFF    0x1au  /* u16 payload length, within the header */
#define HIL_RX_HDR_NEXT_OFF   0x20u  /* u32 next sub-MSDU buffer devVA, 0 = end */
#define HIL_RX_HDR_FLAGS9_OFF 0x05u  /* [U] byte buffer+9 (bit 0/7 observed) */
#define HIL_RX_HDR_STATUSB_OFF 0x07u /* [I] byte buffer+0xb (status nibble) */
#define HIL_RX_PAYLOAD_OFF    (HIL_RX_SKB_WORD + HIL_RX_HDR_BYTES) /* 0x28 */
#define HIL_RX_MPDU_MAX       0x618u /* max payload: 0x640 - 0x28 [V check] */

/* Drain action bound (completions consumed per sysfs write). */
#define HIL_RX_DRAIN_MAX      64u

/* Error codes (negative). */
#define HIL_RX_OK       0
#define HIL_RX_EARG    (-1)
#define HIL_RX_ERANGE  (-2)
#define HIL_RX_EFORMAT (-3)
#define HIL_RX_ECHAIN  (-4)  /* sub-MSDU chain not decoded yet: refuse */

/* Decoded view of one completion header (raw bytes are kept by the caller). */
struct hi5622_rx_hdr {
    rx_u16 payload_len;  /* [V] header+0x1a */
    rx_u32 next_devva;   /* [V] header+0x20, 0 = last segment */
    rx_u8  flags9;       /* [U] header+0x05 */
    rx_u8  status_b;     /* [I] header+0x07 */
};
struct hi5622_rx_hdr_raw {
    rx_u8 bytes[HIL_RX_HDR_BYTES];
};

/* -------------------------------------------------------------------
 * Ring arithmetic.  A packed MAC ring index is 15 index bits + one phase
 * bit [V]: hal_set_*_rptr/wptr de-interleave bit 15 as the phase, and
 * hal_ring_get_entry_count (0x31f80) compares low 15 bits + phase byte.
 * ------------------------------------------------------------------- */

/* Entries the producer is ahead of the consumer (completion ring).  Returns
 * HIL_RX_EFORMAT if the two indices cannot describe a valid distance. */
int hi5622_rx_ring_pending(rx_u32 producer, rx_u32 consumer, rx_u32 depth,
                           rx_u32 *count);
/* Slots the device consumer advanced between two snapshots (free rings).
 * Distinguishes "no movement" from "exactly one lap". */
int hi5622_rx_ring_consumed(rx_u32 old_consumer, rx_u32 new_consumer,
                            rx_u32 depth, rx_u32 *count);
/* Advance a packed producer/consumer index by one slot at depth. */
rx_u32 hi5622_rx_idx_advance(rx_u32 packed, rx_u32 depth);
/* Pack (index, phase); refuses index >= depth. */
int hi5622_rx_wptr_pack(rx_u32 index, unsigned int phase, rx_u32 depth,
                        rx_u32 *packed);

/* Parse the 36-byte MAC completion header at buffer+4.  dma_cap is the DMA
 * area length after the skb word (0x63c norm / 0xb0 small).  A non-empty
 * next-sub-MSDU chain is refused (HIL_RX_ECHAIN) until the chain decode is
 * proven, and a length outside 1..dma_cap-0x24 is refused (HIL_RX_EFORMAT). */
int hi5622_rx_hdr_parse(const rx_u8 *hdr, rx_u32 dma_cap,
                        struct hi5622_rx_hdr *out);

/* Classify one 4-byte completion-ring entry (device VA of buffer+4) against
 * the two RX pools: *ring_kind 0 = norm, 1 = small; HIL_RX_ERANGE when the
 * address belongs to neither pool. */
int hi5622_rx_entry_split(rx_u32 entry, rx_u32 norm_lo, rx_u32 norm_hi,
                          rx_u32 small_lo, rx_u32 small_hi, rx_u8 *ring_kind);

/* Live status decode: RX completion pending when status bit 2 is asserted and
 * not masked. */
int hi5622_rx_status_rx_bit(rx_u32 status, rx_u32 mask, rx_u32 *active);

#endif /* HI5622_RX_LIVE_H */
