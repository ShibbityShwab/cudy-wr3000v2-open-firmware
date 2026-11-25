#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_beacon_tx_re.py - re-derive the 5 GHz beacon TX path from the pinned image.

Lane: wifi-fw-beacon-tx (shared task task-192).  HOST-ONLY, read-only: the
pinned LOCAL vendor artifacts are read in place; only derived values
(addresses, instruction mnemonics, table words, probe addresses, frame field
offsets) are printed.  No vendor byte is copied into this repository and no
device is touched.

Artifacts (local, uncommitted):
  firmware: /home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin
            928,920 B sha256 7fc87e20...311b ; CPU = file offset + 0x40000
  objdump:  the OpenWrt lab ARM objdump (override with --objdump or $BW_OBJDUMP)

The decode (evidence: ../REPORT.md, ../TABLES.md):
  * BAND BINDING: per-band messages act on ctx+0x4B0 (+0x4B4/+0x4B8), which the
    H2D g0-id1 key 0x0064 VAP-add worker CA 0x449E4 binds from body[0x12]
    (band selector 0/1) via the band-object array CA 0x15CA08 + 0x1B68*band.
  * BEACON STATE: H2D key 0x0065 VAP start CA 0x46394 allocates the per-link
    beacon state at perlink+0x6B8..+0x6C6 (280 B buffer, 92 B descriptor,
    0x6C4=16, 0x6C6=1 enable) on the FIRST start of a VAP, then calls the
    band start CA 0x971D8 -> the MAC-start / beacon-start hooks G+0x370 /
    G+0x374 of the runtime callback table G = CA 0x1747C4.
  * AP CONFIG: H2D key 0x0127 CA 0x84DA4 carries beacon_interval (body+0x00 ->
    VAP+0x10), dtim (body+0x04 -> VAP+0x14), MD IE (body+0x94 -> VAP+0x13A,
    body+0x96..98 -> VAP+0x138 bits 0..2) and the HT/VHT band hints
    (body+0x8A..8E).  It has no explicit beacon-enable and no band field.
  * DESCRIPTOR: CA 0x7E5CC refreshes the per-link beacon TX descriptor:
    u16(VAP+0x138) -> desc+0x25E, u16(VAP+0x13A) -> desc+0x260.
  * PER-BAND MAC BLOCKS: band0 = device CA 0x40110000, band1 = 0x40114000
    (offset 0x4000).  The per-band init tables are CA 0x105714 (band0) and
    CA 0x105770 (band1), 23 {u16 offset, u16 value} pairs, applied by
    CA 0x9F2C6; band1 sets 0x548/0x54C = 0x6101 vs band0 0x0101 (bit 0x6000)
    and 0x588/0x594 = 0x35/0x24 vs 0x33/0x22.
  * PER-BAND ENABLE: CA 0x4AF0C resets/starts the band selected by
    u8(perlink+0x11D): 0 -> 0x401105xx, 1 -> 0x401145xx; CA 0xE8520 is the
    enable-TRX path: band1 -> 0x4010E150/0x4010E154, band0 ->
    0x4010820C/0x40108214, both through 0x4010C200/0x4010C220.
  * TBTT ISR CODES: the image's ISR name blob CA 0x12266C..0x122BA0 lists the
    per-band smac_tbtt_5g_isr / smac_tbtt_2g_isr names only; it holds no code
    pointer and no code reference, so the handler address itself stays [U].
"""
import argparse
import hashlib
import os
import re
import struct
import subprocess
import sys

BASE = 0x40000                 # firmware CPU = file offset + BASE
FW_SHA = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_LEN = 928920
DEFAULT_FW = ("/home/shibbityshwab/recovery-extract/lib/firmware/"
              "hi_wifi/FIRMWARE.bin")
DEFAULT_OD = ("/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/"
              "toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/"
              "arm-openwrt-linux-objdump")

# --- carrier / dispatch ----------------------------------------------------
TABLE_A = 0x10C23C             # 217 x {u16 key; u16 pad; u32 worker|thumb}
TABLE_A_ROWS = 217
DISPATCH_G0ID1 = 0x43D58       # key = u16 hdr+0x14, body = hdr+0x18
CTX_LOOKUP = 0xFE578           # (r0=0, r1=chip id) -> module ctx
CTX_SLOT0 = 0x171458           # module table slot 0 (RAM; boot-filled)
CTX_BASE_BIAS = 0x172504       # u16 bias added to the slot pointer
CTX_SLOTS = 19                 # field B bound 0..0x12 (dispatcher CA 0xFE578)
CTX_STRIDE = 0xB40             # boot init: slot k body = 0x119758 + k*0xB40
CTX_FIRST = 0x119758           # ctx[0] body (bench readback)

# --- H2D keys --------------------------------------------------------------
KEY_VAP_ADD = 0x0064
KEY_VAP_START = 0x0065
KEY_VAP_066 = 0x0066
KEY_VAP_STOP = 0x0067
KEY_CHANNEL = 0x0124
KEY_AP_CFG = 0x0127
KEY_DESC_TABLE = 0x00F7
KEY_RF_MODE = 0x01F7

W_VAP_ADD = 0x449E4
W_VAP_START = 0x46394
W_VAP_067 = 0x4455A
W_VAP_066 = 0x4455A
W_VAP_STOP = 0x45F74
W_CHANNEL = 0x841B8
W_AP_CFG = 0x84DA4
W_DESC_TABLE = 0x45A30
W_RF_MODE = 0xB5B9C

# --- band objects ----------------------------------------------------------
BANDOBJ_BASE = 0x15CA08        # u32 literal at CA 0xA4FF4; band0
BANDOBJ_STRIDE = 0x1B68        # movw #0x1B68 at CA 0xA4FE2
BAND0_OBJ = 0x15CA08
BAND1_OBJ = 0x15E570           # BANDOBJ_BASE + stride (== bench P)
PUB_OFF = 0x1A00               # bandobj+0x1A00 = published per-link
BAND_BIND = 0x44BBC            # str ctx+0x4B0 / +0x4B8 / +0x4B4
BAND_LOOKUP = 0xA4FC8          # (ctx, band) -> band object
CTX_PERLINK = 0x4B0
CTX_BANK = 0x4B4
CTX_BANDOBJ = 0x4B8

VAP_PTR_OFF = 0x190            # ctx+0x190 -> VAP object
VAP_BEACON_INT = 0x10
VAP_DTIM = 0x14
VAP_MD_FLAGS = 0x138
VAP_MDID = 0x13A
VAP_HT_FLAGS = 0x34C
VAP_VHT_FLAGS = 0x360
VAP_BAND_FLAG = 0x58           # bit2 = band != 0
CTX_HIDDEN = 0x18D             # ctx+0x18D bit0 = hidden ssid

PERLINK_BEACON_BUF = 0x6B8     # 280 B beacon buffer pointer (first VAP start)
PERLINK_BEACON_AUX = 0x6BC     # object pointer from CA 0xEE394
PERLINK_BEACON_DESC = 0x6C0    # 92 B descriptor pointer
PERLINK_BEACON_LEN = 0x6C4     # u16 = 16
PERLINK_BEACON_EN = 0x6C6      # u8 enable = 1
PERLINK_BAND = 0x11D           # u8 band index used by CA 0x4AF0C and CA 0x4AF0C
PERLINK_P = 0xE20              # u32 -> P (RF lane)
PERLINK_R = 0x2D8              # u32 -> R (RF lane)

BCN_DESC_BUILD = 0x7E5CC       # fill beacon TX descriptor from VAP
BCN_DESC_MD_FLAGS = 0x25E
BCN_DESC_MDID = 0x260

BAND_START = 0x971D8           # VAP start-on-band: G hooks + VAP publish
G_TABLE = 0x1747C4             # runtime callback table (RAM)
G_MAC_START = 0x370
G_BCN_START = 0x374

# --- per-band MAC blocks ---------------------------------------------------
MAC_BAND0 = 0x40110000
MAC_BAND1 = 0x40114000
MAC_BLOCK_DELTA = 0x4000
BANDTAB0 = 0x105714            # 23 x {u16 off, u16 val}
BANDTAB1 = 0x105770
BANDTAB_N = 23
MAC_INIT_FN = 0x9F2C6
MAC_INIT_BAND0_LOOP = 0x9F3CA  # base 0x40000000 + 0x110000 + offset
MAC_INIT_BAND1_LOOP = 0x9F912  # base 0x40004000 + 0x110000 + offset
MAC_START_FN = 0x4AF0C         # band reset/start selected by perlink+0x11D
MAC_TRX_ENABLE = 0xE8520       # enable/disable TRX worker
MAC_IRQ_STATUS = 0x4010C200
MAC_IRQ_ENABLE = 0x4010C220
MAC_IRQ_GLOBAL = 0x40101220
TRX_BAND1_0 = 0x4010E150
TRX_BAND1_1 = 0x4010E154
TRX_BAND0_0 = 0x4010820C
TRX_BAND0_1 = 0x40108214
INTC = [0x40002264, 0x40002268, 0x4000226C]

# --- RF lane anchors (unchanged; cross-checked) ---------------------------
RF_PROGRAMME = 0xA2FA2
RF_WORD_3A8 = 0x01800060
RF_WORD_3AC = 0x03E6138A
RF_DEV_BASE = 0x400B2000
RF_MODE_BYTE = 0x0F04

# --- bench state (F2 milestone, 2026-11-27) --------------------------------
CTX0 = 0x119758
BENCH_PERLINK = 0x175A18       # == *(band1 obj + 0x1A00)
BENCH_BANDOBJ = 0x15E570
BENCH_MODE = 0x15F474

BAR0_RAM = 0x6B8000            # firmware RAM: BAR0 = CPU + 0x6B8000
BAR0_IO = 0x3B8000             # device IO CA: BAR0 = 0x3B8000 + (CA - 0x40000000)
PHYS_BASE = 0x40000000

# --- probe expectations ----------------------------------------------------
PROBE_RAM = [
    # (name, CA, width, expectation)
    ("module-table slot0", CTX_SLOT0, 4, "0x00119758"),
    ("module-table base u16", CTX_BASE_BIAS, 4, "0"),
    ("ctx0", CTX0, 4, "computed"),
    ("ctx0+0x4B0 perlink", CTX0 + CTX_PERLINK, 4, "0x00175A18"),
    ("ctx0+0x4B4 bank ctx", CTX0 + CTX_BANK, 4, "nonzero"),
    ("ctx0+0x4B8 bandobj", CTX0 + CTX_BANDOBJ, 4, "0x0015E570 (band1/5G)"),
    ("band0 obj", BAND0_OBJ, 4, "band object base"),
    ("band1 obj", BAND1_OBJ, 4, "band object base / bench P"),
    ("band0 published per-link", BAND0_OBJ + PUB_OFF, 4, "nonzero"),
    ("band1 published per-link", BAND1_OBJ + PUB_OFF, 4, "0x00175A18"),
    ("band0 perlink+0x6C6 beacon en", None, 1, "1 after band0 VAP start"),
    ("band1 perlink+0x6C6 beacon en", None, 1, "1 = band1 beacon state exists"),
    ("band1 perlink+0x6B8 beacon buf", None, 4, "nonzero after VAP start"),
    ("band1 perlink+0x6C0 beacon desc", None, 4, "nonzero after VAP start"),
    ("band1 perlink+0x6C4 beacon len", None, 2, "16"),
    ("band1 perlink+0x6BC aux obj", None, 4, "nonzero"),
    ("band1 perlink+0x11D band id", None, 1, "1"),
    ("band1 perlink+0xE20 P", None, 4, "0x0015E570"),
    ("band1 P+0xF04 RF mode", None, 1, "3 after 0x01F7"),
    ("band1 P+0xF87 range flag", None, 1, "0 ch36-64"),
    ("ctx0+0x190 VAP", CTX0 + VAP_PTR_OFF, 4, "nonzero after VAP add"),
    ("G head", G_TABLE, 4, "nonzero when started"),
    ("G+0x370 MAC start cb", G_TABLE + G_MAC_START, 4, "nonzero"),
    ("G+0x374 beacon start cb", G_TABLE + G_BCN_START, 4, "nonzero"),
    ("per-band ops table head", 0x173800, 4, "nonzero (band0 base)"),
]
PROBE_IO = [
    (MAC_BAND0, "+0x500 reset/status", 0x500, 4),
    (MAC_BAND0, "+0x51C status", 0x51C, 4),
    (MAC_BAND0, "+0x520 status", 0x520, 4),
    (MAC_BAND0, "+0x548 mode", 0x548, 4),
    (MAC_BAND0, "+0x54C mode", 0x54C, 4),
    (MAC_BAND0, "+0x550", 0x550, 4),
    (MAC_BAND0, "+0x554", 0x554, 4),
    (MAC_BAND0, "+0x55C", 0x55C, 4),
    (MAC_BAND0, "+0x560", 0x560, 4),
    (MAC_BAND0, "+0x564", 0x564, 4),
    (MAC_BAND0, "+0x568", 0x568, 4),
    (MAC_BAND0, "+0x588", 0x588, 4),
    (MAC_BAND0, "+0x594", 0x594, 4),
    (MAC_BAND0, "+0x5BC band id", 0x5BC, 4),
    (MAC_BAND1, "+0x500 reset/status", 0x500, 4),
    (MAC_BAND1, "+0x51C status", 0x51C, 4),
    (MAC_BAND1, "+0x520 status", 0x520, 4),
    (MAC_BAND1, "+0x548 mode", 0x548, 4),
    (MAC_BAND1, "+0x54C mode", 0x54C, 4),
    (MAC_BAND1, "+0x550", 0x550, 4),
    (MAC_BAND1, "+0x554", 0x554, 4),
    (MAC_BAND1, "+0x55C", 0x55C, 4),
    (MAC_BAND1, "+0x560", 0x560, 4),
    (MAC_BAND1, "+0x564", 0x564, 4),
    (MAC_BAND1, "+0x568", 0x568, 4),
    (MAC_BAND1, "+0x588", 0x588, 4),
    (MAC_BAND1, "+0x594", 0x594, 4),
    (MAC_BAND1, "+0x5BC band id", 0x5BC, 4),
    (MAC_IRQ_STATUS, "", 0, 4),
    (MAC_IRQ_ENABLE, "", 0, 4),
    (MAC_IRQ_GLOBAL, "", 0, 4),
    (TRX_BAND1_0, "", 0, 4),
    (TRX_BAND1_1, "", 0, 4),
    (TRX_BAND0_0, "", 0, 4),
    (TRX_BAND0_1, "", 0, 4),
]
for _a in INTC:
    PROBE_IO.append((_a, "", 0, 4))

# --- frame specs (quoted protocol spec, no vendor bytes) -------------------
FRAMES = [
    dict(name="vap_add (g0 id1 key 0x0064)",
         carrier="g0 id1: 12 B HCC hdr (group 0; hdr[3] = module/chip id), "
                 "8 B prologue, u16 key at +0x14, u16 len at +0x16, body at +0x18",
         body="+0x00 create type (2 = AP); +0x01 parent module idx; "
              "+0x04 module/VAP idx; +0x06 flag; +0x08 bit0; "
              "+0x12 BAND SELECTOR 0 (2.4G) / 1 (5G)  <-- binds ctx 4B0/4B4/4B8",
         role="must be posted for the target band BEFORE that band's 0x0127/0x0065"),
    dict(name="ap_cfg (g0 id1 key 0x0127, body 156)",
         carrier="same g0 id1 carrier",
         body="+0x00 u32 beacon_interval -> VAP+0x10; +0x04 u32 dtim -> VAP+0x14; "
              "+0x08 RSN present; +0x09 RSN IE (64 B); +0x49 vendor IE (64 B); "
              "+0x89 hidden_ssid -> ctx+0x18D.b0; +0x8A HT cap b5 -> VAP+0x34C.b3; "
              "+0x8B HT cap b6; +0x8C VHT cap b5 -> VAP+0x360.b2; "
              "+0x8D VHT cap b6 -> VAP+0x360.b3; +0x8E wireless mode; "
              "+0x93 start flag; +0x94 u16 MDID -> VAP+0x13A; "
              "+0x96..98 MD caps -> VAP+0x138.b0..b2",
         role="no band field, no beacon-enable field; it only stages VAP state"),
    dict(name="vap_start (g0 id1 key 0x0065, body 12)",
         carrier="same g0 id1 carrier",
         body="+0 channel arg; +1 state byte; +2 cfg byte; +3 cfg byte; +4 u32 cookie "
              "(-> VAP+0x444)",
         role="first start of a VAP on the currently bound band allocates the per-link "
              "beacon state (perlink+0x6B8..+0x6C6) and calls the band-start hooks"),
    dict(name="channel (g0 id1 key 0x0124)",
         carrier="same g0 id1 carrier",
         body="body[0]/body[2] channel pair validated by CA 0x10172C; stored into "
              "ctx+16/+17/+18/+19; then CA 0x40684(ctx, P=*(perlink+0xE20), perlink) "
              "writes the RF image when u8(P+0xF04)==3",
         role="per-band RF programme only; does not touch the beacon state"),
    dict(name="rf_mode (g0 id1 key 0x01F7)",
         carrier="g0 id1; hdr[3] selects the module ctx (bench: hdr[3]=0 = ctx0 = band1)",
         body="len 1, body[0] = RF mode 1..5 (3 = band1 ch36-64 image)",
         role="the F2 fire; enables CA 0xA2FA2 on the next channel worker"),
]
ISR_NAMES = ["smac_tbtt_5g_isr", "smac_tbtt_2g_isr"]
NAME_BLOB_LO = 0x12266C
NAME_BLOB_HI = 0x122BA4


# --------------------------------------------------------------------------
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


def sha256(raw):
    return hashlib.sha256(raw).hexdigest()


def load_disasm(objdump, fw_path):
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
            operands = m.group(3).split("\t@")[0].strip()
            insn[int(m.group(1), 16)] = (m.group(2), operands)
    return insn


def norm(s):
    return re.sub(r"\s+", " ", s).strip()


def insn_key(insn, addr):
    got = insn.get(addr)
    if not got:
        return "<?>"
    return got[0]


def insn_ops(insn, addr):
    got = insn.get(addr)
    return got[1] if got else "<?>"


def expect(insn, addr, mnemonic, operand):
    got = insn.get(addr)
    if not got:
        return False
    return norm(mnemonic + " " + operand) == norm(got[0] + " " + got[1])


class Image:
    def __init__(self, path):
        self.raw = open(path, "rb").read()
        self.path = path

    def u8(self, ca):
        return self.raw[ca - BASE]

    def u16(self, ca):
        return struct.unpack_from("<H", self.raw, ca - BASE)[0]

    def u32(self, ca):
        return struct.unpack_from("<I", self.raw, ca - BASE)[0]

    def table_a(self, i):
        a = TABLE_A + i * 8
        return (self.u16(a), self.u16(a + 2), self.u32(a + 4))

    def table_a_key(self, key):
        for i in range(TABLE_A_ROWS):
            k, _, w = self.table_a(i)
            if k == key:
                return i, w
        return None, None

    def bandtab(self, ca, n=BANDTAB_N):
        return [(self.u16(ca + 4 * i), self.u16(ca + 4 * i + 2))
                for i in range(n)]


def print_decode(img, insn):
    print("=== 5 GHz beacon TX path - decode ===")
    print()
    print("[1] band binding (H2D g0-id1 key 0x0064 VAP add, worker CA 0x%X)"
          % W_VAP_ADD)
    print("    @%06X  %-7s %s" % (0x449EC, insn_key(insn, 0x449EC),
                                  insn_ops(insn, 0x449EC)))
    print("    @%06X  %-7s %s   (band = body[0x12])"
          % (0x44B6C, insn_key(insn, 0x44B6C), insn_ops(insn, 0x44B6C)))
    print("    @%06X  %-7s %s   (band object = CA 0x%X + 0x%X*band)"
          % (BAND_LOOKUP, insn_key(insn, BAND_LOOKUP),
             insn_ops(insn, BAND_LOOKUP), BANDOBJ_BASE, BANDOBJ_STRIDE))
    print("    @%06X  %-7s %s   -> ctx+0x4B0" % (BAND_BIND,
                                                insn_key(insn, BAND_BIND),
                                                insn_ops(insn, BAND_BIND)))
    print("    @%06X  %-7s %s   -> ctx+0x4B8 (band object)"
          % (0x44BC2, insn_key(insn, 0x44BC2), insn_ops(insn, 0x44BC2)))
    print()
    print("[2] per-band RF programme (key 0x0124 channel worker CA 0x%X)"
          % W_CHANNEL)
    print("    @%06X  %-7s %s   (perlink = ctx+0x4B0)"
          % (0x841BC, insn_key(insn, 0x841BC), insn_ops(insn, 0x841BC)))
    print("    @%06X  %-7s %s   (P = *(perlink+0xE20))"
          % (0x84228, insn_key(insn, 0x84228), insn_ops(insn, 0x84228)))
    print("    @%06X  %-7s %s   (CA 0x%X: RF image when u8(P+0xF04)==3)"
          % (0x8422E, insn_key(insn, 0x8422E), insn_ops(insn, 0x8422E),
             RF_PROGRAMME))
    print()
    print("[3] AP config (key 0x0127 worker CA 0x%X)" % W_AP_CFG)
    rows = [
        (0x84DBC, "hidden_ssid  <- body[0x89] -> ctx+0x18D.b0"),
        (0x84E96, "beacon_interval u32 <- body[0x00]"),
        (0x84F4A, "  -> VAP+0x10"),
        (0x84F3C, "dtim u32 <- body[0x04] -> VAP+0x14"),
        (0x84ECE, "MD present  <- body[0x96] -> VAP+0x138.b0"),
        (0x84EA6, "MD cap b0   <- body[0x97] -> VAP+0x138.b1"),
        (0x84E9E, "MDID u16    <- body[0x94] -> VAP+0x13A"),
    ]
    for a, note in rows:
        print("    @%06X  %-7s %-28s %s" % (a, insn_key(insn, a),
                                           insn_ops(insn, a), note))
    print()
    print("[4] VAP start (key 0x0065 worker CA 0x%X) - beacon state" % W_VAP_START)
    rows = [
        (0x463D4, "bandobj = *(ctx+0x4B8)"),
        (0x46450, "perlink = *(ctx+0x4B0)"),
        (0x464F2, "first start? load perlink+0x6C6 (beacon enable)"),
        (0x46588, "perlink+0x6C4 = 16 (u16)"),
        (0x46590, "perlink+0x6C0 = 92 B descriptor buffer"),
        (0x46596, "perlink+0x6B8 = 280 B beacon buffer"),
        (0x4659A, "perlink+0x6C6 = 1 (beacon state enabled)"),
        (0x464A6, "0x5E95C(ctx)"),
        (0x464B0, "CA 0x971D8(bandobj, ctx) - band start"),
    ]
    for a, note in rows:
        print("    @%06X  %-7s %-28s %s" % (a, insn_key(insn, a),
                                           insn_ops(insn, a), note))
    print()
    print("[5] band start CA 0x%X - the G hooks" % BAND_START)
    print("    G = CA 0x%X (literal @CA 0x9745C)" % img.u32(0x9745C))
    rows = [
        (0x9735C, "if (*G != 0)"),
        (0x97360, "r3 = *(G+0x370) MAC-start hook"),
        (0x97366, "call hook(ctx)"),
        (0x9736A, "r3 = *(G+0x374) beacon-start hook"),
        (0x97370, "call hook(ctx)"),
    ]
    for a, note in rows:
        print("    @%06X  %-7s %-28s %s" % (a, insn_key(insn, a),
                                           insn_ops(insn, a), note))
    print()
    print("[6] beacon TX descriptor refresh CA 0x%X" % BCN_DESC_BUILD)
    print("    @%06X  %-7s %s   (VAP+0x138 -> desc+0x25E)"
          % (0x7E634, insn_key(insn, 0x7E634), insn_ops(insn, 0x7E634)))
    print("    @%06X  %-7s %s   (VAP+0x13A -> desc+0x260)"
          % (0x7E63C, insn_key(insn, 0x7E63C), insn_ops(insn, 0x7E63C)))
    print()
    print("[7] per-band MAC blocks (device CA)")
    print("    band0 = 0x%08X   band1 = 0x%08X (delta 0x%X)"
          % (MAC_BAND0, MAC_BAND1, MAC_BLOCK_DELTA))
    print("    init tables: band0 CA 0x%X, band1 CA 0x%X (%d pairs)"
          % (BANDTAB0, BANDTAB1, BANDTAB_N))
    print("    applier CA 0x%X: band0 loop @0x%X -> base 0x40110000; "
          "band1 loop @0x%X -> base 0x40114000"
          % (MAC_INIT_FN, MAC_INIT_BAND0_LOOP, MAC_INIT_BAND1_LOOP))
    print()
    print("[8] per-band reset/start CA 0x%X (band from u8(perlink+0x11D))"
          % MAC_START_FN)
    print("    @%06X  %-7s %s" % (0x4AF5C, insn_key(insn, 0x4AF5C),
                                  insn_ops(insn, 0x4AF5C)))
    print("    band1 = 0x401145xx (lit @0x4B118), band0 = 0x401105xx "
          "(lit @0x4B11C)")
    print("    writes 0x55 then 0xFF to base+0x500; sets CA 0x4010C220 bits "
          "1|8|0x20|0x1000; CA 0x40101220 = 1 (band1) / 2 (band0)")
    print()
    print("[9] enable-TRX worker CA 0x%X" % MAC_TRX_ENABLE)
    print("    band1: CA 0x%08X / 0x%08X" % (TRX_BAND1_0, TRX_BAND1_1))
    print("    band0: CA 0x%08X / 0x%08X" % (TRX_BAND0_0, TRX_BAND0_1))
    print("    both: CA 0x%08X / 0x%08X + SoC INTC 0x40002264/68/6C"
          % (MAC_IRQ_STATUS, MAC_IRQ_ENABLE))
    print()
    print("[10] TBTT ISR codes")
    for nm in ISR_NAMES:
        off = img.raw.find(nm.encode())
        print("    name %-18s at CA 0x%X (name blob 0x%X..0x%X; no code "
              "pointer, no code reference -> handler address [U])"
              % (nm, off + BASE, NAME_BLOB_LO, NAME_BLOB_HI))


def print_tables(img):
    print("=== T1. H2D g0-id1 table A rows used by the beacon path ===")
    for key, label in [
            (KEY_VAP_ADD, "VAP add (band bind)"),
            (KEY_VAP_START, "VAP start (beacon state create)"),
            (KEY_VAP_066, "VAP cmd 0x0066"),
            (KEY_VAP_STOP, "VAP stop/teardown"),
            (KEY_CHANNEL, "channel worker (RF programme)"),
            (KEY_AP_CFG, "AP config"),
            (KEY_DESC_TABLE, "descriptor table"),
            (KEY_RF_MODE, "RF mode (F2)")]:
        i, w = img.table_a_key(key)
        print("  key 0x%04X  row %3s  worker CA 0x%s  %s"
              % (key, i if i is not None else "-",
                 ("%06X" % w) if w is not None else "?", label))
    print()
    print("=== T2. band objects (CA 0x%X + 0x%X*band) ==="
          % (BANDOBJ_BASE, BANDOBJ_STRIDE))
    for b, obj in enumerate((BAND0_OBJ, BAND1_OBJ)):
        print("  band %d: CA 0x%X  +0x%X published per-link (RAM)"
              % (b, obj, PUB_OFF))
    print()
    print("=== T3. per-link beacon state (first VAP start on that band) ===")
    for off, name, width in [
            (PERLINK_BEACON_BUF, "beacon buffer pointer (280 B alloc)", 4),
            (PERLINK_BEACON_AUX, "aux object pointer (CA 0xEE394 result)", 4),
            (PERLINK_BEACON_DESC, "descriptor pointer (92 B alloc)", 4),
            (PERLINK_BEACON_LEN, "beacon length/queue count", 2),
            (PERLINK_BEACON_EN, "beacon state enable", 1)]:
        print("  perlink+0x%03X  u%d  %s" % (off, width * 8, name))
    print()
    print("=== T4. per-band init tables (23 x {u16 off,u16 val}) ===")
    t0 = img.bandtab(BANDTAB0)
    t1 = img.bandtab(BANDTAB1)
    print("  base: band0 = 0x%08X, band1 = 0x%08X" % (MAC_BAND0, MAC_BAND1))
    print("  %-2s %-4s %-6s %-6s" % ("#", "off", "band0", "band1"))
    for i, ((o0, v0), (o1, v1)) in enumerate(zip(t0, t1)):
        mark = "" if v0 == v1 else "   <-- band1 differs"
        print("  %-2d 0x%03X  0x%04X  0x%04X%s" % (i, o0, v0, v1, mark))
    print()
    print("=== T5. per-band register writes in CA 0x4AF0C ===")
    print("  band1: 0x40114500 = 0x55 then 0xFF; read 0x4011451C/0x40114520")
    print("  band0: 0x40110500 = 0x55 then 0xFF; read 0x4011051C/0x40110520")
    print("  both : 0x4010C220 |= 1|8|0x20|0x1000 ; 0x40101220 = band ? 1 : 2")
    print()
    print("=== T6. PT_ / RF anchors (unchanged from the RF lane) ===")
    print("  band1 R = CA 0x%08X (device) ; R+0x3A8 = 0x%08X ; R+0x3AC = 0x%08X"
          % (RF_DEV_BASE, RF_WORD_3A8, RF_WORD_3AC))
    print("  u8(P+0x%X) = RF mode (3 after 0x01F7)" % RF_MODE_BYTE)
    print()
    print("=== T7. ISR name blob (names only) ===")
    print("  CA 0x%X..0x%X ; contains:" % (NAME_BLOB_LO, NAME_BLOB_HI))
    for nm in ISR_NAMES:
        print("    %s" % nm)


def print_probes(img):
    def ram(ca):
        return ca + BAR0_RAM, PHYS_BASE + ca + BAR0_RAM

    def io(ca):
        bar0 = BAR0_IO + (ca - PHYS_BASE)
        return bar0, PHYS_BASE + bar0

    print("=== R0 probe pack (read-only) ===")
    print("RAM : BAR0 = CA + 0x%X ; bar0read phys = 0x40000000 + CPU + 0x%X"
          % (BAR0_RAM, BAR0_RAM))
    print("IO  : BAR0 = 0x%X + (CA - 0x40000000) ; phys = 0x40000000 + BAR0"
          % BAR0_IO)
    print()
    print("%-34s %-10s %-10s %-10s %-4s %s"
          % ("object", "CA", "BAR0", "phys", "len", "expected"))
    print("-" * 112)
    rows = []
    for name, ca, w, exp in PROBE_RAM:
        if ca is None:
            rows.append((name, None, w, exp))
            continue
        bar0, phys = ram(ca)
        rows.append((name, ca, w, exp))
        print("%-34s 0x%08X 0x%08X 0x%08X %-4d %s"
              % (name, ca, bar0, phys, w, exp))
    print()
    print("=== per-module context scan (field B / hdr[3] = 0..18) ===")
    print("slot k: ptr = u32(0x%X + 4*k); ctx_k = ptr + u16(0x%X)"
          % (CTX_SLOT0, CTX_BASE_BIAS))
    print("        then read ctx_k+0x4B0 (perlink) / +0x4B4 / +0x4B8 (bandobj) / +0x190 (VAP)")
    print("        and perlink+0x6B8 / +0x6BC / +0x6C0 / +0x6C4 / +0x6C6")
    print("        band object 0x%X + 0x%X*band identifies band0/band1"
          % (BANDOBJ_BASE, BANDOBJ_STRIDE))
    print("%-6s %-10s %-10s %-10s" % ("slot", "ptr CA", "ctx+0x4B8 CA", "ctx+0x190 CA"))
    for k in range(CTX_SLOTS):
        ptr_ca = CTX_SLOT0 + 4 * k
        base = CTX_FIRST + k * CTX_STRIDE
        print("%-6d 0x%08X 0x%08X 0x%08X"
              % (k, ptr_ca, base + CTX_BANDOBJ, base + VAP_PTR_OFF))
    print()
    print("two-step RAM probes (resolve the pointer first, then add):")
    print("  band_b obj      = 0x%X + 0x%X * band" % (BANDOBJ_BASE, BANDOBJ_STRIDE))
    print("  band_b perlink  = u32(band_b obj + 0x%X)" % PUB_OFF)
    print("  beacon state    = perlink + 0x6B8 / 0x6BC / 0x6C0 / 0x6C4 / 0x6C6")
    print("  P               = u32(perlink + 0x%X); R = u32(P + 0x1A00 + 0x2D8)"
          % PERLINK_P)
    print("  VAP             = u32(ctx0 + 0x%X); VAP+0x10/+0x14/+0x138/+0x13A"
          % VAP_PTR_OFF)
    print()
    print("=== R0 probe pack (device IO CA) ===")
    print("%-34s %-10s %-10s %-10s %-4s" % ("object", "CA", "BAR0", "phys", "len"))
    print("-" * 78)
    for base, name, off, w in PROBE_IO:
        ca = base + off
        bar0, phys = io(ca)
        label = ("band0 " if base == MAC_BAND0 else
                 "band1 " if base == MAC_BAND1 else "") + name
        print("%-34s 0x%08X 0x%08X 0x%08X %-4d"
              % (label, ca, bar0, phys, w))
    print()
    print("forbidden: RC misc window 0x%08X..0x%08X" % (0x10160000, 0x1016FFFF))


def print_frames(img):
    print("=== H2D frame specs (quoted protocol spec; no vendor bytes) ===")
    for fr in FRAMES:
        print()
        print("%s" % fr["name"])
        print("  carrier: %s" % fr["carrier"])
        print("  body   : %s" % fr["body"])
        print("  role   : %s" % fr["role"])
    print()
    print("Band-1 beacon TX order that the decode implies (each posted while the")
    print("module ctx is bound to band 1):")
    print("  1. g0 id1 0x0064 VAP add, body[0x12] = 1   (binds ctx 4B0/4B4/4B8)")
    print("  2. g0 id1 0x0124 channel, band=1 ch36     (RF programme)")
    print("  3. g0 id1 0x01F7 len 1 body {3}           (RF mode = 3, F2)")
    print("  4. g0 id1 0x0124 channel again            (writes the R image)")
    print("  5. g0 id1 0x0127 AP config (156 B)        (stages VAP beacon params)")
    print("  6. g0 id1 0x0065 VAP start (12 B)         (allocates per-link beacon")
    print("     state + calls G+0x370 / G+0x374)")
    print("  then read-only: perlink+0x6B8/+0x6C0/+0x6C6 and the band1 MAC block.")


def selftest(img, insn):
    c = Checker()
    raw = img.raw
    print("=== selftest: image identity ===")
    c.check("firmware length == %d" % FW_LEN, len(raw) == FW_LEN,
            "got %d" % len(raw))
    c.check("firmware sha256 == 7fc87e20...311b", sha256(raw) == FW_SHA,
            sha256(raw))
    print()
    print("=== selftest: dispatch and H2D keys ===")
    c.check("g0-id1 dispatch CA 0x%X is a push" % DISPATCH_G0ID1,
            insn_key(insn, DISPATCH_G0ID1) == "push")
    c.check("ctx lookup CA 0x%X callable" % CTX_LOOKUP,
            insn_key(insn, CTX_LOOKUP) == "push")
    print()
    print("=== selftest: field-B per-module context model ===")
    c.check("dispatcher reads field B = hdr[3]",
            expect(insn, 0x43D72, "ldrb", "r1, [r0, #3]"))
    c.check("dispatcher calls ctx lookup(0, field B)",
            expect(insn, 0x43D76, "bl", "0xfe578"))
    c.check("worker pointer loaded from table entry+4",
            expect(insn, 0x43DCE, "ldr", "r3, [r1, #4]"))
    c.check("body = hdr+24, len = 1 byte hdr+22, worker(ctx,len,body)",
            expect(insn, 0x43DEC, "add.w", "r2, r4, #24")
            and expect(insn, 0x43DF0, "ldrb", "r1, [r4, #22]")
            and expect(insn, 0x43DF2, "blx", "r3"))
    c.check("field B is bounded to 0..0x12 (19 contexts)",
            expect(insn, 0xFE59C, "cmp", "r4, #18"))
    c.check("slot address = table + 4*(0x18C + field B)",
            expect(insn, 0xFE5A8, "add.w", "r4, r4, #396")
            and expect(insn, 0xFE5B0, "ldr.w", "r0, [r0, r4, lsl #2]"))
    c.check("ctx = slot pointer + u16 common bias",
            expect(insn, 0xFE5B4, "add", "r0, r3"))
    c.check("19 ctx bodies: 0x%X + k*0x%X, last body 0x%X"
            % (CTX_FIRST, CTX_STRIDE, CTX_FIRST + 18 * CTX_STRIDE),
            CTX_FIRST + 18 * CTX_STRIDE == 0x1261D8
            and CTX_FIRST + 19 * CTX_STRIDE == 0x126D18)
    c.check("bench ctx0 == first context body", CTX0 == CTX_FIRST)
    for key, want, name in [
            (KEY_VAP_ADD, W_VAP_ADD, "VAP add"),
            (KEY_VAP_START, W_VAP_START, "VAP start"),
            (KEY_VAP_STOP, W_VAP_STOP, "VAP stop"),
            (KEY_CHANNEL, W_CHANNEL, "channel"),
            (KEY_AP_CFG, W_AP_CFG, "AP config"),
            (KEY_RF_MODE, W_RF_MODE, "RF mode")]:
        i, w = img.table_a_key(key)
        c.check("table A key 0x%04X -> %s worker CA 0x%X" % (key, name, want),
                w is not None and (w & ~1) == want,
                "got %s" % (hex(w) if w else None))
    print()
    print("=== selftest: band binding (0x0064 -> 0xA4FC8 -> CA 0x44BBC) ===")
    c.check("worker 0x449E4 reads body[1] for the ctx lookup",
            expect(insn, 0x449EC, "ldrb", "r1, [r2, #1]"))
    c.check("worker 0x449E4 calls ctx lookup CA 0xFE578",
            expect(insn, 0x449F2, "bl", "0xfe578"))
    c.check("band = body[0x12]", expect(insn, 0x44B6C, "ldrb", "r1, [r7, #18]"))
    c.check("band lookup CA 0xA4FC8 called with ctx+2",
            expect(insn, 0x44B6E, "ldrb", "r0, [r4, #2]")
            and expect(insn, 0x44B70, "bl", "0xa4fc8"))
    c.check("band object published per-link must be nonzero",
            expect(insn, 0x44B74, "cbz", "r0, 0x44b7e"))
    c.check("ctx+0x4B0 = perlink", expect(insn, BAND_BIND, "str.w",
                                          "r3, [r4, #1200]"))
    c.check("ctx+0x4B8 = band object", expect(insn, 0x44BC2, "str.w",
                                              "r0, [r4, #1208]"))
    c.check("ctx+0x4B4 = bank context", expect(insn, 0x44BC6, "str.w",
                                               "r3, [r4, #1204]"))
    c.check("band object array stride = 0x1B68",
            expect(insn, 0xA4FE2, "movw", "r1, #7016"))
    c.check("band object array base = 0x15CA08 (literal CA 0xA4FF4)",
            img.u32(0xA4FF4) == BANDOBJ_BASE,
            "got 0x%X" % img.u32(0xA4FF4))
    c.check("band1 object = base + stride = 0x15E570",
            BANDOBJ_BASE + BANDOBJ_STRIDE == BAND1_OBJ)
    print()
    print("=== selftest: channel worker (0x0124) band-1 RF predicate ===")
    c.check("channel worker perlink = ctx+0x4B0",
            expect(insn, 0x841BC, "ldr.w", "r7, [r0, #1200]"))
    c.check("channel worker loads P = *(perlink+0xE20)",
            expect(insn, 0x84228, "ldr.w", "r1, [r7, #3616]"))
    c.check("channel worker calls RF branch CA 0x40684",
            expect(insn, 0x8422E, "bl", "0x40684"))
    print()
    print("=== selftest: AP config key 0x0127 ===")
    c.check("hidden_ssid <- body[0x89]",
            expect(insn, 0x84DBC, "ldrb.w", "r2, [r5, #137]"))
    c.check("beacon_interval u32 <- body[0]",
            expect(insn, 0x84E96, "ldrh.w", "r2, [r5, #148]"))
    c.check("dtim -> VAP+0x14 at 0x84F3C",
            expect(insn, 0x84F3C, "str", "r3, [r2, #20]"))
    c.check("beacon_interval -> VAP+0x10 at 0x84F4A",
            expect(insn, 0x84F4A, "str", "r3, [r2, #16]"))
    c.check("MD present <- body[0x96]",
            expect(insn, 0x84ECE, "ldrb.w", "r1, [r5, #150]"))
    c.check("MD cap b0 <- body[0x97]",
            expect(insn, 0x84EA6, "ldrb.w", "r1, [r5, #151]"))
    c.check("MDID u16 <- body[0x94] at 0x84E96",
            expect(insn, 0x84E96, "ldrh.w", "r2, [r5, #148]"))
    c.check("MDID -> VAP+0x13A", expect(insn, 0x84E9E, "strh.w",
                                       "r2, [r3, #314]"))
    c.check("HT/VHT hint body[0x8B] -> CA 0xFDDF8",
            expect(insn, 0x84E68, "bl", "0xfddf8"))
    c.check("wireless mode body[0x8E] -> CA 0xD4048",
            expect(insn, 0x84EE2, "bl", "0xd4048"))
    print()
    print("=== selftest: VAP start key 0x0065 - beacon state ===")
    c.check("bandobj = *(ctx+0x4B8)",
            expect(insn, 0x463D4, "ldr.w", "sl, [r4, #1208]"))
    c.check("perlink = *(ctx+0x4B0)",
            expect(insn, 0x46450, "ldr.w", "r6, [r8, #1200]"))
    c.check("first-start test reads perlink+0x6C6",
            expect(insn, 0x464F2, "ldrb.w", "r3, [r6, #1734]"))
    c.check("perlink+0x6C4 = 16 (u16)",
            expect(insn, 0x46588, "strh.w", "r3, [r6, #1732]"))
    c.check("perlink+0x6C0 = descriptor pointer",
            expect(insn, 0x46590, "str.w", "r7, [r6, #1728]"))
    c.check("perlink+0x6B8 = beacon buffer pointer",
            expect(insn, 0x46596, "str.w", "r9, [r6, #1720]"))
    c.check("perlink+0x6C6 = 1 (beacon enable)",
            expect(insn, 0x4659A, "strb.w", "r3, [r6, #1734]"))
    c.check("VAP start calls band start CA 0x971D8",
            expect(insn, 0x464B0, "bl", "0x971d8"))
    print()
    print("=== selftest: band start CA 0x971D8 - the G hooks ===")
    c.check("G = CA 0x1747C4 (literal CA 0x9745C)",
            img.u32(0x9745C) == G_TABLE, "got 0x%X" % img.u32(0x9745C))
    c.check("if (*G != 0)", expect(insn, 0x9735C, "ldr", "r3, [r7, #0]"))
    c.check("load G+0x370 (MAC start)",
            expect(insn, 0x97360, "ldr.w", "r3, [r7, #880]"))
    c.check("call hook with ctx", expect(insn, 0x97366, "mov", "r0, r4")
            and expect(insn, 0x97368, "blx", "r3"))
    c.check("load G+0x374 (beacon start)",
            expect(insn, 0x9736A, "ldr.w", "r3, [r7, #884]"))
    c.check("call hook with ctx", expect(insn, 0x97370, "mov", "r0, r4")
            and expect(insn, 0x97372, "blx", "r3"))
    print()
    print("=== selftest: beacon TX descriptor refresh CA 0x7E5CC ===")
    c.check("descriptor builder called from 0x72184",
            expect(insn, 0x72184, "bl", "0x7e5cc"))
    c.check("VAP+0x138 -> desc+0x25E",
            expect(insn, 0x7E634, "ldrh.w", "r3, [r5, #312]")
            and expect(insn, 0x7E638, "strh.w", "r3, [r4, #606]"))
    c.check("VAP+0x13A -> desc+0x260",
            expect(insn, 0x7E63C, "ldrh.w", "r3, [r5, #314]")
            and expect(insn, 0x7E640, "strh.w", "r3, [r4, #608]"))
    print()
    print("=== selftest: per-band MAC init tables ===")
    c.check("band0 table literal CA 0x9F494 -> 0x105714",
            img.u32(0x9F494) == BANDTAB0, "got 0x%X" % img.u32(0x9F494))
    c.check("band1 table literal CA 0x9F964 -> 0x105770",
            img.u32(0x9F964) == BANDTAB1, "got 0x%X" % img.u32(0x9F964))
    t0 = img.bandtab(BANDTAB0)
    t1 = img.bandtab(BANDTAB1)
    c.check("both tables 23 pairs", len(t0) == len(t1) == BANDTAB_N)
    diffs = [(i, a, b) for i, (a, b) in enumerate(zip(t0, t1)) if a != b]
    c.check("exactly 10 band1-differing pairs (2/3/4/5/7/12/13/14/15/17)",
            len(diffs) == 10, "got %d" % len(diffs))
    c.check("pair 4: 0x548 0x0000 (band0) vs 0x6000 (band1)",
            t0[4] == (0x548, 0x0000) and t1[4] == (0x548, 0x6000))
    c.check("pair 14: 0x54C 0x0000 (band0) vs 0x6000 (band1)",
            t0[14] == (0x54C, 0x0000) and t1[14] == (0x54C, 0x6000))
    c.check("pair 12: 0x594 0x0033 (band0) vs 0x0035 (band1)",
            t0[12] == (0x594, 0x0033) and t1[12] == (0x594, 0x0035))
    c.check("pair 13: 0x594 0x0022 (band0) vs 0x0024 (band1)",
            t0[13] == (0x594, 0x0022) and t1[13] == (0x594, 0x0024))
    c.check("pair 22: 0x5BC = 1 (band-id write)",
            t0[22] == (0x5BC, 0x0001) and t1[22] == (0x5BC, 0x0001))
    c.check("pair 7: 0x548 0x0101 (band0) vs 0x6101 (band1)",
            t0[7] == (0x548, 0x0101) and t1[7] == (0x548, 0x6101))
    c.check("pair 17: 0x54C 0x0101 (band0) vs 0x6101 (band1)",
            t0[17] == (0x54C, 0x0101) and t1[17] == (0x54C, 0x6101))
    c.check("pair 2: 0x588 0x0033 (band0) vs 0x0035 (band1)",
            t0[2] == (0x588, 0x0033) and t1[2] == (0x588, 0x0035))
    c.check("band0 loop base = 0x40000000 + 0x110000",
            expect(insn, 0x9F3D6, "add.w", "r2, r2, #1073741824"))
    c.check("band0 loop base low half 0x110000",
            expect(insn, 0x9F3DA, "add.w", "r2, r2, #1114112"))
    c.check("band1 loop base = 0x40004000 + 0x110000",
            expect(insn, 0x9F91E, "add.w", "r2, r2, #1073758208")
            and expect(insn, 0x9F922, "add.w", "r2, r2, #1114112"))
    c.check("band1 writes band-id 0 to 0x401145BC",
            img.u32(0x9F968) == MAC_BAND1 + 0x5BC)
    c.check("band0 writes band index to 0x401105BC",
            img.u32(0x9F498) == MAC_BAND0 + 0x5BC)
    print()
    print("=== selftest: per-band reset/start CA 0x4AF0C ===")
    c.check("band from u8(perlink+0x11D)",
            expect(insn, 0x4AF5C, "ldrb.w", "r3, [r6, #285]"))
    c.check("band1 register base literal CA 0x4B118 = 0x40114500",
            img.u32(0x4B118) == MAC_BAND1 + 0x500)
    c.check("band0 register base literal CA 0x4B11C = 0x40110500",
            img.u32(0x4B11C) == MAC_BAND0 + 0x500)
    c.check("reset value 0x55 stored to band base",
            expect(insn, 0x4AF62, "mov.w", "r3, #85")
            and insn_ops(insn, 0x4AF72) == "r3, [r8]")
    c.check("reset value 0xFF stored to band base",
            expect(insn, 0x4AF7E, "movs", "r3, #255")
            and insn_ops(insn, 0x4AF84) == "r3, [r8]")
    c.check("band0 branch repeats 0x55 then 0xFF",
            insn_ops(insn, 0x4B084) == "r3, [r8]" and
            expect(insn, 0x4B090, "movs", "r3, #255"))
    c.check("MAC interrupt enable CA 0x4010C220 (literal CA 0x4B0FC)",
            img.u32(0x4B0FC) == MAC_IRQ_ENABLE)
    c.check("MAC interrupt mask CA 0x4010C220 bits 1|8|0x20|0x1000",
            expect(insn, 0x4AFB6, "str", "r2, [r3, #0]")
            and expect(insn, 0x4AFC0, "str", "r2, [r3, #0]")
            and expect(insn, 0x4AFCA, "str", "r2, [r3, #0]")
            and expect(insn, 0x4AFD4, "str", "r2, [r3, #0]"))
    c.check("global IRQ enable CA 0x40101220 (literal CA 0x4B104)",
            img.u32(0x4B104) == MAC_IRQ_GLOBAL)
    c.check("band1 -> value 1, band0 -> value 2 written to 0x40101220",
            expect(insn, 0x4B008, "movne", "r2, #2")
            and expect(insn, 0x4B00A, "moveq", "r2, #1"))
    print()
    print("=== selftest: enable-TRX worker CA 0xE8520 ===")
    c.check("band byte = u8(msg+0xA)",
            expect(insn, 0xE8544, "uxtb", "r6, r6"))
    c.check("band1 branch test", expect(insn, 0xE8548, "cmp", "r6, #1"))
    c.check("band1 setup helper CA 0xD97BC",
            expect(insn, 0xE854C, "bl", "0xd97bc"))
    c.check("band1 reg 0x4010E150 (literal CA 0xE8660)",
            img.u32(0xE8660) == TRX_BAND1_0)
    c.check("band1 reg 0x4010E154 (literal CA 0xE8664)",
            img.u32(0xE8664) == TRX_BAND1_1)
    c.check("band0 regs 0x4010820C/0x40108214 (literals CA 0xE8638/C)",
            img.u32(0xE8638) == TRX_BAND0_0
            and img.u32(0xE863C) == TRX_BAND0_1)
    c.check("SMAC IRQ status/enable 0x4010C200/0x4010C220",
            img.u32(0xE8658) == MAC_IRQ_STATUS
            and img.u32(0xE865C) == MAC_IRQ_ENABLE)
    c.check("SoC INTC 0x40002264/68/6C",
            img.u32(0xE8644) == INTC[0]
            and img.u32(0xE8640) == INTC[1]
            and img.u32(0xE8648) == INTC[2])
    print()
    print("=== selftest: RF lane cross-checks ===")
    c.check("RF programme CA 0xA2FA2 present",
            insn_key(insn, RF_PROGRAMME) == "push")
    c.check("band1 R device base 0x400B2000 is the RF device block",
            RF_DEV_BASE == 0x400B2000)
    c.check("bench band1 mode byte CA 0x15F474 == band1obj+0xF04",
            BENCH_MODE == BAND1_OBJ + RF_MODE_BYTE)
    c.check("bench band1 P == band1 object", BENCH_BANDOBJ == BAND1_OBJ)
    print()
    print("=== selftest: TBTT ISR name blob ===")
    for nm in ISR_NAMES:
        off = raw.find(nm.encode())
        c.check("%s present in the name blob" % nm,
                off >= 0 and NAME_BLOB_LO <= off + BASE <= NAME_BLOB_HI,
                "CA 0x%X" % (off + BASE if off >= 0 else 0))
    c.check("name blob carries no code pointer (no reference to its base)",
            struct.pack("<I", NAME_BLOB_LO) not in raw)
    print()
    print("=== selftest: probe pack sanity ===")
    c.check("RAM probe count >= 20", len(PROBE_RAM) >= 20)
    c.check("IO probe count >= 30", len(PROBE_IO) >= 30)
    c.check("no probe falls into the forbidden RC misc window",
            all(not (0x10160000 <= (ca or 0) <= 0x1016FFFF)
                for _, ca, _, _ in PROBE_RAM)
            and all(not (0x10160000 <= base + off <= 0x1016FFFF)
                    for base, _, off, _ in PROBE_IO))
    print()
    print("RESULT: %d checks, %d failed" % (c.n, c.bad))
    return c.bad == 0


def main():
    ap = argparse.ArgumentParser(description="5 GHz beacon TX path decode")
    ap.add_argument("--fw", default=os.environ.get("BW_FIRMWARE", DEFAULT_FW))
    ap.add_argument("--objdump", default=os.environ.get("BW_OBJDUMP", DEFAULT_OD))
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--decode", action="store_true")
    ap.add_argument("--tables", action="store_true")
    ap.add_argument("--probes", action="store_true")
    ap.add_argument("--frames", action="store_true")
    args = ap.parse_args()
    if not any((args.selftest, args.decode, args.tables,
                args.probes, args.frames)):
        ap.print_help()
        return 1
    img = Image(args.fw)
    insn = load_disasm(args.objdump, args.fw)
    ok = True
    if args.decode:
        print_decode(img, insn)
    if args.tables:
        print_tables(img)
    if args.probes:
        print_probes(img)
    if args.frames:
        print_frames(img)
    if args.selftest:
        ok = selftest(img, insn)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())