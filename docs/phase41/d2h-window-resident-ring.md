# The D2H data path is window-resident: the vendor's DR ring is carved from the message window (phase 41, 2026-10-04)

Read-only `devmem` of the healthy device's message window (BAR0 `0x3f1000..0x3f1fff`, 1024 words),
2026-10-04 ~19:55 local.

## The observation

In NORMAL operation the message window is **full of live D2H payload traffic**, and the payloads
are laid out in **repeating 0x200-byte blocks** whose first word is a counter word ending in
`0x016000`:

```
3f1004 = 0x33016000   3f1204 = 0x29016000   3f1804 = 0x53016000
3f1a04 = 0x1D016000   3f1e04 = 0x79016000
```

Inside each block: live packets and protocol text - an IPv4/UDP DNS query (`0x08004500` ethertype +
IPv4, `192.168.1.10 <-> 192.168.1.83`), SSDP/UPnP discovery
(`ST: urn:schemas-upnp-org:service:WANIPv6FirewallControl:1`, `SERVER: ... UPNP/1.0
MiniUPnPd/2.2.3`, `USN: uuid:...`), HTTP requests (`POST /restore/schemas...`-style control
traffic).  The counter word increments per block (0x33, 0x29, 0x53, 0x1d, 0x79) - a live
ring-usage pattern.

## What this settles

1. **The vendor's D2H transport is window-resident.** The DR descriptors and their payload buffers
   live inside the message window (BAR0 `0x3f1xxx`), at 0x200 strides, not in system RAM.  The
   device DMA-writes D2H frames directly into this window.
2. **The port's DR buffers are invisible to the device.** Every phase-40 run posted DR nodes whose
   buffers were `dma_alloc_coherent` system RAM; the device never deposited because its D2H path
   targets the window-carved ring the vendor's own descriptors describe.
3. The earlier "mailbox idle = all zero" readings were moments of an empty ring, not register
   semantics - the window is a shared data region in steady state.

## What this unblocks

The next experiment has a concrete target: **post DR descriptors whose nodes and buffers are carved
from the message window at the vendor's layout** (0x200 strides, word0 = window buffer address,
word1 = length|flags), instead of system RAM - then the device's D2H frames become visible to the
port for the first time.  The exact ring geometry (node count, base, the counter semantics) needs
one more normal-op capture round before the port replicates it.

