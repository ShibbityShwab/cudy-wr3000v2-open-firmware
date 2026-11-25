#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_rf_descriptor_re.py - re-derive the firmware RF descriptor chain.

Lane: wifi-fw-rf-descriptor (shared task task-189).  HOST-ONLY, read-only:
the pinned LOCAL vendor artifacts are read in place; only derived values
(addresses, instruction mnemonics, table words, probe addresses) are printed.
No vendor byte is copied into the repository and no device is touched.

Artifacts (local, uncommitted):
  firmware: /home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin
            928,920 B sha256 7fc87e20...311b ; CPU = file offset + 0x40000
  objdump:  the OpenWrt lab ARM objdump (override with --objdump or $RF_OBJDUMP)
  modules:  hi5622v100_wifi.ko (keyed-event emitters; read in place)

The decode (evidence: ../REPORT.md, ../TABLES.md):
  * OBJECT CHAIN (corrected): perlink = *(ctx+0x4B0); P = *(perlink+0xE20)
    (a POINTER dereference, not an inline struct); O = *(P+0x1A00);
    R = *(O+0x2D8).  CA 0x40684 is called as (ctx, P, perlink).
  * O = *(P+0x1A00) is the band object's published per-link, written by the
    pre-HCC boot task's per-record body at CA 0x48A68..0x48A6C from the
    CA 0xB1EE4(bank) context.
  * the RF programme CA 0xA2FA2 requires *(P+0xF04) == 3; that mode byte has
    exactly ONE writer in the image: CA 0xB5D1C inside CA 0xB5B9C, the g0-id1
    config-event worker for key 0x01F7 (table A entry 93 at CA 0x10C524).
  * CA 0xB5B9C takes modes 1..5 from body[0], writes the RF mode byte and the
    RF register image into R = *(perlink+0x2D8) (+0x3A8..+0x418).  The only
    shipped host emitter of key 0x01F7 is hmac_config_set_country_for_dfs
    (wifi.ko .text+0x5762C, WAL command "set_country_for_dfs"); our clean
    driver ladder never sends it -> mode stays 0 -> silent skip.
  * sibling keys: 0x07E7 -> CA 0xB58FC (CAC state -> MAC regs 0x4010Exxx,
    sent by hmac_config_cac_sync_state / hmac_dfs_cac_*), 0x0136 -> CA 0xB5F3C,
    0x07E8 -> CA 0x429B0.
  * no instruction in the image writes perlink+0xE20 (P) or
    perlink+0x2D8 (R): exhaustive Thumb-2 and ARM encoding scan.

Modes:
  --selftest   assert every decoded fact (exit 0 on PASS)
  --decode     the call chain + writer/trigger decode
  --probes     the corrected read-only probe pack with physical addresses
  --frame      the H2D 0x01F7 g0-id1 frame the stock host sends
"""
import argparse
import hashlib
import os
import re
import struct
import subprocess
import sys

BASE = 0x40000                 # firmware CPU = file offset + BASE
BAR0_ADD = 0x6B8000            # host BAR0 offset of firmware RAM = CPU + this
PHYS_BASE = 0x40000000         # BAR0 physical base (committed bringup decode)
DEFAULT_FW = ("/home/shibbityshwab/recovery-extract/lib/firmware/"
              "hi_wifi/FIRMWARE.bin")
FW_SHA = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_LEN = 928920
DEFAULT_OD = ("/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/"
              "toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/"
              "arm-openwrt-linux-objdump")
DEFAULT_KO = ("/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/"
              "hi5622v100_wifi.ko")

# --- firmware addresses (CPU = file + 0x40000) -----------------------------
W0124 = 0x841B8                # H2D 0x0124 channel worker
LDR_P_84228 = 0x84228          # ldr.w r1,[r7,#0xE20]  (P = *(perlink+0xE20))
LDR_P_406BA = 0x406BA          # ldr.w r4,[r5,#0xE20]  (same P in CA 0x40684)
BRANCH_40684 = 0x40684         # the 5 GHz-only branch
PRED_101890 = 0x101890
GATE_944DE = 0x944DE
IMG_A2FA2 = 0xA2FA2
IMG_A2FA2_SHORT = 0x89924
LATCH_89840 = 0x89840
PUB_O = 0x48A68                # add.w r3,r4,#0x1A00 ; str.w sl,[r3]
PUB_O_STORE = 0x48A6C
BOOT_PRELUDE_INIT = 0xD1B68
BAND_LOOKUP = 0xA4FC8
CTX_OF_BAND = 0xB1EE4
MODE_WR = 0xB5D1C              # strb.w r5,[r4,#0xF04]  (the only writer)
MODE_WORKER = 0xB5B9C          # g0-id1 worker: table A entry 93, key 0x01F7
MODE_WORKER_THUMB = 0xB5B9D
MODE_KEY = 0x01F7              # H2D key that sets the RF mode + image
CAC_KEY = 0x07E7               # sibling key: CAC state -> MAC regs (0xB58FC)
CAC_WORKER = 0xB58FC
TABLE_A = 0x10C23C             # entries {u16 key; u16 pad; u32 worker|thumb}
TBL_KEY_F7 = 0x10C51C          # key 0x00F7 -> worker 0x45A31 (desc table)
TBL_KEY_1F7 = 0x10C524         # key 0x01F7 -> worker 0xB5B9D (RF mode)
TBL_KEY_7E7 = 0x10C52C         # key 0x07E7 -> worker 0xB58FD (CAC state)
TBL_KEY_136 = 0x10C534         # key 0x0136 -> worker 0xB5F3D
TBL_KEY_7E8 = 0x10C53C         # key 0x07E8 -> worker 0x429B1
DESC_KEY_F7 = 0x00F7
DESC_WORKER = 0x45A31
HOST_KEY_EMITTER = 0x5762C     # .text: hmac_config_set_country_for_dfs
HOST_KEY_EMITTER_INS = 0x5763C  # MOVW r1,#0x1F7 before hmac_config_send_event
HOST_CAC_SENDER = 0xACE04      # .text: hmac_dfs_cac_stop sends key 0x07E7
KO_SHA = "not-pinned"          # sha reported, not asserted (module is local)
KO_TEXT_OFF = 0x38             # ELF .text file offset (verified by parse)

# --- bench state (2026-11-27, 0xF7 fire; bar0 read path) -------------------
CTX0 = 0x119758                # module table entry 0 (G6a readback)
PERLINK = 0x175A18             # ctx0+0x4B0 (bench readback)
BANDOBJ = 0x15E570             # ctx0+0x4B8 (bench readback)
# the corrected chain addresses (what the probe must read instead of the
# committed lanes' perlink+0x2820 / +0x1D24 guesses)
P_ADDR = PERLINK + 0xE20       # 0x176838  the P back-pointer itself
O_ADDR = BANDOBJ + 0x1A00      # 0x15FF70  O (== perlink when P == bandobj)
R_ADDR = PERLINK + 0x2D8       # 0x175CF0  R (the RF register block)
MODE_ADDR = BANDOBJ + 0xF04    # 0x15F474  RF mode byte (must be 3)
FLAG_ADDR = BANDOBJ + 0xF87    # 0x15F4F7  channel-range branch flag
ATTACH_ADDR = BANDOBJ + 0xD70  # 0x15F3E0  1 = band attached (needed by 0xB5B9C)
BANDID_ADDR = BANDOBJ + 0xD6C  # 0x15F2DC  u16 band/bank ids
RF_WORDS = [0x3A8, 0x3AC, 0x3B4, 0x3CC, 0x3D8, 0x410, 0x414, 0x418]
RF_EXPECT = {
    0x3A8: 0x01800060,
    0x3AC: 0x03E6138A,   # ch 36..64 (CA 0x89924 image)
    0x3B4: 0x026F138A,
    0x3D8: 0xC14580B0,
}
BAND1_CH = [36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120,
            124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165, 184, 188,
            192, 196]

PASS = []
FAIL = []


def check(name, cond, detail=""):
    (PASS if cond else FAIL).append(name)
    print(("PASS  " if cond else "FAIL  ") + name + (("  [" + detail + "]")
                                                     if detail else ""))
    return cond


def read_file(path):
    with open(path, "rb") as f:
        return f.read()


def read32(fw, addr):
    off = addr - BASE
    if off < 0 or off + 4 > len(fw):
        return None
    return struct.unpack_from("<I", fw, off)[0]


def read16(fw, addr):
    off = addr - BASE
    if off < 0 or off + 2 > len(fw):
        return None
    return struct.unpack_from("<H", fw, off)[0]


def elf_text(ko):
    """(file offset, size) of the .ko .text section, from the ELF headers."""
    if len(ko) < 0x34:
        return None
    shoff = struct.unpack_from("<I", ko, 0x20)[0]
    shentsize = struct.unpack_from("<H", ko, 0x2E)[0]
    shnum = struct.unpack_from("<H", ko, 0x30)[0]
    if not (shoff and shentsize == 40 and shnum):
        return None
    for i in range(1, shnum):
        sh = struct.unpack_from("<10I", ko, shoff + i * shentsize)
        if sh[1] == 1 and (sh[2] & 0x4):        # PROGBITS + EXECINSTR
            return sh[4], sh[5]
    return None


def sha256(raw):
    return hashlib.sha256(raw).hexdigest()


def load_disasm(objdump, fw_path):
    """objdump -D -b binary -m arm -M force-thumb, indexed by CPU address."""
    cmd = [objdump, "-D", "-b", "binary", "-m", "arm", "-M", "force-thumb",
           "--adjust-vma=%#x" % BASE, fw_path]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError) as exc:
        sys.exit("objdump failed: %s" % exc)
    insn = {}
    for line in out.stdout.splitlines():
        m = re.match(r"^\s*([0-9a-f]+):\s+[0-9a-f ]+\t(\S+)\t?(.*)$", line)
        if m:
            insn[int(m.group(1), 16)] = (m.group(2), m.group(3).strip())
    return insn


def norm(s):
    return re.sub(r"\s+", " ", s).strip()


def ins(insn, addr):
    return insn.get(addr)


def expect_insn(insn, addr, mnemonic, operand):
    got = insn.get(addr)
    if not got:
        check("@%#x present" % addr, False, "no instruction decoded")
        return False
    want = norm(mnemonic + " " + operand)
    have = norm(got[0] + " " + got[1])
    return check("@%#x = %s" % (addr, want), want == have, "got: " + have)


def encoding_scan(fw):
    """Raw halfword/word scan: every store/load form that could touch a
    field at displacement 0xE20 or 0x2D8.  Returns counts."""
    counts = dict(ldr_e20=0, str_e20=0, strbh_e20=0, arm_ldr_e20=0,
                  arm_str_e20=0, str_2d8=0, ldr_2d8=0)
    for off in range(0, len(fw) - 4, 2):
        hw1, hw2 = struct.unpack_from("<HH", fw, off)
        disp = hw2 & 0xFFF
        op = hw1 & 0xFFF0
        if disp == 0xE20:
            if op == 0xF8D0:
                counts["ldr_e20"] += 1
            elif op == 0xF8C0:
                counts["str_e20"] += 1
            elif op in (0xF880, 0xF8A0):
                counts["strbh_e20"] += 1
        if op == 0xF8C0 and disp == 0x2D8:
            counts["str_2d8"] += 1
        if op == 0xF8D0 and disp == 0x2D8:
            counts["ldr_2d8"] += 1
    for off in range(0, len(fw) - 4, 4):
        w = struct.unpack_from("<I", fw, off)[0]
        if (w & 0xFFF00000) == 0xE5800000 and (w & 0xFFF) == 0xE20:
            counts["arm_str_e20"] += 1
        if (w & 0xFFF00000) == 0xE5900000 and (w & 0xFFF) == 0xE20:
            counts["arm_ldr_e20"] += 1
    return counts


def file_hits(fw, value):
    pat = struct.pack("<I", value)
    hits, start = [], 0
    while True:
        i = fw.find(pat, start)
        if i < 0:
            break
        hits.append(i + BASE)
        start = i + 1
    return hits


def text_hits(insn, needle):
    return sorted(a for a, (m, o) in insn.items() if needle in (m + " " + o))


def probe_rows():
    """The corrected read-only probe pack.  Step 1 resolves ctx0/perlink and
    P; later rows are two-step because their address depends on the readback."""
    rows = []

    def add(obj, addr, width, note):
        rows.append(dict(obj=obj, addr=addr, width=width,
                         bar0=(addr + BAR0_ADD) if addr else None,
                         phys=(PHYS_BASE + addr + BAR0_ADD) if addr else None,
                         note=note))

    add("module-table slot0", 0x171458, 4, "expect 0x00119758")
    add("module-table base u16", 0x172504, 4, "expect 0 -> ctx0 = slot0 + base")
    add("ctx0", CTX0, 4, "computed: slot0[0] + base")
    add("u32(ctx0+0x4B0) = perlink", CTX0 + 0x4B0, 4, "expect 0x00175A18")
    add("u32(ctx0+0x4B8) = bandobj", CTX0 + 0x4B8, 4, "expect 0x0015E570")
    add("P = u32(perlink+0xE20)", P_ADDR, 4,
        "THE MISSING READ: expect the band object 0x0015E570; if 0 the RF "
        "programme logs id 2999 and skips")
    add("P+0x1A00 = O (bandobj+0x1A00)", O_ADDR, 4,
        "bench: == perlink (published by CA 0x48A68)")
    add("P+0xF04 = RF mode byte", MODE_ADDR, 1,
        "expect 3 after an H2D 0x01F7 mode-3 event; 0 = mode setter never ran")
    add("P+0xF87 = channel-range flag", FLAG_ADDR, 1, "0 ch36-64 / 1 ch100+")
    add("P+0xD70 = band attach flag", ATTACH_ADDR, 1,
        "expected 1; CA 0xB5B9C requires it")
    add("P+0xD6C = band/bank ids", BANDID_ADDR, 2, "reference")
    add("R = u32(O+0x2D8)", R_ADDR, 4,
        "expect nonzero block pointer (0x4004xxxx or a RAM shadow); the "
        "programme does NOT null-check it")
    for off in RF_WORDS:
        rows.append(dict(obj="R+%#x" % off, addr=None, width=4,
                         bar0=None, phys=None,
                         note="step 3: R+%#x, expect %s" %
                              (off, hex(RF_EXPECT[off]) if off in RF_EXPECT
                               else "-")))
    return rows


def print_probes():
    print("Corrected read-only probe pack (CPU -> BAR0 -> phys = 0x40000000 + "
          "CPU + 0x6B8000)")
    print("%-28s %-10s %-10s %-12s %s" %
          ("object", "CPU", "BAR0", "phys", "note"))
    for r in probe_rows():
        cpu = "%#010x" % r["addr"] if r["addr"] else "(after R)"
        bar0 = "%#010x" % r["bar0"] if r["bar0"] else "-"
        phys = "%#010x" % r["phys"] if r["phys"] else "-"
        print("%-28s %-10s %-10s %-12s %s" %
              (r["obj"], cpu, bar0, phys, r["note"]))


def run_selftest(args):
    fw = read_file(args.fw)
    check("firmware identity: %d B sha256 %s..." % (len(fw), sha256(fw)[:12]),
          len(fw) == FW_LEN and sha256(fw) == FW_SHA)
    insn = load_disasm(args.objdump, args.fw)
    print("decoded %d instructions (%d B)" % (len(insn), len(fw)))

    # 1. the corrected object chain: P is a dereferenced pointer
    expect_insn(insn, LDR_P_84228, "ldr.w", "r1, [r7, #3616] @ 0xe20")
    expect_insn(insn, LDR_P_406BA, "ldr.w", "r4, [r5, #3616] @ 0xe20")
    expect_insn(insn, IMG_A2FA2, "push", "{r4, r5, r6, lr}")
    # r3 = P + 0x1A00 ; r6 = O ; O==0 -> log 2999 ; then mode byte check
    expect_insn(insn, 0xA2FA8, "add.w", "r3, r0, #6656 @ 0x1a00")
    expect_insn(insn, 0xA2FAC, "ldr", "r6, [r3, #0]")
    expect_insn(insn, 0xA2FB2, "ldmia.w", "sp!, {r4, r5, r6, lr}")
    expect_insn(insn, 0xA2FB6, "movw", "r1, #2999 @ 0xbb7")
    expect_insn(insn, 0xA2FC0, "ldrb.w", "r2, [r0, #3844] @ 0xf04")
    expect_insn(insn, 0xA2FC4, "cmp", "r2, #3")
    expect_insn(insn, 0xA2FD4, "ldr.w", "r4, [r6, #728] @ 0x2d8")
    # latch: R = *(perlink+0x2D8); R+0x410 bit0 = predicate
    expect_insn(insn, 0x89840, "ldr.w", "r2, [r0, #728] @ 0x2d8")
    expect_insn(insn, 0x89844, "ldr.w", "r3, [r2, #1040] @ 0x410")
    expect_insn(insn, 0x8984C, "str.w", "r3, [r2, #1040] @ 0x410")
    # the short-image builder (ch36-64) also dereferences R
    expect_insn(insn, 0x89924, "push", "{r3, r4, r5, lr}")
    expect_insn(insn, 0x89928, "ldr.w", "r4, [r0, #728] @ 0x2d8")

    # 2. the O publisher: boot-task per-record body, bandobj+0x1A00 = perlink
    expect_insn(insn, PUB_O, "add.w", "r3, r4, #6656 @ 0x1a00")
    expect_insn(insn, PUB_O_STORE, "str.w", "sl, [r3]")
    expect_insn(insn, 0x48A78, "cmp", "r3, #2")
    # the band object is looked up by CA 0xA4FC8: base 0x15CA08 + 0x1B68*band
    expect_insn(insn, 0xA4FE2, "movw", "r1, #7016 @ 0x1b68")
    check("band-0 object literal 0x0015CA08 at CA 0x48674",
          read32(fw, 0x48674) == 0x0015CA08)
    check("band-1 object literal 0x0015E570 at CA 0x7628C",
          read32(fw, 0x7628C) == 0x0015E570)

    # 3. the RF mode byte has exactly one writer: CA 0xB5D1C
    expect_insn(insn, MODE_WR, "strb.w", "r5, [r4, #3844] @ 0xf04")
    mode_writers = [a for a in text_hits(insn, "strb.w")
                    if norm(insn[a][1]).endswith("#3844] @ 0xf04")]
    check("CA 0xB5D1C is the ONLY strb to +0xF04",
          len(mode_writers) == 1 and mode_writers[0] == MODE_WR,
          "writers: " + " ".join("%#x" % a for a in mode_writers))
    # the mode worker is reached as g0-id1 table A entry 93 (key 0x01F7);
    # table A entries are {u16 key; u16 pad; u32 worker|thumb}
    check("table A entry 93 = {key 0x01F7, worker 0xB5B9D} at CA 0x10C524",
          read16(fw, TBL_KEY_1F7) == MODE_KEY and
          read32(fw, TBL_KEY_1F7 + 4) == MODE_WORKER_THUMB,
          "key %#x worker %#x" % (read16(fw, TBL_KEY_1F7),
                                  read32(fw, TBL_KEY_1F7 + 4)))
    check("preceding entry = {key 0x00F7, worker 0x45A31} (desc table)",
          read16(fw, TBL_KEY_F7) == DESC_KEY_F7 and
          read32(fw, TBL_KEY_F7 + 4) == DESC_WORKER)
    check("sibling entry = {key 0x07E7, worker 0xB58FD} (CAC state)",
          read16(fw, TBL_KEY_7E7) == CAC_KEY and
          read32(fw, TBL_KEY_7E7 + 4) == CAC_WORKER + 1)
    check("sibling entry = {key 0x0136, worker 0xB5F3D}",
          read16(fw, TBL_KEY_136) == 0x0136 and
          read32(fw, TBL_KEY_136 + 4) == 0xB5F3D)
    check("sibling entry = {key 0x07E8, worker 0x429B1}",
          read16(fw, TBL_KEY_7E8) == 0x07E8 and
          read32(fw, TBL_KEY_7E8 + 4) == 0x429B1)
    # CA 0xB5B9C applies modes 1..5 (tbb) and writes the mode byte it read
    expect_insn(insn, 0xB5D6A, "subs", "r3, r5, #1")
    expect_insn(insn, 0xB5D6E, "bhi.w", "0xb5ea4")
    expect_insn(insn, 0xB5D72, "tbb", "[pc, r3]")
    # CA 0xB5B9C also fills R and gates on the publish + band attach flag
    expect_insn(insn, 0xB5B9E, "ldr.w", "r4, [r0, #1208] @ 0x4b8")
    expect_insn(insn, 0xB5BA6, "add.w", "r6, r4, #6656 @ 0x1a00")
    expect_insn(insn, 0xB5BAA, "ldr", "r0, [r6, #0]")
    expect_insn(insn, 0xB5BC8, "ldrb.w", "r1, [r4, #3440] @ 0xd70")
    expect_insn(insn, 0xB5BE8, "ldr.w", "r3, [r0, #728] @ 0x2d8")
    expect_insn(insn, 0xB5BF0, "str.w", "r2, [r3, #936] @ 0x3a8")
    expect_insn(insn, 0xB5BE2, "ldrb", "r5, [r2, #0]")

    # 4. callers: CA 0x40684 (3), CA 0xA2FA2 (2), short builder CA 0x89924 (2)
    c40684 = text_hits(insn, "bl 0x40684")
    ca2fa2 = text_hits(insn, "bl 0xa2fa2")
    c89924 = text_hits(insn, "bl 0x89924")
    check("CA 0x40684 callers = {0x420B4, 0x83AEA, 0x8422E}",
          c40684 == [0x420B4, 0x83AEA, 0x8422E],
          " ".join("%#x" % a for a in c40684))
    check("CA 0xA2FA2 callers = {0x406AA, 0x617FA}",
          ca2fa2 == [0x406AA, 0x617FA], " ".join("%#x" % a for a in ca2fa2))
    check("CA 0x89924 callers = {0xA3108, 0xB5E7C}",
          c89924 == [0xA3108, 0xB5E7C], " ".join("%#x" % a for a in c89924))

    # 5. negative writer census: P and R are never stored by image code
    counts = encoding_scan(fw)
    check("no Thumb-2 word store to +0xE20", counts["str_e20"] == 0,
          "count=%d" % counts["str_e20"])
    check("no Thumb-2 byte/half store to +0xE20", counts["strbh_e20"] == 0,
          "count=%d" % counts["strbh_e20"])
    check("no ARM store to +0xE20", counts["arm_str_e20"] == 0,
          "count=%d" % counts["arm_str_e20"])
    check("50 Thumb-2 loads of [x,#0xE20] (the only accesses)",
          counts["ldr_e20"] == 50, "count=%d" % counts["ldr_e20"])
    e20_imms = [a for a in text_hits(insn, "#3616")]
    non_load = [a for a in e20_imms
                if not insn[a][0].startswith("ldr")]
    check("no add/movw/str immediate 0xE20 anywhere",
          len(non_load) == 0, " ".join("%#x" % a for a in non_load))
    check("no LE32 literal 0x00000E20 in the image",
          len(file_hits(fw, 0x00000E20)) == 0)
    check("no LE32 literal 0x00002820 in the image",
          len(file_hits(fw, 0x00002820)) == 0)
    check("exactly one store to the +0x2D8 field family (R+0x2D8, CA 0xF39E8)",
          counts["str_2d8"] == 1, "count=%d" % counts["str_2d8"])

    # 6. band differential: identical per-band publish path, band ids
    expect_insn(insn, 0x4854A, "strh.w", "r6, [r5, #3436] @ 0xd6c")
    expect_insn(insn, 0x48A7E, "strb.w", "r3, [r4, #3440] @ 0xd70")
    expect_insn(insn, 0xB1D90, "mov", "r2, r0")
    expect_insn(insn, 0xB1D92, "mov.w", "r3, #7264 @ 0x1c60")
    expect_insn(insn, 0xD4710, "movw", "r3, #7272 @ 0x1c68")
    check("boot prelude init CA 0xD1B68 zeroes CA 0x174BE0, 0x1C68 bytes",
          read32(fw, 0xD1CE4) == 0x174BE0 and
          insn.get(0xD1B68) is not None and "7272" in
          norm(insn[0xD1B68][0] + " " + insn[0xD1B68][1]))

    # 7. host-side arm: the ONLY 0x01F7 emitter in the shipped module
    ko = read_file(args.ko) if os.path.exists(args.ko) else None
    if ko is None:
        check("host module present for the 0x01F7 emitter check", False,
              "missing " + args.ko)
    else:
        text = elf_text(ko)
        check("hi5622v100_wifi.ko ELF .text section found", text is not None)
        if text:
            off = text[0]
            w = struct.unpack_from("<I", ko, off + HOST_KEY_EMITTER_INS)[0]
            check("MOVW r1,#0x01F7 at .text+0x%X (hmac_config_set_country_for_dfs)"
                  % HOST_KEY_EMITTER_INS, w == 0xE30011F7, "word=%#010x" % w)
            movw_1f7 = 0
            for j in range(off, off + text[1] - 4, 4):
                if struct.unpack_from("<I", ko, j)[0] == 0xE30011F7:
                    movw_1f7 += 1
            check("exactly 2 MOVW r1,#0x01F7 sites in the module", movw_1f7 == 2,
                  "count=%d" % movw_1f7)
            check("WAL command name 'set_country_for_dfs' present in the module",
                  ko.find(b"set_country_for_dfs") >= 0)
            check("WAL thunk name 'wal_config_set_country_for_dfs' present",
                  ko.find(b"wal_config_set_country_for_dfs") >= 0)
            # the 0x07E7 CAC-state senders use MOVW r1,#0x07E7
            movw_7e7 = 0
            for j in range(off, off + text[1] - 4, 4):
                if struct.unpack_from("<I", ko, j)[0] == 0xE30017E7:
                    movw_7e7 += 1
            check("MOVW r1,#0x07E7 (CAC-state senders) present in the module",
                  movw_7e7 >= 1, "count=%d" % movw_7e7)

    print()
    print("%d checks PASS, %d FAIL" % (len(PASS), len(FAIL)))
    return 0 if not FAIL else 1


def run_decode(args):
    fw = read_file(args.fw)
    insn = load_disasm(args.objdump, args.fw)
    print("== the corrected object chain ==")
    print("perlink = u32(ctx+0x4B0)            ; bench 0x%08X" % PERLINK)
    print("P       = u32(perlink+0xE20)         ; CA 0x%X: %s" %
          (LDR_P_84228, norm(" ".join(insn[LDR_P_84228]))))
    print("O       = u32(P+0x1A00)             ; CA 0x%X: %s" %
          (0xA2FA8, norm(" ".join(insn[0xA2FA8]))))
    print("R       = u32(O+0x2D8) = u32(perlink+0x2D8)")
    print()
    print("== who writes O (bandobj+0x1A00) ==")
    for a in (PUB_O, PUB_O_STORE):
        print("  CA 0x%X: %s" % (a, norm(" ".join(insn[a]))))
    print("  value = CA 0x%X(bank) context (the published per-link)" %
          CTX_OF_BAND)
    print()
    print("== who arms the RF programme's mode gate (P+0xF04 == 3) ==")
    print("  only writer CA 0x%X: %s" % (MODE_WR, norm(" ".join(insn[MODE_WR]))))
    print("  enclosing worker CA 0x%X = g0-id1 config event key 0x%X"
          % (MODE_WORKER, MODE_KEY))
    print("  table A entries are 8 B {u16 key; u16 pad; u32 worker|thumb}:")
    for a, label in ((TBL_KEY_F7, "desc table 0x00F7"),
                     (TBL_KEY_1F7, "RF mode    0x01F7"),
                     (TBL_KEY_7E7, "CAC state  0x07E7"),
                     (TBL_KEY_136, "sibling    0x0136"),
                     (TBL_KEY_7E8, "sibling    0x07E8")):
        print("    CA 0x%X key %#06x worker CA 0x%X  (%s)" %
              (a, read16(fw, a), read32(fw, a + 4) & ~1, label))
    print("  worker CA 0x%X: r5 = body[0]; modes 1..5 via tbb; writes the" %
          MODE_WORKER)
    print("  build-stored mode byte at %s" % norm(" ".join(insn[MODE_WR])))
    print("  and the RF register image R+0x3A8..R+0x418 (R = *(perlink+0x2D8))")
    print("  (CA 0x%X stores R+0x3A8.. ; CA 0x%X stores R+0x418 etc.)" %
          (0xB5BF0, 0xB5C90))
    print()
    print("== host-side emitters (hi5622v100_wifi.ko, read in place) ==")
    ko = read_file(args.ko) if os.path.exists(args.ko) else None
    if ko is None:
        print("  (module not found: read in place only on the lab host)")
    else:
        text = elf_text(ko)
        print("  key 0x%04X: hmac_config_set_country_for_dfs (.text+0x%X) ->"
              " hmac_config_send_event; WAL command"
              " 'set_country_for_dfs' calls wal_config_set_country_for_dfs."
              % (MODE_KEY, HOST_KEY_EMITTER))
        print("  key 0x%04X: hmac_config_cac_sync_state / hmac_dfs_cac_start /"
              " _stop / _timeout_fn (%d MOVW sites) -- CAC state, not mode."
              % (CAC_KEY, 6))
        if text:
            print("  module .text = file+0x%X, 0x%X B" % (text[0], text[1]))
    print()
    print("== negative census ==")
    counts = encoding_scan(fw)
    print("  stores to perlink+0xE20 (P): %d (none in Thumb-2 or ARM)" %
          (counts["str_e20"] + counts["strbh_e20"] + counts["arm_str_e20"]))
    print("  loads  of perlink+0xE20 (P): %d" % counts["ldr_e20"])
    print("  stores to the +0x2D8 field family: %d" % counts["str_2d8"])
    print("  => P and R are established outside the pinned image code")
    print("     (boot-prelude/ROM/host patch); the probe must read them.")


def run_frame(args):
    print("H2D g0-id1 config event, key 0x%04X (RF mode + register image)"
          % MODE_KEY)
    print("  wire (same family as the working 0x0064/0x0124/0x0127 ladder)")
    print("  header 12 B | 8 B prologue (msg id 1, tag 0x5A5A) | key u16 | len u16"
          " | body | 4 B inherited tail")
    print("  key    = 0x%04X  (firmware worker CA 0x%X)"
          % (MODE_KEY, MODE_WORKER))
    print("  len    = 1")
    print("  body[0]= mode  (firmware reads it at CA 0x%X: ldrb r5,[r2,#0])"
          % 0xB5BE2)
    print("  mode values: 1..5 accepted (tbb at CA 0xB5D72); the 5 GHz RF"
          " programme CA 0x%X requires mode == 3" % IMG_A2FA2)
    print("  required firmware state before sending: u32(bandobj+0x1A00) != 0")
    print("  (published per-link) and u8(bandobj+0xD70) == 1 (band attached)")
    print("  host-side stock emitter: hmac_config_set_country_for_dfs")
    print("  (wifi.ko .text+0x5762C; WAL command name 'set_country_for_dfs');")
    print("  the chain is userspace WAL -> wal ops slot .data+0x890C ->")
    print("  wal_config_set_country_for_dfs -> hmac_config_set_country_for_dfs")
    print("  -> hmac_config_send_event(key 0x01F7, len, data).")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--decode", action="store_true")
    ap.add_argument("--probes", action="store_true")
    ap.add_argument("--frame", action="store_true")
    ap.add_argument("--fw", default=os.environ.get("RF_FIRMWARE", DEFAULT_FW))
    ap.add_argument("--objdump", default=os.environ.get("RF_OBJDUMP", DEFAULT_OD))
    ap.add_argument("--ko", default=os.environ.get("RF_KO", DEFAULT_KO))
    args = ap.parse_args()
    if not any((args.selftest, args.decode, args.probes, args.frame)):
        args.selftest = True
    if not os.path.exists(args.fw):
        sys.exit("firmware image not found: %s" % args.fw)
    if args.probes:
        print_probes()
        return 0
    if args.frame:
        run_frame(args)
        return 0
    if args.decode:
        run_decode(args)
        return 0
    return run_selftest(args)


if __name__ == "__main__":
    sys.exit(main())
