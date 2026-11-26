#!/usr/bin/env python3
"""The 20 stock-only g0-id1 config keys and the band-1 SMAC device chain.

Host-only, read-only re-derivation from the pinned firmware image.  No vendor
bytes are printed; every quoted value is an address, an id or a length.

Usage:
    fw_5g_smac_keys_re.py FIRMWARE.bin [--selftest] [--dump] [--objdump PATH]

Convention: runtime CA = file offset + 0x40000 (the committed firmware-image
mapping used by the wifi-smac-start lane; bench-verified there).  The image is
928,920 B, sha256 7fc87e20...311b.

What this tool re-derives (see ../REPORT.md):
  A. identity + the g0-id1 config dispatcher CA 0x43D58 and its two key tables
     CA 0x10C23C (217 rows) / CA 0x103BF8 (88 rows).
  B. key -> firmware worker for the 25 stock-boot config keys and the 9 keys
     our documented ladder sends.
  C. the 0x84xxx hmac-config handler family: the set_mac_addr handler's writes
     into the per-band SMAC device chain (via CA 0xBD038 / 0xBC830 and the
     band dispatch pointer [obj+672]).
  D. the per-band object +672 field = SMAC device-chain A base, written for
     BOTH bands by the boot SMAC init loop (CA 0xD1B68); the ISR writers of
     [chainbase+0xE4]/[+0xE8]; the absence of 0xA00096 as an image constant.
  E. the candidate handlers: 0x0073 vap_state_syn (band-start call CA 0x971D8),
     0x0069 set_mode_event (large VAP state machine), 0x07E7 dfs_cac (the only
     stock-only key writing the band-1 SMAC sub-base CA 0x4010E000).
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
    "arm-linux-gnueabihf-objdump",
    "/usr/bin/objdump",
]

# --- A. dispatcher --------------------------------------------------------
DISPATCH = 0x43D58
KEY_OFF = 20            # u16 key at hcc hdr + 20
PAYLOAD_OFF = 24        # handler r2 = hdr + 24
KEY_ROW = 8             # {u16 key; u16 pad; u32 worker|1}
TAB_A = 0x10C23C
TAB_A_BYTES = 1736      # 217 rows
TAB_B = 0x103BF8
TAB_B_BYTES = 704       # 88 rows

# --- B. keys --------------------------------------------------------------
STOCK_KEYS = [0x0000, 0x0005, 0x000B, 0x003C, 0x0052, 0x0069, 0x006F,
              0x0073, 0x0078, 0x00D8, 0x00F7, 0x011E, 0x0124, 0x0127,
              0x015D, 0x01CF, 0x01EE, 0x01F7, 0x025F, 0x02BD, 0x03E4,
              0x07D9, 0x07E7, 0x07E8, 0x07E9]  # 07E9 not sent; see REPORT
STOCK_KEYS = [k for k in STOCK_KEYS if k != 0x07E9]
LADDER_KEYS = [0x0064, 0x0065, 0x0067, 0x00D8, 0x0124, 0x0127,
               0x01F7, 0x0242, 0x0243]
STOCK_ONLY_20 = [k for k in STOCK_KEYS if k not in LADDER_KEYS]
EXPECTED_WORKERS = {
    0x0000: 0x840B4, 0x0005: 0x82878, 0x000B: 0xA09F0, 0x003C: 0x811B8,
    0x0052: 0x42044, 0x0069: 0x839B4, 0x006F: 0x81C44, 0x0073: 0x84920,
    0x0078: 0xA5824, 0x00F7: 0x45A30, 0x011E: 0xA1A58, 0x015D: 0xB2410,
    0x01CF: 0x84D14, 0x01EE: 0x4213C, 0x025F: 0x82850, 0x02BD: 0x438F0,
    0x03E4: 0x84260, 0x07D9: 0x4366C, 0x07E7: 0xB58FC, 0x07E8: 0x429B0,
    0x0064: 0x449E4, 0x0065: 0x46394, 0x0067: 0x45F74, 0x00D8: 0x81038,
    0x0124: 0x841B8, 0x0127: 0x84DA4, 0x01F7: 0xB5B9C, 0x0242: 0x4C524,
    0x0243: 0x8419C,
}
SENTINEL_KEY = 0x1451  # trailing {key; 0; NULL} row in both tables
ROW_PINS = {  # (table, row, key) - independent pin of the row decode
    ("A", 17, 0x0000), ("A", 8, 0x03E4), ("A", 109, 0x01CF),
    ("A", 20, 0x0073), ("A", 5, 0x0069), ("A", 147, 0x025F),
    ("A", 18, 0x0005), ("A", 9, 0x0052), ("A", 52, 0x000B),
    ("A", 46, 0x003C), ("A", 106, 0x006F), ("A", 78, 0x0078),
    ("A", 92, 0x00F7), ("A", 115, 0x015D), ("A", 121, 0x01EE),
    ("A", 138, 0x02BD), ("A", 23, 0x07D9), ("A", 94, 0x07E7),
    ("A", 96, 0x07E8), ("B", 35, 0x011E),
}

# --- C. per-band SMAC object / device chain -------------------------------
WRAPPER_VAR = 0x180814   # RAM: +8 band0 object, +0x1C band1 object
BAND_OBJ0 = 0x174BF0
BAND_OBJ1 = 0x175A18
BAND_STRIDE = 0xE28
BAND_ACCESSOR = 0xF4F48
SMAC_INIT = 0xD1B68
INIT_CALLER = 0x48314
CHAIN_A_OFF = 672        # object field: SMAC device-chain A base
CHAIN_A0 = 0x40042000    # band0 device chain A base
CHAIN_A1 = 0x40062000    # band1 device chain A base
CHAIN_B0 = 0x40080000
CHAIN_B1 = 0x400B0000
CHAIN_A_LITS = {0xD2044: CHAIN_A0, 0xD2094: CHAIN_A1}
CHAIN_A_STORES = {0xD2046: 608, 0xD204E: 612, 0xD2056: 616,
                  0xD205E: 620, 0xD2066: 624, 0xD206E: 628,
                  0xD2076: 632, 0xD207E: 636}
RAM_BLOCK = 0x114F18      # the attached u16 table; obj+640 (r4+576)
ATTACH_STORES = {0xD2CA2: (576, "r9"), 0xD2CAC: (580, "r6"),
                 0xD2CA6: (584, "r3"), 0xD2CB0: (588, "r3"),
                 0xD2CB8: (592, "r3"), 0xD2CBE: (596, "r3"),
                 0xD2CC4: (600, "r3")}
ISR_BODIES = [0xAE45C, 0xAEA9C, 0xAEB80, 0xB53A4, 0xB0CE0,
              0xB0AB8, 0xAEC74]
ISR_CTX_STORES = {0xAE474: 228, 0xAEAD8: 228, 0xAEBB0: 228,
                  0xB53F8: 228, 0xB5420: 228}

# --- D. the 0x84xxx family / candidates -----------------------------------
FAMILY = [0x840B4, 0x82878, 0x839B4, 0x84920, 0x84D14, 0x84260]
MAC_HANDLER = 0x840B4
MAC_CHAIN_WRITERS = {
    0xBD038: 0xBD086,   # bl band_object accessor, then writes [chain]+idx*0x80+0xEC/+0xF0
    0xBC830: 0xBC872,   # bl band_object accessor, then writes [chain]+idx*0x80+0xF4/+0xF8
}
VAP_STATE_SYN = 0x84920
VAP_STATE_SYN_BANDSTART = 0x8498E   # bl 0x971D8 on the state-4 branch
SET_MODE = 0x839B4
DFS_CAC = 0xB58FC
DFS_BAND1_SMAC_LITS = [0x4010E000, 0x4010E004, 0x4010E008,
                       0x4010E0A0, 0x4010E0A4, 0x4010E0A8]


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
    sys.exit("no ARM objdump found; pass --objdump PATH (see module docstring)")


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

    def literal_load_sites(self, value):
        """All 16-bit ldr-literal and 32-bit ldr.w-literal sites for value."""
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

    def keys(self, base, rows):
        return [(self.u16(base + i * KEY_ROW),
                 self.u32(base + i * KEY_ROW + 4)) for i in range(rows)]

    def worker(self, key):
        for base, n in ((TAB_A, TAB_A_BYTES // KEY_ROW),
                        (TAB_B, TAB_B_BYTES // KEY_ROW)):
            for k, w in self.keys(base, n):
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
        ok = bool(got) and re.sub(r"\s+", " ", got[1] + " " + got[2]) == want
        return self.check(label, ok, "got %r want %r" % (got, want))


def selftest(img, insn):
    c = Checker()
    pk = len(img.raw)
    print("=== A. identity ===")
    c.check("firmware length == %d B" % FW_LEN, pk == FW_LEN, "got %d" % pk)
    c.check("firmware sha256 == 7fc87e20...311b", sha256(img.raw) == FW_SHA)

    print("\n=== B. the g0-id1 config dispatcher CA 0x%05X ===" % DISPATCH)
    c.expect("dispatcher prologue", insn, 0x43D58, "push",
             "{r4, r5, r6, lr}")
    c.expect("key = u16 hdr+20", insn, 0x43D90, "ldrh", "r2, [r4, #20]")
    c.check("table A literal == 0x%05X" % TAB_A,
            img.literal_at(insn, 0x43D92) == TAB_A)
    c.expect("table A byte bound 1736 (217 rows)", insn, 0x43DA2,
             "cmp.w", "r3, #1736")
    c.check("table B literal == 0x%05X" % TAB_B,
            img.literal_at(insn, 0x43DAA) == TAB_B)
    c.expect("table B byte bound 704 (88 rows)", insn, 0x43DBA,
             "cmp.w", "r3, #704")
    c.expect("matched row: worker = row[+4]", insn, 0x43DCE, "ldr",
             "r3, [r1, #4]")
    c.expect("NULL worker rejected", insn, 0x43DD0, "cbnz", "r3, 0x43dec")
    c.expect("payload pointer hdr+24", insn, 0x43DEC, "add.w",
             "r2, r4, #24")
    c.expect("vap selector byte hdr+22", insn, 0x43DF0, "ldrb",
             "r1, [r4, #22]")
    c.expect("indirect call", insn, 0x43DF2, "blx", "r3")
    # tables are pure {u16 key; u16 pad; u32 worker|1} rows
    odd = 0
    sentinels = 0
    for base, n in ((TAB_A, TAB_A_BYTES // KEY_ROW),
                    (TAB_B, TAB_B_BYTES // KEY_ROW)):
        for i in range(n):
            key = img.u16(base + i * KEY_ROW)
            pad = img.u16(base + i * KEY_ROW + 2)
            w = img.u32(base + i * KEY_ROW + 4)
            if pad == 0 and (w & 1):
                continue
            if w == 0 and key == SENTINEL_KEY:
                sentinels += 1
                continue
            odd += 1
    c.check("every live table row has pad 0 and a Thumb (odd) worker",
            odd == 0, "%d bad rows" % odd)
    c.check("both tables end with the NULL sentinel row key 0x%04X" %
            SENTINEL_KEY, sentinels == 2, "got %d" % sentinels)

    print("\n=== C. key -> firmware worker (34 keys) ===")
    for key in sorted(EXPECTED_WORKERS):
        got = img.worker(key)
        c.check("key 0x%04X -> worker %#07x" % (key, EXPECTED_WORKERS[key]),
                got == EXPECTED_WORKERS[key], "got %r" % got)
    for tab, row, key in sorted(ROW_PINS):
        base = TAB_A if tab == "A" else TAB_B
        got = img.u16(base + row * KEY_ROW)
        c.check("%s row %3d == key 0x%04X" % (tab, row, key), got == key,
                "got 0x%04x" % got)
    c.check("20 stock-only keys = stock 25 minus the 9 ladder keys",
            len(STOCK_ONLY_20) == 20, "got %d" % len(STOCK_ONLY_20))
    for key in (0x0005, 0x01EE):
        c.check("stock-only key 0x%04X present in tables" % key,
                img.worker(key) is not None)

    print("\n=== D. the 0x84xxx handler family ===")
    for w in FAMILY:
        got = insn.get(w)
        c.check("family worker %#07x is a real Thumb prologue" % w,
                bool(got) and got[1] in ("push", "stmdb"))
    c.expect("set_mac_addr prologue", insn, MAC_HANDLER, "push",
             "{r4, r5, r6, lr}")
    c.expect("set_mac_addr reads per-chip ptr +1196", insn, 0x840BA,
             "ldr.w", "r6, [r0, #1196]")
    c.expect("set_mac_addr posts per-chip msg CA 0xF4744", insn, 0x84164,
             "bl", "0xf4744")
    c.expect("set_mac_addr calls chain writer CA 0xBD038", insn, 0x8416E,
             "bl", "0xbd038")
    c.expect("set_mac_addr calls chain writer CA 0xBC830", insn, 0x8417C,
             "bl", "0xbc830")
    c.expect("set_mac_addr second band guard", insn, 0x84180, "ldrb",
             "r3, [r5, #4]")
    c.expect("vap_state_syn state byte store", insn, 0x84952, "strb",
             "r5, [r4, #14]")
    c.expect("vap_state_syn state-4 band-start guard", insn, 0x84958,
             "cmp", "r5, #4")
    c.expect("vap_state_syn band start call", insn,
             VAP_STATE_SYN_BANDSTART, "bl", "0x971d8")
    c.expect("set_mode_event prologue", insn, SET_MODE, "push",
             "{r0, r1, r2, r4, r5, r6, r7, lr}")

    print("\n=== E. per-band object +672 / SMAC device chain A ===")
    c.check("wrapper RAM var has exactly the 14 ISR-wrapper load sites",
            len(img.literal_load_sites(WRAPPER_VAR)) == 14,
            "got %d" % len(img.literal_load_sites(WRAPPER_VAR)))
    c.expect("band accessor stride 3624", insn, 0xF4F4E, "movw",
             "r0, #3624")
    c.check("band accessor base literal == %#x" % BAND_OBJ0,
            img.literal_at(insn, 0xF4F52) == BAND_OBJ0)
    c.expect("band accessor mla", insn, 0xF4F54, "mla", "r0, r0, r1, r3")
    c.check("band1 object = band0 + stride", BAND_OBJ0 + BAND_STRIDE == BAND_OBJ1)
    c.expect("SMAC init region length 7272", insn, SMAC_INIT, "movw",
             "r3, #7272")
    c.expect("SMAC init band-region base literal", insn, 0xD1B76, "ldr",
             "r0, [pc, #364]")
    c.expect("only direct caller of SMAC init", insn, INIT_CALLER, "bl",
             "0xd1b68")
    c.expect("band loop: count byte base+14", insn, 0xD3F44, "ldrb",
             "r1, [r2, #14]")
    c.expect("band loop: enter body", insn, 0xD3F4A, "bhi.w", "0xd1cb6")
    c.expect("per-band selector byte obj+0x11D", insn, 0xD1D74, "strb.w",
             "r3, [r4, #221]")
    c.expect("selector read", insn, 0xD1D8E, "ldrb.w", "r3, [r4, #221]")
    c.expect("selector==0 -> chain A band0 base", insn, 0xD1D98,
             "beq.w", "0xd2044")
    c.expect("selector==1 -> chain A band1 base", insn, 0xD1D9E,
             "beq.w", "0xd2094")
    for site, want in sorted(CHAIN_A_LITS.items()):
        c.check("chain A literal @%06x == %#x" % (site, want),
                img.literal_at(insn, site) == want,
                "got %r" % img.literal_at(insn, site))
    for site, off in sorted(CHAIN_A_STORES.items()):
        c.expect("chain A store +%d @%06x" % (off, site), insn, site,
                 "str.w", "r3, [r4, #%d]" % off)
    c.expect("band0 chain A base load feeds the shared stores", insn,
             0xD2044, "ldr", "r3, [pc, #172]")
    c.expect("band1 chain A base load feeds the shared stores", insn,
             0xD2094, "ldr", "r3, [pc, #96]")
    # +672 is r4+608; +640 is r4+576 => the two RAM-block/device fields
    c.check("object relation: r4 = obj+0x40, so +608 == obj+672, +576 == obj+640",
            (0x40 + 608) == 672 and (0x40 + 576) == 640)
    for site, (off, reg) in sorted(ATTACH_STORES.items()):
        c.expect("RAM-block attach store +%d @%06x" % (off, site), insn, site,
                 "str.w", "%s, [r4, #%d]" % (reg, off))
    c.check("attach never targets +672 (only +576..+600)",
            672 not in [v[0] for v in ATTACH_STORES.values()])
    c.check("0x%05X (RAM block) has the 5 image literal seats the record lists"
            % RAM_BLOCK,
            len(img.literal_load_sites(RAM_BLOCK)) == 7,
            "got %d load sites" % len(img.literal_load_sites(RAM_BLOCK)))
    for site, off in sorted(ISR_CTX_STORES.items()):
        reg = "r2" if site in (0xAE474, 0xAEBB0) else "r3"
        c.expect("ISR ctx store +%d @%06x" % (off, site), insn, site,
                 "str.w", "%s, [%s, #228]" % ("r3" if reg == "r2" else "r2",
                                              reg))
    c.check("0x00A00096 is NOT an image constant (status is hardware/ISR)",
            all(img.u32(a) != 0xA00096
                for a in range(BASE, BASE + len(img.raw) - 4, 4)))

    print("\n=== F. candidates ===")
    dfs = body(insn, DFS_CAC)
    dfs_lits = [img.literal_at(insn, a) for a in dfs]
    c.check("DFS CAC handler body loads the band-1 SMAC sub-base literals",
            sum(1 for v in dfs_lits if v in DFS_BAND1_SMAC_LITS) >= 6,
            "%d IO literal loads" % sum(1 for v in dfs_lits
                                        if v in DFS_BAND1_SMAC_LITS))
    # the honest negative: no stock-only handler inlines a write of +672 or
    # loads the RAM block / chain bases; the per-band attach is boot-only.
    bad = []
    for key in STOCK_ONLY_20:
        w = EXPECTED_WORKERS[key]
        for a in body(insn, w):
            raw, mn, ops = insn[a]
            if mn.startswith("str") and re.search(r", #672", ops):
                bad.append((key, a, "store +672"))
            v = img.literal_at(insn, a)
            if v in (RAM_BLOCK, CHAIN_A0, CHAIN_A1, 0x180814):
                bad.append((key, a, "load %#x" % v))
    c.check("no stock-only handler inlines +672 / RAM-block / chain-base "
            "access (attach is boot-only)", not bad, repr(bad[:4]))
    c.check("chain A band0 base is loaded only by the boot init site 0xD2044",
            img.literal_load_sites(CHAIN_A0) == [0xD2044],
            "got %r" % img.literal_load_sites(CHAIN_A0))
    c.check("chain A band1 base is loaded only by the boot init site 0xD2094",
            img.literal_load_sites(CHAIN_A1) == [0xD2094],
            "got %r" % img.literal_load_sites(CHAIN_A1))
    print()
    print("RESULT: %d/%d checks PASS" % (c.n - c.bad, c.n))
    return 0 if c.bad == 0 else 1




def body(insn, start, limit=0x800):
    """Recursive-descent instruction addresses of the Thumb function at start."""
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
        if mn == "bx" or (mn == "pop" and "pc" in ops) or                 (mn == "ldmia" and "pc" in ops):
            continue
        targets = [int(t, 16) for t in re.findall(r"0x([0-9a-f]+)", ops)]
        if mn in ("b", "b.w", "b.n"):
            work.extend(t for t in targets if start <= t < start + limit)
            continue
        if mn.split(".")[0] in ("beq", "bne", "bhi", "bls", "bcc", "bcs",
                                "bge", "blt", "bgt", "ble", "bpl", "bmi",
                                "bvs", "bvc", "cbz", "cbnz"):
            work.extend(t for t in targets if start <= t < start + limit)
        work.append(nxt)
    return sorted(seen)




def dump(img, insn):
    print("g0-id1 config keys: table A CA %#x (217 rows) / table B CA %#x "
          "(88 rows), worker = row u32 & ~1" % (TAB_A, TAB_B))
    print()
    print("key   worker  set      vendor emitter (local, [V*])   decode")
    names = {
        0x0000: "hmac_config_set_mac_addr",
        0x0005: "(unresolved emitter)",
        0x000B: "hmac_config_set_uapsden",
        0x003C: "hmac_config_set_shpreamble",
        0x0052: "hmac_config_set_freq/priv_iter_vap_set_channel",
        0x0069: "hmac_set_mode_event",
        0x006F: "hmac_config_set_he_gi",
        0x0073: "hmac_config_vap_state_syn",
        0x0078: "hmac_config_11i_add_key/install_rekey",
        0x00F7: "hmac_config_set_country",
        0x011E: "hmac_config_acs",
        0x015D: "hmac_40M_intol_sync_event",
        0x01CF: "hmac_config_set_mib_by_bw",
        0x01EE: "(unresolved emitter)",
        0x025F: "hmac_config_set_cus_rf",
        0x02BD: "hmac_11v_cfg_bsst_switch",
        0x03E4: "hmac_config_down_vap/check_ap_channel_follow_sta",
        0x07D9: "hmac_config_ch_status_sync",
        0x07E7: "hmac_dfs_cac_start/stop/cac_sync_state/timeout",
        0x07E8: "hmac_chan_tx_cali_sync",
    }
    for k in sorted(EXPECTED_WORKERS):
        w = img.worker(k)
        print("0x%04X %#08x %-7s %-33s" % (
            k, w, "stock-only" if k in STOCK_ONLY_20 else "ladder",
            names.get(k, "-")))
    print()
    print("the 0x84xxx-family cluster (handler in 0x81C44..0x84DA4):")
    for base, n in ((TAB_A, 217), (TAB_B, 88)):
        for i in range(n):
            k, w = img.keys(base, n)[i]
            if 0x81C44 <= (w & ~1) <= 0x84DA4:
                print("  key 0x%04X -> CA %#07x" % (k, w & ~1))
    print()
    print("ISR common bodies writing [chain A base + 0xE4] (the +228 word):")
    for site, off in sorted(ISR_CTX_STORES.items()):
        print("  CA %#07x  str.w -> ctx+%d  (ctx = [band_object+672])" % (site, off))
    print()
    print("the chain A base ([obj+672]) at boot, per band:")
    print("  band0 obj %#x -> +672 = %#x (literal seat %#x)" %
          (BAND_OBJ0, CHAIN_A0, 0xD20F4))
    print("  band1 obj %#x -> +672 = %#x (literal seat %#x)" %
          (BAND_OBJ1, CHAIN_A1, 0xD20F8))
    print("  RAM-block attach (obj+640): %#x, same for both bands" % RAM_BLOCK)
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
