# THE RING MAP: what the phase-40 zero-buffer result really means (phase 42, lead-written, 2026-10-04)

Merged from the verified phase-42 lanes plus the lead's own re-derivation.  The run's two lane
children died in the host restart; their files were verified and corrected by the lead
(VERIFICATION.md), and this map is written from the VERIFIED material only.

## 1. The rings are host DRAM with 8-byte nodes - and the port already posts that

The vendor's live SR ring (phase25/live-vendor-ring-ground-truth.md, read-only):
`base=0x848F6000`, 8 bytes per node, contents `idx 18 | buf 0x82483840 | len=72` etc.
Node format (verified instruction-level, phase-37 map + this phase): word0 = buffer devva,
word1 = (len<<16)|flags, flags = 0xd2b | 0x2000 | 0x4000 = 0x6d2b.

**The port's existing SR/DR rings already have this exact shape** (OMO_SR_FLAG = 0x6d2b,
8-byte nodes, word0 = devva, word1 = (len<<16)|flags).  The phase-41 window-carve theory is
RETRACTED: the rings were never window-resident, and nothing in the ring layout explains the
phase-40 zero buffers.

## 2. What the message-window blocks are

The 0x200-stride blocks with (len<<16)|0x6000 heads (17 observed) are the device's staged D2H
records in the window - payload staging, not ring descriptors.  Their heads carry a length
halfword whose low byte varies with content (the nine identical STP records all read 0x79016000),
not a counter.  They exist only in normal op because the device's D2H path only runs there.

## 3. What the phase-40 zero-buffer result means, corrected

The port posted correct-format DR nodes in host DRAM and the device never deposited.  The window
theory would have explained it as a wrong-buffer-target; it is not.  The deposit absence is the
KNOWN gate: the device's message service (which produces D2H transfers) never starts in a
takeover - the mailbox->line-0x4C delivery proven unreachable (phases 31-35).  No ring-layout
change can move that.

## 4. What remains, in order

1. [read-only, cheap] Fresh normal-op read of the DR ring base register (device CA 0x4004a004,
   reg_all.txt holds 0x84a90000) to confirm the DR base and its wptr/rptr neighbours.
2. [static] Reconcile which register pair (0x4004a004/a008) is DR base vs index, from the module
   code that programs them.
3. [live, gated on 1-2] If a vendor-boot ring read shows the device depositing into the
   vendor's DR base, compare the vendor's DR programming (base register + the enable bits) with
   the port's - the deposit path's activation may be a register the port never sets.
4. The mailbox gate itself (line 0x4C) remains closed on the host side, as recorded in phases
   31-35; the vendor source or a device-side trace stay the only instruments for it.
