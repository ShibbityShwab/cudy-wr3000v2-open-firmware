#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""make-patch.py - the wifi-rc-impl-2 base pin + patch application tool (host-only).

The RC-2 patch (patches/0001-rate-ctl-2.patch) is the committed source of truth:
it rebases the rate-control surface onto the IMAGE #3 merged driver source tree
and adds the driver-enforced corrected C2 precondition.  This tool:

  --verify           clean-stage the pinned base tree, apply the committed patch,
                     check every result hash and the patch identity against
                     evidence/patch-apply.txt, and leave the applied tree in
                     /tmp/rc2-build (or --dest).
  --apply DIR        same staging, applied into DIR (used by the build script).
  --regen            regenerate the patch from a base/patched tree pair (the
                     lane's edited work tree) and normalize the diff headers so
                     the patch bytes are host-independent.

The base tree is local scratch: the project never commits the vendor or built
artifacts.  Default BASE_TREE=/tmp/wifi-image-3-module (the IMAGE #3 merged
tree; see ../BUILD-NOTES.md and ../evidence/base-pin.txt).  No device access.
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys

LANE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATCH = os.path.join(LANE, "patches", "0001-rate-ctl-2.patch")
EVID = os.path.join(LANE, "evidence", "patch-apply.txt")
BASE_PIN = os.path.join(LANE, "evidence", "base-pin.txt")
DEFAULT_BASE = os.environ.get("BASE_TREE", "/tmp/wifi-image-3-module")
ART_SUFFIX = (".o", ".ko", ".mod", ".mod.c", "build.log")
ART_NAMES = ("Module.symvers", "modules.order", ".modules.order.cmd")


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_pin():
    pin = {}
    with open(BASE_PIN) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) == 2 and len(parts[0]) == 64:
                pin[parts[1]] = parts[0]
    return pin


def clean_stage(src, dst):
    if os.path.exists(dst):
        shutil.rmtree(dst)
    os.makedirs(dst)
    n = 0
    for name in sorted(os.listdir(src)):
        p = os.path.join(src, name)
        if not os.path.isfile(p) or name.startswith("."):
            continue
        if name.endswith(ART_SUFFIX) or name in ART_NAMES:
            continue
        shutil.copyfile(p, os.path.join(dst, name))
        n += 1
    return n


def manifest_hash(tree):
    # Exactly the command recorded in evidence/base-pin.txt:
    #   (cd <base tree>; LC_ALL=C ls -1 | sort | xargs sha256sum | sha256sum)
    names = sorted(n for n in os.listdir(tree)
                   if not n.startswith(".")
                   and os.path.isfile(os.path.join(tree, n)))
    lines = ["%s  %s\n" % (sha256_file(os.path.join(tree, n)), n)
             for n in names]
    return hashlib.sha256("".join(lines).encode()).hexdigest()


def check_pin(tree):
    pin = read_pin()
    bad = 0
    for name, want in sorted(pin.items()):
        p = os.path.join(tree, name)
        got = sha256_file(p) if os.path.isfile(p) else "missing"
        if got != want:
            print("PIN FAIL %s: %s != %s" % (name, got, want))
            bad += 1
    want_manifest = None
    with open(BASE_PIN) as f:
        for line in f:
            if line.startswith("source_manifest_sha256 "):
                want_manifest = line.split()[1]
    got_manifest = manifest_hash(tree)
    if want_manifest and got_manifest != want_manifest:
        print("PIN FAIL source manifest %s != %s" % (got_manifest, want_manifest))
        bad += 1
    else:
        print("pin: base tree OK (%d pinned files + manifest %s)"
              % (len(pin), got_manifest[:16]))
    return bad == 0


def apply_patch(dest):
    patch_sha = sha256_file(PATCH)
    r = subprocess.run(["patch", "-p1", "--no-backup-if-mismatch",
                        "-i", PATCH], cwd=dest,
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout)
        print(r.stderr)
        sys.exit("patch apply failed")
    if "fuzz" in (r.stdout + r.stderr).lower():
        sys.exit("patch applied with fuzz - refusing")
    print("patch: applied cleanly (%d bytes, sha256 %s)" % (os.path.getsize(PATCH), patch_sha))
    return patch_sha


def read_evidence():
    ev = {"files": {}}
    with open(EVID) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) == 2 and len(parts[0]) == 64:
                ev["files"][parts[1]] = parts[0]
            else:
                ev[parts[0]] = parts[1] if len(parts) > 1 else ""
    return ev


def verify_applied(tree, patch_sha):
    if not os.path.isfile(EVID):
        print("verify: no committed evidence yet (run with --evidence to write it)")
        return True
    ev = read_evidence()
    bad = 0
    if ev.get("patch_sha256") and ev["patch_sha256"] != patch_sha:
        print("VERIFY FAIL patch sha256 %s != %s" % (patch_sha, ev["patch_sha256"]))
        bad += 1
    for name, want in sorted(ev["files"].items()):
        p = os.path.join(tree, name)
        got = sha256_file(p) if os.path.isfile(p) else "missing"
        if got != want:
            print("VERIFY FAIL %s: %s != %s" % (name, got, want))
            bad += 1
    print("verify: %d patched files match the committed hashes (patch sha256 %s)"
          % (len(ev["files"]) - bad, patch_sha))
    return bad == 0


def regen(base, patched):
    import tempfile

    root = tempfile.mkdtemp(prefix="rc2-patchgen-")
    try:
        a = os.path.join(root, "a")
        b = os.path.join(root, "b")
        clean_stage(base, a)
        clean_stage(patched, b)
        r = subprocess.run(["diff", "-ruN", "a", "b"], cwd=root,
                           capture_output=True, text=True)
        out = []
        for line in r.stdout.splitlines(True):
            if line.startswith(("--- ", "+++ ")) and "\t" in line:
                line = line.split("\t", 1)[0] + "\n"
            out.append(line)
        data = "".join(out)
        with open(PATCH, "w") as f:
            f.write(data)
        print("regen: %d bytes, sha256 %s, %d files"
              % (len(data), hashlib.sha256(data.encode()).hexdigest(),
                 data.count("diff -ruN")))
    finally:
        shutil.rmtree(root, ignore_errors=True)


def write_evidence(tree, patch_sha):
    names = sorted(l.split()[3][2:]
                   for l in open(PATCH) if l.startswith("diff -ruN")
                   if l.split()[3].startswith("b/"))
    lines = ["# wifi-rc-impl-2 patch application evidence (generated by tools/make-patch.py --regen --verify)",
             "# base: evidence/base-pin.txt source tree + patches/0001-rate-ctl-2.patch",
             "patch_sha256 " + patch_sha,
             "applied_files %d" % len(names)]
    for name in names:
        lines.append("%s  %s" % (sha256_file(os.path.join(tree, name)), name))
    with open(EVID, "w") as f:
        f.write("\n".join(lines) + "\n")
    print("evidence: wrote %s (%d files)" % (EVID, len(names)))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--base", default=DEFAULT_BASE)
    ap.add_argument("--dest", default="/tmp/rc2-build")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--regen", action="store_true")
    ap.add_argument("--regen-patched", default="/tmp/rc2-work")
    ap.add_argument("--evidence", action="store_true")
    ap.add_argument("--manifest", action="store_true",
                    help="print the locale-independent source manifest of --base")
    a = ap.parse_args()
    if a.manifest:
        names = sorted(n for n in os.listdir(a.base)
                       if not n.startswith(".")
                       and os.path.isfile(os.path.join(a.base, n)))
        sys.stdout.write("".join(
            "%s  %s\n" % (sha256_file(os.path.join(a.base, n)), n)
            for n in names))
        return 0
    if not (a.verify or a.apply or a.regen):
        ap.error("choose --verify, --apply, --regen or --manifest")
    if not os.path.isdir(a.base):
        sys.exit("base tree missing: %s (set BASE_TREE to the IMAGE #3 merged "
                 "driver source tree; see BUILD-NOTES.md)" % a.base)
    if a.regen:
        regen(a.base, a.regen_patched)
    if a.apply:
        n = clean_stage(a.base, a.dest)
        print("stage: %d source files -> %s" % (n, a.dest))
        apply_patch(a.dest)
        return 0
    if a.verify:
        n = clean_stage(a.base, a.dest)
        print("stage: %d source files -> %s" % (n, a.dest))
        ok = check_pin(a.dest)
        patch_sha = apply_patch(a.dest)
        ok = verify_applied(a.dest, patch_sha) and ok
        if a.evidence:
            write_evidence(a.dest, patch_sha)
        print("RESULT:", "PASS" if ok else "FAIL")
        return 0 if ok else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
