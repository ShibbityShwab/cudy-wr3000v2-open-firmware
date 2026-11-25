#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
desc_table_send_test.py - host-only cross-check for lane wifi-desc-table-send
(shared task task-188).

HOST-ONLY: reads the lane's own sources/patch and a hex frame emitted by the
portable selftest; no device, no kernel, no vendor artifact.

Checks
  1. FRAME: re-derives the 68-byte 0x00F7 frame from the committed spec
     offsets (../wifi-fw-desc-table/TABLES.md T2/T3) and requires it to be
     byte-identical to what src/hi5622_desc_table.c builds.
  2. ORDER: in the patched driver tree, the 'desc_table' post must sit after
     the VAP base (0x0064) and before the ap_chan (0x0124) post, inside the
     beacon_static branch; the fail-closed parameter gate must run before any
     frame; the plan must carry the desc_table step between vap_add and
     ap_chan (hi5622_scan_logic.c).
  3. CONTRACT: the parameter/dmesg/sysfs strings and the Kbuild entry exist.
Exit 0 = PASS.
"""
import argparse
import struct
import sys

FRAME_LEN = 68


def u16(v):
    return struct.pack("<H", v)


def u32(v):
    return struct.pack("<I", v)


def record(word, f4, f6, bitmap):
    return u32(word) + bytes([f4, 0]) + u16(f6) + b"\x00" * 4 + u32(bitmap)


def derive_frame():
    """The lane spec, field by field (wifi-fw-desc-table TABLES T2/T3)."""
    blob = b"US" + b"\x00\x00" + bytes([2, 0, 0, 0])
    blob += record(0x00800000, 30, 300, 0x00001FFF)  # 2.4G armed
    blob += record(0x00800201, 30, 300, 0x000000FF)  # 5G armed ch36-64
    assert len(blob) == 8 + 2 * 16, "blob is 8+16N"
    payload = (b"\x00\x00\x00\x00\x01\x00\x00\x00"
               + u16(0x00F7) + u16(len(blob)) + blob + b"\xff" * 4)
    total = 12 + len(payload)
    frame = (bytes([0x00, 0x01, 0x00, 0x04]) + u16(total) + u16(0x0001)
             + bytes([0x00, 0x00, 0x5A, 0x5A]) + payload)
    assert len(frame) == FRAME_LEN and total == 0x44
    return frame


class Checks:
    def __init__(self):
        self.n = 0
        self.fail = 0

    def check(self, cond, what):
        self.n += 1
        if not cond:
            self.fail += 1
            print("FAIL " + what)

    def eq(self, got, want, what):
        self.n += 1
        if got != want:
            self.fail += 1
            print("FAIL %s: got %r want %r" % (what, got, want))


def check_frame(ck, frame_hex):
    want = derive_frame()
    try:
        got = bytes.fromhex(frame_hex.strip())
    except ValueError:
        ck.check(False, "C selftest frame is valid hex")
        return
    ck.eq(len(got), FRAME_LEN, "C selftest frame is 68 bytes")
    ck.check(got == want, "C frame byte-identical to the spec re-derivation")
    if got:
        ck.eq(got[0:12].hex(), "000100044400010000005a5a", "header")
        ck.eq(got[12:20].hex(), "0000000001000000", "token/preamble")
        ck.eq(got[20:22].hex(), "f700", "key 0x00F7 at hdr+20")
        ck.eq(got[22:24].hex(), "2800", "len 0x28 at hdr+22")
        ck.eq(got[24:32].hex(), "5553000002000000", "blob head cc US/count 2")
        ck.eq(got[32:48].hex(),
              "000080001e002c0100000000ff1f0000", "record 0 (2.4G armed)")
        ck.eq(got[48:64].hex(),
              "010280001e002c0100000000ff000000", "record 1 (5G armed)")
        ck.eq(got[64:68].hex(), "ffffffff", "inherited tail")
        word1 = ((len(got) << 16) | 0x6D2B) & 0xFFFFFFFF
        ck.eq(word1, 0x00446D2B, "SR node word1 0x00446D2B")
        ck.eq(struct.unpack("<I", got[48:52])[0] & 0x00800000,
              0x00800000, "5G record armed bit (byte2 bit7)")


def check_order(ck, tree):
    import os
    src_path = os.path.join(tree, "hi5622_scan.c")
    logic_path = os.path.join(tree, "hi5622_scan_logic.c")
    kbuild_path = os.path.join(tree, "Kbuild")
    src = open(src_path, encoding="utf-8", errors="replace").read()
    logic = open(logic_path, encoding="utf-8", errors="replace").read()
    kbuild = open(kbuild_path, encoding="utf-8", errors="replace").read()

    start = src.find("int hi5622_scan_op_start_ap(")
    ck.check(start >= 0, "start_ap function present")
    body = src[start:] if start >= 0 else ""

    gate = body.find("is not a valid descriptor-table parameter")
    post = body.find('"desc_table", &dt_out0')
    ap_chan = body.find('"ap_chan", NULL')
    vap_add = body.find("hi5622_scan_post_vap_add_frame")
    readback = body.find("VAP-object readback=")
    ap_cfg = body.find("memset(&cp, 0, sizeof(cp));")

    ck.check(gate >= 0, "fail-closed desc_table parameter gate present")
    ck.check("desc_table=1 needs the beacon_static=1 vendor order" in body,
             "fail-closed gate refuses desc_table=1 without beacon_static")
    ck.check(post >= 0, "desc_table frame_post call present")
    ck.check(ap_chan >= 0, "ap_chan frame_post call present")
    ck.check(gate >= 0 and vap_add >= 0 and gate < vap_add,
             "parameter gate runs before the VAP add (no frame on a bad value)")
    ck.check(post >= 0 and ap_chan >= 0 and post < ap_chan,
             "ORDER: desc_table (0xF7) posted BEFORE ap_chan (0x0124)")
    ck.check(post >= 0 and readback >= 0 and post > readback,
             "ORDER: desc_table posted AFTER the VAP-base readback (0x0064)")
    ck.check(post >= 0 and ap_cfg >= 0 and post < ap_cfg,
             "ORDER: desc_table posted before the AP config sequence")
    ck.check("if (dtp.post)" in body,
             "desc_table post is gated by the fail-closed plan")
    ck.check("hi5622_desc_table_build(" in body,
             "start_ap uses the portable 68-byte builder")
    ck.check("hi5622_desc_table_status_line(" in src,
             "sysfs status line wired")
    for s in ('"desc_table"', "0x00F7", "armed"):
        ck.check(s in body or s in src, "dmesg/status mentions " + s)

    ck.check("hi5622_desc_table.o" in kbuild, "Kbuild lists hi5622_desc_table.o")
    ck.check("HI5622_BEACON_STEP_DESC_TABLE" in logic,
             "scan_logic implements the desc_table step")
    ck.check("desc_table == 1u" in logic,
             "scan_logic enables the step only for desc_table=1")
    ck.check(logic.find("HI5622_BEACON_STEP_DESC_TABLE") <
             logic.find("HI5622_BEACON_STEP_CHAN"),
             "plan inserts the step before the channel step")
    ck.check('case HI5622_BEACON_STEP_DESC_TABLE:	return "desc_table";'
             in logic, "step name desc_table")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frame-hex", help="hex frame emitted by desc_table_selftest")
    ap.add_argument("--tree", help="patched driver tree (order/contract checks)")
    a = ap.parse_args()
    ck = Checks()
    if a.frame_hex:
        check_frame(ck, a.frame_hex)
    else:
        print("SKIP frame check (no --frame-hex)")
    if a.tree:
        check_order(ck, a.tree)
    else:
        print("SKIP order/contract check (no --tree)")
    print("== desc_table_send_test: %d checks, %d failures" % (ck.n, ck.fail))
    if ck.fail:
        print("FAIL")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
