# The port's own driver elicits the firmware's full dialogue (phase 24k, 2026-10-02)

Evidence `build/register-dumps/exp/20261002-214710/`, `WIFIDRV1 RESULT: PASS`,
`params=[hw=1 program=1 wr=1 fw=1 release=1 srpost=1 pollms=25 polldur=4000]`.

## The result

```
omo-drv1: SR ch0 payload 16384 bytes @ 0x84e74000 (devva 0x84e74000)
omo-drv1: SR ch0 base [0x0410] <= 0x83a01000 readback=0x83a01000 match=YES
omo-drv1: DR ch3 base [0x05c0] <= 0x839ee000 readback=0x839ee000 match=YES
omo-drv1: outbound viewport0: devva 0x80000000..0xffffffff -> host 0x80000000
omo-drv1: SR ch0 posted 32 nodes word0=0x83a01000; commit SR+0x18 <= 0x00000400 readback=0x00000400
omo-drv1: ENABLE SR ch0 +0x00 0x00000000 -> 0x00000001 readback=0x00000001
omo-drv1: ENABLE SR ch0 +0x48 0x00000001 -> 0x00000001 readback=0x00000001
   (likewise ch1 and ch2)
omo-drv1: firmware readback diffs=0 match=YES
omo-drv1: release readback = 0x00005a5a match=YES
omo-drv1: ---- post-release mailbox poll (25 ms interval, 4000 ms total) ----
omo-drv1:   t=25ms  out[1] 0x00000000 -> 0x00000040        <- id 6
omo-drv1:     out[1] bit 6 set (id 6)
omo-drv1:   t=450ms out[1] 0x00000040 -> 0x00000004        <- id 2
omo-drv1:     out[1] bit 2 set (id 2)
omo-drv1:     out[1] bit 6 cleared (id 6)
omo-drv1:   poll done: 2 transitions in 4000 ms; final out[1]=0x00000004
omo-drv1: [sig] 9/9 signature registers changed -> THE CHIP LEFT ROM STATE
```

**The port's own driver now does every part of the sequence in one boot.** Claim EP0, program the six
inbound viewports, program the ETE rings, **post SR descriptors + commit the producer index + enable
the channels**, write and verify the firmware, release the CPU (9/9 signature), and **elicit the
firmware's full dialogue: id 6 then id 2** - the `pcie_trigger_ete_sending_handle` word that this
project could previously only see from `lab/fwaccept`.

## What changed, and what it proves

Phases 19/23/24 saw a single word because the host never posted a descriptor. Phase 24i proved the
post was the trigger with a controlled A/B against `fwaccept`. This run shows the port's own
re-implementation reproduces it - so the trigger is no longer a property of one module's configuration
but a **reproduced, understood step in the port**.

The word arrived at **+25 ms** here versus `fwaccept`'s +750 ms. The difference is ordering: this module
posts the descriptors inside the `wr` stage, *before* the firmware load and release. That is not a
contradiction but a useful narrowing - the firmware's id-6 word is emitted as soon as it is running and
has a descriptor to wake a thread about.

## Two of my own errors on the way, both recorded rather than glossed

1. **The build failed** with `OMO_SR_PAYLOAD undeclared`: the new `#define`s sat next to the functions
   (~line 700) while `omo_rings_alloc` uses them at ~378. `presence is not placement` - the
   pre-build sweep must assert **define-line < first-use-line**, not merely that the identifier appears.
2. **The first run silently skipped the post** because the runner had lost `wr=1`; the post is gated
   inside `if (omo_wr_en)`, so it neither ran nor complained. The only tell was *absence* - no
   `posted`, no `ENABLE`, no payload-alloc lines. A missing prerequisite produced output
   indistinguishable from "the port doesn't work", which is precisely the trap the address-window bug
   set for several phases: **check that the step executed before drawing a conclusion from its
   absence.**

## Device state

`W=2 I=6 OFF=0 OMO=0 FAIL=0`, calibration `[SUCC]` on both bands, vendor modules intact, no
`.omo-off` leftovers, no unhandled faults.
