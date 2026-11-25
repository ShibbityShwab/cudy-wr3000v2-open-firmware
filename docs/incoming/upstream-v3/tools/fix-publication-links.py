#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Fix the publication-check section G (broken relative markdown links).

Host-only, idempotent, no network.  Applies explicit per-file rules to the
2026-11-29 published tree so that tools/publish-prep-check.sh section G passes:

  * docs/incoming/vendor-userspace/REPORT.md - links escape the repository
    root (../../../../...); they are parent-notebook paths that cannot resolve
    inside this repository, so keep the label and the path as code text.
  * docs/incoming/wifi-5ghz/BUILD-NOTES.md - link targets use one ../ too
    many (same-directory dirs); drop the ../.
  * docs/incoming/wifi-ap-start/HOSTAPD-BRINGUP.md - same.
  * docs/incoming/wifi-closeout/OVERVIEW-README.md - the file is a proposed
    repository-root README whose links are written root-relative; make them
    resolve from the file's current path with ../../.  If the file is promoted
    to the repository root later, remove those ../../ prefixes again.
  * docs/incoming/wifi-scan-{ap,complete}/configs/README.md - TEST-LADDER.md
    is one level up.
  * docs/incoming/wifi-stability/SOAK.md - RESULT-TEMPLATE.md is one level up.
  * docs/phase17/ete-engine.md, docs/phase20/rx-loop.md - pseudo-call syntax
    ([x](y)) that markdown misreads as a link; break the []() adjacency with
    an inline-code close.

Every rule is a plain literal/regex replacement; re-running the tool changes
nothing once the fixes are in place.  Usage:
  python3 tools/fix-publication-links.py [--check]
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]  # opensource/

# literal replacements: (file, old, new)
LITERAL = [
    ("docs/incoming/vendor-userspace/REPORT.md", None, None),  # regex rule below
    ("docs/incoming/wifi-5ghz/BUILD-NOTES.md", "](../patches/", "](patches/"),
    ("docs/incoming/wifi-5ghz/BUILD-NOTES.md", "](../evidence/", "](evidence/"),
    ("docs/incoming/wifi-5ghz/BUILD-NOTES.md", "](../.gitignore)", "](.gitignore)"),
    ("docs/incoming/wifi-ap-start/HOSTAPD-BRINGUP.md", "](../image/", "](image/"),
    ("docs/incoming/wifi-ap-start/HOSTAPD-BRINGUP.md", "](../tools/", "](tools/"),
    ("docs/incoming/wifi-ap-start/HOSTAPD-BRINGUP.md", "](../recipes/", "](recipes/"),
    ("docs/incoming/wifi-scan-ap/configs/README.md", "](TEST-LADDER.md)", "](../TEST-LADDER.md)"),
    ("docs/incoming/wifi-scan-complete/configs/README.md", "](TEST-LADDER.md)", "](../TEST-LADDER.md)"),
    ("docs/incoming/wifi-stability/SOAK.md", "](../results/RESULT-TEMPLATE.md)", "](results/RESULT-TEMPLATE.md)"),
    ("docs/phase17/ete-engine.md", "`[bus_ctx+0x38](handle, ...)`", "`[bus_ctx+0x38]`(handle, ...)"),
    ("docs/phase20/rx-loop.md", "`[ete+0x1c](ete, bit)`", "`[ete+0x1c]`(ete, bit)"),
]

ESCAPE_RE = re.compile(r"\[([^\]]*)\]\((\.[.]/){4}([^)]+)\)")
CLOSEOUT_RE = re.compile(
    r"\]\(((?:docs/|upstream/|target-skeleton/|NOTICE\.md|LICENSE))")
CLOSEOUT = "docs/incoming/wifi-closeout/OVERVIEW-README.md"


def escape_sub(m):
    label, target = m.group(1), m.group(0).split("](")[1][:-1]
    return "`%s` (`%s`)" % (label, target)


def fix_file(rel, text, apply):
    original = text
    if rel == "docs/incoming/vendor-userspace/REPORT.md":
        text = ESCAPE_RE.sub(escape_sub, text)
    elif rel == CLOSEOUT:
        text = CLOSEOUT_RE.sub(lambda m: "](../../../" + m.group(1), text)
    else:
        for f, old, new in LITERAL:
            if f == rel and old is not None:
                text = text.replace(old, new)
    return text, text != original


def main():
    check_only = "--check" in sys.argv[1:]
    total = 0
    for rel in sorted({r[0] for r in LITERAL} | {CLOSEOUT}):
        p = ROOT / rel
        if not p.is_file():
            print("MISSING %s" % rel)
            return 1
        new, changed = fix_file(rel, p.read_text(), not check_only)
        if changed:
            total += 1
            if not check_only:
                p.write_text(new)
            print(("would fix " if check_only else "fixed     ") + rel)
    print("%d file(s) %s" % (total, "need fixes" if check_only else "fixed"))
    return 0 if (total == 0 or not check_only) else 1


if __name__ == "__main__":
    raise SystemExit(main())
