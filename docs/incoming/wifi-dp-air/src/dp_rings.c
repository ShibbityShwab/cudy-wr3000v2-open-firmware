// SPDX-License-Identifier: GPL-2.0
/*
 * dp_rings.c - portable data-plane geometry/register-model implementation for
 * the Hi5622V100 clean-room host (wifi-datapath-bringup lane, task-111).
 *
 * ORIGINAL clean-room code; no vendor source/object code, firmware byte,
 * calibration value or per-unit identity is reproduced.  The constants and the
 * vendor init order are derived facts with evidence markers in REPORT.md and
 * FRAME-SPECS.md (and re-asserted host-only by tools/dp_static_decode.py).
 */
#include "dp_rings.h"

/* ===================================================================
 * MAC slot-5 register program (vendor init order)
 * =================================================================== */
int dp_mac_program(const struct dp_mac_params *p, struct dp_mac_reg *regs,
                   unsigned int max, unsigned int *count)
{
    unsigned int n = 0;

    if (!p || !regs || !count)
        return DP_EARG;
    if (max < DP_MAC_REG_COUNT)
        return DP_EARG;
    if (p->rx_norm_len > 0xffffu || p->rx_small_len > 0xffffu)
        return DP_ERANGE;
    if (p->rx_norm_depth > 0xfffu || p->rx_small_depth > 0xfffu ||
        p->rx_cmp_depth > 0xfffu)
        return DP_ERANGE;
    if (p->txba_depth > 0xffffu)
        return DP_ERANGE;
    if (p->txba_depth != DP_TXBA_DEPTH_INIT ||
        p->rx_norm_depth != DP_RX_NORM_DEPTH ||
        p->rx_small_depth != DP_RX_SMALL_DEPTH ||
        p->rx_cmp_depth != DP_RXCMP_DEPTH ||
        p->rx_norm_len != DP_RX_NORM_BUFLEN ||
        p->rx_small_len != DP_RX_SMALL_BUFLEN)
        return DP_EARG;
    if (p->msdu_table_acp < DP_ACP_CA_LO || p->msdu_table_acp > DP_ACP_CA_HI)
        return DP_ERANGE;
    if (p->rx_norm_addr < DP_HOST_DMA_LO || p->rx_norm_addr > DP_HOST_DMA_HI ||
        p->rx_small_addr < DP_HOST_DMA_LO || p->rx_small_addr > DP_HOST_DMA_HI ||
        p->rx_cmp_addr < DP_HOST_DMA_LO || p->rx_cmp_addr > DP_HOST_DMA_HI ||
        p->txba_addr < DP_HOST_DMA_LO || p->txba_addr > DP_HOST_DMA_HI)
        return DP_ERANGE;

    /* 1. MSDU-info ring-pointer table base (ACP alias of ready id2) [V] */
    regs[n].off = DP_MAC_REG_MSDU_TBL;
    regs[n].mask = 0xffffffffu;
    regs[n].value = p->msdu_table_acp;
    n++;
    /* 2. RX buffer lengths: norm[15:0] | small[31:16] = 0x00b0063c [V*] */
    regs[n].off = DP_MAC_REG_RX_BUFLEN;
    regs[n].mask = 0xffffffffu;
    regs[n].value = (p->rx_norm_len & 0xffffu) |
                    ((p->rx_small_len & 0xffffu) << 16);
    n++;
    /* 3. the three ring base addresses (device VAs) [V] */
    regs[n].off = DP_MAC_REG_RXNORM_ADDR;
    regs[n].mask = 0xffffffffu;
    regs[n].value = p->rx_norm_addr;
    n++;
    regs[n].off = DP_MAC_REG_RXSMALL_ADDR;
    regs[n].mask = 0xffffffffu;
    regs[n].value = p->rx_small_addr;
    n++;
    regs[n].off = DP_MAC_REG_RXCMP_ADDR;
    regs[n].mask = 0xffffffffu;
    regs[n].value = p->rx_cmp_addr;
    n++;
    /* 4. ring sizes (bits 11:0, RMW preserves the other bits) [V] */
    regs[n].off = DP_MAC_REG_RXNORM_SIZE;
    regs[n].mask = 0x00000fffu;
    regs[n].value = p->rx_norm_depth;
    n++;
    regs[n].off = DP_MAC_REG_RXSMALL_SIZE;
    regs[n].mask = 0x00000fffu;
    regs[n].value = p->rx_small_depth;
    n++;
    regs[n].off = DP_MAC_REG_RXCMP_SIZE;
    regs[n].mask = 0x00000fffu;
    regs[n].value = p->rx_cmp_depth;
    n++;
    /* 5. tx_ba_info depth (bits 15:0) + address [V] */
    regs[n].off = DP_MAC_REG_TXBA_DEPTH;
    regs[n].mask = 0x0000ffffu;
    regs[n].value = p->txba_depth;
    n++;
    regs[n].off = DP_MAC_REG_TXBA_ADDR;
    regs[n].mask = 0xffffffffu;
    regs[n].value = p->txba_addr;
    n++;
    /* 6. host MAC interrupt mask (vendor initialises the full word) [V] */
    regs[n].off = DP_MAC_REG_INT_MASK;
    regs[n].mask = 0xffffffffu;
    regs[n].value = p->int_mask;
    n++;
    /* 7. producer/consumer indices: the free rings were filled exactly once,
     * so both wptrs publish index 0/phase 1 (live vendor 0x8000) [V*]; the
     * completion ring consumer starts at 0. */
    regs[n].off = DP_MAC_REG_RXNORM_WPTR;
    regs[n].mask = 0xffffffffu;
    regs[n].value = DP_RING_WPTR_FULL;
    n++;
    regs[n].off = DP_MAC_REG_RXSMALL_WPTR;
    regs[n].mask = 0xffffffffu;
    regs[n].value = DP_RING_WPTR_FULL;
    n++;
    regs[n].off = DP_MAC_REG_RXCMP_RPTR;
    regs[n].mask = 0xffffffffu;
    regs[n].value = 0u;
    n++;

    *count = n;
    return DP_EOK;
}

/* ===================================================================
 * Ring index arithmetic (15-bit index + phase bit; MAC rings)
 * =================================================================== */
dp_u32 dp_ring_idx_pack(dp_u32 index, unsigned int phase)
{
    return (index & DP_RING_IDX_MASK) |
           (phase ? DP_RING_PHASE : 0u);
}

void dp_ring_idx_unpack(dp_u32 packed, dp_u32 *index, unsigned int *phase)
{
    if (index)
        *index = packed & DP_RING_IDX_MASK;
    if (phase)
        *phase = (packed & DP_RING_PHASE) ? 1u : 0u;
}

dp_u32 dp_ring_idx_next(dp_u32 packed, dp_u32 depth)
{
    dp_u32 index = packed & DP_RING_IDX_MASK;
    unsigned int phase = (packed & DP_RING_PHASE) ? 1u : 0u;

    if (!depth)
        return packed;
    index++;
    if (index >= depth) {
        index = 0;
        phase ^= 1u;
    }
    return dp_ring_idx_pack(index, phase);
}

/* ===================================================================
 * Device-RAM MSDU-info ring-pointer table
 * =================================================================== */
int dp_ready_payload_ptrtable(dp_u32 payload, dp_u32 *table_ca,
                              dp_u32 *table_acp)
{
    if (!table_ca || !table_acp)
        return DP_EARG;
    /* The ready id2 dword0 is a device CA in the PKTRAM window. */
    if (payload < DP_PKTRAM_CA_LO || payload > DP_PKTRAM_CA_HI)
        return DP_ERANGE;
    if (payload & 3u)
        return DP_EALIGN;
    if (payload > DP_PKTRAM_CA_HI - (DP_MSDU_TABLE_ENTRY_BYTES - 1u))
        return DP_ERANGE;
    *table_ca = payload;
    *table_acp = payload + DP_MSDU_TABLE_ACP_DELTA;
    return DP_EOK;
}

int dp_msdu_table_entry_ca(dp_u32 table_ca, unsigned int user,
                           unsigned int tid, dp_u32 *entry_ca,
                           dp_u32 *entry_acp)
{
    dp_u32 index, off, ca;

    if (!entry_ca || !entry_acp)
        return DP_EARG;
    if (user > DP_MSDU_TABLE_MAX_USER || tid > DP_MSDU_TABLE_MAX_TID)
        return DP_EARG;
    if (table_ca < DP_PKTRAM_CA_LO || table_ca > DP_PKTRAM_CA_HI)
        return DP_ERANGE;
    if (table_ca & 3u)
        return DP_EALIGN;
    index = (dp_u32)tid + (dp_u32)user * DP_MSDU_TABLE_USER_STRIDE;
    off = index * DP_MSDU_TABLE_ENTRY_BYTES;
    ca = table_ca + off;
    if (ca < table_ca || ca > DP_PKTRAM_CA_HI ||
        ca > DP_PKTRAM_CA_HI - (DP_MSDU_TABLE_ENTRY_BYTES - 1u))
        return DP_ERANGE;
    *entry_ca = ca;
    *entry_acp = ca + DP_MSDU_TABLE_ACP_DELTA;
    return DP_EOK;
}

void dp_msdu_table_entry_encode(dp_u8 entry[DP_MSDU_TABLE_ENTRY_BYTES],
                                dp_u32 ring_devva)
{
    unsigned int i;

    for (i = 0; i < DP_MSDU_TABLE_ENTRY_BYTES; i++)
        entry[i] = 0u;
    entry[0] = (dp_u8)(ring_devva & 0xffu);
    entry[1] = (dp_u8)((ring_devva >> 8) & 0xffu);
    entry[2] = (dp_u8)((ring_devva >> 16) & 0xffu);
    entry[3] = (dp_u8)((ring_devva >> 24) & 0xffu);
}

dp_u32 dp_tx_ring_devva(dp_u32 rings_dma, unsigned int tid)
{
    return rings_dma + (dp_u32)tid * DP_TX_MSDU_RING_DEPTH *
                       DP_TX_MSDU_ENTRY_BYTES;
}

dp_u32 dp_rx_buf_entry_devva(dp_u32 buf_dma)
{
    return buf_dma + DP_RX_BUF_HDR;
}
