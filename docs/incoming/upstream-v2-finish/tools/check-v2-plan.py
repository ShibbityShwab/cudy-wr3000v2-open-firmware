#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Host-only consistency checks for the luofu v2 review-form plan.

The v2 plan is spread over three documents that were written at different
times:

  * opensource/docs/incoming/upstream-v4/V2-REVIEW-FORM-STATUS.md   (v4, 2026-11-29)
  * opensource/docs/incoming/wifi-upstream-target/submission-series.md (plan, 2026-11-24)
  * opensource/docs/incoming/wifi-upstream-target/PATCH-CLASSIFICATION.md (2026-11-24)

This tool re-derives every count and fold set from those files and the tree,
and reports mismatches.  It is read-only and host-only.

Usage:
  python3 tools/check-v2-plan.py [--write <file>]
"""

import argparse
import hashlib
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
LANE = HERE.parent
REPO = LANE.parents[3]
OS = REPO / "opensource"
SERIES_DIR = OS / "upstream" / "series-6.18"
SERIES = SERIES_DIR / "series"
MESSAGES = SERIES_DIR / "commit-messages"
MANIFEST = SERIES_DIR / "release-line" / "manifest.tsv"
L0 = OS / "target-skeleton" / "target" / "linux" / "hisilicon" / "patches-6.18"
DTS_PATCH = SERIES_DIR / "dts-series"
V4_STATUS = OS / "docs" / "incoming" / "upstream-v4" / "V2-REVIEW-FORM-STATUS.md"
PLAN = OS / "docs" / "incoming" / "wifi-upstream-target" / "submission-series.md"
CLASSIFICATION = OS / "docs" / "incoming" / "wifi-upstream-target" / "PATCH-CLASSIFICATION.md"
NL = chr(10)


def sha256(path):
    h = hashlib.sha256()
    h.update(path.read_bytes())
    return h.hexdigest()


def cell_parts(line):
    return [c.strip() for c in line.strip().strip("|").split("|")]


def patch_tokens(text):
    """3-digit patch numbers in a table cell (start with 0; skip line counts)."""
    out = set()
    flat = text.replace("+", " ").replace("(", " ").replace(")", " ")
    for word in flat.split():
        for part in word.split("-"):
            part = part.strip(",;:.")
            if len(part) == 3 and part.isdigit() and part.startswith("0"):
                out.add(part)
    return out


def fold_set_v4():
    """Distinct fold source numbers from the V2-REVIEW-FORM-STATUS 16-row table."""
    found = set()
    text = V4_STATUS.read_text().splitlines()
    in_table = False
    for line in text:
        if line.startswith("| --- "):
            in_table = True
            continue
        if not in_table:
            continue
        if not line.startswith("|"):
            break
        parts = cell_parts(line)
        if len(parts) < 2 or not parts[0].isdigit():
            continue
        found |= patch_tokens(parts[1])
    return found


def fold_set_classification():
    found = set()
    for line in CLASSIFICATION.read_text().splitlines():
        if not line.startswith("|"):
            continue
        parts = cell_parts(line)
        if len(parts) < 4 or "FOLD" not in parts[3]:
            continue
        token = parts[0]
        if token.startswith("013a"):
            found.add("013a")
        elif token.isdigit() and len(token) == 3:
            found.add(token)
    return found


def source_set_plan():
    """Fold sources named in the submission-series v2 delta table."""
    keep = set("%03d" % i for i in range(1, 27))
    found = set()
    in_table = False
    for line in PLAN.read_text().splitlines():
        if line.startswith("| --- "):
            in_table = True
            continue
        if not in_table:
            continue
        if not line.startswith("|"):
            break
        parts = cell_parts(line)
        if len(parts) < 3:
            continue
        for word in patch_tokens(parts[2]):
            if word not in keep:
                found.add(word)
    return found


def new_rfc_numbers_plan():
    """Proposed new RFC entry numbers from the delta table."""
    found = set()
    in_table = False
    for line in PLAN.read_text().splitlines():
        if line.startswith("| --- "):
            in_table = True
            continue
        if not in_table or not line.startswith("|"):
            if in_table and not line.startswith("|"):
                break
            continue
        parts = cell_parts(line)
        if len(parts) < 2:
            continue
        first = parts[0].split()[0] if parts[0] else ""
        if first.isdigit() and len(first) == 4 and int(first) >= 27:
            found.add(first)
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--write")
    args = ap.parse_args()

    checks = []

    def check(name, expected, actual, ok, detail=""):
        checks.append({"name": name, "expected": str(expected),
                       "actual": str(actual), "ok": bool(ok), "detail": detail})

    l0 = sorted(L0.glob("*.patch"))
    v1 = sorted(SERIES.glob("*.patch"))
    msgs = sorted(MESSAGES.glob("*.txt"))
    dts = sorted(DTS_PATCH.glob("*.patch"))
    include = exclude = 0
    manifest_rows = []
    for line in MANIFEST.read_text().splitlines():
        if line.startswith("#") or not line.strip():
            continue
        parts = line.split(chr(9))
        if parts[0] == "include":
            include += 1
        elif parts[0] == "exclude":
            exclude += 1
        if len(parts) >= 4 and parts[2].endswith(".patch"):
            manifest_rows.append((parts[0], parts[2], parts[3]))

    check("L0 patch files", "66 (v4 status: 66-file stack)", len(l0),
          len(l0) == 66, "PATCH-CLASSIFICATION still says 67 (pre-013a-fold)")
    check("v1 review-form patches", 26, len(v1), len(v1) == 26)
    check("commit-message files", 26, len(msgs), len(msgs) == 26,
          "v2 needs 0027..0030 messages (absent today)")
    check("release-line manifest include", 55, include, include == 55)
    check("release-line manifest exclude", 11, exclude, exclude == 11)
    check("DTS-series patches", 1, len(dts), len(dts) == 1)
    check("new RFC entries planned", 4, len(new_rfc_numbers_plan()),
          len(new_rfc_numbers_plan()) == 4,
          "planned: " + ",".join(sorted(new_rfc_numbers_plan())))

    # 013a: folded into 013 by v4 -> must be gone from L0 and not in the form
    has_013a = any(p.name.startswith("013a-") for p in l0)
    check("013a still a separate L0 patch", "no (v4 folded it into 013)",
          "yes" if has_013a else "no", not has_013a)
    fold_013a = "013a" in fold_set_classification()
    check("classification lists 013a fold", "yes (historic fold)", "yes" if fold_013a else "no",
          fold_013a, "013a is the one of 17 folds already executed")

    cls = fold_set_classification() - {"013a"}
    v4set = fold_set_v4()
    plan_set = source_set_plan()
    check("v4-status fold set == classification fold set", ",".join(sorted(v4set)),
          ",".join(sorted(cls)), v4set == cls)
    check("plan delta table fold set == v4-status fold set",
          ",".join(sorted(v4set)), ",".join(sorted(plan_set)), plan_set == v4set,
          "missing from submission-series.md: " +
          ",".join(sorted(v4set - plan_set)) if plan_set != v4set else "")

    # manifest hash drift vs current L0 patches
    drift = []
    for status, name, want in manifest_rows:
        p = L0 / name
        if not p.is_file():
            drift.append(name + " (missing)")
            continue
        got = sha256(p)
        if got != want:
            drift.append(name + " (hash differs)")
    check("manifest hashes match current L0 patches", "0 drift", len(drift),
          not drift, ",".join(drift[:6]))

    # v1 form still carries bench gates -> acceptance criterion not met by v1
    gated = []
    for p in v1:
        n = p.read_text().count("module_param")
        if n:
            gated.append(p.name[:4] + ":" + str(n))
    check("v1 form entries with module_param gates", "0 (acceptance)",
          ",".join(gated), not gated, "baseline v1: " + ",".join(gated))

    # gen-series drift
    p = subprocess.run([sys.executable, "tools/gen-series.py", "--check"],
                       cwd=SERIES_DIR, capture_output=True, text=True)
    check("gen-series.py --check (v1 form)", "clean", (p.stdout + p.stderr).strip()[-120:],
          p.returncode == 0)

    # DTS-0001 v4 claims
    if dts:
        lines = len(dts[0].read_text().splitlines())
        digest = sha256(dts[0])
        check("DTS-0001 v4 sha256", "6fa1c690...", digest[:8] + "...",
              digest.startswith("6fa1c690"))
        check("DTS-0001 v4 line count", "716", str(lines), lines == 716)
        hog = "wifi-pcie-hog" in dts[0].read_text()
        check("DTS-0001 carries wifi-pcie-hog", "no (retired before v4)",
              "yes" if hog else "no", not hog,
              "classification doc still says it must be removed")

    # 0013 carries the folded DT reset pulse
    p13 = None
    for p in v1:
        if p.name.startswith("0013-"):
            p13 = p.read_text()
    for needle in ("reset-gpios", "reset-assert-us", "reset-deassert-us"):
        check("0013 carries " + needle, "yes (v4 fold)",
              "yes" if p13 and needle in p13 else "no",
              bool(p13 and needle in p13))

    # gate-policy contradiction: submission-series drafts keep the gate; the
    # v4 status says fold -> normal-boot init / no gates.
    plan_text = PLAN.read_text()
    keeps_gate = "remains behind the attending gate" in plan_text or "behind the attending arm option" in plan_text
    v4_text = V4_STATUS.read_text()
    says_no_gate = "no luofu_* module parameters / bench gates" in (v4_text + PLAN.read_text())
    check("new-RFC drafts keep an attending gate", "no, if gates->none/DT is decided",
          "yes" if keeps_gate else "no", not keeps_gate,
          "v2 plan docs contradict each other on the gate policy")

    lines = ["# v2 plan consistency check (host-only, read-only)", "",
             "Derived from the three v2 plan documents and the tree by",
             "tools/check-v2-plan.py.  Every row is a reproducible check.", "",
             "| check | expected | actual | verdict | detail |",
             "| --- | --- | --- | --- | --- |"]
    for c in checks:
        lines.append("| %s | %s | %s | %s | %s |"
                     % (c["name"], c["expected"], c["actual"],
                        "PASS" if c["ok"] else "FAIL", c["detail"]))
    fails = [c for c in checks if not c["ok"]]
    lines += ["", "Result: %d checks, %d PASS, %d FAIL."
              % (len(checks), len(checks) - len(fails), len(fails)), ""]
    report = NL.join(lines)
    print(report)
    if args.write:
        Path(args.write).write_text(report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
