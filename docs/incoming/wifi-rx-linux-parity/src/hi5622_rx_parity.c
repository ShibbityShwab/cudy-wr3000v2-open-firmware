// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_rx_parity.c - portable Linux-driver RX-enable parity rules for the
 * Hi5622V100 clean-room host (lane wifi-rx-linux-parity, shared task task-166).
 *
 * ORIGINAL clean-room code; no vendor source/object code is reproduced.  The
 * rules are derived facts with Linux architectural references (quoted by
 * file/function/line in REPORT.md) and committed project evidence.  This file
 * is device-free: the kernel glue (hi5622_dp_txrx.c) applies the rules to the
 * MAC, and tools/rx_parity_selftest.c pins them on the host.
 */
#include "hi5622_rx_parity.h"
#include "hi5622_rx_live.h"

/* ------------------------------------------------------------------ */
/* The step catalogue                                                 */
/* ------------------------------------------------------------------ */
static const struct rxp_step_info rxp_steps[RXP_STEP_COUNT] = {
    [RXP_STEP_RING_ALLOC] = {
        "ring_alloc",
        "mt76 mt76_dma_alloc_queue; iwlwifi iwl_pcie_rx_alloc; ath11k ath11k_dp_srng_setup (dma_alloc_coherent)",
        "done: dp_setup allocates norm 256x4, small 512x4, completion 4095x4 coherent"
    },
    [RXP_STEP_RING_PROGRAM] = {
        "ring_program",
        "iwlwifi rx_hw_init (RBDCB_BASE/STTS_WPTR/size); ath10k ath10k_ce_init_dest_ring; ath11k hal_srng_dst_hw_init",
        "done: the 14-write MAC program (reads back verified)"
    },
    [RXP_STEP_BUF_FILL] = {
        "buf_fill",
        "mt76 mt76_dma_rx_fill (dma_map_single DMA_FROM_DEVICE); iwlwifi rxq_alloc_rbs; ath10k rx_post_pipe",
        "done: coherent buffers, devVA(buffer+4) entries, both free rings filled"
    },
    [RXP_STEP_DMA_ENABLE] = {
        "dma_enable",
        "mt76 MT_WPDMA_GLO_CFG RX_DMA_EN; iwlwifi FH_MEM_RCSR_CHNL0_CONFIG_REG EN / RFH_RXF_DMA_CFG RFH_DMA_EN; ath11k HAL_REO1_RING_MISC_SRNG_ENABLE",
        "MISSING/UNKNOWN: no decoded host-writable RX engine/ring enable word [U] - the open bug"
    },
    [RXP_STEP_IRQ_NAPI] = {
        "irq_napi",
        "mt76 mt7615_irq_enable; iwlwifi RX MSI-X + napi; ath10k hif_start napi_enable+irq_enable; brcmfmac D2H doorbell",
        "deliberately not done: poll-driven bench model; MAC int mask already vendor value 0xfffffcc1"
    },
    [RXP_STEP_RX_FILTER] = {
        "rx_filter",
        "mt76 MT_WF_RFCR read-modify-write; iwlwifi/mac80211 filter config; ath10k MAC filter; brcmfmac firmware filter",
        "MISSING/UNKNOWN: driver writes no RX filter; whether the firmware programs it is [U]"
    },
    [RXP_STEP_PRODUCER_PUBLISH] = {
        "producer_publish",
        "mt76 writel(q->head, cpu_idx); iwlwifi inc_wr_ptr; ath10k CE ring index; ath11k hal_srng_access_end hp",
        "done: packed producer 0x8000 (index 0, phase 1); width corrected by this lane"
    },
    [RXP_STEP_KICK] = {
        "kick",
        "mt76 mt76_dma_kick_queue (wmb then index store); brcmfmac commonring write_complete (wptr store then ring bell)",
        "partial: the single producer store is the only kick; opt-in re-publish added by this lane"
    },
    [RXP_STEP_COMPLETION_CONSUME] = {
        "completion_consume",
        "mt76 DMA_DONE check; ath11k loop count; iwlwifi uCode READ index; Hi1105 HAL spin-until-nonzero",
        "done by wifi-rx-static; commit wait + clear-after-read added by this lane"
    },
    [RXP_STEP_CONSUMER_PUBLISH] = {
        "consumer_publish",
        "iwlwifi RBDC write index; ath11k hal_srng_access_end tp; brcmfmac read_complete rptr",
        "done: completion consumer packed index; width corrected by this lane"
    },
    [RXP_STEP_REFILL] = {
        "refill",
        "iwlwifi rxq_restock; ath10k rx_post retry timer; ath11k rxdma replenish; mt76 rx_fill after poll",
        "done: rx-static drain + rx-watermark sustained refill; window rule added by this lane"
    },
    [RXP_STEP_STALL_WATCH] = {
        "stall_watch",
        "iwlwifi RX_STALLED when no free buffers; Hi1105 free-ring-empty ISR; ath10k rx_post retry timer",
        "missing as telemetry: added log-only classification by this lane"
    },
    [RXP_STEP_TEARDOWN] = {
        "teardown",
        "iwlwifi rx_stop disables the RX DMA first; mt76 mt76_dma_rx_reset; ath11k srng cleanup",
        "n/a: the clean-room driver has no device stop/reload path (module reload power-cycles)"
    },
};

const struct rxp_step_info *rxp_step_info(enum rxp_step step)
{
    if ((unsigned int)step >= (unsigned int)RXP_STEP_COUNT)
        return NULL;
    return &rxp_steps[step];
}

const char *rxp_step_name(enum rxp_step step)
{
    const struct rxp_step_info *info = rxp_step_info(step);

    return info ? info->name : "invalid";
}

/* ------------------------------------------------------------------ */
/* 2. Exact-width pointer publication                                 */
/* ------------------------------------------------------------------ */
int rxp_publish_value(rxp_u32 index, unsigned int phase, rxp_u32 depth,
                      rxp_u32 *value)
{
    if (value == NULL || depth == 0u || depth > RXP_IDX_MASK)
        return -1;
    if (index >= depth || phase > 1u)
        return -1;
    *value = (index & RXP_IDX_MASK) | (phase ? RXP_PHASE_BIT : 0u);
    return 0;
}

/* ------------------------------------------------------------------ */
/* 3. Completion-entry commit decision                                */
/* ------------------------------------------------------------------ */
int rxp_commit_settled(rxp_u32 first, rxp_u32 last, int *settled)
{
    if (settled == NULL)
        return -1;
    if (last == 0u) {
        *settled = 0; /* still not committed */
        return 0;
    }
    /* A zero first read that became non-zero committed between the reads; a
     * non-zero first read must agree with the second one to be stable. */
    *settled = (first == 0u || first == last) ? 1 : 0;
    return 0;
}

int rxp_commit_retry(rxp_u32 last, unsigned int waits_done,
                     unsigned int wait_max, int *retry)
{
    if (retry == NULL)
        return -1;
    *retry = (last == 0u && waits_done < wait_max) ? 1 : 0;
    return 0;
}

int rxp_spurious_limit(rxp_u32 spurious, rxp_u32 cap, int *close_lane)
{
    if (close_lane == NULL)
        return -1;
    if (cap == 0u)
        return -1;
    *close_lane = (spurious > cap) ? 1 : 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 4. Served-bits-only W1C                                            */
/* ------------------------------------------------------------------ */
int rxp_w1c_served(rxp_u32 status, rxp_u32 mask, rxp_u32 *value)
{
    if (value == NULL)
        return -1;
    *value = status & ~mask;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 5. Bounded refill window                                           */
/* ------------------------------------------------------------------ */
int rxp_refill_plan(rxp_u32 old_consumer, rxp_u32 fresh_consumer,
                    rxp_u32 producer, rxp_u32 depth, rxp_u32 queued,
                    rxp_u32 cap, rxp_u32 *count, rxp_u32 *new_producer)
{
    rxp_u32 avail = 0u, posted = 0u, room, plan, prod;
    int rc;

    if (count == NULL || new_producer == NULL)
        return -1;
    if (depth == 0u || depth > RXP_IDX_MASK || queued > depth || cap > depth)
        return -1;
    rc = hi5622_rx_ring_consumed(old_consumer, fresh_consumer, depth, &avail);
    if (rc != HIL_RX_OK)
        return -1;
    rc = hi5622_rx_ring_pending(producer, fresh_consumer, depth, &posted);
    if (rc != HIL_RX_OK)
        return -1;
    if (posted > depth)
        return -1;
    room = depth - posted;
    plan = avail;
    if (queued < plan)
        plan = queued;
    if (room < plan)
        plan = room;
    if (cap < plan)
        plan = cap;
    prod = producer;
    {
        rxp_u32 n = plan;

        while (n > 0u) {
            prod = hi5622_rx_idx_advance(prod, depth);
            if ((prod & RXP_IDX_MASK) >= depth)
                return -1;
            n--;
        }
    }
    *count = plan;
    *new_producer = prod;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 6. Opt-in RX-control candidate word                                */
/* ------------------------------------------------------------------ */
static const rxp_u32 rxp_ctl_candidates[RXP_CTL_OFF_CANDIDATE_MAX] = {
    0x08u, 0x0cu, 0x14u, 0x18u
};

int rxp_ctl_offset_allowed(rxp_u32 off)
{
    unsigned int i;

    if ((off & 3u) != 0u)
        return 0;
    if (off + 4u > RXP_MAC_CFG_SPAN)
        return 0;
    for (i = 0; i < RXP_CTL_OFF_CANDIDATE_MAX; i++) {
        if (rxp_ctl_candidates[i] == off)
            return 1;
    }
    return 0;
}

int rxp_ctl_value(rxp_u32 off, rxp_u32 old, rxp_u32 set, rxp_u32 clr,
                  rxp_u32 *value)
{
    if (value == NULL)
        return -1;
    if (!rxp_ctl_offset_allowed(off))
        return -1;
    if ((set & clr) != 0u) /* a bit cannot be both set and cleared */
        return -1;
    if ((set | clr) == 0u) /* nothing to do */
        return -1;
    *value = (old & ~clr) | set;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 7. Stall classification                                            */
/* ------------------------------------------------------------------ */
int rxp_stall_class(rxp_u32 arm_status, rxp_u32 arm_norm_rptr,
                    rxp_u32 arm_small_rptr, rxp_u32 arm_cmp_wptr,
                    rxp_u32 now_status, rxp_u32 now_norm_rptr,
                    rxp_u32 now_small_rptr, rxp_u32 now_cmp_wptr,
                    rxp_u32 spurious, int *class_out)
{
    if (class_out == NULL)
        return -1;
    if (arm_status == 0xffffffffu || now_status == 0xffffffffu ||
        arm_norm_rptr == 0xffffffffu || now_norm_rptr == 0xffffffffu ||
        arm_small_rptr == 0xffffffffu || now_small_rptr == 0xffffffffu ||
        arm_cmp_wptr == 0xffffffffu || now_cmp_wptr == 0xffffffffu) {
        *class_out = RXP_STALL_ERROR;
        return 0;
    }
    if (now_cmp_wptr != arm_cmp_wptr) {
        *class_out = RXP_STALL_PROGRESS;
        return 0;
    }
    if (spurious > 0u) {
        *class_out = RXP_STALL_SPURIOUS;
        return 0;
    }
    if (now_norm_rptr != arm_norm_rptr ||
        now_small_rptr != arm_small_rptr) {
        *class_out = RXP_STALL_PREFETCH_ONLY;
        return 0;
    }
    *class_out = RXP_STALL_IDLE;
    return 0;
}

const char *rxp_stall_name(enum rxp_stall stall)
{
    switch (stall) {
    case RXP_STALL_IDLE:          return "IDLE";
    case RXP_STALL_PREFETCH_ONLY: return "PREFETCH_ONLY";
    case RXP_STALL_PROGRESS:      return "PROGRESS";
    case RXP_STALL_SPURIOUS:      return "SPURIOUS_WPTR";
    case RXP_STALL_ERROR:         return "PROBE_ERROR";
    default:                      return "invalid";
    }
}

/* ------------------------------------------------------------------ */
/* 8. Parity probe classification                                     */
/* ------------------------------------------------------------------ */
static const rxp_u32 rxp_program_offs[] = {
    0x00u, 0x04u, 0x10u, 0x1cu, 0x20u, 0x24u, 0x28u, 0x2cu,
    0x30u, 0x34u, 0x38u, 0x3cu, 0x40u, 0x48u
};

int rxp_is_program_off(rxp_u32 off)
{
    unsigned int i;

    for (i = 0; i < (unsigned int)(sizeof(rxp_program_offs) /
                                   sizeof(rxp_program_offs[0])); i++) {
        if (rxp_program_offs[i] == off)
            return 1;
    }
    return 0;
}

rxp_u32 rxp_mac_cfg_stock(rxp_u32 off)
{
    /* opensource/dumps/reg_all.txt, MAC block CA 0x40040000, stock vendor boot
     * [V*] - every word not listed is 0 in that capture.  The two address
     * values are the vendor boot's own DMA allocations, already published in
     * the committed capture; they are not per-unit or secret data. */
    switch (off) {
    case 0x00u: return 0x00000100u;
    case 0x04u: return 0x83fd7000u;
    case 0x10u: return 0x02060850u;
    case 0x1cu: return 0x00b0063cu;
    case 0x20u: return 0x844c7000u;
    case 0x24u: return 0x844c8000u;
    case 0x28u: return 0x83734000u;
    case 0x2cu: return 0x00000100u;
    case 0x30u: return 0x00000200u;
    case 0x34u: return 0x00000fffu;
    case 0x38u: return 0x00008000u;
    case 0x3cu: return 0x00008000u;
    case 0x48u: return 0xfffffcc1u;
    case 0x5cu: return 0x00000004u;
    case 0x6cu: return 0x00000004u;
    default:    return 0x00000000u;
    }
}

int rxp_mac_cfg_class(rxp_u32 off, rxp_u32 value, int *stock_nonzero,
                      int *matches_stock)
{
    rxp_u32 stock;
    int programmed;

    if ((off & 3u) != 0u || off + 4u > RXP_MAC_CFG_SPAN)
        return 0;
    programmed = rxp_is_program_off(off);
    stock = rxp_mac_cfg_stock(off);
    if (stock_nonzero != NULL)
        *stock_nonzero = (stock != 0u) ? 1 : 0;
    if (matches_stock != NULL)
        *matches_stock = (value == stock) ? 1 : 0;
    if (programmed)
        return (value == stock) ? 1 : 2;
    if (stock != 0u)
        return 3;
    return 4;
}
