# The one-variable test: negative, and the header is now closed (phase 25r, 2026-10-03)

Evidence `build/register-dumps/exp/20261003-093338/`, `params=[srpost=1 msgsvc=1 pollms=25]`.

## The test

The single field a BOUND vendor message of the port's own type differed in - `+0x06` - was changed **alone**
(`0x0001` -> `0x001d`), leaving `+0 = 0x04000100` and `+4 = 0x0048` untouched. This is the combination that
phase 25i never actually ran, because that edit moved `+0` at the same time.

## The result

```
ANNOUNCE out[0] <= 0x00000008 (x3, pre-release)
t=0    out[0]=0x00000008                    <- still set at release
t=25ms out[0] 0x00000008 -> 0x00000000      <- consumed
t=25ms  out[1] bit 6 set (id 6)
t=450ms out[1] 0x00000040 -> 0x00000004     <- id 2
poll done: 2 transitions
```

**Identical to every other variant.** The announce is consumed, the CPU starts, the dialogue is id 6 then
id 2 and nothing follows.

## What this closes

The header is now exhausted, field by field, **each tested individually against bound vendor evidence**:

| field | vendor value | tested | outcome |
| --- | --- | --- | --- |
| `+0x00` proto | `0x0100` / `0x0102` | yes (and the port's `0x0100` matches) | no change |
| `+0x02` | `0x0000`/`0x0001`/`0x0002`/`0x0005`/... | via the header-verbatim test | no change |
| `+0x04` length | `0x0048` (72, dominant) | yes, both 48 and 72 | no change |
| `+0x06` | `0x001d` | **yes, alone, this phase** | **no change** |
| `+0x0a` magic | `0x5a5a` | matches already | - |
| announce id / timing / registers | `pcie_msg_send(chip,3)`, pre-release | yes | consumed, but no advance |

So **no header field, and no combination of them, moves the firmware.** The message is structurally correct
by every measure this project can apply, and the firmware reads it and does not act.

## What that leaves, stated without a story

The remaining candidate is the **payload body** - the bytes after the header - whose meaning is defined by
the HCC message definitions and the command table, not by anything derivable from the ring. Two inputs
could settle it and neither is host-side work:

1. **the vendor's message definitions or source** (`OPEN-SOURCE-REQUEST.md`, which needs the human); or
2. **a device-side trace** of what the firmware does with the message it reads.

That is the honest boundary of this approach. Twelve host-side candidates have now been tested and
eliminated, each recorded with its evidence, and the value of the record is that the elimination is
exhaustive rather than a series of guesses.

