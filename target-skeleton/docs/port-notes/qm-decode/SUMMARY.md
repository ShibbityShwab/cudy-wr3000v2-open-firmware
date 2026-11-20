# incoming/qm-decode - round 4r: CPU-copy delivery + static dst 4 (patch 057/W20)

**Lane:** `qm-decode`, shared task `task-61`, 2026-11-20 (round 23).  Prior: `032`...`056`.
This package delivers **patch 057/W20**, the last known gap: deliver the port-0 CPU-copy completions
created by the static mirror destination 4.  No device access.

## 1. Decisive W19 dwell result

Image 4a38b497 (W19 dwell, 20 s per destination) identified the switch CPU port: inside the
**dst=4** dwell window (67.7-88.2 s) the PIE RX ring completed 13 frames with
`len=98 status=0 err=0 port=0 vport=0 vlan=0` (the host's ICMP requests arriving through the
normal path), plus 7 ARP exception completions (len=60 status=37 err=1 port=11).  Steps dst=5..15
produced no len=98 completions - destination 4 is the CPU port.  Ping was still 0/220 because our
driver delivers only to netdevs registered for the completion's port (8..12), and these CPU copies
carry port 0, which has no netdev -> dropped.

## 2. Patch 057 (W20)

- **`luofu_pie.cpucopy_deliver=1`**: port-0 normal-path completions are handed to the net_device
  registered for `luofu_pie.cpucopy_port` (default 11 = eth2, the mirror source port) instead of
  being dropped; first 16 deliveries are logged as
  `pie-trace RXCPUCOPY port=0 -> port 11 len=98 status=0 (cpucopy_deliver=1, mirror source port)`.
  Vendor behaviour is preserved for every other port and for exception completions (those still
  follow `luofu_pie.accepterr`).
- **Static CPU-copy at dst 4**: the W19 machinery already re-applies the mirror after every MAC
  link-up and re-latches it every 2 s; with `luofu_switch.cpu_mirror=1` (default mirror_dst=4) the
  start line now notes `"= CPU port identified by the W19 dwell sweep"`, i.e. this is the sustained
  configuration.

## 3. Sustained run command line

```
luofu_switch.arm=1 egress=1 qidmap=1 eps=1 qminit=1 pfe=1 ifc=1 ext=1
luofu_pie.arm=1 rxcfg=1 accepterr=1 cpucopy_deliver=1 cpucopy_port=11 rxdiag=1
luofu_gmac.macregs=1 w9=1 hwinitwrite=1
luofu_switch.cpu_mirror=1 luofu_switch.mirror_dst=4
```
(W9 hwinitwrite is optional; it restores the stock max-frame value and is already covered by W16.)

Expected console: the W19 post-link re-apply lines, `RXCPUCOPY port=0 -> port 11` lines for the
host's ICMP requests, and the host should finally get sustained ping replies.  The host ARP/ICMP
traffic also exercises the TX direction that already worked for the ARP reply.

## 4. Deliverables / validation

- Patch `057-net-pie-cpucopy-w20.patch` (`290d3b9c...`): all patches apply (58/58, 0 rejects);
  luofu-pie/luofu-switch compile clean under WERROR; checkpatch 0 errors (2 cosmetic warnings:
  missing commit description, one blank-line-after-declarations, and one multiple-blank-line check).
- This SUMMARY is the lane record; docs stack update is batched for the next handover.
