// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_rx_live.c - portable MAC RX data-path logic for the Hi5622V100
 * clean-room host (lane wifi-rx-static, shared task task-137).
 *
 * ORIGINAL clean-room code; no vendor source/object code, firmware byte,
 * calibration value or per-unit identity is reproduced.  Every constant and
 * every arithmetic rule is a derived fact with an evidence marker in
 * REPORT.md and is re-asserted by tools/rx_selftest.c against LITERAL vendor
 * offsets (so a header macro regression cannot make the test lie).
 */
#include "hi5622_rx_live.h"

static rx_u16 hil_le16(const rx_u8 *p)
{
    return (rx_u16)((rx_u16)p[0] | ((rx_u16)p[1] << 8));
}

static rx_u32 hil_le32(const rx_u8 *p)
{
    return (rx_u32)p[0] | ((rx_u32)p[1] << 8) |
           ((rx_u32)p[2] << 16) | ((rx_u32)p[3] << 24);
}

/* -------------------------------------------------------------------
 * Producer / consumer distance.  Both rings use 15 index bits + one phase
 * bit, so a distance of exactly one ring lap is only expressible with the
 * phase bit.  pending() = producer - consumer (completion ring direction);
 * consumed() = new consumer - old consumer (free-ring direction, where the
 * ring is kept full so the consumer chases the producer).
 * ------------------------------------------------------------------- */
int hi5622_rx_ring_pending(rx_u32 producer, rx_u32 consumer, rx_u32 depth,
                           rx_u32 *count)
{
    rx_s32 d;

    if (count == NULL || depth == 0u || depth > HIL_RX_IDX_MASK)
        return HIL_RX_EARG;
    if ((producer & HIL_RX_IDX_MASK) >= depth ||
        (consumer & HIL_RX_IDX_MASK) >= depth)
        return HIL_RX_ERANGE;
    d = (rx_s32)(producer & HIL_RX_IDX_MASK) -
        (rx_s32)(consumer & HIL_RX_IDX_MASK);
    if (((producer ^ consumer) & HIL_RX_PHASE_BIT) != 0u)
        d += (rx_s32)depth;
    if (d < 0 || (rx_u32)d > depth)
        return HIL_RX_EFORMAT;
    *count = (rx_u32)d;
    return HIL_RX_OK;
}

int hi5622_rx_ring_consumed(rx_u32 old_consumer, rx_u32 new_consumer,
                            rx_u32 depth, rx_u32 *count)
{
    rx_u32 oi, ni, d;

    if (count == NULL || depth == 0u || depth > HIL_RX_IDX_MASK)
        return HIL_RX_EARG;
    oi = old_consumer & HIL_RX_IDX_MASK;
    ni = new_consumer & HIL_RX_IDX_MASK;
    if (oi >= depth || ni >= depth)
        return HIL_RX_ERANGE;
    d = (ni >= oi) ? (ni - oi) : (depth - oi + ni);
    if (d == 0u && ((old_consumer ^ new_consumer) & HIL_RX_PHASE_BIT) != 0u)
        d = depth; /* exactly one lap between the snapshots */
    if (d > depth)
        return HIL_RX_EFORMAT;
    *count = d;
    return HIL_RX_OK;
}

rx_u32 hi5622_rx_idx_advance(rx_u32 packed, rx_u32 depth)
{
    rx_u32 idx = packed & HIL_RX_IDX_MASK;
    rx_u32 phase = (packed & HIL_RX_PHASE_BIT) ? 1u : 0u;

    if (depth == 0u || idx >= depth)
        return packed;
    idx++;
    if (idx == depth) {
        idx = 0u;
        phase ^= 1u;
    }
    return idx | (phase ? HIL_RX_PHASE_BIT : 0u);
}

int hi5622_rx_wptr_pack(rx_u32 index, unsigned int phase, rx_u32 depth,
                        rx_u32 *packed)
{
    if (packed == NULL || depth == 0u || depth > HIL_RX_IDX_MASK)
        return HIL_RX_EARG;
    if (index >= depth)
        return HIL_RX_ERANGE;
    *packed = (index & HIL_RX_IDX_MASK) |
              ((phase & 1u) ? HIL_RX_PHASE_BIT : 0u);
    return HIL_RX_OK;
}

/* -------------------------------------------------------------------
 * Completion header decode.  hdr points at buffer+4, the device-written
 * 36-byte header; dma_cap is the usable DMA area (0x63c norm / 0xb0 small).
 * The MPDU payload starts at hdr+0x24 == buffer+0x28, so the length field
 * must leave the whole payload inside the DMA area.  The vendor's own check
 * is len + 40 <= buffer allocation (0x640 band 0), identical to
 * len <= 0x640 - 0x28 = 0x618 = dma_cap - 0x24.
 * ------------------------------------------------------------------- */
int hi5622_rx_hdr_parse(const rx_u8 *hdr, rx_u32 dma_cap,
                        struct hi5622_rx_hdr *out)
{
    rx_u32 len, next;

    if (hdr == NULL || out == NULL)
        return HIL_RX_EARG;
    if (dma_cap < HIL_RX_HDR_BYTES + 1u)
        return HIL_RX_ERANGE;
    len = hil_le16(hdr + HIL_RX_HDR_LEN_OFF);
    next = hil_le32(hdr + HIL_RX_HDR_NEXT_OFF);
    if (len == 0u || len > dma_cap - HIL_RX_HDR_BYTES)
        return HIL_RX_EFORMAT;
    if (next != 0u) {
        /* Sub-MSDU chaining is real [V] but its slot accounting is still [U]:
         * refuse instead of consuming/refilling a partial chain. */
        if ((next & 3u) != 0u)
            return HIL_RX_EFORMAT;
        return HIL_RX_ECHAIN;
    }
    out->payload_len = (rx_u16)len;
    out->next_devva = 0u;
    out->flags9 = hdr[HIL_RX_HDR_FLAGS9_OFF];
    out->status_b = hdr[HIL_RX_HDR_STATUSB_OFF];
    return HIL_RX_OK;
}

int hi5622_rx_entry_split(rx_u32 entry, rx_u32 norm_lo, rx_u32 norm_hi,
                          rx_u32 small_lo, rx_u32 small_hi, rx_u8 *ring_kind)
{
    if (ring_kind == NULL)
        return HIL_RX_EARG;
    if ((entry & 3u) != 0u)
        return HIL_RX_EFORMAT;
    if (norm_lo <= entry && entry <= norm_hi) {
        *ring_kind = 0u;
        return HIL_RX_OK;
    }
    if (small_lo <= entry && entry <= small_hi) {
        *ring_kind = 1u;
        return HIL_RX_OK;
    }
    return HIL_RX_ERANGE;
}

int hi5622_rx_status_rx_bit(rx_u32 status, rx_u32 mask, rx_u32 *active)
{
    if (active == NULL)
        return HIL_RX_EARG;
    *active = ((status & ~mask) & (1u << HIL_RX_INT_RX_BIT)) ? 1u : 0u;
    return HIL_RX_OK;
}

/* -------------------------------------------------------------------
 * Free-ring refill watermark (lane wifi-rx-watermark, task-156).
 *
 * See the header for the policy.  The rules are deliberately total and
 * fail-closed: every input is range-checked, the result is bounded by both the
 * queue and the ring room, and a malformed state returns an error so the
 * driver closes the RX lane instead of guessing a pointer.
 * ------------------------------------------------------------------- */
int hi5622_rx_wm_target(rx_u32 depth, unsigned int wm_pct, rx_u32 *target)
{
    rx_u32 t;

    if (target == NULL || depth == 0u || depth > HIL_RX_IDX_MASK)
        return HIL_RX_EARG;
    if (wm_pct > HIL_RX_WM_PCT_MAX)
        return HIL_RX_ERANGE;
    if (wm_pct == 0u) {
        *target = 0u; /* disabled */
        return HIL_RX_OK;
    }
    t = (depth * (rx_u32)wm_pct) / 100u;
    if (t == 0u)
        t = 1u; /* a set watermark always asks for at least one posted slot */
    *target = t;
    return HIL_RX_OK;
}

int hi5622_rx_wm_triggered(rx_u32 depth, unsigned int wm_pct, rx_u32 posted)
{
    rx_u32 target;
    int rc;

    if (posted > depth)
        return HIL_RX_EFORMAT;
    rc = hi5622_rx_wm_target(depth, wm_pct, &target);
    if (rc != HIL_RX_OK)
        return rc;
    return (posted < target) ? 1 : 0;
}

int hi5622_rx_wm_plan(rx_u32 depth, unsigned int wm_pct, rx_u32 posted,
                      rx_u32 avail, rx_u32 cap, rx_u32 *refill)
{
    rx_u32 target, room, n;
    int rc;

    if (refill == NULL)
        return HIL_RX_EARG;
    *refill = 0u;
    if (posted > depth || avail > depth)
        return HIL_RX_ERANGE;
    rc = hi5622_rx_wm_target(depth, wm_pct, &target);
    if (rc != HIL_RX_OK)
        return rc;
    if (posted >= target)
        return HIL_RX_OK; /* healthy: no trigger */
    room = depth - posted; /* consumed slots no live entry occupies */
    n = (avail < room) ? avail : room;
    if (n > cap)
        n = cap;
    *refill = n;
    return HIL_RX_OK;
}
