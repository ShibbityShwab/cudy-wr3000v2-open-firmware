#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""selftest.py - host-only selftest for the wifi-perf-baseline harness.

Lane: wifi-perf-baseline (shared task task-132).  No device contact: every
check runs on synthetic fixtures created in a temp dir plus the committed
harness files.  Run:

    python3 tools/selftest.py

Exit code 0 = all checks passed.
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile

TOOLDIR = os.path.dirname(os.path.abspath(__file__))
LANE_DIR = os.path.dirname(TOOLDIR)
if TOOLDIR not in sys.path:
    sys.path.insert(0, TOOLDIR)

import perf_session as P  # noqa: E402

CHECKS = []
FAILURES = []


def check(name: str, cond: bool, detail: str = "") -> None:
    CHECKS.append(name)
    if not cond:
        FAILURES.append("%s %s" % (name, detail))


def between(value, lo, hi, tol=1e-6) -> bool:
    return value is not None and lo - tol <= float(value) <= hi + tol


# ---------------------------------------------------------------------------
# Synthetic fixtures
# ---------------------------------------------------------------------------
LADDER_DMESG = """[    5.800000] Run /sbin/init as init process
[   10.000000] hi5622_wifi: loading out-of-tree module taints kernel.
[   10.200000] hi5622_wifi 0000:00:00.0: D2H DIRECT receive service ready (read-only)
[   10.330000] hi5622_wifi 0000:00:00.0: ETE ring @ chan 0x400: nodes dma 0x1 depth 32 buflen 2048
[   10.340000] hi5622_wifi 0000:00:00.0: ETE ring @ chan 0x450: nodes dma 0x2 depth 32 buflen 2048
[   10.350000] hi5622_wifi 0001:00:00.0: ETE ring @ chan 0x400: nodes dma 0x3 depth 32 buflen 2048
[   10.360000] hi5622_wifi 0001:00:00.0: ETE ring @ chan 0x450: nodes dma 0x4 depth 32 buflen 2048
[   10.400000] hi5622_wifi 0000:00:00.0: HCC service ready: 3 SR + 4 DR ring mirrors
[   10.410000] hi5622_wifi 0001:00:00.0: HCC service ready: 3 SR + 4 DR ring mirrors
[   11.000000] hi5622_wifi 0000:00:00.0: iATU: programming 6 inbound viewports through BAR2
[   11.100000] hi5622_wifi 0000:00:00.0: iATU vp5: ctrl2=0x80000000 readback=OK
[   11.200000] hi5622_wifi 0000:00:00.0: upload hi_wifi/FIRMWARE.PD4.bin: 928920 bytes
[   11.500000] hi5622_wifi 0000:00:00.0: upload: 928920 bytes in 2 chunks
[   12.300000] hi5622_wifi 0000:00:00.0: verify target BAR0+0x6f8000: file=928920 bytes diffs=0 match=YES
[   12.400000] hi5622_wifi 0000:00:00.0: seed platform block (built-in INI descriptor): 96 B
[   12.430000] hi5622_wifi 0000:00:00.0: seed: platform + wifi-custom blocks written and verified
[   12.500000] hi5622_wifi 0000:00:00.0: HCC arm: vendor-exact ETE init then SR0-2
[   12.700000] hi5622_wifi 0000:00:00.0: HCC arm: 7 channels armed
[   12.800000] hi5622_wifi 0000:00:00.0: HCC loop: service thread started (cadence 5 ms)
[   13.000000] hi5622_wifi 0000:00:00.0: release: 0x5a5a -> CA 0x40000108
[   13.100000] hi5622_wifi 0000:00:00.0: HCC core: transport release recorded - boot watch armed
[   13.800000] hi5622_wifi 0000:00:00.0: D2H DIRECT READY #1: g4 id1 device_plat_ready
[   14.100000] hi5622_wifi 0000:00:00.0: D2H DIRECT READY #2: g4 id2 device_ready
[   14.200000] hi5622_wifi 0000:00:00.0: hcc-core: heartbeat data=0x00000001 (tick 1000)
[   15.200000] hi5622_wifi 0000:00:00.0: hcc-core: heartbeat data=0x00000002 (tick 2000)
[   16.200000] hi5622_wifi 0000:00:00.0: hcc-core: heartbeat data=0x00000003 (tick 3000)
[   16.260000] hi5622_wifi 0000:00:00.0: hcc-core: h2d_sent data=0x00000008 (tick 3000)
[   16.268000] hi5622_wifi 0000:00:00.0: H2D ctx_create_t1: frame 48 B in a 48 B node committed on SR0 slot 0; coalesced announce id3 out[0]=0x00000000 CONSUMED after 0 polls
[   16.300000] hi5622_wifi 0000:00:00.0: hcc-core: h2d_complete data=0x00000008
[   16.360000] hi5622_wifi 0000:00:00.0: hcc-core: h2d_sent data=0x00000008
[   16.368000] hi5622_wifi 0000:00:00.0: H2D cfg_cal: frame 298 B in a 298 B node committed on SR0 slot 1; coalesced announce id3 out[0]=0x00000000 CONSUMED after 0 polls
[   20.000000] hi5622_wifi 0000:00:00.0: cfg80211: scan request posted by provider (1 ssids, 13 channels); timeout 5000 ms, dwell 60 ms
[   20.160000] hi5622_wifi 0000:00:00.0: cfg80211: scan channel result slot=0 ch=1 (2412 MHz)
[   20.270000] hi5622_wifi 0000:00:00.0: cfg80211: scan channel result slot=1 ch=2 (2417 MHz)
[   20.400000] hi5622_wifi 0000:00:00.0: cfg80211: scan channel result slot=2 ch=3 (2422 MHz)
[   24.900000] hi5622_wifi 0000:00:00.0: cfg80211: scan DONE (finish-abort[host-timeout], results=3 bss=3 complete=0 channel=3)
[   25.000000] hi5622_wifi 0000:00:00.0: DP setup: OK - rings 256/512/4095 (4-byte entries), TXQ table 0x2524, MSDU table 8 entries
[   25.100000] hi5622_wifi 0000:00:00.0: DP txq_sync: g0 id1 key 0x0320 sent
[   25.200000] hi5622_wifi 0000:00:00.0: DP tx_frame: wptr 0x0000->0x0001 table wptr=0x00000001 completion_pending=1
"""

REPORT_TXT = """== SECTION seq-final-h2d_status
H2D send path (real post-ready messages): budget=8 frames_sent=8
counters: ok=8 timeout=0 refused=0 desync=0
H2D send path counters max_polls=0
== SECTION seq-final-hcc_status
DIRECT(device group-A): polls=16 nodes=21 frames=21 id1_seen=1 id2_seen=1 overruns=0 layout=OK
== SECTION seq-final-hcc_ready
HCC ready exchange: phase=READY_TXQ_SYNC fail=none boot_latched=1 boot_age_ms=720 ready={id1=1 id2=1}
"""

DEVICE_SCRIPT = """#!/bin/sh
insmod /root/hi5622_wifi.ko
sleep 1
w_attr prepare 1 prepare
w_attr upload 1 upload
w_attr verify 1 verify
w_attr release 1 release
sleep 0.4
while [ 1 ]; do
  grep -q "id1_seen=1" "$P/hcc_ready" && break
  sleep 1
done
say "READY phase=id1 seen=1 deadline_s=2"
say "READY phase=id2 seen=1 deadline_s=20"
"""

UART_TEXT = """[    0.000000] Booting Linux on physical CPU 0x0
[    5.800000] hi5622_wifi: loading out-of-tree module taints kernel.
[    5.900000] hi5622_wifi 0000:00:00.0: HCC loop: service thread started (cadence 5 ms)
[    5.910000] hi5622_wifi 0000:00:00.0: a
[    5.920000] hi5622_wifi 0000:00:00.0: b
[    5.930000] hi5622_wifi 0000:00:00.0: c
[    6.000000] Run /sbin/init as init process
[   10.000000] hi5622_wifi 0000:00:00.0: cfg80211: start_ap reached (iftype=3 beacon_interval=100 dtim=2 head=63 tail=9)
[   10.020000] hi5622_wifi 0000:00:00.0: frame post 'vap_add': g0 id1 total 48 B; coalesced announce consumed after 0 polls
[   10.030000] hi5622_wifi 0000:00:00.0: frame post 'vap_start': g0 id1 total 40 B; coalesced announce consumed after 0 polls
[   10.300000] hi5622_wifi 0000:00:00.0: frame post 'ap_ssid': g0 id1 total 64 B; coalesced announce consumed after 0 polls
[   10.400000] hi5622_wifi 0000:00:00.0: frame post 'ap_cfg': g0 id1 total 184 B; coalesced announce consumed after 0 polls
[   10.450000] hi5622_wifi 0000:00:00.0: cfg80211: start_ap sequence posted to provider
[   20.000000] hi5622_wifi 0000:00:00.0: cfg80211: scan request posted by provider; timeout 14700 ms
[   20.140000] hi5622_wifi 0000:00:00.0: cfg80211: scan channel result slot=0 ch=1
[   20.260000] hi5622_wifi 0000:00:00.0: cfg80211: scan channel result slot=1 ch=2
[   34.700000] hi5622_wifi 0000:00:00.0: cfg80211: scan DONE (finish-abort[host-timeout], results=2)
[   40.000000] hi5622_wifi 0000:00:00.0: hcc-core: heartbeat data=0x00000001 (tick 1000)
[   41.000000] hi5622_wifi 0000:00:00.0: hcc-core: heartbeat data=0x00000002 (tick 2000)
"""


def make_fixture_root(tmp: str) -> str:
    root = os.path.join(tmp, "fake-repo")
    os.makedirs(os.path.join(root, "tools"))
    open(os.path.join(root, "tools", "bench-link.sh"), "w").write("#!/bin/sh\n")
    base = os.path.join(root, "build", "wifi-bench-phase-matrix", "20260101T000000Z")
    for phase in ("boot", "C2", "D2", "E1"):
        pd = os.path.join(base, phase)
        os.makedirs(pd)
        d = {"dmesg": LADDER_DMESG, "sections": ["_preamble"],
             "protocol": {"session": {"mode": "protocol",
                                      "module": {"size": 407996, "sha256": "ab" * 32},
                                      "firmware": {"len": 928920, "sha256": "cd" * 32}}}}
        if phase == "C2":
            d["dmesg"] += LADDER_DMESG
        json.dump(d, open(os.path.join(pd, "summary.json"), "w"))
        open(os.path.join(pd, "report.txt"), "w").write(REPORT_TXT)
        open(os.path.join(pd, "device-script.sh"), "w").write(DEVICE_SCRIPT)
    pm = {"generated": "2026-01-01T00:00:00Z", "phases": [
        {"phase": "boot", "status": "PASS", "elapsed_s": 6.0,
         "reboot_after": "reboot issued=True rc=0 pre_uptime=62; back after 80.0s, uptime=70s (seen_down=1)"},
        {"phase": "C2", "status": "PASS", "elapsed_s": 7.0,
         "reboot_after": "back after 81.0s, uptime=71s (seen_down=1)"},
        {"phase": "D1", "status": "PASS", "elapsed_s": 7.2,
         "reboot_after": "back after 82.0s, uptime=72s (seen_down=1)"},
        {"phase": "D2", "status": "PASS", "elapsed_s": 14.1,
         "reboot_after": "back after 83.0s, uptime=73s (seen_down=1)"},
        {"phase": "E1", "status": "PASS", "elapsed_s": 6.4, "reboot_after": ""},
    ]}
    os.makedirs(os.path.join(base, ".."), exist_ok=True)
    json.dump(pm, open(os.path.join(root, "build", "wifi-bench-phase-matrix",
                                    "20260101T000000Z", "phase-matrix.json"), "w"))
    uartd = os.path.join(root, "build", "uart-logs")
    os.makedirs(uartd)
    open(os.path.join(uartd, "fixture-uart.log"), "w").write(UART_TEXT)
    return root


def run_tests() -> int:
    tmp = tempfile.mkdtemp(prefix="wifi-perf-selftest-")
    try:
        # ---- 1. device probe script (syntax + refusal) ------------------
        rc = subprocess.run(["sh", "-n", P.PROBE], capture_output=True)
        check("probe-sh-syntax", rc.returncode == 0, rc.stderr.decode()[:200])
        rc = subprocess.run(["sh", P.PROBE, "--help"], capture_output=True, text=True)
        check("probe-help", rc.returncode == 0 and "read-only" in rc.stdout)
        rc = subprocess.run(["sh", P.PROBE, "--bogus"], capture_output=True, text=True)
        check("probe-unknown-arg-refused", rc.returncode == 2)

        # ---- 2. dmesg parser -------------------------------------------
        ev = P.parse_dmesg(LADDER_DMESG)
        check("dmesg-parse-count", len(ev) >= 30, "got %d" % len(ev))
        check("dmesg-first-ts", ev[0][0] == 5.8)
        check("dmesg-first-needle", P.first_ts(ev, "release: 0x5a5a") == 13.0)
        check("dmesg-missing", P.first_ts(ev, "does not exist") is None)

        # ---- 3. session summary extraction -----------------------------
        root = make_fixture_root(tmp)
        b = P.Baseline()
        status = os.path.join(root, "build", "wifi-bench-phase-matrix",
                              "20260101T000000Z", "boot", "summary.json")
        P.collect_session_summary(status, b)
        m = b.metrics
        check("ladder-load-to-ready", between(m["boot.load_to_ready_pair_ms"].samples[0].value, 4100, 4100))
        check("ladder-upload", between(m["boot.upload_ms"].samples[0].value, 300, 300))
        check("ladder-verify", between(m["boot.verify_ms"].samples[0].value, 800, 800))
        check("ladder-release-ready", between(m["boot.release_to_ready_pair_ms"].samples[0].value, 1100, 1100))
        check("ladder-ready1", between(m["boot.release_to_ready1_ms"].samples[0].value, 800, 800))
        check("ladder-ready2-delta", between(m["boot.ready1_to_ready2_ms"].samples[0].value, 300, 300))
        check("h2d-consumed", between(m["h2d.send_to_consumed_ms"].samples[0].value, 8, 8))
        check("h2d-polls-zero", m["h2d.send_to_consumed_polls"].samples[0].value == 0)
        check("h2d-spacing", between(m["h2d.frame_spacing_ms"].samples[0].value, 100, 100))
        check("h2d-complete", between(m["h2d.send_to_complete_ms"].samples[0].value, 40, 40))
        check("scan-first-latency", between(m["scan.first_result_latency_ms"].samples[0].value, 160, 160))
        check("scan-channels", m["scan.channels_seen"].samples[0].value == 3)
        check("scan-window", between(m["scan.host_window_ms"].samples[0].value, 5000, 5000))
        check("scan-done", between(m["scan.done_total_ms"].samples[0].value, 4900, 4900))
        check("scan-effective", between(m["scan.effective_ms_per_channel"].samples[0].value, 1666.67, 1666.67))
        check("scan-results", m["scan.results"].samples[0].value == 3)
        check("heartbeat-interval", between(m["heartbeat.interval_ms"].samples[0].value, 1000, 1000))
        check("heartbeat-tick", between(m["heartbeat.tick_delta_ms"].samples[0].value, 1000, 1000))
        check("dp-rings-tx", m["dp.rings_tx"].samples[0].value == 256)
        check("dp-txq-bytes", m["dp.txq_table_bytes"].samples[0].value == 0x2524)
        check("dp-msdu", m["dp.msdu_table_entries"].samples[0].value == 8)
        check("dp-publish", m["dp.tx_published_frames"].samples[0].value == 1)
        check("dp-post-only", m["dp.tx_completion_pending"].samples[0].value == 1)
        check("mem-ring-buffers", m["mem.ring_buffers_bytes"].samples[0].value == 2 * 32 * 2048)
        check("mem-module-size", m["mem.module_artifact_bytes"].samples[0].value == 407996)
        check("mem-firmware-size", m["mem.firmware_image_bytes"].samples[0].value == 928920)
        check("console-cost", between(m["console.printk_line_ms"].samples[0].value, 10, 10))
        check("report-max-polls", m["h2d.max_polls"].samples[0].value == 0)
        check("report-d2h-polls", m["d2h.poll_counter"].samples[0].value == 16)
        check("report-ready-age", m["ready.boot_age_ms"].samples[0].value == 720)
        check("script-sleeps", len(m["boot.script_fixed_sleep_s"].samples) == 3)
        check("script-deadlines", len(m["boot.ready_deadline_s"].samples) == 2)
        check("ready-poll-1s", m["boot.ready_poll_interval_s"].samples[0].value == 1.0)

        # ---- 4. phase matrix extraction --------------------------------
        b2 = P.Baseline()
        pm = os.path.join(root, "build", "wifi-bench-phase-matrix",
                          "20260101T000000Z", "phase-matrix.json")
        P.collect_phase_matrix(pm, b2)
        check("matrix-phase-elapsed", between(b2.metrics["bench.phase.D2.elapsed_s"].samples[0].value, 14.1, 14.1))
        check("matrix-reboot", between(b2.metrics["bench.reboot.cycle_s"].samples[0].value, 80, 80))
        check("matrix-total-phase", between(b2.metrics["bench.matrix.total_phase_elapsed_s"].samples[0].value,
                                           40.7, 40.7))
        check("matrix-total-reboot", between(b2.metrics["bench.matrix.total_reboot_s"].samples[0].value,
                                             326, 326))

        # ---- 5. UART extraction ----------------------------------------
        b3 = P.Baseline()
        P.collect_uart(os.path.join(root, "build", "uart-logs", "fixture-uart.log"), b3)
        m3 = b3.metrics
        check("uart-ap-total", between(m3["ap.start_ap_to_sequence_posted_ms"].samples[0].value, 450, 450))
        check("uart-ap-frames", m3["ap.frames_posted"].samples[0].value == 4)
        check("uart-ap-polls", all(s.value == 0 for s in m3["ap.frame_consumed_polls"].samples))
        check("uart-beacon", m3["ap.beacon_interval"].samples[0].value == 100)
        check("uart-dtim", m3["ap.dtim"].samples[0].value == 2)
        check("uart-scan-window", between(m3["scan.host_window_ms"].samples[0].value, 14700, 14700))
        check("uart-scan-channels", m3["scan.channels_seen"].samples[0].value == 2)
        check("uart-scan-effective", between(m3["scan.effective_ms_per_channel"].samples[0].value, 7350, 7350))
        check("uart-heartbeat", between(m3["heartbeat.interval_ms"].samples[0].value, 1000, 1000))
        check("uart-console-cost", between(m3["console.printk_line_ms"].samples[0].value, 10, 10))
        check("uart-kernel-to-init", between(m3["os.kernel_to_userspace_ms"].samples[0].value, 6000, 6000))

        # ---- 6. aggregation + writers + determinism --------------------
        vals = [s.value for s in b2.metrics["bench.reboot.cycle_s"].samples]
        summ = b2.metrics["bench.reboot.cycle_s"].summary()
        check("agg-n", summ["n"] == len(vals) == 4)
        check("agg-minmax", summ["min"] == 80 and summ["max"] == 83)
        check("agg-median", summ["median"] == 81.5)
        outdir = os.path.join(tmp, "out")
        written = b.write_all(outdir)
        check("writer-count", len(written) == 3)
        check("writer-json", os.path.isfile(os.path.join(outdir, "baseline.json")))
        check("writer-csv", os.path.isfile(os.path.join(outdir, "baseline.csv")))
        check("writer-md", os.path.isfile(os.path.join(outdir, "BASELINE.md")))
        js = json.load(open(os.path.join(outdir, "baseline.json")))
        check("json-schema", js.get("schema") == P.SCHEMA)
        check("json-host-only", js.get("host_only") is True and js.get("device_contacted") is False)
        check("json-metrics", len(js.get("metrics", {})) == len(b.metrics))
        check("json-grades", set(js.get("grades", {})) == {"V*", "V", "I", "U"})
        csv_txt = open(os.path.join(outdir, "baseline.csv")).read()
        check("csv-header", csv_txt.splitlines()[0].startswith("metric,unit,grade,n"))
        md_txt = open(os.path.join(outdir, "BASELINE.md")).read()
        check("md-table", "| metric |" in md_txt and "boot.upload_ms" in md_txt)

        # ---- 7. documented input --------------------------------------
        b4 = P.Baseline()
        P.load_documented(P.DOCUMENTED_INPUT, b4)
        check("documented-schema", len(b4.documented) == 27)
        check("documented-grade", all(e.get("grade") == "V" for e in b4.documented))
        check("documented-metric", "ap.hostapd_launch_to_ap_enabled_ms" in b4.metrics)

        # ---- 8. extraction driver on the fixture repo -----------------
        old_lane = P.LANE_DIR
        try:
            P.LANE_DIR = os.path.join(root, "opensource", "docs", "incoming",
                                      "wifi-perf-baseline")
            root2 = P.repo_root()
        finally:
            P.LANE_DIR = old_lane
        # repo_root() walks up from LANE_DIR; the fake repo has no such lane dir,
        # so the extractor is exercised through the collectors above.  The real
        # repo extraction is exercised by the CLI dry-run check below.

        # ---- 9. live gating (no device contact) -----------------------
        class A:
            yes_really = False
            attended = False
            host = None
        check("live-refuse-default", P.live_guard(A()) != "")
        A.yes_really = True
        A.attended = True
        env = dict(os.environ)
        env.pop("PERF_ALLOW_DEVICE", None)
        old_env = os.environ.get("PERF_ALLOW_DEVICE")
        try:
            os.environ.pop("PERF_ALLOW_DEVICE", None)
            check("live-refuse-without-env", P.live_guard(A()) != "")
            os.environ["PERF_ALLOW_DEVICE"] = "1"
            check("live-allow-with-both", P.live_guard(A()) == "")
        finally:
            if old_env is None:
                os.environ.pop("PERF_ALLOW_DEVICE", None)
            else:
                os.environ["PERF_ALLOW_DEVICE"] = old_env

        # ---- 10. probe output parser + deltas -------------------------
        probe_txt = """PERF-PROBE v1
SNAP-START 0 100.00 1000
CPU-AGG 10 0 5 80 0 0 0 0
STAT ctxt 1000
PARAM hcc_poll_ms=5
THREAD 42 hi5622_wifi-svc 10 0
ATTR-BEGIN 0000:00:00.0 rd_status
D2H DIRECT status: init=1 boot_ok=1 windows=1 polls=20 manual=0 nodes=0 frames=0
ATTR-END
DMESG-BEGIN
DMESG-END
SNAP-END
SNAP-START 1 105.00 1005
CPU-AGG 30 0 15 110 0 0 0 0
STAT ctxt 3000
PARAM hcc_poll_ms=5
THREAD 42 hi5622_wifi-svc 210 0
ATTR-BEGIN 0000:00:00.0 rd_status
D2H DIRECT status: init=1 boot_ok=1 windows=1 polls=1020 manual=0 nodes=0 frames=0
ATTR-END
DMESG-BEGIN
DMESG-END
SNAP-END
SNAPSHOTS 2
PERF-PROBE-END
"""
        snaps = P.parse_probe_output(probe_txt)
        check("probe-parse-snapshots", len(snaps) == 2)
        check("probe-parse-params", snaps[0]["params"].get("hcc_poll_ms") == "5")
        check("probe-parse-thread", snaps[0]["threads"].get("42", {}).get("comm") == "hi5622_wifi-svc")
        ps = P.probe_summary(snaps)
        row = ps["intervals"][0]
        check("probe-cpu-busy", between(row["cpu_busy_pct"], 50, 50))
        check("probe-polls-rate", abs(row["d2h_polls_per_s"].get("0000:00:00.0:rd_status", 0) - 200) < 0.01)
        check("probe-thread-cpu", abs(row["thread_cpu_pct"].get("hi5622_wifi-svc", 0) - 40) < 0.01)
        check("probe-ctxt", row["ctxt_delta"] == 2000)

        # ---- 11. CLI gates (host-only commands) -----------------------
        lane = LANE_DIR
        r = subprocess.run([sys.executable, os.path.join(lane, "tools", "perf_session.py"),
                            "dry-run"], capture_output=True, text=True, cwd=lane)
        check("cli-dry-run", r.returncode == 0 and "extract smoke" in r.stdout, r.stderr[-300:])
        r = subprocess.run([sys.executable, os.path.join(lane, "tools", "perf_session.py"),
                            "probe"], capture_output=True, text=True, cwd=lane)
        check("cli-probe-refused", r.returncode == 2 and "REFUSED" in r.stderr)
        r = subprocess.run([sys.executable, os.path.join(lane, "tools", "perf_session.py"),
                            "watch", "--dry-run", "--duration", "5", "--interval", "1"],
                           capture_output=True, text=True, cwd=lane)
        check("cli-watch-dryrun-refused", r.returncode == 2 and "REFUSED" in r.stderr)
        r = subprocess.run([sys.executable, os.path.join(lane, "tools", "perf_session.py"),
                            "extract", "--dry-run"], capture_output=True, text=True, cwd=lane)
        check("cli-extract-dryrun", r.returncode == 0 and "DRY-RUN" in r.stdout, r.stderr[-300:])
        r = subprocess.run([sys.executable, os.path.join(lane, "tools", "perf_session.py"),
                            "list-evidence"], capture_output=True, text=True, cwd=lane)
        check("cli-list-evidence", r.returncode == 0 and "phase_matrices" in r.stdout)
        r = subprocess.run([sys.executable, os.path.join(lane, "tools", "perf_session.py")],
                           capture_output=True, text=True, cwd=lane)
        check("cli-no-command-usage", r.returncode == 2)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    print("wifi-perf-baseline selftest: %d checks, %d failure(s)"
          % (len(CHECKS), len(FAILURES)))
    for f in FAILURES:
        print("  FAIL: %s" % f)
    return 0 if not FAILURES else 1


if __name__ == "__main__":
    sys.exit(run_tests())
