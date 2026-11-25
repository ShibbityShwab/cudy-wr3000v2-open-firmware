/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rx_wm_selftest.c - host-only selftest for the wifi-rx-watermark free-ring
 * refill watermark model (shared task task-156).
 *
 * Why: the RX drain may re-publish only buffers that a validated completion
 * handed back, and one drain action consumes at most rx_drain_max (64)
 * completions.  A completion backlog larger than that left posted free-ring
 * entries leaked forever, so sustained EAPOL/data traffic could starve the
 * MAC at its prefetch watermark (the iwlwifi RX_STALLED shape).  This test:
 *
 *   1. pins the portable watermark rules (target, trigger, plan) with LITERAL
 *      expected values, so a macro/rounding regression cannot make the test
 *      lie;
 *   2. models the two free rings (posted / in-flight / returned-queued buffer
 *      states) with the real packed-index helpers and checks every invariant
 *      after every simulated drain;
 *   3. reproduces the old rx-static leak with the watermark disabled (control)
 *      and proves the watermark pass repairs it - including with no new
 *      completions and with a runtime enable of the parameter;
 *   4. bounds the pass: it never publishes a buffer still in flight, never
 *      overwrites a posted entry, never exceeds avail / depth - posted / cap,
 *      and a sustained <= rx_drain_max workload never drips below full.
 *
 * Build/run (no kernel, no vendor data):
 *   cc -std=c99 -O2 -Wall -Wextra -Werror -pedantic -I src \
 *      tools/rx_wm_selftest.c src/hi5622_rx_live.c -o /tmp/rx_wm_selftest
 *   /tmp/rx_wm_selftest
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_rx_live.h"

#define NORM_DEPTH 256u
#define SMALL_DEPTH 512u
#define DRAIN_MAX 64u
#define WM_DEF 75u
#define SIM_MAX_DEPTH 512u

static int checks;
static int fails;

static void ck(int cond, const char *what)
{
    checks++;
    if (!cond) {
        fails++;
        printf("FAIL: %s\n", what);
    } else {
        printf("PASS: %s\n", what);
    }
}

/* ------------------------------------------------------------------ */
/* 1. Portable rule table: LITERAL expected values                    */
/* ------------------------------------------------------------------ */
static void test_rules(void)
{
    rx_u32 t = 0xdeadbeefu;
    rx_u32 plan = 0xdeadbeefu;

    ck(hi5622_rx_wm_target(NORM_DEPTH, WM_DEF, &t) == HIL_RX_OK && t == 192u,
       "target 256/75 = 192 (>= 3/4 posted)");
    ck(hi5622_rx_wm_target(SMALL_DEPTH, WM_DEF, &t) == HIL_RX_OK && t == 384u,
       "target 512/75 = 384");
    ck(hi5622_rx_wm_target(NORM_DEPTH, 100u, &t) == HIL_RX_OK && t == 256u,
       "target 256/100 = full ring");
    ck(hi5622_rx_wm_target(NORM_DEPTH, 50u, &t) == HIL_RX_OK && t == 128u,
       "target 256/50 = 128");
    ck(hi5622_rx_wm_target(NORM_DEPTH, 33u, &t) == HIL_RX_OK && t == 84u,
       "target 256/33 = 84 (floor)");
    ck(hi5622_rx_wm_target(SMALL_DEPTH, 33u, &t) == HIL_RX_OK && t == 168u,
       "target 512/33 = 168 (floor)");
    ck(hi5622_rx_wm_target(NORM_DEPTH, 1u, &t) == HIL_RX_OK && t == 2u,
       "target 256/1 = 2 (floor, nonzero)");
    ck(hi5622_rx_wm_target(NORM_DEPTH, 0u, &t) == HIL_RX_OK && t == 0u,
       "target wm=0 disabled");
    ck(hi5622_rx_wm_target(NORM_DEPTH, 101u, &t) == HIL_RX_ERANGE,
       "target wm>100 refused");
    ck(hi5622_rx_wm_target(0u, WM_DEF, &t) == HIL_RX_EARG,
       "target depth 0 refused");
    ck(hi5622_rx_wm_target(NORM_DEPTH, WM_DEF, NULL) == HIL_RX_EARG,
       "target NULL refused");

    ck(hi5622_rx_wm_triggered(NORM_DEPTH, WM_DEF, 191u) == 1,
       "trigger 191 < 192");
    ck(hi5622_rx_wm_triggered(NORM_DEPTH, WM_DEF, 192u) == 0,
       "trigger 192 == target is healthy");
    ck(hi5622_rx_wm_triggered(NORM_DEPTH, WM_DEF, 256u) == 0,
       "trigger full ring healthy");
    ck(hi5622_rx_wm_triggered(NORM_DEPTH, WM_DEF, 257u) == HIL_RX_EFORMAT,
       "trigger posted>depth refused");
    ck(hi5622_rx_wm_triggered(NORM_DEPTH, 0u, 3u) == 0,
       "trigger disabled at wm=0");
    ck(hi5622_rx_wm_triggered(SMALL_DEPTH, WM_DEF, 383u) == 1,
       "trigger small 383 < 384");

    /* plan */
    ck(hi5622_rx_wm_plan(NORM_DEPTH, WM_DEF, 192u, 100u, DRAIN_MAX, &plan) ==
           HIL_RX_OK && plan == 0u,
       "plan healthy ring publishes nothing");
    ck(hi5622_rx_wm_plan(NORM_DEPTH, WM_DEF, 191u, 100u, DRAIN_MAX, &plan) ==
           HIL_RX_OK && plan == DRAIN_MAX,
       "plan trigger at 191 bounded by cap 64");
    ck(hi5622_rx_wm_plan(NORM_DEPTH, WM_DEF, 120u, 10u, DRAIN_MAX, &plan) ==
           HIL_RX_OK && plan == 10u,
       "plan bounded by queued buffers");
    ck(hi5622_rx_wm_plan(NORM_DEPTH, WM_DEF, 120u, 136u, DRAIN_MAX, &plan) ==
           HIL_RX_OK && plan == DRAIN_MAX,
       "plan bounded by cap (136 queued, 136 room)");
    ck(hi5622_rx_wm_plan(NORM_DEPTH, WM_DEF, 0u, 300u, DRAIN_MAX, &plan) ==
           HIL_RX_ERANGE,
       "plan avail>depth refused");
    ck(hi5622_rx_wm_plan(NORM_DEPTH, WM_DEF, 300u, 0u, DRAIN_MAX, &plan) ==
           HIL_RX_ERANGE,
       "plan posted>depth refused");
    ck(hi5622_rx_wm_plan(NORM_DEPTH, 0u, 0u, 256u, DRAIN_MAX, &plan) ==
           HIL_RX_OK && plan == 0u,
       "plan disabled at wm=0 even when empty");
    ck(hi5622_rx_wm_plan(0u, WM_DEF, 0u, 0u, DRAIN_MAX, &plan) == HIL_RX_EARG,
       "plan depth 0 refused");
    ck(hi5622_rx_wm_plan(NORM_DEPTH, 101u, 10u, 10u, DRAIN_MAX, &plan) ==
           HIL_RX_ERANGE,
       "plan wm>100 refused");
    ck(hi5622_rx_wm_plan(NORM_DEPTH, WM_DEF, 10u, 10u, DRAIN_MAX, NULL) ==
           HIL_RX_EARG,
       "plan NULL refused");
    ck(hi5622_rx_wm_plan(SMALL_DEPTH, 100u, 511u, 200u, 64u, &plan) ==
           HIL_RX_OK && plan == 1u,
       "plan 512/100 at 511: only one slot can be re-published");

    /* free-ring direction / packed phase one-lap */
    {
        rx_u32 n = 0;
        ck(hi5622_rx_ring_pending(0x8000u, 0x0000u, NORM_DEPTH, &n) ==
               HIL_RX_OK && n == 256u,
           "free ring full: producer 0x8000 vs consumer 0 => 256 posted");
        ck(hi5622_rx_ring_pending(0x8000u, 0x0020u, NORM_DEPTH, &n) ==
               HIL_RX_OK && n == 224u,
           "32 free entries consumed => 224 posted");
        ck(hi5622_rx_ring_pending(0x8000u, 0x8000u, NORM_DEPTH, &n) ==
               HIL_RX_OK && n == 0u,
           "no posted entries after one lap");
    }
}

/* ------------------------------------------------------------------ */
/* 2. Ring state-machine model                                        */
/* ------------------------------------------------------------------ */
struct simring {
    unsigned int depth;
    int st[SIM_MAX_DEPTH];   /* 1 posted, 2 in-flight, 3 queued-for-refill */
    int slot[SIM_MAX_DEPTH]; /* buffer id posted in the ring slot, -1 empty */
    rx_u32 prod;
    rx_u32 cons;
    rx_u32 last_rptr;
    int q[SIM_MAX_DEPTH];
    int qhead;
    int qtail;
    int qcount;
    int inflight[SIM_MAX_DEPTH];
    int ifhead;
    int iftail;
    int ifcount;
    unsigned int wm_pct;
    unsigned int cap;
    int trips;
    int credit_low;
    int wm_refilled;
    int publishes;           /* total buffer re-publications (checks budgets) */
};

static void sim_init(struct simring *s, unsigned int depth, unsigned int wm_pct,
                     unsigned int cap)
{
    unsigned int i;

    memset(s, 0, sizeof(*s));
    s->depth = depth;
    s->wm_pct = wm_pct;
    s->cap = cap;
    for (i = 0; i < depth; i++) {
        s->st[i] = 1;
        s->slot[i] = (int)i;
    }
    s->prod = 0x8000u; /* one full lap published: hi5622_rx_wptr_pack(0,1,depth) */
    s->cons = 0u;
    s->last_rptr = 0u;
}

static int sim_posted(struct simring *s)
{
    rx_u32 n = 0;

    if (hi5622_rx_ring_pending(s->prod, s->cons, s->depth, &n) != HIL_RX_OK)
        return -1;
    return (int)n;
}

/* Device consumes up to want posted entries (it cannot consume unposted ones). */
static int sim_consume(struct simring *s, unsigned int want)
{
    int posted = sim_posted(s);
    unsigned int i;

    if (posted < 0)
        return -1;
    if (want > (unsigned int)posted)
        want = (unsigned int)posted;
    for (i = 0; i < want; i++) {
        rx_u32 idx = s->cons & HIL_RX_IDX_MASK;
        int b;

        if (idx >= s->depth)
            return -1;
        b = s->slot[idx];
        if (b < 0 || s->st[b] != 1)
            return -1;
        s->slot[idx] = -1;
        s->st[b] = 2;
        s->inflight[s->iftail] = b;
        s->iftail = (s->iftail + 1) % (int)s->depth;
        s->ifcount++;
        s->cons = hi5622_rx_idx_advance(s->cons, s->depth);
    }
    return (int)want;
}

/* Publish one queued buffer into the next producer slot. */
static int sim_publish(struct simring *s)
{
    rx_u32 idx = s->prod & HIL_RX_IDX_MASK;
    int b;

    if (s->qcount <= 0 || idx >= s->depth)
        return -1;
    b = s->q[s->qhead];
    if (s->st[b] != 3)
        return -1;
    s->qhead = (s->qhead + 1) % (int)s->depth;
    s->qcount--;
    s->slot[idx] = b;
    s->st[b] = 1;
    s->prod = hi5622_rx_idx_advance(s->prod, s->depth);
    s->publishes++;
    return 0;
}

/* Mirror of the driver drain: bounded completion drain -> window refill ->
 * fresh-consumer watermark pass.  Returns the number of re-publications, or a
 * negative error. */
static int sim_drain(struct simring *s)
{
    unsigned int want = (unsigned int)s->ifcount;
    rx_u32 avail = 0, posted = 0, plan = 0;
    int rc, i, refills = 0, refilled_wm = 0, wm_pub = 0;

    if (want > s->cap)
        want = s->cap;
    for (i = 0; i < (int)want; i++) {
        int b = s->inflight[s->ifhead];

        if (s->st[b] != 2)
            return -1;
        s->ifhead = (s->ifhead + 1) % (int)s->depth;
        s->ifcount--;
        s->st[b] = 3;
        s->q[s->qtail] = b;
        s->qtail = (s->qtail + 1) % (int)s->depth;
        s->qcount++;
    }
    /* window refill (old rx-static path) */
    rc = hi5622_rx_ring_consumed(s->last_rptr, s->cons, s->depth, &avail);
    if (rc != HIL_RX_OK)
        return -1;
    while (avail > 0u && s->qcount > 0) {
        if (sim_publish(s) < 0)
            return -1;
        avail--;
        refills++;
    }
    s->last_rptr = s->cons;
    /* watermark pass against the fresh consumer snapshot (== s->cons here) */
    if (s->wm_pct > 0u) {
        rx_u32 target = 0;

        if (hi5622_rx_ring_pending(s->prod, s->cons, s->depth, &posted) !=
            HIL_RX_OK)
            return -1;
        if (posted < s->depth) {
            rc = hi5622_rx_wm_target(s->depth, s->wm_pct, &target);
            if (rc != HIL_RX_OK)
                return -1;
            if (posted < target) {
                s->trips++;
                rc = hi5622_rx_wm_plan(s->depth, s->wm_pct, posted,
                                       (rx_u32)s->qcount, s->cap, &plan);
                if (rc != HIL_RX_OK)
                    return -1;
                while (plan > 0u) {
                    if (sim_publish(s) < 0)
                        return -1;
                    plan--;
                    wm_pub++;
                    refills++;
                }
                s->last_rptr = s->cons;
                if (hi5622_rx_wm_triggered(s->depth, s->wm_pct,
                                            posted + (rx_u32)wm_pub) == 1)
                    s->credit_low++;
                s->wm_refilled += wm_pub;
            }
        }
    }
    if (refilled_wm != 0)
        return -1; /* placate -Werror unused */
    return refills;
}

/* Structural invariants of one ring after any action. */
static int sim_check(struct simring *s, const char *label)
{
    int posted = sim_posted(s);
    int n1 = 0, n2 = 0, n3 = 0, i, slots = 0, dups = 0;
    int seen[SIM_MAX_DEPTH];

    memset(seen, 0, sizeof(seen));
    if (posted < 0) {
        ck(0, label);
        return -1;
    }
    for (i = 0; i < (int)s->depth; i++) {
        if (s->st[i] == 1)
            n1++;
        else if (s->st[i] == 2)
            n2++;
        else if (s->st[i] == 3)
            n3++;
        else
            n3 = -99999;
    }
    for (i = 0; i < (int)s->depth; i++) {
        if (s->slot[i] < 0)
            continue;
        slots++;
        if (s->slot[i] >= (int)s->depth) {
            dups = -1;
            continue;
        }
        if (seen[s->slot[i]]++ != 0)
            dups = 1;
    }
    ck(n3 >= 0 && n1 + n2 + n3 == (int)s->depth,
       label ? "buffer state partition covers the whole ring" : "partition");
    ck(posted == n1, "posted count == ring producer-consumer distance");
    ck(slots == n1 && dups == 0,
       "ring slots hold exactly the posted buffers, no duplicate");
    ck(n2 == s->ifcount, "in-flight count matches completion backlog");
    ck(n3 == s->qcount, "queued count matches refill queue");
    return posted;
}

/* Silent structural verifier for long loops: returns the posted count when the
 * ring and buffer-state invariants hold, -1 otherwise (no PASS line spam). */
static int sim_quiet(struct simring *s)
{
    int posted = sim_posted(s);
    int n1 = 0, n2 = 0, n3 = 0, i, slots = 0, dups = 0;
    int seen[SIM_MAX_DEPTH];

    if (posted < 0)
        return -1;
    memset(seen, 0, sizeof(seen));
    for (i = 0; i < (int)s->depth; i++) {
        if (s->st[i] == 1)
            n1++;
        else if (s->st[i] == 2)
            n2++;
        else if (s->st[i] == 3)
            n3++;
        else
            return -1;
    }
    for (i = 0; i < (int)s->depth; i++) {
        if (s->slot[i] < 0)
            continue;
        slots++;
        if (s->slot[i] >= (int)s->depth || seen[s->slot[i]]++ != 0)
            dups = 1;
    }
    if (posted != n1 || n1 + n2 + n3 != (int)s->depth || slots != n1 ||
        dups != 0 || n2 != s->ifcount || n3 != s->qcount)
        return -1;
    return posted;
}

static void test_burst_recovery(void)
{
    struct simring s;

    sim_init(&s, NORM_DEPTH, WM_DEF, DRAIN_MAX);
    (void)sim_check(&s, "burst: initial full ring");
    ck(sim_posted(&s) == 256, "burst: 256 entries posted after init");
    ck(sim_consume(&s, 200u) == 200, "burst: device consumed 200 free entries");
    ck(sim_posted(&s) == 56, "burst: 56 posted after the 200-entry burst");

    ck(sim_drain(&s) == 64, "burst: drain 1 re-published 64 (window refill only)");
    (void)sim_check(&s, "burst: after drain 1");
    ck(sim_posted(&s) == 120, "burst: posted 120 after drain 1 (old window refill)");
    ck(s.trips == 1 && s.credit_low == 1,
       "burst: watermark tripped once and classified CREDIT_LOW (queue empty)");

    (void)sim_drain(&s);
    (void)sim_check(&s, "burst: after drain 2");
    ck(sim_posted(&s) == 184, "burst: watermark recovered posted to 184 after drain 2");
    ck(s.credit_low == 2, "burst: second CREDIT_LOW while still below 192");

    (void)sim_drain(&s);
    (void)sim_check(&s, "burst: after drain 3");
    ck(sim_posted(&s) == 248, "burst: posted 248 after drain 3 (above 3/4 target)");

    (void)sim_drain(&s);
    (void)sim_check(&s, "burst: after drain 4 (last completions)");
    ck(s.ifcount == 0 && s.qcount == 8 && sim_posted(&s) == 248,
       "burst: idle after backlog drained, queue 8 above-watermark buffers");
    ck(sim_drain(&s) == 0 && sim_posted(&s) == 248,
       "burst: above-watermark idle drain publishes nothing (floor, not full)");
    ck(s.credit_low == 2, "burst: no further CREDIT_LOW once above the watermark");
}

static void test_control_old_behaviour(void)
{
    struct simring s;

    /* wm=0 must reproduce the old rx-static leak exactly: this is the control
     * proving the fix is gated by the parameter. */
    sim_init(&s, NORM_DEPTH, 0u, DRAIN_MAX);
    ck(sim_consume(&s, 200u) == 200, "control: device consumed 200 free entries");
    (void)sim_drain(&s);
    (void)sim_drain(&s);
    (void)sim_drain(&s);
    (void)sim_drain(&s);
    (void)sim_check(&s, "control: after old-path drains");
    ck(s.ifcount == 0, "control: completion backlog fully drained");
    ck(sim_posted(&s) == 120 && s.qcount == 136,
       "control: old path leaked 136 slots (posted 120, stuck) - the T4 bug");
    ck(sim_drain(&s) == 0 && sim_posted(&s) == 120 && s.qcount == 136,
       "control: old path stays leaked with no new completions");
    ck(s.wm_refilled == 0 && s.trips == 0,
       "control: watermark disabled does nothing (old behaviour preserved)");
}

static void test_runtime_enable_repair(void)
{
    struct simring s;

    sim_init(&s, NORM_DEPTH, 0u, DRAIN_MAX);
    (void)sim_consume(&s, 200u);
    (void)sim_drain(&s);
    (void)sim_drain(&s);
    (void)sim_drain(&s);
    (void)sim_drain(&s);
    ck(sim_posted(&s) == 120 && s.qcount == 136 && s.ifcount == 0,
       "repair: leaked state reached with wm=0 (pending==0, 136 queued)");

    /* Operator enables the watermark at runtime and writes dp_rx_drain once
     * more.  There are no new completions; the pass may only use the queue. */
    s.wm_pct = WM_DEF;
    ck(sim_drain(&s) == 64, "repair: pending==0 drain republished 64 queued buffers");
    (void)sim_check(&s, "repair: after first no-completion pass");
    ck(sim_posted(&s) == 184, "repair: posted 184 after first pass (cap bound)");
    ck(s.trips == 1 && s.credit_low == 1,
       "repair: CREDIT_LOW while below the watermark");
    (void)sim_drain(&s);
    (void)sim_check(&s, "repair: after second no-completion pass");
    ck(sim_posted(&s) == 248, "repair: posted 248 after second pass");
    (void)sim_drain(&s);
    ck(sim_posted(&s) == 248 && s.qcount == 8,
       "repair: above watermark, remaining 8 buffers stay queued (bounded)");
    ck(s.credit_low == 1, "repair: no CREDIT_LOW after recovery");
}

static void test_sustained(void)
{
    struct simring s;
    unsigned int i;
    unsigned long lcg = 12345u;
    int bad_invariants = 0, starvation = 0, budget = 0, full_after = 1;

    sim_init(&s, NORM_DEPTH, WM_DEF, DRAIN_MAX);
    for (i = 0; i < 400u; i++) {
        unsigned int c;
        int before, refills;

        lcg = (lcg * 1103515245u + 12345u) & 0xffffffffu;
        c = (unsigned int)((lcg >> 16) % 65u); /* 0..64: <= rx_drain_max */
        before = sim_posted(&s);
        (void)sim_consume(&s, c);
        refills = sim_drain(&s);
        if (refills < 0) {
            budget = 1;
            break;
        }
        if (refills > (int)s.cap) {
            budget = 1;
            break;
        }
        if (sim_quiet(&s) != (int)s.depth)
            bad_invariants = 1;
        if (sim_posted(&s) < (int)s.depth)
            full_after = 0;
        if (before >= 0 && sim_posted(&s) < before - (int)c)
            starvation = 1;
        if (s.credit_low != 0)
            starvation = 1;
    }
    ck(bad_invariants == 0 && budget == 0,
       "sustained<=64: every drain kept all ring/buffer invariants, <=cap refills");
    ck(full_after == 1,
       "sustained<=64: ring returns to full after every drain (no permanent leak)");
    ck(starvation == 0,
       "sustained<=64: posted never lost a buffer and CREDIT_LOW never tripped");
}

static void test_one_lap(void)
{
    struct simring s;
    int i;

    sim_init(&s, NORM_DEPTH, WM_DEF, DRAIN_MAX);
    ck(sim_consume(&s, NORM_DEPTH) == 256,
       "lap: device consumed exactly one full lap (phase bit flip)");
    ck(sim_posted(&s) == 0, "lap: no posted entries after the full lap");
    for (i = 0; i < 8; i++)
        (void)sim_drain(&s);
    (void)sim_check(&s, "lap: after draining the full lap");
    ck(s.ifcount == 0, "lap: all lap completions drained");
    ck(sim_posted(&s) == 192,
       "lap: ring recovered to the 3/4 target after the one-lap burst");
    ck(s.wm_refilled == 128, "lap: watermark re-published 128 of the lap (window refill did 64)");
    ck(s.qcount == 64, "lap: 64 above-watermark buffers stay queued (bounded)");
}

static void test_small_ring(void)
{
    struct simring s;

    sim_init(&s, SMALL_DEPTH, WM_DEF, DRAIN_MAX);
    ck(sim_posted(&s) == 512, "small: 512 posted after init");
    (void)sim_consume(&s, 500u);
    ck(sim_posted(&s) == 12, "small: 12 posted after a 500-entry burst");
    (void)sim_drain(&s);
    (void)sim_check(&s, "small: after drain 1");
    ck(sim_posted(&s) == 76, "small: 76 posted after drain 1");
    {
        int guard;

        for (guard = 0; guard < 24 && (s.ifcount > 0 || s.qcount > 0); guard++)
            (void)sim_drain(&s);
    }
    (void)sim_check(&s, "small: after backlog drains");
    ck(sim_posted(&s) >= 384, "small: posted >= 3/4 target after recovery");
    ck(sim_posted(&s) == 396,
       "small: 396 posted (target 384) once the queue above the watermark is left alone");
    ck(s.qcount == 116, "small: 116 buffers stay queued above the watermark");
}

static void test_plan_grid(void)
{
    unsigned int depths[2] = { NORM_DEPTH, SMALL_DEPTH };
    unsigned int wms[4] = { 1u, 50u, 75u, 100u };
    unsigned int caps[3] = { 1u, 8u, DRAIN_MAX };
    int violations = 0;
    int seen_trigger = 0;
    unsigned int di, wi, ci;

    for (di = 0; di < 2u; di++) {
        for (wi = 0; wi < 4u; wi++) {
            for (ci = 0; ci < 3u; ci++) {
                rx_u32 posted;

                for (posted = 0; posted <= depths[di]; posted += 7u) {
                    rx_u32 avail;

                    for (avail = 0; avail <= depths[di]; avail += 13u) {
                        rx_u32 plan = 0xdeadbeefu;
                        rx_u32 room = depths[di] - posted;
                        int trg;

                        if (hi5622_rx_wm_plan(depths[di], wms[wi], posted,
                                              avail, caps[ci], &plan) !=
                            HIL_RX_OK) {
                            violations++;
                            continue;
                        }
                        trg = hi5622_rx_wm_triggered(depths[di], wms[wi],
                                                     posted);
                        if (trg < 0) {
                            violations++;
                            continue;
                        }
                        if (plan > avail || plan > room || plan > caps[ci])
                            violations++;
                        if (trg == 0 && plan != 0u)
                            violations++;
                        if (trg == 1) {
                            seen_trigger++;
                            if (plan != ((avail < room ? avail : room) < caps[ci]
                                             ? (avail < room ? avail : room)
                                             : caps[ci]))
                                violations++;
                        }
                    }
                }
            }
        }
    }
    ck(violations == 0, "plan grid: bounded by avail/room/cap and trigger-gated");
    ck(seen_trigger > 0, "plan grid: trigger cases exercised");
}

int main(void)
{
    printf("== wifi-rx-watermark host selftest (portable rules + ring model)\n");
    test_rules();
    test_burst_recovery();
    test_control_old_behaviour();
    test_runtime_enable_repair();
    test_sustained();
    test_one_lap();
    test_small_ring();
    test_plan_grid();
    printf("\n== %d checks, %d failures\n", checks, fails);
    if (fails != 0) {
        printf("RESULT: FAIL\n");
        return 1;
    }
    printf("RESULT: PASS\n");
    return 0;
}
