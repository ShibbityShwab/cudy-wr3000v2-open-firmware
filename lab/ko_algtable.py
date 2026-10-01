#!/usr/bin/env python3
import struct
import sys

from elftools.elf.elffile import ELFFile

TABLE = 0x35C8
ENTRIES = 414
STRIDE = 12

USAGE = "usage: ko_algtable.py <hi5622v100_wifi.ko> [out.csv]"


def section_string(e, sym, addend: int) -> str | None:
    sec = e.get_section(sym["st_shndx"])
    raw = sec.data()
    base = 0 if sym["st_value"] == 0 else sym["st_value"] - sec["sh_addr"]
    off = base + addend
    if not (0 <= off < len(raw)):
        return None
    text = raw[off:off + 48].split(b"\x00")[0]
    if not text or any(b < 0x20 or b > 0x7E for b in text):
        return None
    return text.decode("latin1")


def main() -> int:
    if len(sys.argv) < 2:
        print(USAGE)
        return 2
    path = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else None
    e = ELFFile(open(path, "rb"))
    db = e.get_section_by_name(".data").data()
    syms = list(e.get_section_by_name(".symtab").iter_symbols())
    rels = {r["r_offset"]: r["r_info_sym"] for r in e.get_section_by_name(".rel.data").iter_relocations()}

    rows = []
    for i in range(ENTRIES):
        off = TABLE + i * STRIDE
        addend, cfg_id = struct.unpack_from("<IH", db, off)
        direction = db[off + 6]
        idx = rels.get(off)
        name = section_string(e, syms[idx], addend) if idx is not None else None
        rows.append((name or "", cfg_id, direction))

    named = [r for r in rows if r[0]]
    print(f"entries: {len(rows)}, named: {len(named)}")
    print(f"getters: {sum(1 for r in named if r[2] == 1)}, setters: {sum(1 for r in named if r[2] == 0)}")
    if out:
        open(out, "w").write("name,cfg_id,dir\n" + "\n".join(f"{n},{c:#06x},{d}" for n, c, d in rows))
        print(f"written: {out}")
    else:
        for n, c, d in named[:30]:
            print(f"  {n},{c:#06x},{d}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
