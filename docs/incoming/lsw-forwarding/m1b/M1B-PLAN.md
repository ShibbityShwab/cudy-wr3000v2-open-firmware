# M1b: per-port native delivery and LAN-to-LAN (next hardware experiment)

Status 2026-11-21. M1a is PROVEN (proof image 5800c9ab): with the CPU mirror off and the decoded
NAPT/flow init applied, eth2 ingress is delivered natively (port=11 in the completion, status 77
class) and ping/SSH work. M1b widens that to every wired port and to the LAN-to-LAN case, and
decides whether the software bridge over eth0..3 is enough or the hardware L2/VLAN bridge (M2) is
still required.

## 1. Questions M1b must answer

1. Does the native punt deliver ingress from **all** wired ports with the right identity?
   Ports 9/10/11/12 (eth0/eth1/eth2/eth3) must produce completions whose decoded port maps to the
   matching netdev. The proof covered port 11 only.
2. Which frame classes are punted to CPU? The M1a counter work showed IFC punts broadcast/ARP and
   routes unicast into NAPT (napt up_tocpu). For a software bridge, LAN-to-LAN traffic needs at
   least broadcast/unknown-unicast copies to reach the CPU, and the CPU must be able to TX out the
   other port (TX path already works on eth2).
3. Does the NAPT/flow configuration survive link events and reboots (the mirror's post-link
   reset behaviour is the cautionary precedent)? If it does not, W22 needs the same re-apply hook
   the mirror has.
4. Multicast: does the mc class reach the CPU (the 0x160/0x170 MCC words were not in W22; stock
   showed a 41-frame mc class)? Needed for IPv6/IGMP but not a blocker for IPv4 LAN-to-LAN.

## 2. Image to use

- Primary: the W22 release image (patch 059/060, canonical command line without mirror tokens),
  i.e. the image from the release-integration step.
- Optional diagnostic variants (no functional change):
  * add luofu_pie.rxdiag=1 to see the first 24 completion descriptors per boot (useful for a
    10-frame probe per jack; 200-frame runs will exhaust it, use netdev counters there);
  * a bench-only debug patch adding a periodic per-port RX counter print is possible but NOT
    needed if the counter-readback method below is used (it avoids more code on a proven image).
- The fallback image (naptinit=0 + mirror tokens) stays available for recovery.

## 3. Test matrix (attended; lead/human runs the on-box steps)

Common setup per run: recovery door armed, UART capture running
(tools/uart-capture.sh start --label m1b), host ping loop ready, box on the W22 image, no mirror
tokens. Only one cable move at a time; always restore the bench cable to eth2 afterwards.

M1b-1 - per-port native delivery (four runs):
  For each jack X in {eth0=port9, eth1=port10, eth3=port12} (eth2/port11 already proven):
    1. move the bench cable from eth2 to jack X (UART capturing);
    2. host: ping -i 0.02 -c 200 <box-ip>; record loss and the first/last reply time;
    3. host: send a short probe first (ping -c 10) so the RXDIAG ring shows the class if the
       diagnostic image is used;
    4. move the cable back to eth2; confirm SSH is restored;
    5. read on the box (SSH): /sys/class/net/ethX/statistics/rx_packets/rx_bytes/rx_dropped and
       ip -s link for every eth0..3; the jack under test must show the 200-frame RX increase on
       its own netdev (not on eth2), and eth2's counters must not have moved;
    6. record whether ping succeeded (native delivery) and the decoded port/status from UART if
       rxdiag was on.
  Pass criterion: ping 0% loss on every jack and the counter increase lands on the matching
  netdev. Any failure is a per-port coverage finding (possible causes: port/vport decode, IFC
  default coverage, EPS/qid mapping for that port).

M1b-2 - LAN-to-LAN, single host (no second device needed; partial):
  With the cable back on eth2:
    1. from the host, send broadcasts (ARP/DHCP-like) and raw unknown-unicast frames
       (AF_PACKET, destination 02:00:00:00:00:0a) while a ping runs;
    2. on the box: check bridge FDB learning (brctl showmacs br-lan / bridge fdb show), and the
       RX counters of eth2; with the diagnostic image, check that the unknown-unicast class is
       delivered (RXDIAG port=11) and whether the bridge floods it to the other ports;
    3. the other ports have no link, so no egress is observable; this step proves only the
       CPU-delivery class for unknown unicast, not end-to-end forwarding.
  This step can also be done while the cable is on a non-eth2 jack if the FDB/counter reads are
  captured via UART; prefer eth2 to keep SSH.

M1b-3 - true LAN-to-LAN (needs a second host or a second bench NIC):
  Requires two active links on two box ports (e.g. host on eth2 and a second host/laptop on eth0,
  or a second USB NIC). Then:
    1. host A pings host B through the box's software bridge; host B pings A;
    2. verify br-lan FDB has both MACs on the right ports, ping 0% loss, no mirror, no duplicate
       replies;
    3. watch the box counters: both eth ports show RX/TX increases, no drops/errors; check
       whether the switch hardware forwards any class directly instead of punting (it may, for
       known unicast after learning; either way the bridge must be consistent);
    4. repeat after a link flap and after a reboot (persistence).
  If no second host/NIC is available, record the environment limitation and leave M1b-3 open;
  do not fake it with a single host.

M1b-4 - persistence and re-apply:
  - cable re-plug on eth2: native ping/SSH must return without a reboot;
  - reboot the box: native ping/SSH must work again after links come up;
  - read napt+0x130/0x70/0x64 and compare with the W22 logged values (read-only devmem, same
    tools as m1a/napt-init/observe-napt-init.sh). If they are reset after link-up, propose a W22
    re-apply hook from the existing post-link worker (the code already has one for the mirror).

M1b-5 - multicast observation (optional):
  with the diagnostic image, send IPv6/mDNS/IGMP-class frames from the host and record whether
  the mc class reaches the CPU (RXDIAG class/port and counters). This scopes the MCC follow-up;
  it is not a blocker for IPv4 LAN-to-LAN.

## 4. What decides "software bridge is enough" vs "hardware bridge still needed"

- If M1b-1 passes on all ports and M1b-2 shows the unknown-unicast/broadcast classes are punted to
  the CPU, and M1b-3 (with a second host) passes, then the **software bridge over the four
  netdevs is sufficient for D1**: no hardware L2/VLAN bridge is needed for multi-port LAN or
  WAN routing; the CPU sees the traffic and the PIE/GEMAC TX path sends it out.
- If LAN-to-LAN traffic is not punted (e.g. the hardware forwards station-to-station directly, or
  drops unknown unicast), then M1b-3's ping will fail while M1b-1 passes. In that case the M2
  hardware bridge is required for LAN-to-LAN (port-based VLAN/default-forward state + FDB) and
  becomes the next RE/implementation lane; the NAPT/IFC punt still covers CPU-bound traffic.
- If per-port tests reveal missing identity or missing coverage, fix the port/vport decode or the
  IFC default coverage before any hardware-bridge work.

## 5. Observation method (no extra kernel code required)

- Per-port netdev counters are persistent: run the traffic on the cable under test, move the
  cable back to eth2, then read /sys/class/net/ethX/statistics on the box over SSH. The netdev
  that received the frames is unambiguous (the counter that jumped), and dropped/error counters
  bound the failure mode.
- UART (read-only capture) covers crash/panic and the RXDIAG lines; rxdiag is limited to the
  first 24 completions per boot, so use a short probe (10 pings) when the class is needed and the
  counters for the 200-frame quantisation.
- Dmesg (via SSH when reachable) for W22/log lines and NAPI/RX errors; bridge FDB via
  brctl/bridge commands for M1b-2/3.
- Read-only devmem for napt+0x130/0x70/0x64 and the decoded counters (m1a/napt-init/
  check-napt-path.sh and observe-napt-init.sh) before/after link events.

## 6. Failure and rollback rules

- Stop and restore the cable to eth2 immediately if a jack test shows no link, no ping, or a
  kernel Oops/panic; capture the UART log first (read-only), then power-cycle (human) if needed.
- If SSH does not return within ~30 s after restoring the cable, treat the image as failed: reboot
  (human) and, if it fails again, broker-install the previous canonical image or the fallback
  image (naptinit=0 + mirror tokens).
- Never move or overwrite the stock pair A; recovery door armed throughout; no hot register
  writes from the host session; every step is a normal reboot/install with the documented broker
  path.
- Record each run in build/evidence-bench-gates/m1b/ (gitignored): UART log, ping output, counter
  snapshots, bridge FDB, dmesg excerpts, image hash, cable position log.
