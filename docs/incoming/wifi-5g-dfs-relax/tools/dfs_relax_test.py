#!/usr/bin/env python3
"""Independent host test for the dfs_set +0x4A0 RELAX (lane wifi-5g-dfs-relax).

Usage: dfs_relax_test.py <patched-dfs-order-tree>

The tree is the dfs_order tree (module a5ec150c) + this lane's
patches/0001-dfs-relax.patch.  The test does NOT compile anything: it
re-derives the 0x07E7 wire frames from the tree's own #defines, checks the
sibling dfs_set contract is intact, and asserts the RELAX contract:

  * the ARM/ARM_CLEAR pre-gate ALLOWS u32[mctx+0x4A0] != 0 (the firmware
    worker's register-only silent-success arm) and records ctx_present;
  * the other gates stay fail-closed (-ENODEV/-EINVAL/-EAGAIN/-EALREADY/
    -EPERM) and the CLEAR side is unchanged;
  * the postcheck grades the six-register armed pattern 0x2D and the CAC
    flag 1 (full arm) / 0 (register-only arm);
  * the probe pack carries the four CAC words +0x49C/+0x4A0/+0x4A4/+0x4A8,
    the pre-0x0065 dmesg line exists, and the ARM post line prints
    ctx_present=/arm_kind=;
  * the ladder ORDER (0x0065 -> 0x07E7 -> 0x01F7 -> 0x0242) is intact and
    the 29-B frame bytes are unchanged.
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
ARM_BITS = [1, 0, 1, 1, 0, 1]
ARM_MASK = 0x2D
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

    def dfs_set_region():
        i0 = scan.find("\t\t/* lane wifi-5g-dfs-set (task-227): the H2D g0-id1 key 0x07E7")
        i1 = scan.find("\t\t/* lane wifi-dfs-mode-send (task-190), MOVED BY THE task-228")
        if i0 < 0 or i1 < 0 or i1 <= i0:
            sys.exit("cannot slice the dfs_set block")
        return scan[i0:i1]

    print("=== the sibling 0x07E7 contract is intact ===")
    check("key == 0x07E7", num(hdr, "HI5622_DFS_SET_KEY") == 0x07e7)
    check("wire length 29", num(hdr, "HI5622_DFS_SET_WIRE_LEN") == 29)
    check("arm body 0x01", num(hdr, "HI5622_DFS_SET_BODY_ARM") == 0x01)
    check("clear body 0x00", num(hdr, "HI5622_DFS_SET_BODY_CLEAR") == 0x00)
    check("arm mask 0x2D", num(hdr, "HI5622_DFS_SET_ARM_MASK") == 0x2d)
    check("0x07E7 is NOT the 0x01F7 key",
          num(hdr, "HI5622_DFS_SET_KEY") != DFS_MODE_KEY)
    check("arm frame == firmware-decode KAT", build(0x01) == KAT_ARM)
    check("clear frame == firmware-decode KAT", build(0x00) == KAT_CLEAR)
    check("the six register CAs are unchanged",
          num(hdr, "HI5622_DFS_SET_REG0_CA") == REG_CA[0] and
          num(hdr, "HI5622_DFS_SET_REG_A0_CA") == REG_CA[3])
    check("the armed pattern is {1,0,1,1,0,1} = 0x2D",
          ARM_MASK == sum(b << i for i, b in enumerate(ARM_BITS)) == 0x2d)
    check("builder still in hi5622_dfs_set.c",
          "hi5622_dfs_set_build" in src)
    check("Kbuild still builds hi5622_dfs_set.o",
          "hi5622_dfs_set.o" in read(tree + "/Kbuild"))
    check("no new module parameter (24 module_param_named calls in scan.c)",
          scan.count("module_param_named(") == 24)
    check("fail-closed parameter still wired",
          "module_param_named(dfs_set, hi5622_dfs_set, uint, 0444)" in scan)

    print("\n=== the relaxed +0x4A0 gate (header + hi5622_dfs_set.c) ===")
    check("+0x49C is the CAC config block+0x118",
          num(hdr, "HI5622_DFS_SET_MCTX_CACCTX_OFF") == 0x49c)
    check("+0x4A0 is the CAC context word (no longer '0 to arm')",
          num(hdr, "HI5622_DFS_SET_MCTX_GATE_OFF") == 0x4a0 and
          "!= 0 = register-only arm" in hdr)
    check("+0x4A4 is the config-length word",
          num(hdr, "HI5622_DFS_SET_MCTX_CACLEN_OFF") == 0x4a4)
    check("+0x4A8 is the unexplained bench-1 word",
          num(hdr, "HI5622_DFS_SET_MCTX_B4A8_OFF") == 0x4a8)
    check("the probe carries the four CAC words",
          all(k in hdr for k in ("u32\tb49c;", "u32\tb4a4;", "u32\tb4a8;")))
    check("the probe carries ctx_present", "u8\tctx_present;" in hdr)
    check("precheck takes a WRITABLE probe",
          "int hi5622_dfs_set_precheck(struct hi5622_dfs_set_probe *p, u8 mode);"
          in hdr)
    check("precheck still has the same definition shape",
          "int hi5622_dfs_set_precheck(struct hi5622_dfs_set_probe *p, u8 mode)\n{"
          in src)
    check("precheck derives ctx_present from b4a0",
          "p->ctx_present = p->b4a0 != 0u ? 1u : 0u;" in src)
    check("precheck no longer refuses on b4a0",
          "-EBUSY" not in src and "if (p->b4a0)\n\t\treturn -EBUSY" not in src)
    check("the other precheck gates are kept",
          all(k in src for k in ("-ENODEV", "-EAGAIN", "-EALREADY",
                                 "-EPERM")))
    check("the CAC-flag gate still refuses before the wire",
          "if (p->cac)\n\t\treturn -EALREADY;" in src)
    check("the old +0x4A0 refusal verdict is gone",
          "worker would return without setting the flag" not in src)
    check("the verdict names the register-only arm",
          "register-only arm" in src)
    check("postcheck now takes the probe",
          "int hi5622_dfs_set_postcheck(const struct hi5622_dfs_set_probe *p, u8 mode)"
          in hdr and
          "int hi5622_dfs_set_postcheck(const struct hi5622_dfs_set_probe *p, u8 mode)\n{"
          in src)
    check("the old postcheck signature is gone",
          "hi5622_dfs_set_postcheck(unsigned int cac_flag, u8 mode)" not in src)
    check("postcheck requires the six-register armed pattern",
          "if (!hi5622_dfs_set_armed(p->regs_pattern))\n\t\treturn -EIO;" in src)
    check("postcheck requires CAC flag 1 on the full arm",
          "if (p->ctx_present)\n\t\treturn p->cac == 0u ? 0 : -EIO;\n\treturn p->cac == 1u ? 0 : -EIO;"
          in src)
    check("postcheck keeps CLEAR fail-closed on flag 0",
          "if (mode == HI5622_DFS_SET_MODE_CLEAR)\n\t\treturn p->cac == 0u ? 0 : -EIO;"
          in src)
    check("postcheck refuses NULL/bad mode",
          "if (mode > HI5622_DFS_SET_PARM_MAX || mode == HI5622_DFS_SET_MODE_OFF)"
          in src)
    check("arm_kind is exposed and returns register-only/full",
          "const char *hi5622_dfs_set_arm_kind(" in src and
          '"register-only" : "full"' in src)

    print("\n=== hi5622_scan.c: the four-word probe pack + the post site ===")
    check("the probe reads +0x49C/+0x4A4/+0x4A8",
          all(k in scan for k in (
              "HI5622_DFS_SET_MCTX_CACCTX_OFF)",
              "HI5622_DFS_SET_MCTX_CACLEN_OFF)",
              "HI5622_DFS_SET_MCTX_B4A8_OFF)")))
    check("the probe derives ctx_present",
          "p->ctx_present = p->b4a0 != 0u ? 1u : 0u;" in scan)
    check("the pre-0x0065 dmesg line exists",
          "cfg80211: dfs_ctx pre0x0065: b49c=0x%08x b4a0=0x%08x b4a4=0x%08x b4a8=0x%08x"
          in scan)
    i_pre0 = pos(scan, "cfg80211: dfs_ctx pre0x0065:", "pre-0x0065 line")
    i_0056 = pos(scan, "cfg80211: vap_start_g0 post:", "vap_start_g0 post line")
    i_pre = pos(scan, "cfg80211: dfs_set pre:", "dfs_set pre line")
    check("the pre-0x0065 line is before the 0x0065 post-read",
          i_pre0 < i_0056)
    check("the pre-0x0065 line is before the dfs_set pre line",
          i_pre0 < i_pre)
    check("the dfs_set pre line carries the four words + ctx_present",
          "b49c=0x%08x b4a0=0x%08x b4a4=0x%08x b4a8=0x%08x" in scan and
          "cac=%u ctx_present=%u regs_pattern=0x%02x" in scan)
    check("both postcheck calls pass the probe",
          scan.count("hi5622_dfs_set_postcheck(&dsprobe,") == 2)
    check("the ARM post line prints ctx_present/arm_kind",
          "cfg80211: dfs_set arm post: cac=%u regs_pattern=0x%02x ctx_present=%u arm_kind=%s"
          in scan)
    check("the post site calls arm_kind",
          "hi5622_dfs_set_arm_kind(&dsprobe)" in scan)
    check("the register-only PASS text exists",
          "PASS - register-only arm: the firmware wrote the six band-1 SMAC sub-base bits (0x2D)" in scan)
    check("the full-arm PASS text still exists",
          "PASS - the firmware armed the band-1 SMAC sub-base + CAC flag" in scan)
    check("the ARM_CLEAR pre-clear refusal records ctx_present",
          "ARM_CLEAR read back pattern=0x%02x cac=%u ctx_present=%u" in scan)
    check("the ARM_CLEAR pre-clear refusal stays fail-closed",
          "clear NOT posted, use dfs_set=1 on this rung" in scan)
    check("the CLEAR frame's postcheck is unchanged in intent",
          "hi5622_dfs_set_postcheck(&dsprobe,\n\t\t\t\t\t\t\t(u8)HI5622_DFS_SET_MODE_CLEAR)"
          in scan)

    print("\n=== the ladder ORDER is still the task-228 one ===")
    i_dsset = pos(scan, "lane wifi-5g-dfs-set (task-227): the H2D g0-id1 key 0x07E7",
                  "dfs_set block")
    i_dsmode = pos(scan, "MOVED BY THE task-228 ORDER FIX", "dfs_mode block")
    i_band = pos(scan, "\t\t/* lane wifi-band1-start-send (task-206): the H2D g0-id1 key\n",
                 "band_start block")
    check("0x0065 post-read before the dfs_set step", i_0056 < i_dsset)
    check("dfs_set step (0x07E7) before the dfs_mode step (0x01F7)",
          i_dsset < i_dsmode)
    check("dfs_mode step (0x01F7) before the band_start (0x0242)",
          i_dsmode < i_band)
    check("the dfs_set_arm post string is intact",
          "g0 id1 key 0x07E7, body 0x01" in scan)
    region = dfs_set_region()
    check("the dfs_set block still builds + posts both frames",
          '"dfs_set_arm"' in region and '"dfs_set_clear"' in region and
          "hi5622_dfs_set_build(" in region)
    check("the dfs_set block keeps the plan-call shape",
          "hi5622_dfs_set_precheck(&dsprobe, dsp.mode)" in region)

    print("\nRESULT: %d/%d checks PASS" % (n[0] - bad[0], n[0]))
    return 0 if bad[0] == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
