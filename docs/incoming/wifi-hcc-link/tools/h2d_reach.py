#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# h2d_reach.py -- which firmware H2D receive paths can reach the HCC link/event
# layer?  Host-only; reads the LOCAL vendor firmware blob at runtime.
#
# Receive roots are the firmware's own H2D HCC dispatch tables (re-verified in
# docs/incoming/wifi-h2d-catalog, table CAs quoted below) plus the H2D mailbox
# handlers.  Targets are the link/event functions named in the lane brief.
#
# Usage: h2d_reach.py [--fw FIRMWARE.bin] [--targets a,b,c] [--depth N]
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fw_xref

DEFAULT_FW = fw_xref.DEFAULT_FW

# H2D HCC receive tables, from wifi-h2d-catalog REPORT section 4.2/4.3
# (handler CA includes the Thumb bit; entry = CA & ~1).
G2_TABLE = 0x10CC18   # 19 entries
G0_TABLE = 0x10CCFC   # 40 entries
G0_EARLY = 0x10CBEC   # 1 entry (id 0x2F)

# H2D mailbox handlers (wifi-h2d-catalog REPORT section 3)
MAILBOX = {
    1: 0x00040511,
    3: 0x000C5145,
    5: 0x000C19DD,   # BUG
    6: 0x0008CCE5,
}

# lane-brief targets (CPU addresses, Thumb bit stripped)
TARGETS = {
    0x46848: 'link-create worker (contains obj+0x4B0 store at 0x48840)',
    0x437B0: 'per-link early bind (writes +0x4B0/+0x4B4/+0x4B8 at 0x44BBC)',
    0x7243C: 'event tag dispatcher (table 0x104CB8, tail-call callbacks)',
    0x745E0: 'callback tag 0xDBD',
    0x7469C: 'callback tag 0xDBC',
    0x87D64: 'DR descriptor-state writer (abort site)',
    0x88AFC: 'descriptor-buffer builder (calls 0x87D64)',
    0x84664: 'per-core record init (writes +0x2CC family)',
    0x65148: 'per-core bring-up loop (firmware-init only per abort-rca)',
    0xDA6DC: 'caller chain of 0x88AFC',
    0xE9A7C: 'caller chain of 0x88AFC',
}


def decode_table(data, cpu, n):
    import struct
    rows = []
    fileoff = cpu - 0x40000
    # 12-byte {u16 id; u16 pad; u32 fn; u32 aux} records
    for i in range(n):
        idv = struct.unpack_from('<H', data, fileoff + i * 12)[0]
        fn = struct.unpack_from('<I', data, fileoff + i * 12 + 4)[0]
        aux = struct.unpack_from('<I', data, fileoff + i * 12 + 8)[0]
        rows.append((idv, fn, aux))
    return rows


def main():
    fw = DEFAULT_FW
    depth = 99
    args = sys.argv[1:]
    target_set = dict(TARGETS)
    i = 0
    while i < len(args):
        if args[i] == '--fw':
            fw = args[i + 1]; i += 2
        elif args[i] == '--depth':
            depth = int(args[i + 1], 0); i += 2
        elif args[i] == '--targets':
            target_set = {}
            for x in args[i + 1].split(','):
                target_set[int(x, 0)] = 'user'
            i += 2
        else:
            raise SystemExit(__doc__)
    data = fw_xref.load(fw)
    base = 0x40000
    im = fw_xref.InsnMap(data, base)
    cg = fw_xref.CallGraph(im, base)

    roots = {}
    for gid, cpu in MAILBOX.items():
        roots[f'mailbox id {gid} fn {cpu & ~1:#x}'] = [cpu & ~1]
    for name, cpu, n in (('hcc-g2', G2_TABLE, 19), ('hcc-g0', G0_TABLE, 40), ('hcc-g0-early', G0_EARLY, 1)):
        rows = decode_table(data, cpu, n)
        for k in range(n):
            idv, fn, _aux = rows[k]
            if fn == 0:
                continue
            roots[f'{name} id {idv:#04x} fn {fn & ~1:#x}'] = [fn & ~1]

    print(f'roots: {len(roots)}  targets: {len(target_set)}')
    for label, r in roots.items():
        # skip roots that are not decode-aligned code
        for r0 in r:
            path = cg.reach([r0], list(target_set), depth)
            if path:
                desc = target_set.get(path[-1], '')
                print(f'{label} -> ' + ' > '.join(f'{x:#08x}' for x in path) + f'   [{desc}]')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
