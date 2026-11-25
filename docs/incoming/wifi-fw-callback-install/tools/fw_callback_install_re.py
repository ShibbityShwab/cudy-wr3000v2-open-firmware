#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_callback_install_re.py - WHO INSTALLS the global callback G+0x370?

Lane: wifi-fw-callback-install (shared task task-186).  HOST-ONLY, read-only:
the pinned LOCAL vendor firmware image is read in place; only derived
addresses, instruction windows and small constants are printed.  No vendor
byte is copied into the repository and no device is touched.

Question: who INSTALLS G+0x370 (CA 0x174B34, the band-1 MAC-start callback)
in the hi5622v100 Wi-Fi firmware, given the bench read of 2026-11-27 shows
G+0x370 = NULL while G+0x374 (CA 0x174B38, the beacon-start callback) =
0x00057639 and the G head (CA 0x1747C4) = 0x00005d12 are non-NULL?

Method (search first):
  * every write to CA 0x174B34 / 0x174B38 in the image: movw/movt immediates
    (0x4b34/0x4b38 + movt 0x17), raw little-endian pointer literals, and the
    resolved G-base (=G literal pool) reference sites followed by a taint scan
    for stores at [G+offset];
  * the desc-pool boot block CA 0x485FA..0x4861E (bl 0x1017F8) for G stores;
  * the band-0 counterpart slots G+0x36C / G+0x368 (differential);
  * every hook-offset store with ANY base, classified by base taint.

Conventions: CPU = file offset + 0x40000; BAR0 = CPU + 0x6B8000;
bar0read phys = 0x40000000 + BAR0 (= CPU + 0x46B8000).  G is a runtime RAM
global at CA 0x1747C4, so it is outside the 928,920-byte image: only code
that loads the CA 0x1747C4 pointer literal can reach it.

Modes: --selftest, --search, --dump LO HI, --fn ADDR, --xref CA.
"""
import argparse
import hashlib
import os
import re
import struct
import subprocess
import sys

BASE = 0x40000
DEFAULT_FW = "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin"
FW_SHA = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_LEN = 928920
DEFAULT_OD = ("/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/"
              "toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/"
              "arm-openwrt-linux-objdump")
SCRATCH = os.path.expanduser("~/Documents/GitHub/cudy-wr3000v2/build/"
                             "wifi-fw-callback-install")

G = 0x1747C4                    # global table base (runtime RAM)
CB_MAC = G + 0x370              # CA 0x174B34, band-1 MAC-start callback
CB_BEACON = G + 0x374           # CA 0x174B38, beacon-start callback
HOOKS = [0x364, 0x368, 0x36C, 0x370, 0x374, 0x378]
HOOK_NAMES = {
    0x364: "? (installed at boot)",
    0x368: "? (installed at boot)",
    0x36C: "band-0 MAC-start counterpart (installed at boot)",
    0x370: "band-1 MAC-start callback (NULL on host-only boot)",
    0x374: "beacon-start callback (installed at boot)",
    0x378: "? (installed)",
}
VAP_ON_BAND = 0x971D8
VAP_CB_CALL = {0x97360: "G+0x370 load", 0x97366: "blx G+0x370",
               0x9736A: "G+0x374 load", 0x97372: "blx G+0x374"}
POOL_BOOT_BLOCK = 0x485FA       # desc-pool boot init block (lane task-184)
POOL_POPULATOR = 0x1017F8

# the four callbacks installed by the two hook installers found by the scan
KNOWN_INSTALLED = {
    (0x364, 0x565C5), (0x368, 0x52BE1), (0x36C, 0x727D1),
    (0x374, 0x57639), (0x378, 0x68C0B),
}

REG_ALIAS = {"fp": "r11", "sl": "r10", "ip": "r12", "sb": "r9"}
CALLER_SAVED = {"r0", "r1", "r2", "r3", "r12", "lr"}
TERM_RE = re.compile(r"^(pop|ldmia|ldm|bx)(\.\w+)?\b")
PROLOGUE_RE = re.compile(r"^(push|push\.w|stmdb|stmdb\.w)\b")
STORE_RE = re.compile(r"^(str|strb|strh|strd|stm|stmia|stmdb)(\.\w+)?\s+(.+)$")
LDR_RE = re.compile(r"^ldr(\.\w+)?\s+(\w+),")
LIT_RE = re.compile(r"\[pc, #\d+\]\s+@\s+\(?(0x[0-9a-f]+)\)?")
CALL_RE = re.compile(r"^(bl|blx|b|b\.w|b\.n|[a-z]{2}(?:\.\w+)?)\s+0x([0-9a-f]+)")
MOV_RE = re.compile(r"^(?:mov|movs|mov\.w)\s+(\w+),\s*(\w+)$")
MOVIMM_RE = re.compile(r"^(?:mov|movs|mov\.w)\s+(\w+),\s*#(-?\d+)$")
LDRD_RE = re.compile(r"^ldrd\s+(\w+),\s*(\w+),")
NONWRITE = {"cmp", "cmn", "tst", "teq", "cbz", "cbnz", "nop", "push", "it",
            "itt", "ite", "ittt", "itee", "str", "strb", "strh", "strd",
            "stm", "stmia", "stmdb", "vstr", "bx", "dmb", "dsb", "isb",
            "wfi", "svc", "bkpt", "udf", "sev", "clrex", "pl"}
MOVI_RE = re.compile(r"^(movw|movt)\s+(\w+), #(\d+)")
ARITH_RE = re.compile(r"^(add|adds|add\.w|adds\.w|sub|subs|sub\.w|subs\.w)\s+(\w+),\s*(.+)$")
LDRMEM_RE = re.compile(r"^ldr(?:\.\w+)?\s+(\w+),\s*\[(\w+)(?:,\s*#(-?\d+))?\]")


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

    def u32(self, ca):
        off = ca - BASE
        if off < 0 or off + 4 > self.len:
            return None
        return struct.unpack("<I", self.data[off:off + 4])[0]

    def u16(self, ca):
        off = ca - BASE
        if off < 0 or off + 2 > self.len:
            return None
        return struct.unpack("<H", self.data[off:off + 2])[0]

    def raw(self, ca, n):
        off = ca - BASE
        if off < 0 or off + n > self.len:
            return None
        return self.data[off:off + n]


class Ins:
    __slots__ = ("addr", "raw", "text")

    def __init__(self, addr, raw, text):
        self.addr = addr
        self.raw = raw
        self.text = text

    def __repr__(self):
        return "%06x: %s" % (self.addr, self.text)


def sh(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)


def disassemble(fw, od):
    os.makedirs(SCRATCH, exist_ok=True)
    cache = os.path.join(SCRATCH, "fw.dis")
    stamp = os.path.join(SCRATCH, "fw.dis.sha")
    ok = False
    if os.path.exists(cache) and os.path.exists(stamp):
        ok = open(stamp).read().strip() == fw.sha
    if not ok:
        r = sh([od, "-D", "-b", "binary", "-m", "arm", "-M", "force-thumb",
                "--adjust-vma=0x%x" % BASE, fw.path])
        if r.returncode != 0:
            sys.stderr.write(r.stderr)
            sys.exit("objdump failed")
        open(cache, "w").write(r.stdout)
        open(stamp, "w").write(fw.sha + "\n")
    out = []
    rx = re.compile(r"^\s*([0-9a-f]+):\t([0-9a-f ]+)\t(.*)$")
    for line in open(cache):
        m = rx.match(line.rstrip("\n"))
        if m:
            out.append(Ins(int(m.group(1), 16), m.group(2).strip(),
                           m.group(3).strip()))
    return out


class Image:
    def __init__(self, fw, od):
        self.fw = fw
        self.ins = disassemble(fw, od)
        self.addr = [i.addr for i in self.ins]
        self.index = {i.addr: n for n, i in enumerate(self.ins)}
        self.calls = set()
        for i in self.ins:
            t = self.branch_target(i.text)
            if t is not None and not i.text.startswith("cb"):
                self.calls.add(t)

    @staticmethod
    def strip_comment(t):
        return t.split("\t@")[0].strip()

    @classmethod
    def mnemonic_clean(cls, text):
        return cls.strip_comment(text)

    @classmethod
    def branch_target(cls, text):
        m = re.match(r"^(bl|blx|b|b\.w|b\.n|[a-z]{2}(?:\.\w+)?)\s+0x([0-9a-f]+)$",
                     cls.strip_comment(text))
        if m:
            return int(m.group(2), 16)
        return None

    def at(self, addr):
        n = self.index.get(addr)
        return self.ins[n] if n is not None else None

    def lit_at(self, ins):
        m = LIT_RE.search(ins.text)
        if not m:
            return None
        return int(m.group(1), 16)

    def window(self, lo, hi):
        return [i for i in self.ins if lo <= i.addr <= hi]

    def prologues(self):
        out = []
        for i in self.ins:
            t = self.strip_comment(i.text)
            if PROLOGUE_RE.match(t) and "lr" in t and "sp" in t:
                out.append(i.addr)
        return out

    def function_start(self, addr):
        """[I] heuristic: nearest preceding push/stmdb-with-lr prologue."""
        pro = getattr(self, "_pro", None)
        if pro is None:
            pro = self._pro = self.prologues()
        best = None
        for c in reversed(pro):
            if c <= addr:
                best = c
                break
        return best

    def code_ptr_occurrences(self, addr):
        """Literal/data occurrences of a Thumb function pointer: addr or
        addr|1 (Thumb bit).  Returns [(ca, value)]."""
        out = []
        for v in (addr & ~1, (addr & ~1) | 1):
            for ca in self.ptr_occurrences(v):
                out.append((ca, v))
        return out

    def callers(self, addr):
        out = []
        for i in self.ins:
            t = self.branch_target(i.text)
            if t == addr:
                out.append(i.addr)
        return out

    def ptr_occurrences(self, value):
        pat = struct.pack("<I", value)
        data = self.fw.data
        out = []
        start = 0
        while True:
            n = data.find(pat, start)
            if n < 0:
                break
            out.append(n + BASE)
            start = n + 1
        return out

    def find_g_load_sites(self):
        """(addr of ldr, destination reg, literal CA) for every ldr that
        loads the G pointer literal (CA 0x1747C4)."""
        out = []
        for i in self.ins:
            t = self.strip_comment(i.text)
            if not t.startswith("ldr"):
                continue
            lit = self.lit_at(i)
            if lit is None:
                continue
            if self.fw.u32(lit) == G:
                m = LDR_RE.match(t)
                out.append((i.addr, m.group(2) if m else "?", lit))
        return out


def norm_reg(r):
    return REG_ALIAS.get(r, r)


def parse_store(text):
    """Return (kind, base, off, regs) or None.  The '[...]' operand contains a
    comma, so locate the bracket first and take the registers before it."""
    t = Image.strip_comment(text)
    m = STORE_RE.match(t)
    if not m:
        return None
    kind = m.group(1)
    rest = m.group(3)
    if kind in ("stm", "stmia", "stmdb"):
        bm = re.match(r"^(\w+)!?,\s*\{(.*)\}$", rest)
        if not bm:
            return None
        base = norm_reg(bm.group(1))
        regs = [norm_reg(r) for r in
                re.findall(r"\b(?:r\d+|lr|sp|pc)\b", bm.group(2))]
        return ("stm", base, 0, regs)
    bm = re.search(r"\[(\w+),\s*(\w+)(?:,\s*lsl #(\d+))?\]", rest)
    if bm:
        base = norm_reg(bm.group(1))
        regs = [norm_reg(r) for r in
                re.findall(r"\b(?:r\d+|lr|sp|pc)\b", rest[:bm.start()])]
        return ("IDX", base, norm_reg(bm.group(2)),
                int(bm.group(3)) if bm.group(3) else 0, regs)
    bm = re.search(r"\[(\w+)(?:,\s*#(-?\d+))?\]", rest)
    if not bm:
        return None
    base = norm_reg(bm.group(1))
    off = int(bm.group(2)) if bm.group(2) else 0
    regs = [norm_reg(r) for r in
            re.findall(r"\b(?:r\d+|lr|sp|pc)\b", rest[:bm.start()])]
    return (kind, base, off, regs)


def parse_arith(text):
    t = Image.strip_comment(text)
    m = ARITH_RE.match(t)
    if not m:
        return None
    dst = norm_reg(m.group(2))
    rest = [o.strip() for o in m.group(3).split(",")]
    return (m.group(1), dst, rest)


class Taint:
    """Forward register value/offset tracking with a small call model."""

    def __init__(self, img, fw):
        self.img = img
        self.fw = fw
        self.events = []          # (addr, kind, target, value)
        self.reads = []           # (addr, target, reg)
        self.indexed = []         # (addr, base, idx, shift, value) G+index
        self.value_stores = []    # (addr, base, off, value) G stored as data
        self.call_sites = []      # (addr, target, {argreg: value})
        self._done = set()

    def _reg_copy(self, regs, consts, dst, src, val):
        if val is not None:
            regs[dst] = val
        else:
            regs.pop(dst, None)
            consts.pop(dst, None)

    def _feed(self, init_regs):
        regs = dict(init_regs)
        consts = {}
        return regs, consts

    def run(self, start_idx, init_regs, depth=0):
        key = (start_idx, tuple(sorted(init_regs.items())))
        if key in self._done or depth > 3:
            return
        self._done.add(key)
        img, fw = self.img, self.fw
        regs, consts = self._feed(init_regs)
        ins = img.ins
        n = start_idx
        while n < len(ins):
            i = ins[n]
            t = img.strip_comment(i.text)
            n += 1
            if n - start_idx > 600:
                break
            # ---- prologue terminators
            if TERM_RE.match(t):
                if "pc" in t or t.startswith("bx"):
                    break
            # ---- mov immediate / register
            m = MOVIMM_RE.match(t)
            if m:
                dst = norm_reg(m.group(1))
                regs[dst] = ("C", int(m.group(2)) & 0xFFFFFFFF)
                continue
            m = MOV_RE.match(t)
            if m:
                dst, src = norm_reg(m.group(1)), norm_reg(m.group(2))
                if src in regs:
                    regs[dst] = regs[src]
                else:
                    regs.pop(dst, None)
                continue
            m = LDRD_RE.match(t)
            if m:
                regs.pop(norm_reg(m.group(1)), None)
                regs.pop(norm_reg(m.group(2)), None)
                continue
            # ---- movw / movt
            m = MOVI_RE.match(t)
            if m:
                dst = norm_reg(m.group(2))
                imm = int(m.group(3))
                if m.group(1) == "movw":
                    consts[dst] = (imm & 0xFFFF, consts.get(dst, (0, 0))[1])
                else:
                    consts[dst] = (consts.get(dst, (0, 0))[0], imm & 0xFFFF)
                lo, hi = consts[dst]
                regs[dst] = ("C", ((hi << 16) | lo) if hi else None)
                if regs[dst][1] is None:
                    del regs[dst]
                continue
            # ---- ldr with literal / memory
            if t.startswith("ldr"):
                lit = img.lit_at(i)
                if lit is not None:
                    m2 = LDR_RE.match(t)
                    if m2:
                        dst = norm_reg(m2.group(2))
                        v = fw.u32(lit)
                        if v is None:
                            regs.pop(dst, None)
                        else:
                            regs[dst] = ("C", v)
                    continue
                m2 = LDRMEM_RE.match(t)
                if m2:
                    dst = norm_reg(m2.group(1))
                    bas = norm_reg(m2.group(2))
                    io = int(m2.group(3)) if m2.group(3) else 0
                    if bas in regs and regs[bas][0] == "G":
                        self.reads.append((i.addr, ("G", regs[bas][1] + io),
                                           dst))
                    regs.pop(dst, None)
                continue
            # ---- arithmetic
            a = parse_arith(t)
            if a:
                op, dst, rest = a
                imm = None
                src = None
                for o in rest:
                    im = re.match(r"^#(-?\d+)$", o)
                    if im:
                        imm = int(im.group(1))
                    rm = re.match(r"^(\w+)$", o)
                    if rm and rm.group(1) not in ("lsl", "lsr", "asr"):
                        src = norm_reg(rm.group(1))
                sign = -1 if op.startswith("sub") else 1
                val = None
                if src is not None and src in regs:
                    base = regs[src]
                    if base[0] == "G" or base[0] == "C":
                        val = (base[0], base[1] + sign * (imm or 0))
                elif imm is not None and dst in regs:
                    base = regs[dst]
                    if base[0] == "G" or base[0] == "C":
                        val = (base[0], base[1] + sign * imm)
                elif imm is not None and src is None:
                    val = ("C", sign * imm)
                if val is not None:
                    if val[0] == "C" and val[1] < 0:
                        regs.pop(dst, None)
                    else:
                        regs[dst] = val
                else:
                    regs.pop(dst, None)
                continue
            # ---- store
            st = parse_store(t)
            if st:
                if st[0] == "IDX":
                    _, base, idxr, shift, sregs = st
                    if base in regs and regs[base][0] == "G":
                        val = None
                        if sregs and sregs[0] in regs:
                            val = regs[sregs[0]]
                        self.indexed.append((i.addr, regs[base][1], idxr,
                                             shift, val))
                    continue
                kind, base, off, sregs = st
                if base in regs:
                    b = regs[base]
                    if b[0] == "G":
                        tgt = b[1] + off
                        if kind == "stm":
                            for k, _ in enumerate(sregs):
                                self.events.append((i.addr, kind,
                                                    ("G", tgt + 4 * k), None))
                        elif kind == "strd":
                            for k in range(2):
                                self.events.append((i.addr, kind,
                                                    ("G", tgt + 4 * k), None))
                        else:
                            val = None
                            if sregs and sregs[0] in regs:
                                val = regs[sregs[0]]
                            self.events.append((i.addr, kind, ("G", tgt), val))
                elif sregs and sregs[0] in regs and regs[sregs[0]][0] == "G":
                    # a G-derived pointer is stored into some other object:
                    # possible alias that a later writer could use as a base
                    self.value_stores.append((i.addr, base, off,
                                              regs[sregs[0]]))
                continue
            # ---- call
            if t.startswith("bl"):
                mt = CALL_RE.match(t)
                if mt:
                    target = int(mt.group(2), 16)
                    argv = {}
                    for r in ("r0", "r1", "r2", "r3"):
                        if r in regs:
                            argv[r] = regs[r]
                    self.call_sites.append((i.addr, target, argv))
                    for r in list(regs):
                        if r in CALLER_SAVED:
                            del regs[r]
                    continue
            if t.startswith("b") and not t.startswith("bf"):
                mt = CALL_RE.match(t)
                if mt:
                    target = int(mt.group(2), 16)
                    argv = {}
                    for r in ("r0", "r1", "r2", "r3"):
                        if r in regs:
                            argv[r] = regs[r]
                    self.call_sites.append((i.addr, target, argv))
                    break
            # ---- unknown instruction: invalidate its destination register so
            # stale taint cannot leak (conservative; keeps the absence search
            # honest at the cost of missing exotic propagation)
            mt = re.match(r"^([a-z][a-z0-9]*(?:\.\w+)?)\s+(.*)$", t)
            if mt:
                mnem = mt.group(1)
                if mnem in NONWRITE or mnem[0] == "b":
                    continue
                rest = mt.group(2)
                dm = re.match(r"^(r\d+|lr|sp|pc)\b", rest)
                if dm:
                    regs.pop(norm_reg(dm.group(1)), None)
        # recurse into callees that received G-tainted args
        if depth < 2:
            for site, target, argv in self.call_sites:
                gargs = {r: v for r, v in argv.items() if v[0] == "G"}
                if not gargs:
                    continue
                fs = img.function_start(target)
                if fs is None:
                    continue
                idx = img.index.get(fs)
                if idx is None:
                    continue
                self.run(idx, gargs, depth + 1)


def analyze(img, fw, seed_limit=None):
    sites = img.find_g_load_sites()
    t = Taint(img, fw)
    for addr, reg, lit in sites:
        idx = img.index.get(addr)
        if idx is None:
            continue
        # start AFTER the ldr so the seed taint is not overwritten by the
        # constant load of the G pointer itself
        t.run(idx + 1, {norm_reg(reg): ("G", 0)})
    if seed_limit:
        sites = sites[:seed_limit]
    return sites, t


# ---------------------------------------------------------------------------

def print_ins(img, fw, i):
    lit = img.lit_at(i)
    extra = ""
    if lit is not None:
        v = fw.u32(lit)
        extra = "   ; literal @%06x = %s" % (lit, "0x%08x" % v if v is not None else "?")
    print("  %06x: %-12s %s%s" % (i.addr, i.raw, i.text, extra))


def cmd_selftest(img, fw):
    ok = 0
    fail = 0

    def check(name, cond, detail=""):
        nonlocal ok, fail
        if cond:
            ok += 1
            print("PASS %-58s %s" % (name, detail))
        else:
            fail += 1
            print("FAIL %-58s %s" % (name, detail))

    check("firmware sha256", fw.sha == FW_SHA, fw.sha[:16])
    check("firmware length", fw.len == FW_LEN, str(fw.len))
    check("G is outside the image (runtime RAM)", (G - BASE) > fw.len,
          "CA 0x%x file 0x%x > 0x%x" % (G, G - BASE, fw.len))
    check("no raw LE32 literal of CA 0x174B34",
          len(img.ptr_occurrences(CB_MAC)) == 0)
    check("no raw LE32 literal of CA 0x174B38",
          len(img.ptr_occurrences(CB_BEACON)) == 0)
    gocc = img.ptr_occurrences(G)
    check("G pointer literal occurrences", len(gocc) > 0, "%d" % len(gocc))
    sites, t = analyze(img, fw)
    check("G load sites resolved", len(sites) > 0, "%d" % len(sites))
    gst = {}
    for addr, kind, tgt, val in t.events:
        if tgt[0] == "G":
            gst.setdefault(tgt[1], []).append((addr, kind, val))
    check("G+0x370 store count is ZERO", 0x370 not in gst,
          "offsets %s" % ",".join("0x%x" % k for k in sorted(gst)))
    expect = {0x364: (0x5D13A, 0x565C5), 0x368: (0x5CFC2, 0x52BE1),
              0x36C: (0x5CF60, 0x727D1), 0x374: (0x5D144, 0x57639),
              0x378: (0x5C3B6, 0x68C0B)}
    for off, (want_addr, want_val) in expect.items():
        got = set()
        for addr, kind, val in gst.get(off, []):
            if val and val[0] == "C":
                got.add((addr, val[1]))
        check("G+0x%x writer = CA 0x%x <= 0x%x" % (off, want_addr, want_val),
              (want_addr, want_val) in got,
              "stores %s" % [(hex(a), hex(v)) for a, v in sorted(got)])
    # VAP start-on-band call sites
    want = {(0x97360, "ldr.w r3, [r7, #880]"),
            (0x9736A, "ldr.w r3, [r7, #884]"),
            (0x97368, "blx r3"), (0x97372, "blx r3")}
    got = set()
    for i in img.window(0x97340, 0x97380):
        ct = img.strip_comment(i.text).replace("\t", " ").replace("  ", " ")
        for a, want_txt in want:
            if i.addr == a and ct == want_txt.replace("\t", " "):
                got.add(a)
    check("VAP-on-band G+0x370 / G+0x374 call sites", len(got) == 4,
          "matched %s" % sorted(hex(x) for x in got))
    # desc-pool boot block still bls the populator
    blk = [i for i in img.window(0x485E0, 0x48630)
           if img.branch_target(i.text) == POOL_POPULATOR]
    check("desc-pool boot block bl 0x1017F8", len(blk) == 1,
          "at %s" % [hex(i.addr) for i in blk])
    # known installed callback values
    for off, val in KNOWN_INSTALLED:
        check("G+0x%x installed value 0x%x seen in a literal" % (off, val),
              fw.u32_lit_exists(val) if hasattr(fw, "u32_lit_exists") else
              bool(img.ptr_occurrences(val)), "")
    print("\n%d PASS, %d FAIL" % (ok, fail))
    return 0 if fail == 0 else 1


def resolve_value_chain(img, fw, store_addr, steps=8):
    """Best-effort: walk back from a store to the instruction that produced
    the stored register value, returning the constant if a literal/movw chain
    is found.  Used for the report only."""
    return None


def cmd_search(img, fw):
    print("== 0. artifact")
    print("firmware: %s" % fw.path)
    print("size %d  sha256 %s  CPU=file+0x%x" % (fw.len, fw.sha, BASE))
    print("G=CA 0x%x -> G+0x370=CA 0x%x, G+0x374=CA 0x%x"
          % (G, CB_MAC, CB_BEACON))
    print()

    print("== 1. direct address construction / literal tables")
    movw = []
    for i in img.ins:
        t = img.strip_comment(i.text)
        m = MOVI_RE.match(t)
        if m and int(m.group(3)) in (0x4B34, 0x4B38):
            movw.append((i.addr, t))
    print("movw/movt immediates 0x4b34/0x4b38 : %d" % len(movw))
    for a, t in movw:
        print("  %06x  %s" % (a, t))
    for val, name in ((CB_MAC, "0x174B34"), (CB_BEACON, "0x174B38")):
        occ = img.ptr_occurrences(val)
        print("raw LE32 literal %s : %d %s"
              % (name, len(occ), [hex(o) for o in occ[:8]]))
    print()

    print("== 2. G-pointer (CA 0x1747c4) occurrences in the image")
    occ = img.ptr_occurrences(G)
    print("count %d" % len(occ))
    # which are code-referenced (literal-pool) vs data-table entries
    litrefs = {}
    for i in img.ins:
        lit = img.lit_at(i)
        if lit is not None:
            litrefs.setdefault(lit, []).append(i.addr)
    code_refs = [o for o in occ if o in litrefs]
    data_only = [o for o in occ if o not in litrefs]
    print("referenced by a load  : %d" % len(code_refs))
    print("data occurrences only : %d  %s"
          % (len(data_only), [hex(o) for o in data_only[:20]]))
    print()

    print("== 3. G-base reference sites (ldr = CA 0x1747c4)")
    sites, t = analyze(img, fw)
    for addr, reg, lit in sites:
        fs = img.function_start(addr)
        print("  site %06x  reg %-3s  literal %06x  enclosing function %s"
              % (addr, norm_reg(reg), lit, "0x%06x" % fs if fs else "?"))
    print("resolved G load sites: %d" % len(sites))
    print()

    print("== 4. G stores found by the taint scan (all offsets)")
    gst = {}
    for addr, kind, tgt, val in t.events:
        if tgt[0] != "G":
            continue
        gst.setdefault(tgt[1], []).append((addr, kind, val))
    print("distinct G store offsets: %d" % len(gst))
    for off in sorted(gst):
        for addr, kind, val in gst[off]:
            fs = img.function_start(addr)
            vs = ""
            if val and val[0] == "C":
                vs = " value 0x%08x" % val[1]
            print("  G+0x%03x  %06x  %-6s fn 0x%06x%s"
                  % (off, addr, kind, fs or 0, vs))
    print()
    print("G+0x370 STORES:", len(gst.get(0x370, [])),
          "   <- the question")
    print("hook-block store counts:",
          {("0x%x" % o): len(gst.get(o, [])) for o in HOOKS})
    print()

    print("== 5. hook-offset store inventory with ANY base")
    for i in img.ins:
        st = parse_store(i.text)
        if not st or st[0] == "IDX":
            continue
        kind, base, off, regs = st
        if off not in HOOKS:
            continue
        fs = img.function_start(i.addr)
        print("  %06x  %-8s base %-4s off 0x%x  fn 0x%06x  %s"
              % (i.addr, kind, base, off, fs or 0, img.strip_comment(i.text)))
    print()

    print("== 6. desc-pool boot block (CA 0x485FA..0x4861E, bl 0x1017F8)")
    fs = img.function_start(POOL_BOOT_BLOCK)
    print("enclosing function start (heuristic): %s"
          % ("0x%06x" % fs if fs else "?"))
    instores = [(a, tg[1]) for a, k, tg, v in t.events
                if tg[0] == "G" and fs is not None and
                fs <= a <= (fs + 0x2000)]
    print("G stores in that function: %d  %s"
          % (len(instores),
             " ".join("0x%x" % off for a, off in sorted(instores, key=lambda x: x[1]))))
    for i in img.window(0x485E0, 0x48630):
        print_ins(img, fw, i)
    print()

    print("== 7. VAP start-on-band CA 0x971D8 callback dispatch")
    for i in img.window(0x97340, 0x97380):
        print_ins(img, fw, i)
    print()

    print("== 8. installed callback values -> candidate target functions")
    for off, val in sorted(KNOWN_INSTALLED):
        fs = img.function_start(val & ~1)
        callers = img.callers(val & ~1)
        occ2 = img.code_ptr_occurrences(val)
        print("  G+0x%03x <= 0x%06x (function start [I] 0x%06x) callers %s "
              "code-ptr occurrences %s"
              % (off, val, fs or 0, [hex(c) for c in callers[:6]],
                 [(hex(o), hex(v)) for o, v in occ2[:6]]))
    print()

    print("== 9. hook installers: function starts + callers")
    fns = {}
    for off, lst in gst.items():
        if off not in HOOKS:
            continue
        for addr, kind, val in lst:
            fs = img.function_start(addr)
            fns.setdefault(fs, []).append((off, addr))
    for fs, items in sorted(fns.items()):
        if fs is None:
            continue
        callers = img.callers(fs)
        pocc = img.code_ptr_occurrences(fs)
        print("  installer fn 0x%06x [I] : %s" % (fs, items))
        print("      callers: %s" % [hex(c) for c in callers])
        print("      function-pointer table entries (Thumb-bit occurrences): %s"
              % [(hex(o), hex(v)) for o, v in pocc])
    print()

    print("== 10. data-only G pointer occurrences (possible registration tables)")
    for o in data_only[:20]:
        words = []
        for k in range(-4, 5):
            v = fw.u32(o + 4 * k)
            words.append("%s" % ("0x%08x" % v if v is not None else "?"))
        print("  CA 0x%06x  neighbours: %s" % (o, " ".join(words)))
    print()

    print("== 11. G stored into hook block 0x360..0x380 (the writer answer)")
    hook_block = {off: v for off, v in gst.items() if 0x360 <= off <= 0x380}
    for off in sorted(hook_block):
        for addr, kind, val in sorted(hook_block[off]):
            vs = "0x%08x" % val[1] if val and val[0] == "C" else "-"
            fs = img.function_start(addr)
            print("  G+0x%03x  store %06x  %-5s value %s  fn 0x%06x"
                  % (off, addr, kind, vs, fs or 0))
    print("  G+0x370 present: %s" % (0x370 in hook_block))
    print()

    print("== 12. every word-store in the image with immediate offset 880 (0x370)")
    for i in img.ins:
        st = parse_store(i.text)
        if not st or st[0] == "IDX":
            continue
        kind, base, off, regs = st
        if off != 0x370 or kind not in ("str", "strd", "stm"):
            continue
        fs = img.function_start(i.addr)
        print("  %06x  fn 0x%06x  base %-4s  %s"
              % (i.addr, fs or 0, base, img.strip_comment(i.text)))
    print()

    print("== 13. direct call sites passing G (or a G-derived pointer) as an arg")
    seen = set()
    for site, target, argv in t.call_sites:
        gargs = {r: v for r, v in argv.items() if v[0] in ("G",)}
        if not gargs:
            continue
        key = (site, target, tuple(sorted((r, v) for r, v in gargs.items())))
        if key in seen:
            continue
        seen.add(key)
        fs = img.function_start(target)
        print("  %06x -> 0x%06x (fn 0x%06x) args %s"
              % (site, target, fs or 0,
                 ", ".join("%s=G+0x%x" % (r, v[1]) for r, v in
                           sorted(gargs.items()))))
    print()

    print("== 14b. G used as a stored VALUE (possible alias pointers)")
    seen = set()
    for addr, base, off, val in t.value_stores:
        key = (addr, base, off)
        if key in seen:
            continue
        seen.add(key)
        print("  %06x  store G+0x%x -> [%s, #0x%x]"
              % (addr, val[1], base, off))
    print()

    print("== 14b2. G-base INDEXED (register-offset) accesses")
    for addr, boff, idxr, shift, val in t.indexed:
        fs = img.function_start(addr)
        print("  %06x  [G+0x%x + %s<<%d] fn 0x%06x" % (addr, boff, idxr,
                                                        shift, fs or 0))
    print()

    print("== 14c. G+0x364..0x378 READ sites (hook-block reads)")
    for addr, tgt, dst in t.reads:
        if tgt[0] == "G" and 0x364 <= tgt[1] <= 0x378:
            fs = img.function_start(addr)
            nxt = []
            idx = img.index.get(addr)
            has_blx = False
            for k in range(1, 9):
                if idx is not None and idx + k < len(img.ins):
                    tt = img.ins[idx + k].text
                    nxt.append(img.ins[idx + k].addr)
                    if "blx" in tt:
                        has_blx = True
            print("  %06x  G+0x%x -> %-4s fn 0x%06x  blx-within-8:%s"
                  % (addr, tgt[1], dst, fs or 0, has_blx))
    print()

    print("== 14. boot-installer functions: starts, callers, table entries")
    for probe in (0x5CF48, 0x5C2E2, 0x5967C, 0x59E54, 0x59CA6, 0x59F3E):
        fs = img.function_start(probe)
        if fs is None:
            continue
        callers = img.callers(fs)
        pocc = img.code_ptr_occurrences(fs)
        print("  probe %06x -> fn [I] 0x%06x  callers %s  code-ptr occurrences %s"
              % (probe, fs, [hex(c) for c in callers],
                 [(hex(o), hex(v)) for o, v in pocc[:8]]))
    return 0


CALLBACKS = {
    0x364: 0x565C4, 0x368: 0x52BE0, 0x36C: 0x727D0,
    0x370: None, 0x374: 0x57638, 0x378: 0x68C0A,
}


def cmd_callbacks(img, fw):
    pro = img.prologues()

    def next_pro(addr):
        for p in pro:
            if p > addr:
                return p
        return addr + 0x400

    print("== installed callbacks in the G hook block 0x364..0x378")
    for off, entry in sorted(CALLBACKS.items()):
        if entry is None:
            print("  G+0x%03x : NONE (slot never installed; no writer in image)" % off)
            continue
        end = min(next_pro(entry), entry + 0x400)
        calls = []
        lits = []
        for i in img.window(entry, end):
            t = img.strip_comment(i.text)
            bt = img.branch_target(i.text)
            if bt is not None and t.startswith("bl"):
                calls.append("%06x->%06x" % (i.addr, bt))
            lit = img.lit_at(i)
            if lit is not None:
                lits.append("%06x:%08x" % (i.addr, fw.u32(lit) or 0))
        print("  G+0x%03x = 0x%06x (CA 0x%06x, value 0x%06x) region end 0x%06x"
              % (off, entry, entry, entry | 1, end))
        print("      calls   : %s" % calls)
        print("      literals: %s" % lits[:20])
    print()

    print("== VAP start-on-band 0x971D8: the ONLY caller of G+0x370 / G+0x374")
    for i in img.window(0x97352, 0x97378):
        print_ins(img, fw, i)
    print()

    print("== G+0x378 is called from a different path (0x0B56EA)")
    for i in img.window(0x0B56EA, 0x0B5700):
        print_ins(img, fw, i)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--fw", default=DEFAULT_FW)
    ap.add_argument("--objdump", default=DEFAULT_OD)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--search", action="store_true")
    ap.add_argument("--callbacks", action="store_true")
    ap.add_argument("--dump", nargs=2, metavar=("LO", "HI"))
    ap.add_argument("--fn", metavar="ADDR")
    ap.add_argument("--xref", metavar="CA")
    args = ap.parse_args(argv)
    for tool in (args.fw, args.objdump):
        if not os.path.exists(tool):
            sys.exit("missing artifact: %s" % tool)
    fw = Fw(args.fw)
    img = Image(fw, args.objdump)
    if args.selftest:
        return cmd_selftest(img, fw)
    if args.search:
        return cmd_search(img, fw)
    if args.callbacks:
        return cmd_callbacks(img, fw)
    if args.dump:
        lo, hi = int(args.dump[0], 0), int(args.dump[1], 0)
        for i in img.window(lo, hi):
            print_ins(img, fw, i)
        return 0
    if args.fn:
        a = int(args.fn, 0)
        fs = img.function_start(a)
        print("function start [I] 0x%06x ; callers %s"
              % (fs or 0, [hex(c) for c in img.callers(fs)] if fs else []))
        for i in img.window(fs, a + 0x40):
            print_ins(img, fw, i)
        return 0
    if args.xref:
        v = int(args.xref, 0)
        print("raw LE32 occurrences of 0x%08x: %s"
              % (v, [hex(o) for o in img.ptr_occurrences(v)]))
        return 0
    ap.print_help()
    return 0


if __name__ == "__main__":
    sys.exit(main())
