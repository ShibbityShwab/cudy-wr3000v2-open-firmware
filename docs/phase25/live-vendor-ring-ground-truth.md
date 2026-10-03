# Ground truth: reading the live vendor's own SR ring (phase 25g, 2026-10-03)

Every previous attempt to learn the message *content* went through static analysis - and all of them
dead-ended (no CA immediates in `wifi.ko`, no message constants in `plat.ko`, the ops-table consumer in
an unnamed region, a spurious firmware hit inside zero-padding). This phase took a **dynamic** route
instead, and it is the first one that produced real bytes.

## How

The vendor stack is running **right now** in normal operation, so its ETE ring is live and its
descriptors and message buffers are plain host DRAM - reachable, **read-only**, with `devmem` through
the endpoint's BAR0 window (no module, no takeover, no writes).

```
SR ch0: ctrl=0x00000000 base=0x848F6000 wptr=0x0000041B rptr=0x00000002
SR ch1: ctrl=0x00000000 base=0x00000000 wptr=0x00000000 rptr=0x00000000   (unused)
SR ch2: ctrl=0x00000001 base=0x00000000 wptr=0x01060220 rptr=0x0000001F
```

**The vendor's SR ch0 is active**, and its descriptor array is at `0x848F6000` - 8 bytes per node,
exactly the layout the port builds.

## What the live descriptors say

```
idx 18 | buf 0x82483840 | len=72
idx 19 | buf 0x82483440 | len=72
...
idx 25 | buf 0x82482240 | len=48
idx 26 | buf 0x82483a40 | len=184
idx 27 | buf 0x82482840 | len=72
```

Two findings, and both matter:

1. **The descriptor format is confirmed** against live vendor state: `{word0 = buffer address,
   word1 = (len << 16) | flag}` - identical to what `omo_sr_post` writes. That is now verified three
   ways (the fill disassembly, the vendor's own `get_dscr_len`/`get_dscr_flag` decoders, and the live
   ring).
2. **The lengths VARY: 48, 72 and 184 all appear in one ring.** The vendor's SR channel carries
   *several message types of different sizes*; the port posts exactly one shape, always 72 bytes.

## What the live buffers contain

Reading the buffers shows heterogeneous payloads - ASCII strings among them
(`...om Disconnect received\n`, `conn...`) - not one fixed frame.

**A caution this measurement forces me to state:** the ring recycles fast. Re-reading the *same*
descriptor seconds later returned different lengths and different data, so the vendor is sending
continuously and any single read is a snapshot of a churning ring. Individual readings are therefore
**not** individually trustworthy - but the *aggregate* facts (format, and lengths of 48/72/184 all
present) are robust, because they were observed repeatedly across indices.

Decisive point for the port: **the port's posted frame `00 01 00 04 30 00 01 00 00 00 5a 5a ...` is not
representative.** Live messages are of several shapes and sizes, and the port always sends the one
captured 72-byte frame - with the added mismatch that its own `+4` field reads 48 while its descriptor
announces 72.

## What this changes

The content question is no longer "we cannot see the vendor's messages". **We can.** The vendor's live
ring is readable read-only, on demand, in normal operation. The remaining work is to characterise which
message *type* the firmware expects at the point the port is at - which is now an empirical question
with a working instrument, not a static-analysis dead end.

**Immediate consequence for the port:** an always-72-byte fixed frame is not what the vendor sends. Any
next experiment should treat the message as a *typed* object with a per-type length, starting from the
live examples rather than from the single capture.
