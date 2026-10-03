# Where the TX credit comes from: the vendor's chip layer (phase 24v, 2026-10-02)

Following `tx-credit-flow-control.md`'s callback to its origin.

## The chain, as far as plat.ko can show it

`bal_init` @ 0x109e8 builds a **per-port array** of 0x14-byte entries anchored at `.LANCHOR1`:

```
0x10a2c mla  r4, r4(=0x14), r5(port), r8(.LANCHOR1)   ; entry = &array[port]
0x10a30 ldr  r3, [r6, #0x3c]                           ; r6 = .LANCHOR0
0x10a38 str  r3, [r4, #0x14]                           ; entry+0x14 = that object
0x10a58 ldr  r3, [r4, #0x14]
0x10a5c ldr  r3, [r3, #8]                              ; the object's +0x8 callback
0x10a68 blx  r3                                        ; call it
0x10a6c str  r0, [r4, #0x18]                           ; store the result
```

So `.LANCHOR1 + 0x14` - the pointer `bal_get_port_info` dereferences and then calls `+0x1c` on - **is port
0's entry field**, which `bal_init` fills from **`.LANCHOR0 + 0x3c`**. And `bal_init` itself only ever
*calls into* that object; it never constructs it.

`.LANCHOR0 + 0x3c` is therefore populated by **the chip layer** - i.e. by `hi5622v100_wifi.ko`'s
registration, at runtime, in a normal vendor boot.

## What that means, stated plainly

**The device-granted TX credit is provided by the vendor's chip-layer stack.** It is not a register in
`plat.ko`, not a constant, and not something the protocol layer owns - it arrives through a callback
that the chip layer installs when the vendor driver initialises.

A takeover **never runs that chip layer** (it is exactly the stack the port replaces), so that callback
is never installed and the credit never exists. That is a complete explanation of why the port cannot
make the device consume its H2D notification - and, importantly, it is not a defect in the port:

> The vendor's own `hcc_queue_tx_process` refuses to transmit when `hcc_queue_get_tx_buf_num` returns
> zero, and that value comes from a callback the chip layer installs. **In a takeover, the vendor's
> driver would refuse to send for exactly the same reason the port cannot.**

This is the cleanest form of the phase 20/22 conclusion the whole investigation has been converging on,
and it now has a mechanism attached: eight host-side register candidates failed because none of them is
the missing thing. The missing thing is the chip layer's credit source.

## The two remaining directions, both outside host-side register work

1. **Find the registration in `hi5622v100_wifi.ko`** - what writes `.LANCHOR0+0x3c`, and what the
   `+8` / `+0x1c` callbacks read. If the credit is derived from a **device register the port could
   read**, then even *observing* it converts "the device does not consume the bit" into "the device has
   granted zero TX buffers" - a far more specific and checkable statement, and it would settle whether
   the zero is a device-side refusal or an artefact of the missing stack.
2. **The vendor's SDL/GPL source request**, which answers a device-side gate that no host-side work has
   moved.

Both are honest next steps; neither is another register write, and that is the point of this phase.
