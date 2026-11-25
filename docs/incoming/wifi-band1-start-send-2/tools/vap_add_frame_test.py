#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""vap_add_frame_test.py - independent cross-check for lane
wifi-band1-start-send-2 (task-207).

HOST-ONLY.  Two jobs:

1. Re-derive the g0-id1 key-0x0064 VAP-add wire frame for the plan fields
   (index -> body data[4] + hdr[3], parent -> body data[1], band -> body
   data[0x12]) from the decoded carrier geometry ONLY, and compare it with
   the KATs the C selftest asserts.
2. Source contract on the patched tree (default /tmp/vapbound-build):
   the provider sets data[4]/data[1]/hdr[3] from the explicit VAP index, the
   cfg80211 layer refuses an out-of-table index or an index !=
   vap_start_module before any frame, the post-add readback exists, the
   0x0064 post precedes the 0x0242 band-start plan in start_ap, and the
   0x0242 frame's field B is the same module.

Usage: vap_add_frame_test.py [TREE]   (exit 0 = pass)
"""
import os
import re
import sys

TAG = 0x5A5A
HERE = os.path.dirname(os.path.abspath(__file__))
LANE = os.path.dirname(HERE)
DOCS = os.path.dirname(LANE)
DEFAULT_TREE = "/tmp/vapbound-build"

checks = 0
fails = 0


def check(cond, msg):
    global checks, fails
    checks += 1
    if not cond:
        fails += 1
        print("FAIL: " + msg)


def derive_vap_add(index, parent, band, field_b, create_type=2):
    """g0 id1 config-event carrier: 12-B hdr | 8-B token | key u16 | len u16
    | body | 4 x 0xFF tail.  Header [0]=group 0, [1]=alloc|dst, [2]=fa,
    [3]=field B, [4..5]=total, [6..7]=id 1, [8]=fc, [9]=dst, [10..11]=0x5a5a.
    Body (20 B): data[0]=create type, data[1]=parent, data[4]=index,
    data[0x12]=band."""
    body = bytearray(20)
    body[0] = create_type
    body[1] = parent
    body[4] = index
    body[0x12] = band
    total = 12 + 8 + 2 + 2 + len(body) + 4
    f = bytearray()
    f += bytes([0x00, 0x01, 0x00, field_b])
    f += total.to_bytes(2, "little")
    f += (1).to_bytes(2, "little")
    f += bytes([0x00, 0x00, 0x5A, 0x5A])
    f += bytes([0, 0, 0, 0, 1, 0, 0, 0])       # token (inherited)
    f += (0x0064).to_bytes(2, "little")
    f += len(body).to_bytes(2, "little")
    f += bytes(body)
    f += b"\xff" * 4
    return bytes(f)


def rederive(kat_c):
    src = open(os.path.join(LANE, "src", "vap_index_selftest.c")).read()
    for name, index in (("want0", 0), ("want4", 4)):
        m = re.search(name + r'\s*=\s*((?:\s*"[0-9a-f]+")+);', src)
        check(m is not None, "KAT " + name + " present in the C selftest")
        if not m:
            continue
        want = "".join(re.findall(r'"([0-9a-f]+)"', m.group(1)))
        got = derive_vap_add(index, index, 1, index).hex()
        check(got == want, "independent derivation == C KAT " + name)
        check(want[20 * 2:20 * 2 + 4] == "%02x00" % 0x64, name + " carries key 0x0064")
        check(want[48:48 + 2] == "02", name + " data[0] create type AP")
        check(want[48 + 2:48 + 4] == "%02x" % index, name + " data[1] parent")
        check(want[48 + 8:48 + 10] == "%02x" % index,
              name + " data[4] VAP index SET")


def read(path):
    with open(path, "r", errors="replace") as fh:
        return fh.read()


def contract(tree):
    prov = read(os.path.join(tree, "hi5622_vapscan_provider.c"))
    scan = read(os.path.join(tree, "hi5622_scan.c"))
    bs = read(os.path.join(tree, "hi5622_band_start.c"))
    check("gp.module = (u8)hi5622_vap_add_index;" in prov,
          "provider: body data[4] = the explicit VAP index")
    check("gp.field_b = (u8)hi5622_vap_add_index;" in prov,
          "provider: hdr[3] field B = the same VAP index")
    check("gp.parent = (u8)hi5622_vap_add_index;" in prov,
          "provider: parent = the same VAP index (self-parent)")
    check("p->module != (u8)hi5622_vap_add_index" in prov,
          "provider: fail-closed module/index agreement")
    check("hi5622_vap_add_index > HI5622_BAND_START_MODULE_MAX" in scan,
          "cfg80211 gate: index outside the module table refused")
    check("hi5622_vap_add_index != hi5622_vap_start_module" in scan,
          "cfg80211 gate: bind ctx != start ctx refused")
    check('"cfg80211: vap_add post: index=%u band=%u' in scan,
          "read-only post-add readback line")
    check("bandlist_count=%u" in scan, "readback reports the band VAP list")
    # order: the structural vap_add post before the 0x0242 plan in start_ap
    i_add = scan.find("hi5622_scan_post_vap_add_frame(")
    i_plan = scan.find("hi5622_band_start_plan")
    check(i_add != -1 and i_plan != -1 and i_add < i_plan,
          "order: 0x0064 post < 0x0242 band-start plan in start_ap")
    # 0x0242 field B is the plan module (the same index the gate pins)
    check(re.search(r'\[3\]\s*=\s*module', bs) is not None,
          "0x0242 builder writes field B from the plan module")
    check("hi5622_vap_add_index" in scan, "scan.c uses the explicit VAP index")


def main():
    rederive(None)
    tree = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_TREE
    if os.path.isdir(tree):
        contract(tree)
    else:
        check(False, "patched tree %s absent (build first)" % tree)
    print("vap_add_frame_test: %d checks, %d failures" % (checks, fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
