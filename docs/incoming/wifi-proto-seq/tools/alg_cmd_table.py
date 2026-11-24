#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""alg_cmd_table.py - recover the vendor alg_cfg command table of the local vendor driver
(Hi5622V100, Cudy WR3000 v2.0) in place, host-only.

Lane: wifi-proto-seq (shared task task-103, 2026-11-24).
Reads the local (never committed) vendor modules and prints derived annotations only: no
vendor byte or calibration value is written into the repository.

The four facts this tool pins [V]
---------------------------------
1. The 414-entry alg process table (.data+0x35c8) is NOT runtime-patched through a "custom
   fixup list": each entry's name word is an R_ARM_ABS32 relocation against the
   .rodata.str1.4 section symbol with the string offset as addend (the "0x0a9874 fixup
   list" in an earlier lane report is simply the addend of entry 0).  The six calibration
   command names therefore resolve statically to their cfg ids.
2. The six "alg set_*" commands from /usr/bin/hi5622v100_cal_init.sh map to cfg ids
   3502/3503/3504/3505/3506/3507, type SET, flags need_d_process=1 / need_w4_device=1
   (device-processed with a wait for the device response).
3. Their per-command LUT entries (176 B at .data+0x4934, key = (type<<16)|cfg_id) all
   select the parser alg_cfg_args_param_analyse_equipment_param.
4. That parser's output is a fixed 270-byte (0x10E) blob; the equipment defaults table
   (g_equiment_default_param, sizes + offsets into g_dev_custom_info) gives the value
   sizes 0x68/0x68/0x46/0x46/(none)/0x28.  g_dev_custom_info itself is BSS in the plat
   module (filled at runtime), so the vendor default VALUES are not statically
   recoverable - the bench uses the per-unit factory cal file (local-only) or a zero-value
   structural probe.
"""
import argparse
import hashlib
import os
import struct
import sys

WIFI_SHA256 = "22fa789a3d431f49f9c52c76c402e5740d1d14118b5e47a347f2cc247532c7d2"
PLAT_SHA256 = "27c697918bc00f37afd490a812edff49cd2df9f8ac1d791ce2ca79c87adbba27"

PROC_TABLE_SYM = "g_ast_alg_cfg_process_info_table"
PROC_TABLE_ENTRIES = 414
LUT_SYM = "g_alg_cfg_lut"
LUT_ENTRY = 176
NUL = bytes(1)
MOVW_0x10E = bytes.fromhex("0e3100e3")
STRH_LEN = bytes.fromhex("b030cae1")

CAL = (
    ("set_2g_power_param", 3502, 0x68),
    ("set_2g_low_power_param", 3503, 0x68),
    ("set_5g_power_param", 3504, 0x46),
    ("set_5g_low_power_param", 3505, 0x46),
    ("set_xo_ppm_cali_param", 3506, None),
    ("set_rssi_param", 3507, 0x28),
)


class Elf:
    """Minimal ELF32 relocatable reader (no pyelftools dependency)."""

    def __init__(self, path):
        self.d = open(path, "rb").read()
        d = self.d
        if d[:4] != bytes([0x7F]) + b"ELF":
            raise ValueError("not an ELF file: %s" % path)
        self.sha256 = hashlib.sha256(d).hexdigest()
        e_shoff = struct.unpack_from("<I", d, 0x20)[0]
        e_shentsize = struct.unpack_from("<H", d, 0x2E)[0]
        e_shnum = struct.unpack_from("<H", d, 0x30)[0]
        e_shstrndx = struct.unpack_from("<H", d, 0x32)[0]
        self.secs = []
        for i in range(e_shnum):
            off = e_shoff + i * e_shentsize
            f = struct.unpack_from("<IIIIIIIIII", d, off)
            self.secs.append(dict(name=f[0], typ=f[1], flags=f[2], addr=f[3],
                                  offset=f[4], size=f[5], link=f[6]))
        shstr = self.secs[e_shstrndx]

        def sname(idx):
            if idx == 0:
                return ""
            o = shstr["offset"] + idx
            return d[o:d.index(NUL, o)].decode()

        for s in self.secs:
            s["sname"] = sname(s["name"])
        self.syms = []
        symtab = next((s for s in self.secs if s["sname"] == ".symtab"), None)
        if symtab:
            strtab = self.secs[symtab["link"]]
            for i in range(symtab["size"] // 16):
                st_name, st_value, st_size, st_info, st_other, st_shndx = (
                    struct.unpack_from("<IIIBBH", d, symtab["offset"] + i * 16))
                o = strtab["offset"] + st_name
                nm = d[o:d.index(NUL, o)].decode(errors="replace") if st_name else ""
                self.syms.append(dict(name=nm, value=st_value, size=st_size,
                                      shndx=st_shndx))

    def sec(self, name):
        return next(s for s in self.secs if s["sname"] == name)

    def read(self, sec, off, n):
        return self.d[sec["offset"] + off:sec["offset"] + off + n]

    def sym(self, name):
        for s in self.syms:
            if s["name"] == name:
                return s
        return None

    def relocs(self, secname):
        rel = next((s for s in self.secs if s["sname"] == ".rel" + secname), None)
        out = {}
        if not rel:
            return out
        for i in range(rel["size"] // 8):
            r_off, r_info = struct.unpack_from("<II", self.d, rel["offset"] + i * 8)
            symidx = r_info >> 8
            out[r_off] = (r_info & 0xFF, self.syms[symidx]["name"], self.syms[symidx])
        return out


def resolve_paths():
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.normpath(os.path.join(here, "..", "..", "..", "..", ".."))
    raw = os.path.join(repo, "opensource", "docs", "incoming", "wifi-shim-port", "raw")
    wifi = os.environ.get("HI5622_WIFI_KO") or os.path.join(raw, "wifi-patched.ko")
    plat = os.environ.get("HI5622_PLAT_KO") or os.path.join(raw, "plat-patched.ko")
    return wifi, plat


def load_tables(wifi_path):
    e = Elf(wifi_path)
    data = e.sec(".data")
    dr = e.relocs(".data")
    rostr = e.sec(".rodata.str1.4")
    sym = e.sym(PROC_TABLE_SYM)
    if not sym:
        raise ValueError("process table symbol missing")
    base = sym["value"]
    entries = []
    for i in range(PROC_TABLE_ENTRIES):
        off = base + i * 12
        addend = struct.unpack("<I", e.read(data, off, 4))[0]
        rel = dr.get(off)
        shndx = rel[2]["shndx"] if rel else 0
        secname = e.secs[shndx]["sname"] if shndx else ""
        if not secname.startswith(".rodata"):
            raise ValueError("entry %d name is not a rodata string offset (%r)"
                             % (i, secname))
        s = rostr["offset"] + addend
        name = e.d[s:e.d.index(NUL, s)].decode(errors="replace")
        cfg_id, cfg_type, resv = struct.unpack("<HBB", e.read(data, off + 4, 4))
        flags = struct.unpack("<I", e.read(data, off + 8, 4))[0]
        entries.append(dict(idx=i, name=name, id=cfg_id, type=cfg_type, flags=flags))
    lut = e.sym(LUT_SYM)
    lut_entries = {}
    for i in range(416):
        off = lut["value"] + i * LUT_ENTRY
        key = struct.unpack("<I", e.read(data, off, 4))[0]
        rel = dr.get(off + 8)
        lut_entries[key] = rel[1] if rel else None
    return e, entries, lut_entries


def selftest(wifi_path=None, plat_path=None):
    default_wifi, default_plat = resolve_paths()
    wifi_path = wifi_path or default_wifi
    plat_path = plat_path or default_plat
    checks = 0

    def ck(cond, msg):
        nonlocal checks
        checks += 1
        if not cond:
            raise AssertionError("FAIL: " + msg)

    e, entries, lut = load_tables(wifi_path)
    ck(e.sha256 == WIFI_SHA256, "wifi.ko sha256 pin (%s)" % e.sha256[:16])
    ck(len(entries) == 414, "process table has 414 entries")
    ck(len(lut) == 416, "LUT has 416 keyed entries")

    by = {}
    for r in entries:
        by[(r["name"], r["type"])] = r
    for name, cfg_id, vsize in CAL:
        setrow = by.get((name, 0))
        ck(setrow is not None, "%s SET row present" % name)
        ck(setrow["id"] == cfg_id, "%s cfg id %d" % (name, cfg_id))
        ck(setrow["flags"] == 0x01010000,
           "%s flags need_d_process/need_w4_device" % name)
        getrows = [r for r in entries if r["id"] == cfg_id and r["type"] == 1]
        ck(len(getrows) == 1 and getrows[0]["name"].startswith("get_"),
           "%s GET row" % name)
        ck(lut.get(cfg_id) == "alg_cfg_args_param_analyse_equipment_param",
           "%s parser = equipment-param analyser (%s)" % (name, lut.get(cfg_id)))
        ck(lut.get((1 << 16) | cfg_id) == "alg_cfg_args_param_analyse_equipment_param",
           "%s GET parser = equipment-param analyser" % name)

    text = e.sec(".text")
    movw = e.d[text["offset"] + 0x156DC0:text["offset"] + 0x156DC0 + 4]
    strh = e.d[text["offset"] + 0x156DC4:text["offset"] + 0x156DC4 + 4]
    ck(movw == MOVW_0x10E, "host parser movw r3,#0x10E (270-byte blob)")
    ck(strh == STRH_LEN, "host parser *len = 270")

    drel = e.relocs(".data")
    data = e.sec(".data")
    got = {}
    for i in range(10):
        off = 0x30E4 + i * 8
        size = struct.unpack("<I", e.read(data, off, 4))[0]
        rel = drel.get(off + 4)
        ptr_sym = rel[1] if rel else None
        got[3502 + i] = (size, ptr_sym)
    for name, cfg_id, vsize in CAL:
        if vsize is None:
            ck(got[cfg_id][1] is None, "%s has no vendor default (NULL ptr)" % name)
        else:
            ck(got[cfg_id][1] == "g_dev_custom_info",
               "%s default points into g_dev_custom_info" % name)
            ck(got[cfg_id][0] == vsize,
               "%s default value size %#x" % (name, vsize))

    pe = Elf(plat_path)
    ck(pe.sha256 == PLAT_SHA256, "plat.ko sha256 pin")
    gci = pe.sym("g_dev_custom_info")
    ck(gci is not None, "g_dev_custom_info symbol in plat.ko")
    gsec = pe.secs[gci["shndx"]]["sname"] if gci["shndx"] < len(pe.secs) else "?"
    ck(gsec == ".bss", "g_dev_custom_info is BSS (runtime data), section %r" % gsec)

    print("alg_cmd_table: %d/%d checks PASS (wifi sha256 %s)"
          % (checks, checks, e.sha256[:16]))
    return 0


def show(wifi_path=None):
    wifi_path = wifi_path or resolve_paths()[0]
    e, entries, lut = load_tables(wifi_path)
    print("wifi module :", wifi_path)
    print("sha256      :", e.sha256)
    print()
    print("== the six calibration alg commands (from /usr/bin/hi5622v100_cal_init.sh) ==")
    by = {(r["name"], r["type"]): r for r in entries}
    for name, cfg_id, vsize in CAL:
        row = by[(name, 0)]
        print("  %-26s cfg_id=%-5d SET flags=%#010x value_size=%s parser=%s"
              % (name, cfg_id, row["flags"],
                 ("%#x" % vsize) if vsize else "none/default",
                 lut.get(cfg_id)))
    print()
    print("== wire message ==")
    print("  HCC group 0 / id 2, key 0x0101, alg_cfg_blob = 270 B (0x10E)")
    print("  blob: +0 u16 cfg_id | +2 u8 type(0=SET) | +3 u8 pkt_type | +4 u32 dev_ret")
    print("        +8 u8 need_w4(1) | +9 u8 resv | +10 value[]")
    print("  frame total = 12 + (0x10 + 270) = 298 B (0x12A)")
    print("  SR node word1 = 0x012A6D2B")
    print()
    print("  NOTE: the vendor default VALUES (g_equiment_default_param -> g_dev_custom_info)")
    print("  are BSS in the plat module and only exist at runtime; use a zero-value structural")
    print("  probe or the per-unit factory cal file (local-only, never committed).")


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--show", action="store_true")
    ap.add_argument("--wifi-ko", default=None)
    ap.add_argument("--plat-ko", default=None)
    args = ap.parse_args(argv)
    if args.selftest:
        try:
            return selftest(args.wifi_ko, args.plat_ko)
        except AssertionError as exc:
            print(exc, file=sys.stderr)
            return 1
    if args.show:
        show(args.wifi_ko)
        return 0
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
