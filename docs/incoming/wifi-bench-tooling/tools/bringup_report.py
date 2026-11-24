#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""bringup_report.py - bring-up status table generator (host-only).

Lane: wifi-bench-tooling (shared task task-114), HOST-ONLY.

Reads one or more phase-matrix.json records produced by tools/phase_matrix.py
and renders the bring-up state table:

    boot | ready | calibration | vap | scan | dataplane

Each row carries a status (PASS / READY / BLOCKED / FAIL / STOP / NOT-RUN), the
evidence directory and the next action for a blocked/failed row.  This tool
never contacts the device: it only aggregates evidence files that already exist
under the gitignored build/ evidence tree.

    python3 tools/bringup_report.py --matrix build/.../phase-matrix.json
    python3 tools/bringup_report.py --dir build/evidence-bench-gates/wifi-bench-phase-matrix
    python3 tools/bringup_report.py --dir ... --json
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import sys
import time

TOOLDIR = os.path.dirname(os.path.abspath(__file__))
if TOOLDIR not in sys.path:
    sys.path.insert(0, TOOLDIR)

SCHEMA = "hi5622-bringup-status/1"
ROWS = (
    ("boot", "boot"),
    ("ready", "ready handshake"),
    ("calibration", "calibration (C2)"),
    ("vap", "VAP add/del (D1)"),
    ("scan", "scan request (D2)"),
    ("dataplane", "data-plane (E1)"),
)
PX = ("0x%08x" % 0x86807FEF, "0x%08x" % 7, "0x%08x" % 2,
      "0x%08x" % 0xABCD)
STATUS_MAP = {
    "PLAN-OK": "READY",
    "PASS": "PASS",
    "BLOCKED-TOKEN": "BLOCKED",
    "BLOCKED-DRIVER": "BLOCKED",
    "BLOCKED-PREREQ": "BLOCKED",
    "REFUSED": "REFUSED",
    "STOP": "STOP",
    "FAIL": "FAIL",
    "ERROR": "ERROR",
}
NEXT_ACTION = {
    "boot": "run the boot phase on the bench (read-only sequence): it gates the "
            "golden page, state, mask and the ready pair",
    "ready": "the ready pair is gated by every phase's boot ladder; no separate "
             "run is needed",
    "calibration": "run phase C2 on the calibration-token driver with "
                   "h2d_send_budget=8",
    "vap": "run phase D1 on the cfg80211 image/module: wiphy + VAP add/del",
    "scan": "run phase D2 on the wifi-vap-start PROVIDER module "
            "(vap_start_mode=1, allow_frames=12; build with "
            "../wifi-vap-start/tools/kernel-check.sh): the ladder is ready -> "
            "ctx_create -> txq_sync72 -> iw iface add/link up -> ONE iw scan; "
            "results=0 is the documented BLOCKED-DRIVER outcome",
    "dataplane": "wait for the dp-bringup driver token (contracted name "
                 "h2d_txq_addr_sync; the E1 sequence JSON is the single edit "
                 "point), then run phase E1",
}


def load_matrix(path: str) -> dict | None:
    try:
        with open(path, "r", encoding="utf-8") as f:
            doc = json.load(f)
    except (OSError, ValueError):
        return None
    if not isinstance(doc, dict) or doc.get("schema") != "hi5622-phase-matrix/1":
        return None
    return doc


def find_matrices(directory: str) -> list:
    paths = sorted(glob.glob(os.path.join(directory, "**", "phase-matrix.json"),
                             recursive=True),
                   key=lambda p: os.path.getmtime(p))
    return [p for p in paths if load_matrix(p) is not None]


def read_summary(rec: dict) -> dict:
    d = rec.get("evidence_dir") or ""
    for name in ("summary.json", "protocol-evidence.json"):
        path = os.path.join(d, name)
        if not os.path.isfile(path):
            continue
        try:
            with open(path, "r", encoding="utf-8") as f:
                return json.load(f)
        except (OSError, ValueError):
            pass
    return {}


def phase_record(matrix: dict, pid: str) -> dict | None:
    for rec in matrix.get("phases") or []:
        if rec.get("phase") == pid:
            return rec
    return None


def row_from_matrix(matrix: dict, row: str) -> dict:
    pid = {"calibration": "C2", "vap": "D1", "scan": "D2",
           "dataplane": "E1", "boot": "boot", "ready": "boot"}.get(row)
    rec = phase_record(matrix, pid) if pid else None
    out = {"row": row, "phase": pid, "status": "NOT-RUN",
           "detail": "no phase record in the selected matrix",
           "evidence_dir": None, "run_kind": matrix.get("run_kind"),
           "matrix": matrix.get("_path")}
    if rec is None:
        return out
    out["status"] = STATUS_MAP.get(rec.get("status"), rec.get("status"))
    out["evidence_dir"] = rec.get("evidence_dir")
    out["phase_status"] = rec.get("status")
    out["detail"] = rec.get("detail") or ""
    if row == "boot":
        summary = read_summary(rec)
        if rec.get("run_kind") == "dry-run":
            out["detail"] = ("plan validated (%s step(s)); a real run gates the "
                             "golden page %s/%s/%s/%s, state 1, mask 0 and the "
                             "strict ready pair"
                             % ((rec.get("totals") or {}).get("steps"), *PX))
        else:
            page = summary.get("page") or {}
            words = page.get("words")
            out["detail"] = ("page %s ack2=%s state=%s mask=%s"
                             % ("/".join("0x%08x" % w for w in words)
                                if words else "?",
                                summary.get("page_ack2"),
                                summary.get("state"),
                                ("0x%08x" % summary["mask"])
                                if isinstance(summary.get("mask"), int) else "?"))
    if row == "ready":
        summary = read_summary(rec)
        ready = summary.get("ready") or {}
        if rec.get("run_kind") == "dry-run":
            out["detail"] = ("gated by the strict ready pair before the "
                             "sequence (g4 id1 <= 2.0 s, g4 id2 <= 20.0 s, "
                             "phase READY_TXQ_SYNC)")
        else:
            out["detail"] = ("id1_seen=%s id2_seen=%s payload=%s"
                             % (ready.get("id1_seen"),
                                ready.get("id2_seen"),
                                ready.get("id2_payload")))
    if out["status"] in ("BLOCKED", "REFUSED", "FAIL", "STOP", "ERROR"):
        out["next_action"] = NEXT_ACTION.get(row, "inspect the evidence dir")
    else:
        out["next_action"] = None
    return out


def render_markdown(status: dict) -> str:
    L = []
    a = L.append
    a("# Wi-Fi bring-up status")
    a("")
    a("- schema: %s" % status["schema"])
    a("- generated: %s" % status["generated"])
    a("- source matrix: %s" % status["matrix"])
    a("- run kind: **%s**" % status["run_kind"])
    a("")
    a("| state | status | detail | evidence | next action |")
    a("| --- | --- | --- | --- | --- |")
    for row in status["rows"]:
        a("| **%s** | %s | %s | %s | %s |"
          % (row["row"], row["status"], row["detail"].replace("|", "\\|")[:240],
             (row["evidence_dir"] or "-").replace("|", "\\|"),
             (row.get("next_action") or "-").replace("|", "\\|")[:220]))
    a("")
    a("## Reading the table")
    a("")
    a("- READY = dry-run plan validated for that phase (no device contact);")
    a("  PASS = real attended session with the phase verdict PASS.")
    a("- BLOCKED = a prerequisite deliverable is missing, or a documented")
    a("  pending driver feature stopped the ladder (e.g. the M2 scan stub).")
    a("- NOT-RUN = the phase was not part of the selected matrix.")
    a("")
    a("Regenerate the matrix: python3 tools/phase_matrix.py --phase all --dry-run ...")
    a("")
    return "\n".join(L) + "\n"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description="render the Wi-Fi bring-up state table from phase-matrix "
                    "evidence (host-only; never contacts the device)")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--matrix", help="path to phase-matrix.json")
    g.add_argument("--dir", help="search a directory tree for phase-matrix.json")
    ap.add_argument("--out", help="write BRINGUP-STATUS.md here (default: next to "
                                  "the matrix)")
    ap.add_argument("--json", action="store_true", help="print the status JSON")
    ap.add_argument("--no-write", action="store_true",
                    help="do not write BRINGUP-STATUS.md")
    args = ap.parse_args(argv)
    if args.matrix:
        paths = [args.matrix]
    else:
        paths = find_matrices(args.dir)
        if not paths:
            print("error: no phase-matrix.json under %r (run "
                  "tools/phase_matrix.py first)" % args.dir, file=sys.stderr)
            return 2
    matrix = load_matrix(paths[-1])
    if matrix is None:
        print("error: not a hi5622-phase-matrix/1 document: %s" % paths[-1],
              file=sys.stderr)
        return 2
    matrix["_path"] = os.path.abspath(paths[-1])
    rows = [row_from_matrix(matrix, row) for row, _label in ROWS]
    status = {
        "schema": SCHEMA,
        "generated": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "matrix": matrix["_path"],
        "run_kind": matrix.get("run_kind"),
        "rows": rows,
        "history": [os.path.abspath(p) for p in paths],
    }
    md = render_markdown(status)
    print("== Wi-Fi bring-up status (%s) ==" % status["run_kind"])
    for r in rows:
        print("  %-11s %-10s %s" % (r["row"], r["status"],
                                    r["detail"][:110]))
    out = args.out
    if not out and not args.no_write:
        out = os.path.join(os.path.dirname(matrix["_path"]),
                           "BRINGUP-STATUS.md")
    if out and not args.no_write:
        os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
        with open(out, "w", encoding="utf-8") as f:
            f.write(md)
        print("markdown: %s" % out)
    if args.json:
        print(json.dumps(status, indent=1, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
