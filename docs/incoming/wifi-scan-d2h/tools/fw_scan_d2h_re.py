#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_scan_d2h_re.py - regenerable static evidence for the scan D2H RE lane.

Lane: wifi-scan-d2h (shared task task-118, 2026-11-26).  Reads the pinned
LOCAL vendor artifacts in place and prints derived annotations only.  No
firmware byte, vendor object byte, calibration value or secret is committed.

Address conventions:
  firmware CPU address (CA) = file offset + 0x40000; Thumb code at CA & ~1
  vendor .ko offsets are .text section offsets; file offset = .text sh_offset +
  offset (0x38 for this module).  The .ko is ARM mode, the firmware Thumb-2.

Evidence asserted here (all [V] in ../REPORT.md):
  F1  the firmware scan-request handler CA 0x6494C: hcc_msg_get_header, VAP-0
      lookup CA 0x581C8, tail-call into the scan engine CA 0x644A4; the engine
      copies the 0x1FC-byte request to vap+0x300 and sets vap+0x51C=1.
  F2  the firmware g1 id17 sender CA 0x641B4: alloc 0x70, group 1, id 0x11
      store, field B 0x12, 0x6C-byte copy from ctx+0x1688 to payload+4, tx.
  H1  host handlers and their payload reads: hmac_scan_proc_scanned_bss,
      hmac_scan_proc_scan_result_handle (frame_len = len-0x14 + IE parse),
      hmac_fill_bss_dscr (20-byte trailer fields), scan_comp (payload[0xC]),
      chan_result (payload[0xC] slot / payload+0x10 block / 0x6C copy) and
      wal_scan_comp_proc_sta (payload[0xD]) -> wal_inform_all_bss.
"""
import argparse
import hashlib
import os
import struct
import sys

try:
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB
    from elftools.elf.elffile import ELFFile
    from elftools.elf.relocation import RelocationSection
except Exception as exc:  # pragma: no cover
    print("error: capstone and pyelftools are required (project venv "
          ".provision-venv/bin/python): %s" % exc, file=sys.stderr)
    raise SystemExit(2)

FW_SHA256 = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_LEN = 928920
KO_SHA256 = "de78ec07e46e70ce8befa788a2a5d11d17e80cdea7c2789b721b0be5dcfd9521"
KO_SIZE = 3564728
KO_TEXT_OFF = 0x38
FW_BASE = 0x40000

# --- firmware anchors (CA) -------------------------------------------------
FW_SCAN_REQ_HANDLER = 0x6494C
FW_SCAN_ENGINE = 0x644A4
FW_ID17_SENDER = 0x641B4
FW_GET_HEADER = 0x409EA
FW_HCC_TX = 0x102760
FW_VAP_LOOKUP = 0x581C8

# --- host .ko anchors (.text section offsets) ------------------------------
KO_SYMS = {
    "hmac_scan_proc_scanned_bss": 0x9E00C,
    "hmac_scan_proc_scan_comp_event": 0x9A0EC,
    "hmac_scan_process_chan_result_event": 0x9EB54,
    "wal_scan_comp_proc_sta": 0x125714,
    "hmac_scan_proc_scan_result_handle": 0x9CF7C,
    "hmac_fill_bss_dscr": 0x9CCB4,
}


def repo_root():
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.normpath(os.path.join(here, "..", "..", "..", "..", ".."))


def resolve(rel_candidates, env):
    root = repo_root()
    value = os.environ.get(env) if env else None
    for c in [value] + [os.path.join(root, r) for r in rel_candidates]:
        if c and os.path.isfile(c):
            return c
    return None


class Fw(object):
    def __init__(self, path):
        with open(path, "rb") as fh:
            self.d = fh.read()
        self.sha256 = hashlib.sha256(self.d).hexdigest()
        self.md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)

    def win(self, ca, n):
        return self.d[ca - FW_BASE:ca - FW_BASE + n]

    def dis(self, ca, n):
        return list(self.md.disasm(self.win(ca, n), ca))

    def insn(self, ca):
        out = self.dis(ca, 4)
        return out[0] if out else None

    def u32(self, ca):
        return struct.unpack_from("<I", self.d, ca - FW_BASE)[0]


class Ko(object):
    def __init__(self, path):
        self.path = path
        self.fh = open(path, "rb")
        self.e = ELFFile(self.fh)
        self.text = self.e.get_section_by_name(".text")
        self.tdata = self.text.data()
        self.symlist = list(self.e.get_section_by_name(".symtab")
                            .iter_symbols())
        self.fh.seek(0)
        self.sha256 = hashlib.sha256(self.fh.read()).hexdigest()
        self.md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
        self.relocs = {}
        secs = list(self.e.iter_sections())
        tidx = secs.index(self.text)
        for sec in secs:
            if isinstance(sec, RelocationSection) and sec["sh_info"] == tidx:
                for r in sec.iter_relocations():
                    sym = self.symlist[r["r_info_sym"]]
                    self.relocs[r["r_offset"]] = sym.name

    def sim(self, sym):
        for s in self.symlist:
            if s.name == sym and s["st_shndx"] == 1:
                return s["st_value"], s["st_size"]
        return None, None

    def dis(self, off, n):
        return list(self.md.disasm(self.tdata[off:off + n], off))

    def insn_at(self, off):
        for i in self.dis(off, 4):
            if i.address == off:
                return i
        return None

    def bl_target(self, off):
        """Resolve the R_ARM_CALL relocation at an instruction offset."""
        return self.relocs.get(off)


def fmt(i):
    if not i:
        return "<none>"
    return "0x%06x %s %s" % (i.address, i.mnemonic, i.op_str)


def selftest(args):
    checks = 0
    fails = 0

    def ck(cond, msg):
        nonlocal checks, fails
        checks += 1
        if not cond:
            fails += 1
            print("FAIL: " + msg, file=sys.stderr)

    fw_path = args.fw or resolve(
        ["rootfs-2.5.24/lib/firmware/hi_wifi/FIRMWARE.bin",
         "rootfs-2.4.15/lib/firmware/hi_wifi/FIRMWARE.bin"],
        "HI5622_FIRMWARE")
    ko_path = args.ko or resolve(
        ["rootfs-2.5.24/lib/modules/5.10.201/hi5622v100_wifi.ko",
         "rootfs-2.4.15/lib/hisilicon/ko/hi5622v100_wifi.ko"],
        "HI5622_WIFI_KO")
    if not fw_path or not ko_path:
        print("FAIL: pinned local vendor artifacts not found "
              "(set HI5622_FIRMWARE / HI5622_WIFI_KO)", file=sys.stderr)
        return 1

    fw = Fw(fw_path)
    ko = Ko(ko_path)

    # ---------------- pins ----------------
    ck(fw.sha256 == FW_SHA256, "firmware sha256 (got %s)" % fw.sha256)
    ck(len(fw.d) == FW_LEN, "firmware length %d" % len(fw.d))
    ck(ko.sha256 == KO_SHA256, "wifi.ko sha256 (got %s)" % ko.sha256)
    ck(os.path.getsize(ko_path) == KO_SIZE,
       "wifi.ko size %d" % os.path.getsize(ko_path))

    # ---------------- F1: scan request handler + engine ----------------
    i = fw.insn(FW_SCAN_REQ_HANDLER)
    ck(i and i.mnemonic == "push.w", "F1 scan handler entry: " + fmt(i))
    ck(fw.insn(FW_SCAN_REQ_HANDLER + 0x18).mnemonic == "bl" and
       fw.insn(FW_SCAN_REQ_HANDLER + 0x18).op_str == "#0x409ea",
       "F1 scan handler hcc_msg_get_header: " +
       fmt(fw.insn(FW_SCAN_REQ_HANDLER + 0x18)))
    ck(fw.insn(FW_SCAN_REQ_HANDLER + 0x50).mnemonic == "bl" and
       fw.insn(FW_SCAN_REQ_HANDLER + 0x50).op_str == "#0x581c8",
       "F1 scan handler VAP-0 lookup CA 0x581C8: " +
       fmt(fw.insn(FW_SCAN_REQ_HANDLER + 0x50)))
    tail = fw.insn(FW_SCAN_REQ_HANDLER + 0x82)
    ck(tail and tail.mnemonic == "b" and tail.op_str == "#0x644a4",
       "F1 scan handler tail-call into scan engine: " + fmt(tail))
    eng = fw.dis(FW_SCAN_ENGINE, 0x40)
    ck(eng and eng[0].mnemonic == "push.w", "F1 engine entry push")
    found_51c = False
    found_copy = False
    for ins in fw.dis(FW_SCAN_ENGINE, 0x120):
        if ins.mnemonic == "strb.w" and "#0x51c" in ins.op_str:
            found_51c = True
        if ins.mnemonic == "mov.w" and "#0x1fc" in ins.op_str:
            found_copy = True
    ck(found_51c, "F1 engine sets vap+0x51C scan-in-progress")
    ck(found_copy, "F1 engine copies the 0x1FC-byte request")

    # ---------------- F2: g1 id17 sender ----------------
    s = fw.dis(FW_ID17_SENDER, 8)
    ck(s and s[0].mnemonic == "push.w", "F2 id17 sender entry: " + fmt(s[0]))
    i = fw.insn(0x642DA)
    ck(i and i.mnemonic == "movs" and i.op_str == "r0, #0x70",
       "F2 id17 sender payload alloc 0x70: " + fmt(i))
    i = fw.insn(0x64314)
    ck(i and i.mnemonic == "bfi" and "#0, #4" in i.op_str,
       "F2 id17 sender header group nibble: " + fmt(i))
    g = fw.insn(0x6430E)
    ck(g and g.mnemonic == "movs" and g.op_str == "r2, #1",
       "F2 id17 sender group value 1: " + fmt(g))
    i = fw.insn(0x6431C)
    ck(i and i.mnemonic == "movs" and i.op_str == "r2, #0x11",
       "F2 id17 sender id value 0x11: " + fmt(i))
    i = fw.insn(0x64320)
    ck(i and i.mnemonic.startswith("strb") and "#-0xa" in i.op_str,
       "F2 id17 sender id store at hdr+6: " + fmt(i))
    i = fw.insn(0x64348)
    ck(i and i.mnemonic.startswith("strb") and "#-0xd" in i.op_str,
       "F2 id17 sender field B store at hdr+3: " + fmt(i))
    i = fw.insn(0x6434C)
    ck(i and i.mnemonic.startswith("strb") and "#-0x4" in i.op_str,
       "F2 id17 sender payload[0] (slot index) store: " + fmt(i))
    i = fw.insn(0x64350)
    ck(i and i.mnemonic == "movs" and i.op_str == "r2, #0x6c",
       "F2 id17 sender block copy length 0x6c: " + fmt(i))
    i = fw.insn(0x64358)
    ck(i and i.mnemonic == "bl" and i.op_str == "#0x102760",
       "F2 id17 sender hcc_msg_tx: " + fmt(i))

    # ---------------- H1: host handlers ----------------
    for name, off in KO_SYMS.items():
        st, size = ko.sim(name)
        ck(st == off, "H1 symbol %s at 0x%x (got 0x%x)" % (name, off, st or 0))
        ck(size and size > 100, "H1 symbol %s has a body" % name)
    # scanned_bss wrapper: msg+0x118 = frame data pointer, calls the parser
    i = ko.insn_at(0x9E01C)
    ck(i and i.mnemonic == "ldr" and "[r4, #0x118]" in i.op_str,
       "H1 scanned_bss reads msg+0x118: " + fmt(i))
    calls = {}
    for off in range(0x9E00C, 0x9E00C + 0x230, 4):
        if ko.bl_target(off):
            calls.setdefault(ko.bl_target(off), []).append(off)
    ck("hmac_scan_proc_scan_result_handle" in calls,
       "H1 scanned_bss calls hmac_scan_proc_scan_result_handle via reloc")
    # big parser: frame_len = len - 0x14 then IE parse; fill_bss_dscr trailer use
    i = ko.insn_at(0x9D104)
    ck(i and i.mnemonic == "sub" and i.op_str == "r3, sl, #0x14",
       "H1 parser frame_len = skb_len - 0x14: " + fmt(i))
    ck(ko.bl_target(0x9D180) == "hmac_fill_bss_dscr",
       "H1 parser calls hmac_fill_bss_dscr (reloc: %s)" %
       ko.bl_target(0x9D180))
    ck(ko.bl_target(0x9D160) == "mac_ie_get_chan_num",
       "H1 parser calls mac_ie_get_chan_num (reloc: %s)" %
       ko.bl_target(0x9D160))
    i = ko.insn_at(0x9CD6C)
    ck(i and i.mnemonic.startswith("ldrb") and "[r8, #4]" in i.op_str,
       "H1 fill_bss_dscr trailer+0x04 u8: " + fmt(i))
    i = ko.insn_at(0x9CD7C)
    ck(i and i.mnemonic == "ldr" and "[r8]" in i.op_str,
       "H1 fill_bss_dscr trailer+0x00 u32: " + fmt(i))
    i = ko.insn_at(0x9CDC0)
    ck(i and i.mnemonic.startswith("ldrb") and "[r8, #0x11]" in i.op_str,
       "H1 fill_bss_dscr trailer+0x11 phy mode: " + fmt(i))
    ck(ko.insn_at(0x9CE6C).op_str == "r3, #0x10" and
       ko.insn_at(0x9CE5C).op_str == "r3, #0x11" and
       ko.insn_at(0x9CDD8).op_str.startswith("r3, #0xf"),
       "H1 phy mode mapping 8->0x10 12->0x11 4->0x0f")
    # scan complete: status at msg+0xC
    i = ko.insn_at(0x9A0F8)
    ck(i and i.mnemonic == "ldr" and "[r0, #0x118]" in i.op_str,
       "H1 scan_comp reads msg+0x118: " + fmt(i))
    i = ko.insn_at(0x9A154)
    ck(i and i.mnemonic.startswith("ldrb") and "[r5, #0xc]" in i.op_str,
       "H1 scan_comp status at frame+0xC: " + fmt(i))
    # chan result: slot at msg+0xC, 0x6C copy from msg+0x10, chan at msg+0x10
    i = ko.insn_at(0x9EB84)
    ck(i and i.mnemonic.startswith("ldrb") and "[r6, #0xc]" in i.op_str,
       "H1 chan_result slot at frame+0xC: " + fmt(i))
    i = ko.insn_at(0x9EBC8)
    ck(i and i.mnemonic.startswith("ldrb") and "[r6, #0x10]" in i.op_str,
       "H1 chan_result channel number at frame+0x10: " + fmt(i))
    ck(ko.insn_at(0x9EBA8).op_str == "r7, #0x6c",
       "H1 chan_result block copy length 0x6C")
    # g2 id2: status at frame+0xD + wal_inform_all_bss
    i = ko.insn_at(0x125724)
    ck(i and i.mnemonic == "ldr" and "[r1, #0x118]" in i.op_str,
       "H1 wal_scan_comp reads msg+0x118: " + fmt(i))
    i = ko.insn_at(0x1257D0)
    ck(i and i.mnemonic.startswith("ldrb") and "[r5, #0xd]" in i.op_str,
       "H1 wal_scan_comp status at frame+0xD: " + fmt(i))
    found_wal = False
    for off in range(0x125714, 0x125714 + 0x460, 4):
        if ko.bl_target(off) == "wal_inform_all_bss":
            found_wal = True
    ck(found_wal, "H1 wal_scan_comp calls wal_inform_all_bss")

    print("fw_scan_d2h_re: %d/%d checks %s (fw %s, ko %s)" %
          (checks - fails, checks, "PASS" if not fails else "FAIL",
           fw.sha256[:16], ko.sha256[:16]))
    return 0 if not fails else 1


def show(args):
    fw_path = args.fw or resolve(
        ["rootfs-2.5.24/lib/firmware/hi_wifi/FIRMWARE.bin",
         "rootfs-2.4.15/lib/firmware/hi_wifi/FIRMWARE.bin"],
        "HI5622_FIRMWARE")
    ko_path = args.ko or resolve(
        ["rootfs-2.5.24/lib/modules/5.10.201/hi5622v100_wifi.ko"],
        "HI5622_WIFI_KO")
    fw = Fw(fw_path)
    ko = Ko(ko_path)
    print("firmware:", fw_path)
    print("  sha256:", fw.sha256, "len:", len(fw.d))
    print("wifi.ko :", ko_path)
    print("  sha256:", ko.sha256)
    print()
    print("== firmware g1 id17 sender CA %#x ==" % FW_ID17_SENDER)
    for i in fw.dis(FW_ID17_SENDER, 0x1B0):
        print("  %s" % fmt(i))
    print()
    print("== host scan handlers (.text section offsets) ==")
    for name, off in KO_SYMS.items():
        st, size = ko.sim(name)
        print("  %-40s sym 0x%06x file 0x%06x size %d" %
              (name, st, KO_TEXT_OFF + st, size))
    print()
    print("== host relocations used by the decoder evidence ==")
    for off in (0x9D15C, 0x9D180, 0x9E0CC, 0x1257EC):
        print("  site 0x%06x -> %s" % (off, ko.bl_target(off)))


def main(argv):
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--show", action="store_true")
    ap.add_argument("--fw", default=None)
    ap.add_argument("--ko", default=None)
    args = ap.parse_args(argv)
    if args.selftest:
        return selftest(args)
    if args.show:
        show(args)
        return 0
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
