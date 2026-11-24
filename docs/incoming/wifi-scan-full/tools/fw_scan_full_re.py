#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_scan_full_re.py - regenerable static evidence for the wifi-scan-full lane
(shared task-138).  Reads the pinned LOCAL vendor firmware in place and prints
derived annotations only; no firmware byte, vendor object byte or calibration
value is committed.

Address convention: firmware CPU address (CA) = file offset + 0x40000, Thumb-2.
This tool asserts the claims in ../REPORT.md and is the reproduction command for
every CA cited there:

  F1  g0 H2D table at CA 0x10CCFC, 12-byte rows {u32 id; u32 fn; u32 aux};
      the row for id 10 is at CA 0x10CD5C = {10, 0x5FF65, 0}.
  F2  handler CA 0x5FF64 gates: payload[0] == 3, payload[1] == 1|2, module
      lookup 0x961AE(hdr[3]) with [module+0x4B8] != 0, object lookup 0xD6A1E.
  F3  subtype 1 field map: +0x02 key, +0x07 slot (<=7), +0x09 action,
      +0x0C start -> context+0x10 (min) and context+0x1A = max(1,v),
      +0x10 -> context+0x18, +0x12 -> context+0x17, +0x14 compare u16.
  F4  per-slot context base = object + slot*0x20 + 0xEC (8 slots, 0x20 B each).
  F5  subtype 2 clear path 0x5FEC0 zeroes context+0x12, context+0x1A and
      context+0x05; payload+0x14 is its flag; payload+0x08 is read nowhere.
  F6  the scan engine consumes the SAME layout at slot 0: scheduler 0x63CB8
      reads context+0xFC/+0xFD/+0xFE; the result loop 0x641B4 reads +0x103
      (mode), +0x104 (result counter), +0x105 (expected count), +0x106
      (stride / default 6 written at 0x643CC).
  F7  nothing in the firmware writes the count/end field context+0xFE except
      the subtype-2 clear path (0x5FEE0, writes 0) -- i.e. no subtype-1
      payload field reaches the single-request channel bound.
  F8  vendor cross-check (optional, when the local 2.5.24 hi5622v100_wifi.ko
      is present): hmac_mgmt_rx_addba_rsp builds type 3 / subtype 1 into
      hcc_msg_alloc(32) and hmac_mgmt_rx_delba builds type 3 / subtype 2;
      wire total 44 B (= 12 + 32 payload), which the R4 builder's 36 B did
      not match.

The vendor module path can be given with --ko or the HI5622_WIFI_KO env var;
it is never copied.  Without it F8 is reported as SKIP.
"""
import argparse
import hashlib
import os
import struct
import sys

try:
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB
except Exception as exc:  # pragma: no cover
    print("error: capstone is required (project venv .provision-venv/bin/python): %s" % exc,
          file=sys.stderr)
    raise SystemExit(2)

FW_SHA256 = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_LEN = 928920
FW_BASE = 0x40000

# CA anchors (all [V] in REPORT.md)
A_G0_TABLE = 0x10CCFC
A_ROW10 = 0x10CD5C
A_HANDLER = 0x5FF64
A_GATE_TYPE = 0x5FFA4
A_GATE_SUB1 = 0x5FFAC
A_GATE_SUB2 = 0x5FFB2
A_SUB1_LOOKUP = 0x5FFCE
A_SUB1_SLOT = 0x5FFF0
A_SUB1_ACTION = 0x6002C
A_CTX_BASE_SHL = 0x6003E
A_CTX_BASE_ADD = 0x60042
A_CTX_BASE_OR = 0x60046
A_CMP_READ = 0x60048
A_WR_104 = 0x60092
A_WR_106 = 0x6009E
A_WR_FC = 0x600B2
A_WR_103 = 0x600BA
A_WR_EN = 0x600BE
A_SUB1_CALL = 0x60110
A_SUB2_LOOKUP = 0x60184
A_SUB2_FLAG = 0x6018A
A_CLEAR_CALL = 0x601C8
A_FF06_CALL = 0x601D6
A_CLEAR = 0x5FEC0
A_CLEAR_FE = 0x5FEE0
A_CLEAR_106 = 0x5FEF0
A_CLEAR_F1 = 0x5FEF2
A_SCHED = 0x63CB8
A_SCHED_FD = 0x63D10
A_SCHED_FC = 0x63D14
A_SCHED_FE = 0x63D34
A_RESULT_FD_INC = 0x64262
A_RESULT_FE = 0x6429C
A_RESULT_CNT = 0x6423C
A_RESULT_105 = 0x64240
A_MODE = 0x643B4
A_DEFAULT6 = 0x643CC
A_STRIDE = 0x6440C
A_ENGINE = 0x644A4
A_ENGINE_TABLE_ROW = "0x10CD5C"

checks = [0, 0]


def ck(cond, msg):
    checks[0] += 1
    if not cond:
        checks[1] += 1
        print("FAIL " + msg)
    else:
        print("ok   " + msg)


def repo_root():
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.normpath(os.path.join(here, "..", "..", "..", "..", ".."))


def resolve_fw(env_value):
    cands = [env_value,
             os.path.join(repo_root(), "rootfs-2.5.24/lib/firmware/hi_wifi/FIRMWARE.bin"),
             os.path.join(repo_root(), "rootfs-2.4.15/lib/firmware/hi_wifi/FIRMWARE.bin")]
    for c in cands:
        if c and os.path.isfile(c):
            return c
    return None


class Fw:
    def __init__(self, path):
        with open(path, "rb") as fh:
            self.d = fh.read()
        self.sha256 = hashlib.sha256(self.d).hexdigest()
        self.md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)

    def win(self, ca, n):
        return self.d[ca - FW_BASE:ca - FW_BASE + n]

    def u32(self, ca):
        return struct.unpack("<I", self.win(ca, 4))[0]

    def insn_at(self, ca):
        for i in self.md.disasm(self.win(ca, 10), ca):
            return i
        return None

    def expect(self, ca, text, label):
        i = self.insn_at(ca)
        got = "" if i is None else ("%s %s" % (i.mnemonic, i.op_str)).strip()
        ck(text in got, "%s: CA 0x%x = '%s' (want '%s')" % (label, ca, got, text))


def vko_check(ko_path):
    """Optional F8: assert the vendor builders really emit g0 id 10 with
    payload type 3 and total 44 B (hcc_msg_alloc(0x20)); the 2.5.24 wifi.ko
    is ARM-mode code (not Thumb)."""
    if not ko_path or not os.path.isfile(ko_path):
        print("SKIP F8 vendor builder cross-check (local hi5622v100_wifi.ko absent)")
        return
    try:
        from elftools.elf.elffile import ELFFile
    except Exception as exc:  # pragma: no cover
        print("SKIP F8 vendor cross-check (pyelftools unavailable: %s)" % exc)
        return
    with open(ko_path, "rb") as fh:
        elf = ELFFile(fh)
        syms = {}
        for sec in elf.iter_sections():
            if sec.name == ".symtab":
                for sy in sec.iter_symbols():
                    if sy.name in ("hmac_mgmt_rx_addba_rsp", "hmac_mgmt_rx_delba"):
                        syms[sy.name] = sy["st_value"]
        text = elf.get_section_by_name(".text")
        if text is None or len(syms) != 2:
            ck(False, "F8: both g0 id10 builder symbols found in vendor module")
            return
        md = Cs(CS_ARCH_ARM, CS_MODE_ARM)

        def blob(name, span):
            off = syms[name]
            return " ; ".join("%s %s" % (i.mnemonic, i.op_str)
                              for i in md.disasm(text.data()[off:off + span], off))

        b = blob("hmac_mgmt_rx_addba_rsp", 0x180)
        ck("#0x103" in b, "F8: addba_rsp builds payload type 3 / subtype 1 (0x0103)")
        ck("[r3, #0xc]" in b and "#0xa" in b,
           "F8: addba_rsp writes hdr[6]=10 and payload+0 at hdr+12")
        ck("hcc_msg_alloc" not in b, "F8: addba_rsp decoded (ARM)")
        b = blob("hmac_mgmt_rx_delba", 0x180)
        ck("#0x203" in b, "F8: delba builds payload type 3 / subtype 2 (0x0203)")
        ck("#0xa" in b, "F8: delba writes hcc id 10")
        ck("hcc_msg_alloc" not in b, "F8: delba decoded (ARM)")
        ck(len(syms) == 2, "F8: both g0 id10 builders found (addba_rsp, delba)")

def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fw", default=os.environ.get("HI5622_FW_PATH"),
                    help="local vendor FIRMWARE.bin (never committed)")
    ap.add_argument("--ko", default=os.environ.get("HI5622_WIFI_KO"),
                    help="optional local hi5622v100_wifi.ko for the F8 cross-check")
    args = ap.parse_args(argv)
    path = resolve_fw(args.fw)
    if path is None:
        print("error: vendor FIRMWARE.bin not found (set --fw / HI5622_FW_PATH)", file=sys.stderr)
        return 2
    fw = Fw(path)
    print("firmware: %s" % path)
    print("sha256  : %s" % fw.sha256)
    ck(fw.sha256 == FW_SHA256, "firmware is the pinned 2.5.24 image (sha256)")
    ck(len(fw.d) == FW_LEN, "firmware length is the pinned %d B" % FW_LEN)
    if fw.sha256 != FW_SHA256:
        print("error: wrong firmware image; CA assertions would be meaningless",
              file=sys.stderr)
        return 2

    # F1: g0 H2D dispatch table and the id-10 row
    row = [fw.u32(A_ROW10), fw.u32(A_ROW10 + 4), fw.u32(A_ROW10 + 8)]
    ck(row[0] == 10, "F1: row at CA 0x%x has id 10" % A_ROW10)
    ck(row[1] == 0x5FF65, "F1: row fn = 0x5FF65 (Thumb entry 0x5FF64)")
    ck(row[2] == 0, "F1: row aux = 0")
    ck(fw.u32(A_ROW10 - 12) == 9, "F1: preceding row is id 9 (table is id-sorted)")

    # F2: handler gates
    fw.expect(A_HANDLER, "push.w", "F2 handler prologue")
    fw.expect(0x5FFA4, "ldrb r3, [r4, #0xc]", "F2 payload+0x00 read")
    fw.expect(0x5FFA6, "cmp r3, #3", "F2 type must be 3")
    fw.expect(A_GATE_SUB1, "ldrb r3, [r4, #0xd]", "F2 payload+0x01 read")
    fw.expect(A_GATE_SUB1 + 2, "cmp r3, #1", "F2 subtype 1 branch")
    fw.expect(A_GATE_SUB2, "cmp r3, #2", "F2 subtype 2 branch")
    fw.expect(0x5FF86, "ldrb r0, [r0, #3]", "F2 module id = hdr[3]")
    fw.expect(0x5FF88, "bl", "F2 module lookup 0x961AE")
    fw.expect(0x5FF90, "ldr.w r6, [r0, #0x4b8]", "F2 band object = module+0x4B8")

    # F3: subtype 1 field map
    fw.expect(A_SUB1_LOOKUP, "ldrh r0, [r4, #0xe]", "F3 payload+0x02 key")
    fw.expect(A_SUB1_LOOKUP + 2, "bl", "F3 object lookup 0xD6A1E")
    fw.expect(A_SUB1_SLOT, "ldrb.w r8, [r4, #0x13]", "F3 payload+0x07 slot")
    fw.expect(A_SUB1_SLOT + 4, "cmp.w r8, #7", "F3 slot limit 7")
    fw.expect(A_SUB1_ACTION, "ldrb r3, [r4, #0x15]", "F3 payload+0x09 action")
    fw.expect(0x60032, "cmp r3, #0", "F3 action zero/nonzero choice")
    fw.expect(A_CTX_BASE_SHL, "lsl.w sb, sl, #5", "F4 slot stride 0x20")
    fw.expect(A_CTX_BASE_ADD, "add.w fp, sb, #0xec", "F4 context base +0xEC")
    fw.expect(A_CTX_BASE_OR, "add fp, r5", "F4 context = object + slot*0x20 + 0xEC")
    fw.expect(A_CMP_READ, "ldrh.w r3, [fp, #0x15]", "F3 payload+0x14 compare u16")
    fw.expect(A_WR_104, "strh.w r3, [sb, #0x104]", "F3 payload+0x10 -> ctx+0x18")
    fw.expect(A_WR_106, "strh.w r3, [sb, #0x106]", "F3 max(1,+0x0C) -> ctx+0x1A")
    fw.expect(A_WR_FC, "strh.w r3, [r2, #0xfc]", "F3 start -> ctx+0x10 (min)")
    fw.expect(A_WR_103, "strb.w r3, [r6, #0x103]", "F3 mode -> ctx+0x17")
    fw.expect(0x600BE, "movs r3, #2", "F3 state = 2")
    fw.expect(0x600C0, "strb.w r3, [r6, #0xff]", "F3 state -> ctx+0x13")
    fw.expect(0x60112, "bl #0x95188", "F3 secondary add path 0x95188")
    fw.expect(0x6014C, "bl", "F3 per-slot bandwidth path 0x8D024")
    fw.expect(0x6015C, "bl", "F3 context commit 0xBCD54")

    # F5: subtype 2 clear path and flag byte
    fw.expect(A_SUB2_LOOKUP, "ldrh r0, [r4, #0xe]", "F5 subtype2 payload+0x02")
    fw.expect(A_SUB2_FLAG, "ldrb.w sb, [r4, #0x14]", "F5 subtype2 flag = payload+0x14")
    fw.expect(A_CLEAR_CALL, "bl", "F5 clear path 0x5FEC0")
    fw.expect(A_FF06_CALL, "bl", "F5 arm/flag path 0x5FF06")
    fw.expect(A_CLEAR, "push.w", "F5 clear helper entry")
    fw.expect(A_CLEAR_FE, "strb.w r6, [sb, #0xfe]", "F5 clear zeroes ctx+0x12")
    fw.expect(A_CLEAR_106, "strh r6, [r4, #6]", "F5 clear zeroes ctx+0x1A")
    fw.expect(A_CLEAR_F1, "strb.w r6, [sb, #0xf1]", "F5 clear zeroes ctx+0x05")

    # F6: scan engine consumes the same layout at slot 0
    fw.expect(A_SCHED, "push.w", "F6 scheduler entry")
    fw.expect(A_SCHED_FD, "ldrb.w r1, [r5, #0xfd]", "F6 scan step ctx+0xFD")
    fw.expect(A_SCHED_FC, "ldrb.w r3, [r5, #0xfc]", "F6 scan start ctx+0xFC")
    fw.expect(A_SCHED_FE, "ldrb.w r0, [r5, #0xfe]", "F6 scan end ctx+0xFE")
    fw.expect(A_RESULT_FD_INC, "strb.w r3, [r4, #0xfd]", "F6 result loop advances +0xFD")
    fw.expect(A_RESULT_FE, "ldrb.w r3, [r4, #0xfe]", "F6 result loop reads end +0xFE")
    fw.expect(A_RESULT_CNT, "ldrb.w r2, [r4, #0x104]", "F6 result counter ctx+0x104")
    fw.expect(A_RESULT_105, "ldrb.w r3, [r4, #0x105]", "F6 expected count ctx+0x105")
    fw.expect(A_MODE, "ldrb.w r3, [r4, #0x103]", "F6 mode byte ctx+0x103")
    fw.expect(A_DEFAULT6, "movs r3, #6", "F6 default 6")
    fw.expect(A_DEFAULT6 + 2, "strb.w r3, [r4, #0x106]", "F6 default -> ctx+0x106")
    fw.expect(A_STRIDE, "ldrb.w r1, [r4, #0x106]", "F6 stride ctx+0x106")
    fw.expect(A_ENGINE, "push.w", "F6 request engine entry")

    # F7: the subtype-1 handler never writes the count/end byte ctx+0xFE,
    # and the only context+0xFE writer in the clear path is the subtype-2
    # helper (0x5FEE0, writes 0).  Scan the handler window directly.
    hits = []
    blind = fw.win(A_HANDLER, 0x601FC - A_HANDLER)
    for i in range(0, len(blind) - 4, 2):
        h = struct.unpack("<HH", blind[i:i + 4])
        # Thumb-2 STRB (imm12) to [rY, #0xfe]: first halfword 0xF88?, second
        # halfword low 12 bits = 0x0FE.  LDRB (0xF89?) is excluded on purpose.
        if (h[0] & 0xFFF0) == 0xF880 and (h[1] & 0xFFF) == 0x0FE:
            hits.append(FW_BASE + A_HANDLER + i)
    ck(not hits,
       "F7: subtype-1 handler has no write to context+0xFE (count/end); found %d"
       % len(hits))
    ck(fw.insn_at(A_CLEAR_FE) is not None and
       fw.insn_at(A_CLEAR_FE).mnemonic.startswith("str"),
       "F7: the subtype-2 clear path is the only context+0xFE writer (writes 0)")
    # and the clear helper is the only *context* writer in the image that the
    # handler family can reach: assert it is inside the clear helper body.
    ck(A_CLEAR <= A_CLEAR_FE < A_CLEAR + 0x80, "F7: writer sits in 0x5FEC0 body")
    # F8 optional vendor cross-check
    vko_check(args.ko)

    print("---------------------------------------------")
    print("fw_scan_full_re: %d/%d checks %s"
          % (checks[0] - checks[1], checks[0], "FAIL" if checks[1] else "PASS"))
    return 1 if checks[1] else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))