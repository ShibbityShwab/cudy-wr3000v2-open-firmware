# The firmware's word stays pending, and my service never acted on it (phase 24a, 2026-10-02)

Evidence `build/register-dumps/exp/20261002-192029/`, `params=[hw=1 program=1 fw=1 release=1 msgsvc=1]`.

## The result

```
omo-drv1: ---- mailbox service: ack/clear/re-arm + dispatch (10000 ms) ----
omo-drv1:   pending out[1] BAR0+0x3f1014 = 0x00000004 (baseline)
omo-drv1:   mailbox service done: 0 words serviced in 10000 ms; final out[1]=0x00000004
omo-drv1:   NOTE nothing was pending to service in this window
```

Two facts, one of them my bug:

1. **The firmware's word is persistent.** `out[1] = 0x00000004` (bit 2) is set when the service starts
   and is *still* `0x4` ten seconds later - the device never clears it, which is exactly what
   `docs/phase20/tx-path.md` recorded from the other direction ("the H2D mask is never cleared by the
   device").
2. **My service logic was wrong.** `omo_msg_service()` was keyed on a **transition**
   (`st && st != prev`), so when the word was already pending at entry it serviced **nothing** and
   reported the misleading "nothing was pending". The vendor's `pcie_msg_handle` runs whenever the
   pending mask is non-zero - it has no transition requirement.

So the ack/re-arm/dispatch sequence has still never actually been executed against the correct
registers. Fixed (commit `6347c72`): service on the pending **state**.

## What this changes

The previous claim "the host half was never run against the real registers" stands, but the reason is
now precise: the *observation* worked (the poll saw `0x4`), and the *service* was mis-gated. Once the
gate is right, the sequence runs on every iteration of a 10 s window, and the log will show the ack,
the clear and the re-arm with readbacks, plus whatever the device does afterwards - which is the
actual test of whether the dialogue advances.

## Worth noting about the persistent word

A pending bit that never clears is consistent with the firmware waiting for a host ack: it asserts
"message pending" and holds it until someone acknowledges. That is the shape the recovered host half
expects, and it is the first time this session has had the host and device halves described by the
same register actually observed together.
