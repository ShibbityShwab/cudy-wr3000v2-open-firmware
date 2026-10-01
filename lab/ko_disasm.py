#!/usr/bin/env python3
"""Function-level disassembler for an ARM kernel module (.ko).

Prints the disassembly of one or more named functions, annotating every
relocation site with the symbol it targets (so `bl`/`ldr`/`movw`+`movt`
pairs that reference other functions, globals or PCI accessors are readable).

usage: ko_disasm.py <module.ko> <func> [func ...]
       ko_disasm.py <module.ko> --list          # all STT_FUNC symbols
       ko_disasm.py <module.ko> --grep PATTERN  # functions matching PATTERN
"""
import re
import struct
import sys

from capstone import CS_ARCH_ARM, CS_MODE_ARM, Cs
from elftools.elf.elffile import ELFFile

# relocation types that name a symbol at the instruction address
RELOC_AT_INSN = {
    "R_ARM_CALL", "R_ARM_JUMP24", "R_ARM_PC24", "R_ARM_MOVW_ABS_NC",
    "R_ARM_MOVT_ABS", "R_ARM_MOVW_PREL_NC", "R_ARM_MOVT_PREL",
    "R_ARM_THM_CALL", "R_ARM_THM_JUMP24",
}


def functions(e):
    out = {}
    st = e.get_section_by_name(".symtab")
    if st is None:
        return out
    for s in st.iter_symbols():
        if s["st_info"]["type"] == "STT_FUNC" and s.name and s["st_shndx"] != "SHN_UNDEF":
            out.setdefault(s.name, (s["st_value"], s["st_size"], s["st_shndx"]))
    return out


def reloc_map(e, shndx):
    """addr -> (symbol, reloc_type_name) for relocations in the section."""
    sec = e.get_section(shndx)
    rs = e.get_section_by_name(".rel" + sec.name)
    m = {}
    if rs is None:
        return m
    symtab = e.get_section(rs["sh_link"])
    for r in rs.iter_relocations():
        nm = symtab.get_symbol(r["r_info_sym"]).name
        rt = r["r_info_type"]
        try:
            from elftools.elf.enums import ENUM_RELOC_TYPE_ARM
            rtn = {v: k for k, v in ENUM_RELOC_TYPE_ARM.items()}.get(rt, str(rt))
        except Exception:
            rtn = str(rt)
        m[r["r_offset"]] = (nm, rtn)
    return m


def str_at(e, addr):
    """If addr points into a .rodata-ish section, return the C string there."""
    for sec in e.iter_sections():
        if not (sec["sh_flags"] & 0x2):  # SHF_ALLOC only
            continue
        if sec["sh_addr"] <= addr < sec["sh_addr"] + sec["sh_size"]:
            data = sec.data()
            off = addr - sec["sh_addr"]
            end = data.find(b"\x00", off)
            if end < 0:
                end = len(data)
            raw = data[off:end]
            try:
                return raw.decode("latin1")
            except Exception:
                return None
    return None


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    path, args = sys.argv[1], sys.argv[2:]
    e = ELFFile(open(path, "rb"))
    fns = functions(e)
    md = Cs(CS_ARCH_ARM, CS_MODE_ARM)

    if args[0] == "--list":
        for n in sorted(fns, key=lambda n: fns[n][0]):
            print(f"{fns[n][0]:#010x} {fns[n][1]:6d} {n}")
        return 0
    if args[0] == "--grep":
        pat = re.compile(args[1])
        for n in sorted(fns, key=lambda n: fns[n][0]):
            if pat.search(n):
                print(f"{fns[n][0]:#010x} {fns[n][1]:6d} {n}")
        return 0

    for name in args:
        if name not in fns:
            print(f"!! no function {name}")
            continue
        val, size, shndx = fns[name]
        sec = e.get_section(shndx)
        data = sec.data()
        rmap = reloc_map(e, shndx)
        print(f"\n===== {name} @ {val:#x} size={size} ({sec.name}) =====")
        chunk = data[val:val + size]
        for ins in md.disasm(chunk, val):
            ann = ""
            if ins.address in rmap:
                sym, rt = rmap[ins.address]
                if rt in RELOC_AT_INSN or rt.startswith("R_ARM_CALL") or "CALL" in rt or "JUMP" in rt or "PC24" in rt or "ABS" in rt:
                    ann = f"   ; -> {sym} [{rt}]"
            # pc-relative literal load
            m = re.match(r"^(r\d+|r1[0-5]|sp|lr|pc),\s*\[pc,\s*#(-?0x[0-9a-f]+)\]", ins.op_str)
            if ins.mnemonic == "ldr" and m:
                lit = int(m.group(2), 16)
                lit_addr = ins.address + 8 + lit
                if lit_addr in rmap:
                    sym, rt = rmap[lit_addr]
                    ann = f"   ; = {sym} [{rt}]"
                else:
                    if 0 <= lit_addr - val < len(chunk) - 3:
                        w = struct.unpack_from("<I", chunk, lit_addr - val)[0]
                        s = str_at(e, w)
                        ann = f"   ; lit {w:#010x}" + (f' "{s}"' if s else "")
            print(f"  {ins.address:#08x}: {ins.bytes.hex():8s} {ins.mnemonic:8s} {ins.op_str}{ann}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
