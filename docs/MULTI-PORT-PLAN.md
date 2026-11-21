# Multi-port Ethernet: current limit, experiment plan, decision gate

Status: 2026-11-21, after the 2-CPU / single-port bench baseline. Wired LAN works on
the verified baseline through the documented CPU-copy workaround (mirror eth2 /
switch port 11 ingress to the CPU destination port 4; PIE delivers the copies to
the eth2 netdev). This note records why the other jacks do not work yet and the
cheapest path to a decision.

## What is measured

- The switch ingress mirror is programmed by patches 054-057: OPP+0xd8 low 16 bits
  = source-port mask (BIT(11) for eth2/port 11), OPP+0xdc encodes the destination
  port 4, OPP+0xc0 bit 6 enables, OPP+0xb4 latches the change.
- The destination sweep (W17/W19) found exactly one CPU-delivery destination: port 4.
  Mirror copies arriving there are reported by PIE with completion port == 0,
  not with the source port.
- The PIE RX completion decode (patch 053) has (port, vport) fields, but for the
  CPU-copy completions we have only seen port == 0 and the current delivery code
  ignores vport (luofu_pie.cpucopy_port is a fixed module parameter, default 11).
- Consequence: with the current evidence the CPU cannot tell which jack a mirrored
  frame came from. Delivering all mirrored frames to one netdev would make the
  Linux bridge treat every LAN jack as the same upstream port (wrong egress and
  hairpin behaviour), so extending the mirror mask alone is NOT a correct
  multi-port solution.
- The far-right jack on the bench unit was moved to while the box was running:
  the box stayed alive (UART heartbeat) and its LAN bridge sent IPv6 router
  advertisements to the host, but host-to-box frames were not delivered (mirror
  covers eth2/port 11 only). So that jack is a LAN port, not WAN, and the
  limitation is exactly the missing source-port/CPU-delivery mechanism.

## Experiment plan (attended, cable on the working eth2 jack first)

Purpose: decide whether the CPU-copy path can carry a per-port source identifier.

1. Build a test image (not a release change) adding a module parameter
   luofu_switch.mirror_src_mask (default BIT(11)) and using it in the mirror
   program/readback instead of the hardcoded BIT(11); keep the existing
   cpucopy_deliver path. Enable luofu_pie.rxdiag=1 (full RXDIAG descriptor dump)
   and luofu_pie.cpucopy_deliver=1.
2. With the cable on eth2, verify the known-good ping/SSH path still works and
   capture RXDIAG descriptors for eth2 traffic (baseline port/vport/bytes).
3. Set mirror_src_mask to include both eth2 and the candidate jack (e.g. BIT(11)
   plus one of BIT(9)/BIT(10)/BIT(12)), move the cable to that jack, ping from the
   host, and capture RXDIAG descriptors for the mirrored frames.
4. Compare every descriptor byte between the two jacks. If any field (likely
   vport or a reserved byte) differs and maps to the source port, implement a
   per-port delivery table in PIE (completion field -> netdev) and re-test all
   jacks. If the descriptors are identical, the CPU-copy path cannot distinguish
   jacks and multi-port LAN needs the switch hardware bridge instead.
5. Restore the known-good image and the mirror default afterwards; every step is
   attended (recovery armed, UART captured).

## Decision gate

- If a source-port field exists: multi-port via CPU-copy workaround is feasible and
  can be implemented in the PIE delivery path plus per-port mirror mask and
  netdev mapping.
- If not: keep the single-port bench workaround, and multi-port LAN/WAN forwarding
  is the native L2/VLAN hardware bridge project already scoped in
  opensource/docs/LSW-SPEC.md (the separate long-term track). Do not fake
  multi-port by delivering all copies to one netdev.

## Current keep-alive

Until the experiment runs, the verified bench topology is: host on the eth2 jack,
br-lan 192.168.1.1/24, mirror eth2/port 11 -> CPU dst 4, cpucopy delivery to eth2.
