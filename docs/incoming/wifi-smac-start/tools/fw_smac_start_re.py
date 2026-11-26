#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_smac_start_re.py - the firmware SMAC/beacon-timer start decode (task-215).

Lane: wifi-smac-start (shared task task-215).  HOST-ONLY, read-only: the pinned
LOCAL vendor artifacts are read in place; only derived values (addresses,
instruction mnemonics, table words, probe addresses) are printed.  No vendor
byte is stored in this repository and no device is touched.

Artifacts (local, uncommitted):
  firmware: /home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin
            928,920 B sha256 7fc87e20...311b ; CPU = file offset + 0x40000
  objdump:  the OpenWrt lab ARM objdump (override with --objdump or $BW_OBJDUMP)
  capture:  ../wifi-f1-from-capture/evidence/FAMILY-TABLE.txt (committed
            derived keys of the stock H2D capture; no raw bytes)

What this tool re-derives (see ../REPORT.md, ../TABLES.md):
  A. The timer words 0x401015CC / 0x400024B0: the complete literal reference
     set is the two ref-counted latch helpers CA 0x8980C (write 1 = start) and
     CA 0x897D6 (write 0 = stop) plus the four per-link init/clear paths
     (0x9F43x, 0xA0052, 0xA02Ax, 0xA047E).  The helpers are called only from
     CA 0xE8670 and CA 0xF3B22; the Lead's 2.4 GHz differential fire shows the
     same 0 / 0x64 state on the working and silent bands, so these words are
     not the 5 GHz discriminator (refutes the task-214 latch hypothesis).
  B. CA 0xD1B68 is the boot SMAC init (caller 0x48314).  It memsets 7,272 B at
     0x174BE0 and then walks the band instances (base 0x174C30 + band x 0xE28)
     setting per band: the device-base chain at +608..644 (0x40042000 band0 /
     0x40062000 band1), the second chain at +648..708 (0x40080000 / 0x400B0000),
     the SMAC sub-bases at +720 / +724 (0x4010A000/0x40109000 vs
     0x4010E000/0x4010D000) and the RAM block attach set at +576..600
     (0x114F18 / 0x114F18+488 / 0x115128 / 0x183438 / 0x1817A0 / 0x180904 /
     0x180E68), filled at boot from tables 0x1B2A87 / 0x1B2AAC.
  C. Wrapper CA 0x180814 +8/+0x1C = band objects 0x174BF0/0x175A18; the SMAC
     ISR common bodies read the context pointer from [object+672]; the band
     object accessor CA 0xF4F48 = 0x174BF0 + 0xE28*band.
  D. The full-image literal scan for 0x114F18 (all five copies, seven load
     sites) - the boot fill loop and the later rate/power-table readers.
  E. The stock H2D key set vs our documented ladder (25 vs 9 g0-id1 keys) and
     the dispatch workers of the stock-only candidates.
  F. The per-band SMAC ISR/handler name table at CA 0x12266C (45 strings): the
     inferred body/IRQ mapping, and that smac_tbtt_{5g,2g}_isr is not one of
     the 8 registered INTC sources.

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

BASE = 0x40000
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

# --- A. timer words --------------------------------------------------------
SMAC_TIMER_W0 = 0x401015CC
SMAC_TIMER_W1 = 0x400024B0
W0_REF_SITES = [0x897EE, 0x89814, 0x9F436, 0xA0052, 0xA02A0, 0xA047A]
W1_REF_SITES = [0x897F6, 0x9F43E, 0xA02A8]
TIMER_SAVE_SLOT = 0x001664C8
START_HELPER = 0x8980C
STOP_HELPER = 0x897D6
HELPER_CALLERS = [(0xE868E, START_HELPER), (0xE8804, STOP_HELPER),
                  (0xF3C02, START_HELPER), (0xF3D1C, STOP_HELPER)]
HELPER_OWNERS = [0xE8670, 0xF3B22]
TBTT_BLOCK = [0x40101584, 0x40101588, 0x40101594, 0x4010159C,
              0x401015A4, 0x401015A8, 0x401015AC]
TBTT_BLOCK_SITES = {0xF3C34: 0x401015A4, 0xF3C36: 0x401015AC,
                    0xF3C3C: 0x40101584, 0xF3C4C: 0x40101594,
                    0xF3C96: 0x401015A8, 0xF3CEA: 0x40101588,
                    0xF3CF6: 0x4010159C}

# --- B. boot SMAC init / band object --------------------------------------
BAND_REGION = 0x00174BE0
BAND_REGION_LEN = 7272
BAND_STRIDE = 0xE28
BAND_OBJ0 = 0x00174BF0
BAND_OBJ1 = 0x00175A18
BAND_ACCESSOR = 0xF4F48
SMAC_INIT = 0xD1B68
SMAC_INIT_CALLER = 0x48314
SMAC_INIT_OBJ0 = 0x00174C30           # per-band loop object (band0)
DEV_CHAIN_A_SITES = {0xD2046: 608, 0xD204E: 612, 0xD2056: 616,
                     0xD205E: 620, 0xD2066: 624, 0xD206E: 628,
                     0xD2076: 632, 0xD207E: 636}
DEV_CHAIN_A_BASES = {0xD2044: 0x40042000, 0xD2094: 0x40062000}
DEV_CHAIN_B_SITES = {0xD20FE: 648, 0xD2106: 652, 0xD210E: 656,
                     0xD2116: 660, 0xD211E: 664, 0xD2126: 668,
                     0xD212E: 672, 0xD2136: 676}
DEV_CHAIN_B_BASES = {0xD20FC: 0x40080000, 0xD217C: 0x400B0000}
SMAC_BASE0 = 0x4010A000
SMAC_BASE1 = 0x4010E000
RAM_BLOCK = 0x00114F18
RAM_BLOCK_LEN = 488
RAM_BLOCK_SET = [RAM_BLOCK, RAM_BLOCK + RAM_BLOCK_LEN, 0x00115128,
                 0x00183438, 0x001817A0, 0x00180904, 0x00180E68]
ATTACH_LITERALS = {0xD2BC8: RAM_BLOCK, 0xD2CA0: 0x00115128,
                   0xD2CAA: 0x00183438, 0xD2CB4: 0x001817A0,
                   0xD2CBC: 0x00180904, 0xD2CC2: 0x00180E68}
ATTACH_STORES = {0xD2CA2: (576, "r9"), 0xD2CAC: (580, "r6"),
                 0xD2CA6: (584, "r3"), 0xD2CB0: (588, "r3"),
                 0xD2CB8: (592, "r3"), 0xD2CBE: (596, "r3"),
                 0xD2CC4: (600, "r3")}
FILL_TABLES = [0x001B2A87, 0x001B2AAC]
RAM_BLOCK_LOADS = {0x85EF2: "rate/power table read",
                   0xA5414: "rate/power table read",
                   0xA54D2: "rate/power table read",
                   0xA5652: "rate/power table read",
                   0xA5770: "rate/power table read",
                   0xD2BC8: "boot fill source",
                   0xDA824: "rate/power table select"}

# --- C. wrapper / ISR bodies ----------------------------------------------
WRAPPER_VAR = 0x00180814
ISR_BODIES = [0xAE45C, 0xAEA9C, 0xAEB80, 0xB53A4, 0xB0CE0, 0xB0AB8, 0xAEC74]
NAMES_LO = 0x12266C
NAMES_HI = 0x122BA8
NAME_PAIRS = ["smac_coex_rx_abort_end", "smac_rx_complete",
              "smac_tx_complete", "smac_common_timer",
              "smac_coex_pta_rx_abort_timeout", "smac_coex_tx_abort_end",
              "smac_coex_pta_tx_abort_timeout"]
MSG_PROC = ["set_mu_edca_timer_req", "ac_suspend_req",
            "set_cali_self_cts_tx_num_req", "set_cali_ppdu_tx_num_req",
            "rx_ring_reset_req", "update_tx_bypass_ctrl_req",
            "beacon_tx_resume_req", "beacon_tx_suspend_req",
            "clr_hw_fifo_req", "tx_resume_req", "tx_suspend_req",
            "enable_trx_req", "disable_trx_req"]
EVENT_NAMES = ["smac_tbtt_5g_isr", "smac_tbtt_2g_isr",
               "smac_rx_timeout_5g_isr", "smac_rx_timeout_2g_isr",
               "smac_pre_tx_5g_isr", "smac_pre_tx_2g_isr",
               "smac_backoff_timeout_5g_isr", "smac_backoff_timeout_2g_isr",
               "smac_user_queue_empty_5g_isr", "smac_user_queue_empty_2g_isr",
               "smac_user_ps_5g_isr", "smac_user_ps_2g_isr",
               "smac_tx_abort_start_5g_isr", "smac_tx_abort_start_2g_isr",
               "smac_lifetime_expire_5g_isr", "smac_lifetime_expire_2g_isr",
               "smac_tx_exit_5g_isr", "smac_tx_exit_2g_isr"]
REG_IRQ0 = [47, 49, 50, 51, 52, 82, 83, 59]
REG_IRQ1 = [53, 55, 56, 57, 58, 84, 85, 60]

# --- D. keys --------------------------------------------------------------
KEYS_LADDER = [0x0064, 0x0065, 0x0067, 0x00D8, 0x0124, 0x0127, 0x01F7,
               0x0242, 0x0243]
KEY_WORKERS = {0x0064: 0x449E4, 0x0065: 0x46394, 0x0067: 0x45F74,
               0x00D8: 0x81038, 0x0124: 0x841B8, 0x0127: 0x84DA4,
               0x01F7: 0xB5B9C, 0x0242: 0x4C524, 0x0243: 0x8419C,
               0x01CF: 0x84D14, 0x03E4: 0x84260}
TAB_A = 0x10C23C
TAB_A_ROWS = 217
TAB_B = 0x103BF8
TAB_B_ROWS = 88

IO_LO, IO_HI = 0x40000000, 0x40120000


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
    pat = re.compile(r"^\s*([0-9a-f]+):\s+[0-9a-f ]+\t(\S+)\t?(.*)$")
    for line in out.stdout.splitlines():
        m = pat.match(line)
        if m:
            ops = m.group(3).split("\t@")[0].strip()
            insn[int(m.group(1), 16)] = (m.group(2), re.sub(r"\s+", " ", ops))
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
        m = re.match(r"^\w+, \[pc, #(-?\d+)\]$", got[1])
        if not m:
            return None
        tgt = ((site + 4) & ~3) + int(m.group(1))
        if BASE <= tgt <= BASE + len(self.raw) - 4:
            return self.u32(tgt)
        return None

    def literal_ref_sites(self, insn, value):
        out = []
        for site in insn:
            if self.literal_at(insn, site) == value:
                out.append(site)
        return sorted(out)

    def literal_load_sites(self, value):
        """Brute-force all Thumb literal-load sites for a 32-bit word value,
        independent of the disassembler decode (16-bit ldr literal + 32-bit
        ldr.w literal encodings)."""
        seats = set()
        raw = self.raw
        for i in range(0, len(raw) - 4, 2):
            if struct.unpack_from("<I", raw, i)[0] == value:
                seats.add(BASE + i)
        out = []
        for ca in range(BASE, BASE + len(raw) - 4, 2):
            h = self.u16(ca)
            if (h & 0xF800) == 0x4800:
                tgt = ((ca + 4) & ~3) + (h & 0xFF) * 4
                if tgt in seats:
                    out.append(ca)
            elif h == 0xF8DF:
                h2 = self.u16(ca + 2)
                tgt = ((ca + 4) & ~3) + (h2 & 0xFFF)
                if tgt in seats:
                    out.append(ca)
        return sorted(out)

    def keys(self, base, rows):
        return [(self.u16(base + i * 8), self.u32(base + i * 8 + 4))
                for i in range(rows)]

    def cstrings(self, lo, hi):
        """Printable runs (>=4 chars) in [lo,hi).  The CA 0x12266C table is a
        plain string blob but one entry is preceded by the 3-byte separator
        01 02 03, so run extraction (not NUL splitting) is the correct parse."""
        raw = self.raw
        out = []
        i = lo - BASE
        end = hi - BASE
        while i < end:
            if 32 <= raw[i] < 127:
                j = i
                while j < end and 32 <= raw[j] < 127:
                    j += 1
                if j - i >= 4:
                    out.append((i + BASE, raw[i:j].decode()))
                i = j
            else:
                i += 1
        return out


def worker(img, key):
    for base, rows in ((TAB_A, TAB_A_ROWS), (TAB_B, TAB_B_ROWS)):
        for k, w in img.keys(base, rows):
            if k == key:
                return w & ~1
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

    def expect(self, label, insn, addr, mnem, ops):
        got = insn.get(addr)
        want = re.sub(r"\s+", " ", mnem + " " + ops)
        ok = bool(got) and re.sub(r"\s+", " ", got[0] + " " + got[1]) == want
        return self.check(label, ok, "got %r want %r" % (got, want))


def capture_keys(img):
    keys = {}
    try:
        for line in open(CAPTURE_KEYS):
            m = re.search(r"^  key (0x[0-9a-f]{4}) n=\s*(\d+)", line)
            if m:
                keys[int(m.group(1), 16)] = int(m.group(2))
    except OSError:
        pass
    return keys


def selftest(img, insn):
    c = Checker()
    print("=== selftest: image identity ===")
    c.check("firmware length == %d" % FW_LEN, len(img.raw) == FW_LEN)
    c.check("firmware sha256 == 7fc87e20...311b", sha256(img.raw) == FW_SHA)

    print("\n=== A. timer words: complete literal reference set ===")
    c.check("0x401015CC literal refs == the six documented sites",
            img.literal_ref_sites(insn, SMAC_TIMER_W0) == W0_REF_SITES,
            "got %r" % img.literal_ref_sites(insn, SMAC_TIMER_W0))
    c.check("0x400024B0 literal refs == the three documented sites",
            img.literal_ref_sites(insn, SMAC_TIMER_W1) == W1_REF_SITES,
            "got %r" % img.literal_ref_sites(insn, SMAC_TIMER_W1))
    # start helper
    c.expect("start helper state read", insn, 0x8980C, "ldrb.w",
             "r3, [r0, #796]")
    c.expect("start helper guard", insn, 0x89810, "cbnz", "r3, 0x8982a")
    c.expect("start helper value 1", insn, 0x89812, "movs", "r2, #1")
    c.check("start helper literal == 0x401015CC",
            img.literal_at(insn, 0x89814) == SMAC_TIMER_W0)
    c.expect("start helper write-low-half", insn, 0x89816, "strh",
             "r2, [r3, #0]")
    c.expect("start helper derive interval word", insn, 0x89818, "sub.w",
             "r3, r3, #1044480")
    c.expect("start helper derive interval word 2", insn, 0x8981C, "sub.w",
             "r3, r3, #284")
    c.expect("start helper read old interval", insn, 0x89820, "ldrh",
             "r1, [r3, #0]")
    c.check("start helper save slot == 0x001664C8",
            img.literal_at(insn, 0x89822) == TIMER_SAVE_SLOT)
    c.expect("start helper save old interval", insn, 0x89824, "strh",
             "r1, [r2, #0]")
    c.expect("start helper interval 100", insn, 0x89826, "movs", "r2, #100")
    c.expect("start helper write interval", insn, 0x89828, "strh",
             "r2, [r3, #0]")
    c.expect("start helper refcount++", insn, 0x89830, "strb.w",
             "r3, [r0, #796]")
    # stop helper
    c.expect("stop helper state read", insn, 0x897D6, "ldrb.w",
             "r3, [r0, #796]")
    c.expect("stop helper decrement", insn, 0x897E4, "subs", "r3, #1")
    c.check("stop helper literal == 0x401015CC",
            img.literal_at(insn, 0x897EE) == SMAC_TIMER_W0)
    c.expect("stop helper write 0", insn, 0x897F0, "strh", "r3, [r2, #0]")
    c.check("stop helper save-slot literal",
            img.literal_at(insn, 0x897F2) == TIMER_SAVE_SLOT)
    c.check("stop helper interval literal",
            img.literal_at(insn, 0x897F6) == SMAC_TIMER_W1)
    c.expect("stop helper restore interval", insn, 0x897F8, "strh",
             "r2, [r3, #0]")
    c.expect("stop helper return", insn, 0x897FA, "bx", "lr")
    # callers
    for site, target in HELPER_CALLERS:
        c.expect("helper caller @%06x" % site, insn, site, "bl",
                 "0x%x" % target)
    for fn in HELPER_OWNERS:
        c.check("helper owner %06x is a real prologue" % fn,
                insn.get(fn) and insn[fn][0] in ("stmdb", "push"))
    c.expect("E8670 band selector", insn, 0xE86EE, "ldrb.w",
             "ip, [r4, #1]")
    c.expect("E8670 band0 MAC-start literal", insn, 0xE86FC, "ldr",
             "r1, [pc, #424]")
    c.check("E8670 band0 MAC-start literal == 0x4011080C",
            img.literal_at(insn, 0xE86FC) == 0x4011080C)
    c.expect("F3B22 freq compare (2400 = 2.4 GHz)", insn, 0xF3B36,
             "cmp.w", "r2, #2400")
    for site, val in sorted(TBTT_BLOCK_SITES.items()):
        c.check("TBTT block literal @%06x == %08x" % (site, val),
                img.literal_at(insn, site) == val)
    c.check("TBTT block set complete", set(TBTT_BLOCK_SITES.values()) ==
            set(TBTT_BLOCK))
    # the earlier start paths do not touch the timer words
    mac_lits = set()
    for site in insn:
        if 0x4AF0C <= site < 0x4B120:
            v = img.literal_at(insn, site)
            if v is not None and IO_LO <= v < 0x40200000:
                mac_lits.add(v)
    c.check("MAC start 0x4AF0C device-literal set == documented 10",
            mac_lits == {0x40114500, 0x4011451C, 0x40114520,
                         0x40110500, 0x4011051C, 0x40110520,
                         0x4010C220, 0x40108214, 0x4011080C, 0x40101220},
            "got %r" % sorted(mac_lits))
    c.check("MAC start does not touch the timer words",
            SMAC_TIMER_W0 not in mac_lits and SMAC_TIMER_W1 not in mac_lits)

    print("\n=== B. boot SMAC init CA 0xD1B68 ===")
    c.expect("init prologue", insn, 0xD1B6C, "stmdb", "sp!, {r4, r5, r6, r7, "
             "r8, r9, sl, fp, lr}")
    c.expect("init region length 7272", insn, 0xD1B68, "movw", "r3, #7272")
    c.check("init region base == 0x174BE0",
            img.literal_at(insn, 0xD1B76) == BAND_REGION)
    c.expect("init memset call", insn, 0xD1B78, "bl", "0xc1e0c")
    c.check("init per-band object base == 0x174C30",
            img.literal_at(insn, 0xD1C2A) == SMAC_INIT_OBJ0)
    c.expect("init band stride 3624", insn, 0xD1D70, "movw", "r8, #3624")
    c.expect("init stride multiply", insn, 0xD1D86, "mul.w", "r8, r8, r7")
    c.expect("band loop count read", insn, 0xD3F44, "ldrb", "r1, [r2, #14]")
    c.expect("band loop compare", insn, 0xD3F48, "cmp", "r1, r3")
    c.expect("band loop body branch", insn, 0xD3F4A, "bhi.w", "0xd1cb6")
    c.expect("band loop advance object", insn, 0xD3F3C, "addw",
             "r4, r4, #3624")
    c.expect("band loop advance index", insn, 0xD3F3A, "adds", "r7, #1")
    for site, base in sorted(DEV_CHAIN_A_BASES.items()):
        c.check("device chain A base literal @%06x == %08x" % (site, base),
                img.literal_at(insn, site) == base)
    for site, base in sorted(DEV_CHAIN_B_BASES.items()):
        c.check("device chain B base literal @%06x == %08x" % (site, base),
                img.literal_at(insn, site) == base)
    for site, off in sorted(DEV_CHAIN_A_SITES.items()):
        c.expect("device chain A store +%d" % off, insn, site, "str.w",
                 "r3, [r4, #%d]" % off)
    for site, off in sorted(DEV_CHAIN_B_SITES.items()):
        c.expect("device chain B store +%d" % off, insn, site, "str.w",
                 "r3, [r4, #%d]" % off)
    c.check("band0 SMAC base == 0x4010A000",
            img.literal_at(insn, 0xD1E44) == SMAC_BASE0)
    c.check("band1 SMAC base == 0x4010E000",
            img.literal_at(insn, 0xD2180) == SMAC_BASE1)
    for site, val in sorted(ATTACH_LITERALS.items()):
        got = img.literal_at(insn, site)
        c.check("RAM block attach literal @%06x == %08x" % (site, val),
                got == val, "got %r" % got)
    for site, (off, reg) in sorted(ATTACH_STORES.items()):
        c.expect("RAM block attach store +%d" % off, insn, site, "str.w",
                 "%s, [r4, #%d]" % (reg, off))
    c.check("attach +580 value is the +488 block",
            RAM_BLOCK + RAM_BLOCK_LEN == 0x00115100)
    c.expect("attach group memset call", insn, 0xD2C9C, "bl", "0xc1e0c")
    c.check("attach group base == 0x174E70",
            img.literal_at(insn, 0xD2C94) == BAND_REGION + 0x290)
    fill = img.literal_load_sites(RAM_BLOCK)
    c.check("0x114F18 has exactly the seven documented load sites",
            fill == sorted(RAM_BLOCK_LOADS),
            "got %r" % fill)
    for site, what in sorted(RAM_BLOCK_LOADS.items()):
        c.check("0x114F18 load @%06x (%s) inside the image" % (site, what),
                site in insn)
    c.check("fill source tables are the two literals",
            img.literal_at(insn, 0xD2BDA) == FILL_TABLES[0] and
            img.literal_at(insn, 0xD2C0A) == FILL_TABLES[1])
    c.expect("fill writes u16 into 0x114F18", insn, 0xD2BE6, "strh.w",
             "lr, [r3, r2, lsl #2]")
    c.expect("fill first loop ends at 37", insn, 0xD2C04, "cmp", "r2, #37")
    c.expect("fill second loop ends at 244", insn, 0xD2C3C, "cmp", "r2, #244")
    c.expect("fill block stride 244", insn, 0xD2C44, "adds", "r3, #244")
    c.check("only one direct caller of the SMAC init",
            [a for a in insn if insn[a][0] == "bl" and
             insn[a][1] == "0xd1b68"] == [SMAC_INIT_CALLER],
            "got %r" % [a for a in insn if insn[a][0] == "bl" and
                        insn[a][1] == "0xd1b68"])

    print("\n=== C. band objects / wrapper / ISR context ===")
    c.expect("band accessor band guard", insn, 0xF4F48, "cbnz",
             "r0, 0xf4f5a")
    c.expect("band accessor stride", insn, 0xF4F4E, "movw", "r0, #3624")
    c.check("band accessor base == 0x174BF0",
            img.literal_at(insn, 0xF4F52) == BAND_OBJ0)
    c.expect("band accessor mla", insn, 0xF4F54, "mla", "r0, r0, r1, r3")
    c.check("band1 object = band0 + stride",
            BAND_OBJ0 + BAND_STRIDE == BAND_OBJ1)
    c.expect("wrapper band0 load", insn, 0xAE48C, "ldr", "r3, [pc, #4]")
    c.check("wrapper literal == 0x180814",
            img.literal_at(insn, 0xAE48C) == WRAPPER_VAR)
    c.expect("wrapper band0 object load", insn, 0xAE48E, "ldr",
             "r0, [r3, #8]")
    c.expect("wrapper band1 object load", insn, 0xAE482, "ldr",
             "r0, [r3, #28]")
    c.expect("ISR body 0xAE45C context read", insn, 0xAE462, "ldr.w",
             "r2, [r0, #672]")
    c.expect("ISR body 0xAE45C status read", insn, 0xAE46A, "ldr.w",
             "r1, [r2, #232]")
    c.expect("ISR body 0xAE45C mask write", insn, 0xAE474, "str.w",
             "r3, [r2, #228]")
    c.expect("ISR body 0xAEB80 context read", insn, 0xAEB84, "ldr.w",
             "r2, [r0, #672]")
    c.expect("ISR body 0xAEB80 status read", insn, 0xAEB8A, "ldr.w",
             "r3, [r2, #232]")
    c.expect("ISR body 0xAEB80 mask write", insn, 0xAEBB0, "str.w",
             "r3, [r2, #228]")
    c.check("seven common ISR bodies documented", len(ISR_BODIES) == 7)

    print("\n=== D. per-band SMAC ISR name table CA 0x12266C ===")
    names = [s for _, s in img.cstrings(NAMES_LO, NAMES_HI)]
    c.check("name blob has 45 strings", len(names) == 45,
            "got %d" % len(names))
    c.check("first 7 name pairs (5g/2g) in order",
            names[:14] == [p + suf for p in NAME_PAIRS
                           for suf in ("_5g_isr", "_2g_isr")],
            "got %r" % names[:14])
    c.check("12 smac_msg_proc names present",
            all("smac_msg_proc_" + m in names for m in MSG_PROC))
    c.check("event names present",
            all(n in names for n in EVENT_NAMES))
    c.check("smac_tbtt names present", "smac_tbtt_5g_isr" in names and
            "smac_tbtt_2g_isr" in names)
    c.check("tbtt handler is not among the 8 registered INTC sources",
            len(REG_IRQ0) == 8 and len(REG_IRQ1) == 8)

    print("\n=== E. stock H2D keys vs our ladder ===")
    cap = capture_keys(img)
    c.check("stock capture keys parsed (25 distinct)", len(cap) == 25,
            "got %d" % len(cap))
    c.check("stock capture has the beacon/VAP/channel/RF keys",
            all(k in cap for k in (0x0127, 0x0124, 0x0065, 0x00D8, 0x01F7)))
    c.check("stock capture does not contain 0x0242/0x0243",
            0x0242 not in cap and 0x0243 not in cap)
    missing = [k for k in KEYS_LADDER if k not in cap]
    c.check("our keys absent from the stock boot == 0x0064/0x0067/0x0242/0243",
            sorted(missing) == [0x0064, 0x0067, 0x0242, 0x0243],
            "got %r" % [hex(k) for k in missing])
    stock_only = sorted(set(cap) - set(KEYS_LADDER))
    c.check("20 stock-only g0-id1 keys", len(stock_only) == 20,
            "got %d %r" % (len(stock_only), [hex(k) for k in stock_only]))
    for key, want in sorted(KEY_WORKERS.items()):
        c.check("key 0x%04x -> worker 0x%06x" % (key, want),
                worker(img, key) == want,
                "got %r" % (hex(worker(img, key)) if worker(img, key)
                            else None))

    print("\n=== F. negative results ===")
    all_workers = set(w & ~1 for _, w in img.keys(TAB_A, TAB_A_ROWS))
    all_workers |= set(w & ~1 for _, w in img.keys(TAB_B, TAB_B_ROWS))
    c.check("no dispatch worker is the SMAC init / timer helper / band start",
            not (all_workers & {SMAC_INIT, START_HELPER, STOP_HELPER,
                                0x9F2C4, 0xE8670, 0xF3B22, 0x971D8, 0x4AF0C}))
    return c


def decode(img, insn):
    print("the 5 GHz SMAC start decode (host-only)\n")
    print("BENCH DATUM (Lead 2026-11-26 11:30, 2.4 GHz differential):")
    print("  working 2.4 AP: 0x401015CC=0 0x400024B0=0x64 obj+672=RAM block")
    print("  silent  5G AP: 0x401015CC=0 0x400024B0=0x64 obj+672=0x40062000")
    print("  => the timer words are NOT the discriminator (task-214 latch")
    print("     hypothesis refuted); the per-band SMAC context pointer is.\n")
    print("A. timer words 0x401015CC/0x400024B0 complete reference sites:")
    for site in img.literal_ref_sites(insn, SMAC_TIMER_W0):
        print("   0x%06x -> 0x401015CC" % site)
    for site in img.literal_ref_sites(insn, SMAC_TIMER_W1):
        print("   0x%06x -> 0x400024B0" % site)
    print("   start helper CA 0x%06x (write 1), stop helper CA 0x%06x "
          "(write 0);" % (START_HELPER, STOP_HELPER))
    print("   called only from CA 0xE8670 and CA 0xF3B22 (per-band TBTT "
          "timing config).\n")
    print("B. boot SMAC init CA 0x%06x (caller 0x%06x):" %
          (SMAC_INIT, SMAC_INIT_CALLER))
    print("   memsets %d B at 0x%06x; loops band instances (base "
          "0x%06x + band*0x%X):" %
          (BAND_REGION_LEN, BAND_REGION, SMAC_INIT_OBJ0, BAND_STRIDE))
    for base_site, base in sorted(DEV_CHAIN_A_BASES.items()):
        print("   [obj+608..] device chain A base 0x%08x (literal @0x%06x)"
              % (base, base_site))
    for base_site, base in sorted(DEV_CHAIN_B_BASES.items()):
        print("   [obj+648..] device chain B base 0x%08x (literal @0x%06x)"
              % (base, base_site))
    print("   [obj+720/+724] SMAC bases 0x%08x/0x40109000 (band0) vs "
          "0x%08x/0x4010D000 (band1)" % (SMAC_BASE0, SMAC_BASE1))
    print("   [obj+576..] RAM block attach: " +
          " ".join("%08x" % b for b in RAM_BLOCK_SET))
    print("   address relation: obj=wrapper object, r4=obj+0x40 =>")
    print("     r4+608 = obj+672 (device base; band1 = 0x40062000)")
    print("     r4+576 = obj+640 (RAM block 0x114F18)\n")
    print("C. band objects: band0 0x%06x, band1 0x%06x (accessor CA "
          "0x%06x); ISR bodies read [obj+672]." %
          (BAND_OBJ0, BAND_OBJ1, BAND_ACCESSOR))
    print("   silent band1 obj+672 = the boot device-base default 0x40062000")
    print("   => the band-1 SMAC context never advanced to the attached")
    print("      RAM-block state the working 2.4 GHz band shows.\n")
    print("D. 0x114F18 load sites (five literal copies, seven loads):")
    for site in img.literal_load_sites(RAM_BLOCK):
        print("   0x%06x  %s" % (site, RAM_BLOCK_LOADS.get(site, "?")))


def tables(img, insn):
    print("T2 timer writers:")
    print("  %-10s %-10s %s" % ("site", "fn", "write"))
    rows = [(0x897EE, "0x897D6", "strh 0 -> 0x401015CC; restore interval"),
            (0x89814, "0x8980C", "strh 1 -> 0x401015CC; save; 100"),
            (0x9F436, "per-link init", "str count -> 0x401015CC"),
            (0x9F43E, "per-link init", "str 100 -> 0x400024B0"),
            (0xA0052, "per-link init", "str 0 -> 0x401015CC"),
            (0xA02A0, "per-link init", "str count -> 0x401015CC"),
            (0xA02A8, "per-link init", "str 100 -> 0x400024B0"),
            (0xA047A, "per-link init", "str 0 -> 0x401015CC")]
    for site, fn, w in rows:
        print("  0x%06x  %-10s %s" % (site, fn, w))
    print("\nT3 per-band object fields (r4 = 0x174C30 + band*0xE28):")
    print("  %-8s %-12s %-12s" % ("offset", "band0", "band1"))
    for off, b0, b1 in ((576, "0x114F18", "0x114F18"),
                        (584, "0x115128", "0x115128"),
                        (608, "0x40042000", "0x40062000"),
                        (648, "0x40080000", "0x400B0000"),
                        (672, "0x40083000", "0x400B3000"),
                        (720, "0x4010A000", "0x4010E000"),
                        (724, "0x40109000", "0x4010D000")):
        print("  +%-7d %-12s %-12s" % (off, b0, b1))
    print("\nT5 stock-vs-ladder g0-id1 keys:")
    cap = capture_keys(img)
    print("  stock keys (%d): %s" % (len(cap),
          " ".join("%04x" % k for k in sorted(cap))))
    print("  ladder keys:     %s" %
          " ".join("%04x" % k for k in KEYS_LADDER))
    print("  stock-only:      %s" %
          " ".join("%04x" % k for k in sorted(set(cap) - set(KEYS_LADDER))))
    print("  stock-only -> worker: " + " ".join(
        "%04x->%06x" % (k, worker(img, k)) for k in
        sorted(set(cap) - set(KEYS_LADDER)) if worker(img, k)))
    print("\nT6 ISR names CA 0x12266C: %d strings; first 7 pairs:" %
          len(img.cstrings(NAMES_LO, NAMES_HI)))
    for i, p in enumerate(NAME_PAIRS):
        print("  %d %s_{5g,2g}_isr -> body 0x%06x (IRQ %d/%d)" %
              (i + 1, p, ISR_BODIES[i], REG_IRQ0[i], REG_IRQ1[i]))


def probes(img, insn):
    print("R7 SMAC-context differential (read-only; the Lead fires)\n")
    print("device IO: BAR0 = CA + 0x3B8000 - 0x40000000, phys = CA + 0x3B8000")
    print("fw RAM:    BAR0 = CA + 0x6B8000, phys = 0x40000000 + CA + 0x6B8000\n")
    rows = [
        ("wrapper band0 obj", 0x18081C, 4, "0x00174BF0"),
        ("wrapper band1 obj", 0x180830, 4, "0x00175A18"),
        ("band0 obj+576", 0x174E30, 4, "0x00114F18 (attach)"),
        ("band0 obj+608", 0x174E5C, 4, "0x40042000"),
        ("band0 obj+672", 0x174E90, 4, "the Lead's +672 datum"),
        ("band1 obj+576", 0x175C58, 4, "attach slot"),
        ("band1 obj+608", 0x175C90, 4, "0x40062000"),
        ("band1 obj+672", 0x175CB8, 4, "0x40062000 (silent)"),
        ("band1 obj+640", 0x175C98, 4, "r4+576 if r4=obj+0x20"),
        ("band0 obj+640", 0x174E70, 4, "0x00114F18"),
        ("timer word 0", SMAC_TIMER_W0, 4, "0 on both bands"),
        ("timer word 1", SMAC_TIMER_W1, 2, "0x64 on both bands"),
        ("TBTT block band0 slot", 0x40101584, 4, "per-band TBTT slot"),
        ("TBTT block band1 slot", 0x40101588, 4, "per-band TBTT slot"),
        ("SMAC block 0x114F18", 0x114F18, 4, "attached block set"),
        ("SMAC block 0x115128", 0x115128, 4, "attached block set"),
        ("SMAC block 0x180E68", 0x180E68, 4, "attached block set"),
    ]
    for name, ca, width, note in rows:
        bar = ca + 0x6B8000 if ca < 0x40000000 else ca + 0x3B8000 - 0x40000000
        print("  %-22s CA %08x  w%d  BAR0 off %#x  %s" %
              (name, ca, width, bar, note))
    print("\nRead-only. Never write. Never read the RC misc window.")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--firmware", default=os.environ.get("BW_FIRMWARE",
                                                         DEFAULT_FW))
    ap.add_argument("--objdump", default=os.environ.get("BW_OBJDUMP",
                                                        DEFAULT_OD))
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--decode", action="store_true")
    ap.add_argument("--tables", action="store_true")
    ap.add_argument("--probes", action="store_true")
    args = ap.parse_args()
    if not (args.selftest or args.decode or args.tables or args.probes):
        ap.print_help()
        return 0
    img = Image(args.firmware)
    insn = load_disasm(args.objdump, args.firmware)
    if args.decode:
        decode(img, insn)
    if args.tables:
        tables(img, insn)
    if args.probes:
        probes(img, insn)
    if args.selftest:
        chk = selftest(img, insn)
        print("\n%s: %d assertions, %d failed" %
              ("PASS" if chk.bad == 0 else "FAIL", chk.n, chk.bad))
        return 0 if chk.bad == 0 else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
