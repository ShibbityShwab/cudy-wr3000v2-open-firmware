# The credit registration: the init order, and where the thread stops (phase 24w, 2026-10-02)

Following `tx-credit-source.md`'s last link into the init sequence.

## `plat_hcc_init` @ 0x1a690 - the order

```
0x1a694 bl plat_custom_init
0x1a6a0 bl plat_main_init
0x1a6ac bl pcie_init_static_res
0x1a6b0 bl plat_debug_sysfs_init
0x1a6bc bl bal_init                 ; reads .LANCHOR0+0x3c HERE
0x1a6c8 bl hcc_init                 ; registers the hcc callbacks
0x1a6d4 bl plat_exception_init
```

## What that order says

`bal_init` reads the object at `.LANCHOR0+0x3c` and, when that read is **zero**, takes its `cmp r3, #0;
beq 0x10a8c` path and skips the port entry entirely. Nothing before it in the sequence is a chip-layer
registration, and the chip layer's own registration call runs **after** it (`hcc_init`).

So there is a real possibility that `.LANCHOR0+0x3c` is **null at the point `bal_init` reads it** and the
credit structure is populated later, on the path that actually needs it - in which case the object is
not "installed once at init" but bound somewhere in the chip layer's own bring-up, which is precisely
the part a takeover does not run.

## Where the thread stops, stated plainly

I followed this chain as far as the module boundary can carry it:

- `hcc_queue_tx_process` refuses to send without a credit - **established**;
- the credit is `min(queue, port)` and the port value comes through a runtime callback - **established**;
- the callback's object comes from `.LANCHOR0+0x3c`, which `bal_init` only *reads* - **established**;
- who *writes* it is not visible in the static data: it is not stored by `plat_main_init`,
  `plat_custom_init`, or anything before `bal_init` in the sequence, and there are no exported
  registration symbols in `plat.ko` matching the usual naming.

That is a legitimate stopping point rather than a failure: **the conclusion does not depend on the last
link.** Whatever installs the credit, it is a chip-layer binding, and a takeover does not run the chip
layer - which is why the port cannot send and why the vendor's own queue would not send either. The
open question is only whether the credit's *value* is additionally readable from a device register,
which is the one thing that could still convert a negative into a measurement.

## Value judgement on continuing this chain

Three further levels of a 21k-symbol module have been traversed to reach "a chip-layer binding", at
some cost, with the conclusion already in hand at the level above. Continuing is defensible but has poor
marginal value; the two things that would actually settle the data-path question remain the same:
a device register that reports the credit (if one exists), or the vendor's source.
