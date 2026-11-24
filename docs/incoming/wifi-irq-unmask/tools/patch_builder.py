#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""patch_builder.py - build firmware-IRQ patch candidates for the Hi5622V100 Wi-Fi image.

Lane wifi-irq-unmask (shared task task-90). Host-only: reads the LOCAL vendor blob in place,
verifies it, and writes PATCHED COPIES into a directory OUTSIDE this repository (default
/tmp/hi5622-patched). No vendor firmware bytes are written into the repository; the manifest
contains only the patch recipe and hashes.

Patch candidates (see REPORT.md sections 4-5 for the full ranking and expected observables):
  PA  initial task CPSR I-bit clear (task create 0xC7218)           - control
  PB  remove cpsid i at the head of the scheduler switch 0xC24DE    - control
  PC  clear I in the ARM arch context-save SPSR (0x102C14)          - control
  PD  redirect the boot-task success epilogue into a permanent H2D
      dispatcher (0xC18AC / wrapper 0x40294) poll loop              - recommended

Usage:
  patch_builder.py --list
  patch_builder.py --patch PD
  patch_builder.py --patch PA --patch PC
  patch_builder.py --all
  patch_builder.py --selftest
Options: --fw PATH (or $HI5622_FW), --out DIR (default /tmp/hi5622-patched), --allow-other-source
"""
import argparse
import hashlib
import json
import os
import sys

try:
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB
except Exception:  # pragma: no cover
    print("error: capstone is required (project venv .provision-venv/bin/python)")
    raise SystemExit(2)

BASE = 0x40000
DEFAULT_FW = "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin"
DEFAULT_OUT = "/tmp/hi5622-patched"
FW_SHA256 = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_LEN = 928920

# NOTE: original windows are identified by sha256 only; the bytes are read from the local
# blob at run time and never embedded here.
PATCHES = {
    "PA": {
        "about": "initial task CPSR I bit clear (task create 0xC7218)",
        "expected": "new tasks start with I=0 instead of 0xB3; predicted no-op because the "
                    "task-entry trampoline 0xC2B7A already executes cpsie i",
        "segments": [
            {"file": 0x872F6, "len": 2, "sha16": "51028cac2ede4fff",
             "patched": "3321", "isa": "thumb", "expect": "movs r1, #0x33"},
        ],
    },
    "PB": {
        "about": "remove cpsid i at the head of the scheduler switch 0xC24DE",
        "expected": "context switch no longer forces I=1 into the saved CPSR; small scheduler "
                    "critical-section window (spinlock 0xC2354 remains)",
        "segments": [
            {"file": 0x824F8, "len": 2, "sha16": "560de662284ac420",
             "patched": "00bf", "isa": "thumb", "expect": "nop"},
        ],
    },
    "PC": {
        "about": "clear I in the ARM arch context-save SPSR (0x102C14)",
        "expected": "every switched-out context resumes with IRQs enabled; keeps the scheduler "
                    "cpsid i, so it is safer than PB",
        "segments": [
            {"file": 0xC2C20, "len": 8, "sha16": "1498affc745102d7",
             "patched": "8040c4e300f020e3", "isa": "arm",
             "expect": "bic r4, r4, #0x80 ; nop"},
        ],
    },
    "PD": {
        "about": "redirect the boot-task success epilogue into a permanent H2D dispatcher "
                 "(0x40294 -> 0xC18AC) poll loop at the repurposed handshake-failure path",
        "expected": "after the golden boot + ready pair, the firmware polls the H2D mailbox "
                    "dispatcher forever: a host out[0]=BIT(3) + out[2]|=1 post is consumed - "
                    "out[0] clears, out[5] reads 1, out[2] reads 8",
        "segments": [
            {"file": 0x7610, "len": 6, "sha16": "e81740ccb394d442",
             "patched": "f8f740fefce7", "isa": "thumb", "expect": "bl #0x40294 ; b #0x47610"},
            {"file": 0x916A, "len": 4, "sha16": "c5975cf4bd702f88",
             "patched": "fef751ba", "isa": "thumb", "expect": "b.w #0x47610"},
        ],
    },
}


def decode(isa, data, addr):
    md = Cs(CS_ARCH_ARM, CS_MODE_ARM if isa == "arm" else CS_MODE_THUMB)
    return " ; ".join(("%s %s" % (i.mnemonic, i.op_str)).strip() for i in md.disasm(data, addr))


def refuse_out_inside_repo(out):
    here = os.path.dirname(os.path.abspath(__file__))
    # .../opensource/docs/incoming/wifi-irq-unmask/tools -> opensource root
    opensource_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(here))))
    out_abs = os.path.abspath(out)
    if out_abs == opensource_root or out_abs.startswith(opensource_root + os.sep):
        raise SystemExit("refusing output inside the opensource submodule (%s); choose e.g. /tmp" % out_abs)


def verify_source(blob, allow_other):
    sha = hashlib.sha256(blob).hexdigest()
    if len(blob) != FW_LEN or sha != FW_SHA256:
        msg = "source blob is not the pinned vendor image: len=%d sha256=%s" % (len(blob), sha)
        if not allow_other:
            raise SystemExit(msg + "\n(refusing; pass --allow-other-source only for a known variant)")
        print("warning:", msg)


def apply_patches(blob, names):
    out = bytearray(blob)
    recipe = []
    for name in names:
        p = PATCHES[name]
        for seg in p["segments"]:
            off, n = seg["file"], seg["len"]
            src = bytes(blob[off:off + n])
            if hashlib.sha256(src).hexdigest()[:16] != seg["sha16"]:
                raise SystemExit("%s: source window hash mismatch at file 0x%06X" % (name, off))
            patched = bytes.fromhex(seg["patched"])
            if len(patched) != n:
                raise SystemExit("%s: internal error: patched length" % name)
            out[off:off + n] = patched
            recipe.append({"file": off, "cpu": off + BASE, "len": n,
                           "source_sha16": seg["sha16"],
                           "patched_hex": seg["patched"],
                           "expect": seg["expect"]})
    return bytes(out), recipe


def cmd_list():
    for name in sorted(PATCHES):
        p = PATCHES[name]
        print("%s: %s" % (name, p["about"]))
        print("   expected: %s" % p["expected"])
        for seg in p["segments"]:
            print("   file 0x%06X cpu 0x%08X len %d -> %s (want %s)"
                  % (seg["file"], seg["file"] + BASE, seg["len"], seg["patched"],
                     seg["expect"]))
    return 0


def cmd_build(args):
    if not os.path.exists(args.fw):
        print("error: vendor firmware not found:", args.fw)
        return 2
    blob = open(args.fw, "rb").read()
    verify_source(blob, args.allow_other_source)
    names = sorted(PATCHES) if args.all else args.patch
    if not names:
        print("nothing to do: pass --patch NAME (repeatable) or --all")
        return 2
    for n in names:
        if n not in PATCHES:
            print("error: unknown patch %r (use --list)" % n)
            return 2
    refuse_out_inside_repo(args.out)
    os.makedirs(args.out, exist_ok=True)
    stem = os.path.splitext(os.path.basename(args.fw))[0]
    manifest = {"source": {"path": args.fw, "sha256": hashlib.sha256(blob).hexdigest(),
                           "len": len(blob)},
                "note": "patched copies only; the vendor blob stays local and is not committed",
                "builds": []}
    names = sorted(names)
    for name in names:
        patched, recipe = apply_patches(blob, [name])
        outp = os.path.join(args.out, "%s.%s.bin" % (stem, name))
        with open(outp, "wb") as f:
            f.write(patched)
        entry = {"patch": name, "about": PATCHES[name]["about"],
                 "out": outp, "out_sha256": hashlib.sha256(patched).hexdigest(),
                 "segments": recipe}
        manifest["builds"].append(entry)
        print("built", name, "->", outp,
              "sha256", entry["out_sha256"][:16] + "...")
        for r in recipe:
            print("   file 0x%06X cpu 0x%08X  %s" % (r["file"], r["cpu"], r["patched_hex"]))
    if args.all:
        # combined build too (useful for the mask-control session)
        combined, recipe = apply_patches(blob, names)
        outp = os.path.join(args.out, "%s.ALL.bin" % stem)
        with open(outp, "wb") as f:
            f.write(combined)
        manifest["builds"].append({"patch": "ALL", "about": "all candidates combined",
                                   "out": outp,
                                   "out_sha256": hashlib.sha256(combined).hexdigest(),
                                   "segments": recipe})
        print("built ALL ->", outp)
    with open(os.path.join(args.out, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    print("manifest:", os.path.join(args.out, "manifest.json"))
    return 0


def cmd_selftest(args):
    if not os.path.exists(args.fw):
        print("error: vendor firmware not found:", args.fw)
        return 2
    blob = open(args.fw, "rb").read()
    verify_source(blob, False)
    checks = []
    for name in sorted(PATCHES):
        patched, _ = apply_patches(blob, [name])
        for seg in PATCHES[name]["segments"]:
            off, n = seg["file"], seg["len"]
            got = patched[off:off + n]
            want = bytes.fromhex(seg["patched"])
            checks.append(("%s file 0x%06X applied" % (name, off), got == want))
            dec = decode(seg["isa"], got, off + BASE)
            checks.append(("%s decode %r" % (name, dec), dec == seg["expect"]))
        # untouched bytes elsewhere must be identical
        checks.append(("%s leaves rest identical" % name,
                       patched[:0x100] == blob[:0x100]))
    # combined build must apply all
    allp, _ = apply_patches(blob, sorted(PATCHES))
    checks.append(("combined build applies all", len(allp) == len(blob)))
    passed = sum(1 for _, ok in checks if ok)
    for n, ok in checks:
        if not ok:
            print("FAIL:", n)
    print("selftest: %d/%d PASS" % (passed, len(checks)))
    return 0 if passed == len(checks) else 1


def main():
    ap = argparse.ArgumentParser(description="patch builder for the Hi5622V100 firmware "
                                             "(wifi-irq-unmask lane; writes outside the repo)")
    ap.add_argument("--fw", default=os.environ.get("HI5622_FW", DEFAULT_FW))
    ap.add_argument("--out", default=os.environ.get("HI5622_PATCH_OUT", DEFAULT_OUT))
    ap.add_argument("--patch", action="append", metavar="NAME",
                    help="patch name (repeatable); see --list")
    ap.add_argument("--all", action="store_true", help="build every candidate + combined")
    ap.add_argument("--list", action="store_true", help="list candidates")
    ap.add_argument("--selftest", action="store_true", help="verify and decode all patches")
    ap.add_argument("--allow-other-source", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return cmd_selftest(args)
    if args.list or (not args.patch and not args.all):
        cmd_list()
        return 0 if args.list else 2
    return cmd_build(args)


if __name__ == "__main__":
    raise SystemExit(main())
