#!/usr/bin/env python3
"""Heuristic payload-access extractor for the firmware H2D handlers.

Usage:
    payload_offsets.py /path/to/FIRMWARE.bin [--verify-addresses]

For every registration-table handler this prints the payload offsets the handler
reads, with the access width: [B] = ldrb, [H] = ldrh, [W] = ldr (word; may be a
pointer dereference). The heuristic: after the firmware message-header fetch
(bl CA 0x409EA) it tracks which register(s) hold the header pointer (simple
mov/add forwarding only) and records memory operands at header offsets >= 0xC,
i.e. payload offsets = disp-0xC. It is a *candidate* list, not a dataflow proof:
use it to pick safe probe messages, not to reverse a struct field by field.
"""
import sys, struct
from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB

BASE = 0x40000
TABLES = [
    ("g2", 2, 0x10CC18, 0x13),
    ("g0", 0, 0x10CCFC, 0x28),
    ("g0-early", 0, 0x10CBEC, 0x01),
]
HDR_FETCH = 0x409EA

def u16(fw, o): return struct.unpack_from("<H", fw, o)[0]
def u32(fw, o): return struct.unpack_from("<I", fw, o)[0]

def analyze(fw, fn_ca, md, limit=0x300, window=0x140):
    fo = (fn_ca - BASE) & ~1
    end = fo + BASE + window
    hdr = set()
    pending = False
    acc = []
    for ins in md.disasm(fw[fo:fo + limit], fo + BASE):
        m = ins.mnemonic
        if m == "bl" and ins.op_str.strip() in ("#0x409ea", "#0x409eb"):
            pending = True
            continue
        if pending:
            if m in ("mov", "mov.w") and len(ins.operands) == 2                and ins.operands[0].type == 1 and ins.operands[1].type == 1                and ins.reg_name(ins.operands[1].reg) == "r0":
                hdr.add(ins.reg_name(ins.operands[0].reg))
                hdr.add("r0")
            pending = False
        if m in ("mov", "mov.w") and len(ins.operands) == 2            and all(o.type == 1 for o in ins.operands):
            d = ins.reg_name(ins.operands[0].reg)
            s = ins.reg_name(ins.operands[1].reg)
            if s in hdr:
                hdr.add(d)
        if m.startswith("ldr") and ins.operands and ins.operands[-1].type == 3:
            mem = ins.operands[-1].mem
            if ins.reg_name(mem.base) in hdr and mem.disp >= 0xC and ins.address < end:
                kind = "W" if ("b" not in m and "h" not in m) else ("B" if "b" in m else "H")
                acc.append((mem.disp - 0xC, kind))
    return sorted(set(acc))

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    fw = open(sys.argv[1], "rb").read()
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
    md.detail = True
    for label, group, tca, count in TABLES:
        f = tca - BASE
        print("## %s (group %d), table CA %#x" % (label, group, tca))
        for i in range(count):
            o = f + i * 12
            idv = u16(fw, o)
            fn = u32(fw, o + 4)
            if not fn:
                continue
            acc = analyze(fw, fn, md)
            if not acc:
                print("- id %#04x fn %#010x: (no early payload access found)" % (idv, fn))
                continue
            print("- id %#04x fn %#010x: " % (idv, fn) +
                  ", ".join("+%#x[%s]" % (off, k) for off, k in acc))
    return 0

if __name__ == "__main__":
    sys.exit(main())
