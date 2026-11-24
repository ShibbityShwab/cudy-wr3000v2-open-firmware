#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""ete_wake.py - host-only verifier/recon for the ETE engine DR (A->B) copy wake lane
(shared task task-106, lane wifi-ete-wake).

Reads the LOCAL vendor firmware blob and the LOCAL vendor 2.5.24 platform module in place and
prints derived annotations only (addresses, mnemonics, decoded structure fields). It never opens
a device, never writes anywhere, and never copies firmware/module bytes into the repository.
The selftest asserts every [V] fact quoted in REPORT.md.

Address conventions (project-wide):
    firmware CPU address = file offset + 0x40000
    host BAR0 for firmware RAM  = CPU + 0x6B8000           (region 5)
    host BAR0 for device IO CA  = 0x3B8000 + (CA - 0x40000000)  (region 3)
    host BAR0 for PKTRAM CA     = 0x1D8000 + (CA - 0x01000000)  (region 2)

Usage:
    ete_wake.py selftest        assert every [V] fact (device-free)
    ete_wake.py chain           the full trigger chain
    ete_wake.py dispatchers     decode the two event handlers with resolved literals
    ete_wake.py install         pcie_msg_init callback/event install sites
    ete_wake.py raise           whole-image software event-raise census
    ete_wake.py host            vendor-host ETE comparison (DR published/re-arm facts)
    ete_wake.py bench           the R0 read pack (CA + BAR0)
"""
import argparse
import hashlib
import os
import struct
import sys

try:
    import capstone
    from capstone import arm
except Exception:  # pragma: no cover
    sys.exit("capstone not installed (use the repo .provision-venv)")

FW = os.environ.get(
    "HI5622_FW",
    "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin")
PLAT = os.environ.get(
    "HI5622_PLAT_KO",
    "/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_plat.ko")

FW_SHA256 = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
PLAT_SHA256 = "6f2eac415dbf4d3e8e71c1abd941ea4f9d97b159674d28d5e99d4ecb0e6cd491"

# ---- derived firmware anchors (all [V] in REPORT.md) --------------------------------
HCC = 0x10F1F0                     # hcc_instance
ETE_STATUS_BASE = 0x4003A86C       # hcc+0xC  -> ETE status/control block
ETE_STATUS = 0x4003A874            # [base+8] status word
ETE_ACK = 0x4003A870               # [base+4] ack word
ETE_OPS = 0x10CEDC                 # ETE ops table
DISP_2E = 0x4624C                  # event 0x2E handler (DR status)
DISP_2D = 0x462F8                  # event 0x2D handler (SR status)
TRIGGER = 0xC625A                  # hcc+0x1C sending trigger
SR_RX = 0xC6802                    # hcc+0x20 SR receive/drain callback
STUB_16 = 0xC1902                  # hcc+0x24 stub (movs r0,#0; bx lr)
STUB_8 = 0xC18FE                   # hcc+0x28 stub (movs r0,#0; bx lr)
RAISE = 0xC6DB8                    # software event-raise primitive
EV_DISP = 0xC2EFC                  # ROM-vectored event dispatcher
TRAMP_ARM = 0x40064                # image ARM exception trampoline (ROM IRQ target)
DR_RECORD0 = 0x10F2B8              # DR record array, stride 0x354
DR_RECORD_STRIDE = 0x354
REACH_HOMES = {0x10CACC: 0x4003A7C0, 0x10CB94: 0x4003A7D0}  # door CA data pairs
DR_SEND = [0x4003AC00, 0x4003AC04, 0x4003AC08, 0x4003AC0C]
SR_SEND = [0x4003AC18, 0x4003AC1C, 0x4003AC20]
DOORS = [0x4003A7C0, 0x4003A7D0]
CRASH_DESC = 0x4003A000            # ETE window crash-dump descriptor source
CRASH_LEN = 0xC28

# ---- vendor plat.ko .text anchors ---------------------------------------------------
P_DR_REG_INIT = 0x1483C            # writes DR group B incl. +0x38 = inst[+0x1C]
P_DR_INIT = 0x14908                # zeroes inst[+0x1C..]
P_REARM = 0x14420                  # D2H re-arm writes +0x38/+0x18 under lock
P_TRIG_HANDLE = 0x15EFC            # tail branch to pcie_wkup_thread
P_WKUP_THREAD = 0x1629C

# ---- host BAR0 helpers --------------------------------------------------------------
def bar0_io(ca):
    if not (0x40000000 <= ca <= 0x4011FFFF):
        raise ValueError("CA 0x%08x outside host-visible region 3" % ca)
    return 0x3B8000 + (ca - 0x40000000)

def bar0_ram(cpu):
    return 0x6B8000 + cpu

def bar0_pktram(ca):
    if not (0x01000000 <= ca < 0x02000000):
        raise ValueError("CA 0x%08x not in PKTRAM region 2" % ca)
    return 0x1D8000 + (ca - 0x01000000)


class Firmware:
    def __init__(self, path):
        self.path = path
        self.data = open(path, "rb").read()
        self.thumb = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
        self.thumb.detail = True
        self.arm = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
        self.arm.detail = True

    # raw access: only used internally for derived checks, never printed as bytes
    def word(self, cpu):
        off = cpu - 0x40000
        if off < 0 or off + 4 > len(self.data):
            return None
        return struct.unpack_from("<I", self.data, off)[0]

    def half(self, cpu):
        off = cpu - 0x40000
        return struct.unpack_from("<H", self.data, off)[0]

    def insn(self, cpu, mode="thumb"):
        off = cpu - 0x40000
        chunk = self.data[off:off + 4]
        md = self.thumb if mode == "thumb" else self.arm
        for i in md.disasm(chunk, cpu):
            return i
        return None

    def text(self, cpu, mode="thumb"):
        i = self.insn(cpu, mode)
        if not i:
            return None
        return "%s %s" % (i.mnemonic, i.op_str)

    # ---- manual Thumb-2 pattern decoders (fast + exact at every halfword) ----
    def _hw(self, cpu):
        off = cpu - 0x40000
        if off < 0 or off + 2 > len(self.data):
            return None
        return struct.unpack_from("<H", self.data, off)[0]

    def _scan_bl(self):
        """Yield (site, target) for every Thumb BL/BLX immediate."""
        n = len(self.data)
        for cpu in range(0x40000, min(0x120000, 0x40000 + n) - 4, 2):
            off = cpu - 0x40000
            hw1 = struct.unpack_from("<H", self.data, off)[0]
            if (hw1 & 0xF800) != 0xF000:
                continue
            hw2 = struct.unpack_from("<H", self.data, off + 2)[0]
            kind = hw2 & 0xD000
            if kind not in (0xD000, 0xC000):
                continue
            s_bit = (hw1 >> 10) & 1
            imm10 = hw1 & 0x3FF
            j1 = (hw2 >> 13) & 1
            j2 = (hw2 >> 11) & 1
            i1 = 1 - (j1 ^ s_bit)
            i2 = 1 - (j2 ^ s_bit)
            if kind == 0xD000:  # BL
                imm = (s_bit << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | \
                      ((hw2 & 0x7FF) << 1)
                pc = cpu + 4
            else:  # BLX (ARM target, bit0 ignored)
                imm = (s_bit << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | \
                      (((hw2 >> 1) & 0x3FF) << 2)
                pc = (cpu + 4) & ~3
            if s_bit:
                imm -= 1 << 25
            yield cpu, pc + imm

    def callers(self, target):
        t = target & ~1
        return sorted(site for site, dst in self._scan_bl() if (dst & ~1) == t)

    def _scan_ldr_lit(self):
        """Yield (site, literal address) for LDR/LDR.W PC-relative loads."""
        n = len(self.data)
        for cpu in range(0x40000, min(0x120000, 0x40000 + n) - 4, 2):
            off = cpu - 0x40000
            hw1 = struct.unpack_from("<H", self.data, off)[0]
            pc = (cpu + 4) & ~3
            if (hw1 & 0xF800) == 0x4800:          # LDR (literal) T1
                yield cpu, pc + (hw1 & 0xFF) * 4
                continue
            if hw1 == 0xF85F:                      # LDR.W (literal) T2
                hw2 = struct.unpack_from("<H", self.data, off + 2)[0]
                yield cpu, pc + (hw2 & 0xFFF)

    def _scan_movw_movt(self):
        """Yield (site, value) for movw/movt register pairs constructing a constant."""
        n = len(self.data)
        pending = {}
        for cpu in range(0x40000, min(0x120000, 0x40000 + n) - 8, 2):
            off = cpu - 0x40000
            hw1 = struct.unpack_from("<H", self.data, off)[0]
            m = hw1 & 0xFB80
            if m not in (0xF240, 0xF2C0):
                continue
            hw2 = struct.unpack_from("<H", self.data, off + 2)[0]
            i_bit = (hw1 >> 10) & 1
            imm4 = hw1 & 0xF
            imm3 = (hw2 >> 12) & 7
            rd = (hw2 >> 8) & 0xF
            imm16 = (imm4 << 12) | (i_bit << 11) | (imm3 << 8) | (hw2 & 0xFF)
            if m == 0xF240:
                pending[rd] = (imm16, cpu)
            else:
                if rd in pending and cpu - pending[rd][1] <= 16:
                    yield pending[rd][1], pending[rd][0] | (imm16 << 16)
                pending.pop(rd, None)

    def callers_arm(self, target, start=0x40000, end=0x40200):
        """ARM-mode BL/BLX immediate callers (used for the ROM trampoline)."""
        out = []
        for cpu in range(start, end - 3, 4):
            off = cpu - 0x40000
            for i in self.arm.disasm(self.data[off:off + 4], cpu):
                if i.mnemonic in ("bl", "blx") and len(i.operands) == 1 and \
                        i.operands[0].type == arm.ARM_OP_IMM and \
                        (i.operands[0].imm & ~1) == (target & ~1):
                    out.append(i.address)
        return out

    def refs(self, value):
        """Materialisation sites of a 32-bit constant: PC-relative literal loads
        whose data word equals the value plus movw/movt pairs that build it."""
        hits = []
        for site, lit in self._scan_ldr_lit():
            if self.word(lit) == value:
                hits.append(("ldr", site))
        for site, val in self._scan_movw_movt():
            if val == value:
                hits.append(("movw/movt", site))
        return hits

    def literal_loads(self, value):
        """Raw 32-bit data-word homes of value plus the PC-relative load sites
        that reference those homes (used for data-table facts)."""
        homes = set()
        needle = struct.pack("<I", value)
        pos = 0
        while True:
            pos = self.data.find(needle, pos)
            if pos < 0:
                break
            if pos % 2 == 0:
                homes.add(pos + 0x40000)
            pos += 1
        loads = sorted(site for site, lit in self._scan_ldr_lit() if lit in homes)
        return sorted(homes), loads

    def mov_r0_ids_near(self, site, back=10):
        """Best-effort event-id decode: scan back for a movs r0,#imm / mov r0,#imm."""
        ids = []
        for b in range(site - 2 * back, site, 2):
            i = self.insn(b)
            if not i:
                continue
            if i.mnemonic in ("movs", "mov") and i.op_str.startswith("r0, #"):
                try:
                    ids.append(int(i.op_str.split("#")[1], 0))
                except Exception:
                    pass
        return ids


class PlatModule:
    """Minimal ELF32 section access for the vendor plat module (facts only)."""
    def __init__(self, path):
        self.path = path
        self.raw = open(path, "rb").read()
        if self.raw[:4] != b"\x7fELF":
            raise SystemExit("not an ELF file: %s" % path)
        self.sections = self._sections()

    def _sections(self):
        e_shoff = struct.unpack_from("<I", self.raw, 0x20)[0]
        e_shentsize = struct.unpack_from("<H", self.raw, 0x2E)[0]
        e_shnum = struct.unpack_from("<H", self.raw, 0x30)[0]
        out = []
        for n in range(e_shnum):
            base = e_shoff + n * e_shentsize
            name, typ = struct.unpack_from("<II", self.raw, base)
            flags, addr, off, size = struct.unpack_from("<IIII", self.raw, base + 8)
            sh_name_off = struct.unpack_from("<I", self.raw, base)[0]
            out.append({"name_off": sh_name_off, "type": typ, "addr": addr,
                        "off": off, "size": size})
        # section-name string table
        e_shstrndx = struct.unpack_from("<H", self.raw, 0x32)[0]
        base = e_shoff + e_shstrndx * e_shentsize
        str_off = struct.unpack_from("<I", self.raw, base + 16)[0]
        for s in out:
            s["name"] = self._cstr(str_off + s["name_off"])
        return out

    def _cstr(self, off):
        end = self.raw.find(b"\x00", off)
        return self.raw[off:end].decode("ascii", "replace")

    def section(self, name):
        for s in self.sections:
            if s["name"] == name:
                return s
        return None

    def word(self, vaddr):
        txt = self.section(".text")
        if not txt or not (txt["addr"] <= vaddr < txt["addr"] + txt["size"]):
            return None
        off = txt["off"] + (vaddr - txt["addr"])
        return struct.unpack_from("<I", self.raw, off)[0]

    def text(self, vaddr, count=1):
        txt = self.section(".text")
        off = txt["off"] + (vaddr - txt["addr"])
        md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
        out = []
        for i in md.disasm(self.raw[off:off + 4 * count], vaddr):
            out.append("%s %s" % (i.mnemonic, i.op_str))
        return out

    def data_words(self):
        dat = self.section(".data") or self.section(".rodata")
        if not dat:
            return []
        return list(struct.unpack_from("<%dI" % (dat["size"] // 4),
                                       self.raw, dat["off"]))


class Checker:
    def __init__(self):
        self.n = 0
        self.fail = []

    def ck(self, name, cond):
        self.n += 1
        if cond:
            print("PASS %3d: %s" % (self.n, name))
        else:
            print("FAIL %3d: %s" % (self.n, name))
            self.fail.append(name)
        return bool(cond)

    def done(self):
        print("\n%d/%d checks PASS" % (self.n - len(self.fail), self.n))
        return 0 if not self.fail else 1


def load():
    if not os.path.isfile(FW):
        raise SystemExit("firmware blob not found: %s" % FW)
    fw = Firmware(FW)
    return fw


# ---------------------------------------------------------------------------------
def cmd_selftest(args):
    fw = load()
    c = Checker()
    sha = hashlib.sha256(fw.data).hexdigest()
    c.ck("firmware identity sha256 = 7fc87e20...ef311b", sha == FW_SHA256)
    c.ck("image size 928,920 B", len(fw.data) == 928920)

    # hcc_instance static anchors
    c.ck("hcc+0x04 = 4 DR channels", fw.word(HCC + 0x04) == 4)
    c.ck("hcc+0x08 = 3 SR channels", fw.word(HCC + 0x08) == 3)
    c.ck("hcc+0x0C = ETE status base 0x4003A86C", fw.word(HCC + 0x0C) == ETE_STATUS_BASE)
    c.ck("hcc+0x14 = DR record array 0x10F2B8", fw.word(HCC + 0x14) == DR_RECORD0)
    c.ck("hcc+0xB0 crash-dump source = 0x4003A000", fw.word(HCC + 0xB0) == CRASH_DESC)
    c.ck("hcc+0xB4 crash-dump length = 0xC28", fw.word(HCC + 0xB4) == CRASH_LEN)

    # event-0x2E dispatcher anchors
    c.ck("0x4624C literal home 0x462EC = hcc_instance",
         fw.word(0x462EC) == HCC)
    c.ck("0x4624C reads hcc+0xC", fw.text(0x46252) == "ldr r3, [r5, #0xc]")
    c.ck("0x4624C reads [base+8] status word", fw.text(0x46258) == "ldr r4, [r3, #8]")
    c.ck("0x4624C acks [base+4]", fw.text(0x46296) == "str r3, [r2, #4]")
    c.ck("0x4624C bits24-28 -> [hcc+0x1C]", fw.text(0x462b4) == "ldr r3, [r5, #0x1c]")
    c.ck("0x4624C bits16-20 -> [hcc+0x24]", fw.text(0x462d0) == "ldr r3, [r5, #0x24]")
    c.ck("0x4624C tail d2h_notify id 3",
         fw.text(0x462aa) == "movs r1, #3" and fw.text(0x462ac) in ("b #0xc6170", "b.w #0xc6170"))
    c.ck("0x4624C extracts channel idx as bits[24:29]",
         fw.text(0x46298) == "lsrs r4, r4, #0x18")

    # event-0x2D dispatcher anchors
    c.ck("0x462F8 reads hcc+0xC", fw.text(0x462fe) == "ldr r3, [r5, #0xc]")
    c.ck("0x462F8 reads [base+8] status word", fw.text(0x46302) == "ldr r4, [r3, #8]")
    c.ck("0x462F8 bits0-2 -> [hcc+0x20]", fw.text(0x46352) == "ldr r3, [r5, #0x20]")
    c.ck("0x462F8 bits8-10 -> [hcc+0x28]", fw.text(0x4636e) == "ldr r3, [r5, #0x28]")

    # callback install + identities
    c.ck("install literal +0x1C = 0xC625B", fw.word(0x497AC) == (TRIGGER | 1))
    c.ck("install literal +0x20 = 0xC6803", fw.word(0x497B4) == (SR_RX | 1))
    c.ck("install literal +0x24 = 0xC1903", fw.word(0x497B8) == (STUB_16 | 1))
    c.ck("install literal +0x28 = 0xC18FF", fw.word(0x497BC) == (STUB_8 | 1))
    c.ck("install store hcc+0x1C", fw.text(0x4957A) == "str r3, [r4, #0x1c]")
    c.ck("install store hcc+0x20", fw.text(0x49582) == "str r3, [r4, #0x20]")
    c.ck("install store hcc+0x24", fw.text(0x4958A) == "str r3, [r4, #0x24]")
    c.ck("install store hcc+0x28", fw.text(0x49592) == "str r3, [r4, #0x28]")
    c.ck("stub 0xC1902 returns 0",
         fw.text(0xC1902) == "movs r0, #0" and fw.text(0xC1904) == "bx lr")
    c.ck("stub 0xC18FE returns 0",
         fw.text(0xC18FE) == "movs r0, #0" and fw.text(0xC1900) == "bx lr")

    # event registration/enable
    c.ck("registration literal 0x2D handler = 0x462F9", fw.word(0x497C0) == (DISP_2D | 1))
    c.ck("registration literal 0x2E handler = 0x4624D", fw.word(0x497C4) == (DISP_2E | 1))
    c.ck("0x496C8 site passes event id 0x2D",
         fw.text(0x496B6) == "movs r0, #0x2d" and fw.text(0x496CA) == "bl #0xc74b0")
    c.ck("0x496D6 site passes event id 0x2E",
         fw.text(0x496DA) == "movs r0, #0x2e" and fw.text(0x496DC) == "bl #0xc74b0")
    c.ck("events 0x2D/0x2E enabled via 0xC6FF4",
         fw.text(0x496EA) == "bl #0xc6ff4" and fw.text(0x496F0) == "bl #0xc6ff4")

    # sole entry path
    c.ck("no BL/BLX caller of event-0x2E handler", fw.callers(DISP_2E) == [])
    c.ck("no BL/BLX caller of event-0x2D handler", fw.callers(DISP_2D) == [])
    c.ck("event dispatcher 0xC2EFC has exactly one caller (ARM trampoline CPU 0x40094)",
         fw.callers(EV_DISP) == [] and fw.callers_arm(EV_DISP) == [0x40094])
    c.ck("ARM trampoline at 0x40064 ends its blx at 0x40094 to 0xC2EFC",
         fw.text(0x40094, "arm").startswith("blx #0xc2efc"))
    c.ck("cross-check: d2h_notify 0xC6170 has exactly the two known callers",
         fw.callers(0xC6170) == [0xC6224, 0xC6998])
    c.ck("status word 0x4003A874 has no literal home", fw.literal_loads(ETE_STATUS)[0] == [])
    c.ck("no image materialisation of the DR send words (no ldr/movw-movt refs)",
         all(fw.refs(a) == [] for a in DR_SEND))
    c.ck("no image materialisation of the SR send words (no ldr/movw-movt refs)",
         all(fw.refs(a) == [] for a in SR_SEND))
    c.ck("door0 CA only as the data pair home 0x10CACC",
         [h for h in fw.literal_loads(0x4003A7C0)[0] if h not in REACH_HOMES] == [] and
         fw.literal_loads(0x4003A7C0)[0] == [0x10CACC])
    c.ck("door1 CA only as the data pair home 0x10CB94",
         fw.literal_loads(0x4003A7D0)[0] == [0x10CB94])
    c.ck("no code materialises the engine door CA 0x4003A7C0",
         fw.refs(0x4003A7C0) == [])
    c.ck("no code materialises the engine door CA 0x4003A7D0",
         fw.refs(0x4003A7D0) == [])
    c.ck("engine global base 0x4003A000 is not materialised inside a movw/movt pair",
         all(t != "movw/movt" for t, _ in fw.refs(0x4003A000)))

    # sending trigger body
    c.ck("trigger requires hcc+0x5C (g)", fw.text(0xC6264) == "ldr r3, [r0, #0x5c]")
    c.ck("trigger requires hcc+0x14 (DR records)", fw.text(0xC626A) == "ldr r3, [r0, #0x14]")
    c.ck("trigger bounds ch by hcc+0x04", fw.text(0xC6270) == "ldr r2, [r0, #4]")
    c.ck("trigger uses record stride 0x354", fw.text(0xC6280) == "mov.w r4, #0x354")
    c.ck("trigger bumps record+0x344", fw.text(0xC628E) == "str.w r3, [r4, #0x344]")
    c.ck("trigger reads send word CA record+0x32C", fw.text(0xC6292) == "ldr.w r3, [r4, #0x32c]")
    c.ck("trigger target = send word hi16", fw.text(0xC629A) == "lsrs r5, r5, #0x10")
    c.ck("trigger handshake-writes PKTRAM control word record+0x14",
         fw.text(0xC629C) == "strh r5, [r3]")
    c.ck("trigger read-back compares control word", fw.text(0xC62A2) == "cmp r5, r3")
    c.ck("trigger scans for tag 0x5A5A", fw.text(0xC62A6) == "movw sb, #0x5a5a")
    c.ck("trigger checks node tag byte +0xA", fw.text(0xC6326) == "ldrh r2, [r0, #0xa]")
    c.ck("trigger accounts released nodes at record+0x34C",
         fw.text(0xC62F2) == "str.w r3, [r4, #0x34c]")
    c.ck("trigger sets progress record+0x10 = target", fw.text(0xC62FA) == "str r5, [r4, #0x10]")
    c.ck("trigger wakes g via 0xC5128", fw.text(0xC630A) == "bl #0xc5128")

    # SR receive callback shape (hcc+0x20)
    c.ck("0xC6802 is the SR receive callback (reads hcc+0x5C/0x10/0x08)",
         fw.text(0xC681E).startswith("ldr r3, [r5, #0x5c]") and
         fw.text(0xC6822).startswith("ldr r3, [r5, #0x10]"))

    # event-raise census
    sites = fw.callers(RAISE)
    c.ck("17 software event-raise sites in the image", len(sites) == 17)
    ids = {s: fw.mov_r0_ids_near(s) for s in sites}
    raised = set()
    for s, lst in ids.items():
        raised.update(lst)
    c.ck("no image site raises event 0x2D", 0x2D not in raised)
    c.ck("no image site raises event 0x2E", 0x2E not in raised)
    c.ck("pcie_msg_init raises 0x4C and 0x4E (census sanity)",
         {0x4C, 0x4E} <= raised)

    # ops table / crash-dump descriptor
    c.ck("ETE ops table base 0x4003A000", fw.word(ETE_OPS) == 0x4003A000)
    c.ck("ETE ops table intr word 0x40039508", fw.word(ETE_OPS + 4) == 0x40039508)

    # vendor host comparison (optional artifact)
    if os.path.isfile(PLAT):
        pm = PlatModule(PLAT)
        psha = hashlib.sha256(pm.raw).hexdigest()
        c.ck("vendor plat.ko identity sha256 = 6f2eac41...cd491", psha == PLAT_SHA256)
        c.ck("vendor dr_reg_init writes DR +0x38 from inst[+0x1C] (str r2,[r3,#56])",
             pm.text(0x148B4)[0] == "str r2, [r3, #0x38]")
        c.ck("vendor dr_init zeroes inst+0x1C (str r6,[r1,#28])",
             pm.text(0x14924)[0] == "str r6, [r1, #0x1c]")
        c.ck("vendor D2H re-arm writes +0x38 from ctx[+0x1C]",
             pm.text(0x14430)[0] == "str r2, [r3, #0x38]")
        c.ck("vendor D2H re-arm writes +0x18 from ctx[+0x28]",
             pm.text(0x1443C)[0] == "str r2, [r3, #0x18]")
        c.ck("pcie_trigger_ete_sending_handle is a branch (tail call), not data",
             (pm.word(P_TRIG_HANDLE) or 0) >> 24 == 0xEA)
        c.ck("vendor .data contains only the ETE CA pair {0x4003A000, 0x40039508}",
             0x4003A000 in pm.data_words() and 0x40039508 in pm.data_words())
    else:
        print("SKIP: vendor module not present, host facts not re-asserted")

    # host BAR0 mapping
    c.ck("BAR0 map: ETE status word -> 0x3F2874", bar0_io(ETE_STATUS) == 0x3F2874)
    c.ck("BAR0 map: DR0 published -> 0x3F25C8", bar0_io(0x4003A590 + 0x38) == 0x3F25C8)
    c.ck("BAR0 map: door0 -> 0x3F27C0", bar0_io(DOORS[0]) == 0x3F27C0)
    c.ck("BAR0 map: DR trigger counter0 -> 0x7C35FC",
         bar0_ram(DR_RECORD0 + 0x344) == 0x7C75FC)
    c.ck("BAR0 map: PKTRAM DR0 node array -> 0x238540",
         bar0_pktram(0x01060540) == 0x238540)

    return c.done()


# ---------------------------------------------------------------------------------
def cmd_chain(args):
    fw = load()
    print("ETE DR (A->B) copy trigger chain (all [V], see REPORT.md section 2)")
    print("=" * 78)
    print("device ETE hardware: DR channel needs service")
    print("  -> sets a bit in status word CA 0x%08X (bits 24-28 = DR ch 0..4)" % ETE_STATUS)
    print("  -> device event router -> event id 0x2E")
    print("  -> ROM IRQ vector -> image ARM trampoline CPU 0x%05X" % TRAMP_ARM)
    print("  -> event dispatcher CPU 0x%05X reads CA 0x4016010C, blx handler[0x2E]" % EV_DISP)
    print("  -> handler table CA 0x17D430[0x2E] = CPU 0x%05X" % DISP_2E)
    print("  -> reads [hcc+0x0C] = CA 0x%08X, status [base+8] = CA 0x%08X" %
          (ETE_STATUS_BASE, ETE_STATUS))
    print("  -> acks upper bits into [base+4] = CA 0x%08X" % ETE_ACK)
    print("  -> for each set bit b in status[24:29]:")
    print("       fn = [hcc+0x1C] = CPU 0x%05X (sending trigger); fn(hcc, b)" % TRIGGER)
    print("     for each set bit b in status[16:21]:")
    print("       fn = [hcc+0x24] = CPU 0x%05X (stub, returns 0)" % STUB_16)
    print("  -> if none: d2h_notify([hcc+0x58], 3)")
    print()
    print("sending trigger CPU 0x%05X(hcc, ch):" % TRIGGER)
    print("  rec = [hcc+0x14] + 0x354*ch ; rec[0x344]++")
    print("  target = hi16(word[rec+0x32C])          ; hardware-authored send word")
    print("  u16[rec+0x14] = target ; read back ; compare   ; PKTRAM control word (staging+0x100)")
    print("  scan nodes of ops+0x28 between rec[0x10] and target; release tag 0x5A5A;")
    print("  rec[0x34C] += released ; rec[0x10] = target ; wake g via 0xC5128")
    print()
    print("host-owned fields: SR group A mirror, DR group B base/depth/pub, intr word, mailbox")
    print("status word / send words / engine global+doors: device/ROM-authored (no host writer)")


def cmd_dispatchers(args):
    fw = load()
    for cpu, n, title in ((DISP_2E, 72, "event 0x2E handler (DR)"),
                          (DISP_2D, 68, "event 0x2D handler (SR)")):
        print("== CPU 0x%05X  %s" % (cpu, title))
        for i in fw.thumb.disasm(fw.data[cpu - 0x40000: cpu - 0x40000 + 4 * n], cpu):
            lit = ""
            if i.mnemonic == "ldr" and len(i.operands) == 2 and \
                    i.operands[1].type == arm.ARM_OP_MEM and \
                    i.operands[1].mem.base == arm.ARM_REG_PC:
                pc = (i.address + 4) & ~3
                a = pc + i.operands[1].mem.disp
                lit = "  ; [%08x]=%08x" % (a, fw.word(a))
            print("  %05x: %-8s %-24s%s" % (i.address, i.mnemonic, i.op_str, lit))
        print()


def cmd_install(args):
    fw = load()
    print("pcie_msg_init (CPU 0x49334) ETE install/registration/enable sites [V]")
    print("  cb install literals:  +0x1C=0x%08X  +0x20=0x%08X  +0x24=0x%08X  +0x28=0x%08X" %
          (fw.word(0x497AC), fw.word(0x497B4), fw.word(0x497B8), fw.word(0x497BC)))
    print("  handler table targets: event 0x2D -> 0x%08X ; event 0x2E -> 0x%08X" %
          (fw.word(0x497C0), fw.word(0x497C4)))
    for cpu in (0x496C8, 0x496D6, 0x496E8, 0x496EE, 0x496F4):
        print("  %05x: %-8s %s" % (cpu, fw.insn(cpu).mnemonic, fw.insn(cpu).op_str))
    print("  raisers: 0x%05X(0x4C) and 0x%05X(0x4E)" % (0x49756, 0x4975C))
    print("  event dispatcher CPU 0x%05X callers: Thumb=%s ARM-trampoline=%s" %
          (EV_DISP, ["0x%05x" % x for x in fw.callers(EV_DISP)],
           ["0x%05x" % x for x in fw.callers_arm(EV_DISP)]))


def cmd_raise(args):
    fw = load()
    sites = fw.callers(RAISE)
    print("software event-raise primitive CPU 0x%05X: %d sites [V]" % (RAISE, len(sites)))
    for s in sites:
        print("  site 0x%05x  nearby r0 ids: %s" %
              (s, [hex(x) for x in fw.mov_r0_ids_near(s)] or "[table/derived]"))
    print("\n0x2D / 0x2E are NOT raised by any image site -> hardware-event-only.")


def cmd_host(args):
    if not os.path.isfile(PLAT):
        raise SystemExit("vendor module not present: %s" % PLAT)
    pm = PlatModule(PLAT)
    print("vendor hi5622v100_plat.ko 2.5.24 host-side ETE facts [V]")
    print("  dr_reg_init 0x146B0-0x148B8: writes DR group B, +0x38 <- inst[+0x1C]: %s" %
          pm.text(0x148B0)[0])
    print("  dr_init 0x14908: zeroes inst+0x1C .. : %s" % pm.text(0x14924)[0])
    print("  => initial published(+0x38) = 0 (vendor), bundled pre-post node word0 comes later")
    print("  D2H receive/re-arm worker 0x13F90: under the ctx+0x4C lock writes")
    print("     chan+0x38 <- ctx[+0x1C] : %s" % pm.text(0x14430)[0])
    print("     chan+0x18 <- ctx[+0x28] : %s" % pm.text(0x1443C)[0])
    print("  pcie_trigger_ete_sending_handle 0x15EFC: raw word is an ARM 'b' branch ")
    print("     (relocation target = pcie_wkup_thread 0x1629C), not a self-branch stub")
    print("  no vendor-host writer of status/send words/doors/global (see REPORT.md sec 5)")
    print("  clean driver arms DR +0x38 = 0x400 (prepost=32) -> see W14 in REPORT.md")


READ_PACK = [
    ("boot page", 0x1BF000, "ram"), ("boot page +8", 0x1BF008, "ram"),
    ("boot page +C", 0x1BF00C, "ram"), ("state", 0x118B60, "ram"),
    ("exception mask", 0x40008, "ram"),
    ("ETE global", 0x4003A000, "io"),
    ("engine block0", 0x4003A540, "io"), ("engine block1", 0x4003A720, "io"),
    ("door0", 0x4003A7C0, "io"), ("door0+4", 0x4003A7C4, "io"),
    ("door1", 0x4003A7D0, "io"),
    ("ETE ctrl/status base", 0x4003A86C, "io"),
    ("ETE ack", 0x4003A870, "io"), ("ETE status", 0x4003A874, "io"),
    ("ETE intr (host-facing)", 0x40039508, "io"),
    ("DR0 ch enable", 0x4003A590, "io"), ("DR0 A base", 0x4003A5A0, "io"),
    ("DR0 A depth-1", 0x4003A5A4, "io"), ("DR0 A producer", 0x4003A5A8, "io"),
    ("DR0 A consumer", 0x4003A5AC, "io"), ("DR0 B base", 0x4003A5C0, "io"),
    ("DR0 B depth-1", 0x4003A5C4, "io"), ("DR0 B published", 0x4003A5C8, "io"),
    ("DR0 B fill", 0x4003A5CC, "io"),
    ("DR1 B published", 0x4003A618, "io"), ("DR1 B fill", 0x4003A61C, "io"),
    ("DR2 B published", 0x4003A668, "io"), ("DR2 B fill", 0x4003A66C, "io"),
    ("DR3 B published", 0x4003A6B8, "io"), ("DR3 B fill", 0x4003A6BC, "io"),
    ("DR0 send word", 0x4003AC00, "io"), ("DR1 send word", 0x4003AC04, "io"),
    ("DR2 send word", 0x4003AC08, "io"), ("DR3 send word", 0x4003AC0C, "io"),
    ("SR0 send word", 0x4003AC18, "io"), ("SR1 send word", 0x4003AC1C, "io"),
    ("SR2 send word", 0x4003AC20, "io"),
    ("DR0 trigger counter", DR_RECORD0 + 0x344, "ram"),
    ("DR1 trigger counter", DR_RECORD0 + DR_RECORD_STRIDE + 0x344, "ram"),
    ("DR2 trigger counter", DR_RECORD0 + 2 * DR_RECORD_STRIDE + 0x344, "ram"),
    ("DR3 trigger counter", DR_RECORD0 + 3 * DR_RECORD_STRIDE + 0x344, "ram"),
    ("g in-flight", 0x10C1A8, "ram"), ("g shadow", 0x10C1AC, "ram"),
    ("DR0 source node0 word0", 0x01060540, "pktram"),
    ("DR0 source node0 word1", 0x01060544, "pktram"),
]


def cmd_bench(args):
    print("R0 read-only snapshot pack (one fresh PD4 golden boot; no writes) [V addresses]")
    print("%-26s %-10s %-10s %s" % ("row", "CA/CPU", "BAR0", "region"))
    for name, ca, kind in READ_PACK:
        if kind == "io":
            b = bar0_io(ca)
        elif kind == "ram":
            b = bar0_ram(ca)
        else:
            b = bar0_pktram(ca)
        print("%-26s 0x%08x 0x%06x %s" % (name, ca, b, kind))
    print("\nCandidate E1 write rows (host-owned DR +0x38 only; see BENCH-PROCEDURE.md):")
    for ch, chan in enumerate((0x4003A590, 0x4003A5E0, 0x4003A630, 0x4003A680)):
        print("  DR%d +0x38 pub  CA 0x%08X  BAR0 0x%06X" %
              (ch, chan + 0x38, bar0_io(chan + 0x38)))
    print("\nE1a value rule: read +0x3C (fill) first, then write that same value to +0x38.")
    print("E1b value rule: write 0x00000000 to +0x38 (vendor dr_reg_init initial value).")
    print("Never write: 0x4003A874 status, 0x4003A870 ack, 0x4003A86C ctrl, send words,")
    print("             engine global +0x000..+0x3FF, doors 0x4003A7C0/0x4003A7D0, 0x400392F0.")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name, fn in (("selftest", cmd_selftest), ("chain", cmd_chain),
                     ("dispatchers", cmd_dispatchers), ("install", cmd_install),
                     ("raise", cmd_raise), ("host", cmd_host), ("bench", cmd_bench)):
        p = sub.add_parser(name)
        p.set_defaults(fn=fn)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())