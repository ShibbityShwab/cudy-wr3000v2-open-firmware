#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_cfg.py - host-only control-flow / literal-field helper for the Hi5622V100
firmware image (wifi-fw-link-event lane).

Reads the LOCAL vendor firmware blob in place (never copies it) and prints
derived annotations. Address conventions used by every lane of this project:

    CPU address = file offset + 0x40000
    host BAR0 (CPU RAM)    = CPU + 0x6B8000
    host BAR0 (device IO)  = 0x3B8000 + (CA - 0x40000000)

The image is a flat blob with embedded literal pools, so a plain linear sweep
desynchronises inside pools.  This tool does a recursive-descent basic-block
walk from an explicit entry point (e.g. a header entry or a dispatch-table
handler) and never decodes bytes that no branch reaches.  It resolves every
PC-relative LDR to its literal address/value, which is how the "boot-region
dispatch table" at CPU 0x474B8 was identified as entry A's literal pool.

Usage:
  fw_cfg.py dump <cpu> [nbytes]
  fw_cfg.py func <cpu> [--lo 0x..] [--hi 0x..] [--max 4000]
  fw_cfg.py reach <root> <target>[,<target>...] [--lo --hi --max]
  fw_cfg.py back <target> [--root <cpu>] [--lo --hi --max]
  fw_cfg.py lits <cpu> [--lo --hi --max]     # literal words loaded by a function
  fw_cfg.py calls <cpu> [--lo --hi]
  fw_cfg.py findlit <value> [<value> ...]    # literal words + the load sites
  fw_cfg.py movimm <value>                   # movw/movt constructions of a value

Nothing is written by the tool; the blob path can be overridden with --fw or
$HI5622_FW (default is the local recovery-extract copy).
"""
import argparse
import os
import struct
import sys

try:
    import capstone
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_OP_IMM, CS_OP_REG, CS_OP_MEM
    CAP_OK = True
except Exception:                                     # pragma: no cover
    CAP_OK = False

FW = os.environ.get("HI5622_FW",
                    "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin")
BASE = 0x40000

BRANCH_COND = ("beq", "bne", "bcs", "bcc", "bmi", "bpl", "bvs", "bvc",
               "bhi", "bls", "bge", "blt", "bgt", "ble", "bal")
TERMINATORS = ("bx", "bxns", "blx", "pop", "ldm", "tbb", "tbh", "svc", "udf",
               "bkpt", "bxj")


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
        ins = next(iter(self.md.disasm(self.d[off:off + n], cpu)), None)
        if ins is None or len(ins.bytes) != n:
            return None
        return ins

    def word(self, cpu):
        off = cpu - BASE
        if off < 0 or off + 4 > len(self.d):
            return None
        return struct.unpack_from("<I", self.d, off)[0]


def reg_name(ins, op):
    try:
        return ins.reg_name(op.reg)
    except Exception:
        return "?"


def literal_of(ins):
    """(literal_cpu, value) for a Thumb LDR/PLD-style PC-relative load."""
    for op in ins.operands:
        if op.type == CS_OP_MEM and ins.reg_name(op.mem.base) == "pc":
            pc = (ins.address + 4) & ~3
            return pc + op.mem.disp
    return None


def is_branch(ins):
    return capstone.CS_GRP_JUMP in ins.groups or capstone.CS_GRP_CALL in ins.groups


def branch_imm(ins):
    if not ins.operands:
        return None
    if ins.operands[0].type != CS_OP_IMM:
        return None
    if not is_branch(ins):
        return None
    return ins.operands[0].imm


def is_call(ins):
    m = ins.mnemonic.split(".")[0]
    if m in ("bl", "blx"):
        return ins.operands and ins.operands[0].type == CS_OP_IMM
    return False


def is_uncond(ins):
    m = ins.mnemonic.split(".")[0]
    return m == "b" and capstone.CS_GRP_JUMP in ins.groups


def is_cond(ins):
    m = ins.mnemonic.split(".")[0]
    if m in ("b", "bl", "blx", "bx", "bxns", "bxj"):
        return False
    if m in ("tbb", "tbh"):
        return False
    if m.startswith("b") and capstone.CS_GRP_JUMP in ins.groups:
        return True
    if m in ("cbz", "cbnz"):
        return True
    return False


def terminates(ins):
    m = ins.mnemonic.split(".")[0]
    if m in ("bx", "bxns", "bxj"):
        return True
    if m in ("svc", "udf", "bkpt"):
        return True
    if m in ("tbb", "tbh"):
        return True
    if m in ("pop", "ldm"):
        return any(o.type == CS_OP_REG and ins.reg_name(o.reg) == "pc"
                   for o in ins.operands)
    if ins.operands and ins.operands[0].type == CS_OP_REG and ins.reg_name(ins.operands[0].reg) == "pc":
        return True          # ldr pc / mov pc / add pc / blx pc
    if m == "blx" and ins.operands and ins.operands[0].type == CS_OP_REG:
        return True
    return False


def edge_label(ins):
    m = ins.mnemonic.split(".")[0]
    if m in ("cbz", "cbnz"):
        return m
    if m == "b":
        return "b"
    if m in BRANCH_COND or m.startswith("b"):
        return m
    if m in ("it", "itt", "ite", "ittt", "itte", "itet", "itee"):
        return m
    return m


class Cfg:
    def __init__(self, blob, lo, hi, max_blocks=20000):
        self.b = blob
        self.lo = lo
        self.hi = hi
        self.max_blocks = max_blocks
        self.blocks = {}     # start -> dict(end, insns, succ=list[(kind,target)], terms)
        self.calls = {}      # start -> [(call site, target)]

    def in_region(self, a):
        return self.lo <= a < self.hi

    def walk(self, roots):
        work = [r for r in roots if self.in_region(r)]
        seen = set()
        while work and len(self.blocks) < self.max_blocks:
            start = work.pop()
            if start in seen:
                continue
            seen.add(start)
            self._block(start, work)

    def _block(self, start, work):
        insns = []
        succ = []
        calls = []
        a = start
        end = None
        while True:
            ins = self.b.insn(a)
            if ins is None or not self.in_region(a):
                end = a
                break
            insns.append(ins)
            if is_call(ins):
                t = branch_imm(ins)
                if t is not None:
                    calls.append((a, t))
                a += ins.size
                continue
            if is_uncond(ins):
                t = branch_imm(ins)
                if t is not None:
                    succ.append(("b", t))
                    if self.in_region(t):
                        work.append(t)
                end = a + ins.size
                break
            if is_cond(ins):
                if ins.mnemonic.startswith("cb"):
                    t = ins.operands[1].imm if len(ins.operands) > 1 else None
                else:
                    t = branch_imm(ins)
                nxt = a + ins.size
                if t is not None:
                    succ.append((edge_label(ins), t))
                    if self.in_region(t):
                        work.append(t)
                succ.append(("fall", nxt))
                if self.in_region(nxt):
                    work.append(nxt)
                end = nxt
                break
            if terminates(ins):
                end = a + ins.size
                break
            a += ins.size
        self.blocks[start] = {"end": end, "insns": insns, "succ": succ}
        self.calls[start] = calls

    # ---- graph queries -------------------------------------------------
    def containing(self, addr):
        """block whose [start,end) range contains addr (largest start <= addr)."""
        best = None
        for s, info in self.blocks.items():
            if s <= addr < info["end"]:
                best = s
        return best

    def succ_starts(self, blk):
        out = []
        for kind, t in self.blocks[blk]["succ"]:
            c = self.containing(t)
            if c is not None and c not in out:
                out.append(c)
        return out

    def reach_path(self, root, targets):
        tset = set(targets)
        prev = {root: None}
        q = [root]
        while q:
            cur = q.pop(0)
            if cur in tset:
                path = [cur]
                while prev[path[-1]] is not None:
                    path.append(prev[path[-1]])
                return list(reversed(path))
            for t in self.succ_starts(cur):
                if t in prev:
                    continue
                prev[t] = cur
                q.append(t)
        return None

    def ancestors(self, target):
        rev = {}
        for blk in self.blocks:
            for t in self.succ_starts(blk):
                rev.setdefault(t, []).append(blk)
        seen = {target}
        q = [target]
        while q:
            cur = q.pop(0)
            for p in rev.get(cur, ()):
                if p in seen:
                    continue
                seen.add(p)
                q.append(p)
        return seen

    def all_literals(self):
        out = {}
        for blk, info in self.blocks.items():
            for ins in info["insns"]:
                l = literal_of(ins)
                if l is not None:
                    out.setdefault(l, []).append((blk, ins.address))
        return out


def print_block(cfg, blk, show_insns=True):
    info = cfg.blocks[blk]
    succ = ", ".join("%s->%05X" % (k, t) for k, t in info["succ"])
    print("BLOCK %05X..%05X  succ: %s" % (blk, info["end"], succ))
    if not show_insns:
        return
    for ins in info["insns"]:
        l = literal_of(ins)
        extra = ""
        if l is not None:
            w = cfg.b.word(l)
            if w is not None:
                extra = "   [lit %05X = %08X]" % (l, w)
        print("   %05X: %-24s %s%s" % (ins.address, ins.mnemonic + " " + ins.op_str, "", extra))


def guess_region(a):
    """default region for a function walk: window around the entry."""
    return a, a + 0x8000


def main():
    if not CAP_OK:
        sys.exit("capstone not installed (use the repo .provision-venv)")
    p = argparse.ArgumentParser()
    p.add_argument("--fw", default=FW)
    p.add_argument("--lo", type=lambda s: int(s, 0), default=None)
    p.add_argument("--hi", type=lambda s: int(s, 0), default=None)
    p.add_argument("--max", type=int, default=20000)
    sub = p.add_subparsers(dest="cmd", required=True)
    q = sub.add_parser("dump"); q.add_argument("cpu", type=lambda s: int(s, 0)); q.add_argument("n", type=lambda s: int(s, 0), default=0x80)
    q = sub.add_parser("func"); q.add_argument("cpu", type=lambda s: int(s, 0)); q.add_argument("--noblocks", action="store_true")
    q = sub.add_parser("reach"); q.add_argument("root", type=lambda s: int(s, 0)); q.add_argument("targets"); q.add_argument("--compact", action="store_true")
    q = sub.add_parser("back"); q.add_argument("target", type=lambda s: int(s, 0)); q.add_argument("--root", type=lambda s: int(s, 0), default=None)
    q = sub.add_parser("lits"); q.add_argument("cpu", type=lambda s: int(s, 0))
    q = sub.add_parser("calls"); q.add_argument("cpu", type=lambda s: int(s, 0))
    q = sub.add_parser("findlit"); q.add_argument("values", nargs="+")
    q = sub.add_parser("movimm"); q.add_argument("value")
    q = sub.add_parser("pool"); q.add_argument("lo", type=lambda s: int(s, 0)); q.add_argument("hi", type=lambda s: int(s, 0))
    q = sub.add_parser("regs")
    a = p.parse_args()
    b = Blob(a.fw)
    if a.cmd == "dump":
        cpu = a.cpu
        for ins in b.md.disasm(b.d[cpu - BASE: cpu - BASE + a.n], cpu):
            l = literal_of(ins)
            extra = ""
            if l is not None:
                w = b.word(l)
                extra = "   [lit %05X = %08X]" % (l, w if w is not None else -1)
            print("%05X: %-10s %-30s%s" % (ins.address, ins.bytes.hex(), ins.mnemonic + " " + ins.op_str, extra))
        return
    if a.cmd in ("func", "reach", "back", "lits", "calls"):
        if a.cmd == "func":
            root = a.cpu
        elif a.cmd == "reach":
            root = a.root
        elif a.cmd == "back":
            root = a.root if a.root is not None else a.lo
        elif a.cmd == "lits":
            root = a.cpu
        else:
            root = a.cpu
        lo = a.lo if a.lo is not None else root
        hi = a.hi if a.hi is not None else (root + 0x8000)
        cfg = Cfg(b, lo, hi, a.max)
        cfg.walk([root])
        if a.cmd == "func":
            for blk in sorted(cfg.blocks):
                print_block(cfg, blk, not a.noblocks)
            print("blocks: %d" % len(cfg.blocks))
        elif a.cmd == "reach":
            targets = [int(x, 0) for x in a.targets.split(",")]
            # map target cpu to enclosing block (largest block start <= target)
            starts = sorted(cfg.blocks)
            def enclosing(t):
                best = None
                for s in starts:
                    if s <= t:
                        best = s
                return best
            rootblk = cfg.containing(root)
            if rootblk is None:
                print("root %05X not inside a decoded block" % root)
                return
            path = cfg.reach_path(rootblk, [cfg.containing(t) for t in targets if cfg.containing(t) is not None])
            if path is None:
                print("NO PATH %05X -> %s" % (root, a.targets))
                return
            for i, blk in enumerate(path):
                if not a.compact:
                    print_block(cfg, blk)
                else:
                    succ = ", ".join("%s->%05X" % (k, t) for k, t in cfg.blocks[blk]["succ"])
                    print("BLOCK %05X..%05X succ: %s" % (blk, cfg.blocks[blk]["end"], succ))
                    for ins in cfg.blocks[blk]["insns"]:
                        m = ins.mnemonic.split(".")[0]
                        if m in ("cmp", "tst", "cbz", "cbnz") or (m.startswith("b") and m not in ("bl", "blx", "bx", "bic", "bfi", "bfc", "bic.w")) or m in ("ldrb", "ldrh", "ldr"):
                            l = literal_of(ins)
                            extra = ""
                            if l is not None:
                                w = cfg.b.word(l)
                                extra = "   [lit %05X = %08X]" % (l, w if w is not None else -1)
                            print("   %05X: %-22s %s%s" % (ins.address, ins.mnemonic + " " + ins.op_str, "", extra))
                if i:
                    prev = path[i - 1]
                    for k, t in cfg.blocks[prev]["succ"]:
                        if t == blk:
                            print("   ^ entered via %s" % k)
        elif a.cmd == "back":
            anc = cfg.ancestors(a.target)
            print("ancestors of %05X from root-walk: %d blocks" % (a.target, len(anc)))
            for blk in sorted(anc):
                info = cfg.blocks[blk]
                print("  %05X..%05X" % (blk, info["end"]))
        elif a.cmd == "lits":
            for lit, sites in sorted(cfg.all_literals().items()):
                w = b.word(lit)
                print("lit %05X = %08X  sites: %s" % (lit, w if w is not None else -1,
                      " ".join("%05X" % s for s, _ in sites)))
        elif a.cmd == "calls":
            for blk in sorted(cfg.calls):
                for site, t in cfg.calls[blk]:
                    print("  %05X: bl %05X" % (site, t))
        return
    if a.cmd == "pool":
        # every PC-relative literal load whose literal word lies in [lo,hi):
        # this is the census of the CONSUMERS of a literal-pool/table range.
        n = len(b.d)
        for off in range(0, n - 4, 2):
            cpu = BASE + off
            hw = struct.unpack_from("<H", b.d, off)[0]
            n_ins = 4 if hw >= 0xE800 else 2
            ins = b.insn(cpu)
            if ins is None or len(ins.bytes) != n_ins:
                continue
            lit = literal_of(ins)
            if lit is None or not (a.lo <= lit < a.hi):
                continue
            w = b.word(lit)
            print("load %05X (lit %05X) = %08X   insn %s %s" % (cpu, lit, w if w is not None else -1, ins.mnemonic, ins.op_str))
        return
    if a.cmd == "regs":
        # event-handler registrations: BL to 0xC6DA2 (register+enable, Thumb-ptr
        # convention) and BL to 0xC74B0 (raw register). Decode r0/r1/r2 backwards
        # from the call site through straight-line code.
        import capstone as _cs
        def find_calls(target):
            t = target | 1
            out = []
            for off in range(0, len(b.d) - 4, 2):
                cpu = BASE + off
                ins = b.insn(cpu)
                if ins is None:
                    continue
                if ins.mnemonic.split(".")[0] in ("bl", "blx") and ins.operands and ins.operands[0].type == _cs.CS_OP_IMM:
                    if (ins.operands[0].imm | 1) == t:
                        out.append(cpu)
            return out
        def back(cpu, maxback=24):
            """straight-line preceding instructions (decode from a window; robust
            enough because call sites are preceded by a few setup insns)."""
            seq = []
            a = cpu - maxback * 2
            while a < cpu:
                ins = b.insn(a)
                if ins is None:
                    a += 2
                    continue
                seq.append(ins)
                a += ins.size
            return seq
        def regs_at(cpu):
            r = {"r0": None, "r1": None, "r2": None}
            for ins in back(cpu):
                m = ins.mnemonic.split(".")[0]
                if m in ("movs", "mov") and len(ins.operands) >= 2:
                    d = ins.operands[0]
                    if d.type == _cs.CS_OP_REG and ins.reg_name(d.reg) in r and ins.operands[1].type == _cs.CS_OP_IMM:
                        r[ins.reg_name(d.reg)] = ins.operands[1].imm
                if m.startswith("ldr") and len(ins.operands) >= 2:
                    d = ins.operands[0]
                    s = ins.operands[1]
                    if d.type == _cs.CS_OP_REG and ins.reg_name(d.reg) in r and s.type == _cs.CS_OP_MEM and ins.reg_name(s.mem.base) == "pc":
                        lit = ((ins.address + 4) & ~3) + s.mem.disp
                        r[ins.reg_name(d.reg)] = b.word(lit)
            return r
        print("== event-handler registrations (calls to 0xC6DA2 / 0xC74B0) ==")
        for name, t in (("0xC6DA2", 0xC6DA2), ("0xC74B0", 0xC74B0)):
            for cpu in find_calls(t):
                r = regs_at(cpu)
                print("  %s at %05X: id=%s level/handler=%s callarg r1=%s" % (
                    name, cpu,
                    hex(r["r0"]) if r["r0"] is not None else "?",
                    hex(r["r2"]) if r["r2"] is not None else "?",
                    hex(r["r1"]) if r["r1"] is not None else "?"))
        return
    if a.cmd == "findlit":
        vals = set(int(v, 0) for v in a.values)
        d = b.d
        print("== literal words equal to the requested values ==")
        for val in sorted(vals):
            pat = struct.pack("<I", val & 0xffffffff)
            start = 0
            homes = []
            while True:
                i = d.find(pat, start)
                if i < 0:
                    break
                homes.append(i + BASE)
                start = i + 1
            print("%08X: %s" % (val, " ".join("%05X" % h for h in homes) or "-"))
        print("== PC-relative LDR sites loading those values ==")
        n = len(d)
        for off in range(0, n - 4, 2):
            cpu = BASE + off
            hw = struct.unpack_from("<H", d, off)[0]
            n_ins = 4 if hw >= 0xE800 else 2
            ins = b.insn(cpu)
            if ins is None or len(ins.bytes) != n_ins:
                continue
            lit = literal_of(ins)
            if lit is None:
                continue
            w = b.word(lit)
            if w in vals:
                print("  insn %05X loads lit %05X = %08X (%s)" % (cpu, lit, w, ins.mnemonic))
        return
    if a.cmd == "movimm":
        val = int(a.value, 0)
        lo16 = val & 0xffff
        hi16 = (val >> 16) & 0xffff
        n = len(b.d)
        print("movw #%#x / movt #%#x sites (candidate-only sweep):" % (lo16, hi16))
        for off in range(0, n - 4, 2):
            cpu = BASE + off
            ins = b.insn(cpu)
            if ins is None:
                continue
            m = ins.mnemonic.split(".")[0]
            if m in ("movw", "movt") and ins.operands and ins.operands[0].type == CS_OP_IMM:
                if ins.operands[0].imm == (lo16 if m == "movw" else hi16):
                    print("  %05X: %s %s" % (cpu, ins.mnemonic, ins.op_str))
        return


if __name__ == "__main__":
    raise SystemExit(main())
