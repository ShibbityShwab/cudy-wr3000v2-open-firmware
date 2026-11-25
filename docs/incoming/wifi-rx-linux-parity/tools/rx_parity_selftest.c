/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rx_parity_selftest.c - host-only checks for hi5622_rx_parity.c
 * (lane wifi-rx-linux-parity, shared task task-166).
 *
 * Strict C99, no kernel, no device, no vendor data.  Build/run:
 *   cc -std=c99 -O2 -Wall -Wextra -Werror -pedantic \
 *      -Isrc tools/rx_parity_selftest.c src/hi5622_rx_parity.c \
 *      src/hi5622_rx_live.c -o /tmp/rx_parity_selftest && /tmp/rx_parity_selftest
 *
 * Every check prints PASS/FAIL and the summary line is
 *   "<n> checks, <m> failures"
 * so tools/build.sh can gate on it (device-free, vendor-data-free).
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_rx_parity.h"
#include "hi5622_rx_live.h"

static int checks;
static int failures;

#define CHECK(cond, what) do {                                           \
        checks++;                                                        \
        if (cond) {                                                      \
            printf("PASS: %s\n", (what));                                \
        } else {                                                         \
            failures++;                                                  \
            printf("FAIL: %s (line %d)\n", (what), __LINE__);            \
        }                                                                \
    } while (0)

static void group_publish(void)
{
    rxp_u32 v = 0xdeadbeefu;

    printf("-- group 1: exact-width packed publication --\n");
    CHECK(rxp_publish_value(0u, 0u, 256u, &v) == 0 && v == 0x0000u,
          "pack(index 0, phase 0) = 0x0000");
    CHECK(rxp_publish_value(0u, 1u, 256u, &v) == 0 && v == 0x8000u,
          "pack(index 0, phase 1) = 0x8000 (the dp_setup full-ring publish)");
    CHECK(rxp_publish_value(255u, 0u, 256u, &v) == 0 && v == 0x00ffu,
          "pack(index 255, phase 0, depth 256) = 0x00ff");
    CHECK(rxp_publish_value(255u, 1u, 256u, &v) == 0 && v == 0x80ffu,
          "pack(index 255, phase 1, depth 256) = 0x80ff");
    CHECK(rxp_publish_value(32u, 1u, 256u, &v) == 0 && v == 0x8020u,
          "pack(index 32, phase 1, depth 256) = 0x8020");
    CHECK(rxp_publish_value(256u, 0u, 256u, &v) != 0,
          "refuse index == depth");
    CHECK(rxp_publish_value(0u, 2u, 256u, &v) != 0,
          "refuse phase 2");
    CHECK(rxp_publish_value(0u, 0u, 0u, &v) != 0, "refuse depth 0");
    CHECK(rxp_publish_value(0u, 0u, 0x8000u, &v) != 0,
          "refuse depth above the 15-bit index field");
    CHECK(rxp_publish_value(0u, 0u, 256u, NULL) != 0, "refuse NULL out");
    CHECK(v <= RXP_PTR_MAX, "every accepted value fits the 16-bit register");
}

static void group_w1c(void)
{
    rxp_u32 v = 0u;
    rxp_u32 status = 0x00000182u; /* bench RX-arm live status [L] */
    rxp_u32 mask = 0xfffffcc1u;   /* vendor host-MAC int mask [V*]  */

    printf("-- group 2: served-bits-only W1C --\n");
    CHECK(rxp_w1c_served(status, mask, &v) == 0 && v == 0x00000102u,
          "served(status 0x182, mask 0xfffffcc1) = 0x102");
    CHECK((v & mask) == 0u, "served value contains no masked-off bit");
    CHECK((v & status) == v, "served value is a subset of the status word");
    CHECK(rxp_w1c_served(0u, mask, &v) == 0 && v == 0u,
          "served(0, mask) = 0");
    CHECK(rxp_w1c_served(0xffffffffu, 0u, &v) == 0 && v == 0xffffffffu,
          "mask 0 serves every bit");
    CHECK(rxp_w1c_served(1u, 1u, &v) == 0 && v == 0u,
          "masked bit is never acknowledged");
    CHECK(rxp_w1c_served(1u, 0xfffffffeu, &v) == 0 && v == 1u,
          "unmasked bit is acknowledged");
    CHECK(rxp_w1c_served(1u, 1u, NULL) != 0, "refuse NULL out");
}

static void group_commit(void)
{
    int settled = -1;
    int retry = -1;

    printf("-- group 3: completion-entry commit decision --\n");
    CHECK(rxp_commit_settled(0u, 0u, &settled) == 0 && settled == 0,
          "zero/zero is NOT settled (spurious producer advance)");
    CHECK(rxp_commit_settled(0u, 0x844d0004u, &settled) == 0 && settled == 1,
          "zero then non-zero is settled (committed between reads)");
    CHECK(rxp_commit_settled(0x844d0004u, 0x844d0004u, &settled) == 0 &&
          settled == 1, "equal non-zero reads are settled");
    CHECK(rxp_commit_settled(0x844d0004u, 0x844d0008u, &settled) == 0 &&
          settled == 0, "changing non-zero reads are NOT settled");
    CHECK(rxp_commit_settled(0x844d0004u, 0u, &settled) == 0 && settled == 0,
          "non-zero then zero is NOT settled");
    CHECK(rxp_commit_settled(0u, 0u, NULL) != 0, "refuse NULL settled");

    CHECK(rxp_commit_retry(0u, 0u, 32u, &retry) == 0 && retry == 1,
          "zero entry with budget left: retry");
    CHECK(rxp_commit_retry(0u, 31u, 32u, &retry) == 0 && retry == 1,
          "retry at the last budget slot");
    CHECK(rxp_commit_retry(0u, 32u, 32u, &retry) == 0 && retry == 0,
          "stop retrying at the bounded wait budget");
    CHECK(rxp_commit_retry(0x844d0004u, 0u, 32u, &retry) == 0 && retry == 0,
          "a non-zero entry needs no retry");
    CHECK(rxp_commit_retry(0u, 0u, 0u, &retry) == 0 && retry == 0,
          "rx_entry_wait=0 reproduces the old single-read behaviour");
    CHECK(rxp_commit_retry(0u, 0u, 32u, NULL) != 0, "refuse NULL retry");
}

static void group_spurious(void)
{
    int close_lane = -1;

    printf("-- group 4: spurious-advance budget --\n");
    CHECK(rxp_spurious_limit(0u, RXP_SPURIOUS_MAX, &close_lane) == 0 &&
          close_lane == 0, "no spurious yet: lane stays open");
    CHECK(rxp_spurious_limit(RXP_SPURIOUS_MAX, RXP_SPURIOUS_MAX,
                             &close_lane) == 0 && close_lane == 0,
          "at the cap: lane stays open (a transient is not corruption)");
    CHECK(rxp_spurious_limit(RXP_SPURIOUS_MAX + 1u, RXP_SPURIOUS_MAX,
                             &close_lane) == 0 && close_lane == 1,
          "above the cap: fail closed");
    CHECK(rxp_spurious_limit(0u, 0u, &close_lane) != 0,
          "refuse a zero spurious cap");
    CHECK(rxp_spurious_limit(0u, 1u, NULL) != 0, "refuse NULL close_lane");
}

static void group_refill(void)
{
    rxp_u32 count = 0xdeadbeefu;
    rxp_u32 prod = 0xdeadbeefu;

    printf("-- group 5: bounded refill window --\n");
    CHECK(rxp_refill_plan(0x20u, 0x20u, 0x8000u, 256u, 100u, 64u,
                          &count, &prod) == 0 &&
          count == 0u && prod == 0x8000u,
          "no consumer movement: no refill, producer unchanged");
    CHECK(rxp_refill_plan(0x20u, 0x40u, 0x8000u, 256u, 100u, 64u,
                          &count, &prod) == 0 &&
          count == 32u && prod == 0x8020u,
          "32 consumed, 100 queued, cap 64 -> refill 32, producer 0x8020");
    CHECK(rxp_refill_plan(0x20u, 0x40u, 0x8000u, 256u, 100u, 16u,
                          &count, &prod) == 0 &&
          count == 16u && prod == 0x8010u,
          "cap 16 bounds the refill and the producer");
    CHECK(rxp_refill_plan(0x20u, 0x40u, 0x8000u, 256u, 5u, 64u,
                          &count, &prod) == 0 &&
          count == 5u && prod == 0x8005u,
          "only 5 returned buffers queued -> refill 5");
    CHECK(rxp_refill_plan(0x20u, 0x20u, 0x8020u, 256u, 100u, 64u,
                          &count, &prod) == 0 &&
          count == 0u && prod == 0x8020u,
          "full ring (posted == depth): room 0, no refill");
    CHECK(rxp_refill_plan(0x20u, 0x40u, 0x8000u, 256u, 300u, 64u,
                          &count, &prod) != 0,
          "refuse queued > depth");
    CHECK(rxp_refill_plan(0x20u, 0x40u, 0x8000u, 256u, 100u, 300u,
                          &count, &prod) != 0,
          "refuse cap > depth");
    CHECK(rxp_refill_plan(0x20u, 0x40u, 0x8000u, 0u, 100u, 64u,
                          &count, &prod) != 0, "refuse depth 0");
    CHECK(rxp_refill_plan(0x20u, 0x40u, 0x8000u, 256u, 100u, 64u,
                          &count, NULL) != 0, "refuse NULL new_producer");
    CHECK(rxp_refill_plan(0x20u, 0x40u, 0x8000u, 256u, 0u, 64u,
                          &count, &prod) == 0 && count == 0u && prod == 0x8000u,
          "no returned buffers queued is a no-op");
    CHECK(rxp_refill_plan(0x20u, 0x40u, 0x8200u, 256u, 0u, 64u,
                          &count, &prod) != 0,
          "refuse a producer index outside the depth");
    CHECK(rxp_refill_plan(0xffu, 0x20u, 0x8000u, 256u, 64u, 64u,
                          &count, &prod) == 0 && count == 32u && prod == 0x8020u,
          "index wrap (255 -> 32): 33 consumed but the posted room caps it at 32");
    CHECK(rxp_refill_plan(0x40u, 0x8040u, 0x8040u, 256u, 64u, 64u,
                          &count, &prod) == 0 && count == 64u && prod == 0x8080u,
          "one-lap phase wrap gives a depth-sized window, bounded by cap");
}

static void group_ctl(void)
{
    rxp_u32 v = 0xdeadbeefu;

    printf("-- group 6: opt-in RX-control candidate word --\n");
    CHECK(rxp_ctl_offset_allowed(0x08u) == 1, "candidate offset 0x08 allowed");
    CHECK(rxp_ctl_offset_allowed(0x0cu) == 1, "candidate offset 0x0c allowed");
    CHECK(rxp_ctl_offset_allowed(0x14u) == 1, "candidate offset 0x14 allowed");
    CHECK(rxp_ctl_offset_allowed(0x18u) == 1, "candidate offset 0x18 allowed");
    CHECK(rxp_ctl_offset_allowed(0x00u) == 0,
          "ring-program word 0x00 refused");
    CHECK(rxp_ctl_offset_allowed(0x10u) == 0,
          "msdu ring table word 0x10 refused");
    CHECK(rxp_ctl_offset_allowed(0x1cu) == 0,
          "RX buffer length word 0x1c refused");
    CHECK(rxp_ctl_offset_allowed(0x38u) == 0,
          "free-ring producer word 0x38 refused");
    CHECK(rxp_ctl_offset_allowed(0x44u) == 0,
          "interrupt status/clear word 0x44 refused");
    CHECK(rxp_ctl_offset_allowed(0x48u) == 0,
          "interrupt mask word 0x48 refused");
    CHECK(rxp_ctl_offset_allowed(0x5cu) == 0,
          "vendor-live +0x5c refused (covered by mac_tail_writes)");
    CHECK(rxp_ctl_offset_allowed(0x6cu) == 0,
          "vendor-live +0x6c refused (covered by mac_tail_writes)");
    CHECK(rxp_ctl_offset_allowed(0x06u) == 0, "unaligned offset refused");
    CHECK(rxp_ctl_offset_allowed(0xb0u) == 0, "outside the 0xb0 span refused");
    CHECK(rxp_ctl_offset_allowed(0x58u) == 0,
          "unlisted but in-span 0x58 refused (not a candidate)");

    CHECK(rxp_ctl_value(0x08u, 0u, 0x4u, 0u, &v) == 0 && v == 0x4u,
          "RMW set on a zero word");
    CHECK(rxp_ctl_value(0x08u, 0x300u, 0u, 0x100u, &v) == 0 && v == 0x200u,
          "RMW clear preserves the other bits");
    CHECK(rxp_ctl_value(0x14u, 0x300u, 0x1u, 0x100u, &v) == 0 && v == 0x201u,
          "RMW set+clear compose");
    CHECK(rxp_ctl_value(0x08u, 0u, 0x1u, 0x1u, &v) != 0,
          "refuse a bit both set and cleared");
    CHECK(rxp_ctl_value(0x08u, 0u, 0u, 0u, &v) != 0,
          "refuse an empty set/clr request");
    CHECK(rxp_ctl_value(0x38u, 0u, 0x1u, 0u, &v) != 0,
          "refuse a non-candidate offset");
    CHECK(rxp_ctl_value(0x08u, 0u, 0x1u, 0u, NULL) != 0, "refuse NULL out");
}

static void group_stall(void)
{
    int cls = -1;
    rxp_u32 st = 0x182u;

    printf("-- group 7: stall classification --\n");
    CHECK(rxp_stall_class(st, 0x20u, 0x20u, 0u, st, 0x20u, 0x20u, 0u, 0u,
                          &cls) == 0 && cls == RXP_STALL_IDLE,
          "no movement -> IDLE");
    CHECK(rxp_stall_class(st, 0x20u, 0x20u, 0u, st, 0x40u, 0x20u, 0u, 0u,
                          &cls) == 0 && cls == RXP_STALL_PREFETCH_ONLY,
          "free-ring rptr moved, completion wptr did not -> PREFETCH_ONLY");
    CHECK(rxp_stall_class(st, 0x20u, 0x20u, 0u, st, 0x40u, 0x40u, 1u, 0u,
                          &cls) == 0 && cls == RXP_STALL_PROGRESS,
          "completion production is the strongest progress signal");
    CHECK(rxp_stall_class(st, 0x20u, 0x20u, 0u, st, 0x20u, 0x20u, 0u, 2u,
                          &cls) == 0 && cls == RXP_STALL_SPURIOUS,
          "spurious producer visible with no movement -> SPURIOUS_WPTR");
    CHECK(rxp_stall_class(0xffffffffu, 0x20u, 0x20u, 0u, st, 0x20u, 0x20u, 0u,
                          0u, &cls) == 0 && cls == RXP_STALL_ERROR,
          "a transport miss -> PROBE_ERROR");
    CHECK(strcmp(rxp_stall_name(RXP_STALL_IDLE), "IDLE") == 0 &&
          strcmp(rxp_stall_name(RXP_STALL_PREFETCH_ONLY), "PREFETCH_ONLY") == 0 &&
          strcmp(rxp_stall_name(RXP_STALL_PROGRESS), "PROGRESS") == 0 &&
          strcmp(rxp_stall_name(RXP_STALL_SPURIOUS), "SPURIOUS_WPTR") == 0 &&
          strcmp(rxp_stall_name(RXP_STALL_ERROR), "PROBE_ERROR") == 0,
          "stall names are stable");
}

static void group_probe(void)
{
    int stock_nonzero = -1;
    int matches = -1;
    static const rxp_u32 prog[] = {
        0x00u, 0x04u, 0x10u, 0x1cu, 0x20u, 0x24u, 0x28u, 0x2cu,
        0x30u, 0x34u, 0x38u, 0x3cu, 0x40u, 0x48u
    };
    unsigned int i;

    printf("-- group 8: parity-probe classification --\n");
    for (i = 0; i < sizeof(prog) / sizeof(prog[0]); i++)
        CHECK(rxp_is_program_off(prog[i]) == 1,
              "the 14-write program word is recognised");
    CHECK(rxp_is_program_off(0x08u) == 0 && rxp_is_program_off(0x0cu) == 0 &&
          rxp_is_program_off(0x14u) == 0 && rxp_is_program_off(0x18u) == 0,
          "the four candidate words are NOT in the 14-write program");
    CHECK(rxp_is_program_off(0x5cu) == 0 && rxp_is_program_off(0x6cu) == 0,
          "the vendor-live +0x5c/+0x6c are NOT in the 14-write program");
    CHECK(rxp_mac_cfg_stock(0x1cu) == 0x00b0063cu,
          "stock +0x1c = 0x00b0063c");
    CHECK(rxp_mac_cfg_stock(0x48u) == 0xfffffcc1u,
          "stock +0x48 = 0xfffffcc1");
    CHECK(rxp_mac_cfg_stock(0x5cu) == 4u && rxp_mac_cfg_stock(0x6cu) == 4u,
          "stock +0x5c/+0x6c = 4 (no identified writer)");
    CHECK(rxp_mac_cfg_stock(0x08u) == 0u && rxp_mac_cfg_stock(0x18u) == 0u,
          "stock 0x08/0x18 = 0 (candidate control words)");
    CHECK(rxp_mac_cfg_stock(0xadu) == 0u, "unknown offset classifies as 0");
    CHECK(rxp_mac_cfg_class(0x1cu, 0x00b0063cu, &stock_nonzero, &matches) == 1 &&
          stock_nonzero == 1 && matches == 1,
          "programmed word matching stock -> class 1");
    CHECK(rxp_mac_cfg_class(0x1cu, 0x0000063cu, &stock_nonzero, &matches) == 2 &&
          matches == 0, "programmed word differing from stock -> class 2");
    CHECK(rxp_mac_cfg_class(0x5cu, 0u, &stock_nonzero, &matches) == 3 &&
          stock_nonzero == 1,
          "unprogrammed vendor-nonzero word -> class 3 (missing-write candidate)");
    CHECK(rxp_mac_cfg_class(0x08u, 0u, &stock_nonzero, &matches) == 4 &&
          stock_nonzero == 0,
          "unprogrammed stock-zero word -> class 4 (candidate control word)");
    CHECK(rxp_mac_cfg_class(0x0feu, 0u, &stock_nonzero, &matches) == 0,
          "offset outside the 0xb0 span -> class 0");
    CHECK(rxp_mac_cfg_class(0x06u, 0u, &stock_nonzero, &matches) == 0,
          "unaligned offset -> class 0");
}

static void group_steps(void)
{
    const struct rxp_step_info *info;

    printf("-- group 9: step catalogue --\n");
    info = rxp_step_info((enum rxp_step)0);
    CHECK(info != NULL && strcmp(info->name, "ring_alloc") == 0,
          "step 0 is ring_alloc");
    info = rxp_step_info(RXP_STEP_DMA_ENABLE);
    CHECK(info != NULL && strcmp(info->name, "dma_enable") == 0,
          "DMA-enable step is named and present");
    CHECK(info != NULL && strncmp(info->our_state, "MISSING", 7) == 0,
          "DMA-enable step is honestly marked MISSING/UNKNOWN");
    info = rxp_step_info(RXP_STEP_RX_FILTER);
    CHECK(info != NULL && strncmp(info->our_state, "MISSING", 7) == 0,
          "RX-filter step is honestly marked MISSING/UNKNOWN");
    info = rxp_step_info(RXP_STEP_STALL_WATCH);
    CHECK(info != NULL && strncmp(info->our_state, "missing", 7) == 0,
          "stall-watch step is honestly marked missing");
    CHECK(rxp_step_info(RXP_STEP_COUNT) == NULL, "out-of-range step is NULL");
    CHECK(strcmp(rxp_step_name(RXP_STEP_PRODUCER_PUBLISH),
                 "producer_publish") == 0,
          "producer_publish name");
    CHECK(strcmp(rxp_step_name((enum rxp_step)RXP_STEP_COUNT), "invalid") == 0,
          "invalid step name is 'invalid'");
    CHECK(RXP_STEP_COUNT == 13, "the catalogue has 13 steps");
}

static void group_anchors(void)
{
    rxp_u32 v = 0u;

    printf("-- group 10: literal decode anchors --\n");
    CHECK(HIL_RX_CFG_WPTR_NORM == 0x38u &&
          HIL_RX_CFG_WPTR_SMALL == 0x3cu &&
          HIL_RX_CFG_CMP_RPTR == 0x40u &&
          HIL_RX_CFG_INT_STATUS == 0x44u &&
          HIL_RX_CFG_INT_MASK == 0x48u,
          "config-block pointer/status offsets unchanged");
    CHECK(HIL_RX_LIVE_STATUS == 0x00u && HIL_RX_LIVE_NORM_RPTR == 0x08u &&
          HIL_RX_LIVE_SMALL_RPTR == 0x0cu && HIL_RX_LIVE_CMP_WPTR == 0x10u,
          "live-bank offsets unchanged");
    CHECK(HIL_RX_NORM_DEPTH == 256u && HIL_RX_SMALL_DEPTH == 512u &&
          HIL_RX_CMP_DEPTH == 4095u, "ring depths unchanged");
    CHECK(rxp_publish_value(0u, 1u, HIL_RX_NORM_DEPTH, &v) == 0 &&
          v == HIL_RX_PHASE_BIT,
          "the dp_setup full-ring publish is the model's phase-bit value");
    CHECK(RXP_ENTRY_WAIT_DEF == 32u && RXP_SPURIOUS_MAX == 64u,
          "bounded-wait and spurious-budget constants are pinned");
}

int main(void)
{
    printf("rx_parity_selftest - WiFi rx Linux-parity model (task task-166)\n");
    group_publish();
    group_w1c();
    group_commit();
    group_spurious();
    group_refill();
    group_ctl();
    group_stall();
    group_probe();
    group_steps();
    group_anchors();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
