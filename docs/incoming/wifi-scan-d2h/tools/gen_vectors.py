#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""gen_vectors.py - generate and verify the scan D2H test vectors.

Lane wifi-scan-d2h (task-118).  Every frame here is SYNTHETIC: it is built
byte-by-byte from the derived layout in ../REPORT.md, never copied from a
vendor artifact.  The JSON is committed as testvectors/scan-d2h-vectors.json;
--check rebuilds tools/scan_d2h_dump.c and asserts that the committed C
decoder reproduces every expected field.

    python3 tools/gen_vectors.py --write
    python3 tools/gen_vectors.py --check
"""
import argparse
import json
import os
import struct
import subprocess
import sys
import tempfile

LANE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VEC = os.path.join(LANE, "testvectors", "scan-d2h-vectors.json")
SRC = os.path.join(LANE, "src")
TOOLS = os.path.join(LANE, "tools")


def u16(v):
    return struct.pack("<H", v)


def u64(v):
    return struct.pack("<Q", v)


def ie(eid, body):
    return bytes([eid, len(body)]) + body


def bss_event(subtype, bssid, cap, interval, chan, ht5g, ssid, trailer):
    p = bytearray()
    p += u16((subtype << 4) | 0x0000)          # frame control
    p += u16(0)                                # duration
    p += b"\xff" * 6                           # addr1
    p += bytes(range(0x20, 0x26))              # addr2
    p += bytes(bssid)                          # addr3 = BSSID
    p += u16(0)                                # seq
    p += u64(0x0102030405060708)               # timestamp
    p += u16(interval)
    p += u16(cap)
    p += ie(0, ssid.encode())
    if ht5g:
        ht = bytearray(22)
        ht[0] = chan
        p += ie(61, ht)
    else:
        p += ie(3, bytes([chan]))
    if trailer is not None:
        p += bytes(trailer)
    return bytes(p)


def trailer(rssi_candidate, phy):
    t = bytearray(20)
    t[0x04] = 0x11
    t[0x05] = rssi_candidate & 0xFF
    t[0x06] = (rssi_candidate - 5) & 0xFF
    t[0x08] = 0x34
    t[0x0C] = 0x56
    t[0x10] = 0x78
    t[0x11] = phy
    return bytes(t)


def build_vectors():
    vecs = []
    bssid = bytes([0x02, 0x11, 0x22, 0x33, 0x44, 0x55])
    tr = trailer(-42, 4)
    vecs.append({
        "name": "bss_beacon_2g_trailer",
        "kind": "bss",
        "provenance": "synthetic from REPORT.md section 3.1 layout",
        "payload_hex": bss_event(8, bssid, 0x0411, 100, 6, False,
                                 "bench-ap-2g", tr).hex(),
        "expect": {
            "rc": 0, "layout": 1, "frame_type": 1,
            "bssid": bssid.hex(), "ssid_len": 11,
            "ssid": "bench-ap-2g".encode().hex(),
            "chan": 6, "is_5g": 0, "freq": 2437,
            "interval": 100, "capability": 0x0411,
            "tsf": 0x0102030405060708, "phy": 4,
            "trailer": 1, "signal_mbm": -4200,
            "ie_len": None,  # checked for > 0 only
        },
    })
    b5 = bytes([2, 3, 4, 5, 6, 7])
    vecs.append({
        "name": "bss_presp_5g_ht_trailer",
        "kind": "bss",
        "provenance": "synthetic from REPORT.md section 3.1 layout",
        "payload_hex": bss_event(5, b5, 0x0001, 50, 36, True, "5g",
                                 trailer(-55, 8)).hex(),
        "expect": {
            "rc": 0, "layout": 1, "frame_type": 2,
            "bssid": b5.hex(), "ssid_len": 2,
            "ssid": "5g".encode().hex(),
            "chan": 36, "is_5g": 1, "freq": 5180,
            "interval": 50, "capability": 1,
            "tsf": 0x0102030405060708, "phy": 8,
            "trailer": 1, "signal_mbm": -5500,
            "ie_len": None,
        },
    })
    bn = bytes([0x0a, 0, 0, 0, 0, 0x0b])
    vecs.append({
        "name": "bss_beacon_2g_frame_only",
        "kind": "bss",
        "provenance": "synthetic; layout hypothesis B (no trailer)",
        "payload_hex": bss_event(8, bn, 0x0401, 200, 11, False, "bare",
                                 None).hex(),
        "expect": {
            "rc": 0, "layout": 2, "frame_type": 1,
            "bssid": bn.hex(), "ssid_len": 4,
            "ssid": b"bare".hex(),
            "chan": 11, "is_5g": 0, "freq": 2462,
            "interval": 200, "capability": 0x0401,
            "tsf": 0x0102030405060708, "phy": 0,
            "trailer": 0, "signal_mbm": 0,
            "ie_len": None,
        },
    })
    bh = bytes([2, 2, 2, 2, 2, 2])
    vecs.append({
        "name": "bss_hidden_ssid_trailer",
        "kind": "bss",
        "provenance": "synthetic; hidden SSID is a zero-length SSID IE",
        "payload_hex": bss_event(8, bh, 0x0401, 100, 1, False, "",
                                 trailer(-70, 0)).hex(),
        "expect": {
            "rc": 0, "layout": 1, "frame_type": 1,
            "bssid": bh.hex(), "ssid_len": 0, "ssid": "",
            "chan": 1, "is_5g": 0, "freq": 2412,
            "interval": 100, "capability": 0x0401,
            "tsf": 0x0102030405060708, "phy": 0,
            "trailer": 1, "signal_mbm": -7000,
            "ie_len": None,
        },
    })
    # Negative vector: data frame (type 2) must be refused.
    bad = bytearray(bss_event(8, bh, 0, 100, 6, False, "x", None))
    bad[0] |= 0x08
    vecs.append({
        "name": "reject_data_frame",
        "kind": "bss",
        "provenance": "synthetic negative: 802.11 type field is not mgmt",
        "payload_hex": bytes(bad).hex(),
        "expect": {"rc": -95},
    })
    # Negative vector: truncated IE chain.
    good = bss_event(8, bh, 0, 100, 6, False, "abcdef", None)
    vecs.append({
        "name": "reject_truncated_ie",
        "kind": "bss",
        "provenance": "synthetic negative: last IE cut in half",
        "payload_hex": good[:-2].hex(),
        "expect": {"rc": -95},
    })
    vecs.append({
        "name": "complete_status1",
        "kind": "complete",
        "provenance": "synthetic; g1 id16 status byte at payload[0]",
        "payload_hex": bytes([1, 0x10, 0, 0]).hex(),
        "expect": {"rc": 0, "status": 1},
    })
    chanp = bytearray(0x70)
    chanp[0] = 7
    chanp[4] = 36
    vecs.append({
        "name": "chan_result_slot7_5g",
        "kind": "chan",
        "provenance": "synthetic; g1 id17 payload from firmware sender CA 0x641B4",
        "payload_hex": bytes(chanp).hex(),
        "expect": {"rc": 0, "slot": 7, "chan": 36, "is_5g": 1,
                   "freq": 5180, "block_len": 108},
    })
    stab = bytearray(16)
    stab[0] = 0x12
    stab[0x0D] = 1
    vecs.append({
        "name": "sta_scan_complete_status1",
        "kind": "sta",
        "provenance": "synthetic; g2 id2 status byte at payload[0xD] [I]",
        "payload_hex": bytes(stab).hex(),
        "expect": {"rc": 0, "status": 1},
    })
    return {"lane": "wifi-scan-d2h", "synthetic": True, "vectors": vecs}


def compile_dump(tmpdir):
    exe = os.path.join(tmpdir, "scan_d2h_dump")
    cmd = ["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic",
           "-I" + SRC, os.path.join(TOOLS, "scan_d2h_dump.c"),
           os.path.join(SRC, "hi5622_scan_d2h.c"), "-o", exe]
    subprocess.run(cmd, check=True)
    return exe


def check(exe):
    with open(VEC) as fh:
        doc = json.load(fh)
    checks = 0
    fails = 0
    for v in doc["vectors"]:
        out = subprocess.run([exe, v["kind"], v["payload_hex"]],
                             capture_output=True, text=True)
        if out.returncode != 0:
            print("FAIL %s: dump rc %d" % (v["name"], out.returncode))
            fails += 1
            continue
        got = {}
        for line in out.stdout.splitlines():
            if "=" in line:
                k, val = line.split("=", 1)
                got[k] = val
        for key, want in v["expect"].items():
            if key in ("bssid", "ssid"):
                continue
            checks += 1
            if key == "ie_len":
                if int(got.get(key, "0")) <= 0:
                    print("FAIL %s: ie_len" % v["name"])
                    fails += 1
                continue
            have = got.get(key)
            if have is None or int(have) != int(want):
                print("FAIL %s: %s got %s want %s" %
                      (v["name"], key, have, want))
                fails += 1
        # also require the decoded string fields to match exactly
        for key in ("bssid", "ssid"):
            if key in v["expect"]:
                checks += 1
                if got.get(key) != v["expect"][key]:
                    print("FAIL %s: %s got %s want %s" %
                          (v["name"], key, got.get(key), v["expect"][key]))
                    fails += 1
        checks += 1
        if int(got.get("rc", 999)) != int(v["expect"]["rc"]):
            print("FAIL %s: rc got %s want %s" %
                  (v["name"], got.get("rc"), v["expect"]["rc"]))
            fails += 1
    print("gen_vectors --check: %d/%d checks %s" %
          (checks - fails, checks, "FAIL" if fails else "PASS"))
    return 0 if not fails else 1


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args(argv)
    doc = build_vectors()
    if args.write:
        os.makedirs(os.path.dirname(VEC), exist_ok=True)
        with open(VEC, "w") as fh:
            json.dump(doc, fh, indent=2, sort_keys=False)
            fh.write("\n")
        print("wrote", VEC, "(%d vectors)" % len(doc["vectors"]))
        return 0
    if args.check:
        if not os.path.isfile(VEC):
            print("missing", VEC, "- run --write first", file=sys.stderr)
            return 1
        with tempfile.TemporaryDirectory() as td:
            return check(compile_dump(td))
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
