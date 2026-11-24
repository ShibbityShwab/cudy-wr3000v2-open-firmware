/* SPDX-License-Identifier: GPL-2.0 */
/*
 * dp_txrx.c - first TX/RX data-plane host logic for the Hi5622V100
 * clean-room driver. Original clean-room code (lane wifi-dp-txrx, task-122);
 * only decoded constants/offsets, no vendor code or data. See REPORT.md.
 */
#include "dp_txrx.h"

/* ------------------------------------------------------------------ */
/* little-endian helpers                                              */
/* ------------------------------------------------------------------ */
dp_u32 dp_txrx_le32(const dp_u8 *p)
{
    return (dp_u32)p[0] | ((dp_u32)p[1] << 8) | ((dp_u32)p[2] << 16) |
           ((dp_u32)p[3] << 24);
}

void dp_txrx_put_le16(dp_u8 *p, dp_u16 v)
{
    p[0] = (dp_u8)(v & 0xffu);
    p[1] = (dp_u8)((v >> 8) & 0xffu);
}

void dp_txrx_put_le32(dp_u8 *p, dp_u32 v)
{
    p[0] = (dp_u8)(v & 0xffu);
    p[1] = (dp_u8)((v >> 8) & 0xffu);
    p[2] = (dp_u8)((v >> 16) & 0xffu);
    p[3] = (dp_u8)((v >> 24) & 0xffu);
}

static dp_u16 dp_le16(const dp_u8 *p)
{
    return (dp_u16)(p[0] | ((dp_u16)p[1] << 8));
}

/* ------------------------------------------------------------------ */
/* device-RAM MSDU-info ring-pointer table entries                    */
/* ------------------------------------------------------------------ */
void dp_txrx_table_entry_pack(dp_u8 out[DP_TXRX_TABLE_ENTRY_BYTES],
                              dp_u32 ring_devva, dp_u32 wptr, dp_u32 rptr)
{
    if (out == NULL)
        return;
    dp_txrx_put_le32(out + 0, ring_devva);
    dp_txrx_put_le32(out + 4, wptr);
    dp_txrx_put_le32(out + 8, rptr);
}

dp_u32 dp_txrx_table_entry_wptr(const dp_u8 e[DP_TXRX_TABLE_ENTRY_BYTES])
{
    return e == NULL ? 0u : dp_txrx_le32(e + 4);
}

dp_u32 dp_txrx_table_entry_rptr(const dp_u8 e[DP_TXRX_TABLE_ENTRY_BYTES])
{
    return e == NULL ? 0u : dp_txrx_le32(e + 8);
}

int dp_txrx_table_entry_acp(dp_u32 table_ca, unsigned int user,
                            unsigned int tid, dp_u32 *acp)
{
    dp_u32 ca;

    if (dp_txrx_table_entry_ca(table_ca, user, tid, &ca) != DP_TXRX_OK)
        return DP_TXRX_EARG;
    *acp = ca + DP_TXRX_TABLE_ACP_DELTA;
    return DP_TXRX_OK;
}

int dp_txrx_table_entry_ca(dp_u32 table_ca, unsigned int user,
                           unsigned int tid, dp_u32 *ca)
{
    dp_u32 index;

    if (ca == NULL || user >= DP_TXRX_TABLE_USERS || tid >= DP_TXRX_TABLE_TIDS)
        return DP_TXRX_EARG;
    if (table_ca == 0u)
        return DP_TXRX_EARG;
    index = tid + user * DP_TXRX_TABLE_TIDS;
    /* CA = base + 12 * index; the multiply cannot overflow for the bounded
     * user/tid space (0x91*8+7 = 1167 -> 14 KB). */
    *ca = table_ca + DP_TXRX_TABLE_ENTRY_BYTES * index;
    return DP_TXRX_OK;
}

/* ------------------------------------------------------------------ */
/* TX MSDU-info ring entry                                            */
/* ------------------------------------------------------------------ */
int dp_txrx_txe_encode(dp_u8 entry[DP_TXRX_TXE_BYTES], dp_u32 buf_devva,
                       dp_u16 frame_len, dp_u8 path_class,
                       dp_u8 hdr_flag, dp_u8 tid, dp_u8 vap_type)
{
    dp_u32 ctl;

    if (entry == NULL)
        return DP_TXRX_EARG;
    if (frame_len == 0u || frame_len > DP_TXRX_TXE_LEN_MASK ||
        path_class > 7u || tid > 0x0fu || vap_type > 3u || vap_type == 0u)
        return DP_TXRX_EARG;
    ctl = (dp_u32)frame_len;
    ctl |= ((dp_u32)path_class << DP_TXRX_TXE_PATH_SHIFT) &
           DP_TXRX_TXE_PATH_MASK;
    if (hdr_flag)
        ctl |= DP_TXRX_TXE_FLAG15;
    ctl |= ((dp_u32)tid << 16) & DP_TXRX_TXE_TID_MASK;
    ctl |= ((dp_u32)vap_type << DP_TXRX_TXE_VAP_SHIFT) & DP_TXRX_TXE_VAP_MASK;
    dp_txrx_put_le32(entry + 0, buf_devva);
    dp_txrx_put_le32(entry + 4, ctl);
    return DP_TXRX_OK;
}

int dp_txrx_txe_decode(const dp_u8 entry[DP_TXRX_TXE_BYTES],
                       dp_u32 *buf_devva, dp_u32 *ctl)
{
    if (entry == NULL)
        return DP_TXRX_EARG;
    if (buf_devva != NULL)
        *buf_devva = dp_txrx_le32(entry);
    if (ctl != NULL)
        *ctl = dp_txrx_le32(entry + 4);
    return DP_TXRX_OK;
}

int dp_txrx_txe_is_processed(const dp_u8 entry[DP_TXRX_TXE_BYTES])
{
    if (entry == NULL)
        return 0;
    return (dp_txrx_le32(entry + 4) & DP_TXRX_TXE_MAGIC_MASK) ==
           DP_TXRX_TXE_MAGIC_MASK;
}

void dp_txrx_txe_mark_processed(dp_u8 entry[DP_TXRX_TXE_BYTES])
{
    dp_u32 ctl;

    if (entry == NULL)
        return;
    ctl = dp_txrx_le32(entry + 4) | DP_TXRX_TXE_MAGIC_MASK;
    dp_txrx_put_le32(entry + 4, ctl);
}

/* ------------------------------------------------------------------ */
/* packed ring index arithmetic                                       */
/* ------------------------------------------------------------------ */
dp_u32 dp_txrx_idx_pack(dp_u32 index, unsigned int phase)
{
    return (index & 0x7fffu) | (phase != 0u ? 0x8000u : 0u);
}

void dp_txrx_idx_unpack(dp_u32 packed, dp_u32 *index, unsigned int *phase)
{
    if (index != NULL)
        *index = packed & 0x7fffu;
    if (phase != NULL)
        *phase = (packed & 0x8000u) != 0u ? 1u : 0u;
}

dp_u32 dp_txrx_idx_next(dp_u32 packed, dp_u32 depth)
{
    dp_u32 index = packed & 0x7fffu;
    dp_u32 phase = packed & 0x8000u;

    if (depth == 0u || depth > 0x7fffu)
        return packed;
    index++;
    if (index >= depth) {
        index = 0;
        phase ^= 0x8000u;
    }
    return (index & 0x7fffu) | phase;
}

int dp_txrx_idx_count(dp_u32 producer, dp_u32 consumer, dp_u32 depth)
{
    dp_u32 idx_p, idx_c, phase_p, phase_c, distance;

    if (depth == 0u || depth > 0x7fffu)
        return DP_TXRX_EARG;
    idx_p = producer & 0x7fffu;
    idx_c = consumer & 0x7fffu;
    phase_p = producer & 0x8000u;
    phase_c = consumer & 0x8000u;
    if (idx_p >= depth || idx_c >= depth)
        return DP_TXRX_EFORMAT;
    if (phase_p == phase_c) {
        if (idx_p < idx_c)
            return DP_TXRX_EFORMAT; /* consumer overtook the producer */
        distance = idx_p - idx_c;
    } else {
        if (depth == 0u)
            return DP_TXRX_EARG;
        distance = depth - idx_c + idx_p;
    }
    if (distance > depth)
        return DP_TXRX_EFORMAT;
    return (int)distance;
}

/* ------------------------------------------------------------------ */
/* TX frame buffer layout                                             */
/* ------------------------------------------------------------------ */
int dp_txrx_tx_buffer_init(dp_u8 *buf, size_t cap, dp_u32 owner,
                           const dp_u8 *mpdu, size_t mpdu_len,
                           dp_u32 *buf_devva_off)
{
    if (buf == NULL || mpdu == NULL || buf_devva_off == NULL)
        return DP_TXRX_EARG;
    if (mpdu_len == 0u || mpdu_len > DP_TXRX_MAX_MPDU)
        return DP_TXRX_EARG;
    if (cap < DP_TXRX_FRAME_MPDU_OFF + mpdu_len)
        return DP_TXRX_EBOUNDS;
    memset(buf, 0, DP_TXRX_FRAME_MPDU_OFF);
    dp_txrx_put_le32(buf + 0, owner);
    memcpy(buf + DP_TXRX_FRAME_MPDU_OFF, mpdu, mpdu_len);
    *buf_devva_off = DP_TXRX_FRAME_HOST_HDR;
    return DP_TXRX_OK;
}

int dp_txrx_tx_buffer_check(const dp_u8 *buf, size_t cap)
{
    size_t i;

    if (buf == NULL || cap < DP_TXRX_FRAME_MPDU_OFF)
        return DP_TXRX_ESHORT;
    for (i = DP_TXRX_FRAME_HOST_HDR; i < DP_TXRX_FRAME_MPDU_OFF; i++) {
        if (buf[i] != 0u)
            return DP_TXRX_EFORMAT;
    }
    return DP_TXRX_OK;
}

/* ------------------------------------------------------------------ */
/* 802.11 MPDU builders                                               */
/* ------------------------------------------------------------------ */
static void dp_put_addr(dp_u8 *p, const dp_u8 addr[6])
{
    memcpy(p, addr, 6);
}

static void dp_put_bcast(dp_u8 *p)
{
    memset(p, 0xff, 6);
}

/* Common 24-byte header: FC, duration 0, addr1, addr2, addr3, seq. */
static void dp_write_hdr(dp_u8 *out, dp_u16 fc, const dp_u8 a1[6],
                         const dp_u8 a2[6], const dp_u8 a3[6],
                         dp_u16 seq)
{
    dp_txrx_put_le16(out + 0, fc);
    dp_txrx_put_le16(out + 2, 0u);
    dp_put_addr(out + 4, a1);
    dp_put_addr(out + 10, a2);
    dp_put_addr(out + 16, a3);
    dp_txrx_put_le16(out + 22, (dp_u16)((seq & 0x0fffu) << 4));
}

int dp_txrx_build_probe_req(dp_u8 *out, size_t cap, const dp_u8 bssid[6],
                            dp_u16 seq, size_t *len)
{
    static const dp_u8 rates[4] = { 0x82u, 0x84u, 0x8bu, 0x96u };

    if (out == NULL || bssid == NULL || len == NULL)
        return DP_TXRX_EARG;
    if (cap < DP_TXRX_HDR_LEN + 2u + 2u + 2u + sizeof(rates))
        return DP_TXRX_EBOUNDS;
    dp_put_bcast(out + 4);              /* addr1 = DA = broadcast */
    dp_write_hdr(out, DP_TXRX_FC_PROBE_REQ, out + 4, bssid, bssid, seq);
    {
        size_t off = DP_TXRX_HDR_LEN;

        out[off++] = 0u;                /* SSID IE id */
        out[off++] = 0u;                /* wildcard SSID length */
        out[off++] = 1u;                /* supported rates IE */
        out[off++] = (dp_u8)sizeof(rates);
        memcpy(out + off, rates, sizeof(rates));
        off += sizeof(rates);
        *len = off;
    }
    return DP_TXRX_OK;
}

int dp_txrx_build_null_data(dp_u8 *out, size_t cap, const dp_u8 sa[6],
                            dp_u16 seq, size_t *len)
{
    if (out == NULL || sa == NULL || len == NULL)
        return DP_TXRX_EARG;
    if (cap < DP_TXRX_HDR_LEN)
        return DP_TXRX_EBOUNDS;
    dp_put_bcast(out + 4);
    dp_write_hdr(out, DP_TXRX_FC_NULL_DATA, out + 4, sa, sa, seq);
    *len = DP_TXRX_HDR_LEN;
    return DP_TXRX_OK;
}

int dp_txrx_build_qos_null(dp_u8 *out, size_t cap, const dp_u8 sa[6],
                           dp_u16 seq, size_t *len)
{
    if (out == NULL || sa == NULL || len == NULL)
        return DP_TXRX_EARG;
    if (cap < DP_TXRX_QOS_HDR_LEN)
        return DP_TXRX_EBOUNDS;
    dp_put_bcast(out + 4);
    dp_write_hdr(out, DP_TXRX_FC_QOS_NULL, out + 4, sa, sa, seq);
    dp_txrx_put_le16(out + DP_TXRX_HDR_LEN, 0u); /* QoS control: TID 0 */
    *len = DP_TXRX_QOS_HDR_LEN;
    return DP_TXRX_OK;
}

int dp_txrx_build_arp_bcast(dp_u8 *out, size_t cap, const dp_u8 sa[6],
                            const dp_u8 sip[4], const dp_u8 tip[4],
                            dp_u16 seq, size_t *len)
{
    static const dp_u8 llc_snap[8] = { 0xaau, 0xaau, 0x03u, 0x00u, 0x00u,
                                         0x00u, 0x08u, 0x06u };
    dp_u8 *arp;
    size_t off;

    if (out == NULL || sa == NULL || sip == NULL || tip == NULL || len == NULL)
        return DP_TXRX_EARG;
    if (cap < DP_TXRX_HDR_LEN + sizeof(llc_snap) + 28u)
        return DP_TXRX_EBOUNDS;
    dp_put_bcast(out + 4);
    dp_write_hdr(out, DP_TXRX_FC_QOS_DATA, out + 4, sa, sa, seq);
    off = DP_TXRX_HDR_LEN;
    memcpy(out + off, llc_snap, sizeof(llc_snap));
    off += sizeof(llc_snap);
    arp = out + off;
    dp_txrx_put_le16(arp + 0, 0x0001u);  /* htype Ethernet */
    dp_txrx_put_le16(arp + 2, 0x0800u);  /* ptype IPv4 */
    arp[4] = 6u;                          /* hlen */
    arp[5] = 4u;                          /* plen */
    dp_txrx_put_le16(arp + 6, 0x0001u);  /* op request */
    memcpy(arp + 8, sa, 6);               /* sender hardware */
    memcpy(arp + 14, sip, 4);             /* sender protocol */
    memset(arp + 18, 0, 6);               /* target hardware */
    memcpy(arp + 24, tip, 4);             /* target protocol */
    off += 28u;
    if (off > cap)
        return DP_TXRX_EBOUNDS;
    *len = off;
    return DP_TXRX_OK;
}

int dp_txrx_frame_class(dp_u16 fc, dp_u8 *vap_type, dp_u8 *tid,
                        dp_u8 *hdr_flag)
{
    dp_u8 type = (dp_u8)((fc >> 2) & 0x3u);

    if (vap_type == NULL || tid == NULL || hdr_flag == NULL)
        return DP_TXRX_EARG;
    if (type == 0u) {            /* management */
        *vap_type = DP_TXRX_VAP_TYPE_1;
        *tid = 0u;
        *hdr_flag = 1u;
        return DP_TXRX_OK;
    }
    if (type == 2u) {            /* data/QoS: TID comes from QoS control [I] */
        *vap_type = DP_TXRX_VAP_TYPE_2;
        *tid = 0u;
        *hdr_flag = 0u;
        return DP_TXRX_OK;
    }
    return DP_TXRX_EUNKNOWN;     /* control frames need their own builder */
}

/* ------------------------------------------------------------------ */
/* RX descriptor + capture                                            */
/* ------------------------------------------------------------------ */
int dp_txrx_rx_desc_parse(const dp_u8 *buf, size_t cap,
                          struct dp_txrx_rx_desc *out)
{
    if (buf == NULL || out == NULL)
        return DP_TXRX_EARG;
    if (cap < DP_TXRX_RX_DESC_LEN)
        return DP_TXRX_ESHORT;
    memset(out, 0, sizeof(*out));
    out->len_a = (dp_u16)(dp_le16(buf + 0x08) & 0x0fffu);
    out->word0c = dp_le16(buf + 0x0c);
    out->len_b = (dp_u16)(dp_le16(buf + 0x0e) & 0x0fffu);
    out->word10 = dp_txrx_le32(buf + 0x10);
    out->word14 = dp_le16(buf + 0x14);
    out->word16 = dp_le16(buf + 0x16);
    out->len_c = (dp_u16)(dp_le16(buf + 0x18) & 0x0fffu);
    out->band_vap = (dp_u8)((buf[0x19] >> 5) & 0x3u);
    out->flags_1b = buf[0x1b];
    out->word1c = (dp_u16)(dp_le16(buf + 0x1c) >> 4);
    out->payload_len = dp_le16(buf + 0x1e);
    out->word20 = (dp_u16)(dp_le16(buf + 0x20) & 0x0fffu);
    out->flags_21 = buf[0x21];
    out->flags_23 = buf[0x23];
    if (out->payload_len < DP_TXRX_RX_DESC_LEN)
        return DP_TXRX_EFORMAT; /* vendor skb_put keeps the descriptor inside */
    return DP_TXRX_OK;
}

int dp_txrx_rx_dma_ok(const struct dp_txrx_rx_desc *d, size_t ring_buflen)
{
    if (d == NULL)
        return DP_TXRX_EARG;
    if (d->payload_len < DP_TXRX_RX_DESC_LEN)
        return DP_TXRX_EFORMAT;
    if ((size_t)d->payload_len > ring_buflen)
        return DP_TXRX_EBOUNDS;
    return DP_TXRX_OK;
}

int dp_txrx_rx_capture_build(const dp_u8 *ring_buf, size_t ring_cap,
                             dp_u8 ring_kind, dp_u32 ring_index,
                             struct dp_txrx_rx_capture *out)
{
    struct dp_txrx_rx_desc desc;
    size_t mpdu_len;

    if (out == NULL)
        return DP_TXRX_EARG;
    memset(out, 0, sizeof(*out));
    out->ring_kind = ring_kind;
    out->ring_index = ring_index;
    if (ring_buf == NULL)
        return DP_TXRX_EARG;
    if (ring_kind > 1u)
        return DP_TXRX_EARG;
    if (ring_cap < DP_TXRX_RX_DESC_LEN)
        return DP_TXRX_ESHORT;
    if (dp_txrx_rx_desc_parse(ring_buf, ring_cap, &desc) != DP_TXRX_OK)
        return DP_TXRX_EFORMAT;
    if (dp_txrx_rx_dma_ok(&desc, ring_cap) != DP_TXRX_OK)
        return DP_TXRX_EFORMAT;
    memcpy(out->desc, ring_buf, DP_TXRX_RX_DESC_LEN);
    out->desc_ok = 1u;
    mpdu_len = (size_t)desc.payload_len - DP_TXRX_RX_DESC_LEN;
    if (mpdu_len > DP_TXRX_RX_CAPTURE_MPDU)
        mpdu_len = DP_TXRX_RX_CAPTURE_MPDU;
    if (mpdu_len > ring_cap - DP_TXRX_RX_DESC_LEN)
        mpdu_len = ring_cap - DP_TXRX_RX_DESC_LEN;
    memcpy(out->mpdu, ring_buf + DP_TXRX_RX_DESC_LEN, mpdu_len);
    out->mpdu_len = (dp_u16)mpdu_len;
    return DP_TXRX_OK;
}

/* ------------------------------------------------------------------ */
/* RX bookkeeping                                                     */
/* ------------------------------------------------------------------ */
void dp_txrx_rx_state_init(struct dp_txrx_rx_state *st)
{
    if (st == NULL)
        return;
    memset(st, 0, sizeof(*st));
}

int dp_txrx_rx_pending(const struct dp_txrx_rx_state *st,
                       dp_u32 device_cmp_wptr, dp_u32 *count)
{
    int n;

    if (st == NULL || count == NULL)
        return DP_TXRX_EARG;
    n = dp_txrx_idx_count(device_cmp_wptr, st->cmp_rptr, DP_TXRX_RX_CMP_DEPTH);
    if (n < 0)
        return DP_TXRX_EFORMAT;
    *count = (dp_u32)n;
    return DP_TXRX_OK;
}

int dp_txrx_rx_plan(const dp_u8 *cmp_ring, size_t cmp_cap,
                    dp_u32 device_cmp_wptr, struct dp_txrx_rx_state *st,
                    struct dp_txrx_rx_capture *out)
{
    dp_u32 index, count;
    int rc;

    if (cmp_ring == NULL || st == NULL || out == NULL)
        return DP_TXRX_EARG;
    rc = dp_txrx_rx_pending(st, device_cmp_wptr, &count);
    if (rc != DP_TXRX_OK)
        return rc;
    if (count == 0u)
        return DP_TXRX_EBUSY;
    index = st->cmp_rptr & 0x7fffu;
    if (index >= DP_TXRX_RX_CMP_DEPTH || 4u * (index + 1u) > cmp_cap)
        return DP_TXRX_EBOUNDS;
    memset(out, 0, sizeof(*out));
    out->cmp_raw = dp_txrx_le32(cmp_ring + 4u * index);
    out->ring_kind = 0u;                 /* decoder [U] supplies the kind */
    out->ring_index = st->cmp_rptr & 0x7fffu;
    st->last_cmp_raw = out->cmp_raw;
    st->captured++;
    return DP_TXRX_OK;                   /* capture-only: pointers not moved */
}

int dp_txrx_rx_commit(struct dp_txrx_rx_state *st, dp_u8 ring_kind)
{
    if (st == NULL)
        return DP_TXRX_EARG;
    if (ring_kind > 1u)
        return DP_TXRX_EARG;
    st->cmp_rptr = dp_txrx_idx_next(st->cmp_rptr, DP_TXRX_RX_CMP_DEPTH);
    if (ring_kind == 0u)
        st->norm_rptr = dp_txrx_idx_next(st->norm_rptr, DP_TXRX_RX_NORM_DEPTH);
    else
        st->small_rptr = dp_txrx_idx_next(st->small_rptr,
                                          DP_TXRX_RX_SMALL_DEPTH);
    st->consumed++;
    return DP_TXRX_OK;
}

int dp_txrx_rx_free_entry(const dp_u32 *entries, size_t entries_count,
                          dp_u32 host_rptr, dp_u32 *entry)
{
    dp_u32 index;

    if (entries == NULL || entry == NULL)
        return DP_TXRX_EARG;
    index = host_rptr & 0x7fffu;
    if (index >= entries_count)
        return DP_TXRX_EBOUNDS;
    *entry = entries[index];
    return DP_TXRX_OK;
}

/* ====================================================================== */
/* 8. TXQ-table queue-state publish (vendor hmac_update_user_tid_txq_table) */
/* ====================================================================== */
dp_u32 dp_txrx_txq_user_block(unsigned int user)
{
    if (user > DP_TXRX_TXQ_USER_MAX)
        return DP_TXRX_TXQ_USER_MAX + 1u;
    return DP_TXRX_TXQ_USER_BLOCKS_OFF + user * DP_TXRX_TXQ_USER_BLOCK_BYTES;
}

dp_u8 dp_txrx_txq_flags_set(dp_u8 flags, unsigned int tid, bool nonempty)
{
    dp_u8 bit;

    if (tid >= DP_TXRX_TABLE_TIDS)
        return flags;
    bit = (dp_u8)(1u << tid);
    if (nonempty)
        return (dp_u8)(flags | bit);
    return (dp_u8)(flags & (dp_u8)~bit);
}

void dp_txrx_txq_publish(void *table, unsigned int user, unsigned int tid,
                         dp_u32 count, dp_u32 start_sn, bool has_sn,
                         dp_u32 bytes, dp_u16 msec)
{
    dp_u8 *t = (dp_u8 *)table;
    dp_u32 blk;
    dp_u8 flags;

    if (t == NULL || user > DP_TXRX_TXQ_USER_MAX ||
        tid >= DP_TXRX_TABLE_TIDS)
        return;
    blk = dp_txrx_txq_user_block(user);
    if (blk + DP_TXRX_TXQ_USER_BLOCK_BYTES > DP_TXRX_TXQ_TABLE_BYTES ||
        DP_TXRX_TXQ_FLAGS_OFF + user >= DP_TXRX_TXQ_TABLE_BYTES)
        return;

    /* vendor store order: timestamp, queue len/sn/bytes, bitmap */
    dp_txrx_put_le16(t + DP_TXRX_TXQ_TS_OFF, msec);
    dp_txrx_put_le16(t + blk + DP_TXRX_TXQ_QLEN_OFF + 2u * tid, (dp_u16)count);
    if (has_sn)
        dp_txrx_put_le16(t + blk + DP_TXRX_TXQ_START_SN_OFF + 2u * tid,
                         (dp_u16)start_sn);
    dp_txrx_put_le32(t + blk + DP_TXRX_TXQ_BYTES_OFF + 4u * tid, bytes);
    flags = dp_txrx_txq_flags_set(t[DP_TXRX_TXQ_FLAGS_OFF + user], tid,
                                  count != 0u);
    t[DP_TXRX_TXQ_FLAGS_OFF + user] = flags;
}
