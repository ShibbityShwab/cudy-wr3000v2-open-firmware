# Native forwarding for the luofu internal LSW - implementation plan

**Status 2026-11-21. Host-side plan only.** No register has been written for this plan; no
hardware has been used; the release patch series and the curated upstream tree are untouched.

> **Revision 2026-11-21 (after the E0-a, E0-b, M1a and stock-procfs rounds):** read sections 2.1,
> 2.2, 2.3 and 4.4 first. E0-a proved the exception path works natively but only ARP-class traffic
> is punted. E0-b decoded the stock-vs-ours deltas (tagact = tag delete, pfe_glb = up_port, L2 page
> = DFX counters, IFC identical). The two M1a replay probes were then run on hardware and were
> both NULL (tagact write ignored; up_port no effect), so the write experiments closed. The
> consistent-stock decoded procfs capture then resolved the mechanism: IFC punts broadcast to CPU
> (ifc_tocpu=104) and routes unicast to the NAPT/flow engine (ifc_napt=170 -> up_tocpu=129); our
> image has the IFC defaults but NAPT uninitialized. The next step is the NAPT init replay in
> m1a/napt-init/ (register-only, reserved switch flow window). Milestones: M1a (unicast to eth2),
> M1b (multi-port).
This plan extends the release line's verified bench workaround
([../../MULTI-PORT-PLAN.md](../../MULTI-PORT-PLAN.md), [../../LSW-SPEC.md](../../LSW-SPEC.md),
[../../PIE-DATAPATH.md](../../PIE-DATAPATH.md)) with the path to per-port native forwarding.

Evidence markers are the repository convention: **[V]** verified by a named artifact or a
recorded measurement, **[I]** inferred/planned, **[U]** unknown/unmeasured, **[X]** blocked.

Inputs used: `opensource/docs/LSW-SPEC.md`, `PIE-DATAPATH.md`, `MISC-SOC-BLOCKS.md`,
`MULTI-PORT-PLAN.md`, the release-line patches (canonical series 001-058 + bench-only 066),
the pinned vendor fixtures (parent `rootfs-2.4.15/lib/hisilicon/ko/*.ko`, read-only), the
2026-11-18 read-only inventory, and the 2026-11-20 stock-vs-ours dumps in
`build/evidence-bench-gates/` (gitignored).

---

## 0. What is already proven on the bench (do not re-derive)

| fact | evidence |
| --- | --- |
| Release line = patches 001-058; canonical attended kernel command line in `target-skeleton/README.md`; working image family 7421bb7a / 39424b94 | [V] `target-skeleton/README.md` "Canonical attended bench command line", release-line manifest |
| Two CPUs online; OpenWrt 6.18.60 boots, SSH, ubifs overlay | [V] release notes / README |
| Wired LAN works **on eth2 only** through the CPU-copy workaround: LSW W17-W20 mirror programs OPP and PIE delivers port-0 completions to eth2 | [V] patches 054-057, `MULTI-PORT-PLAN.md` |
| Switch init stages W3-W20 are programmed and verified by the release driver: CRG power, qid maps, R1/R3/R5 probe config, EPS channel map, EQM queue-manager tables, PFE base init, IFC default-classification table, extended EPS/DP words, PIE RXCFG, GEMAC static config | [V] patches 022/025/031/032/036/043/046/048/051 and `LSW-SPEC.md` rounds 4-4s |
| The CPU-copy completion carries **no source-port identifier**; far-right-jack descriptors are identical to eth2; multi-port over CPU-copy is impossible | [V] `MULTI-PORT-PLAN.md` "Measured result", commit 117ea55 |
| All *persistent-looking* switch register state that the W11/W14/W15 dumps compared already matches the stock vendor boot; the only differing pages are runtime counters/status | [V] `2026-11-20-lsw-diff.txt`, `2026-11-20-pha-diff.txt`, `LSW-SPEC.md` W11/W14/W15 |
| The vendor modules that install the runtime forwarding layer (CFE/rootif: `hi_kcfe_pub`, `hi_kcfe_lrn*`, `hi_kcara`) cannot load on 6.18 (vendor sk_buff ABI + bridge patches), so their state cannot simply be replayed from a module run | [V] [VENDOR-SKB-BRIDGE-PORT.md](../../VENDOR-SKB-BRIDGE-PORT.md), `DOCUMENTATION-HUNT.md` |
| PIE RX completion decode already has normal-path port/vport, error-path port/vport and a tag-class field; frames for a registered port are delivered to that port's netdev | [V] patch 053, `PIE-DATAPATH.md`, `luofu-pie.c` `luofu_pie_rx_decode/rx_deliver` |
| The CPU/mirror destination in the LSW is **switch port 4** (W19 dwell sweep) and the PIE RX path for a port-4 delivery currently reports completion port 0 | [V] patch 054-057, W19 log |
| Per-port netdevs already exist: GEMAC creates one netdev per DT port (`hisilicon,port` 8..12), PIE registers the RX receiver for that port; the Linux bridge software-forwards br-lan | [V] patches 009/020/023/024/051, `luofu-gmac.c`/`luofu-pie.c` |
| A per-port mirror source-mask parameter exists as a bench-only patch (066) in `../multi-port-probe/` | [V] `066-net-lsw-mirror-src-mask.patch` |
| **E0-a run (2026-11-21, image 990c5cab):** with `cpu_mirror=0` the box boots (2 CPUs, LEDs, event0) and the PIE RX ring receives only ~60-byte exception-class completions (`status=37 err=1 port=11`, once per second - the host's ARP retries); no 98-byte unicast completions appear and IP is dead | [V] appended E0-a result, ledger 2026-11-21 18:10, `build/evidence-bench-gates/e0a-mirroroff/e0a-boot-excerpt.txt`; analysis `E0A-ANALYSIS.md` |

**Consequence:** the missing capability is not "more of the same init" - every decodable init
write matches stock. The missing capability is the **runtime forwarding/CPU-delivery
configuration** that the vendor's CFE stack installs after boot, or an equivalent our driver
designs itself.

---

## 1. Architecture: two forwarding planes, and which one multi-port needs

The LSW has two different jobs in the vendor system, and they are easy to conflate:

1. **The hardware forwarding plane** (PFE: IFC classifier, L2, NAPT/MCC, OPP/egress): frames
   ingressing a MAC port are classified and forwarded to MAC egress ports, or punted to the
   CPU/PIE egress ports (switch ports 4..7 in the measured namespace; port 4 is the one the
   W19 sweep proved delivers to PIE).
2. **The CPU delivery plane**: whatever the hardware decides belongs to the CPU is delivered
   through PIE RX rings. PIE's completion descriptor is the only metadata the Linux driver
   gets: `port/vport`, status, length and a 2-bit tag class.

The release line currently has **(2) for one mirror source only** and only as a copy
(OPP mirror -> port 4 -> PIE port-0 completion -> delivered to eth2). It has **(1) only for the
static init that we decoded**: the 8 IFC default-classification entries and the PFE base init.

What multi-port actually requires, in increasing ambition:

- **M1 (minimal, the goal of this lane):** every wired MAC port's ingress can reach the CPU
  with a reliable **ingress-port identity**, so the Linux bridge software-forwards between
  eth0..eth2 and the CPU routes WAN on eth3. No hardware L2 offload is required for M1; the
  bridge is in software exactly as it is today on stock when flows are not offloaded.
- **M2 (optional):** LAN-to-LAN traffic is forwarded by the hardware L2/flow plane so it does
  not cross the CPU. This is an efficiency/upstream feature (switchdev offload), not a
  functional requirement for multi-port.
- **M3 (optional/out of scope):** flow offload (NAPT/CAR/mirror/WoE).

The lead's framing ("port-based VLAN/untagged forwarding table entry shape, MAC learning
enable, CPU port tagging for routed traffic") is exactly M1 done the classic switch way:
per-port VLAN/port identity for CPU delivery, and (for M2) MAC learning. This plan treats M1 as
the first deliverable and M2 as the follow-on.

### 1.1 Why the identity problem is the whole problem

The negative experiment is decisive: mirror copies from all jacks are identical, so the CPU-copy
path has no source identity. There are only three ways a frame delivered to the CPU can carry
its ingress port:

1. **in the completion metadata** (PIE descriptor `port/vport` / tag class): the decode has the
   fields; whether the *native punt* path fills `port` with the ingress port is not yet measured;
2. **in the frame** (a tag/header inserted by the switch, e.g. the vendor's tag action /
   802.1Q PVID): PIE delivers the frame bytes, the Linux driver parses the tag;
3. **by the port it was delivered to** (different PIE channel/queue per ingress port): the
   measured CPU destination is a single port 4, so this would need per-port CPU destinations
   (ports 4..7) and per-channel rings - more machinery and not supported by the current PIE
   driver (only the two wired channels are initialized).

All three are testable **read-only** first (see section 2/MINIMAL-EXPERIMENT.md); only then does
it make sense to write a tag action or an IFC entry.

**Update after E0-a (section 2.1):** the completion-port route is now proven for the exception
class (the mirror-off boot delivered ARP punts with port=11). What remains is making the
**unicast** class CPU-bound in the first place; tag work is demoted until a tagged CPU delivery is
observed.

---

## 2. The three implementation routes, ordered by risk/impact

### Route A - replay the vendor runtime forwarding state, read from a stock boot (lowest risk)

The vendor stock boot has the full CFE stack running. Its **readable** persistent tables can be
captured with devmem and compared with ours; anything non-zero only on stock is a candidate for
exact replay. This is the same method that produced W10/W12, extended to the tables never yet
captured:

- **IFC tab128 banks**: `ifc+0x50000` and `ifc+0x60000`, entries 0..127 x 4 words each
  (physical `0x12150000` and `0x12160000`). Bank0 entry 1..8 is the W10 default set [V];
  entries 9+ and bank1 are **not captured on stock yet**.
- **tag action table**: `pub+0x54000`, 16-byte entries (physical `0x124D4000` + idx*16),
  table-manager id `0x708` ("tagact", 12-byte key/data). No vendor module writes it at init:
  it is runtime CFE state. Never captured.
- **L2 window** `0x12180000`: config words `+0x100..0x18c` are identical on stock and ours and
  no vendor module writes the L2 window at all [V] - it is configured before Linux (boot
  loader). The L2 table/learning area pages (`+0x8000/0x10000/0x18000/0x20000/0x40000/0x60000/
  0x70000`) were scanned at page granularity on stock (all zero except counters), but **not
  content-dumped with traffic**. If hardware MAC learning is active, a learned entry must
  appear there while traffic flows.
- **vendor procfs**: `/proc/hsan/cfe/*` and `/proc/hsan/net/swa/*` expose the CFE's own view
  (interfaces, FDB, flows). This is the cheapest way to learn *what the vendor programmed and
  why*; it is completely absent from our image.

Risk/impact: read-only capture is safe; replay writes are only to readable, snapshot-able words
and are revertible. It can answer "does the vendor's native path exist and what does it look
like" without guessing hardware encodings.
[V method; U results]

### Route B - design our own CPU-punt classification (the likely M1 path)

If Route A shows the vendor state is tiny/absent (the W14/W15 conclusion suggests there is no
extra persistent configuration), then the hardware must be given a **new** classification/CPU
delivery configuration by our driver, using the two write engines we have fully decoded:

1. **IFC entry write** (protocol [V], payload field encoding [U]): 5 descriptor words to
   `ifc+0x138/0x13c/0x140/0x144/0x148`, command `ifc+0x120 = 0xc0000000 | (idx & 0x7f)`,
   poll bit31 (busy) <=1001, bit29 = error; plus a 4-word shadow entry through
   `hi_lsw_write_tab128` at `ifc+0x50000 + idx*16`. The eight W10 default entries are the only
   reference payloads we have (and they are exact vendor values).
2. **tag action write** (protocol [V], payload partly decoded): 16-byte entries through the
   same tab128 primitive at module 0xb offset 0x54000 (physical `0x124D4000` + idx*16);
   `tag_write` builds the entry from a 12-byte software descriptor: bits at word0 carry the
   VLAN id (`GENMASK(11,0)`), a `0x40000` flag, a TPID class and priority bits; byte 9 carries
   `0x80 | (mode << 4)` where mode is derived from descriptor byte 8 (2 = double-tag path).
   Field semantics are [I]/[U]; there is no vendor read-back path for the table, but the
   hardware table itself is directly readable (tab128 = memory-mapped).

The design hypothesis for M1 is the classic switch shape:

- one IFC rule per wired ingress port, matching "any frame on port N" (or the port's default
  classification) with an action that **sends a copy to the CPU port (4)** and references a
  **tag action** that pushes a VLAN tag carrying N (or an interface id);
- the Linux side parses the tag, maps it back to the ingress netdev, and hands the frame to the
  bridge/router exactly as if it had been received on ethN;
- optionally the same rule(s) leave normal L2 forwarding to the L2 plane (flood/learn), so
  LAN-to-LAN frames are forwarded in hardware and only broadcast/unknown/routed frames are
  copied to the CPU.

Every element of this design exists in decoded vendor mechanisms (IFC actions, tag actions,
per-port classification) - but the **field encodings are [U]**. Route B is therefore an
experiment loop, not a one-shot write: start from the vendor's W10 payloads, vary one action
word at a time, observe PIE RX completions with `luofu_pie.rxdiag=1`, and keep the mirror
fallback for the management port.
[V protocols; U payload encoding; I design]

### Route C - hardware L2/VLAN bridge offload for LAN-to-LAN (M2, not the first deliverable)

Even if M1 makes multi-port work, LAN-to-LAN unicast still traverses the CPU (software bridge).
M2 offloads that to the hardware L2/flow plane. It requires either (a) a per-port PVID/VLAN
membership table and MAC learning, or (b) installing FDB/flows like the vendor CFE does. The
vendor's L2 learning is a *software* library (`hi_kcfe_lrn_l2`) that programs flows through the
CFE acceleration API; no vendor module writes the L2 window [V]. The hardware L2 table format,
learning enable and the per-port PVID table are therefore all [U] and M2 is deferred until M1
has proved the CPU path and a stock capture has shown the L2 table with traffic.
[V evidence; U format]

**Plan decision:** do Route A first (read-only), then Route B per-port punt for M1. Route C is a
separate follow-on and must not gate M1.

### 2.1 Revision after the E0-a run (2026-11-21) - read this first

The E0-a hardware run refines this section (full analysis: `E0A-ANALYSIS.md`):

- **The exception path works natively, without the mirror, with the correct source port.** The
  `status=37` completions are the host's ARP retries (60-byte minimum frames); with
  `luofu_pie.accepterr=1` the same class was already proven deliverable and parseable on
  2026-11-20 15:30 (the box answered the ARP on the wire). `status=37 / err=1` is an
  exception-class marker, not a frame error: the vendor decode sets the summary no-error bit only
  when `status == 0`, and our driver derives `err` from `status != 0`. The completion `port` field
  (11 = eth2) already provides identity for this class [V].
- **What is missing is the unicast-to-CPU state.** In E0-a the only completions over 17 s were
  60-byte ARP frames; the host's 98-byte IPv4/ICMP frames never appeared (the same was true on
  2026-11-20 15:30/18:30). The switch never routes unicast-to-the-box-MAC frames to the CPU.
  Multi-port needs the same class of state per port. This is most plausibly an L2/FDB or flow
  entry mapping the bridge MAC (and/or unknown unicast) to CPU port 4, or an equivalent per-port
  copy-to-CPU rule - not a missing tag on the ARP frames [I].
- **Milestone split:** **M1a** = unicast-to-CPU on eth2 with the mirror off (ping works);
  **M1b** = the same per port, so the software bridge sees all four ports. M1a is the smallest
  decisive test and can be probed over SSH on the adopted baseline image (mirror kept for
  management; duplicate deliveries while probing are visible in RXDIAG and are an accepted,
  temporary observation state).
- **Route ordering is otherwise unchanged**, but Route A's first target is now the stock
  *unicast-to-CPU* state (stock CFE procfs + IFC banks + L2 with traffic + OPP/PA), and Route B's
  first patch writes a single unicast-to-CPU rule, not a generic tag-action replay.
- The E0-a image 990c5cab booted with **2 CPUs**; the link down/up at 12.9 s/38.9 s is the normal
  bench link cycle, not a new fault. The adopted baseline is now image **c3ef2a50**.


### 2.2 After E0-b (2026-11-21): decode results and the first hardware step

The E0-b read-only capture (ours image 6e9c54d0 vs vendor stock) and its host-side decode
(E0B-ANALYSIS.md) settle the candidate list:

- **tag action entry 1** (0x124d4010): stock w1=0x0000c000. Decoded writer tag_write@0x2eb0:
  descriptor desc[8]=0x08 -> add_type 0, del_type 2 -> selector 4 -> a **tag DELETE action of
  type 2** with no VLAN/TPID. It is not a CPU-punt classifier action by itself; tagact_id is
  referenced by L3-interface and multicast entries in the vendor code, none present/readable on
  our image. Replay = hypothesis test, expected null.
- **pfe_glb+0x2c**: vendor dump_glb_attr prints up_port=high16 (stock 0x1000 = WAN port 12) and
  flow_age_en=napt+0x54 bit8 (stock 1). It is a flow-engine uplink attribute, not an L2/CPU
  delivery control; the flow_age_en half is in the refused NAPT window.
- **L2 l2+0x40000 page**: the vendor hi_pfe_dfx_dump_cnt string map decodes the differing words
  as L2 DFX counters (rx_total/tx_total/rx_drop/tx_drop/port_move/age_drop/sport_clash/
  search_ok_mc/search_fail_mc) - runtime statistics, never a table to replay.
- **EQM** differences are indirect command/data residue; **PIE** differences are our own driver's
  ring config; the **IFC table is identical** to ours (no stock-only classifier entries).

Decision: the minimal justified M1a write set is two independent replay probes, prepared
fail-closed in m1a/ under this directory:

1. probe A - tag action entry 1, four words at 0x124d4010..0x124d401c (stock content
   0x00000000, 0x0000c000, 0x00000000, 0x00000000);
2. probe B (control) - pfe_glb+0x2c = 0x10000034 (up_port high16 0x1000).

The positive M1a signal is an **extra native 98-byte RX delivery on eth2 while the mirror is
still on** (eth2 rx_packets delta about 2x the controlled ping count). Null is the expected
outcome; if null, no further register writes are attempted until the stock procfs decoded
tables (tagact, l3intf, ifc, glb_attr, tocpu_cnt, br_dump, flow/mac) are captured and the
IFC/L3 action encoding is decoded host-side. L2, NAPT, EQM, PIE and DDR are never in the set.

### 2.3 After M1a (both NULL) and the stock decoded procfs: the CPU-punt path is NAPT

The M1a probes were run (2026-11-21) and both were NULL:

- tagact entry 1 write at 0x124d4014 was silently ignored (wrote 0xc000, read 0x0);
- pfe_glb+0x2c=0x10000034 was accepted/read back, but eth2 rx delta stayed 531 = mirror copies
  only. All words reverted; the write experiments are closed.

The consistent-stock read-only procfs capture (hsan-stock/hsan2/, 137 files) then gave the
decisive counter decomposition: ifc rx_total/hit = 992/992, **ifc_tocpu = 104** (the broadcast
class, ~103), **ifc_napt = 170**; napt tx_total = 170, **up_tocpu = 129** (the unicast class,
~130); dp port4 queue 274/274 (all CPU frames = uc130+mc41+bc103); pie ch0/ch1 rx = 191/83.
Therefore: the IFC default rules (the same eight W10 entries our image has) punt broadcast to
the CPU and route unicast to the **NAPT/flow engine**, whose up-direction punt sends unicast to
the CPU. Our image leaves NAPT uninitialized (all config words zero), so the unicast class is
dropped inside the engine before PIE - exactly the E0-a/E0-b symptom (ARP arrives, 98-byte
unicast never does). The tagact entry is a CFE rootif/L3 tag action, not part of this path.

**Next hardware step (prepared, not run):** replay the vendor NAPT/flow init
(hi_pfe_napt_init@0x4314) - 12 register writes with the stock-captured final values, two
engine table-clear commands polled through napt+0x60, table bases inside the reserved switch
flow carveout 0x80400000/1 MiB (the Linux memory node starts at 0x80500000, so no Linux page is
reachable). Artifacts: m1a/napt-init/ (NAPT-INIT-PROPOSAL.md, apply-napt-init.sh,
observe-napt-init.sh, check-napt-path.sh). The planner keeps its default NAPT refusal; the
prototype uses its own explicit 13-address allow-list with the same fail-closed rules.

Expected observable (mirror still on): eth2 rx_packets delta about 2x the controlled ping count
(mirror copy + native NAPT-punt copy), napt+0x1010 up_tocpu and the ifc_napt counter increment;
then the cpu_mirror=0 proof boot. A read-only confirmation first: m1a/napt-init/check-napt-path.sh
before/after a ping must show the IFC napt-class counter rising while up_tocpu stays 0.

---

## 3. Current netdev + driver model (what the first patch builds on)

- **Ports**: GEMAC DT nodes carry `hisilicon,port` = 9..12 (8 = the disabled gemac0); each GEMAC
  registers one netdev and calls `luofu_pie_rx_register(port, ndev)`. Measured mapping:
  `14380000 eth0/port 9?` - careful: the board userspace maps eth0/eth1/eth2 = LAN ports
  9/10/11 and eth3 = WAN port 12; the *phy* mapping measured in ETHERNET-BRINGUP-FRAMING is
  14380000->eth0, 14400000->eth1, 14480000->eth2, 14500000->eth3 [V]. The DT `hisilicon,port`
  values are 9..12 by hardware port, not by eth index [V patch 022].
- **RX**: PIE decode (patch 053) supports three paths: error (port = byte[+9] bits7:4),
  normal (`+16 != 0`: port = byte[+16]), no-status (`+16 == 0`: port = byte[+23] bits7:4);
  tag class = byte[+10] bits5:4 [V]. Port-0 normal completions are the CPU-copy case and are
  delivered to `luofu_pie.cpucopy_port` (11) when `cpucopy_deliver=1` [V]. A completion whose
  port has a registered netdev is delivered there; anything else is dropped and counted [V].
- **TX**: GEMAC `ndo_start_xmit` -> PIE TX descriptor with the port number; egress to the
  switch is via the qid/port map (W2) and works today for eth2 (and for TX onto any port, as
  the far-jack RA observation shows) [V].
- **LSW driver**: one platform device, 17 windows (2 DDR + 15 MMIO), gates W3-W12, a link-state
  notifier (mirror re-apply 200 ms after every MAC link-up and every 2 s), `luofu_switch_ready()`
  exported to GEMAC. The W17-W20 mirror code is the template for both the fail-closed gating
  style and the post-link re-apply hook.
- **Userspace**: br-lan bridges eth0..eth2 (LAN), eth3 is WAN; no switchdev/hardware offload is
  claimed. The clean target is: same userspace, native RX for all four ports.

---

## 4. Write inventory: exact words we can justify today

Legend: **stock** = value read from the stock vendor boot in the 2026-11-20 dumps;
**ours** = value on the release-line armed image. Physical address = window base + offset.

### 4.1 Known-decoded writes NOT yet in the release line

| # | word (physical) | value / operation | evidence | why it is a candidate | risk |
| --- | --- | --- | --- | --- | --- |
| A1 | `0x1200002c` (`pfe_glb+0x2c`) | RMW bits 15..0 unchanged, bits 31..16 = `0x1000`; stock final `0x10000034`, ours `0x00000034` | writer decoded: `hi_lsw_pfe` `write_glb_attr(attr=0)` does `bfi(val, attr, #16, #16)`; stock difference in `2026-11-20-pha-diff.txt` [V] | only persistent stock-vs-ours PFE-global difference; vendor runtime code sets it after `hi_pfe_init` | low (one RMW word, readable, revertible) |
| A2 | `0x124D4000 + idx*16` (`pub+0x54000`, tag action table) | replay exactly the stock-captured 16-byte entries (only the non-zero ones) | `tag_write@0x2eb0` writes tab128 at `0xb054000`; table id `0x708` "tagact" (12-byte key/data) [V]; stock content [U - not captured] | CPU-tag / VLAN-tag actions are the vendor's runtime mechanism; if stock has entries they are the fastest route to identity | medium (unknown semantics; but each word snapshotted and revertible) |
| A3 | `0x12150000 + idx*16` and `0x12160000 + idx*16` (IFC banks) | replay stock-captured entries beyond the 8 W10 defaults | write protocol [V] (W10); stock bank0 entries 1..8 match W10 [V]; stock entries 9+ and bank1 [U] | the classifier is where the CPU-punt action would live; replaying vendor entries is safer than designing payloads | medium |
| A4 | `0x12000000+0x2004`? - not present | - | - | - | - |

**Explicitly excluded (decoded but must not be written):**

- **NAPT engine configuration** (`napt+0x54/+0x60/+0x64/+0x68/+0x70/+0x170` and the table
  bases `napt+0x130..0x13c`): the table bases point at the vendor's reserved DDR carveout
  (`stock value 0x80400000`); on our kernel that physical range belongs to Linux memory, so the
  flow engine would DMA into live kernel memory. The qm-decode lane already reached this
  conclusion ([../../../target-skeleton/docs/incoming/qm-decode/LIVE-DEVMEM-EXPERIMENTS.md](../../../target-skeleton/docs/incoming/qm-decode/LIVE-DEVMEM-EXPERIMENTS.md)
  addendum). Geometry-only replay was also classified as "will not produce native forwarding".
- **`queue`/`flow` DDR windows** (`0x80000000`/`0x80400000`): never mapped, never written.
- **L2 window**: no writes - it is identical on stock and ours and was configured before Linux
  [V]; changing it without knowing the format would be the most dangerous change in the block.
- **EQM indirect command/data registers**: the stock difference in `eqm+0x48..0x70` is the
  last-completed-command residue, not configuration [V W11/W12].

### 4.2 Fully decoded write protocols (safe to implement in the driver)

**IFC 16-byte entry write (tab128 only; the command-engine half has no read path):**

~~~
addr   = ifc_base + 0x50000 + index*16            /* physical 0x12150000 + index*16 */
words  = 4 x u32, entry word0..word3
verify = read back all 4 words, restore snapshot on mismatch
~~~

The W10 default entries (vendor values, index 1..8) are:

| idx | word0 | word1 | word2 | word3 |
| --- | --- | --- | --- | --- |
| 1 | 0x00000000 | 0x00000000 | 0x00000000 | 0x00000000 |
| 2 | 0x00100800 | 0x00000000 | 0x00000000 | 0x00000000 |
| 3 | 0x00001d01 | 0x00000000 | 0x00000000 | 0x00000000 |
| 4 | 0x00001d01 | 0x00000000 | 0x00000000 | 0x00000000 |
| 5 | 0x00001d01 | 0x00000000 | 0x00000000 | 0x00000000 |
| 6 | 0x00001d01 | 0x00000000 | 0x00000000 | 0x00000000 |
| 7 | 0x00001d01 | 0x00000000 | 0x00000000 | 0x00000000 |
| 8 | 0x00100800 | 0x00000000 | 0x00000000 | 0x00000000 |

**IFC command-engine descriptor (5 words, write-only):**

~~~
data   = ifc_base + 0x138, +0x13c, +0x140, +0x144, +0x148
cmd    = ifc_base + 0x120
write: *(u32*)cmd = 0xc0000000 | (index & 0x7f)
poll : read cmd until (cmd & BIT(31)) == 0, <= 1001 times
error: cmd & BIT(29) -> fail the stage
~~~

The W10 engine payloads (vendor values, index 1..8) are in patch 043's
`luofu_switch_v250_ifc_entries` table (they are part of the release line:
patch 043 + the patch-046 correction that writes entries at `idx*16` and uses stock
word0 `0x1d01` for entries 3..7).

**Tag action entry (16 bytes, no vendor read path but the table is readable memory):**

~~~
addr   = pub_base + 0x54000 + index*16             /* physical 0x124D4000 + index*16 */
~~~

Decoded shape from `tag_write` (pseudo-fields, semantics [I]/[U]):
word0 bits11..0 = VLAN id (from descriptor bytes 0..1), bit18 = `0x40000` flag,
bits12.. priority/TPID class bits; byte 9 = `0x80 | (mode << 4)`, where mode comes from
descriptor byte 8: values 0/1 = single-tag path, 2 = double-tag path, 3 = [U];
TPID index from `hi_tpid_get_idx()` stored in low bits of byte 6/8. A tag action is a
**push/strip VLAN tag** description, not a port number: the ingress-port identity has to be
encoded as the **VID** (port N -> VID N) or by writing one tag action per port.

**Per-port mirror (fallback, already implemented in the release line):**

~~~
OPP = opp_base (physical 0x12400000)
OPP+0xd8: bits15:0 = src-port mask word 1, bits31:16 = src-port mask word 2
OPP+0xdc: bits31:16 = 0x600 | ((dst & 0xf) << 4)   (dst 4 = CPU per W19)
OPP+0xc0: |= BIT(6)                                 (enable)
OPP+0xb4: write BIT(0), then clear BIT(0)          (latch pulse)
re-apply 200 ms after every MAC link-up and every 2 s (W19: the registers do not
survive switch port-up)
~~~

### 4.3 What the existing dumps do NOT decode (and what this plan does instead)

The lead's target list asks for three classic switch mechanisms; here is the honest split
between decoded and unknown, so the first experiment can be scoped correctly:

| mechanism | status today | plan |
| --- | --- | --- |
| **tag action / VLAN tag entry shape** | **decoded at the write-protocol level** (`tag_write`): 16-byte entries at pub+0x54000, word0 carries VID (bits 11..0) + a `0x40000` flag, word1 carries `0x80 | (mode<<4)`, TPID class from `hi_tpid_get_idx()`; field semantics [I]/[U] | capture the stock table (U1); replay readable entries; then use a VID=port mapping for CPU tagging (mode 1) |
| **per-port PVID / untagged membership table** | **not decoded**: no vendor module writes the L2 window from Linux [V]; the L2 config words identical on stock and ours are pre-Linux bootloader state; a PVID table could be in that pre-Linux config or behind an unidentified table engine | do not write the L2 window; search for a table that follows the jack/interface in the stock capture (U4/U10); if absent, use explicit per-port IFC rules instead of a PVID table |
| **MAC learning enable** | **not decoded**: the hardware L2 counters prove a search/age block exists, but no readable enable bit is identified and the vendor's "learning" is a software library that programs flows, not L2 rows [V] | M1 does not need learning (software bridge); M2 needs the capture in U4 with traffic to see whether learned entries appear anywhere readable |
| **CPU port tagging for routed traffic** | **mechanism known, format unknown**: CPU destination = switch port 4; PIE completion has port/vport + a 2-bit tag class; the vendor tag action table is the likely in-frame tag source [V/I] | first measure whether native (non-mirror) deliveries already carry port/tag info (U5-U7); only then write a tag action |
| **IFC action encoding ("send to CPU", "forward to port set")** | **write protocol decoded; payload encoding [U]**: the only known vendor payloads are the 8 W10 default entries; the command-engine descriptors are write-only | probe matrix U11: one action bit/nibble per attended session, seeded by the W10 payloads, observed with rxdiag |
| **"untagged forwarding table" row shape** | **not decoded**; not even the owning window is identified | treat as the same problem as the PVID table: find it in the stock capture first; do not invent rows |

Consequence: the plan does not claim to have the full hardware L2/VLAN programming recipe yet.
What it does have is (a) the exact write protocols for the two candidate mechanisms (IFC and tag
actions), (b) exact read plans that decide whether the missing state is vendor-readable, and
(c) a first milestone (M1: native CPU delivery with per-port identity) that does not require the
undecoded L2/VLAN rows at all.

### 4.4 E0-a revision of the candidate write set

The E0-a result moves the candidate priorities:

| candidate | status after E0-a | action |
| --- | --- | --- |
| tag action table replay (A2) | **demoted**: the exception class (ARP) is delivered with no tag; a tag is not needed for M1a identity because the completion port field already carries it | keep as a secondary candidate only if the stock capture shows tagged CPU delivery for unicast |
| IFC bank replay (A3) | **promoted to primary**: look for a stock entry that classifies unicast to the bridge MAC (or a catch-all) with a to-CPU action; if found, replay is the smallest W21 step | E0-b focus |
| L2/FDB state | **new primary candidate**: capture L2/OPP/PA with unicast traffic; a static MAC-to-CPU entry (or unknown-unicast-to-CPU rule) is the most plausible missing state | E0-b focus; no L2 window writes until the format is identified |
| `pfe_glb+0x2c` attr (A1) | secondary (flow-age style attribute, not a delivery enabler) | keep behind its own sub-gate |
| NAPT / DDR pointers | **excluded, unchanged** | never write |

No new writes are added by this revision; it re-ranks the candidates and makes the stock
unicast-to-CPU capture the deciding input. The write planner remains fail-closed and the NAPT/DDR
windows remain refused.

**E0-b outcome (2026-11-21, final for this revision):** the capture was done. The decode
(E0B-ANALYSIS.md) demotes all three original candidates: the tagact word is a tag-delete action
(not a punt classifier), pfe_glb+0x2c is the flow-engine up_port attribute (not an L2/CPU
control), the L2 page is DFX counters, IFC is identical, and EQM/PIE are residue/our-own config.
The section 2.2 two-probe replay (tagact entry 1 + up_port) is the reviewed minimal set; its
expected observable is null and it is exercised only to validate/refute the last readable deltas.

---

## 5. Netdev model and coexistence with the CPU-copy workaround

### 5.1 Model

- **One netdev per switch port, unchanged**: eth0/eth1/eth2/eth3 <-> switch ports 9/10/11/12
  (per DT `hisilicon,port`, validated against the populated qid rows by patch 022). PIE keeps
  the `rx_ports[port]` registration; a native completion with `port = N` is delivered to that
  port's netdev by the existing code - **no netdev-model change is needed for M1**.
- **LAN**: Linux bridge (br-lan) over eth0..eth2, in software. This is the M1 model; the
  hardware L2 plane is not required. Userspace stays standard OpenWrt.
- **WAN**: eth3 (port 12) is routed, not bridged; it needs only CPU RX/TX (same M1 mechanism).
- **M2 (later)**: bridge/VLAN objects offloaded through switchdev over the same netdevs; nothing
  in M1's netdev model blocks it (the switch core device already exists as a platform device).
- **Foreign userspace contract**: none. We do not implement `hi_cfm`/`/proc/hsan`; the CFE is
  not recreated (see [../../LSW-SPEC.md](../../LSW-SPEC.md) section 9).

### 5.2 Coexistence during bring-up (mirror = fallback, never two deliveries)

Duplicate delivery is a correctness hazard: if a frame is delivered both as a native copy and as
a mirror copy, the bridge sees it twice and routed protocols can break. During the M1a devmem
probe (S2b) duplicates are deliberately accepted for observation only (the mirror stays on eth2 so
management is never lost); any real deployment must exclude the port from the mirror mask as soon
as native delivery is confirmed. Therefore:

1. patch 066 (`luofu_switch.mirror_src_mask`) is promoted from the bench-only probe patch to the
   bring-up mechanism: the mirror source mask becomes a parameter, not a hardcoded `BIT(11)`.
2. a new native stage keeps a **per-port native mask**: `luofu_switch.native_ports=<mask>`.
   The effective mirror mask is `(all_wired_ports & ~native_ports) & mirror_src_mask`; for the
   management port (11) the mirror stays on until native delivery for port 11 has been proven
   in a separate attended run.
3. **fallback**: the 2 s re-latch worker keeps running; if the driver's native completion
   counter for a native port does not increase within a timeout after the most recent link-up,
   the port falls back into the mirror mask (the mirror is the only path that is currently
   proven). This is a software policy, not a hardware write, and can be disabled by
   `native_fallback=0` for experiments.
4. the driver refuses to arm the native stage unless the whole W3-W12 chain verified and the
   PIE `arm=1/rxcfg=1` pair is armed (`luofu_switch_ready()` semantics);
5. if the native stage fails its own readback, it restores the snapshot and the mirror mask to
   the pre-step values and fails closed (same style as W10/W12).

### 5.3 Proposed module parameters (all default off except the existing ones)

| parameter | default | meaning |
| --- | --- | --- |
| `luofu_switch.native_rx` | 0 | master gate for the native RX stage (W21) |
| `luofu_switch.native_ports` | `0x1e00` (ports 9,10,11,12) | ports the stage targets |
| `luofu_switch.native_mode` | 0 | experiment selector: 0 = replay captured tagact/IFC entries; 1 = per-port punt entry set; 2 = tag-act only |
| `luofu_switch.native_tag` | 0 | 0 = no tag action, 1 = per-port VLAN tag actions (VID = port) |
| `luofu_switch.native_fallback` | 1 | put a native port back into the mirror mask if no native completion is seen |
| `luofu_pie.cputag` | 0 | PIE RX decode selector for a CPU-delivery tag: 0 = none, 1 = in-frame 802.1Q tag carries VID=ingress port, 2 = 4-byte vendor-style tag (experiment), 3 = reserved |
| `luofu_pie.cputag_base` | 0 | first VID/port value of the mapping for mode 1 |

The PIE side needs one new function: after `luofu_pie_rx_decode()` and before
`luofu_pie_rx_deliver()`, if `cputag` is active and the decoded tag class/frame tag matches,
derive the ingress port from the tag and override `compl->port` (fail closed if the port is
outside the registered range). The frame then follows the existing delivery path.

---

## 6. Bring-up sequence testable with existing tooling

All steps are host-side until a bench window; each hardware step is attended with the recovery
door armed and a UART capture. The tools referenced are the repository's:
`tools/stock-telnet-install.sh`, `tools/uart-capture.sh`, and the on-image static `devmem`.

| step | what | where | writes | exit criterion |
| --- | --- | --- | --- | --- |
| S0 | compile the current release line unchanged (baseline) | host | none | RC=0; image hash recorded |
| S1 | **N0-a: no-mirror cmdline experiment - DONE 2026-11-21 (image 990c5cab)** | bench | none | result: exception punts only (60-byte ARP, port 11), no unicast CPU delivery; see `E0A-ANALYSIS.md` |
| S2 | **N0-b read-only stock-vs-ours capture - DONE 2026-11-21** (ours image 6e9c54d0 vs stock; 9,488 words each, 0 failures); decode in `E0B-ANALYSIS.md`: no readable unicast-to-CPU enabler; only tagact entry 1 and pfe_glb+0x2c are stock-only config words, L2 page = DFX counters | bench + host | none | done; candidate list = m1a/ proposal |
| S2b | **M1a replay probes - RUN 2026-11-21: BOTH NULL**. A (tagact entry 1): write ignored (wrote 0xc000, read 0x0). B (pfe_glb+0x2c): accepted/read back, eth2 rx delta stayed 531 = mirror only. All words reverted; write experiments closed | bench | none left | negative result recorded in E0B-RESULTS/ANALYSIS; mechanism resolved by S2d |
| S2c | **Decoded procfs capture on consistent stock - DONE 2026-11-21** (hsan-stock/hsan2/, 137 files): ifc_tocpu=104 broadcast vs ifc_napt=170 -> napt up_tocpu=129 unicast; dp port4 274/274. Conclusion: the CPU-punt rule for unicast is the NAPT/flow engine path, not an IFC table entry | bench + host | none | mechanism resolved; next step S2d |
| S2d | **NAPT init replay prototype (next hardware step)** - m1a/napt-init/apply-napt-init.sh (12 register writes + 2 polled clear commands; stock values; flow table bases in the reserved switch carveout). Read-only confirmation first: check-napt-path.sh before/after a ping (IFC napt-class counter rises, up_tocpu stays 0) | host + bench | 12 words, NAPT window, explicit allow-list | readback exact and commands complete; eth2 rx delta about 2x the ping count + napt up_tocpu increments -> go to S2e; null -> decode hi_pfe_pub_init flow setup next, no further writes |
| S2e | **M1a proof boot** - only if S2d shows the native signal: boot the release-line image with `luofu_switch.cpu_mirror=0` (the E0-a build recipe), attended with UART; ICMP unicast must work without the mirror | bench | cmdline only | M1a proven (unicast-to-CPU on eth2 without the mirror) |
| S3 | **N1 (revised): replay the unicast-to-CPU candidate set** (stock-readable IFC/L2/OPP words preferred) - via `plan-writes.sh` (devmem) or the W21 driver stage; mirror excludes only the experimented port | bench | candidate words | readback exact; with `cpu_mirror=0` on the next boot, ICMP unicast is delivered natively to eth2 (M1a); rollback otherwise |
| S4 | **N2: per-port punt classification** (Route B) if S3 shows nothing: build the W21 driver stage with the tag/punt entries, mirror off for the target port, one entry per attended run | host + bench | new driver stage | native delivery with correct ingress identity for the target port |
| S5 | repeat S4 for each wired port; keep management port on mirror until last | bench | stage writes | all four ports native; bridge/router works |
| S6 | switch off the mirror for all ports, run the 15-minute soak (canonical soak script if present), then remove the request for the CPU-copy workaround from the bench command line | bench | driver stage only | multi-port LAN and WAN routing with no mirror; M1 met |
| S7 | (optional) M2 hardware L2 learning/offload study from the same captures | host + bench | later stage | not gating |

**Fail-closed rules for the driver stage:** every new write is snapshotted and stable-two-reads
checked; every write is read back and compared with the computed expected value; any mismatch
restores all stage writes (and the mirror mask) and refuses readiness; `luofu_switch_ready()`
grows a native bit only when the stage verified; GEMAC netdevs keep refusing to register if the
chain is incomplete (existing behaviour). No new write set is enabled by default.

**Rollback between bench sessions:** the canonical command line (with the mirror) is the
fallback; the stock pair on B is the recovery path. `plan-writes.sh` always records the exact
pre-write words, and the devmem experiments must be reverted before any reboot that is not part
of the experiment.

---

## 7. First patch design (release-line W21 stage) - not compiled here

**Ordering after E0-b and M1a (both NULL):** the first hardware step is still not this driver
patch. It is the NAPT init replay prototype (m1a/napt-init/, section 2.3 / 6 S2d). If that
prototype produces the native duplicate signal, the driver work becomes a **W22 NAPT/flow init
stage**: the same register sequence behind a gate (luofu_switch.naptinit=1), with the flow table
bases derived from the DT flow window, the two clear commands polled with the vendor timeout, and
the stage re-applied from the link worker like the mirror. The W21 IFC/tag stage is deferred: the
IFC defaults are already installed and the tag action is not on the CPU-punt path. The remaining
decode for W22 is the hi_pfe_pub_init flow-table/manager setup, only if the register-only replay
is insufficient.

The first patch is intentionally **data-driven and fail-closed**, in the same style as W10/W12.
It is designed against the current release-line driver
(`luofu-switch.c` 3855 lines, `luofu-pie.c` 1689 lines in the canonical tree). This section is a
design; it is not a compiled patch and must be reviewed before it is cut into the release series.

### 7.1 Files and symbols

| file | change |
| --- | --- |
| `drivers/net/ethernet/hisilicon/luofu-switch-regs.h` | add: `LUOFU_SWITCH_IFC_TAB128_BANK1 0x60000`, `LUOFU_SWITCH_IFC_MAX_ENTRIES 128`, `LUOFU_SWITCH_TAGACT_OFF 0x54000`, `LUOFU_SWITCH_TAGACT_ENTRIES 64`, `LUOFU_SWITCH_TAGACT_WORDS 4`, `LUOFU_SWITCH_PFE_GLB_ATTR0_OFF 0x2c`, `LUOFU_SWITCH_PFE_GLB_ATTR0_SHIFT 16`, `LUOFU_SWITCH_PFE_GLB_ATTR0_FLOW_AGE 0x1000` (stock-observed), `LUOFU_SWITCH_NATIVE_PORTS 13` (ports 0..12), the W21 payload tables |
| `drivers/net/ethernet/hisilicon/luofu-switch.c` | add: the `native_rx`/`native_ports`/`native_mode`/`native_tag`/`native_fallback` params; `struct luofu_switch_ifc_entry` bank-1-aware; `luofu_switch_tab128_read()/write()` helpers (the W10 code currently has a single-bank inline loop); `luofu_switch_native_snapshot()`, `luofu_switch_native_program()`, `luofu_switch_native_restore()`; readiness bit `native_ready`; a native-completion counter hook; the mirror-mask computation change (`effective_mask`); re-apply the native stage from the existing post-link worker (it already re-applies the mirror) |
| `drivers/net/ethernet/hisilicon/luofu-switch.h` | new accessor `bool luofu_switch_native_port(unsigned int port)` (PIE needs it to accept/deliver native completions for the target mask) |
| `drivers/net/ethernet/hisilicon/luofu-pie.c` | add `cputag`/`cputag_base` params; a `luofu_pie_decode_cputag()` helper called from `luofu_pie_rx_decode()`/`luofu_pie_rx_deliver()`; a per-port native RX counter for the fallback policy |
| `drivers/net/ethernet/hisilicon/luofu-gmac.c` | none for M1 (per-port netdev already correct); WAN routing is userspace |
| bindings/docs | no DT change for the devmem prototype; the W21 stage uses no new DT property. If tags become the permanent design, the switch binding documents the tag mode (not this patch) |

### 7.2 Write order inside the stage (fail-closed)

**Priority after E0-a:** the first stage target is a single unicast-to-CPU rule (or the replay of
the stock-readable equivalent), not a tag action. The tag-action paths are only worth enabling
after M1a has been observed, and only if the stock capture shows tagged CPU delivery.

1. refuse unless `luofu_switch_ready()` (W3-W12) and PIE `arm/rxcfg` are ready;
2. snapshot all target IFC tab128 words and tagact words, reading each target twice and
   requiring stability; snapshot `pfe_glb+0x2c`;
3. optional A1: RMW `pfe_glb+0x2c` bits31..16 = current | `0x1000` (a single attributable
   word, behind a separate sub-gate `native_attr=1` so it can be isolated);
4. mode 0 (replay): write the captured stock values as compiled-in constants with their
   provenance recorded in the patch comment; the patch ships only entries whose stock capture is
   unambiguous and refuses otherwise;
5. mode 1 (punt): write one IFC entry per `native_ports` bit using the W10-proven engine
   protocol + tab128 table, with the tag action index in the action word once known
   ([U] until the probe matrix resolves it; the patch refuses in mode 1 if the entry payload
   table is not filled);
6. read back every word; on mismatch restore (tab128 restorable; engine descriptors are
   write-only so the stage acknowledges them and states that fact, exactly like W10);
7. set `native_ready`; log the effective mirror mask; PIE starts tag decoding;
8. `remove()`/failure path restores tab128/tagact/attr words and the mirror mask.

### 7.3 PIE hook

In `luofu_pie_rx_deliver()` before the existing port lookup:

~~~
if (compl->port == 0) {
        port = luofu_pie_decode_cputag(pie, skb, compl);  /* 0 = no tag */
        if (port > 0 && port <= LUOFU_PIE_RX_PORT_MAX)
                compl->port = port;
}
~~~

`luofu_pie_decode_cputag()` implements the modes selected by `luofu_pie.cputag`:
mode 1 - inspect the in-frame 802.1Q tag (VID -> port mapping, `cputag_base` offset), strip the
tag before `eth_type_trans()` and set the correct netdev; mode 2 - 4-byte vendor-style tag
(format to be determined by the probe matrix; the function fails closed until the format is
known); mode 0 - no-op. The current code explicitly leaves an in-frame tag to
`eth_type_trans()`, so mode 1 must strip it (only when it is our injected tag, identified by a
reserved VID range).

### 7.4 Validation for the patch itself

Host-only: full kernel compile in the release-line tree (WERROR, 0 warnings from the changed
files), `dt_binding_check` unchanged (no binding change), checkpatch 0 errors, and the series
apply test 001-058+W21 RC=0. Hardware validation is S3-S6 of section 6 and only runs after the
read-only phase.

---

## 8. Risks

| risk | severity | mitigation |
| --- | --- | --- |
| writing IFC/tagact entries with unknown field encodings misclassifies traffic and drops the management path | high | never touch the management port's delivery first; mirror keeps eth2 alive; all tab128 words snapshotted and restored; driver stage fails closed on readback; devmem prototype prints/executes one word set at a time with rollback |
| duplicate delivery (mirror + native) breaks the bridge/router | high | per-port native mask excludes the port from the mirror mask; fallback policy; validation via packet counters both sides |
| tag parsing mistakes deliver frames to the wrong netdev | med | mode 1 maps a reserved VID range only; unknown tags are dropped and counted (fail closed); rxdiag logs the raw descriptor |
| the native stage depends on runtime state that is not persistent (CFE-installed) | med | Route A read-only capture decides before any write; if the state is not readable/persistent, Route B designs our own entries |
| the LSW drops the mirror registers at link-up (already observed) | known | reuse the existing post-link worker for the native stage too |
| CPU load if all ingress is punted at line rate | med (bench) | bench traffic is a single host; M2/offload is the line-rate answer; document as an M1 limitation |
| no bootable 6.18 target / bench availability | high (schedule) | S1/S2 are cmdline + read-only and need only the existing canonical image; the driver work (S4) can proceed host-only |
| release-series hygiene | med | W21 stays under `docs/incoming/` until the read-only phase proves the direction; the curated series is not edited |

---

## 9. Effort, order and can-do-offline summary

| stage | work | depends on | can do offline |
| --- | --- | --- | --- |
| N0 read-only experiment design + capture/diff tooling | this directory | - | **yes** (done here) |
| N0-a no-mirror cmdline experiment (S1) | command line only, no build | bench window | no |
| N0-b stock-vs-ours runtime capture + diff (S2) | run the two scripts | bench + stock boot | diff tooling yes, data no |
| N1 replay candidate words (S3) | devmem or early W21 mode 0 | S2 result | code/tooling yes |
| M1a unicast-to-CPU on eth2 (S2b/S3) | one rule or one stock-word replay; mirror-off boot to confirm | S2 result | host-side prep yes; signal needs bench |
| N2 per-port punt entries (S4/S5) | W21 driver patch, probe matrix | S3 result + bench | most of it yes |
| N3 M2 L2/learning offload study | new RE/capture | M1 proven | partially |
| M1 exit | all four ports native, mirror off, soak | N2 + bench | no |

Rough estimate: N0-a/N0-b one attended session each (one our image, one stock); N1 1-2 attended
sessions; N2 3-6 attended sessions (matrix + driver iterations) plus 2-4 host sessions per
iteration; M1 exit after a clean soak. Every hardware step is run by the lead/human, never by
this document.

---

## 10. Evidence map (claim -> artifact)

| claim | artifact |
| --- | --- |
| release line stages, canonical cmdline, gates | `target-skeleton/README.md`; release-line manifest |
| mirror registers/value shapes | `hi_lsw_pfe!hi_port_set_mirror@0xc10` (fixture disassembly); patches 054-057 |
| CPU port = 4 | W19 dwell log `build/evidence-bench-gates/2026-11-20-dwell-cpuport4.log` [V] |
| CPU-copy has no source identity | `MULTI-PORT-PLAN.md` "Measured result"; commit 117ea55 |
| PIE RX decode paths + tag class | patch 053 + `luofu-pie.c` `luofu_pie_rx_decode()` (tree source) |
| port -> netdev registration | `luofu-pie.c` `luofu_pie_rx_register/rx_port_netdev`; patches 023/024 |
| IFC entry write protocol + W10 payloads | patches 043/046; `hi_lsw_pfe!hi_pfe_ifc_init@0x794`, `ifc_write@0x5b4` |
| tagact table location/id | `hi_lsw_pfe!tag_write@0x2eb0` (tab128 at `0xb054000`); pub-init .data table (id 0x708 "tagact") |
| pfe_glb+0x2c attr writer | `hi_lsw_pfe!write_glb_attr@0x2210`; stock diff `2026-11-20-pha-diff.txt` |
| L2 window has no Linux writer; pre-Linux config | fixture nm/disassembly of hi_lsw_*; `2026-11-20-pha-stock.txt` |
| NAPT DDR pointers unsafe | qm-decode addendum `LIVE-DEVMEM-EXPERIMENTS.md` |
| stock-vs-ours persistent state matches | `2026-11-20-lsw-diff.txt`, `2026-11-20-pha-diff.txt`, W11/W14/W15 sections of `LSW-SPEC.md` |
| per-port mirror mask patch | `../multi-port-probe/066-net-lsw-mirror-src-mask.patch` |
| E0-a mirror-off result (exception punts only, no unicast CPU delivery) | `MINIMAL-EXPERIMENT.md` appended result; ledger 2026-11-21 18:10; `build/evidence-bench-gates/e0a-mirroroff/e0a-boot-excerpt.txt`; analysis `E0A-ANALYSIS.md` |
| status=37 semantics (class marker, not frame error; valid ARP delivered) | `E0A-ANALYSIS.md`; vendor `hi_pie_rx_get_desc@0xf14`; ledger 2026-11-20 15:30 (ARP reply on the wire) |
| exception path carries source port | E0-a RXDIAG `port=11`; ledger 2026-11-20 15:30 |
| E0-b stock-vs-ours capture (30 candidates / 72 counters) | `E0B-ANALYSIS.md` sections 1-5; `E0B-RESULTS.md` decode review; capture dirs build/evidence-bench-gates/e0b-* |
| tagact entry 1 is a tag-delete action (writer decoded) | `E0B-ANALYSIS.md` section 2; hi_lsw_pfe!tag_write@0x2eb0 / tag_dump@0x300c |
| pfe_glb+0x2c = up_port + flow_age_en | `E0B-ANALYSIS.md` section 3; hi_lsw_pfe!write_glb_attr@0x2210 / dump_glb_attr@0x218c |
| L2 l2+0x40000 = DFX counters | `E0B-ANALYSIS.md` section 4; hi_pfe_dfx_dump_cnt string map |
| M1a probe scripts, plans, runbook | `m1a/` (SESSION.md, probe-*.plan, apply-*.sh, observe-m1a.sh) |
| M1a probes both NULL (tagact ignored; up_port no effect) | `E0B-ANALYSIS.md` section 11; `E0B-RESULTS.md` M1a results; m1a/SESSION.md closeout |
| CPU-punt mechanism = IFC broadcast->CPU + IFC unicast->NAPT->CPU | `E0B-ANALYSIS.md` section 11; stock procfs counters build/evidence-bench-gates/hsan-stock/hsan2/ |
| NAPT init write path decoded (hi_pfe_napt_init) + reserved flow carveout safety | `m1a/napt-init/NAPT-INIT-PROPOSAL.md`; fixture hi_pfe_napt_init@0x4314; DT memory@80500000 + switch reg flow window 0x80400000/1MiB |
| NAPT init prototype scripts (fail-closed, host-tested) | `m1a/napt-init/` (apply-napt-init.sh, observe-napt-init.sh, check-napt-path.sh) |
