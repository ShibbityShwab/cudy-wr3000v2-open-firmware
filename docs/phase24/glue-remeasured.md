# The glue status re-measured at the correct address, after servicing (phase 24f, 2026-10-02)

Evidence `build/register-dumps/exp/20261002-212053/`, `params=[hw=1 program=1 fw=1 release=1 msgsvc=1 svc=1]`.

## The measurement

```
omo-drv1: ---- glue-status service (200 ms interval, 8000 ms total) ----
omo-drv1:   t=0  glue status 0x3f02ec = 0x00000000 (masked 0x00000000)     <- LABEL, see below
omo-drv1:   service done: 0 pending events in 8000 ms; final status=0x00000000
omo-drv1:   NOTE the glue status never asserted in this window
```

**Zero events in 8 s, final status `0x00000000`** - and this time the measurement is valid where the
earlier one was not.

## Why the earlier result had to be redone

Two independent defects in the phase-23w version:

1. **Wrong address.** That run read `omo_msg + 0x2ec` while `omo_msg` was mapped at `0x3f0000`, so it
   watched `0x3f02ec` - a page below the real `0x3f12ec` (CA `0x400392ec`). The conclusion drawn from
   it ("both levels of the vendor's dispatch are silent") rested on a register that holds something
   else.
2. **Wrong sequence.** It also ran *without* servicing the firmware's pending word first. If the
   firmware holds `out[1] = 0x4` waiting for an ack, the glue path may not progress until that word
   is acknowledged - so even at the right address the test would have been premature.

The corrected run fixes both: the offset is now relative to the corrected window
(`0x3f1000 + 0x2ec = 0x3f12ec`), and the mailbox service runs first
(`poll -> service -> send -> glue watch`).

## A labelling defect found on the way, same class as before

The log line printed a **hardcoded** `0x3f02ec` beside a **relative** read
(`omo_rd(omo_msg, OMO_GLUE_STAT)`). The read was always correct; the label lied. That is precisely the
defect class recorded earlier in this project - a hardcoded absolute address printed next to a relative
read - and it is dangerous for exactly the same reason: it makes a correct measurement look like it was
taken somewhere else, and would make a future reader distrust a valid result (or trust an invalid one).
Fixed (commit `3c4b91c`): the line now prints the computed `BAR0+0x%05lx` and names the CA. A sweep
confirms **0** stale `0x3f0xxx` labels remain in the module.

## What the corrected result establishes

The glue status genuinely does not assert - not at `0x3f12ec`, not over 8 s, not after the firmware's
ready word has been acknowledged and cleared. So phase 23w's *conclusion* survives even though its
*measurement* was unsound: there is nothing for the two-level dispatch to dispatch, which is consistent
with phase 20's `irq_taken = 0` and with the H2D accept gate staying closed.

The distinction is worth keeping: a right answer from a broken measurement is still a broken
measurement, and this project has now been bitten twice by exactly that pattern (the window base, and
this label). Both are covered by the same rule - **print computed addresses, never hardcode them**.
