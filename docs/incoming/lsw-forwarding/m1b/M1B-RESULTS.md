# M1b results: per-port native delivery (2026-11-22)

Bench test of the W22 image with the CPU-copy mirror OFF, moving the single bench cable to each
LAN/WAN jack in turn. Evidence: build/evidence-bench-gates/m1b/ (gitignored).

## Jack map (physical order -> netdev/port)

| physical jack | netdev | switch port | link | native delivery |
| --- | --- | --- | --- | --- |
| far-right | eth0 | 9 | up when connected | **PASS** - host ping 200/200 0%, SSH works, eth0 rx/tx counters advance |
| second from right | eth1 | 10 | up when connected | **PASS** - ping 200/200 0%, SSH works, eth1 counters advance |
| second from left (bench) | eth2 | 11 | up | **PASS** (baseline) - ping 20/20 0%, SSH |
| leftmost (WAN) | eth3 | 12 | up (UART: link up 1000 Mbps at t=1060 s, down at 1145 s) | **delivery observed** - eth3 rx=77 tx=44 (DHCP attempts); ping N/A because eth3 is the WAN zone, not in br-lan |

After moving the cable back to eth2: 20/20 ping 0% loss. All four netdevs show non-zero rx/tx
counters from their respective sessions (eth0 261/224, eth1 262/225, eth2 228/112, eth3 77/44).

## Conclusions

- Native per-port CPU delivery works on all four switch ports with the W22 NAPT init and no CPU
  copy mirror (M1a generalises).
- Nothing in the W22 path needs port-specific work for delivery; the remaining multi-port work is
  the LAN-to-LAN forwarding behaviour (M1b-3), which requires a second host/NIC on another jack
  (not available at the bench) - the software bridge path is expected to work because each port
  already reaches the CPU bridge, but it is NOT claimed yet.
- WAN-port validation at L3 can be done by running a DHCP server on the bench host while the cable
  is on the leftmost jack; that would prove the eth3 DHCP client path end to end.

## Remaining

- M1b-3 true LAN-to-LAN: needs a second device; open.
- WAN L3 path: optional host-DHCP validation; open.
- Persistence: the earlier reboot determinism + link-flap test already passed on eth2; the jack
  moves themselves exercised link events on every port without issues.
