# Both levels of the dispatch are silent, and the subtraction says it is not my writes (phase 23w, 2026-10-02)

Two results this phase, both negatives, both narrowing the problem honestly.

## 1. The glue-status service never sees an event

`build/register-dumps/exp/20261002-182028/` (`svc=1`):

```
omo-drv1: ---- glue-status service (200 ms interval, 8000 ms total) ----
omo-drv1:   t=0  glue status 0x3f02ec = 0x00000000 (masked 0x00000000)
omo-drv1:   service done: 0 pending events in 8000 ms; final status=0x00000000
omo-drv1:   NOTE the glue status never asserted in this window
```

Zero events in 8 s. Combined with phase 20's `irq_taken = 0`, **both levels of the vendor's two-level
dispatch are confirmed silent in a takeover**: no glue status bit sets, so `pcie_intr_handle` has
nothing to dispatch and the message-level pending mask is never reached. The ISR route was reproduced
faithfully (mask `0x3d8`, vendor clear write-back, handler-index reporting) and there was simply
nothing to service. So the ISR route is not a missing piece of *our* implementation - what is missing
is on the device side.

## 2. The subtraction: my extra writes are not the cause

Phase 19 saw `out[1] = 0x4` after a **minimal** sequence (claim, decode viewports, write firmware,
release). Every later run of mine did *more* - ring programming, the glue channel-resource RMW, the
interrupt mask write - and produced nothing. The obvious suspicion was that one of those extra writes
suppressed the firmware.

`build/register-dumps/exp/20261002-182329/` ran the **minimal** path (`hw=1 program=1 fw=1 release=1`,
no rings, no service, no chn_res RMW):

```
omo-drv1: firmware readback diffs=0 match=YES
omo-drv1: release readback = 0x00005a5a match=YES
omo-drv1:   poll done: 0 transitions in 8000 ms
omo-drv1:   NOTE no mailbox transition in this window - the firmware produced nothing
```

**Still nothing.** So it is not the rings, not the glue RMW, not the interrupt mask - the extra writes
are exonerated. The difference from phase 19 must be elsewhere.

## 3. The observable that was never checked

Every "the release worked" claim in this repo (mine included) rests on the **register readback** - but a
readback only proves the register latched the value. It does **not** prove the CPU started. Phase 19's
actual evidence was a *signature*: the firmware's BSS words going `0x40080000/0x25/0x1110/0x8 -> 0`,
the frozen analog registers coming alive (`dcoldo_vset 0xffffffff -> 0x260d4184`, `pbank_code ->
0x312`), and `tcxo_pll_mux_sel 0 -> 1`.

That signature has never been read in any of my runs. **It is the difference between "the write
landed" and "the chip is running"**, and it is the first thing to establish before theorising further.

So the module now reads the same nine signature registers before and after the release and reports how
many changed, with the verdict spelled out (`THE CHIP LEFT ROM STATE` vs `the chip did NOT start`).
The region-3 window was widened to the full `0x120000` for it - the offset-fits check caught five
registers outside the old 0x1000 window, the same class of error that faulted once already.

## If the signature does not change

That would mean the release **is not starting the CPU at all**, and every downstream silence follows
from that - a much simpler and more useful conclusion than the register-by-register hunt. The next
question would then be why phase 19's identical write DID start it: candidate differences are the
write path (phase 19 used `devmem` from userspace; this module uses a kernel `iowrite32` through an
`ioremap`) and whatever state phase 19's `fwboot` module had established before the kick.
