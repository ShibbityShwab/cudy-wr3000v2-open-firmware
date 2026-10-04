# The DR buffers stay zero: the device signals but never deposits (phase 40, 2026-10-04)

Evidence `build/register-dumps/exp/20261004-125119/`,
`params=[... srpost=1 drpost=1 msgsvc=1 d2hsvc=1 hccpost=1 msgb=1 drparse=1 drdump=1]`.

## The buffer-content measurement

```
[drdump] ch3 node0 w0=0x84f50000 w1=0x00000000 pay=00 00 ... 00 (16 zero bytes)
[drdump] ch4 node0 w0=0x84f60000 w1=0x00000000 pay=00 00 ... 00
[drdump] ch5 node0 w0=0x84f80000 w1=0x00000000 pay=00 00 ... 00
[drdump] ch6 node0 w0=0x84fa0000 w1=0x00000000 pay=00 00 ... 00
ring watch done: 0 DR deposit events, 0 SR consumption events
```

All four DR payload buffers are **still zero** after the boot dialogue - the device wrote nothing
into the port's receive buffers, at boot or during the watch.  The node words are the port's own
posted descriptors.  This closes the timing question from the previous run with a content
measurement: **the device never deposits in a takeover**, not even the device-ready message
(group 4 id 1) that the mailbox bit 2 announces.

## Why this is the SAME gate, not a new mystery

The device's boot path signals the dialogue (bits 6/2) without the message service.  The message
service - the worker that produces the DR deposits - waits on the mailbox interrupt, line 0x4C,
which the phase-35 ledger proved never fires in a takeover and the phase-34 run proved
PCIe-unreachable.  The DR absence is a CONSEQUENCE of that gate, consistent with every prior
measurement.

## The host-side boot handshake is now complete and correct

The port posts the definitive frame (message B), announces id 3, and services the mailbox with the
vendor's exact ack/clear/re-arm maths (verified working, phase 39).  Everything the host can do
for the dialogue is done, and done to the verified specification.

## The next self-contained lane

The web survey (phase 38) located GPLv2 sibling trees: OpenHarmony hi3881v100 (HCC) and the
hi1105 PCIe tree with `pcie_firmware_msg.c` (the H2D/D2H message ring).  The siblings may reveal
**what enables the mailbox doorbell -> interrupt forwarding** on this family - the register the
luofu blob's own init calls (0x7c0/0x78e with the mailbox base 0x40039000) configure.  Mining the
sibling sources for that enable register is the next lane, and a hit would give the port a NEW
register to try live - the first new lead the gate has had since phase 31.

