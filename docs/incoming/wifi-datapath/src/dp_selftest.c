/* SPDX-License-Identifier: GPL-2.0 */
/*
 * dp_selftest.c - host-only known-answer and validation suite for dp_frames.
 * Lane wifi-datapath (task-105). Build/run: tools/build.sh
 *
 * Vectors are protocol facts sourced in REPORT.md:
 *  - ctx-create frame: synthesized + hardware-validated (wifi-ctx-create lane, lap 2026-11-25)
 *  - txq config sync 0xd8: vendor live capture (phase20 tx-path A.5 / phase38)
 *  - txq address sync 0x320: decoded from the shipped 2.5.24 host module
 *  - MAC data-ring constants: vendor .ko + live register dump (phase43)
 */
#include "dp_frames.h"

#include <stdio.h>
#include <string.h>

static int g_pass, g_fail;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) {                                                            \
            g_pass++;                                                          \
        } else {                                                               \
            g_fail++;                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
        }                                                                      \
    } while (0)

static int hexeq(const uint8_t *buf, size_t len, const uint8_t *ref, size_t reflen)
{
    return len == reflen && memcmp(buf, ref, len) == 0;
}

static const uint8_t ctx_create_ref[] = {
    0x00, 0x01, 0x00, 0x04, 0x30, 0x00, 0x01, 0x00, 0x00, 0x00, 0x5a, 0x5a,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x64, 0x00, 0x14, 0x00,
    0x03, 0x04, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff,
};

static const uint8_t txq_sync_ref[] = {
    0x00, 0x01, 0x00, 0x04, 0x30, 0x00, 0x01, 0x00, 0x00, 0x00, 0x5a, 0x5a,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0xd8, 0x00, 0x14, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff,
};

static void test_header_validation(void)
{
    struct dp_frame f;
    uint8_t g;
    uint16_t id, total;
    int err;
    uint8_t bad[64];

    err = dp_build_ctx_create(&f, 3, 4, 4, 4);
    CHECK(err == DP_OK);
    CHECK(f.len == 48);
    err = dp_frame_decode(f.buf, f.len, &g, &id, &total);
    CHECK(err == DP_OK);
    CHECK(g == 0 && id == 1 && total == 0x30);
    CHECK(dp_frame_validate(f.buf, 11) == DP_ESHORT);
    memcpy(bad, f.buf, f.len);
    bad[0x0a] = 0x00;
    CHECK(dp_frame_validate(bad, f.len) == DP_ETAG);
    memcpy(bad, f.buf, f.len);
    bad[0x04] = 0x00;
    bad[0x05] = 0x00;
    CHECK(dp_frame_validate(bad, f.len) == DP_ELEN);
    memcpy(bad, f.buf, f.len);
    bad[0x04] = 0xff;
    bad[0x05] = 0xff;
    CHECK(dp_frame_validate(bad, f.len) == DP_ELEN);
    memcpy(bad, f.buf, f.len);
    bad[0x00] = 0x04;
    CHECK(dp_frame_validate(bad, f.len) == DP_EGROUP);
    memcpy(bad, f.buf, f.len);
    bad[0x01] = 0x02;
    CHECK(dp_frame_validate(bad, f.len) == DP_EALLOC);
    memcpy(bad, f.buf, f.len);
    bad[0x09] = 0x03;
    CHECK(dp_frame_validate(bad, f.len) == DP_EDST);
    CHECK(dp_frame_validate(NULL, 12) == DP_ESHORT);
}

static void test_ctx_create(void)
{
    struct dp_frame f;
    int err;
    uint8_t out[96];
    size_t n = 0;

    err = dp_build_ctx_create(&f, 3, 4, 4, 4);
    CHECK(err == DP_OK);
    CHECK(hexeq(f.buf, f.len, ctx_create_ref, sizeof(ctx_create_ref)));
    err = dp_frame_export(&f, out, sizeof(out), 72, 0xff, &n);
    CHECK(err == DP_OK && n == 72);
    CHECK(memcmp(out, ctx_create_ref, 48) == 0);
    CHECK(out[48] == 0xff && out[71] == 0xff);
    CHECK(dp_build_ctx_create(&f, 0, 4, 4, 4) == DP_EARG);
    CHECK(dp_build_ctx_create(&f, 4, 4, 4, 4) == DP_EARG);
    CHECK(dp_build_ctx_create(&f, 3, 4, 0x13, 4) == DP_EARG);
}

static void test_txq_sync_capture(void)
{
    struct dp_frame f;
    int err;

    err = dp_build_txq_sync_capture(&f);
    CHECK(err == DP_OK);
    CHECK(f.len == 48);
    CHECK(hexeq(f.buf, f.len, txq_sync_ref, sizeof(txq_sync_ref)));
    CHECK(f.buf[0x04] == 0x30 && f.buf[0x05] == 0x00);
}

static void test_txq_addr_sync(void)
{
    struct dp_frame f;
    int err;

    err = dp_build_txq_addr_sync(&f, 0x84543c00u, 0);
    CHECK(err == DP_OK);
    CHECK(f.len == 0x20);
    CHECK((f.buf[0] & 0x0fu) == 0);
    CHECK(f.buf[0x06] == 0x01 && f.buf[0x07] == 0x00);
    CHECK(f.buf[0x03] == 0x00);
    CHECK(f.buf[0x04] == 0x20 && f.buf[0x05] == 0x00);
    CHECK(f.buf[0x14] == 0x20 && f.buf[0x15] == 0x03);
    CHECK(f.buf[0x16] == 0x04 && f.buf[0x17] == 0x00);
    CHECK(f.buf[0x18] == 0x00 && f.buf[0x19] == 0x3c && f.buf[0x1a] == 0x54 &&
          f.buf[0x1b] == 0x84);
    CHECK(dp_frame_validate(f.buf, f.len) == DP_OK);
}

static void test_builders_generic(void)
{
    struct dp_frame f;
    uint8_t data[4] = { 0xaa, 0xbb, 0xcc, 0xdd };
    int err;

    err = dp_build_g0_cfg(&f, 2, 0, 0x0dae, data, sizeof(data));
    CHECK(err == DP_OK);
    CHECK(f.len == 0x20);
    CHECK(f.buf[0x06] == 0x02);
    CHECK(f.buf[0x14] == 0xae && f.buf[0x15] == 0x0d);
    CHECK(f.buf[0x16] == 0x04);
    CHECK(memcmp(f.buf + 0x18, data, 4) == 0);
    CHECK(dp_build_g0_cfg(&f, 3, 0, 1, data, 4) == DP_EID);
    CHECK(dp_build_g0_cfg(&f, 1, 0, 1, NULL, 4) == DP_EARG);
    CHECK(dp_build_g0_cfg(&f, 1, 0, 1, data, 0x670) == DP_EBOUNDS);
}

static void test_tx_entry(void)
{
    uint8_t e[DP_TX_ENTRY_LEN];
    uint32_t ctl;
    int err;

    memset(e, 0, sizeof(e));
    err = dp_tx_msdu_entry_encode(e, 0x84543000u, 1500, 2, 1, 5, 2);
    CHECK(err == DP_OK);
    CHECK(dp_tx_msdu_entry_buf(e) == 0x84543000u);
    ctl = (uint32_t)e[4] | ((uint32_t)e[5] << 8) | ((uint32_t)e[6] << 16) |
          ((uint32_t)e[7] << 24);
    CHECK(dp_tx_msdu_entry_len(ctl) == 1500);
    CHECK(((ctl & DP_TXE_PATH_MASK) >> DP_TXE_PATH_SHIFT) == 2);
    CHECK((ctl & DP_TXE_FLAG15) != 0);
    CHECK(((ctl & DP_TXE_TID_MASK) >> 16) == 5);
    CHECK(((ctl & DP_TXE_VAP_MASK) >> DP_TXE_VAP_SHIFT) == 2);
    CHECK(dp_tx_msdu_entry_encode(e, 0, 0x1000, 0, 0, 0, 0) == DP_EARG);
    CHECK(dp_tx_msdu_entry_encode(e, 0, 1, 8, 0, 0, 0) == DP_EARG);
    CHECK(dp_tx_msdu_entry_encode(e, 0, 1, 0, 0, 0x10, 0) == DP_EARG);
    CHECK(dp_tx_msdu_entry_encode(e, 0, 1, 0, 0, 0, 4) == DP_EARG);
}

static void test_tx_index(void)
{
    uint32_t idx;
    unsigned phase;

    CHECK(dp_tx_idx_next(0, 32) == 1);
    CHECK(dp_tx_idx_next(31, 32) == 0x8000);
    CHECK(dp_tx_idx_next(0x8000, 32) == 0x8001);
    CHECK(dp_tx_idx_next(0x801f, 32) == 0);
    dp_tx_idx_unpack(0x8005, &idx, &phase);
    CHECK(idx == 5 && phase == 1);
    dp_tx_idx_unpack(0x0007, &idx, &phase);
    CHECK(idx == 7 && phase == 0);
    CHECK(dp_tx_idx_pack(5, 1) == 0x8005);
    CHECK(dp_tx_idx_next(0, 0) == 0);
}

static void test_ring_constants(void)
{
    CHECK(DP_RX_DESC_LEN == 0x28);
    CHECK(DP_RX_NORM_BUFLEN == 0x63c);
    CHECK(DP_RX_SMALL_BUFLEN == 0xb0);
    CHECK(DP_RXNORM_RING_SIZE == 256 && DP_RXSMALL_RING_SIZE == 512);
    CHECK(DP_RXCMP_RING_SIZE == 4095);
    CHECK(DP_TXBA_RING_DEPTH == 256);
    CHECK(DP_MAC_REG_MSDU_TBL == 0x10 && DP_MAC_REG_RXNORM_ADDR == 0x20 &&
          DP_MAC_REG_RXCMP_ADDR == 0x28 && DP_MAC_REG_INT_MASK == 0x48);
}

static void test_catalogue(void)
{
    const struct dp_msg_row *row;
    size_t n = 0;

    (void)dp_msg_catalogue(&n);
    CHECK(n >= 40);
    row = dp_msg_lookup(0, 0x01);
    CHECK(row != NULL && row->fw_handler_ca == 0x00043D59u);
    row = dp_msg_lookup(0, 0x0C);
    CHECK(row != NULL && row->fixed_payload == 0x1FCu);
    row = dp_msg_lookup(0, 0x07);
    CHECK(row != NULL && row->fixed_payload == 0xA8u);
    row = dp_msg_lookup(0, 0x0F);
    CHECK(row != NULL && row->fixed_payload == 0x40u);
    row = dp_msg_lookup(2, 0x15);
    CHECK(row != NULL && row->fixed_payload == 0x108u);
    CHECK(dp_msg_lookup(4, 1) == NULL);
    CHECK(dp_msg_lookup(0, 0x7f) == NULL);
}

int main(void)
{
    test_header_validation();
    test_ctx_create();
    test_txq_sync_capture();
    test_txq_addr_sync();
    test_builders_generic();
    test_tx_entry();
    test_tx_index();
    test_ring_constants();
    test_catalogue();
    printf("SUMMARY: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}