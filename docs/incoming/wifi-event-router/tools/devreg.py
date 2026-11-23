#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""devreg.py - device-register access census for the Hi5622V100 Wi-Fi firmware.

Reads the LOCAL vendor firmware blob in place (path from --fw or $HI5622_FW) and
prints derived annotations. Nothing vendor is copied; nothing is written.

Address conventions (project-wide):
    image "CPU" address = file offset + 0x40000
    device IO CA        = literal constant 0x4xxxxxxx in the code
    host BAR0 (IO)      = 0x3B8000 + (CA - 0x40000000)
    host BAR0 (RAM)     = 0x6B8000 + CPU

Commands
    dis   <cpu> [n]          annotated disassembly (Thumb by default, --arm)
    defs  [--lo --hi]        literal / movw+movt constants in a value range,
                             with the load site and nearest function start
    access <CA> [CA ...]     focused census for exact device addresses:
                             literal homes, load sites, movimm pairs, and every
                             resolved memory access (base literal + disp)
    census [--lo --hi]       every resolved memory access whose effective address
                             falls in a device-CA range, grouped by address
    fnstart <cpu>            nearest function start for an address + a short
                             annotated window around it

The forward scanner is a bounded (48 instruction) local constant tracker: it
starts at a literal/movimm definition, follows straight-line Thumb code, keeps a
register->constant map, reports every load/store whose effective address is a
device CA, and resets call-clobbered registers at BL.  It deliberately does not
follow branches, so every hit is a *candidate* that the report re-reads in full
context.  Branch/pool bytes decoded as instructions can only produce candidates
whose access never resolves, so the census is conservative.
"""
import argparse
import os
import struct
import sys

try:
    import capstone
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_ARM
    from capstone import CS_OP_IMM, CS_OP_REG, CS_OP_MEM
    CAP_OK = True
except Exception:                                     # pragma: no cover
    CAP_OK = False

FW = os.environ.get("HI5622_FW",
                    "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin")
BASE = 0x40000
IMG_LO, IMG_HI = 0x40000, 0xE2C18          # image CPU span (file 0 .. 928920)
DEV_LO, DEV_HI = 0x40000000, 0x40200000    # device IO CA sweep range

# known function starts / labels of this image (from prior lanes; CPU addresses)
KNOWN_FUNCS = {
    0x40064: "arm_irq_fiq_trampoline",
    0x46970: "entry_A_system_init",
    0xC742C: "entry_B_bootstrap",
    0xC2EFC: "event_dispatcher",
    0x46848: "device_event_pump",
    0x46848 + 0: "device_event_pump",
    0x461B6: "pump_cb_A",
    0x467D8: "pump_cb_B",
    0x46938: "event_kick_helper",
    0xC6DA2: "event_register_enable",
    0xC6DB8: "event_raise_pending",
    0xC6FF4: "event_enable",
    0xC74B0: "event_register_raw",
    0xC1A3C: "event_register_check",
    0x496C8: "pcie_msg_init",
    0x47628: "boot_task",
    0x103140: "reset_handler",
    0xC6E14: "exception_common_handler",
    0xC2EEC: "reset_dispatcher",
    0x40294: "event_4C_wrapper",
    0x402A0: "event_4E_wrapper",
    0x40670: "event_40_handler",
    0x4065C: "event_42_handler",
    0x462F8: "event_2D_handler",
    0x4624C: "event_2E_handler",
    0x40524: "event_46_handler",
    0x407C0: "mbox_helper_A",
    0x4078E: "mbox_helper_B",
    0xC6E14: "exception_common_handler",
}


class Blob:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.d = f.read()
        self.md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
        self.md.detail = True
        self.ma = Cs(CS_ARCH_ARM, CS_MODE_ARM)
        self.ma.detail = True

    def word(self, cpu):
        off = cpu - BASE
        if off < 0 or off + 4 > len(self.d):
            return None
        return struct.unpack_from("<I", self.d, off)[0]

    def thumb(self, cpu):
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

    def arm(self, cpu):
        off = cpu - BASE
        if off < 0 or off + 4 > len(self.d):
            return None
        ins = next(iter(self.ma.disasm(self.d[off:off + 4], cpu)), None)
        if ins is None or len(ins.bytes) != 4:
            return None
        return ins


def lit_of(b, ins, thumb=True):
    """(literal_cpu, value) for a PC-relative load, else None."""
    for op in ins.operands:
        if op.type == CS_OP_MEM and ins.reg_name(op.mem.base) == "pc":
            if thumb:
                pc = (ins.address + 4) & ~3
            else:
                pc = ins.address + 8
            la = pc + op.mem.disp
            return la, b.word(la)
    return None


def dst_reg(ins):
    if ins.operands and ins.operands[0].type == CS_OP_REG:
        return ins.reg_name(ins.operands[0].reg)
    return None


def is_mem_ins(ins):
    m = ins.mnemonic.split(".")[0]
    return m in ("ldr", "str", "ldrb", "strb", "ldrh", "strh", "ldrsb",
                 "ldrsh", "ldrd", "strd", "ldm", "stm", "ldmdb", "stmdb")


def mem_base_disp(ins):
    """(base_reg, disp, index_reg) for the first memory operand, else None."""
    for op in ins.operands:
        if op.type == CS_OP_MEM:
            base = ins.reg_name(op.mem.base) if op.mem.base else None
            idx = ins.reg_name(op.mem.index) if op.mem.index else None
            return base, op.mem.disp, idx
    return None


def is_cond_branch(ins):
    m = ins.mnemonic.split(".")[0]
    if m in ("b", "bl", "blx", "bx", "bxns"):
        return False
    return m.startswith("b") and capstone.CS_GRP_JUMP in ins.groups


def end_of_block(ins):
    m = ins.mnemonic.split(".")[0]
    if m in ("b", "bx", "bxns", "bxj", "tbb", "tbh", "svc", "udf", "bkpt"):
        return True
    if m in ("pop", "ldm", "ldmia", "ldmdb"):
        return any(o.type == CS_OP_REG and ins.reg_name(o.reg) == "pc" for o in ins.operands)
    if ins.operands and ins.operands[0].type == CS_OP_REG and ins.reg_name(ins.operands[0].reg) == "pc":
        return True
    return False


def thumb_defs(b, lo=0, hi=None):
    """All constant definitions in the image: (site, reg, value, kind, ins).
    Scans Thumb 2-byte aligned; resolves PC-relative literal loads and
    movw/movt pairs.  Returns candidates (validated by the forward scanner)."""
    n = len(b.d)
    if hi is None:
        hi = n
    out = []
    for off in range(max(0, lo), min(n, hi) - 4, 2):
        cpu = BASE + off
        ins = b.thumb(cpu)
        if ins is None:
            continue
        l = lit_of(b, ins, True)
        if l is not None and l[1] is not None:
            out.append((cpu, dst_reg(ins), l[1], "lit"))
            continue
        if ins.mnemonic.split(".")[0] in ("movw", "movt"):
            out.append((cpu, None, None, "movimm"))
    return out


def movimm_values(b, lo=0, hi=None, window=6):
    """Resolve movw/movt pairs -> (site, reg, value)."""
    n = len(b.d)
    if hi is None:
        hi = n
    res = []
    pend = {}
    for off in range(max(0, lo), min(n, hi) - 4, 2):
        cpu = BASE + off
        ins = b.thumb(cpu)
        if ins is None:
            continue
        m = ins.mnemonic.split(".")[0]
        if m not in ("movw", "movt"):
            continue
        if len(ins.operands) < 2 or ins.operands[1].type != CS_OP_IMM:
            continue
        reg = ins.reg_name(ins.operands[0].reg)
        imm = ins.operands[1].imm & 0xFFFF
        if m == "movw":
            pend[reg] = (cpu, imm)
        else:
            if reg in pend:
                site, low = pend.pop(reg)
                res.append((site, reg, (imm << 16) | low))
            pend.pop(reg, None)
        # a movw re-defines its dst reg; other writes to pend regs are not tracked
    return res


def forward_scan(b, site, reg, value, device_range=(DEV_LO, DEV_HI), maxins=48):
    """Follow straight-line Thumb code from a constant definition and report
    every load/store whose effective address (base + disp) falls in device_range.
    Returns list of (access_site, mnemonic, eff_addr, base_reg, disp, via_reg)."""
    hits = []
    state = {}
    if reg:
        state[reg] = value
    pc = site
    ins = b.thumb(pc)
    if ins is None:
        return hits
    pc += ins.size
    for _ in range(maxins):
        ins = b.thumb(pc)
        if ins is None:
            break
        m = ins.mnemonic.split(".")[0]
        # --- memory access? ------------------------------------------------
        if is_mem_ins(ins):
            md = mem_base_disp(ins)
            if md is not None:
                base, disp, idx = md
                val = state.get(base)
                if val is not None:
                    eff = (val + disp) & 0xFFFFFFFF
                    if idx is not None and state.get(idx) is not None:
                        eff = (eff + state[idx]) & 0xFFFFFFFF
                    if device_range[0] <= eff < device_range[1]:
                        hits.append((pc, ins.mnemonic, eff, base, disp, val))
                # a load clobbers its destination
                d = dst_reg(ins)
                if d:
                    state.pop(d, None)
            pc += ins.size
            if end_of_block(ins):
                break
            continue
        # --- control flow ---------------------------------------------------
        if m in ("bl", "blx"):
            for r in ("r0", "r1", "r2", "r3", "r12", "lr"):
                state.pop(r, None)
            pc += ins.size
            continue
        if m in ("b", "bx", "bxns", "bxj", "tbb", "tbh", "svc", "udf"):
            break
        if is_cond_branch(ins) or m in ("cbz", "cbnz"):
            pc += ins.size
            continue
        if m in ("it", "itt", "ite", "ittt", "itte", "itet", "itee"):
            pc += ins.size
            continue
        # --- constant propagation ------------------------------------------
        l = lit_of(b, ins, True)
        d = dst_reg(ins)
        if l is not None:
            if d:
                state[d] = l[1]
            pc += ins.size
            continue
        if m in ("movs", "mov", "mov.w"):
            if len(ins.operands) == 2 and d:
                if ins.operands[1].type == CS_OP_IMM:
                    state[d] = ins.operands[1].imm & 0xFFFFFFFF
                else:
                    state.pop(d, None)
            pc += ins.size
            continue
        if m in ("movw", "movt"):
            if d:
                state.pop(d, None)
            pc += ins.size
            continue
        if m in ("adds", "add", "add.w", "subs", "sub", "sub.w", "adc", "sbc",
                 "addw", "subw"):
            if len(ins.operands) >= 2 and d:
                a = ins.operands[1]
                if a.type == CS_OP_REG and state.get(ins.reg_name(a.reg)) is not None:
                    base = state[ins.reg_name(a.reg)]
                    if len(ins.operands) >= 3 and ins.operands[2].type == CS_OP_IMM:
                        imm = ins.operands[2].imm
                        state[d] = (base + imm if m.startswith("add") else base - imm) & 0xFFFFFFFF
                    else:
                        state.pop(d, None)
                elif a.type == CS_OP_IMM and d in state:
                    imm = a.imm
                    state[d] = (state[d] + imm if m.startswith("add") else state[d] - imm) & 0xFFFFFFFF
                else:
                    state.pop(d, None)
            pc += ins.size
            continue
        if m in ("orr", "orr.w", "and", "and.w", "bic", "bic.w"):
            if len(ins.operands) >= 3 and d:
                a = ins.operands[1]
                if a.type == CS_OP_REG and state.get(ins.reg_name(a.reg)) is not None \
                        and ins.operands[2].type == CS_OP_IMM:
                    v = state[ins.reg_name(a.reg)]
                    imm = ins.operands[2].imm & 0xFFFFFFFF
                    if m.startswith("orr"):
                        state[d] = v | imm
                    elif m.startswith("and"):
                        state[d] = v & imm
                    else:
                        state[d] = v & ~imm
                else:
                    state.pop(d, None)
            pc += ins.size
            continue
        # unknown def -> clear destination register
        if d:
            state.pop(d, None)
        pc += ins.size
        if end_of_block(ins):
            break
    return hits


def func_starts(b):
    """Best-effort function start set: header entries + BL targets + Thumb
    pointers found in literal pools that point into the image."""
    starts = set(KNOWN_FUNCS)
    starts.add(0x40050)
    n = len(b.d)
    # BL/BLX targets, Thumb linear sweep
    for off in range(0, n - 4, 2):
        cpu = BASE + off
        ins = b.thumb(cpu)
        if ins is None:
            continue
        m = ins.mnemonic.split(".")[0]
        if m in ("bl", "blx") and ins.operands and ins.operands[0].type == CS_OP_IMM:
            t = ins.operands[0].imm
            if IMG_LO <= t < IMG_HI:
                starts.add(t & ~1)
    # Thumb pointers in literal words (data read via PC-relative loads only;
    # a raw 4-byte scan is fine because the image has no data above 0x40000
    # that could alias a code pointer except pools we want anyway)
    for off in range(0, n - 4, 4):
        w = struct.unpack_from("<I", b.d, off)[0]
        if 0x40001 <= w < IMG_HI and (w & 1):
            starts.add((w & ~1))
    return sorted(s for s in starts if IMG_LO <= s < IMG_HI)


def containing(starts, addr):
    import bisect
    i = bisect.bisect_right(starts, addr) - 1
    return starts[i] if i >= 0 else None


def label(addr, starts=None):
    if addr in KNOWN_FUNCS:
        return KNOWN_FUNCS[addr]
    if starts:
        s = containing(starts, addr)
        if s is not None:
            return "%s%s" % (KNOWN_FUNCS.get(s, "fn_0x%05X" % s), "" if s == addr else "+0x%X" % (addr - s))
    return "fn_0x%05X" % addr if addr else "?"


def fmt_access(b, hit, starts):
    site, mnem, eff, base, disp, baseval = hit
    return "0x%08X  %-8s %-6s by %s (base 0x%08X+0x%X)  @0x%05X in %s" % (
        eff, mnem, "write" if mnem.split(".")[0].startswith("st") else "read",
        base, baseval, disp, site, label(site, starts))


def cmd_census(b, args):
    starts = func_starts(b)
    lo, hi = args.lo, args.hi
    rel = {}
    n = len(b.d)
    print("# scanning %d bytes of Thumb code for device-CA constant definitions" % n)
    for off in range(0, n - 4, 2):
        cpu = BASE + off
        ins = b.thumb(cpu)
        if ins is None:
            continue
        l = lit_of(b, ins, True)
        if l is not None and l[1] is not None and (lo <= l[1] < hi or lo <= (l[1] + 0x400) < hi):
            for h in forward_scan(b, cpu, dst_reg(ins), l[1]):
                rel.setdefault(h[2], []).append(h)
    for site, reg, val in movimm_values(b):
        if lo <= val < hi or lo <= val + 0x400 < hi:
            for h in forward_scan(b, site, reg, val):
                rel.setdefault(h[2], []).append(h)
    print("# %d distinct device addresses touched" % len(rel))
    for addr in sorted(rel):
        print("\n## CA 0x%08X  (BAR0 IO 0x%06X)" % (addr, 0x3B8000 + (addr - 0x40000000)))
        seen = set()
        for h in rel[addr]:
            s = fmt_access(b, h, starts)
            if s in seen:
                continue
            seen.add(s)
            print("   " + s)
    return 0


def cmd_access(b, args):
    starts = func_starts(b)
    targets = [int(x, 0) for x in args.addrs]
    d = b.d
    n = len(d)
    for t in targets:
        print("\n=== CA 0x%08X  (host BAR0 IO 0x%06X) ===" % (t, 0x3B8000 + (t - 0x40000000)))
        pat = struct.pack("<I", t)
        homes, s = [], 0
        while True:
            i = d.find(pat, s)
            if i < 0:
                break
            homes.append(i + BASE)
            s = i + 1
        print(" literal words == target: %s" % (" ".join("%05X" % h for h in homes) or "-"))
        # load sites of those literal words (Thumb)
        loads = []
        for off in range(0, n - 4, 2):
            cpu = BASE + off
            ins = b.thumb(cpu)
            if ins is None:
                continue
            l = lit_of(b, ins, True)
            if l is not None and l[0] in homes:
                loads.append((cpu, dst_reg(ins), l[1]))
                print("   loads 0x%08X at 0x%05X (%s)  [literal 0x%05X]" % (
                    l[1], cpu, label(cpu, starts), l[0]))
        hits = []
        for site, reg, val in loads:
            hits += forward_scan(b, site, reg, val)
        # base+offset forms: literal value in [t-0x800, t]
        base_loads = []
        for off in range(0, n - 4, 2):
            cpu = BASE + off
            ins = b.thumb(cpu)
            if ins is None:
                continue
            l = lit_of(b, ins, True)
            if l is None or l[1] is None:
                continue
            if t - 0x800 <= l[1] < t:
                base_loads.append((cpu, dst_reg(ins), l[1], t - l[1]))
        for site, reg, val, want in base_loads:
            for h in forward_scan(b, site, reg, val):
                if h[2] == t:
                    hits.append(h)
        if not hits:
            print(" resolved accesses: NONE")
        seen = set()
        for h in sorted(hits):
            s = fmt_access(b, h, starts)
            if s in seen:
                continue
            seen.add(s)
            print("   " + s)
        # movw/movt constructions
        for site, reg, val in movimm_values(b):
            if val == t:
                print("   movw/movt construction at 0x%05X reg %s value 0x%08X (%s)" % (
                    site, reg, val, label(site, starts)))
    return 0


def cmd_defs(b, args):
    starts = func_starts(b)
    lo, hi = args.lo, args.hi
    print("# literal constants in [0x%08X,0x%08X)" % (lo, hi))
    for off in range(0, len(b.d) - 4, 2):
        cpu = BASE + off
        ins = b.thumb(cpu)
        if ins is None:
            continue
        l = lit_of(b, ins, True)
        if l is not None and l[1] is not None and lo <= l[1] < hi:
            print("0x%08X literal @0x%05X site 0x%05X (%s) insn %s" % (
                l[1], l[0], cpu, label(cpu, starts), ins.mnemonic))
    for site, reg, val in movimm_values(b):
        if lo <= val < hi:
            print("0x%08X movw/movt @0x%05X reg %s (%s)" % (val, site, reg, label(site, starts)))
    return 0


def cmd_dis(b, args):
    cpu = args.cpu
    n = args.n
    end = cpu + n
    thumb = not args.arm
    while cpu < end:
        ins = b.arm(cpu) if args.arm else b.thumb(cpu)
        if ins is None:
            print("0x%05X: <undecodable>" % cpu)
            cpu += 4 if args.arm else 2
            continue
        extra = ""
        if thumb:
            l = lit_of(b, ins, True)
            if l is not None:
                extra = "   [lit 0x%05X = 0x%08X]" % (l[0], l[1] if l[1] is not None else -1)
        else:
            # ARM literal: ldr rX, [pc, #imm]
            for op in ins.operands:
                if op.type == CS_OP_MEM and ins.reg_name(op.mem.base) == "pc":
                    la = ins.address + 8 + op.mem.disp
                    extra = "   [lit 0x%05X = 0x%08X]" % (la, b.word(la) if b.word(la) is not None else -1)
        print("0x%05X: %-10s %-32s%s" % (cpu, ins.bytes.hex(), ins.mnemonic + " " + ins.op_str, extra))
        cpu += ins.size
    return 0


def main():
    if not CAP_OK:
        sys.exit("capstone not installed (use the repo .provision-venv)")
    p = argparse.ArgumentParser()
    p.add_argument("--fw", default=FW)
    sub = p.add_subparsers(dest="cmd", required=True)
    q = sub.add_parser("dis")
    q.add_argument("cpu", type=lambda s: int(s, 0))
    q.add_argument("n", type=lambda s: int(s, 0), nargs="?", default=0x80)
    q.add_argument("--arm", action="store_true")
    q = sub.add_parser("defs")
    q.add_argument("--lo", type=lambda s: int(s, 0), default=DEV_LO)
    q.add_argument("--hi", type=lambda s: int(s, 0), default=DEV_HI)
    q = sub.add_parser("access")
    q.add_argument("addrs", nargs="+")
    q = sub.add_parser("census")
    q.add_argument("--lo", type=lambda s: int(s, 0), default=DEV_LO)
    q.add_argument("--hi", type=lambda s: int(s, 0), default=DEV_HI)
    args = p.parse_args()
    b = Blob(args.fw)
    if args.cmd == "dis":
        return cmd_dis(b, args)
    if args.cmd == "defs":
        return cmd_defs(b, args)
    if args.cmd == "access":
        return cmd_access(b, args)
    if args.cmd == "census":
        return cmd_census(b, args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
