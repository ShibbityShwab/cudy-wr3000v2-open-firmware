#!/usr/bin/env python3
"""Disassemble the firmware-side H2D receive path from FIRMWARE.bin (phase 21).

usage: fwaccept_disasm.py [FIRMWARE.bin]

Prints the four windows quoted in docs/phase20/fw-accept.md Part A.  File offsets
are the blob's offsets; the firmware's runtime address is file + 0x40000.
"""
import sys

from capstone import CS_ARCH_ARM, CS_MODE_THUMB, Cs

WINDOWS = [
    (0x9334, 0x9390, "firmware pcie_msg_init head (ops table / handler register)"),
    (0x9758, 0x97a8, "firmware message register map + out[0]/out[1] zero + 0x40101410 enable"),
    (0x97f4, 0x9800, "firmware pcie_msg_init tail: 0x40101430 enable"),
    (0x818a8, 0x818f4, "firmware pcie_msg_handle: ack 0x400392f0, clear out[0], re-arm 0x400392d4=8"),
]


def main() -> int:
    path = sys.argv[1] if len(sys.argv) > 1 else "../build/tmp/FIRMWARE.bin"
    d = open(path, "rb").read()
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
    for start, end, label in WINDOWS:
        print("==== %s  file 0x%x..0x%x (runtime 0x%x) ====" %
              (label, start, end, start + 0x40000))
        for i in md.disasm(d[start:end], start):
            print("  %05x rt%05x  %-8s %s" %
                  (i.address, i.address + 0x40000, i.mnemonic, i.op_str))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
