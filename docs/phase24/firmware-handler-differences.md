# The firmware's own message handler, and two differences from the host's (phase 24m, 2026-10-02)

Reading the Wi-Fi firmware's own `pcie_msg_handle` (`build/tmp/fwaccept/fw_disasm.txt`, ARM Thumb,
file `0x818a8`) turns up two things the host-side disassembly does not show.

## The firmware's handler

```
818b2  movs r7, #1
818b4  movs r1, #0
818b6  ldr  r2, [r0, #0xc]
818b8  str  r7, [r2]        ; *(ctx+0xc)  = 1     <- the ack, value 1
818ba  ldr  r2, [r0, #4]
818bc  ldr  r5, [r2]        ; pending = *(ctx+4)
818be  str  r1, [r2]        ; *(ctx+4)    = 0     <- the clear
818c0  movs r1, #8
818c2  ldr  r2, [r0, #0x10]
818c4  str  r1, [r2]        ; *(ctx+0x10) = 8     <- the third write, value 8
818ca  ...                  ; 31 - clz(r5 & -r5) = LOWEST set bit, then dispatch handler[bit]
```

The dispatch shape matches the host's exactly (lowest set bit, `cmp #9`, table at `ctx+0x20`, clear the
dispatched bit, loop), which is what makes the two differences stand out:

## Difference 1: the third write is 8 on the device, 1 on the host

`docs/phase20/msg-host-half.md` recovered the HOST's sequence as `*(ctx+0xc)=1, *(ctx+4)=0,
*(ctx+0x10)=1` - and the port has written **1** to `out[4]` (CA `0x40101414`) since phase 24c. The
firmware writes **8** to its third register. The two sides of the same protocol do not use the same
value, and the reason is unresolved: it could be a different register, or a bitmask where the host only
needs bit 0.

It is now a parameter (`rearm_val`, default 1) so the value can be tested without a rebuild each time.

## Difference 2: the firmware's ack register is `0x400392f0` (`out[5]`)

The header recorded during the firmware analysis names the firmware's ack as CA `0x400392f0` and its
third write as CA `0x400392d4` - i.e. `out[5]` and `out[2]`, **not** the `out[3]`/`out[4]` pair the host
side uses. If the firmware acknowledges the host at `out[5]`, then every poll in this project has been
blind to the firmware's own acknowledgement, because no run ever read `out[5]`.

Reading it is explicitly allowed (the project rule forbids only *writing* CA `0x400392f0`). The mailbox
poll now reads `out[5]` alongside `out[0]`/`out[1]` and reports any change it sees.

## Why this is worth a run

Both differences are **specific, testable, and derived from device code rather than inference**:

- if the third write must be `8`, then every service this project has performed sent a value the device
  did not expect - and the dialogue never advanced because of it;
- if the firmware acknowledges at `out[5]`, then the dialogue may have been advancing all along and the
  host simply never looked at the register carrying the answer.

Either would be a better explanation of the silence than anything the host-side readings have produced.
