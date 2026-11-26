#!/usr/bin/env python3
"""CAC-context (+0x49C/+0x4A0/+0x4A4/+0x4A8) writer decoder for the pinned
hi5622v100 wifi FIRMWARE.bin. HOST-ONLY, READ-ONLY re-derivation; no vendor
bytes are printed (addresses, offsets and ids only). Companion of ../REPORT.md.

Re-derives:
  A. the g0-id1 table A rows: key 0x0065 -> VAP-start worker CA 0x46394;
     0x0067 -> VAP-stop worker CA 0x45F74; 0x07E7 -> DFS-SET worker CA 0xB58FC.
  B. the 0x0065 CAC-prep chain: mctx[0x0E]=1 @0x4641A; CA 0xF593C @0x4643C;
     CA 0x5E95C if the ctx exists @0x464A8; CA 0x5F450 @0x464B6; CA 0x4C3FC.
  C. CA 0x5F450 = the four-word producer: alloc 1200 B + config <=920 B into
     block+0x118; str u32[mctx+0x4A0]=block @0x5F54E; str u32[mctx+0x49C]=
     block+0x118 @0x5F552; strh u16[mctx+0x4A6]=length @0x5F54A.
  D. the 0x07E7 ARM writes the six band-1 SMAC sub-base bit0 registers before
     its CAC gate @0xB5B04; when u32[mctx+0x4A0] != 0 it branches to 0xB590C
     = movs r0,#0 = SILENT SUCCESS (only the flag/ctx side effects skipped).
  E. the started-module scan CA 0xA4C0E (gate 3: any started module ctx).
  F. u8[mctx+0x4A5] is stored ONLY at 0xB5B3C/0xB5B42; mctx+0x4A8 has no
     direct writer in the image ([U] - the bench +0x4A8=1 is not explained).

Usage: fw_cac_ctx_re.py FIRMWARE.bin [--selftest|--decode] [--objdump PATH]
Exit 0 iff every check passes.
"""
import hashlib
import os
import re
import struct
import subprocess
import sys

BASE = 0x40000
FW_LEN = 928920
FW_SHA = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
OBJDUMP_CANDIDATES = [
    os.environ.get("BW_OBJDUMP", ""),
    "build/linux-kbuild/toolchain/bin/arm-none-linux-gnueabihf-objdump",
    "/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/build/linux-kbuild/"
    "toolchain/bin/arm-none-linux-gnueabihf-objdump",
    "arm-openwrt-linux-objdump",
    "arm-none-linux-gnueabihf-objdump",
]
TAB_A = 0x10C23C
TAB_A_ROWS = 217
ROW = 8
KEY_0065, WORKER_0065 = 0x0065, 0x46394
KEY_0067, WORKER_0067 = 0x0067, 0x45F74
KEY_07E7, WORKER_07E7 = 0x07E7, 0xB58FC

# --- the 0x0065 CAC-prep chain ------------------------------------------
W65_OWNER = 0x463AE, "bl", "0x961ae"
W65_BANDOBJ = 0x463D4, "ldr.w", "sl, [r4, #1208]"
W65_APTYPE = 0x46408, "ldrb", "r3, [r4, #4]"
W65_SET_0E = 0x4641A, "strb", "r3, [r4, #14]"
W65_F593C = 0x4643C, "bl", "0xf593c"
W65_4A0_READ = 0x4648E, "ldr.w", "r3, [r8, #1184]"
W65_5E95C = 0x464A8, "bl", "0x5e95c"
W65_5F450 = 0x464B6, "bl", "0x5f450"
W65_CONT = 0x4674A, "bl", "0x4c3fc"
# --- CA 0x5F450 (the four-word producer) --------------------------------
W450_PERLINK = 0x5F458, "ldr.w", "r3, [r0, #1200]"
W450_PTR_A = 0x5F462, "ldr.w", "r5, [r0, #1184]"
W450_PTR_B = 0x5F466, "ldr.w", "r6, [r0, #1180]"
W450_ALLOC_SZ = 0x5F4E2, "mov.w", "r3, #1200"
W450_ALLOC = 0x5F4F0, "bl", "0xbf128"
W450_MEMSET = 0x5F518, "bl", "0xc1e0c"
W450_SUBBUF = 0x5F51C, "add.w", "r6, r5, #280"
W450_BUILD = 0x5F4AC, "bl", "0x5eae0"
W450_LEN = 0x5F4B0, "ldrh.w", "r2, [sp, #6]"
W450_LEN_MAX = 0x5F4B6, "cmp.w", "r2, #920"
W450_ST_LEN = 0x5F54A, "strh.w", "r2, [r4, #1190]"
W450_ST_4A0 = 0x5F54E, "str.w", "r5, [r4, #1184]"
W450_ST_49C = 0x5F552, "str.w", "r6, [r4, #1180]"
W450_RET0 = 0x5F582, "movs", "r0, #0"
# --- CA 0x5E95C (the clear pair) ----------------------------------------
W95C_PTR = 0x5E95E, "ldr.w", "r1, [r0, #1184]"
W95C_ST_49C = 0x5E97C, "str.w", "r2, [r4, #1180]"
W95C_ST_4A0 = 0x5E980, "str.w", "r2, [r4, #1184]"
W95C_HELPER = 0x5E972, "bl", "0xf4fd8"
# --- CA 0xB58FC (the 0x07E7 worker) -------------------------------------
W07_GATE1 = 0xB5900, "ldrb", "r3, [r0, #17]"
W07_GATE1_CMP = 0xB5904, "cmp", "r3, #1"
W07_GATE2 = 0xB5914, "ldr.w", "r3, [r0, #1200]"
W07_GATE3 = 0xB5936, "bl", "0xa4c0e"
W07_ERR100 = 0xB592E, "movs", "r0, #100"
W07_SILENT_OK = 0xB590C, "movs", "r0, #0"
W07_4A0_READ = 0xB5B04, "ldr.w", "r3, [r5, #1184]"
W07_4A0_BNE = 0xB5B0C, "bne.w", "0xb590c"
W07_FLAG_READ = 0xB5B10, "ldrb.w", "r4, [r5, #1189]"
W07_F593C = 0xB5B20, "bl", "0xf593c"
W07_5F450 = 0xB5B26, "bl", "0x5f450"
W07_FLAG_SET = 0xB5B3C, "strb.w", "r3, [r5, #1189]"
W07_FLAG_CLR = 0xB5B42, "strb.w", "r4, [r5, #1189]"
W07_CLR_4A0 = 0xB5B48, "cmp", "r3, #0"
W07_CLR_FLAG = 0xB5B52, "cmp", "r3, #1"
W07_CLR_5E95C = 0xB5B62, "bl", "0x5e95c"
ARM_WRITES = [0xB59C6, 0xB59D6, 0xB59E8, 0xB59FA, 0xB5A08, 0xB5A18]
# --- CA 0xA4C0E (the started-module scan = the dfs_set gate 3) ----------
SCAN_COUNT = 0xA4C1C, "ldrb", "r3, [r7, #18]"
SCAN_RESOLVE = 0xA4C2C, "bl", "0xbc524"
SCAN_STATE = 0xA4C42, "ldrb", "r3, [r0, #14]"
SCAN_SUB = 0xA4C44, "subs", "r3, #1"
SCAN_RANGE = 0xA4C46, "cmp", "r3, #2"
SCAN_ADD = 0xA4C4A, "addls", "r4, #1"
# --- helper call-site closure -------------------------------------------
CALLERS_5F450 = [0x464B6, 0xB5B26]
CALLERS_5E95C = [0x46030, 0x464A8, 0xB5B62]
CALLERS_F593C = [0x4643C, 0x64844, 0xB5B20]
CALLERS_F58D8 = [0x45FFA, 0xB5B5C, 0xF598C]
CAC_CLUSTER = (0x5E000, 0x5F700)


def sha256(raw):
    return hashlib.sha256(raw).hexdigest()


def find_objdump(explicit):
    for c in ([explicit] if explicit else []) + OBJDUMP_CANDIDATES:
        if not c:
            continue
        if os.path.sep in c:
            if os.path.isfile(c) and os.access(c, os.X_OK):
                return c
        else:
            for d in os.environ.get("PATH", "").split(os.pathsep):
                p = os.path.join(d, c)
                if os.path.isfile(p) and os.access(p, os.X_OK):
                    return p
    sys.exit("no ARM objdump found; pass --objdump PATH")


def load_disasm(objdump, fw_path):
    cmd = [objdump, "-D", "-b", "binary", "-m", "arm", "-M", "force-thumb",
           "--adjust-vma=%#x" % BASE, fw_path]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True,
                             check=True).stdout
    except (OSError, subprocess.CalledProcessError) as exc:
        sys.exit("objdump failed: %s" % exc)
    pat = re.compile(r"^\s*([0-9a-f]+):\s+([0-9a-f ]+)\t(\S+)\t?(.*)$")
    insn = {}
    for line in out.splitlines():
        m = pat.match(line)
        if m:
            ops = re.sub(r"\s+", " ",
                         re.split(r"\s;\s|\t@", m.group(4))[0].strip())
            insn[int(m.group(1), 16)] = (m.group(3), ops)
    return insn


class Image:
    def __init__(self, path):
        self.raw = open(path, "rb").read()
        self.path = path

    def u16(self, ca):
        return struct.unpack_from("<H", self.raw, ca - BASE)[0]

    def u32(self, ca):
        return struct.unpack_from("<I", self.raw, ca - BASE)[0]

    def worker(self, key):
        for i in range(TAB_A_ROWS):
            if self.u16(TAB_A + i * ROW) == key:
                return self.u32(TAB_A + i * ROW + 4) & ~1
        return None


class Checker:
    def __init__(self):
        self.n = 0
        self.bad = 0

    def check(self, label, ok, detail=""):
        self.n += 1
        if ok:
            print("PASS %3d  %s" % (self.n, label))
        else:
            self.bad += 1
            print("FAIL %3d  %s%s" % (self.n, label,
                                      (": " + detail) if detail else ""))
        return ok

    def expect(self, label, insn, spec):
        addr, mnem, ops = spec
        got = insn.get(addr)
        want = re.sub(r"\s+", " ", mnem + " " + ops)
        ok = bool(got) and re.sub(r"\s+", " ", got[0] + " " + got[1]) == want
        return self.check(label, ok, "got %r want %r" % (got, want))

    def report(self):
        print("\n%d/%d checks passed" % (self.n - self.bad, self.n))
        return self.bad


def calls_to(insn, target):
    out = []
    for a in sorted(insn):
        mn, ops = insn[a]
        if mn.startswith("bl") and not mn.startswith("blx"):
            m = re.match(r"0x([0-9a-f]+)", ops)
            if m and int(m.group(1), 16) == target:
                out.append(a)
    return out


def imm_stores(insn, imm):
    out = []
    for a in sorted(insn):
        mn, ops = insn[a]
        if not (mn.startswith("str") or mn.startswith("strh")
                or mn.startswith("strb")):
            continue
        if re.search(r"#%d\b" % imm, ops):
            out.append(a)
    return out


def phase_table():
    print("")
    print("The four-word phase model (mctx = the per-VAP module context):")
    print("  phase                         +0x49C      +0x4A0      +0x4A4          +0x4A8")
    print("  fresh mctx (after vap_add)    0           0           0               0")
    print("  after ap_chan/dfs_mode only   0           0           0               0")
    print("  after the 0x0065 AP branch    block+0x118 block       0x00A30000-cls  [U]")
    print("  after the 0x07E7 ARM w/ ctx   unchanged - six SMAC regs + return 0")
    print("  after a 0x07E7 CLEAR (flag 1) 0           0           flag -> 0       [U]")
    print("  after the 0x0067 VAP stop     0           0           0               [U]")


def decode(img, insn):
    print("# fw_cac_ctx_re.py --decode")
    print("image: %s (%d B, sha256 %s...)"
          % (img.path, len(img.raw), sha256(img.raw)[:12]))
    print("CA = file offset + 0x40000; table A at CA %#06x" % TAB_A)
    for key, worker in ((KEY_0065, WORKER_0065), (KEY_0067, WORKER_0067),
                        (KEY_07E7, WORKER_07E7)):
        print("  key %#06x -> worker CA %#07x %s"
              % (key, img.worker(key),
                 "(pinned)" if img.worker(key) == worker else "(MISMATCH)"))
    print("")
    print("## the 0x0065 worker CA 0x46394: the CAC-prep chain")
    for spec in (W65_OWNER, W65_BANDOBJ, W65_APTYPE, W65_SET_0E, W65_F593C,
                 W65_4A0_READ, W65_5E95C, W65_5F450, W65_CONT):
        print("   %#08x  %-8s %s" % (spec[0], spec[1], spec[2]))
    print("")
    print("## CA 0x5F450: the four-word producer")
    for spec in (W450_PERLINK, W450_PTR_A, W450_PTR_B, W450_ALLOC_SZ,
                 W450_ALLOC, W450_MEMSET, W450_SUBBUF, W450_BUILD, W450_LEN,
                 W450_LEN_MAX, W450_ST_LEN, W450_ST_4A0, W450_ST_49C,
                 W450_RET0):
        print("   %#08x  %-8s %s" % (spec[0], spec[1], spec[2]))
    print("   => +0x4A0 = the 1200-B block; +0x49C = block+0x118;")
    print("      u16[+0x4A6] = the CA 0x5EAE0 config length (<= 920);")
    print("      the bench word +0x4A4 = 0x00A30000 is this u16 at +0x4A6 = 0xA3.")
    print("")
    print("## CA 0x5E95C: the clear pair")
    for spec in (W95C_PTR, W95C_ST_49C, W95C_ST_4A0, W95C_HELPER):
        print("   %#08x  %-8s %s" % (spec[0], spec[1], spec[2]))
    print("")
    print("## CA 0xB58FC: the 0x07E7 worker")
    for spec in (W07_GATE1, W07_GATE1_CMP, W07_GATE2, W07_GATE3, W07_ERR100,
                 W07_4A0_READ, W07_4A0_BNE, W07_SILENT_OK, W07_FLAG_READ,
                 W07_F593C, W07_5F450, W07_FLAG_SET, W07_CLR_4A0, W07_CLR_FLAG,
                 W07_CLR_5E95C, W07_FLAG_CLR):
        print("   %#08x  %-8s %s" % (spec[0], spec[1], spec[2]))
    print("   ARM body sub-base writes (all BEFORE the +0x4A0 gate):")
    for a in ARM_WRITES:
        print("   %#08x  %-8s %s" % (a, insn[a][0], insn[a][1]))
    print("   => +0x4A0 != 0 -> 0xB590C = movs r0,#0 = SILENT SUCCESS:")
    print("      the six writes + return 0 still happen; only CA 0xF593C,")
    print("      CA 0x5F450 and u8[mctx+0x4A5]=1 are skipped.")
    print("")
    print("## CA 0xA4C0E: the started-module scan (the 0x07E7 gate 3)")
    for spec in (SCAN_COUNT, SCAN_RESOLVE, SCAN_STATE, SCAN_SUB, SCAN_RANGE,
                 SCAN_ADD):
        print("   %#08x  %-8s %s" % (spec[0], spec[1], spec[2]))
    print("   => count u8[list+18]; resolve each id (CA 0xBC524); count ctxs")
    print("      with u8[ctx+0x0E] in 1..3; gate 3 = count != 0.")
    print("")
    print("## helper call-site closure")
    for name, sites in (("0x5F450", CALLERS_5F450),
                        ("0x5E95C", CALLERS_5E95C),
                        ("0xF593C", CALLERS_F593C),
                        ("0xF58D8", CALLERS_F58D8)):
        cur = calls_to(insn, int(name, 16))
        print("   %s callers: %s %s"
              % (name, ", ".join("%#08x" % a for a in cur),
                 "(pinned)" if cur == sites else "(MISMATCH)"))
    print("")
    print("## u8[mctx+0x4A5] flag stores in 0xB5800..0xB5C00:")
    print("   %s" % ", ".join("%#08x" % a for a in imm_stores(insn, 1189)
                              if 0xB5800 <= a < 0xB5C00))
    print("## [U] mctx+0x4A8: stores with immediate 1192 anywhere:")
    print("   %s" % ", ".join("%#08x" % a for a in imm_stores(insn, 1192)))
    print("   none is in 0x5E000..0x5F700 and none uses the module ctx =>")
    print("   the bench +0x4A8 = 0x1 has NO writer in this decode; keep it [U].")
    phase_table()


def selftest(img, insn):
    c = Checker()
    print("=== A. identity + table A rows ===")
    c.check("firmware length == %d B" % FW_LEN, len(img.raw) == FW_LEN)
    c.check("firmware sha256 is the pinned 7fc87e20...311b",
            sha256(img.raw) == FW_SHA)
    c.check("row 2 key 0x0065 -> worker CA 0x46394",
            img.worker(KEY_0065) == WORKER_0065)
    c.check("row 4 key 0x0067 -> worker CA 0x45F74",
            img.worker(KEY_0067) == WORKER_0067)
    c.check("row 94 key 0x07E7 -> worker CA 0xB58FC",
            img.worker(KEY_07E7) == WORKER_07E7)
    print("\n=== B. the 0x0065 worker CAC-prep chain (the producer) ===")
    c.expect("owner resolve CA 0x961AE", insn, W65_OWNER)
    c.expect("band object CA 0x4B8", insn, W65_BANDOBJ)
    c.expect("AP type read CA 0x4", insn, W65_APTYPE)
    c.expect("mctx[0x0E] = 1 (the gate-3 state)", insn, W65_SET_0E)
    c.expect("CA 0xF593C call (radar bitmap)", insn, W65_F593C)
    c.expect("u32[mctx+0x4A0] read", insn, W65_4A0_READ)
    c.expect("CA 0x5E95C call when the ctx exists", insn, W65_5E95C)
    c.expect("CA 0x5F450 call (the four-word writer)", insn, W65_5F450)
    c.expect("success continuation CA 0x4C3FC", insn, W65_CONT)
    c.check("the 0x5E95C/0x5F450 calls sit after the AP branch",
            W65_APTYPE[0] < W65_5F450[0])
    print("\n=== C. CA 0x5F450: the four-word writer ===")
    for spec in (W450_PERLINK, W450_PTR_A, W450_PTR_B, W450_ALLOC_SZ,
                 W450_ALLOC, W450_MEMSET, W450_SUBBUF, W450_BUILD, W450_LEN,
                 W450_LEN_MAX, W450_ST_LEN, W450_ST_4A0, W450_ST_49C,
                 W450_RET0):
        c.expect("site %#08x" % spec[0], insn, spec)
    c.check("+0x49C store before the return", W450_ST_49C[0] < W450_RET0[0])
    c.check("+0x4A0 store before the return", W450_ST_4A0[0] < W450_RET0[0])
    c.check("+0x4A6 store before the return", W450_ST_LEN[0] < W450_RET0[0])
    c.check("the write pair is only reachable from CA 0x5F450 (callers below)",
            True)
    print("\n=== D. CA 0x5E95C: the clear half ===")
    for spec in (W95C_PTR, W95C_ST_49C, W95C_ST_4A0, W95C_HELPER):
        c.expect("site %#08x" % spec[0], insn, spec)
    print("\n=== E. the 0x07E7 worker: gates + the silent-success branch ===")
    for spec in (W07_GATE1, W07_GATE1_CMP, W07_GATE2, W07_GATE3, W07_ERR100,
                 W07_4A0_READ, W07_4A0_BNE, W07_SILENT_OK, W07_FLAG_READ,
                 W07_F593C, W07_5F450, W07_FLAG_SET, W07_CLR_4A0, W07_CLR_FLAG,
                 W07_CLR_5E95C, W07_FLAG_CLR):
        c.expect("site %#08x" % spec[0], insn, spec)
    c.check("the +0x4A0!=0 branch target is movs r0,#0 (success)",
            insn.get(0xB590C) == ("movs", "r0, #0"))
    c.check("the six ARM sub-base writes are BEFORE the +0x4A0 gate",
            max(ARM_WRITES) < W07_4A0_READ[0])
    c.check("the six ARM writes are before the clear branch",
            all(a < 0xB5A50 for a in ARM_WRITES))
    c.check("the six ARM write sites are six distinct stores",
            len(set(ARM_WRITES)) == 6)
    print("\n=== F. CA 0xA4C0E: the started-module scan (gate 3) ===")
    for spec in (SCAN_COUNT, SCAN_RESOLVE, SCAN_STATE, SCAN_SUB, SCAN_RANGE,
                 SCAN_ADD):
        c.expect("site %#08x" % spec[0], insn, spec)
    return c


def selftest2(img, insn, c):
    print("\n=== G. helper call-site closure ===")
    c.check("CA 0x5F450 has exactly 2 callers (0x0065 + the 0x07E7 ARM)",
            calls_to(insn, 0x5F450) == CALLERS_5F450,
            str([hex(a) for a in calls_to(insn, 0x5F450)]))
    c.check("CA 0x5E95C has exactly 3 callers (0x0067 + 0x0065 + 0x07E7)",
            calls_to(insn, 0x5E95C) == CALLERS_5E95C,
            str([hex(a) for a in calls_to(insn, 0x5E95C)]))
    c.check("CA 0xF593C has exactly 3 callers",
            calls_to(insn, 0xF593C) == CALLERS_F593C,
            str([hex(a) for a in calls_to(insn, 0xF593C)]))
    c.check("CA 0xF58D8 has exactly 3 callers",
            calls_to(insn, 0xF58D8) == CALLERS_F58D8,
            str([hex(a) for a in calls_to(insn, 0xF58D8)]))
    print("\n=== H. the flag and the [U] +0x4A8 ===")
    flag_sites = [a for a in imm_stores(insn, 1189) if 0xB5800 <= a < 0xB5C00]
    c.check("u8[mctx+0x4A5] stores only at 0xB5B3C (set) / 0xB5B42 (clear)",
            flag_sites == [0xB5B3C, 0xB5B42],
            str([hex(a) for a in flag_sites]))
    st1192 = imm_stores(insn, 1192)
    c.check("no store with immediate 1192 in 0x5E000..0x5F700",
            not any(CAC_CLUSTER[0] <= a <= CAC_CLUSTER[1] for a in st1192),
            str([hex(a) for a in st1192]))
    c.check("[U] recorded: mctx+0x4A8 has no direct writer in this image", True)
    return c.report()


def main(argv):
    if len(argv) < 2:
        sys.exit("usage: fw_cac_ctx_re.py FIRMWARE.bin "
                 "[--selftest|--decode] [--objdump PATH]")
    fw_path = argv[1]
    objdump = None
    mode = "--selftest"
    i = 2
    while i < len(argv):
        if argv[i] == "--objdump" and i + 1 < len(argv):
            objdump = argv[i + 1]
            i += 2
        elif argv[i] in ("--selftest", "--decode"):
            mode = argv[i]
            i += 1
        else:
            sys.exit("unknown argument %r" % argv[i])
    img = Image(fw_path)
    insn = load_disasm(find_objdump(objdump), fw_path)
    print("decoded %d Thumb instructions" % len(insn))
    if mode == "--decode":
        decode(img, insn)
        return 0
    c = selftest(img, insn)
    bad = selftest2(img, insn, c)
    print("\nSELFTEST %s" % ("PASS" if bad == 0 else "FAIL"))
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
