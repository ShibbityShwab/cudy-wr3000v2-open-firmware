# The dialogue's two endpoints, and a blocking wait (phase 24p, 2026-10-02)

Continuing into `hi5622v100_plat.ko` with the symbol disassembler. Two handlers sit at the ends of the
host/device handshake, and one of them shows the vendor's init **blocks**.

## `device_plat_ready_msg_process` @ 0xeb78 - the host's handler for the device's "plat ready"

```
0xeb7c ldr  r3, [r0, #0x118]      ; the message object
0xeb80 cmp  r3, #0
0xeb84 movweq r4, #0x8b2d         ; an error code when absent
0xeb94 ldrb r4, [r3, #1]
0xeb98 lsrs r4, r4, #4            ; the top nibble must be zero
0xeba0 beq  0xebbc                ; ...and then the success path runs
0xebbc printk(.LC52)
0xebc8 ldr  r0, [pc, #4]
0xebcc bl   complete              ; <-- COMPLETE()
```

**It calls `complete()`.** That is a Linux completion, so the vendor's initialisation path *waits* on
the device announcing it is ready, and is released when this handler runs. Two consequences worth
stating:

1. In a takeover the firmware never emits the id-1 word, so that completion would never fire - which is
   exactly why a takeover cannot follow the vendor's init sequence to the point where it is released.
   Our runs have never seen `out[1] = 0x01`: the first change is `0x00 -> 0x40` (id 6) at +25 ms, and
   the poll now starts at the instant of the release, so this is not a sampling artefact.
2. It means **id 1 is load-bearing for the vendor's own flow**, not incidental - the init is gated on it.

## `host_ready_msg_process` @ 0xebd8 - the device's handler for the host's ready message

```
0xebf8 ldrh r6, [r5, #4]          ; a 16-bit length field
0xec00 sub  r8, r6, #0xc          ; payload length = len - 12
0xec04 tst  r1, #0xf0             ; byte[1]'s top nibble must be zero
0xec28 ldrb r7, [r5, #9]          ; the same +9 counter seen in hcc_msg_tx_to_core
0xec30 sub  r6, r6, #0xd
0xec48 cmp  r6, #0x62
0xec4c bhi  <error>               ; length bound: len-13 <= 0x62
0xec7c bl   memcpy_s              ; copy the payload
```

The payload begins at message `+0xc` and is copied into a **per-entity buffer** at
`.LANCHOR0 + 0x1a8 + 100*idx`, where `idx` is the message's top nibble - so the device's ready message
carries a per-entity capability/parameter block, bounded, and the driver stores it for later use.

This is the shape of the H2D command the port would eventually need to construct: a header (length at
+4, a zero top nibble at +1, the +9 counter) and a payload with a checked length. It is **not** a bare
message-id bitmap, which corroborates `hcc-command-layer.md`: the bitmap announces, the body travels
elsewhere (the SR ring), and the body has structure.

## What this changes about the port

Nothing in the port writes a message *body* with this structure. `omo_sr_post` posts the vendor's
captured first SR frame (72 B) and the alg frame, which were live captures - but there is no evidence
yet that either is the `host_ready` body, and the handler above shows the device validates that body
(length bound, zero top nibble, counter). A command whose body does not match is plausibly **silently
dropped**, which would look exactly like the "device does not accept" results already recorded.

So the next two questions are now specific and answerable from this file:

- **what does the driver put in the `host_ready` body** (the caller that fills a message and calls
  `hcc_msg_tx_to_core` with the ready id); and
- **which id bitmap bit corresponds to it**, since `pcie_msg_send(chip, id)` and the queue are keyed on
  the same type.
