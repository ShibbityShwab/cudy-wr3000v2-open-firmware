#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""pd4_variants.py - behavioural variants of the PD4 firmware patch (host-only).

Lane: wifi-fw-audit (shared task task-115).  This tool takes the PD4 recipe, rebuilds the
published PD4 image in memory, applies SMALL behavioural deltas to the same cave stub,
decodes the result and prints a manifest (hashes + our bytes only).  It never writes
vendor bytes anywhere and refuses an output directory inside the opensource/ repository.

Why: the audit (REPORT.md sec.5) found one unresolved question - the stub compares the
hcc instance's handler-table pointer field (CPU 0x10C1B0) with the static value
0x00118D68, but no image writer of that field was found, so the "consume-only" branch may
in fact be the ALWAYS-taken branch.  Two 2-4 byte variants make the two hypotheses
explicit without touching the dispatcher contract:

    PD4-NOSENT   force the live-table branch   (cmp/beq patched to nops)
    PD4-ALWAYS0  force the sentinel branch     (beq -> b, deterministic consume-only)

Both keep the cave, the entry hunk, the init guard and the wrapper call identical to
PD4; they differ only in which dispatch table the dispatcher sees.  They are DIAGNOSTIC
variants, not new validated artifacts: firing them is a separate attended decision.

Also specified (NOT built here; would need a fresh instruction encoder):
    PD4-MIN      drop the sentinel branch entirely: stub 32 B instead of 64 B
                 (patch delta 4+32 = 36 B vs 68 B) - strictly smaller, no ROM-RAZ
                 dependency, real handler table; the safest long-term shape IF the
                 lead/mailbox handler path is wanted.
    PD4-LITE     no firmware-code call: the stub performs the dispatcher's three device
                 writes itself (ack=1 CA 0x400392F0, clear out[0] CA 0x40039010,
                 doorbell=8 CA 0x400392D4); 40 B stub, patch delta 44 B; only valid if
                 mailbox handler execution is proven unnecessary, and it writes CA
                 0x400392F0 from firmware code (same as the dispatcher does).
    PD-ONESHOT   branch to a single "bl 0x40294; <epilogue island>" at the epilogue -
                 useless for the live protocol (the host posts H2D frames AFTER boot,
                 so a one-shot call sees nothing); rejected as a non-variant.

Usage:
    PY=.provision-venv/bin/python
    $PY tools/pd4_variants.py --selftest
    $PY tools/pd4_variants.py --table
    $PY tools/pd4_variants.py --build /tmp/pd4-variants          # all diagnostic variants
    $PY tools/pd4_variants.py --build /tmp/pd4-variants --json
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import verify_pd4 as V  # noqa: E402  (same lane: shared pinned recipe/geometry helpers)

try:
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_MCLASS
except Exception as exc:  # pragma: no cover
    print("FATAL: capstone required: %s" % exc)
    sys.exit(2)

RECIPE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                      "recipes", "PD4.recipe.json")
LOOP = V.LOOP_CPU
ENTRY = V.ENTRY_CPU

# variant hunks are expressed RELATIVE TO THE PD4 PATCHED IMAGE (our own bytes)
VARIANTS = [
    {
        "name": "PD4-NOSENT",
        "kind": "diagnostic-build",
        "what": "force the live handler-table branch: the sentinel compare becomes two nops",
        "hunks": [{"file": 0xC8256, "len": 4, "from_hex": "a34202d0", "to_hex": "c046c046",
                   "expect": "mov r8, r8 ; mov r8, r8 (nop x2)"}],
        "decode": [(LOOP + 0x16, "mov r8, r8"), (LOOP + 0x18, "mov r8, r8")],
        "delta_bytes": 4,
        "risk": "the dispatcher now always uses [instance+0x20]=0x118D68; if that RAM table "
                "is populated (registration helper writes ids 1/3/5/6 at boot) the real "
                "vendor mailbox handlers run (id3 = SR announce consumer; id5 = BUG trap, "
                "only reachable by a pending bit 5 which the host must never post)",
        "rank": 1,
    },
    {
        "name": "PD4-ALWAYS0",
        "kind": "diagnostic-build",
        "what": "force the sentinel/consume-only branch deterministically (beq -> b)",
        "hunks": [{"file": 0xC8258, "len": 2, "from_hex": "02d0", "to_hex": "02e0",
                   "expect": "b #0x108260"}],
        "decode": [(LOOP + 0x18, "b #0x108260")],
        "delta_bytes": 2,
        "risk": "mailbox messages are acknowledged/cleared but NO firmware mailbox handler "
                "runs; makes explicit the behaviour suspected under hypothesis H1.  If the "
                "protocol still works under this image, handler execution was never needed "
                "for the measured duplex lap; if it breaks, H1 is refuted and PD4's live "
                "branch is the real one",
        "rank": 2,
    },
]

VARIANTS_SPEC_ONLY = [
    {
        "name": "PD4-MIN",
        "kind": "spec-only",
        "what": "drop the sentinel branch and its two literals entirely; same guard, same "
                "wrapper call, real handler table always",
        "size": "stub 32 B (22 B code + 2 B pad + 8 B pool) instead of 64 B; patch delta "
                "4+32 = 36 B instead of 68 B; same 2 segments",
        "risk": "requires a fresh stub encoding (not built in this lane); removes the "
                "ROM-RAZ dependency and the H1/H2 ambiguity - the recommended long-term "
                "shape once a bench decision wants the live table",
        "rank": 3,
    },
    {
        "name": "PD4-LITE",
        "kind": "spec-only",
        "what": "never call firmware code: the stub performs the dispatcher's own three "
                "device writes (ack=1 CA 0x400392F0, clear out[0] CA 0x40039010, "
                "doorbell=8 CA 0x400392D4) when out[0] is pending",
        "size": "stub 40 B; patch delta 44 B; 2 segments",
        "risk": "only valid if handler execution is proven unnecessary; writes CA "
                "0x400392F0 from firmware code (the dispatcher does the same) - still a "
                "firmware patch, and it loses the vendor handler dispatch",
        "rank": 4,
    },
    {
        "name": "PD-ONESHOT",
        "kind": "rejected",
        "what": "replace the epilogue with one 'bl 0x40294' and branch to a spare epilogue "
                "island; no poll loop",
        "size": "4 B bl + 4 B b.w (8 B, 2 segments) - smallest possible",
        "risk": "functionally useless for the live protocol: the dispatcher is called once at "
                "boot when out[0] is still empty, and the host posts H2D frames after the "
                "boot; the vendor mailbox consumer must be reachable at frame time. "
                "Rejected (not a candidate)",
        "rank": 5,
    },
]


def decode_stub_at(img: bytes, cpu: int, n: int):
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
    return [(i.address, ("%s %s" % (i.mnemonic, i.op_str)).strip())
            for i in md.disasm(img[cpu - V.CPU_BASE:cpu - V.CPU_BASE + n], cpu)]


def apply_variant(pd4: bytes, variant: dict) -> bytes:
    out = bytearray(pd4)
    for h in variant["hunks"]:
        f, ln = int(h["file"]), int(h["len"])
        cur = bytes(out[f:f + ln])
        if cur != bytes.fromhex(h["from_hex"]):
            raise ValueError("%s: hunk @%#x expected %s got %s" %
                             (variant["name"], f, h["from_hex"], cur.hex()))
        if len(bytes.fromhex(h["to_hex"])) != ln:
            raise ValueError("%s: replacement length mismatch" % variant["name"])
        out[f:f + ln] = bytes.fromhex(h["to_hex"])
    return bytes(out)


def build_all(fw_path: str, recipe_path: str) -> dict:
    fw = open(fw_path, "rb").read()
    recipe = V.load_recipe(recipe_path)
    pd4, _ = V.build_patched(fw, recipe)
    pd4_sha = hashlib.sha256(pd4).hexdigest()
    if pd4_sha != recipe["image_sha256"]:
        raise SystemExit("FATAL: rebuilt PD4 sha %s != recipe %s" % (pd4_sha, recipe["image_sha256"]))
    res = {"source_sha256": hashlib.sha256(fw).hexdigest(), "pd4_sha256": pd4_sha,
           "variants": []}
    for var in VARIANTS:
        img = apply_variant(pd4, var)
        dec = []
        for h in var["hunks"]:
            dec += decode_stub_at(img, h["file"] + V.CPU_BASE, h["len"])
        # every diff vs PD4 must stay inside the variant hunks
        runs = V.diff_windows(pd4, img)
        hunks = [(int(h["file"]), int(h["file"]) + int(h["len"]) - 1) for h in var["hunks"]]
        outside = [r for r in runs if not any(a <= r[0] and r[1] <= b for a, b in hunks)]
        if outside:
            raise SystemExit("FATAL: %s diff escaped its hunks: %s" % (var["name"], outside))
        res["variants"].append({
            "name": var["name"], "kind": var["kind"], "what": var["what"],
            "hunks": var["hunks"], "changed_windows": [list(r) for r in runs],
            "changed_bytes": sum(e - s + 1 for s, e in runs),
            "sha256": hashlib.sha256(img).hexdigest(),
            "len": len(img), "decode": ["%#x: %s" % (a, t) for a, t in dec],
            "risk": var["risk"], "rank": var["rank"], "image": img,
        })
    return res


def selftest() -> int:
    ok = tot = 0

    def ck(desc, cond):
        nonlocal ok, tot
        tot += 1
        if cond:
            ok += 1
        else:
            print("FAIL selftest: %s" % desc)

    pd4_recipe = V.load_recipe(RECIPE)
    ck("recipe is PD4", pd4_recipe.get("name") == "PD4")
    ck("recipe image sha pinned", pd4_recipe["image_sha256"] ==
       "cfe24bfb194899f362bfcb0301d7e28a3b4dce0ef4e70b705d54a1f78d3037b1")
    # hunk source bytes as they appear in the PD4 patched image, by decode of the
    # published recipe stub bytes (our bytes only)
    stub = bytes.fromhex(pd4_recipe["segments"][1]["patched_hex"])
    entry = bytes.fromhex(pd4_recipe["segments"][0]["patched_hex"])
    ck("PD4 entry hunk decodes to the cave branch", entry.hex() == "bff069b8")
    ck("sentinel compare bytes at stub+0x16", stub[0x16:0x18].hex() == "a342")
    ck("sentinel branch bytes at stub+0x18", stub[0x18:0x1A].hex() == "02d0")
    ck("NOSENT from-bytes match", VARIANTS[0]["hunks"][0]["from_hex"] == stub[0x16:0x1A].hex())
    ck("ALWAYS0 from-bytes match", VARIANTS[1]["hunks"][0]["from_hex"] == stub[0x18:0x1A].hex())
    ck("NOSENT to-bytes are two nops", VARIANTS[0]["hunks"][0]["to_hex"] == "c046c046")
    ck("ALWAYS0 to-bytes are unconditional b", VARIANTS[1]["hunks"][0]["to_hex"] == "02e0")
    # decode checks using the patched stub bytes (no vendor blob needed)
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
    got = {(i.address): "%s %s" % (i.mnemonic, i.op_str) for i in md.disasm(stub, LOOP)}
    ck("cmp r3,r4 at stub+0x16", got.get(LOOP + 0x16) == "cmp r3, r4")
    ck("beq at stub+0x18", got.get(LOOP + 0x18) == "beq #0x108260")
    # forced branch target arithmetic: b at LOOP+0x18 with imm 2 -> LOOP+0x20
    ck("forced b target is the zero/sentinel path", LOOP + 0x18 + 4 + 2 * 2 == LOOP + 0x20)
    # spec-only entries are documented but not buildable
    names = [v["name"] for v in VARIANTS]
    ck("diagnostic variants are exactly NOSENT/ALWAYS0", names == ["PD4-NOSENT", "PD4-ALWAYS0"])
    ck("spec-only includes MIN/LITE/ONESHOT",
       [v["name"] for v in VARIANTS_SPEC_ONLY] == ["PD4-MIN", "PD4-LITE", "PD-ONESHOT"])
    print("pd4_variants selftest: %d/%d PASS" % (ok, tot))
    return 0 if ok == tot else 1


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="PD4 behavioural variants (host-only)")
    ap.add_argument("--fw", help="local vendor FIRMWARE.bin (default: search)")
    ap.add_argument("--recipe", default=RECIPE)
    ap.add_argument("--build", metavar="DIR", help="write variant images to DIR (outside opensource/)")
    ap.add_argument("--table", action="store_true", help="print the variant/option table")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args(argv)
    if args.selftest:
        return selftest()
    if args.table:
        print("VARIANT        KIND             DELTA  WHAT")
        for v in VARIANTS:
            print("%-14s %-16s %-6s %s" % (v["name"], v["kind"], "%dB" % v["delta_bytes"], v["what"]))
        for v in VARIANTS_SPEC_ONLY:
            print("%-14s %-16s %-6s %s" % (v["name"], v["kind"], "-", v["what"]))
        return 0
    fw = V.find_fw(args.fw)
    if not fw:
        print("FATAL: no local firmware blob; pass --fw PATH", file=sys.stderr)
        return 2
    res = build_all(fw, args.recipe)
    if args.build:
        outdir = os.path.abspath(args.build)
        if "/opensource/" in outdir:
            print("FATAL: refusing to write patched vendor images inside the repository: %s" % outdir,
                  file=sys.stderr)
            return 2
        os.makedirs(outdir, exist_ok=True)
        for var in res["variants"]:
            path = os.path.join(outdir, "FIRMWARE.%s.bin" % var["name"])
            with open(path, "wb") as fh:
                fh.write(var["image"])
            var["image_path"] = path
        manifest = os.path.join(outdir, "variants-manifest.json")
        clean = {k: v for k, v in res.items() if k != "variants"}
        clean["variants"] = [{k: v for k, v in var.items() if k != "image"}
                             for var in res["variants"]]
        with open(manifest, "w", encoding="utf-8") as fh:
            json.dump(clean, fh, indent=1)
        res["manifest_path"] = manifest
    if args.json:
        clean = {k: v for k, v in res.items() if k != "variants"}
        clean["variants"] = [{k: v for k, v in var.items() if k != "image"}
                             for var in res["variants"]]
        print(json.dumps(clean, indent=1))
    else:
        print("source sha256: %s" % res["source_sha256"])
        print("PD4    sha256: %s  (reference, validated artifact)" % res["pd4_sha256"])
        for var in res["variants"]:
            print("\n%s (%s) sha256 %s  delta %d B" % (var["name"], var["kind"],
                                                       var["sha256"], var["changed_bytes"]))
            print("  what: %s" % var["what"])
            for h in var["hunks"]:
                print("  hunk @file %#x len %d: %s -> %s  (%s)" %
                      (int(h["file"]), int(h["len"]), h["from_hex"], h["to_hex"], h["expect"]))
            print("  decode: %s" % "; ".join(var["decode"]))
            print("  risk: %s" % var["risk"])
        for v in VARIANTS_SPEC_ONLY:
            print("\n%s (%s, not built): %s\n  size: %s\n  risk: %s" %
                  (v["name"], v["kind"], v["what"], v["size"], v["risk"]))
        if "manifest_path" in res:
            print("\nbuilt -> %s (manifest %s)" % (args.build, res["manifest_path"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())