#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""bootgate.py - host-only static-recon helper for the Hi5622V100 firmware image
(wifi-boot-gate lane).

Reads the LOCAL vendor firmware blob in place (never copies it) and prints
derived annotations only.

Address conventions (same as every lane of this project):
    CPU address = file offset + 0x40000
    host BAR0 (CPU RAM, region 5) = CPU + 0x6B8000
    host BAR0 (device IO, region 3) = 0x3B8000 + (CA - 0x40000000)
    region-0 identity window maps BAR0 offset = CPU for CPU < 0x40000

Usage:
  bootgate.py dis <cpu> [n]
  bootgate.py func <cpu> [--lo --hi]       recursive-descent CFG blocks
  bootgate.py block <cpu> [--lo --hi]      block containing cpu + predecessors
  bootgate.py trace <root> <target>        block path root -> target
  bootgate.py callers <target>             BL/BLX-imm call sites (whole image)
  bootgate.py xref <value> [value ...]     literal words + PC-relative load sites
  bootgate.py writers <addr> [--slack N]   literal-seed + straight-line store taint
  bootgate.py calls <cpu> [--lo --hi]      BL targets inside a CFG walk
  bootgate.py bytes <cpu> <n>              hex bytes (LOCAL ONLY; never commit)
  bootgate.py bar <cpu>                    print host BAR0 mapping

The blob path can be overridden with --fw or $HI5622_FW (default: the local
recovery-extract copy).  Nothing is written.
"""
import argparse
import os
import struct
import sys

try:
    import capstone
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_OP_IMM, CS_OP_REG, CS_OP_MEM
except Exception:  # pragma: no cover
    sys.exit("capstone not installed (use the repo .provision-venv)")

FW = os.environ.get("HI5622_FW",
                    "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin")
BASE = 0x40000


def is_branch(ins):
    return capstone.CS_GRP_JUMP in ins.groups or capstone.CS_GRP_CALL in ins.groups


def is_call_imm(ins):
    m = ins.mnemonic.split(".")[0]
    return m in ("bl", "blx") and ins.operands and ins.operands[0].type == CS_OP_IMM


def is_cond(ins):
    m = ins.mnemonic.split(".")[0]
    if m in ("b", "bl", "blx", "bx", "bxns", "bxj", "tbb", "tbh"):
        return False
    if m in ("cbz", "cbnz"):
        return True
    return capstone.CS_GRP_JUMP in ins.groups and m.startswith("b")


def terminates(ins):
    m = ins.mnemonic.split(".")[0]
    if m in ("bx", "bxns", "bxj", "svc", "udf", "bkpt", "tbb", "tbh"):
        return True
    if m in ("pop", "ldm", "ldmia", "ldmdb"):
        return any(o.type == CS_OP_REG and ins.reg_name(o.reg) == "pc" for o in ins.operands)
    if ins.operands and ins.operands[0].type == CS_OP_REG and ins.reg_name(ins.operands[0].reg) == "pc":
        return True
    return False


def branch_target(ins):
    if not is_branch(ins) or not ins.operands:
        return None
    if ins.mnemonic.split(".")[0] in ("cbz", "cbnz"):
        return ins.operands[1].imm if len(ins.operands) > 1 and ins.operands[1].type == CS_OP_IMM else None
    if ins.operands[0].type == CS_OP_IMM:
        return ins.operands[0].imm
    return None


def literal_of(ins):
    for op in ins.operands:
        if op.type == CS_OP_MEM and ins.reg_name(op.mem.base) == "pc":
            pc = (ins.address + 4) & ~3
            return pc + op.mem.disp
    return None


class Blob:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.d = f.read()
        self.md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
        self.md.detail = True

    def insn(self, cpu):
        off = cpu - BASE
        if off < 0 or off + 2 > len(self.d):
            return None
        hw = struct.unpack_from("<H", self.d, off)[0]
        n = 4 if hw >= 0xE800 else 2
        if off + n > len(self.d):
            return None
        try:
            ins = next(iter(self.md.disasm(self.d[off:off + n], cpu)), None)
        except Exception:
            return None
        if ins is None or len(ins.bytes) != n:
            return None
        return ins

    def word(self, cpu):
        off = cpu - BASE
        if off < 0 or off + 4 > len(self.d):
            return None
        return struct.unpack_from("<I", self.d, off)[0]


class Cfg:
    """Recursive-descent Thumb CFG over a bounded region.

    BL/BLX-imm calls are recorded but not followed (each callee is walked on
    demand).  Indirect calls (blx rN) are treated as returning calls.  bx /
    ldr pc / pop {..,pc} terminate a block."""

    def __init__(self, blob, lo, hi, max_blocks=40000):
        self.b = blob
        self.lo = lo
        self.hi = hi
        self.max_blocks = max_blocks
        self.blocks = {}
        self.work = []

    def in_region(self, a):
        return self.lo <= a < self.hi

    def walk(self, roots):
        for r in roots:
            if self.in_region(r):
                self.work.append(r)
        while self.work and len(self.blocks) < self.max_blocks:
            start = self.work.pop()
            if start in self.blocks:
                continue
            self._block(start)

    def _block(self, start):
        insns = []
        succ = []
        calls = []
        a = start
        while True:
            if not self.in_region(a):
                break
            ins = self.b.insn(a)
            if ins is None:
                break
            insns.append(ins)
            if is_call_imm(ins):
                calls.append((a, branch_target(ins)))
                a += ins.size
                continue
            if terminates(ins):
                break
            if is_cond(ins):
                t = branch_target(ins)
                nxt = a + ins.size
                if t is not None:
                    succ.append((ins.mnemonic, t))
                    if self.in_region(t):
                        self.work.append(t)
                succ.append(("fall", nxt))
                if self.in_region(nxt):
                    self.work.append(nxt)
                break
            if is_branch(ins):
                t = branch_target(ins)
                if t is None and capstone.CS_GRP_CALL in ins.groups:
                    # indirect call (blx rN): returns, fall through
                    a += ins.size
                    continue
                if t is not None:
                    succ.append(("b", t))
                    if self.in_region(t):
                        self.work.append(t)
                break
            a += ins.size
        self.blocks[start] = {"start": start, "end": a, "insns": insns,
                              "succ": succ, "calls": calls}

    def covering(self, addr):
        best = None
        for s, info in self.blocks.items():
            if s <= addr < info["end"]:
                if best is None or s > best:
                    best = s
        return best

    def preds(self, addr):
        tgt = self.covering(addr)
        out = []
        if tgt is None:
            return out
        for s, info in self.blocks.items():
            for kind, t in info["succ"]:
                if t == tgt:
                    out.append((s, kind))
        return out

    def path(self, root, target):
        rb = self.covering(root)
        tb = self.covering(target)
        if rb is None or tb is None:
            return None
        prev = {rb: None}
        q = [rb]
        while q:
            cur = q.pop(0)
            if cur == tb:
                path = [cur]
                while prev[path[-1]] is not None:
                    path.append(prev[path[-1]])
                return list(reversed(path))
            for kind, t in self.blocks[cur]["succ"]:
                c = self.covering(t)
                if c is not None and c not in prev:
                    prev[c] = cur
                    q.append(c)
        return None


def find_literal_loads(blob, value):
    """Even-offset PC-relative literal-load sites whose loaded word == value."""
    n = len(blob.d)
    hits = []
    for off in range(0, n - 4, 2):
        cpu = BASE + off
        hw = struct.unpack_from("<H", blob.d, off)[0]
        n_ins = 4 if hw >= 0xE800 else 2
        ins = blob.insn(cpu)
        if ins is None or len(ins.bytes) != n_ins:
            continue
        lit = literal_of(ins)
        if lit is None:
            continue
        w = blob.word(lit)
        if w is not None and (w | 1) == (value | 1) if value & 1 else w == value:
            hits.append((cpu, lit, w, ins))
    return hits


def load_reg(ins):
    ops = ins.operands
    if ops and ops[0].type == CS_OP_REG:
        return ins.reg_name(ops[0].reg)
    return None

def taint_windows(blob, seeds, hops=40):
    """Forward straight-line dataflow from (cpu, reg, value) seeds.

    Propagates mov/movs/add/sub immediates on tracked registers across
    unconditional fallthrough and records stores whose effective address is
    computed from a tracked register.  Does not follow branches.  Returns
    (store_cpu, kind, effective_addr, text, seed_cpu)."""
    out = []
    seen = set()
    for (seed_cpu, reg, val) in seeds:
        st = {reg: val}
        a = seed_cpu
        for step in range(hops):
            ins = blob.insn(a)
            if ins is None:
                break
            m = ins.mnemonic.split(".")[0]
            ops = ins.operands
            if is_call_imm(ins) or (m in ("blx",) and ops and ops[0].type == CS_OP_REG):
                for r in ("r0", "r1", "r2", "r3", "ip", "lr"):
                    st.pop(r, None)
            if m in ("mov", "movs") and len(ops) == 2:
                d = ins.reg_name(ops[0].reg) if ops[0].type == CS_OP_REG else None
                if d:
                    if ops[1].type == CS_OP_REG:
                        s = ins.reg_name(ops[1].reg)
                        if s in st:
                            st[d] = st[s]
                        else:
                            st.pop(d, None)
                    elif ops[1].type == CS_OP_IMM:
                        st[d] = ops[1].imm
            elif m in ("add", "adds", "add.w") and len(ops) == 3 and ops[0].type == CS_OP_REG:
                d = ins.reg_name(ops[0].reg)
                if ops[1].type == CS_OP_REG and ops[2].type == CS_OP_IMM and ins.reg_name(ops[1].reg) in st:
                    st[d] = st[ins.reg_name(ops[1].reg)] + ops[2].imm
                elif ops[1].type == CS_OP_IMM and ops[2].type == CS_OP_REG and ins.reg_name(ops[2].reg) in st:
                    st[d] = st[ins.reg_name(ops[2].reg)] + ops[1].imm
                else:
                    st.pop(d, None)
            elif m == "sub" and len(ops) == 3 and ops[0].type == CS_OP_REG:
                d = ins.reg_name(ops[0].reg)
                if ops[1].type == CS_OP_REG and ops[2].type == CS_OP_IMM and ins.reg_name(ops[1].reg) in st:
                    st[d] = st[ins.reg_name(ops[1].reg)] - ops[2].imm
                else:
                    st.pop(d, None)
            if m.startswith("str") and len(ops) >= 2 and ops[1].type == CS_OP_MEM:
                base = ins.reg_name(ops[1].mem.base)
                disp = ops[1].mem.disp
                if base in st:
                    ea = (st[base] + disp) & 0xFFFFFFFF
                    key = (a, ea)
                    if key not in seen:
                        seen.add(key)
                        out.append((a, m, ea, ins.mnemonic + " " + ins.op_str, seed_cpu))
            a += ins.size
    return out


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--fw", default=FW)
    sub = p.add_subparsers(dest="cmd", required=True)
    q = sub.add_parser("dis"); q.add_argument("cpu", type=lambda s: int(s, 0))
    q.add_argument("n", type=lambda s: int(s, 0), default=0x80)
    q = sub.add_parser("func"); q.add_argument("cpu", type=lambda s: int(s, 0))
    q.add_argument("--lo", type=lambda s: int(s, 0)); q.add_argument("--hi", type=lambda s: int(s, 0))
    q = sub.add_parser("block"); q.add_argument("cpu", type=lambda s: int(s, 0))
    q.add_argument("--lo", type=lambda s: int(s, 0)); q.add_argument("--hi", type=lambda s: int(s, 0))
    q = sub.add_parser("trace"); q.add_argument("root", type=lambda s: int(s, 0))
    q.add_argument("target", type=lambda s: int(s, 0))
    q.add_argument("--lo", type=lambda s: int(s, 0)); q.add_argument("--hi", type=lambda s: int(s, 0))
    q = sub.add_parser("callers"); q.add_argument("target", type=lambda s: int(s, 0))
    q = sub.add_parser("xref"); q.add_argument("values", nargs="+")
    q = sub.add_parser("writers"); q.add_argument("addr", type=lambda s: int(s, 0))
    q.add_argument("--hops", type=int, default=40)
    q = sub.add_parser("calls"); q.add_argument("cpu", type=lambda s: int(s, 0))
    q.add_argument("--lo", type=lambda s: int(s, 0)); q.add_argument("--hi", type=lambda s: int(s, 0))
    q = sub.add_parser("bytes"); q.add_argument("cpu", type=lambda s: int(s, 0))
    q.add_argument("n", type=lambda s: int(s, 0), default=0x10)
    q = sub.add_parser("bar"); q.add_argument("addr", type=lambda s: int(s, 0))
    a = p.parse_args()
    b = Blob(a.fw)

    def dump_block(info, lit_too=True):
        succ = ", ".join("%s->%05X" % (k, t) for k, t in info["succ"])
        print("BLOCK %05X..%05X succ: %s" % (info["start"], info["end"], succ))
        for ins in info["insns"]:
            extra = ""
            l = literal_of(ins)
            if l is not None:
                extra = "   ; lit %05X = %08X" % (l, b.word(l))
            print("   %05X: %-8s %-34s%s" % (ins.address, ins.bytes.hex(),
                  ins.mnemonic + " " + ins.op_str, extra))

    if a.cmd == "dis":
        cpu = a.cpu
        end = cpu + a.n
        while cpu < end:
            ins = b.insn(cpu)
            if ins is None:
                print("%05X: <undecodable>" % cpu); break
            extra = ""
            l = literal_of(ins)
            if l is not None:
                extra = "   ; lit %05X = %08X" % (l, b.word(l))
            print("%05X: %-10s %-32s%s" % (cpu, ins.bytes.hex(), ins.mnemonic + " " + ins.op_str, extra))
            cpu += ins.size
        return

    if a.cmd in ("func", "block", "trace", "calls"):
        if a.cmd == "func":
            root, lo = a.cpu, (a.lo if a.lo is not None else a.cpu - 0x1000)
        elif a.cmd == "trace":
            root, lo = a.root, (a.lo if a.lo is not None else min(a.root, a.target) - 0x2000)
        else:
            root, lo = a.cpu, (a.lo if a.lo is not None else a.cpu - 0x1000)
        hi = a.hi if a.hi is not None else root + 0x20000
        cfg = Cfg(b, lo, hi)
        cfg.walk([root] if a.cmd != "trace" else [a.root, a.target])
        if a.cmd == "calls":
            for blk in sorted(cfg.blocks):
                for site, t in cfg.blocks[blk]["calls"]:
                    print("  %05X: bl %05X" % (site, t))
            return
        if a.cmd == "func":
            for blk in sorted(cfg.blocks):
                dump_block(cfg.blocks[blk])
            print("blocks: %d" % len(cfg.blocks))
            return
        if a.cmd == "block":
            blk = cfg.covering(a.cpu)
            if blk is None:
                print("no block contains %05X" % a.cpu); return
            for s, kind in sorted(cfg.preds(a.cpu)):
                print("  pred %05X via %s" % (s, kind))
            dump_block(cfg.blocks[blk])
            return
        path = cfg.path(a.root, a.target)
        if path is None:
            print("NO PATH %05X -> %05X" % (a.root, a.target)); return
        for i, blk in enumerate(path):
            if i:
                prev = path[i - 1]
                for k, t in cfg.blocks[prev]["succ"]:
                    if t == blk:
                        print("   ^ entered via %s" % k)
            dump_block(cfg.blocks[blk])
        return

    if a.cmd == "callers":
        n = len(b.d)
        hits = []
        for off in range(0, n - 4, 2):
            cpu = BASE + off
            ins = b.insn(cpu)
            if ins is None:
                continue
            if is_call_imm(ins) and branch_target(ins) == a.target:
                hits.append(cpu)
        print("BL/BLX-imm callers of %05X: %d site(s)" % (a.target, len(hits)))
        for s in hits:
            print("  %05X" % s)
        return

    if a.cmd == "xref":
        d = b.d
        for sv in a.values:
            val = int(sv, 0)
            print("== value %08X ==" % val)
            pat = struct.pack("<I", val & 0xFFFFFFFF)
            start = 0
            homes = []
            while True:
                i = d.find(pat, start)
                if i < 0:
                    break
                homes.append(i + BASE)
                start = i + 1
            print("  literal-home words: %s" % (" ".join("%05X" % h for h in homes) if homes else "-"))
            ls = find_literal_loads(b, val)
            print("  PC-relative load sites: %d" % len(ls))
            for cpu, lit, w, ins in ls:
                print("  load %05X (lit %05X = %08X) %s %s" % (cpu, lit, w, ins.mnemonic, ins.op_str))
        return

    if a.cmd == "writers":
        hits = find_literal_loads(b, a.addr)
        print("== literal-load seeds with value %08X: %d ==" % (a.addr, len(hits)))
        seeds = []
        for cpu, lit, w, ins in hits:
            r = load_reg(ins)
            print("  seed %05X: lit %05X = %08X -> reg %s" % (cpu, lit, w, r))
            if r:
                seeds.append((cpu, r, w))
        print("== stores reached by straight-line taint ==")
        for (sa, kind, ea, text, seed) in taint_windows(b, seeds, hops=a.hops):
            print("  %05X (%s): %-32s -> [%08X]  (seed %05X)" % (sa, kind, text, ea, seed))
        return

    if a.cmd == "bytes":
        off = a.cpu - BASE
        print(b.d[off:off + a.n].hex())
        return

    if a.cmd == "bar":
        v = a.addr
        if v < 0x40000000:
            print("CPU %05X: BAR0(region5 CPU-RAM alias) = %06X ; identity(region0) = %05X" % (v, v + 0x6B8000, v))
        else:
            print("CA %08X: BAR0(region3 device IO) = %06X" % (v, 0x3B8000 + (v - 0x40000000)))
        return


if __name__ == "__main__":
    main()

