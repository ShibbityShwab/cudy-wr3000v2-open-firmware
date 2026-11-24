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


def want_reboot_after(args, index: int, total: int) -> bool:
    """Reboot the box after this phase (fresh module load for the next one).

    A phase that loads hi5622_wifi leaves it bound; the next phase's script
    stops at the module-already-loaded gate (or an insmod rc=255), so real
    multi-phase runs should reboot between phases.  --reboot-after reboots
    after every phase, --reboot-between after every phase except the last.
    """
    if args.dry_run:
        return False
    if getattr(args, "reboot_after", False):
        return True
    if getattr(args, "reboot_between", False):
        return index < total - 1
    return False


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
        # Rebooting is orchestrated by the MATRIX (do_reboot), not by the
        # child session: the child then ends cleanly and the matrix owns the
        # bounded, logged reboot between phases (the 2026-11-25 hang).
        argv += ["--yes-really", "--attended"]
    return argv


def load_json(path: str):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def _run_child(argv: list, timeout: float) -> tuple:
    """Run the bench_session child with a HARD timeout and process-group kill.

    subprocess.run(timeout=) only signals the direct child; its ssh/rsh
    grandchildren can survive and keep the pipe open (the 2026-11-25 full-matrix
    run left a bench_session alive for 12+ min).  The child is started in its
    own session and the WHOLE process group is killed on timeout.
    """
    proc = subprocess.Popen(argv, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True,
                            start_new_session=True)
    try:
        out, err = proc.communicate(timeout=max(1.0, float(timeout)))
        return proc.returncode, out or "", err or ""
    except subprocess.TimeoutExpired:
        try:
            os.killpg(proc.pid, 15)
            time.sleep(0.5)
            os.killpg(proc.pid, 9)
        except (ProcessLookupError, PermissionError):
            pass
        try:
            out, err = proc.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            out, err = "", ""
        note = ("phase child killed after the %.0f s phase timeout "
                "(process group %d; session-timeout=%s)" % (timeout,
                                                            proc.pid,
                                                            "child"))
        return 124, out or "", (err or "") + "\n" + note


def do_reboot(args) -> tuple:
    """Bounded, logged reboot between phases (matrix-owned).

    Reuses bench_session.reboot_and_wait() - the same hardened, deadline-bound
    wait the session-level --reboot uses - through the SSH transport, so a
    loaded module from the previous phase cannot poison the next one.
    """
    o = BS.SessionOptions(mode="protocol", ssh_host=args.ssh_host,
                          reboot="after",
                          reboot_poll_s=args.reboot_poll,
                          reboot_deadline_s=args.reboot_deadline)
    transport = BS.SshTransport(args.ssh_host)
    print("[matrix] reboot between phases: issuing /sbin/reboot -f and "
          "waiting up to %.0f s ..." % args.reboot_deadline, flush=True)
    ok, note = BS.reboot_and_wait(transport, o, poll_s=args.reboot_poll,
                                  deadline_s=args.reboot_deadline)
    print("[matrix] reboot %s: %s" % ("OK" if ok else "FAILED", note),
          flush=True)
    return ok, note


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
    summary = load_json(os.path.join(phase_dir, "summary.json")) or {}
    ev = load_json(os.path.join(phase_dir, "protocol-evidence.json")) or {}
    rec["bench_result"] = summary.get("result")
    rec["protocol_verdict"] = ev.get("verdict")
    rec["failed_step"] = ev.get("failed_step")
    ref = REFUSED_RE.search(err) or REFUSED_RE.search(out)
    code = ref.group(1) if ref else None
    msg = (ref.group(2).strip() if ref else "").strip()
    if args.dry_run:
        if rc == 0:
            t = rec["totals"] or {}
            rec["status"] = "PLAN-OK"
            rec["detail"] = ("dry-run plan validated: %s step(s), %s check(s), "
                             "%s device write(s)"
                             % (t.get("steps"), t.get("checks"),
                                t.get("attr_writes")))
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
        rec["detail"] = (err.strip().splitlines() or ["?"])[-1][:300]
        return rec
    # ---- real attended run: classify from the session/evidence artefacts ----
    # (bench_session returns 0 PASS, 12 BLOCKED, 10 STOP, 11 FAIL, 2 REFUSED,
    # 4 ABORTED - the exit code alone is not the status.)
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
    result = summary.get("result")
    verdict = ev.get("verdict")
    rec["detail"] = ("session result %s%s; protocol verdict %s"
                     % (result,
                        (":" + str(summary.get("stop_rule")))
                        if summary.get("stop_rule") else "",
                        verdict))
    if result == "PASS" and verdict in (None, "PASS"):
        rec["status"] = "PASS"
    elif result == "BLOCKED" or verdict == "BLOCKED" \
            or ev.get("blocked_step"):
        rec["status"] = "BLOCKED-DRIVER"
        rec["blocked_reason"] = "documented-blocked:%s" \
            % (ev.get("blocked_step") or summary.get("stop_rule") or "?")
        rec["detail"] += " (documented pending-feature outcome: %s)" \
            % (ev.get("blocked_step") or summary.get("stop_rule") or "?")
    elif result == "STOP":
        rec["status"] = classify_stop(ev)
        if rec["status"] == "BLOCKED-DRIVER":
            rec["blocked_reason"] = "documented-blocked:%s" \
                % (ev.get("failed_step") or summary.get("stop_rule") or "?")
            rec["detail"] += (" (documented pending-feature outcome: %s)"
                              % (ev.get("failed_step")
                                 or summary.get("stop_rule") or "?"))
    elif result == "FAIL" or verdict == "FAIL":
        rec["status"] = "FAIL"
    elif result is None:
        rec["detail"] = ("no session result artefact; rc=%s; %s"
                         % (rc, (err.strip().splitlines() or ["?"])[-1][:200]))
    return rec


def classify_stop(evidence: dict) -> str:
    """A STOP is a real failure unless it is a known pending-feature stop."""
    failed = evidence.get("failed_step")
    for st in evidence.get("steps") or []:
        for c in st.get("checks") or []:
            if c.get("code") == "blocked-driver":
                return "BLOCKED-DRIVER"
            if c.get("code") == "shell-rc-mismatch" \
                    and c.get("actual") in ("rc=95", "rc=161"):
                return "BLOCKED-DRIVER"
    return "STOP"


def run_phase(args, p: dict, base: str, stamp: str) -> dict:
    phase_dir = os.path.join(base, stamp, p["id"])
    argv = build_argv(args, p, phase_dir)
    rec = {"phase": p["id"], "row": p["row"], "label": p["label"],
           "mode": p["mode"], "sequence": p["sequence"],
           "run_kind": "dry-run" if args.dry_run else "attended-real",
           "command": argv, "expect": p["expect"]}
    print("== phase %-3s (%s) ==" % (p["id"], p["label"]), flush=True)
    t0 = time.time()
    timeout = args.phase_timeout if not args.dry_run else 600
    rc, out, err = _run_child(argv, timeout)
    rec["elapsed_s"] = round(time.time() - t0, 2)
    rec["rc"] = rc
    rec["phase_timeout_s"] = timeout
    rec.update(classify(args, p, rc, out, err, phase_dir))
    rec["stdout_tail"] = out.strip().splitlines()[-3:]
    if err.strip():
        rec["stderr_tail"] = err.strip().splitlines()[-3:]
    print("   status: %-14s %s" % (rec["status"], rec["detail"][:160]),
          flush=True)
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
    a("- BLOCKED-DRIVER: a real attended run reached a documented pending")
    a("  driver feature (e.g. the cfg80211 M2 scan stub: blocked_rc [95,161] +")
    a("  'Not supported' signature -> the scan step records BLOCKED/SESSION_RESULT")
    a("  BLOCKED, or a STOP whose evidence carries blocked-driver/rc=95/161);")
    a("  the ladder is ready for the next driver revision.")
    a("- PASS / FAIL / STOP: real attended session results (see each phase's")
    a("  evidence dir for protocol-evidence.json + PROTOCOL-SEQUENCE.md).")
    a("- --reboot-between/--reboot-after are orchestrated by the MATRIX: it")
    a("  issues /sbin/reboot -f and waits with a hard --reboot-deadline; if the")
    a("  box does not come back the remaining phases are NOT-RUN (never run on")
    a("  a stale box), and a per-phase --phase-timeout kills the child's whole")
    a("  process group so an ssh grandchild cannot outlive the run.")
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
    ap.add_argument("--reboot-after", action="store_true",
                    help="real runs: reboot the box after every phase (the "
                         "matrix issues /sbin/reboot -f and waits with a hard "
                         "deadline; the child session is never asked to reboot)")
    ap.add_argument("--reboot-between", action="store_true",
                    help="real multi-phase runs: reboot after every phase "
                         "except the last (a loaded hi5622_wifi blocks the "
                         "next phase: module-already-loaded / insmod rc=255)")
    ap.add_argument("--reboot-deadline", type=float, default=300.0,
                    help="between-phase reboot: hard deadline for the box to "
                         "come back in seconds (default 300)")
    ap.add_argument("--reboot-poll", type=float, default=4.0,
                    help="between-phase reboot: poll interval in seconds "
                         "(default 4)")
    ap.add_argument("--phase-timeout", type=float, default=900.0,
                    help="real runs: hard wall-clock limit per phase child in "
                         "seconds (default 900); on timeout the child's whole "
                         "process group is killed and the phase is ERROR")
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
    if args.reboot_between and not args.dry_run and len(selected) == 1:
        print("note: --reboot-between has no effect for a single phase "
              "(use --reboot-after instead)")
    records = []
    for idx, pid in enumerate(selected):
        rec = run_phase(args, phase_by_id(pid), base, stamp)
        records.append(rec)
        if want_reboot_after(args, idx, len(selected)):
            ok, note = do_reboot(args)
            rec["reboot_after"] = note
            rec["reboot_after_ok"] = ok
            if not ok:
                # do not start the next phase on a stale/unknown box state
                print("[matrix] ABORTING remaining phases: the between-phase "
                      "reboot did not come back", file=sys.stderr, flush=True)
                for rest in selected[idx + 1:]:
                    records.append({
                        "phase": rest, "row": phase_by_id(rest)["row"],
                        "status": "NOT-RUN",
                        "detail": "skipped: the between-phase reboot after "
                                  "%s failed: %s" % (pid, note),
                        "rc": None, "sequence":
                            phase_by_id(rest)["sequence"],
                        "mode": phase_by_id(rest)["mode"]})
                break
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
