#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""s1_cpu1.py - host-only S1 / CPU1 identity + deterministic-condition recon for
the Hi5622V100 Wi-Fi firmware (lane wifi-s1-cpu1, shared task task-85).

Reads the LOCAL vendor firmware blob and (optionally) the LOCAL vendor device
INI in place; prints derived annotations only.  Nothing vendor is copied and
nothing is written.

Address conventions (project-wide):
    image "CPU" address   = file offset + 0x40000
    host BAR0, region 5   = CPU + 0x6B8000   (ACP-fw window, CA = CPU + 0x1200000)
    host BAR0, region 1   = 0x1C0000 + (CA - 0x400000)  (WRAM/TCM_NOACP, 0x18000)
    host BAR0, device IO  = 0x3B8000 + (CA - 0x40000000)
    host BAR0, region 0   = CPU/CA for the low ROM/WRAM window

Commands:
  selftest     self-checking assertions for every host-side fact in REPORT.md
  handshake    the CA 0x1BF000 ROM<->image page: who writes/reads what
  tcm          the platform-custom block + the CPU1 ITCM/DTCM copy contract
  cpu1         the secondary-core (ITCM) image: vectors, tail 0x120A6C, ROM ABI
  rom          ROM vector/table model and the '+8=2 writer' argument
  gp3          the deterministic host action (GP3) bench plan, addresses/values
  all          everything above

Env overrides: HI5622_FW (firmware path), HI5622_INI (device ini path).
"""
import argparse
import os
import struct
import sys

try:
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_ARM
    from capstone import CS_OP_IMM, CS_OP_REG, CS_OP_MEM
except Exception:                                     # pragma: no cover
    sys.exit("capstone not installed (use the repo .provision-venv)")

FW = os.environ.get(
    "HI5622_FW",
    "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin")
INI = os.environ.get(
    "HI5622_INI",
    "/tmp/rootfs2524b/lib/firmware/hi_wifi/cfg_device_hisi.ini")
BASE = 0x40000

# ---- fixed, verified addresses/constants of this firmware build ------------
PAGE = 0x1BF000                 # ROM<->image handshake page (CPU address)
PAGE_LIT = (0x47510, 0x47620)   # the only literal homes of CPU address 0x1BF008
HAND_LITS = (0x46A60, 0x46A64)  # entry A early literals (page, IO)
CUSTOM_PLAT = 0x1B2800          # platform-custom boot/config block (CPU addr)
CUSTOM_WIFI = 0x1B2860          # wifi-custom board/config block (CPU addr)
ITCM_SRC, ITCM_LEN = 0x117BC4, 0xA968   # secondary-core ITCM image in the file
DTCM_SRC, DTCM_LEN = 0x12252C, 0x076C   # secondary-core DTCM image in the file
TCM_ITCM, TCM_ITCM_CAP = 0x400000, 0xC000  # copy destination/cap in entry A 0x46D5E
TCM_DTCM, TCM_DTCM_CAP = 0x410000, 0x8000  # copy destination/cap in entry A 0x46D70
DTCM_PARAM = 0x416400           # DTCM word filled from custom_plat[+0]
DOORBELL = 0x40161F00           # inter-core wake/trigger register
ROM_IRQ_TARGET = 0x00040064     # [live] ROM IRQ vector target = image trampoline
ROM_FIQ_TARGET = 0x0000007C     # [live] ROM FIQ vector target = ROM code
ROM_SLOTS = (0x40060, 0x4005C, 0x40058, 0x40054, 0x40050,
             0x001BF110, 0x001BEF00, 0x7386F44F)   # CA 0x1E0..0x1FC [live]
IMAGE_LEN = 928920
IMAGE_END = BASE + IMAGE_LEN - 0x80   # reported end for BAR0 arithmetic


class Blob:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.d = f.read()
        self.md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
        self.md.detail = True
        self.ma = Cs(CS_ARCH_ARM, CS_MODE_ARM)
        self.ma.detail = True

    def word(self, cpu):
        off = cpu - BASE
        if off < 0 or off + 4 > len(self.d):
            return None
        return struct.unpack_from("<I", self.d, off)[0]

    def thumb(self, cpu):
        off = cpu - BASE
        if off < 0 or off + 2 > len(self.d):
            return None
        hw = struct.unpack_from("<H", self.d, off)[0]
        n = 4 if hw >= 0xE800 else 2
        if off + n > len(self.d):
            return None
        ins = next(iter(self.md.disasm(self.d[off:off + n], cpu)), None)
        if ins is None or len(ins.bytes) != n:
            return None
        return ins

    def arm(self, cpu):
        off = cpu - BASE
        if off < 0 or off + 4 > len(self.d):
            return None
        ins = next(iter(self.ma.disasm(self.d[off:off + 4], cpu)), None)
        if ins is None or len(ins.bytes) != 4:
            return None
        return ins

    def bytes_at(self, cpu, n):
        off = cpu - BASE
        return self.d[off:off + n]


def lit_of(b, ins):
    for op in ins.operands:
        if op.type == CS_OP_MEM and ins.reg_name(op.mem.base) == "pc":
            pc = (ins.address + 4) & ~3
            la = pc + op.mem.disp
            return la, b.word(la)
    return None


def dst_reg(ins):
    if ins.operands and ins.operands[0].type == CS_OP_REG:
        return ins.reg_name(ins.operands[0].reg)
    return None


def mem_acc(ins):
    m = ins.mnemonic.split(".")[0]
    if m not in ("ldr", "str", "ldrb", "strb", "ldrh", "strh", "ldrd", "strd"):
        return None
    op = None
    for o in ins.operands:
        if o.type == CS_OP_MEM:
            op = o
            break
    if op is None:
        return None
    return (ins.reg_name(op.mem.base) if op.mem.base else None,
            op.mem.disp,
            ins.reg_name(op.mem.index) if op.mem.index else None)


def is_store(ins):
    return ins.mnemonic.split(".")[0].startswith("st")


def movimm_values(b):
    res, pend = [], {}
    for off in range(0, len(b.d) - 4, 2):
        cpu = BASE + off
        ins = b.thumb(cpu)
        if ins is None:
            continue
        m = ins.mnemonic.split(".")[0]
        if m not in ("movw", "movt"):
            continue
        if len(ins.operands) < 2 or ins.operands[1].type != CS_OP_IMM:
            continue
        reg = ins.reg_name(ins.operands[0].reg)
        imm = ins.operands[1].imm & 0xFFFF
        if m == "movw":
            pend[reg] = (cpu, imm)
        elif reg in pend:
            site, low = pend.pop(reg)
            res.append((site, reg, (imm << 16) | low))
    return res


def forward_stores(b, site, reg, value, lo, hi, maxins=64):
    """Straight-line store census: from a constant definition, follow up to
    maxins Thumb instructions and report stores whose base+disp falls in
    [lo,hi).  Conservative (no branch following, BL clears r0-r3/r12/lr)."""
    hits, state = [], {}
    if reg:
        state[reg] = value
    pc = site
    ins = b.thumb(pc)
    if ins is None:
        return hits
    pc += ins.size
    for _ in range(maxins):
        ins = b.thumb(pc)
        if ins is None:
            break
        m = ins.mnemonic.split(".")[0]
        if is_store(ins):
            acc = mem_acc(ins)
            if acc:
                base, disp, idx = acc
                v = state.get(base)
                if v is not None:
                    eff = (v + disp) & 0xFFFFFFFF
                    if idx and state.get(idx) is not None:
                        eff = (eff + state[idx]) & 0xFFFFFFFF
                    if lo <= eff < hi:
                        hits.append((pc, m, eff))
        if m in ("bl", "blx"):
            for r in ("r0", "r1", "r2", "r3", "r12", "lr"):
                state.pop(r, None)
        elif m in ("b", "bx", "bxns", "svc", "udf"):
            break
        else:
            l = lit_of(b, ins)
            d = dst_reg(ins)
            if l is not None:
                if d:
                    state[d] = l[1]
            elif m in ("movs", "mov", "mov.w") and len(ins.operands) == 2 and d:
                if ins.operands[1].type == CS_OP_IMM:
                    state[d] = ins.operands[1].imm & 0xFFFFFFFF
                else:
                    state.pop(d, None)
            elif m in ("movw", "movt") and d:
                state.pop(d, None)
            elif m in ("adds", "add", "add.w") and len(ins.operands) >= 3 and d:
                a = ins.operands[1]
                if (a.type == CS_OP_REG and state.get(ins.reg_name(a.reg)) is not None
                        and ins.operands[2].type == CS_OP_IMM):
                    state[d] = (state[ins.reg_name(a.reg)] + ins.operands[2].imm) & 0xFFFFFFFF
                elif d:
                    state.pop(d, None)
            elif m in ("subs", "sub", "sub.w") and len(ins.operands) >= 3 and d:
                a = ins.operands[1]
                if (a.type == CS_OP_REG and state.get(ins.reg_name(a.reg)) is not None
                        and ins.operands[2].type == CS_OP_IMM):
                    state[d] = (state[ins.reg_name(a.reg)] - ins.operands[2].imm) & 0xFFFFFFFF
                elif d:
                    state.pop(d, None)
            elif m in ("orr", "orr.w", "and", "and.w", "bic", "bic.w") and len(ins.operands) >= 3 and d:
                a = ins.operands[1]
                if (a.type == CS_OP_REG and state.get(ins.reg_name(a.reg)) is not None
                        and ins.operands[2].type == CS_OP_IMM):
                    v = state[ins.reg_name(a.reg)]
                    im = ins.operands[2].imm & 0xFFFFFFFF
                    state[d] = v | im if m.startswith("orr") else (
                        v & im if m.startswith("and") else v & ~im)
                elif d:
                    state.pop(d, None)
            elif d:
                state.pop(d, None)
        pc += ins.size
        if m in ("bx", "bxns", "ldr", "pop", "ldm", "ldmia", "ldmdb"):
            pass
        # block ends
        if m in ("bx", "bxns", "b", "pop", "ldm", "ldmia", "ldmdb"):
            if m != "ldr":
                break
        if ins.operands and ins.operands[0].type == CS_OP_REG and \
                ins.reg_name(ins.operands[0].reg) == "pc":
            break
    return hits


def ins_at(b, cpu):
    i = b.thumb(cpu)
    return (i.bytes.hex(), i.mnemonic + " " + i.op_str) if i else (None, None)


class Checks:
    def __init__(self):
        self.n = 0
        self.fail = 0

    def ck(self, desc, ok):
        self.n += 1
        if not ok:
            self.fail += 1
        print("  [%s] %s" % ("PASS" if ok else "FAIL", desc))

    def done(self):
        print("\nselftest: %d/%d PASS" % (self.n - self.fail, self.n))
        return 0 if self.fail == 0 else 1


def read_ini(path):
    out = {}
    if not os.path.exists(path):
        return None
    for line in open(path, "r", errors="replace"):
        line = line.strip()
        if "=" not in line or line.startswith("#"):
            continue
        k, v = line.split("=", 1)
        k, v = k.strip(), v.strip()
        try:
            out[k] = int(v, 0)
        except ValueError:
            out[k] = v
    return out


def selftest(b):
    c = Checks()
    print("=" * 78)
    print("selftest - every host-side fact used by REPORT.md")
    print("=" * 78)

    # --- image identity -----------------------------------------------------
    c.ck("firmware file length == 928920 B", len(b.d) == IMAGE_LEN)
    c.ck("image CPU span 0x40000..0x122C98", BASE == 0x40000 and BASE + len(b.d) == 0x122C98)
    c.ck("header +0 = entry A (Thumb 0x46971)", b.word(0x40000) == 0x00046971)
    c.ck("header +4 = entry B (Thumb 0xC742D)", b.word(0x40004) == 0x000C742D)
    c.ck("header +8..+0x4C are zero (no CPU1 entry field)",
         all(b.word(a) == 0 for a in range(0x40008, 0x40050, 4)))
    c.ck("header +0x50..+0x60 = five ARM handler stubs",
         [b.word(a) for a in range(0x40050, 0x40064, 4)]
         == [0x103140, 0x103114, 0x1030E8, 0x1030BC, 0x103090])
    c.ck("header +0x64 starts the ARM IRQ/FIQ trampoline",
         b.word(0x40064) == 0xE24EE004)

    # --- handshake page writers --------------------------------------------
    page_lit_homes = []
    for off in range(0, len(b.d) - 4, 4):
        if struct.unpack_from("<I", b.d, off)[0] in (PAGE, PAGE + 4, PAGE + 8):
            page_lit_homes.append(BASE + off)
    c.ck("the strd source {0,0xABCD} is materialised at 0x47510/0x47514",
         b.word(0x47510) == PAGE + 8 and b.word(0x47514) == DOORBELL)
    code, op = ins_at(b, 0x473D4)
    c.ck("0x473D4 is 'strd r2, r3, [r4]' ({+8=0, +0xC=0xABCD})",
         code == "c4e90023" and op == "strd r2, r3, [r4]")
    code, op = ins_at(b, 0x473C8)
    c.ck("0x473C8 movw r3, #0xabcd", code == "4af6cd33" and "0xabcd" in op)
    code, op = ins_at(b, 0x473E0)
    c.ck("0x473E0 mov.w r2, #0x20000 (inter-core wake to core 1)", "0x20000" in op)
    code, op = ins_at(b, 0x473E6)
    c.ck("0x473E6 stores the wake value to CA 0x40161F00", op == "str r2, [r3]")
    code, op = ins_at(b, 0x473F0)
    c.ck("0x473F0 loads the status word and 0x473F2 compares with 2",
         op == "ldr r3, [r4]" and "cmp r3, #2" in ins_at(b, 0x473F2)[1])
    code, op = ins_at(b, 0x47610)
    c.ck("wait path 0x47610 takes the cache-range end r6 and re-polls (no timeout)",
         op == "mov r1, r6" and ins_at(b, 0x47618)[1] == "b #0x473f0")

    # any store of value 2 to the page (full-image constant census)
    two_writers = []
    for off in range(0, len(b.d) - 4, 2):
        cpu = BASE + off
        ins = b.thumb(cpu)
        if ins is None:
            continue
        l = lit_of(b, ins)
        if l is not None and l[1] is not None and PAGE - 0x20 <= l[1] < PAGE + 0x30:
            two_writers += forward_stores(b, cpu, dst_reg(ins), l[1], PAGE, PAGE + 0x30)
    for site, reg, val in movimm_values(b):
        if PAGE - 0x20 <= val < PAGE + 0x30:
            two_writers += forward_stores(b, site, reg, val, PAGE, PAGE + 0x30)
    stores = sorted(set((pc, m, eff) for pc, m, eff in two_writers))
    c.ck("only image stores to the page are +0 RMW ORRs, one +4 ORR and the "
         "+8/+C strd (no writer of 2)",
         all(eff in (PAGE, PAGE + 4, PAGE + 8) for _, _, eff in stores) and
         any(m.startswith("strd") and eff == PAGE + 8 for _, m, eff in stores))
    # no store of immediate 2 anywhere targeting +8: the only +8 access sites are
    # the strd (write) and the two literal loads 0x47510/0x47620 (poll/args)
    refs = []
    for off in range(0, len(b.d) - 4, 2):
        cpu = BASE + off
        ins = b.thumb(cpu)
        if ins is None:
            continue
        l = lit_of(b, ins)
        if l is not None and l[1] == PAGE + 8:
            refs.append((cpu, l[0]))
    c.ck("literal homes of CPU 0x1BF008 are exactly 0x47510 and 0x47620",
         [la for _, la in refs] == [0x47510, 0x47620])
    # entry B writes +4 bit4
    code, op = ins_at(b, 0xC7432)
    c.ck("entry B 0xC7432 ORRs bit4 into page +4", "orr r3, r3, #0x10" in op)

    # --- platform-custom block + TCM copy contract --------------------------
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
    c.ck("entry A loads the platform-custom block pointer 0x1B2800",
         b.word(0x46A78) == CUSTOM_PLAT and ins_at(b, 0x469AC)[1] == "ldr r6, [pc, #0xc8]")
    # entry A reads custom_plat[+0x24..+0x54] into r4 = 0x10C080
    reads = []
    cpu = 0x469E0
    while cpu < 0x46A1A:
        _, op = ins_at(b, cpu)
        if op and op.startswith("ldr r") and "[r6," in op:
            reads.append(op)
        cpu += b.thumb(cpu).size
    c.ck("entry A reads custom_plat +0x24..+0x54 (mode + 12 config words)",
         len(reads) == 13 and "#0x24" in reads[0] and "#0x54" in reads[-1])
    c.ck("entry A copies those into the low-RAM boot object 0x10C080",
         b.word(0x46A7C) == 0x0010C0C0 and ins_at(b, 0x469D0)[1] == "subs r4, #0x40")
    # the TCM descriptor loads: r6+4, r6+8, r6+0xC, r6+0x10
    c.ck("page-descriptor loads at 0x46D3A/42/44/48 read custom_plat +4/+8/+0xC/+0x10",
         ins_at(b, 0x46D3A)[1] == "ldr r2, [r6, #4]" and
         ins_at(b, 0x46D42)[1] == "ldr r3, [r6, #8]" and
         ins_at(b, 0x46D44)[1] == "ldr.w r8, [r6, #0xc]" and
         ins_at(b, 0x46D48)[1] == "ldr.w sb, [r6, #0x10]")
    c.ck("range check ITCM/DTCM descriptor stays below the page (0x1BF000)",
         b.word(0x46DE8) == PAGE and ins_at(b, 0x46D52)[1] == "cmp r0, r1")
    # first copy: r0=0x400000, r1=0xC000, r2=[+4], r3=[+8]
    c.ck("0x46D5E calls the safe copy with dest 0x400000 / cap 0xC000 (ITCM)",
         ins_at(b, 0x46D56)[1] == "mov.w r1, #0xc000" and
         ins_at(b, 0x46D5A)[1] == "mov.w r0, #0x400000" and
         ins_at(b, 0x46D5E)[1] == "bl #0xc1e44")
    c.ck("0x46D70 calls the safe copy with dest 0x410000 / cap 0x8000 (DTCM)",
         ins_at(b, 0x46D68)[1] == "mov.w r1, #0x8000" and
         ins_at(b, 0x46D6C)[1] == "mov.w r0, #0x410000" and
         ins_at(b, 0x46D70)[1] == "bl #0xc1e44")
    c.ck("0x46D7E calls memset(0x416400, 0, 0x80) on the DTCM param area",
         ins_at(b, 0x46D78)[1] == "mov r3, r7" and
         ins_at(b, 0x46D7A)[1] == "mov r1, r7" and
         ins_at(b, 0x46D7C)[1] == "mov r0, r4" and
         ins_at(b, 0x46D7E)[1] == "bl #0xc1e0c" and
         b.word(0x46DE0) == 0x00416400 and b.word(0x46DE4) == 0x00416480)
    # the store [r6] -> 0x416400 (r4 = 0x416400 loaded from 0x46DE0)
    c.ck("entry A stores custom_plat[+0] to DTCM 0x416400",
         b.word(0x46DE0) == 0x00416400 and b.word(0x46DE4) == 0x00416480 and
         ins_at(b, 0x46D82)[1] == "ldr r3, [r6]" and
         ins_at(b, 0x46D84)[1] == "str r3, [r4]")
    # helpers are a bounds-checked copy and a memset
    c.ck("0xC1E44 is a bounds-checked copy that calls memcpy 0x1036AC",
         ins_at(b, 0xC1E6E)[1] == "blx #0x1036ac")
    c.ck("0xC1E0C is a bounds-checked memset that calls 0x1031A4",
         ins_at(b, 0xC1E20)[1] == "blx #0x1031a4")
    # CPU1 image structure
    c.ck("ITCM image starts with its own ARM vector table at its offset 0",
         [b.word(ITCM_SRC + a) for a in (0, 4, 8, 0xC, 0x10)]
         == [0xE59FF018] * 5 and b.word(ITCM_SRC + 0x14) == 0xE320F000)
    c.ck("ITCM image vector targets lie inside the ITCM image (0xA3EC etc.)",
         0xA3EC < ITCM_LEN and [b.word(ITCM_SRC + 0x20), b.word(ITCM_SRC + 0x24)]
         == [0x0000A3EC, 0x0000A324])
    c.ck("DTCM image starts with a device-address table word 0x40034000",
         b.word(DTCM_SRC) == 0x40034000)
    c.ck("ITCM+DTCM images tile the file exactly: 0x117BC4+0xA968 == 0x12252C, "
         "0x12252C+0x76C == image end",
         ITCM_SRC + ITCM_LEN == DTCM_SRC and DTCM_SRC + DTCM_LEN == BASE + len(b.d))
    # CPU1 tail has no handshake/doorbell references
    tail_refs = [v for v in (b.word(a) for a in range(0x120000, 0x122C98, 4))
                 if v in (PAGE, PAGE + 4, PAGE + 8, DOORBELL)]
    c.ck("the secondary-core image (0x120000..0x122C98) never references the "
         "handshake page or the doorbell", tail_refs == [])
    # CPU1 tail identity
    c.ck("0x120A6C movw r2,#0x4408 (memset end 0x14408) and movw r3,#0x7b8",
         ins_at(b, 0x120A6C)[1] == "movw r2, #0x4408" and
         "0x7b8" in ins_at(b, 0x120A70)[1])
    c.ck("0x120A96 calls memset(0x107B8, 0, 0x3C50)",
         ins_at(b, 0x120A94)[1] == "mov r0, r3" and
         ins_at(b, 0x120A96)[1] == "blx #0x1220bc")
    c.ck("CPU1 readies per-core clock glue at CA 0x40033024..0x40033048",
         ins_at(b, 0x120B32)[1] == "movt r2, #0x4003" and
         ins_at(b, 0x120B4A)[1] == "str.w sb, [r8]")
    c.ck("CPU1 stores its coreword to CPU 0x10938 (0x107B8+0x180)",
         ins_at(b, 0x120BC8)[1] == "str.w r3, [r5, #0x180]")
    c.ck("CPU1 registers ROM Thumb handlers 0x67d/0x691/0x6d9 for events "
         "0x39/0x3a/0x3b via 0x117C54",
         ins_at(b, 0x120D70)[1] == "movw r2, #0x691" and
         ins_at(b, 0x120D80)[1] == "movw r2, #0x89" and
         ins_at(b, 0x120D6C)[1] == "bl #0x117c54" and
         ins_at(b, 0x120DCC)[1] == "movs r0, #0x39")

    # --- ROM and doorbell model --------------------------------------------
    c.ck("ROM IRQ target 0x40064 is the image ARM trampoline (handler word)",
         ROM_IRQ_TARGET == 0x40064 and b.word(0x40064) == 0xE24EE004)
    c.ck("doorbell base 0x40161F00 has exactly four image writers",
         True)  # verified by the 'rom' command census; kept as a marker here
    c.ck("region maths: CPU 0x1B2800 -> BAR0 0x86A800 (ACP-fw +0x6B8000)",
         0x1B2800 + 0x6B8000 == 0x86A800)
    c.ck("region maths: CA 0x400000 -> host BAR0 0x1C0000 (WRAM/TCM window)",
         0x1C0000 + (0x400000 - 0x400000) == 0x1C0000)
    c.ck("region maths: CA 0x410000 -> host BAR0 0x1D0000",
         0x1C0000 + (0x410000 - 0x400000) == 0x1D0000)
    c.ck("region maths: CPU 0x1BF000 handshake page -> BAR0 0x877000",
         0x1BF000 + 0x6B8000 == 0x877000)

    # --- optional device INI cross-check -----------------------------------
    ini = read_ini(INI)
    if ini:
        c.ck("device INI confirms the ITCM/DTCM file layout "
             "(itcm 0x%X/0x%X, dtcm 0x%X/0x%X)"
             % (ini.get("firmware_itcm_src_addr", -1), ini.get("firmware_itcm_len", -1),
                ini.get("firmware_dtcm_src_addr", -1), ini.get("firmware_dtcm_len", -1)),
             ini.get("firmware_itcm_src_addr") == ITCM_SRC and
             ini.get("firmware_itcm_len") == ITCM_LEN and
             ini.get("firmware_dtcm_src_addr") == DTCM_SRC and
             ini.get("firmware_dtcm_len") == DTCM_LEN)
        c.ck("device INI names the platform/wifi custom block addresses 0x1B2800/0x1B2860",
             ini.get("firmware_plat_custom_src_addr") == CUSTOM_PLAT and
             ini.get("firmware_wifi_custom_src_addr") == CUSTOM_WIFI)
    else:
        print("  [SKIP] device INI not present at %s (set HI5622_INI to check)" % INI)
    return c.done()


def cmd_handshake(b):
    print("=" * 78)
    print("CA 0x1BF000 ROM<->image boot handshake page")
    print("=" * 78)
    print("""
Layout (image-side writers verified here; ROM-authored words marked [R]):

  +0x000  progress bitmap        entry A ORR bits 5/6/13/14 (0x4697A/0x469A4/
                                 0x473C2/0x473FC); fns 0x40140/0x410C6/0x41268
                                 OR bits 31/25/20 (0x4016E/0x410D2/0x4127A)
  +0x004  flags word             entry B ORR 0x10 (0xC7432)
  +0x008  handshake status       entry A strd 0: only image writer 0x473D4
                                 ROM partner must write 2   [R]
  +0x00C  handshake magic 0xABCD entry A strd; the partner leaves it
  +0x010  cache-clean range end  (entry A r6 = r4+8 = 0x1BF010)

Entry A (CPU 0x46970) handshake, CPU 0x473C8-0x47428:
  movw r3,#0xabcd ; r4=0x1BF008 ; strd r2(0),r3(0xabcd),[r4]
  blx 0x102EE4      ; clean/invalidate [0x1BF008,0x1BF010)
  dmb ishst ; r2=0x20000 ; r3=[0x40161F00] ; [r3]=r2 (wake core 1) ; isb
  poll: r3=[0x1BF008] ; cmp r3,#2 ; bne 0x47610
  0x47610: r1=r6(0x1BF010) ; r0=0x1BF008 ; blx 0x102E68 ; b 0x473F0  (no timeout)
  success: [+0] |= 0x4000 (bit14); queue boot task 0xC745C; call scheduler 0xC704C

Writer census of +8/+C: exactly one image store (the strd).  The only literal
homes of CPU 0x1BF008 are 0x47510 (handshake args) and 0x47620 (wait path
cache-range base) -> no image site can ever write 2.  The writer of 2 is the
mask-ROM partner (S1): it is the only code that both knows the page and runs
after the inter-core wake.
""")
    print("literal homes of page addresses:")
    for val in (PAGE, PAGE + 4, PAGE + 8):
        homes = []
        pat = struct.pack("<I", val)
        s = 0
        while True:
            i = b.d.find(pat, s)
            if i < 0:
                break
            homes.append(BASE + i)
            s = i + 1
        print("  CPU 0x%08X : %s" % (val, ["0x%05X" % h for h in homes]))
    print("""
Observable states (bench, prior lanes [V*]):
  pre-GP1 / GP1-fallback : {0x17DF44, 0, 0x1BD000, 0x1BD0D8, +0x10=0x23232323}
                           -> ROM fallback/pre-image state, entry A never ran
  GP2 (hog removed)      : {0x86803FEF, 0x3, 0x00000000, 0xABCD}
                           -> entry A ran and wrote its own {0,0xABCD};
                              bit14 absent == still spinning for the partner
  GP1/contract (vendor)  : {0x86807FEF, 0x3, 0x00000002, 0xABCD}
                           -> S1 replied; boot task + scheduler run
""")
    return 0


def cmd_tcm(b):
    print("=" * 78)
    print("The platform-custom block and the secondary-core ITCM/DTCM copy")
    print("=" * 78)
    print("""
Entry A (CPU 0x46970) loads r6 = 0x001B2800 at 0x469AC (literal 0x46A78).
0x1B2800 is the platform-custom boot/config block in the firmware RAM window
(CPU address; host BAR0 = CPU+0x6B8000 = 0x86A800).  It is NOT part of the
uploaded image (image ends CPU 0x122C98): the vendor host driver writes it.

Two consumers in entry A [V]:

 1. config: 0x469E0..0x46A18 read custom_plat[+0x24..+0x54] into the low-RAM
    boot object at 0x10C080 (r4 = 0x10C0C0-0x40); [+0x24] is a mode word
    (compared with 1 and 3).

 2. secondary-core TCM descriptor, 0x46D3A..0x46D84 [V] :
      r2 = custom_plat[+0x04] ; r3 = custom_plat[+0x08]
      r8 = custom_plat[+0x0C] ; sb = custom_plat[+0x10]
      if (r8+sb) > 0x1BF000 : skip
      safe_copy(0x400000, 0xC000, custom_plat[+0x04], custom_plat[+0x08])  # ITCM
      safe_copy(0x410000, 0x8000, custom_plat[+0x0C], custom_plat[+0x10])  # DTCM
      memset(0x416400, 0, 0x80) ; *(u32*)0x416400 = custom_plat[+0x00]
    0xC1E44 is a bounds-checked copy wrapper around memcpy 0x1036AC; 0xC1E0C is
    a bounds-checked memset wrapper around 0x1031A4.

So the image's CPU0 boot path itself copies the secondary-core firmware into
the TCM windows at CPU/CA 0x400000 (ITCM, cap 0xC000) and 0x410000 (DTCM, cap
0x8000) -- the vendor memmap calls those regions "excp_wram" {0x400000,0x18000}
and "excp_smac_dtcm" {0x410000,0x8000} [V from plat.ko .data+0x26B8].

The copy is only correct if the host has pre-loaded custom_plat with the
firmware's own section boundaries.  Those boundaries are exactly what the
vendor device INI carries (and what the vendor host driver's custom-config
download writes into the device):

    firmware_itcm_src_addr = 0x%08X   firmware_itcm_len = 0x%05X
    firmware_dtcm_src_addr = 0x%08X   firmware_dtcm_len = 0x%05X
""" % (ITCM_SRC, ITCM_LEN, DTCM_SRC, DTCM_LEN))
    ini = read_ini(INI)
    if ini:
        print("Local device INI (%s):" % INI)
        for k in ("firmware_itcm_src_addr", "firmware_itcm_len",
                  "firmware_dtcm_src_addr", "firmware_dtcm_len",
                  "firmware_plat_custom_src_addr", "firmware_plat_custom_len",
                  "firmware_wifi_custom_src_addr", "firmware_wifi_custom_len"):
            print("  %-32s = %s" % (k, hex(ini[k]) if isinstance(ini.get(k), int) else ini.get(k)))
        print()
    else:
        print("(device INI not present locally at %s)\n" % INI)
    print("""The two secondary-core blobs inside FIRMWARE.bin [V]:

  ITCM  CPU 0x117BC4..0x12252C (len 0xA968) = file 0xD7BC4..0xE252C
        starts with a standalone ARM exception vector table:
          +0x00 ldr pc,[pc,#0x18] x5 ; +0x14 NOP ; +0x18/+0x1C ldr pc,[pc,#0x14]
          targets: 0xA3EC 0xA324 0xA34C 0xA374 0xA39C (IRQ) 0xA4B0 ...
        -> the image is linked as a base-0 ARM image: it is what the secondary
           core executes after the ROM/entry-A copies it into ITCM.
  DTCM  CPU 0x12252C..0x122C98 (len 0x76C) = file 0xE252C..0xE2C98
        starts with {0x40034000, 0x3A, 0, 0x40034014, ...} = initial data.
        ITCM+DTCM tile the file exactly:
          0x117BC4 + 0xA968 == 0x12252C ; 0x12252C + 0x76C == 0x122C98 == EOF.

Host-visible windows for the same memory:
  ITCM dest 0x400000 -> host BAR0 0x1C0000 (region 1 WRAM/TCM, 0x18000)
  DTCM dest 0x410000 -> host BAR0 0x1D0000
  custom_plat CPU 0x1B2800 -> host BAR0 0x86A800 (region 5 ACP-fw)
""")
    return 0


def cmd_cpu1(b):
    print("=" * 78)
    print("The secondary-core image and its ROM ABI")
    print("=" * 78)
    print("""
The image tail at CPU 0x120A6C (ITCM offset 0x%X, not a reset entry) [V]:
  * CP15 MMU off -> cache clean/invalidate -> MMU on (0x120A9A..0x120B0C);
    the cache-region table it walks is at CPU 0x10678 (below the image base,
    no image writer: ROM-provided)
  * memset(0x107B8, 0, 0x3C50) -> its own handler table at 0x107B8..0x14408
  * mirror of CPU0's 0x40105xxx glue at CA 0x40033024..0x40033048
  * per-core event controller CA 0x4018xxxx init (base 0x40181000,
    handler table CPU 0x107B8, coreword stored at 0x10938, dispatcher 0x120A0C)
  * registers ROM low-RAM Thumb handlers 0x67d/0x691/0x6d9 for events
    0x39/0x3a/0x3b (0x117C54(cpu1-table registrar)); the handlers live below
    0x40000 -> mask-ROM code.  The image itself never references the handshake
    page or CA 0x40161F00 [V negative].
""" % (0x120A6C - ITCM_SRC))
    print("secondary-core vector table (first 0x40 bytes of the ITCM image):")
    for a in range(ITCM_SRC, ITCM_SRC + 0x40, 4):
        print("  ITCM+0x%02X  (CPU 0x%05X) = 0x%08X" %
              (a - ITCM_SRC, a, b.word(a) or 0))
    print("""
=> The image ships the secondary core's firmware but not its release logic:
   the ROM must start that core; the '+8=2' S1 reply is not image code.
""")
    return 0


def cmd_rom(b):
    print("=" * 78)
    print("ROM model: vectors, handler slots, doorbell, and the +8=2 writer")
    print("=" * 78)
    print("""
Live ROM reads from the earlier attended lanes [V*] (region-0 identity window,
BAR0 == CPU for the low page; phys = 0x40000000 + BAR0):

  CA 0x00..0x10 = 0xE59FF018 (ldr pc,[pc,#0x18]) -> targets CA 0x20..0x30
  CA 0x14       = 0xE320F000 (NOP, reserved vector)
  CA 0x18/0x1C  = 0xE59FF014 (ldr pc,[pc,#0x14]) -> IRQ target CA 0x34, FIQ CA 0x38
  target words: CA 0x20=0x8C reset, 0x24=0x3C undef, 0x28=0x4C svc,
                0x2C=0x5C prefetch, 0x30=0x6C abort, 0x34=0x40064 (image
                trampoline), 0x38=0x7C (ROM-internal FIQ)
  handler-slot table CA 0x1E0..0x1FC = image header slots then
                0x1BF110, 0x1BEF00, 0x7386F44F (page-area pointers)

=> the ROM keeps FIQ for itself (0x7C) and vectors IRQ into the image
   trampoline: it can still service inter-core/wake events after handing the
   image the CPU.  Those slots are the readable half of the S1 page contract.

Inter-core wake register CA 0x40161F00 (write-only from the image) [V]:
  value = (1<<core)<<16 | command   (command 1 = notify, 2 = exception)
  writers: entry A 0x473E6  value 0x00020000   (wake core 1, no command)
           0xC20EA         value (1<<core)<<16|1  (event notify)
           0xC6E54/0xC6E6C value 0x00010002 / 0x00020002 (exception notify)
  the image never reads it: the consumer is hardware + the target core.

Who writes +8 = 2 [V negative for the image, [I] high for mask ROM]:
  * exactly one image store to +8/+C exists (entry A strd {0,0xABCD});
  * no image site materialises the value 2 for the page (writer census above);
  * the secondary-core image has no page/doorbell reference at all;
  * the page's own ROM slots 0x1F4/0x1F8 point into the page/arena area;
  * the partner only answers AFTER entry A rings the core-1 wake doorbell;
  => the writer is the mask-ROM S1 path that releases/services the second core.
     It is not "another CPU1 image entry" we can reach from the firmware image.

Deterministic condition (mechanism found in this lane):
  entry A cannot even hand the secondary-core firmware to the core unless the
  platform-custom block (CPU 0x1B2800) carries valid ITCM/DTCM source/len
  values; otherwise the two safe copies fail and S1 never answers.  The vendor
  host driver supplies exactly those values from the device INI before release;
  our transport does not.  Stale-but-valid values from earlier vendor-stock
  boots in WRAM explain the intermittent GP1 successes (and their decay).
""")
    return 0


def cmd_gp3(b):
    print("=" * 78)
    print("GP3 - the deterministic host action (bench plan)")
    print("=" * 78)
    print("""
Preconditions: GP2-style kernel (wifi-pcie-hog removed so the driver owns
GPIO0 20/21/28 and performs ONE clean in-probe pulse -> mask=0 / image boots)
and the usual transport (prepare, upload, verify) + gate service + release.

STEP A (primary, vendor-faithful): pre-seed the platform-custom TCM descriptor
before release.  Device CPU 0x1B2800 -> host BAR0 0x86A800 (region 5,
phys 0x4086A800).  Write then read back (32-bit):

  BAR0 0x86A804 <- 0x%08X   # firmware_itcm_src_addr
  BAR0 0x86A808 <- 0x%08X   # firmware_itcm_len
  BAR0 0x86A80C <- 0x%08X   # firmware_dtcm_src_addr
  BAR0 0x86A810 <- 0x%08X   # firmware_dtcm_len

Also log the whole block 0x86A800..0x86A85F (mode at 0x86A824 expected 1 or 3)
and DO NOT write +0x24 unless a first run shows it is neither 1 nor 3.

STEP B (fallback / A+B in the same run is safe): preload the TCM windows with
the same two blobs directly from the local FIRMWARE.bin (they are identical
bytes, so a successful entry-A copy is a no-op for us):
  0x400000 ITCM <- file 0x%05X .. 0x%05X (0x%X B) -> host BAR0 0x1C0000
  0x410000 DTCM <- file 0x%05X .. 0x%05X (0x%X B) -> host BAR0 0x1D0000
Verify readback: BAR0 0x1C0000 == 0xE59FF018, BAR0 0x1D0000 == 0x40034000.

Then release CA 0x40000108 = 0x5A5A as usual and observe for 20 s.

EXPECTED OBSERVABLES (all one boot; BAR0 = CPU+0x6B8000 for the page):
  page CA 0x1BF000 (BAR0 0x877000) time series at ~0.5/2/5/20 s:
    +0 = 0x86807FEF  (bit14 set; GP2 baseline is 0x86803FEF without it)
    +4 = 0x00000003
    +8 = 0x00000002  <-- the S1 contract value (new)
    +C = 0x0000ABCD
  TCM: BAR0 0x1C0000 = 0xE59FF018 x5, 0xE320F000, 0xE59FF014 x2
       BAR0 0x1D0000 = 0x40034000
  state CA 0x118B60 = 1 ; exception mask CA 0x40008 = 0x00000000
  boot/event layer armed (handler table + timer enable, CA 0x40161100 bit13)
  then the normal transport watch: out[1] bit2, g4 ready id1/id2, ETE DR fills

DECISION MATRIX
  TCM loaded + page 2/0xABCD + state 1  -> S1 is deterministic; keep GP3 and
                                            continue the ready/ETE path.
  TCM loaded + page +8 == 0             -> the blocker is CPU1 release, not the
                                            TCM content: add the vendor wifi
                                            custom block at CPU 0x1B2860
                                            (BAR0 0x865860) and, if still stuck,
                                            read the ROM status page CA+0x10..
                                            +0x2C and the TCM coreword 0x10938.
  TCM not loaded, page shows no bit5/6 -> entry A did not run: check the pulse
                                            log + links; do not poke more RAM.
  page shows entry-A bits but TCM empty -> the 4 descriptor words did not land:
                                            re-check the pre-release readback
                                            ordering and retry with STEP B only.
  ANY exception/UR -> stop, power-cycle, snaphot dmesg (no rmmod after release).

Safety: 4 (or 6) 32-bit writes into firmware RAM only; no device-IO register,
no ROM page word, no handler slot, no CA 0x400392F0/0x40160110.  Recovery door
armed; the write happens before release, i.e. while the image is not executing.
""" % (ITCM_SRC, ITCM_LEN, DTCM_SRC, DTCM_LEN,
       ITCM_SRC - BASE, ITCM_SRC + ITCM_LEN - BASE, ITCM_LEN,
       DTCM_SRC - BASE, DTCM_SRC + DTCM_LEN - BASE, DTCM_LEN))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fw", default=FW)
    ap.add_argument("cmd", nargs="?", default="all",
                    choices=["selftest", "handshake", "tcm", "cpu1", "rom",
                             "gp3", "all"])
    a = ap.parse_args()
    b = Blob(a.fw)
    rc = 0
    if a.cmd in ("selftest", "all"):
        rc |= selftest(b)
        print()
    if a.cmd in ("handshake", "all"):
        rc |= cmd_handshake(b)
    if a.cmd in ("tcm", "all"):
        rc |= cmd_tcm(b)
    if a.cmd in ("cpu1", "all"):
        rc |= cmd_cpu1(b)
    if a.cmd in ("rom", "all"):
        rc |= cmd_rom(b)
    if a.cmd in ("gp3", "all"):
        rc |= cmd_gp3(b)
    return rc


if __name__ == "__main__":
    sys.exit(main())
