#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""rom_s01.py - Hi5622V100 ROM stage S0/S1 reconstruction (lane wifi-rom-s01).

HOST-ONLY static reconstruction from the LOCAL vendor firmware image, the LOCAL
vendor kernel modules and the LOCAL project session evidence.  This tool never
opens a device, never copies firmware bytes and never writes outside stdout:
it only prints derived addresses, constants, structure layouts and a read-only
bench plan.

Subcommands:
  selftest   re-verify every fact quoted in REPORT.md (N checks)
  handshake  frame the CA 0x1BF000 ROM<->image boot handshake page: layout,
             writers, stages, golden-vendor values and the 0x1BD000 anomaly
  rom        decode the readable ROM identity/vector window (CA 0x0..0x3C,
             0x1E0..0x1FF) and the image-side ABI it double-indirects into
  engine     ETE engine-global / doors: image + host writer negatives, the
             firmware-owned crash-dump snapshot route, PKTRAM addresses
  pack       print the discriminating READ-ONLY bench pack (ours vs stock)
  all        everything above

Address conventions (project-wide): CPU = file + 0x40000 for the image;
host BAR0 CPU-RAM/ROM view = CPU + 0x6B8000 (region 5) or = CPU for CPU <
0x1BFFFF (region 0 identity); IO CA 0x40000000..0x4011FFFF = 0x3B8000 +
(CA-0x40000000) (region 3); PKTRAM CA 0x01000000.. = 0x1D8000 + (CA-0x01000000)
(region 2); TCM CA 0x00400000.. = region 1 (not addressed here).

Evidence grades in REPORT.md: [V] verified here from the named local artifact,
[V*] measured on the bench by an earlier attended lane (cited), [I] inference,
[U] unknown.
"""
import argparse
import hashlib
import os
import struct
import sys

try:
    import capstone
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB, CS_OP_IMM, CS_OP_MEM, CS_OP_REG
except Exception:  # pragma: no cover
    sys.exit("capstone not installed (use the repo .provision-venv)")

FW = os.environ.get("HI5622_FW",
                    "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin")
KO_DIRS = [
    os.environ.get("HI5622_KO_DIR", "/tmp/rootfs2524b/lib/modules/5.10.201"),
    "/home/shibbityshwab/recovery-extract/lib/hisilicon/ko",
]
PLAT = "/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_plat.ko"
BASE = 0x40000
FW_SIZE = 928920
FW_SHA256_PREFIX = "7fc87e20"
BAR0_RAM = 0x6B8000
BAR0_IO = 0x3B8000
BAR0_PKT = 0x1D8000
CPU_RAM_END = 0x1BFFFF     # region-0 identity window limit

# candidate values under test -------------------------------------------------
BOOT_PAGE = 0x1BF000       # ROM<->image boot handshake/flags page
BOOT_PAGE_MAGIC = 0x1BF004 # second flag word (entry B sets bit 4)
HS_STATUS = 0x1BF008       # inter-core handshake status: image 0, partner 2
HS_MAGIC = 0x1BF00C        # 0xABCD written by the image, left by the partner
LOG_ARENA = 0x1BD000       # image-nominated crash-dump region 0 (0x2000 B)
ARENA_CURSOR = 0x001BD0D8  # observed D1 second word at HS_MAGIC
DOOR0 = 0x4003A7C0
DOOR1 = 0x4003A7D0
DOOR0_D1_VALUE = 0x8CAC4ECA  # live read in the D1 confirmation pack [V*]
ETE_BASE = 0x4003A000
ETE_CRASH_LEN = 0xC28
CRASH_TABLE = 0x10F294
CRASH_DEST = 0x010DB800
CRASH_END = 0x010DFFFF

# image header / entries ------------------------------------------------------
HDR_ENTRY_A = 0x46970
HDR_ENTRY_B = 0xC742C
IRQ_TRAMPOLINE = 0x40064
EVENT_DISPATCH = 0xC2EFC

# ROM vector window model (live reads [V*], verified arithmetic here) ----------
ROM_VECTOR_WORDS = {
    0x00: 0xE59FF018, 0x04: 0xE59FF018, 0x08: 0xE59FF018, 0x0C: 0xE59FF018,
    0x10: 0xE59FF018, 0x14: 0xE320F000, 0x18: 0xE59FF014, 0x1C: 0xE59FF014,
}


def u16(b, o): return struct.unpack_from("<H", b, o)[0]
def u32(b, o): return struct.unpack_from("<I", b, o)[0]


class Blob:
    def __init__(self, path=FW):
        try:
            with open(path, "rb") as f:
                self.d = f.read()
        except OSError as e:
            sys.exit("firmware image not available: %s" % e)
        self.md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
        self.md.detail = True
        self.mda = Cs(CS_ARCH_ARM, CS_MODE_ARM)
        self.mda.detail = True

    def w(self, cpu):
        o = cpu - BASE
        if o < 0 or o + 4 > len(self.d):
            return None
        return u32(self.d, o)

    def insn(self, cpu, arm=False):
        o = cpu - BASE
        if o < 0 or o + 2 > len(self.d):
            return None
        md = self.mda if arm else self.md
        if arm:
            n = 4
        else:
            n = 4 if u16(self.d, o) >= 0xE800 else 2
        if o + n > len(self.d):
            return None
        try:
            ins = next(iter(md.disasm(self.d[o:o + n], cpu)), None)
        except Exception:
            return None
        if ins is None or len(ins.bytes) != n:
            return None
        return ins

    def lit(self, ins):
        for op in ins.operands:
            if op.type == CS_OP_MEM and ins.reg_name(op.mem.base) == "pc":
                return ((ins.address + 4) & ~3) + op.mem.disp
        return None

    def opdown(self, cpu):
        ins = self.insn(cpu)
        if ins is None:
            return None
        l = self.lit(ins)
        return (ins.mnemonic, ins.op_str, l, self.w(l) if l is not None else None)


def bar0_io(ca):
    return BAR0_IO + (ca - 0x40000000)


def bar0_pkt(ca):
    return BAR0_PKT + (ca - 0x01000000)


def bar0_ram(cpu):
    return BAR0_RAM + cpu


def read_regions():
    return [
        ("0 ROM_WRAM ", 0x000000, 0x1BFFFF, 0x00000000),
        ("1 TCM      ", 0x1C0000, 0x1D7FFF, 0x00400000),
        ("2 PKTRAM   ", 0x1D8000, 0x3B7FFF, 0x01000000),
        ("3 IO       ", 0x3B8000, 0x4D7FFF, 0x40000000),
        ("4 ACP      ", 0x4D8000, 0x6B7FFF, 0x02000000),
        ("5 ACP/fw   ", 0x6B8000, 0x8CFFFF, 0x01200000),
    ]


def readable_views(ca):
    """Host read views that decode EXACTLY to device CPU address ca.

    Region 0 maps BAR0 offset == device CA (ROM/WRAM identity, CA<0x1C0000).
    Region 5 maps BAR0 0x6B8000+X -> ACP CA 0x01200000+X, and the firmware
    upload target proves ACP CA = CPU + 0x01200000, so a CPU address ca is
    visible at BAR0 0x6B8000+ca for ca < 0x218000.
    Region 3 (IO) and region 2 (PKTRAM) are device-IO, not CPU addresses.
    """
    views = []
    if ca < 0x1C0000:
        views.append(("identity", ca))
    if ca < 0x218000:
        views.append(("ram5", bar0_ram(ca)))
    if 0x40000000 <= ca <= 0x4011FFFF:
        views.append(("io3", bar0_io(ca)))
    if 0x01000000 <= ca < 0x01080000:
        views.append(("pkt2", bar0_pkt(ca)))
    return views


def host_readable_ca(ca):
    v = readable_views(ca)
    return (v[0][1], v[0][0]) if v else None


def region_decode(bar0):
    """Which device CA does a BAR0 offset decode to (first matching region)?"""
    for name, lo, hi, tgt in read_regions():
        if lo <= bar0 <= hi:
            return tgt + (bar0 - lo), name
    return None, None


def census(b, values):
    out = {}
    for v in values:
        pat = struct.pack("<I", v & 0xFFFFFFFF)
        n, i = 0, 0
        while True:
            i = b.d.find(pat, i)
            if i < 0:
                break
            n += 1
            i += 1
        out[v] = n
    return out


def lit_refs(b, target, lo=BASE, hi=None):
    hi = hi if hi is not None else BASE + len(b.d) - 4
    hits = []
    for off in range(0, len(b.d) - 4):
        cpu = BASE + off
        if cpu < lo or cpu >= hi:
            continue
        h = u16(b.d, off)
        if (h >> 11) == 0b01001:
            tgt = ((cpu + 4) & ~3) + ((h & 0xFF) << 2)
            o = tgt - BASE
            if 0 <= o <= len(b.d) - 4 and u32(b.d, o) == target:
                hits.append((cpu, "thumb16", tgt))
        if h == 0xF8DF and off + 4 <= len(b.d):
            h2 = u16(b.d, off + 2)
            tgt = ((cpu + 4) & ~3) + (h2 & 0xFFF)
            o = tgt - BASE
            if 0 <= o <= len(b.d) - 4 and u32(b.d, o) == target:
                hits.append((cpu, "thumb32", tgt))
        if off % 4 == 0:
            w = u32(b.d, off)
            if (w & 0x0F7F0000) == 0x051F0000 and (w >> 28) == 0xE:
                imm = w & 0xFFF
                tgt = cpu + 8 + (imm if (w >> 23) & 1 else -imm)
                o = tgt - BASE
                if 0 <= o <= len(b.d) - 4 and u32(b.d, o) == target:
                    hits.append((cpu, "arm", tgt))
    return hits


def materialised(b):
    """Set of 32-bit constants the image can materialise (PC-relative literal
    loads + movw/movt pairs).  Stronger than a raw byte census, weaker than a
    full dataflow scan; the ete-engine lane's independent scan agrees."""
    vals = set()
    a = BASE
    end = BASE + len(b.d) - 4
    while a < end:
        ins = b.insn(a)
        if ins is None:
            a += 2
            continue
        l = b.lit(ins)
        if l is not None:
            w = b.w(l)
            if w is not None:
                vals.add(w)
        m = ins.mnemonic.split(".")[0]
        if m == "movw" and len(ins.operands) == 2 and \
                ins.operands[0].type == CS_OP_REG and ins.operands[1].type == CS_OP_IMM:
            reg = ins.reg_name(ins.operands[0].reg)
            lo = ins.operands[1].imm & 0xFFFF
            bb = a + ins.size
            for _ in range(8):
                i2 = b.insn(bb)
                if i2 is None:
                    break
                if i2.mnemonic.split(".")[0] == "movt" and len(i2.operands) == 2 \
                        and i2.operands[0].type == CS_OP_REG \
                        and i2.operands[1].type == CS_OP_IMM \
                        and i2.reg_name(i2.operands[0].reg) == reg:
                    vals.add(((i2.operands[1].imm & 0xFFFF) << 16) | lo)
                    break
                bb += i2.size
        a += ins.size
    return vals


def selftest(b):
    checks = []

    def chk(desc, cond):
        checks.append((desc, bool(cond)))

    chk("image size 928920", len(b.d) == FW_SIZE)
    chk("image sha256 prefix %s" % FW_SHA256_PREFIX,
        hashlib.sha256(b.d).hexdigest().startswith(FW_SHA256_PREFIX))
    chk("header entry A = 0x00046971", b.w(BASE) == 0x00046971)
    chk("header entry B = 0x000C742D", b.w(BASE + 4) == 0x000C742D)
    chk("header prefetch slot (+0x5C) = 0x001030BC", b.w(BASE + 0x5C) == 0x001030BC)
    chk("0x46974 ldr r5,=0x001BF000", b.opdown(0x46974)[3] == BOOT_PAGE)
    chk("0x4697A orr #0x20 (bit5)", "orr" in b.opdown(0x4697A)[0] and
        "0x20" in b.opdown(0x4697A)[1])
    chk("0x469A4 orr #0x40 (bit6)", "orr" in b.opdown(0x469A4)[0] and
        "0x40" in b.opdown(0x469A4)[1])
    chk("0x473C2 orr #0x2000 (bit13)", "orr" in b.opdown(0x473C2)[0] and
        "0x2000" in b.opdown(0x473C2)[1])
    chk("0x473FC orr #0x4000 (bit14)", "orr" in b.opdown(0x473FC)[0] and
        "0x4000" in b.opdown(0x473FC)[1])
    chk("0x473C8 movw r3,#0xabcd", "0xabcd" in b.opdown(0x473C8)[1])
    chk("0x473CC ldr r4,=0x001BF008", b.opdown(0x473CC)[3] == HS_STATUS)
    chk("0x473D4 strd {r2,r3},[r4]", b.opdown(0x473D4)[0].startswith("strd"))
    chk("0x473E0 mov.w r2,#0x20000", "0x20000" in b.opdown(0x473E0)[1])
    chk("0x473E4 ldr r3,=0x40161F00", b.opdown(0x473E4)[3] == 0x40161F00)
    chk("0x473F0 ldr r3,[r4] poll", b.opdown(0x473F0)[0].startswith("ldr"))
    chk("0x473F2 cmp r3,#2", "2" in b.opdown(0x473F2)[1])
    chk("0x47610 wait-path reload of 0x001BF008", b.opdown(0x47612)[3] == HS_STATUS)
    chk("0xC742E ldr r2,=0x001BF004", b.opdown(0xC742E)[3] == BOOT_PAGE_MAGIC)
    chk("0xC7432 orr r3,#0x10 (entry B bit4)", "0x10" in b.opdown(0xC7432)[1])
    chk("0x40166 ldr r2,=0x001BF000", b.opdown(0x40166)[3] == BOOT_PAGE)
    chk("0x4016A orr #0x80000000 (bit31)", "0x80000000" in b.opdown(0x4016A)[1])
    chk("0x410C6 ldr r3,=0x001BF000", b.opdown(0x410C6)[3] == BOOT_PAGE)
    chk("0x410CE orr #0x2000000 (bit25)", "0x2000000" in b.opdown(0x410CE)[1])
    chk("0x4126C ldr r4,=0x001BF000", b.opdown(0x4126C)[3] == BOOT_PAGE)
    chk("0x41276 orr #0x100000 (bit20)", "0x100000" in b.opdown(0x41276)[1])
    chk("0x46D4C ldr r1,=0x001BF000 (RAM top bound)",
        b.opdown(0x46D4C)[3] == BOOT_PAGE)

    c = census(b, [0x001BD000, ARENA_CURSOR, HS_STATUS])
    mv = materialised(b)
    chk("0x001BD000 occurs once (crash table src0)", c[0x001BD000] == 1)
    chk("0x001BD0D8 occurs nowhere in image", c[ARENA_CURSOR] == 0)
    chk("0x001BF008 appears exactly twice", c[HS_STATUS] == 2)
    chk("0x8CAC4ECA is not a materialisable constant", 0x8CAC4ECA not in mv)
    chk("0x4003A7C0 door CA not materialised", DOOR0 not in mv)
    chk("0x4003A7D0 door CA not materialised", DOOR1 not in mv)
    chk("0x4003A874 status word not materialised", 0x4003A874 not in mv)
    chk("0x4003AC00 send word not materialised", 0x4003AC00 not in mv)
    hs = lit_refs(b, HS_STATUS)
    chk("0x001BF008 literal homes = 0x47510,0x47620",
        sorted(x[2] for x in hs) == [0x47510, 0x47620])
    chk("doors live as static CA fields paired with control blocks",
        b.w(0x10CAC8) == 0x4003A540 and b.w(0x10CACC) == DOOR0 and
        b.w(0x10CB90) == 0x4003A720 and b.w(0x10CB94) == DOOR1)
    chk("no literal load of the door CA field address 0x10CACC",
        lit_refs(b, 0x10CACC) == [] and lit_refs(b, 0x10CB94) == [])

    chk("crash table src0 = 0x001BD000", b.w(CRASH_TABLE) == LOG_ARENA)
    chk("crash table len0 = 0x2000", b.w(CRASH_TABLE + 4) == 0x2000)
    chk("crash table src1 = 0x4003A000", b.w(CRASH_TABLE + 0xC) == ETE_BASE)
    chk("crash table len1 = 0xC28", b.w(CRASH_TABLE + 0x10) == ETE_CRASH_LEN)
    chk("crash table src2 = 0x40039000", b.w(CRASH_TABLE + 0x18) == 0x40039000)
    chk("crash table len2 = 0xD5C", b.w(CRASH_TABLE + 0x1C) == 0xD5C)
    chk("handler literals dest/end", b.w(0xC6FAC) == CRASH_DEST and
        b.w(0xC6FC4) == CRASH_END)
    chk("ETE dump dest => PKTRAM BAR0 0x2B5800",
        bar0_pkt(CRASH_DEST + 0x2000) == 0x2B5800)
    chk("log copy dest => PKTRAM BAR0 0x2B3800",
        bar0_pkt(CRASH_DEST) == 0x2B3800)
    chk("pcie glue copy dest => PKTRAM BAR0 0x2B6428",
        bar0_pkt(CRASH_DEST + 0x2000 + ETE_CRASH_LEN) == 0x2B6428)

    chk("0x120A6C movw r2,#0x4408", "0x4408" in b.opdown(0x120A6C)[1])
    chk("0x120A9A mrc p15 c1", "p15" in b.opdown(0x120A9A)[1])
    chk("0x120A9E bic r0,#1 (MMU off)", "1" in b.opdown(0x120A9E)[1])
    chk("0x120B04 orr r0,#1 (MMU on)", "1" in b.opdown(0x120B04)[1])
    tail_refs = lit_refs(b, BOOT_PAGE, 0x120A6C, BASE + len(b.d))
    chk("CPU1 tail never references 0x001BF000", tail_refs == [])

    chk("ROM v0 word -> target 0x20", 0 + 8 + 0x18 == 0x20)
    chk("ROM IRQ word (0x18) -> target 0x34", 0x18 + 8 + 0x14 == 0x34)
    chk("ROM FIQ word (0x1C) -> target 0x38", 0x1C + 8 + 0x14 == 0x38)
    chk("ROM slot pointer 0x1E4 -> image header+0x5C", 0x0004005C == BASE + 0x5C)
    chk("header+0x5C value is prefetch stub 0x001030BC", b.w(0x0004005C) == 0x001030BC)

    chk("door CA readable at BAR0 0x3F27C0",
        host_readable_ca(DOOR0) is not None and host_readable_ca(DOOR0)[0] == 0x3F27C0)
    chk("ETE base readable at BAR0 0x3F2000", host_readable_ca(ETE_BASE)[0] == 0x3F2000)
    chk("CA 0x4016010C NOT host-readable", host_readable_ca(0x4016010C) is None)
    chk("BAR0 0x51810C decodes ACP CA 0x0204010C",
        region_decode(0x51810C)[0] == 0x0204010C)
    chk("CPU 0x1BF008 identity BAR0 0x1BF008", host_readable_ca(HS_STATUS)[0] == 0x1BF008)
    chk("CPU 0x1BF008 region-5 BAR0 0x877008", bar0_ram(HS_STATUS) == 0x877008)
    chk("CPU 0x10F29C cursor BAR0 0x7C729C", bar0_ram(0x10F29C) == 0x7C729C)

    if os.path.exists(PLAT):
        with open(PLAT, "rb") as f:
            ko = f.read()
        text = None
        try:
            from elftools.elf.elffile import ELFFile
            with open(PLAT, "rb") as f:
                elf = ELFFile(f)
                for sec in elf.iter_sections():
                    if sec.name == ".text":
                        text = sec.data()
                        break
        except Exception:
            text = None
        if text is not None and len(text) > 0x15F00:
            chk("plat.ko .text pcie_trigger_ete_sending_handle stub = FE FF FF EA",
                text[0x15EFC:0x15F00] == bytes.fromhex("feffffea"))
        else:
            chk("plat.ko .text readable for stub check", False)
        for v in (DOOR0, DOOR1, 0x4003A874, 0x4003AC00):
            chk("plat.ko lacks CA 0x%08X" % v, struct.pack("<I", v) not in ko)

    passed = sum(1 for _, ok in checks if ok)
    for desc, ok in checks:
        print("  [%s] %s" % ("PASS" if ok else "FAIL", desc))
    print("\nselftest: %d/%d PASS" % (passed, len(checks)))
    return 0 if passed == len(checks) else 1


def cmd_handshake(b):
    print("=" * 78)
    print("CA 0x1BF000 ROM<->image boot handshake page (lane wifi-rom-s01)")
    print("=" * 78)
    print("""
Block layout (image accesses verified in this lane; the page is the first RAM
after the image-nominated 0x2000-byte boot-log arena 0x1BD000..0x1BF000):

  +0x000  boot-progress bitmap (written by image stages, likely watched by ROM)
  +0x004  second flag word; entry B ORs 0x10
  +0x008  inter-core handshake STATUS  - image writes 0, partner must write 2
  +0x00C  handshake MAGIC 0xABCD       - image writes it, partner leaves it
  +0x010  end of the cache-clean/invalidate range used in the poll loop

Set bits observed as writers (all [V]):
  bit5  0x00000020  entry A 0x4697A
  bit6  0x00000040  entry A 0x469A4
  bit13 0x00002000  entry A 0x473C2   immediately before the handshake store
  bit14 0x00004000  entry A 0x473FC   on the success path (status == 2)
  bit20 0x00100000  fn 0x41268 (0x41276)  HW/PHY bring-up block
  bit25 0x02000000  fn 0x410C6 (0x410CE)  HW bring-up block
  bit31 0x80000000  helper 0x40140 (0x4016A) after CA 0x40101230 poll == 2
  +0x004 bit4 0x10  entry B 0xC7432
""")
    print("Exact handshake code (entry A = header +0x00, CPU 0x46970):")
    for cpu in (0x473C8, 0x473CC, 0x473CE, 0x473D2, 0x473D4, 0x473D8, 0x473DC,
                0x473E0, 0x473E4, 0x473E6, 0x473E8, 0x473EC, 0x473F0, 0x473F2,
                0x473F4, 0x473F8, 0x473FA, 0x473FC, 0x47400):
        d = b.opdown(cpu)
        if d:
            m, op, l, v = d
            extra = "   ; lit@0x%05X = 0x%08X" % (l, v) if l else ""
            print("  c:0x%05X  %-42s%s" % (cpu, m + " " + op, extra))
    print("""
Contract [V code + V* golden]:
  1. image: [0x1BF008]=0, [0x1BF00C]=0xABCD, cache-clean/invalidate
     [0x1BF008,0x1BF010), DMB ISHST.
  2. image: CA 0x40161F00 <- 0x00020000 (inter-core doorbell; project
     convention is (1<<core)<<16 | 1, so 0x20000 = wake core 1, no SWI;
     the exception handler uses 0x10002 / 0x20002).
  3. partner: must write 2 to 0x1BF008 and must not disturb 0x1BF00C.
  4. image polls [0x1BF008] == 2 forever, invoking ARM cache maintenance
     0x102E68 in the wait path; on success sets bit14 and continues to queue
     the boot task (0xC745C at 0x47424) and call the scheduler (0xC704C at
     0x47428).

Writers / partner evidence [V]:
  * The only image store to 0x1BF008/0x1BF00C is the strd at CPU 0x473D4
    ({0, 0xABCD}); 0x001BF008 is materialised only by literals at
    0x47510 / 0x47620.
  * No image site writes the value 2 to the status word, and the image cannot
    materialise 0x1BD000 / 0x1BD0D8 anywhere (census: 0x1BD000 occurs once as
    the crash-dump table's first source pointer; 0x1BD0D8 occurs nowhere).
  * The CPU1 bootstrap in the image tail (CPU 0x120A6C) never references
    0x1BF000; its literals are its own WRAM tables (0x107B8..0x14408),
    CP15/MMU state and the 0x4018xxxx controller.  It is a reset-time,
    MMU-off bootstrap, so it can only be entered by a ROM-owned secondary-core
    release - it is not the handshake partner either.
  => the writer of 2 is mask-ROM code (the ROM's secondary-core service or a
     ROM-internal IPC handler), i.e. ROM stage S1.  No host register takes
     part; the host cannot author 2 without a firmware-RAM poke.

Golden vendor-boot values [V*: golden_v2 trace on the stock 5.10 vendor stack]:
  0x1BF008 = 0x00000002 , 0x1BF00C = 0x0000ABCD  -> contract complete.
  same trace: mask 0x40008 = 0, state 0x118B60 = 1, boot_slot 0x17D42C =
  0x000C19D1, g = 0x0010C0F4, ETE intr 0x40039508 = 0x3F201818.

Our boot - two observed states [V*]:
  state A: 0x1BF008 = 2 / 0x1BF00C = 0xABCD  (bench read 2026-11-23 ~08:03Z;
           chip mask 0x0A, state 1).
  state B (D1 confirmation pack, up=1904 s): 0x1BF008 = 0x001BD000,
           0x1BF00C = 0x001BD0D8.

Why state B still shows 0x1BD000 [I, strong]:
  * 0x1BD000 is exactly the base of the image-nominated 0x2000-byte boot-log
    arena (crash-dump region 0), and 0x1BD0D8 = base + 0xD8 (216 bytes).
  * neither value can come from image code (census above), so the words are
    pre-image (ROM-authored) state: a {log-arena base, write cursor} pair.
  * both the image's 0 and 0xABCD are absent -> entry A never executed the
    handshake store in that boot; the boot is stuck BEFORE S1 completion, not
    in a different handshake dialect.
  * consequently the D1 ETE negative was measured on a boot whose S1 stage is
    incomplete; whether S1 completion alone makes the event layer work needs
    the co-sampled pack (handshake + 0x1BF000 bits + 0x1BD000 arena +
    entry-A state, on ONE boot).

Caveat [U]: state A's chip provenance (hybrid transport vs stock stack) is not
recorded in the session log; re-sample the page on the same run as the rest of
the pack before treating either state as the transport's canonical outcome.
""")
    return 0


def cmd_rom(b):
    print("=" * 78)
    print("Readable ROM vectors / table and the image-side ABI they reach")
    print("=" * 78)
    print("""
ROM identity window (region 0: host BAR0 offset == CPU address for CPU <
0x1BFFFF; phys = 0x40000000 + offset).  Live words read earlier [V*]:
""")
    for a in sorted(ROM_VECTOR_WORDS):
        print("  CA 0x%05X = 0x%08X" % (a, ROM_VECTOR_WORDS[a]))
    print("""
Decode [V arithmetic]:
  * every vector is a one-instruction PC-relative load:
        CA 0x00..0x10: ldr pc,[pc,#0x18]  -> target word at CA +0x20..+0x30
        CA 0x14     : 0xE320F000 (NOP)    -> ARM reserved vector 5
        CA 0x18     : ldr pc,[pc,#0x14]   -> target word at CA 0x34 (IRQ)
        CA 0x1C     : ldr pc,[pc,#0x14]   -> target word at CA 0x38 (FIQ)
  * so the ROM's vector targets are the words at CA 0x20,0x24,0x28,0x2C,0x30,
    0x34(IRQ),0x38(FIQ) - NONE of which has been read.  They are free
    read-only reads (all < 0x1BFFFF, region-0 identity window).
  * second indirection: ROM code at CA 0x50 is
        ldr r0,[pc,#0x18C]   ; r0 = [CA 0x1E4]
        ldr r0,[r0]          ; r0 = [[0x1E4]]
        bx  r0
        str r1,[sp,#-4]!     ; save the caller's r1 for the image stub
    and [CA 0x1E4] = 0x0004005C = address of the image header's prefetch-abort
    handler slot.  [0x4005C] = 0x001030BC = the image's prefetch-abort stub.
    So one ROM vector path double-indirects into the image header handler
    table - the same table at header +0x50..+0x60 (reset/undef/svc/prefetch/
    abort = 0x103140/114/0E8/0BC/090).  Whether 0x1E4 is a fixed slot or is
    re-pointed per exception class is [U]; read CA 0x1E0..0x1FF (pack D2).

What this proves about ROM's sequence into the image [I]:
  * the ROM has an exception-dispatch contract INTO the image, not only a
    load-and-branch contract; [0x1E4] pointing at an image header slot is the
    readable half of that contract.
  * mask evidence proves the contract is live: our boots show mask 0x0A =
    bit1 (data abort) + bit3 (SVC) set by the image's common handler
    (0xC6E14), so the SVC and data-abort ROM vectors do reach image stubs.
  * IRQ/FIQ are the only vectors whose target words (CA 0x34/0x38) decide
    whether the image's ARM IRQ trampoline at CPU 0x40064 can ever run; the
    trampoline has exactly one caller - the ROM vector - and 0xC2EFC exactly
    one caller - the trampoline BLX at 0x40094 [V].  If CA 0x34 points into
    the image (0x40064/65) delivery is wired and the gap is event assertion;
    if it points into ROM, IRQ delivery stays ROM-internal.

Image side of the S1 ABI [V]:
  * header +0x00 = 0x00046971 (Thumb entry A 0x46970) and header +0x04 =
    0x000C742D (Thumb entry B 0xC742C); both pointers occur only in the
    header (ROM consumes them).
  * entry A's observable prefix (system object CA 0x17D398 = 0x60,
    handler-table 0x3E/0x3F/0x40/0x42, boot global 0x117C00+0xEE0..) is the
    proof ROM invokes A; the 0x1BF000 bits are the finer progress marker.
  * the CPU1 tail at CPU 0x120A6C is a reset-time (MMU-off) bootstrap; the
    image never materialises 0x120A6C, so the ROM owns secondary-core
    release.  Who starts CPU1 and who answers the handshake are the two S1
    unknowns; both live below CPU 0x40000, readable only via the identity
    window. [U]
""")
    return 0


def cmd_engine(b):
    print("=" * 78)
    print("ETE engine-global / doors - who could author them")
    print("=" * 78)
    print("""
Readable window: CA 0x4003A000..0x4003AC28 is host-readable through the IO
region at BAR0 0x3F2000..0x3F2C28.  The firmware itself nominates exactly this
window as crash-dump region 1 ({src 0x4003A000, len 0xC28}) [V].

Image writer census (whole image) [V]:
  * pcie_msg_init 0x49334 writes only: DR group A base/depth/producer/enable,
    SR group B base/depth/published/enable, control blocks 0x4003A540/+4/+8
    and 0x4003A720/+4/+8, and status/control base 0x4003A86C (mask
    0xE0E0F8F8 + bit clears).
  * the two dispatchers 0x4624C / 0x462F8 write only the ack word 0x4003A870.
  * engine global +0x000..+0x3FF, doors +0x7C0/+0x7D0, status word +0x874,
    send words +0xC00.. and fills +0x3C: NO image materialisation at all
    (literal-load + movw/movt scan; the ete-engine lane's independent scan
    agrees).
  * the only static file bytes in the ETE window are the 31+31 {CA,value}
    PHY/MAC tables at CPU 0x1058B8/0x1059B0 - both touch only 0x401105xx /
    0x401145xx. [V]
  * new here: the doors ARE present in image static data, as CA fields paired
    with the two control blocks: {CPU 0x10CAC8: 0x4003A540, 0x10CACC:
    0x4003A7C0} and {0x10CB90: 0x4003A720, 0x10CB94: 0x4003A7D0}.  No image
    code loads those field addresses (literal scan), and the surrounding
    objects are used only through their lock/queue words 0x10CAD0/
    0x10CAD4 by the ETE dispatchers and pcie_msg_init.  The pairing is
    evidence that door0/door1 are the companion registers of engine blocks
    0/1 - not that the image ever writes them. [V]

Vendor host census [V]:
  * 265 vendor modules scanned: only hi5622v100_plat.ko contains ETE CAs, and
    only {0x4003A000, 0x40039508}; no module contains any door/global/send CA.
  * pcie_ete_sending_trigger is exported (ksymtab) but has NO direct BL caller
    and no module imports the symbol in this build; the registered
    pcie_trigger_ete_sending_handle symbol (plat 0x15EFC) is a 4-byte
    self-branch BUG stub (bytes FE FF FF EA).  There is no hidden host ETE
    start write to copy. [V]

Door observation and decode attempt:
  * D1 read: CA 0x4003A7C0 = 0x8CAC4ECA, CA 0x4003A7D0 = 0x00000000 [V*].
  * not a ring index under send-word packing (hi16 0x8CAC = 36012 >> depth
    32; lo16 0x4ECA = 20170).  It is in the device->host DMA window range
    0x80000000.. but is not buffer-aligned, so a DMA pointer read is not
    convincing either.  It may be a hardware/ROM token or residual state; the
    image and vendor host never author it [V negative].  No sane value can be
    derived from the image: only the stock-vendor golden diff (pack row C on
    a stock boot) names it. [U]

Crash-dump snapshot - the decisive free read [V code]:
  exception handler 0xC6E14 walks the descriptor table at CPU 0x10F294 and
  copies, in order, to CA 0x010DB800 with end limit 0x010DFFFF:
      region 0: CA 0x001BD000 len 0x2000 -> CA 0x010DB800 (boot-log arena)
      region 1: CA 0x4003A000 len 0xC28  -> CA 0x010DD800 (ETE window)
      region 2: CA 0x40039000 len 0xD5C  -> CA 0x010DE428 (PCIe glue)
  PKTRAM is region 2: BAR0 = 0x1D8000 + (CA - 0x01000000), so the ETE snapshot
  is at BAR0 0x2B5800..0x2B6428, the log copy at 0x2B3800, the glue copy at
  0x2B6428.  The descriptor cursor words (CPU 0x10F29C/0x10F2A8/0x10F2B4) are
  set to the current destination after each successful memcpy, so a nonzero
  cursor proves the copy for that region ran.
  => on any boot that has taken an SVC/data-abort exception (ours: mask 0x0A)
     the firmware has captured the engine-global + doors + status + send
     words into host-readable PKTRAM without any host action. [I]

What could wake the ETE status layer:
  * The status word 0x4003A874 is hardware-authored; the firmware's only ETE
    kick (sending trigger 0xC625A) is gated on its bits 16-19 (DR) and is
    invoked only by the ETE status dispatchers, which need the ROM-vectored
    event path (0x40064 -> 0xC2EFC).  Nothing the host writes appears in that
    loop (previous lane's whole-image/whole-vendor-host negative stands).
  * The only remaining device-side lever of the image is the ROM bring-up of
    the engine global/door state (unreadable from the image) and the ROM IRQ
    vector (readable only as CA 0x34/0x38).  Both are in the pack.
""")
    return 0


def cmd_pack(b):
    print("=" * 78)
    print("READ-ONLY discriminating bench pack (no writes, no builds)")
    print("=" * 78)
    print("""
Rule reminders: UART read-only; never write CA 0x40160110 (EOI),
CA 0x400392F0 (out[5]), any handler slot or any firmware RAM word.  BAR0
below is the endpoint BAR0 offset; phys = 0x40000000 + BAR0.  All rows reads.
PKTRAM rows are read at BAR0 0x1D8000 + (CA-0x01000000).

--- A. Hybrid boot: handshake page sampled over time (one fresh boot) -------
  #  CA          rule              BAR0      phys
  A1 0x1BF000    CPU+0x6B8000      0x877000  0x40877000  t0,t1,t2,t3
  A2 0x1BF004    CPU+0x6B8000      0x877004  0x40877004  t0,t1,t2,t3
  A3 0x1BF008    CPU+0x6B8000      0x877008  0x40877008  t0,t1,t2,t3
  A4 0x1BF00C    CPU+0x6B8000      0x87700C  0x4087700C  t0,t1,t2,t3
     t0 = right after the release step, then ~1 s, ~5 s, ~20 s.
     D1 measured 1BD000 / 1BD0D8; golden vendor = 2 / 0xABCD.
     Discriminates: {x,y} ROM values = S1 never executed; {0,0xABCD} = entry A
     spinning for the partner; {2,0xABCD} = handshake complete.  A1 bits:
     bit5/6 => entry A ran its first actions; bit13 only => spinning;
     bit14 => success (and the scheduler call follows).
  A5 0x1BD000..0x1BD0FF  CPU+0x6B8000  0x7D3000  0x407D3000  64 words
     the 216-byte ROM boot-log; confirms whether A3/A4 still point into it.
  A6 0x17D398 +0x00/+0x74  BAR0 0x835398/0x83540C: expect 0x60 / 1.
  A7 0x17D430[0x1D,0x3E,0x3F,0x40,0x42] BAR0 0x8354D4/0x835528/0x83552C/
     0x835530/0x835538: 0xC6C89 / 0x46849 / 0x46849 / 0x40671 / 0x4065D.
  A8 0x117C00+0xEE0..0xEEC  BAR0 0x7D0AE0..0x7D0AEC (entry A pump block).
  A9 0x118B60 state / 0x17D42C boot slot / 0x40008 mask: BAR0 0x7D0B60 /
     0x83542C / 0x6F8008.
  A10 0x17DF78 flags BAR0 0x835F78 (expect 0x0F if the exception handler ran).

--- B. Crash-dump (PKTRAM) - the firmware's own ETE-window snapshot ----------
  B1 0x10F29C / 0x10F2A8 / 0x10F2B4  BAR0 0x7C729C/A8/B4 (region 5)
     descriptor cursors: nonzero == that region memcpy ran; values are the
     destination CAs 0x010DB800 / 0x010DD800 / 0x010DE428.
  B2 CA 0x010DD800 len 0xC28  BAR0 0x2B5800  -> ETE window at exception time
  B3 CA 0x010DB800 len 0x2000 BAR0 0x2B3800  -> boot-log arena copy
  B4 CA 0x010DE428 len 0xD5C  BAR0 0x2B6428  -> PCIe glue block copy

--- C. ETE window live (region 3) + golden diff ------------------------------
  C1 full window CA 0x4003A000 len 0xC28 -> BAR0 0x3F2000
  C2 engine global CA 0x4003A000..0x4003A3FC -> BAR0 0x3F2000..0x3F23FC
  C3 doors CA 0x4003A7C0 / 0x4003A7D0 -> BAR0 0x3F27C0 / 0x3F27D0
     (D1 = 0x8CAC4ECA / 0)
  C4 status+ack CA 0x4003A874 / 0x4003A870 -> BAR0 0x3F2874 / 0x3F2870
  C5 control blocks CA 0x4003A540/44/48 and 0x4003A720/24/28
  C6 channels/send/fills: 0x4003A590/+0x38/+0x3C DR0-3, SR0-2,
     0x4003AC00..0x4003AC20 send words, 0x4003A86C mask.
  GOLDEN: run C1-C6 on a stock 5.10 vendor boot with the vendor stack up and
  Wi-Fi working; diff against ours (and B2 if ours took an exception).  Every
  differing global/door word is a ROM-authored ETE state candidate; if the
  doors/global match, the engine-global hypothesis is falsified and the gap is
  event delivery only.

--- D. ROM vector window (region-0 identity: BAR0 == CPU) --------------------
  D1 CA 0x20,0x24,0x28,0x2C,0x30,0x34,0x38,0x3C = BAR0/phys same.
     8 words never read: the ROM vector targets.  CA 0x34/0x38 = IRQ/FIQ.
     Discriminator: 0x40064/0x40065 => ROM vectors IRQ into the image
     trampoline (delivery wired); a value < 0x40000 => ROM-internal IRQ path.
     Also expect one of them == 0x50 (the double-indirect stub whose [0x1E4]
     pointer we already read).
  D2 CA 0x1E0..0x1FF (8 words): ROM handler-slot pointer table around 0x1E4
     (= 0x4005C); entries looking like 0x40050/54/58/5C/60 would prove the ROM
     maps its exception classes directly onto the image header table.
  D3 CA 0x0..0x1C, 0x1E4 re-read for the record.

--- E. Golden stock-boot set (run only on a stock vendor system) -------------
  E1 A3/A4/A1 (handshake page) -> golden reference 2 / 0xABCD.
  E2 A5, A6, A7, A9 (same state words as ours).
  E3 C1-C6 (ETE window live while the vendor stack works).
  E4 D1/D2 (ROM vector targets on stock).
  E5 B1 cursors: on stock mask=0 means no exception -> dump absent (cursors
     0/0/0); that is itself a differential datum.

--- F. DO NOT READ / CANNOT READ (corrections) -------------------------------
  * CA 0x4016xxxx and 0x4018xxxx (event controllers) are OUTSIDE all six
    inbound regions.  BAR0 0x51810C is in region 4 and decodes ACP CA
    0x0204010C - the D1 pack's ev_status/ev_ack/ev_raiseA/ev_raiseB reads (all
    0) are meaningless aliases, not event-register values.  Do not repeat.
  * CA 0x40039508 (host intr word, region 3, BAR0 0x3F1508) IS readable; it is
    host-owned and not part of the ROM contract.
  * CA 0x1BF008 writes: only a Lead-approved diagnostic exists (write 2 to
    fake the partner reply; prior lane phase 3).  Not part of this pack.
""")
    return 0


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", nargs="?", default="all",
                    choices=["selftest", "handshake", "rom", "engine", "pack",
                             "all"])
    a = ap.parse_args()
    b = Blob()
    rc = 0
    if a.cmd in ("selftest", "all"):
        rc |= selftest(b)
        print()
    if a.cmd in ("handshake", "all"):
        cmd_handshake(b)
    if a.cmd in ("rom", "all"):
        cmd_rom(b)
    if a.cmd in ("engine", "all"):
        cmd_engine(b)
    if a.cmd in ("pack", "all"):
        cmd_pack(b)
    return rc


if __name__ == "__main__":
    sys.exit(main())
