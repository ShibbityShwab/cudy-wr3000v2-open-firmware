# The difference is in the BODY, not the header (phase 25t, 2026-10-03)

A third round of bound captures returned **three messages, byte-identical**, all of type `0x04000100`:

```
wptr=  27 slot=26 buf=0x84532e40   04000100 001d0048 5a5a0000 00000000 ... (all zero to +0x47)
wptr=  23 slot=22 buf=0x84532c40   04000100 001d0048 5a5a0000 00000000 ...
wptr=1040 slot=15 buf=0x84aa7440   04000100 001d0048 5a5a0000 00000000 ...
```

Three different buffers, the same message. The port module is **not loaded** (`/proc/modules` has no
`wifidrv1`), so these are the **vendor's** messages.

## Placed against the port's frame, the header agrees and the body does not

```
offset   vendor (bound, real)      port (omo_sr_msg)      verdict
+0x00    04000100                  04000100               same
+0x04    0048 (len 72)              0048                   same
+0x06    001d                      001d                   same   <- after the phase-25p fix
+0x0a    5a5a                      5a5a                   same
+0x0c    00000000                  00000000               same
+0x10    00000000                  00000001               DIFFERENT
+0x14    00000000                  001400d8               DIFFERENT
+0x18    00000000                  00000001               DIFFERENT
+0x1c..  00000000 (all zero)       varied / zero          -
```

## Why this explains the entire negative header series

Every header field has been tested and eliminated, twelve candidates in all. **This is why that series
was negative: the header was never the problem.** The port's header, after the `+6` fix, is
byte-identical to a real bound vendor message of the same type. The difference sits entirely at
`+0x10`, `+0x14` and `+0x18` - bytes no header test touches, because they are past the header.

**The body had never been compared, because until the bound capture existed there was no trustworthy
vendor counterpart to compare it against.** The pooled reads returned recycled content, so the one
comparison that mattered could not be made; the instrument had to be fixed before the question could even
be posed.

## The test this defines

Zero the port's body from `+0x0c`, so the frame is **exactly** `04000100 001d0048 5a5a0000` followed by
zeros - a byte-for-byte copy of what the vendor demonstrably puts on its own SR ring.

**The caveat, stated before the result is known.** Three identical messages with an all-zero body could
mean either that this is a real repeated vendor message or that it is a stale/idle pattern the pool
retains. The evidence for "real" is that they came from **three different buffers** across two runs and
that the captures were bound to their own descriptors. The evidence for caution is the uniformity itself.
Either way the test is worth running: if the firmware acts on a zero-body frame and not on the port's
populated one, the difference is established as load-bearing; if it does not, the body is eliminated too
and the negative series becomes stronger rather than weaker.

