/* SPDX-License-Identifier: GPL-2.0 */
/*
 * dp_txrx.h - first TX/RX data-plane host logic for the Hi5622V100
 * clean-room driver (Cudy WR3000 v2.0, luofu v2).
 *
 * Lane wifi-dp-txrx (shared task task-122). ORIGINAL clean-room code; every
 * constant below is a decoded protocol fact with an evidence marker:
 *   [V]  decoded from a named vendor artifact (host .ko / FIRMWARE.bin)
 *   [V*] device-measured in an earlier project lane
 *   [I]  inference, must be confirmed on the bench
 *   [U]  unknown - the code fails closed instead of guessing
 *
 * No vendor byte, object or calibration value is embedded here: only derived
 * constants, offsets and boolean protocol rules (see FRAME-SPECS.md).
 * This file is device-free C99 (host selftest + kernel extension share it).
 */
#ifndef DP_TXRX_H
#define DP_TXRX_H

#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/types.h>
typedef u8  dp_u8;
typedef u16 dp_u16;
typedef u32 dp_u32;
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t  dp_u8;
typedef uint16_t dp_u16;
typedef uint32_t dp_u32;
#endif

enum {
    DP_TXRX_OK = 0,
    DP_TXRX_ESHORT = 1,   /* buffer too short */
    DP_TXRX_EARG = 2,     /* bad argument (range/type) */
    DP_TXRX_EBOUNDS = 3,  /* would exceed a ring/buffer/window */
    DP_TXRX_EFORMAT = 4,  /* image fails the fail-closed format check */
    DP_TXRX_EBUSY = 5,    /* not idle / already captured */
    DP_TXRX_EUNKNOWN = 6  /* format still [U]: refuse, do not advance */
};

/* ====================================================================== */
/* 1. TX: device-RAM MSDU-info ring-pointer table entry (12 bytes)       */
/* ====================================================================== */
/* [V] shuangta_tx_msdu_ring_buf_alloc (wifi.ko 2.4.15 0x38f5c):
 *   entry = { ring_device_va, published_wptr, device_consumed_rptr }
 * Ring-init writes {devva, 0, 0}; hmac_tx_msdu_ring_push writes only the
 * packed producer index into +4 after the 8-byte ring entry is visible
 * (dmb ish), and hmac_tx_msdu_ring_proc reads the device-consumed index from
 * +8. This corrects the earlier lane's "[U] dwords 1/2" note.
 * Entry CA = table_base_ca + 12 * (tid + user*8); user <= 0x91, tid <= 7. */
#define DP_TXRX_TABLE_ENTRY_BYTES 12u
#define DP_TXRX_TABLE_ENTRY_DWORDS 3u
#define DP_TXRX_TABLE_ACP_DELTA 0x01000000u /* [V] memmap_get_dev_acp_addr */
#define DP_TXRX_TABLE_USERS 0x92u
#define DP_TXRX_TABLE_TIDS 8u

void     dp_txrx_table_entry_pack(dp_u8 out[DP_TXRX_TABLE_ENTRY_BYTES],
                                  dp_u32 ring_devva, dp_u32 wptr,
                                  dp_u32 rptr);
dp_u32 dp_txrx_table_entry_wptr(const dp_u8 e[DP_TXRX_TABLE_ENTRY_BYTES]);
dp_u32 dp_txrx_table_entry_rptr(const dp_u8 e[DP_TXRX_TABLE_ENTRY_BYTES]);
int      dp_txrx_table_entry_ca(dp_u32 table_ca, unsigned int user,
                                unsigned int tid, dp_u32 *ca);
int      dp_txrx_table_entry_acp(dp_u32 table_ca, unsigned int user,
                                 unsigned int tid, dp_u32 *acp);

/* ====================================================================== */
/* 2. TX: 8-byte MSDU-info ring entry (per-TID host-DRAM ring, depth 64) */
/* ====================================================================== */
/* [V] hmac_tx_msdu_ring_push 0x19174 / shuangta_set_tx_msdu_info_ring:
 *   { u32 buf_devva; u32 ctl }
 *   ctl[11:0]  frame length (skb->len; 12-bit bound)
 *   ctl[14:12] tx path class (0 for the first frame; vendor default branch)
 *   ctl[15]    tx header flag
 *   ctl[19:16] tid / priority nibble
 *   ctl[21:20] vap type: 1/2/3 (vendor hmac_tx_frame_control_dscr_set writes
 *              0x10/0x20/0x30 at ctl byte 2 bits 4/5)
 *   ctl[31:22] not written by the analyzed vendor builder -> zero.
 * The vendor marks a *processed* entry by OR-ing 0xE0 into byte 7
 * (shuangta_tx_msdu_info_ring_magic_set); a fresh entry must not carry it. */
#define DP_TXRX_TXE_BYTES 8u
#define DP_TXRX_TXE_LEN_MASK 0x00000FFFu
#define DP_TXRX_TXE_PATH_SHIFT 12u
#define DP_TXRX_TXE_PATH_MASK 0x00007000u
#define DP_TXRX_TXE_FLAG15 0x00008000u
#define DP_TXRX_TXE_TID_MASK 0x000F0000u
#define DP_TXRX_TXE_VAP_SHIFT 20u
#define DP_TXRX_TXE_VAP_MASK 0x00300000u
#define DP_TXRX_TXE_MAGIC_MASK 0xE0000000u
#define DP_TXRX_TX_RING_DEPTH 64u   /* [V] hmac_tx_msdu_dscr_init */
#define DP_TXRX_TX_RING_BYTES (DP_TXRX_TX_RING_DEPTH * DP_TXRX_TXE_BYTES)
#define DP_TXRX_TX_USER_DEFAULT 0u  /* first rings are handed for user 0 [I] */

enum dp_txrx_vap_type {
    DP_TXRX_VAP_TYPE_1 = 1, /* [V] 0x10 in ctl byte 2 */
    DP_TXRX_VAP_TYPE_2 = 2, /* [V] 0x20 (AP path in the vendor branch) */
    DP_TXRX_VAP_TYPE_3 = 3  /* [V] 0x30 (default sub-branch) */
};

int      dp_txrx_txe_encode(dp_u8 entry[DP_TXRX_TXE_BYTES],
                            dp_u32 buf_devva, dp_u16 frame_len,
                            dp_u8 path_class, dp_u8 hdr_flag,
                            dp_u8 tid, dp_u8 vap_type);
int      dp_txrx_txe_decode(const dp_u8 entry[DP_TXRX_TXE_BYTES],
                            dp_u32 *buf_devva, dp_u32 *ctl);
int      dp_txrx_txe_is_processed(const dp_u8 entry[DP_TXRX_TXE_BYTES]);
void     dp_txrx_txe_mark_processed(dp_u8 entry[DP_TXRX_TXE_BYTES]);

/* packed ring index (index[14:0] | phase<<15) [V] */
dp_u32 dp_txrx_idx_pack(dp_u32 index, unsigned int phase);
void     dp_txrx_idx_unpack(dp_u32 packed, dp_u32 *index,
                            unsigned int *phase);
dp_u32 dp_txrx_idx_next(dp_u32 packed, dp_u32 depth);
/* in-ring distance producer->consumer with packed-index phase semantics.
 * 0 = empty, depth = full; DP_TXRX_EFORMAT if the pair is inconsistent. */
int      dp_txrx_idx_count(dp_u32 producer, dp_u32 consumer, dp_u32 depth);

/* ====================================================================== */
/* 3. TX: frame buffer layout (what the ring entry points at)            */
/* ====================================================================== */
/* [V] hmac_tx_msdu_dscr_add 0x19b40 + shuangta_get_msdu_info_ring_addr
 * 0x38ad8: pskb_expand_head(skb, 0x14, ...); the 20 bytes below skb->data are
 * zeroed and dword0 is replaced with the skb pointer; the buffer is DMA-mapped
 * and the ring entry gets devva(skb->data - 0x10). The device-visible part is
 * therefore the 16 zero bytes at +4..+19 followed by the 802.11 MPDU at +20;
 * bytes +0..+3 are host bookkeeping.
 * Clean-room layout: one coherent buffer per posted frame,
 * [owner u32][16-byte zero DMA header][MPDU]; the MSDU entry devVA is
 * buffer + 4 because the coherent DMA address already is the device VA in
 * this driver's identity translation ([V*] E1 dp_setup read-backs). */
#define DP_TXRX_FRAME_HOST_HDR 4u
#define DP_TXRX_FRAME_DMA_HDR 16u
#define DP_TXRX_FRAME_MPDU_OFF (DP_TXRX_FRAME_HOST_HDR + DP_TXRX_FRAME_DMA_HDR)
#define DP_TXRX_FRAME_AREA 0x400u

int dp_txrx_tx_buffer_init(dp_u8 *buf, size_t cap, dp_u32 owner,
                           const dp_u8 *mpdu, size_t mpdu_len,
                           dp_u32 *buf_devva_off);
int dp_txrx_tx_buffer_check(const dp_u8 *buf, size_t cap);

/* ====================================================================== */
/* 4. TX: 802.11 MPDU builders for the first frames                      */
/* ====================================================================== */
/* 802.11-2016 management/data headers; no FCS (the MAC appends it). A first
 * AP-mode broadcast frame uses addr1=DA=broadcast, addr2=SA=BSSID,
 * addr3=BSSID. The FC->vendor-class mapping is [I] (the vendor derives the
 * class from its own frame object, not from the FC): the ladder starts on the
 * AP data path (vap type 2). */
#define DP_TXRX_FC_PROBE_REQ 0x0040u
#define DP_TXRX_FC_NULL_DATA 0x0048u
#define DP_TXRX_FC_QOS_NULL 0x00C8u
#define DP_TXRX_FC_QOS_DATA 0x0088u
#define DP_TXRX_HDR_LEN 24u
#define DP_TXRX_QOS_HDR_LEN 26u
#define DP_TXRX_MAX_MPDU 0x300u

int dp_txrx_build_probe_req(dp_u8 *out, size_t cap, const dp_u8 bssid[6],
                            dp_u16 seq, size_t *len);
int dp_txrx_build_null_data(dp_u8 *out, size_t cap, const dp_u8 sa[6],
                            dp_u16 seq, size_t *len);
int dp_txrx_build_qos_null(dp_u8 *out, size_t cap, const dp_u8 sa[6],
                           dp_u16 seq, size_t *len);
int dp_txrx_build_arp_bcast(dp_u8 *out, size_t cap, const dp_u8 sa[6],
                            const dp_u8 sip[4], const dp_u8 tip[4],
                            dp_u16 seq, size_t *len);
int dp_txrx_frame_class(dp_u16 fc, dp_u8 *vap_type, dp_u8 *tid,
                        dp_u8 *hdr_flag);

/* ====================================================================== */
/* 5. RX: rings, 0x28-byte descriptor, capture + bookkeeping             */
/* ====================================================================== */
#define DP_TXRX_RX_NORM_DEPTH 256u   /* [V] */
#define DP_TXRX_RX_SMALL_DEPTH 512u  /* [V] */
#define DP_TXRX_RX_CMP_DEPTH 4095u   /* [V] */
#define DP_TXRX_RX_NORM_BUFLEN 0x63Cu
#define DP_TXRX_RX_SMALL_BUFLEN 0x00B0u
#define DP_TXRX_RX_BUF_HDR 4u
#define DP_TXRX_RX_DESC_LEN 0x28u
#define DP_TXRX_RX_MPDU_OFF DP_TXRX_RX_DESC_LEN
#define DP_TXRX_MAC_REG_RXNORM_WPTR 0x38u
#define DP_TXRX_MAC_REG_RXSMALL_WPTR 0x3cu
#define DP_TXRX_MAC_REG_RXCMP_RPTR 0x40u
#define DP_TXRX_MAC_REG_RXCMP_WPTR 0x4cu
#define DP_TXRX_MAC_REG_RXNORM_RPTR 0x50u
#define DP_TXRX_MAC_REG_RXSMALL_RPTR 0x54u
#define DP_TXRX_RX_NORM_ALLOC (DP_TXRX_RX_BUF_HDR + DP_TXRX_RX_NORM_BUFLEN)
#define DP_TXRX_RX_SMALL_ALLOC (DP_TXRX_RX_BUF_HDR + DP_TXRX_RX_SMALL_BUFLEN)

/* Device-written 0x28-byte RX descriptor, observed field accesses only [V]
 * (shuangta_host_rx_get_msdu_info_dscr 0x3843c). The parser accepts a
 * candidate only when payload_len (the vendor's skb_put(len+0x28) field at
 * +0x1e) is inside the buffer; all other semantics stay [I]/[U]. */
struct dp_txrx_rx_desc {
    dp_u16 len_a;       /* +0x08 & 0x0fff  [V] first segment length */
    dp_u16 word0c;      /* +0x0c raw u16   [V] */
    dp_u16 len_b;       /* +0x0e & 0x0fff  [V] second length */
    dp_u32 word10;      /* +0x10 raw u32   [V] device pointer field */
    dp_u16 word14;      /* +0x14 raw u16   [V] */
    dp_u16 word16;      /* +0x16 raw u16   [V] */
    dp_u16 len_c;       /* +0x18 & 0x0fff  [V] length */
    dp_u8  band_vap;    /* +0x19 bits 6:5  [I] band/vap */
    dp_u8  flags_1b;    /* +0x1b bits 7:4/3:0 [U] */
    dp_u16 word1c;      /* +0x1c >> 4      [V] */
    dp_u16 payload_len; /* +0x1e full u16  [V] vendor skb_put length */
    dp_u16 word20;      /* +0x20 & 0x0fff  [V] length */
    dp_u8  flags_21;    /* +0x21 bits 6:4  [U] */
    dp_u8  flags_23;    /* +0x23 bits 3:0,bit7 [U] */
};
int dp_txrx_rx_desc_parse(const dp_u8 *buf, size_t cap,
                          struct dp_txrx_rx_desc *out);

#define DP_TXRX_RX_CAPTURE_MPDU 128u
#define DP_TXRX_RX_CAPTURE_MAX 16u
struct dp_txrx_rx_capture {
    dp_u32 cmp_raw;    /* 4-byte rx_data_cmp_ring entry, format [U] */
    dp_u32 ring_index; /* free-ring slot the completion maps to [U] */
    dp_u8  ring_kind;  /* 0=norm (256), 1=small (512) */
    dp_u8  desc_ok;    /* descriptor parse accepted */
    dp_u8  desc[DP_TXRX_RX_DESC_LEN];
    dp_u16 mpdu_len;
    dp_u8  mpdu[DP_TXRX_RX_CAPTURE_MPDU];
};
int dp_txrx_rx_capture_build(const dp_u8 *ring_buf, size_t ring_cap,
                             dp_u8 ring_kind, dp_u32 ring_index,
                             struct dp_txrx_rx_capture *out);

/* RX bookkeeping. The host owns packed rptrs for the two free rings and the
 * completion ring. The completion-word -> (ring, slot) decoder is [U]; until
 * the bench capture fixes it the ladder runs capture-only (dp_txrx_rx_plan +
 * no commit) and the consume path needs the decoder's ring kind, is strictly
 * sequential (slot must equal the current free-ring rptr) and fails closed. */
struct dp_txrx_rx_state {
    dp_u32 norm_rptr;
    dp_u32 small_rptr;
    dp_u32 cmp_rptr;
    dp_u32 captured;  /* raw completion words captured this load */
    dp_u32 consumed;  /* completion entries consumed (pointers moved) */
    dp_u32 unknown;   /* refused completion entries */
    dp_u32 last_cmp_raw;
};
void dp_txrx_rx_state_init(struct dp_txrx_rx_state *st);
int  dp_txrx_rx_pending(const struct dp_txrx_rx_state *st,
                        dp_u32 device_cmp_wptr, dp_u32 *count);
int  dp_txrx_rx_plan(const dp_u8 *cmp_ring, size_t cmp_cap,
                     dp_u32 device_cmp_wptr, struct dp_txrx_rx_state *st,
                     struct dp_txrx_rx_capture *out);
int  dp_txrx_rx_commit(struct dp_txrx_rx_state *st, dp_u8 ring_kind);
int  dp_txrx_rx_free_entry(const dp_u32 *entries, size_t entries_count,
                           dp_u32 host_rptr, dp_u32 *entry);
int  dp_txrx_rx_dma_ok(const struct dp_txrx_rx_desc *d, size_t ring_buflen);

/* ====================================================================== */
/* 7. TXQ table: the device consumer's queue view (task-134 tuning)      */
/* ====================================================================== */
/* [V] wifi.ko 2.4.15 hmac_update_user_tid_txq_table 0x1b294, called by
 * hmac_tx_skb_pop_tid 0x19378 immediately AFTER the per-TID ring entries
 * were pushed (hmac_tx_msdu_ring_push) and their packed producer index was
 * published into the device-RAM MSDU-info table entry:
 *
 *   table dword0 low 16 bits      = jiffies_to_msecs(jiffies) (publish clock)
 *   per-user block (user <= 0x7f) = table + 0x524 + user*0x40
 *     block + tid*2               = u16 queued MSDU count for this TID
 *     block + 0x10 + tid*2        = u16 start sequence number for this TID
 *     block + 0x20 + tid*4        = u32 queued byte count for this TID
 *   table byte + 0x10 + user      = per-user TID-non-empty bitmap (bit tid)
 *
 * The table is the 0x2524-byte host-DRAM table whose device VA the driver
 * hands to the firmware with the g0 id1 key-0x0320 sync.  The vendor's
 * enqueue path publishes this queue state after every ring push; the earlier
 * clean-room lanes published only the ring wptr and left the table zero (a
 * zero bitmap says "no queued frame" to whatever consumes it).  [V] layout
 * and order from the shipped host module; [I] that this publish is the device
 * consumer's wake-up condition - the bench ladder settles it.  Users > 0x7f
 * live in a different small block in the vendor code [V]; this clean-room
 * host owns user 0 only and refuses the rest.  Byte stores only, no alignment
 * assumption, device-free for the host selftest.
 */
#define DP_TXRX_TXQ_TABLE_BYTES     0x2524u
#define DP_TXRX_TXQ_USER_BLOCKS_OFF 0x524u
#define DP_TXRX_TXQ_USER_BLOCK_BYTES 0x40u
#define DP_TXRX_TXQ_USER_MAX        0x7fu
#define DP_TXRX_TXQ_TS_OFF          0x00u
#define DP_TXRX_TXQ_FLAGS_OFF       0x10u
#define DP_TXRX_TXQ_QLEN_OFF        0x00u
#define DP_TXRX_TXQ_START_SN_OFF    0x10u
#define DP_TXRX_TXQ_BYTES_OFF       0x20u

/* Byte offset of the per-user queue block, or DP_TXRX_TXQ_USER_MAX + 1 for a
 * refused user index. */
dp_u32 dp_txrx_txq_user_block(unsigned int user);
/* Set/clear the TID bit in the per-user non-empty bitmap (pure). */
dp_u8  dp_txrx_txq_flags_set(dp_u8 flags, unsigned int tid, bool nonempty);
/* Publish one TID's queue state into the caller's 0x2524-byte table buffer.
 * No-op (no byte written) when an argument is out of range; the caller orders
 * it after the ring-entry/wptr publish with dma_wmb(). */
void   dp_txrx_txq_publish(void *table, unsigned int user, unsigned int tid,
                           dp_u32 count, dp_u32 start_sn, bool has_sn,
                           dp_u32 bytes, dp_u16 msec);

/* little-endian helpers shared by both worlds */
dp_u32 dp_txrx_le32(const dp_u8 *p);
void     dp_txrx_put_le16(dp_u8 *p, dp_u16 v);
void     dp_txrx_put_le32(dp_u8 *p, dp_u32 v);

#endif /* DP_TXRX_H */
