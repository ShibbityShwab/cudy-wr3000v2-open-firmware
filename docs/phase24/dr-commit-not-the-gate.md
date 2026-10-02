# The DR producer commit is not what gates a device deposit (phase 24l, 2026-10-02)

Evidence `build/register-dumps/exp/20261002-222521/`, `params=[... srpost=1 drpost=1 pollms=25 polldur=4000]`.

## The result

```
omo-drv1: SR ch0 posted 32 nodes ... commit SR+0x18 <= 0x00000400 readback=0x00000400
omo-drv1: ---- DR post: 32 nodes + producer commit (the phase-20 open item) ----
omo-drv1: DR ch3 posted 32 nodes word0=0x84e10000; commit DR+0x38 <= 0x00000400 readback=0x00000400 rptr=0x00000010
omo-drv1: DR ch4 posted 32 nodes word0=0x84e30000; commit DR+0x38 <= 0x00000400 readback=0x00000400 rptr=0x00000010
omo-drv1: DR ch5 posted 32 nodes word0=0x84e50000; commit DR+0x38 <= 0x00000400 readback=0x00000400 rptr=0x00000010
omo-drv1: DR ch6 posted 32 nodes word0=0x84e60000; commit DR+0x38 <= 0x00000400 readback=0x00000400 rptr=0x00000010
omo-drv1:   t=25ms  out[1] 0x00000000 -> 0x00000040   bit 6 set (id 6)
omo-drv1:   t=525ms out[1] 0x00000040 -> 0x00000004   bit 2 set (id 2), bit 6 cleared
omo-drv1: ---- DR watch (4000 ms): device index + node words ----
omo-drv1: DR watch done: 0 deposit events in 4000 ms
omo-drv1: NOTE no DR deposit - the device did not write our receive buffers
omo-drv1: [sig] 9/9 signature registers changed -> THE CHIP LEFT ROM STATE
```

**A clean negative that closes the open item.** Phase 20's own conclusion named the DR producer index as
"the most likely next thing to try" and left undetermined whether advancing it - or an explicit
"buffers available" write - was what the engine waited for. It is now **determined**: the buffers were
posted (word0 = payload device VA), the index was committed in the same packed form `fwaccept` uses
(`DR+0x38 <= 0x00000400`, readback match), the firmware emitted its **full dialogue** (id 6 then id 2),
the CPU left ROM state (9/9 signature) - and the device index never moved and no node word changed in
4 s.

So: **the producer commit is necessary state but not the gate.** The receive engine did not DMA.

## What this rules out, and what is left

Ruled out by this run:
- "the DR buffers were never made available" - they were, explicitly, with a matching readback;
- "the SR post / dialogue is missing" - the same run produced id 6 and id 2;
- "the fake rptr baseline hid a deposit" - the watch compares against a live baseline taken *after* the
  commit, and reports both the device index and the node words.

What remains, from phase 20's own list:
1. **The receive machinery behind the wake.** `pcie_rx_handle` reads the DR ring through the BAL
   `cbs->rx` callback, which the vendor binds outside `plat.ko`; a takeover has none of those host
   structures. Note that this explains a *missing consumer*, not a missing deposit - our watch is at the
   ring level and would see the device's write regardless.
2. **The earlier dialogue.** Phase 20 recorded that there is no id-1 "device plat ready" word and no
   message payload in a takeover; the 414-entry HCC command table and the SR send path are not built.
   If the firmware only begins DMA once it has accepted a host command, then no deposit is expected
   yet - and the next testable step is the **H2D send of the id-1 frame** rather than more ring state.
3. **The interrupt source.** Only once a transfer completes will INTA assert; `irq_taken` is 0.

## Useful new observation

`rptr` read `0x00000010` on all four DR channels *before* the release and stayed there - so the device
index has a nonzero resting value, and a deposit would be unmistakable as a change. That also means the
earlier "engine read" style observations (`0x10 -> 0x400` on the SR side) have a comparable baseline on
the DR side to compare against.

Device state after: `W=2 I=6 OFF=0 OMO=0 FAIL=0`, calibration `[SUCC]` both bands.
