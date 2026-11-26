#!/usr/bin/env python3
"""Independent host test for the dfs_set ORDER fix (lane wifi-5g-dfs-order).

Usage: dfs_order_test.py <patched-r5-tree>

The tree is the byte-exact R5 base + ../wifi-5g-dfs-set/patches/0001-dfs-set.patch
+ this lane's patches/0001-dfs-order.patch.  The test does NOT compile anything:
it re-derives the 0x07E7 wire frames from the tree's own #defines, checks the
sibling dfs_set contract is intact, and asserts the NEW ordering contract:

  vap_add -> desc_table(0x00F7) -> ap_chan(0x0124) -> ap_ssid -> ap_cfg(0x0127)
    -> vap_start_g0(0x0065) -> dfs_set(0x07E7) -> dfs_mode(0x01F7)
    -> band_start(0x0242) -> ap_vap_start

i.e. the 0x07E7 ARM is posted AFTER the 0x0065 (its started-module gate) and
BEFORE the 0x01F7 (the bench-proven CAC-starting step), and the plan function
(hi5622_scan_logic.c) reports the same order.  The old placement (0x01F7 before
the 0x0124 channel post) must be gone from the post site and the dmesg text.
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
    logic = read(tree + "/hi5622_scan_logic.c")
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

    def pos(text, needle, what):
        i = text.find(needle)
        if i < 0:
            sys.exit("missing marker %s: %r" % (what, needle[:60]))
        return i

    print("=== the sibling dfs_set contract is intact ===")
    check("key == 0x07E7", num(hdr, "HI5622_DFS_SET_KEY") == 0x07e7)
    check("wire length 29", num(hdr, "HI5622_DFS_SET_WIRE_LEN") == 29)
    check("arm body 0x01", num(hdr, "HI5622_DFS_SET_BODY_ARM") == 0x01)
    check("clear body 0x00", num(hdr, "HI5622_DFS_SET_BODY_CLEAR") == 0x00)
    check("arm mask 0x2d", num(hdr, "HI5622_DFS_SET_ARM_MASK") == 0x2d)
    check("0x07E7 is NOT the 0x01F7 key",
          num(hdr, "HI5622_DFS_SET_KEY") != DFS_MODE_KEY)
    check("arm frame == firmware-decode KAT", build(0x01) == KAT_ARM)
    check("clear frame == firmware-decode KAT", build(0x00) == KAT_CLEAR)
    check("builder still in hi5622_dfs_set.c",
          "hi5622_dfs_set_build" in src)
    check("Kbuild still builds hi5622_dfs_set.o",
          "hi5622_dfs_set.o" in kbuild)
    check("fail-closed parameter still wired",
          "module_param_named(dfs_set, hi5622_dfs_set, uint, 0444)" in scan)
    check("precheck still live",
          "hi5622_dfs_set_precheck(&dsprobe, dsp.mode)" in scan)
    check("postcheck still live", "hi5622_dfs_set_postcheck(" in scan)

    print("\n=== hi5622_scan.c: the NEW execution order ===")
    # block-comment / post-site markers in the start_ap ladder
    i_apchan = pos(scan, "\t\tmemset(&cp, 0, sizeof(cp));", "ap_chan block")
    i_dsset = pos(scan, "lane wifi-5g-dfs-set (task-227): the H2D g0-id1 key 0x07E7",
                  "dfs_set block")
    i_dsmode = pos(scan, "MOVED BY THE task-228 ORDER FIX", "dfs_mode block")
    i_band = pos(scan, "\t\t/* lane wifi-band1-start-send (task-206): the H2D g0-id1 key\n",
                 "band_start block")
    i_0056 = pos(scan, "cfg80211: vap_start_g0 post:", "vap_start_g0 post line")
    i_pre = pos(scan, "cfg80211: dfs_set pre:", "dfs_set pre line")
    i_arm = pos(scan, "cfg80211: start_ap beacon_static frame 'dfs_set_arm' posted",
                "dfs_set_arm posted line")
    i_armpost = pos(scan, "cfg80211: dfs_set arm post:", "dfs_set arm post line")
    i_dmpost = pos(scan, "cfg80211: start_ap beacon_static frame %u/%u 'dfs_mode' posted",
                   "dfs_mode posted line")
    check("0x0124 channel post is before the dfs_set step",
          i_apchan < i_dsset)
    check("0x0065 vap_start post-read is before the dfs_set step",
          i_0056 < i_dsset)
    check("dfs_set step (0x07E7) is before the dfs_mode step (0x01F7)",
          i_dsset < i_dsmode)
    check("dfs_mode step (0x01F7) is before the band_start (0x0242)",
          i_dsmode < i_band)
    check("dfs_set pre-gate line is after the 0x0065 post-read",
          i_0056 < i_pre)
    check("dfs_set pre-gate line is before the arm post",
          i_pre < i_arm)
    check("the ARM post line is before the dfs_mode post line",
          i_arm < i_dmpost)
    check("the ARM post-read is before the dfs_mode post line",
          i_armpost < i_dmpost)
    check("dfs_set ARM body 0x01 marker present",
          "g0 id1 key 0x07E7, body 0x01" in scan)
    check("dfs_mode dmesg names the new predecessor (0x07E7 dfs_set step)",
          "after the 0x0065 VAP start and the 0x07E7 dfs_set step" in scan)
    check("dfs_set dmesg names the new successor (0x01F7 dfs_mode)",
          "before the 0x01F7 dfs_mode / 0x0242 band start" in scan)
    check("old dfs_mode position text is gone",
          "- after the 0x00F7 table and before the 0x0124" not in scan)
    check("old dfs_mode MODULE_PARM_DESC position text is gone",
          "after the 0x00F7 table and before the 0x0124 channel config"
          not in scan)
    check("dfs_mode param comment records the task-228 order fix",
          "ORDER FIXED by\n * the task-228 dfs-order lane" in scan)
    check("dfs_set param comment records the ARM-before-0x01F7 rule",
          "BEFORE the\n * 0x01F7 dfs_mode step" in scan)
    check("dfs_mode block comment records the RF-programme consequence",
          "CA 0xA2FA2" in scan and
          "returns silently on this pass" in scan)
    check("the header documents the ARM-before-0x01F7 order",
          "the 0x07E7 ARM must be posted BEFORE the 0x01F7 mode" in hdr)

    print("\n=== hi5622_scan_logic.c: the plan contract order ===")
    p = pos(logic, "unsigned int hi5622_beacon_plan(", "plan function")
    body = logic[p:]
    i_desc = pos(body, "HI5622_BEACON_STEP_DESC_TABLE", "plan desc_table")
    i_chan = pos(body, "HI5622_BEACON_STEP_CHAN", "plan ap_chan")
    i_ssid = pos(body, "HI5622_BEACON_STEP_SSID", "plan ap_ssid")
    i_cfg = pos(body, "HI5622_BEACON_STEP_AP_CFG", "plan ap_cfg")
    i_vsg0 = pos(body, "HI5622_BEACON_STEP_VAP_START_G0", "plan vap_start_g0")
    i_mode = pos(body, "HI5622_BEACON_STEP_DFS_MODE", "plan dfs_mode")
    i_band = pos(body, "HI5622_BEACON_STEP_BAND_START", "plan band_start")
    i_vs = pos(body, "HI5622_BEACON_STEP_VAP_START;", "plan ap_vap_start")
    check("plan desc_table before ap_chan", i_desc < i_chan)
    check("plan ap_chan before ap_ssid", i_chan < i_ssid)
    check("plan ap_ssid before ap_cfg", i_ssid < i_cfg)
    check("plan ap_cfg before vap_start_g0", i_cfg < i_vsg0)
    check("plan vap_start_g0 before dfs_mode (task-228 reorder)",
          i_vsg0 < i_mode)
    check("plan dfs_mode before band_start", i_mode < i_band)
    check("plan band_start before ap_vap_start", i_band < i_vs)
    check("plan comment records the task-228 rule",
          "task-228 order fix" in body)
    check("plan comment says the 0x01F7 follows the VAP start + ARM",
          "follow the 0x0065 VAP start and the 0x07E7 DFS-SET ARM step"
          in body)
    check("plan 0x0124 comment records the skipped CA 0xA2FA2 gate",
          "CA 0xA2FA2 programme's mode gate" in logic)
    check("plan band_start comment moved 0x01F7 after the VAP start",
          "the mode step moved after the VAP start" in logic)

    print("\n=== the moved dfs_mode block is intact ===")
    i0 = scan.find("\t\t/* lane wifi-dfs-mode-send (task-190), MOVED BY THE task-228")
    i1 = scan.find("\t\t/* lane wifi-band1-start-send (task-206): the H2D g0-id1 key\n", i0)
    block = scan[i0:i1]
    check("moved block still builds the 0x01F7 frame",
          "hi5622_dfs_mode_build(" in block)
    check("moved block still posts 'dfs_mode'",
          '"dfs_mode"' in block)
    check("moved block still has the worker-precondition dmesg",
          "cfg80211: dfs_mode post:" in block)
    check("moved block has no leftover band_start code",
          "hi5622_band_start" not in block)
    i2 = scan.find("\t\t/* lane wifi-5g-dfs-set (task-227): the H2D g0-id1 key 0x07E7")
    i3 = scan.find("\t\t/* lane wifi-dfs-mode-send (task-190), MOVED BY THE task-228")
    sblock = scan[i2:i3]
    check("moved dfs_set block is intact (plan + posts)",
          "hi5622_dfs_set_plan(" in sblock and '"dfs_set_arm"' in sblock
          and '"dfs_set_clear"' in sblock)

    print()
    print("RESULT: %d/%d checks PASS" % (n[0] - bad[0], n[0]))
    return 0 if bad[0] == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
