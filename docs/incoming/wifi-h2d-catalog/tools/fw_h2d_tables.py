#!/usr/bin/env python3
"""Firmware H2D/HCC dispatch-table extractor + byte verifier (host-only, read-only).

Usage:
    fw_h2d_tables.py /path/to/FIRMWARE.bin [--verify] [--json]

Finds the firmware's HCC message-registration tables by locating the registrar's
{group, table, count} call sites in the image, decodes every
{u16 id; u16 pad; u32 handler; u32 aux} record, and prints the id -> handler
catalog for the three tables this lane recovered.

Convention: runtime CA = file offset + 0x40000 (bench-verified: the image is
uploaded to CA 0x01240000 and aliased at device CA 0x40000; the record's live
reads of CA 0x10C0F4 / 0x172130 / BAR0 0x6F8008 match this mapping).
All quoted offsets are FILE offsets unless written "CA"/"rt".

No firmware bytes are printed or committed: only addresses/ids/metadata.
The --verify mode asserts the byte-level claims the report rests on.
"""
import sys, struct, json

BASE = 0x40000
REGISTRAR_RT = 0x102866            # hcc_msg_register(group, table, count) - Thumb
TABLES = [
    # (label, group, table CA, count, registrar call-site FILE offset)
    ("group 2", 2, 0x10CC18, 0x13, 0x833C),
    ("group 0", 0, 0x10CCFC, 0x28, 0x8346),
    ("group 0 (early single)", 0, 0x10CBEC, 0x01, 0x7BC8),
]
# ---- byte-verified claims (asserted by --verify) -------------------------
VERIFY = [
    (0xC2866, bytes.fromhex("f7b5204c0346d4f84c04"), "hcc_msg_register prologue"),
    (0xC2896, bytes.fromhex("0c255d43d4f85804"), "rec = recs + group*12; itemarr=rec[1]"),
    (0xC28D8, bytes.fromhex("1301f25006eb030c"), "item[id*16].id = id (lsls #4; str)"),
    (0xC28E0, bytes.fromhex("51f80c3c0130ccf80430"), "fn=rec[4]; item[id].fn=fn"),
    (0xC28EC, struct.pack("<I", 0x171B6C), "global HCC instance literal = CA 0x171B6C"),
    (0x84E0, struct.pack("<I", 0x10CC18), "registration literal: group-2 table"),
    (0x84E4, struct.pack("<I", 0x10CCFC), "registration literal: group-0 table"),
    (0x7DA8, struct.pack("<I", 0x10CBEC), "registration literal: early group-0 table"),
    (0x8336, bytes.fromhex("1322"), "call 1: movs r2,#0x13 (count 19)"),
    (0x833A, bytes.fromhex("0220"), "call 1: movs r0,#2 (group 2)"),
    (0x8340, bytes.fromhex("2822"), "call 2: movs r2,#0x28 (count 40)"),
    (0x8344, bytes.fromhex("0020"), "call 2: movs r0,#0 (group 0)"),
    (0x7BC6, bytes.fromhex("0020"), "early registration: movs r0,#0 (group 0)"),
    (0x7BBE, bytes.fromhex("0122"), "early registration: movs r2,#1 (count 1)"),
    # mailbox-layer anchors (record evidence re-verified here)
    (0x818AC, bytes.fromhex("f8b5"), "H2D mailbox dispatcher push {r3,r4,r5,r6,r7,lr}"),
    (0x818D6, bytes.fromhex("092c"), "dispatcher: cmp r4,#9 id bound"),
    (0x819DC, bytes.fromhex("00231b60ffde"), "mailbox id-5 handler = BUG() trap"),
    (0x85144, bytes.fromhex("f0e7"), "mailbox id-3 handler thunk b 0x85128"),
    (0x510, bytes.fromhex("034b03f51d71d3f87002"), "mailbox id-1 handler prologue"),
    (0xCC1B0, struct.pack("<I", 0x118D68), "ctx+0x20 mailbox handler-table pointer"),
]

def load(path):
    return open(path, "rb").read()

def u16(fw, off): return struct.unpack_from("<H", fw, off)[0]
def u32(fw, off): return struct.unpack_from("<I", fw, off)[0]

def verify(fw):
    ok, fails = 0, []
    for off, want, what in VERIFY:
        got = fw[off:off+len(want)]
        if got == want:
            ok += 1
        else:
            fails.append((off, want.hex(), got.hex(), what))
    print(f"VERIFY: {ok}/{len(VERIFY)} byte claims PASS")
    for off, want, got, what in fails:
        print(f"  FAIL @file {off:#07x}: want {want} got {got}  ({what})")
    return not fails

def decode_tables(fw):
    out = []
    for label, group, tca, count, callsite in TABLES:
        f = tca - BASE
        t = {"label": label, "group": group, "table_ca": tca,
             "table_file": f, "count": count, "call_site": callsite,
             "entries": []}
        for i in range(count):
            o = f + i * 12
            idv, pad = u16(fw, o), u16(fw, o + 2)
            fn, aux = u32(fw, o + 4), u32(fw, o + 8)
            t["entries"].append({
                "index": i, "id": idv, "pad": pad,
                "handler_ca": fn,
                "handler_file": (fn - BASE) if fn else None,
                "aux": aux,
            })
        out.append(t)
    return out

def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 2
    fw = load(args[0])
    if "--verify" in args:
        return 0 if verify(fw) else 1
    tables = decode_tables(fw)
    if "--json" in args:
        print(json.dumps(tables, indent=1))
        return 0
    print(f"image: {len(fw)} bytes ({len(fw):#x}); CA = file + {BASE:#x}")
    print(f"registrar: CA {REGISTRAR_RT:#x} (file {REGISTRAR_RT-BASE:#x})")
    for t in tables:
        print(f"\n== {t['label']}: table CA {t['table_ca']:#x} "
              f"(file {t['table_file']:#x}), {t['count']} entries, "
              f"registered at file {t['call_site']:#x}")
        print(f"   {'idx':>3} {'id':>4} {'handler CA':>12} {'handler file':>12} {'aux':>10}")
        for e in t["entries"]:
            h = f"{e['handler_ca']:#010x}" if e["handler_ca"] else "NULL"
            hf = f"{e['handler_file']:#09x}" if e["handler_file"] is not None else "-"
            print(f"   {e['index']:>3} {e['id']:>#4x} {h:>12} {hf:>12} {e['aux']:>#10x}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
