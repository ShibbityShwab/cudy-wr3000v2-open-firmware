# The firmware consumes the announce only if it is pending AT RELEASE (phase 25b, 2026-10-03)

The in-post announce produced the first change in the `out[0]` bit in this entire investigation.

## The observation

```
[40.835921] ANNOUNCE (as sr_dscr_fill does) out[0] <= 0x00000008 readback=0x00000008
[40.878110] ANNOUNCE (as sr_dscr_fill does) out[0] <= 0x00000008 readback=0x00000008
[40.920242] ANNOUNCE (as sr_dscr_fill does) out[0] <= 0x00000008 readback=0x00000008
[43.785168] firmware readback diffs=0 match=YES
[43.803233] RELEASE write CA 0x40000108 <- 0x00005a5a
[43.824446]   t=0  out[0]=0x00000008 out[1]=0x00000000 (baseline)      <-- still set at release
[43.879470]   t=25ms out[0] 0x00000008 -> 0x00000000                  <-- CLEARED
[43.891028]     out[1] bit 6 set (id 6)
[44.799526]   t=475ms out[1] 0x00000040 -> 0x00000004                <-- id 2
```

**The `out[0]` bit written *before* the release was cleared by the device within ~25 ms of the poll
starting.** Every previous run announced *after* the release and every one of them ended with
`NOTE the sent bit is STILL SET - the device did not consume it`.

## Why this matters

That is a **timing** result, and it is the first time an `out[0]` write has ever been consumed:

| announce made | consumed by the device |
| --- | --- |
| after the release (all runs, phases 24d–24t) | **no** - "STILL SET", every time |
| **before the release** (this run) | **yes** - cleared within 25 ms |

The natural reading, consistent with everything else: **the firmware reads and clears the message
registers as part of its own boot**, so a host announcement only reaches it if it is **pending at the
moment of release**. An announcement made after the firmware has already passed that point is simply
never read - which is exactly what "the device did not consume it" has meant for nine phases.

This reframes the whole H2D line of work: **every host send this project has ever made was posted
after the moment the firmware looks.** The announce was not being rejected; it was being posted too
late to be seen.

## What it does and does not establish

**Does:** the mechanism by which a host announcement reaches the firmware - it must be pending at
release - and the reason the post-release sends never worked.

**Does not:** advance the dialogue. The firmware read the announce and still emitted only id 6 and id 2.
So consuming the announce is necessary but not sufficient; the *body* it then looks for is the next
question - and the body is now the only thing left, with the timing settled.

## The immediate next test

Announce correctly (before the release) and **remove the post-release send entirely** (`send` off), so
the boot-time announce is the only announcement. If the firmware's behaviour changes with a
correctly-timed announce and no late duplicate, that isolates the timing effect from the noise of a
second, mistimed write.
