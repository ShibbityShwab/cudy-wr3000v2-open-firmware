# The vendor header, matched verbatim, is still negative - field-level comparison is exhausted (phase 25j, 2026-10-03)

Evidence `build/register-dumps/exp/20261003-085125/`, `params=[srpost=1 msgsvc=1 pollms=25]`.

## The test

The port's frame was set to the vendor's **recurring type-1 header, verbatim**:

```
+0 = 0x01000100   +4 = 0x0048   +6 = 0x001d   magic@0xa = 0x5a5a
```

## The result

```
ANNOUNCE out[0] <= 0x00000008 (x3, pre-release)
t=0    out[0]=0x00000008                    <- still set at release
t=25ms out[0] 0x00000008 -> 0x00000000      <- consumed
t=25ms  out[1] bit 6 set (id 6)
t=525ms out[1] 0x00000040 -> 0x00000004     <- id 2
poll done: 2 transitions
[sig] 9/9 signature registers changed -> THE CHIP LEFT ROM STATE
```

**Identical to every previous content variant.** The announce is consumed, the CPU leaves ROM state, and
the firmware's dialogue is still exactly id 6 then id 2.

## What this closes

The port's frame now agrees with the vendor's own live, recurring header in **every field it is possible
to compare**:

| field | status |
| --- | --- |
| `+0` type/proto | matched (`0x01000100`) |
| `+4` length | matched (`0x0048`), and equal to the descriptor length as the vendor's is |
| `+6` sub-field | matched (`0x001d`) |
| `+0xa` magic | matched (`0x5a5a`) |
| node layout | verified against the vendor's own decoders |
| descriptor length | verified equal to the frame's, and 72 is a length the vendor actually uses |
| announce id / timing / registers | verified against the vendor's fill and live behaviour |

**So header-level comparison is exhausted.** Three content hypotheses built from live vendor bytes have
now each been tested and returned the same result, and the remaining difference is no longer any field of
the header: it is the **body** - the ~60 bytes of payload the vendor's messages carry after the header
(which the live ring shows to be heterogeneous: strings, structured records, different lengths).

## The honest position

- The **framing** of the host message is now as well-established as this project can make it, field by
  field, against the vendor's own bytes - and none of it changes the firmware's behaviour.
- The **instrument** to study the body exists and works: the live vendor ring is readable read-only
  (`docs/phase25/live-vendor-ring-ground-truth.md`), and its payloads are visible.
- What has *not* been established is **which** vendor message the firmware expects at this point in its
  startup, which requires characterising the message *set* (types, lengths, payload shapes) rather than
  any single field - a larger piece of work than the field tests that are now exhausted, and the next
  thing this record supports doing.

Device state: `W=2 I=6 OFF=0`, calibration `[SUCC]` both bands.
