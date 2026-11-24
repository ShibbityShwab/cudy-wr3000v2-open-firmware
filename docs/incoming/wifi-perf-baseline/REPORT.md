# wifi-perf-baseline - metrics harness + baseline for the working clean-room Wi-Fi stack

**Lane:** wifi-perf-baseline (shared task task-132). **Host-only:** this lane built and
ran the harness on RECORDED bench evidence only and never contacted the bench router.
**Status:** harness complete + selftest 87/87 + baseline generated + compile-verified on
the 6.18.60 tree. **Date:** 2026-11-27.

This directory is the measurement instrument for the next tuning wave: it turns the
project's recorded bench sessions into a pinned, machine-readable baseline and ships a
read-only live probe that closes the two dimensions the records cannot answer (CPU cost
and effective poll/event rates).

## 0. TL;DR

- Harness: `tools/perf_session.py` (host: ingest recorded evidence, or gated read-only
  live mode) + `tools/perf_probe.sh` (device: read-only /proc, /sys, dmesg; piped over
  ssh stdin so nothing is written on the box). `tools/selftest.py`: **87/87 PASS**.
- Baseline: **105 metrics** from 3 phase-matrix runs, 11 bench-session summaries, 9
  attended session records, 192 UART console logs and the live tuning-phase sample -> `baseline/baseline.json`,
  `baseline/baseline.csv`, `baseline/BASELINE.md`. Every sample carries its evidence
  path and the evidence file's sha256; 14 documented prior-lane numbers are marked
  grade V instead of being passed off as raw measurements.
- Headline numbers:
  - one 5-phase attended matrix: **40.5-40.9 s of phase work + 293.6-351.6 s of
    between-phase reboots** (median reboot 80.1 s, range 73.0-91.0 s) -> **88-90 % of a
    matrix run is waiting for reboots**;
  - boot->ready ladder: **4.03 s** kernel-observed (median, n=11), of which **2.49 s is
    driver work** (upload 215 ms, verify 828 ms dominate) and ~**1.54 s** is host fixed
    sleeps + 1 Hz ready polling + SSH;
  - serial-console printk: **9.99 ms per driver line** (median, n=63) x 176 driver
    lines/session (median) = **~1.8 s of serial time per session** (up to 3.1 s);
  - H2D: consumed in **0 polls always** (57 captures); measured write->consumed
    **8.29 ms** and write->completion **42.8 ms**, both dominated by the two printk
    lines, not by wire latency;
  - scan: **the firmware is fast for 6 channels then hard-stops** - live 0.94 s for
    ch1..ch6 (~156 ms/ch) and then no result for the remaining 14 s; every recorded
    13-channel fire (5 s and 14.7 s windows) covers exactly 6/13, so the host window is
    not the primary limiter;
  - AP start: **437-448 ms** from hostapd NEW_BEACON to all 4 AP frames posted (n=3),
    of which ~300 ms is the bench 100 ms frame gap; frames consumed in 0 polls;
    hostapd launch->AP-ENABLED ~2 s (documented, grade V);
  - heartbeat: 1.00 s median, IQR 990-1010 ms (firmware tick deltas, n=391);
  - memory: ring staging **458,752 B per endpoint** (7 x 32 x 2048 B), BAR0 window
    16 MiB, TXQ table 9,508 B, MSDU table 8 entries, firmware image 928,920 B,
    driver artifact 242,572 B (final combined image module) / 407,996-738,112 B across
    the session modules.
- Live tuning-phase sample (grade V, 2026-11-27 20:30): CPU ~0 % busy during AP
  operation (user+sys unchanged, idle +200/200 jiffies/s), D2H reader ~145 polls/s
  (21,288 polls / 147 s, 0 overruns), heartbeat 1.009-1.011 s with <1 ms jitter,
  66,784 kB free, hi5622_wifi 184,320 B + cfg80211 339,968 B resident.
- Top inefficiencies, in order: (1) attended reboot cycle, (2) serial-console printk,
  (3) fixed ladder sleeps + 1 Hz ready polling, (4) **scan hard-stop after 6 channel
  entries** (firmware/pagination issue, not a slow scan), (5) bench frame-gap policy
  on the H2D/AP sequences. Details + evidence in
  section 3.

## 1. The harness

### 1.1 Device half - `tools/perf_probe.sh` (read-only)

POSIX/busybox sh. Emits a stable text stream (SNAP blocks, `KEY value` lines). Reads
only: /proc/stat, /proc/uptime, /proc/meminfo, /proc/loadavg, /proc/modules,
/sys/module/hi5622_wifi/parameters/*, the endpoint sysfs attributes
(hcc_status, hcc_ready, hcc_events, hcc_rings, h2d_status, h2d_plan, rd_status,
rd_catalog, rd_decode, status/boot/dev_status, dp_*), matching kthread stats from
/proc/PID/stat, and a bounded dmesg tail. It performs **no device register write, no
module load, no reboot, no firmware operation** and is piped to the box over ssh stdin
(no file is written on the device either).

### 1.2 Host half - `tools/perf_session.py`

- `extract` (default): discovers `build/wifi-bench-phase-matrix/*`,
  `build/wifi-bench/*`, `build/wifi-bench-session/*, `build/uart-logs/*` and derives
  metrics from: phase-matrix host records, session-summary dmesg (kernel timestamps),
  generated device scripts (fixed sleeps, ready deadlines), bench-session JSON, and
  the UART console log (multi-boot AP/scan/heartbeat/console analysis).
  Writes `baseline/baseline.json`, `baseline/baseline.csv`, `baseline/BASELINE.md`.
- `dry-run`: validates the probe script, the documented-input file and the extractor
  without writing anything and without device contact.
- `probe` / `watch`: live mode. Requires BOTH `--yes-really --attended` and
  `PERF_ALLOW_DEVICE=1`, uses the project `tools/rsh.sh` transport, runs the read-only
  probe over stdin, and computes CPU%, per-kthread CPU, D2H poll rate, contrast deltas
  and heartbeat spacing between snapshots. This lane **never ran it**.
- `selftest` (standalone file): 87 host-only checks on synthetic fixtures (no device).

Grades used in the baseline (also embedded in baseline.json):

| grade | meaning |
| --- | --- |
| V* | measured from raw recorded evidence (dmesg/UART/host session record) |
| V | hardware-validated number documented by a prior lane (see `baseline/documented-input.json`) |
| I | inference from measured values or source constants |
| U | not measured yet; the live probe is the instrument (section 4) |

### 1.3 Reproduce

    cd opensource/docs/incoming/wifi-perf-baseline
    python3 tools/perf_session.py dry-run          # validate, no writes
    python3 tools/perf_session.py list-evidence    # show the ingested records
    python3 tools/perf_session.py extract          # regenerate baseline/
    python3 tools/selftest.py                      # 87/87 host-only checks

On a fresh clone `build/` is gitignored/absent: `extract` then produces the documented
metrics + a warning, and the committed `baseline/baseline.json` (with one sha256 per
evidence file) remains the pinned record of the numbers below.

## 2. Baseline

Full machine-readable table: `baseline/baseline.json` (per-sample evidence) and
`baseline/baseline.csv`. Current values (n = samples, med = median, range min..max):

### 2.1 Host orchestration (the dominant cost)

| metric | unit | grade | n | med | range |
| --- | --- | --- | ---: | ---: | --- |
| bench.phase.boot.elapsed_s | s | V* | 2 | 6.07 | 6.06..6.08 |
| bench.phase.C2.elapsed_s | s | V* | 2 | 6.88 | 6.84..6.92 |
| bench.phase.D1.elapsed_s | s | V* | 2 | 7.20 | 7.17..7.23 |
| bench.phase.D2.elapsed_s | s | V* | 3 | 14.11 | 14.04..14.17 |
| bench.phase.E1.elapsed_s | s | V* | 2 | 6.44 | 6.41..6.46 |
| bench.reboot.cycle_s | s | V* | 8 | 80.10 | 73.0..91.0 |
| bench.matrix.total_phase_elapsed_s | s | V* | 2 | 40.69 | 40.52..40.86 |
| bench.matrix.total_reboot_s | s | V* | 2 | 322.60 | 293.6..351.6 |
| os.kernel_to_userspace_ms | ms | V* | 45 | 6024 | 5982..15561 |
| os.lan_forwarding_at_s | s | V* | 40 | 19.47 | 18.99..40.54 |
| os.module_load_at_uptime_s | s | V* | 51 | 61.76 | 29.0..159.8 |

### 2.2 Boot ladder (kernel-observed, endpoint 0)

| metric | unit | grade | n | med | range |
| --- | --- | --- | ---: | ---: | --- |
| boot.load_to_service_ready_ms | ms | V* | 11 | 21.17 | 20.76..32.91 |
| boot.prepare_ms | ms | V* | 11 | 66.79 | 66.76..66.85 |
| boot.upload_ms | ms | V* | 11 | 215.28 | 215.13..215.55 |
| boot.upload_MBps | MB/s | V* | 11 | 4.315 | 4.309..4.318 |
| boot.verify_ms | ms | V* | 11 | 827.54 | 820.24..828.70 |
| boot.verify_MBps | MB/s | V* | 11 | 1.123 | 1.121..1.132 |
| boot.seed_ms | ms | V* | 11 | 23.65 | 23.41..23.92 |
| boot.hcc_arm_ms | ms | V* | 11 | 134.69 | 134.67..134.78 |
| boot.loop_to_release_ms | ms | V* | 11 | 79.22 | 51.88..112.52 |
| boot.release_to_ready1_ms | ms | V* | 11 | 828.65 | 793.73..858.88 |
| boot.ready1_to_ready2_ms | ms | V* | 11 | 293.43 | 261.34..303.51 |
| boot.release_to_ready_pair_ms | ms | V* | 11 | 1120.26 | 1087.15..1142.28 |
| boot.load_to_ready_pair_ms | ms | V* | 11 | 4027.68 | 3975.05..4079.23 |
| boot.script_fixed_sleep_s | s | V* | 64 | 1.0 | 0.4..1.0 |
| boot.ready_poll_interval_s | s | V* | 11 | 1.0 | 1.0 |
| boot.ready_deadline_s | s | V* | 27 | 20.0 | 2.0..20.0 |
| ready.boot_age_ms | ms | V* | 26 | 660 | 460..7770 |

### 2.3 H2D frame round trip

| metric | unit | grade | n | med | range |
| --- | --- | --- | ---: | ---: | --- |
| h2d.send_to_consumed_ms | ms | V* | 26 | 8.29 | 8.24..8.36 |
| h2d.send_to_consumed_polls | polls | V* | 26 | 0 | 0 |
| h2d.max_polls | polls | V* | 57 | 0 | 0 |
| h2d.send_to_complete_ms | ms | V* | 44 | 42.77 | 8.26..62.34 |
| h2d.frame_spacing_ms | ms | V* | 19 | 85.5 | 75.69..244.41 |

Interpretation: every recorded announce was consumed on the first read (0 polls =
< 5 ms); the 8.29 ms "write -> consumed" is the gap between two dev_info lines on a
10 ms/line console, i.e. it measures printk, not hardware. The completion event is a
separate later protocol tick. H2D is **not** a bottleneck.

### 2.4 Scan

| metric | unit | grade | n | med | range |
| --- | --- | --- | ---: | ---: | --- |
| scan.host_window_ms | ms | V* | 10 | 5000 | 3900..14700 |
| scan.first_result_latency_ms | ms | V* | 8 | 148.77 | 125.54..278.76 |
| scan.channel_gap_ms | ms | V* | 15 | 118.34 | 82.45..320.41 |
| scan.channels_seen | channels | V* | 8 | 6 | 1..6 |
| scan.done_total_ms | ms | V* | 3 | 4901.82 | 4868.59..4911.83 |
| scan.effective_ms_per_channel | ms/ch | I | 7 | 833.33 | 833.33..3900.0 |
| scan.results / scan.bss | frames/BSS | V* | 3 | 8 / 8 | 8..8 |
| scan.window_actual_ms | ms | V* | 10 | 9756.8 | 195.4..14670.0 |

The auto window is clamp(3000 + 900*n, 1000, 45000) ms (documented, grade V): 14.7 s for
13 channels. The recorded fires all cover exactly 6 of 13 channels (5 s window: 6/13;
14.7 s window: 6/13 in the raw UART record; 4/13 in the older prior-lane fire). The
first results arrive in ~150 ms and then the stream stalls. The **live tuning-phase
sample (grade V) resolves the ambiguity: ch1..ch6 arrive in 0.94 s (~156 ms/channel)
and then there is no result for the remaining ~14 s** - the firmware processes only the
first 6 channel entries of the g0 id0xC request and idles. Raising the host window or
the 45 s clamp cannot fix this; the channel-count/array/pagination fields in the scan
request are the candidates (section 3.4).

### 2.5 AP start

| metric | unit | grade | n | med | range |
| --- | --- | --- | ---: | ---: | --- |
| ap.start_ap_to_sequence_posted_ms | ms | V* | 3 | 445.31 | 436.79..448.38 |
| ap.frame_latency_ms | ms | V* | 12 | 224.52 | 20.74..410.73 |
| ap.frame_consumed_polls | polls | V* | 12 | 0 | 0 |
| ap.frames_posted | frames | V* | 3 | 4 | 4 |
| ap.beacon_interval / ap.dtim | TU / beacons | V* | 3 | 100 / 2 | 100 / 2 |
| ap.hostapd_launch_to_ap_enabled_ms | ms | V | 1 | 2000 | documented |

The four AP messages (vap_add 48 B, vap_start 40 B, ap_ssid 64 B, ap_cfg 184 B) are
posted 437-448 ms after NEW_BEACON; the bench `frame_gap_ms=100` policy accounts for
~300 ms of that. Frames are consumed in 0 polls. hostapd startup is the documented ~2 s.

### 2.6 Heartbeat / direct D2H reader

| metric | unit | grade | n | med | range |
| --- | --- | --- | ---: | ---: | --- |
| heartbeat.interval_ms | ms | V* | 389 | 1000.0 | 725.89..2000 |
| heartbeat.tick_delta_ms | ms | V* | 391 | 1000 | 800..2000 |
| heartbeat.interval_outliers | intervals | V* | 52 | 0 | 0..17 |
| heartbeat.jitter_ms | ms | V* | 3 | 96.2 | 82.62..113.62 |
| d2h.poll_counter | polls | V* | 44 | 10.5 | 1..293 |
| d2h.frames_counter | frames | V* | 44 | 21 | 0..56 |
| d2h.service_cadence_ms | ms | V | 1 | 5 | documented |
| d2h.heartbeat_watchdog_ms | ms | V | 1 | 10000 | documented |
| d2h.irq_count | IRQs | V | 1 | 0 | documented + dmesg |

Firmware tick deltas: p25 990 ms, p50 1000 ms, p75 1010 ms (n=391); the >1200 ms tail
and the 2.3 skipped intervals/run are console/logging artefacts, not firmware jitter.
The receive path is poll-driven (no IRQ); the live probe is the only way to turn
`polls` into a rate.

### 2.7 Memory / data plane

| metric | unit | grade | n | med | range |
| --- | --- | --- | ---: | ---: | --- |
| mem.firmware_image_bytes | B | V* | 11 | 928920 | 928920 |
| mem.module_artifact_bytes | B | V* | 11 | 462952 | 407996..738112 |
| mem.ring_buffers_bytes | B/endpoint | V* | 11 | 458752 | 458752 |
| mem.bar0_window_bytes | B | V* | 2 | 16777216 | 16777216 |
| dp.txq_table_bytes | B | V* | 2 | 9508 | 9508 |
| dp.msdu_table_entries | entries | V* | 2 | 8 | 8 |
| dp.rings_tx / rx0 / rx1 | entries | V* | 2 | 256 / 512 / 4095 | fixed |
| dp.tx_post_only / dp.rx_captures | bool / frames | V | 1 | 1 / 0 | documented |

The `mem.module_artifact_bytes` range is the per-phase bench modules (H2D 407,996 B,
E1 462,952 B, D1 505,076 B, D2 provider 738,112 B); the final self-contained image
ships the combined AP+DP module, **242,572 B sha256 44c6cc12...** (compile-verified
again by this lane, section 5).

### 2.8 Console cost (a first-class metric)

| metric | unit | grade | n | med | range |
| --- | --- | --- | ---: | ---: | --- |
| console.printk_line_ms | ms | V* | 63 | 9.99 | 9.28..10.07 |
| console.driver_lines_per_session | lines | V* | 11 | 176 | 159..313 |
| console.driver_console_time_s | s | I | 11 | 1.77 | 1.60..3.13 |

### 2.9 Live tuning-phase confirmation (grade V; ledger 2026-11-27 20:30)

The tuning-phase bench sample closes the main grade-U dimensions this lane could not
measure from the records. These values are merged into the baseline as documented
entries (source named per entry in baseline/documented-input.json):

| metric | unit | grade | value | note |
| --- | --- | --- | ---: | --- |
| cpu.live_busy_pct | % | V | 0 | user+sys unchanged, idle +200/200 jiffies over 1 s (2 CPUs) |
| d2h.live_poll_rate_hz | polls/s | V | 145 | 21,288 polls over ~147 s, 0 overruns, 64 frames |
| heartbeat.live_interval_ms | ms | V | 1010 | 1.009-1.011 s, jitter < 1 ms (10 samples) |
| mem.live_total_kB / mem.live_free_kB | kB | V | 112400 / 66784 | /proc/meminfo during AP operation |
| mem.live_module_loaded_bytes | B | V | 184320 | hi5622_wifi in /proc/modules (stripped 231,132 B on disk) |
| mem.live_cfg80211_bytes | B | V | 339968 | cfg80211 in /proc/modules |
| boot.live_ready_handshake_ms | ms | V | 1000 | consistent with the 1120 ms dmesg record |
| boot.live_setup_insmod_to_iface_ms | ms | V | 3000 | boot ladder + cfg80211 registration |
| scan.live_first_6ch_span_ms | ms | V | 940 | ch1..ch6; then silence |
| scan.live_ms_per_channel_6ch | ms/ch | V | 156 | contradicts the earlier 830 ms / 3.5 s estimates |
| scan.live_channels_before_stop | channels | V | 6 | every recorded 13-channel fire stops at 6 |

## 3. Top inefficiencies (ranked, with evidence)

### 3.1 The attended reboot cycle dominates every matrix run (largest win available)

Evidence: `bench.reboot.cycle_s` median **80.1 s** (n=8, 73.0-91.0 s);
`bench.matrix.total_reboot_s` **293.6 / 351.6 s** vs
`bench.matrix.total_phase_elapsed_s` **40.52 / 40.86 s** => the 5-phase matrix spends
**88-90 %** of its wall time on 4 reboots. The device itself reaches userspace in
**6.02 s** (median, n=45) and br-lan forwarding at **19.5 s** (median), yet the module
load lands at **61.8 s** uptime (median, n=51). Candidate causes [I]: reset/restart
latency (the restart handler falls back to the 30 s watchdog; `luofu-wdt ... timeout
30 s` in the boot log), the host reboot-wait poll (4 s granularity) plus stability
re-read, and the 1.3 MB artifact deploy/verify path. Next step: timestamp
reset->SSH-ready directly in the attended harness (the watch mode samples uptime only
after SSH is back; a boot-stage UART record is the way to separate reset latency from
host wait), and batch phases per boot where possible (the combined AP+DP module
already permits one-boot AP+data-plane runs). A matrix that avoids per-phase reboots
would be ~40 s + one reboot instead of ~334-392 s.

### 3.2 Serial-console printk: ~10 ms per line, 1.6-3.1 s per session

Evidence: 63 dense driver-line pairs give **9.99 ms median per line** (range
9.28-10.07, consistent with ~115200 baud and ~110-char lines); 159-313 driver lines per
captured session => **1.60-3.13 s** of serial time (I) *inside* the session. Concrete
bursts: the boot DR drain prints 2 endpoints x 4 rings x 16 nodes = 128 lines
(~1.28 s), and every scan/AP/cal event prints 2-5 lines. The probe design is
poll-driven at 5 ms (200 Hz/endpoint); while the thread sleeps on the serial console
the effective cadence stretches, which shows up as heartbeat/logging gaps (2.3
intervals/run outside 500-2000 ms). Next step: gate per-node/per-frame dev_info behind
`pr_debug` / dynamic debug or raise the console loglevel during perf runs; this is a
driver/bench policy change that belongs to the owning lane, not this one. Verify with
the live `watch` mode (CPU%, polls/s).

### 3.3 Fixed host sleeps + 1 Hz ready polling: ~38 % of the boot ladder

Evidence: kernel-observed ladder `boot.load_to_ready_pair_ms` = **4027.7 ms** (n=11),
while the measured driver work chains sum to **2488.6 ms** (21.2 + 66.8 + 215.3 + 827.5
+ 23.7 + 134.7 + 79.2 + 1120.3). The remainder (~**1539 ms**) is the generated script's
fixed `sleep 1` after insmod and `sleep 0.4` after release, the 1 Hz ready poll
(`boot.ready_poll_interval_s` = 1.0 s, up to ~1 s detection quantisation) and SSH
round trips. Next step: replace the post-insmod sleep with an attribute poll
(status/ring presence), start the ready poll immediately after release, and poll at
100 ms; this should recover ~1.5 s per boot (a 37 % ladder reduction) with no
semantic change.

### 3.4 Scan hard-stop after 6 channel entries (corrected diagnosis)

Evidence: every recorded 13-channel fire covers exactly **6/13** channels - a 5 s window
gives 6 results in ~0.9 s, a 14.7 s window gives the same 6 results and then nothing for
14 s, and the live tuning-phase sample measures ch1..ch6 in **0.94 s (~156 ms/channel)**
followed by silence (grade V, ledger 2026-11-27 20:30). The earlier "830 ms/ch" and
"3.5 s/ch" numbers were host-window ratios, not firmware pace; the firmware is fast and
then hard-stops. Hypotheses [I]: wrong channel-count field (+0x1EC), truncated channel
array in the 520 B request, a firmware cap of 6 channels per request (pagination
needed), or a cursor/next-channel field. Next step (scan-tuning lane): instrument the
g0 id0xC request fields against the firmware handler, then test pagination / a second
request from the next channel. The host window and the 45 s clamp are secondary until
the 6-channel stop is fixed; the harness `watch` mode can timestamp arrivals live and
`rd_decode` keeps per-frame device jiffies.

### 3.5 Bench H2D/AP frame-gap policy (100 ms vs 20 ms default)

Evidence: `h2d.frame_spacing_ms` median **85.5 ms** with the bench sequence's
`frame_gap_ms=100`; the C2 phase posts 8 frames (~0.68 s) and the AP sequence posts 4
(~445 ms, ~300 ms of it gap). The production default is 20 ms. Next step: use the
default 20 ms for non-destructive runs (or make the gap a sequence parameter), saving
~0.4 s on C2 and ~0.25 s on AP start; this must be verified against the firmware-rate
policy in the owning lane (the gap exists to avoid inflight refusals).

### 3.6 Things that are NOT bottlenecks (measured)

- H2D consume path: 0 polls in every capture (n=57); measured 8.29 ms is printk, not
  wire latency; completion median 42.8 ms is a protocol tick.
- Heartbeat: 1.00 s median, IQR 990-1010 ms; the tails are console artefacts.
- AP frame posting: 437-448 ms for 4 messages with 0-poll consumption; hostapd startup
  (~2 s, documented) dominates AP bring-up, then reboot/ladder dominate everything.
- Scan first result: 149 ms median - the firmware starts quickly, it is the coverage
  completion that is slow.

## 4. Open measurements the live probe closes (grade U)

Most grade-U dimensions from the first baseline pass are now closed by the grade-V live
tuning-phase sample in section 2.9 (CPU ~0 %, poll rate ~145/s, heartbeat 1.009-1.011 s,
memory). What remains genuinely unmeasured:

| dimension | why it is not in the recorded evidence | instrument |
| --- | --- | --- |
| per-kthread CPU split | the live sample measured total CPU, not the service tasks | `watch` THREAD utime/stime deltas |
| hostapd launch -> AP-ENABLED wall time | hostapd stdout is not in the kernel/UART records; ~2 s is documented | host-side timestamps around hostapd + dmesg `start_ap` |
| full 13-channel scan sequence | the firmware hard-stops at 6; blocked on scan-tuning | `watch` during an attended scan + rd_decode jiffies |
| per-frame event-loop latency | not observable without timestamps/tracepoints | live pairing of dmesg + rd_decode |
| reset -> SSH-ready split of the 80 s reboot cycle | only the total back-after time is recorded | boot-stage timestamps (UART) + host watch |

Attended operator recipe (needs a human at the bench, open recovery door, bench-link
PASS - see `RUNBOOK.md`; this lane does not run it):

    cd opensource/docs/incoming/wifi-perf-baseline
    python3 tools/perf_session.py watch --dry-run --duration 30 --interval 5
    # attended, after the stack (or AP) is up:
    PERF_ALLOW_DEVICE=1 python3 tools/perf_session.py watch \
        --yes-really --attended --duration 30 --interval 5

Outputs go to `build/wifi-perf-baseline/<utc>/probe-raw.txt` and
`probe-metrics.json` (gitignored, like all build evidence). A read-only `probe` one
shot is the same command without `--duration`.

## 5. Verification

- `python3 tools/selftest.py` -> **87 checks / 0 failures** (synthetic fixtures;
  parsers, aggregation, writers, live gating, CLI; no device contact).
- `python3 tools/perf_session.py dry-run` -> PASS (probe sh -n, documented input,
  evidence discovery, extract smoke).
- Project bench tooling unaffected: `wifi-bench-tooling/tools/run-selftest.sh` ->
  **563/563 PASS**.
- **6.18.60 compile verification:** rebuilt the canonical combined AP+DP module
  (wifi-ap-start `tools/build-combined-dp-ap-module.sh`) against
  `build_dir/.../linux-6.18.60`:
  **242,572 B sha256 44c6cc12b5c43d47b4b0ccd1da693c77b00e5ef48912625add52370ea4612c0b**,
  vermagic `6.18.60 SMP mod_unload ARMv7 p2v8`, depends=cfg80211 -
  byte-identical to the hardware-fired combined artifact. All perf-relevant module
  params are present in the built .ko (`hcc_poll_ms`, `frame_gap_ms`,
  `scan_timeout_ms`, `scan_dwell_ms`, `h2d_send_budget`, `allow_frames`,
  `vap_start_mode`/`vap_start_module`, `ap_start_mode`, `rd_ab_write`, ...), so the
  harness can tune/verify them without a driver rebuild for merely changing values.
- No device contact, no vendor binary/firmware/calibration bytes, no root-repo edits;
  `git status` inside `opensource` clean after commit.

## 6. Limits / honest notes

- The `build/` evidence tree is gitignored. `baseline.json` pins each ingested file
  with its sha256 and the extraction is deterministic, but a fresh clone can only
  re-derive the documented (grade V) entries until a bench session is recorded again.
  This is the project convention; `baseline/baseline.json` + this report are the
  committed record.
- Host wall times include SSH/dropbear and deploy/verify overhead; the per-phase
  `elapsed_s` is bench_session's own timer, the reboot `back after` is its wait loop.
- The two recorded 13-channel scan fires disagree (4/13 documented vs 6/13 raw UART);
  both are reported rather than averaged. The scan numbers are the least settled part
  of this baseline (section 3.4).
- `heartbeat.*` and `console.*` metrics are console/dmesg observations, not tracepoints;
  that is why the live watch mode exists.
- No instruction-level, PCIe-latency, or power measurement was attempted; no firmware
  internals are claimed.
- The baseline is pinned to the recorded evidence it was generated from. Lanes
  that landed after the evidence snapshot (e.g. wifi-dp-tuning task-134 TXQ
  queue-state publish, wifi-image-polish task-135 a policy layer) are NOT
  reflected in these numbers; re-run extract after their attended fires to get a
  new baseline generation. The documented POST-ONLY data-plane entry is the
  2026-11-27 10:30 combined fire, not the post-134 code state.
- The source of the documented entries is `baseline/documented-input.json` (each entry
  names its lane/ledger source); they are grade V and are excluded from any claim of
  "measured by this lane". The live tuning-phase values (section 2.9) come from the
  lead's 2026-11-27 20:30 bench sample and the ledger entry that records it, not from
  raw files in this checkout.

## 7. Suggested next actions (for the tuning wave)

1. One attended `watch` run with the stack up (30-60 s) -> CPU%, kthread CPU, real poll
   rate; append as a second baseline generation (`baseline.json` schema is stable).
2. Console-log-level/per-frame printk policy in the driver lane; re-measure the same
   metrics before/after (expect -1.3 s boot ladder and less poll-cadence stretching).
3. Ladder host-script change (kill the fixed sleeps + 100 ms ready poll) and re-run the
   phase matrix; expect ~4.0 s -> ~2.6 s ladder.
4. Reboot path measurement (separate reset latency from host wait; try the restart
   handler path) - the single biggest wall-time item (88-90 % of a matrix run).
5. Scan: fix the 6-channel hard stop (channel count/array/pagination in g0 id0xC), not
   the host window; the scan-tuning lane owns the request fields, this harness supplies
   the timestamps. Re-derive the auto window afterwards from a full 13-channel run.
