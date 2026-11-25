#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""decode_alg_engine.py - decode the Hi5622V100 vendor alg_cfg engine statically.

Lane: wifi-rate-ctl (shared task task-179), host-only.

Reads the local, never-committed vendor module (hi5622v100_wifi.ko, unstripped,
recovered from the stock rootfs) and prints derived annotations only: symbol
addresses, table layouts, command ids, value enum names and wire-blob field
maps.  No vendor byte, firmware byte or calibration value is written into the
repository.

What is decoded (all [V] against the local module):
  1. g_ast_alg_cfg_process_info_table  - 414 command rows {name, u16 id,
     u8 type, 5 flag bytes} at .data+0x35c8.
  2. g_alg_cfg_lut                     - 416 dispatch entries x 176 B at
     .data+0x4934.  Header {u32 key, pad, u32 parser_fn, u8 argc, u8 resv,
     u16 blob_len} + up to 10 field descriptors x 16 B
     {u8 kind, u8 sub, u8 vsize, u8 resv, u16 voffset, u16 pad,
      u32 value_table_ptr, u32 value_table_size_ptr}.  The key is
     (scope << 16) | cfg_id where scope bit 0 selects GET.
  3. The four alg-cfg response output tables (.data+0x870 autorate 22 rows,
     +0x920 otp 4, +0x96c tpc 6, +0x99c the 32-range top-level dispatcher):
     rows are {u16 start, u16 end, u32 handler_fn} and the handler word is an
     R_ARM_ABS32 relocation to the per-id / per-family output formatter.
  4. The value-enum tables referenced by the field descriptors (rate_mode
     auto/fixed, legacy rates, the 26 autorate tuning knobs,
     protocol/bandwidth/GI/preamble/protect/fec/TPC tables).

Regeneration:
    python3 tools/decode_alg_engine.py --selftest
    python3 tools/decode_alg_engine.py --json evidence/alg-engine.json
    python3 tools/decode_alg_engine.py --markdown evidence/TABLES-generated.md

Environment: HI5622_WIFI_KO may point at a different local copy; the sha256 pin
then fails unless --allow-any-sha is given (kept fail-closed).
"""
import argparse
import hashlib
import json
import os
import struct
import sys

WIFI_SHA256 = "22fa789a3d431f49f9c52c76c402e5740d1d14118b5e47a347f2cc247532c7d2"
DEFAULT_KO = "/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko"

PROC_TABLE = "g_ast_alg_cfg_process_info_table"
PROC_ENTRIES = 414
PROC_STRIDE = 12
LUT = "g_alg_cfg_lut"
LUT_ENTRIES = 416
LUT_STRIDE = 176
LUT_HDR = 16
LUT_FIELD = 16
LUT_MAX_FIELDS = 10

OUTPUT_TABLES = {
    "autorate": ("g_ast_hmac_config_alg_autorate_output_table", 22),
    "otp": ("g_ast_hmac_config_alg_otp_output_table", 4),
    "tpc": ("g_hmac_config_alg_tpc_output_table", 6),
    "dispatch": ("g_ast_hmac_config_alg_output_table", 32),
}

VALUE_TABLES = [
    ("rate_mode_tbl", "g_ast_alg_cfg_rate_mode_tbl", None),
    ("legacy_rate_tbl", "g_ast_alg_cfg_legacy_rate_tbl", "g_alg_cfg_legacy_rate_tbl_size"),
    ("ar_dev_param_tbl", "g_ast_alg_cfg_ar_dev_param_tbl", "g_alg_cfg_ar_dev_param_tbl_size"),
    ("protocol_mode_tbl", "g_ast_alg_cfg_protocol_mode_tbl", None),
    ("protocol_mode_user_tbl", "g_ast_alg_cfg_protocol_mode_user_tbl", None),
    ("bandwidth_mode_tbl", "g_ast_alg_cfg_bandwidth_mode_tbl", "g_alg_cfg_bandwidth_mode_tbl_size"),
    ("gi_time_ax_tbl", "g_ast_alg_cfg_gi_time_ax_tbl", None),
    ("gi_time_nonax_tbl", "g_ast_alg_cfg_gi_time_nonax_tbl", None),
    ("gi_type_tbl", "g_ast_alg_cfg_gi_type_tbl", "g_ul_alg_cfg_gi_type_tbl_size"),
    ("gi_type_nonax_tbl", "g_ast_alg_cfg_gi_type_nonax_tbl", "g_alg_cfg_gi_type_nonax_tbl_size"),
    ("preamble_tbl", "g_ast_alg_cfg_preamble_tbl", "g_alg_cfg_preamble_tbl_size"),
    ("protect_mode_tbl", "g_ast_alg_cfg_protect_mode_tbl", "g_alg_cfg_protect_mode_tbl_size"),
    ("fec_coding_tbl", "g_ast_alg_cfg_fec_coding_tbl", "g_alg_cfg_fec_coding_tbl_size"),
    ("he_ltf_tbl", "g_ast_alg_cfg_he_ltf_tbl", "g_alg_cfg_he_ltf_tbl_size"),
    ("ar_probe_tbl", "g_ast_alg_cfg_ar_probe_tbl", "g_ul_alg_cfg_ar_probe_tbl_size"),
    ("ar_scen_mode_tbl", "g_ast_alg_cfg_ar_scen_mode_tbl", "g_ul_alg_cfg_ar_scen_mode_tbl_size"),
    ("tpc_pow_mode_tbl", "g_ast_alg_cfg_tpc_pow_mode_tbl", "g_alg_cfg_tpc_pow_mode_tbl_size"),
    ("tpc_fix_pow_mode_tbl", "g_ast_alg_cfg_tpc_fix_pow_mode_tbl", "g_alg_cfg_tpc_fix_pow_mode_tbl_size"),
    ("tpc_code_protocol_tbl", "g_ast_alg_cfg_tpc_code_protocol_tbl", "g_alg_cfg_tpc_code_protocol_tbl_size"),
    ("bool_tbl", "g_ast_alg_cfg_bool_tbl", "g_alg_cfg_bool_tbl_size"),
    ("pkt_type_info_table", "g_ast_alg_cfg_pkt_type_info_table", None),
    ("he_sigb_rate_mcs_table", "g_alg_cfg_he_sigb_rate_mcs_table", "g_alg_cfg_he_sigb_rate_mcs_table_size"),
]

# The rate-control command family this lane implements (cfg ids).
RATE_IDS = list(range(401, 423)) + list(range(1801, 1810))


class Elf:
    """Minimal ELF32 little-endian relocatable reader (no pyelftools)."""

    def __init__(self, path):
        self.d = open(path, "rb").read()
        d = self.d
        if d[:4] != b"\x7fELF" or d[4] != 1 or d[5] != 1:
            raise ValueError("not an ELF32 little-endian file: %s" % path)
        self.sha256 = hashlib.sha256(d).hexdigest()
        (e_shoff,) = struct.unpack_from("<I", d, 0x20)
        self.e_shentsize, self.e_shnum, self.e_shstrndx = struct.unpack_from("<HHH", d, 0x2E)
        self.secs = []
        for i in range(self.e_shnum):
            off = e_shoff + i * self.e_shentsize
            f = struct.unpack_from("<IIIIIIIIII", d, off)
            self.secs.append(dict(name=f[0], typ=f[1], flags=f[2], addr=f[3],
                                  offset=f[4], size=f[5], link=f[6], info=f[7],
                                  align=f[8], entsize=f[9]))
        shstr = self.secs[self.e_shstrndx]
        for s in self.secs:
            o = shstr["offset"] + s["name"]
            s["sname"] = d[o:d.index(b"\x00", o)].decode("utf-8", "replace")
        self.by_name = {s["sname"]: s for s in self.secs}
        self.syms = []
        for s in self.secs:
            if s["typ"] != 2:
                continue
            strtab = self.secs[s["link"]]
            for i in range(s["size"] // 16):
                st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from(
                    "<IIIBBH", d, s["offset"] + i * 16)
                o = strtab["offset"] + st_name
                nm = d[o:d.index(b"\x00", o)].decode("utf-8", "replace") if st_name else ""
                self.syms.append(dict(i=i, name=nm, value=st_value, size=st_size,
                                      info=st_info, shndx=st_shndx))
        self.sym_by_i = {s["i"]: s for s in self.syms}
        self.relocs = []
        for s in self.secs:
            if s["typ"] != 9:
                continue
            tgt = self.secs[s["info"]]
            for i in range(s["size"] // 8):
                r_off, r_info = struct.unpack_from("<II", d, s["offset"] + i * 8)
                self.relocs.append((tgt["sname"], r_off, r_info & 0xFF,
                                    self.sym_by_i[r_info >> 8]))

    def read(self, secname, off, n):
        s = self.by_name[secname]
        return self.d[s["offset"] + off:s["offset"] + off + n]

    def u32(self, secname, off):
        return struct.unpack("<I", self.read(secname, off, 4))[0]

    def u16(self, secname, off):
        return struct.unpack("<H", self.read(secname, off, 2))[0]

    def sym(self, name):
        for s in self.syms:
            if s["name"] == name:
                return s
        return None

    def section_of(self, sym):
        return self.secs[sym["shndx"]] if 0 <= sym["shndx"] < len(self.secs) else None

    def reloc_map(self, target_section):
        out = {}
        for tgt, r_off, typ, sym in self.relocs:
            if tgt == target_section:
                out[r_off] = (typ, sym)
        return out


def sym_u32_value(elf, name):
    s = elf.sym(name)
    if not s:
        return None
    sec = elf.section_of(s)
    if not sec or sec["size"] < 4:
        return None
    return struct.unpack("<I", elf.d[sec["offset"] + s["value"]:][:4])[0]


def ro_str(elf, off):
    s = elf.by_name[".rodata.str1.4"]
    d = elf.d
    o = s["offset"] + off
    if o >= len(d):
        return "?"
    return d[o:d.index(b"\x00", o)].decode("utf-8", "replace")


def link_addend(elf, off):
    """For a linked R_ARM_ABS32 slot the stored word IS the final value
    (section-relative offset into the symbol's section); the symbol names the
    section.  Return (symbol_name, section_name, stored_word)."""
    m = elf.reloc_map(".data")
    if off not in m:
        return None, None, None
    typ, sym = m[off]
    return sym["name"], elf.secs[sym["shndx"]]["sname"], elf.u32(".data", off)


# =====================================================================
# 1. process table
# =====================================================================
def decode_process_table(elf):
    s = elf.sym(PROC_TABLE)
    if not s:
        raise ValueError("symbol %s missing" % PROC_TABLE)
    rows = []
    for i in range(PROC_ENTRIES):
        off = s["value"] + i * PROC_STRIDE
        symname, secname, word = link_addend(elf, off)
        if symname is None:
            word = elf.u32(".data", off)
        name = ro_str(elf, word) if word < elf.by_name[".rodata.str1.4"]["size"] else "?"
        cfg_id, cfg_type, resv = struct.unpack("<HBB", elf.read(".data", off + 4, 4))
        flags = elf.u32(".data", off + 8)
        rows.append(dict(idx=i, name=name, id=cfg_id, type=cfg_type,
                         flags=flags,
                         need_h_process=(flags >> 0) & 0xff,
                         need_w4_h=(flags >> 8) & 0xff,
                         need_d_process=(flags >> 16) & 0xff,
                         need_w4_d=(flags >> 24) & 0xff))
    return rows


# =====================================================================
# 2. LUT
# =====================================================================
def decode_lut(elf):
    s = elf.sym(LUT)
    if not s:
        raise ValueError("symbol %s missing" % LUT)
    drel = elf.reloc_map(".data")
    rows = []
    for i in range(LUT_ENTRIES):
        off = s["value"] + i * LUT_STRIDE
        key = elf.u32(".data", off)
        parser = drel[off + 8][1]["name"] if off + 8 in drel else None
        argc = elf.read(".data", off + 12, 1)[0]
        blob_len = elf.u16(".data", off + 14)
        fields = []
        for fi in range(min(argc, LUT_MAX_FIELDS)):
            fo = off + LUT_HDR + fi * LUT_FIELD
            kind = elf.read(".data", fo, 1)[0]
            sub = elf.read(".data", fo + 1, 1)[0]
            vsize = elf.read(".data", fo + 2, 1)[0]
            voff = elf.u16(".data", fo + 4)
            tbl = drel[fo + 8][1]["name"] if fo + 8 in drel else None
            sizep = drel[fo + 12][1]["name"] if fo + 12 in drel else None
            sizev = sym_u32_value(elf, sizep) if sizep else None
            fields.append(dict(kind=kind, sub=sub, vsize=vsize, voffset=voff,
                               table=tbl, size_sym=sizep, size=sizev))
        rows.append(dict(idx=i, key=key, cfg_id=key & 0xFFFF,
                         scope=(key >> 16) & 0xFF,
                         is_get=bool((key >> 16) & 1),
                         parser=parser, argc=argc, blob_len=blob_len,
                         fields=fields))
    return rows


# =====================================================================
# 3. response output tables
# =====================================================================
def decode_output_tables(elf):
    drel = elf.reloc_map(".data")
    out = {}
    for key, (symname, count) in OUTPUT_TABLES.items():
        s = elf.sym(symname)
        if not s:
            raise ValueError("symbol %s missing" % symname)
        rows = []
        for i in range(count):
            off = s["value"] + i * 8
            start = elf.u16(".data", off)
            end = elf.u16(".data", off + 2)
            fn = drel[off + 4][1]["name"] if off + 4 in drel else None
            rows.append(dict(start=start, end=end, handler=fn))
        out[key] = dict(symbol=symname, addr=s["value"], rows=rows)
    return out


# =====================================================================
# 4. value tables
# =====================================================================
def decode_value_tables(elf):
    out = {}
    for logical, symname, sizename in VALUE_TABLES:
        s = elf.sym(symname)
        if not s:
            out[logical] = dict(symbol=symname, error="missing")
            continue
        entries = []
        for i in range(s["size"] // 8):
            off = s["value"] + i * 8
            word = elf.u32(".data", off)
            name = ro_str(elf, word) if word < elf.by_name[".rodata.str1.4"]["size"] else ""
            entries.append(dict(value=elf.u32(".data", off + 4), name=name))
        size = sym_u32_value(elf, sizename) if sizename else None
        out[logical] = dict(symbol=symname, addr=s["value"],
                            entries=entries, declared_size=size)
    return out


# =====================================================================
# 5. rate-control wire layouts (join process table + LUT)
# =====================================================================
def rate_wire_layouts(procs, lut):
    by_cfg = {}
    for r in procs:
        by_cfg.setdefault((r["id"], r["type"]), r)
    out = []
    for row in lut:
        if row["cfg_id"] not in RATE_IDS:
            continue
        t = 1 if row["is_get"] else 0
        p = by_cfg.get((row["cfg_id"], t))
        out.append(dict(
            cfg_id=row["cfg_id"],
            access="get" if t else "set",
            name=p["name"] if p else "?",
            flags=("%#010x" % p["flags"]) if p else None,
            blob_len=row["blob_len"],
            parser=row["parser"],
            fields=row["fields"],
        ))
    out.sort(key=lambda r: (r["cfg_id"], r["access"]))
    return out


# =====================================================================
# selftest
# =====================================================================
def selftest(elf, allow_any_sha):
    checks = []

    def ck(cond, msg):
        checks.append((bool(cond), msg))

    ck(allow_any_sha or elf.sha256 == WIFI_SHA256,
       "vendor module sha256 pin (%s)" % elf.sha256[:16])
    procs = decode_process_table(elf)
    ck(len(procs) == PROC_ENTRIES, "process table has 414 entries")
    by = {(r["name"], r["type"]): r for r in procs}
    for name, cid in [("rate_mode", 401), ("ar_dev_param", 409),
                      ("hi_rate", 411), ("legacy_rate", 416),
                      ("ar_probe", 419),
                      ("tpc_mode", 1801), ("tpc_pow_lvl", 1802)]:
        r = by.get((name, 0))
        ck(r is not None and r["id"] == cid,
           "process row %s SET present with cfg id %d" % (name, cid))
        ck(r is not None and r["need_d_process"] == 1 and r["need_w4_d"] == 1,
           "%s SET is device-processed with a device response" % name)

    cur = by.get(("get_current_ration", 1))
    ck(cur is not None and cur["id"] == 421,
       "process row get_current_ration GET present with cfg id 421")
    ck(cur is not None and cur["need_d_process"] == 1 and cur["need_w4_d"] == 1,
       "get_current_ration GET is device-processed with a device response")

    lut = decode_lut(elf)
    ck(len(lut) == LUT_ENTRIES, "LUT has 416 entries")
    lut_by = {(r["cfg_id"], r["is_get"]): r for r in lut}
    rm = lut_by.get((401, False))
    ck(rm is not None and rm["blob_len"] == 11,
       "rate_mode SET alg_cfg blob length = 10 header + 1 value byte")
    ck(rm is not None and len(rm["fields"]) == 1 and rm["fields"][0]["voffset"] == 10,
       "rate_mode SET value is one byte at blob+10")
    ad = lut_by.get((409, False))
    ck(ad is not None and ad["blob_len"] == 15 and len(ad["fields"]) == 2,
       "ar_dev_param SET blob length 15 (u8 id + u32 value)")
    ck(ad is not None and ad["fields"][0]["voffset"] == 10 and
       ad["fields"][0]["vsize"] == 1 and ad["fields"][1]["voffset"] == 11 and
       ad["fields"][1]["vsize"] == 4,
       "ar_dev_param field map {u8 name-id at +10, u32 value at +11}")
    cr = lut_by.get((421, True))
    ck(cr is not None and cr["blob_len"] == 30 and len(cr["fields"]) == 1 and
       cr["fields"][0]["vsize"] == 6 and cr["fields"][0]["voffset"] == 10,
       "get_current_ration GET blob length 30 with a 6-byte MAC at +10")

    outs = decode_output_tables(elf)
    aut = outs["autorate"]["rows"]
    ck(len(aut) == 22, "autorate output table has 22 per-id rows")
    ck(sorted(r["start"] for r in aut) == list(range(401, 423)),
       "autorate output table covers ids 401..422")
    tpc = outs["tpc"]["rows"]
    ck([r["start"] for r in tpc] == [1801, 1802, 1804, 1805, 1806, 1809],
       "tpc output table covers 1801/1802/1804/1805/1806/1809 (not 1803/1807/1808)")
    disp = outs["dispatch"]["rows"]
    ck(len(disp) == 32, "top-level output dispatcher has 32 id ranges")
    ck(disp[0]["start"] == 401 and disp[0]["end"] == 422 and
       disp[0]["handler"] == "alg_autorate_config_param_output_entry",
       "top-level range 401..422 -> alg_autorate_config_param_output_entry")
    ck(disp[7]["start"] == 1801 and disp[7]["end"] == 1809 and
       disp[7]["handler"] == "alg_tpc_bbit_config_param_output_entry",
       "top-level range 1801..1809 -> alg_tpc_bbit_config_param_output_entry")

    vt = decode_value_tables(elf)
    ck([e["name"] for e in vt["rate_mode_tbl"]["entries"]] == ["auto", "fixed"],
       "rate_mode value table = auto(0)/fixed(1)")
    ck(len(vt["ar_dev_param_tbl"]["entries"]) == 26,
       "ar_dev_param value table has 26 tuning knobs")
    ck(vt["ar_dev_param_tbl"]["entries"][0]["name"] == "sudden_good_delta_gdpt_ratio",
       "ar_dev_param id 0 = sudden_good_delta_gdpt_ratio")
    ck(len(vt["legacy_rate_tbl"]["entries"]) == 12,
       "legacy rate table has 12 rates (1M..54M)")
    ck([e["value"] for e in vt["tpc_fix_pow_mode_tbl"]["entries"]] == [0, 1, 2],
       "tpc_fix_pow_mode = data/mag/ctrl")
    ck([e["name"] for e in vt["protocol_mode_tbl"]["entries"]][:3] ==
       ["11b", "11ag", "11n"], "protocol table starts 11b/11ag/11n")

    layouts = rate_wire_layouts(procs, lut)
    ck(len(layouts) == 57, "rate family wire layouts decoded (%d rows)" % len(layouts))
    got = set((r["cfg_id"], r["access"]) for r in layouts)
    ck(all((cid, "set") in got for cid in list(range(401, 421)) + [422]),
       "every autorate-family id except the read-only get_current_ration has a SET layout")
    ck(all((cid, "get") in got for cid in list(range(401, 418)) + [420, 421]),
       "every autorate-family id with a GET row has a GET layout")
    ck([cid for cid in range(401, 423) if (cid, "get") not in got] == [418, 419, 422],
       "no GET layout for gi_type/ar_probe/ar_scen_mode (write-only controls)")
    ck((421, "get") in got and (421, "set") not in got,
       "get_current_ration is GET-only")

    passed = sum(1 for ok, _ in checks if ok)
    for ok, msg in checks:
        print(("PASS " if ok else "FAIL ") + msg)
    print("decode_alg_engine: %d/%d checks PASS" % (passed, len(checks)))
    return 0 if passed == len(checks) else 1


def write_markdown(path, doc):
    lines = []
    lines.append("# Derived alg-engine tables (generated)")
    lines.append("")
    lines.append("Generated by tools/decode_alg_engine.py --markdown; the vendor")
    lines.append("module (sha256 %s) stays local and is never committed." % doc["sha256"])
    lines.append("")
    lines.append("## Rate-control command family (wire layouts)")
    lines.append("")
    lines.append("The alg_cfg blob is a 10-byte header plus the value bytes; off is")
    lines.append("relative to the blob start.  kind 0 = numeric argument,")
    lines.append("kind 1 = enum name argument, kind 2 = fixed binary (MAC).")
    lines.append("")
    lines.append("| cfg id | access | command | blob B | value fields | parser |")
    lines.append("| ---: | --- | --- | ---: | --- | --- |")
    for r in doc["rate_wire_layouts"]:
        fs = ", ".join("k%d sz%d @%d %s" % (f["kind"], f["vsize"], f["voffset"],
                       f["table"] or "") for f in r["fields"]) or "-"
        lines.append("| %d | %s | %s | %d | %s | %s |" % (
            r["cfg_id"], r["access"], r["name"], r["blob_len"], fs,
            r["parser"] or "-"))
    lines.append("")
    lines.append("## Value enums")
    for logical, vt in doc["value_tables"].items():
        if vt.get("error"):
            continue
        ents = ", ".join("%s=%d" % (e["name"], e["value"]) for e in vt["entries"])
        lines.append("")
        lines.append("### %s (%s)" % (logical, vt["symbol"]))
        lines.append("")
        lines.append(ents)
    lines.append("")
    lines.append("## Response output dispatch")
    for name, tab in doc["output_tables"].items():
        lines.append("")
        lines.append("### %s (%s)" % (name, tab["symbol"]))
        lines.append("")
        lines.append("| start | end | handler |")
        lines.append("| ---: | ---: | --- |")
        for r in tab["rows"]:
            lines.append("| %d | %d | %s |" % (r["start"], r["end"], r["handler"]))
    lines.append("")
    with open(path, "w") as fh:
        fh.write("\n".join(lines))


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--json", metavar="PATH")
    ap.add_argument("--markdown", metavar="PATH")
    ap.add_argument("--ko", default=os.environ.get("HI5622_WIFI_KO", DEFAULT_KO))
    ap.add_argument("--allow-any-sha", action="store_true")
    args = ap.parse_args(argv)
    if not os.path.isfile(args.ko):
        print("vendor module not found: %s" % args.ko, file=sys.stderr)
        print("set HI5622_WIFI_KO to the local (never committed) copy", file=sys.stderr)
        return 2
    elf = Elf(args.ko)
    if not args.allow_any_sha and elf.sha256 != WIFI_SHA256:
        print("sha256 mismatch: got %s expected %s (use --allow-any-sha to inspect a "
              "different local copy)" % (elf.sha256, WIFI_SHA256), file=sys.stderr)
        return 2
    if args.selftest:
        return selftest(elf, args.allow_any_sha)
    procs = decode_process_table(elf)
    lut = decode_lut(elf)
    outs = decode_output_tables(elf)
    vt = decode_value_tables(elf)
    layouts = rate_wire_layouts(procs, lut)
    doc = dict(module=os.path.basename(args.ko), sha256=elf.sha256,
               process_table=dict(symbol=PROC_TABLE, rows=procs),
               lut=dict(symbol=LUT, rows=lut),
               output_tables=outs,
               value_tables=vt,
               rate_wire_layouts=layouts)
    if args.json:
        with open(args.json, "w") as fh:
            json.dump(doc, fh, indent=1)
            fh.write("\n")
        print("wrote %s (%d process rows, %d LUT rows, %d rate layouts)"
              % (args.json, len(procs), len(lut), len(layouts)))
        return 0
    if args.markdown:
        write_markdown(args.markdown, doc)
        print("wrote %s" % args.markdown)
        return 0
    for r in layouts:
        fs = " ".join("k%d/sz%d/@%d/%s" % (f["kind"], f["vsize"], f["voffset"],
                     f["table"] or "-") for f in r["fields"])
        print("id=%4d %-3s %-24s blob=%3d parser=%-38s %s" % (
            r["cfg_id"], r["access"], r["name"], r["blob_len"],
            r["parser"] or "-", fs))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))