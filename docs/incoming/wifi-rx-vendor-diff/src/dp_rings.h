/* SPDX-License-Identifier: GPL-2.0 */
/*
 * dp_rings.h - portable data-plane geometry, register-program model and ring
 * encoders for the Hi5622V100 clean-room host (Cudy WR3000 v2.0, luofu v2).
 *
 * ORIGINAL clean-room code written by the wifi-datapath-bringup lane (shared
 * task task-111, 2026-11-25) from the project's committed decoded contract
 * (wifi-datapath REPORT/FRAME-SPECS) plus the static decode done in
 * tools/dp_static_decode.py (vendor .ko / FIRMWARE.bin read locally; only
 * derived constants are committed - no vendor bytes, code text or firmware
 * image material is reproduced here).
 *
 * Why this file: the ring geometry, the device-RAM MSDU-info table arithmetic,
 * the 4-byte RX free-ring entries and the MAC slot-5 register program are the
 * parts that must be testable on the host without a device and without the
 * kernel.  hi5622_dp.c only adds the DMA allocation and the MMIO writes.
 *
 * Evidence markers: [V] decoded from a named vendor artifact; [V*] measured by
 * an attended bench lane and cited in the project reports; [I] inference;
 * [U] unknown/not pinned.
 */
#ifndef DP_RINGS_H
#define DP_RINGS_H

#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/types.h>
typedef u8  dp_u8;
typedef u16 dp_u16;
typedef u32 dp_u32;
typedef s32 dp_s32;
#else
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t  dp_u8;
typedef uint16_t dp_u16;
typedef uint32_t dp_u32;
typedef int32_t  dp_s32;
#endif

/* ===================================================================
 * 1. MAC host-interface block (slot 5) [V: phase43 S5 + wifi.ko hal_set_*]
 * =================================================================== */
#define DP_MAC_CA_BAND0        0x40040000u  /* 2.4 GHz band, host interface */
#define DP_MAC_CA_BAND1        0x40060000u  /* 5 GHz band (NOT programmed yet) */

#define DP_MAC_REG_TXBA_DEPTH  0x00u  /* tx_ba_info_buff_depth (bits 15:0) */
#define DP_MAC_REG_TXBA_ADDR   0x04u  /* tx_ba_info_buf_addr (device VA) */
#define DP_MAC_REG_MSDU_TBL    0x10u  /* msdu_info_ring_ptr_table_base (dev VA) */
#define DP_MAC_REG_RX_BUFLEN   0x1cu  /* norm[15:0] | small[31:16] */
#define DP_MAC_REG_RXNORM_ADDR 0x20u
#define DP_MAC_REG_RXSMALL_ADDR 0x24u
#define DP_MAC_REG_RXCMP_ADDR  0x28u
#define DP_MAC_REG_RXNORM_SIZE 0x2cu  /* bits 11:0 */
#define DP_MAC_REG_RXSMALL_SIZE 0x30u /* bits 11:0 */
#define DP_MAC_REG_RXCMP_SIZE  0x34u  /* bits 11:0 */
#define DP_MAC_REG_RXNORM_WPTR 0x38u  /* idx[14:0] | phase<<15 */
#define DP_MAC_REG_RXSMALL_WPTR 0x3cu
#define DP_MAC_REG_RXCMP_RPTR  0x40u
#define DP_MAC_REG_INT_STATUS  0x44u  /* W1C, host ISR */
#define DP_MAC_REG_INT_MASK    0x48u  /* vendor live 0xfffffcc1 */
#define DP_MAC_REG_RXCMP_WPTR  0x4cu  /* device producer */
#define DP_MAC_REG_RXNORM_RPTR 0x50u  /* device consumer */
#define DP_MAC_REG_RXSMALL_RPTR 0x54u

#define DP_MAC_INT_MASK_INIT   0xfffffcc1u  /* [V] hal_set_host_intr_mask caller */
#define DP_MAC_REG_COUNT       14u          /* entries in the program model */

/* Ring index packing for the MAC rings: low 15 bits index, bit 15 phase.
 * Live vendor values: +0x38/+0x3c read 0x8000 = index 0, phase 1 after the
 * free rings were filled and wrapped once [V*]. */
#define DP_RING_IDX_MASK       0x7fffu
#define DP_RING_PHASE          0x8000u
#define DP_RING_WPTR_FULL      0x8000u  /* after filling exactly depth entries */

/* ===================================================================
 * 2. Host-DRAM rings and buffers [V: wifi.ko shuangta_rx_host_init_dscr_queue,
 *    shuangta_ba_info_res_alloc, hmac_txq_table_init, hmac_tx_msdu_dscr_init]
 * =================================================================== */
#define DP_TXQ_TABLE_BYTES     0x2524u  /* host DRAM, zeroed, oal_noncache_alloc */
#define DP_TXQ_BAND_STRIDE     0x252cu  /* firmware RAM stride per band [V fw] */
#define DP_TX_MSDU_RINGS       8u       /* one ring per TID (0..7) */
#define DP_TX_MSDU_RING_DEPTH  0x40u    /* 64 entries; vendor slot +0x38 default */
#define DP_TX_MSDU_ENTRY_BYTES 8u
#define DP_TX_MSDU_RING_BYTES  (DP_TX_MSDU_RINGS * DP_TX_MSDU_RING_DEPTH * DP_TX_MSDU_ENTRY_BYTES)
#define DP_TXBA_DEPTH          256u     /* oal_noncache_alloc(0x800) = 256 x 8 */
#define DP_TXBA_DEPTH_INIT     0x100u
#define DP_TXBA_BYTES          0x800u
#define DP_TXBA_ENTRY_BYTES    8u

#define DP_RX_NORM_DEPTH       256u
#define DP_RX_SMALL_DEPTH      512u
#define DP_RXCMP_DEPTH         4095u
#define DP_RX_NORM_BUFLEN      0x63cu   /* rx_norm_buff_len */
#define DP_RX_SMALL_BUFLEN     0xb0u    /* rx_small_buff_len */
#define DP_RX_BUF_HDR          4u       /* host bookkeeping word before the DMA area */
#define DP_RX_NORM_STRIDE      (DP_RX_BUF_HDR + DP_RX_NORM_BUFLEN)
#define DP_RX_SMALL_STRIDE     (DP_RX_BUF_HDR + DP_RX_SMALL_BUFLEN)
/* Every ring entry is one 4-byte device VA of a buffer/entry [V]:
 * norm free ring oal_noncache_alloc(0x400) = 256 x 4, small = 512 x 4,
 * completion ring = 0x3ffc = 4095 x 4. */
#define DP_RX_RING_ENTRY_BYTES 4u
#define DP_RX_NORM_RING_BYTES  (DP_RX_NORM_DEPTH * DP_RX_RING_ENTRY_BYTES)
#define DP_RX_SMALL_RING_BYTES (DP_RX_SMALL_DEPTH * DP_RX_RING_ENTRY_BYTES)
#define DP_RXCMP_RING_BYTES    (DP_RXCMP_DEPTH * DP_RX_RING_ENTRY_BYTES)

/* ===================================================================
 * 3. Device-RAM MSDU-info ring-pointer table (the ready g4 id2 payload)
 *
 * The firmware's g4 id2 payload dword0 (0x01060850 on the bench) is the
 * PKTRAM device CA of a table with 12-byte entries; MAC +0x10 receives its
 * ACP alias (+0x01000000, live value 0x02060850) [V/V*].  The vendor resolves
 * entry (user,tid) as base + 12*(tid + user*8) with user <= 0x91 and writes
 * {ring_device_va, 0, 0} into it (hmac_get_user_tid_tx_msdu_info_ring_table_addr
 * + shuangta_tx_msdu_ring_buf_alloc) [V].
 * =================================================================== */
#define DP_MSDU_TABLE_ENTRY_BYTES 12u
#define DP_MSDU_TABLE_USER_STRIDE 8u    /* index = tid + user*8 */
#define DP_MSDU_TABLE_MAX_USER    0x91u
#define DP_MSDU_TABLE_MAX_TID     7u
#define DP_MSDU_TABLE_ACP_DELTA   0x01000000u  /* PKTRAM CA -> ACP alias [V*] */
#define DP_MSDU_TABLE_ENTRY_DWORDS 3u   /* {ring_devva, 0, 0} */

/* Device memory windows used to range-check the payload/table (contract) */
#define DP_PKTRAM_CA_LO       0x01000000u
#define DP_PKTRAM_CA_HI       0x011dffffu
#define DP_ACP_CA_LO          0x02000000u
#define DP_ACP_CA_HI          0x021dffffu

/* Host-DRAM window the device addresses (DTS dma-ranges identity; same check
 * the ETE ring path uses). */
#define DP_HOST_DMA_LO        0x80000000u
#define DP_HOST_DMA_HI        0x8fffffffu

/* ===================================================================
 * 4. TXQ table (handed to the firmware by the g0 id1 key-0x0320 frame)
 *
 * 0x2524-byte host-DRAM non-cached table, allocated+zeroed by
 * hmac_txq_table_init and its device VA sent by hmac_txq_table_addr_sync
 * (hdpp_config_send_event cmd 0x320, len 4).  The per-user blocks live at
 * +0x524 + user*0x40 {8x u16 queue len, 8x u16 start sn, 8x u32 bytes}
 * (hmac_update_user_tid_txq_table) [V].  The table is zero at first sync,
 * exactly what the vendor does.
 * =================================================================== */
#define DP_TXQ_USER_BLOCKS_OFF 0x524u
#define DP_TXQ_USER_BLOCK_BYTES 0x40u
#define DP_TXQ_USER_MAX        0x7fu   /* >0x7f uses a different vendor branch [U] */

/* ===================================================================
 * 5. Error codes (negative; dp_* functions return these)
 * =================================================================== */
#define DP_EOK                 0
#define DP_EARG                (-1)
#define DP_ERANGE              (-2)
#define DP_EALIGN              (-3)

/* ===================================================================
 * 6. The MAC slot-5 register program model
 *
 * One entry per MAC register write, in the vendor init order.  mask = bits to
 * preserve from the read value (RMW); mask == 0xffffffff means a plain write.
 * hi5622_dp.c executes them in order: read (RMW), write, read back and compare
 * the masked fields.
 * =================================================================== */
struct dp_mac_reg {
    dp_u8  off;
    dp_u8  pad[3];
    dp_u32 mask;
    dp_u32 value;
};

struct dp_mac_params {
    dp_u32 msdu_table_acp;  /* ACP alias of the ready id2 payload */
    dp_u32 rx_norm_addr;    /* device VA of the norm free ring */
    dp_u32 rx_small_addr;   /* device VA of the small free ring */
    dp_u32 rx_cmp_addr;     /* device VA of the completion ring */
    dp_u32 txba_addr;       /* device VA of the 0x800-byte BA-info buffer */
    dp_u32 txba_depth;      /* 256 */
    dp_u32 rx_norm_len;     /* 0x63c */
    dp_u32 rx_small_len;    /* 0xb0 */
    dp_u32 rx_norm_depth;   /* 256 */
    dp_u32 rx_small_depth;  /* 512 */
    dp_u32 rx_cmp_depth;    /* 4095 */
    dp_u32 int_mask;        /* DP_MAC_INT_MASK_INIT */
};

/* Fill regs[] (capacity max) with the program in vendor order; *count gets the
 * number of entries (exactly DP_MAC_REG_COUNT for valid parameters).  The
 * function range-checks every value (ring depths <= 0xfff, lengths <= 0xffff,
 * DMA addresses inside DP_HOST_DMA_LO..HI) and returns DP_EARG/DP_ERANGE. */
int dp_mac_program(const struct dp_mac_params *p, struct dp_mac_reg *regs,
                   unsigned int max, unsigned int *count);

/* ===================================================================
 * 7. Ring / table arithmetic
 * =================================================================== */
/* packed MAC-ring index (low 15 bits index, bit 15 phase) */
dp_u32 dp_ring_idx_pack(dp_u32 index, unsigned int phase);
void   dp_ring_idx_unpack(dp_u32 packed, dp_u32 *index, unsigned int *phase);
/* next packed value after publishing one entry at depth */
dp_u32 dp_ring_idx_next(dp_u32 packed, dp_u32 depth);

/* Validate the ready g4 id2 payload (PKTRAM device CA of the MSDU table) and
 * return both the PKTRAM CA and the ACP alias (CA + 0x01000000). */
int dp_ready_payload_ptrtable(dp_u32 payload, dp_u32 *table_ca,
                              dp_u32 *table_acp);

/* Entry (user,tid) of the device-RAM table: CA and ACP alias, both range
 * checked; user <= 0x91, tid <= 7, entry must stay inside PKTRAM. */
int dp_msdu_table_entry_ca(dp_u32 table_ca, unsigned int user,
                           unsigned int tid, dp_u32 *entry_ca,
                           dp_u32 *entry_acp);

/* Encode the 12-byte entry {ring_devva, 0, 0} (host writes it through ACP). */
void dp_msdu_table_entry_encode(dp_u8 entry[DP_MSDU_TABLE_ENTRY_BYTES],
                                dp_u32 ring_devva);

/* Device VA of TID tid inside one contiguous allocation of all 8 rings. */
dp_u32 dp_tx_ring_devva(dp_u32 rings_dma, unsigned int tid);

/* Device VA the device must see as a free-ring entry: the vendor ring entry is
 * the buffer address AFTER the 4-byte host bookkeeping word, i.e. buf+4. */
dp_u32 dp_rx_buf_entry_devva(dp_u32 buf_dma);

#endif /* DP_RINGS_H */
