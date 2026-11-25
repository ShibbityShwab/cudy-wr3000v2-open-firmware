// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_rx_cmp_phase.c - portable completion-ring consumer/phase model
 * (lane wifi-rx-completion-phase, shared task task-168).
 *
 * ORIGINAL clean-room code.  Pure functions only: no kernel, no device, no
 * vendor bytes.  The kernel glue (hi5622_dp_txrx.c) performs the one 32-bit
 * store; this file only decides what the store must be.
 *
 * Contract sources (all committed, decoded in REPORT.md):
 *   - packed pointer = index[14:0] | phase<<15
 *       hal_ring_set_sw2hw 0x31f1c (type 2 -> cfg +0x40, zero-extended u16
 *       stored as a 32-bit word), hal_ring_set_entries 0x322f4 /
 *       hal_ring_get_entries 0x32164 (phase toggles on wrap).
 *   - pending = phase-aware producer-consumer distance
 *       hal_ring_get_entry_count 0x31f80.
 *   - vendor initial completion consumer = 0x0000
 *       shuangta_rx_host_init_dscr_queue 0x37888 leaves cfg +0x40 at reset;
 *       vendor stock dump cfg +0x40 = 0, live +0x10 = 0.
 */
#include "hi5622_rx_cmp_phase.h"

rxc_u32 rxc_pack(rxc_u32 index, rxc_u32 phase)
{
    return (index & RXC_IDX_MASK) | ((phase & 1u) ? RXC_PHASE_BIT : 0u);
}

void rxc_unpack(rxc_u32 packed, rxc_u32 *index, rxc_u32 *phase)
{
    if (index != NULL)
        *index = packed & RXC_IDX_MASK;
    if (phase != NULL)
        *phase = (packed & RXC_PHASE_BIT) ? 1u : 0u;
}

rxc_u32 rxc_next(rxc_u32 packed, rxc_u32 depth)
{
    rxc_u32 index = packed & RXC_IDX_MASK;
    rxc_u32 phase = (packed & RXC_PHASE_BIT) ? 1u : 0u;

    if (depth == 0u || depth > RXC_IDX_MASK)
        return packed;              /* fail closed: never wrap on bad depth */
    index++;
    if (index >= depth) {
        index = 0u;
        phase ^= 1u;
    }
    return rxc_pack(index, phase);
}

rxc_u32 rxc_advance(rxc_u32 packed, rxc_u32 n, rxc_u32 depth)
{
    rxc_u32 i;

    if (depth == 0u || depth > RXC_IDX_MASK)
        return packed;
    if (n > depth)
        n = depth;                  /* cannot consume more than one lap */
    for (i = 0u; i < n; i++)
        packed = rxc_next(packed, depth);
    return packed;
}

int rxc_pending(rxc_u32 producer, rxc_u32 consumer, rxc_u32 depth,
                rxc_u32 *count)
{
    rxc_s32 d;

    if (count == NULL || depth == 0u || depth > RXC_IDX_MASK)
        return RXC_EARG;
    if ((producer & RXC_IDX_MASK) >= depth ||
        (consumer & RXC_IDX_MASK) >= depth)
        return RXC_ERANGE;
    /* Vendor hal_ring_get_entry_count 0x31f80 type-2 branch: signed index
     * distance, +depth when the phase bits differ, then the u16 count; an
     * out-of-range result is the over-consumed/desynced state the caller must
     * refuse. */
    d = (rxc_s32)(producer & RXC_IDX_MASK) -
        (rxc_s32)(consumer & RXC_IDX_MASK);
    if (((producer ^ consumer) & RXC_PHASE_BIT) != 0u)
        d += (rxc_s32)depth;
    if (d < 0 || (rxc_u32)d > depth)
        return RXC_ERANGE;
    *count = (rxc_u32)d;
    return RXC_OK;
}

int rxc_ring_state(rxc_u32 producer, rxc_u32 consumer, rxc_u32 depth,
                   rxc_u32 *pending, rxc_u32 *full_lap)
{
    rxc_u32 count = 0u;
    int rc;

    rc = rxc_pending(producer, consumer, depth, &count);
    if (rc != RXC_OK)
        return rc;
    if (pending != NULL)
        *pending = count;
    if (full_lap != NULL)
        *full_lap = (count == depth) ? 1u : 0u;
    return RXC_OK;
}

enum rxc_gate rxc_prepublish_gate(rxc_u32 param_enabled, rxc_u32 rx_enable,
                                  rxc_u32 ready, rxc_u32 armed,
                                  rxc_u32 done, rxc_u32 lane_failed,
                                  rxc_u32 cfg_cmp_rptr)
{
    if (param_enabled == 0u)
        return RXC_GATE_DISABLED;
    if (rx_enable == 0u)
        return RXC_GATE_NO_RX_ENABLE;
    if (ready == 0u)
        return RXC_GATE_NOT_READY;
    if (armed != 0u)
        return RXC_GATE_ARMED;
    if (done != 0u)
        return RXC_GATE_ALREADY_DONE;
    if (lane_failed != 0u)
        return RXC_GATE_LANE_FAILED;
    if ((cfg_cmp_rptr & 0xffffu) != RXC_VENDOR_INITIAL_CONSUMER)
        return RXC_GATE_CONSUMER_DIRTY;
    return RXC_GATE_ALLOW;
}

rxc_u32 rxc_store_value(rxc_u32 old_word, rxc_u32 packed)
{
    return (old_word & 0xffff0000u) | (packed & 0xffffu);
}

int rxc_readback_ok(rxc_u32 readback, rxc_u32 packed)
{
    return ((readback & 0xffffu) == (packed & 0xffffu)) ? 1 : 0;
}

rxc_u32 rxc_status_reaction(rxc_u32 before, rxc_u32 after)
{
    rxc_u32 watch = RXC_STATUS_BIT_RPTR_OVERTAKE |
                    RXC_STATUS_BIT_PPDU_DESC_ERR;

    return (after & ~before) & watch;
}

int rxc_phase1_looks_full(rxc_u32 producer)
{
    rxc_u32 pending = 0u;
    rxc_u32 full = 0u;

    if (rxc_ring_state(producer, RXC_PHASE1_PREPUBLISH, RXC_CMP_DEPTH,
                       &pending, &full) != RXC_OK)
        return 1;                   /* fail closed: treat as full */
    return (int)full;
}
