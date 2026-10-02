# The HCC command layer is in plat.ko, and it explains the H2D send (phase 24o, 2026-10-02)

Following the frontier from `both-device-leads-negative.md` - the command dialogue rather than another
host register - into the vendor modules themselves.

## Where the command code lives

| module | size | `hcc_msg_tx` / `hcc_msg_tx_to_core` / `hcc_msg_alloc` |
| --- | --- | --- |
| `hi5622v100_wifi.ko` | 3,564,728 B, 21,495 symbols | **imported only** (all `SHN_UNDEF`) |
| `hi5622v100_plat.ko` | 364,660 B, 2,921 symbols | **defined** |

So the layer that builds and sends device commands is in **`plat.ko`** - the module this project has
already disassembled extensively (`docs/phase20/`) - not in the large `wifi.ko`. That is a useful
result on its own: the next RE target is a file whose map is already partly built.

## What `hcc_msg_tx_to_core` does

```
0x11b2c push {r4, lr}
0x11b48 ldr  r2, [r4, #0x118]        ; the host's message object
0x11b54 ldrb r1, [r2, #1]
0x11b58 ubfx r3, r1, #0, #4          ; a 4-bit field
0x11b5c and  r1, r1, #0xf            ; and the other nibble
0x11b60 add  r3, r3, #1
0x11b68 strb r3, [r2, #9]            ; +9 = a counter...
0x11b6c cmp  r3, #2
0x11b70 bgt  <error path>            ; ...that bails above 2
0x11b74 bl   hcc_queue_get_by_msg    ; pick a queue by message type
0x11b90 bl   hcc_queue_add_msg       ; enqueue
```

Two things follow, and both matter for the H2D work already done:

1. **`hcc_msg_tx_to_core` only queues.** The send to the device happens later, when a worker drains
   the queue - which is where `pcie_msg_send(chip, id)` (the `out[0]` bitmap + `out[2]` doorbell) is
   finally called.
2. **The id bitmap is a notification, not the message.** `pcie_msg_send` writes a *bit* saying "there
   is a message of type N waiting"; the message body travels through the **SR ring**. That reframes the
   H2D result from phase 24e: sending `id 3` with the `out[0]`/`out[2]` pair announces a message that
   the device then looks for in the SR ring - and the device not consuming the bit is consistent with it
   not finding, or not accepting, what was posted there.
3. There is a **retry counter** at message `+9`, limit 2, incremented before each queue attempt - so the
   vendor's command path is retry-tolerant in a way the port's single-shot send is not.

## Honest scope

This locates the command dialogue and corrects how the H2D send should be understood; it does not yet
produce a working command. The specific next questions the disassembly makes askable are:

- **which SR-ring message body corresponds to which id bitmap bit** (i.e. what a device-acceptable
  id-1 / id-3 message actually contains), and
- **what the queue worker does between dequeuing and `pcie_msg_send`** - since that gap is where a
  takeover could plausibly be missing state.

Both are answerable from `plat.ko`, which is in hand with a working disassembler
(`build/tmp/dis3.py`, written today because `disas2.py` trips on a string `st_shndx` in the
`wifi.ko` symtab).
