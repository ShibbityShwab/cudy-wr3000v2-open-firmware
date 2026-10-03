# A bound capture: the port's own message type, with one field differing (phase 25o, 2026-10-03)

The write-atomic capture works, and it settled the question the pooled reads could not.

## The capture

Read the write index **and all 32 descriptors in one ssh call** (internally consistent), take the slot the
index just published, then read *that slot's own buffer* in the immediately following call. Ten captures,
no misses. Four of them carried real content:

```
wptr=  28 slot=27 len= 72 buf=0x85000440  head=[04000100 001d0048 5a5a0000 00000000]
wptr=1031 slot= 6 len=184 buf=0x85000840  head=[00330102 001d0010 ffff00ff c45015d0]
wptr=1043 slot=18 len= 72 buf=0x848c6240  head=[00000102 001d0010 00000000 c45022e0]
wptr=   9 slot= 8 len= 72 buf=0x85001a40  head=[31353532 00000039 b95a1e20 bc437672]
```

The layouts are consistent with the magic-anchored parse of phase 25m - `+0` a type word, `+4` a length
(`0x0048` = 72, `0x0010` = 16), `+6` a field, `+0xa` the `0x5a5a` magic.

## What it settles

**The first capture is the port's own message type, and it differs from the port's frame in exactly one
field.**

```
vendor  (bound, slot 27):  04000100  001d0048  5a5a0000      type 0x04000100, +4..5 = 0x0048, +6..7 = 0x001d
port    (omo_sr_msg):      00 01 00 04  48 00  01 00  00 00  5a 5a
                            type 0x04000100, +4..5 = 0x0048, +6..7 = 0x0001
                                                                          ^^^^ the only difference
```

So for the type the port actually sends, every field agrees **except `+0x06`**: the vendor's bound message
carries `0x001d` where the port carries `0x0001`.

## Why this was untested until now

Phase 25i did set `+6 = 0x001d` - but **in the same change as `+0`**, which moved the frame to a different
type (`0x01000100`). Two variables at once, and the result was read as neutral. Phase 25k then restored
`+0` **and** `+6` together, so the combination that a bound capture now shows is the real one - the port's
own type with the vendor's `+6` - was never actually run.

That is the third instance in this phase of a change that looked like a control but wasn't: the pooled
buffer reads, the mismatched id offset, and now an accidental two-variable edit. All three are the same
shape - **a comparison whose variables were not actually held fixed.**

## The test this defines

Change **only** `omo_sr_msg`'s `+0x06` from `0x0001` to `0x001d`, leaving `+0 = 0x04000100` and
`+4 = 0x0048` alone. One constant, one variable, from a capture bound by construction.
