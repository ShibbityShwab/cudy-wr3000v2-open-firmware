#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_tbtt_timer_re.py - the 5 GHz TBTT/beacon-timer arm decode (task-214).

Lane: wifi-tbtt-timer (shared task task-214).  HOST-ONLY, read-only: the pinned
LOCAL vendor artifacts are read in place; only derived values (addresses,
instruction mnemonics, table words, register constants, probe addresses) are
printed.  No vendor byte is stored in this repository and no device is touched.

Artifacts (local, uncommitted):
  firmware: /home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin
            928,920 B sha256 7fc87e20...311b ; CPU = file offset + 0x40000
  objdump:  the OpenWrt lab ARM objdump (override with --objdump or $BW_OBJDUMP)
  capture:  ../wifi-f1-from-capture/evidence/FAMILY-TABLE.txt (committed
            derived keys of the stock H2D capture; no raw bytes)

What this tool re-derives (see ../REPORT.md, ../TABLES.md):
  A. The g0-id1 config dispatch (CA 0x43D58) is an exact u16 key lookup in
     table A (CA 0x10C23C, 217 rows) then table B (CA 0x103BF8, 88 rows), and
     no table key maps to a beacon/TBTT/ISR worker.  Key 0x0242 is the WAL
     set_green_ap band start (worker 0x4C524) whose subcommands 1/2 call the
     per-band MAC start 0x4AF0C; key 0x0243 is the config-notify event
     (worker 0x8419C) that calls 0x4BDCE(bandobj, 5, 0), which only restarts
     the MAC (0x4AF0C) when a band-state byte is 3.
  B. The MAC start 0x4AF0C performs no timer programming: its complete device
     IO literal set is the reset/status words, the two SMAC IRQ-enable words,
     the two MAC-start bits and 0x40101220.  It never touches the SMAC common
     timer word (0x401015CC) or the 16-bit timer word (0x400024B0).
  C. The band start 0x971D8 contains no device IO literal at all; it calls the
     runtime hooks G+0x370 (NULL) and G+0x374 (=0x57638).  0x57638 contains no
     device IO literal either; the beacon arm is deferred to firmware context.
  D. NEW: the per-band SMAC hardware interrupt entry points.  At boot the
     firmware registers 8 INTC sources per band (2.4G: IRQ 47/49/50/51/52/82/
     83/59; 5G: IRQ 53/55/56/57/58/84/85/60) through the RTOS call
     0xC6DA2(irq, prio, handler).  Each handler is a 3-instruction band wrapper
     that loads the per-band SMAC object from RAM 0x180814 (+8 = band0,
     +0x1C = band1) and jumps to one of 7 common ISR bodies.  Two IRQs share
     one body per band (50|83 and 56|85).  This is the first code-level
     identity of the SMAC interrupt vectors that the 45-name debug blob
     (CA 0x12266C) left [U].
  E. The ISR dispatch/registration plumbing: IRQ entry 0xC2EFC reads the
     pending-id word 0x4016010C, takes handler table[0x17D430+4*irq], calls
     it, then acks by writing the id back to 0x40160110; 0xC74B0 installs a
     handler at [0x17D398+152+4*irq] and 0xC6DB8 sets the enable bits at
     0x40161180/0x40161280.  Those INTC words are at CA 0x4016xxxx, OUTSIDE
     the host region-3 window (CA 0x40000000..0x4011FFFF), so the host cannot
     read them; the read-only liveness proxies are the SMAC IRQ enable/status
     words and the per-band SMAC objects.
  F. Two candidate timer words surfaced by the arm/init code: 0x401015CC
     (SMAC common latch/control: written 1 to start, 0 to clear, a count in
     the per-band init paths) and 0x400024B0 (16-bit word written 100 - the
     only literal 100 - by BOTH per-band init paths; candidate beacon-interval
     / tick word).  Their exact roles stay [U]; both are read-only probes.

Modes: --selftest (all assertions, exit 0 PASS), --decode, --tables,
--probes.  No vendor byte is printed in any mode (mnemonics/constants only).
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
CAPTURE_KEYS = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "..", "..", "wifi-f1-from-capture", "evidence",
                            "FAMILY-TABLE.txt")

# --- conventions -----------------------------------------------------------
PHYS_BASE = 0x40000000
BAR0_IO = 0x3B8000             # device IO: BAR0 offset = CA + 0x3B8000 - 0x40000000
BAR0_RAM = 0x6B8000            # firmware RAM: BAR0 offset = CA + 0x6B8000
IO_LO = 0x40000000             # host region-3 window (CA range)
IO_HI = 0x40120000

# --- A. dispatch / key tables ---------------------------------------------
DISPATCH = 0x43D58
TAB_A = 0x10C23C
TAB_A_ROWS = 217
TAB_B = 0x103BF8
TAB_B_ROWS = 88
KEY_SET_BEACON = 0x0127        # AP config (beacon template)
KEY_CHANNEL = 0x0124
KEY_RF_MODE = 0x01F7
KEY_VAP_ADD = 0x0064
KEY_VAP_START = 0x0065
KEY_VAP_STOP = 0x0067
KEY_EDCA = 0x00D8
KEY_GREEN_AP = 0x0242
KEY_CFG_NOTIFY = 0x0243
WORKER_GREEN_AP = 0x4C524
WORKER_CFG_NOTIFY = 0x8419C
WORKER_SET_BEACON = 0x84DA4

# --- B. MAC start ----------------------------------------------------------
MAC_START = 0x4AF0C
MAC_START_END = 0x4B120
MAC_START_IO = {0x40114500, 0x4011451C, 0x40114520,
                0x40110500, 0x4011051C, 0x40110520,
                0x4010C220, 0x40108214, 0x4011080C, 0x40101220}

# --- C. band start + callback ---------------------------------------------
BAND_START = 0x971D8
BAND_START_END = 0x9744C
G_TABLE = 0x1747C4
BCN_CB = 0x57638
BCN_CB_END = 0x576E8
BCN_CB_LIT = 0x5D280           # static word 0x00057639 installed at G+0x374

# --- D. per-band SMAC ISR registration ------------------------------------
REG_BAND0 = [(0xD1EF4, 47, 5, 0xAEC94), (0xD1F04, 49, 5, 0xB54C8),
             (0xD1F14, 50, 5, 0xB0D50), (0xD1F24, 51, 5, 0xAEC68),
             (0xD1F34, 52, 5, 0xAEB74), (0xD1F44, 82, 14, 0xAE48C),
             (0xD1F54, 83, 5, 0xB0D50), (0xD1F64, 59, 5, 0xB0C44)]
REG_BAND1 = [(0xD21DC, 53, 5, 0xAEC88), (0xD21EC, 55, 5, 0xB54BC),
             (0xD21FC, 56, 5, 0xB0D44), (0xD220C, 57, 5, 0xAEC5C),
             (0xD221C, 58, 5, 0xAEB68), (0xD222C, 84, 14, 0xAE480),
             (0xD223C, 85, 5, 0xB0D44), (0xD224C, 60, 5, 0xB0C38)]
BAND_DISPATCH_VAR = 0x180814   # RAM object: +8 -> band0 smac obj, +0x1C -> band1
WRAPPER_OFF_BAND0 = 8
WRAPPER_OFF_BAND1 = 0x1C
COMMON_ISR = {                 # common body -> IRQ class (first status bit tested)
    0xAE45C: "mask-ack (enable-mask literal)",
    0xAEA9C: "completion msg (status bit 8)",
    0xAEB80: "16-bit ring walk (status bit 7)",
    0xAEC74: "deferred task event (status bit 0)",
    0xB53A4: "ring dequeue + msg (status bit 2)",
    0xB0CE0: "IRQ-disable dispatch (50|83, 56|85)",
    0xB0AB8: "error bits 0/1/2 + 0xB0930",
}
IRQ_REG = 0xC6DA2
IRQ_ENABLE_API = 0xC6DB8
IRQ_DISPATCH = 0xC2EFC
INTC_PENDING = 0x4016010C
INTC_ACK = 0x40160110
INTC_ENABLE_SET = 0x40161180
INTC_ENABLE_SET2 = 0x40161280
IRQ_TABLE_BASE = 0x17D398
IRQ_TABLE_HANDLERS = 0x17D430
IRQ_TABLE_SLOTS = 96
COMMON_OF = {0xAEC94: 0xAEC74, 0xAE48C: 0xAE45C, 0xAEB74: 0xAEA9C,
             0xAEC68: 0xAEB80, 0xB54C8: 0xB53A4, 0xB0D50: 0xB0CE0,
             0xB0C44: 0xB0AB8, 0xAEC88: 0xAEC74, 0xAE480: 0xAE45C,
             0xAEB68: 0xAEA9C, 0xAEC5C: 0xAEB80, 0xB54BC: 0xB53A4,
             0xB0D44: 0xB0CE0, 0xB0C38: 0xB0AB8}

# --- E. timer candidates ---------------------------------------------------
SMAC_TIMER_W0 = 0x401015CC     # SMAC common latch/control [U]
SMAC_TIMER_W1 = 0x400024B0     # 16-bit word written 100 by both band inits [U]
TIMER_INIT_SITE = 0x9F436      # per-band init: str count -> 0x401015CC
TIMER_INIT_100 = 0x9F43E       # per-band init: literal 0x400024B0
TIMER_INIT_STR100 = 0x9F440    # per-band init: str r2(#100) -> 0x400024B0
TIMER_SYNC_SITE = 0x89814      # literal site 0x401015CC (write 1)
TIMER_SYNC_100 = 0x89828       # strh r2(#100) -> 0x400024B0
TIMER_SUB_HI = 1044480         # 0xFF000 ; 0x401015CC - 0xFF000 - 0x11C
TIMER_SUB_LO = 284             # 0x11C
TIMER_VAL = 100                # the only literal 100 in the arm paths

# --- F. probes -------------------------------------------------------------
IO_PROBES = [
    ("MAC start band0",            0x4011080C, 4, "bit0 = 1 after R5"),
    ("MAC start band1",            0x4011480C, 4, "bit0 = 1 after R5"),
    ("SMAC IRQ enable band1",      0x4010C220, 4, "contains 0x1029 after R5"),
    ("SMAC IRQ enable band0",      0x40108214, 4, "0x0F"),
    ("SMAC IRQ status band1",      0x4010C200, 4, "[U] pending source bits"),
    ("SMAC TRX0 band0",            0x4010820C, 4, "enable-TRX path"),
    ("SMAC TRX band1",             0x4010E150, 4, "0x33"),
    ("SMAC TRX band1 +4",          0x4010E154, 4, "0xFF"),
    ("SMAC TRX band1 +8",          0x4010E158, 4, "0xFF"),
    ("global run bits",            0x40101220, 4, "bit0 band1, bit1 band0"),
    ("global gate flags",          0x40101208, 4, "beacon/TRX gate bits"),
    ("SMAC timer latch/control",   SMAC_TIMER_W0, 4, "[U] differential"),
    ("SMAC 16-bit timer word",     SMAC_TIMER_W1, 2, "[U] differential"),
    ("band0 MAC +0x500",           0x40110500, 4, "reset 0xFF"),
    ("band0 MAC +0x51C",           0x4011051C, 4, "0xFF"),
    ("band0 MAC +0x520",           0x40110520, 4, "0xFF"),
    ("band0 MAC +0x548",           0x40110548, 4, "0x0101 class"),
    ("band0 MAC +0x54C",           0x4011054C, 4, "0x0101 class"),
    ("band0 MAC +0x5BC",           0x401105BC, 4, "band id 0"),
    ("band1 MAC +0x500",           0x40114500, 4, "reset 0xFF"),
    ("band1 MAC +0x51C",           0x4011451C, 4, "0xFF"),
    ("band1 MAC +0x520",           0x40114520, 4, "0xFF"),
    ("band1 MAC +0x548",           0x40114548, 4, "0x6101 class"),
    ("band1 MAC +0x54C",           0x4011454C, 4, "0x6101 class"),
    ("band1 MAC +0x5AC",           0x401145AC, 4, "restore byte pair"),
    ("band1 MAC +0x5BC",           0x401145BC, 4, "band id 0"),
]
RAM_PTR_PROBES = [
    ("band dispatch word",      BAND_DISPATCH_VAR, [0, 8, 0x1C], 4),
    ("G head",                  G_TABLE, [0], 4),
    ("G+0x370 MAC-start hook",  G_TABLE + 0x370, [], 4),
    ("G+0x374 beacon hook",     G_TABLE + 0x374, [], 4),
]
SMAC_OBJ_FOLLOW = [672, 700, 740]
SMAC_OBJ_WORDS = [(672, 228, 4), (672, 232, 4), (700, 700, 4)]


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
            ops = m.group(3).split("\t@")[0].strip()
            ops = re.sub(r"\s+", " ", ops)
            insn[int(m.group(1), 16)] = (m.group(2), ops)
    return insn


def norm(s):
    return re.sub(r"\s+", " ", s).strip()


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

    def literal_at(self, insn, site):
        got = insn.get(site)
        if not got:
            return None
        m = re.match(r"\w+, \[pc, #(-?\d+)\]", got[1])
        if not m:
            return None
        tgt = ((site + 4) & ~3) + int(m.group(1))
        if BASE <= tgt <= BASE + len(self.raw) - 4:
            return self.u32(tgt)
        return None

    def io_literals_in(self, insn, lo, hi):
        out = {}
        for site in range(lo, hi, 2):
            v = self.literal_at(insn, site)
            if v is not None and IO_LO <= v < 0x40200000:
                out[site] = v
        return out

    def keys(self, base, rows):
        out = {}
        for i in range(rows):
            ca = base + i * 8
            out[i] = (self.u16(ca), self.u32(ca + 4))
        return out


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

    def expect(self, label, insn, addr, mnemonic, operands):
        got = insn.get(addr)
        ok = bool(got) and norm(got[0] + " " + got[1]) == norm(mnemonic + " " + operands)
        return self.check(label, ok, "got %r" % (got,))


def parse_capture_keys(path):
    keys = {}
    try:
        for line in open(path):
            m = re.search(r"g0 id1 key (0x[0-9a-fA-F]{4})\s+n=\s*(\d+)", line)
            if m:
                keys[int(m.group(1), 16)] = int(m.group(2))
    except OSError:
        pass
    return keys


def worker(keys, key):
    for row, (k, w) in keys.items():
        if k == key:
            return row, (w & ~1)
    return None, None


def selftest(img, insn):
    c = Checker()
    print("=== selftest: image identity ===")
    c.check("firmware length == %d" % FW_LEN, len(img.raw) == FW_LEN)
    c.check("firmware sha256 == 7fc87e20...311b", sha256(img.raw) == FW_SHA)

    print("\n=== A. g0-id1 dispatch and key tables ===")
    c.expect("dispatch loads table A base", insn, 0x43D92, "ldr", "r5, [pc, #132]")
    c.check("table A base literal == 0x10C23C",
            img.literal_at(insn, 0x43D92) == TAB_A)
    c.expect("dispatch loads table B base", insn, 0x43DAA, "ldr", "r5, [pc, #112]")
    c.check("table B base literal == 0x103BF8",
            img.literal_at(insn, 0x43DAA) == TAB_B)
    c.expect("key read = u16 hdr+20", insn, 0x43D90, "ldrh", "r2, [r4, #20]")
    c.expect("row key compare", insn, 0x43D9E, "cmp", "r6, r2")
    c.expect("handler loaded from row+4", insn, 0x43DCE, "ldr", "r3, [r1, #4]")
    c.expect("handler called with body", insn, 0x43DF0, "ldrb", "r1, [r4, #22]")
    c.expect("body = hdr+24", insn, 0x43DEC, "add.w", "r2, r4, #24")
    c.expect("indirect call", insn, 0x43DF2, "blx", "r3")
    tab_a = img.keys(TAB_A, TAB_A_ROWS)
    tab_b = img.keys(TAB_B, TAB_B_ROWS)
    c.check("table A has 217 rows", len(tab_a) == TAB_A_ROWS)
    c.check("table B has 88 rows", len(tab_b) == TAB_B_ROWS)
    want = [(0x0064, 0x449E4), (0x0065, 0x46394), (0x0067, 0x45F74),
            (0x00D8, 0x81038), (0x0124, 0x841B8), (0x0127, 0x84DA4),
            (0x01F7, 0xB5B9C), (0x0242, WORKER_GREEN_AP),
            (0x0243, WORKER_CFG_NOTIFY)]
    for key, fn in want:
        row, w = worker(tab_a, key)
        if w is None:
            row, w = worker(tab_b, key)
    ok = True
    for key, fn in want:
        row, w = worker(tab_a, key)
        if w is None:
            row, w = worker(tab_b, key)
        if w != fn:
            ok = False
            print("      key 0x%04x -> %r (want 0x%06x)" % (key, w, fn))
    c.check("nine known keys map to the documented workers", ok)
    allw = set(w for _, w in tab_a.values()) | set(w for _, w in tab_b.values())
    c.check("no table A/B worker is a SMAC ISR/common body",
            not (allw & set(COMMON_ISR.keys())))
    c.check("no table A/B worker is the beacon callback 0x57638",
            0x57638 not in allw)
    c.check("no table A/B worker is the band start 0x971D8",
            0x971D8 not in allw)
    c.check("no table A/B worker is the MAC start 0x4AF0C",
            0x4AF0C not in allw)
    cap = parse_capture_keys(CAPTURE_KEYS)
    c.check("stock capture keys parsed from the committed evidence",
            len(cap) >= 20)
    c.check("stock capture contains set_beacon 0x0127", 0x0127 in cap)
    c.check("stock capture does NOT contain the band start 0x0242",
            0x0242 not in cap)
    c.check("stock capture does NOT contain the config notify 0x0243",
            0x0243 not in cap)
    capw = set()
    for k in cap:
        _, w = worker(tab_a, k)
        if w is None:
            _, w = worker(tab_b, k)
        if w:
            capw.add(w)
    c.check("no captured key resolves to a SMAC ISR/common body",
            not (capw & set(COMMON_ISR.keys())))

    print("\n=== B. MAC start 0x4AF0C: no timer programming ===")
    iol = img.io_literals_in(insn, MAC_START, MAC_START_END)
    c.check("MAC start IO literal set is the documented start set",
            set(iol.values()) == MAC_START_IO,
            "got %r" % sorted(set(iol.values())))
    c.check("MAC start never references SMAC timer 0x401015CC",
            SMAC_TIMER_W0 not in iol.values())
    c.check("MAC start never references timer word 0x400024B0",
            SMAC_TIMER_W1 not in iol.values())
    c.expect("band1 reset pulse value 0x55", insn, 0x4AF62, "mov.w", "r3, #85")
    c.expect("band1 reset write", insn, 0x4AF72, "str.w", "r3, [r8]")
    c.expect("band1 release value 0xFF", insn, 0x4AF7E, "movs", "r3, #255")
    c.expect("SMAC IRQ enable OR bit0", insn, 0x4AFB2, "orr.w", "r2, r2, #1")
    c.expect("SMAC IRQ enable OR bit3", insn, 0x4AFBC, "orr.w", "r2, r2, #8")
    c.expect("SMAC IRQ enable OR bit5", insn, 0x4AFC6, "orr.w", "r2, r2, #32")
    c.expect("SMAC IRQ enable OR bit12", insn, 0x4AFD0, "orr.w", "r2, r2, #4096")
    c.check("band1 SMAC enable literal == 0x4010C220",
            img.literal_at(insn, 0x4AFAE) == 0x4010C220)
    for addr, mnem, ops in [(0x4B0C8, "orr.w", "r2, r2, #1"),
                            (0x4B0D2, "orr.w", "r2, r2, #2"),
                            (0x4B0DC, "orr.w", "r2, r2, #4"),
                            (0x4B0E6, "orr.w", "r2, r2, #8")]:
        c.expect("band0 SMAC enable %s" % ops, insn, addr, mnem, ops)
    c.check("band0 SMAC enable literal == 0x40108214",
            img.literal_at(insn, 0x4B0C4) == 0x40108214)
    c.expect("MAC start bit0 set", insn, 0x4AFF8, "orr.w", "r2, r2, #1")
    c.expect("MAC start bit0 write", insn, 0x4AFFC, "str", "r2, [r3, #0]")
    c.check("MAC start reg literal == 0x4011080C",
            img.literal_at(insn, 0x4AFE0) == 0x4011080C)
    c.expect("run bit write", insn, 0x4B012, "str", "r2, [r3, #0]")
    c.check("run bits literal == 0x40101220",
            img.literal_at(insn, 0x4B00C) == 0x40101220)
    c.expect("start state = 2", insn, 0x4B05E, "strb", "r3, [r4, #1]")
    c.expect("start counter increment", insn, 0x4B062, "adds", "r3, #1")
    c.expect("start counter store", insn, 0x4B064, "str", "r3, [r4, #100]")

    print("\n=== C. band start 0x971D8 and the beacon hook ===")
    iol = img.io_literals_in(insn, BAND_START, BAND_START_END)
    c.check("band start contains no device IO literal", not iol,
            "got %r" % iol)
    c.check("band start loads G table literal",
            img.literal_at(insn, 0x97352) == G_TABLE)
    c.expect("band start reads G+0x370", insn, 0x97360, "ldr.w", "r3, [r7, #880]")
    c.expect("band start calls G+0x370", insn, 0x97368, "blx", "r3")
    c.expect("band start reads G+0x374", insn, 0x9736A, "ldr.w", "r3, [r7, #884]")
    c.expect("band start calls G+0x374", insn, 0x97372, "blx", "r3")
    c.check("static hook word 0x00057639 at CA 0x5D280",
            img.u32(BCN_CB_LIT) == 0x57639)
    c.expect("hook installer loads the static word", insn, 0x5D13E,
             "ldr", "r3, [pc, #320]")
    c.expect("hook installed at G+0x374", insn, 0x5D144, "str.w", "r3, [fp, #884]")
    iol = img.io_literals_in(insn, BCN_CB, BCN_CB_END)
    c.check("beacon hook 0x57638 contains no device IO literal", not iol,
            "got %r" % iol)
    c.expect("beacon hook operates on a list", insn, 0x576A2, "bl", "0x77324")

    print("\n=== D. per-band SMAC ISR registration (new) ===")
    for site, irq, prio, handler in REG_BAND0:
        c.expect("band0 IRQ %d registered via 0xC6DA2" % irq, insn, site,
                 "bl", "0xc6da2")
        c.expect("band0 IRQ %d priority %d" % (irq, prio), insn, site - 4,
                 "movs", "r1, #%d" % prio)
        c.expect("band0 IRQ %d number" % irq, insn, site - 2,
                 "movs", "r0, #%d" % irq)
        c.check("band0 IRQ %d handler literal == 0x%06X" % (irq, handler),
                (img.literal_at(insn, site - 6) or 0) & ~1 == handler)
    for site, irq, prio, handler in REG_BAND1:
        c.expect("band1 IRQ %d registered via 0xC6DA2" % irq, insn, site,
                 "bl", "0xc6da2")
        c.expect("band1 IRQ %d priority %d" % (irq, prio), insn, site - 4,
                 "movs", "r1, #%d" % prio)
        c.expect("band1 IRQ %d number" % irq, insn, site - 2,
                 "movs", "r0, #%d" % irq)
        c.check("band1 IRQ %d handler literal == 0x%06X" % (irq, handler),
                (img.literal_at(insn, site - 6) or 0) & ~1 == handler)
    bank0, bank1 = {}, {}
    for site, irq, prio, handler in REG_BAND0:
        bank0[handler] = bank0.get(handler, []) + [irq]
    for site, irq, prio, handler in REG_BAND1:
        bank1[handler] = bank1.get(handler, []) + [irq]
    body0 = set(COMMON_OF[h] for h in bank0)
    body1 = set(COMMON_OF[h] for h in bank1)
    c.check("band0 common-body set == band1 common-body set",
            body0 == body1)
    c.check("50|83 share one handler", len(bank0.get(0xB0D50, [])) == 2)
    c.check("56|85 share one handler", len(bank1.get(0xB0D44, [])) == 2)
    for handler, common in [(0xAEC94, 0xAEC74), (0xAE48C, 0xAE45C),
                            (0xAEB74, 0xAEA9C), (0xAEC68, 0xAEB80),
                            (0xB54C8, 0xB53A4), (0xB0D50, 0xB0CE0),
                            (0xB0C44, 0xB0AB8)]:
        c.expect("wrapper %06X band0 loads dispatch var" % handler, insn,
                 handler, "ldr", "r3, [pc, #4]")
        c.check("wrapper %06X band0 literal == 0x180814" % handler,
                img.literal_at(insn, handler) == BAND_DISPATCH_VAR)
        c.expect("wrapper %06X band0 offset +8" % handler, insn, handler + 2,
                 "ldr", "r0, [r3, #8]")
        c.check("wrapper %06X jumps to common %06X" % (handler, common),
                insn.get(handler + 4) is not None
                and insn[handler + 4][0] == "b.n"
                and insn[handler + 4][1] == "0x%x" % common)
    for handler, common in [(0xAEC88, 0xAEC74), (0xAE480, 0xAE45C),
                            (0xAEB68, 0xAEA9C), (0xAEC5C, 0xAEB80),
                            (0xB54BC, 0xB53A4), (0xB0D44, 0xB0CE0),
                            (0xB0C38, 0xB0AB8)]:
        c.expect("wrapper %06X band1 loads dispatch var" % handler, insn,
                 handler, "ldr", "r3, [pc, #4]")
        c.check("wrapper %06X band1 literal == 0x180814" % handler,
                img.literal_at(insn, handler) == BAND_DISPATCH_VAR)
        c.expect("wrapper %06X band1 offset +0x1C" % handler, insn, handler + 2,
                 "ldr", "r0, [r3, #28]")
        c.check("wrapper %06X jumps to common %06X" % (handler, common),
                insn.get(handler + 4) is not None
                and insn[handler + 4][0] == "b.n"
                and insn[handler + 4][1] == "0x%x" % common)

    print("\n=== E. ISR dispatch/registration plumbing (host-invisible INTC) ===")
    c.check("dispatch pending literal == 0x4016010C",
            img.literal_at(insn, 0xC2F00) == INTC_PENDING)
    c.check("dispatch ack literal == 0x40160110",
            img.literal_at(insn, 0xC2F4E) == INTC_ACK)
    c.expect("dispatch reads pending id", insn, 0xC2F04, "ldr", "r7, [r3, #0]")
    c.expect("dispatch masks id", insn, 0xC2F08, "ubfx", "r8, r7, #0, #10")
    c.expect("dispatch bounds-check 95", insn, 0xC2F0C, "cmp.w", "r8, #95")
    c.expect("dispatch handler table = +152", insn, 0xC2F16,
             "ldrls.w", "r5, [r3, #152]")
    c.expect("dispatch calls handler", insn, 0xC2F4A, "blx", "r5")
    c.check("IRQ table base literal == 0x17D398",
            img.literal_at(insn, 0xC2F02) == IRQ_TABLE_BASE)
    c.expect("registration table write", insn, 0xC74E8,
             "str.w", "r8, [r3, #152]")
    c.expect("registration base add", insn, 0xC74C4,
             "add.w", "r3, r5, r4, lsl #2")
    c.check("enable API set literal == 0x40161180",
            img.literal_at(insn, 0xC6DE8) == INTC_ENABLE_SET)
    c.expect("enable API clears/enables +0x100", insn, 0xC6DEE,
             "add.w", "r3, r3, #256")
    c.check("handler table base == table+152",
            IRQ_TABLE_HANDLERS == IRQ_TABLE_BASE + 152)
    c.check("handler slots 96 (384 B)", IRQ_TABLE_SLOTS == 96)

    print("\n=== F. timer candidates ===")
    c.check("per-band init loads 0x401015CC",
            img.literal_at(insn, TIMER_INIT_SITE) == SMAC_TIMER_W0)
    c.check("per-band init loads 0x400024B0",
            img.literal_at(insn, TIMER_INIT_100) == SMAC_TIMER_W1)
    c.expect("per-band init uses literal 100", insn, 0x9F434, "movs", "r2, #100")
    c.expect("per-band init stores 100 to 0x400024B0", insn, TIMER_INIT_STR100,
             "str", "r2, [r3, #0]")
    c.check("timer sync path loads 0x401015CC",
            img.literal_at(insn, TIMER_SYNC_SITE) == SMAC_TIMER_W0)
    c.expect("timer sync writes 1 to the latch", insn, 0x89812,
             "movs", "r2, #1")
    c.expect("timer sync latch store", insn, 0x89816, "strh", "r2, [r3, #0]")
    c.expect("timer sync subtract hi", insn, 0x89818,
             "sub.w", "r3, r3, #1044480")
    c.expect("timer sync subtract lo", insn, 0x8981C,
             "sub.w", "r3, r3, #284")
    c.check("computed 16-bit timer word == 0x400024B0",
            SMAC_TIMER_W0 - TIMER_SUB_HI - TIMER_SUB_LO == SMAC_TIMER_W1)
    c.expect("timer sync writes literal 100", insn, 0x89826,
             "movs", "r2, #100")
    c.expect("timer sync stores 100", insn, 0x89828, "strh", "r2, [r3, #0]")
    c.check("the arm paths use exactly the literal 100", TIMER_VAL == 100)

    print("\n=== G. probe pack ===")
    for label, ca, width, exp in IO_PROBES:
        ok = IO_LO <= ca < IO_HI and (ca & 3) == 0
        c.check("IO probe host-readable: %s" % label, ok, "CA 0x%08X" % ca)
    c.check("timer word probe is inside region-3",
            IO_LO <= SMAC_TIMER_W1 < IO_HI)
    c.check("INTC is outside region-3 (not host-readable)",
            not (IO_LO <= INTC_PENDING < IO_HI))
    c.check("no probe touches the forbidden RC misc window",
            not any(0x10160000 <= ca + BAR0_IO < 0x10170000
                    for _, ca, _, _ in IO_PROBES))

    print("\n=== summary: %d assertions, %d FAIL ===" % (c.n, c.bad))
    return 0 if c.bad == 0 else 1


# ---------------------------------------------------------------------------
def print_decode(img, insn):
    print("=== 5 GHz TBTT/beacon-timer arm - decode ===")
    print()
    print("[A] host messages: the g0-id1 dispatch is an exact u16 key lookup")
    print("    dispatch CA 0x%X; table A CA 0x%X x%d; table B CA 0x%X x%d"
          % (DISPATCH, TAB_A, TAB_A_ROWS, TAB_B, TAB_B_ROWS))
    print("    key 0x%04X set_beacon -> worker 0x%06X (beacon template only)"
          % (KEY_SET_BEACON, WORKER_SET_BEACON))
    print("    key 0x%04X green_ap  -> worker 0x%06X (MAC start subcommands)"
          % (KEY_GREEN_AP, WORKER_GREEN_AP))
    print("    key 0x%04X cfg-recently-changed -> worker 0x%06X"
          % (KEY_CFG_NOTIFY, WORKER_CFG_NOTIFY))
    print("    NO table A/B key maps to a beacon/TBTT/ISR worker.")
    cap = parse_capture_keys(CAPTURE_KEYS)
    print("    stock capture: %d distinct g0-id1 keys; 0x0127 present,"
          " 0x0242/0x0243 absent" % len(cap))
    print()
    print("[B] MAC start CA 0x%X: ARM-only, no timer programming" % MAC_START)
    for site, val in sorted(img.io_literals_in(insn, MAC_START,
                                               MAC_START_END).items(),
                            key=lambda kv: kv[1]):
        print("      site 0x%06X -> 0x%08X" % (site, val))
    print("    -> reset/status words, SMAC IRQ enables, MAC-start bit,")
    print("       0x40101220 run bit only; timer word not referenced.")
    print()
    print("[C] band start CA 0x%X: no device IO literal at all" % BAND_START)
    print("    calls G+0x370 (NULL) then G+0x374 = 0x%06X" % BCN_CB)
    print("    the hook 0x57638 has no device IO literal either -> the")
    print("    beacon arm is deferred to firmware task/timer context.")
    print()
    print("[D] per-band SMAC ISR registration (new):")
    for site, irq, prio, handler in REG_BAND0:
        print("    band0 IRQ %2d prio %2d -> wrapper 0x%06X" % (irq, prio, handler))
    for site, irq, prio, handler in REG_BAND1:
        print("    band1 IRQ %2d prio %2d -> wrapper 0x%06X" % (irq, prio, handler))
    print("    dispatch word 0x%X: +8 band0 object, +0x1C band1 object"
          % BAND_DISPATCH_VAR)
    for body, cls in sorted(COMMON_ISR.items()):
        print("    common body 0x%06X  %s" % (body, cls))
    print()
    print("[E] INTC plumbing (CA 0x4016xxxx, outside the host window):")
    print("    pending 0x%08X -> handler [0x%X+4*irq] -> ack 0x%08X"
          % (INTC_PENDING, IRQ_TABLE_HANDLERS, INTC_ACK))
    print("    enable bits 0x%08X / 0x%08X" % (INTC_ENABLE_SET, INTC_ENABLE_SET2))
    print("    host-readable proxies: the SMAC IRQ enable/status words and")
    print("    the per-band SMAC objects.")
    print()
    print("[F] timer candidates [U]:")
    print("    0x%08X SMAC common latch/control (start=1, clear=0, count)"
          % SMAC_TIMER_W0)
    print("    0x%08X 16-bit word written 100 by both per-band init paths"
          % SMAC_TIMER_W1)


def print_tables(img, insn):
    tab_a = img.keys(TAB_A, TAB_A_ROWS)
    tab_b = img.keys(TAB_B, TAB_B_ROWS)
    print("=== TABLES ===")
    print("\n[1] per-band SMAC ISR registration (call 0xC6DA2(irq,prio,handler))")
    print("| band | IRQ | prio | wrapper | common body |")
    print("| ---: | ---: | ---: | ---: | ---: |")
    common = {0xAEC94: 0xAEC74, 0xAE48C: 0xAE45C, 0xAEB74: 0xAEA9C,
              0xAEC68: 0xAEB80, 0xB54C8: 0xB53A4, 0xB0D50: 0xB0CE0,
              0xB0C44: 0xB0AB8, 0xAEC88: 0xAEC74, 0xAE480: 0xAE45C,
              0xAEB68: 0xAEA9C, 0xAEC5C: 0xAEB80, 0xB54BC: 0xB53A4,
              0xB0D44: 0xB0CE0, 0xB0C38: 0xB0AB8}
    for site, irq, prio, handler in REG_BAND0:
        print("| 0 (2.4G) | %d | %d | 0x%06X | 0x%06X |"
              % (irq, prio, handler, common[handler]))
    for site, irq, prio, handler in REG_BAND1:
        print("| 1 (5G) | %d | %d | 0x%06X | 0x%06X |"
              % (irq, prio, handler, common[handler]))
    print("\n[2] common ISR bodies")
    for body, cls in sorted(COMMON_ISR.items()):
        print("| 0x%06X | %s |" % (body, cls))
    print("\n[3] key facts (table A/B)")
    for key, fn in [(KEY_VAP_ADD, 0x449E4), (KEY_VAP_START, 0x46394),
                    (KEY_VAP_STOP, 0x45F74), (KEY_EDCA, 0x81038),
                    (KEY_CHANNEL, 0x841B8), (KEY_SET_BEACON, WORKER_SET_BEACON),
                    (KEY_RF_MODE, 0xB5B9C), (KEY_GREEN_AP, WORKER_GREEN_AP),
                    (KEY_CFG_NOTIFY, WORKER_CFG_NOTIFY)]:
        row, w = worker(tab_a, key)
        tab = "A"
        if row is None:
            row, w = worker(tab_b, key)
            tab = "B"
        print("| 0x%04X | %s row %s | 0x%06X |" % (key, tab, row, w))
    print("\n[4] timer candidates")
    print("| CA | width | evidence |")
    print("| ---: | ---: | --- |")
    print("| 0x%08X | 4 | start=1/clear=0/count writes; the SMAC common latch |"
          % SMAC_TIMER_W0)
    print("| 0x%08X | 2 | literal 100 written by both per-band init paths |"
          % SMAC_TIMER_W1)


def probe_line(kind, label, ca, width):
    if kind == "io":
        bar0 = ca + BAR0_IO - PHYS_BASE
        phys = ca + BAR0_IO
    else:
        bar0 = ca + BAR0_RAM
        phys = PHYS_BASE + bar0
    return "| %s | 0x%08X | %d | 0x%08X | 0x%08X |" % (label, ca, width, bar0, phys)


def print_probes(img):
    print("=== PROBES (read-only) ===")
    print("host IO rule: BAR0 offset = CA + 0x3B8000 - 0x40000000, phys = CA+0x3B8000")
    print("firmware RAM rule: BAR0 offset = CA + 0x6B8000, phys = 0x40000000+BAR0")
    print("\n[A] device IO (region-3, host-readable)")
    print("| object | CA | width | BAR0 | phys |")
    print("| --- | ---: | ---: | ---: | ---: |")
    for label, ca, width, exp in IO_PROBES:
        print(probe_line("io", label, ca, width))
    print("\n[B] firmware pointers (two-step: read pointer, then the field)")
    print("| object | CA | width | BAR0 | phys |")
    print("| --- | ---: | ---: | ---: | ---: |")
    for label, ca, offs, width in RAM_PTR_PROBES:
        print(probe_line("ram", label, ca, width))
    for off in [WRAPPER_OFF_BAND0, WRAPPER_OFF_BAND1]:
        print(probe_line("ram", "band smac object ptr +0x%X" % off,
                         BAND_DISPATCH_VAR + off, 4))
    print("\n[C] per-band SMAC status/mask (two-step: read [obj+672], then +228/+232)")
    print("| field | offset | width |")
    print("| --- | ---: | ---: |")
    print("| mask/ack word | +228 | 4 |")
    print("| status word | +232 | 4 |")
    print("\nInterpretation: compare band0 (working 2.4G AP) with band1")
    print("(silent 5G AP) in the same boot; band0 is the reference.")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fw", default=os.environ.get("BW_FW", DEFAULT_FW))
    ap.add_argument("--objdump", default=os.environ.get("BW_OBJDUMP", DEFAULT_OD))
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--decode", action="store_true")
    ap.add_argument("--tables", action="store_true")
    ap.add_argument("--probes", action="store_true")
    args = ap.parse_args()
    if not os.path.exists(args.fw):
        sys.exit("firmware not found: %s" % args.fw)
    img = Image(args.fw)
    insn = load_disasm(args.objdump, args.fw)
    if args.selftest:
        return selftest(img, insn)
    if args.decode:
        print_decode(img, insn)
    if args.tables:
        print_tables(img, insn)
    if args.probes:
        print_probes(img)
    if not (args.decode or args.tables or args.probes):
        print_decode(img, insn)
    return 0


if __name__ == "__main__":
    sys.exit(main())
