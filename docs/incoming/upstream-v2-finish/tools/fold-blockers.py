#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Attribute the blocked v2 fold hunks to their missing-context sources.

The sequential composition probe (fold-sequence.py) leaves 6 fold sources
uncomposed.  This tool explains why, without guessing: for every hunk of
those sources it computes the context/removed lines that are absent from the
composed tree (branch v2compose in the fold-probe scratch repo), then looks
up which L0 patch (and which v1 review-form entry) adds each missing line.
The result is the dependency root list used in FOLD-EXECUTION.md.

Host-only, read-only: it reads the scratch repo and the patch stacks and
writes a text/json report.  It never edits the tree.

Usage:
  python3 tools/fold-blockers.py --workdir <fold-probe workdir> [--out DIR]
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
LANE = HERE.parent
REPO = LANE.parents[3]
L0 = (REPO / "opensource" / "target-skeleton" / "target" / "linux"
      / "hisilicon" / "patches-6.18")
SERIES = REPO / "opensource" / "upstream" / "series-6.18" / "series"
BLOCKED = ["041", "046", "048", "053", "059", "060"]
NL = chr(10)


def run(cmd, cwd, check=True):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if check and p.returncode != 0:
        sys.exit("FAILED " + " ".join(cmd) + ": " + (p.stdout or "") + (p.stderr or ""))
    return p


def added_lines(patch):
    out = {}
    for line in patch.read_text(errors="replace").splitlines():
        if line.startswith("+") and not line.startswith("+++"):
            out.setdefault(line[1:].strip(), set()).add(patch.name.split("-")[0])
    return out


def parse_hunks(text):
    hunks = []
    cur = None
    path = None
    for line in text.splitlines():
        if line.startswith("+++ "):
            path = line[6:].strip()
            continue
        if line.startswith("--- "):
            continue
        if line.startswith("@@ "):
            if cur is not None and path:
                hunks.append((path, cur))
            cur = [line]
            continue
        if cur is not None:
            cur.append(line)
    if cur is not None and path:
        hunks.append((path, cur))
    return hunks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--workdir", required=True)
    ap.add_argument("--out", default=None)
    args = ap.parse_args()
    workdir = Path(args.workdir).resolve()
    out = Path(args.out).resolve() if args.out else LANE / "validation"
    out.mkdir(parents=True, exist_ok=True)
    tree = workdir / "tree"
    if not (tree / ".git").is_dir():
        sys.exit("no scratch repo (run fold-probe.py first)")

    run(["git", "checkout", "-qf", "v2compose"], tree)
    l0_added = {}
    for p in sorted(L0.glob("*.patch")):
        for line, nums in added_lines(p).items():
            l0_added.setdefault(line, set()).update(nums)
    v1_added = {}
    for p in sorted(SERIES.glob("*.patch")):
        for line, nums in added_lines(p).items():
            v1_added.setdefault(line, set()).update(nums)

    report = {}
    for num in BLOCKED:
        src = [p for p in sorted(L0.glob("*.patch")) if p.name.startswith(num + "-")][0]
        rec = {"source": src.name, "hunks": []}
        for i, (path, hunk) in enumerate(parse_hunks(src.read_text()), 1):
            p = run(["git", "show", "HEAD:" + path], tree, check=False)
            if p.returncode != 0:
                rec["hunks"].append({"hunk": i, "file": path, "missing": "file absent"})
                continue
            have = set(line.strip() for line in p.stdout.splitlines())
            missing_ctx = [line[1:].strip() for line in hunk
                           if line.startswith(" ") and line[1:].strip()
                           and line[1:].strip() not in have]
            missing_old = [line[1:].strip() for line in hunk
                           if line.startswith("-") and line[1:].strip()
                           and line[1:].strip() not in have]
            if not missing_ctx and not missing_old:
                continue
            l0prov = set()
            v1prov = set()
            for line in (missing_ctx + missing_old)[:12]:
                l0prov |= l0_added.get(line, set())
                v1prov |= v1_added.get(line, set())
            rec["hunks"].append({
                "hunk": i, "file": path, "header": hunk[0].strip(),
                "missing_context": len(missing_ctx),
                "missing_removed": len(missing_old),
                "l0_providers": sorted(l0prov),
                "v1_providers": sorted(v1prov),
                "sample_context": missing_ctx[:3],
            })
        report[num] = rec

    (out / "fold-blockers.json").write_text(json.dumps(report, indent=1))
    lines = ["# Blocked fold hunks - missing-context attribution (host-only)", "",
             "Composed tree = frozen v1 form + the 10 composing folds in L0 order",
             "(branch v2compose).  For every hunk of the 6 blocked sources this",
             "lists the context/removed lines absent from that tree and which L0",
             "patch / v1 entry adds them.  Generated by tools/fold-blockers.py.", ""]
    for num in BLOCKED:
        rec = report[num]
        lines.append("## " + num + " - " + rec["source"])
        lines.append("")
        lines.append("| hunk | file | missing ctx/old | L0 providers | v1 providers |")
        lines.append("| --- | --- | --- | --- | --- |")
        for h in rec["hunks"]:
            if "missing" in h:
                lines.append("| %d | %s | %s | - | - |" % (h["hunk"], h["file"], h["missing"]))
                continue
            lines.append("| %d | %s | %d/%d | %s | %s |"
                         % (h["hunk"], h["file"], h["missing_context"],
                            h["missing_removed"],
                            ",".join(h["l0_providers"]) or "-",
                            ",".join(h["v1_providers"]) or "-"))
        lines.append("")
    (out / "fold-blockers.md").write_text(NL.join(lines))
    print(NL.join(lines))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
