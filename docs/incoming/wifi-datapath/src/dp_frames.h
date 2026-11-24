/* SPDX-License-Identifier: GPL-2.0 */
/*
 * dp_frames.h - data-plane message/frame builders for the Hi5622V100 clean-room host.
 *
 * Lane wifi-datapath (shared task task-105), original code. No vendor text/code is copied;
 * the constants below are decoded protocol facts/addresses (see REPORT.md evidence table).
 *
 * Two distinct families:
 *  1) HCC control messages (12-byte header + payload) carried on the ETE SR ring:
 *     group 0 id 1  = hdpp config event   (payload+0x08 = u16 hdpp command/key)
 *     group 0 id 2  = alg config event    (payload+0x08 = u16 alg cfg id)
 *     group 0 id 7  = hmac_user_add (0xa8 B payload, not synthesizable - catalogue only)
 *     ... see dp_msg_catalogue[] for the map to the firmware handler CAs.
 *  2) MAC host-interface data rings (slot-5 block CA 0x40040000/0x40060000), NOT the ETE
 *     rings: per-frame TX MSDU-info entries are 8 bytes and RX completion buffers carry a
 *     0x28-byte device-written descriptor before the 802.11 MPDU. Encoders/constants here.
 */
#ifndef DP_FRAMES_H
#define DP_FRAMES_H

#include <stddef.h>
#include <stdint.h>

/* ---- HCC message layer (12-byte header) ------------------------------------------- */
#define DP_HDR_LEN        12u
#define DP_TAG            0x5A5Au
#define DP_MAX_PAYLOAD    0x674u
#define DP_MAX_FRAME      0x680u
/* group-0 config event: token[8] | u16 key | u16 len | data[len] | pad[4] */
#define DP_CFG_PAYLOAD    0x10u
#define DP_CFG_TOTAL(len) (DP_HDR_LEN + DP_CFG_PAYLOAD + (uint32_t)(len))

/* firmware config keys (u16 at payload+0x08 of a group-0 id-1 event) ----------------- */
#define DP_KEY_CTX_CREATE   0x0064u  /* g0 id1 -> fw worker CA 0x449E5 -> CA 0xFE6BC */
#define DP_KEY_TXQ_SYNC     0x00D8u  /* g0 id1 -> fw worker CA 0x81039 (vendor capture) */
#define DP_KEY_TXQ_ADDR     0x0320u  /* g0 id1 -> fw worker CA 0x81445 (txq table addr) */
#define DP_KEY_MODULE_FLAG  0x0004u  /* g0 id1 -> fw worker CA 0x811CD (needs created ctx) */

/* ---- MAC slot-5 data-ring block (device CA 0x40040000 band0 / 0x40060000 band1) ---- */
#define DP_MAC_BLOCK_BAND0  0x40040000u
#define DP_MAC_BLOCK_BAND1  0x40060000u
#define DP_MAC_REG_TXBA_DEPTH   0x00u   /* tx_ba_info_buff_depth (bits 15:0)            */
#define DP_MAC_REG_TXBA_ADDR    0x04u   /* tx_ba_info_buf_addr (device VA)              */
#define DP_MAC_REG_MSDU_TBL     0x10u   /* msdu_info_ring_ptr_table_base (device VA)    */
#define DP_MAC_REG_RX_BUFLEN    0x1Cu   /* norm[15:0] | small[31:16]                     */
#define DP_MAC_REG_RXNORM_ADDR  0x20u
#define DP_MAC_REG_RXSMALL_ADDR 0x24u
#define DP_MAC_REG_RXCMP_ADDR   0x28u
#define DP_MAC_REG_RXNORM_SIZE  0x2Cu   /* bits 11:0                                   */
#define DP_MAC_REG_RXSMALL_SIZE 0x30u   /* bits 11:0                                   */
#define DP_MAC_REG_RXCMP_SIZE   0x34u   /* bits 11:0                                   */
#define DP_MAC_REG_RXNORM_WPTR  0x38u   /* idx[14:0] + phase bit15                     */
#define DP_MAC_REG_RXSMALL_WPTR 0x3Cu
#define DP_MAC_REG_RXCMP_RPTR   0x40u
#define DP_MAC_REG_INT_STATUS   0x44u   /* write 1 to clear                            */
#define DP_MAC_REG_INT_MASK     0x48u   /* vendor live value 0xfffffcc1                */
#define DP_MAC_REG_RXCMP_WPTR   0x4Cu
#define DP_MAC_REG_RXNORM_RPTR  0x50u
#define DP_MAC_REG_RXSMALL_RPTR 0x54u

#define DP_RX_NORM_BUFLEN   0x063Cu  /* live vendor value 1596 B / 12 + 0x28 hdr?   */
#define DP_RX_SMALL_BUFLEN  0x00B0u
#define DP_RX_DESC_LEN      0x28u    /* device-written per-buffer RX descriptor     */
#define DP_RXNORM_RING_SIZE 256u
#define DP_RXSMALL_RING_SIZE 512u
#define DP_RXCMP_RING_SIZE  4095u
#define DP_TXBA_RING_DEPTH  256u

/* TX MSDU-info ring entry (8 B): {u32 buf_devva; u32 ctl} --------------------------- */
#define DP_TXE_LEN_MASK    0x00000FFFu  /* ctl[11:0]  = 802.11 frame length            */
#define DP_TXE_PATH_SHIFT  12u          /* ctl[14:12] = tx path class (3 bits)         */
#define DP_TXE_PATH_MASK   0x00007000u
#define DP_TXE_FLAG15      0x00008000u  /* ctl[15]    = tx-header flag bit             */
#define DP_TXE_TID_MASK    0x000F0000u  /* ctl[19:16] = tid / priority nibble          */
#define DP_TXE_VAP_SHIFT   20u          /* ctl[21:20] = vap type (1 STA, 2 AP, 3 ?)    */
#define DP_TXE_VAP_MASK    0x00300000u
#define DP_TX_ENTRY_LEN    8u

/* TX MSDU-info ring index: low 15 bits index, bit 15 phase (vendor rw_index_check) --- */
#define DP_TXIDX_INDEX_MASK 0x00007FFFu
#define DP_TXIDX_PHASE      0x00008000u

/* ---- small helpers ---------------------------------------------------------------- */
struct dp_frame {
    uint8_t buf[DP_MAX_FRAME];
    size_t  len;
};

enum dp_err {
    DP_OK = 0,
    DP_ESHORT = 1,
    DP_ETAG,
    DP_ELEN,
    DP_EBOUNDS,
    DP_EALLOC,
    DP_EDST,
    DP_EGROUP,
    DP_EARG,
    DP_EID
};

/* frame model */
void        dp_frame_reset(struct dp_frame *f);
int         dp_frame_validate(const uint8_t *buf, size_t len);
int         dp_frame_decode(const uint8_t *buf, size_t len, uint8_t *group,
                            uint16_t *id, uint16_t *total);
/* group-0 config-event builders (id 1 = hdpp, id 2 = alg) */
int         dp_build_g0_cfg(struct dp_frame *f, uint8_t id, uint8_t fb,
                            uint16_t key, const uint8_t *data, uint16_t len);
int         dp_build_ctx_create(struct dp_frame *f, uint8_t type, uint8_t parent,
                                uint8_t user, uint8_t fb);
int         dp_build_txq_sync_capture(struct dp_frame *f);
int         dp_build_txq_addr_sync(struct dp_frame *f, uint32_t txq_devva, uint8_t fb);
int         dp_frame_export(const struct dp_frame *f, uint8_t *out, size_t cap,
                            size_t pad_to, uint8_t pad_byte, size_t *out_len);

/* TX data ring */
int         dp_tx_msdu_entry_encode(uint8_t entry[DP_TX_ENTRY_LEN], uint32_t buf_devva,
                                    uint16_t frame_len, uint8_t path_class,
                                    uint8_t hdr_flag, uint8_t tid, uint8_t vap_type);
uint32_t    dp_tx_msdu_entry_len(uint32_t ctl);
uint32_t    dp_tx_msdu_entry_buf(const uint8_t entry[DP_TX_ENTRY_LEN]);
uint32_t    dp_tx_idx_pack(uint32_t index, unsigned phase);
void        dp_tx_idx_unpack(uint32_t packed, uint32_t *index, unsigned *phase);
uint32_t    dp_tx_idx_next(uint32_t packed, uint32_t depth);

/* message catalogue (all families; firmware handler CAs are image facts) */
enum dp_kind { DP_CTRL = 0, DP_CFG = 1, DP_MGMT = 2, DP_DATA = 3, DP_EVENT = 4 };
struct dp_msg_row {
    uint8_t  group;
    uint16_t id;
    const char *sender;
    uint32_t fw_handler_ca;
    uint16_t fixed_payload;   /* 0 = variable / unknown */
    enum dp_kind kind;
};
const struct dp_msg_row *dp_msg_catalogue(size_t *count);
const struct dp_msg_row *dp_msg_lookup(uint8_t group, uint16_t id);

#endif /* DP_FRAMES_H */
