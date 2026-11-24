#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""perf_session.py - host-side metrics harness for the clean-room Hi5622 Wi-Fi stack.

Lane: wifi-perf-baseline (shared task task-132).  This is the HOST half of the
harness; the device half is tools/perf_probe.sh (read-only /proc, /sys, dmesg).

Two modes, both host-only by default:

  extract   ingest the project's RECORDED bench evidence
            (build/wifi-bench-phase-matrix/*, build/wifi-bench/*,
            build/wifi-bench-session/*, build/uart-logs/*) and emit the
            baseline JSON/CSV/Markdown.  This is the mode that produced the
            committed baseline in baseline/; it contacts no device.

  probe     (gated live mode, ATTENDED OPERATOR ONLY) pipe tools/perf_probe.sh
  watch     to the bench box through tools/rsh.sh on stdin, so nothing is
            written on the device.  Refuses to run without --yes-really
            --attended and PERF_ALLOW_DEVICE=1.  This lane never ran it
            (host-only lane).

Every metric carries its evidence (session/evidence file), its method, and a
grade:
  V*  measured, raw evidence (session summary dmesg / UART console / host
      session record)
  V   documented in a prior lane's hardware-validated result, not re-derived
      from raw evidence in this lane
  I   inference from measured values / source constants
  U   not measured yet (the live probe is the instrument that closes it)

Usage:
  python3 tools/perf_session.py extract            # regenerate baseline/
  python3 tools/perf_session.py dry-run            # validate, no writes, no device
  python3 tools/perf_session.py list-evidence
  python3 tools/perf_session.py probe --dry-run
  python3 tools/perf_session.py watch --dry-run --duration 30 --interval 5
  python3 tools/perf_session.py watch --yes-really --attended --duration 30
"""
from __future__ import annotations

import argparse
import csv
import dataclasses
import glob
import hashlib
import json
import os
import re
import statistics
import subprocess
import sys
import time

SCHEMA = "hi5622-perf-baseline/1"
DOCUMENTED_SCHEMA = "hi5622-perf-documented/1"
TOOL_VERSION = "wifi-perf-baseline/1"
TOOLDIR = os.path.dirname(os.path.abspath(__file__))
LANE_DIR = os.path.dirname(TOOLDIR)
PROBE = os.path.join(TOOLDIR, "perf_probe.sh")
DOCUMENTED_INPUT = os.path.join(LANE_DIR, "baseline", "documented-input.json")
DEFAULT_OUT = os.path.join(LANE_DIR, "baseline")

TS_RE = re.compile(r"^\[\s*(\d+\.\d+)\]\s?(.*)$")
BDF_RE = re.compile(r"hi5622_wifi (000[0-9]:00:00\.0):")
DRV_LINE_RE = re.compile(
    r"(hi5622_wifi|hcc-core|hi5622 frames|D2H DIRECT|cfg80211: (scan|start_ap|VAP|wiphy|REPORTS_OBSS|mgmt_stypes))"
)


def repo_root() -> str:
    cur = LANE_DIR
    while True:
        if os.path.isfile(os.path.join(cur, "tools", "bench-link.sh")):
            return cur
        parent = os.path.dirname(cur)
        if parent == cur:
            return os.getcwd()
        cur = parent


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    try:
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        return h.hexdigest()
    except OSError:
        return "missing"


def rel(path: str, root: str) -> str:
    try:
        return os.path.relpath(path, root)
    except ValueError:
        return path


# ---------------------------------------------------------------------------
# Metric model
# ---------------------------------------------------------------------------
@dataclasses.dataclass
class Sample:
    value: float
    evidence: str
    detail: str = ""


@dataclasses.dataclass
class Metric:
    mid: str
    title: str
    unit: str
    method: str
    grade: str
    samples: list = dataclasses.field(default_factory=list)

    def add(self, value, evidence, detail="") -> None:
        try:
            v = float(value)
        except (TypeError, ValueError):
            return
        self.samples.append(Sample(round(v, 4), evidence, detail))

    def summary(self) -> dict:
        vals = [s.value for s in self.samples]
        if not vals:
            return {"n": 0}
        return {
            "n": len(vals),
            "min": round(min(vals), 4),
            "median": round(statistics.median(vals), 4),
            "mean": round(statistics.fmean(vals), 4),
            "max": round(max(vals), 4),
            "stdev": round(statistics.pstdev(vals), 4) if len(vals) > 1 else 0.0,
        }


class Baseline:
    def __init__(self):
        self.metrics: dict = {}
        self.evidence: dict = {}
        self.warnings: list = []
        self.documented: list = []

    def metric(self, mid: str, title: str, unit: str, method: str, grade: str) -> Metric:
        m = self.metrics.get(mid)
        if m is None:
            m = Metric(mid, title, unit, method, grade)
            self.metrics[mid] = m
        return m

    def add(self, mid, title, unit, method, grade, value, evidence, detail=""):
        self.metric(mid, title, unit, method, grade).add(value, evidence, detail)

    def note_evidence(self, kind: str, path: str) -> None:
        entry = self.evidence.setdefault(kind, [])
        r = rel(path, repo_root())
        if r in [e.get("path") for e in entry]:
            return
        entry.append({"path": r, "sha256": sha256_file(path)})

    def warn(self, msg: str) -> None:
        if msg not in self.warnings:
            self.warnings.append(msg)

    def to_dict(self) -> dict:
        return {
            "schema": SCHEMA,
            "tool_version": TOOL_VERSION,
            "generated_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "host_only": True,
            "device_contacted": False,
            "grades": {
                "V*": "measured here from raw recorded evidence (dmesg/UART/session record)",
                "V": "hardware-validated result documented by a prior lane, not re-derived here",
                "I": "inference from measured values or source constants",
                "U": "not measured yet; the live perf_probe/watch mode is the instrument",
            },
            "evidence": self.evidence,
            "metrics": {
                mid: {
                    "title": m.title,
                    "unit": m.unit,
                    "method": m.method,
                    "grade": m.grade,
                    "summary": m.summary(),
                    "samples": [dataclasses.asdict(s) for s in m.samples],
                }
                for mid, m in sorted(self.metrics.items())
            },
            "documented": self.documented,
            "warnings": self.warnings,
        }

    # -- writers ---------------------------------------------------------
    def write_all(self, outdir: str) -> list:
        os.makedirs(outdir, exist_ok=True)
        written = []
        jp = os.path.join(outdir, "baseline.json")
        with open(jp, "w") as f:
            json.dump(self.to_dict(), f, indent=1, sort_keys=True)
            f.write("\n")
        written.append(jp)
        cp = os.path.join(outdir, "baseline.csv")
        with open(cp, "w", newline="") as f:
            w = csv.writer(f, lineterminator="\n")
            w.writerow(["metric", "unit", "grade", "n", "min", "median", "mean",
                        "max", "stdev", "method"])
            for mid, m in sorted(self.metrics.items()):
                s = m.summary()
                w.writerow([mid, m.unit, m.grade, s["n"], s.get("min"), s.get("median"),
                            s.get("mean"), s.get("max"), s.get("stdev"), m.method])
        written.append(cp)
        mp = os.path.join(outdir, "BASELINE.md")
        with open(mp, "w") as f:
            f.write("# Wi-Fi stack performance baseline (generated)\n\n")
            f.write("Generated by tools/perf_session.py extract (schema %s).\n" % SCHEMA)
            f.write("Host-only: produced from recorded bench evidence; no device contact.\n\n")
            f.write("| metric | unit | grade | n | min | median | mean | max |\n")
            f.write("| --- | --- | --- | ---: | ---: | ---: | ---: | ---: |\n")
            for mid, m in sorted(self.metrics.items()):
                s = m.summary()
                f.write("| %s | %s | %s | %d | %s | %s | %s | %s |\n" % (
                    mid, m.unit, m.grade, s["n"], s.get("min", "-"),
                    s.get("median", "-"), s.get("mean", "-"), s.get("max", "-")))
            if self.warnings:
                f.write("\n## Warnings\n\n")
                for wmsg in self.warnings:
                    f.write("- %s\n" % wmsg)
        written.append(mp)
        return written


# ---------------------------------------------------------------------------
# Parsers
# ---------------------------------------------------------------------------
def parse_dmesg(text: str) -> list:
    out = []
    for line in (text or "").splitlines():
        m = TS_RE.match(line)
        if m:
            out.append((float(m.group(1)), m.group(2)))
    return out


def first_ts(events, needle: str, after: float = None):
    for t, msg in events:
        if after is not None and t < after:
            continue
        if needle in msg:
            return t
    return None


def all_matching(events, needle: str) -> list:
    return [(t, msg) for t, msg in events if needle in msg]


def last_ts(events, needle: str):
    hits = all_matching(events, needle)
    return hits[-1][0] if hits else None


def _num(pattern: str, text: str, default=None):
    m = re.search(pattern, text)
    if not m:
        return default
    try:
        return float(m.group(1))
    except (IndexError, ValueError):
        return default


def _ms(a, b):
    return None if a is None or b is None else round((b - a) * 1000.0, 2)


# ---------------------------------------------------------------------------
# Collector: phase matrices (host wall-clock per phase + reboot cycle)
# ---------------------------------------------------------------------------
def collect_phase_matrix(path: str, base: Baseline) -> None:
    root = repo_root()
    try:
        d = json.load(open(path))
    except (OSError, ValueError):
        base.warn("unreadable phase matrix: %s" % rel(path, root))
        return
    base.note_evidence("phase_matrices", path)
    for ph in d.get("phases", []):
        phase = ph.get("phase") or "?"
        elapsed = ph.get("elapsed_s")
        status = ph.get("status") or "?"
        ev = "%s:%s" % (rel(path, root), phase)
        if elapsed is not None:
            base.add("bench.phase.%s.elapsed_s" % phase,
                     "host wall time of matrix phase '%s'" % phase, "s",
                     "phase-matrix.json elapsed_s",
                     "V*", elapsed, ev, "status=%s module=%s" % (status, ph.get("module_size")))
        rb = ph.get("reboot_after") or ""
        m = re.search(r"back after ([0-9.]+)s", rb)
        if m:
            base.add("bench.reboot.cycle_s", "reboot -> box reachable again (attended matrix)", "s",
                     "phase-matrix reboot_after record", "V*", float(m.group(1)), ev,
                     "seen_down=%s" % ("1" if "seen_down=1" in rb else "0"))
        if rb:
            m = re.search(r"pre_uptime=([0-9]+)", rb)
            if m:
                base.add("bench.reboot.pre_uptime_s", "device uptime at reboot command", "s",
                         "phase-matrix reboot_after record", "V*", float(m.group(1)), ev)
    phases = d.get("phases", [])
    if len(phases) >= 5 and all(p.get("status") == "PASS" for p in phases):
        total_phase = sum(p.get("elapsed_s") or 0.0 for p in phases)
        total_reboot = 0.0
        n_rb = 0
        for p in phases:
            mm = re.search(r"back after ([0-9.]+)s", p.get("reboot_after") or "")
            if mm:
                total_reboot += float(mm.group(1))
                n_rb += 1
        base.add("bench.matrix.total_phase_elapsed_s",
                 "one-command 5-phase matrix: sum of phase elapsed times", "s",
                 "sum(phase-matrix elapsed_s)", "V*", round(total_phase, 2), rel(path, root))
        base.add("bench.matrix.total_reboot_s",
                 "one-command 5-phase matrix: sum of between-phase reboot cycles", "s",
                 "sum(phase-matrix reboot_after back-after values)", "V*",
                 round(total_reboot, 2), rel(path, root), "reboots=%d" % n_rb)


# ---------------------------------------------------------------------------
# Collector: bench-session JSON (wifi-bench-session package)
# ---------------------------------------------------------------------------
def collect_bench_session_json(path: str, base: Baseline) -> None:
    root = repo_root()
    try:
        d = json.load(open(path))
    except (OSError, ValueError):
        return
    base.note_evidence("bench_sessions", path)
    for ph in d.get("phases", []):
        el = ph.get("elapsed_s")
        ev = "%s:%s" % (rel(path, root), ph.get("phase"))
        if el and ph.get("status") not in (None, "PLAN-OK", "DEPENDENCY-PENDING"):
            base.add("bench.attended_phase.%s.elapsed_s" % (ph.get("phase") or "?"),
                     "host wall time of attended phase '%s'" % ph.get("phase"), "s",
                     "wifi-bench-session session.json elapsed_s", "V*",
                     el, ev, "status=%s bench_rc=%s" % (ph.get("status"), ph.get("bench_rc")))
    cr = d.get("cleanup_reboot") or ""
    m = re.search(r"back after ([0-9.]+)s", cr)
    if m:
        base.add("bench.reboot.cleanup_back_s", "cleanup reboot -> reachable again", "s",
                 "wifi-bench-session session.json cleanup_reboot", "V*",
                 float(m.group(1)), rel(path, root))


# ---------------------------------------------------------------------------
# Collector: bench_session summary.json -> dmesg-derived metrics
# ---------------------------------------------------------------------------
def _parse_report_sections(path: str) -> dict:
    """Parse bench_session report.txt (== SECTION <name> blocks) into a dict."""
    out = {}
    cur = None
    buf = []
    try:
        fh = open(path, errors="replace")
    except OSError:
        return out
    with fh:
        for line in fh:
            m = re.match(r"^== SECTION (\S+)\s*$", line)
            if m:
                if cur is not None:
                    out[cur] = "\n".join(buf)
                cur = m.group(1)
                buf = []
            elif cur is not None:
                buf.append(line.rstrip("\n"))
    if cur is not None:
        out[cur] = "\n".join(buf)
    return out


def collect_session_summary(path: str, base: Baseline) -> None:
    root = repo_root()
    try:
        d = json.load(open(path))
    except (OSError, ValueError):
        return
    dmesg = d.get("dmesg") or ""
    if not dmesg.strip():
        return
    ev = rel(path, root)
    events = parse_dmesg(dmesg)
    if not events:
        return
    base.note_evidence("session_summaries", path)
    sess = d.get("session") or (d.get("protocol") or {}).get("session") or {}
    mode = sess.get("mode") or "?"
    mod = sess.get("module") or {}
    if mod.get("size"):
        base.add("mem.module_artifact_bytes", "driver .ko artifact size", "B",
                 "bench_session session.module.size", "V*", mod.get("size"), ev,
                 "sha256=%s mode=%s" % (str(mod.get("sha256"))[:12], mode))
    fw = sess.get("firmware") or {}
    if fw.get("len"):
        base.add("mem.firmware_image_bytes", "PD4 research firmware image size (BAR0 MMIO upload)", "B",
                 "bench_session session.firmware.len", "V*", fw.get("len"), ev,
                 "sha256=%s" % str(fw.get("sha256"))[:12])

    # ---- boot ladder (kernel timestamps) --------------------------------
    load = first_ts(events, "hi5622_wifi: loading out-of-tree module taints")
    svc0 = first_ts(events, "D2H DIRECT receive service ready")
    svc_all = last_ts(events, "HCC service ready")
    prep0 = first_ts(events, "iATU: programming 6 inbound viewports")
    prep1 = first_ts(events, "iATU vp5:")
    up0 = first_ts(events, "upload hi_wifi/")
    up1 = first_ts(events, "upload: 928920 bytes in")
    ver = first_ts(events, "verify target BAR0+0x6f8000")
    seed0 = first_ts(events, "seed platform block")
    seed1 = first_ts(events, "seed: platform + wifi-custom blocks")
    arm0 = first_ts(events, "HCC arm: vendor-exact ETE init")
    arm1 = first_ts(events, "HCC arm: 7 channels armed")
    loop = first_ts(events, "HCC loop: service thread started")
    release_t = first_ts(events, "release: 0x5a5a")
    ready1 = first_ts(events, "D2H DIRECT READY #1")
    ready2 = first_ts(events, "D2H DIRECT READY #2")
    if load is not None and svc0 is not None:
        base.add("boot.load_to_service_ready_ms", "insmod -> direct-D2H service ready (endpoint 0)", "ms",
                 "dmesg kernel timestamps", "V*", _ms(load, svc0), ev)
    if load is not None and svc_all is not None:
        base.add("boot.load_to_both_endpoints_ms", "insmod -> both endpoints probed", "ms",
                 "dmesg kernel timestamps", "V*", _ms(load, svc_all), ev)
    if prep0 is not None and prep1 is not None:
        base.add("boot.prepare_ms", "prepare: 6 inbound iATU viewports programmed+read back", "ms",
                 "dmesg kernel timestamps (prepare write -> last iATU vp line)", "V*", _ms(prep0, prep1), ev)
    if up0 is not None and up1 is not None:
        base.add("boot.upload_ms", "firmware upload (928,920 B, 2 chunks) [driver]", "ms",
                 "dmesg kernel timestamps", "V*", _ms(up0, up1), ev)
        if up1 > up0:
            base.add("boot.upload_MBps", "firmware upload throughput", "MB/s",
                     "928,920 B / dmesg upload duration", "V*", round(0.928920 / (up1 - up0), 3), ev)
    if up1 is not None and ver is not None:
        base.add("boot.verify_ms", "firmware verify: read back 928,920 B, diffs=0 [driver]", "ms",
                 "dmesg kernel timestamps (upload done -> verify done; no host sleep between)", "V*",
                 _ms(up1, ver), ev)
        if ver > up1:
            base.add("boot.verify_MBps", "firmware verify throughput (BAR0 read-back+compare)", "MB/s",
                     "928,920 B / dmesg verify duration", "V*", round(0.928920 / (ver - up1), 3), ev)
    if seed0 is not None and seed1 is not None:
        base.add("boot.seed_ms", "seed platform+wifi-custom blocks (96 B + 2472 B, readback)", "ms",
                 "dmesg kernel timestamps", "V*", _ms(seed0, seed1), ev)
    if arm0 is not None and arm1 is not None:
        base.add("boot.hcc_arm_ms", "HCC arm: ETE init + 7 ring arm/readback", "ms",
                 "dmesg kernel timestamps", "V*", _ms(arm0, arm1), ev)
    if loop is not None and release_t is not None:
        base.add("boot.loop_to_release_ms", "HCC loop started -> CPU release word 0x5a5a", "ms",
                 "dmesg kernel timestamps", "V*", _ms(loop, release_t), ev)
    if release_t is not None and ready1 is not None:
        base.add("boot.release_to_ready1_ms", "CPU release -> g4 id1 device_plat_ready (direct reader)", "ms",
                 "dmesg kernel timestamps", "V*", _ms(release_t, ready1), ev)
    if ready1 is not None and ready2 is not None:
        base.add("boot.ready1_to_ready2_ms", "g4 id1 -> g4 id2 DEVICE READY", "ms",
                 "dmesg kernel timestamps", "V*", _ms(ready1, ready2), ev)
    if load is not None and ready2 is not None:
        base.add("boot.load_to_ready_pair_ms", "insmod -> READY pair complete (kernel-observed ladder)", "ms",
                 "dmesg kernel timestamps", "V*", _ms(load, ready2), ev)
    if release_t is not None and ready2 is not None:
        base.add("boot.release_to_ready_pair_ms", "CPU release -> READY pair complete", "ms",
                 "dmesg kernel timestamps", "V*", _ms(release_t, ready2), ev)

    # ---- script-level fixed sleeps / ready polling ----------------------
    script = os.path.join(os.path.dirname(path), "device-script.sh")
    if os.path.isfile(script):
        try:
            txt = open(script, errors="replace").read()
        except OSError:
            txt = ""
        for m in re.finditer(r"^\s*sleep\s+([0-9.]+)\s*$", txt, re.M):
            base.add("boot.script_fixed_sleep_s", "fixed host-side sleep in the generated boot script", "s",
                     "generated device-script.sh sleep lines", "V*", float(m.group(1)),
                     rel(script, root))
        for m in re.finditer(r"deadline_s=([0-9]+)", txt):
            base.add("boot.ready_deadline_s", "generated boot-script ready-phase deadline", "s",
                     "generated device-script.sh deadline_s=", "V*", float(m.group(1)),
                     rel(script, root))
        if "sleep 1" in txt:
            base.add("boot.ready_poll_interval_s", "ready-phase poll cadence (shell sleep)", "s",
                     "generated device-script.sh ready loop", "V*", 1.0, rel(script, root))
        base.note_evidence("device_scripts", script)

    # ---- ready attribute captures --------------------------------------
    sections = d.get("sections") or {}
    if not isinstance(sections, dict):
        sections = _parse_report_sections(os.path.join(os.path.dirname(path), "report.txt"))
    for name, body in sections.items():
        if not isinstance(body, str):
            continue
        if "boot_age_ms=" in body:
            base.add("ready.boot_age_ms", "device boot_age_ms observed by the ready gate", "ms",
                     "hcc_ready capture in session summary", "V*",
                     _num(r"boot_age_ms=(\d+)", body), "%s#%s" % (ev, name))
        if "phase=READY_TXQ_SYNC" in body:
            base.add("ready.phase_reached", "ready phase reached (1 = READY_TXQ_SYNC)", "bool",
                     "hcc_ready capture", "V*", 1, "%s#%s" % (ev, name))
        if "max_polls=" in body:
            base.add("h2d.max_polls", "max announce-consumption polls (5 ms units, 0 => consumed on first read)",
                     "polls", "h2d_status capture counters", "V*",
                     _num(r"max_polls=(\d+)", body), "%s#%s" % (ev, name))
        m = re.search(r"counters: ok=(\d+) timeout=(\d+) refused=(\d+)", body)
        if m:
            base.add("h2d.frames_ok", "H2D frames consumed by the firmware", "frames",
                     "h2d_status capture counters", "V*", int(m.group(1)), "%s#%s" % (ev, name))
            base.add("h2d.frames_timeout", "H2D frames that timed out", "frames",
                     "h2d_status capture counters", "V*", int(m.group(2)), "%s#%s" % (ev, name))
        m = re.search(r"DIRECT\(device group-A\): polls=(\d+) nodes=(\d+) frames=(\d+)", body)
        if m:
            base.add("d2h.poll_counter", "direct D2H reader poll counter at capture", "polls",
                     "hcc_status/rd_status capture", "V*", int(m.group(1)), "%s#%s" % (ev, name))
            base.add("d2h.frames_counter", "direct D2H frames decoded at capture", "frames",
                     "hcc_status/rd_status capture", "V*", int(m.group(2)), "%s#%s" % (ev, name))

    # ---- H2D send -> announce-consumed round trip ----------------------
    sent_t = None
    h2d_frames = []
    for t, msg in events:
        if "hcc-core: h2d_sent" in msg:
            sent_t = t
        m = re.search(r"H2D ([a-z0-9_]+): frame (\d+) B .*?CONSUMED after (\d+) polls", msg)
        if m:
            h2d_frames.append((t, m.group(1), int(m.group(3))))
            if sent_t is not None and 0 <= (t - sent_t) < 2.0:
                base.add("h2d.send_to_consumed_ms",
                         "H2D frame write -> coalesced announce consumed (driver observation)", "ms",
                         "dmesg h2d_sent -> CONSUMED after N polls delta; N is in 5 ms poll units",
                         "V*", _ms(sent_t, t), ev, "msg=%s polls=%d" % (m.group(1), int(m.group(3))))
                base.add("h2d.send_to_consumed_polls", "consumption poll count for one H2D frame", "polls",
                         "dmesg CONSUMED after N polls", "V*", int(m.group(3)), ev,
                         "msg=%s" % m.group(1))
    completes = [t for t, msg in events if "hcc-core: h2d_complete" in msg]
    sent_list = [t for t, msg in events if "hcc-core: h2d_sent" in msg]
    for st in sent_list:
        nxt = next((c for c in completes if 0 <= c - st < 1.0), None)
        if nxt is not None:
            base.add("h2d.send_to_complete_ms", "H2D frame write -> mailbox completion event", "ms",
                     "dmesg h2d_sent -> h2d_complete sequential delta", "V*", _ms(st, nxt), ev)
    for i in range(1, len(h2d_frames)):
        delta = _ms(h2d_frames[i - 1][0], h2d_frames[i][0])
        if delta is not None and 0 < delta < 500:
            base.add("h2d.frame_spacing_ms", "spacing between consecutive H2D frames (bench policy frame_gap_ms)",
                     "ms", "dmesg frame timestamps", "V*", delta, ev,
                     "prev=%s cur=%s" % (h2d_frames[i - 1][1], h2d_frames[i][1]))

    # ---- scan ----------------------------------------------------------
    reqs = [(t, msg) for t, msg in events if "scan request posted" in msg]
    results = [(t, msg) for t, msg in events if "scan channel result" in msg]
    done = [(t, msg) for t, msg in events if "cfg80211: scan DONE" in msg]
    if reqs:
        treq = reqs[0][0]
        win = _num(r"timeout (\d+) ms", reqs[0][1])
        dwell = _num(r"dwell (\d+) ms", reqs[0][1])
        if win is not None:
            base.add("scan.host_window_ms", "host bounded scan window requested", "ms",
                     "dmesg scan request posted ... timeout N ms", "V*", win, ev)
        if dwell is not None:
            base.add("scan.dwell_ms", "requested per-channel dwell (g0 id0xC field)", "ms",
                     "dmesg dwell N ms", "V*", dwell, ev)
        res_before_done = [t for t, _ in results if not done or t <= done[0][0]]
        if res_before_done:
            base.add("scan.first_result_latency_ms", "scan request -> first g1 id17 channel result", "ms",
                     "dmesg kernel timestamps", "V*", _ms(treq, res_before_done[0]), ev)
            base.add("scan.channels_seen", "channels with a decoded result in the window", "channels",
                     "dmesg scan channel result count", "V*", len(res_before_done), ev)
            for i in range(1, len(res_before_done)):
                base.add("scan.channel_gap_ms", "gap between consecutive decoded channel results", "ms",
                         "dmesg kernel timestamps", "V*",
                         _ms(res_before_done[i - 1], res_before_done[i]), ev)
            if done:
                total = _ms(treq, done[0][0])
                base.add("scan.done_total_ms", "scan request -> cfg80211 scan DONE", "ms",
                         "dmesg kernel timestamps", "V*", total, ev, done[0][1][:160])
                if total and total > 0 and win:
                    base.add("scan.effective_ms_per_channel",
                             "host window / channels covered (what the auto-window formula must budget)",
                             "ms/ch", "host_window_ms / channels_seen", "I",
                             round(win / len(res_before_done), 2), ev)
        else:
            base.add("scan.channels_seen", "channels with a decoded result in the window", "channels",
                     "dmesg scan channel result count", "V*", 0, ev)
        if done:
            m = re.search(r"results=(\d+) bss=(\d+) complete=(\d+) channel=(\d+)", done[0][1])
            if m:
                base.add("scan.results", "decoded scan frames", "frames", "dmesg scan DONE counters", "V*",
                         int(m.group(1)), ev)
                base.add("scan.bss", "cfg80211 BSS entries informed", "BSS", "dmesg scan DONE counters", "V*",
                         int(m.group(2)), ev)

    # ---- heartbeat (per endpoint) --------------------------------------
    hb = {}
    for t, msg in events:
        m = BDF_RE.search(msg)
        if m and "hcc-core: heartbeat data=" in msg:
            hb.setdefault(m.group(1), []).append(t)
    hbtick = {}
    for t, msg in events:
        m = BDF_RE.search(msg)
        mt = re.search(r"heartbeat data=0x[0-9a-fA-F]+ \(tick (\d+)\)", msg)
        if m and mt:
            hbtick.setdefault(m.group(1), []).append(int(mt.group(1)))
    for bdf, ticks in hbtick.items():
        td = [ticks[i + 1] - ticks[i] for i in range(len(ticks) - 1)]
        for v in td:
            if 500 <= v <= 2000:
                base.add("heartbeat.tick_delta_ms",
                         "g4 id4 firmware-tick delta (device time base; console-independent)", "ms",
                         "dmesg heartbeat tick field deltas", "V*", v, ev, "bdf=%s" % bdf)
    for bdf, ts in hb.items():
        raw = [round((ts[i + 1] - ts[i]) * 1000.0, 2) for i in range(len(ts) - 1)]
        ivals = [v for v in raw if 500.0 <= v <= 2000.0]
        for v in ivals:
            base.add("heartbeat.interval_ms", "g4 id4 heartbeat interval (dmesg print timestamps)", "ms",
                     "dmesg heartbeat print spacing; kernel console timestamps", "V*", v, ev, "bdf=%s" % bdf)
        if raw:
            base.add("heartbeat.interval_outliers", "heartbeat intervals outside 500..2000 ms (console/logging gaps)",
                     "intervals", "dmesg heartbeat intervals", "V*", len(raw) - len(ivals), ev, "bdf=%s" % bdf)
        if len(ivals) >= 2:
            base.add("heartbeat.jitter_ms", "heartbeat interval spread (max-min) in one session", "ms",
                     "dmesg heartbeat intervals", "V*", round(max(ivals) - min(ivals), 2), ev,
                     "bdf=%s n=%d" % (bdf, len(ivals)))

    # ---- data plane ----------------------------------------------------
    dpline = next((msg for t, msg in events if "DP setup:" in msg), None)
    if dpline:
        rings = re.search(r"rings (\d+)/(\d+)/(\d+)", dpline)
        if rings:
            for i, rname in enumerate(("tx", "rx0", "rx1")):
                base.add("dp.rings_%s" % rname, "data-plane %s ring size (4-byte entries)" % rname, "entries",
                         "dmesg DP setup: line", "V*", int(rings.group(i + 1)), ev)
        txqm = re.search(r"TXQ table 0x([0-9a-fA-F]+)", dpline)
        if txqm:
            base.add("dp.txq_table_bytes", "TXQ table size (0x2524 = 9,508 B)", "B",
                     "dmesg DP setup: line", "V*", int(txqm.group(1), 16), ev)
        msdu = _num(r"MSDU table (\d+) entries", dpline)
        if msdu is not None:
            base.add("dp.msdu_table_entries", "device-RAM MSDU table entries", "entries",
                     "dmesg DP setup: line", "V*", int(msdu), ev)
        base.add("dp.setup_ok", "dp_setup completed and MAC verified", "bool",
                 "dmesg DP setup: OK", "V*", 1, ev)
    txqline = next((msg for t, msg in events if "DP txq_sync:" in msg), None)
    if txqline:
        base.add("dp.txq_addr_sync_ok", "h2d_txq_addr_sync posted (key 0x0320)", "bool",
                 "dmesg DP txq_sync: line", "V*", 1, ev)
    txline = next((msg for t, msg in events if "DP tx_frame:" in msg), None)
    if txline:
        m = re.search(r"wptr 0x([0-9a-fA-F]+)->0x([0-9a-fA-F]+)", txline)
        if m:
            base.add("dp.tx_published_frames", "frames published to the MAC TX MSDU ring", "frames",
                     "dmesg dp_tx_frame wptr delta", "V*",
                     int(m.group(2), 16) - int(m.group(1), 16), ev)
        cp = _num(r"completion_pending=(\d+)", txline)
        if cp is not None:
            base.add("dp.tx_completion_pending", "TX completion pending after publish (1 = POST-ONLY)",
                     "bool", "dmesg dp_tx_frame completion_pending", "V*", cp, ev)

    # ---- memory: rings / BAR windows -----------------------------------
    ringlines = all_matching(events, "ETE ring @ chan")
    if ringlines:
        depth = _num(r"depth (\d+)", ringlines[0][1])
        buflen = _num(r"buflen (\d+)", ringlines[0][1])
        per_bdf = {}
        for _t, _m in ringlines:
            mb = BDF_RE.search(_m)
            key = mb.group(1) if mb else "?"
            per_bdf[key] = per_bdf.get(key, 0) + 1
        rings = max(per_bdf.values()) if per_bdf else len(ringlines)
        if depth and buflen:
            base.add("mem.ring_buffers_bytes", "channel rings x depth x buffer per endpoint (host DRAM)",
                     "B", "dmesg ETE ring lines (per-endpoint)", "V*", rings * depth * buflen, ev,
                     "rings_per_endpoint=%d endpoints=%d depth=%d buflen=%d"
                     % (rings, len(per_bdf), int(depth), int(buflen)))
            base.add("mem.ring_depth", "HCC ring depth", "nodes", "dmesg ETE ring lines", "V*",
                     int(depth), ev)
            base.add("mem.ring_buflen", "HCC ring buffer length", "B", "dmesg ETE ring lines", "V*",
                     int(buflen), ev)
    for t, msg in events:
        m = re.search(r"BAR 0 \[mem 0x([0-9a-f]+)-0x([0-9a-f]+) [0-9a-f]+bit\]: assigned", msg)
        if m:
            base.add("mem.bar0_window_bytes", "endpoint BAR0 inbound window (16 MiB)", "B",
                     "dmesg PCI BAR assignment", "V*", int(m.group(2), 16) - int(m.group(1), 16) + 1, ev)

    # ---- kernel/userspace boundary ------------------------------------
    init_t = first_ts(events, "Run /sbin/init as init process")
    if init_t is not None:
        base.add("os.kernel_to_userspace_ms", "kernel boot: start -> /sbin/init", "ms",
                 "dmesg kernel timestamps (boot time base)", "V*", init_t * 1000.0, ev)
    if load is not None:
        base.add("os.module_load_at_uptime_s", "module load happened at this kernel uptime", "s",
                 "dmesg module load timestamp; host waited for the box before deploying", "V*",
                 load, ev)

    # ---- console printk cost ------------------------------------------
    console = []
    for t, msg in events:
        if DRV_LINE_RE.search(msg) or "not decodable" in msg:
            console.append((t, msg))
    dense = []
    for i in range(1, len(console)):
        dt = console[i][0] - console[i - 1][0]
        if 0.0 < dt < 0.050:
            dense.append(dt * 1000.0)
    if dense:
        base.add("console.printk_line_ms", "serial-console cost of one driver printk line (115200 baud)",
                 "ms", "median spacing of dense consecutive driver dmesg lines", "V*",
                 round(statistics.median(dense), 2), ev, "dense_pairs=%d" % len(dense))
        base.add("console.driver_lines_per_session", "driver console lines emitted in the captured dmesg",
                 "lines", "count of hi5622 driver lines in the session dmesg", "V*", len(console), ev)
        base.add("console.driver_console_time_s",
                 "estimated serial time spent printing driver lines in this session", "s",
                 "console.driver_lines x median line cost", "I",
                 round(len(console) * statistics.median(dense) / 1000.0, 3), ev)


# ---------------------------------------------------------------------------
# Collector: UART console log (multi-boot; AP-start, scan and console metrics)
# ---------------------------------------------------------------------------
def _split_boots(text: str) -> list:
    boots, cur = [], None
    for line in text.splitlines():
        if "Booting Linux on physical CPU" in line:
            cur = []
            boots.append(cur)
        if cur is not None:
            cur.append(line)
    return boots


def collect_uart(path: str, base: Baseline) -> None:
    root = repo_root()
    try:
        text = open(path, errors="replace").read()
    except OSError:
        return
    boots = _split_boots(text)
    if not boots:
        return
    base.note_evidence("uart_logs", path)
    name = os.path.basename(path)
    for bi, lines in enumerate(boots):
        events = []
        for line in lines:
            m = TS_RE.match(line)
            if m:
                events.append((float(m.group(1)), m.group(2)))
        if not events:
            continue
        if not any("hi5622_wifi" in line for line in lines):
            continue
        ev = "uart:%s:boot%d" % (name, bi)
        # AP-start sequence: start_ap reached -> frames -> sequence posted
        reach = None
        frames = []
        for t, msg in events:
            if "cfg80211: start_ap reached" in msg:
                reach = t
                frames = []
            elif reach is not None and "frame post '" in msg:
                m = re.search(r"frame post '([a-z_0-9]+)': .*?consumed after (\d+) polls", msg)
                if m:
                    frames.append((t, m.group(1), int(m.group(2))))
            elif reach is not None and "start_ap sequence posted" in msg:
                if frames:
                    base.add("ap.start_ap_to_sequence_posted_ms",
                             "hostapd NEW_BEACON reaches cfg80211 -> provider AP sequence posted", "ms",
                             "UART dmesg: start_ap reached -> start_ap sequence posted", "V*",
                             _ms(reach, t), ev)
                    for ft, fname, polls in frames:
                        base.add("ap.frame_latency_ms",
                                 "start_ap reached -> AP frame posted", "ms",
                                 "UART dmesg frame timestamps", "V*", _ms(reach, ft), ev,
                                 "frame=%s" % fname)
                        base.add("ap.frame_consumed_polls", "firmware announce polls for an AP-start frame",
                                 "polls", "UART dmesg frame post line", "V*", polls, ev,
                                 "frame=%s" % fname)
                    base.add("ap.frames_posted", "AP-start frames posted before the sequence completed",
                             "frames", "UART dmesg frame posts in the start_ap sequence", "V*",
                             len(frames), ev)
                    reach_msg = next((m for tt, m in events if "start_ap reached" in m), "")
                    bi_val = _num(r"beacon_interval=(\d+)", reach_msg)
                    if bi_val is not None:
                        base.add("ap.beacon_interval", "start_ap beacon interval", "TU",
                                 "UART dmesg start_ap reached line", "V*", bi_val, ev)
                    dtim = _num(r"dtim=(\d+)", reach_msg)
                    if dtim is not None:
                        base.add("ap.dtim", "start_ap DTIM period", "beacons",
                                 "UART dmesg start_ap reached line", "V*", dtim, ev)
                reach = None
                frames = []
            elif reach is not None and "announce NOT consumed" in msg:
                polls = _num(r"after (\d+) polls", msg)
                if polls is not None:
                    base.add("ap.failure_timeout_polls", "AP frame announce timeout (host poll bound)", "polls",
                             "UART dmesg failed frame post", "V*", polls, ev)
        # scan windows
        treq = win = None
        chans = []
        for t, msg in events:
            if "scan request posted" in msg:
                treq = t
                chans = []
                win = _num(r"timeout (\d+) ms", msg)
            elif "scan channel result" in msg and treq is not None:
                chans.append(t)
            elif "scan DONE" in msg and treq is not None:
                if chans:
                    base.add("scan.channels_seen", "channels with a decoded result in one window", "channels",
                             "UART dmesg result count", "V*", len(chans), ev, "window_ms=%s" % win)
                    base.add("scan.first_result_latency_ms", "scan request -> first channel result", "ms",
                             "UART dmesg timestamps", "V*", _ms(treq, chans[0]), ev)
                base.add("scan.window_actual_ms", "scan request -> scan DONE (window actually consumed)", "ms",
                         "UART dmesg timestamps", "V*", _ms(treq, t), ev, "window_ms=%s" % win)
                if win:
                    base.add("scan.host_window_ms", "host bounded scan window requested", "ms",
                             "UART dmesg scan request line", "V*", win, ev)
                    if chans:
                        base.add("scan.effective_ms_per_channel",
                                 "host window / channels covered (auto-window budgeting)", "ms/ch",
                                 "host_window_ms / channels_seen", "I", round(win / len(chans), 2), ev)
                treq = None
                chans = []
        # heartbeat per endpoint
        hb = {}
        for t, msg in events:
            m = BDF_RE.search(msg)
            if m and "hcc-core: heartbeat data=" in msg:
                hb.setdefault(m.group(1), []).append(t)
        hbtick = {}
        for t, msg in events:
            m = BDF_RE.search(msg)
            mt = re.search(r"heartbeat data=0x[0-9a-fA-F]+ \(tick (\d+)\)", msg)
            if m and mt:
                hbtick.setdefault(m.group(1), []).append(int(mt.group(1)))
        for bdf, ticks in hbtick.items():
            td = [ticks[i + 1] - ticks[i] for i in range(len(ticks) - 1)]
            for v in td:
                if 500 <= v <= 2000:
                    base.add("heartbeat.tick_delta_ms",
                             "g4 id4 firmware-tick delta (device time base; console-independent)", "ms",
                             "UART dmesg heartbeat tick field deltas", "V*", v, ev, "bdf=%s" % bdf)
        for bdf, ts in hb.items():
            raw = [round((ts[i + 1] - ts[i]) * 1000.0, 2) for i in range(len(ts) - 1)]
            ivals = [v for v in raw if 500.0 <= v <= 2000.0]
            for v in ivals:
                base.add("heartbeat.interval_ms", "g4 id4 heartbeat interval (UART console timestamps)", "ms",
                         "UART dmesg heartbeat spacing", "V*", v, ev, "bdf=%s" % bdf)
            if raw:
                base.add("heartbeat.interval_outliers",
                         "heartbeat intervals outside 500..2000 ms (console/logging gaps)", "intervals",
                         "UART dmesg heartbeat spacing", "V*", len(raw) - len(ivals), ev, "bdf=%s" % bdf)
        # console cost
        drv = [(t, msg) for t, msg in events if DRV_LINE_RE.search(msg)]
        dense = [drv[i][0] - drv[i - 1][0] for i in range(1, len(drv))
                 if 0.0 < drv[i][0] - drv[i - 1][0] < 0.050]
        if dense:
            base.add("console.printk_line_ms", "serial-console cost of one driver printk line (UART)", "ms",
                     "UART: median spacing of dense consecutive driver lines", "V*",
                     round(statistics.median(dense) * 1000.0, 2), ev, "dense_pairs=%d" % len(dense))
        # module load / userspace boundaries
        load = first_ts(events, "hi5622_wifi: loading out-of-tree module taints")
        if load is not None and load >= 5.0:
            base.add("os.module_load_at_uptime_s", "module load at this kernel uptime (boot segment)", "s",
                     "UART dmesg module load timestamp", "V*", load, ev)
        userspace = first_ts(events, "Run /sbin/init as init process")
        if userspace is not None and 3.0 <= userspace <= 20.0:
            base.add("os.kernel_to_userspace_ms", "kernel boot: start -> /sbin/init", "ms",
                     "UART dmesg timestamps", "V*", userspace * 1000.0, ev)
        lan = first_ts(events, "br-lan: port 1(eth2) entered forwarding state")
        if lan is not None:
            base.add("os.lan_forwarding_at_s", "userspace: br-lan forwarding up", "s",
                     "UART dmesg kernel timestamp", "V*", lan, ev)


# ---------------------------------------------------------------------------
# Evidence discovery + extraction driver
# ---------------------------------------------------------------------------
def discover_evidence(root: str) -> dict:
    return {
        "phase_matrices": sorted(glob.glob(os.path.join(
            root, "build/wifi-bench-phase-matrix/*/phase-matrix.json"))),
        "session_summaries": sorted(glob.glob(os.path.join(
            root, "build/wifi-bench-phase-matrix/*/*/summary.json")))
        + sorted(glob.glob(os.path.join(root, "build/wifi-bench/*/summary.json"))),
        "bench_sessions": sorted(glob.glob(os.path.join(root, "build/wifi-bench-session/*/session.json"))),
        "uart_logs": sorted(glob.glob(os.path.join(root, "build/uart-logs/*.log"))),
    }


def load_documented(path: str, base: Baseline) -> None:
    if not os.path.isfile(path):
        base.warn("documented input missing: %s" % rel(path, repo_root()))
        return
    try:
        d = json.load(open(path))
    except (OSError, ValueError):
        base.warn("unreadable documented input")
        return
    if d.get("schema") != DOCUMENTED_SCHEMA:
        base.warn("documented input schema mismatch")
    base.note_evidence("documented_input", path)
    for e in d.get("entries", []):
        mid = e.get("id")
        if not mid:
            continue
        base.metric(mid, e.get("title", mid), e.get("unit", ""),
                    e.get("method", "documented"), e.get("grade", "V"))
        val = e.get("value")
        if isinstance(val, (int, float)):
            base.add(mid, e.get("title", mid), e.get("unit", ""),
                     e.get("method", "documented"), e.get("grade", "V"), val,
                     "documented:%s" % e.get("source", "?"), e.get("detail", ""))
        base.documented.append(e)


def extract(root: str, use_uart: bool = True) -> Baseline:
    base = Baseline()
    ev = discover_evidence(root)
    for p in ev["phase_matrices"]:
        collect_phase_matrix(p, base)
    for p in ev["bench_sessions"]:
        collect_bench_session_json(p, base)
    for p in ev["session_summaries"]:
        collect_session_summary(p, base)
    if use_uart:
        for p in ev["uart_logs"]:
            collect_uart(p, base)
    load_documented(DOCUMENTED_INPUT, base)
    if not base.metrics:
        base.warn("no evidence found: expected gitignored build/ session and UART evidence "
                  "(run perf_session.py list-evidence); the live probe is the alternative")
    return base


# ---------------------------------------------------------------------------
# Live probe (gated, read-only, attended operator only)
# ---------------------------------------------------------------------------
def router_host(cli_host: str = None) -> str:
    h = (cli_host or os.environ.get("ROUTER_HOST")
         or os.environ.get("PERF_ROUTER_HOST") or "root@192.168.1.1")
    if h and "@" not in h:
        h = "root@" + h
    return h


def rsh_path(root: str) -> str:
    return os.path.join(root, "tools", "rsh.sh")


def live_guard(args) -> str:
    if not (getattr(args, "yes_really", False) and getattr(args, "attended", False)):
        return ("REFUSED: live mode is ATTENDED OPERATOR ONLY; pass --yes-really --attended.\n"
                "This lane never contacts the bench router. The probe is read-only (piped over "
                "ssh stdin; no device writes).")
    if os.environ.get("PERF_ALLOW_DEVICE") != "1":
        return ("REFUSED: set PERF_ALLOW_DEVICE=1 together with --yes-really --attended "
                "(two independent confirmations).")
    if not os.path.isfile(PROBE):
        return "REFUSED: missing device probe: %s" % PROBE
    return ""


def run_probe_live(args, probe_args: list) -> tuple:
    root = repo_root()
    rsh = rsh_path(root)
    if not os.path.isfile(rsh):
        return 1, "", "missing %s" % rsh
    with open(PROBE, "rb") as f:
        script = f.read()
    env = dict(os.environ)
    env["ROUTER_HOST"] = router_host(args.host)
    cmd = [rsh, "sh", "-s", "--"] + probe_args
    try:
        p = subprocess.run(cmd, input=script, capture_output=True, timeout=args.timeout, env=env)
    except subprocess.TimeoutExpired:
        return 1, "", "probe timed out after %ss" % args.timeout
    return p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace")


def parse_probe_output(text: str) -> list:
    snaps = []
    cur = None
    attr = None
    for line in text.splitlines():
        if line.startswith("SNAP-START"):
            parts = line.split()
            cur = {"n": int(parts[1]) if len(parts) > 1 else 0,
                   "uptime": float(parts[2]) if len(parts) > 2 else 0.0,
                   "attrs": {}, "dmesg": [], "cpu": {}, "cores": {}, "params": {},
                   "threads": {}, "mem": {}, "stat": {}}
            if snaps:
                snaps[-1]["next_uptime"] = cur["uptime"]
            snaps.append(cur)
        elif line.startswith("SNAP-END"):
            cur = None
        elif cur is None:
            continue
        elif line.startswith("CPU-AGG"):
            vals = [int(x) for x in line.split()[1:9]]
            cur["cpu"] = dict(zip(("user", "nice", "system", "idle", "iowait",
                                   "irq", "softirq", "steal"), vals))
        elif line.startswith("CPU-CORE"):
            parts = line.split()
            cur["cores"][parts[1]] = [int(x) for x in parts[2:10]]
        elif line.startswith("STAT "):
            parts = line.split()
            if len(parts) >= 3:
                try:
                    cur["stat"][parts[1]] = int(parts[2])
                except ValueError:
                    pass
        elif line.startswith("MEM "):
            k, _, v = line[4:].partition("=")
            cur["mem"][k] = v
        elif line.startswith("PARAM "):
            k, _, v = line[6:].partition("=")
            cur["params"][k] = v
        elif line.startswith("THREAD "):
            parts = line.split()
            if len(parts) >= 5:
                cur["threads"][parts[1]] = {"comm": parts[2],
                                            "utime": int(parts[3]), "stime": int(parts[4])}
        elif line.startswith("ATTR-BEGIN"):
            parts = line.split()
            if len(parts) >= 3:
                attr = [parts[1] + ":" + parts[2], []]
        elif line.startswith("ATTR-END") and attr:
            cur["attrs"][attr[0]] = "\n".join(attr[1])
            attr = None
        elif line.startswith("DMESG-BEGIN"):
            attr = ["DMESG", []]
        elif line.startswith("DMESG-END") and attr:
            cur["dmesg"] = attr[1]
            attr = None
        elif attr is not None:
            attr[1].append(line)
    return snaps


def probe_summary(snaps: list, hz: int = 100) -> dict:
    out = {"snapshots": len(snaps), "intervals": []}
    for i in range(1, len(snaps)):
        a, b = snaps[i - 1], snaps[i]
        dur = b["uptime"] - a["uptime"]
        if dur <= 0:
            continue
        delta = {k: b["cpu"].get(k, 0) - a["cpu"].get(k, 0) for k in a["cpu"]}
        total = sum(delta.values())
        idle = delta.get("idle", 0) + delta.get("iowait", 0)
        busy = total - idle
        row = {"uptime_s": round(dur, 3),
               "cpu_busy_pct": round(100.0 * busy / total, 2) if total else None,
               "ctxt_delta": b["stat"].get("ctxt", 0) - a["stat"].get("ctxt", 0) if "ctxt" in a["stat"] else None,
               "cpu_jiffies": total}
        th = {}
        for pid, st in b["threads"].items():
            old = a["threads"].get(pid)
            if not old:
                continue
            th[st["comm"]] = round(100.0 * ((st["utime"] - old["utime"]) + (st["stime"] - old["stime"]))
                                   / (dur * hz), 2)
        row["thread_cpu_pct"] = th
        polls = {}
        for key, body in b["attrs"].items():
            m = re.search(r"D2H DIRECT status: .*?polls=(\d+)", body)
            if not m:
                continue
            pm = re.search(r"polls=(\d+)", a["attrs"].get(key, ""))
            if pm:
                polls[key] = round((int(m.group(1)) - int(pm.group(1))) / dur, 2)
        row["d2h_polls_per_s"] = polls
        out["intervals"].append(row)
    return out


def cmd_probe(args) -> int:
    guard = live_guard(args)
    if guard:
        print(guard, file=sys.stderr)
        return 2
    probe_args = ["--snapshot", "--dmesg-lines", str(args.dmesg_lines)]
    if args.dry_run:
        print("DRY-RUN: would run: %s sh -s -- %s  (stdin: %s, read-only)"
              % (rsh_path(repo_root()), " ".join(probe_args), os.path.basename(PROBE)))
        print("DRY-RUN: host=%s; a real run needs --yes-really --attended and PERF_ALLOW_DEVICE=1"
              % router_host(args.host))
        return 0
    rc, out, err = run_probe_live(args, probe_args)
    if rc != 0:
        print("probe failed rc=%d: %s" % (rc, err.strip()[:400]), file=sys.stderr)
        return rc
    snaps = parse_probe_output(out)
    evidence_dir = args.out or os.path.join(repo_root(), "build", "wifi-perf-baseline",
                                            time.strftime("%Y%m%dT%H%M%SZ", time.gmtime()))
    os.makedirs(evidence_dir, exist_ok=True)
    with open(os.path.join(evidence_dir, "probe-raw.txt"), "w") as f:
        f.write(out)
    print("probe OK: %d snapshot(s); evidence: %s" % (len(snaps), evidence_dir))
    if snaps:
        print("  params: %s" % json.dumps(snaps[0]["params"], sort_keys=True))
        print("  threads: %s" % json.dumps(snaps[0]["threads"], sort_keys=True))
    return 0


def run_watch(args) -> int:
    guard = live_guard(args)
    if guard:
        print(guard, file=sys.stderr)
        return 2
    probe_args = ["--watch", str(args.duration), "--interval", str(args.interval),
                  "--dmesg-lines", str(args.dmesg_lines)]
    if args.dry_run:
        print("DRY-RUN: would run: %s sh -s -- %s  (stdin: %s, read-only)"
              % (rsh_path(repo_root()), " ".join(probe_args), os.path.basename(PROBE)))
        print("DRY-RUN: host=%s; a real run needs --yes-really --attended and PERF_ALLOW_DEVICE=1"
              % router_host(args.host))
        return 0
    rc, out, err = run_probe_live(args, probe_args)
    if rc != 0:
        print("probe failed rc=%d: %s" % (rc, err.strip()[:400]), file=sys.stderr)
        return rc
    snaps = parse_probe_output(out)
    summ = probe_summary(snaps)
    evidence_dir = args.out or os.path.join(repo_root(), "build", "wifi-perf-baseline",
                                            time.strftime("%Y%m%dT%H%M%SZ", time.gmtime()))
    os.makedirs(evidence_dir, exist_ok=True)
    with open(os.path.join(evidence_dir, "probe-raw.txt"), "w") as f:
        f.write(out)
    with open(os.path.join(evidence_dir, "probe-metrics.json"), "w") as f:
        json.dump({"schema": "hi5622-perf-probe/1", "tool_version": TOOL_VERSION,
                   "host": router_host(args.host),
                   "generated_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                   "snapshots": summ["snapshots"], "intervals": summ["intervals"],
                   "params": snaps[0]["params"] if snaps else {},
                   "module": next((line for line in out.splitlines() if line.startswith("MOD ")),
                                  "MOD-ABSENT"),
                   "threads": snaps[0]["threads"] if snaps else {}}, f, indent=1)
    print("probe OK: %d snapshot(s); evidence: %s" % (len(snaps), evidence_dir))
    for row in summ["intervals"]:
        print("  over %ss: cpu_busy=%s%% ctxt_delta=%s kthread_cpu=%s d2h_polls/s=%s"
              % (row["uptime_s"], row["cpu_busy_pct"], row["ctxt_delta"],
                 row["thread_cpu_pct"], row["d2h_polls_per_s"]))
    return 0


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------
def cmd_extract(args) -> int:
    root = repo_root()
    base = extract(root, use_uart=not args.no_uart)
    if args.dry_run:
        ev = discover_evidence(root)
        print("DRY-RUN: root=%s" % root)
        for k, v in ev.items():
            print("  %-18s %d file(s)" % (k, len(v)))
        print("DRY-RUN: %d metrics, %d documented entries, %d warning(s); no files written"
              % (len(base.metrics), len(base.documented), len(base.warnings)))
        return 0
    outdir = args.out or DEFAULT_OUT
    written = base.write_all(outdir)
    for wpath in written:
        print("wrote %s" % rel(wpath, root))
    print("%d metrics, %d documented entries, %d warning(s)"
          % (len(base.metrics), len(base.documented), len(base.warnings)))
    return 0


def cmd_list_evidence(args) -> int:
    ev = discover_evidence(repo_root())
    total = 0
    for k, v in ev.items():
        print("%s (%d):" % (k, len(v)))
        for p in v:
            print("  %s" % rel(p, repo_root()))
            total += 1
    print("total evidence files: %d" % total)
    return 0


def cmd_dry_run(args) -> int:
    root = repo_root()
    rc = 0
    print("perf_session dry-run (host-only; no device contact)")
    print("  root: %s" % root)
    p = subprocess.run(["sh", "-n", PROBE], capture_output=True)
    print("  probe sh -n: %s" % ("PASS" if p.returncode == 0 else "FAIL: " + p.stderr.decode()[:200]))
    rc |= 0 if p.returncode == 0 else 1
    if os.path.isfile(DOCUMENTED_INPUT):
        try:
            d = json.load(open(DOCUMENTED_INPUT))
            n = len(d.get("entries", []))
            ok = d.get("schema") == DOCUMENTED_SCHEMA
            print("  documented input: %s (%d entries)" % ("PASS" if ok else "SCHEMA-WARN", n))
        except (OSError, ValueError) as exc:
            print("  documented input: FAIL (%s)" % exc)
            rc = 1
    else:
        print("  documented input: MISSING")
        rc = 1
    ev = discover_evidence(root)
    n = sum(len(v) for v in ev.values())
    print("  recorded evidence: %d file(s)" % n)
    base = extract(root, use_uart=not args.no_uart)
    print("  extract smoke: %d metric(s), %d documented, %d warning(s)"
          % (len(base.metrics), len(base.documented), len(base.warnings)))
    for wmsg in base.warnings:
        print("    warning: %s" % wmsg)
    print("  live transport (NOT contacted): %s, probe=%s" % (rsh_path(root), PROBE))
    return rc


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(
        description="Host-side metrics harness for the clean-room Hi5622 Wi-Fi stack "
                    "(lane wifi-perf-baseline, task-132). Host-only by default; live mode is "
                    "attended and read-only.")
    sub = ap.add_subparsers(dest="command")

    e = sub.add_parser("extract", help="extract baseline from recorded evidence")
    e.add_argument("--out", default=None, help="output directory (default lane baseline/)")
    e.add_argument("--no-uart", action="store_true", help="skip build/uart-logs/*.log evidence")
    e.add_argument("--dry-run", action="store_true", help="report only; write nothing")
    e.set_defaults(func=cmd_extract)

    le = sub.add_parser("list-evidence", help="list recorded evidence files")
    le.set_defaults(func=cmd_list_evidence)

    dr = sub.add_parser("dry-run", help="validate harness + evidence; no writes, no device")
    dr.add_argument("--no-uart", action="store_true")
    dr.set_defaults(func=cmd_dry_run)

    for name, fn, hlp in (("probe", cmd_probe, "one read-only snapshot (live, gated)"),
                          ("watch", run_watch, "bounded read-only sampling (live, gated)")):
        p = sub.add_parser(name, help=hlp)
        p.add_argument("--host", default=None, help="ssh target (default root@192.168.1.1)")
        p.add_argument("--yes-really", action="store_true")
        p.add_argument("--attended", action="store_true")
        p.add_argument("--dry-run", action="store_true")
        p.add_argument("--timeout", type=float, default=180.0)
        if name == "watch":
            p.add_argument("--duration", type=int, default=30)
            p.add_argument("--interval", type=int, default=5)
        p.add_argument("--dmesg-lines", type=int, default=200)
        p.add_argument("--out", default=None)
        p.set_defaults(func=fn)
    return ap


def main(argv=None) -> int:
    ap = build_parser()
    args = ap.parse_args(argv)
    if not getattr(args, "command", None):
        ap.print_help()
        return 2
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
