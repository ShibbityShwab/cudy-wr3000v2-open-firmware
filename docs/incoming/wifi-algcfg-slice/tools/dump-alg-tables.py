#!/usr/bin/env python3
# Regenerate the alg_cfg table CSVs from the local e_machine-patched vendor
# hi5622v100_wifi.ko (NEVER committed; see wifi-datapath-slice REPORT for the
# e_machine patch). Emits mapping/alg-process-table.csv and
# mapping/alg-param-size-table.csv.
#
# usage: python3 tools/dump-alg-tables.py <path-to-patched-wifi.ko> [outdir]
import re, struct, subprocess, sys, csv, os

KO = sys.argv[1] if len(sys.argv) > 1 else "/home/shibbityshwab/wifi-datapath-slice-work/wifi-patched.ko"
OUT = sys.argv[2] if len(sys.argv) > 2 else "mapping"

def run(cmd):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True).stdout

secs = {}
for line in run("readelf -SW " + KO).splitlines():
    m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9a-f]+)\s+([0-9a-f]+)", line)
    if m:
        secs[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16))

data = open(KO, "rb").read()
df = secs[".data"][1]
os.makedirs(OUT, exist_ok=True)

# process-info table: .data+0x35c8, 0x1368 bytes, 12 B/entry
with open(os.path.join(OUT, "alg-process-table.csv"), "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["idx", "cfg_id", "cfg_type", "need_h_process", "need_w4_h",
                "need_d_process", "need_w4_d", "raw12", "name_evidence"])
    for i in range(0x1368 // 12):
        off = 0x35c8 + i * 12
        raw = data[df + off:df + off + 12]
        cid = struct.unpack_from("<H", raw, 4)[0]
        w.writerow([i, cid, raw[6], raw[8], raw[9], raw[10], raw[11], raw.hex(), ""])

# param-size table: .data+0x2d9c, 40 x 4 B
with open(os.path.join(OUT, "alg-param-size-table.csv"), "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["idx", "param_type", "tbl_size"])
    for i in range(40):
        off = 0x2d9c + i * 4
        raw = data[df + off:df + off + 4]
        w.writerow([i, raw[0], raw[1]])

print("wrote", os.path.join(OUT, "alg-process-table.csv"), "and",
      os.path.join(OUT, "alg-param-size-table.csv"))
