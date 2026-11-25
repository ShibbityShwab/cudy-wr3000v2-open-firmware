# wifi-power-perf - PM path RE + safe subset + performance tuning

**Lane:** wifi-power-perf (shared task-160), Cudy WR3000 v2.0 / Hi5622V100
clean-room host.  **Host-only lane:** every test here runs on the host; the
attended rungs live in [LADDER-BENCH.md](LADDER-BENCH.md) and were not fired.
**Status:** delivered 2026-11-25; base = FINAL association module (56 params).

## Status in one paragraph

Static RE of the vendor PM path shows that the vendor's platform sleep
machine is **dead on this board** (the arming vote has no importer, the only
userspace switch is in `wifi_debug.ko` which the boot script loads only for
"turbo" builds, `low_power_init` is a stub, and the stock INI has no PM
keys), while the runtime differences that do exist are per-VAP chip power
gating and cfg80211 STA power-save - both of which need firmware/transport
cooperation our one-shot transport does not have.  The safe subset is
therefore: **no PM message on the boot path**, an ASPM vendor-parity
assertion (the stock DTS has no `aspm-enable`, so the vendor runs with L0s/L1
off), an adaptive idle back-off for the 5 ms service poll, a tunable TX
completion rearm, and default-off, gated observability for a future attended
PM rung.  The performance model shows the shipped TX stack is **air-bound at
2.4 GHz MCS7** (16 in-flight frames at 5 ms = 3200 fps vs a ~3436 fps
non-aggregated air ceiling), that scan is **batching-bound at 6 channels per
request** (13 fully blocked at the DMAC), and that the 5 ms polling is the
only always-on host power cost the lane can safely reduce.

## Deliverables

| path | what it is |
| --- | --- |
| [REPORT.md](REPORT.md) | full lane report: vendor PM RE, risk matrix, safe subset, performance analysis, tuning, evidence |
| [LADDER-BENCH.md](LADDER-BENCH.md) | attended rungs P0-P7 with pass/stop/recovery rules (P5 = extreme risk) |
| [BUILD-NOTES.md](BUILD-NOTES.md) | build identity, base gate, patch scope, hashes |
| [patches/0001-power-perf-safe-subset.patch](patches/) | source delta on the FINAL association module (4 new params, 1 portable header, PM/ASPM attrs, poll back-off) |
| [src/hi5622_pm_perf_logic.h](src/hi5622_pm_perf_logic.h) | portable pure logic shared by the module and the host selftest |
| [tools/pm_perf_selftest.c](tools/pm_perf_selftest.c) | 73 host checks (PM gate, ack clamp, poll governor, ASPM, TX/air model, scan, beacon) |
| [tools/perf_model.py](tools/perf_model.py) | independent performance model + asserts; writes [evidence/PERF-MODEL.md](evidence/PERF-MODEL.md) |
| [tools/build-patched-module.sh](tools/build-patched-module.sh) | host cross-build of base + patch, gates 56->60 params, 0 warnings, records hash |
| [tools/run-host-checks.sh](tools/run-host-checks.sh) | one-command device-free gate (`--with-build` adds the cross-build) |
| [tools/vendor_pm_disasm.py](tools/vendor_pm_disasm.py) | regenerates the section-2 RE evidence from an operator-local vendor .ko (never committed) |
| [evidence/](evidence/) | committed host evidence: selftest, model, host checks, build |

## Reproduce (device-free)

```sh
cd opensource/docs/incoming/wifi-power-perf
./tools/run-host-checks.sh              # selftest + model + patch dry-run
./tools/run-host-checks.sh --with-build # + cross-build the patched module
```

The build script needs the project kernel tree and staging dir (auto-detected
under /home/shibbityshwab/openwrt-lab-*) and a base tree: it uses
`/tmp/sibling-mgmt-fix` when present, otherwise it rebuilds the final
association module with the tracked `wifi-stability/tools/build-association-module.sh`.
The built .ko is local evidence only and is never committed.

## Limits

- The vendor PM dispatch table could not be followed by symbol; the
  boot-time caller chain of the per-VAP power gate is graded [I] (REPORT 2.2).
- Frame-timing inputs of the air model are classic 802.11 values [I]; the
  throughput comparison is a model, not a measurement.  P3 is the ladder that
  turns it into [V*].
- The safety of P5 cannot be argued from the host: it may hang the device
  service until a physical power-cycle.
