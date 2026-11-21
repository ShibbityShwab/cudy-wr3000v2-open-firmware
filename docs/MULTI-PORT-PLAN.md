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

## Prepared experiment image (2026-11-21)

- Image: **a91ebcf0** (save copy: staging-fi/img-mirrorprobe.bin, sha256
  a91ebcf04892ac9d5128c6ae5144eb9d6e09674b98938aac0aa8af289b3bc653).
- It is the verified 7421bb7a userspace/kernel plus two bench-only changes:
  1. patch 066-net-lsw-mirror-src-mask.patch (saved in
     opensource/docs/incoming/multi-port-probe/): adds the
     luofu_switch.mirror_src_mask module parameter (default 0x800 = BIT(11))
     and uses it in the mirror program/readback instead of the hardcoded BIT(11);
  2. built-in cmdline probe tokens: luofu_pie.rxdiag=1 and
     luofu_switch.mirror_src_mask=0x1e00 (mirror ports 9,10,11,12 ingress to
     CPU dst 4; full RXDIAG descriptor dump for the first completions).
- Run plan when the bench cable is back on the eth2 jack:
  1. install this image with tools/stock-telnet-install.sh (verify PASS, 2 CPUs,
     ping/SSH on eth2);
  2. capture the RXDIAG descriptor lines for host traffic on eth2 (baseline);
  3. move the cable to the candidate jack, ping from the host, capture the RXDIAG
     lines again;
  4. compare all descriptor bytes (port/vport/status/desc words) between the two
     jacks; a differing source-identifying field decides the CPU-copy extension
     vs native-bridge path (decision gate above);
  5. restore the known-good image 7421bb7a afterwards.
- The probe changes are bench-only (patch lives under docs/incoming, not in the
  release series; the build clone was restored to the canonical config and patch
  set after producing the image).

## Measured result of the source-port experiment (2026-11-21)

The probe image a91ebcf0 was installed on the unit (active stock pair untouched, selector 0x11
verified) and the experiment was run: cable on the far-right LAN jack, host ping loop running,
router power-cycled so the probe image booted with only that jack connected, descriptors captured
from the fresh dmesg (boot_id 45df3d10-72ad-43a4-bc2a-88df40710639; UART capture missed the boot
window but dmesg retained the 24 capped RXDIAG lines).

Result: the far-right-jack descriptors are FIELD-FOR-FIELD IDENTICAL to the eth2 baseline:

  baseline unique fields:  (ch,len,status,err,port,vport,vlan) =
      (1,74,0,0,0,0,0), (1,66,0,0,0,0,0), (1,88,0,0,0,0,0),
      (0,60,0,0,0,0,0), (0,60,37,1,11,0,0)
  far-right-jack unique fields: the same five tuples
  descriptor words: only word0 (0x04000000) and word1 (DMA address | 0xc2) populated;
  words 2..7 are zero in every mirrored completion. There is no source-port field.

Conclusions [V]:
1. The extended mirror mask (ports 9-12 -> CPU dst 4) DOES mirror the other jacks: frames were
   mirrored and delivered while only the far-right jack was connected.
2. The CPU-copy completion carries no source-port/vport identifier and no spare descriptor field
   that varies by jack. Delivering all copies to one netdev (the current cpucopy_port=11) is the
   only possible CPU-copy behaviour.
3. Therefore multi-port LAN/WAN over the CPU-copy workaround is NOT feasible. The decision gate
   resolves to the native path: the L2/VLAN hardware bridge in opensource/docs/LSW-SPEC.md
   (separate project). Do not extend mirror mask + cpucopy delivery as a multi-port solution, and
   do not fake port identity by MAC heuristics.

The probe image and patch 066 are retained as bench artifacts only; the board was restored to the
known-good image 7421bb7a afterwards.
