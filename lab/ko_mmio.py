#!/usr/bin/env python3
import re
import struct
import sys

from capstone import CS_ARCH_ARM, CS_MODE_ARM, Cs
from elftools.elf.elffile import ELFFile

RANGES = ((0x40000000, 0x4012FFFF), (0x58000000, 0x5812FFFF))
ACCESSORS = ("oal_pcie_devca_to_hostva", "oal_pcie_inbound_ca_to_va")
REG = re.compile(r"^(r1[0-5]|r[0-9]|sp|lr|pc|fp|ip|sl|sb)$")

USAGE = "usage: ko_mmio.py <module.ko>"


def in_range(v, lo, hi):
    return lo <= v <= hi


def functions(e):
    out = []
    for s in e.get_section_by_name(".symtab").iter_symbols():
        if s["st_info"]["type"] == "STT_FUNC" and s.name and s["st_shndx"] != "SHN_UNDEF":
            out.append((s.name, s["st_value"], s["st_size"], s["st_shndx"]))
    return out


def accessor_sites(e):
    sites = {}
    for rn in (".rel.text", ".rel.text.unlikely", ".rel.init.text"):
        rs = e.get_section_by_name(rn)
        if rs is None:
            continue
        symtab = e.get_section(rs["sh_link"])
        for r in rs.iter_relocations():
            if symtab.get_symbol(r["r_info_sym"]).name in ACCESSORS:
                sites.setdefault(rs["sh_info"], []).append(r["r_offset"])
    return sites


def clobber_tokens(ops):
    return [p.strip().rstrip("!") for p in ops.split(",") if REG.match(p.strip().rstrip("!"))]


def scan(e, name, value, size, shndx, md):
    sec = e.get_section(shndx)
    if sec["sh_type"] != "SHT_PROGBITS":
        return []
    data = sec.data()
    regs = {}
    found = []
    for ins in md.disasm(data[value:value + size], value):
        m = ins.mnemonic
        ops = ins.op_str
        if m in ("movw", "movt", "mov") and ", #" in ops:
            r, imm = ops.split(", #", 1)
            r = r.strip()
            imm = int(imm, 16)
            if m == "movw":
                regs[r] = imm & 0xFFFF
            elif m == "movt":
                regs[r] = (regs.get(r, 0) & 0xFFFF) | ((imm & 0xFFFF) << 16)
            else:
                regs[r] = imm
        elif m in ("add", "sub", "rsb") and ops.count(",") >= 1:
            parts = [p.strip() for p in ops.split(",")]
            if len(parts) == 3 and parts[1] in regs and parts[2].startswith("#"):
                a = regs[parts[1]]
                b = int(parts[2][1:], 16)
                regs[parts[0]] = a + b if m == "add" else a - b if m == "sub" else b - a
            else:
                regs.pop(parts[0], None)
        elif m in ("ldr", "ldrb", "ldrh", "ldrsb", "ldrsh", "adr", "ldrd", "vldr", "mrs"):
            parts = [p.strip() for p in ops.split(",")]
            for p in parts[:2] if m == "ldrd" else parts[:1]:
                regs.pop(p, None)
            if m == "ldr" and "[pc," in ops:
                try:
                    lit = int(ops.split("#")[-1].rstrip("]"), 16)
                except ValueError:
                    lit = 0
                if ops.split("#")[-1].strip().startswith("-"):
                    lit = -lit
                addr = ins.address + 8 + lit
                if 0 <= addr <= len(data) - 4:
                    word = struct.unpack_from("<I", data, addr)[0]
                    if any(in_range(word, lo, hi) for lo, hi in RANGES):
                        found.append((word, ins.address, "lit"))
        elif m in ("pop", "ldm", "ldmia", "ldmdb"):
            for p in re.findall(r"(r1[0-5]|r[0-9]|sp|lr|pc|fp|ip|sl|sb)", ops):
                regs.pop(p, None)
        else:
            for p in clobber_tokens(ops):
                regs.pop(p, None)
        for r, val in regs.items():
            if any(in_range(val, lo, hi) for lo, hi in RANGES):
                found.append((val, ins.address, "movw/movt" if m in ("movw", "movt") else m))
    return found


def main() -> int:
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        print(USAGE)
        return 2
    path = sys.argv[1]
    e = ELFFile(open(path, "rb"))
    md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
    fns = functions(e)
    sites = accessor_sites(e)
    hits = {}
    for name, value, size, shndx in fns:
        calls = 0
        for off in sites.get(shndx, ()):
            if value <= off < value + size:
                calls += 1
        for val, addr, kind in scan(e, name, value, size, shndx, md):
            hits.setdefault(name, {"calls": calls, "vals": {}})
            hits[name]["vals"].setdefault(val, (addr, kind))

    print(f"module: {path}")
    print(f"functions scanned: {len(fns)}")
    print(f"accessors: {', '.join(ACCESSORS)}")
    for lo, hi in RANGES:
        rows = {n: {v: k for v, k in d["vals"].items() if in_range(v, lo, hi)} for n, d in hits.items()}
        rows = {n: r for n, r in rows.items() if r}
        total = sum(len(r) for r in rows.values())
        print(f"\nrange {lo:#010x}-{hi:#010x}: {total} constants in {len(rows)} functions")
        for n in sorted(rows, key=lambda n: (-len(rows[n]), n)):
            print(f"  {n} (accessor calls: {hits[n]['calls']})")
            for v in sorted(rows[n]):
                addr, kind = rows[n][v]
                print(f"    {v:#010x}  @{addr:#08x}  {kind}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
