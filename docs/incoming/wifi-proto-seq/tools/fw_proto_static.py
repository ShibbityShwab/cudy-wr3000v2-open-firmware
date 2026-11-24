#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_proto_static.py - static assertions for the wifi-proto-seq lane against the pinned
local vendor firmware image (Hi5622V100, Cudy WR3000 v2.0; host-only).

Lane: wifi-proto-seq (shared task task-103, 2026-11-24).
Reads the local pinned FIRMWARE.bin in place and prints derived annotations only; no
firmware byte or calibration value is written into the repository.

Address convention: CPU (CA) = file offset + 0x40000, Thumb code starts at CA & ~1.

What it verifies [V]:
  1. hcc_msg_alloc CA 0x102822 (size bound movw #0x674) - the message allocator.
  2. the D2H config-syn sender CA 0x85914: payload layout
     {cookies[8], syn_id u16 @hdr+0x14, body_len u16 @hdr+0x16, body @hdr+0x18 (+4 pad)},
     group 1, id 3, then hcc_msg_tx.
  3. g0 id2 key dispatch table CA 0x106EFC: keys 0x00FE / 0x0101 / 0x0102 / sentinel 0x1451.
  4. key-0x101 handler CA 0xD5FA6: clears dev_ret, reads data[8] (need_w4), answers through
     CA 0x85914 with syn_id 0x0101.
  5. alg-cfg range table CA 0x106D4C: entry 22 = cfg ids 3500..3530 -> handler CA 0x7243D.
  6. equipment-param sub-table CA 0x104CB8: the six calibration cfg ids -> handlers.
  7. value layout: equipment value area starts at blob+0xA; power value sizes 0x68 (2g) /
     0x46 (5g); xo-ppm u32 at +0xA; rssi block 0x28 at +0xA; blob length 270 (0x10E).
"""
import argparse
import hashlib
import os
import struct
import sys

try:
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB
except Exception:  # pragma: no cover
    print("error: capstone is required (project venv .provision-venv/bin/python)",
          file=sys.stderr)
    raise SystemExit(2)

BASE = 0x40000
FW_SHA256 = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_LEN = 928920

HCC_ALLOC = 0x102822
HCC_TX = 0x102760
CONFIG_SYN_SENDER = 0x85914
G0ID2_DISPATCH = 0x106EFC
KEY101_HANDLER = 0xD5FA6
ALG_RANGE_TABLE = 0x106D4C
EQUIP_HANDLER = 0x7243C
EQUIP_SUBTABLE = 0x104CB8
POWER_APPLY = 0xD0D44
XO_PPM_HANDLER = 0x72B88
RSSI_HANDLER = 0x72A38


def resolve_default_fw():
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.normpath(os.path.join(here, "..", "..", "..", "..", ".."))
    env = os.environ.get("HI5622_FIRMWARE")
    for c in (env,
              os.path.join(repo, "rootfs-2.4.15", "lib", "firmware", "hi_wifi",
                           "FIRMWARE.bin"),
              os.path.join(repo, "rootfs-2.5.24", "lib", "firmware", "hi_wifi",
                           "FIRMWARE.bin")):
        if c and os.path.isfile(c):
            return c
    return None


class Blob:
    def __init__(self, path):
        with open(path, "rb") as fh:
            self.d = fh.read()
        self.sha256 = hashlib.sha256(self.d).hexdigest()
        self.md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)

    def u16(self, ca):
        return struct.unpack_from("<H", self.d, ca - BASE)[0]

    def u32(self, ca):
        return struct.unpack_from("<I", self.d, ca - BASE)[0]

    def win(self, ca, n):
        return self.d[ca - BASE:ca - BASE + n]

    def dis(self, ca, n):
        return list(self.md.disasm(self.win(ca, n), ca))

    def insn(self, ca):
        out = self.dis(ca, 4)
        return out[0] if out else None


def fmt_insn(i):
    return "%08x %s %s" % (i.address, i.mnemonic, i.op_str) if i else "<none>"


def selftest(fw_path=None):
    path = fw_path or resolve_default_fw()
    if not path:
        print("FAIL: pinned FIRMWARE.bin not found", file=sys.stderr)
        return 1
    b = Blob(path)
    checks = 0

    def ck(cond, msg):
        nonlocal checks
        checks += 1
        if not cond:
            raise AssertionError("FAIL: " + msg)

    ck(b.sha256 == FW_SHA256, "pinned firmware sha256 (%s)" % b.sha256)
    ck(len(b.d) == FW_LEN, "pinned firmware length")

    # 1. allocator
    i = b.insn(HCC_ALLOC)
    ck(i and i.mnemonic == "push", "hcc_msg_alloc at %#x" % HCC_ALLOC)
    ck(b.u16(HCC_ALLOC + 2) == 0xF240 and b.u16(HCC_ALLOC + 4) == 0x6374,
       "hcc_msg_alloc movw r3,#0x674 bound encoding")
    tx = b.insn(HCC_TX)
    ck(tx and tx.mnemonic == "push", "hcc_msg_tx at %#x" % HCC_TX)

    # 2. config-syn sender layout (CA 0x85914): the exact payload/stores byte-verified
    s = b.win(CONFIG_SYN_SENDER, 0x100)
    ck(s[0:4] == bytes.fromhex("2de9f843"), "sender: push.w entry")
    ck(s[0x06:0x0A] == bytes.fromhex("02f11000"), "sender: alloc size = body_len + 0x10")
    i_alloc = b.insn(CONFIG_SYN_SENDER + 0x14)
    ck(i_alloc and i_alloc.mnemonic == "bl" and i_alloc.op_str == "#0x102822",
       "sender: hcc_msg_alloc call (%s)" % fmt_insn(i_alloc))
    i_grp = b.insn(CONFIG_SYN_SENDER + 0x4A)
    ck(i_grp and i_grp.mnemonic == "bfi" and "#0, #4" in i_grp.op_str,
       "sender: group = 1 via bfi (%s)" % fmt_insn(i_grp))
    i_id = b.insn(CONFIG_SYN_SENDER + 0x56)
    ck(i_id and i_id.mnemonic.startswith("strb") and "#-0x12" in i_id.op_str,
       "sender: id 3 store at hdr+6 (%s)" % fmt_insn(i_id))
    ck(s[0x84:0x88] == bytes.fromhex("20f8046c"),
       "sender: strh syn_id at hdr+0x14")
    ck(s[0x8C:0x90] == bytes.fromhex("20f8024c"),
       "sender: strh body_len at hdr+0x16")
    ck(s[0x90:0x94] == bytes.fromhex("40e90367"),
       "sender: strd cookie pair at hdr+0xC")
    i_cp = b.insn(CONFIG_SYN_SENDER + 0x98)
    ck(i_cp and i_cp.mnemonic == "bl" and i_cp.op_str == "#0xc1e44",
       "sender: memcpy_s body to hdr+0x18 (%s)" % fmt_insn(i_cp))
    i_tx = b.insn(CONFIG_SYN_SENDER + 0xB8)
    ck(i_tx and i_tx.mnemonic == "bl" and i_tx.op_str == "#0x102760",
       "sender: hcc_msg_tx (%s)" % fmt_insn(i_tx))

    # 3. g0 id2 key dispatch table
    rows = [(b.u16(H0), b.u32(H0 + 4)) for H0 in
            (G0ID2_DISPATCH + 8 * k for k in range(4))]
    ck(rows[0][0] == 0x00FE and rows[1][0] == 0x0101 and rows[2][0] == 0x0102,
       "g0 id2 key table keys 0xFE/0x101/0x102 (%s)" % rows)
    ck(rows[3][0] == 0x1451, "g0 id2 key table sentinel 0x1451")
    ck(rows[0][1] == 0xD5B41 and rows[1][1] == 0xD5FA7 and rows[2][1] == 0xD5F81,
       "g0 id2 key table handlers (%s)" % [hex(r[1]) for r in rows])

    # 4. key 0x101 handler: dev_ret clear, need_w4 read, 0x0101 answer via the syn sender
    h = b.win(KEY101_HANDLER, 0xC0)
    ck(h.find(bytes.fromhex("157155719571d571")) >= 0,
       "key101: clears dev_ret bytes data[4..7]")
    ck(h.find(bytes.fromhex("237a012b")) >= 0,
       "key101: ldrb r3,[data,#8] (need_w4) + cmp #1")
    ck(h.find(bytes.fromhex("40f20111")) >= 0,
       "key101: response syn id movw #0x101")
    # the answer is issued through the config-syn sender: find a bl whose target is 0x85914
    i_send = None
    for k in range(0, 0xC0, 2):
        try:
            i = b.insn(KEY101_HANDLER + k)
        except Exception:
            continue
        if i and i.mnemonic == "bl" and i.op_str == "#0x85914":
            i_send = i
            break
    ck(i_send is not None, "key101: answers through CA 0x85914 config-syn sender")

    # 5. alg-cfg range table: cfg ids 3500..3530 -> handler 0x7243D
    r = b.win(ALG_RANGE_TABLE, 0x200)
    lo, hi = struct.unpack_from("<HH", r, 22 * 12 + 0x18)
    fn = struct.unpack_from("<I", r, 22 * 12 + 0x20)[0]
    ck((lo, hi) == (3500, 3530) and fn == 0x7243D,
       "alg range entry 22 = 3500..3530 -> %#x" % fn)

    # 6. equipment sub-table: the six calibration cfg ids
    want = {3502: 0x72BEF, 3503: 0x72BE5, 3504: 0x72BDB, 3505: 0x72BD1,
            3506: 0x72B89, 3507: 0x72A39}
    got = {}
    for k in range(24):
        cid = b.u16(EQUIP_SUBTABLE + 8 * k)
        got[cid] = b.u32(EQUIP_SUBTABLE + 8 * k + 4)
    for cid, hfn in want.items():
        ck(got.get(cid) == hfn, "equipment sub-table cfg %d -> %#x" % (cid, hfn))

    # 7. value layout: value area at blob+0xA; power sizes 0x68/0x46; xo u32; rssi 0x28
    p = b.win(POWER_APPLY, 0xC0)
    ck(p.find(bytes.fromhex("00f10a02")) >= 0, "power apply: add.w r2,r0,#0xA (SET src)")
    ck(p.find(bytes.fromhex("6825")) >= 0, "power apply: 2g value size 0x68")
    ck(p.find(bytes.fromhex("4625")) >= 0, "power apply: 5g value size 0x46")
    ck(p.find(bytes.fromhex("02f2c462")) >= 0, "power apply: 2g dst offset 0x6c4")
    ck(p.find(bytes.fromhex("00f68a00")) >= 0, "power apply: 5g dst offset 0x88a")
    xo = b.win(XO_PPM_HANDLER, 0x30)
    ck(xo.find(bytes.fromhex("8b780e4a012b")) >= 0, "xo_ppm: reads cfg_type at data[2]")
    ck(xo.find(bytes.fromhex("d1f80a30")) >= 0, "xo_ppm: u32 value at blob+0xA")
    rs = b.win(RSSI_HANDLER, 0x60)
    ck(rs.find(bytes.fromhex("4ff02803")) >= 0, "rssi: 0x28-byte value block")
    ck(rs.find(bytes.fromhex("01f10a02")) >= 0, "rssi: SET src = blob + 0xA")
    # (the 270-byte blob length is a host-driver fact asserted by tools/alg_cmd_table.py)

    print("fw_proto_static: %d/%d checks PASS (fw sha256 %s)"
          % (checks, checks, b.sha256[:16]))
    return 0


def show(fw_path=None):
    path = fw_path or resolve_default_fw()
    b = Blob(path)
    print("firmware:", path)
    print("sha256  :", b.sha256)
    print()
    print("== g0 id2 key dispatch (CA %#x) ==" % G0ID2_DISPATCH)
    for k in range(4):
        ca = G0ID2_DISPATCH + 8 * k
        print("  key %#06x -> handler CA %#x" % (b.u16(ca), b.u32(ca + 4)))
    print()
    print("== equipment-param cfg ids (CA %#x) ==" % EQUIP_SUBTABLE)
    for k in range(24):
        ca = EQUIP_SUBTABLE + 8 * k
        print("  cfg %5d -> handler CA %#x" % (b.u16(ca), b.u32(ca + 4)))
    print()
    print("== config-syn D2H sender (CA %#x) ==" % CONFIG_SYN_SENDER)
    for i in b.dis(CONFIG_SYN_SENDER, 0xC0):
        print("  %08x  %-8s %s" % (i.address, i.mnemonic, i.op_str))


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--show", action="store_true", help="print derived firmware annotations")
    ap.add_argument("--fw", default=None, help="local FIRMWARE.bin path override")
    args = ap.parse_args(argv)
    if args.selftest:
        try:
            return selftest(args.fw)
        except AssertionError as exc:
            print(exc, file=sys.stderr)
            return 1
    if args.show:
        show(args.fw)
        return 0
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
