#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Sequential composition probe for the v2 review-form folds (host-only).

The per-hunk probe (fold-probe.py) tells whether each fold hunk applies at a
given frozen v1 state.  This tool answers the next question: if the 16 fold
source patches are applied **in L0 order on top of the frozen 26-patch v1
review form**, which ones compose cleanly?  That is the optimistic Option-B
composition (fold content carried into the review form without rewriting L0).

It reuses the scratch repo from fold-probe.py (branch "v1"), checks out a new
branch "v2compose" at the end of v1, then for every fold source patch in L0
order attempts git apply; on failure it retries with reduced context (-C1),
then patch(1) with 0 fuzz, and records the failure instead of forcing it.
Nothing outside the scratch repo is touched.

Usage:
  python3 tools/fold-sequence.py --workdir <fold-probe workdir> [--out DIR]
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
NL = chr(10)

FOLD_ORDER = ["027", "031", "032", "036", "038", "041", "043", "044", "046",
              "048", "051", "053", "059", "060", "066", "067"]


def run(cmd, cwd, check=True):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if check and p.returncode != 0:
        sys.exit("FAILED " + " ".join(cmd) + ": " + (p.stdout or "") + (p.stderr or ""))
    return p


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
        sys.exit("no scratch repo in " + str(tree) + " (run fold-probe.py first)")

    v1 = run(["git", "rev-parse", "v1"], tree).stdout.strip()
    run(["git", "checkout", "-q", "-B", "v2compose", v1], tree)
    run(["git", "clean", "-qfd"], tree)

    results = []
    for num in FOLD_ORDER:
        src = [p for p in sorted(L0.glob("*.patch")) if p.name.startswith(num + "-")]
        if not src:
            sys.exit("missing L0 patch " + num)
        src = src[0]
        rec = {"fold": num, "source": src.name, "method": None, "ok": False,
               "error": []}
        p = run(["git", "apply", "--check", str(src)], tree, check=False)
        if p.returncode == 0:
            run(["git", "apply", str(src)], tree)
            rec["method"] = "git apply"
            rec["ok"] = True
        else:
            first = (p.stderr or p.stdout).strip().splitlines()[:2]
            p1 = run(["git", "apply", "-C1", "--check", str(src)], tree, check=False)
            if p1.returncode == 0:
                run(["git", "apply", "-C1", str(src)], tree)
                rec["method"] = "git apply -C1 (reduced context)"
                rec["ok"] = True
            else:
                rec["error"] = first
                # a failed git apply -C1 --check never modified the tree; the
                # plain patch(1) fallback is deliberately not attempted because
                # patch(1) applies partial hunks and leaves .rej state behind.
        if rec["ok"]:
            run(["git", "add", "-A"], tree)
            run(["git", "commit", "-q", "-m",
                 "fold " + num + " via " + rec["method"]], tree)
        results.append(rec)

    diffstat = run(["git", "diff", "--stat", v1, "HEAD"], tree, check=False)
    files = run(["git", "diff", "--numstat", v1, "HEAD"], tree, check=False)
    head = run(["git", "rev-parse", "HEAD"], tree).stdout.strip()
    payload = {"v1_state": v1, "head_state": head,
               "composed": sum(1 for r in results if r["ok"]),
               "total": len(results), "folds": results,
               "diffstat": diffstat.stdout.strip(),
               "numstat": files.stdout.strip()}
    (out / "fold-sequence.json").write_text(json.dumps(payload, indent=1))

    lines = ["# Sequential fold composition probe (host-only)", "",
             "Starting state: frozen v1 26-patch review form (" + v1[:12] + ").",
             "Fold source patches applied in L0 order; nothing forced.", "",
             "| fold | method | composed | error |",
             "| --- | --- | --- | --- |"]
    for r in results:
        lines.append("| %s | %s | %s | %s |"
                     % (r["fold"], r["method"] or "-",
                        "yes" if r["ok"] else "NO",
                        (r["error"][0] if r["error"] else "")))
    lines += ["", "composed " + str(payload["composed"]) + "/" + str(payload["total"]),
              "", "Cumulative diffstat v1end -> composed tree:", "", "~~~",
              payload["diffstat"], "~~~", ""]
    (out / "fold-sequence.md").write_text(NL.join(lines))
    print(NL.join(lines))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
