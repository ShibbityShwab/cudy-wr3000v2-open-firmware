#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
verify_merge.py - reproduce and verify the wifi-dp-air merge (host-only).

The merge source of truth is:
  base  = ../../wifi-scan-complete/src   (the hardware-validated provider
            tree: wifi-scan-ap + wifi-vap-start provider patch + scan-complete)
  lane  = ../src                         (this lane's merged tree)

This tool:
  1. regenerates the merge patch from (base -> lane) in memory and compares it
     byte-for-byte with patches/0001-merge-dp-txrx-into-provider.patch, so a
     committed patch can never silently drift from the committed sources;
  2. stages a copy of the base tree, applies the committed patch with
     "patch -p1", and compares every resulting file with the lane tree;
  3. enforces the merge scope: the changed/added file set is exactly the
     documented one (no provider file outside the merge may differ).

No kernel tree, no device access, no network, no vendor artifact.
Exit 0 = PASS; any drift/scope violation is a hard failure.
"""
import difflib
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
LANE = os.path.dirname(HERE)
SRC = os.path.join(LANE, "src")
BASE = os.path.normpath(os.path.join(LANE, "..", "wifi-scan-complete", "src"))
PATCH = os.path.join(LANE, "patches",
                     "0001-merge-dp-txrx-into-provider.patch")

# Files this merge is allowed to touch/add (exactly).
EXPECTED_ADDED = {
    "dp_rings.c", "dp_rings.h",                 # E1 portable geometry
    "hi5622_dp.c", "hi5622_dp.h",               # E1 MAC data-plane bring-up
    "dp_txrx.c", "dp_txrx.h",                   # txrx portable TX/RX logic
    "hi5622_dp_txrx.c", "hi5622_dp_txrx.h",     # txrx kernel extension
}
EXPECTED_MODIFIED = {
    "Kbuild", "main.c", "hi5622_driver.h", "hi5622_hcc.h",
    "h2d_send.c", "h2d_send.h", "h2d_send_logic.c", "h2d_send_logic.h",
}

SOURCE_SUFFIXES = (".c", ".h", ".inc")


SKIP_GENERATED = (".mod.c", ".o.cmd", ".symvers.old")


def is_source(name: str) -> bool:
    if name.endswith(".mod.c"):
        return False
    return (name.endswith(SOURCE_SUFFIXES) or name in
            ("Kbuild", "Makefile", "cfg80211-provider.symvers", ".gitignore"))


def tree(root: str) -> dict:
    out = {}
    for name in sorted(os.listdir(root)):
        if not os.path.isfile(os.path.join(root, name)) or not is_source(name):
            continue
        with open(os.path.join(root, name), "rb") as fh:
            out[name] = fh.read()
    return out


def gen_patch(base: dict, lane: dict) -> str:
    chunks = []
    for name in sorted(set(base) | set(lane)):
        a = base.get(name)
        b = lane.get(name)
        if a == b:
            continue
        a_text = "" if a is None else a.decode("utf-8", "surrogateescape")
        b_text = "" if b is None else b.decode("utf-8", "surrogateescape")
        a_label = "/dev/null" if a is None else "a/" + name
        b_label = "/dev/null" if b is None else "b/" + name
        diff = difflib.unified_diff(a_text.splitlines(True),
                                    b_text.splitlines(True),
                                    fromfile=a_label, tofile=b_label)
        chunks.append("".join(diff))
    return "".join(chunks)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    emit = "--emit" in sys.argv[1:]
    if not os.path.isdir(BASE):
        print("SKIP: base provider tree not found at %s" % BASE)
        return 0
    base = tree(BASE)
    lane = tree(SRC)
    added = sorted(set(lane) - set(base))
    removed = sorted(set(base) - set(lane))
    modified = sorted(n for n in set(base) & set(lane) if base[n] != lane[n])
    problems = []

    # 1. scope
    if set(added) != EXPECTED_ADDED:
        problems.append("added-file scope drift: got %s want %s"
                        % (added, sorted(EXPECTED_ADDED)))
    if removed:
        problems.append("merge removed provider files: %s" % removed)
    if set(modified) != EXPECTED_MODIFIED:
        problems.append("modified-file scope drift: got %s want %s"
                        % (modified, sorted(EXPECTED_MODIFIED)))

    # 2. committed patch == regenerated patch (no drift)
    want = gen_patch(base, lane)
    if emit and not problems:
        os.makedirs(os.path.dirname(PATCH), exist_ok=True)
        with open(PATCH, "w", encoding="utf-8") as fh:
            fh.write(want)
        print("EMITTED: %s (%d B sha256 %s)"
              % (PATCH, len(want.encode("utf-8")),
                 sha(want.encode("utf-8"))))
        return 0
    if not os.path.isfile(PATCH):
        problems.append("missing committed patch %s" % PATCH)
    else:
        with open(PATCH, "r", encoding="utf-8") as fh:
            got = fh.read()
        if got != want:
            problems.append("committed patch drifts from (base -> src); "
                            "regenerate with: python3 tools/verify_merge.py "
                            "--emit")
            with open(os.path.join(tempfile.gettempdir(),
                                   "wifi-dp-air.expected.patch"),
                      "w", encoding="utf-8") as fh:
                fh.write(want)

    # 3. apply the committed patch to a staged base copy and compare trees
    staged = tempfile.mkdtemp(prefix="wifi-dp-air-verify-")
    try:
        stage = os.path.join(staged, "src")
        shutil.copytree(BASE, stage)
        if os.path.isfile(PATCH):
            with open(PATCH, "rb") as fh:
                proc = subprocess.run(["patch", "-p1", "-s"], cwd=stage,
                                      stdin=fh, capture_output=True)
            if proc.returncode != 0:
                problems.append("committed patch does not apply to the base "
                                "tree: %s"
                                % proc.stderr.decode("utf-8", "replace")[:300])
            else:
                got = tree(stage)
                if got != lane:
                    diff = [n for n in sorted(set(got) | set(lane))
                            if got.get(n) != lane.get(n)]
                    problems.append("patched base tree != lane src: %s" % diff)
    finally:
        shutil.rmtree(staged, ignore_errors=True)

    print("base  : %s (%d source files)" % (BASE, len(base)))
    print("lane  : %s (%d source files)" % (SRC, len(lane)))
    print("added : %s" % ", ".join(added))
    print("mod   : %s" % ", ".join(modified))
    print("patch : %d B sha256 %s" % (os.path.getsize(PATCH)
                                     if os.path.isfile(PATCH) else 0,
                                     sha(want.encode("utf-8"))))
    for name in ("src",):
        print("merged tree sha256 (fingerprint): %s"
              % sha(b"".join(n.encode() + b"\0" + lane[n] + b"\0"
                             for n in sorted(lane))))
    if problems:
        for p in problems:
            print("FAIL: " + p)
        print("SUMMARY: %d problem(s)" % len(problems))
        return 1
    print("PASS: merge patch reproduces the lane tree; scope + drift checks OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
