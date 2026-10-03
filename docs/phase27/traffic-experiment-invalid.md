# A traffic experiment that did not test what it claimed (phase 27d, 2026-10-03)

I ran the experiment phase 27c said to run - drive traffic while sampling the DR ring - and it **did not
test the hypothesis**. Reporting it as a result would have been wrong, so here is the failure and what it
does and does not license.

## What I ran

`ping -f` plus repeated fetches from the device toward this host (`192.168.10.28`), while polling the
vendor's live DR ch0 for a **filled** slot:

```
moves = 202        filled = 0
```

## Why that number means nothing

The counters afterwards:

```
eth2  rx_packets = 33205        <- the traffic went HERE
vap0/vap1/vap8/vap9 rx_packets = 0     <- no radio interface saw a single packet
```

`br-lan` bridges `eth0, eth1, eth2, vap0, vap3, vap8, vap11`, and the route to `192.168.10.28` resolves
through it. The bridge picked the **wired** path, so the traffic never traversed the Wi-Fi chip at all.
The DR ring is the Wi-Fi **receive** ring; with nothing arriving over the radio there was nothing to
deposit, and **"filled = 0" is precisely what that predicts.**

So the experiment is **invalid for its purpose**, and I am not recording it as evidence about DR deposits.
The measurement is real; the inference would not have been.

## What it does not change

Phase 27c's own result stands on its own terms: the device index advanced **229 times across 231 polls**
with the host index tracking, so **the ring is walked and is not dormant** - measured without any traffic
claim attached.

## What the experiment *would* need

For Wi-Fi RX there must be a **station generating traffic over the air**. The device has **zero associated
stations** (`vap0 sta: 0`, `vap1 sta: 0`) and the only host on the LAN is wired. So either:

- a client associates to one of the AP interfaces (`vap0`, `vap3`, `vap8`, `vap11`), or
- the device itself acts as a **managed** client on `vap9`/`vap11` (both are `type managed`) and joins an
  external AP, which is the path this project has used before for `alg` traffic.

Until one of those is true, **any DR-ring sampling on this bench is measuring an idle radio**, and that is
the real constraint on this line of enquiry - not a property of the chip.

## Method note

The failure was caught by a **counter check after the fact**, not by design. The fix for next time: before
sampling a data path, first prove traffic traversed *that* path by reading its own counters - `/sys/class/net/<iface>/statistics/rx_packets` - in the same run, and treat a zero there as an invalid
experiment rather than a negative result.

