# The host's message-handler table, and a correction to the record (phase 24q, 2026-10-02)

Dumping the `.data` handler table in `hi5622v100_plat.ko` (each entry 12 bytes: relocated handler
pointer, a constant `3`, and the message id) gives the host's id -> handler map:

| id | handler |
| --- | --- |
| 1 | `oam_rx_post_action_function` |
| **2** | `device_plat_ready_msg_process` |
| **3** | `host_ready_msg_process` |
| 4 | `exception_info_msg_process` |
| 0 | `heartbeat_msg_process` |

```
0x2664: <device_plat_ready_msg_process ...>   -- see table below
0x2678: 0x00000002
0x2684: 0x00000003
0x2690: 0x00000004
0x269c: 0x00000000
```

## The correction

**The device's "plat ready" message is id 2, not id 1.** `device_plat_ready_msg_process` - the handler
that calls `complete()` and releases the vendor's initialisation - is registered for **id 2**, and every
run in this session has received **id 2**:

```
t=25ms  out[1] 0x00000000 -> 0x00000040   bit 6 set (id 6)
t=525ms out[1] 0x00000040 -> 0x00000004   bit 2 set (id 2)
poll done: ... final out[1]=0x00000004
```

Phases 19/20 recorded "there is still **no id-1 'device plat ready' word**". That statement is wrong in
its id, and the conclusion drawn from it - that the device never announces readiness in a takeover - is
the opposite of what the mailbox shows. **The device does announce plat-ready; it is the persistent
`0x04` bit that this project has been reading (and clearing!) for many phases, under the label
"id 2".**

That also explains a detail the port logged without recognising it: its own dispatch line reads
`bit 2 pending -> dispatch handler[2]` - which is precisely `device_plat_ready_msg_process`.

## And the host's ready message is id 3

`host_ready_msg_process` is registered for **id 3**, and the port's H2D send used **`send=3`**
(phase 24d/24e, following phase 20's `pcie_msg_send(chip,3)`). So at the *id* level the port has been
sending the right message all along, and the device-side handler for it validates a **body**:

- a 16-bit length at message `+4`;
- a zero top nibble at `+1`;
- payload length `len-13 <= 0x62`;
- the payload copied from `+0xc`.

**So the handshake is id-correct and body-wrong.** That is a much sharper statement of the blocker than
"the device does not accept": the device is being told the right thing and is rejecting what came with
it. It matches `hcc-command-layer.md`: the bitmap announces, the body travels via the SR ring, and the
body has a validated structure the port's captured frames may not satisfy.

## The immediate next question

The port's SR post reuses the vendor's **captured** frames (a 72-byte message and a 298-byte alg frame).
Neither has been checked against `host_ready_msg_process`'s validation:

- does the captured frame's `+4` length equal its actual size + 12/13?
- is its `+1` top nibble zero?
- is it within `len-13 <= 0x62`?

Those are cheap to check **offline** against the bytes the port posts, and if one fails, the device would
drop the message silently - which is exactly the observed behaviour.
