# wifi-dp-txrx

First MAC data-plane TX/RX for the Hi5622V100 clean-room driver
(Cudy WR3000 v2.0, luofu v2): 802.11 frame -> 8-byte MSDU-info ring entry ->
device-RAM 12-byte ring-pointer table publish -> TX completion read-back, plus
the first RX capture path (completion-ring counter poll + host-DRAM 0x28-byte
RX descriptor scan, no ring pointers moved).

**Lane:** wifi-dp-txrx (shared task task-122). Host-only, compile-verified
against the project 6.18.60 tree, **not fired at the device**. 2026-11-26.
Builds directly on `../wifi-datapath-bringup` (E1 PASS: dp_setup rings +
TXQ-table address sync 0x0320) and `../wifi-datapath` (static RE).

| file | contents |
| --- | --- |
| REPORT.md | main report: what is new (published wptr, device rptr, completion magic, frame layout), the exact host sequence, integration model, [U]/risks, expected bench observables, evidence index |
| FRAME-SPECS.md | byte/register specs: MSDU ring entry ctl bits, 12-byte table entry publish, 20-byte host descriptor layout, 802.11 builders, RX 0x28-byte descriptor + capture, address/window mapping |
| BENCH-LADDER.md | attended ladder F (F0-F5): preconditions, expected observables, decision matrix, stop rules |
| BUILD-NOTES.md | measured build/test evidence + handoffs |
| src/dp_txrx.h/.c | portable, device-free logic (host selftest 254/254): builders, ring arithmetic, table entry publish model, RX descriptor parse/capture, bookkeeping |
| src/hi5622_dp_txrx.h/.c | kernel extension (one coherent TX frame buffer, four sysfs actions, read-only RX capture) |
| patches/0001-hi5622-dp-txrx.patch | the **only** edit to the E1 base lane: bind call at the end of dp_setup, release calls, four attributes in the existing dp group, two Kbuild objects |
| tools/build.sh | host selftest + static decode + sequence validation + patched-module build (0 warnings) + publication checks |
| tools/dp_txrx_selftest.c | 254 host checks |
| tools/dp_txrx_static_decode.py | 87 assertions that re-derive every fact from the local vendor .ko/FIRMWARE.bin (read in place; nothing vendor is committed) |
| tools/patch_builder.py | reproducible patch generator (patch drift is a build failure) |
| tools/validate_sequence.py | validates sequences/ladder-f-txrx.json through the bench tooling |
| sequences/ladder-f-txrx.json | protocol sequence: boot -> ctx -> E1 setup -> 0x0320 -> 3 TX posts -> read-only RX capture -> packs |

Verification (authoring host):

    ./tools/build.sh
    # 254/254 host, 87/87 static, 12/12 sequence,
    # module 540,936 B sha256 aae8487b4e4dad0c8ec0e20b33ef718e68a4e8a92d78f7f4c11afe1b5c12c502,
    # vermagic 6.18.60, 0 warnings, patch reproduces byte-verbatim.

Quick start (build only; device contact is an attended bench action):

    ./tools/build.sh

Read-first: ../../STATUS.md, ../../mem-entries.md (newest), ../wifi-datapath/REPORT.md,
../wifi-datapath-bringup/REPORT.md + BENCH-LADDER.md (E1), ../wifi-vap-start/REPORT.md
(mode-1 VAP create; the TX-precondition ladder), ../wifi-scan-ap/REPORT.md.

No vendor binaries, firmware bytes, calibration data or per-unit values are
committed; the vendor artifacts are read in place by the local decode tool and
only derived constants/booleans are printed. All C code is GPL-2.0; docs are
original writing. This is a bench-reversal instrument, not an upstream artifact.
