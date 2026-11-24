# wifi-dp-air - merged provider module + first air-TX/RX ladder

The **merged module** the one-command bring-up matrix needs for the first
data-plane attempt: the hardware-validated **mode-1 provider** (wifi-scan-ap +
wifi-vap-start provider patch + scan-complete) **plus** the E1 MAC data-plane
bring-up (wifi-datapath-bringup) **plus** the first TX/RX actions of
wifi-dp-txrx - one `hi5622_wifi.ko`, so a started firmware VAP/scan and a
data-plane frame can exist in the same module load.

* **Lane:** wifi-dp-air (shared task task-126), **host-only**, 2026-11-26.
  Not fired at the device. The attended ladder is LADDER-F-AIR.md; the
  one-command form is the bundled bench sequence
  `../wifi-bench-tooling/tools/sequences/bringup-f-air-txrx.json`
  (30 steps / 83 checks / 7 device writes / 4 shell actions).
* **Base:** the provider tree of ../wifi-scan-complete/src (module 748,888 B
  sha256 b42ff4ec..., hardware-validated 2026-11-26 by the D2 TRUE PASS row).
* **Adds:** E1 host-side data plane (dp_probe / dp_setup / dp_txq_sync /
  dp_status / dp_rings from ../wifi-datapath-bringup) + the wifi-dp-txrx
  portable TX/RX logic and its four sysfs actions (dp_tx_frame /
  dp_tx_status / dp_rx_poll / dp_rx_capture) + the g0 id1 key-0x0320
  TXQ-table address sync post path.
* **Artifact (canonical build):** `hi5622_wifi.ko` **884,576 B** sha256
  **ec74fccd7c6d26585b30604828d4469100bb6714ada7dddf1e1b2ea0320a98e3**,
  vermagic 6.18.60, 0 compiler/modpost warnings (BUILD-NOTES.md records the
  exact build line; the .ko sha is build-path dependent).

| file | contents |
| --- | --- |
| MERGE-NOTES.md | what was merged and why: exact added/modified files, the two design invariants, what is deliberately NOT merged, and how the merge is reproduced/verified |
| BUILD-NOTES.md | environment, all measured host/selftest/kernel numbers, module parameters, deployment order, determinism note, handoffs |
| LADDER-F-AIR.md | the attended ladder: preconditions, stages, exact observables (COMPLETED / POST-ONLY / NO-RX / BLOCKED-DRIVER), decision matrix and stop rules |
| src/ | the merged out-of-tree module source (provider tree + data-plane additions) |
| patches/0001-merge-dp-txrx-into-provider.patch | the merge as one patch from the provider base; reproduced/verified by tools/verify_merge.py |
| tools/build.sh | canonical host+kernel verification (merge provenance, 1,483 host checks, sequence validation, staged clean module build, publication checks) |
| tools/verify_merge.py | patch `--emit` / drift + scope + round-trip verifier |
| tools/merged_selftest.c | 48 host checks: merge invariants, E1 geometry/table/MAC program, the key-0x0320 builder/wire/gate |
| sequences/ | pointer - the canonical F-AIR sequence lives in the bench tooling: `../wifi-bench-tooling/tools/sequences/bringup-f-air-txrx.json`, sha256 96354365867beaedeb77c001fed31a2afdb0d4d2edb949cd898fab2fcc8342f6 |

Quick start (host-only; device contact is an attended bench action):

    cd opensource/docs/incoming/wifi-dp-air && ./tools/build.sh

Then deploy/attend per LADDER-F-AIR.md; the one-command path is:

    python3 ../wifi-bench-tooling/tools/bench_session.py --mode protocol \
        --sequence ../wifi-bench-tooling/tools/sequences/bringup-f-air-txrx.json \
        --module /tmp/wifi-dp-air-build/src/hi5622_wifi.ko ... --yes-really --attended

Read-first context: ../wifi-dp-txrx/REPORT.md + FRAME-SPECS.md (frame byte
layout, table publish semantics, RX descriptor), ../wifi-datapath-bringup/
(BENCH-LADDER.md E1 row), ../wifi-vap-start/REPORT.md (mode-1 VAP create),
../wifi-scan-complete/REPORT.md (provider), ../../mem-entries.md (newest).

No vendor binary, firmware byte, calibration data or per-unit value is
committed. All C code is GPL-2.0; docs are original writing. This is a
bench-reversal instrument, not an upstream artifact.
