# CFE / switch full reversal (read-only stock dump + static decode)

Status 2026-11-21. This directory is the full switch/CFE reversal lane requested after M1a was
proven. It contains:

| file | purpose |
| --- | --- |
| DUMP-REQUEST.md | the exact read-only stock dump the lead can run: /dev/mem dd ranges (carveout + MMIO table banks + PIE), traffic phases p0..p5, procfs text state, second snapshots, exfil/integrity steps, and the minimal subset if time is short |
| collect-stock-dump.sh | on-box POSIX script that performs the read-only dump into /tmp/cfe/<phase>/ (dd + cat only; no register writes) and prints the transfer commands |
| analyze-cfe-dump.py | host decoder: readable MMIO tables (tagact, l3intf, nhp, car, car_profile, mcc userll, ifc tab128, flow/l3/nhp counters), carveout MAC/IP/pointer scan, phase diffs, markdown + JSON report |
| FLOW-MODEL.md | the static decode: module/call graph, complete table-manager map with hardware locations, l3intf/tagact/userll field layouts, flow path, status classes, and what remains open |
| OUR-SIDE-VALIDATION.md | the our-image dump validation: IFC/NAPT/L2 bit-exact checks, solved flow-table DDR geometry (exact 1 MiB partition), queue-window residual-data finding, counters/classes, and the remaining read requests |
| M2-DESIGN.md | optional hardware L2/VLAN/flow offload design (documentation only; not needed for the release line) |

Safety: the stock dump was designed as read-only (dd from /dev/mem, cat of procfs) but is **not
executable**: the vendor 5.10 kernel returns Bad address for every /dev/mem read and has no
editable bootargs env, so the stock runtime CFE tables stay inaccessible. What was possible is the
our-image dump (W22 family) decoded in OUR-SIDE-VALIDATION.md: it validates the readable parts and
solves the flow-table DDR geometry exactly, but cannot validate vendor entry values. Remaining
reads (all read-only, our image) are listed in DUMP-REQUEST.md section 0c. The release series,
curated repos and the device are not modified by this lane.
