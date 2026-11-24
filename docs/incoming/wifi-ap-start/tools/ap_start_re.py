#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""ap_start_re.py - regenerable static evidence for the wifi-ap-start lane.

Lane: wifi-ap-start (shared task task-121, 2026-11-26).  Reads the pinned
LOCAL vendor artifacts in place and prints derived annotations only.  No
firmware byte, vendor object byte, calibration value or secret is committed.

Address conventions (same as ../wifi-scan-d2h/tools/fw_scan_d2h_re.py):
  firmware CPU address (CA) = file offset + 0x40000; Thumb code at CA & ~1
  vendor .ko addresses are .text section offsets.

Evidence asserted here is recorded [V] in ../REPORT.md / ../TABLES.md:
  F1 firmware config-key table A at CA 0x10C23C, 217 x 8-byte rows; the AP
     command keys dispatch to workers: 0x0005 -> 0x82879, 0x0127 -> 0x84DA5.
  F2 the group-0 id-1 handler CA 0x43D58 reads the key at header+0x14, the
     body length byte at +0x16 and calls worker(ctx, len, body at +0x18).
  F3 the WAL-command worker CA 0x84DA4 consumes the 156-byte body exactly as
     decoded: +0x00/+0x04 u32, +0x08 flag, +0x09 RSN-element id 0x30,
     +0x49 vendor-element id 0xDD, +0x89 hidden, +0x8A..+0x8E capability
     bits/mode, +0x94..+0x98 mobility domain.
  F4 the set-SSID worker CA 0x82878 copies body[0] bytes from body+3 into
     VAP ctx+0x190 -> +0x24 (the 36-byte SSID body).
  H1 vendor host AP-start sequence (2.5.24 hi5622v100_wifi.ko, sha de78ec07):
     wal_cfg80211_start_ap -> wal_send_set_ssid_event (WAL cmd 5, body 36),
     wal_cfg80211_set_auth_mode (WAL cmd 0x6D, body 4),
     wal_cfg80211_start_ap_write_msg (WAL cmd 0x127, body 156) +
     wal_start_vap (WAL cmd 0x65, body 12).
  H2 the host AP-config builder fills the body fields asserted in F3
     (fill_beacon_param CA 0x101774, parse_ht_vht_ie CA 0x1076FC,
     parse_md_ie CA 0xFFFF4).
"""
import argparse
import hashlib
import os
import struct
import sys

try:
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB
    from elftools.elf.elffile import ELFFile
except Exception as exc:  # pragma: no cover
    print("error: capstone and pyelftools are required (project venv "
          ".provision-venv/bin/python): %s" % exc, file=sys.stderr)
    raise SystemExit(2)

FW_DEFAULT = "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin"
KO_DEFAULT = "/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_wifi.ko"
FW_SHA256 = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_LEN = 928920
KO_SHA256 = "de78ec07e46e70ce8befa788a2a5d11d17e80cdea7c2789b721b0be5dcfd9521"
KO_SIZE = 3564728
FW_BASE = 0x40000

TAB_A = 0x10C23C
TAB_A_ROWS = 217
G0_ID1_HANDLER = 0x43D58
AP_CFG_WORKER = 0x84DA4
SSID_WORKER = 0x82878

# host module .text anchors (section offsets), 2.5.24
KO_SYMS = {
    "wal_cfg80211_start_ap": 0x103634,
    "wal_cfg80211_start_ap_write_msg": 0x103548,
    "wal_cfg80211_fill_beacon_param": 0x101774,
    "wal_cfg80211_set_auth_mode": 0x101584,
    "wal_send_set_ssid_event": 0x1284EC,
    "wal_start_vap": 0x11519C,
    "wal_parse_ht_vht_ie": 0x1076FC,
    "wal_parse_md_ie": 0xFFFF4,
}

# instruction words (ARM little-endian) that encode the decoded behaviour
KO_WORDS = [
    # start_ap: body[0x93] = 1, cmd 0x0127, body_len 156
    (0x1037AC, 0xE5CD30A3, "strb r3,[sp,#0xa3]  body+0x93 = 1 (start)"),
    (0x1037B0, 0xE3003127, "movw r3,#0x127  WAL AP-config command"),
    (0x1037B8, 0xE3A0309C, "mov r3,#156  AP-config body length"),
    # write_msg: 156-byte copy, 160-byte event, then wal_start_vap
    (0x103564, 0xE3A0309C, "mov r3,#156  memcpy body length"),
    (0x103588, 0xE3A020A0, "mov r2,#160  WAL cfg event size"),
    (0x103590, 0xE3A01001, "mov r1,#1  WAL cfg-event id"),
    # set_ssid: cmd 5, body 36, ssid_len byte at body+0, ssid at body+3
    (0x128510, 0xE5CDC010, "strb ip,[sp,#16]  body[0] = ssid_len"),
    (0x128514, 0xE3A0C005, "mov ip,#5  WAL set-SSID command"),
    (0x12852C, 0xE3A0C024, "mov ip,#36  SSID body length"),
    # auth mode: cmd 0x6D, body 4
    (0x101588, 0xE3A0206D, "mov r2,#0x6d  WAL auth command"),
    (0x101598, 0xE58D1010, "str r1,[sp,#16]  body = u32 auth type"),
    (0x1015A0, 0xE3A02004, "mov r2,#4  auth body length"),
    # fill_beacon_param: flag byte, hidden byte, mode byte
    (0x101864, 0xE5C45008, "strb r5,[r4,#8]  body+0x08 = 0 (no IE copies)"),
    (0x101894, 0x05C43089, "strbeq r3,[r4,#0x89]  hidden-SSID flag"),
    (0x101958, 0xE5C4308E, "strb r3,[r4,#0x8e]  wireless-mode byte"),
    # parse_md_ie: MDID + capability bits into body+0x94..0x98
    (0x100044, 0xE1C439B4, "strh r3,[r4,#0x94]  MDID"),
    (0x100030, 0xE5C43096, "strb r3,[r4,#0x96]  MD present"),
    (0x100058, 0xE5C43097, "strb r3,[r4,#0x97]  MD bit0"),
    (0x100064, 0xE5C43098, "strb r3,[r4,#0x98]  MD bit1"),
]

KO_STRINGS = [
    "beacon_interval=%d, dtim_period=%d, hidden_ssid=%d!",
    "en_auth_algs error[%d]",
    "wal_send_set_ssid_event::fail to send ssid cfg msg!",
    "wal_cfg80211_start_ap::failed to start addset beacon, error[%d]",
    "wal_cfg80211_start_ap::failed to start ap, error[%d]",
]


def rev(paths, env):
    val = os.environ.get(env)
    cands = ([val] if val else []) + paths
    for cand in cands:
        if cand and os.path.isfile(cand):
            return cand
    return None


class Fw(object):
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as fh:
            self.d = fh.read()
        self.sha256 = hashlib.sha256(self.d).hexdigest()
        self.md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)

    def u16(self, ca):
        return struct.unpack_from("<H", self.d, ca - FW_BASE)[0]

    def u32(self, ca):
        return struct.unpack_from("<I", self.d, ca - FW_BASE)[0]

    def insn(self, ca, mnemonic, ops):
        """True when the Thumb instruction at CA decodes to mnemonic+operands."""
        out = list(self.md.disasm(self.d[ca - FW_BASE:ca - FW_BASE + 4], ca))
        if not out:
            return False
        ins = out[0]
        have = (ins.mnemonic + " " + ins.op_str).replace(" ", "")
        want = (mnemonic + " " + ops).replace(" ", "")
        return have == want


class Ko(object):
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as fh:
            self.d = fh.read()
        self.sha256 = hashlib.sha256(self.d).hexdigest()
        self.elf = ELFFile(open(path, "rb"))
        self.text = self.elf.get_section_by_name(".text")
        if self.text is None:
            raise SystemExit("error: %s has no .text" % path)

    def word(self, addr):
        return struct.unpack_from("<I", self.d, self.text["sh_offset"] + addr)[0]

    def has_string(self, needle):
        return needle.encode() in self.d


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--fw", default=None, help="firmware blob path")
    ap.add_argument("--ko", default=None, help="vendor host module path")
    ap.add_argument("--selftest", action="store_true",
                    help="run all probes; exit nonzero on failure")
    ap.add_argument("--tables", action="store_true",
                    help="print the decoded table-A rows for the AP commands")
    ap.add_argument("--disasm", type=str, default=None,
                    help="dump the Thumb worker at CA (hex address)")
    args = ap.parse_args()

    fw_path = rev([FW_DEFAULT,
                   os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "..", "..", "..", "..",
                                "recovery-extract", "lib", "firmware",
                                "hi_wifi", "FIRMWARE.bin")], "HAF_FW")
    ko_path = rev([KO_DEFAULT], "HAF_KO")
    if not args.tables and not args.disasm:
        if not fw_path or not ko_path:
            print("error: pinned local firmware/module not found; set HAF_FW/"
                  "HAF_KO" + (" --tables/--disasm needs them too"),
                  file=sys.stderr)
            return 2
        if not args.selftest:
            ap.print_help()
            print("\nlocal evidence present: fw=%s ko=%s" % (fw_path, ko_path))
            return 0

    fw = Fw(fw_path) if fw_path else None
    ko = Ko(ko_path) if ko_path else None

    if args.disasm:
        if not fw:
            print("error: firmware not found", file=sys.stderr)
            return 2
        ca = int(args.disasm, 16) & ~1
        for ins in fw.md.disasm(fw.d[ca - FW_BASE:ca - FW_BASE + 0x200], ca):
            print("%#08x  %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))
        return 0

    if args.tables:
        if not fw:
            print("error: firmware not found", file=sys.stderr)
            return 2
        for row in (1, 2, 18, 110):
            ca = TAB_A + row * 8
            print("table A row %3d: key=%#06x pad=%#06x worker=%#x" %
                  (row, fw.u16(ca), fw.u16(ca + 2), fw.u32(ca + 4)))
        return 0

    checks = []

    def ck(cond, what):
        checks.append((bool(cond), what))

    ck(fw.sha256 == FW_SHA256 and len(fw.d) == FW_LEN,
       "firmware blob sha256/size pin")
    ck(ko.sha256 == KO_SHA256 and len(ko.d) == KO_SIZE,
       "host module sha256/size pin (2.5.24)")
    ck(fw.u16(TAB_A) == 0x0004 and fw.u32(TAB_A + 4) == 0x0811CD,
       "table A row 0 (key 0x0004 -> helper 0x811CD)")
    ck(fw.u16(TAB_A + 8) == 0x0064 and fw.u32(TAB_A + 12) == 0x0449E5,
       "table A key 0x0064 -> module/VAP-add worker 0x449E4")
    ck(fw.u16(TAB_A + 16) == 0x0065 and fw.u32(TAB_A + 20) == 0x046395,
       "table A key 0x0065 -> VAP-start worker 0x46394")
    ck(fw.u16(TAB_A + 18 * 8) == 0x0005 and
       fw.u32(TAB_A + 18 * 8 + 4) == 0x082879,
       "table A key 0x0005 -> set-SSID worker 0x82878")
    ck(fw.u16(TAB_A + 110 * 8) == 0x0127 and
       fw.u32(TAB_A + 110 * 8 + 4) == 0x084DA5,
       "table A key 0x0127 -> AP-config worker 0x84DA4")
    ck(fw.u32(0x43E18) == TAB_A, "g0 id1 handler references table A")
    ck(fw.insn(G0_ID1_HANDLER + 0x38, "ldrh", "r2, [r4, #0x14]"),
       "g0 id1 handler reads the config key at hdr+0x14")
    ck(fw.insn(G0_ID1_HANDLER + 0x94, "add.w", "r2, r4, #0x18"),
       "g0 id1 handler passes body pointer frame+0x18")
    ck(fw.insn(G0_ID1_HANDLER + 0x98, "ldrb", "r1, [r4, #0x16]"),
       "g0 id1 handler passes body length byte frame+0x16")
    ck(fw.insn(G0_ID1_HANDLER + 0x9A, "blx", "r3"),
       "g0 id1 handler calls the table-A worker")

    # F3: body field consumers in the AP-config worker
    f3 = [
        (0x02, "ldr", "r3, [r2, #4]"),
        (0x10, "ldr", "r3, [r2]"),
        (0x18, "ldrb.w", "r2, [r5, #0x89]"),
        (0x29C, "ldrb", "r3, [r5, #9]"),
        (0xAE, "ldrb.w", "r1, [r5, #0x8a]"),
        (0xC0, "ldrb.w", "r1, [r5, #0x8b]"),
        (0xCC, "ldrb.w", "r1, [r5, #0x8c]"),
        (0xE2, "ldrb.w", "r1, [r5, #0x8d]"),
        (0xF2, "ldrh.w", "r2, [r5, #0x94]"),
        (0x102, "ldrb.w", "r1, [r5, #0x97]"),
        (0x116, "ldrb.w", "r1, [r5, #0x98]"),
        (0x12A, "ldrb.w", "r1, [r5, #0x96]"),
        (0x13A, "ldrb.w", "r1, [r5, #0x8e]"),
        (0x1F6, "cmp", "r3, #0xdd"),
        (0x29E, "cmp", "r3, #0x30"),
    ]
    for off, mn, ops in f3:
        ck(fw.insn(AP_CFG_WORKER + off, mn, ops),
           "AP-config worker body access %s %s" % (mn, ops))
    # F4: SSID worker copies the 36-byte body into VAP ctx +0x24
    ck(fw.insn(SSID_WORKER + 0x16, "ldrb", "r4, [r2], #3"),
       "set-SSID worker reads body[0] length and skips 3")
    ck(fw.insn(SSID_WORKER + 0x1A, "ldr.w", "r5, [r0, #0x190]"),
       "set-SSID worker binds the VAP object")
    ck(fw.insn(SSID_WORKER + 0x24, "adds", "r5, #0x24"),
       "set-SSID worker SSID target VAP+0x24")

    # H1/H2: host module symbols and instruction words
    for name, addr in KO_SYMS.items():
        sym = None
        symtab = ko.elf.get_section_by_name(".symtab")
        for s in symtab.iter_symbols():
            if s.name == name:
                sym = s
                break
        ck(sym is not None and sym["st_value"] == addr,
           "host symbol %s at .text %#x" % (name, addr))
    for addr, word, what in KO_WORDS:
        ck(ko.word(addr) == word, "host instruction %#x: %s" % (addr, what))
    for needle in KO_STRINGS:
        ck(ko.has_string(needle), "host string: %s" % needle)
    ck(ko.text["sh_size"] > 0x1284EC + 0xF4,
       "host .text covers the SSID builder")

    fails = [w for ok, w in checks if not ok]
    print("ap_start_re: %u/%u probes PASS"
          % (len(checks) - len(fails), len(checks)))
    for w in fails:
        print("FAIL: %s" % w)
    if fails:
        return 1
    print("note: derived addresses/constants only; no vendor byte is printed "
          "or committed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
