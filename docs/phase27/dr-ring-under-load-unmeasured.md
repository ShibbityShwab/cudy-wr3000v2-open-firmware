# The DR ring under real station traffic - and why my instrument cannot settle it (phase 27f, 2026-10-03)

The first **valid** station-traffic run, thanks to a real 5 GHz client and the right counter.

## The run

```
station rx BEFORE = 1835485 bytes / 2845 packets
station rx AFTER  = 2858988 bytes / 4353 packets
VALIDITY: bytes +1023503, packets +1508    <- 1 MB ARRIVED OVER THE AIR
moves = 152   filled = 0
```

Unlike the three earlier attempts, **the validity check passed**: a laptop associated on `vap8` (5 GHz, -41 dBm)
pushed **1 MB / 1,508 packets** of inbound radio traffic while the sampler watched the DR ring process **152
slots, none of them filled**.

And a re-read of every ring with that station associated shows no channel came alive:
```
DR ch0 base=0x83611000 depth-1=31 wptr=0x408 rptr=0x408     <- caught up
DR ch1/ch2/ch3  at reset (ch2's registers hold unrelated values: base=0x40b, depth-1=11)
SR ch0 base=0x848fb000 depth-1=31 wptr=0x406 rptr=0x406     <- caught up
```

## The caveat that stops me short of a conclusion

**The vendor's own consumer is running.** On a live vendor boot the host driver drains the DR ring
continuously, so by the time a sampler over an ssh lane reads a slot, the consumer has almost certainly
already taken it. **A caught-up ring with empty slots is exactly what "deposits are drained immediately"
looks like** - and it is also what "nothing is deposited" looks like. This instrument cannot separate the
two.

That is why `filled = 0` here is **not** evidence that the device never deposits. It is evidence that I
cannot see a deposit at this sampling granularity, competing with a running consumer. The correct reading
is **UNMEASURED**, not negative.

Phase 27e's "free-running" observation is consistent with both readings for the same reason: continuously
advancing indices with empty slots is what a drained ring looks like.

## What would actually settle it

A capture that reads the slot **before** the consumer can, i.e. an atomic bind of the device's index to
the slot it just wrote, at a granularity finer than a userspace ssh round trip. That is the same problem
phase 25 solved for the **SR** ring by reading the index and all descriptors in one call - and it worked
there because the *host* was the producer, so it controlled the timing. Here the **device** is the producer
and the vendor's driver is the competing consumer, so the symmetric trick is not available.

So the honest position: **the DR ring is not shown to carry station frames, and not shown not to.** It is
the one question on this bench that my instrument cannot answer, and saying so is better than either
conclusion.

