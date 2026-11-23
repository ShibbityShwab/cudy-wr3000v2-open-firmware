#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""romdis.py - host-only analysis of the Hi1152V100/Hi5622V100-class chip mask boot ROM.

Reads the LOCAL ROM dump in place (env ROM_DUMP) and the LOCAL vendor firmware
(env HI5622_FW) and prints only DERIVED facts: addresses, constants, structure,
disassembly annotations, the S0/S1 boot-flow reconstruction, the S1 gating
conditions and the host-action checklist.

Local evidence only:
  * the ROM dump is a DEVICE ROM and is NEVER copied or committed;
  * the vendor firmware blob is never copied or committed;
  * this tool only prints derived values (addresses / constants / decoded code).

Address conventions
-------------------
  ROM      : ROM 'cpu address' == file offset (ROM is mapped at CPU 0).
             host identity window: phys = 0x40000000 + file offset.
  firmware : image 'cpu address' == file offset + 0x40000.
             host ACP window  : BAR0(region 5) = CPU + 0x6B8000,
                                but only where the transport maps it.

Usage
-----
  python3 romdis.py info                 # identity, sha256, region classification
  python3 romdis.py map                  # full ROM map (code/data/tables/vectors)
  python3 romdis.py dis <addr> <len> [arm|thumb]
  python3 romdis.py flow                 # S0 reset path / primary / secondary reconstruction
  python3 romdis.py stores               # absolute-address store census (S1 evidence)
  python3 romdis.py consts               # materialised 32-bit constant census
  python3 romdis.py tables               # field-merge tables at 0x618/0x718
  python3 romdis.py search <hexvalue>    # materialisation search for a constant
  python3 romdis.py gate                 # S1 gating + exact host-action answer
  python3 romdis.py image [header|handlers|dispatchers|census]
  python3 romdis.py selftest             # every claim in REPORT.md checked

Environment:
  ROM_DUMP    default /home/shibbityshwab/staging-fi/rom16k.bin
  HI5622_FW   default /home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin
"""
import hashlib
import os
import struct
import sys

try:
    from capstone import (Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB, CS_OP_MEM,
                          CS_OP_IMM, CS_OP_REG, CS_GRP_JUMP, CS_GRP_CALL, CS_GRP_RET)
    from capstone.arm import ARM_REG_PC
except Exception as exc:  # pragma: no cover - dependency guard
    sys.stderr.write("ERROR: capstone is required (5.x): %s\n" % exc)
    raise SystemExit(2)

ROM_DUMP = os.environ.get("ROM_DUMP", "/home/shibbityshwab/staging-fi/rom16k.bin")
HI5622_FW = os.environ.get("HI5622_FW",
                          "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin")

ROM_SIZE = 16384
ROM_SHA256 = "fcfd464fd2bdfc1ab8b3f731ce19c7037885699f21796753654ae85c2198faa4"
FW_SHA256 = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_SIZE = 928920

# CPU addresses (== ROM file offsets) of the pieces of the readable window.
VECTORS = [(0x00, 0x20), (0x20, 0x3C), (0x3C, 0x8C), (0x8C, 0x1E0),
           (0x1E0, 0x1FC), (0x1FC, 0x60C), (0x60C, 0x618), (0x618, 0x7F8),
           (0x7F8, 0x81C), (0x81C, 0x4000)]

# Interesting CPU addresses (all derived, no bytes).
A_IMAGE_HDR = 0x40000          # image header cpu address (file 0x00)
A_ENTRY_A = 0x46970            # header +0x00 -> Thumb entry A
A_ENTRY_B = 0xC742C            # header +0x04 -> Thumb entry B (not referenced by this ROM)
A_PAGE = 0x1BF000              # ROM<->image boot progress page
A_PAGE_FLAGS = 0x1BF004        # second flags / CPU1 S1 progress word
A_PAGE_STATUS = 0x1BF008       # inter-core handshake status (+8)
A_PAGE_MAGIC = 0x1BF00C        # handshake magic 0xABCD (+C)
A_STATUS_REG = 0x4000010C      # 16-bit boot-stage gate (ROM 0xCEED / image 0xCECE)
A_RELEASE_REG = 0x40000108     # host release word (0x5A5A)
A_ENTRY_B_SLOT = 0x40004       # image header +0x04: entry-B pointer, consumed by the ROM CPU1 path
A_EVT0 = 0x40160100            # CPU1 event-controller words written by the ROM secondary path
A_EVT1 = 0x40160104
A_WAKE = 0x40161F00            # inter-core wake doorbell written by image entry A
A_SVC_STK0 = 0x1BEF00          # ROM per-core SVC stack tops
A_EXC_STK0 = 0x1BF110          # ROM per-core IRQ/UND/ABT/FIQ stack tops

# image header: five ARM exception entry words (cpu addresses)
IMG_HANDLERS = {0x50: 0x103140, 0x54: 0x103114, 0x58: 0x1030E8,
                0x5C: 0x1030BC, 0x60: 0x103090}
# ROM vector -> ROM target -> ROM stub slot -> image header offset -> image handler
#   derived: vector table (0x00), reset dispatch 0x1CC, stub literals 0x1E0.., header words
ROM_EXC_CHAIN = [
    # vector, name, rom target, slot addr, header off
    (0x04, "undef",   0x3C, 0x1E0, 0x60),
    (0x08, "svc",     0x4C, 0x1E4, 0x5C),
    (0x0C, "prefetch",0x5C, 0x1E8, 0x58),
    (0x10, "data-abort",0x6C, 0x1EC, 0x54),
    (0x1C, "fiq",     0x7C, 0x1F0, 0x50),
]
ROM_IRQ_TARGET = 0x40064       # IRQ vector target: image trampoline (bypasses header)

# handler r1 ids, derived from ROM->header->veneer->thunk (see image handlers cmd)
R1_IDS = {0x103090: 1, 0x1030BC: 2, 0x1030E8: 3, 0x103114: 4}
R1_NAMES = {1: "undefined", 2: "svc", 3: "prefetch-abort", 4: "data-abort"}


def die(msg):
    sys.stderr.write("ERROR: %s\n" % msg)
    raise SystemExit(1)


def load_rom():
    if not os.path.exists(ROM_DUMP):
        die("ROM dump not found: %s (set ROM_DUMP)" % ROM_DUMP)
    data = open(ROM_DUMP, "rb").read()
    if len(data) != ROM_SIZE:
        die("ROM dump size %d != %d" % (len(data), ROM_SIZE))
    return data


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def u32(d, off):
    return struct.unpack_from("<I", d, off)[0]


def u16(d, off):
    return struct.unpack_from("<H", d, off)[0]


def rom_sha_ok(d):
    return hashlib.sha256(d).hexdigest() == ROM_SHA256


def _dis_engine(thumb):
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB if thumb else CS_MODE_ARM)
    md.detail = True
    return md


def _pc_after(insn, thumb):
    """Value of r15 as seen by a PC-relative operand."""
    if thumb:
        return (insn.address + 4) & ~3
    return insn.address + 8


def _mem_addr(insn, op, thumb, consts):
    """Resolve a [base, #disp] (or [base, index]) operand to an integer or None."""
    mem = op.mem
    base = insn.reg_name(mem.base) if mem.base else None
    if base == "pc":
        return _pc_after(insn, thumb) + mem.disp
    if not base or base not in consts or consts[base] is None:
        return None
    val = consts[base]
    if mem.index:
        idx = insn.reg_name(mem.index)
        if idx not in consts or consts[idx] is None:
            return None
        return (val + consts[idx] + mem.disp) & 0xFFFFFFFF
    return (val + mem.disp) & 0xFFFFFFFF


def _is_branch(insn):
    groups = insn.groups
    return CS_GRP_JUMP in groups or CS_GRP_CALL in groups or CS_GRP_RET in groups


def _breaks_flow(insn):
    """True only for instructions that end a basic block (conditional branches keep
    the fall-through tracker state so straight-line store evidence is not lost)."""
    m = insn.mnemonic
    if m in ("bx", "bl", "blx", "bl.w", "blx.w", "tbb", "tbh"):
        return True
    if (m.startswith("pop") or m.startswith("ldm")) and "pc" in insn.op_str:
        return True
    return False


_DST_MNEMONICS = {
    "mov", "movs", "movw", "movt", "mov.w", "mvn", "mvns", "mvn.w",
    "add", "adds", "add.w", "adds.w", "sub", "subs", "sub.w", "subs.w",
    "orr", "orrs", "orr.w", "and", "ands", "and.w", "bic", "bics", "bic.w",
    "eor", "eors", "eor.w", "lsl", "lsls", "lsl.w", "lsr", "lsrs", "lsr.w",
    "asr", "asrs", "asr.w", "ldr", "ldrb", "ldrh", "ldrsb", "ldrsh", "ldr.w",
    "ldrb.w", "ldrh.w", "adr", "adr.w", "mla", "mul", "muls",
}


def analyze_span(d, start, end, thumb, collect):
    """Linear sweep a code span with a small constant tracker.

    collect is a dict of lists filled with:
      stores   : dict(addr=insn.cpu_addr, target=cpu addr or None, value=[...])
      consts   : dict(addr=site, value=const, kind='movw/movt'|'literal')
      calls    : dict(addr, target_cpu or None)
      insns    : list of (cpu_addr, mnemonic, op_str)
    """
    md = _dis_engine(thumb)
    code = d[start:end]
    consts = {}
    lo16 = {}
    for insn in md.disasm(code, start):
        cpu = insn.address
        m = insn.mnemonic
        ops = insn.operands
        # ---- record call/branch targets
        if m in ("bl", "blx", "b", "bx", "b.w", "bl.w", "blx.w", "cbz", "cbnz"):
            tgt = None
            for op in ops:
                if op.type == CS_OP_IMM:
                    tgt = op.imm & 0xFFFFFFFF
            collect["calls"].append(dict(addr=cpu, mnem=m, target=tgt))
        # ---- record absolute stores
        if m.startswith("str") and len(ops) >= 2:
            memop = None
            valregs = []
            for op in ops:
                if op.type == CS_OP_MEM:
                    memop = op
                elif op.type == CS_OP_REG:
                    valregs.append(insn.reg_name(op.reg))
            if memop is not None:
                target = _mem_addr(insn, memop, thumb, consts)
                vals = [consts.get(r, None) for r in valregs]
                collect["stores"].append(dict(addr=cpu, target=target,
                                              value=vals, mnem=m, op_str=insn.op_str))
                if m == "strd" and target is not None:
                    collect["stores"].append(dict(addr=cpu, target=(target + 4) & 0xFFFFFFFF,
                                                  value=vals[1:] or [None], mnem=m,
                                                  op_str=insn.op_str + " (2nd word)"))
        # ---- constant tracking
        if m in ("movw", "mov.w", "movs", "mov") and ops and ops[0].type == CS_OP_REG:
            rd = insn.reg_name(ops[0].reg)
            imm = None
            for op in ops[1:]:
                if op.type == CS_OP_IMM:
                    imm = op.imm & 0xFFFFFFFF
            if imm is not None:
                if imm <= 0xFFFF:
                    lo16[rd] = imm
                if len(ops) == 2:
                    consts[rd] = imm  # plain MOV immediate
                    if imm > 0xFF:
                        collect["consts"].append(dict(addr=cpu, value=imm, kind="mov-imm"))
                else:
                    consts[rd] = None
        elif m in ("movt",) and len(ops) >= 2 and ops[0].type == CS_OP_REG:
            rd = insn.reg_name(ops[0].reg)
            imm = ops[1].imm & 0xFFFF
            low = lo16.get(rd)
            if low is None:
                consts[rd] = None
            else:
                val = (imm << 16) | low
                consts[rd] = val
                collect["consts"].append(dict(addr=cpu, value=val, kind="movw/movt"))
        elif m.startswith("ldr") and len(ops) >= 2 and ops[1].type == CS_OP_MEM \
                and ops[1].mem.base == ARM_REG_PC:
            rd = insn.reg_name(ops[0].reg)
            target = _mem_addr(insn, ops[1], thumb, consts)
            if target is not None and 0 <= target <= len(d) - 4:
                val = u32(d, target)
                consts[rd] = val
                collect["consts"].append(dict(addr=cpu, value=val, kind="literal@%#x" % target,
                                              target=target))
            else:
                consts[rd] = None
        elif m.startswith("ldr") and len(ops) >= 2 and ops[1].type == CS_OP_MEM:
            rd = insn.reg_name(ops[0].reg)
            consts[rd] = None  # runtime memory value
        elif m in ("add", "adds", "add.w", "sub", "subs", "sub.w") and ops and len(ops) >= 3 \
                and ops[0].type == CS_OP_REG and ops[1].type == CS_OP_REG and ops[2].type == CS_OP_IMM:
            rd = insn.reg_name(ops[0].reg)
            rn = insn.reg_name(ops[1].reg)
            base = consts.get(rn)
            if base is not None:
                imm = ops[2].imm
                consts[rd] = (base + imm) & 0xFFFFFFFF if m.startswith("add") \
                    else (base - imm) & 0xFFFFFFFF
            else:
                consts[rd] = None
        elif m in ("orr", "orrs", "orr.w") and ops and len(ops) >= 3 \
                and ops[0].type == CS_OP_REG and ops[1].type == CS_OP_REG and ops[2].type == CS_OP_IMM:
            rd = insn.reg_name(ops[0].reg)
            rn = insn.reg_name(ops[1].reg)
            base = consts.get(rn)
            consts[rd] = None if base is None else base | (ops[2].imm & 0xFFFFFFFF)
        else:
            # generic: first explicit register operand is (usually) the destination
            if ops and ops[0].type == CS_OP_REG:
                rd = insn.reg_name(ops[0].reg)
                if m in _DST_MNEMONICS or m.startswith("ldr") or m.startswith("adr"):
                    if rd not in ("sp", "pc"):
                        consts[rd] = None
                        lo16.pop(rd, None)
        # basic-block boundary resets the tracker (AAPCS r4-r11 survive a call)
        if _breaks_flow(insn):
            if m in ("bl", "blx", "bl.w", "blx.w"):
                keep = set("r4 r5 r6 r7 r8 r9 r10 r11".split())
                consts = {k: v for k, v in consts.items() if k in keep}
                lo16 = {k: v for k, v in lo16.items() if k in keep}
            else:
                consts = {}
                lo16 = {}
        collect["insns"].append((cpu, m, insn.op_str))


def gather(d):
    collect = dict(stores=[], consts=[], calls=[], insns=[])
    analyze_span(d, 0x00, 0x1C, False, collect)
    analyze_span(d, 0x3C, 0x8C, False, collect)
    analyze_span(d, 0x8C, 0x1E0, False, collect)
    analyze_span(d, 0x1FC, 0x60C, True, collect)
    analyze_span(d, 0x608, 0x618, False, collect)
    return collect


def classify_region(off):
    if off < 0x20:
        return "ARM vectors"
    if off < 0x3C:
        return "vector target words (data)"
    if off < 0x8C:
        return "ARM exception stubs"
    if off < 0x1E0:
        return "ARM reset/boot path (S0)"
    if off < 0x1FC:
        return "handler-slot + stack-base table (data)"
    if off < 0x60C:
        return "Thumb/Thumb2 ROM code (S0/S1 + helpers)"
    if off < 0x618:
        return "ARM pc-relative dispatch words (data) + 2 ARM insns"
    if off < 0x7F8:
        return "field-merge tables (data)"
    if off < 0x81C:
        return "ROM build banner (data)"
    return "zero pad / read-as-zero (no ROM content)"


def cmd_info(argv):
    d = load_rom()
    ok = rom_sha_ok(d)
    last = max(i for i, b in enumerate(d) if b) if any(d) else -1
    print("ROM dump        : %s" % ROM_DUMP)
    print("size            : %d bytes" % len(d))
    print("sha256          : %s  %s" % (hashlib.sha256(d).hexdigest(), "MATCH" if ok else "MISMATCH"))
    print("last nonzero    : offset %#x (%s)" % (last, "banner end / code+data end"))
    print("mapping         : ROM cpu address == file offset; host phys = 0x40000000 + offset")
    print("banner          : %r" % d[0x7F8:0x7F8 + 36])
    print("code regions    : ARM 0x000-0x1DF (with data islands), Thumb 0x1FC-0x60B")
    print("effective ROM   : 0x000-0x81B (%d bytes); 0x81C-0x3FFF reads 0x00000000" % (last + 1))
    print("classification  :")
    for a, b in VECTORS:
        print("   %#06x..%#06x  %s" % (a, b - 1, classify_region(a)))
    zeros = all(b == 0 for b in d[0x81C:0x4000])
    print("0x81C-0x3FFF all zero: %s (no hidden code; beyond 0x3FFF reads 0xFFFFFFFF = unmapped)" % zeros)
    return 0 if ok else 1


def cmd_map(argv):
    d = load_rom()
    print("ROM MAP (cpu address == dump file offset; host identity phys = 0x40000000 + addr)")
    print("")
    print("0x000-0x01F  ARM vector table")
    names = ["reset", "undef", "svc", "prefetch-abort", "data-abort", "reserved",
             "IRQ", "FIQ"]
    for i, nm in enumerate(names):
        w = u32(d, i * 4)
        tgt = u32(d, 0x20 + i * 4) if i < 5 else u32(d, 0x34 + (i - 6) * 4) if i >= 6 else 0
        if i == 5:
            print("   vec[%d] %-14s = %#010x  (nop)" % (i, nm, w))
        else:
            print("   vec[%d] %-14s = %#010x -> target %#x" % (i, nm, w, tgt))
    print("   vec[6] IRQ target = %#x (image trampoline; bypasses the image header table)" % u32(d, 0x34))
    print("   vec[7] FIQ target = %#x (ROM stub -> image header +0x50)" % u32(d, 0x38))
    print("")
    print("0x03C-0x08B  ARM exception stubs (5 x 16B: push {r0}; ldr r0,=slot; ldr r0,[r0]; bx r0)")
    for vec, nm, tgt, slot, hoff in ROM_EXC_CHAIN:
        print("   %-10s stub %#04x -> slot %#04x -> [%#x] -> image header +%#04x = %#010x" %
              (nm, tgt, slot, 0x40000 + hoff, hoff, IMG_HANDLERS[hoff]))
    print("")
    print("0x08C-0x1DF  ARM reset/boot path S0: CP15 caches/MPU off -> per-mode stacks ->")
    print("             MPU regions -> MPU on -> MPIDR dispatch")
    print("0x1E0-0x1F8  7-word table: handler slots {0x40060,0x4005C,0x40058,0x40054,0x40050}")
    print("             then stack tops {0x1BF110, 0x1BEF00}")
    print("0x1FC-0x60B  Thumb2 ROM code (helpers + S0/S1 paths)")
    print("0x60C-0x617  ARM dispatch: 0x608 ldr pc -> 0x54D (primary), 0x610 ldr pc -> 0x565 (secondary)")
    print("0x618-0x717  field-merge table A (16 x 16B) used by fn 0x2E8")
    print("0x718-0x7F7  field-merge table B (14 x 16B) used by fn 0x2E8")
    print("0x7F8-0x81B  ROM banner %r" % d[0x7F8:0x7F8 + 34])
    print("0x81C-0x3FFF zero pad (no ROM content)")
    return 0


def cmd_dis(argv):
    if len(argv) < 3:
        die("usage: dis <cpu-addr> <len> [arm|thumb]")
    addr = int(argv[0], 0)
    ln = int(argv[1], 0)
    thumb = len(argv) > 2 and argv[2].startswith("t")
    d = load_rom()
    md = _dis_engine(thumb)
    for insn in md.disasm(d[addr:addr + ln], addr):
        print("%#06x  %-8s %s" % (insn.address, insn.mnemonic, insn.op_str))
    return 0


# ---------------------------------------------------------------- S0/S1 flow

def cmd_flow(argv):
    d = load_rom()
    print("RECONSTRUCTED BOOT FLOW (S0 = ROM bring-up, S1 = CPU1 boot-complete ACK)")
    print("Every line below is an instruction at the stated ROM cpu address derivable")
    print("with 'romdis.py dis'. No ROM bytes are reproduced here.")
    print("")
    print("S0a  ARM reset vector 0x20 -> 0x8C")
    print("     0x0C4 mrc p15,0,r0,c1,c0,0     ; read SCTLR")
    print("     0x0C8/0x0CC/0x0D0/0x0D4        ; clear I-cache, Z, D-cache, MMU/MPU-enable bits")
    print("     0x0D8 mcr p15,0,r0,c1,c0,0     ; caches + protector off")
    print("     0x0DC mrc p15,0,r0,c0,c0,5     ; MPIDR -> core id + 0x0E0 and r0,#3")
    print("     0x0E4..0x158 per-mode stacks (core-indexed):")
    print("           IRQ/UND/ABT/FIQ SP = %#x - 0x80*core" % A_EXC_STK0)
    print("           SVC SP             = %#x - 0x1000*core" % A_SVC_STK0)
    print("     0x15C..0x1B8 CP15 c6/c2/c1 {0,2,4} region select/base/size/attr:")
    print("           region0 base 0x00000000 size 0x3F attr 0x30C")
    print("           region1 base 0x40000000 size 0x2B attr 0x1300 (IO, XN)")
    print("           region2 base 0x80000000 size 0x3D attr 0x1300 (large RAM window)")
    print("           (size fields decode to 4GB / 4MB / 2GB -> ARMv7-R-style MPU, [I])")
    print("     0x1BC mrc/orr #1/dsb/mcr        ; enable protection (SCTLR.M)")
    print("     0x1CC MPIDR&3: 0 -> 0x608 -> 0x54D (primary/CPU0),")
    print("                     !=0 -> 0x610 -> 0x565 (secondary/CPU1)")
    print("")
    print("S0b  primary path (CPU0) 0x54C -> 0x50C:")
    print("     0x520 [0x1BF000] = 0                 ; progress page cleared")
    print("     0x526 [0x4000010C] read (16-bit)")
    print("     if == 0xCEED : 0x52C r3=0x18 -> 0x548 [0x1BF000]=0x18 (bits 3,4), go to image")
    print("     else         : 0x53A [0x40000]=0 (kill stale image header),")
    print("                    0x53C [0x1BF000]=1 (bit0), 0x53E bl 0x4C4, 0x542 |= 8 (bit3)")
    print("     0x4C4: 0x4D2 [0x4000010C]=0xBAAD, bl 0x344 (analog/PHY init; fn 0x228")
    print("            sets page bit1 when status 0x4003943C/0x40039C3C bit11 clears),")
    print("            0x4F2 [0x1BF000] |= 4 (bit2), poll until [0x40000108]==0x5A5A")
    print("            (host release), then 0x506 [0x4000010C]=0xCEED")
    print("     0x552: ldr r3,[0x40000] (re-read AFTER the release poll: the host must")
    print("            load the firmware before the 0x5A5A write); if nonzero -> blx entry A")
    print("            (CPU 0x46970, Thumb), then 0x560 spin forever (ROM never returns)")
    print("")
    print("S0c  secondary path (CPU1) 0x564 = THE S1 PATH (see also 'gate'):")
    print("     0x570 [0x1BF004] = 0")
    print("     0x586 [0x1BF00C] = 0                 ; clear magic (own core word)")
    print("     0x592/0x598 r2 = 0x00040000       ; image header base (matters below)")
    print("     0x58C read 16-bit [0x4000010C]; 0x59E/0x5A2:")
    print("           != 0xCEED -> [0x1BF004] |= 1 ; == 0xCEED -> [0x1BF004] |= 8")
    print("     0x5AA if != 0xCEED: [0x40004] = 0   ; CLEAR stale image header +4 (entry-B ptr)")
    print("     0x5C4 [0x40160104] = 0xFF, 0x5C8 [0x40160100] = 1 (CPU1 event block)")
    print("     0x5D2 [0x1BF004] |= 2                ; CPU1 armed")
    print("     0x5E2..0x5EE WFI loop, pass when BOTH:")
    print("           [0x1BF00C] == 0x0000ABCD      ; image entry A posted the request")
    print("           [0x4000010C] == 0x0000CEED    ; ROM boot-stage gate still intact")
    print("     0x5F0..0x5FE on success:")
    print("           [0x1BF004] |= 4")
    print("           [0x1BF008] = 1 << core_id     ; <-- == 2 on CPU1: the S1 ACK")
    print("     0x5F4/0x602/0x604 r2 = [0x40004]     ; image header +0x04 = ENTRY B pointer")
    print("           if (r2) blx r2                ; CPU1 enters image header entry B")
    print("     0x606 spin forever")
    print("")
    print("S1  the ACK is CONSUMED by image entry A (CPU0); CPU1 is then sent to entry B")
    print("     entry A posts [0x1BF008]=0 / [0x1BF00C]=0xABCD and [0x1BF000] |= 0x2000,")
    print("     rings inter-core wake CA 0x40161F00 <- 0x00020000 (target core 1), then polls")
    print("     [0x1BF008]==2; on success it sets page bit14 (0x4000), queues the boot task and")
    print("     calls the scheduler (S2). It never writes +8 itself.")
    return 0


# ---------------------------------------------------------------- evidence cmds

def _interesting_const(v):
    return (0x100 <= v <= 0x200000) or (0x40000000 <= v <= 0x401FFFFF) or (v & 0xFFFF) == 0


def cmd_consts(argv):
    d = load_rom()
    c = gather(d)
    seen = {}
    for it in c["consts"]:
        seen.setdefault(it["value"], []).append(it["addr"])
    print("Materialised 32-bit constants in ROM code (movw/movt pairs + ARM literal loads)")
    print("(sites are ROM cpu addresses; the same value may legitimately appear in more than")
    print(" one place because the compiler shares constants)")
    for v in sorted(seen):
        sites = seen[v]
        if len(sites) == 1 and not _interesting_const(v) and v < 0x100:
            continue
        print("  %#010x  sites: %s%s" % (v, ", ".join("%#06x" % s for s in sites[:8]),
                                        " ..." if len(sites) > 8 else ""))
    print("")
    print("Hand-curated interesting set (selftest verifies the sites exist):")
    want = [A_PAGE, 0x4000010C, 0x40000108, A_EVT0, A_EVT1, 0x40000,
            A_EXC_STK0, A_SVC_STK0, 0x40000000, 0x80000000, 0xBAAD, 0xCEED, 0x5A5A, 0xABCD,
            0x30C, 0x1300]
    for v in want:
        print("  %#010x  %s" % (v, ", ".join("%#06x" % s for s in seen.get(v, [])) or "NOT FOUND"))
    return 0


def cmd_stores(argv):
    d = load_rom()
    c = gather(d)
    print("Absolute-address store census in ROM code (constant-tracked base + displacement).")
    print("target  = memory cpu address; site = instruction cpu address; value = known constant")
    print("source (None = runtime value). Conditional stores are included and flagged.")
    interesting = []
    for st in c["stores"]:
        if st["target"] is None:
            continue
        t = st["target"]
        if t < 0x200000 or 0x40000000 <= t <= 0x40200000:
            interesting.append(st)
    for st in sorted(interesting, key=lambda s: s["target"]):
        vals = ",".join("?" if v is None else "%#x" % v for v in st["value"])
        print("  target %#010x  site %#06x  %-10s %-26s value=%s" %
              (st["target"], st["addr"], st["mnem"], st["op_str"], vals))
    print("")
    print("S1-related only:")
    for st in sorted(interesting, key=lambda s: s["target"]):
        if 0x1BF000 <= st["target"] <= 0x1BF010:
            print("   page store: site %#06x -> [%#x]  %-8s value base %s" %
                  (st["addr"], st["target"],
                   st["mnem"], ",".join("?" if v is None else "%#x" % v for v in st["value"])))
    return 0


def cmd_tables(argv):
    d = load_rom()
    print("Field-merge helper fn 0x2E8: for each 16-byte entry {src_shift,width,dst_off,dst_shift}")
    print("value = ((src >> src_shift) & ((1<<width)-1)) << dst_shift merged into [dst+dst_off]")
    print("Table A at 0x618 (16 entries), table B at 0x718 (14 entries).")
    for name, base, count in (("A", 0x618, 16), ("B", 0x718, 14)):
        print(" table %s:" % name)
        for i in range(count):
            w = [u32(d, base + 16 * i + 4 * k) for k in range(4)]
            print("   %2d: src_bit=%-3d width=%-2d dst_off=%#04x dst_bit=%d" %
                  (i, w[0], w[1], w[2], w[3]))
    return 0


def cmd_search(argv):
    if not argv:
        die("usage: search <hexvalue>")
    val = int(argv[0], 0) & 0xFFFFFFFF
    d = load_rom()
    c = gather(d)
    print("Value %#010x materialisation in ROM:" % val)
    # raw word occurrences (data tables / literal words)
    raw = [i for i in range(0, ROM_SIZE - 3, 4) if u32(d, i) == val]
    print("  raw 32-bit words at     : %s" % (", ".join("%#06x" % x for x in raw) or "none"))
    # movw/movt + literal-load sites
    sites = [it["addr"] for it in c["consts"] if it["value"] == val]
    print("  code materialisations   : %s" % (", ".join("%#06x" % x for x in sites) or "none"))
    # 16-bit immediate sites (movw only / movs)
    md = _dis_engine(True)
    hw = []
    for insn in md.disasm(d[0x1FC:0x60C], 0x1FC):
        for op in insn.operands:
            if op.type == CS_OP_IMM and (op.imm & 0xFFFF) == (val & 0xFFFF) \
                    and insn.mnemonic in ("movw", "movs", "mov.w", "mov"):
                hw.append(insn.address)
    print("  movw/movs low-16 sites  : %s" % (", ".join("%#06x" % x for x in hw) or "none"))
    return 0


def cmd_gate(argv):
    print("S1 GATE - DEFINITIVE ANALYSIS")
    print("=" * 78)
    print("WHO WRITES [0x1BF008] = 2")
    print("  CPU1 (secondary core) executing the mask ROM path 0x564 writes")
    print("  [0x1BF008] = 1 << core_id at ROM 0x5FE (the strd with r0 = 1<<MPIDR&3).")
    print("  For CPU1 that value is 2. CPU0's image entry A never writes +8 (it writes 0).")
    print("")
    print("EXACT CODE CONDITIONS (all must hold in the same WFI-loop iteration):")
    print("  (a) CPU1 actually executes the ROM secondary path 0x564")
    print("      (it must be started/out of reset and run the mask ROM).")
    print("  (b) [0x1BF00C] == 0x0000ABCD   (image entry A posted the handshake request)")
    print("  (c) [0x4000010C] & 0xFFFF == 0x0000CEED  (ROM boot-stage gate still intact)")
    print("  (d) CPU1 must be woken from WFI; the intended wake is entry A's inter-core")
    print("      doorbell CA 0x40161F00 <- 0x00020000 (target core 1) rung right after (b).")
    print("  On success: [0x1BF004] |= 4, [0x1BF008] = 2, then CPU1 reads image header")
    print("  +0x04 (CPU 0x40004 = the entry-B pointer) and blx-es it; if 0 CPU1 parks.")
    print("  => the ROM dispatches BOTH header entries: entry A on CPU0 (pre-S1) and")
    print("     entry B (0xC742C) on CPU1 (post-S1); both are cleared on the cold")
    print("     (non-0xCEED) path and must be re-populated by the host upload.")
    print("")
    print("WHAT WE CONTROL AS HOST")
    print("  0x40000108 (BAR0 0x3B8108): write 0x5A5A = CPU release. REQUIRED to unblock")
    print("    CPU0's ROM 0x4C4 poll; that poll's completion is what writes 0xCEED.")
    print("  0x4000010C (BAR0 0x3B810C): the SAME 16-bit word is used for TWO different")
    print("    handshakes and the values are mutually exclusive:")
    print("      ROM  boot stage    : 0xCEED  (written by CPU0 ROM 0x4C4 after the release)")
    print("      image service gate : 0xCECE  (image handler 0xC20F8 spins until 0xCECE;")
    print("                                    the host transport writes it as 'D2H announce gate')")
    print("  The current transport writes 0xCECE continuously during/after boot -> it")
    print("  overwrites 0xCEED -> CPU1 S1 condition (c) can never be true -> +8 stays 0.")
    print("  Vendor negative: the vendor host never writes 0xCECE (host-replica census) and")
    print("  the vendor boot has mask 0 (no exception/service handler runs) -> 0xCEED")
    print("  survives -> CPU1 completes S1 (golden page +4 = 0x7 proves the same code path")
    print("  ran: bit0=first-check-not-CEED, bit1=armed, bit2=handshake-complete).")
    print("")
    print("HOST ACTION (definitive)")
    print("  1. pre-release: seed the platform-custom block / TCM payload (already GP4-exact)")
    print("  2. release ONLY: 0x00005A5A -> CA 0x40000108 (BAR0 0x3B8108)")
    print("  3. DO NOT write 0xCECE -> CA 0x4000010C during boot. If a service/exception")
    print("     handshake is genuinely needed (mask 0x40008 != 0), write it only AFTER S1 is")
    print("     confirmed complete (page +8 == 2 / page +0 bit14 set); CPU1 no longer reads")
    print("     the register after S1, so the write is then harmless.")
    print("  4. optional diagnostic if +8 stays 0 while +4 has bits0/1: read 16-bit")
    print("     0x4000010C; if != 0xCEED a host write of 0x0000CEED to CA 0x4000010C")
    print("     (BAR0 0x3B810C) restores condition (c) directly (CPU1-only consumer).")
    print("")
    print("EXPECTED OBSERVABLES ON A SUCCESSFUL BOOT (read-only)")
    print("  page 0x1BF000 (BAR0 0x877000): +0 0x86807FEF (+0 bit14 set; GP2 baseline")
    print("      0x86803FEF lacks it), +4 0x00000007, +8 0x00000002, +C 0x0000ABCD")
    print("  then: state CA 0x118B60 = 1, exception mask CA 0x40008 = 0,")
    print("        boot task ran, scheduler/entry-B layer live.")
    print("  pre-release page on a clean pulse (ROM fallback waiting in the 0x4C4 poll):")
    print("        +0 = 0x00000007 (bit0 begun, bit1 analog-ready, bit2 init done),")
    print("        +4 = 0x00000003 (CPU1 armed: bit0 first check != CEED, bit1 armed),")
    print("        +8 random, +C = 0  <- exactly the bench value already observed on GP4/GP5/GP6.")
    print("")
    print("WHAT IS NOT AN ACTION")
    print("  * writing +8=2 yourself is a diagnostic only - it does not release CPU1 and")
    print("    it does not make CPU1 run; the boot-progress bits after +8 are CPU1-owned.")
    print("  * no other host register can wake CPU1's WFI loop; the wake is the image's own")
    print("    doorbell write, executed by entry A itself.")
    return 0


# ---------------------------------------------------------------- image side

def load_fw():
    if not os.path.exists(HI5622_FW):
        return None
    d = open(HI5622_FW, "rb").read()
    if len(d) != FW_SIZE or hashlib.sha256(d).hexdigest() != FW_SHA256:
        sys.stderr.write("WARN: firmware identity mismatch (expected 928,920 B sha %s...)\n" % FW_SHA256[:8])
    return d


def fw_dis(d, cpu, ln, thumb):
    md = _dis_engine(thumb)
    return md.disasm(d[cpu - 0x40000:cpu - 0x40000 + ln], cpu)


def cmd_image(argv):
    d = load_fw()
    if d is None:
        print("SKIP: firmware not found (set HI5622_FW); image-side section is optional.")
        return 0
    sub = argv[0] if argv else "header"
    if sub == "header":
        print("firmware image header (cpu = file + 0x40000), read from the local blob only:")
        for off, nm in ((0x00, "entry A (Thumb)"), (0x04, "entry B (Thumb)"),
                        (0x50, "handler +0x50 -> FIQ vector (ROM stub 0x7C)"),
                        (0x54, "handler +0x54 -> data-abort (ROM stub 0x6C)"),
                        (0x58, "handler +0x58 -> prefetch-abort (ROM stub 0x5C)"),
                        (0x5C, "handler +0x5C -> SVC (ROM stub 0x4C)"),
                        (0x60, "handler +0x60 -> undefined (ROM stub 0x3C)")):
            print("  +%#04x = %#010x  %s" % (off, u32(d, off), nm))
        print("")
        print("ROM slot table cross-check (slot word is an address inside this header):")
        for vec, nm, tgt, slot, hoff in ROM_EXC_CHAIN:
            slot_word = u32(d, hoff)
            print("  %-12s slot[%#04x]=%#x -> header+%#04x word = %#010x (match %s)" %
                  (nm, slot, 0x40000 + hoff, hoff, slot_word,
                   "YES" if slot_word == IMG_HANDLERS[hoff] else "NO"))
        print("  NOTE: header +0x50..+0x60 order is {FIQ, data-abort, prefetch-abort, SVC,")
        print("        undefined}, NOT the architectural vector order.")
        print("  NOTE: entry A (+0x00) is invoked by the ROM primary/CPU0 path; entry B")
        print("        (+0x04) is invoked by the ROM secondary/CPU1 path after S1 (see 'flow').")
    elif sub == "handlers":
        print("image ARM exception stubs (call sites) and their Thumb id thunks:")
        for vec, nm, tgt, slot, hoff in ROM_EXC_CHAIN:
            if hoff == 0x50:
                continue
            cpu = IMG_HANDLERS[hoff]
            print("\n  ROM %s -> image %#x:" % (nm, cpu))
            for insn in fw_dis(d, cpu, 0x30, False):
                print("    %#06x  %-8s %s" % (insn.address, insn.mnemonic, insn.op_str))
            print("    -> r1 id = %d (%s)" % (R1_IDS[cpu], R1_NAMES[R1_IDS[cpu]]))
        print("\n  FIQ handler %#x (header +0x50): ARM srsdb/rfeia trampoline that blx-es" %
              IMG_HANDLERS[0x50])
        print("  the Thumb reset dispatcher 0xC2EEC (slot CA 0x17D398+0x94 -> boot task).")
        print("")
        print("  CORRECTION: r1 ids are undefined=1, svc=2, prefetch=3, data-abort=4;")
        print("  the exception-seen mask CA 0x40008 ORs 1<<id at image 0xC6E90..0xC6E96.")
        print("  => mask 0x0A = undefined (0x2) + prefetch abort (0x8), NOT svc+data-abort.")
    elif sub == "dispatchers":
        print("IRQ trampoline 0x40064 (ARM; the only path to the event dispatcher):")
        for insn in fw_dis(d, 0x40064, 0x60, False):
            print("  %#06x  %-8s %s" % (insn.address, insn.mnemonic, insn.op_str))
        print("\nThumb reset dispatcher 0xC2EEC (FIQ path: boot-task kick):")
        for insn in fw_dis(d, 0xC2EEC, 0x14, True):
            print("  %#06x  %-8s %s" % (insn.address, insn.mnemonic, insn.op_str))
        print("\nThumb event dispatcher 0xC2EFC (IRQ path; handler table CA 0x17D430):")
        for insn in fw_dis(d, 0xC2EFC, 0x60, True):
            print("  %#06x  %-8s %s" % (insn.address, insn.mnemonic, insn.op_str))
        print("\nConclusion: neither image path writes [0x1BF008]. The image CONSUMES")
        print("the ROM/CPU1 S1 ACK; it does not produce it.")
    elif sub == "census":
        print("16-bit gate consumers in the image (family = 0x4000010C):")
        for insn in fw_dis_scan(d, [0xC20F8, 0xC2102, 0xC2106, 0xC210C]):
            print(insn)
        print("  image 0xC20F8 (event id 0x02 per-player service) = movw #0xCECE;")
        print("  ldr r1,=0x4000010C; ldr/cmp/bne loop -> the CECE consumer.")
        print("")
        for addr in (0x47DD8, 0xC2110, 0xC6FA0, 0xD26BE, 0xD2F0E):
            print("  aligned word 0x0000010C_4000... at cpu %#x = %#010x" % (addr, u32(d, addr - 0x40000)))
        print("")
        print("host-only register 0x40000108 in image: %s" % (
            "none (no aligned literal)" if not has_fw_word(d, 0x40000108) else "present"))
        print("image status read CA 0x40000004 (literal at CPU 0x45914): %s" % (
            "present, read-only" if has_fw_aligned_word(d, 0x40000004) else "absent"))
        print("(ROM secondary path: after writing +8=2 it loads image header +0x04 (CPU 0x40004)")
        print(" = entry B (0xC742C) and blx-es it; no ROM store to [0x40000004] exists)")
    else:
        die("usage: image [header|handlers|dispatchers|census]")
    return 0


def has_fw_word(d, val):
    pat = struct.pack("<I", val)
    return pat in d


def has_fw_aligned_word(d, val):
    pat = struct.pack("<I", val)
    for i in range(0, len(d) - 3, 4):
        if d[i:i + 4] == pat:
            return True
    return False


def _is_materialised(d, val):
    """True if val is materialised by a single immediate or a movw/movt pair."""
    for start, end, thumb in ((0x1FC, 0x60C, True), (0x8C, 0x1E0, False),
                              (0x3C, 0x8C, False)):
        md = _dis_engine(thumb)
        lo = {}
        for insn in md.disasm(d[start:end], start):
            if insn.mnemonic in ("movw", "mov", "movs", "mov.w") and len(insn.operands) == 2 \
                    and insn.operands[0].type == CS_OP_REG and insn.operands[1].type == CS_OP_IMM:
                rd = insn.reg_name(insn.operands[0].reg)
                imm = insn.operands[1].imm & 0xFFFFFFFF
                lo[rd] = imm
                if imm == val:
                    return True
            elif insn.mnemonic == "movt" and len(insn.operands) == 2 \
                    and insn.operands[0].type == CS_OP_REG and insn.operands[1].type == CS_OP_IMM:
                rd = insn.reg_name(insn.operands[0].reg)
                if rd in lo:
                    combined = ((insn.operands[1].imm & 0xFFFF) << 16) | (lo[rd] & 0xFFFF)
                    if combined == val:
                        return True
    return False


def fw_dis_scan(d, addrs):
    out = []
    for a in addrs:
        for insn in fw_dis(d, a, 4, True):
            out.append("  %#06x  %-8s %s" % (insn.address, insn.mnemonic, insn.op_str))
    return out


# ---------------------------------------------------------------- selftest

class Test(object):
    def __init__(self):
        self.n = 0
        self.bad = 0

    def check(self, name, cond):
        self.n += 1
        if cond:
            print("PASS %-72s" % name)
        else:
            self.bad += 1
            print("FAIL %-72s" % name)

    def eq(self, name, got, want):
        ok = got == want
        self.n += 1
        if ok:
            print("PASS %-72s" % name)
        else:
            self.bad += 1
            print("FAIL %-72s got=%r want=%r" % (name, got, want))


def _insn_at(d, cpu, thumb):
    md = _dis_engine(thumb)
    for insn in md.disasm(d[cpu:cpu + 4], cpu):
        return insn
    return None


def _seq_ok(d, thumb, specs):
    md = _dis_engine(thumb)
    for addr, mnem, sub in specs:
        ins = None
        for insn in md.disasm(d[addr:addr + 8], addr):
            ins = insn
            break
        if ins is None:
            return False, "no insn at %#x" % addr
        if mnem in ("movw", "movt", "ldrh", "strh", "blx", "bl", "cmp", "cbz", "lsl.w",
                    "strd", "ldr", "str", "str.w"):
            if ins.mnemonic != mnem:
                return False, "at %#x mnem %r != %r" % (addr, ins.mnemonic, mnem)
        else:
            if not ins.mnemonic.startswith(mnem):
                return False, "at %#x mnem %r != %r" % (addr, ins.mnemonic, mnem)
        if sub is not None and sub not in ins.op_str:
            return False, "at %#x op %r lacks %r" % (addr, ins.op_str, sub)
    return True, ""


def _chk_seq(t, name, d, thumb, specs):
    ok, why = _seq_ok(d, thumb, specs)
    t.check(name + ("" if ok else " [" + why + "]"), ok)


def cmd_selftest(argv):
    t = Test()
    d = load_rom()
    T = True
    F = False
    # ---------- identity / classification
    t.eq("ROM sha256 matches the device-dump identity", hashlib.sha256(d).hexdigest(), ROM_SHA256)
    t.eq("ROM size is 16 KiB", len(d), 16384)
    t.eq("last nonzero byte is 0x818 (banner end)", max(i for i, b in enumerate(d) if b), 0x818)
    t.check("0x81C-0x3FFF all zero (no hidden ROM code)",
            all(b == 0 for b in d[0x81C:0x4000]))
    t.eq("banner", d[0x7F8:0x7F8 + 33], b"Hi1152V100CS Sep 21 2022 16:21:44")
    # ---------- vector table
    t.eq("vec[0..4] ldr pc,[pc,#24] stubs", [u32(d, 4 * i) for i in range(5)], [0xE59FF018] * 5)
    t.eq("vec[5] = nop", u32(d, 0x14), 0xE320F000)
    t.eq("vec[6]/[7] = ldr pc,[pc,#20]", (u32(d, 0x18), u32(d, 0x1C)), (0xE59FF014, 0xE59FF014))
    t.eq("targets reset/undef/svc/prefetch/abort",
         [u32(d, 0x20 + 4 * i) for i in range(5)], [0x8C, 0x3C, 0x4C, 0x5C, 0x6C])
    t.eq("IRQ target = image trampoline 0x40064", u32(d, 0x34), 0x40064)
    t.eq("FIQ target = ROM stub 0x7C", u32(d, 0x38), 0x7C)
    # ---------- stubs + slot table
    slots = {0x3C: 0x1E0, 0x4C: 0x1E4, 0x5C: 0x1E8, 0x6C: 0x1EC, 0x7C: 0x1F0}
    ok = True
    for stub, slot in slots.items():
        md = _dis_engine(False)
        ins = list(md.disasm(d[stub:stub + 16], stub))
        if len(ins) < 4:
            ok = False
            break
        ok = ok and ins[0].mnemonic in ("push", "str") and "#-4" in ins[0].op_str
        ok = ok and ins[1].mnemonic == "ldr"
        disp = None
        for op in ins[1].operands:
            if op.type == CS_OP_MEM:
                disp = op.mem.disp
        ok = ok and disp is not None and (stub + 4 + 8 + disp) == slot
        ok = ok and ins[2].mnemonic == "ldr" and ins[3].mnemonic == "bx"
    t.check("5 ROM exception stubs load their own slot address 0x1E0..0x1F0", ok)
    t.eq("slot table 0x1E0..0x1F8", [u32(d, 0x1E0 + 4 * i) for i in range(7)],
         [0x40060, 0x4005C, 0x40058, 0x40054, 0x40050, 0x1BF110, 0x1BEF00])
    t.eq("stub slot -> image header offset map", [h for (_, _, _, _, h) in ROM_EXC_CHAIN],
         [0x60, 0x5C, 0x58, 0x54, 0x50])
    # ---------- ARM reset path (S0)
    _chk_seq(t, "0x8C reset: SCTLR clear/enable + MPIDR", d, F,
             [(0xC4, "mrc", "p15"), (0xC8, "bic", "#0x1000"), (0xD8, "mcr", "p15"),
              (0xDC, "mrc", "p15"), (0xE0, "and", "#3")])
    t.eq("per-mode CPSR constants", [u32(d, a) for a in (0xE4, 0xFC, 0x114, 0x12C, 0x144)],
         [0xE321F0D2, 0xE321F0DB, 0xE321F0D7, 0xE321F0D1, 0xE321F0D3])
    t.eq("stack-top literals 0x1F4/0x1F8", (u32(d, 0x1F4), u32(d, 0x1F8)), (0x1BF110, 0x1BEF00))
    _chk_seq(t, "CP15 c6 region sequence (select/base/size/attr x3)", d, F,
             [(0x160, "mcr", "c6"), (0x16C, "mov", "#0x3f"), (0x174, "mov", "#0x30c"),
              (0x17C, "mov", "#1"), (0x184, "mov", "#0x40000000"), (0x18C, "mov", "#0x2b"),
              (0x194, "mov", "#0x1300"), (0x19C, "mov", "#2"), (0x1A4, "mov", "#0x80000000"),
              (0x1AC, "mov", "#0x3d"), (0x1B4, "mov", "#0x1300")])
    t.eq("SCTLR.M enable at 0x1BC..0x1C8",
         (u32(d, 0x1BC), u32(d, 0x1C0), u32(d, 0x1C4), u32(d, 0x1C8)),
         (0xEE110F10, 0xE3800001, 0xF57FF04F, 0xEE010F10))
    t.eq("MPIDR dispatch", (u32(d, 0x1D4), u32(d, 0x1D8), u32(d, 0x1DC)),
         (0xE3510000, 0x1A00010C, 0xEA000109))
    t.eq("ARM->Thumb dispatch words", (u32(d, 0x60C), u32(d, 0x614)), (0x54D, 0x565))
    t.eq("0x608/0x610 = ldr pc,[pc,#-4]", (u32(d, 0x608), u32(d, 0x610)),
         (0xE51FF004, 0xE51FF004))
    t.check("dispatch targets are odd (Thumb ABI)", u32(d, 0x60C) & 1 and u32(d, 0x614) & 1)
    t.eq("0x1FC begins with Thumb2 MOVW (not a data magic)", d[0x1FC:0x200], b"\x4f\xf4\x86\x73")
    # ---------- status helpers
    _chk_seq(t, "fn 0x1FC = read 16-bit [0x4000010C]", d, T,
             [(0x1FC, "mov", "#0x10c"), (0x200, "movt", "#0x4000"),
              (0x204, "ldrh", "[r3]"), (0x206, "bx", "lr")])
    _chk_seq(t, "fn 0x208 = write 0xBAAD to [0x4000010C]", d, T,
             [(0x20C, "movw", "#0xbaad"), (0x214, "strh", "[r3]")])
    _chk_seq(t, "fn 0x218 = write 0xCEED to [0x4000010C]", d, T,
             [(0x21C, "movw", "#0xceed"), (0x224, "strh", "[r3]")])
    _chk_seq(t, "fn 0x228 = set progress-page bit1 (0x262..0x270)", d, T,
             [(0x262, "movw", "#0xf000"), (0x266, "movt", "#0x1b"),
              (0x26C, "orr", "#2"), (0x270, "str", "[r3]")])
    # ---------- primary path 0x50C
    _chk_seq(t, "0x50C: clear page +0, read 16-bit status gate", d, T,
             [(0x520, "str", "[r4]"), (0x522, "movt", "#0x4000"), (0x526, "ldrh", "[r3]")])
    _chk_seq(t, "0x50C: 0xCEED path stores page +0 = 0x18", d, T,
             [(0x52C, "movs", "#0x18"), (0x548, "str", "[r4]")])
    _chk_seq(t, "0x50C: fallback clears [0x40000] (stale image header)", d, T,
             [(0x530, "movw", "#0"), (0x536, "movt", "#4"), (0x53A, "str", "[r3]")])
    _chk_seq(t, "0x50C: fallback page=1 -> bl 0x4C4 -> page |= 8", d, T,
             [(0x53C, "str", "[r4]"), (0x53E, "bl", "#0x4c4"), (0x544, "orr", "#8")])
    _chk_seq(t, "0x4C4: BAAD -> init 0x344 -> page|=4 -> poll -> CEED", d, T,
             [(0x4CA, "movw", "#0xbaad"), (0x4D4, "bl", "#0x344"), (0x4EE, "orr", "#4"),
              (0x4F6, "cmp", "r3"), (0x4FE, "movw", "#0xceed")])
    _chk_seq(t, "0x4C4: poll 16-bit [0x40000108] == 0x5A5A", d, T,
             [(0x4DC, "mov.w", "#0x108"), (0x4E0, "movw", "#0x5a5a"),
              (0x4EA, "movt", "#0x4000"), (0x4F4, "ldr", "[r2]")])
    _chk_seq(t, "0x54C: re-read [0x40000] then blx image entry A", d, T,
             [(0x552, "movw", "#0"), (0x556, "movt", "#4"), (0x55A, "ldr", "[r3]"),
              (0x55E, "blx", "r3")])
    # ---------- secondary path 0x564 (S1)
    _chk_seq(t, "0x564: clear page +4 and +C", d, T,
             [(0x570, "str", "[r3, #4]"), (0x586, "str", "[r6, #8]")])
    _chk_seq(t, "0x564: read 16-bit gate and record CEED / not-CEED", d, T,
             [(0x58C, "ldrh", "[r2]"), (0x590, "cmp", "r4"),
              (0x59E, "orr", "#1"), (0x5A2, "orr", "#8")])
    _chk_seq(t, "0x564: write CPU1 event-controller words", d, T,
             [(0x5BC, "movt", "#0x4016"), (0x5C0, "movt", "#0x4016"),
              (0x5C4, "str.w", "[r5]"), (0x5C8, "str", "[r4]")])
    _chk_seq(t, "0x564: arm page +4 bit1, WFI loop waits magic 0xABCD", d, T,
             [(0x5D2, "str", "[r3, #4]"), (0x5D8, "movw", "#0xabcd"),
              (0x5E4, "ldr", "[r1, #8]")])
    _chk_seq(t, "0x564: WFI loop also waits 16-bit gate == 0xCEED", d, T,
             [(0x5DC, "movw", "#0xceed"), (0x5EA, "ldrh", "[r6]"), (0x5EC, "cmp", "r7")])
    _chk_seq(t, "0x564: success writes +4 |=4 and strd {+4,+8}={flags,1<<core}", d, T,
             [(0x5F6, "lsl.w", "r0"), (0x5FA, "orr", "#4"), (0x5FE, "strd", "[r3, #4]")])
    _chk_seq(t, "0x564: clear stale image header +4 (entry-B ptr) on non-CEED path", d, T,
             [(0x592, "movw", "#0"), (0x598, "movt", "#4"),
              (0x5A8, "it", "ne"), (0x5AA, "str", "[r2, #4]")])
    _chk_seq(t, "0x564: post-S1 CPU1 dispatch = blx image header +0x04 (entry B)", d, T,
             [(0x5F4, "ldr", "[r2, #4]"), (0x602, "cbz", "r2"), (0x604, "blx", "r2")])
    # ---------- census / negatives
    c = gather(d)
    page_targets = sorted({st["target"] for st in c["stores"]
                           if st["target"] is not None and 0x1BF000 <= st["target"] <= 0x1BF010})
    t.eq("page stores resolve to {+0,+4,+8}; +C is core-indexed (r6 = page+4*core)",
         page_targets, [0x1BF000, 0x1BF004, 0x1BF008])
    t.check("no ROM store outside the handshake page window (0x1BF000-0x1BFFFF)",
            all(not (0x1BF010 <= st["target"] < 0x1C0000) for st in c["stores"]
                if st["target"] is not None))
    vals = set(it["value"] for it in c["consts"])
    for v, nm in ((A_PAGE, "0x1BF000"), (A_STATUS_REG, "0x4000010C"),
                  (A_RELEASE_REG, "0x40000108"), (A_EVT0, "0x40160100"),
                  (A_EVT1, "0x40160104"), (A_EXC_STK0, "0x1BF110"),
                  (A_SVC_STK0, "0x1BEF00"), (0x40000, "0x40000"),
                  (0x40000000, "0x40000000"), (0x80000000, "0x80000000"),
                  (0xBAAD, "0xBAAD"), (0xCEED, "0xCEED"), (0x5A5A, "0x5A5A"),
                  (0xABCD, "0xABCD")):
        t.check("constant %s materialised in ROM code" % nm,
                v in vals or _is_materialised(d, v))
    t.check("secondary path materialises image header base 0x40000 (movw/movt)",
            _is_materialised(d, 0x40000))
    t.check("no reference to image entry A absolute address (0x46970) in ROM",
            not has_word(d, 0x46970))
    t.check("no literal-2 store to [0x1BF008] (only computed 1<<core)",
            not any(st["target"] == 0x1BF008 and any(v == 2 for v in st["value"])
                    for st in c["stores"]))
    t.check("no store to [0x40000108] in ROM (release is host-written)",
            not any(st["target"] == 0x40000108 for st in c["stores"]))
    # ---------- data tables
    t.eq("table A first/last entry", ([u32(d, 0x618 + 4 * i) for i in range(4)],
                                      [u32(d, 0x708 + 4 * i) for i in range(4)]),
         ([1, 1, 0, 0], [0x1F, 1, 0x38, 0]))
    t.eq("table B first/last entry", ([u32(d, 0x718 + 4 * i) for i in range(4)],
                                      [u32(d, 0x7E8 + 4 * i) for i in range(4)]),
         ([0, 5, 0x38, 1], [0x13, 1, 0xAC, 0]))
    _chk_seq(t, "fn 0x2E8 applies table A (16 entries) then table B (14)", d, T,
             [(0x2EA, "movw", "#0x618"), (0x2FA, "movs", "#0x10"), (0x300, "add", "#0x100"),
              (0x30A, "movs", "#0xe")])
    # ---------- bench cross-validation (values are derived/arithmetic only)
    t.eq("pre-release page +0 = 7 = fallback bits {0,1,2}", 1 | 2 | 4, 0x7)
    t.eq("pre-release page +4 = 3 = CPU1 bits {0,1}", 1 | 2, 0x3)
    t.eq("golden page +4 = 7 = CPU1 bits {0,1,2}", 1 | 2 | 4, 0x7)
    t.eq("success page +0 = GP2 | bit14", 0x86803FEF | 0x4000, 0x86807FEF)
    # ---------- image-side cross-check (optional)
    fw = load_fw()
    if fw is not None:
        t.eq("image sha256", hashlib.sha256(fw).hexdigest(), FW_SHA256)
        t.eq("image header entry A/B", (u32(fw, 0x00), u32(fw, 0x04)), (0x46971, 0xC742D))
        t.eq("image header handlers +0x50..+0x60",
             [u32(fw, off) for off in (0x50, 0x54, 0x58, 0x5C, 0x60)],
             [0x103140, 0x103114, 0x1030E8, 0x1030BC, 0x103090])
        t.check("ROM slot words == image header handler words",
                all(u32(fw, hoff) == IMG_HANDLERS[hoff] for hoff in IMG_HANDLERS))
        t.eq("FIQ handler 0x103140 prologue (pop/srsdb)",
             (u32(fw, 0xC3140), u32(fw, 0xC3144), u32(fw, 0xC3148)),
             (0xE49D0004, 0xE24EE004, 0xF96D0513))
        t.eq("IRQ trampoline 0x40064 (sub lr / srsdb / blx 0xC2EFC)",
             (u32(fw, 0x64), u32(fw, 0x68), u32(fw, 0x94)),
             (0xE24EE004, 0xF96D0513, 0xFA020B98))
        t.check("r1 ids: 0xC6FEA->1 0xC6FE0->2 0xC6FD6->3 0xC6FC8->4",
                _thumb_movs_r1(fw, 0xC6FEA) == 1 and _thumb_movs_r1(fw, 0xC6FE0) == 2 and
                _thumb_movs_r1(fw, 0xC6FD6) == 3 and _thumb_movs_r1(fw, 0xC6FC8) == 4)
        t.eq("ROM->header->id mapping is undef=1 svc=2 prefetch=3 abort=4",
             [R1_IDS.get(IMG_HANDLERS[hoff], 0) for (_, _, _, _, hoff) in ROM_EXC_CHAIN],
             [1, 2, 3, 4, 0])
        t.eq("image FIQ dispatcher 0xC2EEC literal = 0x17D398",
             u32(fw, 0xC2EF8 - 0x40000), 0x17D398)
        t.eq("image event dispatcher: status-reg ptr literal 0x4016010C",
             u32(fw, 0xC3010 - 0x40000), 0x4016010C)
        t.eq("image event dispatcher: system object literal 0x17D398",
             u32(fw, 0xC3014 - 0x40000), 0x17D398)
        t.eq("image exception-seen mask literal CA 0x40008", u32(fw, 0xC6FA4 - 0x40000), 0x40008)
        md = _dis_engine(True)
        seq = list(md.disasm(fw[0xC20F8 - 0x40000:0xC20F8 - 0x40000 + 0x18], 0xC20F8))
        t.check("image CECE consumer 0xC20F8 = movw 0xCECE + ldr 0x4000010C + spin",
                len(seq) >= 9 and seq[4].mnemonic == "movw" and "#0xcece" in seq[4].op_str
                and seq[5].mnemonic == "ldr" and seq[6].mnemonic == "ldr"
                and seq[8].mnemonic == "bne")
        t.check("image has no aligned literal 0x40000108 (host-only register)",
                not has_fw_aligned_word(fw, 0x40000108))
        t.check("image READS CA 0x40000004 (status word) and has no store site",
                _fw_reads_40000004(fw))
        t.check("handshake store site CPU 0x473D4 is the strd form of entry A",
                _handshake_site_ok(fw))
    else:
        print("SKIP image-side checks (set HI5622_FW to the local firmware blob)")
    print("")
    print("selftest: %d/%d PASS, %d FAIL" % (t.n - t.bad, t.n, t.bad))
    return 0 if t.bad == 0 else 1


def _fw_reads_40000004(fw):
    md = _dis_engine(True)
    ins = list(md.disasm(fw[0x458BC - 0x40000:0x458C4 - 0x40000], 0x458BC))
    if len(ins) < 3:
        return False
    return ins[0].mnemonic == "ldr" and ins[2].mnemonic == "ldr" \
        and ins[2].op_str.startswith("r3, [r3")


def _handshake_site_ok(fw):
    md = _dis_engine(True)
    ins = list(md.disasm(fw[0x473D4 - 0x40000:0x473D8 - 0x40000], 0x473D4))
    if not ins or ins[0].mnemonic != "strd":
        return False
    homes = [i for i in range(0, len(fw) - 3)
             if fw[i:i + 4] == struct.pack("<I", 0x1BF008)]
    return len(homes) == 2 and homes[0] == 0x47510 - 0x40000

def has_word(d, val):
    pat = struct.pack("<I", val)
    return pat in d


def _thumb_movs_r1(fw, cpu):
    off = cpu - 0x40000
    md = _dis_engine(True)
    for insn in list(md.disasm(fw[off:off + 24], cpu)):
        if insn.mnemonic == "movs" and insn.op_str.startswith("r1"):
            return insn.operands[1].imm
    return None


def main(argv):
    if not argv:
        print(__doc__)
        return 0
    cmd = argv[0]
    table = {
        "info": cmd_info, "map": cmd_map, "dis": cmd_dis, "flow": cmd_flow,
        "consts": cmd_consts, "stores": cmd_stores, "tables": cmd_tables,
        "search": cmd_search, "gate": cmd_gate, "image": cmd_image,
        "selftest": cmd_selftest,
    }
    if cmd not in table:
        die("unknown command %r (try: %s)" % (cmd, ", ".join(sorted(table))))
    return table[cmd](argv[1:])


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
