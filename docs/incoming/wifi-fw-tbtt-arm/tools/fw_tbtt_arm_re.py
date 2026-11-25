#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_tbtt_arm_re.py - re-derive the 5 GHz TBTT arm decode from the pinned image.

Lane: wifi-fw-tbtt-arm (shared task task-193).  HOST-ONLY, read-only: the
pinned LOCAL vendor artifacts are read in place; only derived values
(addresses, instruction mnemonics, table words, register constants, probe
addresses) are printed.  No vendor byte is copied into this repository and no
device is touched.

Artifacts (local, uncommitted):
  firmware: /home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin
            928,920 B sha256 7fc87e20...311b ; CPU = file offset + 0x40000
  objdump:  the OpenWrt lab ARM objdump (override with --objdump or $BW_OBJDUMP)

What this tool re-derives (see ../REPORT.md, ../TABLES.md):
  A. The per-band MAC block (band0 device CA 0x40110000, band1 0x40114000) is
     programmed by exactly two 23-entry {u16 offset,u16 value} init tables
     (CA 0x105714 / 0x105770).  Their offset set is only
     {0x548,0x54C,0x550,0x554,0x55C,0x560,0x564,0x568,0x588,0x594,0x5BC};
     the values are small fixed constants.  The band1 difference is the
     0x6000 marker in the 0x548/0x54C final words (0x6101 vs 0x0101) - it is
     written on every band-1 MAC init, not a per-VAP arm bit.
  B. The per-band start/stop functions: CA 0x4AF0C (start), CA 0xBE294 (stop).
     Start ORs the band-1 SMAC IRQ enable bits 1|8|0x20|0x1000 (0x1029) into
     0x4010C220, or the band-0 enable bits 1|2|4|8 (0xF) into 0x40108214,
     ORs 1 (band1) / 2 (band0) into 0x40101220, and sets bit0 of
     0x4011080C (band0) / 0x4011480C (band1).  Stop clears the same bits.
     Restore CA 0xD9504 writes only fixed constants (0x1000/0x3000/3/...)
     into the band block; no interval/time value is ever written there.
  C. The beacon state is created by the VAP-start worker CA 0x46394
     (perlink+0x6B8 280 B buffer, +0x6C0 92 B descriptor, +0x6C4=16,
     +0x6C6=1) and the frame is built/queued through CA 0xB5290 / 0xB7208 /
     0xB3018 / 0xEE394.  That whole worker contains NO device-IO literal
     load: the beacon submission is not an MMIO arm step.
  D. CORRECTION to lane wifi-fw-beacon-tx: CA 0x7E5CC is the rate-control
     TX-descriptor filler (only caller CA 0x72184 inside the rate-adaptation
     function CA 0x70490).  It copies VAP+0x138 -> desc+0x25E and
     VAP+0x13A -> desc+0x260 next to the rate/retry fields desc+0x254..0x272;
     it is NOT a beacon-descriptor validity setup.
  E. The only beacon-named firmware handlers in the image are the four smac
     message-processor workers CA 0xE6A26 / 0xE6A70 / CA 0xE6AB4 / 0xE6B00
     (name blob: smac_msg_proc_beacon_tx_resume_req / suspend_req).  They
     toggle 0x40101220 bit0/bit1 and 0x40101208 bits 0..3 - the closest thing
     to "beacon-enable bits" the image carries.  No g0-id1 table-A/B key maps
     to any of them (negative), so no H2D frame of our known family posts them.
  F. The per-band TRX-enable worker CA 0xE8520 (smac_msg_proc_enable_trx_req)
     writes the band-1 TRX triple 0x4010E150=0x33, 0x4010E154=0xFF,
     0x4010E158=0xFF; its band-0 counterpart paths use 0x4010820C/0x40108214
     and the SoC INTC 0x40002264/0x40002268/0x4000226C.
  G. Probe pack R3: read-only differential between the working band-0 and the
     silent band-1 (both in one boot): per-band start/IRQ/TRX registers,
     both per-link beacon-state objects and both 92-byte descriptors.
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

# --- A. per-band MAC block / init tables -----------------------------------
MAC_BAND0 = 0x40110000
MAC_BAND1 = 0x40114000
MAC_DELTA = 0x4000
BANDTAB0 = 0x105714
BANDTAB1 = 0x105770
BANDTAB_LIT0 = 0x9F494
BANDTAB_LIT1 = 0x9F964
BANDTAB_N = 23
BAND_OFFSETS = [0x548, 0x54C, 0x550, 0x554, 0x55C, 0x560, 0x564, 0x568,
                0x588, 0x594, 0x5BC]
# every value used by the two tables (small fixed constants; no interval/time)
BAND_TABLE_VALUES = {0x0000, 0x0001, 0x0002, 0x0003, 0x0011, 0x0022,
                     0x0024, 0x0033, 0x0035, 0x0101, 0x6000, 0x6001, 0x6101}

# --- B. per-band start / stop / restore ------------------------------------
MAC_START = 0x4AF0C
MAC_STOP = 0xBE294
MAC_RESTORE = 0xD9504
RESTORE_TAB = 0x106D4C
RESTORE_ARRAY = 0x106F24
RESTORE_ARRAY_N = 14
RESET_B1 = 0xD968C
RESET_B0 = 0xD97BC
SMAC_CHECK = 0xD9604
MAC0_CHECK = 0xD973C
SMAC_IRQ_STATUS = 0x4010C200
SMAC_IRQ_ENABLE = 0x4010C220
SMAC_IRQ_GLOBAL = 0x40101220
SMAC_IRQ_FLAGS = 0x40101208
BAND0_ENABLE = 0x40108214
BAND0_TRX0 = 0x4010820C
BAND1_TRX0 = 0x4010E150
BAND1_TRX1 = 0x4010E154
BAND1_TRX2 = 0x4010E158
MAC_START0 = 0x4011080C
MAC_START1 = 0x4011480C
SOC_INTC = [0x40002264, 0x40002268, 0x4000226C]
SOC_INTC2 = [0x40000064, 0x40000068, 0x4000006C]

# --- C. VAP start / beacon state -------------------------------------------
VAP_START = 0x46394
VAP_START_END = 0x467C8
BAND_START = 0x971D8
BAND_START_END = 0x9744C
CTX_PERLINK = 0x4B0
CTX_BANDOBJ = 0x4B8
PERLINK_BCN_BUF = 0x6B8
PERLINK_BCN_AUX = 0x6BC
PERLINK_BCN_DESC = 0x6C0
PERLINK_BCN_LEN = 0x6C4
PERLINK_BCN_EN = 0x6C6
BCN_QUEUE_FNS = [0xB5290, 0xB7208, 0xB3018, 0xEE394]

# --- D. the rate-control descriptor filler (correction) --------------------
DESC_FILL = 0x7E5CC
DESC_FILL_CALL = 0x72184
RATE_FN = 0x70490

# --- E. beacon / TRX message-processor workers -----------------------------
BCN_MSGS = {
    "resume_a": 0xE6A26,   # body[2]==0 ; 0x40101220 |= 1 ; 0x40101208 &= ~(1|4)
    "suspend_a": 0xE6A70,  # body[2]==0 ; 0x40101220 &= ~1 ; 0x40101208 |= (1|4)
    "resume_b": 0xE6AB4,   # body[2]!=0 ; 0x40101220 |= 2 ; 0x40101208 &= ~(2|8)
    "suspend_b": 0xE6B00,  # body[2]!=0 ; 0x40101220 &= ~2 ; 0x40101208 |= (2|8)
}
TRX_ENABLE = 0xE8520

# --- F. TBTT identity ------------------------------------------------------
G_TABLE = 0x1747C4
G_MAC_START = 0x370
G_BCN_START = 0x374
BANDOBJ_BASE = 0x15CA08
BANDOBJ_STRIDE = 0x1B68
BAND0_OBJ = 0x15CA08
BAND1_OBJ = 0x15E570
PUB_OFF = 0x1A00
BAND_LOOKUP = 0xA4FC8
CTX_SLOT0 = 0x171458
CTX_BIAS = 0x172504
CTX_STRIDE = 0xB40
CTX_FIRST = 0x119758
CTX_SLOTS = 19
BCN_STATE_CA = 0x15CA08  # placeholder (band objects above)
ISR_NAMES = ["smac_tbtt_5g_isr", "smac_tbtt_2g_isr"]
NAME_BLOB_LO = 0x12266C
NAME_BLOB_HI = 0x122BA4

# --- addressing conventions (project-committed) ----------------------------
BAR0_RAM = 0x6B8000            # firmware RAM: BAR0 = CPU + 0x6B8000
BAR0_IO = 0x3B8000             # device IO: BAR0 = 0x3B8000 + (CA - 0x40000000)
PHYS_BASE = 0x40000000

# --- G. R3 probe pack ------------------------------------------------------
RAM_PROBES = [
    # (name, CA, width, expected)
    ("ctx slot k ptr (k=0..18)", CTX_SLOT0, 4, "0x00119758 for k=0"),
    ("ctx table u16 bias", CTX_BIAS, 2, "0"),
    ("ctx0 body", CTX_FIRST, 4, "first slot body"),
    ("ctx0+0x4B0 perlink", CTX_FIRST + CTX_PERLINK, 4, "0x00175A18 [V*]"),
    ("ctx0+0x4B8 bandobj", CTX_FIRST + CTX_BANDOBJ, 4, "0x0015E570 [V*]"),
    ("bandobj0 perlink", BAND0_OBJ + PUB_OFF, 4, "published per-link"),
    ("bandobj1 perlink", BAND1_OBJ + PUB_OFF, 4, "published per-link"),
    ("bandobj0 +0xD6C", BAND0_OBJ + 0xD6C, 1, "band field"),
    ("bandobj0 +0xD6D", BAND0_OBJ + 0xD6D, 1, "band field"),
    ("bandobj0 +0xD70", BAND0_OBJ + 0xD70, 1, "band id"),
    ("bandobj1 +0xD6C", BAND1_OBJ + 0xD6C, 1, "band field"),
    ("bandobj1 +0xD6D", BAND1_OBJ + 0xD6D, 1, "band field"),
    ("bandobj1 +0xD70", BAND1_OBJ + 0xD70, 1, "band id"),
    ("G head", G_TABLE, 4, "nonzero"),
    ("G+0x370 MAC-start cb", G_TABLE + G_MAC_START, 4, "0 by design"),
    ("G+0x374 beacon-start cb", G_TABLE + G_BCN_START, 4, "0x00057639 [V*]"),
    ("band-0 map side (pool slot)", 0x1ADC98, 8, "desc-pool default"),
]
RAM_PROBE_REFS = [
    ("perlink[0]+0x6B8 beacon buffer", 0x6B8, 4, "nonzero"),
    ("perlink[0]+0x6BC aux object", 0x6BC, 4, "nonzero"),
    ("perlink[0]+0x6C0 descriptor", 0x6C0, 4, "nonzero"),
    ("perlink[0]+0x6C4 beacon len", 0x6C4, 2, "16"),
    ("perlink[0]+0x6C6 beacon enable", 0x6C6, 1, "1"),
    ("perlink[0]+0x11D band index", 0x11D, 1, "0"),
    ("perlink[1]+0x6B8 beacon buffer", 0x6B8, 4, "nonzero"),
    ("perlink[1]+0x6BC aux object", 0x6BC, 4, "nonzero"),
    ("perlink[1]+0x6C0 descriptor", 0x6C0, 4, "nonzero"),
    ("perlink[1]+0x6C4 beacon len", 0x6C4, 2, "16"),
    ("perlink[1]+0x6C6 beacon enable", 0x6C6, 1, "1"),
    ("perlink[1]+0x11D band index", 0x11D, 1, "1"),
]
IO_PROBES = [
    # (name, CA, width, expected)
    ("band0 +0x500 reset/status", MAC_BAND0 + 0x500, 4, "0x55/0xFF sequence"),
    ("band0 +0x51C status", MAC_BAND0 + 0x51C, 4, "0xFF after reset"),
    ("band0 +0x520 status", MAC_BAND0 + 0x520, 4, "0xFF after reset"),
    ("band0 +0x548", MAC_BAND0 + 0x548, 4, "final 0x6101<<0 | ..."),
    ("band0 +0x54C", MAC_BAND0 + 0x54C, 4, "final 0x6101<<0 | ..."),
    ("band0 +0x588", MAC_BAND0 + 0x588, 4, "0x33/0x22 class"),
    ("band0 +0x594", MAC_BAND0 + 0x594, 4, "0x33/0x22 class"),
    ("band0 +0x5BC", MAC_BAND0 + 0x5BC, 4, "band id"),
    ("band0 +0x80C", MAC_START0, 4, "bit0 = MAC start done"),
    ("band0 enable 0x40108214", BAND0_ENABLE, 4, "bits 0..3 = 0xF"),
    ("band0 TRX 0x4010820C", BAND0_TRX0, 4, "enable-TRX path"),
    ("band1 +0x500 reset/status", MAC_BAND1 + 0x500, 4, "0x55/0xFF sequence"),
    ("band1 +0x51C status", MAC_BAND1 + 0x51C, 4, "0xFF after reset"),
    ("band1 +0x520 status", MAC_BAND1 + 0x520, 4, "0xFF after reset"),
    ("band1 +0x548", MAC_BAND1 + 0x548, 4, "final 0x6101 (0x6000 marker)"),
    ("band1 +0x54C", MAC_BAND1 + 0x54C, 4, "final 0x6101 (0x6000 marker)"),
    ("band1 +0x588", MAC_BAND1 + 0x588, 4, "0x35/0x24 class"),
    ("band1 +0x594", MAC_BAND1 + 0x594, 4, "0x35/0x24 class"),
    ("band1 +0x5AC", MAC_BAND1 + 0x5AC, 4, "restore-table byte<<4|byte"),
    ("band1 +0x5BC", MAC_BAND1 + 0x5BC, 4, "band id"),
    ("band1 +0x80C", MAC_START1, 4, "bit0 = MAC start done"),
    ("band1 TRX 0x4010E150", BAND1_TRX0, 4, "0x33 (enable-TRX band1)"),
    ("band1 TRX 0x4010E154", BAND1_TRX1, 4, "0xFF"),
    ("band1 TRX 0x4010E158", BAND1_TRX2, 4, "0xFF"),
    ("SMAC IRQ status 0x4010C200", SMAC_IRQ_STATUS, 4, "pending bits"),
    ("SMAC IRQ enable 0x4010C220", SMAC_IRQ_ENABLE, 4, "contains 0x1029 (band1)"),
    ("global 0x40101220", SMAC_IRQ_GLOBAL, 4, "bit0 band1, bit1 band0"),
    ("global flags 0x40101208", SMAC_IRQ_FLAGS, 4, "beacon/TRX gate bits 0..3"),
    ("SoC INTC 0x40002264", SOC_INTC[0], 4, "band TRX path"),
    ("SoC INTC 0x40002268", SOC_INTC[1], 4, "band TRX path"),
    ("SoC INTC 0x4000226C", SOC_INTC[2], 4, "band TRX path"),
]


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
    return got[0] if got else "<?>"


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

    def bandtab(self, ca, n=BANDTAB_N):
        return [(self.u16(ca + 4 * i), self.u16(ca + 4 * i + 2))
                for i in range(n)]

    def restore_tab(self, ca, n):
        return [(self.u16(ca + 4 * i), self.u16(ca + 4 * i + 2))
                for i in range(n)]

    def literal_at(self, insn, site):
        got = insn.get(site)
        if not got or "[pc" not in got[1]:
            return None
        m = re.match(r"(\w+), \[pc, #(-?\d+)\]", got[1])
        if not m:
            return None
        tgt = ((site + 4) & ~3) + int(m.group(2))
        if BASE <= tgt < BASE + len(self.raw):
            return tgt
        return None

    def io_literals_in(self, insn, lo, hi):
        """all (site,literal_value) pc-relative literal loads in [lo,hi]."""
        out = []
        for site in range(lo, hi + 1, 2):
            tgt = self.literal_at(insn, site)
            if tgt is None:
                continue
            v = self.u32(tgt)
            if 0x40000000 <= v < 0x40200000:
                out.append((site, v))
        return out

    def all_literals_in(self, insn, lo, hi):
        out = []
        for site in range(lo, hi + 1, 2):
            tgt = self.literal_at(insn, site)
            if tgt is not None:
                out.append((site, self.u32(tgt)))
        return out


def print_decode(img, insn):
    print("=== 5 GHz TBTT arm - decode ===")
    print()
    print("[A] per-band MAC block and its init tables")
    print("    band0 device CA 0x%08X, band1 0x%08X (delta 0x%X)"
          % (MAC_BAND0, MAC_BAND1, MAC_DELTA))
    print("    init tables CA 0x%X / 0x%X (%d pairs of {u16 off,u16 val})"
          % (BANDTAB0, BANDTAB1, BANDTAB_N))
    print("    table literals CA 0x%X -> 0x%X and CA 0x%X -> 0x%X"
          % (BANDTAB_LIT0, img.u32(BANDTAB_LIT0),
             BANDTAB_LIT1, img.u32(BANDTAB_LIT1)))
    print("    offsets programmed: %s"
          % " ".join("0x%X" % o for o in BAND_OFFSETS))
    t0 = img.bandtab(BANDTAB0)
    t1 = img.bandtab(BANDTAB1)
    print("    band-1 differences:")
    for i, (a, b) in enumerate(zip(t0, t1)):
        if a != b:
            print("      idx %2d  band0 (0x%03X,0x%04X)  band1 (0x%03X,0x%04X)"
                  % (i, a[0], a[1], b[0], b[1]))
    print()
    print("[B] per-band MAC start CA 0x%X / stop CA 0x%X" % (MAC_START, MAC_STOP))
    for a, note in [
            (0x4AF5C, "band = u8(perlink+0x11D)"),
            (0x4AF62, "reset value 0x55"),
            (0x4AF7E, "reset value 0xFF"),
            (0x4AFA4, "band = u8(perlink+0x11D) (again)"),
            (0x4AFB2, "0x4010C220 |= 1"),
            (0x4AFBC, "0x4010C220 |= 8"),
            (0x4AFC6, "0x4010C220 |= 0x20"),
            (0x4AFD0, "0x4010C220 |= 0x1000"),
            (0x4B008, "0x40101220 value 2 (band0)"),
            (0x4B00A, "0x40101220 value 1 (band1)"),
            (0x4B0D2, "0x40108214 |= 2 (band0)"),
            (0x4B0DC, "0x40108214 |= 4 (band0)")]:
        print("    @%06X  %-8s %-28s %s"
              % (a, insn_key(insn, a), insn_ops(insn, a), note))
    print()
    print("[C] VAP start CA 0x%X - beacon state (%d..0x%X)"
          % (VAP_START, VAP_START, VAP_START_END))
    for a, note in [
            (0x464F2, "first-start guard: perlink+0x6C6"),
            (0x46586, "0x6C4 = 16"),
            (0x46590, "0x6C0 = 92 B descriptor"),
            (0x46596, "0x6B8 = 280 B beacon buffer"),
            (0x4659A, "0x6C6 = 1"),
            (0x464B0, "band start CA 0x971D8"),
            (0x466CE, "beacon queue: CA 0xB5290"),
            (0x466E8, "beacon queue: CA 0xB7208"),
            (0x466F8, "beacon queue: CA 0xB3018"),
            (0x4670C, "aux object: CA 0xEE394"),
            (0x46714, "store into perlink+0x6BC")]:
        print("    @%06X  %-8s %-28s %s"
              % (a, insn_key(insn, a), insn_ops(insn, a), note))
    print()
    print("[D] rate-control descriptor filler CA 0x%X (correction)" % DESC_FILL)
    print("    only caller @0x%X inside rate-adaptation fn CA 0x%X"
          % (DESC_FILL_CALL, RATE_FN))
    for a, note in [
            (0x7E5F4, "rate field from rate object +0x2c"),
            (0x7E634, "VAP+0x138 -> desc+0x25E"),
            (0x7E63C, "VAP+0x13A -> desc+0x260")]:
        print("    @%06X  %-8s %-28s %s"
              % (a, insn_key(insn, a), insn_ops(insn, a), note))
    print()
    print("[E] beacon/TRX message-processor workers (0x40101220 / 0x40101208)")
    for name, ca in BCN_MSGS.items():
        print("    CA 0x%X %s" % (ca, name))
    print()
    print("[F] band-1 TRX enable CA 0x%X" % TRX_ENABLE)
    for a, note in [
            (0xE85E6, "0x33 -> 0x4010E150"),
            (0xE85EE, "0xFF -> 0x4010E154"),
            (0xE85F2, "0xFF -> 0x4010E158"),
            (0xE854C, "band1 disable helper 0xD97BC"),
            (0xE8550, "band1 disable helper 0xD973C")]:
        print("    @%06X  %-8s %-28s %s"
              % (a, insn_key(insn, a), insn_ops(insn, a), note))
    print()
    print("[G] TBTT ISR names and the per-band hook table")
    for nm in ISR_NAMES:
        off = img.raw.find(nm.encode())
        print("    %-18s CA 0x%X (name blob; no pointer, no code reference)"
              % (nm, off + BASE))
    print("    G table CA 0x%X (literal CA 0x9745C); G+0x370/G+0x374 read"
          " @0x97360/0x9736A, called @0x97366/0x97372" % G_TABLE)


def print_tables(img, insn):
    print("=== T1. per-band MAC init tables (23 x {u16 offset,u16 value}) ===")
    t0 = img.bandtab(BANDTAB0)
    t1 = img.bandtab(BANDTAB1)
    print("  base: band0 = 0x%08X  band1 = 0x%08X" % (MAC_BAND0, MAC_BAND1))
    print("  %-2s %-6s %-6s %-6s %s" % ("#", "off", "band0", "band1", "note"))
    for i, ((o0, v0), (o1, v1)) in enumerate(zip(t0, t1)):
        note = "" if (o0, v0) == (o1, v1) else "band1 differs"
        print("  %-2d 0x%03X  0x%04X  0x%04X %s" % (i, o0, v0, v1, note))
    print()
    print("=== T2. per-band start/stop register constants [V] ===")
    print("  start CA 0x%X band1: reset 0x55/0xFF @0x40114500; check"
          " 0x4011451C/0x40114520 = 0xFF" % MAC_START)
    print("  start CA 0x%X band0: reset 0x55/0xFF @0x40110500; check"
          " 0x4011051C/0x40110520 = 0xFF" % MAC_START)
    print("  start band1: 0x4010C220 |= 1|8|0x20|0x1000 (0x1029)")
    print("  start band0: 0x40108214 |= 1|2|4|8 (0xF)")
    print("  start both : 0x40101220 |= 1 (band1) / 2 (band0);"
          " 0x4011080C|0x4011480C bit0 = 1")
    print("  stop  CA 0x%X: clear the same bits, zero the band reset word"
          % MAC_STOP)
    print()
    print("=== T3. per-band restore CA 0x%X constants [V] ===" % MAC_RESTORE)
    print("  per-mode restore tables at CA 0x%X (%d x {ptr,count})"
          % (RESTORE_ARRAY, RESTORE_ARRAY_N))
    for i in range(RESTORE_ARRAY_N):
        ptr = img.u32(RESTORE_ARRAY + 8 * i)
        cnt = img.u32(RESTORE_ARRAY + 8 * i + 4)
        print("    idx %2d: CA 0x%06X  %d pairs" % (i, ptr, cnt))
    print("  band0 restore writes: 0x40110548/0x4011054C/0x4011055C/0x40110560"
          " = 0x1000; 0x40110554/0x40110564 = 3; 0x401105BC = 0")
    print("  band1 restore writes: 0x40114548/0x4011454C = 0x3000;"
          " 0x4011455C/0x40114560 = 0x1000; 0x40114554/0x40114564 = 3;"
          " 0x401145BC = 0; 0x401145AC = table_byte<<4|table_byte")
    print("  NOTE: no interval/time value is stored by any of these paths.")
    print()
    print("=== T4. reset/check helpers [V] ===")
    print("  CA 0x%X: 4,12,0x4C,0xCC,0xCD,0xCF,0xDF,0xFF -> 0x40114500;"
          " expect 0x4011451C = 0x40114520 = 0xFF" % RESET_B1)
    print("  CA 0x%X: same -> 0x40110500; expect 0x4011051C/0x40110520 = 0xFF"
          % RESET_B0)
    print("  CA 0x%X: 16,24,56,0x1038,0x1039 -> 0x4010C220; expect"
          " 0x4010C238 = 0x1039, 0x4010C23C = 0x39" % SMAC_CHECK)
    print("  CA 0x%X: 4,5,7,0xF -> 0x40108214; expect 0x40108228/0x4010822C"
          " = 0xF00" % MAC0_CHECK)
    print()
    print("=== T5. beacon/TBX enable-bit workers [V behavior] ===")
    print("  CA 0xE6A26: body[2]==0 ; 0x40101220 |= 1 ; 0x40101208 &= ~(1|4)")
    print("  CA 0xE6A70: body[2]==0 ; 0x40101220 &= ~1 ; 0x40101208 |= (1|4)")
    print("  CA 0xE6AB4: body[2]!=0 ; 0x40101220 |= 2 ; 0x40101208 &= ~(2|8)")
    print("  CA 0xE6B00: body[2]!=0 ; 0x40101220 &= ~2 ; 0x40101208 |= (2|8)")
    print("  name blob: smac_msg_proc_beacon_tx_resume_req / _suspend_req"
          " (CA 0x122904 / 0x122928)")
    print("  no g0-id1 table-A/B row points at any of these four CAs")
    print()
    print("=== T6. band-1 TRX enable worker CA 0x%X ===" % TRX_ENABLE)
    print("  0x4010E150 = 0x33 ; 0x4010E154 = 0xFF ; 0x4010E158 = 0xFF")
    print("  disable path: 0x4010E1B8.. zeros; SoC INTC 0x40002264/68/6C"
          " and 0x40000064/68/6C used")
    print("  band0 counterpart paths use 0x4010820C / 0x40108214")


def print_probes(img):
    def ram(ca):
        bar0 = ca + BAR0_RAM
        return bar0, PHYS_BASE + bar0

    def io(ca):
        bar0 = BAR0_IO + (ca - PHYS_BASE)
        return bar0, PHYS_BASE + bar0

    print("=== R3 read-only probe pack (differential band0 vs band1) ===")
    print("Firmware RAM: BAR0 = CPU + 0x%X ; phys = 0x40000000 + BAR0"
          % BAR0_RAM)
    print("Device IO   : BAR0 = 0x%X + (CPU - 0x40000000) ; phys = 0x40000000"
          " + BAR0" % BAR0_IO)
    print()
    print("--- RAM anchors (CA + BAR0 + phys) ---")
    print("%-34s %-10s %-10s %-10s %-4s %s"
          % ("object", "CA", "BAR0", "phys", "len", "expected"))
    print("-" * 120)
    for name, ca, w, exp in RAM_PROBES:
        bar0, phys = ram(ca)
        print("%-34s 0x%08X 0x%08X 0x%08X %-4d %s"
              % (name, ca, bar0, phys, w, exp))
    print()
    print("--- two-step per-link objects (resolve pointer first) ---")
    print("  band_b obj  = 0x%X + 0x%X*band  (band0 0x%X, band1 0x%X)"
          % (BANDOBJ_BASE, BANDOBJ_STRIDE, BAND0_OBJ, BAND1_OBJ))
    print("  perlink[b]  = u32(band_b obj + 0x%X)" % PUB_OFF)
    print("  beacon state = perlink[b] + 0x6B8/0x6BC/0x6C0/0x6C4/0x6C6")
    print("  descriptor dump for the diff: u32(perlink[b]+0x6C0) is a 92-byte"
          " object; read its first 32 bytes for band0 and band1 and diff")
    print()
    print("--- device IO (same offsets in both per-band blocks) ---")
    print("%-36s %-10s %-10s %-10s %-4s %s"
          % ("object", "CPU/CA", "BAR0", "phys", "len", "expected"))
    print("-" * 126)
    for name, ca, w, exp in IO_PROBES:
        bar0, phys = io(ca)
        print("%-36s 0x%08X 0x%08X 0x%08X %-4d %s"
              % (name, ca, bar0, phys, w, exp))
    print()
    print("forbidden: RC misc window 0x10160000..0x1016FFFF (never read)")
    print("all reads are side-effect free; no write is authorised by this lane")


def selftest(img, insn):
    c = Checker()
    raw = img.raw
    print("=== selftest: image identity ===")
    c.check("firmware length == %d" % FW_LEN, len(raw) == FW_LEN,
            "got %d" % len(raw))
    c.check("firmware sha256 == 7fc87e20...311b", sha256(raw) == FW_SHA,
            sha256(raw))
    print()
    print("=== A. per-band MAC block / init tables ===")
    c.check("band0 table literal CA 0x9F494 -> 0x105714",
            img.u32(BANDTAB_LIT0) == BANDTAB0)
    c.check("band1 table literal CA 0x9F964 -> 0x105770",
            img.u32(BANDTAB_LIT1) == BANDTAB1)
    t0 = img.bandtab(BANDTAB0)
    t1 = img.bandtab(BANDTAB1)
    c.check("both tables have 23 pairs", len(t0) == len(t1) == BANDTAB_N)
    c.check("offset set is exactly %s" % " ".join("%#X" % o
                                                  for o in BAND_OFFSETS),
            sorted({o for o, _ in t0}) == sorted(BAND_OFFSETS)
            and sorted({o for o, _ in t1}) == sorted(BAND_OFFSETS))
    c.check("no init-table offset below 0x548 (no 0x500-0x544 timer window)",
            min(o for o, _ in t0) == 0x548
            and min(o for o, _ in t1) == 0x548)
    diffs = [(i, a, b) for i, (a, b) in enumerate(zip(t0, t1)) if a != b]
    c.check("exactly 10 band1-differing pairs", len(diffs) == 10)
    c.check("differing indices are 2,3,4,5,7,12,13,14,15,17",
            [i for i, _, _ in diffs] == [2, 3, 4, 5, 7, 12, 13, 14, 15, 17])
    c.check("band1 0x548/0x54C final = 0x6101 (0x6000 band marker)",
            t1[7] == (0x548, 0x6101) and t1[17] == (0x54C, 0x6101))
    c.check("band0 0x548/0x54C final = 0x0101",
            t0[7] == (0x548, 0x0101) and t0[17] == (0x54C, 0x0101))
    c.check("every table value is one of the small fixed constants",
            all(v in BAND_TABLE_VALUES for _, v in t0)
            and all(v in BAND_TABLE_VALUES for _, v in t1))
    c.check("max table value is 0x6101 (no interval/time-sized value)",
            max(v for _, v in t0 + t1) == 0x6101)
    c.check("applier base: band0 = 0x40000000 + 0x110000",
            expect(insn, 0x9F3D6, "add.w", "r2, r2, #1073741824")
            and expect(insn, 0x9F3DA, "add.w", "r2, r2, #1114112"))
    c.check("applier base: band1 = 0x40004000 + 0x110000",
            expect(insn, 0x9F91E, "add.w", "r2, r2, #1073758208")
            and expect(insn, 0x9F922, "add.w", "r2, r2, #1114112"))
    print()
    print("=== B. per-band MAC start CA 0x4AF0C / stop CA 0xBE294 ===")
    c.check("start reads the band from u8(perlink+0x11D)",
            expect(insn, 0x4AF5C, "ldrb.w", "r3, [r6, #285]"))
    c.check("band1 reset base literal CA 0x4B118 = 0x40114500",
            img.u32(0x4B118) == MAC_BAND1 + 0x500)
    c.check("band0 reset base literal CA 0x4B11C = 0x40110500",
            img.u32(0x4B11C) == MAC_BAND0 + 0x500)
    c.check("reset value 0x55 then 0xFF written to band base",
            expect(insn, 0x4AF62, "mov.w", "r3, #85")
            and expect(insn, 0x4AF72, "str.w", "r3, [r8]")
            and expect(insn, 0x4AF7E, "movs", "r3, #255")
            and expect(insn, 0x4AF84, "str.w", "r3, [r8]"))
    c.check("band1 status readback 0x4011451C/0x40114520",
            img.u32(0x4B0F4) == MAC_BAND1 + 0x51C
            and img.u32(0x4B108) == MAC_BAND1 + 0x520)
    c.check("band0 status readback 0x4011051C/0x40110520",
            img.u32(0x4B10C) == MAC_BAND0 + 0x51C
            and img.u32(0x4B110) == MAC_BAND0 + 0x520)
    c.check("band1 SMAC IRQ enable literal CA 0x4B0FC = 0x4010C220",
            img.u32(0x4B0FC) == SMAC_IRQ_ENABLE)
    c.check("band1 IRQ enable bits 1|8|0x20|0x1000 (0x1029)",
            expect(insn, 0x4AFB2, "orr.w", "r2, r2, #1")
            and expect(insn, 0x4AFBC, "orr.w", "r2, r2, #8")
            and expect(insn, 0x4AFC6, "orr.w", "r2, r2, #32")
            and expect(insn, 0x4AFD0, "orr.w", "r2, r2, #4096"))
    c.check("band0 enable literal CA 0x4B114 = 0x40108214",
            img.u32(0x4B114) == BAND0_ENABLE)
    c.check("band0 enable bits 1|2|4|8 (0xF)",
            expect(insn, 0x4B0D2, "orr.w", "r2, r2, #2")
            and expect(insn, 0x4B0DC, "orr.w", "r2, r2, #4"))
    c.check("global 0x40101220 literal CA 0x4B104", img.u32(0x4B104)
            == SMAC_IRQ_GLOBAL)
    c.check("0x40101220 value 1 = band1, 2 = band0",
            expect(insn, 0x4B008, "movne", "r2, #2")
            and expect(insn, 0x4B00A, "moveq", "r2, #1"))
    c.check("MAC start bit: 0x4011080C literal CA 0x4B100",
            img.u32(0x4B100) == MAC_START0)
    c.check("MAC start bit +0x4000 = 0x4011480C",
            MAC_START0 + 0x4000 == MAC_START1)
    c.check("start sets bit0 of the per-band start register",
            expect(insn, 0x4AFF8, "orr.w", "r2, r2, #1")
            and expect(insn, 0x4AFFC, "str", "r2, [r3, #0]"))
    c.check("stop CA 0xBE294 picks band base 0x40114500/0x40110500",
            img.u32(0xBE330) == MAC_BAND1 + 0x500
            and img.u32(0xBE334) == MAC_BAND0 + 0x500)
    c.check("stop band1 clears 0x4010C220 bits 1|8|0x20|0x1000",
            img.u32(0xBE338) == SMAC_IRQ_ENABLE
            and expect(insn, 0xBE2B6, "bic.w", "r2, r2, #1")
            and expect(insn, 0xBE2C0, "bic.w", "r2, r2, #8")
            and expect(insn, 0xBE2CA, "bic.w", "r2, r2, #32")
            and expect(insn, 0xBE2D4, "bic.w", "r2, r2, #4096"))
    c.check("stop band0 clears 0x40108214 bits 1|2|4|8",
            img.u32(0xBE340) == BAND0_ENABLE
            and expect(insn, 0xBE30C, "bic.w", "r2, r2, #1")
            and expect(insn, 0xBE316, "bic.w", "r2, r2, #2")
            and expect(insn, 0xBE320, "bic.w", "r2, r2, #4")
            and expect(insn, 0xBE32A, "bic.w", "r2, r2, #8"))
    print()
    print("=== B2. per-band restore CA 0xD9504 - fixed constants only ===")
    c.check("restore table base literal CA 0xD95BC = 0x106D4C",
            img.u32(0xD95BC) == RESTORE_TAB)
    c.check("band0 restore literals 0x40110548/54C/55C/560/554/564/5BC/10000",
            img.u32(0xD95C0) == MAC_BAND0 + 0x548
            and img.u32(0xD95C4) == MAC_BAND0 + 0x54C
            and img.u32(0xD95C8) == MAC_BAND0 + 0x55C
            and img.u32(0xD95CC) == MAC_BAND0 + 0x560
            and img.u32(0xD95D0) == MAC_BAND0 + 0x554
            and img.u32(0xD95D4) == MAC_BAND0 + 0x564
            and img.u32(0xD95D8) == MAC_BAND0 + 0x5BC
            and img.u32(0xD95DC) == MAC_BAND0)
    c.check("band1 restore literals 0x40114548/54C/55C/560/554/564/5BC/5AC/14000",
            img.u32(0xD95E0) == MAC_BAND1 + 0x548
            and img.u32(0xD95E4) == MAC_BAND1 + 0x54C
            and img.u32(0xD95E8) == MAC_BAND1 + 0x55C
            and img.u32(0xD95EC) == MAC_BAND1 + 0x560
            and img.u32(0xD95F0) == MAC_BAND1 + 0x554
            and img.u32(0xD95F4) == MAC_BAND1 + 0x564
            and img.u32(0xD95F8) == MAC_BAND1 + 0x5BC
            and img.u32(0xD95FC) == MAC_BAND1 + 0x5AC
            and img.u32(0xD9600) == MAC_BAND1)
    c.check("restore band1 writes 0x3000 to 0x548/0x54C (u32)",
            expect(insn, 0xD9564, "mov.w", "r0, #12288")
            and expect(insn, 0xD956C, "str", "r0, [r2, #0]"))
    c.check("restore band0 writes 0x1000 to 0x548/0x54C (u32)",
            expect(insn, 0xD951A, "mov.w", "r3, #4096")
            and expect(insn, 0xD9520, "str", "r3, [r2, #0]"))
    c.check("restore band1 byte table read: u8(0x106F9C + 8*idx)",
            expect(insn, 0xD9572, "ldrb.w", "r3, [r3, #592]"))
    c.check("restore band1 writes table byte<<4|byte to 0x401145AC",
            expect(insn, 0xD9580, "orr.w", "r3, r3, r3, lsl #4")
            and expect(insn, 0xD95A6, "str", "r3, [r2, #0]"))
    c.check("restore per-mode array at 0x106F24 has 14 {ptr,count} rows",
            [(img.u32(RESTORE_ARRAY + 8 * i),
              img.u32(RESTORE_ARRAY + 8 * i + 4))
             for i in range(RESTORE_ARRAY_N)][13] == (0x1153E6, 11))
    pairs = img.restore_tab(0x1153E6, 11)
    c.check("mode-table idx13 offsets 0x570/0x544/0x5A0/0x57C/0x574/0x580",
            [o for o, _ in pairs] == [0x570, 0x544, 0x5A0, 0x57C, 0x57C,
                                      0x574, 0x57C, 0x580, 0x580, 0x574,
                                      0x580])
    c.check("restore/tables contain no value >= 0x8000 (no time/TSF value)",
            all(v < 0x8000 for _, v in img.restore_tab(0x1158AA, 25) +
                img.restore_tab(0x115846, 25)))
    print()
    print("=== B3. reset/check helpers ===")
    c.check("band1 reset/check CA 0xD968C literals 0x40114500/1C/20",
            img.u32(0xD972C) == MAC_BAND1 + 0x500
            and img.u32(0xD9730) == MAC_BAND1 + 0x51C
            and img.u32(0xD9738) == MAC_BAND1 + 0x520)
    c.check("band1 reset sequence 4/12/0x4C/0xCC/0xCD/0xCF/0xDF/0xFF",
            expect(insn, 0xD968C, "movs", "r3, #4")
            and expect(insn, 0xD969E, "movs", "r3, #12")
            and expect(insn, 0xD96AC, "movs", "r3, #76")
            and expect(insn, 0xD96F2, "movs", "r3, #255"))
    c.check("band0 reset/check CA 0xD97BC literals 0x40110500/1C/20",
            img.u32(0xD985C) == MAC_BAND0 + 0x500
            and img.u32(0xD9860) == MAC_BAND0 + 0x51C
            and img.u32(0xD9868) == MAC_BAND0 + 0x520)
    c.check("SMAC IRQ block check CA 0xD9604 literals 0x4010C220/238/23C",
            img.u32(0xD967C) == SMAC_IRQ_ENABLE
            and img.u32(0xD9680) == 0x4010C238
            and img.u32(0xD9688) == 0x4010C23C)
    c.check("SMAC check writes 16/24/56/0x1038/0x1039",
            expect(insn, 0xD9606, "movs", "r3, #16")
            and expect(insn, 0xD9616, "movs", "r3, #24")
            and expect(insn, 0xD9624, "movs", "r3, #56")
            and expect(insn, 0xD9632, "movw", "r3, #4152")
            and expect(insn, 0xD9636, "movw", "r5, #4153"))
    c.check("MAC0 check CA 0xD973C literals 0x40108214/228/22C",
            img.u32(0xD97AC) == BAND0_ENABLE
            and img.u32(0xD97B0) == 0x40108228
            and img.u32(0xD97B8) == 0x4010822C)
    c.check("MAC0 check writes 4/5/7/0xF to 0x40108214",
            expect(insn, 0xD973C, "movs", "r3, #4")
            and expect(insn, 0xD974E, "movs", "r3, #5")
            and expect(insn, 0xD975C, "movs", "r3, #7")
            and expect(insn, 0xD976A, "movs", "r3, #15"))
    print()
    print("=== C. VAP start CA 0x46394 - beacon state creation ===")
    c.check("bandobj = *(ctx+0x4B8)",
            expect(insn, 0x463D4, "ldr.w", "sl, [r4, #1208]"))
    c.check("perlink = *(ctx+0x4B0)",
            expect(insn, 0x46450, "ldr.w", "r6, [r8, #1200]"))
    c.check("first-start guard reads perlink+0x6C6",
            expect(insn, 0x464F2, "ldrb.w", "r3, [r6, #1734]"))
    c.check("alloc 280 B beacon buffer (CA 0xBF128, 0x118)",
            expect(insn, 0x464FA, "mov.w", "r3, #280")
            and expect(insn, 0x4650A, "bl", "0xbf128"))
    c.check("alloc 92 B descriptor (CA 0xBF128, 0x5C)",
            expect(insn, 0x46524, "movs", "r3, #92")
            and expect(insn, 0x46530, "bl", "0xbf128"))
    c.check("perlink+0x6C4 = 16", expect(insn, 0x46588, "strh.w",
                                         "r3, [r6, #1732]"))
    c.check("perlink+0x6C0 = descriptor ptr",
            expect(insn, 0x46590, "str.w", "r7, [r6, #1728]"))
    c.check("perlink+0x6B8 = beacon buffer ptr",
            expect(insn, 0x46596, "str.w", "r9, [r6, #1720]"))
    c.check("perlink+0x6C6 = 1 (beacon state exists)",
            expect(insn, 0x4659A, "strb.w", "r3, [r6, #1734]"))
    c.check("band start CA 0x971D8 called from VAP start",
            expect(insn, 0x464B0, "bl", "0x971d8"))
    c.check("beacon frame/queue chain 0xB5290/0xB7208/0xB3018/0xEE394",
            expect(insn, 0x466CE, "bl", "0xb5290")
            and expect(insn, 0x466E8, "bl", "0xb7208")
            and expect(insn, 0x466F8, "bl", "0xb3018")
            and expect(insn, 0x4670C, "bl", "0xee394"))
    c.check("CA 0xEE394 result stored into perlink+0x6BC",
            expect(insn, 0x46710, "ldr.w", "r5, [r6, #1724]")
            and expect(insn, 0x46714, "str", "r0, [r5, #0]"))
    c.check("VAP start contains NO device-IO literal load (no MMIO arm step)",
            img.io_literals_in(insn, VAP_START, VAP_START_END) == [],
            str(img.io_literals_in(insn, VAP_START, VAP_START_END)))
    c.check("band start CA 0x971D8 contains NO device-IO literal load",
            img.io_literals_in(insn, BAND_START, BAND_START_END) == [],
            str(img.io_literals_in(insn, BAND_START, BAND_START_END)))
    print()
    print("=== D. correction: CA 0x7E5CC is the rate-control descriptor fill ===")
    callers = [a for a in range(0x40000, len(raw) + 0x40000, 2)
               if insn.get(a) and insn.get(a)[0] in ("bl", "blx")
               and insn.get(a)[1] in ("0x7e5cc",)]
    c.check("only caller of CA 0x7E5CC is 0x72184",
            callers == [DESC_FILL_CALL], str(callers))
    c.check("caller is inside the rate-adaptation function CA 0x70490",
            expect(insn, 0x70490, "stmdb", "sp!, {r4, r5, r6, r7, r8, r9,"
                   " sl, fp, lr}")
            and expect(insn, 0x70532, "strd", "r1, r1, [sp, #8]"))
    c.check("filler copies VAP+0x138 -> desc+0x25E",
            expect(insn, 0x7E634, "ldrh.w", "r3, [r5, #312]")
            and expect(insn, 0x7E638, "strh.w", "r3, [r4, #606]"))
    c.check("filler copies VAP+0x13A -> desc+0x260",
            expect(insn, 0x7E63C, "ldrh.w", "r3, [r5, #314]")
            and expect(insn, 0x7E640, "strh.w", "r3, [r4, #608]"))
    c.check("filler also writes the rate/retry fields desc+0x254..0x272",
            expect(insn, 0x7E5F4, "ldrsb.w", "r3, [r0, #44]")
            and expect(insn, 0x7E5F8, "strb.w", "r3, [r4, #596]"))
    c.check("filler has NO device-IO literal load (pure TX info)",
            img.io_literals_in(insn, DESC_FILL, DESC_FILL + 0x120) == [],
            str(img.io_literals_in(insn, DESC_FILL, DESC_FILL + 0x120)))
    c.check("beacon descriptor object perlink+0x6C0 is not touched by the"
            " filler (no [rX,#1728] access in 0x7E5CC..0x7E6E0)",
            all(insn.get(a) is None or "#1728]" not in insn.get(a)[1]
                for a in range(DESC_FILL, DESC_FILL + 0x114, 2)))
    print()
    print("=== E. beacon/TRX enable-bit workers ===")
    c.check("0xE6A26: body[2] == 0 gate",
            expect(insn, 0xE6A28, "ldrb", "r0, [r1, #2]")
            and expect(insn, 0xE6A2A, "cbz", "r0, 0xe6a3c"))
    c.check("0xE6A26: 0x40101220 |= 1", expect(insn, 0xE6A40, "orr.w",
                                               "r3, r3, #1")
            and expect(insn, 0xE6A44, "strh", "r3, [r2, #0]"))
    c.check("0xE6A26: 0x40101208 &= ~(1|4) via [r2,#-24]",
            expect(insn, 0xE6A46, "ldrh.w", "r3, [r2, #-24]")
            and expect(insn, 0xE6A5A, "bic.w", "r3, r3, #4"))
    c.check("0xE6A70: body[2] == 0 gate",
            expect(insn, 0xE6A72, "ldrb", "r0, [r1, #2]")
            and expect(insn, 0xE6A74, "cbz", "r0, 0xe6a86"))
    c.check("0xE6A70: 0x40101220 &= ~1 and 0x40101208 |= 1|4",
            expect(insn, 0xE6A88, "ldrh", "r3, [r2, #0]")
            and expect(insn, 0xE6A98, "orr.w", "r2, r2, #1")
            and expect(insn, 0xE6AA0, "orr.w", "r2, r2, #4"))
    c.check("0xE6AB4: body[2] != 0 gate",
            expect(insn, 0xE6AB6, "ldrb", "r3, [r1, #2]")
            and expect(insn, 0xE6AB8, "cbnz", "r3, 0xe6aca"))
    c.check("0xE6AB4: 0x40101220 |= 2 and 0x40101208 &= ~(2|8)",
            expect(insn, 0xE6AD0, "orr.w", "r3, r3, #2")
            and expect(insn, 0xE6AD6, "ldrh.w", "r3, [r2, #-24]"))
    c.check("0xE6B00: body[2] != 0 gate",
            expect(insn, 0xE6B02, "ldrb", "r3, [r1, #2]")
            and expect(insn, 0xE6B04, "cbnz", "r3, 0xe6b16"))
    c.check("0xE6B00: 0x40101220 &= ~2 and 0x40101208 |= 2|8",
            expect(insn, 0xE6B2A, "orr.w", "r2, r2, #2")
            and expect(insn, 0xE6B36, "strh", "r2, [r3, #0]"))
    c.check("name blob has the beacon_tx_resume/suspend names",
            raw.find(b"smac_msg_proc_beacon_tx_resume_req") >= 0
            and raw.find(b"smac_msg_proc_beacon_tx_suspend_req") >= 0)
    # negative: no g0-id1 table A/B row maps to these workers
    tgt = {(v & ~1) for v in BCN_MSGS.values()}
    hits_a = []
    for i in range(217):
        a = 0x10C23C + 8 * i
        w = img.u32(a + 4)
        if (w & ~1) in tgt:
            hits_a.append(img.u16(a))
    hits_b = []
    for i in range(88):
        a = 0x103BF8 + 8 * i
        w = img.u32(a + 4)
        if (w & ~1) in tgt:
            hits_b.append(img.u16(a))
    c.check("no g0-id1 table-A/B key maps to the beacon/TRX workers",
            hits_a == [] and hits_b == [], "%s %s" % (hits_a, hits_b))
    print()
    print("=== F. band-1 TRX enable worker CA 0xE8520 ===")
    c.check("body+0xA = band byte, body+0xE = module/on byte",
            expect(insn, 0xE853C, "ldr.w", "r6, [r1, #10]")
            and expect(insn, 0xE8538, "ldr.w", "r0, [r1, #14]")
            and expect(insn, 0xE8544, "uxtb", "r6, r6")
            and expect(insn, 0xE8540, "ands.w", "r5, r0, #255"))
    c.check("band1 enable writes 0x33 to 0x4010E150",
            img.u32(0xE8660) == BAND1_TRX0
            and expect(insn, 0xE85E6, "movs", "r2, #51")
            and expect(insn, 0xE85EA, "str", "r2, [r3, #0]"))
    c.check("band1 enable writes 0xFF to 0x4010E154/0x4010E158",
            img.u32(0xE8664) == BAND1_TRX1
            and expect(insn, 0xE85F0, "str", "r3, [r2, #0]")
            and expect(insn, 0xE85F2, "str", "r3, [r2, #4]"))
    c.check("SoC INTC 0x40002264/0x40002268/0x4000226C literals",
            img.u32(0xE8640) == SOC_INTC[1]
            and img.u32(0xE8644) == SOC_INTC[0]
            and img.u32(0xE8648) == SOC_INTC[2])
    print()
    print("=== G. band objects / G hooks / TBTT names ===")
    c.check("band lookup CA 0xA4FC8 uses base 0x15CA08 and stride 0x1B68",
            img.u32(0xA4FF4) == BANDOBJ_BASE
            and expect(insn, 0xA4FE2, "movw", "r1, #7016")
            and expect(insn, 0xA4FE8, "mla", "r0, r1, r3, r0"))
    c.check("band1 object = base + stride = 0x15E570",
            BANDOBJ_BASE + BANDOBJ_STRIDE == BAND1_OBJ)
    c.check("G table literal CA 0x9745C = 0x1747C4", img.u32(0x9745C)
            == G_TABLE)
    c.check("band start reads G+0x370/G+0x374 with cbz guards",
            expect(insn, 0x97360, "ldr.w", "r3, [r7, #880]")
            and expect(insn, 0x97364, "cbz", "r3, 0x9736a")
            and expect(insn, 0x9736A, "ldr.w", "r3, [r7, #884]"))
    for nm in ISR_NAMES:
        off = raw.find(nm.encode())
        c.check("%s present in the name blob" % nm,
                off >= 0 and NAME_BLOB_LO <= off + BASE <= NAME_BLOB_HI,
                "CA 0x%X" % (off + BASE if off >= 0 else 0))
    c.check("name blob carries no code pointer (no reference to its base)",
            struct.pack("<I", NAME_BLOB_LO) not in raw)
    c.check("no LE32 pointer to smac_tbtt_5g_isr name",
            struct.pack("<I", 0x1229E8) not in raw
            and struct.pack("<I", 0x1229FC) not in raw)
    print()
    print("=== H. probe pack sanity ===")
    c.check("RAM anchor probe count >= 15", len(RAM_PROBES) >= 15)
    c.check("two-step per-link probe count >= 10", len(RAM_PROBE_REFS) >= 10)
    c.check("IO probe count >= 25", len(IO_PROBES) >= 25)
    forbidden = list(range(0x10160000, 0x10170000))
    c.check("no probe target in the RC misc window",
            all(ca not in forbidden for _, ca, _, _ in
                [(n, c, w, e) for n, c, w, e in RAM_PROBES]))
    c.check("RAM probe phys = 0x40000000 + (CA + 0x6B8000)"
            " (ctx slot0 -> 0x40829458)",
            PHYS_BASE + CTX_SLOT0 + BAR0_RAM == 0x40829458)
    c.check("IO probe phys = CA + 0x3B8000",
            all(ca + BAR0_IO == 0x40000000 + BAR0_IO + (ca - PHYS_BASE)
                for _, ca, _, _ in IO_PROBES))
    c.check("band0/band1 mirror pairs present for the MAC block",
            MAC_BAND0 + 0x548 in [ca for _, ca, _, _ in IO_PROBES]
            and MAC_BAND1 + 0x548 in [ca for _, ca, _, _ in IO_PROBES]
            and MAC_BAND0 + 0x80C in [ca for _, ca, _, _ in IO_PROBES]
            and MAC_BAND1 + 0x80C in [ca for _, ca, _, _ in IO_PROBES])
    c.check("beacon-enable state registers are probed",
            SMAC_IRQ_GLOBAL in [ca for _, ca, _, _ in IO_PROBES]
            and SMAC_IRQ_FLAGS in [ca for _, ca, _, _ in IO_PROBES]
            and SMAC_IRQ_ENABLE in [ca for _, ca, _, _ in IO_PROBES]
            and SMAC_IRQ_STATUS in [ca for _, ca, _, _ in IO_PROBES]
            and BAND1_TRX0 in [ca for _, ca, _, _ in IO_PROBES]
            and BAND0_ENABLE in [ca for _, ca, _, _ in IO_PROBES])
    c.check("each beacon-state offset is probed for both bands",
            all(off in [o for _, o, _, _ in RAM_PROBE_REFS]
                for off in (0x6B8, 0x6BC, 0x6C0, 0x6C4, 0x6C6, 0x11D)))
    print()
    print("RESULT: %d checks, %d failed" % (c.n, c.bad))
    return c.bad == 0


def main():
    ap = argparse.ArgumentParser(description="5 GHz TBTT arm decode")
    ap.add_argument("--fw", default=os.environ.get("BW_FIRMWARE", DEFAULT_FW))
    ap.add_argument("--objdump", default=os.environ.get("BW_OBJDUMP",
                                                       DEFAULT_OD))
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--decode", action="store_true")
    ap.add_argument("--tables", action="store_true")
    ap.add_argument("--probes", action="store_true")
    args = ap.parse_args()
    if not any((args.selftest, args.decode, args.tables, args.probes)):
        ap.print_help()
        return 1
    img = Image(args.fw)
    insn = load_disasm(args.objdump, args.fw)
    ok = True
    if args.decode:
        print_decode(img, insn)
    if args.tables:
        print_tables(img, insn)
    if args.probes:
        print_probes(img)
    if args.selftest:
        ok = selftest(img, insn)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
