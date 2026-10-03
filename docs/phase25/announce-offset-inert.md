# The announce-to-release offset is inert (phase 25z, 2026-10-03)

Evidence `build/register-dumps/exp/20261003-101254/`, `params=[... reannounce=1000]`.

## The variant did what it was built to do

```
omo-drv1: waiting 1000 ms after the firmware write before re-announcing
omo-drv1: RE-ANNOUNCE out[0] <= 0x00000008 readback=0x00000008
omo-drv1: RE-ANNOUNCE out[2] <= 0x00000001 readback=0x00000000 (doorbell)
```

So the announce was **re-posted 1 s after the firmware write and immediately before the release** - a
deliberately chosen offset, where every previous run had whatever the firmware load happened to cost.

## And the result is the baseline, to the millisecond

```
t=0     out[0]=0x00000008 out[1]=0x00000000        (announce pending, as intended)
t=25ms  out[0] 0x8 -> 0x0                            (CONSUMED)
t=25ms  out[1] bit 6 set (id 6)
t=550ms out[1] 0x40 -> 0x04                          (id 2, id 6 cleared)
poll done: 2 transitions in 4000 ms; final out[1]=0x00000004
```

**Identical to the seven-run baseline.** The announce is *consumed* - so the firmware reads it - and the
dialogue is unchanged.

## What this closes

The **WHEN** direction is now tested and inert. Combined with the payload line (14 candidates, including
byte-exact copies of both real vendor messages), the position is:

> The port can put the vendor's own message, of either real form, on the ring at a moment of its choosing,
> and the firmware **reads it** (the announce is consumed) and **continues regardless**. The message is
> received and not acted upon.

That is a stronger statement than "our message is wrong", and it is the first time the evidence supports
saying the host-side send path is **doing its job** while the firmware declines to advance.

## Being honest about what remains

Two directions are genuinely untested and are *not* more frame edits:

1. **The glue ISR / `pcie_thread` route**, which the record repeatedly names as never entered by a
   takeover (`docs/phase20/fw-accept.md` A.2, `docs/phase22/fw-sr-gate.md`, `docs/phase21/sr-trigger.md`).
   The firmware's receive routine is built and armed but never dispatched. Whether the dispatch needs the
   vendor's own service thread - rather than the port's stand-in - is the open question, and it is a
   *structural* one.
2. **The vendor's own source or message definitions** (G004), which needs the human.

What is *not* on that list is any further variation of the 72 bytes, which is why the frame stays frozen.

