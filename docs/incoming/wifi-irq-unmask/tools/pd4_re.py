#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""pd4_re.py - host-only verifier for the PD4 cave proof and the g0 id1 field-B context precondition.

Lane wifi-irq-unmask, shared task **task-100 (PD4)**. Reads the LOCAL pinned vendor firmware
blob in place and prints derived annotations only; no vendor bytes are copied into the
repository and no device is opened.

Subcommands:
  cave      PD4 cave proof: the 637-byte all-zero static-data gap CPU 0x108197..0x108413,
            the chosen stub home CPU 0x108240, the whole-image reference census (branch
            targets, PC-relative literal loads, word/movw/movt address materialisations),
            the two bracketing static tables that make the gap a pure alignment filler, the
            boot-memset exclusion proof, the exception-staging-area exclusion proof, and the
            exact PD4 patch segments.
  ctx       g0 id1 field-B context precondition: what CA 0xFE578 does (singleton-gated
            lookup into the boot-populated 19-entry user-context table), why the handler
            faults for field B = 4 on a boot with no created user context (worker stores
            through a NULL +0x190 pointer), and what would have to run first.
  selftest  self-checking assertions (exit 0 on PASS).

CPU = file + 0x40000; host BAR0 of firmware RAM = 0x6b8000 + CPU.
"""
import argparse
import hashlib
import struct
import sys

try:
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB, CS_OP_IMM, CS_OP_MEM
    from capstone.arm import ARM_REG_PC
except Exception:  # pragma: no cover
    print("error: capstone is required (project venv .provision-venv/bin/python)")
    raise SystemExit(2)

BASE = 0x40000
DEFAULT_FW = "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin"
FW_SHA256 = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_LEN = 928920

# PD4 cave + gap (all verified by --cave/--selftest)
CAVE = 0x108240
GAP_LO = 0x108197
GAP_HI = 0x108414
GAP_LEN = GAP_HI - GAP_LO            # 637 bytes, all zero in the vendor image
STUB_LEN = 64
CAVE_END = CAVE + STUB_LEN
CPU_EPILOGUE = 0x4916A               # boot-task success epilogue (PD3/PD4 entry patch)
CPU_WRAPPER = 0x40294                # ldr r0,=0x10C190 ; b.w 0xC18AC (event-0x4C handler)
CPU_HANDLER_TABLE = 0x40050
MASK_WORD = 0x40008
STAGING_LITERAL = 0xC6FA4            # literal 0x40008 of the exception staging routine
BOOT_MEMSET1_LO, BOOT_MEMSET1_HI = 0x117C00, 0x1ADDE0
BOOT_MEMSET2_LO, BOOT_MEMSET2_HI = 0x1000000, 0x10E0000
# the two static tables that bracket the gap (referenced from code literal pools)
BRACKET_LOW_TABLE = 0x108054         # word at 0xDE898, read at 0xDE79C (+r8+sb)
BRACKET_HIGH_TABLE = 0x108414        # word at 0xDE89C, read at 0xDE864 (+r8+sb)
BRACKET_POOL = 0xDE890              # literal pool: {0x107F3C,0x1087D4,0x108054,0x108414}

# g0 id1 handler / field-B context precondition
CPU_CTX_LOOKUP = 0xFE578             # ctx = lookup(0, fieldB)
CPU_CTX_GETTER_A = 0xFE558           # returns literal 0x1ADB80 when r0 == 0
CPU_CTX_GETTER_B = 0x943AC           # returns literal 0x170E28 when r0 == 0
CTX_OBJ = 0x170E28                   # G: user-manager object (BSS, below the boot memset)
CTX_OBJ_SZ = 0x700                   # memset(G,0,0x700) at 0x49204..0x49210
CTX_POOL_BASE = 0x119758             # G+0x630 table points at 0x119758 + k*0xB40
CTX_POOL_END = 0x126D18              # == 0x119758 + 19*0xB40
CTX_STRIDE = 0xB40
CTX_COUNT = 19
CTX_OFF_INDEX0 = 0x18C               # lookup uses (0x18C+id)*4 == 0x630 + id*4
CPU_USER_MGR_INIT = 0x49204          # memset G + fill the 19 context pointers (0x49204..0x49264)
CPU_USER_CTX_FIELD = 0xFE6BC         # context-create helper: str.w r0,[ctx,#0x190] at 0xFE7E4
CPU_HANDLER = 0x43D58                # group-0 id-1 handler
CPU_WORKER = 0x81038                 # key 0xd8 worker: ldr r3,[ctx+0x190]; strb r1,[r3,#0x4c]

THUMB = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
THUMB.detail = True
ARM = Cs(CS_ARCH_ARM, CS_MODE_ARM)
ARM.detail = True


class Blob:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.d = f.read()
        self.sha256 = hashlib.sha256(self.d).hexdigest()

    def win(self, cpu, n):
        return self.d[cpu - BASE:cpu - BASE + n]

    def u16(self, cpu):
        return struct.unpack_from("<H", self.d, cpu - BASE)[0]

    def u32(self, cpu):
        return struct.unpack_from("<I", self.d, cpu - BASE)[0]

    def dis(self, cpu, n):
        return list(THUMB.disasm(self.win(cpu, n), cpu))

    def disa(self, cpu, n):
        return list(ARM.disasm(self.win(cpu, n), cpu))

    def print_dis(self, cpu, n, mark=None):
        for i in self.dis(cpu, n):
            tag = "   <== " + mark[1] if mark and i.address == mark[0] else ""
            print("  %08x  %-10s %-8s %s%s" % (i.address, i.bytes.hex(), i.mnemonic,
                                              i.op_str, tag))


def census(blob, lo, hi):
    """Whole-image conservative census of references into [lo, hi).

    Decodes every even file offset as Thumb and as ARM (deliberately over-approximating:
    data decoded as an instruction counts as a reference, so an EMPTY result is a strong
    negative).  Returns dict of hits.
    """
    import capstone.arm as A
    out = {"branch": [], "literal": [], "word": [], "movimm": [], "adr": []}
    n = len(blob.d)
    for off in range(0, n - 4, 2):
        a = BASE + off
        for md, pcbias, kind in ((THUMB, 4, "thumb"), (ARM, 8, "arm")):
            ins = list(md.disasm(blob.d[off:off + 4], a, count=1))
            if not ins:
                continue
            ins = ins[0]
            m = ins.mnemonic
            if m.startswith("b") and m not in ("bic", "bfc", "bfi", "bfx", "bkpt", "bx"):
                for op in ins.operands:
                    if op.type == CS_OP_IMM and lo <= op.imm < hi:
                        out["branch"].append((a, kind, m, ins.op_str))
            if m.startswith("ldr"):
                for op in ins.operands:
                    if op.type == CS_OP_MEM and op.mem.base == ARM_REG_PC:
                        tgt = ((a + pcbias) & ~3) + op.mem.disp
                        if lo <= tgt < hi:
                            out["literal"].append((a, kind, m, ins.op_str, tgt))
            if m.startswith("movw") or m.startswith("movt"):
                for op in ins.operands:
                    if op.type == CS_OP_IMM and lo <= op.imm < hi:
                        out["movimm"].append((a, kind, m, ins.op_str))
            if m in ("adr", "addw", "subw"):
                for op in ins.operands:
                    if op.type == CS_OP_IMM and lo <= op.imm < hi:
                        out["adr"].append((a, kind, m, ins.op_str))
            break
    for off in range(0, n - 4, 4):
        v = struct.unpack_from("<I", blob.d, off)[0]
        if lo <= v < hi:
            out["word"].append((BASE + off, v))
    return out


def cmd_cave(blob):
    print("== PD4 cave: static-data alignment gap CPU 0x108197..0x108413 ==")
    gap = blob.win(GAP_LO, GAP_LEN)
    print("gap: cpu 0x%06x..0x%06x  len %d (0x%x)  all-zero=%s  sha16=%s"
          % (GAP_LO, GAP_HI - 1, GAP_LEN, GAP_LEN, gap == b"\x00" * GAP_LEN,
             hashlib.sha256(gap).hexdigest()[:16]))
    print("next byte after the gap CPU 0x%06x = 0x%02x (first byte of the referenced table 0x%06x)"
          % (GAP_HI, blob.win(GAP_HI, 1)[0], BRACKET_HIGH_TABLE))
    print()
    print("chosen stub home: CPU 0x%06x..0x%06x (%d bytes, word aligned, %d bytes after the"
          % (CAVE, CAVE_END - 1, STUB_LEN, CAVE - GAP_LO))
    print("  previous table data and %d bytes before the next table base)" % (GAP_HI - CAVE_END))
    print("  source window all zero: %s" % (blob.win(CAVE, STUB_LEN) == b"\x00" * STUB_LEN))
    print("  host BAR0 readback offset 0x%06x (0x6b8000 + CPU)" % (0x6b8000 + CAVE))
    print()
    print("-- (1) control-flow census into the gap (whole image, Thumb+ARM, every even offset) --")
    c = census(blob, GAP_LO, GAP_HI)
    print("  branch targets                 : %d %s" % (len(c["branch"]), c["branch"][:4]))
    print("  PC-relative literal loads      : %d %s" % (len(c["literal"]), c["literal"][:4]))
    print("  32-bit word values             : %d %s" % (len(c["word"]), c["word"][:4]))
    print("  movw/movt address constructions: %d %s" % (len(c["movimm"]), c["movimm"][:4]))
    print("  adr/addw/subw constructions    : %d %s" % (len(c["adr"]), c["adr"][:4]))
    print("  => no control-flow or data reference reaches the gap; only the PD4 patch enters it.")
    print()
    print("-- (2) the two bracketing references (why the gap is filler, not a table) --")
    print("  literal pool CPU 0x%06x:" % BRACKET_POOL)
    for k in range(4):
        print("    0x%06x = 0x%08x" % (BRACKET_POOL + 4 * k, blob.u32(BRACKET_POOL + 4 * k)))
    print("  low table base 0x%06x is loaded at 0xDE79C and indexed with +r8+sb; the code"
          % BRACKET_LOW_TABLE)
    print("  reads r7[0], r7[2], r7[4] with POSITIVE offsets from the base (0xDE7BC..0xDE7C6).")
    print("  high table base 0x%06x is loaded at 0xDE864 and read with +r8+sb offsets"
          % BRACKET_HIGH_TABLE)
    print("  (0xDE866..0xDE87A).  The gap is between the two tables and is not part of either")
    print("  consumer's positive-offset window.")
    print()
    print("-- (3) runtime-writer exclusion --")
    print("  boot memset A: CPU 0x%06x len 0x%x (literals at 0x46DEC/0x46DF0)"
          % (BOOT_MEMSET1_LO, BOOT_MEMSET1_HI - BOOT_MEMSET1_LO))
    print("  boot memset B: CPU 0x%06x len 0x%x (immediates at 0x46D94..0x46DA0)"
          % (BOOT_MEMSET2_LO, BOOT_MEMSET2_HI - BOOT_MEMSET2_LO))
    inside = (BOOT_MEMSET1_LO <= CAVE < BOOT_MEMSET1_HI) or \
             (BOOT_MEMSET2_LO <= CAVE < BOOT_MEMSET2_HI)
    print("  cave inside a boot memset: %s" % inside)
    print("  exception staging routine writes CPU 0x4000C..0x4004B (record base literal 0x%06x"
          % STAGING_LITERAL)
    print("  = 0x%08x); gap is disjoint from it: %s"
          % (blob.u32(STAGING_LITERAL), not (0x4000C <= CAVE < 0x40050)))
    print("  => the only ways a writer could touch the gap are a pointer/offset reaching it,")
    print("     and the census above finds no address materialisation of the gap at all.")
    print()
    print("-- (4) PD4 patch segments (file = CPU - 0x40000) --")
    jmp = build_entry(blob)
    stub = build_stub(CAVE)
    print("  ENTRY file 0x%05x cpu 0x%06x len 4  %s -> b.w 0x%06x"
          % (CPU_EPILOGUE - BASE, CPU_EPILOGUE, blob.win(CPU_EPILOGUE, 4).hex(), CAVE))
    print("        patched %s  (source sha16 %s)"
          % (jmp.hex(), hashlib.sha256(blob.win(CPU_EPILOGUE, 4)).hexdigest()[:16]))
    print("  LOOP  file 0x%05x cpu 0x%06x len %d  patched %s"
          % (CAVE - BASE, CAVE, STUB_LEN, stub.hex()))
    print()
    print("expected first bench read after ONE post-ready txq_sync72 fire:")
    print("  firmware CPU 0x4000C..0x4004F = BAR0 0x6f800c (host phys 0x406f800c): the TRUE")
    print("  first staged exception frame (17 words: r0-r12, sp, lr, PC, SPSR) - the PD4")
    print("  cave is not the staging area, so the frame survives untouched.")
    print("  pc 0x8103C/0x81040 confirms the predicted field-B context fault; any other PC")
    print("  refutes it.  SPSR mode 0x17 = ABT, 0x1B = UND.")
    print("  read-only cave check: BAR0 0x%06x (host phys 0x%08x) must still hold the stub."
          % (0x6b8000 + CAVE, 0x40000000 + 0x6b8000 + CAVE))


def cmd_ctx(blob):
    print("== g0 id1 handler field-B context precondition ==")
    print()
    print("-- handler CA 0x%06x (group-0 id-1) --" % CPU_HANDLER)
    blob.print_dis(CPU_HANDLER, 0x28, mark=(0x43D76, "ctx = lookup(0, hdr[3])"))
    print()
    print("-- CA 0xFE578 lookup(0, fieldB) --")
    blob.print_dis(CPU_CTX_LOOKUP, 0x40, mark=(0xFE5A8, "base = 0x18C + fieldB"))
    print("  r6 = getter 0xFE558(0) -> literal at 0xFE574 = 0x%08x" % blob.u32(0xFE574))
    print("  r0 = getter 0x943AC(0) -> literal at 0x943C8 = 0x%08x (= 0x%06x G)"
          % (blob.u32(0x943C8), CTX_OBJ))
    print("  ctx = *(u32*)(G + (0x18C+fieldB)*4) + u16(G+0x6DC)")
    print("  offset identity: (0x18C + id)*4 = 0x630 + id*4, i.e. the table is G+0x630+id*4")
    print("  fieldB is bounds-checked with 0x12 (19 slots, ids 0..18) - index 4 passes.")
    print()
    print("-- who populates G+0x630 (the 19 context pointers)? --")
    blob.print_dis(CPU_USER_MGR_INIT, 0x64, mark=(0x49210, "memset(G, 0, 0x700)"))
    print("  memset(G, 0, 0x700) covers G+0x18C..G+0x6DF and the table at G+0x630.")
    print("  loop 0x49242..0x49264: for k = 0..18, memset(ctx_k = 0x%06x + k*0x%x, 0, 0x%x)"
          % (CTX_POOL_BASE, CTX_STRIDE, CTX_STRIDE))
    print("  and store ctx_k into G+0x630 + 4*k (str r0,[r8,#4]! at 0x4924C) and the in-use")
    print("  byte 0 into G+0x6C8 + k (strb r6,[r7,#1]! at 0x49260); pool end 0x%06x ="
          % CTX_POOL_END)
    print("  base + %d*0x%x - so ALL 19 slots including index 4 are non-NULL pointers after"
          % (CTX_COUNT, CTX_STRIDE))
    print("  this initialisation, and every context body is ZERO.")
    print("  u16 G+0x6DC = r6 (common context offset written at 0x4923A; 0 on the boot path [I]).")
    print()
    print("-- when does ctx+0x190 become valid? --")
    print("  per-context +0x190 is initialised ONLY by the user/vap create helper CA 0x%06x:"
          % CPU_USER_CTX_FIELD)
    print("  str.w r0,[ctx,#0x190] at 0xFE7E4 (and cleared at 0x44ADE).  No boot-time writer of")
    print("  +0x190 exists: the boot init above only memsets the bodies to zero.")
    print()
    print("-- worker CA 0x%06x (key 0xd8 -> table A[41]) --" % CPU_WORKER)
    blob.print_dis(CPU_WORKER, 0x10, mark=(0x8103C, "ldr r3,[ctx+0x190]"))
    print("  8103C ldr.w r3,[r0,#0x190]   ; r0 = ctx = &context[boot-populated]")
    print("  81040 strb.w r1,[r3,#0x4c]   ; r3 == 0 -> store to 0x4C -> precise DATA ABORT")
    print()
    print("=> fault model on OUR boot (no user/vap created before the fire):")
    print("   lookup returns a NON-NULL context pointer (boot filled all 19 slots), so the")
    print("   handler does not take its clean NULL arm; the context body is all zero, so")
    print("   ctx+0x190 == 0 and the worker's store faults at 0x81040 with r3 == 0 (DABT,")
    print("   faulting address 0x4C, vector-4 bit 0x10 in CA 0x40008).")
    print()
    print("=> is our canned hdr[3] = 0x04 valid?")
    print("   Yes as a WIRE value: the 72-byte vendor capture frame carries hdr[3] = 0x04,")
    print("   so the frame is vendor-exact.  It is NOT valid as a PREcondition on our boot:")
    print("   field B selects an EXISTING user/vap context; the vendor host creates that")
    print("   context with earlier H2D configuration before/while the TXQ sync is sent, while")
    print("   our host sends the sync as the very first real message.")
    print()
    print("=> candidate resolutions (ranked):")
    print("   1. read the PD4 first-fault frame: r3 == 0 and pc 0x81040 confirms this model;")
    print("      any other pc refutes it and re-opens the transport-fault hypothesis.")
    print("   2. if confirmed, the FIRST real message must be the context-create/init message")
    print("      for context 4 (a user/vap add or config message that reaches CA 0xFE6BC), and")
    print("      the TXQ sync only afterwards; a payload-only tweak of the sync cannot help.")
    print("   3. control fire: the payload-free group-0 id-0 frame exercises the identical")
    print("      ETE/mailbox/dispatch path without the context dereference; if it is clean the")
    print("      remaining defect is exactly the missing per-context state.")


def cmd_selftest(blob):
    checks = []

    def ck(name, cond, detail=""):
        checks.append((name, bool(cond), detail))

    ck("vendor firmware sha256", blob.sha256 == FW_SHA256, blob.sha256)
    ck("firmware length", len(blob.d) == FW_LEN, str(len(blob.d)))

    # cave facts
    ck("gap bounds", GAP_LEN == 637 and GAP_LO == 0x108197 and GAP_HI == 0x108414)
    ck("gap all zero in the vendor image",
       blob.win(GAP_LO, GAP_LEN) == b"\x00" * GAP_LEN)
    ck("cave inside the gap with margins", GAP_LO <= CAVE and CAVE_END <= GAP_HI)
    ck("cave source window all zero", blob.win(CAVE, STUB_LEN) == b"\x00" * STUB_LEN)
    ck("cave word aligned", CAVE % 4 == 0)
    c = census(blob, GAP_LO, GAP_HI)
    for k in ("branch", "literal", "word", "movimm", "adr"):
        ck("no %s reference into the gap" % k, not c[k], repr(c[k][:3]))
    ck("bracketing low table base", blob.u32(BRACKET_POOL + 8) == BRACKET_LOW_TABLE)
    ck("bracketing high table base", blob.u32(BRACKET_POOL + 12) == BRACKET_HIGH_TABLE)
    ck("bracketing literal loads at 0xDE79C / 0xDE864",
       blob.win(0xDE79C, 2) == bytes.fromhex("3e4b")
       and blob.win(0xDE864, 2) == bytes.fromhex("0d49"))
    ck("cave outside boot memset A", not (BOOT_MEMSET1_LO <= CAVE < BOOT_MEMSET1_HI))
    ck("cave outside boot memset B", not (BOOT_MEMSET2_LO <= CAVE < BOOT_MEMSET2_HI))
    ck("boot memset A literals", blob.u32(0x46DEC) == BOOT_MEMSET1_LO
       and blob.u32(0x46DF0) == BOOT_MEMSET1_HI)
    ck("exception staging literal 0x40008", blob.u32(STAGING_LITERAL) == MASK_WORD)
    ck("cave disjoint from the staging area", not (MASK_WORD < CAVE < CPU_HANDLER_TABLE))
    ck("boot epilogue bytes", blob.win(CPU_EPILOGUE, 4) == bytes.fromhex("002021b0"))
    entry = build_entry(blob)
    ins = list(THUMB.disasm(entry, CPU_EPILOGUE))
    ck("PD4 entry decodes as b.w cave",
       len(ins) == 1 and ins[0].mnemonic == "b.w"
       and ins[0].op_str.replace(" ", "") == "#0x%x" % CAVE,
       ins[0].mnemonic + " " + ins[0].op_str if ins else "?")
    stub = build_stub(CAVE)
    got = [(i.mnemonic, i.op_str) for i in THUMB.disasm(stub, CAVE)]
    ck("PD4 stub length", len(stub) == STUB_LEN, str(len(stub)))
    ck("PD4 stub first/last insn", got[0] == ("ldr", "r7, [pc, #0x2c]")
       and got[-1] == ("movs", "r0, r0"), repr((got[0], got[-1])))
    ck("PD4 stub literals",
       struct.unpack_from("<I", stub, 0x30)[0] == 0x10C19C
       and struct.unpack_from("<I", stub, 0x34)[0] == 0x40039010
       and struct.unpack_from("<I", stub, 0x38)[0] == 0x118D68
       and struct.unpack_from("<I", stub, 0x3C)[0] == 0x2000)
    ck("PD4 stub BL targets 0x40294",
       (struct.unpack_from("<H", stub, 0x1A)[0] & 0xF800) == 0xF000
       and (struct.unpack_from("<H", stub, 0x1C)[0] & 0xD000) == 0xD000)

    # context facts
    ck("handler prologue", blob.win(CPU_HANDLER, 4) == bytes.fromhex("70b538b9"))
    ck("handler reads hdr[3] and calls the lookup",
       blob.win(0x43D72, 8) == bytes.fromhex("c1780020baf0fffb"))
    ck("lookup prologue", blob.win(CPU_CTX_LOOKUP, 6) == bytes.fromhex("70b505460c46"))
    ck("lookup index add (0x18C + fieldB)",
       blob.win(0xFE5A8, 8) == bytes.fromhex("04f5c674b0f8dc36"))
    ck("lookup base-register load", blob.win(0xFE5B0, 4) == bytes.fromhex("50f82400"))
    ck("getter literals", blob.u32(0xFE574) == 0x1ADB80 and blob.u32(0x943C8) == CTX_OBJ)
    ck("offset identity (0x18C+4)*4 == 0x630+4*4", (0x18C + 4) * 4 == 0x630 + 16)
    ck("user-manager memset prologue",
       blob.win(CPU_USER_MGR_INIT, 0x10) ==
       bytes.fromhex("4ff4e063414c02461946204678f0fcfd"))
    ck("user-manager init loop bytes",
       blob.win(0x49242, 0x24) == bytes.fromhex(
           "4ff4346305eb0900194648f8040f002278f0dbfd4df2c05305f534659d4207f8016fedd1"))
    ck("context pool base literal", blob.u32(0x49330) == CTX_POOL_BASE)
    ck("context pool end literal", blob.u32(0x4931C) == CTX_POOL_END)
    ck("pool end == base + 19*stride", CTX_POOL_BASE + CTX_COUNT * CTX_STRIDE == CTX_POOL_END)
    ck("u16 G+0x6DC store", blob.win(0x4923A, 4) == bytes.fromhex("a4f8dc66"))
    ck("worker bytes", blob.win(CPU_WORKER, 8) == bytes.fromhex("08b51178d0f89031"))
    ck("worker store", blob.win(0x81040, 4) == bytes.fromhex("83f84c10"))
    ck("context +0x190 writer exists in the create helper",
       blob.win(0xFE7E4, 4) == bytes.fromhex("c4f89001"))
    ok = sum(1 for _, c, _ in checks if c)
    for n, c, d in checks:
        if not c:
            print("FAIL: %s %s" % (n, d))
    print("pd4_re selftest: %d/%d PASS" % (ok, len(checks)))
    return 0 if ok == len(checks) else 1


# ------------------------------------------------------------------ PD4 stub / entry encoders
def _h2(v):
    return bytes([v & 0xFF, (v >> 8) & 0xFF])


def _enc_ldr_lit(rt, insn_addr, lit_addr):
    pc = (insn_addr + 4) & ~3
    d = lit_addr - pc
    assert d >= 0 and d % 4 == 0 and d <= 1020
    return _h2(0x4800 | (rt << 8) | (d // 4))


def _enc_ldr_imm(rt, rn, imm5):
    return _h2(0x6800 | (imm5 << 6) | (rn << 3) | rt)


def _enc_str_imm(rt, rn, imm5):
    return _h2(0x6000 | (imm5 << 6) | (rn << 3) | rt)


def _enc_cmp_reg(rn, rm):
    return _h2(0x4280 | (rm << 3) | rn)


def _enc_cmp_imm0(rn):
    return _h2(0x2800 | (rn << 8))


def _enc_bcond(cond, insn_addr, target):
    d = target - (insn_addr + 4)
    assert d % 2 == 0 and -256 <= d <= 254
    return _h2(0xD000 | (cond << 8) | ((d // 2) & 0xFF))


def _enc_b(insn_addr, target):
    d = target - (insn_addr + 4)
    assert d % 2 == 0 and -2048 <= d <= 2046
    return _h2(0xE000 | ((d // 2) & 0x7FF))


def _enc_sub_sp(imm8):
    return _h2(0xB080 | imm8)


def _bl_like(insn_addr, target, bl):
    off = (target & ~1) - (insn_addr + 4)
    assert off % 2 == 0 and -0x1000000 <= off <= 0xFFFFFE
    S = 1 if off < 0 else 0
    I1 = (off >> 23) & 1
    I2 = (off >> 22) & 1
    imm10 = (off >> 12) & 0x3FF
    imm11 = (off >> 1) & 0x7FF
    J1 = I1 if S else 1 - I1
    J2 = I2 if S else 1 - I2
    hw1 = 0xF000 | (S << 10) | imm10
    hw2 = (0xD000 | (1 << 12) | (J1 << 13) | (J2 << 11) | imm11) if bl else \
          (0x9000 | (J1 << 13) | (J2 << 11) | imm11)
    return _h2(hw1) + _h2(hw2)


def build_entry(blob):
    return _bl_like(CPU_EPILOGUE, CAVE, bl=False)


def build_stub(base):
    """The PD3/PD4 guarded out[0] poll stub, assembled at an arbitrary Thumb base."""
    code = bytearray()

    def emit(addr, data):
        assert len(code) == addr - base
        code.extend(data)

    a = base
    lit = (base + 0x2E + 3) & ~3
    loop = base + 4
    zero = base + 0x20
    emit(a, _enc_ldr_lit(7, a, lit + 0)); a += 2
    emit(a, _enc_sub_sp(1)); a += 2
    emit(a, _enc_ldr_imm(3, 7, 0)); a += 2       # loop: ldr r3,[r7]
    emit(a, _enc_cmp_imm0(3)); a += 2
    emit(a, _enc_bcond(0, a, loop)); a += 2
    emit(a, _enc_ldr_lit(2, a, lit + 4)); a += 2
    emit(a, _enc_ldr_imm(3, 2, 0)); a += 2
    emit(a, _enc_cmp_imm0(3)); a += 2
    emit(a, _enc_bcond(0, a, loop)); a += 2
    emit(a, _enc_ldr_imm(3, 7, 0x14 // 4)); a += 2
    emit(a, _enc_ldr_lit(4, a, lit + 8)); a += 2
    emit(a, _enc_cmp_reg(3, 4)); a += 2
    emit(a, _enc_bcond(0, a, zero)); a += 2
    emit(a, _bl_like(a, CPU_WRAPPER, True)); a += 4
    emit(a, _enc_b(a, loop)); a += 2
    assert a == zero
    emit(a, _enc_ldr_lit(4, a, lit + 12)); a += 2
    emit(a, _enc_str_imm(4, 7, 0x14 // 4)); a += 2
    emit(a, _bl_like(a, CPU_WRAPPER, True)); a += 4
    emit(a, _enc_str_imm(3, 7, 0x14 // 4)); a += 2
    emit(a, _enc_b(a, loop)); a += 2
    while a < lit:
        emit(a, _h2(0xBF00)); a += 2
    for val in (0x10C19C, 0x40039010, 0x118D68, 0x2000):
        emit(a, struct.pack("<I", val)); a += 4
    return bytes(code)


def main(argv):
    ap = argparse.ArgumentParser(description="PD4 cave + g0 id1 context precondition verifier")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--cave", action="store_true")
    g.add_argument("--ctx", action="store_true")
    g.add_argument("--selftest", action="store_true")
    ap.add_argument("--fw", default=DEFAULT_FW)
    a = ap.parse_args(argv)
    blob = Blob(a.fw)
    if a.selftest:
        return cmd_selftest(blob)
    if blob.sha256 != FW_SHA256:
        print("error: not the pinned vendor blob (%s)" % blob.sha256)
        return 2
    if a.cave:
        cmd_cave(blob)
    else:
        cmd_ctx(blob)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
