# Glue-status service: the status never asserts (phase 23v, 2026-10-02)

Evidence `build/register-dumps/exp/20261002-182028/`, `WIFIDRV1 RESULT: PASS`,
`params=[hw=1 program=1 wr=1 fw=1 release=1 svc=1 verbose=1]`.

## What was tried

The one route phases 20 and 22 both name as the point a raw takeover never reaches: the glue ISR
path. Our takeover has no interrupt line (`PCI_INTERRUPT_LINE = 0xff`, and the vendor's host-side
`bal_irq_enable` action does not exist here), so the module **polls** the glue status at ISR cadence
and performs the vendor's decision logic - read `0x3f02ec`, mask to `0x3d8` (bits 3,4,6,7,8,9),
write the masked remainder back (that write-back *is* the clear), and report each pending bit with its
handler index (`comm+0x48` table semantics).

## Result

```
omo-drv1: ---- glue-status service (200 ms interval, 8000 ms total) ----
omo-drv1:   t=0  glue status 0x3f02ec = 0x00000000 (masked 0x00000000)
omo-drv1:   service done: 0 pending events in 8000 ms; final status=0x00000000
omo-drv1:   NOTE the glue status never asserted in this window
```

**Zero pending events in 8 s, status pinned at `0x00000000`.** Combined with phase 20's
`irq_taken = 0`, both levels of the vendor's two-level dispatch are now confirmed silent in a
takeover: no glue status bit ever sets, so there is nothing for `pcie_intr_handle` to dispatch, and
the message-level pending mask never gets a chance to be read.

## What that eliminates

| hypothesis | status |
| --- | --- |
| host never programs the right registers | **eliminated** - viewports vendor-identical, rings programmed with 0 readback failures, firmware `diffs=0`, release readback match |
| the firmware image is missing/wrong | **eliminated** - loaded and byte-verified this session |
| the mailbox resting state differs from the vendor | **eliminated** - every offset matches exactly |
| nothing polls the status, so a pending bit is missed | **eliminated** - 200 ms polling for 8 s, a shorter interval than any vendor ISR would need |
| the status would assert if we waited longer | **unlikely** - phase 19's first word arrived at +1.85 s under a comparable release |

What remains is a **device-internal** difference: in a takeover the endpoint does not assert its glue
status at all, where on a vendor boot it does (endpoint 1 shows thousands of interrupts). That is
consistent with the record's conclusion that the firmware's dispatcher is gated behind state a raw
takeover does not stand up - and it is now measured from *both* directions (silent status here, and
phase 20's zero interrupt count).

## The next experiment is a subtraction, not an addition

Phase 19 saw `out[1] = 0x4` with a **minimal** sequence (claim, decode viewports, write firmware,
release). Every run of mine since has done *more* than that - ring programming, the glue
channel-resource RMW, the ETE interrupt mask write - and produced **nothing**. A plausible and cheaply
testable explanation: one of those extra writes puts the endpoint into a state where the firmware does
not emit.

So the next boot runs the **minimal** path (viewports + firmware + release + poll, no rings, no
service). If `out[1]` appears, the culprit is one of my extra writes and it can be bisected in one or
two more boots; if it does not, the difference lies in something my driver still does that phase 19's
script did not, and the two can be diffed line by line.
