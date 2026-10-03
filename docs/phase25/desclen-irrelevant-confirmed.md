# The descriptor length is irrelevant, confirmed under corrected timing (phase 25e, 2026-10-03)

Evidence `build/register-dumps/exp/20261003-033550/`,
`params=[... srpost=1 sr_desclen=0x30 drpost=1 msgsvc=1 pollms=25]`.

## Why this was re-run

Phase 24t had already measured that changing the announced descriptor length (72 -> 48) changed nothing -
**but it ran before the announce-timing discovery**, so the firmware was not looking at anything at the
time. That made the earlier negative weak: it could not distinguish "the length does not matter" from
"nothing was being read". With the announce now made before the release, the same parameter is
re-measured under conditions where the device demonstrably *does* consume what we send.

## The result

```
omo-drv1: ANNOUNCE (as sr_dscr_fill does) out[0] <= 0x00000008 readback=0x00000008   (x3, pre-release)
omo-drv1:   t=0  out[0]=0x00000008 (baseline)      <- still set at release
omo-drv1:   t=25ms out[0] 0x00000008 -> 0x00000000 <- CONSUMED
omo-drv1:     out[1] bit 6 set (id 6)
omo-drv1:   t=550ms out[1] 0x00000040 -> 0x00000004 <- id 2
omo-drv1:   SR ch0 baseline: wptr(+0x18)=0x00000400 rptr(+0x1c)=0x00000400   <- caught up
omo-drv1: ring watch done: 0 DR deposit events, 0 SR consumption events
```

**Identical to the `0x48` run in every respect**: the announce is consumed, ch0 is caught up with the
host's own write index, and the dialogue is still id 6 then id 2.

So the negative is now a **strong** one rather than a weak one:

- the device consumes our announce, **and**
- the device's SR index for the channel we posted is caught up (`rptr == wptr`), **and**
- the announced length makes no difference - and neither, therefore, does the question of whether the
  72-byte capture's `0xFF` tail should have been part of the message.

## What that leaves, precisely

The engine's index catching up while the dialogue does not advance is consistent with the device
**reading the descriptors and then rejecting or ignoring their contents** - i.e. the problem is not how
much we announce or when, but **what the message says.**

That is the body question in its final form: **the bytes.** Everything about the body's *framing* is now
settled and verified (node layout against the vendor's own decoder, the length field's irrelevance, the
announce timing, the id). What remains is the **content**: the 48 meaningful bytes of the captured frame
are a live capture, and whether they are the message the firmware needs *at this point* - as opposed to
some other HCC message whose shape is defined by the command table - is not established.

Device state: `W=2 I=6 OFF=0 OMO=0 FAIL=0`.
