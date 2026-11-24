#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_sta_join_re.py - regenerable static evidence for wifi-sta-join (task-146).

Reads the pinned LOCAL vendor module (never committed) read-only and prints
DERIVED facts only: symbol/instruction offsets, decoded fields, dispatch-table
ids.  No vendor code text, data byte, firmware byte or secret is emitted.

What it re-derives (each asserted, FAIL -> non-zero exit):
  A  cfg80211 .connect / .disconnect -> WAL cfg event chain:
     wal_cfg80211_connect builds a 108-byte stack body; wal_cfg80211_start_*
     pass {opcode, body_len} to wal_cfg80211_start_req (0x123/108 and
    0x00d0/16), which prepends the {u16 opcode, u16 len} WAL data block.
  B  the 108-byte body field map: the 12 stores that fill it (offsets +0x01,
     +0x02, +0x04, +0x08, +0x0c, +0x10, +0x11, +0x12, +0x13, +0x14, +0x68)
     and their cfg80211_connect_params sources.
  C  the WAL chip-group-2 dispatch table registered by wal_main_init
     (hcc_msg_register_tab_chip(2, table, 0x13)): id 3 ->
     wal_asoc_comp_proc_sta, id 4 -> wal_disasoc_comp_proc_sta.
  D  the D2H completion decode: the exact instructions of both handlers that
     read status/bssid/lengths/addresses out of the received frame and the
     cfg80211 completion calls they resolve to.

Usage:
  .provision-venv/bin/python tools/fw_sta_join_re.py [/path/to/hi5622v100_wifi.ko]
  (or set HI5622_WIFI_KO; the default candidates are the local stock copies)
"""
import hashlib
import os
import struct
import sys

try:
    from elftools.elf.elffile import ELFFile
    from elftools.elf.relocation import RelocationSection
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
except Exception as exc:  # pragma: no cover
    print("error: pyelftools+capstone required (project .provision-venv): %s"
          % exc, file=sys.stderr)
    raise SystemExit(2)

KO_SHA256 = "de78ec07e46e70ce8befa788a2a5d11d17e80cdea7c2789b721b0be5dcfd9521"
KO_SIZE = 3564728

# link-time .text offsets of the 2.5.24 vendor module (decoded facts)
SYMS = {
    "wal_cfg80211_connect": (0x100590, 0x2b4),
    "wal_cfg80211_disconnect": (0x100844, 0x1b8),
    "wal_cfg80211_start_connect": (0x128310, 0x24),
    "wal_cfg80211_start_disconnect": (0x128334, 0xbc),
    "wal_cfg80211_start_req": (0x127ec4, 0x1d0),
    "wal_send_cfg_event": (0x1270d8, 0x474),
    "wal_asoc_comp_proc_sta": (0x125b6c, 0x2d0),
    "wal_disasoc_comp_proc_sta": (0x125e3c, 0x124),
    "wal_main_init": (0x123cac, 0x150),
    "oal_cfg80211_connect_result": (0x888, 0x98),
    "oal_cfg80211_disconnected": (0x77c, 0x28),
}

DISPATCH_TABLE = 0x1c74        # .data: 19 entries x 12 B {u32 id, fn, u32 arg}
DISPATCH_COUNT = 0x13

class Ko(object):
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as fh:
            self.blob = fh.read()
        self.sha256 = hashlib.sha256(self.blob).hexdigest()
        self.elf = ELFFile(open(path, "rb"))
        self.text = self.elf.get_section_by_name(".text")
        self.tdata = self.text.data()
        self.data = self.elf.get_section_by_name(".data").data()
        symtab = self.elf.get_section_by_name(".symtab")
        self.syms = list(symtab.iter_symbols())
        self.by_name = {}
        for s in self.syms:
            self.by_name.setdefault(s.name, s)
        # .rel.text -> function-name annotations
        self.relocs = {}
        secs = list(self.elf.iter_sections())
        tidx = secs.index(self.text)
        for sec in secs:
            if isinstance(sec, RelocationSection) and sec["sh_info"] == tidx:
                for r in sec.iter_relocations():
                    self.relocs[r["r_offset"]] = self.syms[r["r_info_sym"]].name
        self.md = Cs(CS_ARCH_ARM, CS_MODE_ARM)

    def dis(self, off, n):
        return list(self.md.disasm(self.tdata[off:off + n], off))

    def insn(self, off):
        out = self.dis(off, 4)
        for i in out:
            if i.address == off:
                return i
        return None

    def reloc(self, off):
        return self.relocs.get(off)

    def words(self, off, n):
        return list(struct.unpack_from("<%dI" % n, self.data, off))

    def data_reloc(self, off):
        # .rel.data -> name of the symbol stored at .data+off
        for sec in self.elf.iter_sections():
            if isinstance(sec, RelocationSection) and \
                    sec.name == ".rel.data":
                for r in sec.iter_relocations():
                    if r["r_offset"] == off:
                        return self.syms[r["r_info_sym"]].name
        return None


checks = 0
fails = 0


def ck(cond, msg):
    global checks, fails
    checks += 1
    if not cond:
        fails += 1
        print("FAIL: " + msg)


def expect(ko, off, mnemonic, op_str, why=""):
    i = ko.insn(off)
    ok = i is not None and i.mnemonic == mnemonic and i.op_str == op_str
    ck(ok, "%s at .text+0x%x: expected '%s %s', got '%s'"
       % (why, off, mnemonic, op_str, i.mnemonic + " " + i.op_str if i else
          "<none>"))
    return i


def expect_reloc(ko, off, sym, why=""):
    got = ko.reloc(off)
    ck(got == sym, "%s: reloc at .text+0x%x expected %s, got %s"
       % (why, off, sym, got))


def expect_bl(ko, off, sym, why=""):
    i = ko.insn(off)
    ck(i is not None and i.mnemonic in ("bl", "blx"),
       "%s: call at .text+0x%x (got %s)" % (why, off,
       i.mnemonic + " " + i.op_str if i else "<none>"))
    expect_reloc(ko, off, sym, why)


def section_a(ko):
    print("== A. cfg80211 connect/disconnect -> WAL cfg event")
    # .connect == the 108-byte stack body + wal_cfg80211_start_connect(dev,body)
    expect(ko, 0x1005ec, "mov", "r3, #0x6c", "connect: body memset size 108")
    expect_bl(ko, 0x1005f8, "memset_s", "connect: memset_s target")
    expect_bl(ko, 0x100694, "wal_cfg80211_start_connect",
              "connect: body handed to start_connect")
    # start_connect: opcode 0x123, body 108, wait=1 -> start_req
    expect(ko, 0x128314, "mov", "r2, #1", "start_connect: wait=1")
    expect(ko, 0x128318, "movw", "r3, #0x123", "start_connect: WAL opcode")
    expect(ko, 0x128324, "mov", "r2, #0x6c", "start_connect: body len 108")
    expect_bl(ko, 0x128328, "wal_cfg80211_start_req",
              "start_connect: -> start_req")
    # disconnect: 16-byte body, vendor constant [15]=1, opcode 0xd0
    expect(ko, 0x128360, "strb", "r3, [r4, #0xf]",
           "start_disconnect: body[15]=1")
    expect(ko, 0x1283c0, "mov", "r2, #0x10",
           "start_disconnect: body len 16")
    expect(ko, 0x1283c8, "mov", "r3, #0xd0",
           "start_disconnect: WAL opcode")
    expect_bl(ko, 0x1283cc, "wal_cfg80211_start_req",
              "start_disconnect: -> start_req")
    # start_req: {u16 opcode, u16 len} header at sp+0x14/0x16, 0x24c bound
    expect(ko, 0x127ef8, "cmp", "r4, #0x24c", "start_req: body bound")
    expect(ko, 0x127efc, "strh", "r3, [sp, #0x14]", "start_req: opcode store")
    expect(ko, 0x127f00, "strh", "r4, [sp, #0x16]", "start_req: len store")
    print("  .connect    -> opcode 0x123, body 108 B")
    print("  .disconnect -> opcode 0x0d0, body  16 B (payload[15]=1)")


def section_b(ko):
    print("== B. 108-byte connect body field map (stack body at sp+0x18)")
    # (pc, expected insn, body offset, cfg80211 params source note)
    stores = [
        (0x100600, "str", "r3, [sp, #0x20]", "+0x08 ssid address"),
        (0x100618, "strb", "r3, [sp, #0x19]", "+0x01 ssid_len"),
        (0x100620, "str", "r3, [sp, #0x24]", "+0x0c bssid address"),
        (0x10062c, "strb", "r3, [sp, #0x29]", "+0x11 auth_type"),
        (0x100638, "strb", "r3, [sp, #0x28]", "+0x10 privacy"),
        (0x100644, "strb", "r3, [sp, #0x1a]", "+0x02 mfp"),
        (0x100680, "str", "r2, [sp, #0x1c]", "+0x04 ie address"),
        (0x100684, "str", "r3, [sp, #0x80]", "+0x68 ie_len"),
        (0x1006d4, "strb", "r7, [sp, #0x2a]", "+0x12 key_len"),
        (0x1006d8, "str", "r2, [sp, #0x2c]", "+0x14 key address"),
        (0x1006dc, "strb", "r3, [sp, #0x2b]", "+0x13 key_idx"),
    ]
    for pc, mn, ops, field in stores:
        i = expect(ko, pc, mn, ops, "body store " + field)
        if i is not None:
            print("  .text+0x%06x  %-10s %-22s  %s" % (pc, i.mnemonic,
                                                       i.op_str, field))
    # cfg80211_connect_params source offsets -> the params fields
    for pc, off, field in [(0x1005fc, 0x10, "params+0x10 ssid ptr"),
                           (0x100608, 0x14, "params+0x14 ssid_len"),
                           (0x10061c, 0x08, "params+0x08 bssid ptr"),
                           (0x100624, 0x18, "params+0x18 auth_type"),
                           (0x100630, 0x24, "params+0x24 privacy"),
                           (0x10063c, 0x28, "params+0x28 mfp"),
                           (0x1006c0, 0x78, "params+0x78 key_len"),
                           (0x1006cc, 0x74, "params+0x74 key ptr"),
                           (0x1006d0, 0x79, "params+0x79 key_idx")]:
        i = ko.insn(pc)
        ck(i is not None and (i.mnemonic.startswith("ldr")) and
           (("#0x%x]" % off) in i.op_str or ("#%d]" % off) in i.op_str),
           "params load at .text+0x%x (%s)" % (pc, field))
    print("  NOTE: the four address fields are stored RAW from cfg80211")
    print("        params pointers (no translation helper call between the")
    print("        load and the store) - the vendor runs VMSPLIT_2G, where")
    print("        kmalloc VA == device host-DRAM address.  Our VMSPLIT_3G")
    print("        kernel must pass dma device addresses instead.")


def section_c(ko):
    print("== C. WAL chip-group-2 dispatch table (wal_main_init)")
    expect_bl(ko, 0x123ce0, "hcc_msg_register_tab_chip",
              "wal_main_init: table registration call")
    expect(ko, 0x123cd0, "mov", "r2, #0x13", "table entry count 19")
    expect(ko, 0x123cdc, "mov", "r0, #2", "chip/group 2")
    # .LANCHOR1 relocation at 0x123cd4/0x123cd8 points at .data+0x1c74
    for off in (0x123cd4, 0x123cd8):
        i = ko.insn(off)
        ck(i is not None and i.mnemonic.startswith("movw") or
           (i is not None and i.mnemonic.startswith("movt")),
           "table base load at .text+0x%x" % off)
    table = {}
    for n in range(DISPATCH_COUNT):
        base = DISPATCH_TABLE + n * 12
        mid = ko.words(base, 1)[0]
        fn = ko.data_reloc(base + 4)
        table[mid] = fn
        if fn:
            print("  id %-4u -> %s" % (mid, fn))
    ck(table.get(3) == "wal_asoc_comp_proc_sta",
       "id 3 handler: %s" % table.get(3))
    ck(table.get(4) == "wal_disasoc_comp_proc_sta",
       "id 4 handler: %s" % table.get(4))
    print("  => D2H WAL g2 id3 = STA assoc completion, id4 = disassoc")


def section_d(ko):
    print("== D. D2H completion decode (wire = 12-byte HCC hdr + payload)")
    a = "wal_asoc_comp_proc_sta"
    b = "wal_disasoc_comp_proc_sta"
    # --- assoc completion (id 3) ---
    expect(ko, 0x125b98, "ldr", "r6, [r3, #0x118]",
           a + ": received frame pointer")
    expect_reloc(ko, 0x125bb0, "hmac_vap_get_net_device",
                 a + ": netdev from hdr[1]>>4 / hdr[3]")
    expect(ko, 0x125bd4, "add", "r2, r6, #0x10",
           a + ": bssid source hdr+0x10")
    expect(ko, 0x125bd8, "mov", "r1, r3", a + ": bssid len 6")
    expect_bl(ko, 0x125be0, "memcpy_s", a + ": memcpy_s target")
    expect(ko, 0x125be4, "ldr", "r2, [r6, #0x20]",
           a + ": req_ie address hdr+0x20")
    expect(ko, 0x125be8, "ldr", "r3, [r6, #0x18]",
           a + ": req_ie_len hdr+0x18")
    expect(ko, 0x125bec, "ldr", "ip, [r6, #0x1c]",
           a + ": resp_ie_len hdr+0x1c")
    expect(ko, 0x125bf0, "strd", "r2, r3, [sp, #0x1c]",
           a + ": stage req_ie addr/len")
    expect(ko, 0x125bf4, "str", "ip, [sp, #0x24]",
           a + ": stage resp_ie len")
    expect(ko, 0x125bf8, "ldr", "r0, [r6, #0x24]",
           a + ": resp_ie address hdr+0x24")
    expect(ko, 0x125bfc, "ldrh", "r1, [r6, #0xe]",
           a + ": status hdr+0xe")
    expect(ko, 0x125c00, "str", "r0, [sp, #0x18]",
           a + ": stage resp_ie addr")
    expect(ko, 0x125c08, "strh", "r1, [sp, #0x16]",
           a + ": stage status")
    expect_bl(ko, 0x125c8c, "oal_cfg80211_connect_result",
              a + ": -> oal_cfg80211_connect_result")
    # success path also feeds the BSS database
    expect_reloc(ko, 0x125c60, "wal_update_bss",
                 a + ": success path BSS update")
    # oal wrapper layout -> cfg80211_connect_done
    expect_reloc(ko, 0x8f0, "cfg80211_connect_done",
                 "oal_cfg80211_connect_result: -> cfg80211_connect_done")
    print("  payload offsets (payload base = hdr + 0x0c):")
    print("    +0x02 u16 status        (hdr+0x0e)")
    print("    +0x04 u8  bssid[6]      (hdr+0x10)")
    print("    +0x0c u32 req_ie_len    (hdr+0x18)")
    print("    +0x10 u32 resp_ie_len   (hdr+0x1c)")
    print("    +0x14 u32 req_ie ADDR   (hdr+0x20)")
    print("    +0x18 u32 resp_ie ADDR  (hdr+0x24)")
    print("    +0x00, +0x0a            not read by the handler [U]")
    # --- disassoc completion (id 4) ---
    expect(ko, 0x125e68, "ldr", "r4, [r3, #0x118]",
           b + ": received frame pointer")
    expect_bl(ko, 0x125e80, "hmac_vap_get_net_device",
              b + ": hmac_vap_get_net_device")
    expect(ko, 0x125ea0, "ldrh", "r1, [r4, #0xc]",
           b + ": reason code hdr+0x0c (payload+0)")
    expect(ko, 0x125ea4, "ldrd", "r2, r3, [sp, #0xc]",
           b + ": ie/ie_len zeroed")
    expect_bl(ko, 0x125eb8, "oal_cfg80211_disconnected",
              b + ": -> oal_cfg80211_disconnected")
    expect_reloc(ko, 0x794, "cfg80211_disconnected",
                 "oal_cfg80211_disconnected: -> cfg80211_disconnected")
    print("  payload +0x00 u16 reason code (cfg80211 reason)")


def main():
    path = None
    if len(sys.argv) > 1:
        path = sys.argv[1]
    else:
        path = os.environ.get("HI5622_WIFI_KO")
        if not path:
            for c in ("/tmp/rootfs2524b/lib/modules/5.10.201/"
                      "hi5622v100_wifi.ko",
                      "/home/shibbityshwab/recovery-extract/lib/hisilicon/"
                      "ko/hi5622v100_wifi.ko"):
                if os.path.isfile(c):
                    path = c
                    break
    if not path or not os.path.isfile(path):
        print("error: local vendor module not found (pass it as argv[1] or "
              "set HI5622_WIFI_KO); it is never committed", file=sys.stderr)
        return 2
    ko = Ko(path)
    print("== module identity")
    print("  file   %s" % path)
    print("  size   %d" % len(ko.blob))
    print("  sha256 %s" % ko.sha256)
    ck(len(ko.blob) == KO_SIZE, "ko size pin")
    ck(ko.sha256 == KO_SHA256, "ko sha256 pin")
    print("== symbol anchors (link-time .text offsets)")
    for name, (off, size) in sorted(SYMS.items(), key=lambda kv: kv[1][0]):
        got = ko.by_name.get(name)
        ok = got is not None and got["st_value"] == off and \
            got["st_size"] == size
        ck(ok, "symbol %s @0x%x size 0x%x" % (name, off, size))
        print("  %-34s .text+0x%06x size 0x%x" % (name, off, size))
    section_a(ko)
    section_b(ko)
    section_c(ko)
    section_d(ko)
    print("checks=%d fails=%d" % (checks, fails))
    if fails:
        print("STA-JOIN-RE-FAIL")
        return 1
    print("STA-JOIN-RE-PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
