# H2D send on correct registers: the host lands it, the device does not accept (phase 24e, 2026-10-02)

Evidence `build/register-dumps/exp/20261002-202218/`, `WIFIDRV1 RESULT: PASS`,
`params=[hw=1 program=1 fw=1 release=1 msgsvc=1 send=3]`.

## The result

```
omo-drv1: ---- H2D send: id 3 (bitmap 0x00000008) via out[0] + doorbell out[2] ----
omo-drv1:   NOTE out[5] (CA 0x400392f0) is deliberately NOT written (project rule)
omo-drv1:   out[0] 0x3f1010 0x00000000 -> 0x00000008 readback=0x00000008 match=YES
omo-drv1:   out[2] 0x3f12d4 0x00000000 -> 0x00000001 readback=0x00000000 match=NO (doorbell)
omo-drv1:   observing 6000 ms for a response...
omo-drv1:   send observation done: final out[0]=0x00000008 out[1]=0x00000000 glue=0x00000000
omo-drv1:   NOTE the sent bit is STILL SET - the device did not consume it
```

Three distinct facts, each verified by readback:

1. **The host write lands.** `out[0]` takes the id-3 bitmap bit and reads it back (`match=YES`).
2. **The doorbell is consumed by the endpoint hardware.** `out[2] <= 1` reads back **0** immediately -
   self-clearing, exactly as phase 20 recorded for the doorbell.
3. **The device does not accept.** The sent bit stays `0x00000008` for the whole 6 s window, `out[1]`
   stays 0 and the glue status stays 0 - no response of any kind.

## What this settles, and the caveat that limits it

**Settled:** this reproduces phase 20's `fw-accept` finding **on registers verified correct against a
live vendor boot** - so that negative was not an artefact of the address bug. The entire
address-error hypothesis class is closed for this experiment: the host genuinely posts the message, the
doorbell genuinely reaches the hardware, and the device genuinely does not take it.

**The caveat, stated plainly because it bounds the conclusion:** the vendor's other send helper,
`pcie_msg_send_irq`, additionally writes `8` to `out[5]` (CA `0x400392f0`) - forbidden by this project
because writing it hangs the chip (phase 20 BOOT B). This module omits that arm by rule. So a "device
does not accept" result here **cannot distinguish** between:

- (a) the accept gate is device-side state a takeover cannot stand up (phase 20/22's conclusion), and
- (b) the device requires the `out[5]` arm specifically.

Phase 20's BOOT B did test the `out[5]` arm in the dual-RC configuration and saw it accepted but
**produce no accept either**, which favours (a) - but the arm was written in a different configuration
from this run, so (b) is not formally excluded here.

## Where this leaves the port

The host side of the message service is now demonstrated working end to end on correct registers:
claim, viewports, rings, firmware load, release with a 9/9 CPU-start signature, first-word read,
ack/clear/re-arm/dispatch, and a host->device send. Every one of those is readback-verified.

The device's H2D accept gate remains closed, and closing it is the same problem phases 20 and 22
identified: standing up whatever device-side state the firmware's dispatcher requires, or a
device-side trace of what gates it. That is beyond what host register writes can establish - and this
run is the clean evidence for that statement, because the host writes are now known to land.
