# wifi-datapath-bringup

Data-plane host side for the Hi5622V100 clean-room driver (Cudy WR3000 v2.0,
luofu v2): the MAC host-interface rings and the TXQ-table address sync.

**Lane:** wifi-datapath-bringup (shared task task-111), host-only,
compile-verified, **not fired at the device**. 2026-11-25. Builds on
../wifi-datapath (static RE) and ../wifi-h2d-send (working HCC H2D control
plane).

| file | contents |
| --- | --- |
| REPORT.md | main report: decoded facts (MSDU-info table, ring geometries, TXQ table/sync), implementation, sequence+gates, [U]/risks, expected observables, evidence index |
| FRAME-SPECS.md | byte/register specs: 32-byte key-0x0320 frame, 14-write MAC program, ring geometries, device-RAM table arithmetic |
| BENCH-LADDER.md | attended ladder D0-D4 with preconditions, expected observables, decision matrix and stop rules |
| BUILD-NOTES.md | build/test evidence and handoffs |
| src/ | clean driver copy (from wifi-h2d-send) + dp_rings.h/.c (portable logic) + hi5622_dp.h/.c (kernel extension: dp_probe/dp_setup/dp_txq_sync/dp_status/dp_rings) |
| tools/build.sh | host selftests + static decode + zero-warning kernel build + sequence validation |
| tools/dp_logic_selftest.c | 69-check device-free data-plane selftest |
| tools/h2d_logic_selftest.c | inherited 308-check H2D regression (one test updated for the new message) |
| tools/dp_static_decode.py | re-derives the constants from the local vendor .ko/FIRMWARE.bin (34 checks; nothing vendor is committed) |
| tools/validate_sequence.py | validates sequences/ladder-d-datapath.json through the bench tooling validator (10 checks) |
| sequences/ladder-d-datapath.json | protocol-sequence schema hi5622-protocol-sequence/1: golden boot -> ctx-create -> dp_probe -> dp_setup -> key-0x0320 -> post packs |

Verification (authoring host):

    ./tools/build.sh
    # 69/69 + 308/308 + 34/34 host checks, 10/10 sequence checks,
    # module 462,952 B sha256 8fc693ad..., vermagic 6.18.60, 0 warnings

Quick start (build only; device contact is an attended bench action):

    ./tools/build.sh
    /path/to/.provision-venv/bin/python tools/dp_static_decode.py --selftest

Read-first: ../wifi-datapath/REPORT.md, ../wifi-h2d-send/REPORT.md,
../../phase43/channel-register-offsets.md, STATUS.md 3h-3k, newest ledger
entries.

No vendor binaries, firmware bytes, calibration data or per-unit values are
committed; the vendor artifacts are read in place by the local decode tool and
only derived constants/booleans are printed. All C code is GPL-2.0; docs are
original writing. This is a bench-reversal instrument, not an upstream artifact.
