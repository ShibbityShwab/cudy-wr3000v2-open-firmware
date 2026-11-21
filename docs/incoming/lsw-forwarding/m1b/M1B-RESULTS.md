# M1b results: per-port native delivery and LAN-to-LAN (2026-11-22)

Bench test of the W22 image with the CPU-copy mirror OFF. Evidence:
build/evidence-bench-gates/m1b/ (gitignored).

## Jack map (physical order -> netdev/port)

| physical jack | netdev | switch port | link | native delivery |
| --- | --- | --- | --- | --- |
| far-right | eth0 | 9 | up when connected | **PASS** - host ping 200/200 0%, SSH works, eth0 rx/tx counters advance |
| second from right | eth1 | 10 | up when connected | **PASS** - ping 200/200 0%, SSH works, eth1 counters advance |
| second from left (bench) | eth2 | 11 | up | **PASS** (baseline) - ping 20/20 0%, SSH |
| leftmost (WAN) | eth3 | 12 | up (UART: link up 1000 Mbps at t=1060 s, down at 1145 s) | **delivery observed** - eth3 rx=77 tx=44 (DHCP attempts); ping N/A because eth3 is the WAN zone, not in br-lan |

After moving the cable back to eth2: 20/20 ping 0% loss. All four netdevs show non-zero rx/tx
counters from their respective sessions (eth0 261/224, eth1 262/225, eth2 228/112, eth3 77/44).

## LAN-to-LAN forwarding (M1b-3) - PASS

With a second USB-Ethernet adapter on eth0 and the bench adapter on eth2, raw AF_PACKET frames
(ethertype 0x88b5) were exchanged through the box's software bridge:

| test | direction | result |
| --- | --- | --- |
| broadcast | eth0 -> eth2 | PASS |
| unicast (learned) | eth0 -> eth2 | PASS |
| unicast (learned) | eth2 -> eth0 | PASS |
| broadcast | eth2 -> eth0 | PASS |

Bridge FDB after the run (brctl showmacs br-lan):
- 00:e0:4c:63:17:58 (bench adapter) on port 1 (eth2)
- 00:e0:4c:68:02:14 (second adapter) on port 3 (eth0)
- br-lan local MAC on port 1
Counters advanced both ways (eth0 rx 588/tx 367, eth2 rx 378/tx 319 at capture).
Evidence: lan2lan-l2b.txt, lan2lan-fdb2.txt (the first run lan2lan-l2.txt showed the reverse
direction FAIL because the test socket read stale PACKET_OUTGOING copies; the marker-filtered
rerun proves both directions).

## Conclusions

- Native per-port CPU delivery works on all four switch ports with W22 and no CPU copy mirror
  (M1a generalises).
- The software bridge forwards LAN-to-LAN in both directions on the tested port pair
  (eth2 <-> eth0). A hardware L2/VLAN offload (M2) is therefore NOT required for multi-port LAN
  forwarding on this platform through the tested pair; additional port pairs follow the same
  bridge path (eth1 is another LAN member and was already proven to deliver to the CPU).
- The WAN port (eth3) is a separate zone and not bridged to br-lan; its L3 path is optional to
  validate with a host DHCP server.

## Remaining

- Optional: WAN L3 path validation with a host DHCP server on the leftmost jack.
- Optional: repeat the L2 test through eth1 (expected identical; bridge membership verified).
- No multi-port limitation remains for LAN forwarding over the tested ports.

## LAN-to-LAN second port pair (eth2 <-> eth1) - PASS

The same marker-filtered AF_PACKET test was repeated with the second adapter moved to the jack
second from the right (eth1/port 10):
- broadcast eth1->eth2 PASS, unicast eth1->eth2 PASS,
- unicast eth2->eth1 PASS, broadcast eth2->eth1 PASS.
Bridge FDB: bench adapter MAC 00:e0:4c:63:17:58 on port 1 (eth2), second adapter MAC
00:e0:4c:68:02:14 on port 2 (eth1); counters advanced both ways (eth2 rx 421/tx 359,
eth1 rx 265/tx 228). Evidence: lan2lan-eth1.txt, lan2lan-eth1-fdb.txt.
