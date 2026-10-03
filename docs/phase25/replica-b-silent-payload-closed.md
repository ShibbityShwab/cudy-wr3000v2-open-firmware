# Replica B is silent: the payload question is closed (phase 25x, 2026-10-03)

Evidence `build/register-dumps/exp/20261003-100425/`, `params=[srpost=1 msgsvc=1 pollms=25]`.

## The result

The frame was a **byte-exact copy of message B** - the vendor's *first* frame on a fresh SR ring, the
`(length 48, id 1)` member of the pair:

```
t=25ms  out[0] 0x8 -> 0x0        (announce consumed)
t=25ms  out[1] bit 6 set (id 6)
t=450ms out[1] 0x40 -> 0x04      (id 2, id 6 cleared)
poll done: 2 transitions in 4000 ms; final out[1]=0x00000004
```

**Identical to replica A, and to every mix before it.**

## What is now eliminated, completely

| # | candidate | outcome |
| --- | --- | --- |
| 1-12 | every header field, individually against bound vendor evidence | no change |
| 13 | **message A** exact - len 72, id 29, zero body (the bound capture) | **silent** |
| 14 | **message B** exact - len 48, id 1, recorded body (phase-20 A.5) | **silent** |

Fourteen candidates, and the last two were not guesses: they are the vendor's own frames, byte for byte,
in both of the forms the vendor is known to put on this ring.

## The conclusion the evidence now supports

**No vendor SR message of type `0x04000100`, in either real form, elicits anything from the firmware at
this point.** Seven separate runs - two mixes, replica A, replica B, and the header variants - produced
the *same* trace: announce consumed, id 6, id 2, stop.

So the correct statement is no longer "our bytes are wrong". It is:

> The firmware reads what the port posts and continues its own sequence regardless of the content. The
> content is therefore **not** the gate at this point.

## What that redirects to, and why it is a better question

If the content is not the gate, then the gate is elsewhere, and the evidence names two places:

1. **WHEN.** Phase 25 established that an announce is only read if it is *pending at release*, because the
   firmware clears the message registers during its own boot. Everything since has kept that ordering -
   the announce is the last act before the release - but the **offset between the announce and the
   release write has never been swept**. The firmware may be reading the message at a point where the
   host is not yet posting, or posting before the firmware is listening.
2. **WHICH CHANNEL.** Every experiment has posted on **SR**. The vendor's own SR-channel fill is the
   proven id-6 trigger, but the id-6→id-2 dialogue the firmware runs afterwards may be the *reaction to
   the trigger*, not the conversation. The **DS/DR channel's producer commit has never been exercised**
   at all - and G002's named tables show the chip-side ids are dominated by rx-schedule and data-event
   handlers, which is DR-shaped work.

Both are different experiments rather than more edits to the same 72 bytes, which is why the frame is now
frozen: further payload variants would have no hypothesis behind them.

## CORRECTION to the "which channel" direction, made before acting on it

The first draft of this conclusion named the **DS/DR channel producer** as the untried direction. Reading
the record before running it shows that is **wrong**: `drpost=1` has been run twice -
`docs/phase24/dr-commit-not-the-gate.md` (buffers posted, index committed with a matching readback, the
full id-6/id-2 dialogue, 9/9 CPU start, and **0 deposit events in 4000 ms**) and again in
`docs/phase25/desclen-irrelevant-confirmed.md` **with the corrected window base and the in-post announce**.

So the DR channel is **not** untried, and "exercise the DR producer" would have been a duplicate
experiment aimed at a question already answered: *the producer commit is necessary state but not the gate,
and the receive engine does not DMA even when the buffers are posted and the dialogue runs.*

That leaves the **timing** direction as the only one of the two that is genuinely untested - which is what
phase 25y implements.

