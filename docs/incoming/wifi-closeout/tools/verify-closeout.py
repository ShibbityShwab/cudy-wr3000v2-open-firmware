#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""verify-closeout.py - host-only verifier for the wifi-closeout lane.

Checks, with no device contact and no network:

  1. required deliverables exist and are non-empty;
  2. every relative Markdown link in the lane resolves (OVERVIEW-README.md is
     checked as if it were opensource/README.md, its intended location);
  3. the suggested EV rows start after the current highest EV id and are
     contiguous, with no duplicate/collision against the live report index;
  4. no new file is a vendor binary/blob (extension, NUL byte, long hex run);
  5. the final-state claims that must be present are present, and stale
     "not flashed / AP not up" phrasing is absent from the overview/status;
  6. the host-suite RESULT.txt exists and reports its totals;
  7. soft check: every git-tracked lane directory is mentioned in LANE-INDEX.md.

Usage: ./tools/verify-closeout.py [--quiet] [--self-test]
Exit 0 = all checks pass (warnings do not fail), 1 = failure.
"""
import argparse
import os
import re
import subprocess
import sys

LANE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.abspath(os.path.join(LANE, "..", "..", "..", ".."))
OSROOT = os.path.join(ROOT, "opensource")

REQUIRED = [
    "README.md",
    "LANE-INDEX.md",
    "EVIDENCE-REFRESH.md",
    "OVERVIEW-README.md",
    "TEST-MATRIX.md",
    "PUBLICATION-NOTES.md",
    "STATUS-NEXT-ACTION.md",
    "tools/run-host-suite.sh",
    "build-evidence/host-suite/RESULT.txt",
]

BINARY_EXT = {".o", ".ko", ".bin", ".img", ".dtb", ".apk", ".ipk", ".so", ".a",
              ".pyc", ".elf", ".ubi", ".squashfs", ".gz", ".xz", ".zst"}

OVERVIEW_MUST = ["AP-ENABLED", "cudy-ap-test", "PD4", "POST-ONLY", "flashed",
                 "hostapd", "scan", "EVIDENCE-REFRESH.md"]
STATUS_MUST = ["AP-ENABLED", "flashed", "PD4", "honest", "3m"]

STALE_PHRASES = ["not yet flashed", "nothing was flashed", "flash runbook ready"]

FAILS = []
WARNS = []
PASSES = []


def ok(msg):
    PASSES.append(msg)
    print("PASS  " + msg)


def fail(msg):
    FAILS.append(msg)
    print("FAIL  " + msg)


def warn(msg):
    WARNS.append(msg)
    print("WARN  " + msg)


def read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        return fh.read()


def link_targets(text):
    """Yield relative link targets from Markdown [x](target) forms."""
    for m in re.finditer(r"\[[^\]]*\]\(([^)\s]+)\)", text):
        yield m.group(1)


def resolve_target(src_file, target, assume_root=None):
    target = target.strip()
    if target.startswith("http://") or target.startswith("https://") or target.startswith("mailto:"):
        return None
    target = target.split("#", 1)[0].split("?", 1)[0]
    if not target:
        return None
    if target.startswith("/"):
        return ("absolute", target)
    if os.path.isabs(target):
        return ("absolute", target)
    base = assume_root if assume_root else os.path.dirname(src_file)
    return ("relative", os.path.normpath(os.path.join(base, target)))


def check_required():
    for rel in REQUIRED:
        path = os.path.join(LANE, rel)
        if not os.path.isfile(path):
            fail("missing required deliverable: " + rel)
        elif os.path.getsize(path) == 0:
            fail("empty required deliverable: " + rel)
        else:
            ok("deliverable present: " + rel)


def check_links():
    n = 0
    for name in sorted(os.listdir(LANE)):
        path = os.path.join(LANE, name)
        if not (os.path.isfile(path) and name.endswith(".md")):
            continue
        assume = OSROOT if name == "OVERVIEW-README.md" else None
        for target in link_targets(read(path)):
            resolved = resolve_target(path, target, assume)
            if resolved is None:
                continue
            kind, ref = resolved
            n += 1
            if kind == "absolute":
                fail("%s: absolute path link not allowed: %s" % (name, target))
            elif not os.path.exists(ref):
                fail("%s: broken relative link: %s" % (name, target))
    if not FAILS:
        ok("relative Markdown links resolve (%d checked; OVERVIEW as opensource/README.md)" % n)


def parse_ev_ids(text):
    """All EV ids mentioned anywhere in the text."""
    return sorted({int(x) for x in re.findall(r"\bEV-(\d{3})\b", text)})


def parse_suggested_ev(text):
    """EV ids carried by Markdown table rows (the suggested/added rows)."""
    ids = set()
    for line in text.splitlines():
        m = re.match(r"^\|\s*EV-(\d{3})\s*\|", line)
        if m:
            ids.add(int(m.group(1)))
    return sorted(ids)


def ev_block_ok(ids, after):
    """Suggested rows must be contiguous and start after the live index max."""
    if not ids:
        return False, "no suggested EV rows found"
    if ids[0] <= after:
        return False, ("first suggested row is EV-%03d but the live index already reaches "
                       "EV-%03d - renumber the block above the max" % (ids[0], after))
    expected = list(range(ids[0], ids[-1] + 1))
    if ids != expected:
        missing = sorted(set(expected) - set(ids))
        return False, "non-contiguous suggested rows, missing %s" % missing
    return True, "suggested block EV-%03d..EV-%03d contiguous (%d rows)" % (
        ids[0], ids[-1], len(ids))


def check_ev():
    mine_text = read(os.path.join(LANE, "EVIDENCE-REFRESH.md"))
    mine = parse_suggested_ev(mine_text)
    report_index = os.path.join(OSROOT, "docs", "incoming", "wifi-re-report", "EVIDENCE-INDEX.md")
    live = parse_ev_ids(read(report_index)) if os.path.isfile(report_index) else []
    after = max(live) if live else 0
    overlap = sorted(set(mine) & set(live))
    if overlap:
        fail("EV ids already used by wifi-re-report/EVIDENCE-INDEX.md: %s "
             "(renumber the block above the current max)" % overlap)
    elif live:
        ok("live report index max: EV-%03d; suggested rows start above it" % after)
    good, detail = ev_block_ok(mine, after)
    if good:
        ok("suggested EV block: " + detail)
    else:
        fail("suggested EV block: " + detail)


def check_no_binaries():
    bad = 0
    long_hex = re.compile(r"(?:\b[0-9a-fA-F]{2}\b[ ,]){64,}")
    for dirpath, dirnames, filenames in os.walk(LANE):
        dirnames[:] = [d for d in dirnames if d != "__pycache__"]
        for name in filenames:
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, LANE)
            ext = os.path.splitext(name)[1].lower()
            if ext in BINARY_EXT:
                fail("binary-extension file in lane: " + rel)
                bad += 1
                continue
            if os.path.getsize(path) > 512 * 1024:
                warn("large file in lane (%d B): %s" % (os.path.getsize(path), rel))
            data = open(path, "rb").read()
            if b"\x00" in data:
                fail("NUL byte in text deliverable: " + rel)
                bad += 1
                continue
            if long_hex.search(data.decode("utf-8", "replace")):
                warn("long hex run (possible raw bytes) in " + rel)
    if not bad:
        ok("no binary/vendor-byte files in the lane (extension + NUL scan)")


def check_claims():
    overview = read(os.path.join(LANE, "OVERVIEW-README.md"))
    status = read(os.path.join(LANE, "STATUS-NEXT-ACTION.md"))
    for token in OVERVIEW_MUST:
        if token not in overview:
            fail("OVERVIEW-README.md missing final-state token: " + token)
    for token in STATUS_MUST:
        if token not in status:
            fail("STATUS-NEXT-ACTION.md missing token: " + token)
    for stale in STALE_PHRASES:
        if stale in overview or stale in status:
            fail("stale phrasing present in overview/status: " + stale)
    if not any(f.startswith(("OVERVIEW", "STATUS")) or "stale" in f for f in FAILS):
        ok("final-state claims present in OVERVIEW/STATUS; no stale phrasing")


def check_suite():
    path = os.path.join(LANE, "build-evidence", "host-suite", "RESULT.txt")
    text = read(path)
    m = re.search(r"TOTAL (\d+)\s+PASS (\d+)\s+FAIL (\d+)\s+SKIP (\d+)", text)
    if not m:
        fail("host-suite RESULT.txt has no TOTAL/PASS/FAIL line")
        return
    total, passed, failed, skipped = (int(x) for x in m.groups())
    ok("host-suite: %d entries, %d PASS, %d FAIL, %d SKIP (see TEST-MATRIX.md)" %
       (total, passed, failed, skipped))


def tracked_lanes():
    try:
        out = subprocess.run(["git", "-C", OSROOT, "ls-files", "docs/incoming"],
                             capture_output=True, text=True, check=False).stdout
    except OSError:
        return []
    lanes = set()
    for line in out.splitlines():
        parts = line.split("/")
        if len(parts) >= 4 and parts[0] == "docs" and parts[1] == "incoming":
            lanes.add(parts[2])
    return sorted(lanes)


def check_lane_coverage():
    index = read(os.path.join(LANE, "LANE-INDEX.md"))
    missing = [lane for lane in tracked_lanes()
               if lane != "wifi-closeout" and lane not in index]
    # in-flight lanes owned by other agents may be untracked; those are not required
    in_flight = {"wifi-ready-emit", "wifi-upstream-final"}
    hard = [lane for lane in missing if lane not in in_flight]
    if hard:
        warn("tracked lane(s) not mentioned in LANE-INDEX.md: " + ", ".join(hard))
    else:
        ok("LANE-INDEX.md mentions every tracked lane directory")
    if missing and set(missing) & in_flight:
        print("INFO  in-flight/other-agent lanes not indexed: " +
              ", ".join(sorted(set(missing) & in_flight)))


def self_test():
    """Tiny synthetic checks of the helpers (no repo state needed)."""
    cases = [
        (ev_block_ok([278, 279, 280, 281], 277)[0], True),
        (ev_block_ok([278, 280], 277)[0], False),
        (ev_block_ok([277], 277)[0], False),
    ]
    for got, want in cases:
        if got != want:
            print("FAIL  self-test ev_block_ok returned %s, expected %s" % (got, want))
            return 1
    tmp = os.path.join(LANE, "build-evidence", ".selftest-link")
    os.makedirs(tmp, exist_ok=True)
    open(os.path.join(tmp, "target.md"), "w").close()
    got = resolve_target(os.path.join(tmp, "src.md"), "target.md")
    if got != ("relative", os.path.normpath(os.path.join(tmp, "target.md"))):
        print("FAIL  self-test resolve_target: %r" % (got,))
        return 1
    os.remove(os.path.join(tmp, "target.md"))
    os.rmdir(tmp)
    print("PASS  self-test: EV contiguity + relative link resolution helpers")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    check_required()
    check_links()
    check_ev()
    check_no_binaries()
    check_claims()
    check_suite()
    check_lane_coverage()
    print()
    print("RESULT: %d PASS, %d FAIL, %d WARN" % (len(PASSES), len(FAILS), len(WARNS)))
    if FAILS:
        print("FAILURES:")
        for f in FAILS:
            print("  - " + f)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
