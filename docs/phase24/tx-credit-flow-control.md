# What the vendor's queue does that the port skips: device-granted TX credits (phase 24u, 2026-10-02)

The last host-reachable place a real difference could hide is between `hcc_queue_add_msg` and
`pcie_msg_send`. It is there, and it is **flow control**.

## `hcc_queue_tx_process` @ 0x1128c - the worker

```
0x112b8 bl   bal_get_port_res          ; the port object for this queue
0x112c0 beq  <return 0>                ; no port -> no send
0x112cc bl   hcc_queue_get_tx_buf_num  ; <-- THE CREDIT CHECK
0x112d0 subs r7, r0, #0
0x112d4 bne  0x112fc                   ; only proceed if credits are AVAILABLE
0x112d8 mov  r0, #0                    ; otherwise return 0 and send nothing
0x112fc ...  _raw_spin_lock_irqsave    ; dequeue from the linked list
```

**The worker refuses to transmit when there are no credits.** Then it dequeues under a spinlock and
sends.

## `hcc_queue_get_tx_buf_num` @ 0x111ec - the credit itself

```
0x1121c ldr  r0, [r0, #4]        ; the queue's counter
0x11244 bl   bal_get_port_info   ; <-- the PORT's credit, from the device's side of the stack
0x1124c ldr  r2, [r4, #4]
0x1124e ldrh r3, [sp, #2]        ; a 16-bit field out of the port info
0x11250 ldrh r0, [r2, #4]        ; the queue's 16-bit count
0x11254 cmp  r0, r3
0x11258 movhs r0, r3             ; credits = min(queue count, port count)
```

The available TX buffers are the **minimum of the queue's own count and the port's reported count**, and
the port's count comes from `bal_get_port_info` - a **tail call through a registered function
pointer** (`.LANCHOR1 + 0x14`, field `+0x1c`), i.e. a callback the chip layer (`wifi.ko`) installs at
runtime, not a constant in `plat.ko`.

## Why this is the answer to the question asked

The port's H2D path is:

```
omo_sr_post()   -> posts the ring, commits the index, enables the channel
omo_h2d_send()  -> out[0] |= (1 << id);  out[2] |= 1        ; and nothing else
```

It rings the doorbell **with no credit check at all**. The vendor's path, in contrast:

1. builds a message;
2. **queues** it (`hcc_queue_add_msg`, retry counter at `+9`, limit 2);
3. a worker dequeues it **only when `hcc_queue_get_tx_buf_num` says the device has granted buffers**;
4. and only then calls `pcie_msg_send`.

So the port has been announcing a host message **without any TX buffer having been granted by the
device** - which is a *structural* difference, not a register value, and it is consistent with every
"the device does not consume the bit" result in this project, including the eight candidates eliminated
in `desclen-not-the-gate.md`. Ringing the doorbell while the device has granted no buffers is exactly
the case the vendor's own code refuses to act on.

## What this does and does not settle

**Does:** it answers the question this phase set, and it retires the last host-reachable hypothesis
class. The port is not missing a register write; it is missing the vendor's **flow control**, whose
credit originates on the device's side of the stack (via a callback `wifi.ko` installs).

**Does not:** produce a working send. Two ways forward remain, and both are outside the "write another
host register" loop that has now failed eight times:

1. **Find the credit's actual source in `wifi.ko`** - the callback behind `.LANCHOR1+0x14, +0x1c` - and
   determine whether it reads a device register the port could read (read-only) to *observe* the credit.
   Even observing it would convert "the device does not consume the bit" into "the device has granted
   zero buffers", which is a far more specific statement.
2. **The vendor's SDL/GPL source request**, which answers a device-side gate that eight host-side
   candidates could not move.

An important consistency check this gives for free: if the credit is device-granted and the device
grants none in a takeover, then **the vendor's own driver could not send either** in this state. That
matches the phase 20/22 conclusion that the gate is device-side - and it means the port's inability to
send is not a port defect.
