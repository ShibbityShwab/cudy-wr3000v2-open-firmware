#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""ko_devca.py - device-CA constant / access census for the vendor ARM kernel
modules (host driver side).

Reads the LOCAL vendor .ko files in place (/tmp/rootfs2524b/... by default) and
prints derived annotations only: nothing vendor is copied into the repo.

For each executable section it disassembles ARM and Thumb (using the ELF mapping
symbols $a/$t to pick the right mode when present) and resolves:
  * movw/movt register pairs -> 32-bit constants in the requested CA range
  * PC-relative literal loads (ARM and Thumb) -> constants in that range
For data sections it scans 4-byte aligned words in the range (resource tables).
Each hit is attributed to its enclosing function symbol, plus the neighbouring
instructions, so it is directly readable whether the driver reads or writes the
device register.

usage:
  ko_devca.py <ko> [--lo 0x40000000] [--hi 0x40200000] [--addr 0x4016010c ...]
  ko_devca.py <ko> funcs <regex>       # list function symbols
  ko_devca.py <ko> dis <sym> [n]       # annotated disassembly of a function
"""
import argparse
import re
import struct
import sys

try:
    import capstone
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB
except Exception:                                     # pragma: no cover
    sys.exit("capstone missing (use the repo .provision-venv)")
try:
    from elftools.elf.elffile import ELFFile
except Exception:                                     # pragma: no cover
    sys.exit("pyelftools missing (use the repo .provision-venv)")

DEF_KO = "/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_wifi.ko"


class Ko:
    def __init__(self, path):
        self.fh = open(path, "rb")
        self.e = ELFFile(self.fh)
        self.syms = []
        st = self.e.get_section_by_name(".symtab")
        if st:
            for s in st.iter_symbols():
                if s["st_info"]["type"] in ("STT_FUNC", "STT_OBJECT") and s["st_shndx"] != "SHN_UNDEF":
                    sec = self.e.get_section(s["st_shndx"])
                    self.syms.append((sec["sh_addr"] + s["st_value"], s["st_size"], s.name, s["st_info"]["type"]))
        self.by_sec = {}
        for a, sz, n, t in self.syms:
            self.by_sec.setdefault(self.sec_of(a), []).append((a, sz, n, t))
        self.maps = []          # (sec_name, addr, mode)
        if st:
            for s in st.iter_symbols():
                if s.name in ("$a", "$t", "$d") and s["st_shndx"] != "SHN_UNDEF":
                    self.maps.append((self.e.get_section(s["st_shndx"]).name,
                                      self.e.get_section(s["st_shndx"])["sh_addr"] + s["st_value"], s.name))
        self.maps.sort(key=lambda x: (x[0], x[1]))

    def sec_of(self, addr):
        for sec in self.e.iter_sections():
            if sec["sh_flags"] & 0x2 and sec["sh_addr"] <= addr < sec["sh_addr"] + sec["sh_size"]:
                return sec.name
        return None

    def enclosing(self, addr):
        best = None
        for a, sz, n, t in self.syms:
            if a <= addr < a + max(sz, 1):
                if best is None or a > best[0]:
                    best = (a, n)
        return best[1] if best else None

    def mode_at(self, secname, addr):
        mode = "a"
        for s, a, m in self.maps:
            if s == secname and a <= addr:
                mode = m[1]
        return mode


def ca_range(v, lo, hi, slack=0x800):
    return lo <= v < hi or lo <= v + slack < hi


def scan_ko(k, lo, hi, want):
    CA = Cs(CS_ARCH_ARM, CS_MODE_ARM); CA.detail = True
    CT = Cs(CS_ARCH_ARM, CS_MODE_THUMB); CT.detail = True
    hits = []            # (addr, kind, value, function, sec)
    for sec in k.e.iter_sections():
        name = sec.name
        addr = sec["sh_addr"]
        data = sec.data()
        if sec["sh_flags"] & 0x4:      # SHF_EXECINSTR
            # ARM linear scan 4-byte
            for off in range(0, len(data) - 4, 4):
                a = addr + off
                mode = k.mode_at(name, a)
                md = CT if mode == "t" else CA
                try:
                    ins = next(iter(md.disasm(data[off:off + 4], a)), None)
                except Exception:
                    ins = None
                if ins is None:
                    continue
                m = ins.mnemonic.split(".")[0]
                if m in ("movw", "movt") and ins.operands and ins.operands[0].type == capstone.CS_OP_REG:
                    reg = ins.reg_name(ins.operands[0].reg)
                    imm = ins.operands[1].imm & 0xFFFF
                    # a real pair is the *next* instruction (or the one after a
                    # single unrelated insn), same Rd, opposite half
                    for k2 in (1, 2):
                        off2 = off + 4 * k2
                        if off2 > len(data) - 4:
                            break
                        try:
                            i2 = next(iter(md.disasm(data[off2:off2 + 4], addr + off2)), None)
                        except Exception:
                            i2 = None
                        if i2 is None:
                            continue
                        m2 = i2.mnemonic.split(".")[0]
                        if m2 in ("movw", "movt") and i2.operands and i2.operands[0].type == capstone.CS_OP_REG \
                                and i2.reg_name(i2.operands[0].reg) == reg and m2 != m:
                            v = (imm << 16) | (i2.operands[1].imm & 0xFFFF) if m == "movt" else \
                                ((i2.operands[1].imm & 0xFFFF) << 16) | imm
                            if ca_range(v, lo, hi, 0):
                                hits.append((a, "%s+%s" % (m, m2), v, k.enclosing(a), name))
                            break
            continue
        if sec["sh_flags"] & 0x2:      # SHF_ALLOC data
            for off in range(0, len(data) - 4, 4):
                v = struct.unpack_from("<I", data, off)[0]
                if (lo <= v < hi) or (v in want):
                    hits.append((addr + off, "data-word", v, k.enclosing(addr + off), name))
    return hits


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--ko", default=DEF_KO)
    p.add_argument("--lo", type=lambda s: int(s, 0), default=0x40000000)
    p.add_argument("--hi", type=lambda s: int(s, 0), default=0x40200000)
    p.add_argument("--addr", nargs="*", type=lambda s: int(s, 0), default=[])
    p.add_argument("--ctx", type=int, default=6, help="instructions of context around a hit")
    sub = p.add_subparsers(dest="cmd", required=True)
    q = sub.add_parser("scan")
    q.add_argument("--lo", type=lambda s: int(s, 0), default=0x40000000)
    q.add_argument("--hi", type=lambda s: int(s, 0), default=0x40200000)
    q.add_argument("--addr", nargs="*", type=lambda s: int(s, 0), default=[])
    q = sub.add_parser("funcs"); q.add_argument("pattern")
    q = sub.add_parser("dis"); q.add_argument("sym"); q.add_argument("n", type=lambda s: int(s, 0), nargs="?", default=0x80)
    a = p.parse_args()
    k = Ko(a.ko)
    if a.cmd == "funcs":
        pat = re.compile(a.pattern)
        for addr, sz, n, t in sorted(k.syms):
            if t == "STT_FUNC" and pat.search(n):
                print("0x%08X %6d %s" % (addr, sz, n))
        return 0
    if a.cmd == "dis":
        for addr, sz, n, t in k.syms:
            if n == a.sym:
                sec = k.e.get_section_by_name(k.sec_of(addr))
                data = sec.data()
                base = addr - sec["sh_addr"]
                CA = Cs(CS_ARCH_ARM, CS_MODE_ARM); CA.detail = True
                CT = Cs(CS_ARCH_ARM, CS_MODE_THUMB); CT.detail = True
                off = 0
                while off < a.n:
                    a2 = addr + off
                    md = CT if k.mode_at(sec.name, a2) == "t" else CA
                    ins = next(iter(md.disasm(data[base + off:base + off + 4], a2)), None)
                    if ins is None:
                        print("0x%08X: ??" % a2); off += 4; continue
                    print("0x%08X: %-10s %s" % (a2, ins.bytes.hex(), ins.mnemonic + " " + ins.op_str))
                    off += ins.size
                return 0
    want = set(a.addr)
    hits = scan_ko(k, a.lo, a.hi, want)
    print("# %s: %d executable/data hits for CA range [0x%08X,0x%08X)" % (a.ko, len(hits), a.lo, a.hi))
    seen = set()
    for addr, kind, v, fn, sec in hits:
        if a.addr and v not in want:
            continue
        key = (v, fn, kind)
        if key in seen:
            continue
        seen.add(key)
        print("0x%08X %-10s %-24s %s" % (v, kind, fn or "?", sec))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
