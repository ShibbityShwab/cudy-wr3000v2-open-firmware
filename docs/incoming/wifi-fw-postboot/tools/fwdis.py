#!/usr/bin/env python3
"""fwdis.py - host-only disassembly helper for the local Hi5622V100 vendor firmware.

Reads /home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin IN PLACE and
prints DERIVED facts only.  Never copies or commits firmware bytes, never opens a device.

Address convention (project-wide): CPU address = file offset + 0x40000.

Subcommands
  dis START LEN [--arm]     linear disassembly of a window, ldr-literal values resolved
  words START [N]           dump N u32 words (default 8) from CPU START
  find VALUE                every u32-aligned occurrence of VALUE (file->CPU address)
  lrefs VALUE               every code site whose ldr pc-literal points at a word == VALUE
  lits [VALUE]              dump the (address -> word) literal map (optionally only VALUE)
  callees START LEN [--arm] bl/blx call targets in a window

Examples
  $PY tools/fwdis.py dis 0xC704C 0x120
  $PY tools/fwdis.py lrefs 0x0017D42C
  $PY tools/fwdis.py find 0x00047629
"""
import argparse
import os
import struct
import sys

from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB, CS_OP_IMM, CS_OP_REG
from capstone.arm import ARM_REG_PC, ARM_INS_LDR, ARM_INS_BL, ARM_INS_BLX, ARM_INS_MOVW, ARM_INS_MOVT

BASE = 0x40000
DEFAULT_FW = "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin"
FW = os.environ.get("HI5622_FW", DEFAULT_FW)


def load():
    with open(FW, "rb") as f:
        return f.read()


def u32(data, cpu):
    off = cpu - BASE
    if off < 0 or off + 4 > len(data):
        return None
    return struct.unpack_from("<I", data, off)[0]


def cpu2off(cpu):
    return cpu - BASE


def thumb_md(detail=True):
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
    md.detail = detail
    return md


def arm_md(detail=True):
    md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
    md.detail = detail
    return md


def insn_lines(data, start, length, arm=False, resolve=True):
    off = cpu2off(start)
    code = data[off:off + length]
    md = arm_md() if arm else thumb_md()
    out = []
    for ins in md.disasm(code, start):
        text = f"{ins.address:08x}:  {ins.mnemonic:<8} {ins.op_str}"
        if resolve and ins.id == ARM_INS_LDR and ins.operands:
            base = ins.operands[1]
            if base.type == 3 and base.mem.base == ARM_REG_PC:  # ARM_OP_MEM
                pc = (ins.address + 4) & ~3 if not arm else ins.address + 8
                la = pc + base.mem.disp
                val = u32(data, la)
                if val is not None:
                    text += f"        ; [0x{la:08x}] = 0x{val:08x}"
        out.append(text)
    return out


def cmd_dis(a):
    data = load()
    for line in insn_lines(data, a.start, a.length, arm=a.arm):
        print(line)


def cmd_words(a):
    data = load()
    for i in range(a.count):
        cpu = a.start + 4 * i
        val = u32(data, cpu)
        if val is None:
            break
        print(f"0x{cpu:08x} = 0x{val:08x}")


def cmd_find(a):
    data = load()
    pat = struct.pack("<I", a.value & 0xFFFFFFFF)
    n = 0
    for off in range(0, len(data) - 3, 2):
        if data[off:off + 4] == pat:
            print(f"occurrence: file 0x{off:06x}  cpu 0x{off + BASE:08x}")
            n += 1
    if a.count:
        print(f"total {n}")


def iter_thumb(data):
    md = thumb_md()
    md.skipdata = True
    yield from md.disasm(data, BASE)


def iter_lits(data):
    """yield (ins_addr, literal_addr, value) for every thumb ldr-pc-literal."""
    for ins in iter_thumb(data):
        if ins.id != ARM_INS_LDR or not ins.operands:
            continue
        m = ins.operands[1]
        if m.type != 3:
            continue
        if m.mem.base != ARM_REG_PC:
            continue
        pc = (ins.address + 4) & ~3
        la = pc + m.mem.disp
        val = u32(data, la)
        if val is not None:
            yield ins.address, la, val


def cmd_lrefs(a):
    data = load()
    n = 0
    for ia, la, val in iter_lits(data):
        if val == (a.value & 0xFFFFFFFF):
            print(f"cpu 0x{ia:08x}: ldr-pc-literal -> [0x{la:08x}] = 0x{val:08x}")
            n += 1
    print(f"total {n}")


def cmd_lits(a):
    data = load()
    seen = set()
    for ia, la, val in iter_lits(data):
        if a.value is not None and val != (a.value & 0xFFFFFFFF):
            continue
        key = (la, val)
        if key in seen:
            continue
        seen.add(key)
        print(f"lit 0x{la:08x} = 0x{val:08x}   (first used by 0x{ia:08x})")
    print(f"total {len(seen)}")


def cmd_callees(a):
    data = load()
    off = cpu2off(a.start)
    code = data[off:off + a.length]
    md = arm_md() if a.arm else thumb_md()
    for ins in md.disasm(code, a.start):
        if ins.id in (ARM_INS_BL, ARM_INS_BLX) and ins.operands and ins.operands[0].type == CS_OP_IMM:
            print(f"0x{ins.address:08x}: {ins.mnemonic} 0x{ins.operands[0].imm:08x}")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    d = sub.add_parser("dis")
    d.add_argument("start", type=lambda x: int(x, 0))
    d.add_argument("length", type=lambda x: int(x, 0))
    d.add_argument("--arm", action="store_true")
    d.set_defaults(func=cmd_dis)

    w = sub.add_parser("words")
    w.add_argument("start", type=lambda x: int(x, 0))
    w.add_argument("count", type=int, nargs="?", default=8)
    w.set_defaults(func=cmd_words)

    f = sub.add_parser("find")
    f.add_argument("value", type=lambda x: int(x, 0))
    f.add_argument("--count", action="store_true")
    f.set_defaults(func=cmd_find)

    r = sub.add_parser("lrefs")
    r.add_argument("value", type=lambda x: int(x, 0))
    r.set_defaults(func=cmd_lrefs)

    l = sub.add_parser("lits")
    l.add_argument("value", type=lambda x: int(x, 0), nargs="?")
    l.set_defaults(func=cmd_lits)

    c = sub.add_parser("callees")
    c.add_argument("start", type=lambda x: int(x, 0))
    c.add_argument("length", type=lambda x: int(x, 0))
    c.add_argument("--arm", action="store_true")
    c.set_defaults(func=cmd_callees)

    a = p.parse_args()
    a.func(a)


if __name__ == "__main__":
    main()
