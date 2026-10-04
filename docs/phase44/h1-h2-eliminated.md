# H1 and H2 are eliminated: the DR channels are now programmed exactly like the vendor's, and the deposits still do not come (phase 44, 2026-10-04)

Evidence `build/register-dumps/exp/20261004-140402/`,
`params=[... srpost=1 drpost=1 d2hsvc=1 hccpost=1 msgb=1 drparse=1 drdump=1 dren=1]`.

## The measurement

```
DR ch3 +0x00 enable [0x590] <= 0x1 readback=0x1 match=YES   (same on ch4/5/6, pre-release)
[dren] DR ch3 post-release: +0x00=0x1 +0x48=0x1 +0x30=0x83d24000 +0x34=0x1f
[dren] DR ch4 post-release: +0x00=0x1 +0x48=0x1 +0x30=0x835eb000 +0x34=0x1f
[dren] DR ch5 post-release: +0x00=0x1 +0x48=0x1 +0x30=0x83e8b000 +0x34=0x1f
[dren] DR ch6 post-release: +0x00=0x1 +0x48=0x1 +0x30=0x83d32000 +0x34=0x1f
ring watch done: 0 DR deposit events, 0 SR consumption events in 4000 ms
```

## What this settles

1. **H1 (the missing DR per-channel enable) is eliminated.**  The port set +0x00 bit 0 on all four
   DR channels pre-release, every write verified by readback, and the value held through the boot.
2. **H2 (the firmware overwriting the program group) is eliminated.**  Post-release, +0x30 still
   holds each port devva and +0x34 the port's depth - the firmware did not touch the host-DRAM
   program group.
3. **The DR channel register file is now provably identical to the vendor's live state**
   (+0x00=1, +0x48=1, +0x30=host base, +0x34=depth-1 - matching reg_all.txt's seven live blocks
   word-for-word).

The deposits still do not come.  The channel programming is complete and correct, and the absence
of deposits is therefore unambiguously the device-internal message service - the mailbox -> line
0x4C delivery proven unreachable and unobservable from the host (phases 31-35, 42).  Every
host-side precondition named by the verified maps (frame, servicing, channel registers) is now
satisfied to the vendor's own values.

## What remains

The gate is device-internal.  The only instruments that can open it are the ones already named: a
device-side trace (no hardware available) or the vendor source (unresponsive).  On the host side,
the ring-protocol build-out is as complete as this environment can make it.

