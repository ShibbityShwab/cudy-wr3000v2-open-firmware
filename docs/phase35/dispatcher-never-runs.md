# The dispatcher never runs - proven at 100us, and the host-side space is exhausted (phase 35, 2026-10-04)

Evidence `build/register-dumps/exp/20261004-062313/`, `params=[... fwctx=1 intrsamp=1 ep1db=1 irqwin=1 ackfast=1]`.

## The ackfast measurement

```
[ackfast] start:  out5=0x00000000 out0=0x00000008        (announce pending from pre-release)
[ackfast] CHANGE iter 0 (t<0.1ms): out5 unchanged (0), out0 0x8 -> 0x0
[ackfast] done:   out5=0x00000000 out0=0x00000000 changes=1
```

Sampled at 100us for 500ms, wrapping the exact moment the announce word was consumed: **the
firmware's ack register never pulsed**.  The dispatcher (whose ack signature is a write of 1 to
`0x400392f0`) genuinely never runs in a takeover - this is now a measurement, not an inference
from a 25ms poll.

## The consumption is a boot reset, not a message read

The announce word cleared within 100us of the release.  Combined with everything else on record
(phase 25: the consumption happens even when the mailbox is never rung; the firmware's boot path
calls mailbox-init helpers `0x7c0`/`0x78e` with the base `0x40039000` at file `0x86f3e`/`0x86f44`),
the reading is: **the boot sequence resets the message block**, which clears out[0]; the announce is
never read as a message by the dispatcher.  Nothing about the consumption required the dispatcher.

## The host-side space, now exhaustively measured

1. doorbell values 1 and 8, EP0 and EP1, pre- and post-release: consumed by the mailbox hardware
   (readback 0), no dispatch (phase 31, 31b, 33).
2. the interrupt block (pending 0x4016010C, enables 0x40161100): PCIe-unreachable on BOTH
   endpoints - a spare iATU viewport targeting 0x40160000 returns the no-decode signature
   0xffffffff (phase 34).
3. the ack at 100us across the consumption moment: never pulses (this phase).
4. the vendor host's init of the interrupt block: does not exist - neither plat.ko nor wifi.ko
   references 0x4016xxxx (phase 31 scans).
5. the ETE intr mask the vendor writes (0xffe0f8f8 -> 0x3F201818) is byte-identical to the port's.

**The gate is the device-internal forwarding from the mailbox doorbell to firmware interrupt line
0x4C.  It is unreachable from the host by every mechanism available, and unobservable by every
window that exists.**

## What remains (instruments, not hypotheses)

- A device-side trace (JTAG / ROM-monitor): observes line 0x4C and the mailbox forwarding directly.
- The vendor source for the mailbox/interrupt block (the request the human has set aside).
- Everything else the host could do has been done and is recorded.

