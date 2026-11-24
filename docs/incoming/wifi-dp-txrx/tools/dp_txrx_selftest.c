/* SPDX-License-Identifier: GPL-2.0 */
/*
 * dp_txrx_selftest.c - device-free host tests for the wifi-dp-txrx lane
 * (task-122): table-entry publish, MSDU entry encode, packed ring indices,
 * TX buffer layout, 802.11 MPDU builders, RX descriptor parse/capture and
 * completion-ring bookkeeping. No device contact; synthetic vectors only.
 */
#include <stdio.h>
#include <string.h>

#include "dp_txrx.h"

static unsigned int passed;
static unsigned int failed;

static void chk(const char *name, int cond)
{
    if (cond) {
        passed++;
    } else {
        failed++;
        printf("FAIL: %s\n", name);
    }
}

static void chk_u32(const char *name, unsigned int got, unsigned int want)
{
    if (got == want) {
        passed++;
    } else {
        failed++;
        printf("FAIL: %s (got 0x%x want 0x%x)\n", name, got, want);
    }
}

static void chk_rc(const char *name, int got, int want)
{
    if (got == want) {
        passed++;
    } else {
        failed++;
        printf("FAIL: %s (got %d want %d)\n", name, got, want);
    }
}

static const dp_u8 bssid[6] = { 0x02, 0x00, 0x68, 0x69, 0x35, 0x01 };
static const dp_u8 sa[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
static const dp_u8 bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

static void test_table_entry(void)
{
    dp_u8 e[DP_TXRX_TABLE_ENTRY_BYTES];
    dp_u32 ca = 0, acp = 0, base = 0x01060850u;
    size_t i;

    chk_u32("txrx: table entry bytes", DP_TXRX_TABLE_ENTRY_BYTES, 12u);
    chk_u32("txrx: table entry dwords", DP_TXRX_TABLE_ENTRY_DWORDS, 3u);
    chk_u32("txrx: table acp delta", DP_TXRX_TABLE_ACP_DELTA, 0x01000000u);
    chk_u32("txrx: table users bound", DP_TXRX_TABLE_USERS, 0x92u);
    chk_u32("txrx: table tids", DP_TXRX_TABLE_TIDS, 8u);

    dp_txrx_table_entry_pack(e, 0x84543c00u, 0x8001u, 0x0002u);
    chk_u32("txrx: entry w0", dp_txrx_le32(e), 0x84543c00u);
    chk_u32("txrx: entry wptr", dp_txrx_table_entry_wptr(e), 0x8001u);
    chk_u32("txrx: entry rptr", dp_txrx_table_entry_rptr(e), 0x0002u);
    chk_u32("txrx: entry byte order w0", e[0], 0x00u);
    chk_u32("txrx: entry byte order wptr", e[5], 0x80u);

    chk_rc("txrx: entry ca u0t0", dp_txrx_table_entry_ca(base, 0u, 0u, &ca),
           DP_TXRX_OK);
    chk_u32("txrx: ca u0t0", ca, base);
    chk_rc("txrx: entry ca u0t1", dp_txrx_table_entry_ca(base, 0u, 1u, &ca),
           DP_TXRX_OK);
    chk_u32("txrx: ca u0t1", ca, base + 12u);
    chk_rc("txrx: entry ca u1t0", dp_txrx_table_entry_ca(base, 1u, 0u, &ca),
           DP_TXRX_OK);
    chk_u32("txrx: ca u1t0", ca, base + 96u);
    chk_rc("txrx: entry ca u2t0", dp_txrx_table_entry_ca(base, 2u, 0u, &ca),
           DP_TXRX_OK);
    chk_u32("txrx: ca u2t0", ca, base + 192u);
    chk_rc("txrx: entry ca u0t7", dp_txrx_table_entry_ca(base, 0u, 7u, &ca),
           DP_TXRX_OK);
    chk_u32("txrx: ca u0t7", ca, base + 84u);
    chk_rc("txrx: entry ca max", dp_txrx_table_entry_ca(base, 0x91u, 7u, &ca),
           DP_TXRX_OK);
    chk_u32("txrx: ca max", ca, base + 12u * 1167u);
    chk_rc("txrx: entry acp alias",
           dp_txrx_table_entry_acp(base, 0u, 0u, &acp), DP_TXRX_OK);
    chk_u32("txrx: acp alias value", acp, base + 0x01000000u);

    chk_rc("txrx: entry ca refuses user 0x92",
           dp_txrx_table_entry_ca(base, 0x92u, 0u, &ca), DP_TXRX_EARG);
    chk_rc("txrx: entry ca refuses tid 8",
           dp_txrx_table_entry_ca(base, 0u, 8u, &ca), DP_TXRX_EARG);
    chk_rc("txrx: entry ca refuses base 0",
           dp_txrx_table_entry_ca(0u, 0u, 0u, &ca), DP_TXRX_EARG);
    chk_rc("txrx: entry ca refuses null",
           dp_txrx_table_entry_ca(base, 0u, 0u, NULL), DP_TXRX_EARG);
    chk_rc("txrx: entry acp refuses bad tid",
           dp_txrx_table_entry_acp(base, 0u, 9u, &acp), DP_TXRX_EARG);

    for (i = 0; i < DP_TXRX_TABLE_ENTRY_BYTES; i++)
        chk("txrx: packed entry fully written", e[i] != 0xffu || i == 1u);
}

static void test_tx_entry(void)
{
    dp_u8 e[DP_TXRX_TXE_BYTES];
    dp_u32 devva = 0, ctl = 0;

    chk_u32("txrx: txe bytes", DP_TXRX_TXE_BYTES, 8u);
    chk_u32("txrx: txe depth", DP_TXRX_TX_RING_DEPTH, 64u);
    chk_u32("txrx: txe ring bytes", DP_TXRX_TX_RING_BYTES, 512u);
    chk_rc("txrx: txe encode basic",
           dp_txrx_txe_encode(e, 0x84543c00u, 0x3cu, 0u, 0u, 0u,
                              DP_TXRX_VAP_TYPE_2),
           DP_TXRX_OK);
    chk_u32("txrx: txe entry len", e[0], 0x00u);
    chk_u32("txrx: txe entry ctl byte4", e[4], 0x3cu);
    chk_u32("txrx: txe entry ctl byte6", e[6], 0x20u);
    chk_rc("txrx: txe decode", dp_txrx_txe_decode(e, &devva, &ctl),
           DP_TXRX_OK);
    chk_u32("txrx: txe decode devva", devva, 0x84543c00u);
    chk_u32("txrx: txe decode len", ctl & DP_TXRX_TXE_LEN_MASK, 0x3cu);
    chk_u32("txrx: txe decode vap",
            (ctl & DP_TXRX_TXE_VAP_MASK) >> DP_TXRX_TXE_VAP_SHIFT, 2u);
    chk_u32("txrx: txe decode path",
            (ctl & DP_TXRX_TXE_PATH_MASK) >> DP_TXRX_TXE_PATH_SHIFT, 0u);
    chk_u32("txrx: txe decode tid",
            (ctl & DP_TXRX_TXE_TID_MASK) >> 16, 0u);
    chk("txrx: txe fresh not processed", !dp_txrx_txe_is_processed(e));

    chk_rc("txrx: txe encode full ctl",
           dp_txrx_txe_encode(e, 0x81000004u, 0x63cu, 5u, 1u, 7u,
                              DP_TXRX_VAP_TYPE_3),
           DP_TXRX_OK);
    dp_txrx_txe_decode(e, &devva, &ctl);
    chk_u32("txrx: txe full len", ctl & DP_TXRX_TXE_LEN_MASK, 0x63cu);
    chk_u32("txrx: txe full path",
            (ctl & DP_TXRX_TXE_PATH_MASK) >> DP_TXRX_TXE_PATH_SHIFT, 5u);
    chk("txrx: txe full flag15", (ctl & DP_TXRX_TXE_FLAG15) != 0u);
    chk_u32("txrx: txe full tid",
            (ctl & DP_TXRX_TXE_TID_MASK) >> 16, 7u);
    chk_u32("txrx: txe full vap",
            (ctl & DP_TXRX_TXE_VAP_MASK) >> DP_TXRX_TXE_VAP_SHIFT, 3u);
    chk_u32("txrx: txe full ctl", ctl, 0x0037D63Cu);

    chk_rc("txrx: txe refuses len 0",
           dp_txrx_txe_encode(e, 0u, 0u, 0u, 0u, 0u, 1u), DP_TXRX_EARG);
    chk_rc("txrx: txe refuses len 0x1000",
           dp_txrx_txe_encode(e, 0u, 0x1000u, 0u, 0u, 0u, 1u), DP_TXRX_EARG);
    chk_rc("txrx: txe refuses path 8",
           dp_txrx_txe_encode(e, 0u, 0x100u, 8u, 0u, 0u, 1u), DP_TXRX_EARG);
    chk_rc("txrx: txe refuses tid 16",
           dp_txrx_txe_encode(e, 0u, 0x100u, 0u, 0u, 16u, 1u), DP_TXRX_EARG);
    chk_rc("txrx: txe refuses vap 0",
           dp_txrx_txe_encode(e, 0u, 0x100u, 0u, 0u, 0u, 0u), DP_TXRX_EARG);
    chk_rc("txrx: txe refuses vap 4",
           dp_txrx_txe_encode(e, 0u, 0x100u, 0u, 0u, 0u, 4u), DP_TXRX_EARG);
    chk_rc("txrx: txe refuses null entry",
           dp_txrx_txe_encode(NULL, 0u, 0x100u, 0u, 0u, 0u, 1u), DP_TXRX_EARG);
    chk_rc("txrx: txe max len accepted",
           dp_txrx_txe_encode(e, 0u, 0xfffu, 0u, 0u, 0u, 1u), DP_TXRX_OK);

    dp_txrx_txe_mark_processed(e);
    chk("txrx: txe marked processed", dp_txrx_txe_is_processed(e));
    dp_txrx_txe_mark_processed(e);
    chk("txrx: txe mark idempotent", dp_txrx_txe_is_processed(e));
    chk_u32("txrx: txe magic mask", DP_TXRX_TXE_MAGIC_MASK, 0xE0000000u);
}

static void test_indices(void)
{
    dp_u32 idx = 0;
    unsigned int phase = 0;

    chk_u32("txrx: idx pack 0", dp_txrx_idx_pack(0u, 0u), 0u);
    chk_u32("txrx: idx pack phase", dp_txrx_idx_pack(5u, 1u), 0x8005u);
    chk_u32("txrx: idx pack masks upper bits", dp_txrx_idx_pack(0xffffu, 0u),
            0x7fffu);
    dp_txrx_idx_unpack(0x8005u, &idx, &phase);
    chk_u32("txrx: idx unpack index", idx, 5u);
    chk_u32("txrx: idx unpack phase", phase, 1u);
    dp_txrx_idx_unpack(0x0007u, &idx, NULL);
    chk_u32("txrx: idx unpack null phase", idx, 7u);

    chk_u32("txrx: idx next 0", dp_txrx_idx_next(0u, 64u), 1u);
    chk_u32("txrx: idx next 62", dp_txrx_idx_next(62u, 64u), 63u);
    chk_u32("txrx: idx next 63 wraps", dp_txrx_idx_next(63u, 64u), 0x8000u);
    chk_u32("txrx: idx next 0x8000", dp_txrx_idx_next(0x8000u, 64u),
            0x8001u);
    chk_u32("txrx: idx next 0x803f wraps", dp_txrx_idx_next(0x803fu, 64u),
            0u);
    chk_u32("txrx: idx next 256 ring", dp_txrx_idx_next(0xffu, 256u),
            0x8000u);
    chk_u32("txrx: idx next depth 0 no-op", dp_txrx_idx_next(5u, 0u), 5u);
    chk_u32("txrx: idx next bad depth no-op", dp_txrx_idx_next(5u, 0x8000u),
            5u);

    chk_rc("txrx: idx count empty", dp_txrx_idx_count(0u, 0u, 64u), 0);
    chk_rc("txrx: idx count one", dp_txrx_idx_count(1u, 0u, 64u), 1);
    chk_rc("txrx: idx count phase", dp_txrx_idx_count(0x8001u, 0x8000u, 64u),
           1);
    chk_rc("txrx: idx count empty phase", dp_txrx_idx_count(0x8000u, 0x8000u,
                                                           64u), 0);
    chk_rc("txrx: idx count full", dp_txrx_idx_count(0u, 0x8000u, 64u), 64);
    chk_rc("txrx: idx count wrap", dp_txrx_idx_count(0x8000u, 62u, 64u), 2);
    chk_rc("txrx: idx count overtake",
           dp_txrx_idx_count(0u, 1u, 64u), DP_TXRX_EFORMAT);
    chk_rc("txrx: idx count bad index",
           dp_txrx_idx_count(0x8040u, 0u, 64u), DP_TXRX_EFORMAT);
    chk_rc("txrx: idx count depth 0", dp_txrx_idx_count(0u, 0u, 0u),
           DP_TXRX_EARG);
    chk_rc("txrx: idx count cmp ring 4095",
           dp_txrx_idx_count(0u, 0x8000u, 4095u), 4095);
}

static void test_tx_buffer(void)
{
    dp_u8 buf[DP_TXRX_FRAME_AREA];
    dp_u8 mpdu[32];
    dp_u32 off = 0;
    size_t i;

    memset(mpdu, 0xa5, sizeof(mpdu));
    chk_rc("txrx: buffer init",
           dp_txrx_tx_buffer_init(buf, sizeof(buf), 0x11223344u, mpdu,
                                  sizeof(mpdu), &off), DP_TXRX_OK);
    chk_u32("txrx: buffer devva offset", off, 4u);
    chk_u32("txrx: buffer host word", dp_txrx_le32(buf), 0x11223344u);
    chk_u32("txrx: buffer mpdu offset", DP_TXRX_FRAME_MPDU_OFF, 20u);
    chk_u32("txrx: buffer headroom", DP_TXRX_FRAME_DMA_HDR, 16u);
    chk("txrx: buffer dma header zeroed",
        buf[4] == 0u && buf[19] == 0u);
    chk("txrx: buffer mpdu copied", memcmp(buf + 20, mpdu, sizeof(mpdu)) == 0);
    chk_rc("txrx: buffer check", dp_txrx_tx_buffer_check(buf, sizeof(buf)),
           DP_TXRX_OK);
    chk_rc("txrx: buffer check short",
           dp_txrx_tx_buffer_check(buf, 19u), DP_TXRX_ESHORT);
    buf[7] = 1u;
    chk_rc("txrx: buffer check dirty header",
           dp_txrx_tx_buffer_check(buf, sizeof(buf)), DP_TXRX_EFORMAT);
    buf[7] = 0u;
    for (i = 0; i < 16u; i++)
        chk("txrx: buffer dma header byte zero", buf[4u + i] == 0u);

    chk_rc("txrx: buffer init refuses null mpdu",
           dp_txrx_tx_buffer_init(buf, sizeof(buf), 0u, NULL, 4u, &off),
           DP_TXRX_EARG);
    chk_rc("txrx: buffer init refuses len 0",
           dp_txrx_tx_buffer_init(buf, sizeof(buf), 0u, mpdu, 0u, &off),
           DP_TXRX_EARG);
    chk_rc("txrx: buffer init refuses too big mpdu",
           dp_txrx_tx_buffer_init(buf, sizeof(buf), 0u, mpdu,
                                  DP_TXRX_MAX_MPDU + 1u, &off), DP_TXRX_EARG);
    chk_rc("txrx: buffer init refuses small cap",
           dp_txrx_tx_buffer_init(buf, 20u + sizeof(mpdu) - 1u, 0u, mpdu,
                                  sizeof(mpdu), &off), DP_TXRX_EBOUNDS);
    chk_rc("txrx: buffer check null", dp_txrx_tx_buffer_check(NULL, 32u),
           DP_TXRX_ESHORT);
}

static void test_builders(void)
{
    dp_u8 f[256];
    size_t len = 0;

    chk_rc("txrx: probe req", dp_txrx_build_probe_req(f, sizeof(f), bssid,
                                                      0x1234u, &len),
           DP_TXRX_OK);
    chk_u32("txrx: probe req len", len, 32u);
    chk_u32("txrx: probe req fc", f[0], 0x40u);
    chk_u32("txrx: probe req fc second byte", f[1], 0x00u);
    chk("txrx: probe req da bcast", memcmp(f + 4, bcast, 6) == 0);
    chk("txrx: probe req sa bssid", memcmp(f + 10, bssid, 6) == 0);
    chk("txrx: probe req bssid", memcmp(f + 16, bssid, 6) == 0);
    chk_u32("txrx: probe req seq lo", f[22], 0x40u);
    chk_u32("txrx: probe req seq hi", f[23], 0x23u);
    chk_u32("txrx: probe req ssid ie", f[24], 0u);
    chk_u32("txrx: probe req ssid len", f[25], 0u);
    chk_u32("txrx: probe req rates ie", f[26], 1u);
    chk_u32("txrx: probe req rates len", f[27], 4u);
    chk_u32("txrx: probe req rate basic", f[28], 0x82u);
    chk_rc("txrx: probe req refuses short cap",
           dp_txrx_build_probe_req(f, 31u, bssid, 0u, &len), DP_TXRX_EBOUNDS);
    chk_rc("txrx: probe req refuses null bssid",
           dp_txrx_build_probe_req(f, sizeof(f), NULL, 0u, &len),
           DP_TXRX_EARG);

    chk_rc("txrx: null data", dp_txrx_build_null_data(f, sizeof(f), sa,
                                                      7u, &len), DP_TXRX_OK);
    chk_u32("txrx: null data len", len, 24u);
    chk_u32("txrx: null data fc", f[0], 0x48u);
    chk_u32("txrx: null data fc2", f[1], 0x00u);
    chk("txrx: null data da bcast", memcmp(f + 4, bcast, 6) == 0);
    chk("txrx: null data sa", memcmp(f + 10, sa, 6) == 0);
    chk("txrx: null data bssid", memcmp(f + 16, sa, 6) == 0);
    chk_rc("txrx: null data refuses short cap",
           dp_txrx_build_null_data(f, 23u, sa, 0u, &len), DP_TXRX_EBOUNDS);

    chk_rc("txrx: qos null", dp_txrx_build_qos_null(f, sizeof(f), sa, 9u,
                                                    &len), DP_TXRX_OK);
    chk_u32("txrx: qos null len", len, 26u);
    chk_u32("txrx: qos null fc", f[0], 0xc8u);
    chk_u32("txrx: qos null control lo", f[24], 0u);
    chk_u32("txrx: qos null control hi", f[25], 0u);
    chk_rc("txrx: qos null refuses short cap",
           dp_txrx_build_qos_null(f, 25u, sa, 0u, &len), DP_TXRX_EBOUNDS);

    {
        static const dp_u8 sip[4] = { 192u, 168u, 1u, 44u };
        static const dp_u8 tip[4] = { 192u, 168u, 1u, 1u };

        chk_rc("txrx: arp bcast", dp_txrx_build_arp_bcast(f, sizeof(f), sa,
                                                          sip, tip, 3u, &len),
               DP_TXRX_OK);
        chk_u32("txrx: arp len", len, 60u);
        chk_u32("txrx: arp fc", f[0], 0x88u);
        chk("txrx: arp llc snap", memcmp(f + 24, "\xaa\xaa\x03\x00\x00\x00\x08\x06", 8) == 0);
        chk_u32("txrx: arp htype", f[32], 0x01u);
        chk_u32("txrx: arp ptype", f[34], 0x00u);
        chk_u32("txrx: arp ptype hi", f[35], 0x08u);
        chk_u32("txrx: arp op", f[38], 0x01u);
        chk("txrx: arp sha", memcmp(f + 40, sa, 6) == 0);
        chk("txrx: arp spa", memcmp(f + 46, sip, 4) == 0);
        chk("txrx: arp tha zero", f[50] == 0u && f[55] == 0u);
        chk("txrx: arp tpa", memcmp(f + 56, tip, 4) == 0);
        chk_rc("txrx: arp refuses short cap",
               dp_txrx_build_arp_bcast(f, 59u, sa, sip, tip, 0u, &len),
               DP_TXRX_EBOUNDS);
    }

    {
        dp_u8 vap = 0, tid = 0, hdr = 0;

        chk_rc("txrx: class probe req",
               dp_txrx_frame_class(DP_TXRX_FC_PROBE_REQ, &vap, &tid, &hdr),
               DP_TXRX_OK);
        chk_u32("txrx: class mgmt vap", vap, DP_TXRX_VAP_TYPE_1);
        chk_u32("txrx: class mgmt tid", tid, 0u);
        chk_u32("txrx: class mgmt hdr flag", hdr, 1u);
        chk_rc("txrx: class qos data",
               dp_txrx_frame_class(DP_TXRX_FC_QOS_DATA, &vap, &tid, &hdr),
               DP_TXRX_OK);
        chk_u32("txrx: class data vap", vap, DP_TXRX_VAP_TYPE_2);
        chk_u32("txrx: class data tid", tid, 0u);
        chk_u32("txrx: class data hdr flag", hdr, 0u);
        chk_rc("txrx: class control unknown",
               dp_txrx_frame_class(0x00b4u, &vap, &tid, &hdr),
               DP_TXRX_EUNKNOWN);
        chk_rc("txrx: class null out",
               dp_txrx_frame_class(DP_TXRX_FC_NULL_DATA, NULL, &tid, &hdr),
               DP_TXRX_EARG);
    }
}

static void test_rx_desc_capture(void)
{
    dp_u8 buf[DP_TXRX_RX_NORM_ALLOC];
    dp_u8 out[DP_TXRX_RX_CAPTURE_MPDU];
    struct dp_txrx_rx_desc d;
    struct dp_txrx_rx_capture cap;
    struct dp_txrx_rx_capture *cp = &cap;
    size_t i;

    memset(buf, 0, sizeof(buf));
    dp_txrx_put_le16(buf + 0x08, 0x0140u);   /* len_a 0x140 */
    dp_txrx_put_le16(buf + 0x0c, 0x1201u);
    dp_txrx_put_le16(buf + 0x0e, 0x02ffu);   /* len_b 0x2ff */
    buf[0x10] = 0x78u;
    buf[0x11] = 0x56u;
    buf[0x12] = 0x34u;
    buf[0x13] = 0x12u;
    buf[0x19] = 0x40u;                        /* band_vap bits 6:5 = 2 */
    buf[0x1b] = 0xabdu & 0xffu;
    dp_txrx_put_le16(buf + 0x1c, 0x0123u);
    dp_txrx_put_le16(buf + 0x1e, 0x012cu);   /* payload_len 0x12c */
    dp_txrx_put_le16(buf + 0x20, 0x012cu);
    buf[0x21] = 0x50u;
    buf[0x23] = 0x81u;
    for (i = 0; i < sizeof(out); i++)
        buf[DP_TXRX_RX_DESC_LEN + i] = (dp_u8)(0x30u + i);
    buf[DP_TXRX_RX_DESC_LEN + 0] = 0x80u;    /* 802.11 FC byte 0 = beacon */
    buf[DP_TXRX_RX_DESC_LEN + 1] = 0x00u;

    chk_rc("txrx: rx desc parse", dp_txrx_rx_desc_parse(buf, sizeof(buf), &d),
           DP_TXRX_OK);
    chk_u32("txrx: rx desc len_a", d.len_a, 0x140u);
    chk_u32("txrx: rx desc len_b", d.len_b, 0x2ffu);
    chk_u32("txrx: rx desc word10", d.word10, 0x12345678u);
    chk_u32("txrx: rx desc payload len", d.payload_len, 0x12cu);
    chk_u32("txrx: rx desc band_vap", d.band_vap, 2u);
    chk_u32("txrx: rx desc word20 (low 12 bits)", d.word20, 0x02cu);
    chk_rc("txrx: rx desc dma ok", dp_txrx_rx_dma_ok(&d, sizeof(buf)),
           DP_TXRX_OK);
    chk_rc("txrx: rx desc parse short",
           dp_txrx_rx_desc_parse(buf, 0x27u, &d), DP_TXRX_ESHORT);
    dp_txrx_put_le16(buf + 0x1e, 0x0027u);
    chk_rc("txrx: rx desc refuses payload < hdr",
           dp_txrx_rx_desc_parse(buf, sizeof(buf), &d), DP_TXRX_EFORMAT);
    dp_txrx_put_le16(buf + 0x1e, 0x0800u);
    chk_rc("txrx: rx desc parses oversized payload",
           dp_txrx_rx_desc_parse(buf, sizeof(buf), &d), DP_TXRX_OK);
    chk_rc("txrx: rx desc refuses payload > ring buflen",
           dp_txrx_rx_dma_ok(&d, sizeof(buf)), DP_TXRX_EBOUNDS);
    dp_txrx_put_le16(buf + 0x1e, 0x012cu);
    chk_rc("txrx: rx desc reparse", dp_txrx_rx_desc_parse(buf, sizeof(buf),
                                                          &d), DP_TXRX_OK);
    chk_rc("txrx: rx dma ok null", dp_txrx_rx_dma_ok(NULL, sizeof(buf)),
           DP_TXRX_EARG);

    chk_rc("txrx: rx capture build",
           dp_txrx_rx_capture_build(buf, sizeof(buf), 0u, 5u, &cap),
           DP_TXRX_OK);
    chk_u32("txrx: rx capture kind", cap.ring_kind, 0u);
    chk_u32("txrx: rx capture index", cap.ring_index, 5u);
    chk_u32("txrx: rx capture desc ok", cap.desc_ok, 1u);
    chk_u32("txrx: rx capture mpdu len", cap.mpdu_len,
            DP_TXRX_RX_CAPTURE_MPDU);
    chk_u32("txrx: rx capture fc byte", cap.mpdu[0], 0x80u);
    chk_u32("txrx: rx capture desc byte", cap.desc[8], 0x40u);
    chk_rc("txrx: rx capture refuses bad kind",
           dp_txrx_rx_capture_build(buf, sizeof(buf), 2u, 0u, &cap),
           DP_TXRX_EARG);
    chk_rc("txrx: rx capture refuses short",
           dp_txrx_rx_capture_build(buf, 0x27u, 0u, 0u, &cap),
           DP_TXRX_ESHORT);
    chk_rc("txrx: rx capture refuses null buffer",
           dp_txrx_rx_capture_build(NULL, 0u, 0u, 0u, &cap), DP_TXRX_EARG);
    dp_txrx_put_le16(buf + 0x1e, 0x0020u);
    chk_rc("txrx: rx capture refuses invalid desc",
           dp_txrx_rx_capture_build(buf, sizeof(buf), 0u, 0u, &cap),
           DP_TXRX_EFORMAT);
    dp_txrx_put_le16(buf + 0x1e, 0x012cu);
    cp = &cap;
    (void)cp;
}

static void test_rx_bookkeeping(void)
{
    struct dp_txrx_rx_state st;
    struct dp_txrx_rx_capture cap;
    dp_u8 cmp[4u * 8u];
    dp_u32 entry = 0, count = 0;
    const dp_u32 free_entries[4] = { 0x8417c000u, 0x8417c640u, 0x8417cc80u,
                                       0x8417d2c0u };

    dp_txrx_rx_state_init(&st);
    chk_u32("txrx: rx state zero cmp", st.cmp_rptr, 0u);
    chk_u32("txrx: rx state zero captured", st.captured, 0u);
    memset(cmp, 0, sizeof(cmp));
    dp_txrx_put_le32(cmp + 0, 0x0000003cu);
    dp_txrx_put_le32(cmp + 4, 0x0000007cu);
    dp_txrx_put_le32(cmp + 8, 0x0000007cu);
    chk_rc("txrx: rx pending empty",
           dp_txrx_rx_pending(&st, 0u, &count), DP_TXRX_OK);
    chk_u32("txrx: rx pending empty count", count, 0u);
    chk_rc("txrx: rx plan empty", dp_txrx_rx_plan(cmp, sizeof(cmp), 0u, &st,
                                                  &cap), DP_TXRX_EBUSY);
    chk_rc("txrx: rx pending one",
           dp_txrx_rx_pending(&st, 1u, &count), DP_TXRX_OK);
    chk_u32("txrx: rx pending one count", count, 1u);
    chk_rc("txrx: rx plan capture", dp_txrx_rx_plan(cmp, sizeof(cmp), 1u, &st,
                                                    &cap), DP_TXRX_OK);
    chk_u32("txrx: rx plan raw", cap.cmp_raw, 0x0000003cu);
    chk_u32("txrx: rx plan index", cap.ring_index, 0u);
    chk_u32("txrx: rx plan captured", st.captured, 1u);
    chk_u32("txrx: rx plan last raw", st.last_cmp_raw, 0x0000003cu);
    chk_u32("txrx: rx plan does not move cmp", st.cmp_rptr, 0u);
    chk_rc("txrx: rx plan capture-only re-read",
           dp_txrx_rx_plan(cmp, sizeof(cmp), 1u, &st, &cap), DP_TXRX_OK);
    chk_u32("txrx: rx plan does not advance on re-read", st.cmp_rptr, 0u);
    chk_rc("txrx: rx commit norm", dp_txrx_rx_commit(&st, 0u), DP_TXRX_OK);
    chk_u32("txrx: rx commit cmp", st.cmp_rptr, 1u);
    chk_u32("txrx: rx commit norm", st.norm_rptr, 1u);
    chk_u32("txrx: rx commit small", st.small_rptr, 0u);
    chk_u32("txrx: rx commit consumed", st.consumed, 1u);
    chk_rc("txrx: rx commit small", dp_txrx_rx_commit(&st, 1u), DP_TXRX_OK);
    chk_u32("txrx: rx commit2 cmp", st.cmp_rptr, 2u);
    chk_u32("txrx: rx commit2 small", st.small_rptr, 1u);
    chk_rc("txrx: rx commit refuses kind",
           dp_txrx_rx_commit(&st, 2u), DP_TXRX_EARG);
    chk_u32("txrx: rx commit refused no move", st.cmp_rptr, 2u);
    chk_rc("txrx: rx pending two",
           dp_txrx_rx_pending(&st, 4u, &count), DP_TXRX_OK);
    chk_u32("txrx: rx pending two count", count, 2u);
    chk_rc("txrx: rx plan reads second",
           dp_txrx_rx_plan(cmp, sizeof(cmp), 4u, &st, &cap), DP_TXRX_OK);
    chk_u32("txrx: rx plan second raw", cap.cmp_raw, 0x0000007cu);
    chk_u32("txrx: rx plan second index", cap.ring_index, 2u);

    chk_rc("txrx: rx plan refuses short cmp ring",
           dp_txrx_rx_plan(cmp, 4u * 2u, 3u, &st, &cap), DP_TXRX_EBOUNDS);
    chk_rc("txrx: rx pending cmp wrap",
           dp_txrx_rx_pending(&st, 0x8001u, &count), DP_TXRX_OK);
    chk_u32("txrx: rx pending cmp wrap count", count, 4094u);

    chk_rc("txrx: rx free entry", dp_txrx_rx_free_entry(free_entries, 4u, 0u,
                                                       &entry), DP_TXRX_OK);
    chk_u32("txrx: rx free entry value", entry, 0x8417c000u);
    chk_rc("txrx: rx free entry index 3",
           dp_txrx_rx_free_entry(free_entries, 4u, 3u, &entry), DP_TXRX_OK);
    chk_u32("txrx: rx free entry index 3 value", entry, 0x8417d2c0u);
    chk_rc("txrx: rx free entry bounds",
           dp_txrx_rx_free_entry(free_entries, 4u, 4u, &entry),
           DP_TXRX_EBOUNDS);
    chk_rc("txrx: rx free entry null",
           dp_txrx_rx_free_entry(NULL, 4u, 0u, &entry), DP_TXRX_EARG);
}

static void test_full_post_path(void)
{
    dp_u8 mpdu[64];
    dp_u8 frame[DP_TXRX_FRAME_AREA];
    dp_u8 entry[DP_TXRX_TXE_BYTES];
    dp_u8 table_entry[DP_TXRX_TABLE_ENTRY_BYTES];
    size_t mpdu_len = 0;
    dp_u32 off = 0, ctl = 0, devva = 0, wptr = 0;

    chk_rc("txrx: post path probe req",
           dp_txrx_build_probe_req(mpdu, sizeof(mpdu), bssid, 1u, &mpdu_len),
           DP_TXRX_OK);
    chk_rc("txrx: post path buffer",
           dp_txrx_tx_buffer_init(frame, sizeof(frame), 0x100u, mpdu, mpdu_len,
                                  &off), DP_TXRX_OK);
    chk_rc("txrx: post path entry",
           dp_txrx_txe_encode(entry, 0x84000000u + off, (dp_u16)mpdu_len,
                              0u, 1u, 0u, DP_TXRX_VAP_TYPE_1), DP_TXRX_OK);
    dp_txrx_txe_decode(entry, &devva, &ctl);
    chk_u32("txrx: post path devva", devva, 0x84000004u);
    chk_u32("txrx: post path entry len", ctl & DP_TXRX_TXE_LEN_MASK,
            (dp_u32)mpdu_len);
    wptr = dp_txrx_idx_next(0u, DP_TXRX_TX_RING_DEPTH);
    dp_txrx_table_entry_pack(table_entry, 0x84543c00u, wptr,
                             0u);
    chk_u32("txrx: post path table wptr",
            dp_txrx_table_entry_wptr(table_entry), 1u);
    chk_rc("txrx: post path completion pending",
           dp_txrx_idx_count(dp_txrx_table_entry_wptr(table_entry),
                             dp_txrx_table_entry_rptr(table_entry),
                             DP_TXRX_TX_RING_DEPTH), 1);
    dp_txrx_put_le32(table_entry + 8, wptr);
    chk_rc("txrx: post path completion observed",
           dp_txrx_idx_count(dp_txrx_table_entry_wptr(table_entry),
                             dp_txrx_table_entry_rptr(table_entry),
                             DP_TXRX_TX_RING_DEPTH), 0);
    dp_txrx_txe_mark_processed(entry);
    chk("txrx: post path entry processed", dp_txrx_txe_is_processed(entry));
}

int main(void)
{
    test_table_entry();
    test_tx_entry();
    test_indices();
    test_tx_buffer();
    test_builders();
    test_rx_desc_capture();
    test_rx_bookkeeping();
    test_full_post_path();

    printf("SUMMARY: %u passed, %u failed\n", passed, failed);
    return failed == 0u ? 0 : 1;
}
