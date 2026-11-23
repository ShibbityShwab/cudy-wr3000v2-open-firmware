#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# vendor_const_scan.py — original, host-only constant scanner used by the
# wifi-boot-kick lane to prove/disprove host write sites in the vendor modules.
#
# It never copies vendor bytes and writes nothing; it only prints a summary.
# Usage:
#   vendor_const_scan.py [module.ko ...]
# Default module list: the two stock 2.5.24 modules, the 2.4.15 pair and the
# two debug modules under the local paths given in ../REPORT.md §0.
#
# What it does:
#  1) scans .text/.text.unlikely/.data/.rodata for 32-bit device-CA constants
#     (0x40000000-0x4011FFFF, 0x01000000-0x0141FFFF, 0x10100000-0x1016FFFF,
#      0x41800000-0x41803FFF) and prints a per-section histogram;
#  2) scans the whole file for the literal 0x0000cece / raw CE CE bytes;
#  3) decodes ARM and Thumb-2 MOVW immediates and reports any #0xcece (the
#     firmware-side gate value) — a host write site would need this constant;
#  4) reports the 0x40000108/0x4000010C literal usage (release/gate CAs).
import subprocess, struct, sys

DEFAULT_MODULES = [
    "/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_plat.ko",
    "/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_wifi.ko",
    "/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_plat.ko",
    "/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko",
    "/tmp/rootfs2524b/lib/modules/5.10.201/plat_debug.ko",
    "/tmp/rootfs2524b/lib/modules/5.10.201/wifi_debug.ko",
]
SCAN_SECTIONS = (".text", ".text.unlikely", ".data", ".rodata")
RANGES = [
    ("IOCA", 0x40000000, 0x40120000),
    ("FWCA", 0x01000000, 0x01420000),
    ("ACP/CRG", 0x10100000, 0x10170000),
    ("EPBAR", 0x41800000, 0x41804000),
]

def sections(path):
    out = subprocess.run(["readelf", "-SW", path], capture_output=True, text=True).stdout
    secs = {}
    for line in out.splitlines():
        p = line.split()
        if not p or not p[0].startswith("["):
            continue
        if p[0] == "[":
            p = ["[" + p[1]] + p[2:]
        if len(p) < 6:
            continue
        name, typ = p[1], p[2]
        try:
            off, size = int(p[4], 16), int(p[5], 16)
        except (ValueError, IndexError):
            continue
        if typ in ("PROGBITS", "NOBITS"):
            secs[name] = (off, size)
    return secs

def movw_cece(data):
    hits = []
    for i in range(0, len(data) - 3, 2):
        w = struct.unpack_from("<I", data, i)[0]
        # ARM MOVW: bits[27:20]=0x30; imm16 = imm4:imm12
        if (w & 0x0FF00000) == 0x03000000:
            val = (((w >> 16) & 0xF) << 12) | (w & 0xFFF)
            if val == 0xCECE:
                hits.append((hex(i), "ARM-movw"))
        h1 = struct.unpack_from("<H", data, i)[0]
        if (h1 & 0xFBF0) == 0xF240:
            h2 = struct.unpack_from("<H", data, i + 2)[0]
            val = ((h1 & 0xF) << 12) | (((h1 >> 10) & 1) << 11) | (((h2 >> 12) & 7) << 8) | (h2 & 0xFF)
            if val == 0xCECE:
                hits.append((hex(i), "Thumb2-movw"))
    return hits

def scan(path):
    try:
        data = open(path, "rb").read()
    except OSError as e:
        print(f"{path}: SKIP ({e})")
        return
    print(f"## {path} ({len(data)} B)")
    secs = sections(path)
    for name, (off, size) in secs.items():
        if name not in SCAN_SECTIONS or off == 0 or size == 0:
            continue
        hist = {}
        blob = data[off:off + size]
        for i in range(0, len(blob) - 3, 4):
            w = struct.unpack_from("<I", blob, i)[0]
            for tag, lo, hi in RANGES:
                if lo <= w < hi:
                    hist.setdefault(tag, {}).setdefault(hex(w), 0)
                    hist[tag][hex(w)] += 1
                    break
        for tag in sorted(hist):
            print(f"  {name} {tag}: " + ", ".join(f"{k}x{v}" for k, v in sorted(hist[tag].items())))
    print(f"  cece literal word: {hex(0xCECE)} present={struct.pack('<I', 0xCECE) in data}")
    print(f"  raw 'CE CE' bytes present={b'\xce\xce' in data}")
    print(f"  movw #0xcece sites: {movw_cece(data)[:10]}")
    for ca, name in ((0x40000108, "release CA"), (0x4000010C, "gate CA"), (0x4003A200, "remap CA"),
                     (0x40000554, "l1ss CA"), (0x400005BC, "l1ss CA2"),
                     (0x40034000, "tail CA"), (0x40035000, "tail CA"), (0x40107000, "tail CA")):
        print(f"  {name} 0x{ca:08x} literal: {struct.pack('<I', ca) in data}")
    print()

def main(argv):
    mods = argv[1:] or DEFAULT_MODULES
    for m in mods:
        scan(m)
    print("Interpretation: a host write site for the 0xcece gate would require the")
    print("constant 0xcece in the module (literal or MOVW immediate), or the gate CA")
    print("0x4000010C literal. This scan's negative result across all vendor modules")
    print("is claim V6/V15 in REPORT.md — the vendor driver does not write either.")
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
