# H1 confirmed: the SR ring ALONE elicits the dialogue - the mailbox announce is irrelevant (phase 27a, 2026-10-03)

Evidence `build/register-dumps/exp/20261003-113328/`, `params=[hw=1 program=1 wr=1 fw=1 release=1 srpost=1
sr_announce=0 msgsvc=1 pollms=25 polldur=4000 verbose=1]` - **`sr_announce=0` verified in the result line
and by the live process's own command line.**

This runs the hypothesis ranked highest in `docs/phase22/fw-hostmem.md` - H1, *"the H2D message must arrive
as an ETE SR ring delivery, not (only) as a mailbox register write"* - and specifically its decisive
isolation, *"Do not write `out[0]` on this arm (isolate the ring from the mailbox)"*. It had never been run.

## The result

```
ANNOUNCE lines in the run:            0        <- the mailbox is COMPLETELY untouched
SR ch0 base/depth/wptr/ctrl      written, readback match=YES
ENABLE SR ch0 +0x00, +0x48       readback 0x00000001
SR ch1, SR ch2                   posted and enabled identically
[sig] 9/9 signature registers changed -> THE CHIP LEFT ROM STATE

t=0     out[0]=0x00000000 out[1]=0x00000000    <- out[0] was NEVER set: no host write at all
t=25ms  out[0] 0x0 -> 0x0
t=25ms  out[1] bit 6 set (id 6)                 <- THE FIRMWARE RESPONDS ANYWAY
t=550ms out[1] bit 2 set (id 2)
poll done: 2 transitions in 4000 ms
```

## What this establishes

1. **The mailbox announce is NOT necessary.** With `out[0]` never written and no doorbell, the firmware
   emits the **same** dialogue it emits when the announce *is* sent.
2. **The SR ring alone is sufficient** to move the firmware into its id-6/id-2 sequence. H1's core claim
   is **confirmed**: the ring is the carrier.
3. **The id-6/id-2 dialogue is a fixed response to the SR post** - independent of the mailbox *and*
   independent of the payload (phases 25t-25x established the content does not matter either).

## What it retires

The whole announce line of work. Phases 25p-25z spent considerable effort on the announce's **timing**
(pending-at-release), its **placement** (inside `omo_sr_post` as `sr_dscr_fill` does), and its
**offset** from the release (swept, inert). Those measurements are all still *true* - an announce written
after the release really is not read, and the offset really is inert - but they were characterising **a
signal the firmware does not need.** The dialogue happens without it.

So the honest re-reading of phase 25's "in-post announce CONSUMED" headline: the consumption was real, and
it was **a side-effect of the firmware's own boot/init touching the mailbox**, not the trigger of anything.
The trigger was the ring all along, and it always worked.

## What it does NOT change

**The H2D dispatcher still never runs.** `out[0]` was never cleared by the device because it was never
set - and in every run *where* it was set, the device either cleared it at boot (phase 25's consumption) or
left it set (every post-release send). What has never happened, with or without the mailbox, is the
dispatcher at file `0x818a8` executing: its signature is a write of 1 to the ack CA `0x400392f0`, and that
is not observed.

So H1's **kill** clause is what stands for the H2D accept: the ring reaches the firmware's **receive**
path (id 6/id 2), and the **H2D accept path** is a separate thing that the ring does not arm either.

## The question this sharpens

The firmware, having received the SR ring, **asks the host to send** (id 6 = `pcie_trigger_ete_sending_handle`;
the host's handler for it is literally named *trigger ETE sending*), then reports id 2 and stops.

**So the next question is what the host does when id 6 arrives** - whether it performs a *second*, post-id-6
ring submission, and whether the vendor's `pcie_trigger_ete_sending_handle` does something the port's
handler does not. That is a host-side question about a response the port may simply never make, and it is
where this result points.

