#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_key_worker.py - decode one firmware g0-id1 key-table A worker (lane
rx-init-decode/task-170).  Reads a LOCAL FIRMWARE.bin (never copied into the
repo) and prints the key-table A entry plus the Thumb disassembly of its worker.

Usage: fw_key_worker.py --fw /path/FIRMWARE.bin --key 0x0004
CA = file offset + 0x40000 (project convention).  Requires capstone.
"""
import argparse, struct, sys
from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB

TABLE_A = 0x10C23C     # CA; 217 x {u16 key; u16 pad; u32 worker}
TABLE_A_N = 217
BASE = 0x40000

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fw", required=True)
    ap.add_argument("--key", type=lambda s: int(s, 0), required=True)
    ap.add_argument("--insns", type=int, default=24)
    a = ap.parse_args()
    fw = open(a.fw, "rb").read()
    worker = None
    for i in range(TABLE_A_N):
        off = TABLE_A - BASE + i * 8
        key, _pad, w = struct.unpack_from("<HHI", fw, off)
        if key == a.key:
            print("table A[%d] @CA 0x%x key=0x%04x worker=CA 0x%08x" % (i, TABLE_A + i * 8, key, w))
            worker = w
            break
    if worker is None:
        print("key 0x%04x is NOT live in table A (no worker)" % a.key)
        return 1
    start = worker & ~1
    code = fw[start - BASE:start - BASE + a.insns * 4]
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
    for ins in md.disasm(code, start):
        print("  %08x: %s\t%s" % (ins.address, ins.mnemonic, ins.op_str))
        if ins.mnemonic == "bx" and ins.op_str == "lr":
            break
    return 0

if __name__ == "__main__":
    sys.exit(main())
