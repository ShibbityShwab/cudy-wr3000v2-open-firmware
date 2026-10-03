# The +4 length fix: another negative, from live evidence (phase 25h, 2026-10-03)

Evidence `build/register-dumps/exp/20261003-084547/` (and the same result at `...-033550`).

## The test and its result

Making the message's `+4` length field agree with its own descriptor (48 -> 72, as the live vendor
message at idx 8 does) changes nothing:

```
ANNOUNCE out[0] <= 0x00000008 (x3, pre-release)
t=0    out[0]=0x00000008        <- still set at release
t=25ms out[0] 0x00000008 -> 0x00000000   <- consumed
t=25ms  out[1] bit 6 set (id 6)
t=550ms out[1] 0x00000040 -> 0x00000004  <- id 2
poll done: 2 transitions
```

The announce is still consumed, the dialogue is still exactly id 6 then id 2, and nothing further
happens.

## Why this negative is worth more than the earlier ones

Every previous content-ish negative was an inference from silence. **This one tested a specific field
against the vendor's own live bytes**, and it is the first content change this project has made on that
basis. So it eliminates a *real* candidate rather than a guessed one - and it narrows the remaining
difference to the two fields that still disagree with the vendor's recurring header:

| field | port (captured) | vendor (live, recurring) |
| --- | --- | --- |
| `+0` | `0x04000100` | `0x01000100` |
| `+4` | `0x0048` (now) | `0x0048` |
| `+6` | `0x0001` | `0x001d` |

## The next test, and why it is the right one

**`+0` and `+6` are now the entire remaining difference** between the port's frame and the header the
vendor's own type-1 messages carry. `+0` reads as a type/protocol word, and a wrong type is the most
plausible way for a message to be read and silently discarded - which is exactly the behaviour observed
(the SR index catches up, the dialogue does not move).

So the next run sets the frame to the vendor's recurring header verbatim: `+0 = 0x01000100`,
`+6 = 0x001d`, keeping `+4 = 0x0048`.

That is the last field-level hypothesis the live ring can settle without characterising the message
*payload*, and it is testable with one build.
