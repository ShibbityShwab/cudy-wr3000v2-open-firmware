# First write path: specification (phase 23h, staged 2026-10-02)

The read side is done - both register blocks decode (`wifidrv1-both-blocks.md`). This is the
specification for the first **write** the port's own driver will make to the endpoint, written before
the code so the sequence and its safety gates are reviewable on their own.

## What will be written, and why

The ETE SR/DR ring program registers, reproduced from `pcie_ete_sr_reg_init` / `pcie_ete_dr_init`
as rtmsg already proved them (`docs/phase20/runtime-msg.md`, readback discipline included). All of
them fall inside the window the read side already maps (`BAR0+0x3f2000`, 0x1000 bytes):

| register | ETE offset | absolute BAR0 |
| --- | --- | --- |
| SR ch0 base | depth-1 \| wptr | ctrl | `+0x410` `+0x414` `+0x418` `+0x408` | `0x3f2410` `0x3f2414` `0x3f2418` `0x3f2408` |
| DR ch3 base | depth-1 | wptr | `+0x5c0` `+0x5c4` `+0x5c8` | `0x3f25c0` `0x3f25c4` `0x3f25c8` |
| SR ch0 chn_res (RMW `& 0xfffffc20`) | `+0x6e8` | `0x3f26e8` |

Values: ring base = the coherent DMA address of the node array (`dma_alloc_coherent`), depth-1 = 31
(depth 32), wptr = 0, ctrl = 0 - exactly the values rtmsg wrote and read back `match=YES`.

## The gates (all must hold before a single byte is written)

1. **Takeover boot only.** With the vendor stack loaded the endpoint cannot be claimed at all
   (`EBUSY`, measured twice), so the write path is only reachable in a takeover - which is also the
   only state where a wrong write cannot corrupt a live radio.
2. **The viewports must be programmed first** (`program=1`). Without them every register reads
   `0xffffffff` and a write lands nowhere meaningful - the mis-address signature.
3. **A readback after every write**, per register, logged with `match=YES|NO`. rtmsg's discipline:
   never assume a write took. A `match=NO` aborts the sequence rather than continuing blind.
4. **Never `out[5]` (CA `0x400392f0`).** The write path touches the ETE block only. The mailbox is
   left exactly as the read side found it.
5. **The watchdog is armed before staging** (harness-enforced), and the recovery restores the vendor
   stack - so a bad write costs one reboot, not a brick.
6. **Bounded:** the first write path programs the rings and stops. It does **not** submit a descriptor
   and does **not** ring the doorbell, because phase 22 measured that submitting without the
   device-side accept gate produces nothing (`docs/phase22/h2d-accept.md`, 20 hypotheses + the `out[5]`
   arm, chip state unchanged). Writing the rings is the prerequisite for that work, not a substitute
   for it.

## What success looks like

- every program register reads back the written value (`match=YES`), and
- the pre-write decode shows the `0x00000000` state the read side already recorded, so the
  before/after pair is unambiguous in one boot.

## What it will NOT prove

Programming the rings does **not** make the chip transmit. The H2D accept gate is device-side and was
shown to hold regardless of host register writes; that remains the frontier named in
`docs/phase22/h2d-accept.md`. This write path moves the port from "can read the endpoint" to "can
own the endpoint's ring state", which is the precondition for any later data-path attempt - and is
honestly the smaller of the two steps.
