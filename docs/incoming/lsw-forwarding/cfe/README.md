# CFE / switch full reversal (read-only stock dump + static decode)

Status 2026-11-21. This directory is the full switch/CFE reversal lane requested after M1a was
proven. It contains:

| file | purpose |
| --- | --- |
| DUMP-REQUEST.md | the exact read-only stock dump the lead can run: /dev/mem dd ranges (carveout + MMIO table banks + PIE), traffic phases p0..p5, procfs text state, second snapshots, exfil/integrity steps, and the minimal subset if time is short |
| collect-stock-dump.sh | on-box POSIX script that performs the read-only dump into /tmp/cfe/<phase>/ (dd + cat only; no register writes) and prints the transfer commands |
| analyze-cfe-dump.py | host decoder: readable MMIO tables (tagact, l3intf, nhp, car, car_profile, mcc userll, ifc tab128, flow/l3/nhp counters), carveout MAC/IP/pointer scan, phase diffs, markdown + JSON report |
| FLOW-MODEL.md | the static decode: module/call graph, complete table-manager map with hardware locations, l3intf/tagact/userll field layouts, flow path, status classes, and what remains open |
| OUR-SIDE-VALIDATION.md | the our-image dump validation: IFC/NAPT/L2 bit-exact checks, solved flow-table DDR geometry (exact 1 MiB partition), counters/classes, remaining reads |
| STOCK-DUMP-ANALYSIS.md | the real vendor dump (mmap physdump workaround): stock boot state decoded, tagact row, empty tables, stock==W22 flow window byte-identical, queue-window behaviour, counters, next stock phases needed |
| ROUTED-PHASE-ANALYSIS.md | the routed/WAN stock phase: negative result (box-terminated traffic => zero flow/table writes), deltas decoded, and the exact next forwarded-through-the-box phase recipe/expected observables |
| FWD-PHASE-ANALYSIS.md | the true forwarded LAN->WAN NAT stock phase: first real vendor writes decoded (tagact/l3intf/nhp layouts), NAPT DDR arenas still empty, M2-A closed / M2-B documentation, minimal remaining reads |
| decode-stock-dump.py | regenerates the stock analysis markdown from the cfe-stock dump directory |
| M2-DESIGN.md | optional hardware L2/VLAN/flow offload design (documentation only; not needed for the release line) |

Safety: the stock capture was read-only (mmap of /dev/mem via a brokered static ARM physdump).
The vendor kernel blocks /dev/mem read() but mmap works; the capture is in cfe-stock and decoded in
STOCK-DUMP-ANALYSIS.md. It shows the stock boot state is byte-identical to our W22 image in the
flow window, and that the plain-LAN config populates only tagact + IFC defaults + NAPT/queue config
(no l3intf/userll/flow entries). Remaining work is one stock phase with real forwarding/offload
(second host or WAN peer) to populate vendor tables; no device writes. The release series,
curated repos and the device are not modified by this lane.
