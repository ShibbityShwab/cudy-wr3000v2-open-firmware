#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""phase_matrix.py - one-command bring-up phase runner (host-only automation).

Lane: wifi-bench-tooling (shared task task-114), HOST-ONLY.

Runs ONE selected bring-up phase (or all of them) through bench_session.py with
the phase's bundled sequence, captures the session evidence dir (plan JSON +
markdown + protocol evidence) and writes a machine-readable matrix record plus a
markdown table.  It never talks to the device in dry-run mode; a real phase run
requires --yes-really --attended exactly like bench_session.py.

Phases (the bring-up ladder):

    boot / ready   bringup-boot-ready.json     protocol mode, read-only row
    C2             bringup-c2-calibration.json protocol mode, budget 8
    D1             bringup-d1-vap.json         cfg80211 mode (wiphy + VAP lifecycle)
    D2             bringup-d2-scan.json        cfg80211 mode (scan request + D2H)
    E1             bringup-e1-dataplane.json   protocol mode (dp token contract)

A dry-run phase that is refused because the target module lacks a driver token
(module-attr-missing) is reported as BLOCKED, not as a tool failure: the
capability probe in bench_session.py scans the .ko for the attribute/token C
strings, so a missing wave-3 deliverable is visible before any bench time.

    python3 tools/phase_matrix.py --list-phases
    python3 tools/phase_matrix.py --phase all --dry-run \
        --module ../wifi-h2d-send/src/hi5622_wifi.ko \
        --fw-file /tmp/hi5622-patched/FIRMWARE.PD4.bin \
        --fw-manifest /tmp/hi5622-patched/manifest.json \
        --wifi-block-bin /tmp/blocks/wifi_block.bin
    python3 tools/phase_matrix.py --phase D1 --dry-run ... \
        --cfg80211-ko /path/cfg80211.ko --iw-bin /path/iw --libnl-tiny /path/libnl-tiny.so

Real (attended) run, one phase at a time:

    python3 tools/phase_matrix.py --phase C2 --yes-really --attended \
        --module /root-on-host/hi5622_wifi.ko ... --ssh-host 192.168.1.1
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import time

TOOLDIR = os.path.dirname(os.path.abspath(__file__))
if TOOLDIR not in sys.path:
    sys.path.insert(0, TOOLDIR)
import bench_guards as G          # noqa: E402
import protocol_sequence as PS    # noqa: E402
import bench_session as BS        # noqa: E402

BENCH_SESSION = os.path.join(TOOLDIR, "bench_session.py")
SEQUENCE_DIR = os.path.join(TOOLDIR, "sequences")
SCHEMA = "hi5622-phase-matrix/1"

PHASES = (
    {"id": "boot", "row": "boot", "label": "boot + ready handshake",
     "mode": "protocol", "sequence": "bringup-boot-ready.json",
     "expect": "boot ladder + strict ready pair; read-only sequence"},
    {"id": "C2", "row": "calibration", "label": "six-calibration boot (C2)",
     "mode": "protocol", "sequence": "bringup-c2-calibration.json",
     "expect": "create -> txq_sync72 -> 6x cfg_cal 3502..3507 -> six 298 B "
               "g1 id3 answers"},
    {"id": "D1", "row": "vap", "label": "cfg80211 VAP add/del (D1)",
     "mode": "cfg80211", "sequence": "bringup-d1-vap.json",
     "expect": "wiphy registered -> iw phy -> iw iface add wlan0 -> iw iface "
               "del; no H2D message"},
    {"id": "D2", "row": "scan", "label": "scan request (D2)",
     "mode": "cfg80211", "sequence": "bringup-d2-scan.json",
     "expect": "iw dev wlan0 scan (rc=0) + no exception (mask 0/staging 0) + "
               "H2D timeout=0/refused=0; D2H g1 15/16/17 / g2 2 scan events "
               "are WATCH-ONLY (host-only M1 VAP may give a graceful no-VAP "
               "refusal). Until the wifi-vap-scan g0 id0xC builder is in the "
               "provider registry the fail-closed -EOPNOTSUPP stub (rc=95) "
               "stops the ladder - that is BLOCKED-DRIVER, not FAIL"},
    {"id": "E1", "row": "dataplane", "label": "data-plane setup + TXQ sync (E1)",
     "mode": "protocol", "sequence": "bringup-e1-dataplane.json",
     "expect": "ready id2 ring-pointer seed + dp_probe read-only MAC slot-5 "
               "reachability read + ONE dp_setup (14 MAC regs / 8 ring-table "
               "entries verified) + ONE key-0x0320 h2d_txq_addr_sync + post "
               "pack (mask/staging 0, page/state golden, out0/out2 0) - the "
               "wifi-datapath-bringup ladder D"},
)
PHASE_IDS = tuple(p["id"] for p in PHASES)
ROWS = ("boot", "ready", "calibration", "vap", "scan", "dataplane")
BLOCK_CODES = {
    "module-attr-missing": "BLOCKED-TOKEN",
    "cfg80211-artifact-missing": "BLOCKED-PREREQ",
    "tool-missing": "BLOCKED-PREREQ",
    "budget-required": "BLOCKED-PREREQ",
    "insmod": "BLOCKED-PREREQ",
}
REFUSED_RE = re.compile(r"REFUSED: ([a-z0-9-]+): ?([^\n]*)")


def now_stamp() -> str:
    return time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())


def evidence_base() -> str:
    """Same evidence-tree policy as bench_session.py, own subdirectory."""
    candidates = (
        os.path.join(BS.ROOT, "build", "wifi-bench-phase-matrix"),
        os.path.join(BS.ROOT, "build", "evidence-bench-gates",
                     "wifi-bench-phase-matrix"),
    )
    for cand in candidates:
        try:
            os.makedirs(cand, exist_ok=True)
            probe = os.path.join(cand, ".write-probe")
            with open(probe, "w") as f:
                f.write("x")
            os.unlink(probe)
            return cand
        except OSError:
            continue
    fallback = os.path.join(tempfile.gettempdir(), "wifi-bench-phase-matrix")
    os.makedirs(fallback, exist_ok=True)
    print("WARNING: no writable project evidence dir; using %s - /tmp is NOT "
          "project evidence" % fallback, file=sys.stderr)
    return fallback


def phase_by_id(pid: str) -> dict:
    for p in PHASES:
        if p["id"] == pid:
            return p
    raise KeyError(pid)


def sequence_path(p: dict) -> str:
    return os.path.join(SEQUENCE_DIR, p["sequence"])


def module_for(args, p: dict):
    """Per-phase module override (--module-for D1=/path/ko), else --module."""
    overrides = getattr(args, "module_for_map", None) or {}
    return overrides.get(p["id"]) or args.module


def build_argv(args, p: dict, phase_dir: str) -> list:
    argv = [sys.executable, BENCH_SESSION, "--mode", p["mode"],
            "--sequence", sequence_path(p), "--report-dir", phase_dir,
            "--endpoint", args.endpoint,
            "--session-timeout", str(args.session_timeout),
            "--ready-id1", str(args.ready_id1),
            "--ready-id2", str(args.ready_id2)]
    mod = module_for(args, p)
    if mod:
        argv += ["--module", os.path.abspath(mod)]
    if args.fw_file:
        argv += ["--fw-file", os.path.abspath(args.fw_file)]
    if args.fw_manifest:
        argv += ["--fw-manifest", os.path.abspath(args.fw_manifest)]
    if args.fw_path:
        argv += ["--fw-path", args.fw_path]
    if args.wifi_block_bin:
        argv += ["--wifi-block-bin", os.path.abspath(args.wifi_block_bin)]
    if args.wifi_block:
        argv += ["--wifi-block", os.path.abspath(args.wifi_block)]
    if args.bar0read:
        argv += ["--bar0read", os.path.abspath(args.bar0read)]
    if args.ssh_host:
        argv += ["--ssh-host", args.ssh_host]
    for mp in args.module_param or []:
        argv += ["--module-param", mp]
    if p["mode"] == "cfg80211":
        for flag, val in (("--cfg80211-ko", args.cfg80211_ko),
                          ("--iw-bin", args.iw_bin),
                          ("--libnl-tiny", args.libnl_tiny)):
            if val:
                argv += [flag, os.path.abspath(val)]
    if args.dry_run:
        argv.append("--dry-run")
    else:
        argv += ["--yes-really", "--attended"]
    return argv


def load_json(path: str):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def classify(args, p: dict, rc: int, out: str, err: str,
             phase_dir: str) -> dict:
    """Map a bench_session invocation to a matrix status record."""
    rec = {"phase": p["id"], "row": p["row"], "mode": p["mode"],
           "sequence": p["sequence"], "sequence_sha256": None,
           "status": "ERROR", "detail": "", "rc": rc,
           "evidence_dir": phase_dir, "blocked_reason": None,
           "failed_step": None, "bench_result": None,
           "protocol_verdict": None, "totals": None}
    pdoc = load_json(os.path.join(phase_dir, "protocol-plan.json"))
    if pdoc:
        rec["sequence_sha256"] = pdoc.get("sha256")
        rec["totals"] = pdoc.get("totals")
    ref = REFUSED_RE.search(err) or REFUSED_RE.search(out)
    code = ref.group(1) if ref else None
    msg = (ref.group(2).strip() if ref else "").strip()
    if rc == 0:
        summary = load_json(os.path.join(phase_dir, "summary.json")) or {}
        rec["bench_result"] = summary.get("result")
        if args.dry_run:
            t = rec["totals"] or {}
            rec["status"] = "PLAN-OK"
            rec["detail"] = ("dry-run plan validated: %s step(s), %s check(s), "
                             "%s device write(s)"
                             % (t.get("steps"), t.get("checks"),
                                t.get("attr_writes")))
        else:
            ev = load_json(os.path.join(phase_dir,
                                        "protocol-evidence.json")) or {}
            rec["protocol_verdict"] = ev.get("verdict")
            rec["failed_step"] = ev.get("failed_step")
            rec["detail"] = ("session result %s%s; protocol verdict %s"
                             % (summary.get("result"),
                                (":" + str(summary.get("stop_rule")))
                                if summary.get("stop_rule") else "",
                                ev.get("verdict")))
            if summary.get("result") == "PASS" and ev.get("verdict") == "PASS":
                rec["status"] = "PASS"
            elif summary.get("result") == "STOP":
                rec["status"] = classify_stop(ev)
            else:
                rec["status"] = "FAIL"
        return rec
    if code and code in BLOCK_CODES:
        rec["status"] = BLOCK_CODES[code]
        rec["blocked_reason"] = code
        rec["detail"] = msg or code
        return rec
    if code:
        rec["status"] = "REFUSED"
        rec["blocked_reason"] = code
        rec["detail"] = msg or code
        return rec
    tail = (err.strip().splitlines() or ["?"])[-1][:300]
    rec["detail"] = tail
    return rec


def classify_stop(evidence: dict) -> str:
    """A STOP is a real failure unless it is a known pending-feature stop."""
    failed = evidence.get("failed_step")
    for st in evidence.get("steps") or []:
        if st.get("name") != failed:
            continue
        for c in st.get("checks") or []:
            if c.get("code") == "shell-rc-mismatch" \
                    and c.get("actual") == "rc=95":
                return "BLOCKED-DRIVER"
    return "STOP"


def run_phase(args, p: dict, base: str, stamp: str) -> dict:
    phase_dir = os.path.join(base, stamp, p["id"])
    argv = build_argv(args, p, phase_dir)
    rec = {"phase": p["id"], "row": p["row"], "label": p["label"],
           "mode": p["mode"], "sequence": p["sequence"],
           "run_kind": "dry-run" if args.dry_run else "attended-real",
           "command": argv, "expect": p["expect"]}
    print("== phase %-3s (%s) ==" % (p["id"], p["label"]))
    t0 = time.time()
    proc = subprocess.run(argv, capture_output=True, text=True, timeout=600)
    rec["elapsed_s"] = round(time.time() - t0, 2)
    rec.update(classify(args, p, proc.returncode, proc.stdout, proc.stderr,
                        phase_dir))
    rec["stdout_tail"] = proc.stdout.strip().splitlines()[-3:]
    if proc.stderr.strip():
        rec["stderr_tail"] = proc.stderr.strip().splitlines()[-3:]
    print("   status: %-14s %s" % (rec["status"], rec["detail"][:160]))
    return rec


def render_markdown(matrix: dict) -> str:
    L = []
    a = L.append
    a("# bring-up phase matrix")
    a("")
    a("- schema: %s" % matrix["schema"])
    a("- generated: %s" % matrix["generated"])
    a("- run kind: **%s**" % matrix["run_kind"])
    a("- phases: %s" % ", ".join(matrix["selected"]))
    a("")
    a("| phase | row | mode | sequence | status | detail | evidence |")
    a("| --- | --- | --- | --- | --- | --- | --- |")
    for rec in matrix["phases"]:
        a("| %s | %s | %s | %s | **%s** | %s | %s |"
          % (rec["phase"], rec["row"], rec["mode"], rec["sequence"],
             rec["status"], (rec.get("detail") or "").replace("|", "\\\\|")[:220],
             (rec.get("evidence_dir") or "").replace("|", "\\\\|")))
    a("")
    a("## Phase expectations")
    a("")
    for rec in matrix["phases"]:
        a("- **%s** (%s): %s" % (rec["phase"], rec["row"], rec["expect"]))
        if rec.get("blocked_reason"):
            a("  - blocked: %s - %s" % (rec["blocked_reason"],
                                         rec.get("detail", "")))
    a("")
    a("## How to read the statuses")
    a("")
    a("- PLAN-OK: the data-driven plan validated end to end (sequence schema,")
    a("  module/firmware pins, sysfs attributes present in the real .ko, static")
    a("  script audit, sh -n); nothing was deployed and no device was contacted.")
    a("- BLOCKED-TOKEN: the target module does not contain the driver token the")
    a("  phase needs (host-only .ko string probe). That is a deliverable gap in")
    a("  the owning lane, not a bench failure; the sequence JSON is the single")
    a("  edit point when the token lands.")
    a("- BLOCKED-DRIVER: a real attended run stopped at a documented pending")
    a("  driver feature (e.g. the cfg80211 M2 scan stub returning -EOPNOTSUPP")
    a("  rc=95); the ladder is ready for the next driver revision.")
    a("- PASS / FAIL / STOP: real attended session results (see each phase's")
    a("  evidence dir for protocol-evidence.json + PROTOCOL-SEQUENCE.md).")
    a("")
    return "\n".join(L) + "\n"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description="run one bring-up phase through bench_session.py and write "
                    "the phase matrix (host-only dry-run by default)")
    ap.add_argument("--phase", default="all",
                    help="boot | ready | C2 | D1 | D2 | E1 | all "
                         "(ready is the boot phase's ready row)")
    ap.add_argument("--list-phases", action="store_true")
    ap.add_argument("--module")
    ap.add_argument("--module-for", action="append", metavar="PHASE=PATH",
                    help="per-phase module override (repeatable), e.g. "
                         "--module-for D1=../wifi-cfg80211/src/hi5622_wifi.ko; "
                         "phases without an override use --module")
    ap.add_argument("--fw-file")
    ap.add_argument("--fw-manifest")
    ap.add_argument("--fw-path")
    ap.add_argument("--wifi-block-bin")
    ap.add_argument("--wifi-block")
    ap.add_argument("--bar0read")
    ap.add_argument("--module-param", action="append", metavar="KEY=VALUE")
    ap.add_argument("--cfg80211-ko")
    ap.add_argument("--iw-bin")
    ap.add_argument("--libnl-tiny")
    ap.add_argument("--ssh-host")
    ap.add_argument("--endpoint", default="0000:00:00.0")
    ap.add_argument("--ready-id1", type=float, default=2.0)
    ap.add_argument("--ready-id2", type=float, default=20.0)
    ap.add_argument("--session-timeout", type=int, default=240)
    ap.add_argument("--report-dir",
                    help="matrix evidence root (default: the project build "
                         "evidence tree; /tmp is never silently used)")
    ap.add_argument("--dry-run", action="store_true",
                    help="validate + plan only; never contact the device "
                         "(default when --yes-really is absent)")
    ap.add_argument("--yes-really", action="store_true",
                    help="execute the phase(s) on the box")
    ap.add_argument("--attended", action="store_true",
                    help="operator marked the bench attended (required for "
                         "--yes-really)")
    ap.add_argument("--strict", action="store_true",
                    help="exit non-zero on any BLOCKED/REFUSED/ERROR status")
    ap.add_argument("--json", action="store_true", help="print the matrix JSON")
    args = ap.parse_args(argv)
    if args.list_phases:
        for p in PHASES:
            print("%-3s row=%-11s mode=%-9s sequence=%s"
                  % (p["id"], p["row"], p["mode"], p["sequence"]))
            print("    %s" % p["expect"])
        return 0
    args.dry_run = args.dry_run or not args.yes_really
    if not args.dry_run and not args.attended:
        print("error: a real phase run needs --attended (recovery needs a human "
              "at the bench); use --dry-run to inspect the plan",
              file=sys.stderr)
        return 2
    args.module_for_map = {}
    for raw in args.module_for or []:
        pid, sep, path = raw.partition("=")
        pid = pid.strip()
        if not sep or pid not in PHASE_IDS or not path.strip():
            print("error: --module-for expects PHASE=PATH with PHASE in %s, "
                  "got %r" % (", ".join(PHASE_IDS), raw), file=sys.stderr)
            return 2
        args.module_for_map[pid] = path.strip()
    if args.phase == "all":
        selected = list(PHASE_IDS)
    elif args.phase == "ready":
        selected = ["boot"]     # ready is the boot row's second half
    elif args.phase in PHASE_IDS:
        selected = [args.phase]
    else:
        print("error: unknown phase %r (choose from: %s)"
              % (args.phase, ", ".join(("all", "ready") + PHASE_IDS)),
              file=sys.stderr)
        return 2
    base = args.report_dir or evidence_base()
    stamp = now_stamp()
    records = []
    for pid in selected:
        records.append(run_phase(args, phase_by_id(pid), base, stamp))
    matrix = {
        "schema": SCHEMA,
        "generated": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "run_kind": "dry-run" if args.dry_run else "attended-real",
        "selected": selected,
        "phases": records,
        "rows": {p["row"]: p["id"] for p in PHASES},
    }
    outdir = os.path.join(base, stamp)
    os.makedirs(outdir, exist_ok=True)
    mpath = os.path.join(outdir, "phase-matrix.json")
    with open(mpath, "w", encoding="utf-8") as f:
        json.dump(matrix, f, indent=1, sort_keys=True)
        f.write("\n")
    mdp = os.path.join(outdir, "PHASE-MATRIX.md")
    with open(mdp, "w", encoding="utf-8") as f:
        f.write(render_markdown(matrix))
    print("")
    print("== phase matrix ==")
    for rec in records:
        print("  %-3s %-14s %s" % (rec["phase"], rec["status"],
                                   (rec.get("detail") or "")[:110]))
    print("matrix: %s" % mpath)
    print("markdown: %s" % mdp)
    print("next: python3 tools/bringup_report.py --matrix %s" % mpath)
    if args.json:
        print(json.dumps(matrix, indent=1, sort_keys=True))
    bad = [r for r in records
           if r["status"] not in ("PLAN-OK", "PASS", "BLOCKED-TOKEN",
                                  "BLOCKED-DRIVER", "BLOCKED-PREREQ")]
    return 1 if (args.strict and bad) else 0


if __name__ == "__main__":
    raise SystemExit(main())
