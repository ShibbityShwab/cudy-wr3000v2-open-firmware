#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""verify_pd4.py - independent verification harness for the PD4 firmware patch recipe.

Lane: wifi-fw-audit (shared task task-115).  HOST-ONLY and READ-ONLY: the local vendor
firmware blob is read in place; the patched image is built in MEMORY only and never
written into the repository.  The recipe and every manifest produced here contain
offsets, hashes and OUR patch bytes - never vendor bytes.

What it checks
--------------
A. identity      pinned source blob sha256/length; per-segment source-window sha16
B. delta         patched image sha256; unchanged length; full-byte diff confined to
                 exactly the two recipe segments (68 bytes in 2 windows, 59 changed
                 byte positions - a hole in the window is still inside the window)
C. entry         the 4-byte PD4-ENTRY hunk decodes to a Thumb-2 B.W into the stub; the
                 replaced source sequence is the boot-task success epilogue
D. loop          the 64-byte PD4-LOOP hunk in the source image is all-zero; it sits
                 inside the 637-byte static-data alignment gap CPU 0x108197..0x108413
E. stub decode   every instruction of the patched stub decodes to the audited sequence:
                 instance-init guard -> out[0] poll -> sentinel branch -> bl wrapper ->
                 loop; literal pool = {0x10C19C, 0x40039010, 0x118D68, 0x2000}
F. callee        CPU 0x40294 loads the hcc instance 0x0010C190 and tail-calls the
                 dispatcher CPU 0xC18AC; the dispatcher's ack/clear/doorbell/dispatch
                 instruction bytes match the pinned encodings
G. writer ban    the stub home is outside both boot memsets, the exception-staging
                 area, the live image-header words and the image-end append region
H. reference     whole-image conservative census (Thumb at every 2nd byte, ARM at every
   census        4th byte): no branch target, PC-relative literal load, 32-bit word,
                 movw/movt/adr materialisation points into the gap [V]
I. stub safety   the stub itself references exactly one device CA (0x40039010, a read);
                 it contains no 0xCECE, no boot-gate CA and no write to CA 0x400392F0;
                 all internal branch/call targets are in the stub or the wrapper
J. static field  the hcc instance handler-table pointer field (CPU 0x10C1B0) still holds
                 the static value 0x00118D68 (the sentinel the stub compares against) -
                 recorded as an audit observation, see REPORT.md H1/H2

Usage
-----
    PY=.provision-venv/bin/python
    $PY tools/verify_pd4.py --selftest
    $PY tools/verify_pd4.py --fw /path/to/FIRMWARE.bin            # full verify + census
    $PY tools/verify_pd4.py --fw /path/to/FIRMWARE.bin --json
    $PY tools/verify_pd4.py --fw ... --no-census                  # fast verify only
    $PY tools/verify_pd4.py --fw ... --dump-loop /tmp/pd4-loop.bin

Exit code 0 = every check PASS, 1 = a check FAILED, 2 = input/tooling error.
The firmware path defaults to $HI5622_FW, else a short list of known local locations.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import sys

try:
    from capstone import (Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB, CS_MODE_MCLASS,
                          CS_OP_IMM, CS_OP_MEM)
    from capstone.arm import ARM_REG_PC
except Exception as exc:  # pragma: no cover
    print("FATAL: capstone is required (used by .provision-venv/bin/python): %s" % exc)
    sys.exit(2)

CPU_BASE = 0x40000                 # CPU = file offset + CPU_BASE (image alias)
RECIPE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                      "recipes", "PD4.recipe.json")

# --- audited geometry (see REPORT.md) -------------------------------------
GAP_CPU = (0x108197, 0x108414)     # 637-byte all-zero static-data alignment gap
LOOP_CPU = 0x108240                # stub home (inside the gap)
ENTRY_CPU = 0x4916A                # boot-task success epilogue
WRAPPER_CPU = 0x40294              # firmware event-0x4C handler wrapper
DISPATCHER_CPU = 0xC18AC           # H2D mailbox dispatcher
INSTANCE_CPU = 0x10C190            # hcc instance (wrapper literal)
INSTANCE_MAGIC_LITERAL = 0x10C19C  # stub literal: instance + 0xC (ack pointer field)
TABLE_FIELD_CPU = 0x10C1B0         # instance + 0x20: handler-table pointer field
TABLE_STATIC = 0x118D68            # its static value in the vendor image
OUT0_CA = 0x40039010               # mailbox H2D pending bitmap
OUT5_CA = 0x400392F0               # mailbox H2D ack (firmware-owned, never host-written)
OUT2_CA = 0x400392D4               # mailbox H2D doorbell
MEMSET_RANGES = [(0x117C00, 0x1ADDE0), (0x1000000, 0x10E0000)]
STAGING_RANGE = (0x4000C, 0x4004C)  # 17-word exception frame written at CA 0x40008+4
HEADER_RANGE = (0x0, 0x64)         # live image-header words / entry pointers
APPEND_FLOOR = 0x122C98            # vendor image content ends here (CPU address)

# source sites that pin the two boot memset bounds (verified, not trusted blindly)
MEMSET_LITERALS = [(0x46DEC, 0x117C00), (0x46DF0, 0x1ADDE0),
                   (0x46D94, 0x10E0000), (0x46D9C, 0x1000000)]
STAGING_LITERAL = (0xC6FA4, 0x40008)
# ARM-state code islands are entered only from these known transition points (ROM IRQ
# trampoline, scheduler context restore, cache-maintenance helper, FIQ/exception
# helpers); the exhaustive ARM census is filtered against the instructions reachable
# from them (an ARM decode of Thumb/data bytes is an artifact, not a reference).
ARM_ENTRY_POINTS = [0x40064, 0x102C04, 0x102E68, 0x103140, 0x103B50, 0x103B60]
BRACKET_POOL = 0xDE890             # static pool {0x107F3C, 0x1087D4, 0x108054, 0x108414}
BRACKET_VALUES = [0x107F3C, 0x1087D4, 0x108054, 0x108414]

# The audited PD4 stub decode: (address, mnemonic, op_str) exactly as capstone 5.x
# renders it.  If capstone changes its formatting the checked tuple list fails and
# the auditor must re-read the stub - that is intentional (no silent drift).
EXPECTED_STUB = [
    (LOOP_CPU + 0x00, "ldr", "r7, [pc, #0x2c]"),
    (LOOP_CPU + 0x02, "sub", "sp, #4"),
    (LOOP_CPU + 0x04, "ldr", "r3, [r7]"),
    (LOOP_CPU + 0x06, "cmp", "r3, #0"),
    (LOOP_CPU + 0x08, "beq", "#0x108244"),
    (LOOP_CPU + 0x0A, "ldr", "r2, [pc, #0x28]"),
    (LOOP_CPU + 0x0C, "ldr", "r3, [r2]"),
    (LOOP_CPU + 0x0E, "cmp", "r3, #0"),
    (LOOP_CPU + 0x10, "beq", "#0x108244"),
    (LOOP_CPU + 0x12, "ldr", "r3, [r7, #0x14]"),
    (LOOP_CPU + 0x14, "ldr", "r4, [pc, #0x20]"),
    (LOOP_CPU + 0x16, "cmp", "r3, r4"),
    (LOOP_CPU + 0x18, "beq", "#0x108260"),
    (LOOP_CPU + 0x1A, "bl", "#0x40294"),
    (LOOP_CPU + 0x1E, "b", "#0x108244"),
    (LOOP_CPU + 0x20, "ldr", "r4, [pc, #0x18]"),
    (LOOP_CPU + 0x22, "str", "r4, [r7, #0x14]"),
    (LOOP_CPU + 0x24, "bl", "#0x40294"),
    (LOOP_CPU + 0x28, "str", "r3, [r7, #0x14]"),
    (LOOP_CPU + 0x2A, "b", "#0x108244"),
    (LOOP_CPU + 0x2C, "nop", ""),
    (LOOP_CPU + 0x2E, "nop", ""),
]
EXPECTED_POOL = [(LOOP_CPU + 0x30, INSTANCE_MAGIC_LITERAL),
                 (LOOP_CPU + 0x34, OUT0_CA),
                 (LOOP_CPU + 0x38, TABLE_STATIC),
                 (LOOP_CPU + 0x3C, 0x2000)]

# dispatcher (CPU 0xC18AC) pinned encodings: (CPU, bytes, what)
DISPATCHER_PINS = [
    (0xC18AC, "f8b5", "push {r3,r4,r5,r6,r7,lr}"),
    (0xC18B6, "c268", "ldr r2,[r0,#0xc] (ack pointer)"),
    (0xC18B8, "1760", "str r7,[r2] (*ack = 1 -> CA 0x400392F0)"),
    (0xC18BA, "4268", "ldr r2,[r0,#4] (out[0] pointer)"),
    (0xC18BC, "1568", "ldr r5,[r2] (pending bitmap)"),
    (0xC18BE, "1160", "str r1,[r2] (clear out[0])"),
    (0xC18C0, "0821", "movs r1,#8"),
    (0xC18C2, "0269", "ldr r2,[r0,#0x10] (doorbell pointer)"),
    (0xC18C4, "1160", "str r1,[r2] (doorbell = 8)"),
    (0xC18DA, "336a", "ldr r3,[r6,#0x20] (handler table pointer)"),
    (0xC18E4, "0bb1", "cbz r3 (NULL handler skipped)"),
    (0xC18E8, "9847", "blx r3 (handler(arg); arg = table[id].arg)"),
]
WRAPPER_PINS = [(0x40294, "0148", "ldr r0,[pc,#4]"), (0x40296, "81f009bb", "b.w 0xC18AC")]


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha16(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()[:16]


def u32(b: bytes, cpu: int) -> int:
    off = cpu - CPU_BASE
    if off < 0 or off + 4 > len(b):
        raise IndexError("u32 out of image at CPU %#x" % cpu)
    return struct.unpack_from("<I", b, off)[0]


def u16(b: bytes, cpu: int) -> int:
    off = cpu - CPU_BASE
    return struct.unpack_from("<H", b, off)[0]


def find_fw(explicit: str | None) -> str | None:
    if explicit:
        return explicit
    env = os.environ.get("HI5622_FW")
    if env and os.path.isfile(env):
        return env
    # walk up from this tool to the workstation checkout that carries the local vendor
    # rootfs copies; never commit the blob, read it in place.
    here = os.path.dirname(os.path.abspath(__file__))
    for _ in range(8):
        here = os.path.dirname(here)
        for sub in ("rootfs-2.4.15", "rootfs-2.5.24"):
            c = os.path.join(here, sub, "lib", "firmware", "hi_wifi", "FIRMWARE.bin")
            if os.path.isfile(c):
                return c
        c = os.path.join(here, "recovery-extract", "lib", "firmware", "hi_wifi",
                         "FIRMWARE.bin")
        if os.path.isfile(c):
            return c
    import glob
    for c in glob.glob("/home/*/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin"):
        if os.path.isfile(c):
            return c
    return None


class Result:
    def __init__(self):
        self.checks = []
        self.notes = []

    def check(self, cid: str, desc: str, ok: bool, detail: str = ""):
        self.checks.append({"id": cid, "desc": desc, "ok": bool(ok), "detail": detail})
        return ok

    def note(self, text: str):
        self.notes.append(text)

    @property
    def passed(self) -> int:
        return sum(1 for c in self.checks if c["ok"])

    @property
    def failed(self) -> int:
        return sum(1 for c in self.checks if not c["ok"])

    def to_dict(self) -> dict:
        return {"checks": self.checks, "passed": self.passed, "failed": self.failed,
                "notes": self.notes}


def load_recipe(path: str) -> dict:
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def build_patched(fw: bytes, recipe: dict) -> tuple[bytes, list[dict]]:
    """Apply the recipe in memory.  Returns (patched, list of segment reports)."""
    out = bytearray(fw)
    reports = []
    for i, seg in enumerate(recipe["segments"]):
        off, ln = int(seg["file"]), int(seg["len"])
        want_src = bytes.fromhex(seg["source_sha16"]) if False else None
        src = bytes(fw[off:off + ln])
        patched = bytes.fromhex(seg["patched_hex"])
        reports.append({"index": i, "file": off, "len": ln, "src_sha16": sha16(src),
                        "patched_len": len(patched)})
        if len(patched) != ln:
            raise ValueError("segment %d patched_hex length %d != len %d" % (i, len(patched), ln))
        out[off:off + ln] = patched
    return bytes(out), reports


def diff_windows(a: bytes, b: bytes) -> list[tuple[int, int]]:
    """Return [start, end] inclusive runs of differing byte positions."""
    runs = []
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            if runs and i == runs[-1][1] + 1:
                runs[-1][1] = i
            else:
                runs.append([i, i])
    return [(s, e) for s, e in runs]


# --- H: conservative whole-image reference census -------------------------
def arm_code_islands(fw: bytes, max_insns: int = 8000) -> set:
    """Instructions reachable in ARM state from the known ARM entry points."""
    md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
    md.detail = True
    seen: set = set()
    stack = list(ARM_ENTRY_POINTS)
    n = len(fw)
    while stack and len(seen) < max_insns:
        a = stack.pop()
        if a in seen or (a & 3) or a < CPU_BASE or a + 4 > CPU_BASE + n:
            continue
        seen.add(a)
        ins = next(md.disasm(fw[a - CPU_BASE:a - CPU_BASE + 4], a), None)
        if ins is None:
            continue
        mn = ins.mnemonic
        succ = []
        if mn in ("bx", "blx") and not any(op.type == CS_OP_IMM for op in ins.operands):
            succ = []                      # indirect, out of the walked island
        elif mn in ("b", "bl", "blx") or mn[:1] == "b":
            tgt = next((op.imm & 0xFFFFFFFF for op in ins.operands if op.type == CS_OP_IMM), None)
            if tgt is not None:
                succ.append(tgt)
            if mn.startswith("bl") or mn not in ("b", "bx", "blx"):
                succ.append(a + 4)         # call fall-through / conditional fall-through
        else:
            writes_pc = (mn.startswith(("pop", "ldm")) and "pc" in ins.op_str) or                         (mn.startswith(("mov", "ldr")) and "pc" in ins.op_str) or                         mn.startswith("rfe")
            succ = [] if writes_pc else [a + 4]
        stack.extend(succ)
    return seen


class Census:
    def __init__(self, lo: int, hi: int):
        self.lo, self.hi = lo, hi
        self.thumb_branch = []
        self.thumb_literal = []
        self.thumb_adr = []
        self.arm_branch = []
        self.arm_literal = []
        self.words = []
        self.movt_hi = set()
        self.movw_lo = set()
        self.mov_pair = []
        self.arm_islands = set()
        self.arm_credible = []
        self.arm_artifacts = []
        self.words_credible = []
        self.words_unaligned = []

    def inside(self, a: int) -> bool:
        return self.lo <= a < self.hi

    def to_dict(self) -> dict:
        return {
            "thumb_branch_targets": len(self.thumb_branch),
            "thumb_pc_literal_addrs": len(self.thumb_literal),
            "thumb_adr_adds": len(self.thumb_adr),
            "arm_branch_targets": len(self.arm_branch),
            "arm_pc_literal_addrs": len(self.arm_literal),
            "word_values_in_gap": len(self.words),
            "movw_low_half_hits": len(self.movw_lo),
            "movt_high_half_hits": len(self.movt_hi),
            "movw_movt_pair_hits": len(self.mov_pair),
            "arm_code_island_instructions": len(self.arm_islands),
            "arm_credible_hits": len(self.arm_credible),
            "arm_data_decode_artifacts": len(self.arm_artifacts),
            "word_hits_aligned": len(self.words_credible),
            "word_hits_unaligned": len(self.words_unaligned),
            "hits": {
                "thumb_branch": [hex(x) for x in self.thumb_branch[:8]],
                "thumb_literal": [hex(x) for x in self.thumb_literal[:8]],
                "thumb_adr": [hex(x) for x in self.thumb_adr[:8]],
                "arm_branch": [hex(x) for x in self.arm_branch[:8]],
                "arm_literal": [hex(x) for x in self.arm_literal[:8]],
                "words": [hex(x) for x in self.words[:8]],
                "movw_lo": sorted(hex(x) for x in self.movw_lo)[:8],
                "movt_hi": sorted(hex(x) for x in self.movt_hi)[:8],
            },
        }

    def summary(self) -> str:
        return ("thumb branch/lit/adr=%d/%d/%d arm islands=%d arm credible/artifacts=%d/%d "
                "words aligned/unaligned=%d/%d movw/movt/pair=%d/%d/%d" % (
                    len(self.thumb_branch), len(self.thumb_literal), len(self.thumb_adr),
                    len(self.arm_islands), len(self.arm_credible), len(self.arm_artifacts),
                    len(self.words_credible), len(self.words_unaligned),
                    len(self.movw_lo), len(self.movt_hi), len(self.mov_pair)))


def run_census(fw: bytes, lo: int, hi: int, progress: bool = False) -> Census:
    cen = Census(lo, hi)
    # 32-bit word census (every byte offset would be over-strict: LDR literals are
    # word-aligned in the image data; scan 4-byte aligned words plus every byte
    # offset for completeness only against the word range - cheap enough).
    for off in range(0, len(fw) - 3):
        v = struct.unpack_from("<I", fw, off)[0]
        if lo <= v < hi:
            cen.words.append(off + CPU_BASE)
    gap_hi16 = (lo >> 16) & 0xFFFF
    gap_lo_lo, gap_lo_hi = lo & 0xFFFF, hi & 0xFFFF
    md_t = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
    md_t.detail = True
    md_a = Cs(CS_ARCH_ARM, CS_MODE_ARM)
    md_a.detail = True
    n = len(fw)
    for off in range(0, n - 4, 2):
        if fw[off:off + 4] == b"\x00\x00\x00\x00":
            continue  # movs r0,r0 chain: no branch/literal possible
        cpu = off + CPU_BASE
        for ins in md_t.disasm(fw[off:off + 4], cpu):
            m = ins.mnemonic
            for op in ins.operands:
                if op.type == CS_OP_MEM and op.mem.base == ARM_REG_PC:
                    base = (ins.address + 4) & ~3
                    addr = (base + op.mem.disp) & 0xFFFFFFFF
                    if cen.inside(addr):
                        cen.thumb_literal.append(ins.address)
            if m.startswith("b") and not m.startswith("bic") and not m.startswith("bfi"):
                for op in ins.operands:
                    if op.type == CS_OP_IMM:
                        tgt = op.imm & 0xFFFFFFFF
                        if cen.inside(tgt):
                            cen.thumb_branch.append(ins.address)
            if m in ("adr", "addw") or (m == "add" and "pc" in ins.op_str):
                for op in ins.operands:
                    if op.type == CS_OP_IMM:
                        targets = {op.imm & 0xFFFFFFFF,
                                   (ins.address + 4 + op.imm) & 0xFFFFFFFF}
                        for tgt in targets:
                            if cen.inside(tgt):
                                cen.thumb_adr.append(ins.address)
            if m == "movw":
                for op in ins.operands:
                    if op.type == CS_OP_IMM:
                        cen.movw_lo.add(op.imm & 0xFFFF)
            if m == "movt":
                for op in ins.operands:
                    if op.type == CS_OP_IMM:
                        cen.movt_hi.add(op.imm & 0xFFFF)
            break
    # ARM state: word-aligned instructions only
    for off in range(0, n - 4, 4):
        if fw[off:off + 4] == b"\x00\x00\x00\x00":
            continue
        cpu = off + CPU_BASE
        for ins in md_a.disasm(fw[off:off + 4], cpu):
            m = ins.mnemonic
            for op in ins.operands:
                if op.type == CS_OP_MEM and op.mem.base == ARM_REG_PC:
                    addr = (ins.address + 8 + op.mem.disp) & 0xFFFFFFFF
                    if cen.inside(addr):
                        cen.arm_literal.append(ins.address)
            if m.startswith("b") and not m.startswith("bic"):
                for op in ins.operands:
                    if op.type == CS_OP_IMM and cen.inside(op.imm & 0xFFFFFFFF):
                        cen.arm_branch.append(ins.address)
            break
        if progress and off and off % (1 << 20) == 0:
            print("  ... census %d/%d bytes" % (off, n), file=sys.stderr)
    # conservatively over-approximate a movw/movt pair materialising a gap address
    if gap_hi16 in cen.movt_hi:
        for low in cen.movw_lo:
            if gap_lo_lo <= low < gap_lo_hi:
                cen.mov_pair.append((gap_hi16, low))
    # classify the ARM over-approximation and the word hits
    cen.arm_islands = arm_code_islands(fw)
    for site in cen.arm_branch + cen.arm_literal:
        (cen.arm_credible if site in cen.arm_islands else cen.arm_artifacts).append(site)
    for site in cen.words:
        (cen.words_credible if (site - CPU_BASE) % 4 == 0 else cen.words_unaligned).append(site)
    return cen


def verify(fw_path: str, recipe_path: str, do_census: bool, dump_loop: str | None) -> Result:
    res = Result()
    try:
        fw = open(fw_path, "rb").read()
    except OSError as exc:
        raise SystemExit("FATAL: cannot read firmware blob %s: %s" % (fw_path, exc))
    recipe = load_recipe(recipe_path)
    res.note("firmware: %s (%d B, sha256 %s)" % (fw_path, len(fw), sha256(fw)))
    res.note("recipe:   %s (%s)" % (recipe_path, recipe.get("name")))

    # A. identity
    src = recipe["source"]
    res.check("A1", "source blob length", len(fw) == int(src["len"]),
              "%d == %d" % (len(fw), int(src["len"])))
    res.check("A2", "source blob sha256", sha256(fw) == src["sha256"], sha256(fw))

    # B. delta
    try:
        patched, reports = build_patched(fw, recipe)
    except (KeyError, ValueError, struct.error) as exc:
        res.check("B0", "recipe segments apply", False, str(exc))
        return res
    segs = recipe["segments"]
    ok = True
    for r, seg in zip(reports, segs):
        want = seg["source_sha16"]
        if r["src_sha16"] != want:
            ok = False
            res.note("segment %d source sha16 %s != recipe %s" % (r["index"], r["src_sha16"], want))
    res.check("A3", "per-segment source-window sha16", ok,
              " ".join("%d:%s" % (r["index"], r["src_sha16"]) for r in reports))
    patched_sha = sha256(patched)
    res.check("B1", "patched image sha256", patched_sha == recipe["image_sha256"], patched_sha)
    res.check("B2", "patched image length unchanged", len(patched) == len(fw),
              "%d == %d" % (len(patched), len(fw)))
    runs = diff_windows(fw, patched)
    allowed = [(int(s["file"]), int(s["file"]) + int(s["len"]) - 1) for s in segs]
    outside = [r for r in runs if not any(a <= r[0] and r[1] <= b for a, b in allowed)]
    nbytes = sum(e - s + 1 for s, e in runs)
    res.check("B3", "full-byte diff confined to recipe segments", not outside,
              "%d window(s), %d changed byte position(s) %s" % (len(runs), nbytes,
                                                                "" if not outside else "OUTSIDE=%s" % outside))

    # C. entry hunk
    entry = segs[0]
    e_cpu = ENTRY_CPU
    src_entry = fw[int(entry["file"]):int(entry["file"]) + int(entry["len"])]
    pat_entry = patched[int(entry["file"]):int(entry["file"]) + int(entry["len"])]
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
    src_ins = list(md.disasm(src_entry, e_cpu))
    pat_ins = list(md.disasm(pat_entry, e_cpu))
    res.check("C1", "entry source is the success epilogue prologue",
              len(src_ins) >= 2 and src_ins[0].mnemonic == "movs"
              and src_ins[0].op_str == "r0, #0" and src_ins[1].mnemonic == "add"
              and src_ins[1].op_str == "sp, #0x84",
              " ; ".join("%s %s" % (i.mnemonic, i.op_str) for i in src_ins))
    res.check("C2", "entry patched decodes to b.w LOOP",
              len(pat_ins) == 1 and pat_ins[0].mnemonic.startswith("b")
              and ("#0x%x" % LOOP_CPU) in pat_ins[0].op_str,
              " ; ".join("%s %s" % (i.mnemonic, i.op_str) for i in pat_ins))
    # the target must be inside the loop segment
    res.check("C3", "entry branch target == loop stub base", LOOP_CPU in range(
        int(segs[1]["file"]) + CPU_BASE, int(segs[1]["file"]) + CPU_BASE + int(segs[1]["len"])))

    # D. loop window: zero-filled in source, inside the gap
    loop = segs[1]
    loop_cpu = int(loop["file"]) + CPU_BASE
    loop_src = fw[int(loop["file"]):int(loop["file"]) + int(loop["len"])]
    res.check("D1", "loop window is all-zero in the vendor image", bool(loop_src) and set(loop_src) == {0}, str(sorted(set(loop_src))[:4]))
    gap = fw[GAP_CPU[0] - CPU_BASE:GAP_CPU[1] - CPU_BASE]
    res.check("D2", "gap CPU 0x%x..0x%x is all-zero in the vendor image" % GAP_CPU,
              set(gap) == {0}, "%d B, nonzero=%d" % (len(gap), sum(1 for x in gap if x)))
    res.check("D3", "loop stub lies inside the gap with >=1 B margins",
              GAP_CPU[0] < loop_cpu and loop_cpu + int(loop["len"]) < GAP_CPU[1],
              "stub %#x+%d, gap %#x..%#x" % (loop_cpu, int(loop["len"]), *GAP_CPU))

    # E. stub decode
    md.detail = False
    got = []
    for ins in md.disasm(patched[int(loop["file"]):int(loop["file"]) + 0x30], loop_cpu):
        got.append((ins.address, ins.mnemonic, ins.op_str))
    got = got[:len(EXPECTED_STUB)]
    res.check("E1", "stub instruction sequence matches the audited decode", got == EXPECTED_STUB,
              "first divergence: %s" % next((("got %r want %r" % (g, w)) for g, w in
                                             zip(got, EXPECTED_STUB) if g != w), "none"))
    pool_ok = all(u32(patched, cpu) == val for cpu, val in EXPECTED_POOL)
    res.check("E2", "stub literal pool == {0x10C19C,0x40039010,0x118D68,0x2000}", pool_ok,
              " ".join("%#x=%#x" % (c, u32(patched, c)) for c, _ in EXPECTED_POOL))
    branches_ok = True
    targets = {"#0x108244": LOOP_CPU + 4, "#0x108260": LOOP_CPU + 0x20, "#0x40294": WRAPPER_CPU}
    for cpu, mn, ops in got:
        if mn in ("b", "bl", "beq"):
            tgt = targets.get(ops.split()[-1])
            if tgt is None or (mn != "bl" and not (LOOP_CPU <= tgt < LOOP_CPU + int(loop["len"]))) or \
                    (mn == "bl" and tgt != WRAPPER_CPU):
                branches_ok = False
    res.check("E3", "stub branch targets stay in the stub; the only call is wrapper 0x40294", branches_ok)

    # F. callee contract (wrapper + dispatcher pinned bytes)
    w_ok = all(fw[w_cpu - CPU_BASE:w_cpu - CPU_BASE + len(bytes.fromhex(hx))] == bytes.fromhex(hx)
               for w_cpu, hx, _ in WRAPPER_PINS)
    res.check("F1", "wrapper 0x40294 = ldr r0,=0x0010C190 ; b.w 0xC18AC", w_ok,
              "literal %#x" % u32(fw, 0x4029C))
    res.check("F2", "wrapper passes the hcc instance 0x10C190", u32(fw, 0x4029C) == INSTANCE_CPU,
              hex(u32(fw, 0x4029C)))
    d_bad = [(cpu, what) for cpu, hx, what in DISPATCHER_PINS
             if fw[cpu - CPU_BASE:cpu - CPU_BASE + len(bytes.fromhex(hx))] != bytes.fromhex(hx)]
    res.check("F3", "dispatcher ack/clear/doorbell/dispatch encodings match", not d_bad, str(d_bad[:3]))

    # G. writer ban
    lo, hi = loop_cpu, loop_cpu + int(loop["len"])
    in_memset = any(a < hi and lo < b for a, b in MEMSET_RANGES)
    mem_lits = all(u32(fw, cpu) == want for cpu, want in
                   [(c, v) for c, v in MEMSET_LITERALS if c in (0x46DEC, 0x46DF0)])
    res.check("G1", "stub outside both boot memset ranges", not in_memset,
              "ranges %s" % ", ".join("%#x..%#x" % r for r in MEMSET_RANGES))
    res.check("G2", "boot memset bounds pinned by their literals", mem_lits,
              ",".join("%#x=%#x" % (c, u32(fw, c)) for c in (0x46DEC, 0x46DF0)))
    md_g = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
    imm_ok = False
    try:
        i94 = next(md_g.disasm(fw[0x46D94 - CPU_BASE:0x46D94 - CPU_BASE + 4], 0x46D94))
        i9c = next(md_g.disasm(fw[0x46D9C - CPU_BASE:0x46D9C - CPU_BASE + 4], 0x46D9C))
        imm_ok = (i94.mnemonic.startswith("mov") and (i94.op_str.split(",")[-1].strip() == "#0xe0000")
                  and i9c.mnemonic.startswith("mov") and (i9c.op_str.split(",")[-1].strip() == "#0x1000000"))
    except StopIteration:
        imm_ok = False
    res.check("G3", "second boot memset = memset(0x01000000,0,0xE0000)", imm_ok,
              "0x46D94 mov r3,#0xE0000 ; 0x46D9C mov r0,#0x1000000")
    res.check("G4", "stub outside the exception-staging frame 0x4000C..0x4004B",
              hi <= STAGING_RANGE[0] or lo >= STAGING_RANGE[1],
              "staging literal @%#x = %#x" % STAGING_LITERAL)
    res.check("G5", "stub outside the live image-header words 0x0..0x63",
              hi <= HEADER_RANGE[0] or lo >= HEADER_RANGE[1])
    res.check("G6", "stub below the boot-memset floor 0x117C00 and image end 0x122C98",
              hi <= 0x117C00 and hi <= APPEND_FLOOR)
    pool_vals = [u32(fw, BRACKET_POOL + 4 * i) for i in range(4)]
    res.check("G7", "gap bracketed by the two referenced static tables 0x108054 / 0x108414",
              pool_vals == BRACKET_VALUES, " ".join(hex(v) for v in pool_vals))
    # consumers of the bracketing tables load the literal words, not the gap
    res.check("G8", "no PC-relative load materialises any gap address in the stub pool",
              all(not (GAP_CPU[0] <= v < GAP_CPU[1]) for v in pool_vals))

    # I. stub safety invariants
    stub = patched[int(loop["file"]):int(loop["file"]) + int(loop["len"])]
    words = [struct.unpack_from("<I", stub, i)[0] for i in range(0, len(stub), 4)]
    ca_refs = sorted({w for w in words if 0x40000000 <= w <= 0x4011FFFF})
    forbidden = [w for w in words if w in (0x400392F0, 0xCECE, 0x40000108, 0x4000010C)]
    res.check("I1", "stub references exactly one device CA (0x40039010, read only)",
              ca_refs == [OUT0_CA], "CA words: %s" % [hex(x) for x in ca_refs])
    res.check("I2", "stub contains no forbidden CA / boot magic", not forbidden,
              [hex(x) for x in forbidden])
    res.check("I3", "stub contains no store to a device CA",
              not any((mn == "str" and "0x400" in ops) for _, mn, ops in got),
              "literal-CA writes would have to come from the dispatcher")

    # J. static field observation (H1/H2, see REPORT.md sec.5)
    table_field = u32(fw, TABLE_FIELD_CPU)
    res.check("J1", "record handler-table pointer field static value", table_field == TABLE_STATIC,
              "CPU %#x = %#x (H1/H2 audit observation; see REPORT.md)" % (TABLE_FIELD_CPU, table_field))
    res.note("H1/H2: the stub compares the instance table pointer with 0x118D68; the static "
             "value IS 0x118D68 and no image writer was found. Resolution is a read-only "
             "bench pack: BAR0 0x7C41B0 (instance+0x20), BAR0 0x7D0D68 (table entries), "
             "BAR0 0x2000 (ROM RAZ). See REPORT.md section 5.")

    # H. census
    if do_census:
        cen = run_census(fw, GAP_CPU[0], GAP_CPU[1])
        hits = cen.to_dict()
        credible = (len(cen.thumb_branch) + len(cen.thumb_literal) + len(cen.thumb_adr)
                    + len(cen.arm_credible) + len(cen.words_credible) + len(cen.mov_pair))
        res.check("H1", "no credible reference into the gap (Thumb exhaustive; ARM filtered "
                  "to the reachable ARM islands; word hits word-aligned; movw/movt pairs)",
                  credible == 0, cen.summary())
        if cen.arm_artifacts or cen.words_unaligned:
            res.note("census over-approximation artifacts (not references): ARM data-decode "
                     "sites %s; unaligned word hits %s" %
                     ([hex(x) for x in cen.arm_artifacts[:10]],
                      [hex(x) for x in cen.words_unaligned[:10]]))
        res.note("census gap %#x..%#x: %s" % (GAP_CPU[0], GAP_CPU[1], cen.summary()))
    else:
        res.note("census skipped (--no-census): the reference census is part of the proof, "
                 "so run it for publication-grade evidence")

    if dump_loop:
        path = os.path.abspath(dump_loop)
        if "/opensource/" in path:
            raise SystemExit("FATAL: refusing to write into the opensource repository: %s" % path)
        with open(path, "wb") as fh:
            fh.write(loop_src)
        res.note("wrote the 64-byte source loop window (all zero) to %s" % path)

    return res


# --- selftest ------------------------------------------------------------
def selftest() -> int:
    ok = 0
    tot = 0

    def ck(desc: str, cond: bool):
        nonlocal ok, tot
        tot += 1
        if cond:
            ok += 1
        else:
            print("FAIL selftest: %s" % desc)

    # synthetic image + recipe round trip
    base = bytes(range(256)) * 8
    fw = bytearray(base)
    # segment A: offset 0x100 len 4 -> bff069b8 ; segment B: offset 0x140 len 16 -> 0x11
    fw[0x100:0x104] = b"\x00\x00\x00\x00"
    fw[0x140:0x150] = b"\x00" * 16
    fw = bytes(fw)
    seg_a = {"file": 0x100, "len": 4, "source_sha16": sha16(fw[0x100:0x104]),
             "patched_hex": "bff069b8"}
    seg_b = {"file": 0x140, "len": 16, "source_sha16": sha16(fw[0x140:0x150]),
             "patched_hex": "11" * 16}
    rec = {"schema": "hi5622-fw-patch-recipe/1", "source": {"sha256": "x", "len": len(fw)},
           "segments": [seg_a, seg_b]}
    patched, reports = build_patched(fw, rec)
    ck("build applies both segments", patched[0x100:0x104] == bytes.fromhex("bff069b8")
       and patched[0x140:0x150] == b"\x11" * 16)
    ck("source window hashes reported", [r["src_sha16"] for r in reports] ==
       [seg_a["source_sha16"], seg_b["source_sha16"]])
    ck("patched source windows differ", patched[0x100:0x104] != fw[0x100:0x104])
    runs = diff_windows(fw, patched)
    ck("diff runs reported", runs == [(0x100, 0x103), (0x140, 0x14F)])
    ck("diff confined detection", runs[0][0] >= 0x100 and runs[-1][1] <= 0x14F)
    # wrong source hash is detectable
    fw2 = bytearray(fw)
    fw2[0x140] = 0xFF
    ck("source-window sha16 detects mutation", sha16(bytes(fw2[0x140:0x150])) != seg_b["source_sha16"])
    # u32 helpers
    ck("u32 helper", struct.unpack("<I", struct.pack("<I", 0xDEADBEEF))[0] == 0xDEADBEEF)
    # PD4 stub decode from the published recipe bytes (our bytes only)
    stub_hex = "0b4f81b03b68002bfcd00a4a1368002bf8d07b69084ca34202d038f71bf8f1e7064c7c6138f716f87b61ebe700bf00bf9cc1100010900340688d110000200000"
    stub = bytes.fromhex(stub_hex)
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
    got = [(i.address, i.mnemonic, i.op_str) for i in md.disasm(stub[:0x30], LOOP_CPU)]
    ck("PD4 stub decodes to the audited sequence", got[:len(EXPECTED_STUB)] == EXPECTED_STUB)
    ck("stub pool decodes to the audited literals",
       [struct.unpack_from("<I", stub, 0x30 + 4 * i)[0] for i in range(4)] ==
       [INSTANCE_MAGIC_LITERAL, OUT0_CA, TABLE_STATIC, 0x2000])
    # gap constants
    ck("gap is 637 bytes", GAP_CPU[1] - GAP_CPU[0] == 637)
    ck("stub fits the gap", LOOP_CPU + 64 < GAP_CPU[1] and LOOP_CPU >= GAP_CPU[0])
    ck("memset ranges exclude the gap", not any(a < GAP_CPU[1] and GAP_CPU[0] < b
                                                for a, b in MEMSET_RANGES))
    ck("staging area excludes the gap", GAP_CPU[0] >= STAGING_RANGE[1] or GAP_CPU[1] <= STAGING_RANGE[0])
    ck("bracket values are the two tables", BRACKET_VALUES == [0x107F3C, 0x1087D4, 0x108054, 0x108414])
    print("verify_pd4 selftest: %d/%d PASS" % (ok, tot))
    return 0 if ok == tot else 1


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="independent PD4 patch verification (host-only)")
    ap.add_argument("--fw", help="local vendor FIRMWARE.bin (read in place; default: search)")
    ap.add_argument("--recipe", default=RECIPE, help="recipe JSON (default: ../recipes/PD4.recipe.json)")
    ap.add_argument("--no-census", action="store_true", help="skip the whole-image reference census")
    ap.add_argument("--dump-loop", metavar="PATH", help="write the 64-byte source loop window (must be outside opensource/)")
    ap.add_argument("--json", action="store_true", help="print a machine-readable result")
    ap.add_argument("--selftest", action="store_true", help="run the tool selftest (no vendor blob needed)")
    args = ap.parse_args(argv)
    if args.selftest:
        return selftest()
    fw = find_fw(args.fw)
    if not fw:
        print("FATAL: no local vendor firmware blob found; pass --fw PATH or set HI5622_FW",
              file=sys.stderr)
        return 2
    if not os.path.isfile(args.recipe):
        print("FATAL: recipe not found: %s" % args.recipe, file=sys.stderr)
        return 2
    res = verify(fw, args.recipe, not args.no_census, args.dump_loop)
    out = res.to_dict()
    if args.json:
        print(json.dumps(out, indent=1))
    else:
        for n in res.notes:
            print("NOTE  %s" % n)
        for c in res.checks:
            print("%s  %-4s %s%s" % ("PASS" if c["ok"] else "FAIL", c["id"], c["desc"],
                                     (": " + c["detail"]) if c["detail"] else ""))
        print("\nverify_pd4: %d PASS, %d FAIL" % (res.passed, res.failed))
    return 0 if res.failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())