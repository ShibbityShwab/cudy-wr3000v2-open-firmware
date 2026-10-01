#!/usr/bin/env python3
import sys

from elftools.elf.elffile import ELFFile

PREFIXES = ("hmac_", "wal_", "mac_", "hdpp_", "alg_", "shuangta_", "hal_", "oal_", "oam_")

USAGE = "usage: ko_symmap.py <module.ko> [out.csv]"


def functions(e):
    out = []
    for s in e.get_section_by_name(".symtab").iter_symbols():
        if s["st_info"]["type"] == "STT_FUNC" and s.name and s["st_shndx"] != "SHN_UNDEF":
            out.append((s.name, s["st_size"]))
    return out


def main() -> int:
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        print(USAGE)
        return 2
    path = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else None
    e = ELFFile(open(path, "rb"))
    fns = functions(e)
    census = [(p, len([1 for n, _ in fns if n.startswith(p)]),
               sum(sz for n, sz in fns if n.startswith(p))) for p in PREFIXES]
    top = sorted(fns, key=lambda x: (-x[1], x[0]))[:15]

    if out:
        rows = ["kind,name,count,code_size"]
        for p, c, sz in census:
            rows.append(f"prefix,{p},{c},{sz}")
        for n, sz in top:
            rows.append(f"largest,{n},1,{sz}")
        open(out, "w").write("\n".join(rows) + "\n")
        print(f"written: {out}")
        print(f"functions: {len(fns)}, total code size: {sum(sz for _, sz in fns)}")
        return 0

    print(f"module: {path}")
    print(f"functions: {len(fns)}, total code size: {sum(sz for _, sz in fns)}")
    print("prefix,count,code_size")
    for p, c, sz in census:
        print(f"{p},{c},{sz}")
    print("largest 15:")
    for n, sz in top:
        print(f"  {sz:7d}  {n}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
