# M1a follow-up: the CPU-punt path is NAPT, and the next reviewed write set

Status 2026-11-21, host-only proposal after the two M1a nulls and the stock decoded procfs
capture (build/evidence-bench-gates/hsan-stock/hsan2/). No device write is made by this file.

> **UPDATE 2026-11-21: M1a PROVEN.** The sequence below was replayed by the lead from a probe-only
> init script on proof image 5800c9ab (mirror off): host ping 200/200, SSH working, native UART
> RXDIAG port=11 status=77 deliveries. The kernel-side implementation is now drafted in
> ../w22/ (patch 0001) with the release integration plan in ../w22/RELEASE-INTEGRATION.md.

## 1. What the M1a nulls told us

- Probe A (tagact entry 1 at 0x124d4014=0xc000): write **ignored**, readback 0. The pub-window tag
  action table is not CPU-writable in that way. Contrast: the IFC tab128 writes were verified
  readback-exact in W10 (patch 043) through the ifc+0x50000 path, so the window is writable in
  general; the tagact table specifically has no CPU write path we can use (and, per the decode,
  no reference from any captured state on our image).
- Probe B (pfe_glb+0x2c=0x10000034): accepted and read back; eth2 rx delta stayed 531 = mirror
  copies only. This confirms up_port alone is not an L2/CPU-delivery enabler (it is the
  flow-engine uplink attribute decoded in E0B-ANALYSIS section 3).

## 2. What the stock counters say the CPU-punt rule actually is

Stock capture, host pinging, pipeline counters (all vendor-decoded):

| counter | value | meaning |
| --- | --- | --- |
| pa rx_total / tx_total | 992 / 992 | frames through the packet-analysis block |
| ifc rx_total / ifc_hit | 992 / 992 | every frame hit the IFC classifier |
| **ifc_tocpu** | **104** | IFC-directed to CPU - equals the broadcast count of eth2 RX (103) |
| **ifc_napt** | **170** | IFC-directed to the NAPT/flow engine - equals the unicast/other count |
| napt tx_total / **up_tocpu** | 170 / **129** | NAPT handles 170 and punts 129 "up" to the CPU - equals the unicast count (130) |
| napt sip_chk_tocpu, mc_fail_tocpu | 4, 37 | remaining small punt classes |
| dp port4 queue in/out | 274 / 274 | all CPU deliveries: 103 broadcast + 130 unicast + 41 multicast |
| pie ch0 rx / ch1 rx | 191 / 83 | CPU RX channels; sum 274; eth2 rx 274 = uc130+mc41+bc103 |

So the vendor's CPU-punt rule is:

- **broadcast/ARP-class: IFC default rules -> CPU port 4** (the eight W10 default entries, already
  identical on our image and verified readback - this is the 104/103 class, and it is exactly the
  status=37 exception class our driver receives).
- **unicast: IFC default rules -> NAPT/flow engine; NAPT "up" direction -> CPU port 4**
  (this is the 129/130 class). Our image has NAPT uninitialized (all config words 0), so those
  frames are dropped inside the flow engine: no completion is produced - exactly the E0-a/E0-b
  null (no 98-byte completions with the mirror off).
- multicast: IFC/MCC path (41 frames, cap_tocpu counters); separate, M1b.

This also explains the tagact entry: the vendor CFE rootif/L3 entries that reference it are part
of the flow/L3 program that our kernel never installs; the tag action itself is a tag-delete
action for those entries. It is not the CPU-punt mechanism and its absence does not block M1a.
No further stock reads are needed to establish the mechanism; the counter decomposition above is
quantitative and consistent across PA/IFC/NAPT/DP/PIE queues.

Counter register addresses decoded from the vendor dump routine (all read-only):
- IFC counters: ifc window + 0x1000 group (rx_total/+0x1000, +0x1004, +0x1008, +0x100c,
  +0x1010, ... - names on stock print as rx_total/ifc_hit/ifc_tocpu/ifc_napt; read the whole
  block 0x12101000..0x1210103c and diff before/after traffic).
- NAPT counters (verified mapping): napt+0x1000 tx_total, +0x1004 dn_drop, +0x1008 up_drop,
  +0x100c dn_tocpu, **+0x1010 up_tocpu**, +0x1014 dn_match, +0x1018 up_match, +0x101c
  ctrl_tocpu, +0x1020 option_tocpu, +0x1024 option_drop, +0x1028 flag_tocpu, +0x102c flag_drop,
  +0x1030 age_per_err, +0x1034 age_spc_err, +0x1038 sip_chk_drop, +0x103c sip_chk_tocpu,
  +0x1040 mtu_chk_drop, +0x1044 mtu_chk_tocpu, +0x1048 ddr_xor_err, +0x1060 rx_total,
  +0x1064 rx_drop, +0x1068 rx_unip, +0x106c rx_tocpu, +0x1070 rx_napt, +0x1074 rx_mc,
  +0x1078 rx_sa, +0x107c rx_gro, +0x1080 in_tunnel, +0x1084 out_tunnel, +0x1088 mc_fail_tocpu,
  +0x108c sa_tocpu, +0x1090 pre_drp, +0x1098 sa_drop (physical base 0x12280000).
- L2 counters: 0x121c0000 group (rx_total/tx_total/...), as decoded in E0B-ANALYSIS.

## 3. The missing initialization: hi_pfe_napt_init (fully decoded write path)

The vendor PFE module initializes the NAPT/flow engine in hi_pfe_napt_init@0x4314. Decoded
sequence (register writes, no CPU writes into DDR; the engine initializes its own tables). Our
replay uses the **stock-captured final values** (E0-b stock regs) and the vendor order:

| # | write | stock value | note |
| --- | --- | --- | --- |
| 1 | pfe_glb+0x2c = 0x10000034 | RMW high16 = up_port 0x1000 | flow-engine uplink attribute (decoded write_glb_attr attr 0) |
| 2 | napt+0x70 = 0x00110138 | after vendor RMW | mode/ctrl bits |
| 3 | napt+0x64 = 0x00000001 | enable | |
| 4 | napt+0x54 = 0x00000100 | bit8 flow_age_en | |
| 5 | napt+0x130 = 0x80400000 | flow table base A | vendor derives from the DT flow window (0x80400000/1 MiB) |
| 6 | napt+0x138 = 0x80408000 | table base B | |
| 7 | napt+0x134 = 0x80448000 | table base C | |
| 8 | napt+0x13c = 0x80450000 | table base D | |
| 9 | napt+0x68 = 0x00020303 | hash/aging config | |
| 10 | napt+0x60 = 0x000a0001 -> poll bit0 clear | v6 table clear command | hi_pfe_flow_napt_clr_all; bit0 self-clears, timeout from vendor 0x3d40 iterations |
| 11 | napt+0x60 = 0x00080001 -> poll bit0 clear | v4 table clear command | |
| 12 | napt+0x50 = 0x00f00b69 | hash function params (stock) | |

Safety of the DDR pointers: the switch flow window is 0x80400000/1 MiB and the DT memory node
starts at 0x80500000, so the whole 0x80000000..0x80500000 carveout is outside Linux memory
(queue 0x80000000/4 MiB + flow 0x80400000/1 MiB are the switch's own DDR windows in the DT
switch node). The flow engine therefore DMAs only inside the reserved carveout; no Linux page is
reachable from these table bases. This is the contract the write planner's NAPT refusal was
protecting; it is now decoded and bounded. The planner keeps refusing NAPT by default; this
prototype uses its own fail-closed script with an explicit 13-address allow-list instead.

What is NOT needed: no CPU writes to flow DDR (the engine clears its own tables via the +0x60
commands), no EQM changes, no IFC changes, no tagact, no PIE/GEMAC changes.

## 4. Expected observable (with the mirror still on)

After the sequence, a controlled ping from the host should make the box receive an **extra
native RX delivery of the 98-byte unicast requests** in addition to the mirror copy:

- eth2 rx_packets delta for N ping requests: baseline = N (mirror only) -> after NAPT init
  expected about 2N (mirror copy + native NAPT-punt copy). The native copy is delivered through
  the existing exception path (accepterr=1) to the port-11 netdev, so no driver change is needed
  for the observation.
- napt+0x1010 (up_tocpu) counter increments during the ping.
- ifc_napt counter (ifc+0x1000 block) increments for the same frames.
- dmesg stays clean; SSH/management unaffected (mirror path is not touched).

If the delta stays N, the NAPT punt still does not reach PIE (e.g., another engine step or a
missing NAPT sub-init); the next decode targets would be hi_pfe_pub_init's flow configuration
and the missing +0x160/+0x170 MCC extras, and the IFC action bits that route to NAPT.

## 5. Fail-closed prototype

- Script: apply-napt-init.sh (on box, requires LSW_WRITE_ATTENDED=1 LSW_RECOVERY_ARMED=1;
  prints the sequence with no args). It refuses unless the current words match the recorded
  ours baseline, refuses if the running command line does not have cpu_mirror=1 (management
  must stay reachable), snapshots every target into a rollback file, writes the sequence,
  verifies all readable config words read back exactly, polls the two commands with a bound,
  and restores everything on the first mismatch. --revert restores the snapshot (newest
  rollback file fallback).
- Read-only observer: observe-napt-init.sh (eth2 counters, napt counters, ifc counter block,
  L2 counters, mirror words, dmesg). Use before and after the ping.
- Pre-arm a 90 s auto-revert before the apply, exactly as in m1a/SESSION.md.

## 6. Exact commands

Read-only diagnosis on the current baseline image (no writes; confirms the ifc_napt hypothesis):

    ROUTER_HOST=root@192.168.1.1 tools/rsh.sh 'sh /tmp/m1a/napt-init/check-napt-path.sh'
    ping -i 0.02 -c 200 192.168.1.1
    ROUTER_HOST=root@192.168.1.1 tools/rsh.sh 'sh /tmp/m1a/napt-init/check-napt-path.sh'

Expected from the two snapshots: ifc+0x1000 counters (rx_total/hit/tocpu/napt words) change;
ifc_napt-class word increases by about 200 while up_tocpu stays 0 and no native delivery appears.
That confirms the frames reach the NAPT engine and are dropped there.

If the write experiment is approved later (attended, recovery armed, UART):

    tools/rscp.sh m1a/napt-init/apply-napt-init.sh m1a/napt-init/observe-napt-init.sh root@192.168.1.1:/tmp/m1a/napt-init/
    ROUTER_HOST=root@192.168.1.1 tools/rsh.sh 'sh /tmp/m1a/napt-init/apply-napt-init.sh'   # print only
    ... pre-arm auto-revert ...
    ROUTER_HOST=root@192.168.1.1 tools/rsh.sh "cd /tmp/m1a/napt-init && LSW_WRITE_ATTENDED=1 LSW_RECOVERY_ARMED=1 sh apply-napt-init.sh --apply"
    ping -i 0.02 -c 500 192.168.1.1
    ... snapshot, decide, revert ...

## 7. What to do if these writes are ignored like the tagact write

The tagact null was a table-RAM write with no CPU write path. The NAPT targets are ordinary MMIO
registers (the vendor uses plain hi_lsw_write to the same addresses; our W10/W12 writes to other
windows verify readback), so a silent ignore is not expected. The script reports, per word,
"readback ok" or "READBACK MISMATCH" and reverts; if the enable/ctrl words (0x70/0x64/0x54) do not
read back, the experiment ends there with no further writes and the conclusion is that the NAPT
block needs a different access path (e.g. it may be gated by a clock/reset not yet enabled on our
image - check CRG/DP reset state next).

## 8. Remaining unknowns after this proposal

- Whether NAPT init alone is enough for the up_tocpu punt, or whether hi_pfe_pub_init's flow
  configuration (table managers 0x702/0x70d and flow counters) is also required at hardware
  level. The prototype answers this.
- The exact IFC default entry/action that routes unicast to NAPT (we do not need it if the
  defaults already do it; the read-only counter test confirms this first).
- The multicast class (41 frames) and +0x160/+0x170 MCC extras: M1b, not needed for unicast.
- The tagact reference: no longer on the M1a critical path; it belongs to the CFE rootif/L3
  program (documented in E0B-ANALYSIS).
