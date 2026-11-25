// SPDX-License-Identifier: GPL-2.0
/*
 * rx_cmp_phase_selftest.c - host test for the portable completion-ring
 * consumer/phase model (lane wifi-rx-completion-phase, task-168).
 *
 * Builds with a plain host C compiler; no kernel, no device, no vendor
 * artifact.  Run: ./tools/run-host-tests.sh
 */
#include <stdio.h>
#include <string.h>

#include "../src/hi5622_rx_cmp_phase.h"

static int checks;
static int fails;

static void ck(int cond, const char *what)
{
    checks++;
    if (!cond) {
        fails++;
        printf("FAIL %3d: %s\n", checks, what);
    }
}

static void ck_u32(rxc_u32 got, rxc_u32 want, const char *what)
{
    checks++;
    if (got != want) {
        fails++;
        printf("FAIL %3d: %s (got 0x%08x want 0x%08x)\n",
               checks, what, got, want);
    }
}

static void ck_i32(int got, int want, const char *what)
{
    checks++;
    if (got != want) {
        fails++;
        printf("FAIL %3d: %s (got %d want %d)\n", checks, what, got, want);
    }
}

static void test_pack_unpack(void)
{
    rxc_u32 idx = 0xdeadbeefu, phase = 0xdeadbeefu;

    ck_u32(rxc_pack(0u, 0u), 0x0000u, "pack(0,0)");
    ck_u32(rxc_pack(0u, 1u), 0x8000u, "pack(0,1)");
    ck_u32(rxc_pack(1u, 0u), 0x0001u, "pack(1,0)");
    ck_u32(rxc_pack(0x7fffu, 1u), 0xffffu, "pack(0x7fff,1)");
    ck_u32(rxc_pack(0x1fffu, 1u), 0x9fffu, "pack masks index [14:0]");
    ck_u32(rxc_pack(0x5555u, 2u), 0x5555u, "pack phase & 1");
    ck_u32(rxc_pack(0x5555u, 3u), 0xd555u, "pack phase odd & 1");

    rxc_unpack(0x8000u, &idx, &phase);
    ck_u32(idx, 0u, "unpack idx of 0x8000");
    ck_u32(phase, 1u, "unpack phase of 0x8000");
    rxc_unpack(0x1fffu, &idx, &phase);
    ck_u32(idx, 0x1fffu, "unpack idx of 0x1fff");
    ck_u32(phase, 0u, "unpack phase of 0x1fff");
    rxc_unpack(0x9fffu, &idx, &phase);
    ck_u32(idx, 0x1fffu, "unpack idx of 0x9fff");
    ck_u32(phase, 1u, "unpack phase of 0x9fff");
    rxc_unpack(0x1234u, NULL, NULL);   /* no crash */
    checks++;
}

static void test_next(void)
{
    ck_u32(rxc_next(0x0000u, 4095u), 0x0001u, "next(0)->1");
    ck_u32(rxc_next(0x0001u, 4095u), 0x0002u, "next(1)->2");
    ck_u32(rxc_next(4093u, 4095u), 4094u, "next(4093)->4094");
    ck_u32(rxc_next(4094u, 4095u), 0x8000u, "next(4094) wraps to idx0 phase1");
    ck_u32(rxc_next(0x8000u, 4095u), 0x8001u, "next(0x8000)->0x8001");
    ck_u32(rxc_next(0x8000u | 4094u, 4095u), 0x0000u,
           "next(phase1 idx4094) wraps to phase0 idx0");
    ck_u32(rxc_next(0x0000u, 0u), 0x0000u, "next refuses depth 0");
    ck_u32(rxc_next(0x0000u, 0x8001u), 0x0000u, "next refuses depth > idx mask");
}

static void test_advance(void)
{
    ck_u32(rxc_advance(0x0000u, 0u, 4095u), 0x0000u, "advance 0");
    ck_u32(rxc_advance(0x0000u, 1u, 4095u), 0x0001u, "advance 1");
    ck_u32(rxc_advance(0x0000u, 4094u, 4095u), 4094u, "advance 4094");
    ck_u32(rxc_advance(0x0000u, 4095u, 4095u), 0x8000u,
           "advance one full lap toggles phase");
    ck_u32(rxc_advance(0x8000u, 4095u, 4095u), 0x0000u,
           "advance second lap toggles back");
    ck_u32(rxc_advance(0x0000u, 5000u, 4095u), 0x8000u,
           "advance clamps to one lap");
    ck_u32(rxc_advance(0x0000u, 1u, 0u), 0x0000u, "advance refuses depth 0");
}

static void test_pending(void)
{
    rxc_u32 n = 0xdeadbeefu;

    ck_i32(rxc_pending(0x0000u, 0x0000u, 4095u, &n), RXC_OK, "pending empty rc");
    ck_u32(n, 0u, "pending empty count");
    ck_i32(rxc_pending(0x0001u, 0x0000u, 4095u, &n), RXC_OK, "pending 1 rc");
    ck_u32(n, 1u, "pending 1 count");
    ck_i32(rxc_pending(0x0020u, 0x0000u, 4095u, &n), RXC_OK, "pending 32 rc");
    ck_u32(n, 32u, "pending 32 count");
    /* vendor stock idle: free rings keep 0x8000 published and the device
     * consumer is 0x20; the completion ring is the (0,0) case above. */
    ck_i32(rxc_pending(0x8000u, 0x0000u, 4095u, &n), RXC_OK,
           "pending full-lap rc");
    ck_u32(n, 4095u, "pending equal index opposite phase = depth");
    /* producer wrapped past the consumer: idx2 phase1 vs idx4090 phase0 */
    ck_i32(rxc_pending(0x8002u, 0x0ffau, 4095u, &n), RXC_OK,
           "pending wrap rc");
    ck_u32(n, 7u, "pending wrap count (2 + 4095 - 4090)");
    /* equal index and same phase is empty, never full */
    ck_i32(rxc_pending(0x8000u, 0x8000u, 4095u, &n), RXC_OK,
           "pending phase1 empty rc");
    ck_u32(n, 0u, "pending phase1 equal = empty");
    /* consumer ahead of producer is the over-consumed state: refuse */
    ck_i32(rxc_pending(0x0001u, 0x8000u, 4095u, &n), RXC_ERANGE,
           "pending consumer ahead refused");
    ck_i32(rxc_pending(0x0000u, 0x0001u, 4095u, &n), RXC_ERANGE,
           "pending consumer ahead (same phase) refused");
    /* bounds */
    ck_i32(rxc_pending(4095u, 0u, 4095u, &n), RXC_ERANGE,
           "pending producer idx == depth refused");
    ck_i32(rxc_pending(0u, 4095u, 4095u, &n), RXC_ERANGE,
           "pending consumer idx == depth refused");
    ck_i32(rxc_pending(0u, 0u, 0u, &n), RXC_EARG, "pending depth 0 refused");
    ck_i32(rxc_pending(0u, 0u, 0x8000u, &n), RXC_EARG,
           "pending depth > idx mask refused");
    ck_i32(rxc_pending(0u, 0u, 4095u, NULL), RXC_EARG,
           "pending NULL refused");
}

static void test_ring_state(void)
{
    rxc_u32 n = 0u, full = 0u;

    ck_i32(rxc_ring_state(0x0000u, 0x0000u, 4095u, &n, &full), RXC_OK,
           "state empty rc");
    ck_u32(full, 0u, "state empty not full");
    ck_i32(rxc_ring_state(0x0000u, 0x8000u, 4095u, &n, &full), RXC_OK,
           "state phase1 rc");
    ck_u32(n, 4095u, "state phase1 pending depth");
    ck_u32(full, 1u, "state phase1 reported full-lap");
    ck_i32(rxc_ring_state(0x8000u, 0x8000u, 4095u, &n, &full), RXC_OK,
           "state lap-equal rc");
    ck_u32(full, 0u, "state lap-equal not full");
    ck_i32(rxc_ring_state(0x0000u, 0x0000u, 0u, &n, &full), RXC_EARG,
           "state bad depth refused");
}

static void test_gate(void)
{
    /* param=1, rx=1, ready=1, armed=0, done=0, failed=0, rptr=0 -> allow */
    ck_i32((int)rxc_prepublish_gate(1u, 1u, 1u, 0u, 0u, 0u, 0u),
            (int)RXC_GATE_ALLOW, "gate allow");
    ck_i32((int)rxc_prepublish_gate(0u, 1u, 1u, 0u, 0u, 0u, 0u),
            (int)RXC_GATE_DISABLED, "gate disabled param");
    ck_i32((int)rxc_prepublish_gate(1u, 0u, 1u, 0u, 0u, 0u, 0u),
            (int)RXC_GATE_NO_RX_ENABLE, "gate no rx_enable");
    ck_i32((int)rxc_prepublish_gate(1u, 1u, 0u, 0u, 0u, 0u, 0u),
            (int)RXC_GATE_NOT_READY, "gate not ready");
    ck_i32((int)rxc_prepublish_gate(1u, 1u, 1u, 1u, 0u, 0u, 0u),
            (int)RXC_GATE_ARMED, "gate after arm");
    ck_i32((int)rxc_prepublish_gate(1u, 1u, 1u, 0u, 1u, 0u, 0u),
            (int)RXC_GATE_ALREADY_DONE, "gate one-shot");
    ck_i32((int)rxc_prepublish_gate(1u, 1u, 1u, 0u, 0u, 1u, 0u),
            (int)RXC_GATE_LANE_FAILED, "gate lane failed");
    ck_i32((int)rxc_prepublish_gate(1u, 1u, 1u, 0u, 0u, 0u, 0x0001u),
            (int)RXC_GATE_CONSUMER_DIRTY, "gate dirty consumer");
    ck_i32((int)rxc_prepublish_gate(1u, 1u, 1u, 0u, 0u, 0u, 0x0000u),
            (int)RXC_GATE_ALLOW, "gate vendor stock consumer");
    ck_i32((int)rxc_prepublish_gate(1u, 1u, 1u, 0u, 0u, 0u, 0xffff0000u),
            (int)RXC_GATE_ALLOW, "gate upper half ignored");
    ck_i32((int)rxc_prepublish_gate(1u, 1u, 1u, 0u, 0u, 1u, 0x0001u),
            (int)RXC_GATE_LANE_FAILED, "gate precedence armed/failed");
}

static void test_store(void)
{
    ck_u32(rxc_store_value(0x00000000u, 0x8000u), 0x00008000u,
           "store from vendor stock");
    ck_u32(rxc_store_value(0xffff0000u, 0x8000u), 0xffff8000u,
           "store preserves upper half");
    ck_u32(rxc_store_value(0x12345678u, 0x0001u), 0x12340001u,
           "store replaces low half only");
    ck_u32(rxc_store_value(0x00000000u, 0x8001u), 0x00008001u,
           "store phase1 idx1");
    ck_i32(rxc_readback_ok(0x00008000u, 0x8000u), 1, "readback ok");
    ck_i32(rxc_readback_ok(0xffff8000u, 0x8000u), 1,
           "readback ok ignores upper half");
    ck_i32(rxc_readback_ok(0x00000000u, 0x8000u), 0, "readback mismatch");
    ck_i32(rxc_readback_ok(0x00008001u, 0x8000u), 0, "readback idx mismatch");
}

static void test_status(void)
{
    ck_u32(rxc_status_reaction(0x00000182u, 0x00000582u), 0x00000400u,
           "status reaction: bit10 added (bench 0x182 -> 0x582)");
    ck_u32(rxc_status_reaction(0x00000182u, 0x00000182u), 0x00000000u,
           "status reaction: none");
    ck_u32(rxc_status_reaction(0x00000102u, 0x00000182u), 0x00000080u,
           "status reaction: bit7 added (0x102 -> 0x182)");
    ck_u32(rxc_status_reaction(0x00000000u, 0x00000480u), 0x00000480u,
           "status reaction: both watched bits");
    ck_u32(rxc_status_reaction(0x00000582u, 0x00000582u), 0x00000000u,
           "status reaction: pre-existing bit10 is not a reaction");
    ck_u32(rxc_status_reaction(0x00000582u, 0x00000182u), 0x00000000u,
           "status reaction: bit10 clearing is not a reaction");
    ck_u32(rxc_status_reaction(0x00000000u, 0x00000020u), 0x00000000u,
           "status reaction: unwatched bits ignored");
}

static void test_phase1_prediction(void)
{
    ck_i32(rxc_phase1_looks_full(0x0000u), 1,
           "phase1 prepublish vs idle device producer 0 looks full");
    ck_i32(rxc_phase1_looks_full(0x8000u), 0,
           "phase1 prepublish vs producer phase1 idx0 looks empty");
    ck_i32(rxc_phase1_looks_full(0x8001u), 0,
           "phase1 prepublish vs producer phase1 idx1 looks normal");
    ck_i32(rxc_phase1_looks_full(0x0001u), 1,
           "phase1 prepublish vs producer phase0 idx1 (consumer ahead) refused");
}

static void test_vendor_semantics(void)
{
    rxc_u32 cons = RXC_VENDOR_INITIAL_CONSUMER;
    rxc_u32 prod = 0x0000u;
    rxc_u32 n = 0u;

    /* The vendor-exact constants: the free-ring full publish and the phase-1
     * experiment value are the same packed word, but the completion consumer
     * starts at 0 - the two must never be conflated. */
    ck_u32(RXC_VENDOR_INITIAL_CONSUMER, 0x0000u, "vendor initial consumer 0");
    ck_u32(RXC_PHASE1_PREPUBLISH, 0x8000u, "phase1 experiment value 0x8000");
    ck_u32(RXC_FREE_RING_FULL_PUBLISH, RXC_PHASE1_PREPUBLISH,
           "free-ring full publish equals phase1 word");
    ck(RXC_VENDOR_INITIAL_CONSUMER != RXC_PHASE1_PREPUBLISH,
       "initial consumer is NOT the phase1 word");
    ck_u32(rxc_pack(0u, 0u), RXC_VENDOR_INITIAL_CONSUMER,
           "packed initial consumer = vendor stock");
    ck_i32(rxc_ring_state(prod, cons, RXC_CMP_DEPTH, &n, NULL), RXC_OK,
           "vendor init state valid");
    ck_u32(n, 0u, "vendor init ring empty");

    /* First completion, vendor-observed path: the device writes entry 0 and
     * its producer advances to idx1 phase0; the host consumes 1 and publishes
     * 0x0001 (hal_ring_set_sw2hw type 2).  No phase-1 value is ever written. */
    prod = rxc_next(prod, RXC_CMP_DEPTH);
    ck_u32(prod, 0x0001u, "first completion advances producer to 1");
    ck_i32(rxc_pending(prod, cons, RXC_CMP_DEPTH, &n), RXC_OK,
           "first completion pending rc");
    ck_u32(n, 1u, "first completion pending 1");
    cons = rxc_advance(cons, 1u, RXC_CMP_DEPTH);
    ck_u32(cons, 0x0001u, "first consume publishes 0x0001");
    ck_u32(rxc_store_value(0u, cons), 0x0001u,
           "first consumer store value 0x0001");

    /* Full-lap consume: after 4095 consumed entries the published consumer is
     * 0x8000 (idx 0, phase 1) - the ONLY way the phase bit becomes 1. */
    cons = rxc_advance(0x0000u, RXC_CMP_DEPTH, RXC_CMP_DEPTH);
    ck_u32(cons, 0x8000u, "consumer phase becomes 1 only after a full lap");
    prod = rxc_advance(0x0000u, RXC_CMP_DEPTH, RXC_CMP_DEPTH);
    ck_u32(prod, 0x8000u, "producer phase becomes 1 only after a full lap");
    ck_i32(rxc_pending(prod, cons, RXC_CMP_DEPTH, &n), RXC_OK,
           "lap-equal pending rc");
    ck_u32(n, 0u, "lap-equal pending 0");
}

int main(void)
{
    test_pack_unpack();
    test_next();
    test_advance();
    test_pending();
    test_ring_state();
    test_gate();
    test_store();
    test_status();
    test_phase1_prediction();
    test_vendor_semantics();

    printf("rx_cmp_phase selftest: %d checks, %d failure(s)\n", checks, fails);
    if (fails != 0) {
        printf("RESULT: FAIL\n");
        return 1;
    }
    printf("RESULT: PASS\n");
    return 0;
}
