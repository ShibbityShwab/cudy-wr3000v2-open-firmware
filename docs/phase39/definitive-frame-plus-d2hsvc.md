# The definitive frame + the vendor-exact D2H servicing: first run (phase 39, 2026-10-04)

Evidence `build/register-dumps/exp/20261004-123758/`,
`params=[... srpost=1 msgsvc=1 pollms=25 polldur=4000 fwctx=1 intrsamp=1 ep1db=1 irqwin=1 ackfast=1 d2hsvc=1 hccpost=1 msgb=1]`.

## What ran

1. **Message B posted** - the verified boot announce body (72 B, HCC group 0 / id 1, byte-exact
   from `docs/phase38/announce-body-bytes.md`) as the first SR node's body.
2. **d2hsvc=1** - the D2H service routine performing the verified host-side maths when out[1]
   changes.

## The dialogue

```
t=0     out[1] = 0x40            (bit 6, the id-6 trigger, already set at release)
t=275ms out[1] 0x40 -> 0x04      (bit 6 cleared, bit 2 set - device-ready)
t=300ms out[1] 0x04 -> 0x00      (the PORT's service: ack 0x40101438=1, clear, re-arm 0x40101414=1)
```

```
[d2hsvc] ack=1 clear re-arm=1 for id 2
[d2hsvc] id 2: UNREGISTERED - the vendor's 0x1739c path drops it
```

## The result

**The D2H service routine works exactly as the verified map specifies.**  The port performed the
vendor's own ack/clear/re-arm sequence on the device-ready bit for the first time, and the
UNREGISTERED log for id 2 is precisely the vendor's own behaviour (the bare mailbox bit 2 has no
handler; the real handshake completes on the HCC layer, phase37 section 3.2).

**The dialogue itself is unchanged** - message B elicits the same bit-6 -> bit-2 exchange as every
earlier body.  That is the expected result: the phase-25 record already established the SR body
content is not the trigger (the announce is), and the phase-36 map established the mailbox dialogue
is boot-only.  The frame's correctness matters for what the device's HCC receiver does with it, not
for the mailbox words.

## What this unblocks

The next protocol piece is the HCC-layer handshake: the device-ready completion
(`device_plat_ready_msg_process`, group 4 id 1) arrives as a **DR ring payload**, not a mailbox
word.  The port now has the frame and the servicing maths; the missing piece is the **DR
receive/drain path with HCC payload parsing** - read the DR descriptors the device fills, parse the
12-byte HCC header, and recognise group-4 id 1.

