// SPDX-License-Identifier: GPL-2.0
/*
 * dp_logic_selftest.c - device-free host selftest for the Hi5622V100
 * clean-room data-plane host-side logic (wifi-datapath-bringup lane,
 * task-111).  Builds the same portable sources the kernel module compiles
 * (dp_rings.c + h2d_send_logic.c + h2d_frames.c + hcc_core.c) and asserts:
 *
 *   - the ring geometry decoded from the vendor module (TXQ table 0x2524,
 *     8 x 64 x 8 TX MSDU-info rings, tx_ba_info 0x800, RX free rings 256/512
 *     with 4-byte entries, completion ring 4095 x 4, buffer lengths 0x63c /
 *     0xb0, 4-byte host word before each buffer's DMA area);
 *   - the MAC slot-5 register program order, values, RMW masks and refusals;
 *   - the device-RAM MSDU-info ring-pointer table arithmetic (12-byte entries,
 *     index = tid + user*8, PKTRAM CA -> ACP alias +0x01000000) and the
 *     12-byte entry encoder;
 *   - packed MAC-ring index arithmetic (15-bit index + phase bit; the free
 *     rings publish 0x8000 after a full fill);
 *   - the byte-exact g0 id1 key-0x0320 TXQ-table address sync frame (32-byte
 *     vector) and that the extended builder/gate refuse wrong inputs;
 *   - the new gate rules: txq_addr_sync needs this load's context-create and
 *     is one-shot, with the inherited budget/phase/pending gates.
 *
 * No kernel tree, no device access, no vendor artifact.  Run with the lane's
 * tools/build.sh.
 */
#include <stdio.h>
#include <string.h>

#include "h2d_frames.h"
#include "h2d_send_logic.h"
#include "hcc_core.h"
#include "dp_rings.h"

#define DP_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static int checks_total;
static int checks_fail;

static void check(const char *name, int cond, const char *detail)
{
    checks_total++;
    if (!cond) {
        checks_fail++;
        printf("FAIL: %s%s%s\n", name,
               detail && detail[0] ? " - " : "",
               detail && detail[0] ? detail : "");
    }
}

static void expect_bytes(const char *name, const u8 *got, size_t got_len,
                         const u8 *want, size_t want_len)
{
    size_t i;
    int ok = got_len == want_len && !memcmp(got, want, want_len);

    check(name, ok, "");
    if (!ok) {
        printf("  got  %zu bytes:", got_len);
        for (i = 0; i < got_len; i++)
            printf("%02x", got[i]);
        printf("\n  want %zu bytes:", want_len);
        for (i = 0; i < want_len; i++)
            printf("%02x", want[i]);
        printf("\n");
    }
}

/* ------------------------------------------------------------------ */
/* 1. ring geometry                                                    */
/* ------------------------------------------------------------------ */
static void test_geometry(void)
{
    printf("[geometry]\n");
    check("txq table 0x2524", DP_TXQ_TABLE_BYTES == 0x2524u, "");
    check("txq firmware RAM per-band stride 0x252c",
          DP_TXQ_BAND_STRIDE == 0x252cu, "");
    check("8 TX MSDU rings", DP_TX_MSDU_RINGS == 8u, "");
    check("TX MSDU ring depth 64 (vendor slot +0x38 default)",
          DP_TX_MSDU_RING_DEPTH == 0x40u, "");
    check("TX MSDU ring bytes 8*64*8 = 4096",
          DP_TX_MSDU_RING_BYTES == 4096u, "");
    check("tx_ba_info depth 256 / 0x800 bytes",
          DP_TXBA_DEPTH == 256u && DP_TXBA_BYTES == 0x800u &&
          DP_TXBA_ENTRY_BYTES == 8u, "");
    check("RX norm ring 256 x 4 = 0x400",
          DP_RX_NORM_DEPTH == 256u && DP_RX_NORM_RING_BYTES == 0x400u, "");
    check("RX small ring 512 x 4 = 0x800",
          DP_RX_SMALL_DEPTH == 512u && DP_RX_SMALL_RING_BYTES == 0x800u, "");
    check("RX completion ring 4095 x 4 = 0x3ffc",
          DP_RXCMP_DEPTH == 4095u && DP_RXCMP_RING_BYTES == 0x3ffcu, "");
    check("RX buffer lengths 0x63c / 0xb0",
          DP_RX_NORM_BUFLEN == 0x63cu && DP_RX_SMALL_BUFLEN == 0xb0u, "");
    check("RX buffer stride includes the 4-byte host word",
          DP_RX_NORM_STRIDE == 0x640u && DP_RX_SMALL_STRIDE == 0xb4u &&
          DP_RX_BUF_HDR == 4u, "");
    check("MSDU table entry 12 B, user stride 8, user max 0x91",
          DP_MSDU_TABLE_ENTRY_BYTES == 12u &&
          DP_MSDU_TABLE_USER_STRIDE == 8u &&
          DP_MSDU_TABLE_MAX_USER == 0x91u, "");
    check("MSDU table ACP delta +0x01000000",
          DP_MSDU_TABLE_ACP_DELTA == 0x01000000u, "");
    check("MAC int mask vendor value", DP_MAC_INT_MASK_INIT == 0xfffffcc1u,
          "");
    check("TXQ user block layout", DP_TXQ_USER_BLOCKS_OFF == 0x524u &&
          DP_TXQ_USER_BLOCK_BYTES == 0x40u, "");
    check("host DMA window", DP_HOST_DMA_LO == 0x80000000u &&
          DP_HOST_DMA_HI == 0x8fffffffu, "");
}

/* ------------------------------------------------------------------ */
/* 2. ring index arithmetic                                            */
/* ------------------------------------------------------------------ */
static void test_indices(void)
{
    dp_u32 v = 0;
    dp_u32 idx = 0;
    unsigned int phase = 0, i;
    int ok = 1;

    printf("[ring index]\n");
    for (i = 0; i < DP_RX_NORM_DEPTH; i++) {
        dp_ring_idx_unpack(v, &idx, &phase);
        if (idx != i || phase != 0u) {
            ok = 0;
            break;
        }
        v = dp_ring_idx_next(v, DP_RX_NORM_DEPTH);
    }
    check("256 norm-fill steps -> index 0 phase 1 (0x8000)", ok &&
          v == DP_RING_WPTR_FULL, "");
    check("next after 0x8000 is 0x8001",
          dp_ring_idx_next(DP_RING_WPTR_FULL, DP_RX_NORM_DEPTH) == 0x8001u,
          "");
    {
        dp_u32 w = 0;
        dp_u32 ix = 0;
        unsigned int k;
        unsigned int ph = 0;

        for (k = 0; k < 64; k++)
            w = dp_ring_idx_next(w, DP_TX_MSDU_RING_DEPTH);
        check("depth 64 wraps to 0x8000 after 64 steps", w == 0x8000u, "");
        dp_ring_idx_unpack(dp_ring_idx_pack(0x1234u, 1), &ix, &ph);
        check("pack/unpack roundtrip + phase",
              ix == 0x1234u && ph == 1u, "");
    }
    check("depth 0 leaves the index unchanged",
          dp_ring_idx_next(0x8001u, 0) == 0x8001u, "");
}

/* ------------------------------------------------------------------ */
/* 3. device-RAM MSDU-info ring-pointer table                          */
/* ------------------------------------------------------------------ */
static void test_msdu_table(void)
{
    dp_u32 ca = 0, acp = 0, eca = 0, eacp = 0;
    u8 entry[DP_MSDU_TABLE_ENTRY_BYTES];
    static const u8 want[DP_MSDU_TABLE_ENTRY_BYTES] = {
        0x00, 0x3c, 0x54, 0x84, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };

    printf("[MSDU table]\n");
    check("ready payload 0x01060850 -> CA/ACP 0x02060850",
          dp_ready_payload_ptrtable(0x01060850u, &ca, &acp) == DP_EOK &&
          ca == 0x01060850u && acp == 0x02060850u, "");
    check("ready payload 0 refused",
          dp_ready_payload_ptrtable(0u, &ca, &acp) == DP_ERANGE, "");
    check("ACP-form payload (0x02060850) refused as ready payload",
          dp_ready_payload_ptrtable(0x02060850u, &ca, &acp) == DP_ERANGE, "");
    check("unaligned ready payload refused",
          dp_ready_payload_ptrtable(0x01060851u, &ca, &acp) == DP_EALIGN, "");
    check("ready payload too close to PKTRAM end refused",
          dp_ready_payload_ptrtable(0x011dfff8u, &ca, &acp) == DP_ERANGE, "");
    (void)dp_ready_payload_ptrtable(0x01060850u, &ca, &acp);
    check("entry (user 0, tid 0) = table CA",
          dp_msdu_table_entry_ca(ca, 0u, 0u, &eca, &eacp) == DP_EOK &&
          eca == 0x01060850u, "");
    check("entry (user 0, tid 7) = +12*7 = +0x54",
          dp_msdu_table_entry_ca(ca, 0u, 7u, &eca, &eacp) == DP_EOK &&
          eca == 0x010608a4u, "");
    check("entry (user 1, tid 0) = +12*8 = +0x60",
          dp_msdu_table_entry_ca(ca, 1u, 0u, &eca, &eacp) == DP_EOK &&
          eca == 0x010608b0u, "");
    check("entry ACP alias = CA + 0x01000000",
          dp_msdu_table_entry_ca(ca, 0u, 7u, &eca, &eacp) == DP_EOK &&
          eacp == 0x020608a4u, "");
    check("user > 0x91 refused",
          dp_msdu_table_entry_ca(ca, 0x92u, 0u, &eca, &eacp) == DP_EARG, "");
    check("tid > 7 refused",
          dp_msdu_table_entry_ca(ca, 0u, 8u, &eca, &eacp) == DP_EARG, "");
    dp_msdu_table_entry_encode(entry, 0x84543c00u);
    expect_bytes("12-byte entry {devva,0,0}", entry, sizeof(entry),
                 want, sizeof(want));
    check("TX ring devVA = base + tid*0x200",
          dp_tx_ring_devva(0x84500000u, 3u) == 0x84500600u, "");
    check("RX free-ring entry = buffer DMA + 4",
          dp_rx_buf_entry_devva(0x829d5000u) == 0x829d5004u, "");
}

/* ------------------------------------------------------------------ */
/* 4. MAC slot-5 register program model                                */
/* ------------------------------------------------------------------ */
static struct dp_mac_params good_params(void)
{
    struct dp_mac_params p;

    memset(&p, 0, sizeof(p));
    p.msdu_table_acp = 0x02060850u;
    p.rx_norm_addr = 0x844c7000u;
    p.rx_small_addr = 0x844c7400u;
    p.rx_cmp_addr = 0x83734000u;
    p.txba_addr = 0x83fd7000u;
    p.txba_depth = DP_TXBA_DEPTH_INIT;
    p.rx_norm_len = DP_RX_NORM_BUFLEN;
    p.rx_small_len = DP_RX_SMALL_BUFLEN;
    p.rx_norm_depth = DP_RX_NORM_DEPTH;
    p.rx_small_depth = DP_RX_SMALL_DEPTH;
    p.rx_cmp_depth = DP_RXCMP_DEPTH;
    p.int_mask = DP_MAC_INT_MASK_INIT;
    return p;
}

static void test_mac_program(void)
{
    struct dp_mac_params p = good_params();
    struct dp_mac_reg regs[DP_MAC_REG_COUNT];
    unsigned int count = 0;

    printf("[MAC program]\n");
    check("valid program accepted and has 14 writes",
          dp_mac_program(&p, regs, DP_ARRAY_SIZE(regs), &count) == DP_EOK &&
          count == DP_MAC_REG_COUNT, "");
    check("order 0: +0x10 = ACP(ready id2)",
          regs[0].off == DP_MAC_REG_MSDU_TBL &&
          regs[0].value == 0x02060850u &&
          regs[0].mask == 0xffffffffu, "");
    check("order 1: +0x1c = 0x00b0063c",
          regs[1].off == DP_MAC_REG_RX_BUFLEN &&
          regs[1].value == 0x00b0063cu, "");
    check("order 2..4: RX ring addresses",
          regs[2].off == 0x20u && regs[2].value == 0x844c7000u &&
          regs[3].off == 0x24u && regs[3].value == 0x844c7400u &&
          regs[4].off == 0x28u && regs[4].value == 0x83734000u, "");
    check("order 5..7: ring sizes 256/512/4095 with 12-bit RMW",
          regs[5].off == 0x2cu && regs[5].value == 256u &&
          regs[5].mask == 0xfffu &&
          regs[6].off == 0x30u && regs[6].value == 512u &&
          regs[7].off == 0x34u && regs[7].value == 4095u, "");
    check("order 8: tx_ba depth bits 15:0 = 0x100",
          regs[8].off == DP_MAC_REG_TXBA_DEPTH &&
          regs[8].mask == 0xffffu && regs[8].value == 0x100u, "");
    check("order 9: tx_ba address",
          regs[9].off == DP_MAC_REG_TXBA_ADDR &&
          regs[9].value == 0x83fd7000u, "");
    check("order 10: interrupt mask 0xfffffcc1",
          regs[10].off == DP_MAC_REG_INT_MASK &&
          regs[10].value == DP_MAC_INT_MASK_INIT, "");
    check("order 11..13: wptr/wptr/rptr = 0x8000/0x8000/0",
          regs[11].off == DP_MAC_REG_RXNORM_WPTR &&
          regs[11].value == 0x8000u &&
          regs[12].off == DP_MAC_REG_RXSMALL_WPTR &&
          regs[12].value == 0x8000u &&
          regs[13].off == DP_MAC_REG_RXCMP_RPTR &&
          regs[13].value == 0u, "");
    {
        struct dp_mac_params bad = good_params();

        bad.msdu_table_acp = 0x82060850u;
        check("ACP table pointer outside the ACP window refused",
              dp_mac_program(&bad, regs, DP_MAC_REG_COUNT, &count) ==
              DP_ERANGE, "");
        bad = good_params();
        bad.rx_norm_addr = 0x7fffffffu;
        check("host ring address below the DMA window refused",
              dp_mac_program(&bad, regs, DP_MAC_REG_COUNT, &count) ==
              DP_ERANGE, "");
        bad = good_params();
        bad.rx_norm_depth = 255u;
        check("non-vendor ring depth refused",
              dp_mac_program(&bad, regs, DP_MAC_REG_COUNT, &count) ==
              DP_EARG, "");
        bad = good_params();
        bad.txba_depth = 0x101u;
        check("non-vendor tx_ba depth refused",
              dp_mac_program(&bad, regs, DP_MAC_REG_COUNT, &count) ==
              DP_EARG, "");
        check("too-small output array refused",
              dp_mac_program(&p, regs, DP_MAC_REG_COUNT - 1u, &count) ==
              DP_EARG, "");
        check("NULL params refused",
              dp_mac_program(NULL, regs, DP_MAC_REG_COUNT, &count) ==
              DP_EARG, "");
    }
}

/* ------------------------------------------------------------------ */
/* 5. the key-0x0320 TXQ-table address sync frame                      */
/* ------------------------------------------------------------------ */
static void test_txq_addr_frame(void)
{
    struct h2d_frame f;
    enum h2d_err err = H2D_OK;
    u32 total = 0;
    u8 wire[128];
    static const u8 want[0x20] = {
        0x00, 0x01, 0x00, 0x00, 0x20, 0x00, 0x01, 0x00,
        0x00, 0x00, 0x5a, 0x5a,
        /* token[8] = 0 */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /* key 0x0320, len 4, data = 0x84543c00 LE, tail 0 */
        0x20, 0x03, 0x04, 0x00, 0x00, 0x3c, 0x54, 0x84,
        0x00, 0x00, 0x00, 0x00,
    };

    printf("[txq addr sync frame]\n");
    check("builder accepts dst 0 / fb 0",
          h2d_build_txq_addr_sync(&f, 0u, 0u, 0x84543c00u, &err) == 0,
          h2d_strerror(err));
    check("frame is group 0 id 1, total 0x20, fb 0",
          f.group == 0u && f.id == 1u &&
          f.payload_len == (u16)(0x20u - H2D_HDR_LEN) && f.fb == 0u, "");
    {
        size_t wlen = 0;

        check("wire encode succeeds",
              h2d_frame_encode(&f, wire, sizeof(wire), &wlen, &err) == 0 &&
              wlen == 0x20u, h2d_strerror(err));
    }
    total = (u32)wire[4] | ((u32)wire[5] << 8);
    check("declared total 0x20", total == 0x20u, "");
    expect_bytes("byte-exact 32-byte key-0x0320 frame", wire, 0x20u,
                 want, sizeof(want));
    check("builder refuses a non-zero dst",
          h2d_build_txq_addr_sync(&f, 1u, 0u, 0x84543c00u, &err) != 0, "");
    check("extended build requires the 4-byte device VA",
          h2d_msg_build_ext(H2D_MSG_TXQ_ADDR_SYNC, 0u, 0u, NULL, 0u, 0u,
                            &f, wire, sizeof(wire), &total, &err) != 0, "");
    {
        static const u8 le[4] = { 0x00, 0x3c, 0x54, 0x84 };

        check("extended build carries the devVA from addr",
              h2d_msg_build_ext(H2D_MSG_TXQ_ADDR_SYNC, 0u, 0u, le, 0u, 0u,
                                &f, wire, sizeof(wire), &total, &err) == 0 &&
              wire[24] == 0x00 && wire[25] == 0x3c && wire[26] == 0x54 &&
              wire[27] == 0x84 &&
              total == 0x20u, h2d_strerror(err));
    }
    {
        u32 nlen = 0;
        u8 pad = 0xaa;

        check("emit plan: node length == declared total, pad 0",
              h2d_msg_emit_plan(H2D_MSG_TXQ_ADDR_SYNC, 0x20u, &nlen,
                                &pad) == 0 && nlen == 0x20u && pad == 0u,
              "");
    }
    check("message name", strcmp(h2d_msg_name(H2D_MSG_TXQ_ADDR_SYNC),
                                 "txq_addr_sync") == 0, "");
    check("message name parses",
          h2d_msg_name_parse("txq_addr_sync", &(enum h2d_msg){0}) == 0, "");
}

/* ------------------------------------------------------------------ */
/* 6. the extended gate matrix                                         */
/* ------------------------------------------------------------------ */
static struct h2d_live good_live(void)
{
    struct h2d_live live;

    memset(&live, 0, sizeof(live));
    live.armed = true;
    live.released = true;
    live.boot_latched = true;
    live.phase = HCC_PHASE_READY_TXQ_SYNC;
    live.ctx_created = true;
    live.frames_sent = 1u;
    live.budget = 3u;
    return live;
}

static void test_gates(void)
{
    struct h2d_live live = good_live();

    printf("[gates]\n");
    check("txq_addr_sync OK with ctx_create done",
          h2d_gate_check(&live, H2D_MSG_TXQ_ADDR_SYNC) == H2D_GATE_OK, "");
    live.ctx_created = false;
    check("txq_addr_sync refuses without this load's ctx_create",
          h2d_gate_check(&live, H2D_MSG_TXQ_ADDR_SYNC) == H2D_GATE_NEED_CTX,
          "");
    live = good_live();
    live.txq_addr_sent = true;
    check("txq_addr_sync is one-shot",
          h2d_gate_check(&live, H2D_MSG_TXQ_ADDR_SYNC) ==
          H2D_GATE_DUP_TXQ_ADDR, "");
    live = good_live();
    live.frames_sent = 3u;
    check("budget gate still applies",
          h2d_gate_check(&live, H2D_MSG_TXQ_ADDR_SYNC) == H2D_GATE_BUDGET, "");
    live = good_live();
    live.mbox_inflight = 1u;
    check("announce-in-flight gate still applies",
          h2d_gate_check(&live, H2D_MSG_TXQ_ADDR_SYNC) ==
          H2D_GATE_INFLIGHT, "");
    live = good_live();
    live.phase = HCC_PHASE_ACTIVE;
    check("ACTIVE is allowed after the create (ordered ladder)",
          h2d_gate_check(&live, H2D_MSG_TXQ_ADDR_SYNC) == H2D_GATE_OK, "");
    live = good_live();
    live.phase = HCC_PHASE_WAIT_DEVICE_READY;
    check("pre-ready phase refused",
          h2d_gate_check(&live, H2D_MSG_TXQ_ADDR_SYNC) == H2D_GATE_PHASE, "");
    check("new gate names",
          strcmp(h2d_gate_name(H2D_GATE_NEED_CTX),
                 "ctx_create_required") == 0 &&
          strcmp(h2d_gate_name(H2D_GATE_DUP_TXQ_ADDR),
                 "txq_addr_already_sent") == 0, "");
}

int main(void)
{
    printf("dp_rings selftest (host, device-free)\n");
    test_geometry();
    test_indices();
    test_msdu_table();
    test_mac_program();
    test_txq_addr_frame();
    test_gates();
    printf("dp_rings selftest: %d/%d checks PASS\n",
           checks_total - checks_fail, checks_total);
    return checks_fail ? 1 : 0;
}
