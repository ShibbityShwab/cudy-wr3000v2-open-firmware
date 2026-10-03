# Reproduced: an announce pending at release is consumed (phase 25c, 2026-10-03)

Evidence `build/register-dumps/exp/20261003-023359/`,
`params=[... srpost=1 msgsvc=1 pollms=25]` - **the in-post announce with no post-release send at all.**

## The result

```
omo-drv1: ANNOUNCE (as sr_dscr_fill does) out[0] <= 0x00000008 readback=0x00000008
omo-drv1: ANNOUNCE out[2] <= 0x00000001 readback=0x00000000 (doorbell)
   (x3, per channel, BEFORE the release)
omo-drv1:   t=0  out[0]=0x00000008 out[1]=0x00000000 (baseline)     <- still set at release
omo-drv1:   t=25ms out[0] 0x00000008 -> 0x00000000                 <- CLEARED by the device
omo-drv1:     out[1] bit 6 set (id 6)
omo-drv1:   t=550ms out[1] 0x00000040 -> 0x00000004               <- id 2
omo-drv1:   poll done: 2 transitions
omo-drv1:     bit 2 pending -> dispatch handler[2]
```

**Reproduced, and with the post-release send removed entirely**, so the effect is unambiguously the
boot-time announce:

- the announce written **before** the release is **consumed**;
- made **after** the release it is never consumed (every run in phases 24d-24t: "STILL SET").

This is the first host→device write in this project that the device has ever taken.

## What it means, and what it does not

**The mechanism is now known:** the firmware reads and clears the message registers as part of its own
boot, so a host announcement must be **pending at the moment of release**. Every H2D send this project
made before this phase was posted *after* the firmware had already passed that point - which is exactly
why nine phases of "the device does not accept" never moved.

**But the dialogue is unchanged**: the firmware consumed the announce and still emits only id 6 (then
id 2), exactly as before. So consuming the announce is **necessary but not sufficient**. The firmware
takes the notification and then looks for the message **body** it announces - and the port's body
(the captured 72-byte frame and the alg frame, both now in the ring with the producer committed and the
channel enabled, and with ch0's descriptors measurably consumed) is evidently not what it accepts.

That is a much tighter statement than any previous one: **the remaining question is not timing, not the
id, not the register, not the credit - it is the message body.**

## Device state

`W=2 I=6 OFF=0 OMO=0 FAIL=0`, calibration `[SUCC]` both bands.
