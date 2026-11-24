// SPDX-License-Identifier: GPL-2.0
/*
 * merged_selftest.c - device-free host selftest for the wifi-dp-air merge
 * (Hi5622V100 clean-room driver, Cudy WR3000 v2.0, luofu v2).
 *
 * Builds the SAME portable sources the merged kernel module compiles against
 * (dp_rings.c + dp_txrx.c + h2d_send_logic.c + h2d_frames.c + hcc_core.c) and
 * asserts the merge contract:
 *
 *   - E1 data-plane geometry, ring index arithmetic, device-RAM MSDU-info
 *     table arithmetic and the MAC slot-5 register program (unchanged from
 *     the hardware-validated wifi-datapath-bringup lane);
 *   - the data-plane hand-off (g0 id1 key 0x0320) exact builder, wire image
 *     and the merged post-path build function;
 *   - the hand-off gate (needs this load's ctx_create, once per load, and every
 *     inherited boot/phase/budget/coalescing gate);
 *   - the design invariant that the hand-off is NOT an enum h2d_msg member
 *     (the inherited every-message emission loop and grammar cannot trigger it)
 *     and that the provider's h2d message table is unchanged (H2D_MSG_COUNT);
 *   - the same surface the merged kernel module links; the inherited txrx
 *     portable selftest (254 checks) covers the dp_txrx layer separately.
 *
 * No kernel tree, no device access, no vendor artifact, no network.
 * Run with the lane's tools/build.sh; inherited regressions run separately.
 */
#include <stdio.h>
#include <string.h>

#include "h2d_frames.h"
#include "h2d_send_logic.h"
#include "hcc_core.h"
#include "dp_rings.h"

/* dp_txrx.h defines the same portable typedefs as dp_rings.h (two lanes, one
 * clean-room driver); the merged kernel module compiles both in the same
 * translation unit (C11 permits identical typedef redefinition), but this
 * -pedantic host test deliberately includes only dp_rings.h.  The txrx
 * portable layer is covered by the inherited 254-check host selftest that
 * tools/build.sh also runs against this tree's dp_txrx.c. */

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
/* 1. merged-tree shape (the design invariant)                         */
/* ------------------------------------------------------------------ */
static void test_merge_shape(void)
{
    enum h2d_msg m = H2D_MSG_STUB;

    printf("[merge shape]\n");
    /* Provider message table must stay byte-compatible: no new enum member
     * (8 selectors: stub, txq_sync, txq_sync72, edca, del_user, ctx_create
     * and its t1/t2 variants; COUNT == 8). */
    check("provider enum h2d_msg unchanged (COUNT == 8, no new member)",
          (int)H2D_MSG_COUNT == 8, "");
    check("data-plane hand-off is not an enum h2d_msg member",
          h2d_msg_name_parse("txq_addr_sync", &m) != 0, "");
    check("hand-off frame constants (total 0x20, key 0x0320)",
          H2D_TXQ_ADDR_TOTAL == 0x20u && H2D_TXQ_ADDR_KEY == 0x0320u, "");
}

/* ------------------------------------------------------------------ */
/* 2. ring geometry (E1, unchanged)                                    */
/* ------------------------------------------------------------------ */
static void test_geometry(void)
{
    printf("[geometry]\n");
    check("txq table 0x2524", DP_TXQ_TABLE_BYTES == 0x2524u, "");
    check("txq firmware RAM per-band stride 0x252c",
          DP_TXQ_BAND_STRIDE == 0x252cu, "");
    check("8 TX MSDU rings x depth 64 x 8 B",
          DP_TX_MSDU_RINGS == 8u && DP_TX_MSDU_RING_DEPTH == 0x40u &&
          DP_TX_MSDU_RING_BYTES == 4096u, "");
    check("tx_ba_info depth 256 / 0x800 bytes",
          DP_TXBA_DEPTH == 256u && DP_TXBA_BYTES == 0x800u, "");
    check("RX rings 256/512/4095 x 4 B",
          DP_RX_NORM_DEPTH == 256u && DP_RX_SMALL_DEPTH == 512u &&
          DP_RXCMP_DEPTH == 4095u && DP_RX_NORM_RING_BYTES == 0x400u &&
          DP_RX_SMALL_RING_BYTES == 0x800u &&
          DP_RXCMP_RING_BYTES == 0x3ffcu, "");
    check("RX buffer lengths 0x63c / 0xb0 + 4-byte host word",
          DP_RX_NORM_BUFLEN == 0x63cu && DP_RX_SMALL_BUFLEN == 0xb0u &&
          DP_RX_NORM_STRIDE == 0x640u && DP_RX_SMALL_STRIDE == 0xb4u, "");
    check("MSDU table entry 12 B, user stride 8, ACP delta",
          DP_MSDU_TABLE_ENTRY_BYTES == 12u &&
          DP_MSDU_TABLE_USER_STRIDE == 8u &&
          DP_MSDU_TABLE_ACP_DELTA == 0x01000000u, "");
    check("MAC int mask 0xfffffcc1 / host DMA window",
          DP_MAC_INT_MASK_INIT == 0xfffffcc1u &&
          DP_HOST_DMA_LO == 0x80000000u && DP_HOST_DMA_HI == 0x8fffffffu,
          "");
}

/* ------------------------------------------------------------------ */
/* 3. ring index + device-RAM table arithmetic (E1, unchanged)         */
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
    check("256 norm-fill steps -> 0x8000 (index 0, phase 1)",
          ok && v == DP_RING_WPTR_FULL, "");
    check("depth 64 wraps to 0x8000 after 64 steps",
          dp_ring_idx_next(0x8000u - 1u, DP_TX_MSDU_RING_DEPTH) == 0x8000u ||
          dp_ring_idx_next(0x003fu, DP_TX_MSDU_RING_DEPTH) == 0x8000u, "");
}

static void test_msdu_table(void)
{
    dp_u32 ca = 0, acp = 0, eca = 0, eacp = 0;
    u8 entry[DP_MSDU_TABLE_ENTRY_BYTES];
    static const u8 want[DP_MSDU_TABLE_ENTRY_BYTES] = {
        0x00, 0x3c, 0x54, 0x84, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };

    printf("[MSDU table]\n");
    check("ready payload 0x01060850 -> CA 0x01060850 / ACP 0x02060850",
          dp_ready_payload_ptrtable(0x01060850u, &ca, &acp) == DP_EOK &&
          ca == 0x01060850u && acp == 0x02060850u, "");
    check("ACP-form / zero / unaligned ready payload refused",
          dp_ready_payload_ptrtable(0x02060850u, &ca, &acp) != DP_EOK &&
          dp_ready_payload_ptrtable(0u, &ca, &acp) != DP_EOK &&
          dp_ready_payload_ptrtable(0x01060851u, &ca, &acp) != DP_EOK, "");
    check("entry (user 0, tid 7) = CA + 0x54, ACP alias +0x01000000",
          dp_msdu_table_entry_ca(0x01060850u, 0u, 7u, &eca, &eacp) == DP_EOK &&
          eca == 0x010608a4u && eacp == 0x020608a4u, "");
    dp_msdu_table_entry_encode(entry, 0x84543c00u);
    expect_bytes("12-byte table entry {devva,0,0}", entry, sizeof(entry),
                 want, sizeof(want));
    check("TX ring devVA = base + tid*0x200",
          dp_tx_ring_devva(0x84500000u, 3u) == 0x84500600u, "");
    check("RX free-ring entry = buffer DMA + 4",
          dp_rx_buf_entry_devva(0x829d5000u) == 0x829d5004u, "");
}

/* ------------------------------------------------------------------ */
/* 4. MAC slot-5 register program (E1, unchanged)                      */
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
    check("valid program accepted, 14 writes",
          dp_mac_program(&p, regs, DP_ARRAY_SIZE(regs), &count) == DP_EOK &&
          count == DP_MAC_REG_COUNT, "");
    check("order 0: +0x10 = ACP(ready id2)",
          regs[0].off == DP_MAC_REG_MSDU_TBL &&
          regs[0].value == 0x02060850u, "");
    check("order 1: +0x1c = 0x00b0063c",
          regs[1].off == DP_MAC_REG_RX_BUFLEN &&
          regs[1].value == 0x00b0063cu, "");
    check("order 2..4: RX ring addresses",
          regs[2].value == 0x844c7000u && regs[3].value == 0x844c7400u &&
          regs[4].value == 0x83734000u, "");
    check("order 5..7: ring sizes 256/512/4095 (12-bit RMW)",
          regs[5].value == 256u && regs[5].mask == 0xfffu &&
          regs[6].value == 512u && regs[7].value == 4095u, "");
    check("order 8: tx_ba depth 0x100", regs[8].value == 0x100u, "");
    check("order 9: tx_ba address", regs[9].value == 0x83fd7000u, "");
    check("order 10: int mask", regs[10].value == DP_MAC_INT_MASK_INIT, "");
    check("order 11..13: wptr/wptr/rptr = 0x8000/0x8000/0",
          regs[11].value == 0x8000u && regs[12].value == 0x8000u &&
          regs[13].value == 0u, "");
    {
        struct dp_mac_params bad = good_params();

        bad.msdu_table_acp = 0x82060850u;
        check("ACP table pointer outside the ACP window refused",
              dp_mac_program(&bad, regs, DP_MAC_REG_COUNT, &count) ==
              DP_ERANGE, "");
        bad = good_params();
        bad.rx_norm_depth = 255u;
        check("non-vendor ring depth refused",
              dp_mac_program(&bad, regs, DP_MAC_REG_COUNT, &count) ==
              DP_EARG, "");
        check("NULL params refused",
              dp_mac_program(NULL, regs, DP_MAC_REG_COUNT, &count) ==
              DP_EARG, "");
    }
}

/* ------------------------------------------------------------------ */
/* 5. the data-plane hand-off (g0 id1 key 0x0320)                      */
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
    check("exact builder accepts dst 0 / fb 0",
          h2d_build_txq_addr_sync(&f, 0u, 0u, 0x84543c00u, &err) == 0,
          h2d_strerror(err));
    check("frame is group 0 id 1, total 0x20, fb 0",
          f.group == 0u && f.id == 1u && f.fb == 0u &&
          f.payload_len == (u16)(0x20u - H2D_HDR_LEN), "");
    check("exact builder refuses a non-zero dst",
          h2d_build_txq_addr_sync(&f, 1u, 0u, 0x84543c00u, &err) != 0, "");
    check("merged post-path build succeeds + declared total 0x20",
          h2d_msg_build_txq_addr(&f, 0x84543c00u, wire, sizeof(wire), &total,
                                 &err) == 0 && total == 0x20u,
          h2d_strerror(err));
    expect_bytes("byte-exact 32-byte key-0x0320 frame", wire, 0x20u,
                 want, sizeof(want));
    check("build carries the devVA little-endian at wire[24..27]",
          wire[24] == 0x00u && wire[25] == 0x3cu &&
          wire[26] == 0x54u && wire[27] == 0x84u, "");
    check("build refuses a zero device VA (dv != 0 in the post path)",
          h2d_msg_build_txq_addr(&f, 0u, wire, sizeof(wire), &total, &err)
          != 0, "");
    {
        u32 nlen = 0;
        u8 pad = 0xaa;

        check("emit plan for the hand-off: node length == total, pad 0",
              h2d_msg_emit_plan(H2D_MSG_STUB, 0x20u, &nlen, &pad) == 0 &&
              nlen == 0x20u && pad == 0u, "");
    }
}

/* ------------------------------------------------------------------ */
/* 6. the hand-off gate                                                */
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
    check("hand-off OK with this load's ctx_create done",
          h2d_gate_check_txq_addr(&live) == H2D_GATE_OK, "");
    live.ctx_created = false;
    check("hand-off refuses without this load's ctx_create",
          h2d_gate_check_txq_addr(&live) == H2D_GATE_NEED_CTX, "");
    live = good_live();
    live.txq_addr_sent = true;
    check("hand-off is one-shot per load",
          h2d_gate_check_txq_addr(&live) == H2D_GATE_DUP_TXQ_ADDR, "");
    live = good_live();
    live.frames_sent = 3u;
    check("inherited budget gate still applies",
          h2d_gate_check_txq_addr(&live) == H2D_GATE_BUDGET, "");
    live = good_live();
    live.mbox_inflight = 1u;
    check("inherited announce-in-flight gate still applies",
          h2d_gate_check_txq_addr(&live) == H2D_GATE_INFLIGHT, "");
    live = good_live();
    live.phase = HCC_PHASE_ACTIVE;
    check("ACTIVE is legal after the create (ordered provider ladder)",
          h2d_gate_check_txq_addr(&live) == H2D_GATE_OK, "");
    live = good_live();
    live.phase = HCC_PHASE_WAIT_DEVICE_READY;
    check("pre-ready phase refused",
          h2d_gate_check_txq_addr(&live) == H2D_GATE_PHASE, "");
    live = good_live();
    live.released = false;
    check("transport-release gate still precedes everything",
          h2d_gate_check_txq_addr(&live) == H2D_GATE_NOT_RELEASED, "");
    check("new gate names",
          strcmp(h2d_gate_name(H2D_GATE_NEED_CTX),
                 "ctx_create_required") == 0 &&
          strcmp(h2d_gate_name(H2D_GATE_DUP_TXQ_ADDR),
                 "txq_addr_already_sent") == 0, "");
}

int main(void)
{
    printf("wifi-dp-air merged-tree selftest (host, device-free)\n");
    test_merge_shape();
    test_geometry();
    test_indices();
    test_msdu_table();
    test_mac_program();
    test_txq_addr_frame();
    test_gates();
    printf("wifi-dp-air merged-tree selftest: %d/%d checks PASS\n",
           checks_total - checks_fail, checks_total);
    return checks_fail ? 1 : 0;
}
