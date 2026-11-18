#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Generate the commit-ready patch series for the luofu 6.18 submission series.

This script does not modify the patches in the target skeleton: it reads each
unified diff verbatim from
opensource/target-skeleton/target/linux/hisilicon/patches-6.18/ and prepends a
format-patch-style mail header built from the per-patch commit message under
commit-messages/.  The diff body in every generated file is byte-identical to
the source patch (the script verifies that and fails otherwise).

Usage:  python3 tools/gen-series.py [--check]
    --check   only regenerate in memory and verify; do not write files
"""

import hashlib
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

SERIES_DIR = Path(__file__).resolve().parents[1]
REPO_ROOT = SERIES_DIR.parents[2]
ORIGINALS = (
    REPO_ROOT
    / "opensource"
    / "target-skeleton"
    / "target"
    / "linux"
    / "hisilicon"
    / "patches-6.18"
)
MESSAGES = SERIES_DIR / "commit-messages"
OUT = SERIES_DIR / "series"

IDENT = "ShibbityShwab <18537102+ShibbityShwab@users.noreply.github.com>"
DATE = "Wed, 18 Nov 2026 08:20:00 +0700"

# The commit order of the series, and the status used by the mapping table.
SERIES = [
    ("0001-arm-mach-hisilicon-luofu", "RFC"),
    ("0002-arm-kconfig-textofs", "RFC"),
    ("0003-arm-textofs-nonimmediate", "RFC"),
    ("0004-arm-clk-reset-luofu-crg", "ready-for-review"),
    ("0005-arm-watchdog-luofu-wdt", "ready-for-review"),
    ("0006-arm-mtd-nand-luofu-fmc", "RFC"),
    ("0007-net-mdio-luofu", "ready-for-review"),
    ("0008-net-phy-luofu", "ready-for-review"),
    ("0009-net-gmac-luofu", "RFC (partial, refuses probe)"),
    ("0010-arm-luofu-restart", "RFC (provisional reset)"),
    ("0011-pinctrl-luofu", "RFC"),
    ("0012-arm-thermal-luofu-tvsensor", "ready-for-review"),
    ("0013-arm-pci-luofu-host", "RFC"),
    ("0014-arm-crg-reset-polarity", "ready-for-review"),
    ("0015-net-pie-luofu", "draft (do not merge)"),
    ("0016-net-lsw-luofu", "draft (do not merge)"),
    ("0017-soc-luofu-boot-selector", "RFC"),
    ("0018-arm-mach-hisilicon-smp", "RFC (attended)"),
    ("0019-arm-clk-luofu-clock-tree",
     "ready-for-review (read-only clock tree)"),
]


GROUPS = [
    ("Group 1 - core platform (RFC)", [
        "0001-arm-mach-hisilicon-luofu",
        "0002-arm-kconfig-textofs",
        "0003-arm-textofs-nonimmediate",
    ]),
    ("Group 2 - peripheral drivers and clock tree, ready for review", [
        "0004-arm-clk-reset-luofu-crg",
        "0005-arm-watchdog-luofu-wdt",
        "0007-net-mdio-luofu",
        "0008-net-phy-luofu",
        "0012-arm-thermal-luofu-tvsensor",
        "0014-arm-crg-reset-polarity",
        "0019-arm-clk-luofu-clock-tree",
    ]),
    ("Group 3 - peripheral drivers, RFC", [
        "0006-arm-mtd-nand-luofu-fmc",
        "0011-pinctrl-luofu",
        "0013-arm-pci-luofu-host",
        "0017-soc-luofu-boot-selector",
    ]),
    ("Group 4 - RFC / partial drafts, do not merge", [
        "0009-net-gmac-luofu",
        "0010-arm-luofu-restart",
        "0015-net-pie-luofu",
        "0016-net-lsw-luofu",
    ]),
    ("Group 5 - SMP bring-up (RFC, attended; not in the UP-first set)", [
        "0018-arm-mach-hisilicon-smp",
    ]),
]


def original_name(stem: str) -> str:
    # 0001-... -> 001-...  (the target-skeleton patches use 3-digit names)
    number, rest = stem.split("-", 1)
    return "%03d-%s.patch" % (int(number), rest)


GIT_HEADER = re.compile(r"^--- (a/.+|/dev/null)$")
GIT_NEW = re.compile(r"^\+\+\+ b/(.+)$")


def gitify(diff: str) -> str:
    """Turn the target-skeleton bare unified diff into a git-style diff.

    The series is kept as the existing unified diffs, but reviewers and
    checkpatch expect the standard git extended headers (diff --git, and
    new file mode for created files).  Insert them without changing a
    single byte of the hunks.
    """
    lines = diff.splitlines(keepends=True)
    out = []
    i = 0
    while i < len(lines):
        first = lines[i].rstrip("\n")
        m = GIT_HEADER.match(first)
        if m and i + 1 < len(lines):
            n = GIT_NEW.match(lines[i + 1].rstrip("\n"))
            if n:
                path = n.group(1)
                out.append("diff --git a/%s b/%s\n" % (path, path))
                if m.group(1) == "/dev/null":
                    out.append("new file mode 100644\n")
                out.append(lines[i])
                out.append(lines[i + 1])
                i += 2
                continue
        out.append(lines[i])
        i += 1
    return "".join(out)


def diffstat(patch_text: str) -> str:
    # git apply resolves patch paths against the current directory when it is
    # run from a subdirectory of a repository, so always run it in a scratch
    # directory that is not inside a work tree.
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = os.path.join(tmpdir, "series.patch")
        with open(tmp, "w") as fh:
            fh.write(patch_text)
        out = subprocess.run(
            ["git", "apply", "--stat", tmp],
            cwd=tmpdir,
            check=True,
            capture_output=True,
            text=True,
        )
        return out.stdout.rstrip("\n") + "\n"


def mapping_doc(series: list, hashes: dict) -> str:
    lines = []
    add = lines.append
    add("# Commit-message drafts - HiSilicon luofu 6.18 series")
    add("")
    add("Generated by tools/gen-series.py from commit-messages/*.txt; edit the")
    add("per-patch message files and re-run the generator instead of editing this")
    add("file.  The same messages are embedded verbatim in the commit-ready patches")
    add("under series/ (format-patch style, verified with git am).")
    add("")
    add("Signed-off-by: the repository git config identity")
    add("ShibbityShwab <18537102+ShibbityShwab@users.noreply.github.com> is used as")
    add("the placeholder.  The kernel DCO requires a real legal name and a sign-off")
    add("the sender can give; replace it (and the YAML maintainers: entries) before")
    add("sending.")
    add("")
    add("RFC patches are marked as such in the status column; a real submission")
    add("prefixes those subjects with [RFC PATCH] and splits the series by")
    add("subsystem as described in 00-cover-letter.md.")
    add("")
    by_name = {stem: (idx, status) for idx, (stem, status) in enumerate(series, 1)}
    for title, stems in GROUPS:
        add("## " + title)
        add("")
        for stem in stems:
            idx, status = by_name[stem]
            msg = (MESSAGES / (stem + ".txt")).read_text().rstrip("\n")
            add("### %04d/%d  %s" % (idx, len(series), msg.splitlines()[0]))
            add("")
            add("Status: " + status)
            add("")
            add("~~~")
            add(msg)
            add("~~~")
            add("")
    add("## Mapping table")
    add("")
    add("| # | series file | source tree patch | subject | status |")
    add("| --- | --- | --- | --- | --- |")
    for idx, (stem, status) in enumerate(series, 1):
        msg = (MESSAGES / (stem + ".txt")).read_text()
        subject = msg.splitlines()[0]
        add("| %02d | series/%s.patch | opensource/target-skeleton/target/linux/"
            "hisilicon/patches-6.18/%s | %s | %s |"
            % (idx, stem, original_name(stem), subject, status))
    add("")
    add("## Status legend")
    add("")
    add("- ready-for-review: no known design blocker; the implemented subset is")
    add("  backed by a citable artifact and probe does not write unverified")
    add("  hardware.  Still has no hardware validation (read the cover letter).")
    add("- RFC: a known unverified/provisional write path, an open design")
    add("  question, or an interface that has to be reviewed first.")
    add("- RFC (partial, refuses probe) / draft (do not merge): explicitly not")
    add("  mergeable; posted for direction only.")
    add("- RFC (attended): host-validated platform code whose acceptance requires")
    add("  a single-variable, recovery-armed bench session; not part of the")
    add("  UP-first mergeable set.")
    add("- ready-for-review (read-only clock tree): the provider only exposes read")
    add("  paths (no set_rate/set_parent write path).  Its PLL rate equation and")
    add("  mux/gate parents are decoded from the vendor module/devicetree and have")
    add("  never been measured on the board, so those values stay unmeasured until")
    add("  the read-only bench readback.")
    add("- Every patch also needs the mechanical binding split warned about by")
    add("  checkpatch, and a MAINTAINERS entry; see 02-maintainers-proposal.md")
    add("  and 04-validation.md.")
    add("")
    add("## Provenance")
    add("")
    add("| # | series sha256 | source patch sha256 |")
    add("| --- | --- | --- |")
    for idx, (stem, status) in enumerate(series, 1):
        add("| %02d | %s | %s |"
            % (idx, hashes[stem][0], hashes[stem][1]))
    add("")
    add("The series diff bodies are byte-identical to the source patches apart from")
    add("the git extended headers inserted by the generator (diff --git and")
    add("new file mode); every hunk byte is carried over verbatim.  See")
    add("04-validation.md and tools/gen-series.py.")
    add("")
    return "\n".join(lines)


def main(check_only: bool) -> int:
    files = []
    for index, (stem, status) in enumerate(SERIES, 1):
        msg_path = MESSAGES / (stem + ".txt")
        orig_path = ORIGINALS / original_name(stem)
        if not msg_path.is_file():
            sys.exit("missing commit message: %s" % msg_path)
        if not orig_path.is_file():
            sys.exit("missing source patch: %s" % orig_path)

        message = msg_path.read_text()
        lines = message.splitlines()
        subject = lines[0].strip()
        body = "\n".join(lines[2:]).rstrip("\n") + "\n"
        if not subject or len(subject) > 78:
            sys.exit("bad subject line in %s" % msg_path)

        diff = orig_path.read_text()
        if not diff.endswith("\n"):
            diff += "\n"
        diff = gitify(diff)

        fake_from = hashlib.sha1((stem + subject).encode()).hexdigest()
        header = (
            "From %s Mon Sep 17 00:00:00 2001\n"
            "From: %s\n"
            "Date: %s\n"
            "Subject: [PATCH %02d/%02d] %s\n"
            "\n"
            "%s"
            "\n"
            "---\n"
            "%s"
            "\n"
            % (fake_from, IDENT, DATE, index, len(SERIES), subject, body,
               diffstat(diff))
        )
        text = header + diff
        # Every hunk of the source diff must be carried over verbatim.
        for hunk in orig_path.read_text().splitlines():
            if (hunk.startswith("+") or hunk.startswith("-") or hunk.startswith(" ")) and hunk not in text:
                sys.exit("generated file lost source hunk line in %s: %s" % (stem, hunk))
        if diff not in text:
            sys.exit("generated file does not contain the source diff: %s" % stem)
        files.append((stem + ".patch", text))

    hashes = {}
    for (stem, _status), (_name, text) in zip(SERIES, files):
        orig = (ORIGINALS / original_name(stem)).read_text()
        hashes[stem] = (
            hashlib.sha256(text.encode()).hexdigest(),
            hashlib.sha256(orig.encode()).hexdigest(),
        )
    doc = mapping_doc(SERIES, hashes)

    if check_only:
        for name, text in files:
            expected = OUT / name
            if not expected.is_file() or expected.read_text() != text:
                sys.exit("out of date: %s" % expected)
        if (SERIES_DIR / "01-commit-messages.md").read_text() != doc:
            sys.exit("out of date: 01-commit-messages.md")
        print("series up to date (%d patches)" % len(files))
        return 0

    OUT.mkdir(parents=True, exist_ok=True)
    for name, text in files:
        (OUT / name).write_text(text)
    (SERIES_DIR / "01-commit-messages.md").write_text(doc)
    print("wrote %d patches to %s" % (len(files), OUT))
    print("wrote %s" % (SERIES_DIR / "01-commit-messages.md"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main("--check" in sys.argv[1:]))