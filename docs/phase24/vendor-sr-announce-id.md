# The vendor's SR fill announces with id 3 - the port's id is correct (phase 24z, 2026-10-03)

A short follow of the transmit call graph that **verifies a choice the port made**, and closes one more
hypothesis class.

## `shuangta_ete_sr_dscr_fill` @ 0x17858 - the vendor's SR post

```
0x178c0 ldr  r2, [r4, #0xc]
0x178c8 ubfx r2, r2, #0, #0xa          ; the packed index, low 10 bits
0x178cc str  r1, [r3, r2, lsl #3]      ; node[idx].word0
0x178e0 str  r2, [r3, #4]              ; node[idx].word1
0x178e8 ldrb r1, [r3, #4]
0x178ec bl   <...>
0x178f0 ldr  r0, [r5, #0x80]           ; the chip object
0x178f4 mov  r1, #3                    ; <-- ID 3
0x178f8 bl   pcie_msg_send             ; pcie_msg_send(chip, 3)
0x178fc ldr  r3, [r4, #0x100]
0x17900 add  r3, r3, #1                ; a per-instance counter
```

**The vendor's SR descriptor fill announces its work with `pcie_msg_send(chip, 3)`.**

## Why this matters: it verifies the port

`omo_h2d_send` has sent **id 3** since phase 24d, chosen by following phase 20's
`pcie_msg_send(chip,3)`. This is the first independent confirmation from the *code that performs the
SR post itself*: the fill routine ends by announcing **id 3**. So the port's announce id is **not** a
guess that happened to match - it is the same id the vendor uses at the same point in the same
operation.

That closes the "wrong announce id" hypothesis class. Together with the earlier findings, the port now
matches the vendor on:

| aspect | vendor | port | match |
| --- | --- | --- | --- |
| SR node layout | word0 = buf devva, word1 = (len<<16)\|flag | same | yes |
| producer commit | packed index to SR+0x18 | same (`0x400`) | yes |
| announce id | **3** | **3** | yes |
| announce registers | `out[0]` bitmap + `out[2]` bit 0 | same | yes |
| descriptor consumption | device reads ch0 (rptr == wptr) | measured | yes |

## The one structural difference this leaves

The vendor announces **inside the fill** (`pcie_msg_send` is called as the last act of filling a
descriptor), while the port fills everything, enables, and announces **once, later** - after its poll
window and after the mailbox service. So the remaining host-side difference is not *what* is sent or on
which register, but **when**: per-descriptor announce versus one announce after the fact.

That is a testable difference with no new register and no new constant: `omo_sr_post` could announce id
3 immediately after the commit, before the enable and before the release, and the existing fine poll
would show whether the device consumes the bit at that point.

## Honest scope

This verifies the port's announce path against the vendor's own SR fill; it does not make the device
accept the message. But it removes the last *value* hypothesis on the host side, leaving only the
**ordering/state** hypothesis - and unlike the register candidates, that one has direct code evidence
behind it rather than an inference from silence.
