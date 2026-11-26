#!/usr/bin/env python3
"""H2D g0-id1 config-event key 0x07E7 (the DFS SET / CAC arm-clear) decoder.

HOST-ONLY, READ-ONLY re-derivation from the pinned firmware image; no vendor
bytes are printed (only addresses, ids, lengths and the host-built frame our
own driver synthesizes).  Companion of ../REPORT.md / ../TABLES.md.

Usage:
    fw_dfs_set_re.py FIRMWARE.bin [--selftest] [--dump] [--objdump PATH]

Convention: runtime CA = file offset + 0x40000 (pinned image 928,920 B,
sha256 7fc87e20...311b).  The tool re-derives:
  A. the g0-id1 dispatcher table A row 94: key 0x07E7 -> worker CA 0xB58FC;
  B. the 0x07E7 worker's own gates (hcc ctx byte +17 == 1; per-link word
     +0x4B0 != 0; the started-module-context scan CA 0xA4C0E != 0);
  C. the body parse: body[0] = u8(hdr+24), 1 = arm / 0 = clear;
  D. the six band-1 SMAC sub-base u16 literals (0x4010E000 +0/+4/+8 and
     +0xA0/+0xA4/+0xA8) and the exact per-register bit0 write pattern;
  E. the CAC-active flag u8[module_ctx+0x4A5] set/clear and its pre-gates
     u32[module_ctx+0x4A0] / u32[module_ctx+0x4AC], and the helper pair
     CA 0xF593C + 0x5F450 (arm) / CA 0xF58D8 + 0x5E95C (clear);
  F. the frame our driver must emit (the same 29-B g0-id1 carrier as the
     0x00F7 / 0x01F7 siblings; key 0x07E7, u16 len 1, body {01} / {00}).
The vendor HOST-side CAC timing (60 s / 600 s CAC, 1800 s NOL) is not in the
firmware image: it is graded [V*] from the committed wifi-5g-160-dfs lane and
quoted in TABLES.md, not re-derived here.
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

# --- A. dispatcher / table row -------------------------------------------
DISPATCH = 0x43D58
KEY_OFF = 20
PAYLOAD_OFF = 24
KEY_ROW = 8
TAB_A = 0x10C23C
TAB_A_BYTES = 1736          # 217 rows
KEY_DFS_SET = 0x07E7
ROW_DFS_SET = 94            # table A row index (independently pinned)
WORKER = 0xB58FC

# --- B. worker gates ------------------------------------------------------
GATE_HCC_STATE = 0xB5900     # ldrb r3,[r0,#17]
GATE_HCC_STATE_CMP = 0xB5904 # cmp r3,#1
GATE_PERLINK = 0xB5914       # ldr.w r3,[r0,#0x4b0]
GATE_RESOLVE = 0xB5932       # ldr.w r0,[r3,#0xe20]
GATE_SCAN_CALL = 0xB5936     # bl 0xa4c0e
GATE_SCAN = 0xA4C0E          # started-module-context scan
BODY_LOAD = 0xB594A          # ldrb r4,[r4,#0]
ARM_BRANCH = 0xB5996         # cmp r4,#0 ; beq clear
CLEAR_BRANCH = 0xB5A34

# --- C./D. the band-1 SMAC sub-base (CA) and the per-register writes -----
SUBBASE = 0x4010E000
REG_CA = [0x4010E000, 0x4010E004, 0x4010E008,
          0x4010E0A0, 0x4010E0A4, 0x4010E0A8]
# literal load sites (ldr rX,[pc,#imm]) -> literal-pool seats
LIT_SITES = {0xB595C: (0xB5B7C, 0x4010E000),
             0xB5962: (0xB5B80, 0x4010E004),
             0xB596A: (0xB5B84, 0x4010E008),
             0xB5972: (0xB5B88, 0x4010E0A0),
             0xB597A: (0xB5B8C, 0x4010E0A4),
             0xB5988: (0xB5B90, 0x4010E0A8)}
# arm write sites: +8 |1, +4 &~1, +0 |1, +A8 |1, +A4 &~1, +A0 |1
ARM_WRITES = [  # (site, register, op) op: "set" = (old & ~1)|1, "clr" = old & ~1
    (0xB59C6, 0x4010E008, "set"), (0xB59D6, 0x4010E004, "clr"),
    (0xB59E8, 0x4010E000, "set"), (0xB59FA, 0x4010E0A8, "set"),
    (0xB5A08, 0x4010E0A4, "clr"), (0xB5A18, 0x4010E0A0, "set"),
]
CLEAR_WRITES = [  # (site, register) all (old & ~1)
    (0xB5A8E, 0x4010E008), (0xB5A9E, 0x4010E004), (0xB5AAC, 0x4010E000),
    (0xB5ABA, 0x4010E0A8), (0xB5AC8, 0x4010E0A4), (0xB5AD4, 0x4010E0A0),
]
ARM_PATTERN = [1, 0, 1, 1, 0, 1]  # bit0 of REG_CA[0..5] when armed

# --- E. CAC-active flag / pre-gates / helper pair ------------------------
FLAG_OFF = 0x4A5             # u8[module_ctx+0x4A5] = CAC active
GATE_4A0_OFF = 0x4A0         # u32[module_ctx+0x4A0] must be 0 to arm
HELPER_OBJ_OFF = 0x4AC       # u32[module_ctx+0x4AC] helper object
FLAG_READ = 0xB5B10          # ldrb r4,[r5,#0x4a5]
GLAG_GATE = 0xB5B04          # ldr.w r3,[r5,#0x4a0]
ARM_HELPERS = [(0xB5B20, 0xF593C), (0xB5B26, 0x5F450)]
CLEAR_HELPERS = [(0xB5B5C, 0xF58D8), (0xB5B62, 0x5E95C)]
FLAG_SET = 0xB5B3C           # strb r3,[r5,#0x4a5] ; r3 = 1
FLAG_CLR = 0xB5B42           # strb r4,[r5,#0x4a5] ; r4 = 0
HELPER_F593C = 0xF593C       # validates the helper object, arms the detect
HELPER_5F450 = 0x5F450       # arm path (uses +0x4A0 / +0x49C state)
HELPER_F58D8 = 0xF58D8       # clear path validator
HELPER_5E95C = 0x5E95C       # clear path (zeroes +0x49C/+0x4A0)

# --- F. the host frame ---------------------------------------------------
WIRE_LEN = 29
FRAME_ARM = bytes.fromhex(
    "000100001d00010000005a5a0000000001000000e707010001ffffffff")
FRAME_CLR = bytes.fromhex(
    "000100001d00010000005a5a0000000001000000e707010000ffffffff")
DFS_MODE_KEY = 0x01F7        # the DIFFERENT sibling key (mode 3 frame)


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
            insn[int(m.group(1), 16)] = (
                m.group(2).strip(), m.group(3),
                re.sub(r"\s+", " ", re.split(r"\s;\s|\t@", m.group(4))[0].strip()))
    return insn


class Image:
    def __init__(self, path):
        self.raw = open(path, "rb").read()
        self.path = path

    def u16(self, ca):
        return struct.unpack_from("<H", self.raw, ca - BASE)[0]

    def u32(self, ca):
        return struct.unpack_from("<I", self.raw, ca - BASE)[0]

    def literal_at(self, insn, site):
        got = insn.get(site)
        if not got:
            return None
        m = re.match(r"^\w+, \[pc, #(-?\d+)\]", got[2])
        if not m:
            return None
        tgt = ((site + 4) & ~3) + int(m.group(1))
        if BASE <= tgt <= BASE + len(self.raw) - 4:
            return self.u32(tgt)
        return None

    def worker(self, key):
        for i in range(TAB_A_BYTES // KEY_ROW):
            if self.u16(TAB_A + i * KEY_ROW) == key:
                return self.u32(TAB_A + i * KEY_ROW + 4) & ~1
        return None

    def literal_load_sites(self, value):
        seats = set()
        for i in range(0, len(self.raw) - 4, 4):
            if struct.unpack_from("<I", self.raw, i)[0] == value:
                seats.add(BASE + i)
        out = []
        for ca in range(BASE, BASE + len(self.raw) - 4, 2):
            h = self.u16(ca)
            if (h & 0xF800) == 0x4800:
                tgt = ((ca + 4) & ~3) + (h & 0xFF) * 4
            elif h == 0xF8DF:
                tgt = ((ca + 4) & ~3) + (self.u16(ca + 2) & 0xFFF)
            else:
                continue
            if tgt in seats:
                out.append(ca)
        return sorted(out)


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

    def expect(self, label, insn, addr, mnem, ops):
        got = insn.get(addr)
        want = re.sub(r"\s+", " ", mnem + " " + ops)
        ok = bool(got) and re.sub(r"\s+", " ", got[1] + " " + got[2]) == want
        return self.check(label, ok, "got %r want %r" % (got, want))


def body(insn, start, limit=0x800):
    """Recursive-descent instruction addresses of the Thumb function."""
    seen = set()
    work = [start]
    while work:
        a = work.pop()
        if a in seen or not (start <= a < start + limit):
            continue
        got = insn.get(a)
        if not got:
            continue
        seen.add(a)
        raw, mn, ops = got
        size = len(raw.replace(" ", "")) // 2
        nxt = a + size
        if mn == "bx" or (mn == "pop" and "pc" in ops):
            continue
        if mn.split(".")[0] in ("beq", "bne", "bhi", "bls", "bcc", "bcs",
                                "bge", "blt", "bgt", "ble", "bpl", "bmi",
                                "bvs", "bvc", "cbz", "cbnz"):
            for t in re.findall(r"0x([0-9a-f]+)", ops):
                t = int(t, 16)
                if start <= t < start + limit:
                    work.append(t)
        work.append(nxt)
    return sorted(seen)


def frame_arm():
    return bytearray(FRAME_ARM)


def frame_clear():
    return bytearray(FRAME_CLR)


def build_frame(body_byte):
    """Independent builder (mirrors the driver's hi5622_dfs_set_build)."""
    b = bytearray(WIRE_LEN)
    b[0] = 0x00
    b[1] = 0x01
    b[4:6] = struct.pack("<H", WIRE_LEN)
    b[6:8] = struct.pack("<H", 1)
    b[10:12] = b"\x5a\x5a"
    b[12 + 4] = 0x01
    b[20:22] = struct.pack("<H", KEY_DFS_SET)
    b[22:24] = struct.pack("<H", 1)
    b[24] = body_byte
    b[25:29] = b"\xff\xff\xff\xff"
    return bytes(b)


def selftest(img, insn):
    c = Checker()
    print("=== A. identity + dispatcher row 94 ===")
    c.check("firmware length == %d B" % FW_LEN, len(img.raw) == FW_LEN)
    c.check("firmware sha256 == 7fc87e20...311b", sha256(img.raw) == FW_SHA)
    c.check("table A row %d key == 0x%04X" % (ROW_DFS_SET, KEY_DFS_SET),
            img.u16(TAB_A + ROW_DFS_SET * KEY_ROW) == KEY_DFS_SET)
    c.check("table A key 0x%04X -> worker CA %#07x" % (KEY_DFS_SET, WORKER),
            img.worker(KEY_DFS_SET) == WORKER)
    c.expect("dispatcher key load = u16 hdr+20", insn, 0x43D90, "ldrh",
             "r2, [r4, #20]")
    c.expect("dispatcher payload = hdr+24", insn, 0x43DEC, "add.w",
             "r2, r4, #24")

    print("\n=== B. the worker's gates (CA %#07x) ===" % WORKER)
    c.expect("worker prologue", insn, WORKER, "stmdb",
             "sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}")
    c.expect("gate 1: hcc ctx byte +17", insn, GATE_HCC_STATE, "ldrb",
             "r3, [r0, #17]")
    c.expect("gate 1: == 1", insn, GATE_HCC_STATE_CMP, "cmp", "r3, #1")
    c.expect("gate 2: per-link word +0x4B0", insn, GATE_PERLINK, "ldr.w",
             "r3, [r0, #1200]")
    c.expect("gate 2: resolver word [perlink+0xE20]", insn, GATE_RESOLVE,
             "ldr.w", "r0, [r3, #3616]")
    c.expect("gate 2: started-module scan call", insn, GATE_SCAN_CALL, "bl",
             "0xa4c0e")
    c.expect("scan prologue", insn, GATE_SCAN, "stmdb", "sp!, {r4, r5, r6, r7, r8, lr}")
    c.expect("scan: count byte [arg+18]", insn, 0xA4C1C, "ldrb", "r3, [r7, #18]")
    c.expect("scan: message ctx state -1", insn, 0xA4C44, "subs", "r3, #1")
    c.expect("scan: state in 1..3", insn, 0xA4C46, "cmp", "r3, #2")
    c.expect("scan: count the started ctx", insn, 0xA4C4A, "addls", "r4, #1")

    print("\n=== C. the body parse ===")
    c.expect("payload = hdr+24 in the dispatcher ABI", insn, 0x43DEC,
             "add.w", "r2, r4, #24")
    c.expect("body[0] = u8 payload[0]", insn, BODY_LOAD, "ldrb", "r4, [r4, #0]")
    c.expect("body[0] == 0 -> the clear branch", insn, ARM_BRANCH, "cmp",
             "r4, #0")
    c.expect("clear branch target", insn, 0xB5998, "beq.n", "0xb5a34")

    print("\n=== D. the six band-1 SMAC sub-base registers ===")
    for site, (seat, val) in sorted(LIT_SITES.items()):
        c.check("literal @%06x == sub-base register %#x" % (site, val),
                img.literal_at(insn, site) == val,
                "got %r" % img.literal_at(insn, site))
        c.check("literal pool seat %#x holds %#x" % (seat, val),
                img.u32(seat) == val)
    c.check("the %#x base word has exactly the 4 pinned load sites"
            % SUBBASE,
            img.literal_load_sites(SUBBASE) == [0xB595C, 0xB59B8,
                                                0xB5A84, 0xD2180],
            "got %r" % [hex(x) for x in img.literal_load_sites(SUBBASE)])
    for val in REG_CA[1:]:
        c.check("%#x literal loaded only by its one handler site" % val,
                len(img.literal_load_sites(val)) == 1,
                "got %r" % [hex(x) for x in img.literal_load_sites(val)])
    for site, reg, op in ARM_WRITES:
        got = insn.get(site)
        c.check("arm write @%06x -> %#x %s" % (site, reg, op),
                bool(got) and got[1].startswith("strh") and
                op in ("set", "clr"), "got %r" % (got,))
    # the bit op immediately feeding each store: orr #1 for "set", bic #1 + no
    # orr for "clr" (the store of a bic-only value clears bit0).
    c.expect("arm: +8 gets bit0 SET (orr.w r3,r3,#1)", insn, 0xB59C2,
             "orr.w", "r3, r3, #1")
    c.expect("arm: +4 gets bit0 CLEAR (bic.w r3,r3,#1)", insn, 0xB59CE,
             "bic.w", "r3, r3, #1")
    for site, reg in CLEAR_WRITES[:5]:
        got = insn.get(site)
        c.check("clear write @%06x -> %#x" % (site, reg),
                bool(got) and got[1].startswith("strh"), "got %r" % (got,))
    got = insn.get(0xB5AD4)
    c.check("the sixth clear write reuses the arm path's shared store @0xB5A18",
            bool(got) and got[1].startswith("b") and
            insn.get(0xB5A18, ("", "", ""))[1].startswith("strh"),
            "got %r" % (got,))
    c.check("arm pattern {1,0,1,1,0,1} on REG_CA[0..5]",
            ARM_PATTERN == [1, 0, 1, 1, 0, 1])
    c.check("arm pattern = set,clr,set,set,clr,set",
            ["set" if (r & 1) else "clr" for r in ARM_PATTERN] ==
            ["set", "clr", "set", "set", "clr", "set"])

    print("\n=== E. the CAC-active flag + helper pair ===")
    c.expect("flag gate: u32[ctx+0x4A0]", insn, GLAG_GATE, "ldr.w",
             "r3, [r5, #1184]")
    c.expect("flag read: u8[ctx+0x4A5]", insn, FLAG_READ, "ldrb.w",
             "r4, [r5, #1189]")
    c.expect("arm: flag = 1", insn, FLAG_SET, "strb.w", "r3, [r5, #1189]")
    c.expect("clear: flag = 0", insn, FLAG_CLR, "strb.w", "r4, [r5, #1189]")
    for site, tgt in ARM_HELPERS:
        c.expect("arm helper call @%06x -> %#07x" % (site, tgt), insn, site,
                 "bl", "0x%05x" % tgt)
    for site, tgt in CLEAR_HELPERS:
        c.expect("clear helper call @%06x -> %#07x" % (site, tgt), insn, site,
                 "bl", "0x%05x" % tgt)
    c.expect("arm helper F593C validates its object (cbnz)", insn,
             HELPER_F593C + 6, "cbnz", "r0, 0xf5952")
    c.check("module ctx fields differ from the 0x01F7 mode key: the 0x07E7 "
            "worker never touches bandobj+0xF04",
            not any("0xf04" in (insn.get(a, ("", "", ""))[2]).lower()
                    for a in body(insn, WORKER)))

    print("\n=== F. the host frame (our driver's wire bytes) ===")
    c.check("arm frame == the pinned KAT", bytes(frame_arm()) == FRAME_ARM)
    c.check("clear frame == the pinned KAT", bytes(frame_clear()) == FRAME_CLR)
    for name, fr in (("arm", FRAME_ARM), ("clear", FRAME_CLR)):
        c.check("%s frame length == %d" % (name, WIRE_LEN), len(fr) == WIRE_LEN)
        c.check("%s frame group nibble == 0" % name, (fr[0] & 0x0f) == 0)
        c.check("%s frame id == 1" % name,
                struct.unpack_from("<H", fr, 6)[0] == 1)
        c.check("%s frame total field @+4 == %d" % (name, WIRE_LEN),
                struct.unpack_from("<H", fr, 4)[0] == WIRE_LEN)
        c.check("%s frame key @+20 == 0x%04X" % (name, KEY_DFS_SET),
                struct.unpack_from("<H", fr, 20)[0] == KEY_DFS_SET)
        c.check("%s frame body len @+22 == 1" % name,
                struct.unpack_from("<H", fr, 22)[0] == 1)
        c.check("%s frame tag @+10 == 0x5a5a" % name,
                struct.unpack_from("<H", fr, 10)[0] == 0x5a5a)
        c.check("%s frame 0xFF tail" % name, fr[25:29] == b"\xff" * 4)
    c.check("arm body byte == 0x01", FRAME_ARM[24] == 0x01)
    c.check("clear body byte == 0x00", FRAME_CLR[24] == 0x00)
    c.check("arms and clears differ only in the body byte",
            FRAME_ARM[:24] == FRAME_CLR[:24] and
            FRAME_ARM[25:] == FRAME_CLR[25:])
    c.check("independent builder reproduces the arm KAT",
            build_frame(0x01) == FRAME_ARM)
    c.check("independent builder reproduces the clear KAT",
            build_frame(0x00) == FRAME_CLR)
    c.check("0x07E7 is NOT the 0x01F7 mode key",
            KEY_DFS_SET != DFS_MODE_KEY)

    print()
    print("RESULT: %d/%d checks PASS" % (c.n - c.bad, c.n))
    return 0 if c.bad == 0 else 1


def dump(img, insn):
    print("key 0x%04X -> worker CA %#07x (table A row %d); body = u8(hdr+24)"
          % (KEY_DFS_SET, WORKER, ROW_DFS_SET))
    print("body[0] = 0x01 ARM / 0x00 CLEAR (1-byte body, u16 len at hdr+22)")
    print()
    print("arm writes (u16 bit0, old & ~1):")
    for site, reg, op in ARM_WRITES:
        print("  CA %#x  @%06x  bit0 <- %s" % (reg, site, 1 if op == "set" else 0))
    print("clear writes (all six u16 bit0 <- 0) when the armed pattern is read:")
    for site, reg in CLEAR_WRITES:
        print("  CA %#x  @%06x" % (reg, site))
    print()
    print("per-module context (lookup CA 0xA4C0E gate: >=1 ctx state in 1..3):")
    print("  u32[ctx+0x4A0] gate (0 to arm) / u8[ctx+0x4A5] CAC-active flag")
    print("  u32[ctx+0x4AC] helper object -> CA 0xF593C(arm) / 0xF58D8(clear)")
    print()
    print("wire frame (29 B):")
    print("  " + " ".join("%02x" % b for b in FRAME_ARM) + "  (arm, body 01)")
    print("  " + " ".join("%02x" % b for b in FRAME_CLR) + "  (clear, body 00)")
    return 0


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    if not args:
        print(__doc__)
        return 2
    fw_path = args[0]
    objdump = find_objdump(
        argv[argv.index("--objdump") + 1]
        if "--objdump" in argv and argv.index("--objdump") + 1 < len(argv)
        else None)
    img = Image(fw_path)
    insn = load_disasm(objdump, fw_path)
    print("image: %s (%d B, sha256 %s)" % (fw_path, len(img.raw),
                                           sha256(img.raw)[:12] + "..."))
    print("objdump: %s ; %d instructions decoded\n" % (objdump, len(insn)))
    rc = 0
    if "--selftest" in argv:
        rc |= selftest(img, insn)
    if "--dump" in argv:
        rc |= dump(img, insn)
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
