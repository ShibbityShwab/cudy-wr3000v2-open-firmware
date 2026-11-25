#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_band1_start_re.py - how the stock arms the 5 GHz (band-1) MAC start.

Lane: wifi-band1-start (shared task task-197).  HOST-ONLY, read-only: the
pinned LOCAL vendor artifacts are read in place; only derived addresses,
instruction mnemonics, table words and probe addresses are printed.  No
vendor byte is copied into the repository and no device is touched.

Question: the R3 differential (2026-11-28) shows band-1 MAC start register
0x4010C220 = 0 while band-0 0x40108214 = 0xF, and the VAP-start hook
G+0x370 (CA 0x174B34) is NULL with no writer in the image.  The product is a
working dual-band AX3000, so the stock 5 GHz start must be reached some other
way.  This tool re-derives the alternatives:

  * the per-band start CA 0x4AF0C: its exact register writes and its five
    direct call sites (no G+0x370 needed);
  * which H2D config-event keys reach those call sites (table A workers):
    key 0x242 (WAL set_green_ap, worker 0x4C524, subcommand in body[0]);
    key 0x243 (worker 0x8419C -> helper 0x4BDCE); key 0x67/0x1FE paths;
  * the G+0x370 no-writer recheck (literals and stores);
  * the enable-TRX worker 0xE8520 and the band probe helpers 0xD9604/0xD968C/
    0xD973C/0xD97BC (the only writers of the TRX triple);
  * the fresh band-1-bound module context (key 0x0064 body[4]/body[0x12]);
  * the read-only probe pack and the fire recipe (in ../FIRE-RECIPE.md).

Conventions (pinned by earlier lanes): CPU = file offset + 0x40000;
firmware RAM phys = 0x40000000 + CA + 0x6B8000;
device IO phys = CA + 0x3B8000.

Modes: --selftest (default), --start, --xrefs, --hooks, --keys, --probe,
--recipe, --modules [DIR].
"""
import argparse
import hashlib
import os
import struct
import sys

BASE = 0x40000
DEFAULT_FW = ("/home/shibbityshwab/recovery-extract/lib/firmware/"
              "hi_wifi/FIRMWARE.bin")
FW_SHA = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_LEN = 928920
MOD_DIR = "/home/shibbityshwab/recovery-extract/lib/hisilicon/ko"
RAM_PHYS = 0x40000000
RAM_OFF = 0x6B8000
IO_OFF = 0x3B8000

# globals / firmware addresses (CPU address space)
G = 0x1747C4
CB_MAC = G + 0x370            # 0x174B34, band-1 MAC-start hook (NULL)
CB_BCN = G + 0x374            # 0x174B38, beacon hook
INSTALLER = 0x5B424           # pre-HCC G-callback installer
INSTALLER_CALLER = 0x48442    # bl 0x5b424 in the pre-HCC boot init
BAND_START = 0x4AF0C          # per-band MAC start (R3's 0x1029 writer)
BAND_STOP = 0xBE294           # per-band MAC stop
BAND_START_CALLERS = [0x4BEB0, 0x4C34E, 0x4C3EC, 0x4C612, 0x4C652]
VAP_ON_BAND = 0x971D8         # VAP start-on-band: calls G+0x370/G+0x374
VAP_ON_BAND_CALLERS = [0x46038, 0x464B0, 0x8498E]
TAB_A = 0x10C23C
TAB_B = 0x103BF8
GREEN_AP_WORKER = 0x4C524     # table A row 76, key 0x242
RECENT_CFG_WORKER = 0x8419C   # table A row 77, key 0x243
BAND_HELPER = 0x4BDCE         # direct caller wrapper (contains 0x4BEB0)
VAP_STATE_SYN_WORKER = 0x84920  # table A row 20, key 0x73
TRX_WORKER = 0xE8520          # enable-TRX / disable-TRX message processor
BAND0_PROBE = 0xD973C         # writes 0x40108214 patterns, final 0xF
BAND1_PROBE = 0xD9604         # writes 0x4010C220 patterns, final 0x1039
VAP_ADD_WORKER = 0x449E4      # table A row 1, key 0x0064
BAND_OBJ = {0: 0x15CA08, 1: 0x15E570}
BAND_STATE_OFF = 0x14D0       # band object run state (u8[1], u8[4], +0x64)
REGS = [
    ("band1_smac_irq_en", 0x4010C220),
    ("band0_en", 0x40108214),
    ("band0_trx", 0x4010820C),
    ("gate_band0", 0x4011080C),
    ("gate_band1", 0x4011480C),
    ("run_bits", 0x40101220),
    ("band1_trx0", 0x4010E150),
    ("band1_trx1", 0x4010E154),
    ("band1_trx2", 0x4010E158),
]


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


class Fw:
    def __init__(self, path):
        self.path = path
        self.data = open(path, "rb").read()
        self.len = len(self.data)
        self.sha = sha256_file(path)

    def off(self, ca):
        o = ca - BASE
        return o if 0 <= o and o + 4 <= self.len else None

    def u32(self, ca):
        o = self.off(ca)
        return None if o is None else struct.unpack("<I", self.data[o:o + 4])[0]

    def u16(self, ca):
        o = self.off(ca)
        return None if o is None else struct.unpack("<H", self.data[o:o + 2])[0]

    def u8(self, ca):
        o = ca - BASE
        return self.data[o] if 0 <= o < self.len else None

    def find_u32(self, val):
        pat = struct.pack("<I", val)
        out, i = [], self.data.find(pat)
        while i != -1:
            out.append(i + BASE)
            i = self.data.find(pat, i + 1)
        return out


def capstone():
    try:
        import capstone  # noqa: F401
        return capstone
    except ImportError:
        sys.exit("need capstone (use .provision-venv/bin/python, "
                 "capstone 5.0.7 is provisioned)")


def dis(fw, ca, n=6, thumb=True):
    """Disassemble n instructions at CA (thumb bit tolerated)."""
    cs = capstone()
    md = cs.Cs(cs.CS_ARCH_ARM, cs.CS_MODE_THUMB if thumb else cs.CS_MODE_ARM)
    md.skipdata = True
    o = (ca & ~1) - BASE
    out = []
    for ins in md.disasm(fw.data[o:o + 0x100], ca & ~1):
        out.append(ins)
        if len(out) >= n:
            break
    return out


def text_ins(fw, ca, n=1, thumb=True):
    return [(i.mnemonic + " " + i.op_str).strip()
            for i in dis(fw, ca, n, thumb)]


def scan_calls(fw):
    """Whole-image Thumb sweep: map branch-target -> sorted call sites."""
    cs = capstone()
    md = cs.Cs(cs.CS_ARCH_ARM, cs.CS_MODE_THUMB)
    md.detail = True
    md.skipdata = True
    lo, hi = BASE, BASE + fw.len
    calls = {}
    for ins in md.disasm(fw.data, BASE):
        if ins.id in (cs.arm.ARM_INS_BL, cs.arm.ARM_INS_BLX, cs.arm.ARM_INS_B):
            for op in ins.operands:
                if op.type == cs.arm.ARM_OP_IMM:
                    t = op.imm & ~1
                    if lo <= t < hi:
                        calls.setdefault(t, []).append(ins.address)
    return {k: sorted(set(v)) for k, v in calls.items()}


def scan_stores(fw, disp):
    """Whole-image store instructions with an immediate disp; returns
    (site, mnemonic, op_str, base-register-name-or-None)."""
    cs = capstone()
    md = cs.Cs(cs.CS_ARCH_ARM, cs.CS_MODE_THUMB)
    md.detail = True
    md.skipdata = True
    out = []
    for ins in md.disasm(fw.data, BASE):
        if not ins.mnemonic.startswith("str"):
            continue
        for op in ins.operands:
            if op.type == cs.arm.ARM_OP_MEM and op.mem.disp == disp:
                base = None
                try:
                    base = ins.reg_name(op.mem.base)
                except Exception:
                    pass
                out.append((ins.address, ins.mnemonic, ins.op_str, base))
    return out


def load_table(fw, base, n, tag):
    rows = []
    for i in range(n):
        key = fw.u16(base + 8 * i)
        worker = fw.u32(base + 8 * i + 4)
        if key is None or worker is None:
            continue
        rows.append((i, key, worker & ~1, tag))
    return rows


def key_map(fw):
    rows = load_table(fw, TAB_A, 217, "A") + load_table(fw, TAB_B, 88, "B")
    m = {}
    for i, k, w, t in rows:
        m.setdefault(k, []).append((t, i, w))
    return m


class Checks:
    def __init__(self):
        self.n = 0
        self.fail = 0

    def check(self, name, cond, detail=""):
        self.n += 1
        if cond:
            print("PASS %2d %s%s" % (self.n, name, ("  [" + detail + "]")
                                     if detail else ""))
        else:
            self.fail += 1
            print("FAIL %2d %s%s" % (self.n, name, ("  [" + detail + "]")
                                     if detail else ""))


def phys_ram(ca):
    return RAM_PHYS + ca + RAM_OFF


def phys_io(ca):
    return ca + IO_OFF


def cmd_selftest(fw):
    c = Checks()
    c.check("firmware size pinned", fw.len == FW_LEN, str(fw.len))
    c.check("firmware sha256 pinned", fw.sha == FW_SHA, fw.sha[:16] + "...")

    # CA convention: the VAP-on-band ldr [pc,#264] -> G literal at 0x9745C
    ins = text_ins(fw, 0x97352)
    c.check("CA convention: 0x97352 is 'ldr r7, [pc, #264]'",
            ins and ins[0].startswith("ldr r7, [pc, #") , str(ins))
    c.check("CA convention: literal at 0x9745C is G (0x1747C4)",
            fw.u32(0x9745C) == G, hex(fw.u32(0x9745C) or 0))

    # band start prologue and register writes
    c.check("band start 0x4AF0C starts with a push",
            text_ins(fw, 0x4AF0C)[0].startswith(("push", "stmdb")),
            text_ins(fw, 0x4AF0C)[0])
    pool = {0x4B0F4: 0x4011451C, 0x4B0FC: 0x4010C220, 0x4B100: 0x4011080C,
            0x4B104: 0x40101220, 0x4B114: 0x40108214, 0x4B118: 0x40114500,
            0x4B11C: 0x40110500}
    ok = all(fw.u32(k) == v for k, v in pool.items())
    c.check("band start literal pool writes the R3 registers", ok,
            "; ".join("%s=%s" % (hex(k), hex(fw.u32(k) or 0))
                      for k in sorted(pool)))
    w = text_ins(fw, 0x4AF5C, 2)
    c.check("band byte source is u8(perlink+0x11D) at 0x4AF5C",
            w and "0x11d" in w[0], str(w))
    c.check("band==1 branch writes 0x4010C220 (pool 0x4B0FC)",
            fw.u32(0x4B0FC) == 0x4010C220)
    c.check("band==0 branch writes 0x40108214 (pool 0x4B114)",
            fw.u32(0x4B114) == 0x40108214)

    calls = scan_calls(fw)
    c.check("band start has exactly five direct BL sites",
            calls.get(BAND_START) == sorted(BAND_START_CALLERS),
            str([hex(x) for x in calls.get(BAND_START, [])]))
    c.check("VAP-on-band 0x971D8 has exactly three BL sites",
            calls.get(VAP_ON_BAND) == sorted(VAP_ON_BAND_CALLERS),
            str([hex(x) for x in calls.get(VAP_ON_BAND, [])]))

    # key table rows (exact worker mapping)
    km = key_map(fw)
    expect = {0x0064: 0x449E4, 0x0065: 0x46394, 0x0067: 0x45F74,
              0x0073: 0x84920, 0x0242: 0x4C524, 0x0243: 0x8419C,
              0x01FE: 0xA14BC, 0x0124: 0x841B8, 0x0127: 0x84DA4,
              0x01F7: 0xB5B9C}
    for k, wkr in expect.items():
        got = [w for (_, _, w) in km.get(k, [])]
        c.check("table key 0x%04x -> worker 0x%05x" % (k, wkr),
                got == [wkr], str([hex(x) for x in got]))

    # key 0x242 worker: tbb dispatch on body[0]
    c.check("green-ap worker 0x4C524 prologue",
            text_ins(fw, 0x4C524)[0].startswith(("push", "b5")),
            text_ins(fw, 0x4C524)[0])
    c.check("green-ap worker reads band object ctx+0x4B8",
            "0x4b8" in text_ins(fw, 0x4C526)[0], text_ins(fw, 0x4C526)[0])
    tab = [fw.u8(0x4C540 + i) for i in range(9)]
    tgt = [0x4C540 + 2 * b for b in tab]
    c.check("green-ap tbb subcommand table (9 entries)",
            tab == [0x05, 0x51, 0x7A, 0x91, 0xB3, 0xBD, 0xC0, 0xC3, 0xC6],
            str(tgt))
    c.check("green-ap subcmd 1 -> conditional start (0x4C5E2)",
            tgt[1] == 0x4C5E2)
    c.check("green-ap subcmd 2 -> forced start 0x4AF0C at 0x4C652",
            tgt[2] == 0x4C634 and calls.get(BAND_START, []).count(0x4C652) == 1)
    c.check("green-ap subcmd 3 -> stop 0xBE294 at 0x4C6A0",
            tgt[3] == 0x4C662 and calls.get(BAND_STOP, []).count(0x4C6A0) == 1)
    c.check("green-ap subcmd 1 path start site 0x4C612",
            tgt[1] == 0x4C5E2 and 0x4C612 in calls.get(BAND_START, []))

    # key 0x243 worker -> helper 0x4BDCE -> band start
    c.check("recent-cfg worker 0x8419C calls helper 0x4BDCE with r1=5",
            text_ins(fw, 0x841A8)[0].startswith("movs r1, #5") and
            (0x4BDCE in calls and 0x841AA in calls[0x4BDCE]),
            str(text_ins(fw, 0x841A6, 3)))
    c.check("helper 0x4BDCE contains a band-start call site 0x4BEB0",
            0x4BEB0 in calls.get(BAND_START, []))

    # key 0x73 worker -> hook path only
    c.check("vap-state-syn worker 0x84920 calls 0x971D8 at 0x8498E",
            0x8498E in calls.get(VAP_ON_BAND, []) and
            text_ins(fw, 0x8498E)[0].startswith("bl"),
            text_ins(fw, 0x8498E)[0])

    # G+0x370: no writer in the image (the whole-image negative)
    c.check("no literal 0x174B34 in the image",
            fw.find_u32(CB_MAC) == [], str(fw.find_u32(CB_MAC)))
    c.check("no literal 0x174B38 in the image",
            fw.find_u32(CB_BCN) == [], str(fw.find_u32(CB_BCN)))
    g370 = scan_stores(fw, 0x370)
    c.check("19 image store instructions use disp 0x370 (all non-G bases)",
            len(g370) == 19, str([hex(a) for a, _, _, _ in g370]))
    c.check("no disp-0x370 store inside the G-callback installer",
            not any(INSTALLER <= a < 0x5D400 for a, _, _, _ in g370))
    slot = {0x364: 0x5D13A, 0x368: 0x5CFC2, 0x36C: 0x5CF60,
            0x374: 0x5D144, 0x378: 0x5C3B6}
    got = {}
    for disp, site in slot.items():
        got[disp] = any(a == site for a, _, _, _ in scan_stores(fw, disp))
    c.check("installer writes the five other hook slots, never 0x370",
            all(got.values()), str(got))
    c.check("installer is called from the pre-HCC boot init at 0x48442",
            0x48442 in calls.get(INSTALLER, []),
            str([hex(x) for x in calls.get(INSTALLER, [])]))

    # enable-TRX worker and the probe helpers
    c.check("TRX triple literal 0x4010E150 appears once (worker 0xE8520)",
            fw.find_u32(0x4010E150) == [0xE8660],
            str([hex(x) for x in fw.find_u32(0x4010E150)]))
    c.check("TRX worker writes 0x33/0xFF/0xFF to 0x4010E150/154/158",
            text_ins(fw, 0xE85E6, 1)[0].startswith("movs r2, #0x33") and
            text_ins(fw, 0xE85F0, 1)[0].startswith("str r3, [r2]") and
            text_ins(fw, 0xE85F2, 1)[0].startswith("str r3, [r2, #4]"),
            str(text_ins(fw, 0xE85E6, 7)))
    c.check("band0 probe helper 0xD973C targets 0x40108214",
            fw.u32(0xD97AC) == 0x40108214)
    c.check("band1 probe helper 0xD9604 targets 0x4010C220",
            fw.u32(0xD967C) == 0x4010C220)
    c.check("band1 reset helper 0xD968C targets 0x40114500",
            fw.u32(0xD972C) == 0x40114500)
    c.check("band0 reset helper 0xD97BC targets 0x40110500",
            fw.u32(0xD985C) == 0x40110500)

    # fresh band-1 module context (key 0x0064)
    c.check("VAP-add worker 0x449E4 band lookup from body[0x12] at 0x44B6C",
            "0x12" in text_ins(fw, 0x44B6C)[0] and
            text_ins(fw, 0x44B70)[0].startswith("bl"),
            str(text_ins(fw, 0x44B6C, 3)))
    binds = [0x44BBC, 0x44BC2, 0x44BC6]
    btxt = [text_ins(fw, a)[0] for a in binds]
    c.check("VAP-add binds ctx+0x4B0/+0x4B8/+0x4B4 at 0x44BBC/42/46",
            all("0x4b" in t for t in btxt), str(btxt))

    # probe pack arithmetic
    c.check("G+0x370 RAM phys = 0x4082CB34", phys_ram(CB_MAC) == 0x4082CB34,
            hex(phys_ram(CB_MAC)))
    c.check("band1 0x4010C220 IO phys = 0x404C4220",
            phys_io(0x4010C220) == 0x404C4220, hex(phys_io(0x4010C220)))

    print("\n%d checks, %d FAIL" % (c.n, c.fail))
    return 0 if c.fail == 0 else 1


def cmd_start(fw):
    print("== band start CA 0x%x (per-band MAC start; R3's 0x4010C220 |= 0x1029)"
          % BAND_START)
    for i in dis(fw, BAND_START, 6):
        print("  %06x: %s %s" % (i.address, i.mnemonic, i.op_str))
    print("\n== register literals of the function")
    for ca in (0x4B0F4, 0x4B0FC, 0x4B100, 0x4B104, 0x4B114, 0x4B118,
               0x4B11C, 0x4B108):
        print("  pool CA 0x%05x = 0x%08x" % (ca, fw.u32(ca)))
    print("\n== band==1 branch: OR 1,8,0x20,0x1000 into 0x4010C220")
    for i in dis(fw, 0x4AFB2, 20):
        print("  %06x: %s %s" % (i.address, i.mnemonic, i.op_str))
    print("\n== band==0 branch: OR 1,2,4,8 into 0x40108214")
    for i in dis(fw, 0x4B0C6, 12):
        print("  %06x: %s %s" % (i.address, i.mnemonic, i.op_str))


def cmd_xrefs(fw):
    calls = scan_calls(fw)
    print("== direct BL sites of the band start CA 0x%x" % BAND_START)
    for a in calls.get(BAND_START, []):
        print("  0x%05x" % a)
    print("\n== enclosing helpers")
    print("  0x4BDCE (contains 0x4BEB0) is called from:",
          " ".join(hex(x) for x in calls.get(BAND_HELPER, [])))
    print("  0x4C524 is table A key 0x242 worker (green AP)")
    print("  0x4C3AC is called from:", " ".join(hex(x)
          for x in calls.get(0x4C3AC, [])))
    print("\n== key -> worker chain reaching the band start")
    km = key_map(fw)
    for k in (0x0242, 0x0243, 0x01FE, 0x0067, 0x0065, 0x0073):
        print("  key 0x%04x rows: %s" % (k, [(t, i, hex(w)) for t, i, w
                                            in km.get(k, [])]))
    print("\n== stop CA 0x%x callers" % BAND_STOP,
          " ".join(hex(x) for x in calls.get(BAND_STOP, [])))


def cmd_hooks(fw):
    print("== G hook block CA 0x1747C4; G+0x370 = CA 0x174B34")
    print("  literal 0x174B34 occurrences:", fw.find_u32(CB_MAC))
    print("  literal 0x174B38 occurrences:", fw.find_u32(CB_BCN))
    print("  stores with disp 0x370:")
    for a, m, s, b in scan_stores(fw, 0x370):
        print("    %06x: %s %s   (base %s)" % (a, m, s, b))
    print("\n== the VAP-on-band hook user (the only G+0x370/G+0x374 reader)")
    for i in dis(fw, 0x97352, 12):
        print("  %06x: %s %s" % (i.address, i.mnemonic, i.op_str))
    print("\n== installer slots (five installed, 0x370 never)")
    for d, site in ((0x364, 0x5D13A), (0x368, 0x5CFC2), (0x36C, 0x5CF60),
                    (0x374, 0x5D144), (0x378, 0x5C3B6)):
        print("  G+0x%03x <- 0x%05x" % (d, site))


def cmd_keys(fw):
    print("== key 0x242 worker 0x4C524: subcommand jump table")
    tab = [fw.u8(0x4C540 + i) for i in range(9)]
    names = {0: "diag", 1: "sync/conditional start", 2: "forced start",
             3: "stop", 4: "flag bit1", 5: "state+0x6C", 6: "state+0x70",
             7: "state[5]", 8: "state[6]"}
    for i, b in enumerate(tab):
        print("  body[0]=%d -> 0x%05x  %s" % (i, 0x4C540 + 2 * b,
                                              names.get(i, "")))
    print("\n== key 0x243 worker 0x8419C -> 0x4BDCE(bandobj, 5, 0)")
    for i in dis(fw, 0x8419C, 7):
        print("  %06x: %s %s" % (i.address, i.mnemonic, i.op_str))
    print("\n== key 0x73 worker 0x84920: body[0]=4 -> 0x971D8 at 0x8498E")
    for i in dis(fw, 0x84920, 6):
        print("  %06x: %s %s" % (i.address, i.mnemonic, i.op_str))
    for i in dis(fw, 0x84986, 4):
        print("  %06x: %s %s" % (i.address, i.mnemonic, i.op_str))
    print("\n== key 0x64 worker 0x449E4: band bind")
    for i in dis(fw, 0x44B6C, 8):
        print("  %06x: %s %s" % (i.address, i.mnemonic, i.op_str))


def cmd_probe(fw):
    print("== P1 read-only probe pack (phys, read-only; the Lead fires)")
    items = [
        ("G head", phys_ram(G)),
        ("G+0x370 hook", phys_ram(CB_MAC)),
        ("G+0x374 hook", phys_ram(CB_BCN)),
        ("band1 0x4010C220", phys_io(0x4010C220)),
        ("band0 0x40108214", phys_io(0x40108214)),
        ("band0 0x4010820C", phys_io(0x4010820C)),
        ("gate band0 0x4011080C", phys_io(0x4011080C)),
        ("gate band1 0x4011480C", phys_io(0x4011480C)),
        ("run bits 0x40101220", phys_io(0x40101220)),
        ("band1 trx 0x4010E150", phys_io(0x4010E150)),
        ("band1 trx 0x4010E154", phys_io(0x4010E154)),
        ("band1 trx 0x4010E158", phys_io(0x4010E158)),
        ("band0 MAC 0x40110500", phys_io(0x40110500)),
        ("band1 MAC 0x40114500", phys_io(0x40114500)),
        ("band0 MAC 0x4011051C", phys_io(0x4011051C)),
        ("band1 MAC 0x4011451C", phys_io(0x4011451C)),
        ("band obj0 0x15CA08", phys_ram(BAND_OBJ[0])),
        ("band obj1 0x15E570", phys_ram(BAND_OBJ[1])),
        ("band1 state +0x14D0", phys_ram(BAND_OBJ[1] + BAND_STATE_OFF)),
        ("band0 state +0x14D0", phys_ram(BAND_OBJ[0] + BAND_STATE_OFF)),
        ("band1 perlink ptr +0x1A00", phys_ram(BAND_OBJ[1] + 0x1A00)),
        ("band0 perlink ptr +0x1A00", phys_ram(BAND_OBJ[0] + 0x1A00)),
        ("ctx slot table 0x171458", phys_ram(0x171458)),
    ]
    for name, p in items:
        print("  %-28s phys 0x%08x" % (name, p))
    print("\n  after-fire: 0x4010C220 must contain 0x1029; state[1] == 2;")
    print("              state +0x64 increments; new ctx+0x4B8 == 0x15E570")


def cmd_recipe(fw):
    print("== R3b-extension rungs (the Lead fires; host-only lane)")
    print("  F0 read P1 pack; 2.4 GHz reference must be alive.")
    print("  F1 g0-id1 0x0064: field B=0 len=24, data[4]=N (free module,")
    print("     <=0x12), data[0x12]=1 (band1), data[1]=0x12 (parent),")
    print("     data[0]=2 (AP).  Check ctx(N)+0x4B8 == 0x15E570.")
    print("  F2 g0-id1 0x0242: field B=N, len>=5,")
    print("     body[0]=1 (sync start), body[2]=1 -> band start 0x4AF0C")
    print("     via 0x4C5E2/0x4C612. Check 0x4010C220 & 0x1029.")
    print("  F3 if F2 did not arm: 0x0242 field B=N body[0]=2 (forced")
    print("     start 0x4C634 -> 0x4AF0C at 0x4C652). Check again.")
    print("  F4 if still not armed: 0x0243 field B=N (worker 0x8419C ->")
    print("     0x4BDCE(bandobj,5,0); arms only when state[1]==3).")
    print("  F5 re-read P1; classify; beacon check. Abort rules in")
    print("     ../FIRE-RECIPE.md.")


def cmd_modules(fw, directory):
    print("== boot-module scan for firmware-callback constants (%s)" % directory)
    vals = [G, CB_MAC, CB_BCN, BAND_START, BAND_START | 1,
            VAP_ON_BAND, VAP_ON_BAND | 1, 0x57639]
    found = 0
    for name in sorted(os.listdir(directory)):
        if not name.endswith(".ko"):
            continue
        path = os.path.join(directory, name)
        d = open(path, "rb").read()
        for v in vals:
            for pat, tag in ((struct.pack("<I", v), "le"),
                             (struct.pack(">I", v), "be")):
                i = d.find(pat)
                if i != -1:
                    print("  %s: 0x%x %s @ file 0x%x" % (name, v, tag, i))
                    found += 1
    print("  interpretation: a firmware CA inside a host-built H2D body")
    print("  would be little-endian; a big-endian-only hit is unrelated")
    print("  module data (the lone BE hit above is such a coincidence).")
    if not found:
        print("  no firmware G/callback constant in any boot module [V]")
        print("  (plat.ko downloads FIRMWARE.bin as-is via bal_write; the G")
        print("   hooks are installed by firmware code CA 0x5B424, not by host)")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--fw", default=DEFAULT_FW)
    ap.add_argument("--modules", nargs="?", const=MOD_DIR, metavar="DIR")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--start", action="store_true")
    ap.add_argument("--xrefs", action="store_true")
    ap.add_argument("--hooks", action="store_true")
    ap.add_argument("--keys", action="store_true")
    ap.add_argument("--probe", action="store_true")
    ap.add_argument("--recipe", action="store_true")
    a = ap.parse_args(argv)
    if not os.path.exists(a.fw):
        sys.exit("missing firmware artifact: %s" % a.fw)
    fw = Fw(a.fw)
    if fw.sha != FW_SHA:
        sys.exit("firmware sha mismatch: %s" % fw.sha)
    if a.modules:
        if os.path.isdir(a.modules):
            cmd_modules(fw, a.modules)
        else:
            print("module dir not present: %s" % a.modules)
    if a.start:
        cmd_start(fw)
    if a.xrefs:
        cmd_xrefs(fw)
    if a.hooks:
        cmd_hooks(fw)
    if a.keys:
        cmd_keys(fw)
    if a.probe:
        cmd_probe(fw)
    if a.recipe:
        cmd_recipe(fw)
    if not any((a.start, a.xrefs, a.hooks, a.keys, a.probe, a.recipe,
                a.modules)):
        return cmd_selftest(fw)
    return 0


if __name__ == "__main__":
    sys.exit(main())
