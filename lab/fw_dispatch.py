#!/usr/bin/env python3
import struct
import sys

from capstone import CS_ARCH_ARM, CS_MODE_THUMB, Cs

DISPATCHER = 0x3243C
TABLE = 0xC4CB8
ENTRIES = 25
BIAS = 0x40000

USAGE = (
    "usage: fw_dispatch.py <FIRMWARE.bin>\n"
    "dispatcher at file 0x3243C keys on the low 16 bits of the message id and searches the 25-entry "
    "{cfg_id, handler} table at file 0xC4CB8; handler file offset = handler - 0x40000"
)


def main() -> int:
    if len(sys.argv) < 2:
        print(USAGE)
        return 2
    data = open(sys.argv[1], "rb").read()
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)

    print("== dispatcher ==")
    for ins in list(md.disasm(data[DISPATCHER:DISPATCHER + 80], DISPATCHER))[:16]:
        print(f"  {ins.address:#07x}: {ins.mnemonic} {ins.op_str}")

    print("== handler table ==")
    print("cfg_id,handler_runtime,handler_file")
    for i in range(ENTRIES):
        cfg, handler = struct.unpack_from("<II", data, TABLE + i * 8)
        print(f"{cfg:#06x},{handler:#010x},{(handler - BIAS) & ~1:#08x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
