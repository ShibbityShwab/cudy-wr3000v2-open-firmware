#!/usr/bin/env python3
"""fw-scan.py - locate Thumb-2 call sites in FIRMWARE.bin by CPU address.

CPU address = file offset + 0x40000. Scans the raw image for the 32-bit Thumb
BL / BLX / B.W encodings and reports every site whose target matches one of the
CPU addresses given on the command line (also accepts a decimal/hex address and
optionally --file prints file offsets).

This was the tool used to locate the D2H send helper (CPU 0xC61E0), the D2H
notify primitive (0xC6170) and the firmware message alloc/tx twins
(0x102822 / 0x102760). It needs no vendor data at build time: the image path is
read at runtime and never copied.

usage:
  PY=.provision-venv/bin/python
  $PY tools/fw-scan.py 0x106170 0x1061E0            # CPU addresses
  $PY tools/fw-scan.py --classify 0x102822           # also print caller context
"""
import argparse
import struct
import sys

DEFAULT_FW = "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin"
BASE = 0x40000


def find_calls(data, target_cpu):
    hits = []
    n = len(data)
    for i in range(0, n - 4, 2):
        h1, h2 = struct.unpack_from("<HH", data, i)
        if (h1 & 0xF800) != 0xF000:
            continue
        kind = None
        if (h2 & 0xD000) == 0xD000:
            kind = "bl"
        elif (h2 & 0xD000) == 0xC000:
            kind = "blx"
        elif (h2 & 0xD000) == 0x9000:
            kind = "b.w"
        if not kind:
            continue
        S = (h1 >> 10) & 1
        imm10 = h1 & 0x3FF
        J1 = (h2 >> 13) & 1
        J2 = (h2 >> 11) & 1
        imm11 = h2 & 0x7FF
        I1 = 1 ^ (J1 ^ S)
        I2 = 1 ^ (J2 ^ S)
        imm = (S << 24) | (I1 << 23) | (I2 << 22) | (imm10 << 12) | (imm11 << 1)
        if imm & (1 << 24):
            imm -= 1 << 25
        acpu = BASE + i
        if kind == "blx":
            tgt = (((acpu + 4) & ~3) + imm) & 0xFFFFFFFF
        else:
            tgt = (acpu + 4 + imm) & 0xFFFFFFFF
        if tgt == target_cpu or tgt == (target_cpu & ~1) or (tgt | 1) == target_cpu:
            hits.append((i, acpu, kind))
    return hits


def context(data, file_off, n=6):
    try:
        from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB
    except ImportError:
        return []
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
    out = []
    start = max(0, file_off - n * 4)
    for insn in md.disasm(data[start:file_off + 4], BASE + start):
        out.append("  %06x  %-8s %s" % (insn.address - BASE, insn.mnemonic, insn.op_str))
    return out[-n:]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("targets", nargs="+", help="CPU addresses (hex or decimal)")
    ap.add_argument("--fw", default=DEFAULT_FW)
    ap.add_argument("--classify", action="store_true", help="print the caller context for each hit")
    ap.add_argument("--max", type=int, default=60)
    args = ap.parse_args()

    data = open(args.fw, "rb").read()
    print("image: %d bytes (CPU = file + 0x%x)" % (len(data), BASE))
    for t in args.targets:
        tgt = int(t, 0)
        hits = find_calls(data, tgt | 1) if tgt & 1 else find_calls(data, tgt)
        print("== target CPU 0x%08x (file 0x%06x): %d site(s)" % (tgt, tgt - BASE if tgt >= BASE else -1, len(hits)))
        for i, acpu, kind in hits[: args.max]:
            print("  file 0x%06x  CPU 0x%08x  %s" % (i, acpu, kind))
            if args.classify:
                for line in context(data, i):
                    print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
