# The DR ring MOVES but never carries a frame (phase 27c, 2026-10-03)

A read-only poll of the vendor's live DR ch0 **in normal operation** (radio up), watching the DEVICE's
index and reading the slot it just processed.

## What the indices actually mean

Reading the record pins this, and my first attempt got it wrong:

| register | role | evidence |
| --- | --- | --- |
| `DR+0x38` | the **HOST's** committed index | `pcie_ete_dr_reg_init` writes it; `rxloop` commits `0x400` here |
| `DR+0x3c` | the **DEVICE's** index | on a live vendor boot while the radio is idle **they are equal**, and `rxloop` shows `DR+0x3c` moving `0 -> 0x10` **in reaction to** the host's commit |

My first poll watched the wrong pair and read the *next* slot, which returned `len=0` - **a bug, not a
finding**, and it is discarded. The corrected poll watches `DR+0x3c` and reads the slot the device just
finished.

## The result

```
polls=231   device-index moves=229   slots with len>0 = 0
  DEV MOVED host=0x416 dev=0x40b->0x416  lastSlot=11  buf=0x848e4a40  w1=0x00000000  len=0
  DEV MOVED host=0x008 dev=0x416->0x008  lastSlot=22  buf=0x8504f040  w1=0x00000000  len=0
  DEV MOVED host=0x013 dev=0x008->0x013  lastSlot=8   buf=0x85246c40  w1=0x00000000  len=0
  DEV MOVED host=0x016 dev=0x13->0x016   lastSlot=19  buf=0x85245840  w1=0x00000000  len=0
  DEV MOVED host=0x1d dev=0x16->0x1d     lastSlot=22  buf=0x848e4040  w1=0x00000000  len=0
  DEV MOVED host=0x407 dev=0x1d->0x407   lastSlot=29  buf=0x848e4a40  w1=0x00000000  len=0
```

**The device's index advances continuously - 229 moves across 231 polls - and the host's committed index
tracks it every time.** So the ring is live and being walked. And **not one slot the device processed
carried a frame** (`w1 == 0`, length 0) in this window.

## What this establishes, and what it does not

**Establishes:** the DR ring is not dormant. The device walks it, the host keeps it committed, and the two
indices stay in step. So "0 deposit events" in a takeover (`phase24/dr-commit-not-the-gate.md`) is **not**
by itself evidence of a gate - the same near-zero frame count is what a **working** system shows when there
is nothing to deliver. That retires the "no deposit ⇒ blocked" reading.

**Does not establish:** whether a *filled* slot ever appears when there IS traffic to deliver. This window
had the radio up but no heavy traffic driven, so it is an idle-ish sample. The honest statement is: **the
ring moves and carries nothing at these moments.**

## Where this leaves the reversal

The remaining candidate is now narrow and specific: the device deposits into DR only when it has a frame
for the host, and the port has never caught it with one. To go further one would have to **drive traffic**
(Wi-Fi association or a scan) while sampling, so that a fill is actually expected - the same bound-capture
discipline phase 25 had to invent for the SR ring, but with the radio asked to do something.

That is a device experiment, and it is the next concrete step rather than another read-only sample.

