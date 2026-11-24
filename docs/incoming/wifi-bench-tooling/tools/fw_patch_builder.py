#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""fw_patch_builder.py - build/verify patched Hi5622V100 firmware images (PD/PD2).

Lane: wifi-bench-tooling (shared task task-97).  HOST-ONLY: this tool reads the
LOCAL vendor firmware blob in place, applies a patch recipe, and writes the
patched COPY outside the git repositories (default /tmp/hi5622-patched).  No
vendor bytes are ever written into the repository; a recipe/manifest contains
only offsets, source-window hashes and OUR patch bytes.

Why a second builder next to wifi-irq-unmask/tools/patch_builder.py:
  * task-97 needs a builder the session runner can TRUST and CROSS-CHECK:
    pinned source sha256/len, per-window sha16 of the actual source bytes,
    non-overlapping segments, unchanged image length, full-image verify of an
    existing patched file, and a manifest that binds the deployed file to the
    recipe (the runner refuses a patched fw file that is not in a manifest).
  * PD2 is produced by a different lane (task-91).  Rather than hard-coding a
    moving target, recipes are data: the built-in PD recipe is the
    wifi-irq-unmask one; PD2/other recipes are loaded from a JSON file with the
    "hi5622-fw-patch-recipe/1" schema (see README).  The verify path is the
    same for every recipe.

Usage (host-only):
    fw_patch_builder.py --list
    fw_patch_builder.py --selftest
    fw_patch_builder.py --build PD
    fw_patch_builder.py --build PD2 --recipe-file /path/pd2.recipe.json
    fw_patch_builder.py --build PD --dry-run
    fw_patch_builder.py --verify /tmp/hi5622-patched/FIRMWARE.PD.bin --patch PD
    fw_patch_builder.py --verify-manifest /tmp/hi5622-patched/manifest.json
Options: --fw PATH (or $HI5622_FW), --out DIR, --no-decode, --allow-repo-build,
         --allow-other-source, --deploy-name NAME.
"""
from __future__ import annotations

import argparse
import dataclasses
import hashlib
import json
import os
import sys
import tempfile
import time

TOOLDIR = os.path.dirname(os.path.abspath(__file__))
if TOOLDIR not in sys.path:
    sys.path.insert(0, TOOLDIR)
import bench_guards as G  # noqa: E402

SCHEMA_RECIPE = "hi5622-fw-patch-recipe/1"
SCHEMA_MANIFEST = "hi5622-fw-patch-manifest/1"
TOOL_VERSION = "wifi-bench-tooling/1"

# file offset = CPU address - 0x40000 (the image is loaded at CPU 0x40000).
CPU_BASE = 0x40000
FW_STEM = "FIRMWARE"

# Image regions worth warning about if a recipe touches them.
HANDSHAKE_PAGE_FILE = G.CPU_HANDSHAKE_PAGE - CPU_BASE   # 0x17F000

# Image header protection (REPORT-PD3.md section 3.3, independently checked
# against the pinned blob): file 0x00/0x04 are the live entry A/B addresses and
# must never be patched; file 0x08..0x4F is the reserved exception-record area
# (all zero / RAZ in the pinned vendor image: mask halfword at 0x08, staged
# frame from 0x0C); the live exception handler table starts at 0x50.  A recipe
# may use the reserved area only while the source bytes there are all zero.
ENTRY_WORDS_END = 0x08
EXCP_RECORD_END = 0x50


# ---------------------------------------------------------------------------
# Built-in recipes (all host-only patch data; no vendor bytes)
# ---------------------------------------------------------------------------
def builtin_recipes() -> dict[str, dict]:
    return {
        "PD": {
            "name": "PD",
            "schema": SCHEMA_RECIPE,
            "about": ("redirect the boot-task success epilogue into a permanent "
                      "H2D dispatcher (0x40294 -> 0xC18AC) poll loop at the "
                      "repurposed handshake-failure path"),
            "expected": ("after the golden boot + ready pair, the firmware polls "
                         "the H2D mailbox dispatcher forever: a host out[0]=BIT(3) "
                         "+ out[2]|=1 post is consumed - out[0] clears, out[5] "
                         "reads 1, out[2] reads 8"),
            "source": {"sha256": G.VENDOR_FW_PINS["FIRMWARE.bin"]["sha256"],
                       "len": G.VENDOR_FW_PINS["FIRMWARE.bin"]["len"]},
            "based_on": None,
            "segments": [
                # wifi-irq-unmask/tools/patch_builder.py PD recipe (2026-11-24)
                {"file": 0x7610, "len": 6, "source_sha16": "e81740ccb394d442",
                 "patched_hex": "f8f740fefce7", "isa": "thumb",
                 "expect": "bl #0x40294 ; b #0x47610",
                 "about": "boot-task success epilogue -> bl wrapper 0x40294"},
                {"file": 0x916A, "len": 4, "source_sha16": "c5975cf4bd702f88",
                 "patched_hex": "fef751ba", "isa": "thumb",
                 "expect": "b.w #0x47610",
                 "about": "second failure path -> b.w poll loop"},
            ],
        },
    }


# ---------------------------------------------------------------------------
# Optional capstone decode (never required)
# ---------------------------------------------------------------------------
def decode_insn(isa: str, data: bytes, addr: int | None = None) -> str | None:
    try:
        from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB
    except Exception:
        return None
    mode = CS_MODE_ARM if isa == "arm" else CS_MODE_THUMB
    md = Cs(CS_ARCH_ARM, mode)
    out = []
    for insn in md.disasm(data, addr or 0):
        out.append(("%s %s" % (insn.mnemonic, insn.op_str)).strip())
    return " ; ".join(out) if out else ""


# ---------------------------------------------------------------------------
# Recipe loading / validation
# ---------------------------------------------------------------------------
def _sha16(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()[:16]


def normalize_recipe(doc: dict, origin: str = "built-in") -> dict:
    """Validate + normalise one recipe document; raise G.GuardError on refusal."""
    if not isinstance(doc, dict):
        raise G.GuardError("recipe-not-object", "%s: recipe must be a JSON object" % origin)
    schema = doc.get("schema", SCHEMA_RECIPE)
    if schema != SCHEMA_RECIPE:
        raise G.GuardError(
            "recipe-schema",
            "%s: recipe schema %r is not %r" % (origin, schema, SCHEMA_RECIPE),
        )
    name = doc.get("name")
    if not isinstance(name, str) or not name or not name.replace("_", "").isalnum():
        raise G.GuardError("recipe-name", "%s: bad recipe name %r" % (origin, name))
    if name != name.upper():
        raise G.GuardError("recipe-name", "%s: recipe name %r is not upper-case" % (origin, name))
    segs = doc.get("segments")
    if not isinstance(segs, list) or not segs:
        raise G.GuardError("recipe-segments", "%s: recipe %r has no segments" % (origin, name))
    out_segs = []
    seen: list[tuple[int, int]] = []
    for i, seg in enumerate(segs):
        if not isinstance(seg, dict):
            raise G.GuardError("recipe-segment", "%s: %s seg %d is not an object" % (origin, name, i))
        try:
            off = G.parse_int(seg.get("file"))
            ln = G.parse_int(seg.get("len"))
        except G.GuardError as e:
            raise G.GuardError("recipe-segment", "%s: %s seg %d: %s" % (origin, name, i, e)) from None
        if ln <= 0 or ln > 4096:
            raise G.GuardError("recipe-segment", "%s: %s seg %d bad len %d" % (origin, name, i, ln))
        if off < ENTRY_WORDS_END:
            raise G.GuardError(
                "recipe-entry-protected",
                "%s: %s seg %d at file 0x%X touches the image entry words "
                "(file 0x00/0x04 are live entry A/B addresses; refused)"
                % (origin, name, i, off),
            )
        if off < EXCP_RECORD_END and off + ln > EXCP_RECORD_END:
            raise G.GuardError(
                "recipe-excp-record-cross",
                "%s: %s seg %d (file 0x%X+%d) crosses out of the reserved "
                "exception-record area into the live handler table at file 0x%X "
                "(refused)" % (origin, name, i, off, ln, EXCP_RECORD_END),
            )
        patched_hex = seg.get("patched_hex")
        if not isinstance(patched_hex, str):
            raise G.GuardError("recipe-segment", "%s: %s seg %d has no patched_hex" % (origin, name, i))
        try:
            patched = bytes.fromhex(patched_hex)
        except ValueError:
            raise G.GuardError("recipe-segment", "%s: %s seg %d patched_hex is not hex" % (origin, name, i)) from None
        if len(patched) != ln:
            raise G.GuardError(
                "recipe-segment",
                "%s: %s seg %d patched_hex is %d B, len says %d"
                % (origin, name, i, len(patched), ln),
            )
        sha16 = seg.get("source_sha16")
        if not isinstance(sha16, str) or len(sha16) != 16:
            raise G.GuardError(
                "recipe-segment",
                "%s: %s seg %d source_sha16 must be 16 hex chars" % (origin, name, i),
            )
        try:
            int(sha16, 16)
        except ValueError:
            raise G.GuardError("recipe-segment", "%s: %s seg %d source_sha16 is not hex" % (origin, name, i)) from None
        isa = seg.get("isa", "thumb")
        if isa not in ("arm", "thumb"):
            raise G.GuardError("recipe-segment", "%s: %s seg %d bad isa %r" % (origin, name, i, isa))
        for (o2, l2) in seen:
            if off < o2 + l2 and o2 < off + ln:
                raise G.GuardError(
                    "recipe-overlap",
                    "%s: %s segments overlap (0x%X+%d vs 0x%X+%d)"
                    % (origin, name, off, ln, o2, l2),
                )
        seen.append((off, ln))
        out_segs.append({
            "file": off, "cpu": off + CPU_BASE, "len": ln,
            "source_sha16": sha16.lower(),
            "patched_hex": patched.hex(),
            "patched": patched,
            "isa": isa,
            "expect": seg.get("expect", ""),
            "about": seg.get("about", ""),
        })
    based_on = doc.get("based_on")
    if based_on is not None:
        if not isinstance(based_on, str) or not based_on:
            raise G.GuardError("recipe-based-on", "%s: bad based_on %r" % (origin, based_on))
    src = doc.get("source") or {}
    if not isinstance(src, dict) or not isinstance(src.get("sha256"), str) or not isinstance(src.get("len"), int):
        raise G.GuardError("recipe-source", "%s: recipe %r has no source {sha256,len}" % (origin, name))
    return {
        "schema": SCHEMA_RECIPE,
        "name": name,
        "about": doc.get("about", ""),
        "expected": doc.get("expected", ""),
        "based_on": based_on,
        "source": src,
        "segments": out_segs,
        "origin": origin,
    }


def load_recipe_files(paths: list[str]) -> dict[str, dict]:
    out: dict[str, dict] = {}
    for path in paths:
        if not os.path.isfile(path):
            raise G.GuardError("recipe-file-missing", "recipe file not found: %s" % path)
        try:
            with open(path, "r", encoding="utf-8") as f:
                doc = json.load(f)
        except (OSError, ValueError) as e:
            raise G.GuardError("recipe-file-json", "cannot parse %s: %s" % (path, e)) from None
        docs = doc if isinstance(doc, list) else [doc]
        for d in docs:
            rec = normalize_recipe(d, origin=path)
            if rec["name"] in out:
                raise G.GuardError("recipe-duplicate", "recipe %r defined twice" % rec["name"])
            out[rec["name"]] = rec
    return out


# Recipe directories committed by sibling lanes that this builder picks up
# automatically (data only; no vendor bytes).  Today the wifi-irq-unmask /
# pd2 lane holds PD2, PD3 and PD3B; any future *.recipe.json dropped there is
# loaded deterministically (sorted) without a --recipe-file argument.
LANE_RECIPE_DIRS = [
    os.path.join(TOOLDIR, "..", "..", "wifi-irq-unmask"),
]


def default_recipe_files() -> list[str]:
    files: list[str] = []
    for d in LANE_RECIPE_DIRS:
        d = os.path.normpath(d)
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            if name.endswith(".recipe.json"):
                files.append(os.path.normpath(os.path.join(d, name)))
    return files


def all_recipes(paths: list[str]) -> dict[str, dict]:
    recs = builtin_recipes()
    out = {}
    for name, r in recs.items():
        out[name] = normalize_recipe(r, origin="built-in")
    files = list(paths)
    for default in default_recipe_files():
        if default not in files:
            files.append(default)
    out.update(load_recipe_files(files))
    # resolve based_on chains exist
    for name, r in out.items():
        base = r.get("based_on")
        if base and base not in out:
            raise G.GuardError(
                "recipe-based-on-missing",
                "recipe %r is based_on %r which is not defined" % (name, base),
            )
    return out


def resolve_chain(recipes: dict[str, dict], name: str) -> list[dict]:
    chain: list[dict] = []
    seen: set[str] = set()
    cur = name
    while cur:
        if cur in seen:
            raise G.GuardError("recipe-cycle", "based_on cycle at %r" % cur)
        seen.add(cur)
        rec = recipes.get(cur)
        if rec is None:
            raise G.GuardError("recipe-unknown", "unknown recipe %r (use --list)" % cur)
        chain.append(rec)
        cur = rec.get("based_on")
    chain.reverse()
    return chain


# ---------------------------------------------------------------------------
# Apply / verify
# ---------------------------------------------------------------------------
def apply_recipes(source: bytes, recipes: dict[str, dict], names: list[str]) -> tuple[bytes, list[dict]]:
    """Apply recipe names (chain order) to a source blob; return (patched, segments)."""
    out = bytearray(source)
    all_segments: list[dict] = []
    windows: list[tuple[int, int, str]] = []
    for name in names:
        for rec in resolve_chain(recipes, name):
            for seg in rec["segments"]:
                off, ln = seg["file"], seg["len"]
                if off < 0 or off + ln > len(out):
                    raise G.GuardError(
                        "recipe-out-of-range",
                        "%s seg at file 0x%X+%d is outside the %d B image"
                        % (name, off, ln, len(out)),
                    )
                for (o2, l2, n2) in windows:
                    if off < o2 + l2 and o2 < off + ln:
                        raise G.GuardError(
                            "recipe-overlap",
                            "recipes %r and %r patch overlapping windows "
                            "(0x%X+%d vs 0x%X+%d); use based_on for layering"
                            % (name, n2, off, ln, o2, l2),
                        )
                src = bytes(out[off:off + ln])
                if off < EXCP_RECORD_END and src != b"\x00" * ln:
                    raise G.GuardError(
                        "recipe-window-not-reserved",
                        "%s seg file 0x%X+%d targets the reserved image-header "
                        "exception-record area (file 0x%X..0x%X) but the source "
                        "bytes there are not all zero; this window is only "
                        "patchable while it is RAZ in the pinned image"
                        % (name, off, ln, ENTRY_WORDS_END, EXCP_RECORD_END - 1),
                    )
                got = _sha16(src)
                if got != seg["source_sha16"]:
                    raise G.GuardError(
                        "recipe-window-mismatch",
                        "%s seg file 0x%X: source window sha16 is %s, recipe pins "
                        "%s (wrong source blob, wrong base patch, or a recipe "
                        "written against a different build)"
                        % (name, off, got, seg["source_sha16"]),
                    )
                decoded = decode_insn(seg["isa"], seg["patched"], off + CPU_BASE)
                if seg.get("expect") and decoded is not None and decoded != seg["expect"]:
                    raise G.GuardError(
                        "recipe-decode-mismatch",
                        "%s seg file 0x%X: patched bytes decode as %r, recipe "
                        "expects %r (stale/incorrect patch bytes)"
                        % (name, off, decoded, seg["expect"]),
                    )
                out[off:off + ln] = seg["patched"]
                all_segments.append({
                    "patch": rec["name"],
                    "file": off,
                    "cpu": off + CPU_BASE,
                    "len": ln,
                    "source_sha16": seg["source_sha16"],
                    "patched_hex": seg["patched"].hex(),
                    "isa": seg["isa"],
                    "expect": seg.get("expect", ""),
                    "decode": decoded if decoded is not None else "skipped(no capstone)",
                })
                windows.append((off, ln, rec["name"]))
    return bytes(out), all_segments


def recipe_warnings(source: bytes, segments: list[dict], out: bytes) -> list[str]:
    warns: list[str] = []
    if len(out) != len(source):
        warns.append("patched image length changed (refused elsewhere)")
    for seg in segments:
        off = seg["file"]
        if off < EXCP_RECORD_END:
            warns.append("segment at file 0x%X uses the reserved image-header "
                         "exception-record area (source was RAZ; entry A/B and "
                         "the 0x50 handler table stay untouched)" % off)
        if HANDSHAKE_PAGE_FILE <= off < HANDSHAKE_PAGE_FILE + 0x10:
            warns.append("segment at file 0x%X touches the ROM<->image handshake page image window" % off)
        if seg["patched_hex"] == source[off:off + seg["len"]].hex():
            warns.append("segment at file 0x%X is a no-op (patched bytes equal source)" % off)
    return warns


def verify_patched_file(source_path: str, out_path: str, recipes: dict[str, dict],
                        names: list[str]) -> dict:
    """Verify an existing patched copy against the recipe chain (full compare)."""
    src = open(source_path, "rb").read()
    expected, segments = apply_recipes(src, recipes, names)
    if not os.path.isfile(out_path):
        raise G.GuardError("verify-missing", "patched file not found: %s" % out_path)
    got = open(out_path, "rb").read()
    if len(got) != len(expected):
        raise G.GuardError(
            "verify-length",
            "%s is %d B, expected %d B" % (out_path, len(got), len(expected)),
        )
    diffs = sum(1 for a, b in zip(got, expected) if a != b)
    if diffs:
        bad = [i for i, (a, b) in enumerate(zip(got, expected)) if a != b][:8]
        raise G.GuardError(
            "verify-mismatch",
            "%s has %d byte(s) differing from the recipe output (first offsets: %s)"
            % (out_path, diffs, ", ".join("0x%X" % i for i in bad)),
        )
    return {
        "ok": True,
        "file": os.path.abspath(out_path),
        "sha256": hashlib.sha256(got).hexdigest(),
        "len": len(got),
        "patches": names,
        "segments": segments,
    }


# ---------------------------------------------------------------------------
# Manifest
# ---------------------------------------------------------------------------
def load_manifest(path: str) -> dict | None:
    if not os.path.isfile(path):
        return None
    try:
        with open(path, "r", encoding="utf-8") as f:
            doc = json.load(f)
    except (OSError, ValueError):
        return None
    if not isinstance(doc, dict) or doc.get("schema") != SCHEMA_MANIFEST:
        return None
    return doc


def _normalise_source(source: dict) -> dict:
    """Manifest source block: always {path, sha256, len} (builder FwInfo uses size)."""
    try:
        length = int(source.get("len", source.get("size", 0)))
    except (TypeError, ValueError):
        length = 0
    return {
        "path": source.get("path", ""),
        "name": source.get("name", ""),
        "sha256": source.get("sha256", ""),
        "len": length,
    }


def write_manifest(path: str, source: dict, builds: list[dict]) -> dict:
    source = _normalise_source(source)
    old = load_manifest(path)
    merged: dict[str, dict] = {}
    if old and old.get("source", {}).get("sha256") == source["sha256"]:
        for b in old.get("builds", []):
            merged[b.get("patch", "")] = b
    for b in builds:
        merged[b["patch"]] = b
    doc = {
        "schema": SCHEMA_MANIFEST,
        "tool": TOOL_VERSION,
        "created": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "note": ("patched copies only; the vendor blob and these patched files "
                 "stay local and are never committed"),
        "source": source,
        "builds": [merged[k] for k in sorted(merged) if k],
    }
    os.makedirs(os.path.dirname(os.path.abspath(path)) or ".", exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=1, sort_keys=True)
        f.write("\n")
    return doc


# ---------------------------------------------------------------------------
# Output path policy
# ---------------------------------------------------------------------------
def check_output_dir(path: str, allow_repo_build: bool = False) -> str:
    real = os.path.realpath(path)
    ok, msg = G.gitignored_evidence_ok(real)
    if not ok:
        raise G.GuardError("out-inside-repo", msg)
    if real.startswith(os.path.realpath(os.getcwd()) + os.sep) and not allow_repo_build:
        # cwd-repo outputs are only acceptable under the gitignored build/ tree
        ok2, _ = G.gitignored_evidence_ok(real)
        if not ok2:
            raise G.GuardError("out-inside-repo", msg)
    return real


def deploy_name_for(source_basename: str, patch: str, override: str | None) -> str:
    name = override or ("%s.%s.bin" % (os.path.splitext(source_basename)[0] or FW_STEM, patch))
    if not G.PATCHED_FW_NAME_RE.match(name):
        raise G.GuardError(
            "deploy-name",
            "deploy filename %r is not FIRMWARE.<PATCH>.bin (fw_path grammar)" % name,
        )
    return name


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------
def cmd_list(recipes: dict[str, dict]) -> int:
    for name in sorted(recipes):
        r = recipes[name]
        print("%s: %s" % (name, r.get("about") or "(no description)"))
        print("   recipe source: %s" % (r.get("origin", "?"),))
        if r.get("based_on"):
            print("   based_on: %s" % r["based_on"])
        for seg in r["segments"]:
            print("   file 0x%06X cpu 0x%08X len %d -> %s (%s) want %s"
                  % (seg["file"], seg["cpu"], seg["len"], seg["patched_hex"],
                     seg["isa"], seg.get("expect") or "?"))
    return 0


def check_source_blob(path: str, recipes: dict[str, dict], names: list[str],
                      allow_other_source: bool) -> tuple[bytes, dict]:
    info = G.validate_vendor_fw(path, allow_other_source=allow_other_source)
    blob = open(path, "rb").read()
    if len(blob) != info.size:
        raise G.GuardError("fw-read", "short read of %s" % path)
    for name in names:
        for rec in resolve_chain(recipes, name):
            src = rec.get("source") or {}
            if src.get("sha256") and src["sha256"] != info.sha256:
                raise G.GuardError(
                    "recipe-source-mismatch",
                    "recipe %r pins source sha256 %s but %s is %s"
                    % (rec["name"], src["sha256"][:16], path, info.sha256[:16]),
                )
            if src.get("len") and int(src["len"]) != info.size:
                raise G.GuardError(
                    "recipe-source-mismatch",
                    "recipe %r pins source len %d but %s is %d B"
                    % (rec["name"], int(src["len"]), path, info.size),
                )
    return blob, info.as_dict()


def cmd_build(args, recipes: dict[str, dict]) -> int:
    names = args.patch or []
    if not names:
        print("error: --build needs a recipe name (repeatable); see --list", file=sys.stderr)
        return 2
    for n in names:
        if n not in recipes:
            print("error: unknown recipe %r (see --list)" % n, file=sys.stderr)
            return 2
    src, src_info = check_source_blob(args.fw, recipes, names, args.allow_other_source)
    out = bytearray(src)
    builds = []
    for name in names:
        patched, segments = apply_recipes(src, recipes, [name])
        out = bytearray(patched)
        warns = recipe_warnings(src, segments, patched)
        fname = deploy_name_for(os.path.basename(args.fw), name, args.deploy_name if len(names) == 1 else None)
        out_path = os.path.join(args.out, fname)
        entry = {
            "patch": name,
            "about": recipes[name].get("about", ""),
            "expected": recipes[name].get("expected", ""),
            "chain": [r["name"] for r in resolve_chain(recipes, name)],
            "deploy_name": fname,
            "out": os.path.abspath(out_path),
            "out_sha256": hashlib.sha256(patched).hexdigest(),
            "out_len": len(patched),
            "segments": segments,
            "warnings": warns,
            "dry_run": bool(args.dry_run),
        }
        builds.append(entry)
        print("recipe %s: %d segment(s), %d B -> %s sha256 %s%s"
              % (name, len(segments), len(patched), fname,
                 entry["out_sha256"][:16] + "...",
                 " [dry-run]" if args.dry_run else ""))
        for seg in segments:
            print("   file 0x%06X cpu 0x%08X  %s  (%s)"
                  % (seg["file"], seg["cpu"], seg["patched_hex"], seg["decode"]))
        for w in warns:
            print("   warning:", w)
    if args.all:
        all_names = sorted(recipes)
        patched, segments = apply_recipes(src, recipes, all_names)
        entry = {
            "patch": "ALL",
            "about": "all defined recipes combined (control build)",
            "chain": all_names,
            "deploy_name": deploy_name_for(os.path.basename(args.fw), "ALL", None),
            "out": os.path.abspath(os.path.join(args.out, deploy_name_for(os.path.basename(args.fw), "ALL", None))),
            "out_sha256": hashlib.sha256(patched).hexdigest(),
            "out_len": len(patched),
            "segments": segments,
            "warnings": recipe_warnings(src, segments, patched),
            "dry_run": bool(args.dry_run),
        }
        builds.append(entry)
        print("recipe ALL: %d segment(s), %d B -> %s sha256 %s%s"
              % (len(segments), len(patched), entry["deploy_name"],
                 entry["out_sha256"][:16] + "...",
                 " [dry-run]" if args.dry_run else ""))
    if args.dry_run:
        print("dry-run: source %s (%d B) verified; no file written" % (args.fw, len(src)))
        return 0
    out_dir = check_output_dir(args.out, args.allow_repo_build)
    os.makedirs(out_dir, exist_ok=True)
    manifest_path = args.manifest_out or os.path.join(out_dir, "manifest.json")
    real_builds = []
    for entry in builds:
        patched = bytearray(src)
        patched, segments = apply_recipes(src, recipes, entry.get("chain") or [entry["patch"]])
        out_path = os.path.join(out_dir, entry["deploy_name"])
        with open(out_path, "wb") as f:
            f.write(patched)
        # verify what we just wrote, byte-for-byte
        verified = verify_patched_file(args.fw, out_path, recipes,
                                       entry.get("chain") or [entry["patch"]])
        rebuilt = dict(entry)
        rebuilt.update({"out": out_path, "out_sha256": verified["sha256"],
                        "out_len": verified["len"], "verified": True})
        real_builds.append(rebuilt)
        print("built + verified", rebuilt["deploy_name"], "->", out_path,
              "sha256", rebuilt["out_sha256"][:16] + "...")
    doc = write_manifest(manifest_path, src_info, real_builds)
    print("manifest:", manifest_path, "(%d build(s))" % len(doc["builds"]))
    return 0


def cmd_verify(args, recipes: dict[str, dict]) -> int:
    names = args.patch or []
    if not names:
        # auto-detect: try every recipe whose source pin matches
        names = [n for n in sorted(recipes)
                 if recipes[n].get("source", {}).get("sha256") ==
                 G.VENDOR_FW_PINS["FIRMWARE.bin"]["sha256"]]
        print("auto-detect recipes:", ", ".join(names))
    for name in names:
        if name not in recipes:
            print("error: unknown recipe %r" % name, file=sys.stderr)
            return 2
    src, src_info = check_source_blob(args.fw, recipes, names, args.allow_other_source)
    result = verify_patched_file(args.fw, args.verify, recipes, names)
    print("VERIFY PASS: %s" % result["file"])
    print("  sha256 %s (%d B)" % (result["sha256"], result["len"]))
    for seg in result["segments"]:
        print("  file 0x%06X cpu 0x%08X %s %s"
              % (seg["file"], seg["cpu"], seg["patched_hex"], seg["decode"]))
    return 0


def cmd_verify_manifest(args, recipes: dict[str, dict]) -> int:
    doc = G.load_patch_manifest(args.verify_manifest)
    fails = 0
    for b in doc["builds"]:
        chain = b.get("chain") or [b["patch"]]
        for name in chain:
            if name not in recipes:
                print("FAIL %s: recipe %r not loaded (pass --recipe-file)" % (b["patch"], name))
                fails += 1
                break
        else:
            try:
                res = verify_patched_file(args.fw, b["out"], recipes, chain)
            except G.GuardError as e:
                print("FAIL %s (%s): %s" % (b["patch"], b["out"], e))
                fails += 1
                continue
            ok_sha = res["sha256"] == b["out_sha256"]
            print("%s %s: file matches recipe chain; sha256 %s%s"
                  % ("PASS" if ok_sha else "FAIL", b["patch"],
                     res["sha256"][:16] + "...",
                     "" if ok_sha else " (manifest pins %s)" % b["out_sha256"][:16]))
            if not ok_sha:
                fails += 1
    print("verify-manifest: %d build(s), %d failure(s)" % (len(doc["builds"]), fails))
    return 0 if not fails else 1


# ---------------------------------------------------------------------------
# Synthetic selftest (never reads the vendor blob)
# ---------------------------------------------------------------------------
def _synth_blob(size: int = 4096) -> bytes:
    return bytes(((i * 131 + 7) ^ (i >> 8)) & 0xFF for i in range(size))


def _synth_recipe(name: str = "SYNPD") -> dict:
    blob = _synth_blob()
    segs = []
    for off, ln, pat in ((0x100, 2, b"\x00\x20"), (0x200, 4, b"\xfe\xf7\x51\xba")):
        segs.append({"file": off, "len": ln,
                     "source_sha16": _sha16(blob[off:off + ln]),
                     "patched_hex": pat.hex(), "isa": "thumb",
                     "expect": "", "about": "synthetic"})
    return {"schema": SCHEMA_RECIPE, "name": name, "about": "synthetic",
            "source": {"sha256": hashlib.sha256(blob).hexdigest(), "len": len(blob)},
            "based_on": None, "segments": segs}


def selftest() -> int:
    checks: list[tuple[str, bool]] = []

    def ck(name: str, ok: bool) -> None:
        checks.append((name, bool(ok)))

    blob = _synth_blob()
    rec_doc = _synth_recipe()
    recipes = {"SYNPD": normalize_recipe(rec_doc, "synthetic")}
    patched, segs = apply_recipes(blob, recipes, ["SYNPD"])
    ck("synthetic: apply length unchanged", len(patched) == len(blob))
    ck("synthetic: segments applied",
       all(patched[s["file"]:s["file"] + s["len"]] == bytes.fromhex(s["patched_hex"])
           for s in segs))
    ck("synthetic: rest identical",
       all(a == b for i, (a, b) in enumerate(zip(blob, patched))
           if not any(s["file"] <= i < s["file"] + s["len"] for s in segs)))
    # layered recipes
    layered_doc = {"schema": SCHEMA_RECIPE, "name": "SYNPD2", "about": "synthetic layer",
                   "source": rec_doc["source"], "based_on": "SYNPD",
                   "segments": [{"file": 0x300, "len": 2,
                                 "source_sha16": _sha16(patched[0x300:0x302]),
                                 "patched_hex": "00bf", "isa": "thumb", "expect": ""}]}
    recipes2 = dict(recipes)
    recipes2["SYNPD2"] = normalize_recipe(layered_doc, "synthetic")
    final, segs2 = apply_recipes(blob, recipes2, ["SYNPD2"])
    ck("layered: chain applied", len(segs2) == 3 and final[0x300:0x302] == b"\x00\xbf")
    ck("layered: parent segments present", final[0x100:0x102] == b"\x00\x20")

    # refusals
    def expect_err(label: str, fn, code: str) -> None:
        try:
            fn()
        except G.GuardError as e:
            ck("refuse %s (%s)" % (label, e.code), e.code == code or code == "*")
            return
        ck("refuse %s" % label, False)

    bad = dict(rec_doc)
    bad["segments"] = [dict(rec_doc["segments"][0], patched_hex="ffffffff")]
    expect_err("patched_hex length", lambda: normalize_recipe(bad, "synthetic"), "recipe-segment")
    bad2 = dict(rec_doc)
    bad2["segments"] = [dict(rec_doc["segments"][0], source_sha16="0" * 16)]
    src2 = _synth_blob()
    expect_err("window sha16", lambda: apply_recipes(src2, {"X": normalize_recipe(bad2, "synthetic")}, ["X"]),
               "recipe-window-mismatch")
    bad3 = dict(rec_doc)
    bad3["segments"] = [dict(rec_doc["segments"][0], file=0x04)]
    expect_err("entry word refused", lambda: normalize_recipe(bad3, "synthetic"),
               "recipe-entry-protected")
    bad3b = dict(rec_doc)
    bad3b["segments"] = [{"file": 0x4C, "len": 8,
                          "source_sha16": _sha16(blob[0x4C:0x54]),
                          "patched_hex": "00bf00bf00bf00bf", "isa": "thumb"}]
    expect_err("crosses live handler table",
               lambda: normalize_recipe(bad3b, "synthetic"), "recipe-excp-record-cross")
    # reserved exception-record area: patchable only while the SOURCE is RAZ
    bad3c = dict(rec_doc)
    bad3c["segments"] = [{"file": 0x10, "len": 2,
                          "source_sha16": _sha16(blob[0x10:0x12]),
                          "patched_hex": "00bf", "isa": "thumb"}]
    expect_err("reserved area with non-zero source",
               lambda: apply_recipes(blob, {"X": normalize_recipe(bad3c, "synthetic")}, ["X"]),
               "recipe-window-not-reserved")
    # positive: zero-filled reserved window may be used (the PD3 pattern)
    zero_blob = bytearray(blob)
    zero_blob[0x10:0x18] = b"\x00" * 8
    zero_blob = bytes(zero_blob)
    rec_z = dict(rec_doc)
    rec_z["segments"] = [{"file": 0x10, "len": 8,
                          "source_sha16": _sha16(zero_blob[0x10:0x18]),
                          "patched_hex": "0b4f81b03b68002b", "isa": "thumb"}]
    patched_z, segs_z = apply_recipes(zero_blob,
                                      {"Z": normalize_recipe(rec_z, "synthetic")}, ["Z"])
    ck("reserved area: RAZ source allowed (PD3 pattern)",
       patched_z[0x10:0x18] == bytes.fromhex("0b4f81b03b68002b") and len(segs_z) == 1)
    bad4 = dict(rec_doc)
    bad4["segments"] = [rec_doc["segments"][0], dict(rec_doc["segments"][1], file=0x101)]
    expect_err("overlap", lambda: normalize_recipe(bad4, "synthetic"), "recipe-overlap")
    bad5 = dict(rec_doc)
    bad5["segments"] = [dict(rec_doc["segments"][0], file=999999)]
    expect_err("out of range",
               lambda: apply_recipes(blob, {"X": normalize_recipe(bad5, "synthetic")}, ["X"]),
               "recipe-out-of-range")
    expect_err("cycle",
               lambda: resolve_chain({"A": {"name": "A", "based_on": "B", "segments": [], "source": {}},
                                      "B": {"name": "B", "based_on": "A", "segments": [], "source": {}}}, "A"),
               "recipe-cycle")
    # based_on-derived window sha must match the parent's output
    bad6 = {"schema": SCHEMA_RECIPE, "name": "SYNBAD", "based_on": "SYNPD",
            "source": rec_doc["source"],
            "segments": [{"file": 0x500, "len": 2,
                          "source_sha16": "0" * 16,
                          "patched_hex": "00bf", "isa": "thumb"}]}
    expect_err("stale based_on window",
               lambda: apply_recipes(blob, {**recipes, "SYNBAD": normalize_recipe(bad6, "synthetic")},
                                     ["SYNBAD"]),
               "recipe-window-mismatch")

    # verify + manifest round-trip in a temp dir
    tmp = tempfile.mkdtemp(prefix="fwbuild-selftest-")
    fw_path = os.path.join(tmp, "FIRMWARE.bin")
    with open(fw_path, "wb") as f:
        f.write(blob)
    outdir = os.path.join(tmp, "out")
    os.makedirs(outdir)
    sys_argv = sys.argv
    try:
        sys.argv = ["fw_patch_builder", "--build", "SYNPD", "--fw", fw_path,
                    "--out", outdir, "--allow-other-source", "--no-decode"]
        args = build_parser({"SYNPD": recipes["SYNPD"]}).parse_args(sys.argv[1:])
        args.fw = fw_path
        # route through the real command with the synthetic recipe set
        rc = cmd_build(args, recipes)
        ck("build: command rc==0", rc == 0)
        man = os.path.join(outdir, "manifest.json")
        doc = load_manifest(man)
        ck("manifest: written with 1 build", bool(doc) and len(doc["builds"]) == 1)
        out_file = os.path.join(outdir, "FIRMWARE.SYNPD.bin")
        ck("manifest: output exists", os.path.isfile(out_file))
        args2 = build_parser(recipes).parse_args(
            ["--verify", out_file, "--patch", "SYNPD", "--fw", fw_path,
             "--allow-other-source", "--no-decode"])
        rc2 = cmd_verify(args2, recipes)
        ck("verify: PASS rc==0", rc2 == 0)
        # tamper detection
        tampered = os.path.join(outdir, "tampered.bin")
        data = bytearray(open(out_file, "rb").read())
        data[0x500] ^= 0xFF
        open(tampered, "wb").write(data)
        args3 = build_parser(recipes).parse_args(
            ["--verify", tampered, "--patch", "SYNPD", "--fw", fw_path,
             "--allow-other-source", "--no-decode"])
        try:
            cmd_verify(args3, recipes)
            ck("verify: tampered file detected", False)
        except G.GuardError as e:
            ck("verify: tampered file detected (%s)" % e.code, e.code == "verify-mismatch")
        # manifest verify command
        args4 = build_parser(recipes).parse_args(
            ["--verify-manifest", man, "--fw", fw_path, "--allow-other-source", "--no-decode"])
        ck("verify-manifest: rc==0", cmd_verify_manifest(args4, recipes) == 0)
        # output inside repo refused
        real_root = G.repo_roots(TOOLDIR)[0]
        expect_err("output inside repo",
                   lambda: check_output_dir(os.path.join(real_root, "docs", "out")),
                   "out-inside-repo")
        # deploy names
        ck("deploy name", deploy_name_for("FIRMWARE.bin", "PD", None) == "FIRMWARE.PD.bin")
        expect_err("deploy name grammar",
                   lambda: deploy_name_for("FIRMWARE.bin", "PD", "../../etc/passwd"),
                   "deploy-name")
        # recipe source pin mismatch (recipe pins a different source sha)
        badrec = dict(recipes["SYNPD"])
        badrec["source"] = {"sha256": "0" * 64,
                            "len": recipes["SYNPD"]["source"]["len"]}
        expect_err("source pin mismatch",
                   lambda: check_source_blob(fw_path, {"SYNPD": badrec},
                                             ["SYNPD"], True),
                   "recipe-source-mismatch")
    finally:
        sys.argv = sys_argv
        import shutil
        shutil.rmtree(tmp, ignore_errors=True)

    # committed sibling-lane recipes are auto-discovered (no --recipe-file)
    lane_dir = os.path.normpath(os.path.join(TOOLDIR, "..", "..", "wifi-irq-unmask"))
    if os.path.isdir(lane_dir):
        names = []
        for f in default_recipe_files():
            names.append(json.load(open(f)).get("name"))
        ck("discovery: PD2/PD3/PD3B recipes auto-loaded",
           {"PD2", "PD3", "PD3B"}.issubset(set(names)))

    # optional capstone sanity
    dec = decode_insn("thumb", b"\x00\x20")
    if dec is not None:
        ck("capstone: thumb decode 'movs r0, #0'", dec == "movs r0, #0")

    passed = sum(1 for _, ok in checks if ok)
    for name, ok in checks:
        if not ok:
            print("FAIL:", name)
    print("fw_patch_builder selftest: %d/%d PASS" % (passed, len(checks)))
    return 0 if passed == len(checks) else 1


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def build_parser(recipes: dict[str, dict] | None = None) -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(
        description="Hi5622V100 patched-firmware builder/verifier (host-only; "
                    "patched copies are written outside the repository)")
    ap.add_argument("--fw", default=G.DEFAULT_FW,
                    help="local vendor firmware blob (default: $HI5622_FW or the "
                         "recovery-extract pin)")
    ap.add_argument("--out", default=os.environ.get("HI5622_PATCH_OUT", "/tmp/hi5622-patched"),
                    help="output directory for patched copies (default /tmp/hi5622-patched)")
    ap.add_argument("--manifest-out", default=None, help="manifest path (default <out>/manifest.json)")
    ap.add_argument("--patch", "--build", dest="patch", action="append", metavar="NAME",
                    help="recipe to build/verify (repeatable); see --list")
    ap.add_argument("--all", action="store_true", help="build every defined recipe + combined ALL")
    ap.add_argument("--recipe-file", action="append", default=[], metavar="JSON",
                    help="load additional recipe(s) from a JSON file (hi5622-fw-patch-recipe/1)")
    ap.add_argument("--list", action="store_true", help="list recipe candidates and their segments")
    ap.add_argument("--verify", metavar="FILE", help="verify an existing patched file against a recipe")
    ap.add_argument("--verify-manifest", metavar="JSON", help="verify every build in a manifest")
    ap.add_argument("--selftest", action="store_true", help="synthetic-data selftest (no vendor blob read)")
    ap.add_argument("--dry-run", action="store_true", help="verify source + recipe, write nothing")
    ap.add_argument("--no-decode", action="store_true", help="do not compare capstone disassembly")
    ap.add_argument("--allow-repo-build", action="store_true",
                    help="allow output under the gitignored build/ tree of the repo")
    ap.add_argument("--allow-other-source", action="store_true",
                    help="allow a non-pinned local variant as source (explicit override)")
    ap.add_argument("--deploy-name", default=None,
                    help="output filename (default FIRMWARE.<PATCH>.bin)")
    return ap


def main(argv: list[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    pre = argparse.ArgumentParser(add_help=False)
    pre.add_argument("--recipe-file", action="append", default=[])
    pre_args, _ = pre.parse_known_args(argv)
    try:
        recipes = all_recipes(pre_args.recipe_file)
    except G.GuardError as e:
        print("error:", e, file=sys.stderr)
        return 2
    args = build_parser(recipes).parse_args(argv)
    if args.no_decode:
        # disable capstone comparison by clearing expectations
        for r in recipes.values():
            for s in r["segments"]:
                s["expect"] = ""
    if args.selftest:
        return selftest()
    if args.list or (not args.patch and not args.all and not args.verify
                     and not args.verify_manifest):
        return cmd_list(recipes)
    try:
        if args.verify_manifest:
            return cmd_verify_manifest(args, recipes)
        if args.verify:
            return cmd_verify(args, recipes)
        return cmd_build(args, recipes)
    except G.GuardError as e:
        print("REFUSED:", e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
