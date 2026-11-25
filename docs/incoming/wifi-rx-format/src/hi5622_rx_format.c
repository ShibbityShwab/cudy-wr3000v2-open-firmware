// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_rx_format.c - portable, device-free implementation of the MAC RX
 * ring entry + 36-byte per-buffer header contract (see hi5622_rx_format.h).
 *
 * ORIGINAL clean-room code (lane wifi-rx-format, task-163).  Every rule below
 * is a derived fact from shuangta_host_rx_add_buff (0x374d0),
 * shuangta_rx_host_init_dscr_queue (0x37888), shuangta_host_rx_get_msdu_info_dscr
 * (0x3843c), shuangta_rx_get_next_sub_msdu (0x38814) and hal_ring_* (0x31e1c,
 * 0x31f1c, 0x31f80, 0x32164, 0x322f4) in the vendor host module; no vendor
 * code bytes are reproduced.  The host selftest pins every offset with
 * literal numbers so a header macro regression cannot make a test lie.
 */
#include "hi5622_rx_format.h"

static hif_u16 hif_le16(const hif_u8 *p)
{
    return (hif_u16)((hif_u16)p[0] | ((hif_u16)p[1] << 8));
}

static hif_u32 hif_le32(const hif_u8 *p)
{
    return (hif_u32)p[0] | ((hif_u32)p[1] << 8) |
           ((hif_u32)p[2] << 16) | ((hif_u32)p[3] << 24);
}

/* ------------------------------------------------------------------ */
/* entry encoding                                                      */
/* ------------------------------------------------------------------ */
/* [V] shuangta_host_rx_add_buff 0x37684/0x37688 stores the host word at the
 * allocation base and memsets 36 bytes at base+4; the ring entry pushed by
 * hal_ring_set_entries (0x375b8/0x376e4) is pcie_if_hostca_to_devva() of the
 * base+4 address.  The completion entry is the same value: the ISR converts
 * it with oal_pcie_devca_to_hostva and reads the host word at hostva-4. */
int hif_rx_entry_encode(hif_u32 buf_dma, hif_u32 *entry)
{
    if (entry == NULL || buf_dma == 0u)
        return HIF_RX_EARG;
    if ((buf_dma & 3u) != 0u)
        return HIF_RX_EFORMAT; /* the header start must be 4-byte aligned */
    *entry = buf_dma + HIF_RX_HOST_WORD;
    return HIF_RX_OK;
}

int hif_rx_entry_decode(hif_u32 entry, hif_u32 *buf_dma)
{
    if (buf_dma == NULL)
        return HIF_RX_EARG;
    if (entry <= HIF_RX_HOST_WORD || (entry & 3u) != 0u)
        return HIF_RX_EFORMAT;
    *buf_dma = entry - HIF_RX_HOST_WORD;
    return HIF_RX_OK;
}

int hif_rx_entry_classify(hif_u32 entry, hif_u32 norm_lo, hif_u32 norm_hi,
                          hif_u32 small_lo, hif_u32 small_hi, hif_u8 *kind)
{
    if (kind == NULL)
        return HIF_RX_EARG;
    if (entry == 0u || (entry & 3u) != 0u)
        return HIF_RX_EFORMAT; /* an entry is always an aligned devVA+4 */
    if (norm_lo <= entry && entry <= norm_hi) {
        *kind = 0u;
        return HIF_RX_OK;
    }
    if (small_lo <= entry && entry <= small_hi) {
        *kind = 1u;
        return HIF_RX_OK;
    }
    return HIF_RX_ERANGE;
}

/* ------------------------------------------------------------------ */
/* host word                                                           */
/* ------------------------------------------------------------------ */
int hif_rx_cookie_pack(hif_u8 kind, hif_u32 slot, hif_u32 *cookie)
{
    hif_u32 limit, v;

    if (cookie == NULL || kind > 1u)
        return HIF_RX_EARG;
    limit = (kind == 0u) ? HIF_RX_NORM_DEPTH : HIF_RX_SMALL_DEPTH;
    if (slot >= limit)
        return HIF_RX_ERANGE;
    v = HIF_RX_COOKIE_MAGIC | (slot & HIF_RX_COOKIE_SLOT_BITS);
    if (kind != 0u)
        v |= HIF_RX_COOKIE_KIND_BIT;
    *cookie = v;
    return HIF_RX_OK;
}

int hif_rx_cookie_check(hif_u32 cookie, hif_u8 kind, hif_u32 slot)
{
    hif_u32 want;

    if (hif_rx_cookie_pack(kind, slot, &want) != HIF_RX_OK)
        return HIF_RX_EFORMAT;
    return cookie == want ? HIF_RX_OK : HIF_RX_ESTALE;
}

/* ------------------------------------------------------------------ */
/* buffer prepare / witness                                            */
/* ------------------------------------------------------------------ */
static hif_u32 hif_alloc_size(hif_u8 kind)
{
    return kind == 0u ? HIF_RX_NORM_ALLOC : HIF_RX_SMALL_ALLOC;
}

/* [V] shuangta_host_rx_add_buff: on EVERY add_buff the vendor stores the skb
 * pointer at data[0], memsets 36 bytes at data+4 and only then pushes the
 * entry.  Zeroing on every publish is what makes a stale sub-MSDU chain
 * pointer (header+0x20) or a stale length (header+0x1a) impossible after a
 * buffer recycle; leaving a recycled buffer's old header in place is a real
 * divergence from that contract.  This driver has no skb per buffer, so the
 * host word carries a cookie instead (the device never reads or writes it). */
int hif_rx_buffer_prepare(hif_u8 *buf, hif_u32 alloc, hif_u8 kind,
                          hif_u32 slot)
{
    hif_u32 cookie;
    hif_u32 i;

    if (buf == NULL || kind > 1u || alloc != hif_alloc_size(kind))
        return HIF_RX_EARG;
    if (hif_rx_cookie_pack(kind, slot, &cookie) != HIF_RX_OK)
        return HIF_RX_ERANGE;
    for (i = 0u; i < HIF_RX_HDR_BYTES; i++)
        buf[HIF_RX_HOST_WORD + i] = 0u;
    /* zero the host word first, write the cookie last: a torn buffer can
     * never look prepared */
    buf[0] = (hif_u8)(cookie & 0xffu);
    buf[1] = (hif_u8)((cookie >> 8) & 0xffu);
    buf[2] = (hif_u8)((cookie >> 16) & 0xffu);
    buf[3] = (hif_u8)((cookie >> 24) & 0xffu);
    return HIF_RX_OK;
}

int hif_rx_header_is_zero(const hif_u8 *hdr)
{
    hif_u32 i;

    if (hdr == NULL)
        return 0;
    for (i = 0u; i < HIF_RX_HDR_BYTES; i++) {
        if (hdr[i] != 0u)
            return 0;
    }
    return 1;
}

/* The decisive open-question datum: a published buffer must be untouched
 * (cookie intact + header all zero) until the MAC DMAs a received frame into
 * it.  A nonzero header with an intact cookie means the device wrote a frame
 * into a buffer the completion ring never reported (witness).  A cookie
 * mismatch means the buffer was not published by this driver instance. */
int hif_rx_buffer_witness(const hif_u8 *buf, hif_u32 alloc, hif_u8 kind,
                          hif_u32 slot)
{
    hif_u32 cookie;

    if (buf == NULL || kind > 1u || alloc != hif_alloc_size(kind))
        return HIF_RX_BUF_BAD;
    cookie = hif_le32(buf);
    if (hif_rx_cookie_check(cookie, kind, slot) != HIF_RX_OK)
        return HIF_RX_BUF_STALE;
    return hif_rx_header_is_zero(buf + HIF_RX_HOST_WORD) ?
           HIF_RX_BUF_UNTOUCHED : HIF_RX_BUF_WITNESS;
}

/* ------------------------------------------------------------------ */
/* header parse                                                        */
/* ------------------------------------------------------------------ */
/* [V] shuangta_host_rx_get_msdu_info_dscr 0x38490-0x38678 reads exactly the
 * offsets in hi5622_rx_format.h; the liveness checks are the vendor's own:
 *   payload length  = u16 at header+0x1a (skb_put(len), len+0x28 <= alloc)
 *   next sub-MSDU   = u32 at header+0x20 (oal_pcie_devca_to_hostva then the
 *                     host word at hostva-4; 0 ends the chain)
 *   sub-MSDU number = u8  at header+0x06 (shuangta_host_get_rx_sub_msdu_num)
 *   status nibble   = high nibble of u8 at header+0x07
 *                     (shuangta_host_get_msdu_rx_status, skb->data[0xb]>>4)
 * The next pointer is either 0 or a 4-byte-aligned devVA+4 value. */
int hif_rx_hdr_parse(const hif_u8 *hdr, hif_u32 dma_len,
                     struct hif_rx_hdr *out)
{
    hif_u32 len, next;

    if (hdr == NULL || out == NULL || dma_len <= HIF_RX_HDR_BYTES)
        return HIF_RX_EARG;
    len = hif_le16(hdr + HIF_RX_HDR_LEN);
    if (len == 0u || len > dma_len - HIF_RX_HDR_BYTES)
        return HIF_RX_EFORMAT;
    if (HIF_RX_FRAME_NEED(len) > HIF_RX_HOST_WORD + dma_len)
        return HIF_RX_ERANGE;
    next = hif_le32(hdr + HIF_RX_HDR_NEXT);
    if (next != 0u && (next < (HIF_RX_HOST_WORD + HIF_RX_HDR_BYTES) ||
                       (next & 3u) != 0u))
        return HIF_RX_EFORMAT;
    out->ppdu_devva = hif_le32(hdr + HIF_RX_HDR_PPDU_ADDR);
    out->payload_len = (hif_u16)len;
    out->next_devva = next;
    out->submsdu_num = hdr[HIF_RX_HDR_SUBMSDU_NUM];
    out->status = hdr[HIF_RX_HDR_STATUS];
    out->flags05 = hdr[HIF_RX_HDR_FLAGS05];
    out->fctrl = hif_le16(hdr + HIF_RX_HDR_FCTRL);
    return HIF_RX_OK;
}

/* ------------------------------------------------------------------ */
/* refill budget / reset guard                                         */
/* ------------------------------------------------------------------ */
/* posted is the number of entries posted to the device and not yet consumed
 * (producer - live consumer).  A free-ring slot is only free for reuse after
 * the device consumed it, so between two snapshots the device can have
 * advanced at most (depth - posted) times - exactly the slots the host has
 * not posted.  A larger delta means the live consumer pointer jumped (block
 * reset / desync); publishing on top of that could hand the MAC a buffer the
 * host already believes is in flight, so it must fail closed. */
int hif_rx_refill_budget(hif_u32 depth, hif_u32 posted, hif_u32 delta,
                         hif_u32 *fillable)
{
    if (fillable == NULL || depth == 0u || depth > HIF_RX_IDX_MASK)
        return HIF_RX_EARG;
    if (posted > depth)
        return HIF_RX_ERANGE;
    if (delta > depth - posted)
        return HIF_RX_ERESET;
    *fillable = delta;
    return HIF_RX_OK;
}

/* ------------------------------------------------------------------ */
/* vendor stock-mirror config words                                    */
/* ------------------------------------------------------------------ */
static const hif_u32 hif_cfg_offs[HIF_RX_CFG_WORDS] = {
    0x1cu, 0x2cu, 0x30u, 0x34u, 0x38u, 0x3cu, 0x40u, 0x48u,
};
static const hif_u32 hif_cfg_vals[HIF_RX_CFG_WORDS] = {
    0x00b0063cu, /* norm buff len 0x63c | small buff len 0xb0 (bits31:16) */
    0x00000100u, /* norm free-ring depth 256 */
    0x00000200u, /* small free-ring depth 512 */
    0x00000fffu, /* completion-ring depth 4095 */
    0x00008000u, /* norm free-ring producer: index 0, phase 1 (full fill) */
    0x00008000u, /* small free-ring producer: index 0, phase 1 */
    0x00000000u, /* completion-ring consumer: index 0, phase 0 */
    0xfffffcc1u, /* host MAC interrupt mask (vendor live value) */
};

int hif_rx_cfg_expect(hif_u32 off, hif_u32 *value)
{
    hif_u32 i;

    if (value == NULL)
        return HIF_RX_EARG;
    for (i = 0u; i < HIF_RX_CFG_WORDS; i++) {
        if (hif_cfg_offs[i] == off) {
            *value = hif_cfg_vals[i];
            return 1;
        }
    }
    return 0;
}
