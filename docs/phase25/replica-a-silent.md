# Replica A is silent, and replica B is on the device (phase 25w, 2026-10-03)

Evidence `build/register-dumps/exp/20261003-095907/`, `params=[srpost=1 msgsvc=1 pollms=25]`.

## Replica A: negative, and identical to every mix

The frame was made a **true copy of message A** - length 72, id 29, zero body, zero tail - the first time
any real vendor message has been put on the ring intact. The result:

```
ANNOUNCE out[0] <= 0x00000008 (x3, pre-release)
t=0     out[0]=0x00000008 out[1]=0x00000000
t=25ms  out[0] 0x8 -> 0x0        (consumed)
t=25ms  out[1] bit 6 set (id 6)
t=550ms out[1] 0x40 -> 0x04      (id 2)
poll done: 2 transitions in 4000 ms
```

**Identical to every earlier variant**, mixes included. So A is eliminated - and note what that means
carefully: A's elimination is as informative as the header eliminations, because A is not a guess. It is
the vendor's own message, byte-for-byte, and the firmware does not respond to it either.

## Replica B: the one that matters, now on the device

Message B is the vendor's **first** frame on a fresh SR ring, and it is a **different length and id** than
A: `+0x04 = 0x0030` (48), `+0x06 = 0x0001` (1), body `{1, 0x001400d8, 1, 0,0,0}`, `0xff` from `+0x2c`.

```
0x00,0x01,0x00,0x04, 0x30,0x00,0x01,0x00,     proto 0x04000100, len 0x0030, id 0x0001
0x00,0x00,0x5a,0x5a, 0x00,0x00,0x00,0x00,     +0x08 0, magic 0x5a5a, token 0
0x01,0x00,0x00,0x00, 0xd8,0x00,0x14,0x00,     +0x10 1, +0x14 0x001400d8
0x01,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,     +0x18 1, +0x1c 0
... zeros to +0x2b, then 0xff from +0x2c to +0x47
```

**The SR descriptor still announces 72** while the message inside declares 48 - which is exactly the
vendor's own arrangement (its descriptor says `0x48`, its message says `0x30`), so this replica
reproduces that apparent mismatch deliberately rather than papering over it.

## What the pair now decides

| test | frame | result |
| --- | --- | --- |
| mixes (phases 24-25) | A's header + B's body | silent |
| **replica A** | len 72, id 29, zero body | **silent** |
| replica B | len 48, id 1, recorded body | running |

If **B** also produces nothing, then no vendor SR message of this type elicits a response at this point -
and the honest conclusion is not "our bytes are still wrong" but **"the firmware is not waiting for an SR
message of this type here"**. That would move the question from *what bytes* to *when* (the announce
timing that phase 25 established) or *which channel* (DR rather than SR), which are different experiments
rather than more edits to the same frame.

