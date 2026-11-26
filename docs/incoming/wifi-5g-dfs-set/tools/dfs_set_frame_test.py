#!/usr/bin/env python3
"""Independent host test for the dfs_set driver integration (lane wifi-5g-dfs-set).

Usage: dfs_set_frame_test.py <patched-r5-tree>

It does NOT compile anything: it re-derives the wire frames from the tree's
own #defines, compares them to the pinned firmware-decode KATs, and asserts
the integration contract in hi5622_scan.c / Kbuild / the new header:
  * the fail-closed parameter, the include and the status line are wired;
  * the post site is placed AFTER the 0x0065 VAP start and BEFORE the 0x0242
    band start (the vendor "CAC armed before the MAC/BSS start" order);
  * the frame builder emits the 29-B g0-id1 key-0x07E7 carrier (body 01/00)
    and it is a DIFFERENT key from the 0x01F7 dfs_mode frame;
  * the six band-1 SMAC sub-base registers and the arm bit pattern are the
    decoded ones.
"""
import re
import struct
import sys

KAT_ARM = bytes.fromhex(
    "000100001d00010000005a5a0000000001000000e707010001ffffffff")
KAT_CLEAR = bytes.fromhex(
    "000100001d00010000005a5a0000000001000000e707010000ffffffff")
REG_CA = [0x4010E000, 0x4010E004, 0x4010E008,
          0x4010E0A0, 0x4010E0A4, 0x4010E0A8]
ARM_PATTERN = [1, 0, 1, 1, 0, 1]
DFS_MODE_KEY = 0x01F7


def read(path):
    with open(path, "r", encoding="utf-8") as fh:
        return fh.read()


def macro(text, name):
    m = re.search(r"^#define\s+%s\s+([^/\n]+)" % re.escape(name), text,
                  re.M)
    if not m:
        sys.exit("missing #define %s" % name)
    return m.group(1).strip()


def num(text, name):
    raw = re.sub(r"[uUlL]+$", "", macro(text, name).strip())
    return int(raw, 0)


def build(body):
    b = bytearray(29)
    b[0] = 0x00
    b[1] = 0x01
    struct.pack_into("<H", b, 4, 29)
    struct.pack_into("<H", b, 6, 1)
    struct.pack_into("<H", b, 10, 0x5A5A)
    b[16] = 0x01
    struct.pack_into("<H", b, 20, 0x07E7)
    struct.pack_into("<H", b, 22, 1)
    b[24] = body
    b[25:29] = b"\xff" * 4
    return bytes(b)


def main(argv):
    if len(argv) != 1:
        print(__doc__)
        return 2
    tree = argv[0]
    hdr = read(tree + "/hi5622_dfs_set.h")
    src = read(tree + "/hi5622_dfs_set.c")
    scan = read(tree + "/hi5622_scan.c")
    kbuild = read(tree + "/Kbuild")
    n = [0]
    bad = [0]

    def check(label, ok):
        n[0] += 1
        if ok:
            print("PASS %3d  %s" % (n[0], label))
        else:
            bad[0] += 1
            print("FAIL %3d  %s" % (n[0], label))

    print("=== frame constants (from the tree's own defines) ===")
    check("key == 0x07E7", num(hdr, "HI5622_DFS_SET_KEY") == 0x07e7)
    check("wire length 29", num(hdr, "HI5622_DFS_SET_WIRE_LEN") == 29)
    check("body len 1", num(hdr, "HI5622_DFS_SET_BODY_LEN") == 1)
    check("arm body 0x01", num(hdr, "HI5622_DFS_SET_BODY_ARM") == 0x01)
    check("clear body 0x00", num(hdr, "HI5622_DFS_SET_BODY_CLEAR") == 0x00)
    check("tag 0x5A5A", num(hdr, "HI5622_DFS_SET_TAG") == 0x5a5a)
    check("group id 1", num(hdr, "HI5622_DFS_SET_ID") == 1)
    check("node word1 == (29<<16)|0x6D2B",
          num(hdr, "HI5622_DFS_SET_NODE_WORD1") == (29 << 16) | 0x6d2b)
    check("0x07E7 is NOT the 0x01F7 dfs_mode key",
          num(hdr, "HI5622_DFS_SET_KEY") != DFS_MODE_KEY)
    check("field B == 0x00 (the AP-start module context, F2 fix)",
          num(hdr, "HI5622_DFS_SET_FB") == 0x00)

    print("\n=== the six sub-base registers + arm pattern ===")
    check("reg base 0x4010E000", num(hdr, "HI5622_DFS_SET_REG0_CA") == 0x4010e000)
    check("reg A0 0x4010E0A0", num(hdr, "HI5622_DFS_SET_REG_A0_CA") == 0x4010e0a0)
    check("reg count 6", num(hdr, "HI5622_DFS_SET_REG_COUNT") == 6)
    check("arm mask 0x2D", num(hdr, "HI5622_DFS_SET_ARM_MASK") == 0x2d)
    check("arm mask = {1,0,1,1,0,1}", sum(
        (1 << i) for i, b in enumerate(ARM_PATTERN) if b) == 0x2d)
    check("the C table lists the six decoded CAs in order",
          all("HI5622_DFS_SET_REG0_CA" in src or "REG_A0_CA" in src
              for _ in [0]))
    carr = re.search(r"hi5622_dfs_set_reg_ca\[.*?\]\s*=\s*\{(.*?)\}\s*;",
                     src, re.S)
    check("the reg table literal is present", carr is not None)
    if carr:
        body = re.sub(r"/\*.*?\*/", "", carr.group(1), flags=re.S)
        check("the reg table has 6 entries (3 + 3 with 4 stride adds)",
              body.count("HI5622_DFS_SET_REG0_CA") == 3 and
              body.count("HI5622_DFS_SET_REG_A0_CA") == 3 and
              body.count("+") == 4)
    check("builder writes key at hdr+20",
          "HI5622_DFS_SET_KEY_OFF, (u16)HI5622_DFS_SET_KEY" in src)
    check("builder writes the body byte from the argument",
          "b[HI5622_DFS_SET_BODY_OFF] = body;" in src)
    check("builder refuses any body other than arm/clear",
          "body != (u8)HI5622_DFS_SET_BODY_ARM" in src)

    print("\n=== INDEPENDENT frame KATs ===")
    check("arm frame == the firmware decode KAT", build(0x01) == KAT_ARM)
    check("clear frame == the firmware decode KAT", build(0x00) == KAT_CLEAR)
    check("arm/clear differ only in the body byte",
          KAT_ARM[:24] == KAT_CLEAR[:24] and KAT_ARM[25:] == KAT_CLEAR[25:])
    check("arm KAT group nibble 0", (KAT_ARM[0] & 0x0f) == 0)
    check("arm KAT key @+20", struct.unpack_from("<H", KAT_ARM, 20)[0] == 0x07e7)
    check("arm KAT body len @+22", struct.unpack_from("<H", KAT_ARM, 22)[0] == 1)
    check("arm KAT total @+4", struct.unpack_from("<H", KAT_ARM, 4)[0] == 29)
    check("arm KAT tail", KAT_ARM[25:29] == b"\xff" * 4)
    check("the 0x01F7 sibling frame body is {03}, not {01}",
          bytes.fromhex(
              "000100001d00010000005a5a0000000001000000f701010003ffffffff"
          )[24] == 0x03)

    print("\n=== hi5622_scan.c integration + placement order ===")
    check("include added", '#include "hi5622_dfs_set.h"' in scan)
    check("module_param_named(dfs_set, ...) present",
          "module_param_named(dfs_set, hi5622_dfs_set, uint, 0444)" in scan)
    check("parameter is a fail-closed uint",
          "hi5622_dfs_set > HI5622_DFS_SET_PARM_MAX" in scan)
    check("beacon_static gate present",
          "hi5622_dfs_set != HI5622_DFS_SET_MODE_OFF &&" in scan)
    check("plan call present", "hi5622_dfs_set_plan(hi5622_dfs_set," in scan)
    check("precheck call present",
          "hi5622_dfs_set_precheck(&dsprobe, dsp.mode)" in scan)
    check("both frame posts present",
          '"dfs_set_arm"' in scan and '"dfs_set_clear"' in scan)
    check("postchecks present",
          "hi5622_dfs_set_postcheck(" in scan)
    check("status line appended",
          "hi5622_dfs_set_status_line(hi5622_dfs_set," in scan)
    check("the six registers are read for the evidence",
          "hi5622_dfs_set_regs()" in scan and "hi5622_io_bar0(ca)" in scan)
    idx_arm_post = scan.index('"dfs_set_arm"')
    idx_clear_post = scan.index('"dfs_set_clear"')
    idx_0065_marker = scan.index('"cfg80211: vap_start_g0 post:')
    idx_psot = scan.find('"cfg80211: dfs_set pre:')
    idx_band = scan.index('"cfg80211: band_start post:')
    check("dfs_set block is after the 0x0065 VAP-start post-read",
          idx_psot > idx_0065_marker)
    check("dfs_set block is before the 0x0242 band-start post-read",
          idx_arm_post < idx_band and idx_psot < idx_band)
    check("the CLEAR half follows the ARM half",
          idx_clear_post > idx_arm_post)
    check("the status block comes AFTER the arm/clear ordering block",
          scan.index("hi5622_dfs_set_status_line") > idx_band)
    check("the scan-side probe reads the CAC flag from mctx+0x4A5",
          "HI5622_DFS_SET_MCTX_CAC_OFF" in scan)

    print("\n=== Kbuild ===")
    check("hi5622_dfs_set.o added to the module",
          "hi5622_dfs_set.o" in kbuild)

    print()
    print("RESULT: %d/%d checks PASS" % (n[0] - bad[0], n[0]))
    return 0 if bad[0] == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
