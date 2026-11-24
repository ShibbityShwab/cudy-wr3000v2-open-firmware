#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_dp_tables.py - decode/verify the firmware's group-0 config-event key tables.

Lane wifi-datapath (shared task task-105), host-only static RE. Reads the LOCAL vendor
firmware blob in place and prints/derives annotations only; it never copies vendor bytes
into the repository and never opens a device.

What this verifies (all addresses are firmware CPU addresses, CPU = file + 0x40000):

  group-0 H2D id-1 handler CA 0x43D58  ->  key = u16 at hdr+0x14 (payload+0x08)
      table A: CA 0x10C23C, 217 x {u16 key; u16 pad; u32 worker}
      table B: CA 0x103BF8,  88 x {u16 key; u16 pad; u32 worker}
  worker is a Thumb address (low bit set).

The data-plane keys used by this lane:
  0x0004  module-flag write        worker CA 0x811CD
  0x0064  context create           worker CA 0x449E5   (bench-validated: create frame)
  0x0078  key install (vendor)     worker CA 0xA5825
  0x00D8  TXQ config sync          worker CA 0x81039   (vendor capture)
  0x0128  hmac band config (vendor)worker CA 0x8313D
  0x0311  waveapp scene (vendor)   worker CA 0x436F9
  0x0320  TXQ-table address sync   worker CA 0x81445   (decoded from shipped host module)

Cross-check this tool performs: every hdpp_config_send_event / hmac_config_send_event key
found in the shipped 2.5.24 host module (0x78, 0x78, 0x320 from hdpp and 0x311 from hmac)
MUST exist in table A - i.e. the vendor's group-0 id-1 keys live in this firmware table.

Usage:
  fw_dp_tables.py --selftest                 # assertions, exit 0 on PASS
  fw_dp_tables.py --tables                   # decode both tables
  fw_dp_tables.py --map                      # data-plane key map only
  fw_dp_tables.py --emit-md PATH             # write the decoded table as markdown

The firmware path defaults to the local evidence copy; override with --fw.
"""
import argparse
import hashlib
import struct
import sys

BASE = 0x40000
DEFAULT_FW = "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin"
FW_SHA256 = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"

TABLES = {
    "A": (0x10C23C, 217),
    "B": (0x103BF8, 88),
}

VENDOR_KEYS = {
    0x0064: "context-create (hdpp cmd; bench-validated frame)",
    0x0078: "key install (hdpp_config_send_event from hmac_user_add_key/hmac_install_rekey)",
    0x00D8: "TXQ config sync (vendor live capture; firmware worker stores data[0])",
    0x0128: "hmac band config (hmac_config_send_event from hmac_config_alg)",
    0x0311: "waveapp scene config (hmac_config_send_event)",
    0x0320: "TXQ-table address sync (hmac_txq_table_addr_sync, 4-byte devVA)",
}
DATA_PLANE_KEYS = {
    0x0004: "module flag write through the created context (+0x52)",
    0x0064: "context create (allocates ctx+0x190; only route to CA 0xFE6BC)",
    0x0078: "key install",
    0x00D8: "TXQ config sync (context consumer; create must precede)",
    0x0128: "hmac band config",
    0x0311: "waveapp scene config",
    0x0320: "TXQ-table address sync (first post-ready vendor frame class)",
}


def load(fw_path):
    return open(fw_path, "rb").read()


def u16(fw, ca):
    return struct.unpack_from("<H", fw, ca - BASE)[0]


def u32(fw, ca):
    return struct.unpack_from("<I", fw, ca - BASE)[0]


def decode_table(fw, ca, n):
    rows = []
    for i in range(n):
        e = ca + i * 8
        rows.append((u16(fw, e), u16(fw, e + 2), u32(fw, e + 4)))
    return rows


def find_key(rows, key):
    for i, (k, _pad, worker) in enumerate(rows):
        if k == key:
            return i, worker
    return None, None


def cmd_selftest(fw):
    checks = []

    def chk(cond, what):
        checks.append((bool(cond), what))

    chk(len(fw) == 928920, "firmware length 928,920 B")
    chk(hashlib.sha256(fw).hexdigest() == FW_SHA256, "firmware sha256 matches the pinned image")
    tA = decode_table(fw, *TABLES["A"])
    tB = decode_table(fw, *TABLES["B"])
    chk(len(tA) == 217, "table A has 217 entries")
    chk(len(tB) == 88, "table B has 88 entries")
    # each table ends with a sentinel slot {key 0x1451, worker 0}; the live search
    # entries above it are Thumb workers.
    chk(tA[-1][0] == 0x1451 and tA[-1][2] == 0, "table A ends with sentinel {key 0x1451, worker 0}")
    chk(tB[-1][0] == 0x1451 and tB[-1][2] == 0, "table B ends with sentinel {key 0x1451, worker 0}")
    chk(all(worker & 1 for _k, _p, worker in tA[:-1]), "table A live workers are Thumb (bit0=1)")
    chk(all(worker & 1 for _k, _p, worker in tB[:-1]), "table B live workers are Thumb (bit0=1)")
    chk(all(worker != 0 for _k, _p, worker in tA[:-1]), "table A live entries have non-NULL workers")
    i, w = find_key(tA, 0x0064)
    chk((i, w) == (1, 0x449E5), "key 0x0064 -> entry 1 worker CA 0x449E5")
    i, w = find_key(tA, 0x00D8)
    chk((i, w) == (41, 0x81039), "key 0x00D8 -> entry 41 worker CA 0x81039")
    i, w = find_key(tA, 0x0320)
    chk((i, w) == (164, 0x81445), "key 0x0320 -> entry 164 worker CA 0x81445")
    i, w = find_key(tA, 0x0004)
    chk((i, w) == (0, 0x811CD), "key 0x0004 -> entry 0 worker CA 0x811CD")
    for key in sorted(VENDOR_KEYS):
        i, w = find_key(tA, key)
        if i is None:
            i, w = find_key(tB, key)
        chk(i is not None, "vendor wire key 0x%04x present in tables A or B" % key)
        chk(w is not None and (w & 1), "vendor wire key 0x%04x resolves to a Thumb worker" % key)
    _i, w_b = find_key(tB, 0x0064)
    chk(w_b is None, "key 0x0064 is table-A only")
    keysA = [k for k, _p, _w in tA]
    chk(len(keysA) == len(set(keysA)), "table A keys are unique")
    keysB = [k for k, _p, _w in tB]
    chk(len(keysB) == len(set(keysB)), "table B keys are unique")
    for key in DATA_PLANE_KEYS:
        i, _w = find_key(tA, key)
        if i is None:
            i, _w = find_key(tB, key)
        chk(i is not None, "data-plane key 0x%04x present in tables A or B" % key)
    return checks


def emit_table_md(tA, tB, path):
    out = []
    out.append("# Firmware group-0 id-1 config-event key tables (decoded constants)")
    out.append("")
    out.append("Generated by tools/fw_dp_tables.py --emit-md; do not edit by hand.")
    out.append("Source: pinned local FIRMWARE.bin sha256 " + FW_SHA256 + " (vendor bytes not committed).")
    out.append("")
    out.append("The firmware group-0 id-1 handler (CA 0x43D58) reads key = u16 hdr+0x14")
    out.append("(payload+0x08) and dispatches to a worker from these tables. Entries are")
    out.append("8 bytes: {u16 key; u16 pad; u32 worker}; workers are Thumb addresses.")
    out.append("")
    for name, rows, ca in (("A", tA, TABLES["A"][0]), ("B", tB, TABLES["B"][0])):
        out.append("## Table %s - CA 0x%06X, %d entries" % (name, ca, len(rows)))
        out.append("")
        out.append("| entry | key | pad | worker CA | annotation |")
        out.append("| ---: | ---: | ---: | ---: | --- |")
        for i, (k, pad, w) in enumerate(rows):
            ann = DATA_PLANE_KEYS.get(k) or VENDOR_KEYS.get(k) or ""
            out.append("| %d | 0x%04X | 0x%04X | 0x%06X | %s |" % (i, k, pad, w, ann))
        out.append("")
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")


def cmd_tables(fw):
    for name, (ca, n) in TABLES.items():
        print("== table %s CA 0x%06X n=%d" % (name, ca, n))
        for i, (k, pad, w) in enumerate(decode_table(fw, ca, n)):
            ann = DATA_PLANE_KEYS.get(k) or VENDOR_KEYS.get(k) or ""
            print("  [%3d] key=0x%04X pad=0x%04X worker=CA 0x%06X %s" % (i, k, pad, w, ann))


def cmd_map(fw):
    tA = decode_table(fw, *TABLES["A"])
    tB = decode_table(fw, *TABLES["B"])
    print("data-plane keys (group-0 id-1 config event, firmware CPU addresses):")
    for i, (k, _p, w) in enumerate(tA):
        if k in DATA_PLANE_KEYS:
            print("  key=0x%04X entry=%3d worker=CA 0x%06X  %s" %
                  (k, i, w, DATA_PLANE_KEYS[k]))
    missing = [k for k in DATA_PLANE_KEYS
               if find_key(tA, k)[0] is None and find_key(tB, k)[0] is None]
    if missing:
        print("MISSING from both tables:", ", ".join("0x%04x" % k for k in missing))
        return 1
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fw", default=DEFAULT_FW)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--tables", action="store_true")
    ap.add_argument("--map", action="store_true")
    ap.add_argument("--emit-md", metavar="PATH")
    args = ap.parse_args()
    if not (args.selftest or args.tables or args.map or args.emit_md):
        ap.print_help()
        return 2
    fw = load(args.fw)
    if args.tables:
        cmd_tables(fw)
    if args.map:
        return cmd_map(fw)
    if args.emit_md:
        emit_table_md(decode_table(fw, *TABLES["A"]), decode_table(fw, *TABLES["B"]), args.emit_md)
        print("wrote", args.emit_md)
    if args.selftest:
        checks = cmd_selftest(fw)
        fails = [w for ok, w in checks if not ok]
        for ok, what in checks:
            if not ok:
                print("FAIL:", what)
        print("SUMMARY: %d passed, %d failed" % (len(checks) - len(fails), len(fails)))
        return 0 if not fails else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())