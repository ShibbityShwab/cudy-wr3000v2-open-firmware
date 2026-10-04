# The gate is located: the firmware's H2D handler table is registered in a vendor boot and NOT in a takeover (phase 30, 2026-10-04)

The new instrument (reading the firmware's RAM through the ACP-fw window, firmware addr X ->
BAR0 0x6b8000 + X, verified against the record's own predictions) produced the first direct
comparison of the firmware's live message state between a working vendor boot and a takeover.

## The decisive table

| word (firmware addr) | takeover (module boot) | normal operation (vendor stack) |
| --- | --- | --- |
| `*0x172130` (pcie_msg global) | `0x0010C0F4` | `0x0010C0F4` |
| **`[*0x172130 + 0xbc]` - the handler-table link** | **`0x00000000`** | **`0x00118D68`** |

**The vendor boot populates the H2D message-handler table link; the takeover does not.** The
takeover's ctx object is otherwise built (the six mailbox CAs sit in it, in the record's exact
order), so `pcie_msg_init` ran - but the registration link is missing.

## The firmware's live registered handlers (read from 0x118D68 in normal operation)

```
handler[1] fn=0x00040511  (file 0x00511)   arg=0x00000000
handler[3] fn=0x000c5145  (file 0x85145)   arg=0x0010c0f4   <- the ctx object, as the arg
handler[5] fn=0x000c19dd  (file 0x819dd)   arg=0x00000000
handler[6] fn=0x0008cce5  (file 0x4cce5)   arg=0x00000000
```

- **handler[3] is the registration this project already found statically**: the call at file
  `0x9838` inside `pcie_msg_init` registers `{fn = 0xc5145, arg = the ctx}` - and here it is,
  LIVE, with the ctx pointer as its argument. This independently confirms both the static reading
  and the instrument.
- The dispatcher at file `0x818ac` indexes this same table (`[ctx+0x20]` with ctx = the message
  object whose +0x20 points at 0x118D68), so **this table is the firmware's H2D dispatch**.

## What this explains, in one chain

The takeover's firmware has **no H2D dispatch table** - the link `[pcie_msg+0xbc]` is NULL. So:

1. the dispatcher, even if entered, has no handlers to call - consistent with every H2D accept
   failure across phases 20-25;
2. the firmware's own boot stops short of completing its message service - consistent with it
   "stopping at the pending-word stage" (phase 20);
3. none of the host-side candidates (20 register hypotheses, payload content, announce timing,
   ring geometry) could EVER have worked - the receiver side was never armed. Those eliminations
   are now explained, not just accumulated.

## What is NOT yet established

**Which firmware-side condition writes the +0xbc link, and what it waits for.** The write is
somewhere in the firmware's init; finding its gate is now a *static* question with a precise
observable: the instruction that stores into `[pcie_msg+0xbc]`, and the condition around it. The
firmware blob is on disk (`build/tmp/FIRMWARE.bin`, md5 `0e530b976d5a20e87358671f1a577695`), and
the record's disassembly tooling is already set up for exactly this.

## Method note

This comparison is **read-only**: the takeover read came from the fwctx=1 module dump (two boots,
stable), and the normal-operation read came from `devmem` through BAR0 with the vendor stack
running. The mapping was verified two ways before the comparison was trusted (the ctx global
matching the record's `0x10c0f4` prediction, and the CAs appearing in order).

## CORRECTION, added the same day - the live slot changes between samples

A second normal-operation read of the SAME slot showed `0x0010c1b4` where the first showed
`0x00118d68` - the vendor's running system rewrites `[pcie_msg+0xbc]` as it services messages. So
the single-sample comparison above is evidence, not proof: the slot is **non-zero and live** in a
vendor boot, and **stable at 0** across two takeover boots. The blob's own initial data holds
`0x118d68` at that slot, so 0 in the takeover is a divergence from both the initial data and the
live system.

The decisive follow-up (building): dump the handler table **itself** at the fixed addresses
`0x118d50`/`0x118d68` in a takeover. Normal operation has 4 handlers registered at `0x118d68`
(ids 1/3/5/6). If the takeover's table at the same address is also populated, the registration
happened and the gate is elsewhere; if it is empty, the registration is missing in a takeover.


## REFUTED, the same day - the registration DID happen in the takeover

The follow-up run dumped the handler table ITSELF at firmware `0x118d68` in a takeover
(evidence `build/register-dumps/exp/20261004-043028/`) and it is **populated identically to the
vendor boot**:

```
id 1: fn=0x00040511  arg=0x00000000
id 3: fn=0x000c5145  arg=0x0010c0f4    <- the ctx
id 5: fn=0x000c19dd  arg=0x00000000
id 6: fn=0x0008cce5  arg=0x00000000
```

**So the "registration missing" mechanism in the title is WRONG and is retracted.** The firmware's
message service is fully initialized in a takeover: the ctx is built (six CAs present), and the
handlers ARE registered. The `[pcie_msg+0xbc]` slot difference observed earlier is a live-service
artifact (the running vendor system rewrites that slot), not the gate.

**What stands, corrected:** the firmware is fully armed for H2D in a takeover, and the dispatcher
(file `0x818ac`) is still **never called** - its ack signature is never observed. The gate is the
**caller**: the firmware's own message interrupt, which never fires in a takeover. That is now the
single question, and it is narrower than it has ever been.


## The interrupt service is armed too - the gate is the interrupt FIRING, not the arming (same day)

The widened dump (evidence `build/register-dumps/exp/20261004-044442/`) reached the interrupt fn
slots. **The takeover's interrupt handlers are registered identically to the vendor boot:**

```
fn array @0x17d430 (fn for id N at +N*4):
  id 0x2d = 0x000462f9    <- identical to normal operation
  id 0x2e = 0x0004624d    <- identical to normal operation
  id 0x2f = 0x000aec95
  (plus the populated {fn_ptr, id} list and the packed id table seen in the previous run)
```

**So the entire receive chain is armed in a takeover:** ctx (six CAs) -> H2D handler table (ids
1/3/5/6) -> interrupt handlers (ids 0x2d/0x2e) -> dispatcher (file 0x818ac). Four of the five links
are now proven present, in the vendor's exact values.

**The gate is the last link: the firmware's message interrupt never FIRES.** The host's doorbell
write (out[2] <= 1 to CA 0x400392d4) does not reach the firmware as an interrupt, so the dispatcher
never runs (its ack signature, a write of 1 to 0x400392f0, is never observed - the poll's out[5]
stays 0). The vendor performs the same doorbell write through the same viewport and the firmware
responds - so the remaining difference is either the device's interrupt-mask state at the moment of
the doorbell (the firmware's enable path pokes device CA 0x40161800) or the path the doorbell write
takes through the two root complexes.

That is now a question about ONE link - the doorbell->interrupt delivery - rather than about
anything the host sends.

