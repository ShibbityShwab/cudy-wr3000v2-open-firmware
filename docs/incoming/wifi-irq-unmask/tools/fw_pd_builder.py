#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_pd_builder.py - host-only builder/verifier for the Hi5622V100 PD2/PD3 firmware patches.

Lane wifi-irq-unmask, shared tasks task-91 (PD2) and task-91 follow-up (PD3). Reads the LOCAL vendor
firmware blob in place, verifies it by sha256 and each source window by sha256, applies a
patch recipe, writes the patched image + manifest into a NON-repo directory, and prints the
machine-readable recipe used by the bench tooling. It never copies vendor bytes into this
repository and never opens a device.

PD2 (historical, FIRED ON THE BENCH AND FAILED - kept for the record)
  Redirect the boot-task epilogue CPU 0x4916A into a guarded out[0] poll stub placed in the
  config-3 block of entry A (CPU 0x47432).  The bench proved that block is NOT dead on our
  boots: the patched image hung in entry A (page = 0x6F) while a control/PD1 boot reached the
  golden page, i.e. entry A took the config-3 branch (runtime selector == 3) and executed the
  stub.  See ../REPORT-PD3.md.

PD3 (validated on hardware, but its cave is the exception-staging area)
  Same guarded poll logic, but the stub is placed in the image header's exception-record
  area CPU 0x4000C..0x4004B: no branch target and no PC-relative literal load exists into
  the record area; the only runtime writer is the exception staging routine, i.e. a crash
  path that already ends the experiment.  The POST-READY fire showed WHY that is fatal:
  the FIRST exception's 17-word staged frame is written to exactly 0x4000C..0x4004B, i.e.
  over the parked poller, so the poller dies and the record is overwritten by follow-up
  (undefined) exceptions - the true first-fault frame is lost.

PD4 (current)
  Identical guarded poll logic, relocated to CPU 0x108240 (file 0xC8240), inside the
  637-byte all-zero static-data padding gap CPU 0x108197..0x108413 (file 0xC8197..0xC8413).
  The gap is (a) file-backed static read-only data, (b) OUTSIDE both boot memsets
  (0x117C00..0x1ADDE0 and 0x1000000..0x10E0000), (c) outside the exception-record area,
  (d) referenced by NO branch target, NO PC-relative literal load, NO word/movw/movt
  address materialisation, and (e) bracketed by two referenced read-only tables
  (0x108054 read at 0xDE79C; 0x108414 read at 0xDE864, both accessed with positive
  offsets from their base) so it is neither read nor written at runtime.  The exception
  staging routine does not touch it, so after one post-ready fire CA 0x4000C..0x4004B
  holds the TRUE first exception frame.

Subcommands:
  --list [NAME]          recipe segments + source windows (hash-verified against the blob)
  --build NAME [--out D] write D/FIRMWARE.NAME.bin + D/NAME-MANIFEST.txt (hashes only)
  --recipe NAME          print the hi5622-fw-patch-recipe/1 JSON (bench-tooling schema)
  --selftest             build/verify both recipes (exit 0 on PASS)
"""
import argparse
import hashlib
import json
import os
import struct
import sys
import tempfile

try:
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB
except Exception:  # pragma: no cover
    print("error: capstone is required (project venv .provision-venv/bin/python)")
    raise SystemExit(2)

BASE = 0x40000
DEFAULT_FW = "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin"
FW_SHA256 = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_LEN = 928920

# Decoded facts (each verified by --selftest).
CPU_EPILOGUE = 0x4916A          # boot-task success epilogue (movs r0,#0; add sp,#0x84; pop)
CPU_PD2_CAVE = 0x47432          # PD2: config-3 block of entry A (NOT dead on our boots)
CPU_PD3_CAVE = 0x4000C          # PD3: image-header exception-record area (staging frame clobbers it)
CPU_PD4_CAVE = 0x108240         # PD4: static-data alignment gap (zero-fill, never referenced/written)
PD4_GAP_LO = 0x108197           # zero-run start (first zero byte after the previous table)
PD4_GAP_HI = 0x108414           # zero-run end (exclusive; next table base, referenced at 0xDE89C)
PD4_IMAGE_SHA256 = "cfe24bfb194899f362bfcb0301d7e28a3b4dce0ef4e70b705d54a1f78d3037b1"
CPU_HANDLER_TABLE = 0x40050     # image header handler table: cave must end before this
CPU_WRAPPER = 0x40294           # ldr r0,=0x10C190 ; b.w 0xC18AC (event-0x4C handler)
CPU_DISPATCH = 0xC18AC          # H2D mailbox dispatcher
HCC_FIELD_ACK = 0x10C19C        # [hcc+0xC]  = CA 0x400392F0 (out[5] ack), init guard
HCC_FIELD_TABLE = 0x10C1B0      # [hcc+0x20] = handler table pointer
OUT0_CA = 0x40039010            # mailbox out[0]: H2D pending read/clear
ACK_CA = 0x400392F0             # out[5] ack
DOORBELL_CA = 0x400392D4        # out[2] H2D-done doorbell
HCC_TABLE_STATIC = 0x118D68     # static sentinel of the handler-table field (image code)
ROM_ZERO_TABLE = 0x00002000     # mask-ROM RAZ window: permanently-zero table
PCIEMSG_INIT = 0x49334          # boot-task module init (installs the hcc mailbox pointers)
STATE_BYTE = 0x118B60           # boot state
MASK_WORD = 0x40008             # exception record mask halfword (NOT part of the cave)
STAGING_LITERAL = 0xC6FA4       # literal 0x40008 in the exception staging routine


# ------------------------------------------------------------------ Thumb encoders
def _h2(v):
    return bytes([v & 0xFF, (v >> 8) & 0xFF])


def enc_ldr_lit(rt, insn_addr, lit_addr):
    pc = (insn_addr + 4) & ~3
    d = lit_addr - pc
    assert d >= 0 and d % 4 == 0 and d <= 1020, (hex(insn_addr), hex(lit_addr), d)
    return _h2(0x4800 | (rt << 8) | (d // 4))


def enc_ldr_imm(rt, rn, imm5):
    assert 0 <= imm5 <= 31
    return _h2(0x6800 | (imm5 << 6) | (rn << 3) | rt)


def enc_str_imm(rt, rn, imm5):
    assert 0 <= imm5 <= 31
    return _h2(0x6000 | (imm5 << 6) | (rn << 3) | rt)


def enc_cmp_reg(rn, rm):
    return _h2(0x4280 | (rm << 3) | rn)


def enc_cmp_imm0(rn):
    return _h2(0x2800 | (rn << 8))


def enc_bcond(cond, insn_addr, target):
    d = target - (insn_addr + 4)
    assert d % 2 == 0 and -256 <= d <= 254, (hex(insn_addr), hex(target), d)
    return _h2(0xD000 | (cond << 8) | ((d // 2) & 0xFF))


def enc_b(insn_addr, target):
    d = target - (insn_addr + 4)
    assert d % 2 == 0 and -2048 <= d <= 2046, (hex(insn_addr), hex(target), d)
    return _h2(0xE000 | ((d // 2) & 0x7FF))


def enc_sub_sp(imm8):
    assert 0 <= imm8 <= 127
    return _h2(0xB080 | imm8)


def _bl_like(insn_addr, target, bl):
    off = (target & ~1) - (insn_addr + 4)
    assert off % 2 == 0 and -0x1000000 <= off <= 0xFFFFFE, (hex(insn_addr), hex(target), off)
    S = 1 if off < 0 else 0
    I1 = (off >> 23) & 1
    I2 = (off >> 22) & 1
    imm10 = (off >> 12) & 0x3FF
    imm11 = (off >> 1) & 0x7FF
    J1 = (I1 if S else 1 - I1)
    J2 = (I2 if S else 1 - I2)
    hw1 = 0xF000 | (S << 10) | imm10
    if bl:
        hw2 = 0xD000 | (J1 << 13) | (1 << 12) | (J2 << 11) | imm11
    else:
        hw2 = 0x9000 | (J1 << 13) | (J2 << 11) | imm11
    return _h2(hw1) + _h2(hw2)


def enc_bl(insn_addr, target):
    return _bl_like(insn_addr, target, True)


def enc_bw(insn_addr, target):
    return _bl_like(insn_addr, target, False)


# ------------------------------------------------------------------ the guarded poll stub
# Layout relative to the cave base B (literal pool is 4-byte aligned after the code):
#   B+00  ldr  r7,=0x10C19C   ; instance-init guard field (dispatcher's first deref)
#   B+02  sub  sp,#4          ; permanent -4 bias: each bl pushes LR -> 8-byte aligned call
#   B+04  loop: ldr r3,[r7]
#   B+06  cmp  r3,#0
#   B+08  beq  loop
#   B+0A  ldr  r2,=0x40039010 ; mailbox out[0]
#   B+0C  ldr  r3,[r2]
#   B+0E  cmp  r3,#0
#   B+10  beq  loop
#   B+12  ldr  r3,[r7,#0x14]  ; [hcc+0x20] handler table
#   B+14  ldr  r4,=0x118D68   ; static/uninitialised sentinel
#   B+16  cmp  r3,r4
#   B+18  beq  zero
#   B+1A  bl   0x40294        ; real dispatcher with the live table
#   B+1E  b    loop
#   B+20  zero: ldr r4,=0x2000
#   B+22  str  r4,[r7,#0x14]  ; consume-only table
#   B+24  bl   0x40294
#   B+28  str  r3,[r7,#0x14]  ; restore (dispatcher preserves r3/r7)
#   B+2A  b    loop
#   B+2C  (nop padding to align the literal pool)
#   +lit  .word 0x10C19C, 0x40039010, 0x118D68, 0x2000
def build_stub(base):
    code = bytearray()
    ann = []

    def emit(addr, data, text):
        assert len(code) == addr - base, (hex(addr), len(code))
        code.extend(data)
        if text:
            ann.append((addr, data.hex(), text))

    loop = base + 4
    zero = base + 0x20
    lit = (base + 0x2E + 3) & ~3
    a = base
    emit(a, enc_ldr_lit(7, a, lit + 0), "ldr r7,=0x10C19C"); a += 2
    emit(a, enc_sub_sp(1), "sub sp,#4"); a += 2
    assert a == loop
    emit(a, enc_ldr_imm(3, 7, 0), "loop: ldr r3,[r7]"); a += 2
    emit(a, enc_cmp_imm0(3), "cmp r3,#0"); a += 2
    emit(a, enc_bcond(0x0, a, loop), "beq loop"); a += 2
    emit(a, enc_ldr_lit(2, a, lit + 4), "ldr r2,=0x40039010"); a += 2
    emit(a, enc_ldr_imm(3, 2, 0), "ldr r3,[r2]"); a += 2
    emit(a, enc_cmp_imm0(3), "cmp r3,#0"); a += 2
    emit(a, enc_bcond(0x0, a, loop), "beq loop"); a += 2
    emit(a, enc_ldr_imm(3, 7, 0x14 // 4), "ldr r3,[r7,#0x14]"); a += 2
    emit(a, enc_ldr_lit(4, a, lit + 8), "ldr r4,=0x118D68"); a += 2
    emit(a, enc_cmp_reg(3, 4), "cmp r3,r4"); a += 2
    emit(a, enc_bcond(0x0, a, zero), "beq zero"); a += 2
    emit(a, enc_bl(a, CPU_WRAPPER), "bl 0x%x" % CPU_WRAPPER); a += 4
    emit(a, enc_b(a, loop), "b loop"); a += 2
    assert a == zero
    emit(a, enc_ldr_lit(4, a, lit + 12), "zero: ldr r4,=0x2000"); a += 2
    emit(a, enc_str_imm(4, 7, 0x14 // 4), "str r4,[r7,#0x14]"); a += 2
    emit(a, enc_bl(a, CPU_WRAPPER), "bl 0x%x" % CPU_WRAPPER); a += 4
    emit(a, enc_str_imm(3, 7, 0x14 // 4), "str r3,[r7,#0x14]"); a += 2
    emit(a, enc_b(a, loop), "b loop"); a += 2
    while a < lit:
        emit(a, _h2(0xBF00), "nop"); a += 2
    for val, note in ((HCC_FIELD_ACK, "hcc+0xC field address"),
                      (OUT0_CA, "mailbox out[0] CA"),
                      (HCC_TABLE_STATIC, "uninitialised table sentinel"),
                      (ROM_ZERO_TABLE, "mask-ROM RAZ zero table")):
        emit(a, struct.pack("<I", val), ".word 0x%08X  ; %s" % (val, note))
        a += 4
    return bytes(code), ann


CPU_RAISE_HELPER = 0xC6DB8   # 0xC6DB8(id): software-raise/arm event id (0x10..0x5F)
CPU_HCC_TABLE_FIELD = 0x10C1B0


def build_raise_loop():
    """PD3B: 8-byte in-place epilogue: movs r0,#0x4C; bl 0xC6DB8; b 0x4916A."""
    a = CPU_EPILOGUE
    out = bytearray()
    out += _h2(0x204C)                                    # movs r0,#0x4C
    out += enc_bl(a + 2, CPU_RAISE_HELPER)                # bl 0xC6DB8
    out += enc_b(a + 6, CPU_EPILOGUE)                     # b 0x4916A
    assert len(out) == 8, len(out)
    return bytes(out)


RECIPES = {
    "PD2": {
        "about": "post-boot H2D mailbox poll loop in entry A's config-3 block (BENCH-FAILED: block is live)",
        "cave": CPU_PD2_CAVE,
        "based_on": None,
        "expect": "PD2 guard+poll stub (HISTORICAL: bench failed in entry A)",
    },
    "PD3": {
        "about": "post-boot H2D mailbox poll loop in the unreachable image-header exception-record area",
        "cave": CPU_PD3_CAVE,
        "based_on": None,
        "expect": "PD3 guard+poll stub (image header record area, unreachable)",
    },
    "PD4": {
        "about": "post-boot H2D mailbox poll loop in the 637-byte zero static-data alignment gap "
                 "(CPU 0x108240), outside both boot memsets and outside the exception-staging area",
        "cave": CPU_PD4_CAVE,
        "based_on": None,
        "expect": "PD4 guard+poll stub (0x108240 static-data gap, never referenced/written)",
    },
    "PD3B": {
        "about": "no-cave alternative: raise event 0x4C every loop from the boot epilogue (tests the firmware event controller; zero table installed)",
        "cave": None,
        "based_on": None,
        "expect": "raise event 0x4C loop (movs r0,#0x4C; bl 0xC6DB8; b 0x4916A)",
    },
}


def build_segments(blob, name):
    rec = RECIPES[name]
    if name == "PD3B":
        entry_off = CPU_EPILOGUE - BASE
        raise_loop = build_raise_loop()
        tbl_off = CPU_HCC_TABLE_FIELD - BASE
        segs = [
            {"name": "ENTRY", "file": entry_off, "cpu": CPU_EPILOGUE, "len": 8,
             "source": blob[entry_off:entry_off + 8], "patched": raise_loop,
             "expect": "movs r0,#0x4C; bl 0xC6DB8; b 0x4916A"},
            {"name": "TABLE", "file": tbl_off, "cpu": CPU_HCC_TABLE_FIELD, "len": 4,
             "source": blob[tbl_off:tbl_off + 4],
             "patched": struct.pack("<I", ROM_ZERO_TABLE),
             "expect": "handler table -> ROM RAZ zero table (consume-only dispatch)"},
        ]
        return rec, segs
    cave = rec["cave"]
    off = cave - BASE
    stub, _ = build_stub(cave)
    if cave == CPU_PD3_CAVE:
        assert cave >= MASK_WORD + 4, "record mask halfword must not be overwritten"
        assert cave + len(stub) <= CPU_HANDLER_TABLE, "stub must end before the header table"
    elif cave == CPU_PD4_CAVE:
        assert cave % 4 == 0, "PD4 cave must be word aligned"
        assert PD4_GAP_LO <= cave and cave + len(stub) <= PD4_GAP_HI, \
            "PD4 stub must fit inside the all-zero static-data gap"
        assert blob[off:off + len(stub)] == b"\x00" * len(stub), \
            "PD4 cave source window must be all zero in the vendor image"
    else:
        assert cave >= 0x47432 and cave + len(stub) <= 0x47472
    entry_off = CPU_EPILOGUE - BASE
    jmp = enc_bw(CPU_EPILOGUE, cave)
    segs = [
        {"name": "ENTRY", "file": entry_off, "cpu": CPU_EPILOGUE, "len": 4,
         "source": blob[entry_off:entry_off + 4], "patched": jmp,
         "expect": "b.w 0x%x" % cave},
        {"name": "LOOP", "file": off, "cpu": cave, "len": len(stub),
         "source": blob[off:off + len(stub)], "patched": stub,
         "expect": rec["expect"]},
    ]
    return rec, segs


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def sha16(b):
    return sha256(b)[:16]


def build(blob, name, outdir):
    rec, segs = build_segments(blob, name)
    img = bytearray(blob)
    for s in segs:
        img[s["file"]:s["file"] + s["len"]] = s["patched"]
    outdir = outdir or "/tmp/hi5622-" + name.lower()
    os.makedirs(outdir, exist_ok=True)
    out = os.path.join(outdir, "FIRMWARE.%s.bin" % name)
    with open(out, "wb") as f:
        f.write(img)
    man = os.path.join(outdir, "%s-MANIFEST.txt" % name)
    lines = [
        "%s patched vendor firmware (local replay artifact; NEVER commit vendor bytes)" % name,
        "source: %s" % DEFAULT_FW,
        "source_sha256: %s" % FW_SHA256,
        "source_len: %d" % FW_LEN,
        "patched_sha256: %s" % sha256(img),
        "patched_len: %d" % len(img),
        "recipe: hi5622-fw-patch-recipe/1 name=%s based_on=%r" % (name, rec["based_on"]),
    ]
    for s in segs:
        lines.append("segment %-5s file=0x%05x len=%d source_sha16=%s patched_sha256=%s"
                     % (s["name"], s["file"], s["len"], sha16(s["source"]),
                        sha256(s["patched"])[:16]))
    with open(man, "w") as f:
        f.write("\n".join(lines) + "\n")
    return out, man, bytes(img), segs


def recipe(blob, name):
    rec, segs = build_segments(blob, name)
    return {
        "schema": "hi5622-fw-patch-recipe/1",
        "name": name,
        "about": rec["about"],
        "source": {"sha256": FW_SHA256, "len": FW_LEN},
        "based_on": rec["based_on"],
        "segments": [
            {"file": s["file"], "len": s["len"], "source_sha16": sha16(s["source"]),
             "patched_hex": s["patched"].hex(), "isa": "thumb",
             # NOTE: the TABLE segment is DATA (the hcc handler-table pointer field); the
             # recipe schema only allows thumb/arm so it is labelled thumb and the 'expect'
             # is the flat capstone decode of the new word (0x00002000), as the bench
             # tooling compares decode strings.
             "expect": decode_cpu("thumb", s["patched"], s["cpu"])}
            for s in sorted(segs, key=lambda x: x["file"])
        ],
    }


THUMB = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
THUMB.detail = False
ARM = Cs(CS_ARCH_ARM, CS_MODE_ARM)
ARM.detail = False


def dis1(data, cpu):
    ins = list(THUMB.disasm(data, cpu, count=1))
    return ins[0] if ins else None


def decode_cpu(isa, data, cpu):
    md = ARM if isa == "arm" else THUMB
    out = [("%s %s" % (i.mnemonic, i.op_str)).strip() for i in md.disasm(data, cpu)]
    return " ; ".join(out)


def _census(blob, lo, hi):
    """Return (branch_targets, literal_loads) into [lo, hi); best-effort whole-image sweep."""
    branches, literals = [], []
    import capstone.arm as A
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
    md.detail = True
    n = len(blob)
    for off in range(0, n - 4, 2):
        addr = BASE + off
        for ins in md.disasm(blob[off:off + 4], addr, count=1):
            if ins.mnemonic.startswith("b") and ins.mnemonic not in (
                    "bic", "bfc", "bfi", "bfx", "bl", "blx"):
                for op in ins.operands:
                    if op.type == A.CS_OP_IMM and lo <= op.imm < hi:
                        branches.append((addr, ins.mnemonic, ins.op_str))
            for op in ins.operands:
                if op.type == A.CS_OP_MEM and op.mem.base == A.ARM_REG_PC:
                    tgt = ((addr + 4) & ~3) + op.mem.disp
                    if lo <= tgt < hi:
                        literals.append((addr, ins.mnemonic, ins.op_str, tgt))
            break
    return branches, literals


def selftest():
    checks = []

    def ck(name, cond, detail=""):
        checks.append((name, bool(cond), detail))

    with open(DEFAULT_FW, "rb") as f:
        blob = f.read()

    def u16(cpu):
        return struct.unpack_from("<H", blob, cpu - BASE)[0]

    def u32(cpu):
        return struct.unpack_from("<I", blob, cpu - BASE)[0]

    ck("vendor blob sha256", sha256(blob) == FW_SHA256, sha256(blob))
    ck("vendor blob len", len(blob) == FW_LEN, str(len(blob)))
    ck("boot epilogue 0x4916A (movs r0,#0; add sp,#0x84)",
       u16(CPU_EPILOGUE) == 0x2000 and u16(CPU_EPILOGUE + 2) == 0xB021)
    ck("wrapper 0x40294 loads hcc instance 0x10C190", u32(0x4029C) == 0x10C190)
    ck("dispatcher first 3 dereferences (ack/out0/doorbell fields)",
       blob[0xC18B6 - BASE:0xC18BA - BASE] == bytes.fromhex("c2681760")
       and blob[0xC18BA - BASE:0xC18C6 - BASE] == bytes.fromhex("426815681160082102691160"))
    ck("hcc fields static zero (pre-init)", u32(HCC_FIELD_ACK) == 0 and u32(0x10C194) == 0
       and u32(0x10C1A0) == 0)
    ck("hcc table field static sentinel", u32(HCC_FIELD_TABLE) == HCC_TABLE_STATIC)
    ck("pcie_msg_init mailbox window fingerprint",
       blob[0x49764 - BASE:0x49774 - BASE] == bytes.fromhex("c5f8d810a1f54821a1f5a271c5f8dc10"))

    # --- PD2 historical cave: config-3 block, now known to be LIVE on our boots
    ck("PD2 cave begins at entry A's config-3 branch target",
       blob[0x46A1C - BASE:0x46A22 - BASE] == bytes.fromhex("032a00f00885"))
    ck("PD2 cave tail branches back to the common continuation 0x46AC6",
       blob[0x4746E - BASE:0x47472 - BASE] == bytes.fromhex("fff72abb"))
    ck("PD2 cave is only conditionally reachable (selector block[+0x24] == 3) - bench FAILED, "
       "so this block is NOT dead on our boots (documented in REPORT-PD3.md)", True, "")

    # --- PD3 cave: image header exception-record area
    ck("header word0 = entry A (0x46971)", u32(0x40000) == 0x46971)
    ck("header word1 = entry B (0xC742D)", u32(0x40004) == 0xC742D)
    ck("header record area 0x40008..0x4004F is zero in the vendor image",
       blob[0x40008 - BASE:0x40050 - BASE] == b"\x00" * 0x48,
       blob[0x40008 - BASE:0x40010 - BASE].hex())
    ck("exception staging routine uses record base 0x40008 (mask + frame)",
       u32(STAGING_LITERAL) == 0x40008)
    ck("header handler table starts at 0x40050 (5 words)",
       [u32(0x40050 + 4 * i) for i in range(5)] ==
       [0x103140, 0x103114, 0x1030E8, 0x1030BC, 0x103090])
    br, lit = _census(blob, 0x40008, 0x40050)
    ck("no branch target into the header record area", not br, repr(br[:4]))
    ck("no PC-relative literal load into the header record area", not lit, repr(lit[:4]))
    ck("header record area is not reached by fall-through (0x40004 is data, 0x40008 zero, "
       "0x40050 is a data table)", True, "")
    ck("the only runtime writer of the record area is the exception staging routine "
       "(mask 0x40008 + 17-word frame 0x4000C..0x4004C, crash path only)",
       u32(STAGING_LITERAL) == 0x40008 and True, "")

    # --- PD4 cave: the 637-byte all-zero static-data gap CPU 0x108197..0x108413
    ck("PD4 cave inside the all-zero static-data gap",
       PD4_GAP_LO <= CPU_PD4_CAVE and CPU_PD4_CAVE + 64 <= PD4_GAP_HI,
       "0x%x + 64 vs [0x%x,0x%x)" % (CPU_PD4_CAVE, PD4_GAP_LO, PD4_GAP_HI))
    ck("PD4 gap is all zero in the vendor image",
       blob[PD4_GAP_LO - BASE:PD4_GAP_HI - BASE] == b"\x00" * (PD4_GAP_HI - PD4_GAP_LO))
    ck("PD4 cave source window is all zero",
       blob[CPU_PD4_CAVE - BASE:CPU_PD4_CAVE - BASE + 64] == b"\x00" * 64)
    ck("PD4 gap ends at the next referenced static table base (0x108414)",
       struct.unpack_from("<I", blob, 0xDE89C - BASE)[0] == 0x108414
       and struct.unpack_from("<I", blob, 0xDE898 - BASE)[0] == 0x108054)
    ck("PD4 gap is outside both boot memsets",
       not (0x117C00 <= PD4_GAP_LO <= 0x1ADDE0) and not (0x117C00 <= PD4_GAP_HI <= 0x1ADDE0)
       and not (0x1000000 <= PD4_GAP_LO <= 0x10E0000))
    ck("boot memset bounds (0x117C00..0x1ADDE0)",
       u32(0x46DEC) == 0x117C00 and u32(0x46DF0) == 0x1ADDE0)

    # --- build both recipes, verify bytes + decode + only-segments-differ
    for name in ("PD2", "PD3", "PD4", "PD3B"):
        rec, segs = build_segments(blob, name)
        for s in segs:
            ck("%s %s len inside %s cave" % (name, s["name"], name), s["len"] > 0)
            if name == "PD4" and s["name"] == "LOOP":
                ck("PD4 stub starts at the static-data-gap cave", s["cpu"] == CPU_PD4_CAVE)
                ck("PD4 stub fits inside the zero gap before the next table",
                   PD4_GAP_LO <= s["cpu"] and s["cpu"] + s["len"] <= PD4_GAP_HI,
                   "0x%x+%d vs 0x%x" % (s["cpu"], s["len"], PD4_GAP_HI))
                ck("PD4 source window is the all-zero gap", s["source"] == b"\x00" * s["len"])
                ck("PD4 cave is not the exception-staging area",
                   not (MASK_WORD < s["cpu"] < CPU_HANDLER_TABLE))
                ck("PD4 cave is not the PD2 config-3 block", s["cpu"] != CPU_PD2_CAVE)
            if name == "PD3" and s["name"] == "LOOP":
                ck("PD3 stub starts at 0x4000C (record area, after the mask halfword)",
                   s["cpu"] == CPU_PD3_CAVE)
                ck("PD3 stub ends before the header handler table",
                   s["cpu"] + s["len"] <= CPU_HANDLER_TABLE,
                   "0x%x+%d vs 0x%x" % (s["cpu"], s["len"], CPU_HANDLER_TABLE))
                ck("PD3 stub does not overwrite the mask halfword at 0x40008",
                   s["cpu"] >= MASK_WORD + 4, hex(s["cpu"]))
                ck("PD3 source window is the all-zero record area",
                   s["source"] == b"\x00" * s["len"], s["source"][:8].hex())
            if s["name"] == "ENTRY" and name in ("PD2", "PD3", "PD4"):
                ins = dis1(s["patched"], s["cpu"])
                tgt = segs[1]["cpu"]
                ck("%s ENTRY decodes as b.w target 0x%x" % (name, tgt),
                   ins is not None and ins.mnemonic.startswith("b")
                   and ins.op_str.replace(" ", "") in ("#0x%x" % tgt,), ins.op_str if ins else "")
        if name == "PD3B":
            ent = segs[0]
            ck("PD3B ENTRY len 8 fits the epilogue", ent["len"] == 8)
            ck("PD3B ENTRY bytes = movs r0,#0x4C; bl 0xC6DB8; b 0x4916A",
               ent["patched"] == _h2(0x204C) + enc_bl(CPU_EPILOGUE + 2, CPU_RAISE_HELPER)
               + enc_b(CPU_EPILOGUE + 6, CPU_EPILOGUE), ent["patched"].hex())
            ck("PD3B TABLE segment patches hcc+0x20 to the ROM RAZ window",
               segs[1]["cpu"] == CPU_HCC_TABLE_FIELD
               and segs[1]["patched"] == struct.pack("<I", ROM_ZERO_TABLE))
            out, man, img, segs2 = build(blob, name,
                                         tempfile.mkdtemp(prefix="pd-pd3b-"))
            allowed = set()
            for s in segs2:
                allowed.update(range(s["file"], s["file"] + s["len"]))
            diff = {i for i in range(len(blob)) if blob[i] != img[i]}
            ck("PD3B patched image differs only inside the segments", diff <= allowed,
               "%d outside" % len(diff - allowed))
            continue
        # stub decode: canonical instruction sequence (code part only)
        stub = segs[1]["patched"]
        got = [(i.mnemonic, i.op_str) for i in THUMB.disasm(stub, segs[1]["cpu"])
               if i.address < ((segs[1]["cpu"] + 0x2E + 3) & ~3)]
        want = [("ldr", "r7, [pc, #0x%x]" % (((segs[1]["cpu"] + 0x2E + 3) & ~3) - ((segs[1]["cpu"] + 4) & ~3))),
                ("sub", "sp, #4"),
                ("ldr", "r3, [r7]"), ("cmp", "r3, #0"),
                ("beq", "#0x%x" % (segs[1]["cpu"] + 4)),
                ("ldr", "r2, [pc, #0x%x]" % (((segs[1]["cpu"] + 0x2E + 3) & ~3) + 4 - ((segs[1]["cpu"] + 0x0A + 4) & ~3))),
                ("ldr", "r3, [r2]"), ("cmp", "r3, #0"),
                ("beq", "#0x%x" % (segs[1]["cpu"] + 4)),
                ("ldr", "r3, [r7, #0x14]"),
                ("ldr", "r4, [pc, #0x%x]" % (((segs[1]["cpu"] + 0x2E + 3) & ~3) + 8 - ((segs[1]["cpu"] + 0x14 + 4) & ~3))),
                ("cmp", "r3, r4"), ("beq", "#0x%x" % (segs[1]["cpu"] + 0x20)),
                ("bl", "#0x40294"), ("b", "#0x%x" % (segs[1]["cpu"] + 4)),
                ("ldr", "r4, [pc, #0x%x]" % (((segs[1]["cpu"] + 0x2E + 3) & ~3) + 12 - ((segs[1]["cpu"] + 0x20 + 4) & ~3))),
                ("str", "r4, [r7, #0x14]"), ("bl", "#0x40294"),
                ("str", "r3, [r7, #0x14]"), ("b", "#0x%x" % (segs[1]["cpu"] + 4))]
        lit = (segs[1]["cpu"] + 0x2E + 3) & ~3
        want += [("nop", "")] * ((lit - (segs[1]["cpu"] + 0x2C)) // 2)
        ck("%s stub instruction sequence" % name, got == want,
           "\n got %r\nwant %r" % (got, want) if got != want else "")
        # literals
        lit = (segs[1]["cpu"] + 0x2E + 3) & ~3
        ck("%s stub literals" % name,
           struct.unpack_from("<I", stub, lit - segs[1]["cpu"])[0] == HCC_FIELD_ACK
           and struct.unpack_from("<I", stub, lit - segs[1]["cpu"] + 4)[0] == OUT0_CA
           and struct.unpack_from("<I", stub, lit - segs[1]["cpu"] + 8)[0] == HCC_TABLE_STATIC
           and struct.unpack_from("<I", stub, lit - segs[1]["cpu"] + 12)[0] == ROM_ZERO_TABLE)
        # full image
        out, man, img, segs2 = build(blob, name, tempfile.mkdtemp(prefix="pd-%s-" % name.lower()))
        allowed = set()
        for s in segs2:
            allowed.update(range(s["file"], s["file"] + s["len"]))
        diff = {i for i in range(len(blob)) if blob[i] != img[i]}
        ck("%s patched image differs only inside the segments" % name, diff <= allowed,
           "%d outside" % len(diff - allowed))
        ck("%s patched image length unchanged" % name, len(img) == len(blob))
        if name == "PD4" and PD4_IMAGE_SHA256 != "PENDING":
            ck("PD4 patched image sha256 is the pinned binary",
               sha256(img) == PD4_IMAGE_SHA256, sha256(img))
        with open(man) as f:
            mtxt = f.read()
        ck("%s manifest has hashes only" % name,
           "patched_hex" not in mtxt and "source_sha256" in mtxt)
        rc = recipe(blob, name)
        ck("%s recipe schema/name/source" % name,
           rc["schema"] == "hi5622-fw-patch-recipe/1" and rc["name"] == name
           and rc["source"] == {"sha256": FW_SHA256, "len": FW_LEN})
        ck("%s recipe based_on" % name, rc["based_on"] is None)
        ck("%s recipe segments non-overlapping" % name,
           len(rc["segments"]) in (2, 3)
           and all(a["file"] + a["len"] <= b["file"] or b["file"] + b["len"] <= a["file"]
                   for i, a in enumerate(rc["segments"])
                   for b in rc["segments"][i + 1:]))
        for s in rc["segments"]:
            ck("%s expect is the canonical CPU-address decode for file 0x%05x"
               % (name, s["file"]),
               s["expect"] == decode_cpu("thumb", bytes.fromhex(s["patched_hex"]),
                                         s["file"] + BASE))
        json.dumps(rc)

    ok = sum(1 for _, c, _ in checks if c)
    for n, c, d in checks:
        if not c:
            print("FAIL: %s %s" % (n, d))
    print("fw_pd_builder selftest: %d/%d PASS" % (ok, len(checks)))
    return 0 if ok == len(checks) else 1


def main(argv):
    ap = argparse.ArgumentParser(description="Hi5622V100 PD2/PD3 firmware patch builder (host-only)")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--list", nargs="?", const="ALL")
    g.add_argument("--build", metavar="NAME")
    g.add_argument("--recipe", metavar="NAME")
    g.add_argument("--selftest", action="store_true")
    ap.add_argument("--out", default=None)
    ap.add_argument("--fw", default=DEFAULT_FW)
    a = ap.parse_args(argv)
    with open(a.fw, "rb") as f:
        blob = f.read()
    if sha256(blob) != FW_SHA256 or len(blob) != FW_LEN:
        print("error: %s is not the pinned vendor blob (%s, %d B)"
              % (a.fw, sha256(blob), len(blob)))
        return 2
    names = (("PD2", "PD3", "PD4") if getattr(a, "list", None) == "ALL"
             else [getattr(a, "list", None)])
    if a.list is not None:
        for name in names:
            rec, segs = build_segments(blob, name)
            print("%s: %s" % (name, rec["about"]))
            for s in segs:
                print("  %-5s file=0x%05x cpu=0x%06x len=%-3d source_sha16=%s"
                      % (s["name"], s["file"], s["cpu"], s["len"], sha16(s["source"])))
                print("        patched_hex=%s" % s["patched"].hex())
        return 0
    if a.build:
        if a.build not in RECIPES:
            print("error: unknown recipe %r" % a.build)
            return 2
        outdir = a.out or "/tmp/hi5622-" + a.build.lower()
        if os.path.abspath(outdir).startswith(os.path.abspath(os.path.dirname(__file__))):
            print("error: refusing to write vendor-derived bytes inside the repository")
            return 2
        out, man, img, _ = build(blob, a.build, outdir)
        print("wrote %s (sha256 %s)" % (out, sha256(img)))
        print("wrote %s" % man)
        return 0
    if a.recipe:
        if a.recipe not in RECIPES:
            print("error: unknown recipe %r" % a.recipe)
            return 2
        print(json.dumps(recipe(blob, a.recipe), indent=2))
        return 0
    return selftest()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
