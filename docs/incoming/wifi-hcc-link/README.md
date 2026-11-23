# wifi-hcc-link - vendor host-side HCC link/event handshake: determination + counterpart

**Lane:** wifi-hcc-link (shared task task-75), host-only, 2026-11-23.
**Read [REPORT.md](REPORT.md) first** - it holds the negative proof, the firmware
cross-reference, the alternative lane and the evidence ledger. [SPEC.md](SPEC.md)
is the host counterpart specification; [BENCH-PROCEDURE.md](BENCH-PROCEDURE.md)
is the attended ladder and the stop rules.

## Result in one line

The vendor host drivers (Hi5622V100 2.4.15 and 2.5.24, plat+wifi) send **no** HCC
link-create / link-register / event-register H2D message in any init or release
path - the host-side HCC link/event layer is host-RAM-local and the firmware's
per-link `+0x4B0` contexts are firmware-internal boot state. The delivered host
counterpart is therefore a bounded read-only observability pack plus one
vendor-attested payload-free mailbox id-1 wake that measures whether the firmware
consumes H2D traffic at all; it never posts id 5, never writes out[5], never
touches device RAM and has no fabricated "link create" frame.

## Quick start (host only; no device contact here)

    ./tools/build.sh --gen-patches    # regen patch 0002, then build the module
    ./tools/build.sh                  # build only (uses committed patches)
    # module-only compile; no image build, no router contact

The build copies `../wifi-transport-port/b3`, applies
`../wifi-h2d-inject/patches/0001-hi5622-pciet-h2d-inject-b3.patch` then this
lane's `patches/0002-hi5622-pciet-hcc-link-b3.patch`, and compiles the external
module against the already-built 6.18.60 tree.

## Layout

| path | what |
| --- | --- |
| `REPORT.md` | negative proof, firmware cross-reference, deliverable, next lane, evidence |
| `SPEC.md` | host counterpart spec: sysfs interfaces, address contract, decision table |
| `BENCH-PROCEDURE.md` | attended ladder L0-L3, stop rules, post-run artifacts |
| `files/pciet_hcc_link.c` + `.h` | kernel extension: `hcc_link_status` / `hcc_link_wake` / `hcc_link_plan` |
| `patches/0002-hi5622-pciet-hcc-link-b3.patch` | compile-ready add-on patch (on top of the h2d-inject 0001 patch) |
| `tools/build.sh` | apply 0001+0002 and build the module (host-only) |
| `tools/gen-patches.sh`, `tools/apply-edits.py` | deterministic patch regeneration |
| `tools/ko_xref.py` | relocation-based call graph of the vendor `.ko` files (sender census / init reachability) |
| `tools/fw_xref.py` | recursive-descent Thumb-2 call graph of FIRMWARE.bin |
| `tools/h2d_reach.py` | H2D receive-root -> link/event reachability |
| `tools/census.sh` | one-shot regeneration of the REPORT sender census |

## Hard rules encoded

* never write CA `0x400392F0` (out[5]) - build asserts + gated writer; read-only checkpoint;
* never post mailbox id 5 (firmware BUG trap); the only posted bit is `BIT(1)`;
* no device RAM/IO writes beyond `out[0]`/`out[2]` in the wake probe; per-link
  `+0x4B0` words are read-only;
* reads bounded to the mapped region-5 CPU window (CPU < `0x198000`) and
  region-3 IO window; the RC misc window `0x10161000` is never touched;
* UART is never used by this lane.

No vendor binary, firmware byte-run or secret is committed; the tools read the
local artifacts at runtime and print derived annotations only.

