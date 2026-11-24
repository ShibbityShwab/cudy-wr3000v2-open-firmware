# wifi-perf-baseline - performance metrics harness + baseline

**Lane:** wifi-perf-baseline (shared task task-132). **Host-only deliverable:** the
harness was built and run on the project's recorded bench evidence; this lane never
contacted the bench router and never wrote to the device.

Read `REPORT.md` for the baseline numbers, the top inefficiencies with evidence, and
the remaining (live) measurements. This README is the quickstart.

## What is here

| path | purpose |
| --- | --- |
| `REPORT.md` | the lane report: baseline, top inefficiencies, evidence mapping, limits, next actions |
| `baseline/baseline.json` | machine-readable baseline: 105 metrics, per-sample evidence paths, evidence sha256, documented prior-lane entries, grades |
| `baseline/baseline.csv` | one row per metric (n/min/median/mean/max/stdev/method) |
| `baseline/BASELINE.md` | generated markdown table |
| `baseline/documented-input.json` | prior-lane hardware-validated numbers (grade V) merged into the baseline; each names its source |
| `tools/perf_session.py` | host harness: `extract` (recorded evidence -> baseline), `dry-run`, `list-evidence`, and the gated read-only live `probe`/`watch` |
| `tools/perf_probe.sh` | device half: read-only /proc, /sys, dmesg snapshot/sampler; piped over ssh stdin (no device writes) |
| `tools/selftest.py` + `tools/run-selftest.sh` | 87 host-only checks on synthetic fixtures; no device contact |

## Reproduce the baseline (host-only, no device)

    cd opensource/docs/incoming/wifi-perf-baseline
    python3 tools/perf_session.py dry-run        # validate harness + evidence
    python3 tools/perf_session.py list-evidence  # list ingested recorded sessions
    python3 tools/perf_session.py extract        # regenerate baseline/
    python3 tools/selftest.py                    # 87/87

The extractor reads the project evidence tree (gitignored): `build/wifi-bench-phase-matrix/*`,
`build/wifi-bench/*, `build/wifi-bench-session/*, `build/uart-logs/*`. It makes no
device contact. On a fresh clone without `build/` it still emits the documented
(grade V) entries and a warning; the committed `baseline/baseline.json` remains the
pinned record.

## Live mode (attended operator only - this lane never runs it)

The live mode is deliberately read-only: the probe script is piped to the box through
`tools/rsh.sh` on stdin, so nothing is written on the device; it reads /proc, /sys and
dmesg only. It needs a human at the bench, the recovery door armed and bench-link PASS
(see `RUNBOOK.md`). Two independent gates are required:
`--yes-really --attended` plus `PERF_ALLOW_DEVICE=1`.

    # plan only (safe anywhere):
    python3 tools/perf_session.py probe --dry-run
    python3 tools/perf_session.py watch --dry-run --duration 30 --interval 5

    # attended, with the stack/AP up:
    PERF_ALLOW_DEVICE=1 python3 tools/perf_session.py probe --yes-really --attended
    PERF_ALLOW_DEVICE=1 python3 tools/perf_session.py watch --yes-really --attended \
        --duration 30 --interval 5

Outputs land in `build/wifi-perf-baseline/<utc>/` (gitignored): `probe-raw.txt` and
`probe-metrics.json` with CPU%, per-kthread CPU, D2H poll rate, ctxt deltas and
heartbeat spacing. That is what closes the grade-U entries in the report (CPU cost is
the main one).

## Baseline in one glance

- one attended 5-phase matrix: ~40.7 s of phase work + 293.6-351.6 s of reboots
  (**88-90 % waiting for reboots**; reboot median 80.1 s, n=8);
- boot ladder 4.03 s kernel-observed (driver work 2.49 s; host fixed sleeps/1 Hz ready
  poll/SSH ~1.54 s);
- serial console printk 9.99 ms/line (n=63) x 176 driver lines/session ~ 1.8 s/session;
- H2D: consumed in 0 polls every capture; measured 8.29 ms write->consumed is printk,
  not wire latency; frame gap policy 100 ms (default 20 ms);
- live tuning-phase sample (grade V): CPU ~0 % during AP operation, D2H reader
  ~145 polls/s (0 overruns), heartbeat 1.009-1.011 s (<1 ms jitter), 66,784 kB free;
- scan: the firmware is fast for 6 channels then HARD-STOPS - ch1..ch6 in 0.94 s
  (~156 ms/ch) and then silence; every 13-channel fire covers 6/13 (host window is not
  the limiter; channel count/array/pagination in g0 id0xC is);
- AP start: 437-448 ms for the 4 AP frames (~300 ms is bench frame gap); hostapd
  launch->AP-ENABLED ~2 s (documented);
- heartbeat 1.00 s median (IQR 990-1010 ms); memory: 458,752 B ring staging per
  endpoint, 16 MiB BAR0, 9,508 B TXQ table, 928,920 B firmware image.

## Add a metric

1. add a collector block in `tools/perf_session.py` (session dmesg / UART / phase
   matrix / probe parser) with a stable metric id, unit, method and grade;
2. add a synthetic-fixture assertion in `tools/selftest.py`;
3. regenerate `baseline/` and update `REPORT.md` if the line belongs in a table.

Evidence grading (also in baseline.json): **V*** raw recorded evidence, **V**
documented prior-lane result, **I** inference, **U** not measured yet (live probe).

## Boundaries

Host-only. No vendor binaries/firmware/calibration bytes are stored here; the driver
`.ko` is rebuilt locally only for the 6.18.60 compile verification and is never
committed. The probe must never be extended with device writes without the same
fail-closed gates the bench tooling uses; the lane's own live commands are only
`cat`-style reads and `dmesg`.
