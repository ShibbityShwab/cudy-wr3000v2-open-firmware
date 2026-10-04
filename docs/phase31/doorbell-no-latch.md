# The doorbell does not latch - and the interrupt block is uninitialised in a takeover (phase 31, 2026-10-04)

Evidence `build/register-dumps/exp/20261004-052201/`, `params=[... fwctx=1 intrsamp=1]`.

## The measurement

```
[intrsamp] pre:  irqblock=41f0e8bd b872f7fa 3023f890 bf282b01 429d2301 f7f9d29e b148fbf8 30a9f894  ack=0
[intrsamp] doorbell out[2] <= 0x00000001  readback=0x00000000
[intrsamp] done: irqblock UNCHANGED across 2000 x 100us samples, ack still 0
```

**A post-release doorbell write produces zero change** in the interrupt block at CA `0x40161100`
and zero change in the firmware's ack. The doorbell does not latch as a pending interrupt there.

## The telling detail: the block is uninitialised in the takeover

The same block reads **all zeros in normal operation** and **garbage in the takeover** (values that
change per boot). So the vendor's host driver initialises that interrupt block at probe, and the
port never does. Combined with the negative, there are now two concrete facts pointing at the same
region:

1. the doorbell never latches there;
2. the block's init state differs between a working boot and a takeover.

Either the block's init (missing in the takeover) gates the doorbell->interrupt delivery, or the
pending state lives in a different register of the same block - and in both cases the next static
step is the same: **find the vendor's writes to CA 0x4016xxxx in plat.ko** (the interrupt block
initialisation the takeover lacks).

## Where this leaves the chain

ctx armed -> H2D handlers armed -> interrupt handlers armed -> **doorbell never latches** ->
dispatcher never runs. The un-proven link now has a measured shape: the doorbell's write lands
(readback 0 = consumed) but nothing downstream of the device's interrupt block ever moves.

