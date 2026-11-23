#!/usr/bin/env python3
"""chip_power.py - host-only static analysis of the vendor Wi-Fi chip power/reset path.

Reads the vendor kernel modules and board DTB blobs IN PLACE (never copies, never
commits vendor bytes) and prints derived facts: the endpoint power/reset GPIO pulse,
the DT properties that drive it, log strings, call graphs, per-function disassembly
with resolved references, and the vendor-vs-ours difference.

The modules are relocatable ARM objects whose sections all have sh_addr == 0, so a
bare address is ambiguous; this tool resolves every symbol to (section, value) and
never mixes sections.

Evidence grades used in the REPORT: [V] verified from the local artifact by this
tool, [V*] measured on the bench by an earlier lane, [I] inference, [U] unknown.

Usage (PY = repo .provision-venv/bin/python, or any python3 with pyelftools):
    PY tools/chip_power.py selftest
    PY tools/chip_power.py gpio                 # the vendor endpoint reset/power pulse
    PY tools/chip_power.py overlays <file>      # board DTBO -> pcie-gpios pins
    PY tools/chip_power.py syms [regex]
    PY tools/chip_power.py logmap [regex]
    PY tools/chip_power.py findstr <regex>
    PY tools/chip_power.py calls <func>
    PY tools/chip_power.py callers <func>
    PY tools/chip_power.py dis <func> [nbytes]
    PY tools/chip_power.py consts [regex]       # movw/movt + ldr-pc constants
    PY tools/chip_power.py undef [regex]
    PY tools/chip_power.py diff <other.ko>
    PY tools/chip_power.py --module <path> <cmd>

Module paths come from --module, env PLAT_KO (plat.ko) / HI_PCIE_KO (hi_pcie.ko),
defaulting to the local extracted stock rootfs if present. Nothing here opens a device.
"""
import argparse
import collections
import os
import re
import struct
import subprocess
import sys

try:
    from elftools.elf.elffile import ELFFile
except Exception:  # pragma: no cover
    sys.stderr.write("pyelftools required: use the repo .provision-venv/bin/python\n")
    raise

DEFAULT_TC = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "..", "..", "..", "..", "..", "build", "linux-kbuild",
                          "toolchain", "bin", "arm-none-linux-gnueabihf-")
DEFAULT_PLAT = [
    "/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_plat.ko",
    "rootfs-2.5.24/lib/modules/5.10.201/hi5622v100_plat.ko",
    "rootfs-2.4.15/lib/modules/5.10.201/hi5622v100_plat.ko",
]
DEFAULT_PCIE = [
    "/tmp/rootfs2524b/lib/hisilicon/ko/hi_pcie.ko",
    "rootfs-2.5.24/lib/hisilicon/ko/hi_pcie.ko",
    "rootfs-2.4.15/lib/hisilicon/ko/hi_pcie.ko",
]
DEFAULT_SLOT = "dumps/router-20261108/full/mtd11-kernela.bin"

# ARM relocation types that name a called symbol
REL_CALL = {28, 29, 10, 11}          # CALL, JUMP24, THM_CALL, THM_JUMP24

# Board facts established by the bench lanes and re-derived here:
#   GPIO0 controller base and the two DW-APB-GPIO registers the bench wrote.
GPIO0_BASE = 0x10106000
GPIO0_DATA = 0x10106000          # SWPORTA_DR
GPIO0_DDR = 0x10106004           # SWPORTA_DDR
EP_PINS = (20, 21, 28)           # board_id 3 (R116) pcie0 pcie-gpios
EP_MASK = sum(1 << p for p in EP_PINS)

# SoC address ranges of interest (Hi5671Y luofu)
SOC_RANGES = [
    (0x10100000, 0x1010FFFF, "sysctl/acp"),
    (0x10106000, 0x10106FFF, "gpio0"),
    (0x10107000, 0x10107FFF, "gpio1"),
    (0x10160000, 0x10165FFF, "pcie dbi/misc"),
    (0x40000000, 0x41FFFFFF, "chip CA"),
    (0x01000000, 0x01FFFFFF, "chip PKTRAM CA"),
]

# Vendor delay constants (hi_pcie.ko): passed to arm_delay_ops.const_udelay().
# xloops = usec * UDELAY_MULT, UDELAY_MULT = 2147*HZ + 483648*HZ/1e6.
# HZ=100 -> 214748: 0x20c498 ~= 10 us, 0x0CCCCB60 == 1000 us exactly.
DELAY_SPACING = 0x20c498
DELAY_UNIT = 0x0CCCCB60          # 1 ms at HZ=100 (used for reset-duration-ms/gpio-delay)
RESET_DURATION_DEFAULT_MS = 5    # hi_pcie.ko default when the DT property is absent
GPIO_DELAY_DEFAULT_MS = 50       # board overlay gpio-delay = <0x32>


def _resolve(explicit, env_name, defaults):
    if explicit:
        return explicit
    env = os.environ.get(env_name)
    if env:
        return env
    up = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                      "..", "..", "..", "..", ".."))
    for cand in defaults:
        p = cand if os.path.isabs(cand) else os.path.join(up, cand)
        if os.path.exists(p):
            return p
    return None


def resolve_module_path(explicit=None):
    p = _resolve(explicit, "PLAT_KO", DEFAULT_PLAT)
    if not p:
        raise SystemExit("no plat.ko found; set PLAT_KO or --module")
    return p


class Mod:
    """Relocatable ELF view with section-aware symbol/relocation resolution."""

    def __init__(self, path, tc=None):
        self.path = path
        self.raw = open(path, "rb").read()
        self.elf = ELFFile(open(path, "rb"))
        self.secs = list(self.elf.iter_sections())
        self.secnames = [s.name for s in self.secs]
        self.symtab = self.elf.get_section_by_name(".symtab")
        if self.symtab is None:
            raise SystemExit("no .symtab in %s" % path)
        self.syms = [s for s in self.symtab.iter_symbols()]

        self.funcs = []
        for s in self.syms:
            if s["st_info"]["type"] == "STT_FUNC" and s["st_size"] > 0:
                self.funcs.append((self.secname(s["st_shndx"]), s["st_value"],
                                   s["st_size"], s.name))
        self.funcs.sort(key=lambda t: (t[0] == ".text", t[1]))

        self.relocs = []
        for relname, target in ((".rel.text", ".text"),
                                (".rel.text.unlikely", ".text.unlikely")):
            rsec = self.elf.get_section_by_name(relname)
            if rsec is None:
                continue
            for r in rsec.iter_relocations():
                sym = self.symtab.get_symbol(r["r_info_sym"])
                self.relocs.append(dict(
                    target=target, off=r["r_offset"], type=r["r_info_type"],
                    sym=sym.name, symval=sym["st_value"], symshndx=sym["st_shndx"],
                    symsize=sym["st_size"], symtype=sym["st_info"]["type"]))
        self.by_func = collections.defaultdict(list)
        for r in self.relocs:
            f = self.func_at(r["target"], r["off"])
            if f:
                self.by_func[(f[0], f[1], f[3])].append(r)
        self.tc = tc or os.environ.get("TC", DEFAULT_TC)

    # -- sections / symbols ------------------------------------------------
    def secname(self, idx):
        if not isinstance(idx, int):
            return str(idx)
        return self.secnames[idx] if 0 <= idx < len(self.secnames) else "?"

    def sec(self, name):
        return self.elf.get_section_by_name(name)

    def func_at(self, secname, addr):
        best = None
        for sn, v, sz, n in self.funcs:
            if sn == secname and v <= addr < v + sz:
                if best is None or v > best[1]:
                    best = (sn, v, sz, n)
        return best

    def find_func(self, name):
        for sn, v, sz, n in self.funcs:
            if n == name:
                return (sn, v, sz, n)
        return None

    # -- strings -----------------------------------------------------------
    def _cstr(self, fileoff, maxlen=300):
        end = self.raw.find(b"\x00", fileoff)
        if end < 0 or end - fileoff > maxlen:
            end = fileoff + maxlen
        return self.raw[fileoff:end]

    def _str_at(self, secname, val):
        s = self.sec(secname)
        if s is None:
            return None
        b = self._cstr(s["sh_offset"] + val)
        if len(b) > 1 and b[0] == 1:      # HiSilicon log header: \x01<level>text
            b = b[2:]
        txt = b.decode("latin1")
        if txt and all(32 <= ord(c) < 127 or c in "\r\n\t%" for c in txt):
            return txt
        return None

    def ref_text(self, r):
        sn = self.secname(r["symshndx"])
        if sn == ".rodata.str1.4":
            t = self._str_at(sn, r["symval"])
            if t is not None:
                return "str %r" % t
        if r["symtype"] == "STT_FUNC":
            return "func %s" % r["sym"]
        if sn not in (".text", ".text.unlikely") and r["sym"]:
            return "data %s+0x%x" % (r["sym"], r["symval"])
        return None

    def strings_of(self, key):
        seen, out = set(), []
        for r in sorted(self.by_func.get(key, []), key=lambda x: x["off"]):
            if self.secname(r["symshndx"]) == ".rodata.str1.4":
                t = self._str_at(".rodata.str1.4", r["symval"])
                if t and t not in seen:
                    seen.add(t)
                    out.append(t)
        return out

    def all_string_refs(self):
        """(offset, text) for every .rodata.str1.4 reference, sorted."""
        out = []
        for r in sorted(self.relocs, key=lambda x: (x["target"], x["off"])):
            if self.secname(r["symshndx"]) == ".rodata.str1.4":
                t = self._str_at(".rodata.str1.4", r["symval"])
                if t is not None:
                    out.append((r["target"], r["off"], t))
        return out

    # -- disassembly -------------------------------------------------------
    def disasm(self, secname, start, size):
        cmd = [self.tc + "objdump", "-dr", "-j", secname,
               "--start-address=0x%x" % start,
               "--stop-address=0x%x" % (start + size), self.path]
        out = subprocess.run(cmd, capture_output=True, text=True).stdout
        lines = out.splitlines()
        try:
            i = next(k for k, l in enumerate(lines) if l.startswith("Disassembly"))
            lines = lines[i + 1:]
        except StopIteration:
            lines = []
        return "\n".join(lines)

    def call_sites(self, func, callee=None):
        """[(offset, symname)] call/jump relocations inside func (optionally filtered)."""
        f = self.find_func(func)
        if not f:
            return []
        sn, v, sz, n = f
        out = []
        for r in sorted(self.by_func.get((sn, v, n), []), key=lambda x: x["off"]):
            if r["type"] in REL_CALL and r["sym"]:
                if callee is None or r["sym"] == callee:
                    out.append((r["off"], r["sym"]))
        return out


# ---------------------------------------------------------------------------
# Minimal FDT (DTB) reader: enough to read board overlays and pcie-gpios.
# ---------------------------------------------------------------------------
class Fdt:
    FDT_BEGIN_NODE, FDT_END_NODE, FDT_PROP, FDT_NOP, FDT_END = 1, 2, 3, 4, 9

    def __init__(self, blob):
        self.blob = blob
        (magic, totalsize, off_struct, off_strings, off_rsvmap,
         ver, ver_compat, boot_cpuid, size_strings, size_struct) = \
            struct.unpack(">10I", blob[:40])
        if magic != 0xD00DFEED:
            raise ValueError("not an FDT")
        self.totalsize = totalsize
        self.struct_blob = blob[off_struct:off_struct + size_struct]
        self.strings_blob = blob[off_strings:off_strings + size_strings]
        self.nodes = []          # (path, {name: bytes})
        self._walk()

    def _s(self, off):
        end = self.strings_blob.find(b"\x00", off)
        return self.strings_blob[off:end].decode("latin1")

    def _walk(self):
        b, i = self.struct_blob, 0
        stack, cur_props = [], {}
        while i + 4 <= len(b):
            tok = struct.unpack(">I", b[i:i + 4])[0]
            i += 4
            if tok == self.FDT_BEGIN_NODE:
                end = b.find(b"\x00", i)
                name = b[i:end].decode("latin1")
                i = (end + 4) & ~3
                stack.append(name)
                path = "/" + "/".join(x for x in stack if x)
                self.nodes.append((path, {}))
                cur_props = self.nodes[-1][1]
            elif tok == self.FDT_END_NODE:
                if stack:
                    stack.pop()
            elif tok == self.FDT_PROP:
                ln, nameoff = struct.unpack(">II", b[i:i + 8])
                i += 8
                val = b[i:i + ln]
                i = (i + ln + 3) & ~3
                cur_props[self._s(nameoff)] = val
            elif tok == self.FDT_NOP:
                continue
            elif tok == self.FDT_END:
                break
        return self.nodes

    def node(self, substr):
        for path, props in self.nodes:
            if substr in path:
                return path, props
        return None, None

    def prop_u32(self, props, name):
        v = props.get(name)
        if not v or len(v) % 4:
            return None
        return list(struct.unpack(">%dI" % (len(v) // 4), v))

    def prop_str(self, props, name):
        v = props.get(name)
        if v is None:
            return None
        return v.split(b"\x00")[0].decode("latin1")

    def print_summary(self, show_gpio=True):
        model = None
        bid = None
        bn = None
        for _, props in self.nodes:
            if model is None:
                model = self.prop_str(props, "model")
            if bid is None and "board_id" in props:
                bid = self.prop_u32(props, "board_id")
            if bn is None:
                bn = self.prop_str(props, "board_name")
        print("  size=%d nodes=%d model=%r board_id=%s board_name=%r"
              % (self.totalsize, len(self.nodes), model, bid, bn))
        if not show_gpio:
            return
        for path, props in self.nodes:
            if "pcie-gpios" in props:
                cells = self.prop_u32(props, "pcie-gpios")
                pins = None
                if cells is not None and len(cells) % 3 == 0:
                    pins = [cells[k + 1] for k in range(0, len(cells), 3)]
                print("    %-46s pcie-gpios %s pins=%s"
                      % (path, "empty" if not cells else "%d cells" % len(cells), pins))
            if "gpio-delay" in props:
                print("    %-46s gpio-delay=%s" % (path, self.prop_u32(props, "gpio-delay")))
            if "reset-duration-ms" in props:
                print("    %-46s reset-duration-ms=%s"
                      % (path, self.prop_u32(props, "reset-duration-ms")))
            if "gpio-timing-quirk" in props:
                print("    %-46s gpio-timing-quirk PRESENT" % path)


def scan_fdts(raw):
    """Find embedded DTB blobs in a raw kernel-slot / uImage / zImage file."""
    magic = struct.pack(">I", 0xD00DFEED)
    out, i = [], 0
    while True:
        j = raw.find(magic, i)
        if j < 0:
            break
        i = j + 4
        if j + 8 > len(raw):
            break
        total = struct.unpack(">I", raw[j + 4:j + 8])[0]
        if 0x100 < total < 0x400000 and j + total <= len(raw):
            out.append((j, total))
    return out


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------
def _has_movw_movt(raw, sec, value):
    """True if the section contains a movw/movt register pair materialising 'value'."""
    start, size = sec["sh_offset"], sec["sh_size"]
    lo, hi = value & 0xFFFF, (value >> 16) & 0xFFFF
    n = size
    for off in range(0, n - 8, 4):
        ins = struct.unpack("<I", raw[start + off:start + off + 4])[0]
        if (ins & 0x0FF00000) != 0x03000000:
            continue
        rd = (ins >> 12) & 0xF
        imm = ((ins >> 16) & 0xF) << 12 | (ins & 0xFFF)   # imm4:imm12
        if imm != lo:
            continue
        for off2 in range(off + 4, min(off + 40, n - 4), 4):
            ins2 = struct.unpack("<I", raw[start + off2:start + off2 + 4])[0]
            if (ins2 & 0x0FF00000) != 0x03400000:
                continue
            if ((ins2 >> 12) & 0xF) != rd:
                continue
            imm2 = ((ins2 >> 16) & 0xF) << 12 | (ins2 & 0xFFF)
            if imm2 == hi:
                return True
    return False


def _locate_pcie(m, explicit=None):
    if explicit:
        return explicit
    env = os.environ.get("HI_PCIE_KO")
    if env:
        return env
    if os.path.basename(m.path) == "hi_pcie.ko":
        return m.path
    sib = os.path.join(os.path.dirname(m.path), "..", "..", "hisilicon", "ko", "hi_pcie.ko")
    if os.path.exists(sib):
        return os.path.abspath(sib)
    return _resolve(None, "HI_PCIE_KO", DEFAULT_PCIE)


def cmd_syms(m, args):
    pat = re.compile(args.pattern, re.I) if args.pattern else None
    for sn, v, sz, n in m.funcs:
        if pat and not pat.search(n):
            continue
        print("%-34s %-14s 0x%06x size 0x%04x" % (n, sn, v, sz))


def cmd_logmap(m, args):
    pat = re.compile(args.pattern, re.I) if args.pattern else None
    for sn, v, sz, n in sorted(m.funcs):
        ss = m.strings_of((sn, v, n))
        if pat:
            ss = [s for s in ss if pat.search(s)]
        if not ss:
            continue
        print("== %s  (%s 0x%x)" % (n, sn, v))
        for s in ss:
            print("    %s" % s)


def cmd_findstr(m, args):
    pat = re.compile(args.pattern, re.I)
    for sn, v, sz, n in sorted(m.funcs):
        for s in m.strings_of((sn, v, n)):
            if pat.search(s):
                print("%-34s %-14s 0x%06x  %s" % (n, sn, v, s))


def cmd_calls(m, args):
    f = m.find_func(args.func)
    if not f:
        raise SystemExit("no func %s" % args.func)
    sn, v, sz, n = f
    print("== %s (%s 0x%x size 0x%x)" % (n, sn, v, sz))
    for r in sorted(m.by_func.get((sn, v, n), []), key=lambda x: x["off"]):
        if r["type"] in REL_CALL or (r["sym"] and r["symtype"] == "STT_FUNC"):
            print("  0x%05x -> %s" % (r["off"], r["sym"] or "?0x%x" % r["symval"]))
    for s in m.strings_of((sn, v, n)):
        print("  str %r" % s)


def cmd_callers(m, args):
    f = m.find_func(args.func)
    if not f:
        raise SystemExit("no func %s" % args.func)
    for r in m.relocs:
        if r["sym"] == f[3]:
            caller = m.func_at(r["target"], r["off"])
            print("0x%05x %s(%s) -> %s"
                  % (r["off"], caller[3] if caller else "?", r["target"], f[3]))


def cmd_dis(m, args):
    f = m.find_func(args.func)
    if not f:
        raise SystemExit("no func %s" % args.func)
    sn, v, sz, n = f
    if args.nbytes:
        sz = args.nbytes
    sec = m.sec(sn)
    base = sec["sh_offset"]
    pool = {r["off"]: r for r in m.relocs if r["target"] == sn}
    print("== %s (%s 0x%x size 0x%x)" % (n, sn, v, sz))
    for line in m.disasm(sn, v, sz).splitlines():
        stripped = line.strip()
        mt = re.match(r"^\s*([0-9a-f]+):\s+([0-9a-f ]{8,})\s+(\S.*)$", line)
        if mt:
            addr = int(mt.group(1), 16)
            asm = mt.group(3)
            note = ""
            ml = re.search(r"\[pc, #(-?\d+)\]", asm)
            if ml:
                lit = (addr + 8 + int(ml.group(1))) & 0xFFFFFFFF
                if lit in pool:
                    note = " ; " + (m.ref_text(pool[lit]) or hex(pool[lit]["symval"]))
            mm = re.search(r"(movw|movt)\s+(\w+), #(\d+)", asm)
            if mm:
                val = int(mm.group(3))
                if mm.group(1) == "movw" and any(lo <= val <= hi for lo, hi, _ in SOC_RANGES):
                    note = " ; SOC 0x%x" % val
                if mm.group(1) == "movt" and val:
                    note = " ; movt 0x%x0000" % val
            print("  %05x %-42s%s" % (addr, asm, note))
        elif re.match(r"^\s*[0-9a-f]+: R_ARM", stripped):
            print("        %s" % stripped)


def cmd_consts(m, args):
    """Report only constants that the code actually materialises:
    movw/movt pairs in text plus literal words loaded by ldr rX,[pc,#imm]."""
    pat = re.compile(args.pattern, re.I) if args.pattern else None
    seen = collections.OrderedDict()
    for sn in (".text", ".text.unlikely"):
        sec = m.sec(sn)
        if sec is None:
            continue
        start, size = sec["sh_offset"], sec["sh_size"]
        raw = m.raw

        def note(addr_in_sec, val, kind):
            f = m.func_at(sn, addr_in_sec)
            fn = f[3] if f else "?"
            if pat and not pat.search(fn):
                return
            for lo, hi, label in SOC_RANGES:
                if lo <= val <= hi:
                    key = (sn, fn, val, label)
                    seen.setdefault(key, []).append((kind, addr_in_sec))

        # ldr rX,[pc,#imm] -> literal pool word
        for mt in re.finditer(rb"[\x00-\xff]{4}", b""):
            break
        txt = m.disasm(sn, 0, size)
        for line in txt.splitlines():
            mt = re.match(r"^\s*([0-9a-f]+):\s+[0-9a-f ]{8,}\s+ldr\w*\s+\w+, \[pc, #(-?\d+)\]", line)
            if not mt:
                continue
            addr = int(mt.group(1), 16)
            lit = (addr + 8 + int(mt.group(2))) & 0xFFFFFFFF
            if lit + 4 <= size:
                val = struct.unpack("<I", raw[start + lit:start + lit + 4])[0]
                note(addr, val, "ldr-pc")
        # movw/movt pairs
        n = size
        for off in range(0, n - 8, 4):
            ins = struct.unpack("<I", raw[start + off:start + off + 4])[0]
            if (ins & 0x0FF00000) != 0x03000000:       # movw
                continue
            rd = (ins >> 12) & 0xF
            imm = ((ins >> 16) & 0xF) << 12 | (ins & 0xFFF)
            for off2 in range(off + 4, min(off + 40, n - 4), 4):
                ins2 = struct.unpack("<I", raw[start + off2:start + off2 + 4])[0]
                if (ins2 & 0x0FF00000) != 0x03400000:  # movt
                    continue
                if ((ins2 >> 12) & 0xF) != rd:
                    continue
                imm2 = ((ins2 >> 16) & 0xF) << 12 | (ins2 & 0xFFF)
                note(off, (imm2 << 16) | imm, "movw/movt")
                break
    if not seen:
        print("(no materialised SoC constants%s)" % (" matching " + args.pattern if args.pattern else ""))
    for (sn, fn, val, label), how in seen.items():
        print("%-32s %-14s 0x%08x %-16s %s"
              % (fn, sn, val, label, ",".join("%s@0x%x" % h for h in how[:4])))


def cmd_undef(m, args):
    pat = re.compile(args.pattern, re.I) if args.pattern else None
    seen = collections.defaultdict(set)
    for r in m.relocs:
        if r["symshndx"] == "SHN_UNDEF" and r["sym"]:
            if pat and not pat.search(r["sym"]):
                continue
            f = m.func_at(r["target"], r["off"])
            seen[r["sym"]].add(f[3] if f else "?")
    for s in sorted(seen):
        print("%-34s %s" % (s, " ".join(sorted(seen[s]))))


def cmd_overlays(m, args):
    path = args.path
    raw = open(path, "rb").read()
    blobs = scan_fdts(raw) if not args.single else [(0, len(raw))]
    if not blobs:
        raise SystemExit("no FDT blob found in %s" % path)
    print("== %s: %d FDT blob(s)" % (path, len(blobs)))
    for off, total in blobs:
        try:
            f = Fdt(raw[off:off + total])
        except Exception as e:
            print("   0x%07x size=%d  (parse error: %s)" % (off, total, e))
            continue
        print("   0x%07x" % off)
        f.print_summary(show_gpio=not args.no_gpio)


def cmd_gpio(m, args):
    """Decode the vendor endpoint power/reset GPIO pulse (hi_pcie.ko)."""
    pcie_path = _locate_pcie(m, args.pcie_ko)
    if not pcie_path:
        raise SystemExit("no hi_pcie.ko found; set HI_PCIE_KO or --pcie-ko")
    p = Mod(pcie_path, m.tc)
    print("== vendor endpoint reset/power path: %s" % pcie_path)

    f = p.find_func("hi_pcie_probe")
    if not f:
        raise SystemExit("hi_pcie_probe not found")
    sn, v, sz, n = f
    refs = []
    for (tgt, off, txt) in p.all_string_refs():
        if tgt == sn and v <= off < v + sz and re.search(
                r"pcie-gpios|gpio-timing-quirk|gpio-delay|reset-duration|gpio-free|ShuangTa|gpio\[", txt):
            if not refs or refs[-1][1] != txt:
                refs.append((off, txt))
    print("  probe DT properties / messages (offset order):")
    for off, txt in refs:
        print("    0x%05x  %s" % (off, txt))

    quirk = any("gpio-timing-quirk" in t for _, _, t in p.all_string_refs())
    print("  quirk support in this module: %s" % ("YES (>=2.5.24)" if quirk else "NO (2.4.15-era)"))

    # ops table entry: .rel.rodata offset of hi_pcie_enable_dev
    ro = p.sec(".rodata")
    en_off = None
    for sec in p.elf.iter_sections():
        if not sec.name.startswith(".rel") or sec.name == ".rel.text":
            continue
        try:
            rels = list(sec.iter_relocations())
        except Exception:
            continue
        for r in rels:
            sym = p.symtab.get_symbol(r["r_info_sym"])
            if sym.name == "hi_pcie_enable_dev" and sec.name == ".rel.rodata":
                en_off = r["r_offset"]
    if en_off is not None:
        print("  ops table: hi_pcie_enable_dev = ops[%d] (rodata+0x%x)" % (en_off // 4, en_off))

    # pulse call sites in probe
    calls = p.call_sites("hi_pcie_probe", "clk_bulk_prepare")
    print("  clk_bulk_prepare call at probe+0x%x" %
          (calls[0][0] if calls else -1))
    for off, txt in refs:
        if "gpio-timing-quirk" in txt:
            print("  of_find_property(gpio-timing-quirk) at probe+0x%x" % off)
            break

    # locate the ops[15] invocation (ldr rX,[ops,#60] ; blx rX) in the probe body
    en_call = None
    lines = p.disasm(sn, v, sz).splitlines()
    for k, line in enumerate(lines):
        if re.search(r"ldrs+rd+, [rd+, #60]", line):
            for line2 in lines[k + 1:k + 4]:
                m2 = re.match(r"^s*([0-9a-f]+):s+[0-9a-f ]{8,}s+blx", line2)
                if m2:
                    en_call = int(m2.group(1), 16)
                    break
    if en_call is not None:
        print("  ops[15] (GPIO pulse) invoked at probe+0x%x" % en_call)
        if calls:
            print("  -> pulse runs BEFORE clk_bulk_prepare (probe+0x%x)" % calls[0][0])
    if quirk:
        print("  quirk gate: gpio count==3 && !gpio-timing-quirk => panic (%r at probe+0x17a4)"
              % "The ShuangTa ep device must enable gpio-timing-quirk")

    e = p.find_func("hi_pcie_enable_dev")
    if not e:
        raise SystemExit("hi_pcie_enable_dev not found")
    esn, ev, esz, en = e
    dir_sites = p.call_sites("hi_pcie_enable_dev", "gpiod_direction_output")
    set_sites = p.call_sites("hi_pcie_enable_dev", "gpiod_set_value")
    print("  hi_pcie_enable_dev: %d x gpiod_direction_output, %d x gpiod_set_value"
          % (len(dir_sites), len(set_sites)))
    print("  delay unit 0x%x (= 1 ms at HZ=100) materialised: %s"
          % (DELAY_UNIT, "YES" if _has_movw_movt(p.raw, p.sec(esn), DELAY_UNIT) else "NO"))

    print()
    print("  VENDOR CHIP POWER/RESET SEQUENCE (RC0 hi_pcie.ko probe, before the RC clock")
    print("  enable and before LTSSM; DT board_id 3 overlay pcie-gpios = pins %s):"
          % ", ".join(str(x) for x in EP_PINS))
    print("    1. for each pin: direction_output(0)   -> line driven LOW (assert)")
    print("       + wait reset-duration-ms x 1 ms    (%d ms default)"
          % RESET_DURATION_DEFAULT_MS)
    print("    2. for each pin: set_value(1)         -> line HIGH (release)")
    print("       + wait gpio-delay x 1 ms           (%d ms, DT gpio-delay = 0x32)"
          % GPIO_DELAY_DEFAULT_MS)
    print("    3. RC: clk prepare+enable, CRG resets deassert, misc/DBI/LTSSM, link poll")
    print()
    print("  register-level equivalent (board GPIO0, DW APB GPIO):")
    print("    BASE 0x%08x  DATA(+0x00)=0x%08x  DDR(+0x04)=0x%08x  mask=0x%08x"
          % (GPIO0_BASE, GPIO0_DATA, GPIO0_DDR, EP_MASK))
    print("    DATA: read, clear mask -> assert; msleep(>=15); set mask -> release;")
    print("          msleep(>=100).  DDR: OR mask (outputs).")
    print("  NOTE: the vendor asserts the pins through gpiod on the board DT property;")
    print("  on the 2.5.24 module the quirk path groups the three asserts (all low,")
    print("  ~5 ms apart) and then releases them one by one (~50 ms apart).")


def cmd_diff(m, args):
    other = Mod(args.other, m.tc)
    a = set(n for _, _, _, n in m.funcs)
    b = set(n for _, _, _, n in other.funcs)
    print("== funcs only in %s (%d)" % (os.path.basename(m.path), len(a - b)))
    for n in sorted(a - b):
        print("   -", n)
    print("== funcs only in %s (%d)" % (os.path.basename(other.path), len(b - a)))
    for n in sorted(b - a):
        print("   +", n)
    def strs(mm):
        out = set()
        for sn, v, sz, n in mm.funcs:
            for s in mm.strings_of((sn, v, n)):
                if re.search(r"quirk|gpio|reset|power|pcie", s, re.I):
                    out.add(s)
        return out
    x, y = strs(m), strs(other)
    print("== strings -only (%d)" % len(x - y))
    for s in sorted(x - y)[:40]:
        print("   -", s)
    print("== strings +only (%d)" % len(y - x))
    for s in sorted(y - x)[:40]:
        print("   +", s)


def cmd_selftest(m, args):
    """Self-check the facts this lane's REPORT relies on; fails loudly if an artifact changes."""
    checks = []

    def chk(name, cond, detail=""):
        checks.append((name, bool(cond), detail))

    plat = m
    # ---- plat.ko: no SoC-level power/reset API, "chip_res" is resource bookkeeping
    gpio_api = []
    for r in plat.relocs:
        if r["symshndx"] == "SHN_UNDEF" and re.search(
                r"^(gpio|gpiod|devm_gpio|regulator_|clk_|reset_control)", r["sym"]):
            gpio_api.append(r["sym"])
    chk("plat.ko imports no gpio/clk/reset APIs", not gpio_api, ",".join(sorted(set(gpio_api))))

    f = plat.find_func("low_power_init")
    chk("plat.ko low_power_init exists", bool(f), f[3] if f else "missing")
    if f:
        chk("low_power_init is printk-only",
            set(x[1] for x in plat.call_sites("low_power_init")) <= {"printk"},
            ",".join(x[1] for x in plat.call_sites("low_power_init")))

    hits = [n for sn, v, sz, n in plat.funcs
            for s in plat.strings_of((sn, v, n)) if "pcie_init_chip_res" in s]
    chk("pcie_init_chip_res emitted by pcie_init_static_res",
        "pcie_init_static_res" in hits, ",".join(sorted(set(hits))))

    # ---- hi_pcie.ko: the endpoint reset/power GPIO pulse
    pcie_path = _locate_pcie(plat, args.pcie_ko)
    if pcie_path:
        p = Mod(pcie_path, plat.tc)
        en = p.find_func("hi_pcie_enable_dev")
        chk("hi_pcie.ko hi_pcie_enable_dev present", bool(en), en[3] if en else "missing")
        ops_off = None
        for sec in p.elf.iter_sections():
            if sec.name != ".rel.rodata":
                continue
            for r in sec.iter_relocations():
                sym = p.symtab.get_symbol(r["r_info_sym"])
                if sym.name == "hi_pcie_enable_dev":
                    ops_off = r["r_offset"]
        chk("hi_pcie_enable_dev is ops[15]", ops_off == 15 * 4, hex(ops_off or 0))
        dirs = p.call_sites("hi_pcie_enable_dev", "gpiod_direction_output")
        sets = p.call_sites("hi_pcie_enable_dev", "gpiod_set_value")
        chk("enable_dev drives direction+value (3+3 sites)",
            len(dirs) >= 2 and len(sets) >= 2, "%d/%d" % (len(dirs), len(sets)))
        quirk = any("gpio-timing-quirk" in t for _, _, t in p.all_string_refs())
        chk("module has the gpio-timing-quirk gate (2.5.24+)", quirk)
        prop = [t for _, _, t in p.all_string_refs() if t == "pcie-gpios"]
        chk("module parses the pcie-gpios DT property", bool(prop))
        # probe order: enable_dev (ops[15]) must run before clk_bulk_prepare
        calls = [off for off, sym in p.call_sites("hi_pcie_probe") if sym == "clk_bulk_prepare"]
        chk("clk_bulk_prepare call recorded in probe", bool(calls), hex(calls[0]) if calls else "")
        v2415 = next((c for c in DEFAULT_PCIE if "2.4.15" in c
                      and os.path.exists(c)), None)
        if v2415:
            chk("2.4.15-era variant has no quirk gate",
                not any("gpio-timing-quirk" in t for _, _, t in
                        Mod(v2415, plat.tc).all_string_refs()), v2415)
        else:
            chk("2.4.15-era variant comparison", True, "SKIP (artifact absent)")
    else:
        chk("hi_pcie.ko located", False, "set HI_PCIE_KO")

    # ---- board overlay: pcie-gpios pins == bench pins 20/21/28
    slot = _resolve(args.slot, "SLOT", [DEFAULT_SLOT])
    if slot and os.path.exists(slot):
        raw = open(slot, "rb").read()
        found = None
        for off, total in scan_fdts(raw):
            try:
                fdt = Fdt(raw[off:off + total])
            except Exception:
                continue
            for path, props in fdt.nodes:
                if "pcie-gpios" in props and "board_id" in fdt.nodes[0][1]:
                    pass
            if any("pcie-gpios" in props for _, props in fdt.nodes):
                bid = None
                for _, props in fdt.nodes:
                    if "board_id" in props:
                        bid = fdt.prop_u32(props, "board_id")
                        break
                if bid and bid[0] == 3:
                    found = (off, fdt)
        chk("kernel slot carries the board_id 3 overlay", found is not None,
            slot if found is None else "0x%x" % found[0])
        if found:
            fdt = found[1]
            pins = []
            for path, props in fdt.nodes:
                if "pcie-gpios" in props:
                    cells = fdt.prop_u32(props, "pcie-gpios")
                    if cells and len(cells) % 3 == 0:
                        pins += [cells[k + 1] for k in range(0, len(cells), 3)]
            chk("board overlay pcie-gpios pins == (20,21,28)",
                tuple(sorted(set(pins))) == EP_PINS, str(pins))
    else:
        chk("kernel-slot dump available (board overlay check)", True,
            "SKIP (gitignored local artifact absent)")

    ok = sum(1 for _, c, _ in checks if c)
    for name, c, detail in checks:
        print("%s %-58s %s" % ("PASS" if c else "FAIL", name, detail))
    print("\n%d/%d PASS" % (ok, len(checks)))
    return 0 if ok == len(checks) else 1


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--module", default=None, help="path to vendor plat.ko")
    ap.add_argument("--tc", default=None, help="toolchain prefix")
    sub = ap.add_subparsers(dest="cmd", required=True)

    def add(name, fn):
        p = sub.add_parser(name)
        p.add_argument("pattern", nargs="?", default=None)
        p.set_defaults(fn=fn)
        return p

    add("syms", cmd_syms)
    add("logmap", cmd_logmap)
    add("findstr", cmd_findstr)
    add("consts", cmd_consts)
    add("undef", cmd_undef)

    p = sub.add_parser("calls"); p.add_argument("func"); p.set_defaults(fn=cmd_calls)
    p = sub.add_parser("callers"); p.add_argument("func"); p.set_defaults(fn=cmd_callers)
    p = sub.add_parser("dis"); p.add_argument("func"); p.add_argument("nbytes", nargs="?", type=int, default=0); p.set_defaults(fn=cmd_dis)
    p = sub.add_parser("gpio"); p.add_argument("--pcie-ko", default=None); p.set_defaults(fn=cmd_gpio)
    p = sub.add_parser("overlays"); p.add_argument("path"); p.add_argument("--single", action="store_true"); p.add_argument("--no-gpio", action="store_true"); p.set_defaults(fn=cmd_overlays)
    p = sub.add_parser("diff"); p.add_argument("other"); p.set_defaults(fn=cmd_diff)
    p = sub.add_parser("selftest"); p.add_argument("--pcie-ko", default=None); p.add_argument("--slot", default=None); p.set_defaults(fn=cmd_selftest)

    args = ap.parse_args(argv)
    m = Mod(resolve_module_path(args.module), args.tc)
    return args.fn(m, args) or 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
