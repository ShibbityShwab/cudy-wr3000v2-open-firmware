# The TX path's device-facing half is entirely in wifi.ko (phase 24y, 2026-10-03)

Continuing the chain from `tx-credit-source.md`: plat.ko's transmit path terminates in a callback, so
this phase asked who provides it. The answer is complete.

## `bal_port_start_xfer` @ 0x10b78 - where plat.ko's TX actually goes

```
0x10b98 movw r3, #0 ; .LANCHOR1
0x10ba0 ldr  r2, [r3, #0x14]     ; the same object the credit comes from
0x10ba4 ldr  r2, [r2, #0x18]     ; its +0x18 handler
0x10bb8 ldr  r0, [r3, #0x18]
0x10bbc bx   r2                  ; TAIL CALL - plat.ko never touches the device here
```

So `hcc_queue_tx_process` -> credit check -> dequeue -> `bal_port_start_xfer` -> **a chip-layer
function**. plat.ko's TX path contains no device access at all: it queues, checks credits, and hands the
batch over.

## Who registers those callbacks

`wifi.ko` imports 138 symbols from `plat.ko`, including `hcc_queue_register_customer_for_chip`,
`hcc_queue_register_customer_for_core`, `hcc_msg_register_tab_chip` and
`hcc_msg_register_tab_core`. Its `hdpp_main_init` @ 0x12e0 calls them:

```
0x1334 bl hcc_queue_register_customer_for_chip(2, hdpp_hcc_sync_read_lock, hdpp_hcc_sync_read_unlock)
0x134c bl hcc_queue_register_customer_for_core(0, hdpp_hcc_sync_read_lock, hdpp_hcc_sync_read_unlock)
0x135c bl hcc_msg_register_tab_chip(3, &table@.LANCHOR0+4,  5)    ; FIVE handlers
0x136c bl hcc_msg_register_tab_core(0, &table@.LANCHOR0+0x40, 7)  ; SEVEN handlers
```

and `hcc_queue_register_customer_for_chip` itself is a plain store into a per-index entry
(`0x12538 str r2, [entry, #0x38]` / `0x1253c str r1, [entry, #0x3c]`).

## The conclusion, now complete

**plat.ko is a shim for TX.** It owns the queue, the retry counter and the credit check; everything that
actually reaches the device - the transfer, and the message handler tables - is registered by
`wifi.ko` at runtime through these four entry points.

That closes the question this phase asked and completes the picture:

> **The port's H2D path is a hand-reimplementation of a `wifi.ko` function that has never been read.**
> `omo_sr_post` + `out[0]`/`out[2]` reproduces *what the port believes* the chip layer does. The chip
> layer's actual transfer routine - reached as object `+0x18` from `bal_port_start_xfer` - is the only
> remaining unknown on the host side, and its family is named (`hdpp_*`, `bal_*` in `wifi.ko`).

This also explains, without any further hypothesis, why the port's notifications are ignored while
phases 20/22 measured a device-side gate: the port and the vendor are not doing the same thing on the
wire, and **the difference is not a register value - it is an unread function.**

## What to read next, precisely

The object whose `+0x18` `bal_port_start_xfer` calls, and the `hdpp_*` routine behind it. `wifi.ko` is
21,495 symbols and the callback is bound dynamically, so the route is: the `hdpp_` call graph around
`hdpp_main_init` and the `bal_` accessors it installs, looking for the function that writes `out[0]`
(the `0x40039010` window) and the doorbell - i.e. the function the port's `omo_h2d_send` is a
substitute for.

## A negative that narrows the search

Scanning `wifi.ko`'s `.text` for every one of the mailbox CAs built as an immediate pair
(`movw`/`movt`: `0x40039010`, `0x40039014`, `0x400392d4`, `0x400392f0`, `0x40101438`,
`0x40101414`, `0x40000108`) returns **zero hits** - 875k instructions of ARM, not one of those
addresses.

That is a useful result rather than a dead end: **the chip layer never hardcodes the mailbox
registers.** It receives them as pointers - which is exactly the shape phase 20 found in
`pcie_msg_send` (`ldr r2, [comm, #0x2c]` then a store, rather than an address). So `wifi.ko`'s
transfer routine cannot be located by searching for the register constants; it has to be found by
following the object it is handed, which is why the chain has to go through the registration
(`hcc_queue_register_customer_for_chip` / `hcc_msg_register_tab_chip`) rather than by address.

**This also bounds what "reimplementing the unread function" means.** The port's `omo_h2d_send` writes
`out[0]` and `out[2]` through windows it resolved itself; the vendor's chip layer writes the same two
registers through pointers it was given. The *addresses* are not the difference. Whatever the chip layer
does differently, it is in the **sequence and the state around those writes**, not in which register.

