# Optional M2 design: hardware L2/VLAN/flow offload (documentation only)

Status 2026-11-21. M2 is explicitly **not needed for the release line**: after W22 the D1 data path
(native CPU punt + software bridge over the four netdevs) is proven and is the release scope. This
document records the design if the full reversal later makes the hardware L2/VLAN/flow offload
programmable, so the model is not lost.

## 1. What is already programmable, and with what

Decoded mechanisms (see FLOW-MODEL.md; the dump request closes the remaining layout questions):

- IFC classifier (ifc+0x50000 tab128 keys + 5-word action engine): L2-L4 key match, action with
  igr/egr bitmap, label, priority, pq/queue, car, statistics. The eight vendor defaults are
  already installed (broadcast/ARP punt + unicast-to-NAPT path).
- L3 interface table (pub+0x5b000 key / pub+0x5c000 data, 16B each): interface context with sip or
  smac, mtu, session, ppp/vlan mode, ttl-dec, statistics index and a 6-bit tagact_id reference.
- Tag action table (pub+0x54000, 16B): S/C-tag add/delete actions with VLAN/TPID/priority fields.
- Next-hop (pub+0x59000), CAR (pub+0x55000) and car_profile (pub+0x53000) tables.
- NAPT/flow engine: v4/v6 flow tables in the reserved flow DDR window (bases napt+0x130..0x13c;
  engine insert/delete through napt command registers; layout to be finalized by the carveout
  dump). This is the L3/L4 offload path.
- MCC tables/window (multicast user links at mcc+0x44000; the multicast class counters) and the
  queue/EQM tables in the queue DDR window (W7 decode).
- Learning: the vendor learns in software (hi_kcfe_lrn_*), then installs flow/IFC entries through
  the ADP API in hi_kcara (hi_adp_flow_add/del/get_age). There is no hardware FDB writer in Linux;
  the CFE userspace is the only owner of the L2/flow program today.

## 2. Three M2 options, in increasing order of decode effort

**M2-A - flow offload only (L3/L4, least risk).** Keep the Linux bridge software; offload only
"established flow" entries (TCP/UDP) into the NAPT v4/v6 tables, mirroring what the CFE ADP layer
does. Implementation: a switchdev/nft-flow-offload hook in luofu-switch.c that:
- allocates flow table ids through the same NAPT table-manager contract (decoded: flow managers,
  key/data 16/28B v4 and 40/52B v6, counters in napt+0x40000);
- builds the 68-byte v4 descriptor (dip/sip/sport/dport/proto/igr_vport/action flags/age) from the
  offload event and inserts it through the decoded NAPT command registers;
- deletes/ages on event removal, with a gated snapshot/replay contract like W7/W10/W22.
Prerequisite: the carveout dump to pin the hardware entry fields and the engine insert sequence
(the logical fields are known; the physical halves are the dump's job). Benefit: CPU bypass for
routed flows, meaningful even with software bridging.

**M2-B - bridge/VLAN offload (port-based VLAN + CPU punt rules).** Use the IFC/l3intf/tagact tables
to implement a static, Linux-controlled bridge model: per-port PVID/untagged membership via IFC
entries + tag actions; FDB entries to the CPU or to ports via IFC/l3intf actions; unknown
unicast/broadcast to CPU. This needs a full decode of the IFC action encoding (partially known;
the engine descriptors are write-only, so a bench probe matrix is the only way) and the l3intf/tag
field semantics validated by the dump. Implementation shape: a switchdev bridge notifier
(br_port_attr_set, br_fdb_update, vlan_filtering) that programs a small, closed set of entries and
never forwards frames in hardware between ports that Linux intends to flood; DSA-like port model is
not required (the four ports are separate netdevs in our current model).
This is the option that would let the hardware forward LAN-to-LAN traffic without the CPU; it is
only worth starting after the M1b measurements show the software bridge is the bottleneck.

**M2-C - full hardware L2 bridge.** Reproduce the vendor CFE L2/flow program including learning
and aging in hardware (or a userspace/learning daemon like the vendor's hi_kcfe_lrn_*). This needs
the GPL CFE userspace or a complete decode of the ADP/L2 learning API and its DDR tables; it is
the largest scope and the least justified for a release-target device whose D1 path works.

## 3. Design constraints that apply to all M2 options

- **Gates and fail-closed**: a new module parameter (luofu_switch.offload=1) with the same
  pattern as W22: refuse unless the base chain and W22 are ready; snapshot every written entry,
  read back what is readable, restore on mismatch; never block the D1 path if the offload stage
  cannot verify.
- **Carveout safety**: the flow window is outside Linux RAM (memory node starts 0x80500000), but
  the queue window is the live TX queue area of the working data path. Any table-base change must
  be validated against the DT windows and never overlap the queue window; the existing W22 check
  (flow_base + size <= RAM base) is the template.
- **No CFE resurrection**: M2 should program the hardware with our own state machine. Reproducing
  /proc/hsan or the CFE hooks is out of scope (LSW-SPEC section 9).
- **Bench burden**: every M2 option needs its own probe matrix and rollback session; treat the
  full reversal as documentation-first, implementation-later.
- **Validation**: M1b-style per-port/LAN-to-LAN tests must pass on the D1 path first, so an
  offload regression is always attributable and can be disabled by a command-line token/image
  variant.

## 4. What the current phase must deliver to make M2-A/B possible

1. The carveout dump decode: v4/v6 entry layout, counter/age placement, bucket geometry (M2-A).
2. The MMIO table dump decode: l3intf/tagact/nhp/userll field semantics (M2-B).
3. A status/class catalogue from PIE RX under the stock traffic matrix (CPU punt classes for
   broadcast/unicast/multicast/flow hits).
4. A documented ADP/IFC action encoding or a bench probe matrix plan for the write-only engine
   descriptors (M2-B).
When those exist, M2-A is a bounded, testable next step; M2-B/C stay documented options.

## 5. Verdict after the our-side dump (2026-11-21)

- **M2-A (flow offload into NAPT v4/v6): NOT READY / deferred.** The geometry is now solved
  (8192 buckets + fixed arenas: v4 32B slots x8192, v6 88B slots x8192, exact 1 MiB), but the
  byte layout inside an entry slot and the engine insert/delete entry encoding were never
  observed on hardware: stock is inaccessible (no /dev/mem), and our image never allocates a
  flow. Before any M2-A code, run a bench-only controlled insert probe: one benign IPv4 flow
  programmed through the decoded NAPT command sequence, then a carveout dump to read the bucket
  and entry bytes (status/counter/class unchanged, fully gated and reversible). That is the
  cheapest way to close the remaining [I]; do not implement offload before it.
- **M2-B (bridge/VLAN via IFC/l3intf/tagact): conditionally feasible, not ready.** Our-side dump
  confirms the readable table addresses and decoder path, but all CFE value tables are zero on our
  image (expected: our driver never programs them) and stock is inaccessible, so no real vendor
  entry has ever been observed to validate the static field maps. The IFC action engine is
  write-only (only the last descriptor reads back), so a bench probe matrix is required for the
  action encoding. Verdict: keep as documentation; wait for the static/table decode evidence and
  the M1b data-path measurements before spending bench time.
- **M2-C (full hardware L2 bridge/CFE userspace): not feasible** without the GPL CFE userspace (not
  in the extracted rootfs) or a complete ADP/learning API decode.
- Nothing in M2 blocks the release line: W22/M1b remain the path; the reversal lane now has the
  complete readable-map and table geometry record.

## 6. Update after the real stock capture (2026-11-22)

The stock CFE boot state is now known (STOCK-DUMP-ANALYSIS.md): in the plain-LAN configuration the
vendor populates only the single tagact delete-tag row, the IFC defaults and the NAPT/queue config;
the NAPT flow tables stay byte-identical/empty even after 200 ICMP pings (the pings take the CPU
punt class). So the capture has not exercised any M2 mechanism yet: M2-A still needs a stock phase
with routed/LAN-to-LAN forwarding to create flow entries and reveal the 32 B/88 B slot encoding;
M2-B still needs a phase where l3intf/userll/userll_ext are actually populated (bridge learning/
routing/multicast). Until then the verdict is unchanged: W22/M1b is the release path, M2 remains
documentation, and the next decisive input is one routed/LAN-to-LAN stock phase (second host or
WAN peer), not more static analysis.
