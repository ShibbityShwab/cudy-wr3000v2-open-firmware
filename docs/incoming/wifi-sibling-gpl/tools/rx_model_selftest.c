/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rx_model_selftest.c - host-only, strict-C99 test of the wifi-sibling-gpl lane's
 * proposed RX consumer refinements (PATCH-PROPOSAL.md P1/P3) and the association
 * ordering guard (P2), plus the sibling-derived ring constants used in TABLES.md.
 *
 * ORIGINAL clean-room code.  No vendor source/object code, no firmware byte, no
 * calibration value is reproduced.  The rules are derived facts from the public
 * sources cited in REPORT.md/TABLES.md ([V] markers) and are re-stated here as
 * ordinary portable C so the proposal can be host-tested without a kernel or a
 * bench.
 *
 * Build/run:
 *   gcc -std=c99 -Wall -Wextra -O2 -o /tmp/rx_model rx_model_selftest.c && /tmp/rx_model
 *
 * Exit code 0 = all checks passed.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t  s32;

/* ------------------------------------------------------------------ */
/* Sibling-derived constants (REPORT/TABLES; verified against raw     */
/* sources by tools/verify_quotes.py).                                */
/* ------------------------------------------------------------------ */
#define HL_IDX_MASK            0x7fffu   /* 15 index bits [V] */
#define HL_PHASE_BIT           0x8000u   /* bit 15 wrap/phase [V] */
#define HL_CMP_DEPTH           4095u     /* completion ring depth [V ours] */
#define HL_SMALL_DEPTH         512u      /* small free ring depth [V ours] */
#define HL_NORM_DEPTH          256u      /* normal free ring depth [V ours] */
#define HL_ENTRY_SIZE_OURS     4u        /* 32-bit devVA entry [V ours] */
#define HL_ENTRY_SIZE_HI1105   8u        /* 64-bit devVA entry [V HM] */
#define HL_HI1105_NORM_DEPTH   4095u     /* [V HM host_hal_dscr.h] */
#define HL_HI1105_SMALL_DEPTH  512u      /* [V HM host_hal_dscr.h] */
#define HL_HI1105_CMP_DEPTH    4095u     /* [V HM host_hal_dscr.h] */
#define HL_HI1105_RESET_MASK   0x30u     /* free-ring reset mask [V HM] */
#define HL_ENTRY_WAIT_MAX      256u      /* P1 budget cap (proposal) */
#define HL_SPURIOUS_CAP        64u       /* P1 hard cap (proposal) */

/* Error codes (same style as hi5622_rx_live.h). */
#define HL_OK        0
#define HL_EARG     (-1)
#define HL_ERANGE   (-2)
#define HL_EFORMAT  (-3)

/* P1 entry classification. */
#define HL_ENTRY_VALID     0u
#define HL_ENTRY_WAIT      1u
#define HL_ENTRY_SPURIOUS  2u
#define HL_ENTRY_BAD       3u

/* P3 stall classes. */
#define HL_CLASS_IDLE      0u
#define HL_CLASS_PREFETCH  1u
#define HL_CLASS_PROGRESS  2u
#define HL_CLASS_SPURIOUS  3u

/* ------------------------------------------------------------------ */
/* Portable ring arithmetic (same rules as hi5622_rx_live.c).          */
/* ------------------------------------------------------------------ */
static u32 hl_idx_advance(u32 packed, u32 depth)
{
    u32 idx = packed & HL_IDX_MASK;
    u32 phase = (packed & HL_PHASE_BIT) ? 1u : 0u;

    if (depth == 0u || idx >= depth)
        return packed;
    idx++;
    if (idx == depth) {
        idx = 0u;
        phase ^= 1u;
    }
    return idx | (phase ? HL_PHASE_BIT : 0u);
}

static int hl_ring_pending(u32 producer, u32 consumer, u32 depth, u32 *count)
{
    s32 d;

    if (count == NULL || depth == 0u || depth > HL_IDX_MASK)
        return HL_EARG;
    if ((producer & HL_IDX_MASK) >= depth || (consumer & HL_IDX_MASK) >= depth)
        return HL_ERANGE;
    d = (s32)(producer & HL_IDX_MASK) - (s32)(consumer & HL_IDX_MASK);
    if (((producer ^ consumer) & HL_PHASE_BIT) != 0u)
        d += (s32)depth;
    if (d < 0 || (u32)d > depth)
        return HL_EFORMAT;
    *count = (u32)d;
    return HL_OK;
}

static int hl_ring_consumed(u32 old_consumer, u32 new_consumer, u32 depth,
                            u32 *count)
{
    u32 oi, ni, d;

    if (count == NULL || depth == 0u || depth > HL_IDX_MASK)
        return HL_EARG;
    oi = old_consumer & HL_IDX_MASK;
    ni = new_consumer & HL_IDX_MASK;
    if (oi >= depth || ni >= depth)
        return HL_ERANGE;
    d = (ni >= oi) ? (ni - oi) : (depth - oi + ni);
    if (d == 0u && ((old_consumer ^ new_consumer) & HL_PHASE_BIT) != 0u)
        d = depth; /* exactly one lap between the snapshots */
    if (d > depth)
        return HL_EFORMAT;
    *count = d;
    return HL_OK;
}

/* P1: classify one completion entry against the two host RX pools. */
static int hl_entry_classify(u32 entry, u32 norm_lo, u32 norm_hi,
                             u32 small_lo, u32 small_hi, u8 *ring_kind,
                             u8 *state)
{
    if (ring_kind == NULL || state == NULL)
        return HL_EARG;
    if (entry == 0u) {
        *state = HL_ENTRY_WAIT; /* producer may be visible before the entry */
        return HL_OK;
    }
    if ((entry & 3u) != 0u) {
        *state = HL_ENTRY_BAD;
        return HL_OK;
    }
    if (norm_lo <= entry && entry <= norm_hi) {
        *ring_kind = 0u;
        *state = HL_ENTRY_VALID;
        return HL_OK;
    }
    if (small_lo <= entry && entry <= small_hi) {
        *ring_kind = 1u;
        *state = HL_ENTRY_VALID;
        return HL_OK;
    }
    *state = HL_ENTRY_BAD;
    return HL_OK;
}

/* P1: bounded wait policy.  The callback returns the current entry value; the
 * model reads it up to 'budget' extra times when it is zero.  Returns the
 * classification and the number of extra reads it took. */
typedef u32 (*hl_slot_reader)(void *ctx, u32 slot);

static u8 hl_wait_resolve(void *ctx, hl_slot_reader read_slot, u32 slot,
                          u32 norm_lo, u32 norm_hi, u32 small_lo, u32 small_hi,
                          u32 budget, u8 *ring_kind, u32 *waits)
{
    u32 i;
    u8 state = HL_ENTRY_WAIT;

    *waits = 0u;
    if (budget > HL_ENTRY_WAIT_MAX)
        budget = HL_ENTRY_WAIT_MAX;
    for (i = 0u; i <= budget; i++) {
        u32 entry = read_slot(ctx, slot);

        if (hl_entry_classify(entry, norm_lo, norm_hi, small_lo, small_hi,
                              ring_kind, &state) != HL_OK) {
            state = HL_ENTRY_BAD;
            break;
        }
        if (state != HL_ENTRY_WAIT) {
            *waits = i;
            return state;
        }
    }
    *waits = budget + 1u;
    return HL_ENTRY_SPURIOUS; /* never became non-zero within the budget */
}

/* P1: hard cap on consecutive spurious observations (a persistent desync must
 * still close the lane). */
static u8 hl_spurious_fatal(u32 total, u32 cap)
{
    return (total >= cap) ? 1u : 0u;
}

/* P1: clear-after-read of one consumed completion slot. */
static int hl_cmp_clear(u32 *ring, u32 depth, u32 packed)
{
    u32 idx;

    if (ring == NULL || depth == 0u || depth > HL_IDX_MASK)
        return HL_EARG;
    idx = packed & HL_IDX_MASK;
    if (idx >= depth)
        return HL_ERANGE;
    ring[idx] = 0u;
    return HL_OK;
}

/* P3: stall classification from the arm snapshot and the current snapshot. */
static int hl_stall_classify(u32 arm_cmp_wptr, u32 cmp_wptr,
                             u32 arm_norm_rptr, u32 norm_rptr,
                             u32 arm_small_rptr, u32 small_rptr,
                             u8 spurious, u8 *cls)
{
    if (cls == NULL)
        return HL_EARG;
    if (spurious) {
        *cls = HL_CLASS_SPURIOUS;
        return HL_OK;
    }
    if (cmp_wptr != arm_cmp_wptr) {
        *cls = HL_CLASS_PROGRESS;
        return HL_OK;
    }
    if (norm_rptr != arm_norm_rptr || small_rptr != arm_small_rptr) {
        *cls = HL_CLASS_PREFETCH;
        return HL_OK;
    }
    *cls = HL_CLASS_IDLE;
    return HL_OK;
}

/* P1: refill budget from the free-ring consumer delta and the number of
 * completed (host-owned) buffers.  Never exceeds the host-safe maximum. */
static int hl_refill_count(u32 live_rptr, u32 published_wptr, u32 depth,
                           u32 completed, u32 max_batch, u32 *count)
{
    u32 consumed = 0u, out;
    int rc;

    if (count == NULL)
        return HL_EARG;
    rc = hl_ring_consumed(published_wptr, live_rptr, depth, &consumed);
    if (rc != HL_OK)
        return rc;
    out = consumed;
    if (out > completed)
        out = completed;
    if (out > max_batch)
        out = max_batch;
    if (out > depth)
        out = depth;
    *count = out;
    return HL_OK;
}

/* ------------------------------------------------------------------ */
/* P2: association ordering guard (mgmt/assoc).                       */
/* ------------------------------------------------------------------ */
#define HL_MGMT_ASSOC_GUARD_ON  1u
#define HL_MGMT_ASSOC_GUARD_OFF 0u

static u8 hl_is_assoc_response(u8 fc0)
{
    u8 type = (u8)((fc0 >> 2) & 0x3u);
    u8 subtype = (u8)((fc0 >> 4) & 0xfu);

    if (type != 0u) /* 0 = management */
        return 0u;
    return (subtype == 0x1u || subtype == 0x3u) ? 1u : 0u; /* assoc / reassoc rsp */
}

/* returns 0 = allow, -2 = refuse (ENOENT analogue) */
static int hl_assoc_guard(u8 fc0, u8 peer_known, u32 guard_enabled)
{
    if (!guard_enabled)
        return 0;
    if (hl_is_assoc_response(fc0) && !peer_known)
        return -2;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Test harness                                                       */
/* ------------------------------------------------------------------ */
static unsigned int g_checks;
static unsigned int g_fails;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        g_checks++;                                                            \
        if (!(cond)) {                                                         \
            g_fails++;                                                         \
            printf("FAIL %u: %s\n", g_checks, (msg));                          \
        }                                                                      \
    } while (0)

#define CHECK_EQ(actual, expected, msg)                                        \
    do {                                                                       \
        unsigned long _a = (unsigned long)(actual);                            \
        unsigned long _e = (unsigned long)(expected);                          \
        g_checks++;                                                            \
        if (_a != _e) {                                                        \
            g_fails++;                                                         \
            printf("FAIL %u: %s (got %lu want %lu)\n", g_checks, (msg), _a, _e); \
        }                                                                      \
    } while (0)

/* simulated completion ring / slot visibility model for the wait tests */
struct hl_sim {
    u32 slot_value[8];
    u32 valid_at[8]; /* number of reads needed before the slot becomes valid */
    u32 reads[8];
    u32 value[8];
    u32 valid;
};

static u32 hl_sim_reader(void *ctx, u32 slot)
{
    struct hl_sim *s = (struct hl_sim *)ctx;

    if (slot >= 8u)
        return 0u;
    s->reads[slot]++;
    if (s->reads[slot] < s->valid_at[slot])
        return 0u;
    return s->value[slot];
}

static void test_ring_arithmetic(void)
{
    u32 c = 0u;

    CHECK_EQ(hl_idx_advance(0u, HL_NORM_DEPTH), 1u, "advance 0 -> 1");
    CHECK_EQ(hl_idx_advance(HL_NORM_DEPTH - 1u, HL_NORM_DEPTH),
             HL_PHASE_BIT, "advance depth-1 -> 0 phase flip");
    CHECK_EQ(hl_idx_advance(HL_PHASE_BIT | 5u, HL_NORM_DEPTH),
             HL_PHASE_BIT | 6u, "advance keeps phase");
    CHECK_EQ(hl_idx_advance(0u, 0u), 0u, "advance depth 0 is a no-op");
    CHECK_EQ(hl_idx_advance(HL_NORM_DEPTH, HL_NORM_DEPTH), HL_NORM_DEPTH,
             "advance invalid index is a no-op");

    CHECK_EQ(hl_ring_pending(0u, 0u, HL_CMP_DEPTH, &c), HL_OK,
             "pending equal indices");
    CHECK_EQ(c, 0u, "pending equal -> 0");
    CHECK_EQ(hl_ring_pending(32u, 0u, HL_CMP_DEPTH, &c), HL_OK,
             "pending 32 ahead");
    CHECK_EQ(c, 32u, "pending 32 ahead -> 32");
    CHECK_EQ(hl_ring_pending(HL_PHASE_BIT, 0u, HL_CMP_DEPTH, &c), HL_OK,
             "pending equal idx different phase");
    CHECK_EQ(c, HL_CMP_DEPTH, "pending equal idx different phase -> depth");
    CHECK_EQ(hl_ring_pending(0u, 32u, HL_CMP_DEPTH, &c), HL_EFORMAT,
             "pending consumer ahead -> EFORMAT");
    CHECK_EQ(hl_ring_pending(0u, 0u, 0u, &c), HL_EARG, "pending depth 0 -> EARG");
    CHECK_EQ(hl_ring_pending(HL_CMP_DEPTH, 0u, HL_CMP_DEPTH, &c), HL_ERANGE,
             "pending index >= depth -> ERANGE");

    CHECK_EQ(hl_ring_consumed(0u, 32u, HL_NORM_DEPTH, &c), HL_OK,
             "consumed +32");
    CHECK_EQ(c, 32u, "consumed +32 -> 32");
    CHECK_EQ(hl_ring_consumed(240u, 16u, HL_NORM_DEPTH, &c), HL_OK,
             "consumed across wrap");
    CHECK_EQ(c, 32u, "consumed across wrap -> 32");
    CHECK_EQ(hl_ring_consumed(0u, HL_PHASE_BIT, HL_NORM_DEPTH, &c), HL_OK,
             "consumed exactly one lap");
    CHECK_EQ(c, HL_NORM_DEPTH, "consumed one lap -> depth");
    CHECK_EQ(hl_ring_consumed(0u, 0u, HL_NORM_DEPTH, &c), HL_OK,
             "consumed none");
    CHECK_EQ(c, 0u, "consumed none -> 0");
    CHECK_EQ(hl_ring_consumed(0u, HL_NORM_DEPTH, HL_NORM_DEPTH, &c), HL_ERANGE,
             "consumed index >= depth -> ERANGE");
}

static void test_entry_classify_and_wait(void)
{
    struct hl_sim s;
    u8 kind = 0xffu, state = 0xffu;
    u32 waits = 0u;

    /* pools: norm [0x1000..0x1fff], small [0x2000..0x2fff] (test values only) */
    CHECK_EQ(hl_entry_classify(0u, 0x1000u, 0x1fffu, 0x2000u, 0x2fffu, &kind,
                               &state), HL_OK, "classify zero");
    CHECK_EQ(state, HL_ENTRY_WAIT, "zero entry -> WAIT");
    CHECK_EQ(hl_entry_classify(0x1004u, 0x1000u, 0x1fffu, 0x2000u, 0x2fffu,
                               &kind, &state), HL_OK, "classify norm entry");
    CHECK_EQ(state, HL_ENTRY_VALID, "norm entry -> VALID");
    CHECK_EQ(kind, 0u, "norm entry -> ring kind 0");
    CHECK_EQ(hl_entry_classify(0x2004u, 0x1000u, 0x1fffu, 0x2000u, 0x2fffu,
                               &kind, &state), HL_OK, "classify small entry");
    CHECK_EQ(kind, 1u, "small entry -> ring kind 1");
    CHECK_EQ(hl_entry_classify(0x3004u, 0x1000u, 0x1fffu, 0x2000u, 0x2fffu,
                               &kind, &state), HL_OK, "classify out-of-pool");
    CHECK_EQ(state, HL_ENTRY_BAD, "out-of-pool entry -> BAD");
    CHECK_EQ(hl_entry_classify(0x1005u, 0x1000u, 0x1fffu, 0x2000u, 0x2fffu,
                               &kind, &state), HL_OK, "classify misaligned");
    CHECK_EQ(state, HL_ENTRY_BAD, "misaligned nonzero entry -> BAD");

    /* entry becomes valid after 3 reads; budget 8 -> accepted after 3 reads */
    memset(&s, 0, sizeof(s));
    s.valid_at[0] = 3u;
    s.value[0] = 0x1004u;
    CHECK_EQ(hl_wait_resolve(&s, hl_sim_reader, 0u, 0x1000u, 0x1fffu, 0x2000u,
                             0x2fffu, 8u, &kind, &waits), HL_ENTRY_VALID,
             "wait resolves after transient");
    CHECK_EQ(waits, 2u, "wait reads: 2 extra reads (3rd read valid)");
    CHECK_EQ(kind, 0u, "wait resolved kind norm");

    /* never becomes valid, budget 8 -> SPURIOUS, no consumption */
    memset(&s, 0, sizeof(s));
    s.valid_at[0] = 1000u;
    s.value[0] = 0x1004u;
    CHECK_EQ(hl_wait_resolve(&s, hl_sim_reader, 0u, 0x1000u, 0x1fffu, 0x2000u,
                             0x2fffu, 8u, &kind, &waits), HL_ENTRY_SPURIOUS,
             "wait exhausts -> SPURIOUS");
    CHECK_EQ(waits, 9u, "spurious wait count = budget+1");

    /* budget 0 = today's fail-closed-on-first-zero behaviour (negative control) */
    memset(&s, 0, sizeof(s));
    CHECK_EQ(hl_wait_resolve(&s, hl_sim_reader, 0u, 0x1000u, 0x1fffu, 0x2000u,
                             0x2fffu, 0u, &kind, &waits), HL_ENTRY_SPURIOUS,
             "budget 0 -> SPURIOUS immediately");
    CHECK_EQ(waits, 1u, "budget 0 reads once");

    /* budget is clamped to the proposal maximum */
    memset(&s, 0, sizeof(s));
    CHECK_EQ(hl_wait_resolve(&s, hl_sim_reader, 0u, 0x1000u, 0x1fffu, 0x2000u,
                             0x2fffu, 100000u, &kind, &waits), HL_ENTRY_SPURIOUS,
             "wait budget clamps");
    CHECK_EQ(waits, HL_ENTRY_WAIT_MAX + 1u, "clamped wait count");

    CHECK_EQ(hl_spurious_fatal(0u, HL_SPURIOUS_CAP), 0u, "spurious 0 not fatal");
    CHECK_EQ(hl_spurious_fatal(HL_SPURIOUS_CAP - 1u, HL_SPURIOUS_CAP), 0u,
             "spurious 63 not fatal");
    CHECK_EQ(hl_spurious_fatal(HL_SPURIOUS_CAP, HL_SPURIOUS_CAP), 1u,
             "spurious 64 fatal (fail-closed)");
}

static void test_clear_after_read_and_refill(void)
{
    u32 ring[HL_NORM_DEPTH];
    u32 i, n = 0u;

    for (i = 0u; i < HL_NORM_DEPTH; i++)
        ring[i] = 0xdead0000u + i;
    CHECK_EQ(hl_cmp_clear(ring, HL_NORM_DEPTH, 7u), HL_OK, "clear slot 7");
    CHECK_EQ(ring[7], 0u, "clear slot 7 zeroed");
    CHECK_EQ(ring[6], 0xdead0000u + 6u, "clear leaves neighbours untouched");
    CHECK_EQ(hl_cmp_clear(ring, HL_NORM_DEPTH, HL_NORM_DEPTH), HL_ERANGE,
             "clear index >= depth -> ERANGE");
    CHECK_EQ(hl_cmp_clear(NULL, HL_NORM_DEPTH, 0u), HL_EARG, "clear NULL -> EARG");

    /* published all 256 (0x8000 = idx0 phase1); device consumed 32 */
    CHECK_EQ(hl_refill_count(0x20u, HL_PHASE_BIT, HL_NORM_DEPTH, 1u, 64u, &n),
             HL_OK, "refill count ok");
    CHECK_EQ(n, 1u, "refill bounded by completed buffers");
    CHECK_EQ(hl_refill_count(0x20u, HL_PHASE_BIT, HL_NORM_DEPTH, 100u, 64u, &n),
             HL_OK, "refill count 2 ok");
    CHECK_EQ(n, 32u, "refill bounded by live consumer delta");
    CHECK_EQ(hl_refill_count(0x20u, HL_PHASE_BIT, HL_NORM_DEPTH, 100u, 8u, &n),
             HL_OK, "refill count 3 ok");
    CHECK_EQ(n, 8u, "refill bounded by max batch");
    CHECK_EQ(hl_refill_count(0x20u, HL_PHASE_BIT, HL_NORM_DEPTH, 0u, 64u, &n),
             HL_OK, "refill count no completions");
    CHECK_EQ(n, 0u, "cannot republish a buffer the host does not own");
}

static void test_stall_classify(void)
{
    u8 cls = 0xffu;

    CHECK_EQ(hl_stall_classify(0u, 0u, 0x20u, 0x20u, 0x20u, 0x20u, 0u, &cls),
             HL_OK, "classify idle");
    CHECK_EQ(cls, HL_CLASS_IDLE, "arm == now -> IDLE");
    CHECK_EQ(hl_stall_classify(0u, 0u, 0x20u, 0x40u, 0x20u, 0x20u, 0u, &cls),
             HL_OK, "classify prefetch");
    CHECK_EQ(cls, HL_CLASS_PREFETCH, "free rptr moved only -> PREFETCH");
    CHECK_EQ(hl_stall_classify(0u, 4u, 0x20u, 0x20u, 0x20u, 0x20u, 0u, &cls),
             HL_OK, "classify progress");
    CHECK_EQ(cls, HL_CLASS_PROGRESS, "cmp wptr moved -> PROGRESS");
    CHECK_EQ(hl_stall_classify(0u, 0u, 0x20u, 0x20u, 0x20u, 0x20u, 1u, &cls),
             HL_OK, "classify spurious");
    CHECK_EQ(cls, HL_CLASS_SPURIOUS, "spurious flag -> SPURIOUS");
    CHECK_EQ(hl_stall_classify(0u, 0u, 0x20u, 0x20u, 0x20u, 0x20u, 0u, NULL),
             HL_EARG, "classify NULL -> EARG");
}

static void test_mgmt_assoc_guard(void)
{
    /* frame-control byte 0 values (type bits 2-3, subtype bits 4-7) */
    CHECK_EQ(hl_is_assoc_response(0x10u), 1u, "assoc response detected");
    CHECK_EQ(hl_is_assoc_response(0x30u), 1u, "reassoc response detected");
    CHECK_EQ(hl_is_assoc_response(0x00u), 0u, "assoc request not a response");
    CHECK_EQ(hl_is_assoc_response(0x50u), 0u, "probe response not assoc rsp");
    CHECK_EQ(hl_is_assoc_response(0xb0u), 0u, "auth not assoc rsp");
    CHECK_EQ(hl_is_assoc_response(0xd0u), 0u, "action not assoc rsp");
    CHECK_EQ(hl_is_assoc_response(0x08u), 0u, "data frame not assoc rsp");

    CHECK_EQ(hl_assoc_guard(0x10u, 1u, HL_MGMT_ASSOC_GUARD_ON), 0,
             "guard allows assoc rsp with known peer");
    CHECK_EQ(hl_assoc_guard(0x10u, 0u, HL_MGMT_ASSOC_GUARD_ON), -2,
             "guard refuses assoc rsp for unknown peer");
    CHECK_EQ(hl_assoc_guard(0x50u, 0u, HL_MGMT_ASSOC_GUARD_ON), 0,
             "guard allows probe rsp without station");
    CHECK_EQ(hl_assoc_guard(0x10u, 0u, HL_MGMT_ASSOC_GUARD_OFF), 0,
             "guard off = legacy behaviour");
}

static void test_sibling_constants(void)
{
    CHECK_EQ(HL_IDX_MASK, 0x7fffu, "15 index bits [V]");
    CHECK_EQ(HL_PHASE_BIT, 0x8000u, "phase/wrap bit 15 [V]");
    CHECK_EQ(HL_NORM_DEPTH, 256u, "our norm ring 256 [V]");
    CHECK_EQ(HL_SMALL_DEPTH, 512u, "our small ring 512 [V]");
    CHECK_EQ(HL_CMP_DEPTH, 4095u, "our completion ring 4095 [V]");
    CHECK_EQ(HL_ENTRY_SIZE_OURS, 4u, "our entry is 4 B devVA [V]");
    CHECK_EQ(HL_HI1105_NORM_DEPTH, 4095u, "Hi1105 normal free ring 4095 [V]");
    CHECK_EQ(HL_HI1105_SMALL_DEPTH, 512u, "Hi1105 small free ring 512 [V]");
    CHECK_EQ(HL_HI1105_CMP_DEPTH, 4095u, "Hi1105 completion ring 4095 [V]");
    CHECK_EQ(HL_HI1105_RESET_MASK, 0x30u, "Hi1105 free-ring reset mask 0x30 [V]");
    CHECK_EQ(HL_ENTRY_SIZE_HI1105, 8u, "Hi1105 entry is 8 B devVA [V]");
    CHECK_EQ(HL_ENTRY_WAIT_MAX, 256u, "P1 wait budget cap");
    CHECK_EQ(HL_SPURIOUS_CAP, 64u, "P1 spurious hard cap");

    /* the exact two depths that match across the chips */
    CHECK(HL_SMALL_DEPTH == HL_HI1105_SMALL_DEPTH,
          "small ring depth matches Hi1105");
    CHECK(HL_CMP_DEPTH == HL_HI1105_CMP_DEPTH,
          "completion ring depth matches Hi1105");
}

int main(void)
{
    test_ring_arithmetic();
    test_entry_classify_and_wait();
    test_clear_after_read_and_refill();
    test_stall_classify();
    test_mgmt_assoc_guard();
    test_sibling_constants();

    printf("%s: %u/%u checks passed%s\n",
           g_fails == 0u ? "PASS" : "FAIL", g_checks - g_fails, g_checks,
           g_fails == 0u ? "" : " - see FAIL lines above");
    return g_fails == 0u ? 0 : 1;
}
