# The DR ring is free-running, not event-driven - and three bad instruments (phase 27e, 2026-10-03)

## The one clean measurement

Sampling the device's DR index as fast as the ssh lane allows, counting **value changes** (not a derived
quantity) over three equal 20 s windows:

```
QUIET    polls=55  index CHANGES=50
SCANNING polls=55  index CHANGES=53      <- an active scan loop on vap9
QUIET2   polls=56  index CHANGES=46
```

**The ring advances at the same rate whether the radio is quiet or actively scanning.** Whatever drives the
device index, it is **not** frame reception - the rate is within noise across all three windows, and the
scan in the middle window was real (the same scan finds six neighbouring APs).

That is a genuine negative, and it is the strongest statement this bench can currently support: **the DR
ring's motion is not coupled to received traffic.**

## Three instruments that lied to me first

This result took four attempts because each earlier instrument was broken in a way that produced a
plausible-looking number:

| # | instrument | what it reported | why it was worthless |
| --- | --- | --- | --- |
| 1 | device ping/fetch to this host | `moves=202 filled=0` | `br-lan` bridges `eth*` and `vap*`; the route went **wired** - `eth2 rx=33205`, every radio interface `0`. The Wi-Fi ring never saw a packet |
| 2 | `/sys/class/net/vap9/statistics/rx_packets` as a "did the radio receive" check | `0` before and after a scan that found six APs | **Received frames do not increment this chip's netdev counters at all.** The counter cannot distinguish "idle" from "busy", so a zero means nothing |
| 3 | derived "slots advanced" from the index and its phase bit | `1023`, `18`, `2042` in equal windows | my phase arithmetic wrapped repeatedly; the numbers described **my formula**, not the device |
| 4 | **count of index value changes** | `50 / 53 / 46` | a count of a thing that cannot be miscomputed - the only one of the four I will stand behind |

Instrument 2 is the subtle one and worth naming: I used it to declare the **previous** run invalid, and that
verdict was itself wrong. A counter that never moves cannot certify an experiment as invalid *or* valid.

## What this changes

Phase 27c's finding stands and is now sharpened: the device walks the DR ring continuously (229 moves in
231 polls then, ~50 per 20 s window now) and **the motion is independent of traffic**. Its earlier caution
- "the ring moves and carries nothing at these moments" - can be stated more strongly: **the ring moves
whether or not anything is arriving, and the slots are empty either way.**

So the "no DR deposit" observation, which earlier phases treated as evidence of a device-side gate, is
**neither evidence of a gate nor of its absence**: this ring appears not to be the carrier for received
frames at all on this bench.

## The honest limit

A bench check could not produce genuine over-the-air traffic to a *station* - there are zero associated
stations, the only LAN peer is wired, and this driver advertises only `managed` and `AP` (no monitor mode
to inject with). So "received frames do not reach DR" is measured for **scan/beacon-class reception**, which
is what the radio demonstrably does here. Whether a full data-path transfer would fill DR differently is
**not** established, and cannot be, on this bench as it stands.

