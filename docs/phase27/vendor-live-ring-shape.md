# The vendor's live ring shape, read in normal operation (phase 27b, 2026-10-03)

A read-only devmem pass over the ETE ring registers **in normal operation** (vendor stack running, the
instrument proven in phase 25), against what the port programs in a takeover.

## The vendor, live

```
DR ch0 CA=0x4003a590 base=0x83611000 depth-1=0x0000001F wptr=0x0000041F rptr=0x0000041F   <- ACTIVE
DR ch1 CA=0x4003a5fc base=0x00000000 depth-1=0x00000001 wptr=0x00000000 rptr=0x00000000
DR ch2 CA=0x4003a668 base=0x00000400 depth-1=0x00000400 wptr=0x00000000 rptr=0x00000000
DR ch3 CA=0x4003a6d4 base=0x00000000 depth-1=0x00000000 wptr=0x00000000 rptr=0x00000000

SR ch0 CA=0x4003a400 base=0x848FB000 depth-1=0x0000001F wptr=0x0000001F rptr=0x0000001F   <- ACTIVE
SR ch1 CA=0x4003a514 base=0x00000000 depth-1=0x00000000 wptr=0x00000000 rptr=0x00000000
SR ch2 CA=0x4003a628 base=0x00000000 depth-1=0x00000000 wptr=0x01060220 rptr=0x0000001F
```

## What it adds

1. **The vendor uses ch0 only.** Every other SR and DR channel is left at reset - and ch2's registers hold
   *garbage* (`base=0`, `wptr=0x01060220`), i.e. the vendor never touches them. The port programs **four**
   DR channels and three SR channels. That is not the gate (the extra channels are inert), but it is a
   divergence from the vendor's shape that had not been checked.

2. **Both active rings are at a full lap: index 31 (SR phase 0, DR phase 1), wptr == rptr.** Caught up,
   which is what an idle sampled instant looks like. The port commits `0x400` (index 0, phase 1) on both.

3. **DR ch0 depth is 32 (`depth-1=0x1F`), the same as SR** - so the port's ring *shape* matches the
   vendor's; only the base addresses differ (necessarily, they are per-boot allocations).

## What it does not add

**It does not name the gate.** The interesting quantity would be a DR ring *mid-deposit* (wptr ahead of
rptr), and at the sampled instant both indices are equal, so this is the idle state. To see a deposit one
would have to catch the vendor's DR wptr advanced - the same bound-capture discipline phase 25 had to
invent for the SR ring, applied to DR.

## Why this is worth having anyway

The reversal's remaining candidate space is now small and named, and this closes one item on it: **the ring
geometry the port programs is the vendor's geometry.** Combined with phase 21's boot 2 (engine fetched
*and* irq 209 fired 52,507 times, and the chip still did not answer) and this phase's H1 (the ring alone
elicits the full id-6/id-2 dialogue with the mailbox untouched), the host-side ring programming is
**not** what the firmware is waiting on.

