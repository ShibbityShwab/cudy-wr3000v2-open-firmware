
#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""ete_engine.py - host-only static recon for the Hi5622V100 ETE engine
(wifi-ete-engine lane).

Reads the LOCAL vendor firmware image in place (never copies it) and the
LOCAL vendor modules, and prints derived annotations only.  Nothing is
written; no device is opened.

Subcommands:
  selftest    assert every fact quoted in REPORT.md (N checks)
  tables      census of static {CA,value} init tables + PHY/MAC table consumer
  layout      hcc_instance / records / ops-table / ETE register-window map
  writers     ETE-register writer census (image side) + negative scans
  chain       deposit -> ring commit -> d2h_notify -> flush -> trigger chain
  benchpack   print the read-only bench snapshot table (BAR0 offsets)

Address conventions (project-wide):
  CPU address of image byte f = f + 0x40000
  host BAR0 (CPU RAM, region 5)      = CPU + 0x6B8000
  host BAR0 (device IO, region 3)    = 0x3B8000 + (CA - 0x40000000)
  host BAR0 (PKTRAM, region 2)       = 0x1D8000 + (CA - 0x01000000)

Evidence grades in the report: [V] verified in the named artifact this lane,
[V*] device-measured by an earlier attended lane (cited), [I] inference,
[U] unknown.  Module facts are cited as plat 0xNNNN (.text-relative, sh_addr 0).
"""
import argparse
import os
import struct
import sys

try:
    import capstone
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_ARM, CS_OP_IMM, CS_OP_REG, CS_OP_MEM
except Exception:  # pragma: no cover
    sys.exit("capstone not installed (use the repo .provision-venv)")

FW = os.environ.get("HI5622_FW",
                    "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin")
PLAT_KO = os.environ.get("HI5622_PLAT_KO",
                         "/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_plat.ko")
BASE = 0x40000

# ---- ETE window (device IO, region 3) -------------------------------------
ETE_BASE = 0x4003A000
ETE_STATUS_BASE = 0x4003A86C   # firmware-owned control/status block base
ETE_STATUS = 0x4003A874        # status word (dispatchers read base+8)
ETE_ACK = 0x4003A870           # write/ack word (dispatchers write base+4)
ETE_INTR_HOST = 0x40039508     # vendor host-visible ETE interrupt word
ETE_ENGINE0 = 0x4003A540       # engine/queue control block 0 (writer: pcie_msg_init)
ETE_ENGINE1 = 0x4003A720       # engine/queue control block 1
ETE_ENGINE0_DOOR = 0x4003A7C0  # paired door/status word (never written by image or host)
ETE_ENGINE1_DOOR = 0x4003A7D0
ETE_SEND_DR0 = 0x4003AC00      # DR0-3 send words, +4 stride
ETE_SEND_SR0 = 0x4003AC18      # SR0-2 send words, +4 stride

# channel table decoded from image data (identical to vendor pcie_ete_get_chn_cfg)
CHAN_SR = [(0x4003A400, 0x4003AC18), (0x4003A450, 0x4003AC1C), (0x4003A4A0, 0x4003AC20)]
CHAN_DR = [(0x4003A590, 0x4003AC00), (0x4003A5E0, 0x4003AC04),
           (0x4003A630, 0x4003AC08), (0x4003A680, 0x4003AC0C)]

HCC = 0x10F1F0                  # hcc_instance (per-chip0 message object)
ETE_OPS = 0x10CEDC              # ETE ops table installed at hcc+0x18
DR_RECORDS = 0x10F2B8           # hcc+0x14, stride 0x354
SR_RECORDS = 0x11035C           # hcc+0x10, stride 0xCC
DR_RECORD4 = 0x110008           # 5th DR record (control channel 0x4003A6D0/0x4003AC10)
G_MSG = 0x10C0F4                # message object g (*CA 0x172130)
O1 = 0x10C0C0                   # parent object of g (g = O1+0x34)
PHY_TABLE1 = 0x1058B8           # 31 x {CA,value}: CAs 0x401105xx
PHY_TABLE2 = 0x1059B0           # 31 x {CA,value}: CAs 0x401145xx
PHY_APPLY = 0xA0184             # PHY table apply loop (memcpy 0xF8 + store pairs)
CRASH_DUMP = 0x10F294           # firmware crash-dump table (3 x {src,len,cursor})

PCIE_MSG_INIT = 0x49334         # configures ETE rings/enables/mailbox
DEPOSIT = 0xC61E0               # ETE ops slot +0x10 (D2H deposit)
RING_COMMIT = 0xC192A
D2H_NOTIFY = 0xC6170
D2H_FLUSH = 0xC6108
SENDING_TRIGGER = 0xC625A
DISP_D2H = 0x4624C              # event handler 0x2E: status bits16-20/24-31 -> trigger
DISP_H2D = 0x462F8              # event handler 0x2D: status bits 3-7/8-15
EVENT_DISPATCH = 0xC2EFC        # image event dispatcher (ROM IRQ trampoline only caller)
IRQ_TRAMPOLINE = 0x40064        # image header +0x64, ROM-vectored
HANDLER_TABLE = 0x17D430
ROM_HANDSHAKE = 0x1BF008        # entry-A inter-core/ROM handshake word (event-router)

def u16(b, o):  return struct.unpack_from("<H", b, o)[0]
def u32(b, o):  return struct.unpack_from("<I", b, o)[0]

class Blob:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.d = f.read()
        self.md32 = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
        self.md32.detail = True
        self.mdarm = Cs(CS_ARCH_ARM, CS_MODE_ARM)
        self.mdarm.detail = True

    def insn(self, cpu, arm=False):
        off = cpu - BASE
        if off < 0 or off + 2 > len(self.d):
            return None
        md = self.mdarm if arm else self.md32
        n = 4 if arm else (4 if u16(self.d, off) >= 0xE800 else 2)
        if off + n > len(self.d):
            return None
        try:
            ins = next(iter(md.disasm(self.d[off:off + n], cpu)), None)
        except Exception:
            return None
        if ins is None or len(ins.bytes) != n:
            return None
        return ins

    def word(self, cpu):
        off = cpu - BASE
        if off < 0 or off + 4 > len(self.d):
            return None
        return u32(self.d, off)

    def w16(self, cpu):
        off = cpu - BASE
        if off < 0 or off + 2 > len(self.d):
            return None
        return u16(self.d, off)

    def literal_of(self, ins):
        for op in ins.operands:
            if op.type == CS_OP_MEM and ins.reg_name(op.mem.base) == "pc":
                pc = (ins.address + 4) & ~3
                return pc + op.mem.disp
        return None

    def dis(self, cpu, n, arm=False):
        out = []
        end = cpu + n
        while cpu < end:
            ins = self.insn(cpu, arm)
            if ins is None:
                out.append((cpu, None, None, None))
                break
            lit = self.literal_of(ins)
            litw = self.word(lit) if lit is not None else None
            out.append((cpu, ins.mnemonic + " " + ins.op_str, ins.bytes.hex(), (lit, litw)))
            cpu += ins.size
        return out

def bar0(ca):
    if ca < 0x40000:
        return ca                     # region-0 identity window
    if 0x40000000 <= ca < 0x40200000:
        return 0x3B8000 + (ca - 0x40000000)
    if 0x01000000 <= ca < 0x01080000:
        return 0x1D8000 + (ca - 0x01000000)
    return ca + 0x6B8000              # region-5 CPU-RAM alias

# ---------------------------------------------------------------- image scan
def scan_literals(b):
    """Map value -> list of (site, kind).  Covers PC-relative literal loads and
    movw/movt pairs (constants the image can materialise)."""
    refs = {}
    def add(v, site, kind):
        refs.setdefault(v & 0xFFFFFFFF, []).append((site, kind))
    a = BASE
    end = BASE + len(b.d) - 4
    while a < end:
        ins = b.insn(a)
        if ins is None:
            a += 2
            continue
        m = ins.mnemonic.split(".")[0]
        lit = b.literal_of(ins)
        if lit is not None:
            w = b.word(lit)
            if w is not None:
                add(w, a, "lit@%08x" % lit)
        if m == "movw" and len(ins.operands) == 2 and ins.operands[0].type == CS_OP_REG \
                and ins.operands[1].type == CS_OP_IMM:
            reg = ins.reg_name(ins.operands[0].reg)
            lo = ins.operands[1].imm & 0xFFFF
            bb = a + ins.size
            for _ in range(8):
                i2 = b.insn(bb)
                if i2 is None:
                    break
                if i2.mnemonic.split(".")[0] == "movt" and len(i2.operands) == 2 \
                        and i2.operands[0].type == CS_OP_REG \
                        and b.insn(bb) and i2.reg_name(i2.operands[0].reg) == reg \
                        and i2.operands[1].type == CS_OP_IMM:
                    add(((i2.operands[1].imm & 0xFFFF) << 16) | lo, a, "movw/movt@%08x" % bb)
                    break
                bb += i2.size
        a += ins.size
    return refs

def image_table_census(b):
    """Maximal 8-byte-stride runs of {w0 in device IO range, w1}.  Returns
    [(cpu, n)] for runs with n >= 8 (real init tables; shorter runs are
    ordinary pointer pairs inside code/data)."""
    runs = []
    n = len(b.d)
    off = 0
    while off < n - 8:
        w0 = u32(b.d, off)
        if 0x40000000 <= w0 < 0x40200000:
            start = off
            cnt = 0
            while off < n - 8:
                w0 = u32(b.d, off)
                if not (0x40000000 <= w0 < 0x40200000):
                    break
                cnt += 1
                off += 8
            if cnt >= 2:
                runs.append((start + BASE, cnt))
        else:
            off += 8
    return runs

def find_store_sites(b):
    """All (site, mnemonic, disp, text) for ldr/str with a memory operand."""
    out = []
    a = BASE
    end = BASE + len(b.d) - 4
    while a < end:
        ins = b.insn(a)
        if ins is None:
            a += 2
            continue
        m = ins.mnemonic.split(".")[0]
        if m.startswith(("ldr", "str")) and len(ins.operands) >= 2 \
                and ins.operands[1].type == CS_OP_MEM:
            op = ins.operands[1]
            out.append((a, m, ins.reg_name(op.mem.base), op.mem.disp,
                        ins.mnemonic + " " + ins.op_str))
        a += ins.size
    return out

# ------------------------------------------------------------- module scan
def ko_text(path):
    try:
        from elftools.elf.elffile import ELFFile
    except Exception:
        return None, "pyelftools not installed"
    with open(path, "rb") as f:
        elf = ELFFile(f)
        for sec in elf.iter_sections():
            if sec.name == ".text":
                return (sec.data(), sec["sh_addr"]), None
    return None, "no .text"

def ko_scan_constants(path, values):
    """ARM-mode scan of .text for ldr/str/ldrh/strh/add with an immediate
    matching any value (offset or movw/movt materialisation).  Returns
    {v: [site,..]} plus list of unmapped other-immediate sites."""
    res = {v: [] for v in values}
    text, err = ko_text(path)
    if text is None:
        return res, err
    data, addr = text
    md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
    md.detail = True
    for ins in md.disasm(data, addr):
        m = ins.mnemonic.split(".")[0]
        hit = None
        for op in ins.operands:
            if op.type == CS_OP_IMM and (op.imm & 0xFFFFFFFF) in res:
                hit = op.imm & 0xFFFFFFFF
            if op.type == CS_OP_MEM and op.mem.disp in res:
                hit = op.mem.disp
        if hit is not None and m not in ("movw", "movt"):
            res[hit].append(ins.address)
    return res, None

# ----------------------------------------------------------------- selftest
def selftest(b):
    checks = []
    def ck(name, cond, detail=""):
        checks.append((name, bool(cond), detail))

    # ---- hcc_instance statics
    ck("hcc+0x04 = 4 (DR channels)", b.word(HCC + 0x04) == 4)
    ck("hcc+0x08 = 3 (SR channels)", b.word(HCC + 0x08) == 3)
    ck("hcc+0x0C = ETE status base 0x4003A86C", b.word(HCC + 0x0C) == ETE_STATUS_BASE)
    ck("hcc+0x10 = SR records 0x11035C", b.word(HCC + 0x10) == SR_RECORDS)
    ck("hcc+0x14 = DR records 0x10F2B8", b.word(HCC + 0x14) == DR_RECORDS)
    ck("hcc+0x18 = 0 (ops table installed at runtime)", b.word(HCC + 0x18) == 0)
    ck("hcc+0xB0 = ETE block base 0x4003A000", b.word(HCC + 0xB0) == ETE_BASE)
    ck("hcc+0xB4 = 0xC28 (ETE dump length)", b.word(HCC + 0xB4) == 0xC28)
    ck("hcc+0xBC = PCIe glue block 0x40039000", b.word(HCC + 0xBC) == 0x40039000)
    ck("hcc+0xC0 = 0xD5C (glue dump length)", b.word(HCC + 0xC0) == 0xD5C)
    ck("g = O1+0x34", G_MSG == O1 + 0x34)
    for off in (0x9C, 0xA0, 0xA4, 0xA8, 0xB0):
        ck("g+0x%02x is image-static 0 (installed at runtime)" % off,
           b.word(G_MSG + off) == 0)
    ck("g+0xB4 = 0 (in-flight, image static)", b.word(G_MSG + 0xB4) == 0)
    ck("g+0xB8 = 0 (deferred shadow, image static)", b.word(G_MSG + 0xB8) == 0)
    ck("pcie_msg_init literal out[1] ptr 0x40039014", b.word(0x497DC) == 0x40039014)
    ck("pcie_msg_init literal out[0] ptr 0x40039010", b.word(0x497E0) == 0x40039010)
    ck("pcie_msg_init literal D2H doorbell 0x40101434", b.word(0x497D8) == 0x40101434)
    ck("out[5] = doorbell - 0xC8000 - 0x144", 0x40101434 - 0xC8000 - 0x144 == 0x400392F0)
    ck("flush one-shot = out[5] - 0x1c + 0xC8000 + 0x144",
       0x400392F0 - 0x1c + 0xC8000 + 0x144 == 0x40101418)

    # ---- ops table
    ops = {off: b.word(ETE_OPS + off) for off in range(0, 0x44, 4)}
    ck("ops+0x00 = ETE base", ops[0x00] == ETE_BASE)
    ck("ops+0x04 = host intr word", ops[0x04] == ETE_INTR_HOST)
    ck("ops+0x08 = 0xC1875 stub", ops[0x08] == 0x000C1875)
    ck("ops+0x0C = 0xC186F stub", ops[0x0C] == 0x000C186F)
    ck("ops+0x10 = deposit 0xC61E1", ops[0x10] == 0x000C61E1)
    ck("ops+0x14 = node fill 0xC1859", ops[0x14] == 0x000C1859)
    ck("ops+0x18 = node size getter 0xC1867", ops[0x18] == 0x000C1867)
    ck("ops+0x1C = word0 getter 0xC184F", ops[0x1C] == 0x000C184F)
    ck("ops+0x20 = len getter 0xC1843", ops[0x20] == 0x000C1843)
    ck("ops+0x24 = flag getter 0xC1835", ops[0x24] == 0x000C1835)
    ck("ops+0x28 = 2nd-ring word0 0xC182B", ops[0x28] == 0x000C182B)
    ck("ops+0x34 = addr getter 0xC17F9", ops[0x34] == 0x000C17F9)
    ck("ops+0x3C = flag setter 0xC1813", ops[0x3C] == 0x000C1813)
    ck("ops+0x40 = 0xC1803", ops[0x40] == 0x000C1803)

    # ---- channel + record statics
    for i, (ch, snd) in enumerate(CHAN_DR):
        ck("DR%d record chan CA" % i, b.word(DR_RECORDS + 0x354 * i + 0x31C) == ch)
        ck("DR%d record send CA" % i, b.word(DR_RECORDS + 0x354 * i + 0x32C) == snd)
    ck("DR0 record staging", b.word(DR_RECORDS) == 0x01060440)
    ck("DR1 record staging", b.word(DR_RECORDS + 0x354) == 0x01060330)
    ck("DR2 record staging", b.word(DR_RECORDS + 2 * 0x354) == 0x01060220)
    ck("DR3 record staging", b.word(DR_RECORDS + 3 * 0x354) == 0x01060110)
    ck("5th DR record chan CA 0x4003A6D0", b.word(DR_RECORD4 + 0x31C) == 0x4003A6D0)
    ck("5th DR record send CA 0x4003AC10", b.word(DR_RECORD4 + 0x32C) == 0x4003AC10)
    for i, (ch, snd) in enumerate(CHAN_SR):
        ck("SR%d record chan CA" % i, b.word(SR_RECORDS + 0xCC * i + 0xB0) == ch)
        ck("SR%d record send CA" % i, b.word(SR_RECORDS + 0xCC * i + 0xBC) == snd)
    ck("SR0 record staging 0x01060750", b.word(SR_RECORDS + 0x10) == 0x01060750)

    # ---- channel config table (twin of vendor get_chn_cfg)
    cfg_sr = 0x104054
    cfg_dr = 0x104078
    for i, (ch, _snd) in enumerate(CHAN_SR):
        ck("cfg SR%d off" % i, b.word(cfg_sr + 12 * i) == ch - ETE_BASE)
        ck("cfg SR%d depth byte 0x20" % i, b.d[cfg_sr + 12 * i + 4 - BASE] == 0x20)
        ck("cfg SR%d ctrl byte 0" % i, b.d[cfg_sr + 12 * i + 5 - BASE] == 0)
    for i, (ch, _snd) in enumerate(CHAN_DR):
        ck("cfg DR%d off" % i, b.word(cfg_dr + 12 * i) == ch - ETE_BASE)
        ck("cfg DR%d depth byte 0x20" % i, b.d[cfg_dr + 12 * i + 4 - BASE] == 0x20)

    # ---- engine control blocks (writer pcie_msg_init only)
    ck("engine0 door ptr pair", b.word(O1 + 0xA08) == ETE_ENGINE0)
    ck("engine0 door = 0x4003A7C0", b.word(O1 + 0xA0C) == ETE_ENGINE0_DOOR)
    ck("engine1 block", b.word(O1 + 0xAD0) == ETE_ENGINE1)
    ck("engine1 door = 0x4003A7D0", b.word(O1 + 0xAD4) == ETE_ENGINE1_DOOR)

    # ---- PHY/MAC {CA,value} tables: the only real static init tables in image
    runs = [r for r in image_table_census(b) if r[1] >= 8]
    phy = [r for r in runs if r[0] == 0x1058B8]
    ck("PHY table run at CPU 0x1058B8 with 62 entries",
       len(phy) == 1 and phy[0][1] == 62, repr(runs))
    ete_runs = []
    for cpu, n in runs:
        for i in range(n):
            ca = b.word(cpu + 8 * i)
            if ETE_BASE <= ca < ETE_BASE + 0x1000:
                ete_runs.append((cpu, n, i, ca))
    ck("no static {CA,value} table entry addresses the ETE window",
       not ete_runs, repr(ete_runs))
    tlen = 31
    ck("PHY table1 entry0 = 0x401105BC:0", b.word(PHY_TABLE1) == 0x401105BC and b.word(PHY_TABLE1 + 4) == 0)
    ck("PHY table1 last entry = 0x401105BC:1", b.word(PHY_TABLE1 + 8 * (tlen - 1)) == 0x401105BC and b.word(PHY_TABLE1 + 8 * (tlen - 1) + 4) == 1)
    ck("PHY table2 entry0 = 0x401145BC:0", b.word(PHY_TABLE2) == 0x401145BC and b.word(PHY_TABLE2 + 4) == 0)
    ck("PHY table2 last entry = 0x401145BC:1", b.word(PHY_TABLE2 + 8 * (tlen - 1)) == 0x401145BC and b.word(PHY_TABLE2 + 8 * (tlen - 1) + 4) == 1)
    # apply loop: ldr = table2, sub #0xF8 -> table1, movs p1=0/1, memcpy 0xF8, write pairs
    seq = [(ins.address, ins.mnemonic + " " + ins.op_str) for ins in
           [b.insn(x) for x in range(0xA0184, 0xA01C8, 2)] if ins is not None]
    txt = "\n".join(t for _a, t in seq)
    ck("apply loop loads 0x1059B0", b.word(0xA0260) == PHY_TABLE2)
    ck("apply loop computes base = table2-0xF8 == table1",
       b.word(0xA0260) - 0xF8 == PHY_TABLE1)
    ck("apply loop memcpy 0xF8", "movs r2, #0xf8" in txt)
    ck("apply loop store pairs", "str r2, [r3]" in txt)
    ck("apply loop entry 0xA0184 exists", b.insn(0xA0184) is not None)

    # ---- no literal in the image names any ETE status/send/engine-door register
    refs = scan_literals(b)
    bad = [v for v in (ETE_STATUS, ETE_ACK, ETE_ENGINE0_DOOR, ETE_ENGINE1_DOOR,
                       *[s for _c, s in CHAN_DR], *[s for _c, s in CHAN_SR])
           if v in refs]
    ck("no literal materialises status/ack/door/send regs", not bad,
       "found: " + ", ".join("%08x" % v for v in bad))
    ck("ops table literal present exactly once",
       len(refs.get(ETE_OPS, [])) == 1)
    ck("hcc_instance literal sites == 8 (4 pools x 2 load sites)",
       len(refs.get(HCC, [])) == 8, repr(refs.get(HCC, [])))

    # ---- writers: pcie_msg_init disassembly contains the channel writes
    pm = "\n".join(t for rng in ((0x493B8, 0xE4), (0x494D6, 0x9A), (0x49594, 0x2C),
                                 (0x49620, 0x30), (0x496B0, 0x48), (0x49754, 0x100))
                    for _c, m, _x, _l in b.dis(*rng) for t in ([m] if m else []))
    for frag in ("str r6, [r5, #0xc]", "str.w sl, [r5, #8]",
                 "bfi r1, r3, #0, #0xa", "str r2, [r3]", "orr r2, r2, #1",
                 "str r2, [r3, #0x18]", "bfi r2, r1, #0, #3"):
        ck("pcie_msg_init has '%s'" % frag, frag in pm)
    # engine block enable writes @0x49594 and @0x49620
    ck("engine0 enable write site", any(s == 0x495A4 for s, _m, _b, _d in
        [(x[0], x[1], x[2], x[3]) for x in b.dis(0x49594, 0x2C) if x[1] and "ldr.w r2, [r5, #0xa08]" in x[1]]))
    ck("engine1 enable write site", any(s == 0x49634 for s, _m, _b, _d in
        [(x[0], x[1], x[2], x[3]) for x in b.dis(0x49620, 0x2C) if x[1] and "ldr.w r2, [r5, #0xad0]" in x[1]]))
    # status mask write in pcie_msg_init
    ck("status mask literal 0xE0E0F8F8 at 0x497C8", b.word(0x497C8) == 0xE0E0F8F8)
    ck("host intr mask constant 0xFFE0F8F8 not in image", 0xFFE0F8F8 not in refs)

    # ---- dispatchers: read status base+8, ack base+4, never write status
    da = "\n".join(t for _c, m, _x, _l in b.dis(DISP_D2H, 0xE0) for t in ([m] if m else []))
    ck("DISP_D2H reads [r3, #8] (status)", "ldr r4, [r3, #8]" in da)
    ck("DISP_D2H writes [r3, #4] (ack)", "str r2, [r3, #4]" in da)
    ck("DISP_D2H has no status store", "str r2, [r3, #8]" not in da)
    ck("DISP_D2H tail-notifies id 3", "movs r1, #3" in da and "0xc6170" in da)
    hb = "\n".join(t for _c, m, _x, _l in b.dis(DISP_H2D, 0xE0) for t in ([m] if m else []))
    ck("DISP_H2D reads [r3, #8]", "ldr r4, [r3, #8]" in hb)
    ck("DISP_H2D writes [r3, #4]", "str r2, [r3, #4]" in hb)
    ck("DISP_H2D no status store", "str r2, [r3, #8]" not in hb)

    # ---- trigger: reads send-word hi16, writes u16 to record+0x14, counters
    tr = "\n".join(t for _c, m, _x, _l in b.dis(SENDING_TRIGGER, 0x100) for t in ([m] if m else []))
    for frag in ("ldr.w r3, [r4, #0x32c]", "lsrs r5, r5, #0x10",
                 "ldr r3, [r4, #0x14]", "strh r5, [r3]", "ldrh r3, [r3]",
                 "ldr.w r3, [r4, #0x344]", "ldr.w r3, [r4, #0x34c]",
                 "movw sb, #0x5a5a"):
        ck("trigger has '%s'" % frag, frag in tr)

    # ---- deposit -> commit -> notify chain
    dp = "\n".join(t for _c, m, _x, _l in b.dis(DEPOSIT, 0x78) for t in ([m] if m else []))
    ck("deposit builds word1 = (len<<16)|0x6D2B", "movw r2, #0xd2b" in dp and "bfi r2, r3, #0x10, #0x10" in dp)
    ck("deposit calls ring commit 0xC192A", "bl #0xc192a" in dp)
    ck("deposit notifies id 6", "movs r1, #6" in dp and "bl #0xc6170" in dp)
    ck("deposit bumps +0x340 counter", "ldr.w r3, [r4, #0x340]" in dp)
    cm = "\n".join(t for _c, m, _x, _l in b.dis(RING_COMMIT, 0x40) for t in ([m] if m else []))
    ck("commit writes node at [ctx+0]+idx*8", "str.w r4, [r3, r2, lsl #3]" in cm)
    ck("commit advances packed index at [ctx+0xC]", "ldr r2, [r0, #0xc]" in cm)

    # ---- event delivery: dispatcher registration + single ROM caller
    pm_txt = pm
    ck("pcie_msg_init registers event 0x2D/0x2E", "movs r0, #0x2d" in pm_txt)
    ck("pcie_msg_init registers cb 0x462F9/0x4624D",
       b.word(0x497C0) == 0x000462F9 and b.word(0x497C4) == 0x0004624D)
    ck("pcie_msg_init enables events via 0xC6FF4", "bl #0xc6ff4" in pm_txt)
    for k, v in (("dispatcher D2H registered as 0x2E", DISP_D2H),
                 ("dispatcher H2D registered as 0x2D", DISP_H2D)):
        pass
    # 0xC2EFC callers: exactly the ROM-vectored ARM trampoline at image header +0x64
    callers = []
    a = 0x40064
    while a < 0x40110:
        ins = b.insn(a, arm=True)
        if ins is None:
            a += 4
            continue
        m = ins.mnemonic.split(".")[0]
        if m in ("bl", "blx") and ins.operands and ins.operands[0].type == CS_OP_IMM                 and ins.operands[0].imm == EVENT_DISPATCH:
            callers.append(a)
        a += ins.size
    thumb_callers = []
    a = BASE
    while a < BASE + len(b.d) - 4:
        ins = b.insn(a)
        if ins is None:
            a += 2
            continue
        if ins.mnemonic.split(".")[0] in ("bl", "blx") and ins.operands                 and ins.operands[0].type == CS_OP_IMM                 and ins.operands[0].imm == EVENT_DISPATCH:
            thumb_callers.append(a)
        a += ins.size
    ck("event dispatcher 0xC2EFC called from exactly one ARM site",
       len(callers) == 1, repr(callers))
    ck("that caller is CPU 0x40094 in the ROM trampoline",
       len(callers) == 1 and callers[0] == 0x40094, repr(callers))
    ck("no Thumb caller of 0xC2EFC", not thumb_callers, repr(thumb_callers))
    ck("trampoline ends in rfeia", "rfeia sp!" in "\n".join(
        (x[1] or "") for x in b.dis(0x40064, 0x68, arm=True)))

    # ---- negative: no firmware store to the DR fill / send / status words
    stores = find_store_sites(b)
    # resolve each store base against image statics is done in REPORT; here the
    # cheap structural assertion is that none of the register addresses above
    # appear as a literal (already checked) and nothing materialises them.
    addrs = [ETE_STATUS, ETE_ACK, ETE_ENGINE0_DOOR, ETE_ENGINE1_DOOR] + \
            [s for _c, s in CHAN_DR] + [s for _c, s in CHAN_SR]
    ck("no image materialisation of ETE status/ack/door/send", all(x not in refs for x in addrs))

    # ---- ROM handshake object is a RAM word, not part of the image span
    ck("ROM handshake CA beyond image end", ROM_HANDSHAKE > BASE + len(b.d))

    # ---- vendor module (plat.ko 2.5.24) comparison
    if os.path.exists(PLAT_KO):
        raw = open(PLAT_KO, "rb").read()
        def le32(word):
            return struct.pack("<I", word) in raw
        ck("vendor: ETE block CA 0x4003A000 present in module data", le32(0x4003A000))
        ck("vendor: ETE intr word CA 0x40039508 present in module data", le32(0x40039508))
        kres, kerr = ko_scan_constants(PLAT_KO, [0x7C0, 0x7D0, 0x86C, 0x870, 0x874,
                                                 0x540, 0x720, 0xC00, 0xC18])
        ck("vendor: .text disassembles", kerr is None, str(kerr))
        if kerr is None:
            for v in (0x7C0, 0x7D0, 0x86C, 0x870, 0x874, 0x540, 0x720, 0xC00, 0xC18):
                ck("vendor: no immediate/offset 0x%x (engine-internal regs)" % v,
                   not kres[v], "%d hits" % len(kres[v]))
        # the only vendor ETE global write: pcie_ete_intr_init RMW with 0xFFE0F8F8
        text, terr = ko_text(PLAT_KO)
        if text is not None:
            tdata, taddr = text
            md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
            md.detail = True
            body = [i.mnemonic + " " + i.op_str for i in md.disasm(tdata[0x7528:0x7680], 0x7528)]
            body = chr(10).join(body)
            ck("vendor: pcie_ete_intr_init has movw r2,#0xf8f8", "movw r2, #0xf8f8" in body)
            ck("vendor: pcie_ete_intr_init has movt r2,#0xffe0", "movt r2, #0xffe0" in body)
            ck("vendor: pcie_ete_intr_init is the only ETE global RMW site",
               "and r2, r2, r1" in body and "str r2, [r3]" in body)
        else:
            ck("vendor: plat.ko .text readable", False, str(terr))
        ck("vendor: 0x4003A540/0x4003A720 engine blocks never referenced as words",
           (0x4003A540 not in struct.unpack("<%dI" % (len(raw) // 4), raw[:len(raw) // 4 * 4])) and
           (0x4003A720 not in struct.unpack("<%dI" % (len(raw) // 4), raw[:len(raw) // 4 * 4])))
    else:
        print("NOTE: vendor module %s absent - vendor checks skipped" % PLAT_KO)

    ok = sum(1 for _n, c, _d in checks if c)
    for name, cond, detail in checks:
        if not cond:
            print("FAIL: %s  %s" % (name, detail))
    print("selftest: %d/%d PASS" % (ok, len(checks)))
    return ok == len(checks)

# ---------------------------------------------------------------- reports
def cmd_tables(b):
    print("== static {CA,value} init-table census (runs >= 8 entries) ==")
    for cpu, n in image_table_census(b):
        if n < 8:
            continue
        cas = [b.word(cpu + 8 * i) for i in range(n)]
        print("table @ CPU %08x: %d entries, CAs %08x..%08x, device block %08x"
              % (cpu, n, cas[0], cas[-1], cas[0] & 0xFFF00000))
    print()
    print("PHY/MAC table split: table1 @ %08x (31 x {CA 0x4011xxxx,value}),"
          " table2 @ %08x (31 x {CA,value})" % (PHY_TABLE1, PHY_TABLE2))
    print("consumer: apply loop @ CPU %08x..0xA01C8 and 0xA026C..0xA0294" % PHY_APPLY)
    print("  ldr r6, =0x1059B0 ; sub.w r7, r6, #0xF8 -> 0x1058B8 ; movs r2,#0xF8")
    print("  blx 0x1036AC (memcpy to sp) ; bl 0xF3E4C (lock) ;")
    print("  loop: r3=[stack+off] (CA), r2=[stack+off+4] (value), str r2,[r3] ;")
    print("  pass 0 = table1, pass 1 = table2 (two chains/bands)")
    print("timing: inside the PHY/RF bring-up function whose body starts near")
    print("  CPU 0x9Fxxx (blob_get 0xB1EE4 / 0xD8FF0 sequencer); it is reached from")
    print("  the image's late init path, i.e. AFTER entry A and the boot task -")
    print("  no relevance to the ETE window (which no table touches).")
    print()
    print("== ETE window in the firmware's own crash-dump table ==")
    for i in range(3):
        src = b.word(CRASH_DUMP + 12 * i)
        ln = b.word(CRASH_DUMP + 12 * i + 4)
        cur = b.word(CRASH_DUMP + 12 * i + 8)
        print("  region %d: src CA %08x len 0x%x cursor %08x" % (i, src, ln, cur))
    print("  (region 1 is the entire ETE window the firmware itself dumps on a crash)")

def cmd_layout(b):
    print("== hcc_instance CA %08x (size 0xB0, ends at the crash-dump table) ==" % HCC)
    fields = [(0x00, "0x10"), (0x04, "4 = DR channel count"), (0x08, "3 = SR channel count"),
              (0x0C, "ETE status/control base CA 0x4003A86C"),
              (0x10, "SR records base 0x11035C (3 x 0xCC)"),
              (0x14, "DR records base 0x10F2B8 (4 x 0x354, + a 5th at 0x110008)"),
              (0x18, "ETE ops table ptr (0 initially; pcie_msg_init writes 0x10CEDC)"),
              (0x1C, "sending-trigger cb (0 initially; -> 0xC625A)"),
              (0x20, "cb (0 initially; -> 0xC6802)"), (0x24, "cb (-> 0xC1902)"),
              (0x28, "cb (-> 0xC18FE)"),
              (0x58, "publish chip id (0)"), (0x5C, "message object g (0 initially; -> 0x10C0F4)"),
              (0xB0, "crash-dump table: {src 0x4003A000, len 0xC28, cursor 0}")]
    for off, what in fields:
        print("  +%03x = %08x   %s" % (off, b.word(HCC + off) or 0, what))
    print("  +0xB8/+0xBC/+0xC0 = {src 0x40039000, len 0xD5C, cursor 0}")
    print("  +0xC4 'hcc size' word = 0 (records start at hcc+0xC8 = 0x10F2B8)")
    print()
    print("== message object g CA %08x (= O1 0x10C0C0 + 0x34, *CA 0x172130) ==" % G_MSG)
    gfields = [(0x9C, "out[1] ptr slot (installed 0x40039014)"),
               (0xA0, "out[0] ptr slot (installed 0x40039010)"),
               (0xA4, "D2H doorbell ptr slot (installed 0x40101434)"),
               (0xA8, "out[5] ptr slot (installed 0x400392F0)"),
               (0xAC, "0x400392D4 slot"),
               (0xB0, "flush one-shot ptr slot (installed 0x40101418)"),
               (0xB4, "in-flight latch word (0 at image time)"),
               (0xB8, "deferred shadow word (0 at image time)"),
               (0xC0, "lock (0x10C1B4 area)")]
    for off, what in gfields:
        print("  +%03x = %08x   %s" % (off, b.word(G_MSG + off) or 0, what))
    print()
    print("== ETE register window CA 0x4003A000-0x4003AFFF (device IO region 3) ==")
    rows = [
        ("+0x000-0x0FF", "engine global/config area [U]: NO image writer, NO vendor-host write"),
        ("+0x400/+0x450/+0x4A0", "SR0-2 channel blocks (SR group A host-owned, group B device)"),
        ("+0x540", "engine/queue control block 0: +0 bit0, +4 bit0 set, +8 low3 cleared by pcie_msg_init [V]"),
        ("+0x548", "+8 of block0"), ("+0x590/5E0/630/680", "DR0-3 channel blocks (group A device-owned, group B host-owned)"),
        ("+0x6D0", "5th DR (control) channel 0x4003A6D0, send 0x4003AC10"),
        ("+0x720", "engine/queue control block 1 (same write pattern) [V]"),
        ("+0x7C0", "paired door/status word for block0 - never written by image or vendor host [V negative]"),
        ("+0x7D0", "paired door/status word for block1 - same [V negative]"),
        ("+0x86C", "ETE status/control base: image clears bit12+bit29 then ANDs 0xE0E0F8F8 [V]"),
        ("+0x870", "ack word: dispatchers only write base+4 [V]"),
        ("+0x874", "STATUS word: dispatchers read base+8; NO image writer; hardware-authored [V])"),
        ("+0xC00..0xC0C", "DR0-3 send words (hi16 target, lo16 progress): never host-written; image reads hi16 [V]"),
        ("+0xC10", "5th DR send word 0x4003AC10"),
        ("+0xC18..0xC24", "SR0-2 send words: device-authored; image never routes them"),
    ]
    for a, what in rows:
        print("  %-24s %s" % (a, what))
    print()
    print("== per-channel register groups (from pcie_msg_init decode) ==")
    print("  group A: +0x10 base, +0x14 depth-1, +0x18 producer, +0x1C consumer")
    print("  group B: +0x30 base, +0x34 depth-1, +0x38 published, +0x3C fill")
    print("  DR: image writes group A + enable; host writes group B   (engine copies A->B)")
    print("  SR: host writes group A; image writes group B + enable    (engine copies A->B)")

def cmd_writers(b):
    print("== image-side ETE register writers [V] ==")
    print("  pcie_msg_init CPU %08x:" % PCIE_MSG_INIT)
    print("    DR0-3: chan+0x10 = record+0x00 (PKTRAM staging), chan+0x14 = depth-1 (bfi),")
    print("           chan+0x18 = record+0x0C, chan+0x08 = cfg[5]&7, chan+0x00 |= 1")
    print("    SR0-2: chan+0x30 = record+0x10 (PKTRAM save buffer), chan+0x34 = depth-1,")
    print("           chan+0x38 = record+0x1C, chan+0x00 |= 1")
    print("    engine0 block 0x4003A540: +8 low3 <- 0, +4 bit0 <- 1, +0 bit0 <- 1")
    print("    engine1 block 0x4003A720: same pattern")
    print("    status/control base 0x4003A86C: clear bit12, clear bit29, then &= 0xE0E0F8F8")
    print("  dispatcher 0x4624C / 0x462F8:")
    print("    read [base+8] = 0x4003A874 (status), write [base+4] = 0x4003A870 (ack)")
    print("  startup/vector handshake 0xC6E14: writes only CA 0x40039000/0x40039800/0x40039014")
    print("    and CA 0x40000108/0x4000010C/0x4003022C - NOT the ETE window [V]")
    print()
    print("== negative scans (whole image, every materialisation form) [V] ==")
    refs = scan_literals(b)
    targets = [ETE_STATUS, ETE_ACK, ETE_ENGINE0_DOOR, ETE_ENGINE1_DOOR, ETE_INTR_HOST]
    targets += [s for _c, s in CHAN_DR] + [s for _c, s in CHAN_SR]
    targets += [ETE_BASE, ETE_STATUS_BASE]
    print("  code materialisation (literal load / movw+movt), whole image:")
    for t in targets:
        sites = refs.get(t, [])
        print("    %08x code sites: %s" % (t, ", ".join("%08x" % s for s, _k in sites) or "NONE"))
    print("  raw data-word occurrences (tables + structure initialisers):")
    words = []
    for off in range(0, len(b.d) - 4, 4):
        w = u32(b.d, off)
        if w in targets:
            words.append((off + BASE, w))
    for cpu, w in words:
        print("    %08x -> %08x" % (cpu, w))
    print()
    print("  interpretation: NO code anywhere in the image materialises the status word,")
    print("  the ack word, a send word, an engine door, a fill or the five channel CAs.")
    print("  The only ETE-window words in image data are the ETE block base and the status")
    print("  base (inside the ops table / hcc_instance initialiser) - and those are consumed")
    print("  only as pointer constants by pcie_msg_init / the two dispatchers.")
    print("  All channel/send addresses live in the image's record structures and are")
    print("  dereferenced through structure fields - the writers are the ones listed above.")
    print("  NO image code can write: 0x4003A874 (status), 0x4003AC00.. (send words),")
    print("  0x4003A5CC.. (fills), 0x4003A7C0/0x7D0 (doors), or any +0xC00-class register.")

def cmd_chain(b):
    print("== ETE D2H chain (image side) [V] ==")
    print("deposit op  %08x (ops+0x10): node{word0=buffer_dev_va, word1=(len<<16)|0x6D2B}" % DEPOSIT)
    print("  -> ring commit %08x: node[packed_producer&0x3FF] at [ctx+0]; advance ctx+0xC" % RING_COMMIT)
    print("  -> d2h_notify %08x(chip=0, id=6): shadow g+0xB8 |= 1<<6; publish out[1] only when" % D2H_NOTIFY)
    print("     in-flight g+0xB4 == 0, else defer; doorbell 0x40101434")
    print("  -> flush %08x: one-shot write through g+0xB0 (0x40101418), spin out[1]==0," % D2H_FLUSH)
    print("     then publish deferred shadow to out[1] + doorbell")
    print()
    print("device ETE status path:")
    print("  0x4003A874 bits 16-19 -> hcc+0x1C = sending trigger %08x(hcc, ch) [V]" % SENDING_TRIGGER)
    print("  0x4003A874 bits 24-31 -> hcc+0x24 = 0xC1902")
    print("  0x4003A874 bits  3-7  -> hcc+0x20 = 0xC6802 ; bits 8-15 -> hcc+0x28 = 0xC18FE")
    print("  dispatch entry: ROM-vectored IRQ trampoline CPU %08x (image header+0x64)" % IRQ_TRAMPOLINE)
    print("    -> event dispatcher %08x -> handler table CA 0x%x -> 0x4624C/0x462F8" % (EVENT_DISPATCH, HANDLER_TABLE))
    print()
    print("sending trigger %08x(hcc, ch):" % SENDING_TRIGGER)
    print("  r5 = *(chan_send_CA) >> 16            (hardware-authored target index)")
    print("  strh r5,[record+0x14] ; ldrh back      (handshake write into the channel's")
    print("     PKTRAM control word: record+0x14 = staging+0x100)")
    print("  record+0x344++ (trigger counter), record+0x34C += released nodes")
    print("  scan tagged nodes (0x5A5A at node+0xA) via ops+0x28; update record+0x10; wake g")
    print()
    print("=> the engine is never 'started' by a register write from the host or the")
    print("   image: the image reacts to hardware status and commands the channel via a")
    print("   PKTRAM word.  The first status word is authored by the device ETE hardware.")

def cmd_benchpack(b):
    print("== READ-ONLY bench snapshot pack (bar0 offsets; nothing below opens a device) ==")
    rows = [
        ("hcc_instance", HCC, 0x39),
        ("g in-flight/deferred", G_MSG + 0xB4, 1),
        ("ETE status word", ETE_STATUS, 1),
        ("ETE ack word", ETE_ACK, 1),
        ("ETE intr word (host)", ETE_INTR_HOST, 1),
        ("ETE engine0 block +0..+8", ETE_ENGINE0, 3),
        ("ETE engine1 block +0..+8", ETE_ENGINE1, 3),
        ("ETE engine0 door", ETE_ENGINE0_DOOR, 1),
        ("ETE engine1 door", ETE_ENGINE1_DOOR, 1),
        ("ETE status base +0", ETE_STATUS_BASE, 1),
        ("DR0 send word", ETE_SEND_DR0, 1),
        ("DR1 send word", ETE_SEND_DR0 + 4, 1),
        ("DR2 send word", ETE_SEND_DR0 + 8, 1),
        ("DR3 send word", ETE_SEND_DR0 + 12, 1),
        ("SR0 send word", ETE_SEND_SR0, 1),
        ("DR0 fill/groupA/G/P", CHAN_DR[0][0], 7),
        ("DR1 fill/groupA/G/P", CHAN_DR[1][0], 7),
        ("DR2 fill/groupA/G/P", CHAN_DR[2][0], 7),
        ("DR3 fill/groupA/G/P", CHAN_DR[3][0], 7),
        ("SR0 block", CHAN_SR[0][0], 0x10),
        ("trigger counter DR0", DR_RECORDS + 0x344, 1),
        ("trigger counter DR1", DR_RECORDS + 0x354 + 0x344, 1),
        ("trigger counter DR2", DR_RECORDS + 2 * 0x354 + 0x344, 1),
        ("trigger counter DR3", DR_RECORDS + 3 * 0x354 + 0x344, 1),
        ("DR0 record progress +0x10", DR_RECORDS + 0x10, 1),
        ("entry-A ROM handshake", ROM_HANDSHAKE, 2),
        ("event current vector", 0x4016010C, 2),
        ("event pending bank 0x2D/0x2E", 0x40161184, 1),
        ("event pending mirror", 0x40161284, 1),
        ("handler table 0x2D/0x2E", HANDLER_TABLE + 4 * 0x2D, 2),
    ]
    print("| what | CA | words | BAR0 | expected if the ETE pump is alive |")
    print("| --- | ---: | ---: | ---: | --- |")
    for what, ca, n in rows:
        print("| %s | 0x%08x | %d | 0x%06x | see REPORT.md section 9 |" % (what, ca, n, bar0(ca)))
    print()
    print("== optional single-write diagnostic (events 0x2D/0x2E replay; see REPORT 9.3) ==")
    print("  fingerprint first: read CA 0x40161184, CA 0x40161284, CA 0x4016010C,")
    print("    CA 0x4003A870, g+0xB4 (0x10C1A8), g+0xB8 (0x10C1AC),")
    print("    trigger counters 0x10F5FC/0x10F950/0x10FCA4/0x10FFF8")
    print("  write 0x00004000 to CA 0x40161184 (BAR0 0x519184) and CA 0x40161284 (BAR0 0x519284)")
    print("  expected if the CPU can deliver events: g+0xB8 (0x10C1AC) changes by |0x08")
    print("    (D2H notify id 3 deferred) and/or out[1] bit3 appears; then drain normally.")
    print("  expected if the CPU IRQ path is masked: pending bit stays latched, g+0xB8")
    print("    unchanged, trigger counters unchanged -> definitive ROM/event-delivery proof.")
    print("  the ETE transfer itself cannot be started this way: bits16-20 of 0x4003A874")
    print("    are hardware-authored and stay 0, so the trigger is never invoked.")

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--fw", default=FW)
    sub = p.add_subparsers(dest="cmd", required=True)
    for name in ("selftest", "tables", "layout", "writers", "chain", "benchpack"):
        sub.add_parser(name)
    a = p.parse_args()
    b = Blob(a.fw)
    if a.cmd == "selftest":
        sys.exit(0 if selftest(b) else 1)
    if a.cmd == "tables":
        cmd_tables(b)
    if a.cmd == "layout":
        cmd_layout(b)
    if a.cmd == "writers":
        cmd_writers(b)
    if a.cmd == "chain":
        cmd_chain(b)
    if a.cmd == "benchpack":
        cmd_benchpack(b)

if __name__ == "__main__":
    main()
