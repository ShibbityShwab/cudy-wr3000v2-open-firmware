# The host-side space is closed: both final moves landed correctly and the delivery is still blocked (phase 48, 2026-10-04)

Evidence `build/register-dumps/exp/20261004-151051/`, params include twinmask=1 maskreassert=1.

## The measurement

```
twin ctrl-rb mask 0x40039ae8 [0xae8] <= 0x3f6 readback=0x3f6 match=YES
EP0 mask re-assert 0x400392e8 [0x2e8] <= 0x20  readback=0x20  match=YES (already 0x20)
doorbell bit 0: raw 0x08 -> 0x09, masked 0x08 -> 0x09   (the interrupt fires, as phase 46)
ack: 0 throughout
```

## The final, mechanism-level chain

1. doorbell -> ctrl-rb raw + masked status latch (phase 46, measured)
2. mask open, vendor's own 0x20 (phase 46, measured)
3. source 0x4C registered AND enabled (phase 47, verified static)
4. twin mask written (0x3f6) and EP0 mask re-asserted at the sibling's arm placement (this phase,
   readback-verified)
5. **the ack never flips - the delivery from the ctrl-rb's latched status to the GIC input is the
   gate, and it is device-internal**

Every host-side action the verified maps name has now been performed to the vendor's own values:
the definitive frame, the vendor-exact D2H servicing, the complete DR channel programming, and both
interrupt masks.  The remaining hop is a wire inside the device, unreachable and unobservable from
the host by every mechanism this environment contains (phase 34 proved the controller block is
PCIe-invisible; phase 47 proved the source is armed).

## What remains - the two named instruments, both confirmed unavailable

- A device-side trace (JTAG/ROM-monitor): no hardware available (the human, 2026-10-04).
- The vendor source: unresponsive (the human, 2026-10-04).

The host-side reversal of the Wi-Fi bring-up has reached its true boundary.

