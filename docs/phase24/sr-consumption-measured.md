# The measurement refuted its own hypothesis: the device DID consume ch0's descriptors (phase 24x, 2026-10-03)

Evidence `build/register-dumps/exp/20261003-010115/`,
`params=[... srpost=1 drpost=1 msgsvc=1 send=3 pollms=25]`.

## What the measurement was for

`tx-credit-source.md` ended with the vendor refusing to transmit unless the device has granted TX
buffers, and one thing that could turn that from a story into an observation: the buffers the device
returns are the SR descriptors it has consumed, so **the SR device index (`+0x1c`) advancing is the
observable form of a credit**. It had never been read after the release. The ring watch was built to
measure exactly that.

## What it actually showed

```
omo-drv1: SR ch0 posted 32 nodes word0=0x83b06000; commit SR+0x18 <= 0x00000400 readback=0x00000400
omo-drv1: SR ch1 posted 32 nodes ...; commit SR+0x18 <= 0x00000400
omo-drv1: SR ch2 posted 32 nodes ...; commit SR+0x18 <= 0x00000400
omo-drv1:   SR ch0 baseline: wptr(+0x18)=0x00000400 rptr(+0x1c)=0x00000400   <-- EQUAL
omo-drv1:   SR ch1 baseline: wptr(+0x18)=0x00000400 rptr(+0x1c)=0x00000010
omo-drv1:   SR ch2 baseline: wptr(+0x18)=0x00000400 rptr(+0x1c)=0x00000010
omo-drv1: ring watch done: 0 DR deposit events, 0 SR consumption events in 4000 ms
omo-drv1:     bit 2 pending -> dispatch handler[2]
omo-drv1: ---- H2D send: id 3 (bitmap 0x00000008) via out[0] + doorbell out[2] ----
omo-drv1:   NOTE the sent bit is STILL SET - the device did not consume it
```

**ch0's device index equals the host's write index** (`0x400` both). The packed index's phase bit is
set, which means the index has wrapped past the 32 descriptors the host posted - i.e. **the device read
ch0's descriptors.** ch1 and ch2, which the host filled but which are not the message path, sit at
`0x10` and are *not* caught up.

So the honest reading is the opposite of the one the watch's own note was written to assert:

> A non-advance during the watch window does **not** mean "not consumed". ch0 was **already caught up at
> baseline** - the device had consumed the descriptors before the watch began. **The device took our SR
> messages, and it still did not take the `out[0]` notification.**

## The correction

The module's note reads "the SR device index NEVER ADVANCED - the device never consumed our descriptors,
i.e. it granted no TX buffers (the observable form of zero credit)". **That inference is wrong**, and it
is wrong in the direction that matters: it reports a zero credit where the registers show a credit that
was already spent. The note is corrected to report the relationship (`rptr vs wptr`) instead of drawing
a conclusion from a *lack of change*, exactly the mistake the address-window bug taught this project -
**absence of change is not evidence of absence of the thing.**

## What this leaves

- **"Zero TX credit" is not the explanation.** The descriptors were consumed.
- The device received our SR messages (ch0 consumed) and received its own plat-ready path is exercised
  (`dispatch handler[2]`), and it still does not take the `out[0]` notification. So the gate is
  **downstream of descriptor consumption** - the device's own dispatcher state, which is where
  phases 20/22 put it and where eight host-side candidates have now failed to reach.
- ch1/ch2 sitting at `0x10` while ch0 is at `0x400` is itself a new detail worth keeping: the device
  consumes the channel the host posted messages on and not the others, so its engine is selective rather
  than stalled.

That is a smaller claim than the hypothesis, and it is the one the registers support.
