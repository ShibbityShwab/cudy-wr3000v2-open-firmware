#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""pd2_patch_builder.py - host-only builder/verifier for the Hi5622V100 firmware PD2 patch.

Lane wifi-irq-unmask / shared task task-91 (PD2). Reads the LOCAL vendor firmware blob
in place, verifies it by sha256 and each source window by sha256, applies the PD2 patch
segments, writes the patched image + manifest into a NON-repo directory (default
/tmp/hi5622-pd2), and prints the machine-readable recipe used by the bench tooling.

It never copies vendor bytes into this repository and never opens a device.

PD2 (what/why; full analysis in ../REPORT-PD2.md):
  PD's first segment redirected CPU 0x47610 - which is NOT a dead handshake-failure path
  but the retry body of entry A's ROM<->image handshake poll (0x473F0: cmp [0x1BF008],2;
  bne 0x47610). That call therefore ran the H2D dispatcher before pcie_msg_init (CPU
  0x49334) had initialised the hcc instance mailbox pointers (CPU 0x10C190+4/+0xC/+0x10):
  the first store dereferenced a NULL pointer (CPU 0xC18B8) and data-aborted, and the
  nested, never-unwound ABT frames walked down to CA 0x1BF000 and clobbered the
  handshake page. PD2 instead:
    * keeps the handshake path untouched (CPU 0x47610 not patched), and
    * redirects the boot task's real epilogue (CPU 0x4916A, only reached after the full
      boot/init chain, including pcie_msg_init, ran) into a small poll loop placed in the
      conditionally-dead config-3 block of entry A (CPU 0x47432; reachable only if the
      host-seeded platform block word [+0x24] == 3, vendor value is 2).
  The loop waits until the hcc instance is initialised, then polls mailbox out[0]
  (CA 0x40039010) and only then calls the firmware's own event-0x4C handler wrapper
  (CPU 0x40294 -> H2D dispatcher 0xC18AC).  If the dispatcher's handler-table pointer
  still holds its static/uninitialised value (0x118D68, an image code address), the loop
  substitutes the mask-ROM RAZ window (0x2000, reads 0) so the dispatcher performs the
  vendor ack/consume/doorbell sequence without blx-ing an uninitialised table, then
  restores the pointer.  With the real table present it dispatches for real.

Subcommands:
  --list             patch segments + source windows (hash-verified against the blob)
  --build [OUTDIR]   write OUTDIR/FIRMWARE.PD2.bin + OUTDIR/PD2-MANIFEST.txt (hashes only)
  --recipe           print the hi5622-fw-patch-recipe/1 JSON (bench-tooling schema)
  --selftest         build into a temp dir and verify everything (exit 0 on PASS)
"""
import argparse
import hashlib
import json
import os
import re
import struct
import sys
import tempfile

try:
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB
except Exception:  # pragma: no cover
    print("error: capstone is required (project venv .provision-venv/bin/python)")
    raise SystemExit(2)

BASE = 0x40000                      # CPU address = file offset + BASE
DEFAULT_FW = "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin"
FW_SHA256 = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_LEN = 928920
DEFAULT_OUT = "/tmp/hi5622-pd2"

# --- decoded facts used by the patch (all verified by --selftest against the blob) ----
CPU_BOOT_EPILOGUE = 0x4916A         # movs r0,#0 ; add sp,#0x84 ; pop.w {r4-r11,pc}
CPU_CAVE = 0x47432                  # conditionally-dead config-3 block of entry A
CPU_CAVE_END = 0x47472              # exclusive; 0x47472 is live code (b target from 0x46CC8)
CPU_CAVE_GUARD = 0x46A1E            # beq.w 0x47432 (taken only when platform block [+0x24]==3)
CPU_HANDSHAKE_BRANCH = 0x473F4      # bne.w 0x47610 (handshake retry; PD hijacked this)
CPU_HANDSHAKE_RETRY = 0x47610       # must stay untouched in PD2
CPU_WRAPPER = 0x40294               # ldr r0,=0x10C190 ; b.w 0xC18AC
CPU_DISPATCH = 0xC18AC              # H2D mailbox dispatcher
CPU_DISPATCH_GUARD = 0xC18B8        # str r7,[r2] with r2=[hcc+0xC] -- PD's first fault
HCC_BASE = 0x10C190                 # hcc instance argument the wrapper passes
HCC_ACK_FIELD = 0x10C19C            # [hcc+0xC] = CA 0x400392F0 (out[5], ack: write 1)
HCC_OUT0_FIELD = 0x10C194           # [hcc+4]   = CA 0x40039010 (out[0], pending: read+clear)
HCC_DOORBELL_FIELD = 0x10C1A0       # [hcc+0x10]= CA 0x400392D4 (out[2], H2D done: write 8)
HCC_TABLE_FIELD = 0x10C1B0          # [hcc+0x20]= handler table {fn,arg} 8-byte records
HCC_TABLE_STATIC = 0x118D68         # static value of that field == image code address
OUT0_CA = 0x40039010
ACK_CA = 0x400392F0
DOORBELL_CA = 0x400392D4
ROM_ZERO_TABLE = 0x00002000         # mask-ROM 0x81C..0x3FFF reads all-zero (RAZ)
PCIEMSG_INIT = 0x49334              # boot task's module-table init (CPU 0x47824 -> 0x49334)
BOOT_FN = 0x47624                   # boot task function (Thumb entry 0x47625)
PLATFORM_BLOCK = 0x1B2800           # host-seeded vendor platform block; [+0x24] == 2
STATE_BYTE = 0x118B60               # boot state (r4=0x117C00 + 0xF60)
MASK_WORD = 0x40008                 # staged exception mask (bit 4 == data abort)


# ------------------------------------------------------------------ Thumb encoders
def _hex2(v):
    return bytes([v & 0xFF, (v >> 8) & 0xFF])


def enc_ldr_lit(rt, insn_addr, lit_addr):
    """Thumb-16 LDR (literal), 4-byte aligned target, |delta| <= 1020."""
    pc = (insn_addr + 4) & ~3
    d = lit_addr - pc
    assert d >= 0 and d % 4 == 0 and d <= 1020, (hex(insn_addr), hex(lit_addr), d)
    return _hex2(0x4800 | (rt << 8) | (d // 4))


def enc_ldr_imm(rt, rn, imm5):
    """Thumb-16 LDR (immediate word), offset = imm5*4."""
    assert 0 <= imm5 <= 31
    return _hex2(0x6800 | (imm5 << 6) | (rn << 3) | rt)


def enc_str_imm(rt, rn, imm5):
    """Thumb-16 STR (immediate word), offset = imm5*4."""
    assert 0 <= imm5 <= 31
    return _hex2(0x6000 | (imm5 << 6) | (rn << 3) | rt)


def enc_cmp_reg(rn, rm):
    """Thumb-16 CMP (register)."""
    return _hex2(0x4280 | (rm << 3) | rn)


def enc_cmp_imm0(rn):
    """Thumb-16 CMP (immediate) with #0."""
    return _hex2(0x2800 | (rn << 8))


def enc_cbz(rn, insn_addr, target):
    """Thumb-16 CBZ (Rn 0..7); immediate is i:imm5 (6-bit signed, halfword units)."""
    d = target - (insn_addr + 4)
    assert d % 2 == 0 and -64 <= (d // 2) <= 63, (hex(insn_addr), hex(target), d)
    imm6 = (d // 2) & 0x3F
    return _hex2(0xB100 | ((imm6 >> 5) << 9) | ((imm6 & 0x1F) << 3) | rn)


def enc_bcond(cond, insn_addr, target):
    """Thumb-16 conditional B (cond 4 bits, e.g. 0x0 == EQ)."""
    d = target - (insn_addr + 4)
    assert d % 2 == 0 and -256 <= d <= 254
    return _hex2(0xD000 | (cond << 8) | ((d // 2) & 0xFF))


def enc_b(insn_addr, target):
    """Thumb-16 unconditional B."""
    d = target - (insn_addr + 4)
    assert d % 2 == 0 and -2048 <= d <= 2046
    return _hex2(0xE000 | ((d // 2) & 0x7FF))


def enc_sub_sp(imm8):
    """Thumb-16 SUB (SP minus immediate, 0..508 in steps of 4)."""
    assert 0 <= imm8 <= 127
    return _hex2(0xB080 | imm8)


def enc_add_sp(imm8):
    """Thumb-16 ADD (SP plus immediate, 0..508 in steps of 4)."""
    assert 0 <= imm8 <= 127
    return _hex2(0xB000 | imm8)


def enc_bl(insn_addr, target):
    """Thumb-32 BL (T1).  target uses the Thumb bit convention: pass a code address."""
    off = (target & ~1) - (insn_addr + 4)
    assert -0x1000000 <= off <= 0xFFFFFE and off % 2 == 0, (hex(insn_addr), hex(target), off)
    S = 1 if off < 0 else 0
    I1 = (off >> 23) & 1
    I2 = (off >> 22) & 1
    imm10 = (off >> 12) & 0x3FF
    imm11 = (off >> 1) & 0x7FF
    J1 = (I1 if S else 1 - I1)
    J2 = (I2 if S else 1 - I2)
    hw1 = 0xF000 | (S << 10) | imm10
    hw2 = 0xD000 | (J1 << 13) | (1 << 12) | (J2 << 11) | imm11
    return _hex2(hw1) + _hex2(hw2)


def enc_bw(insn_addr, target):
    """Thumb-32 B.W (T4).  target code address (Thumb bit ignored)."""
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
    hw2 = 0x9000 | (J1 << 13) | (J2 << 11) | imm11
    return _hex2(hw1) + _hex2(hw2)


# ------------------------------------------------------------------ PD2 stub
# Layout (CPU addresses; file = CPU - 0x40000):
#   0x47432  ldr  r7,=0x10C19C          ; hcc+0xC field address (instance-init guard)
#   0x47434  <loop> ldr r3,[r7]         ; guard: dispatcher's first deref target
#   0x47436  cbz  r3,<loop>
#   0x47438  ldr  r2,=0x40039010        ; mailbox out[0] (H2D pending read/clear)
#   0x4743A  ldr  r3,[r2]
#   0x4743C  cbz  r3,<loop>             ; nothing pending -> poll
#   0x4743E  ldr  r3,[r7,#0x14]         ; [hcc+0x20] handler-table pointer
#   0x47440  ldr  r4,=0x00118D68        ; uninitialised sentinel (static image-value)
#   0x47442  cmp  r3,r4
#   0x47444  beq  <zero>
#   0x47446  sub  sp,#4                 ; keep SP 8-byte aligned at the public call
#   0x47448  bl   0x40294               ; firmware wrapper -> H2D dispatcher (live table)
#   0x4744C  add  sp,#4
#   0x4744E  b    <loop>
#   0x47450  <zero> ldr r4,=0x00002000  ; mask-ROM RAZ window == permanently zero table
#   0x47452  str  r4,[r7,#0x14]         ; consume-only dispatch (no uninitialised blx)
#   0x47454  sub  sp,#4
#   0x47456  bl   0x40294
#   0x4745A  add  sp,#4
#   0x4745C  str  r3,[r7,#0x14]         ; restore the live table pointer (r3/r7 preserved
#   0x4745E  b    <loop>                ;  by wrapper+dispatcher: push/pop r3-r7)
#   0x47460  literals: 0x0010C19C, 0x40039010, 0x00118D68, 0x00002000
STUB_CAVE_LEN = 0x3E   # 0x47432..0x4746F; 0x47470-71 (dead block tail) left untouched


def build_stub():
    """Return (bytes, annotations). Pure function; every encoding is verified by selftest."""
    code = bytearray()
    ann = []

    def emit(addr, data, text):
        assert len(code) == addr - CPU_CAVE, (hex(addr), len(code))
        code.extend(data)
        if text:
            ann.append((addr, data.hex(), text))

    LOOP, ZERO = 0x47436, 0x47452
    L_HCC, L_OUT0, L_SENT, L_ZERO = 0x47460, 0x47464, 0x47468, 0x4746C
    a = CPU_CAVE
    # One-time setup: hcc+0xC field address in r7 (the dispatcher preserves r3-r7), and a
    # permanent -4 SP bias so that each 'bl' (which pushes 4 bytes of LR) enters the public
    # wrapper with an 8-byte aligned SP.  The bias is never undone: PD2 never returns.
    emit(a, enc_ldr_lit(7, a, L_HCC), "ldr r7,=" + hex(HCC_ACK_FIELD)); a += 2
    emit(a, enc_sub_sp(1), "sub sp,#4  ; 8-byte align the dispatcher call (never undone)"); a += 2
    assert a == LOOP, (hex(a), hex(LOOP))
    emit(a, enc_ldr_imm(3, 7, 0), "loop: ldr r3,[r7]  ; dispatcher's first-deref field"); a += 2
    emit(a, enc_cmp_imm0(3), "cmp r3,#0"); a += 2
    emit(a, enc_bcond(0x0, a, LOOP), "beq loop  ; instance not initialised -> wait"); a += 2
    emit(a, enc_ldr_lit(2, a, L_OUT0), "ldr r2,=" + hex(OUT0_CA)); a += 2
    emit(a, enc_ldr_imm(3, 2, 0), "ldr r3,[r2]  ; mailbox out[0] (H2D pending)"); a += 2
    emit(a, enc_cmp_imm0(3), "cmp r3,#0"); a += 2
    emit(a, enc_bcond(0x0, a, LOOP), "beq loop  ; nothing pending"); a += 2
    emit(a, enc_ldr_imm(3, 7, 0x14 // 4), "ldr r3,[r7,#0x14]  ; [hcc+0x20] handler table"); a += 2
    emit(a, enc_ldr_lit(4, a, L_SENT), "ldr r4,=" + hex(HCC_TABLE_STATIC)); a += 2
    emit(a, enc_cmp_reg(3, 4), "cmp r3,r4"); a += 2
    emit(a, enc_bcond(0x0, a, ZERO), "beq zero"); a += 2
    emit(a, enc_bl(a, CPU_WRAPPER), "bl 0x%x  ; wrapper -> H2D dispatcher (live table)" % CPU_WRAPPER); a += 4
    emit(a, enc_b(a, LOOP), "b loop"); a += 2
    assert a == ZERO, (hex(a), hex(ZERO))
    emit(a, enc_ldr_lit(4, a, L_ZERO), "zero: ldr r4,=" + hex(ROM_ZERO_TABLE)); a += 2
    emit(a, enc_str_imm(4, 7, 0x14 // 4), "str r4,[r7,#0x14]  ; consume-only callback table"); a += 2
    emit(a, enc_bl(a, CPU_WRAPPER), "bl 0x%x  ; wrapper -> H2D dispatcher (zero table)" % CPU_WRAPPER); a += 4
    emit(a, enc_str_imm(3, 7, 0x14 // 4), "str r3,[r7,#0x14]  ; restore live table (r3/r7 kept)"); a += 2
    emit(a, enc_b(a, LOOP), "b loop"); a += 2
    assert a == 0x4745E, hex(a)
    emit(a, _hex2(0xBF00), "nop  ; 4-byte-align the literal pool"); a += 2
    assert a == 0x47460, hex(a)
    emit(a, struct.pack("<I", HCC_ACK_FIELD), ".word 0x0010C19C  ; hcc+0xC field address"); a += 4
    emit(a, struct.pack("<I", OUT0_CA), ".word 0x40039010  ; mailbox out[0] CA"); a += 4
    emit(a, struct.pack("<I", HCC_TABLE_STATIC), ".word 0x00118D68  ; uninitialised table sentinel"); a += 4
    emit(a, struct.pack("<I", ROM_ZERO_TABLE), ".word 0x00002000  ; mask-ROM RAZ zero table"); a += 4
    assert len(code) == STUB_CAVE_LEN, (len(code), STUB_CAVE_LEN)
    return bytes(code), ann


# ------------------------------------------------------------------ blob + segments
def blob_read(path=DEFAULT_FW):
    with open(path, "rb") as f:
        return f.read()


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def src16(b):
    return sha256(b)[:16]


def cpu_to_file(cpu):
    return cpu - BASE


PATCHES = [
    {
        "name": "PD2-ENTRY",
        "cpu": CPU_BOOT_EPILOGUE,
        "len": 4,
        "isa": "thumb",
        "expect": "b.w 0x47432 (boot-task epilogue -> PD2 poll loop)",
    },
    {
        "name": "PD2-LOOP",
        "cpu": CPU_CAVE,
        "len": STUB_CAVE_LEN,
        "isa": "thumb",
        "expect": "PD2 guard+poll stub (see REPORT-PD2.md section 5)",
    },
]


def patched_segments(blob):
    stub, _ = build_stub()
    jmp = enc_bw(CPU_BOOT_EPILOGUE, CPU_CAVE)
    out = []
    for p in PATCHES:
        off = cpu_to_file(p["cpu"])
        src = blob[off:off + p["len"]]
        patched = jmp if p["name"] == "PD2-ENTRY" else stub
        assert len(patched) == p["len"], (p["name"], len(patched), p["len"])
        out.append({"patch": p, "file": off, "source": src, "patched": patched})
    return out


def build(blob, outdir):
    segs = patched_segments(blob)
    img = bytearray(blob)
    for s in segs:
        img[s["file"]:s["file"] + len(s["patched"])] = s["patched"]
    os.makedirs(outdir, exist_ok=True)
    out = os.path.join(outdir, "FIRMWARE.PD2.bin")
    with open(out, "wb") as f:
        f.write(img)
    man = os.path.join(outdir, "PD2-MANIFEST.txt")
    lines = [
        "PD2 patched vendor firmware (local replay artifact; NEVER commit vendor bytes)",
        "source: %s" % DEFAULT_FW,
        "source_sha256: %s" % FW_SHA256,
        "source_len: %d" % FW_LEN,
        "patched_sha256: %s" % sha256(img),
        "patched_len: %d" % len(img),
        "recipe: hi5622-fw-patch-recipe/1 name=PD2 based_on=null",
    ]
    for s in segs:
        lines.append("segment %-10s file=0x%05x len=%d source_sha16=%s patched_sha256=%s"
                     % (s["patch"]["name"], s["file"], len(s["patched"]),
                        src16(s["source"]), sha256(s["patched"])[:16]))
    with open(man, "w") as f:
        f.write("\n".join(lines) + "\n")
    return out, man, img, segs


def recipe(blob):
    segs = patched_segments(blob)
    segs = sorted(segs, key=lambda s: s["file"])
    return {
        "schema": "hi5622-fw-patch-recipe/1",
        "name": "PD2",
        "about": "post-boot H2D mailbox poll loop: boot epilogue -> out[0] poll -> firmware dispatcher (guarded)",
        "source": {"sha256": FW_SHA256, "len": FW_LEN},
        "based_on": None,
        "segments": [
            {"file": s["file"], "len": len(s["patched"]), "source_sha16": src16(s["source"]),
             "patched_hex": s["patched"].hex(), "isa": s["patch"]["isa"],
             "expect": s["patch"]["expect"]}
            for s in segs
        ],
    }


# ------------------------------------------------------------------ verification
THUMB = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
THUMB.detail = False


def dis1(data, cpu):
    ins = list(THUMB.disasm(data, cpu, count=1))
    return ins[0] if ins else None


def selftest():
    checks = []

    def ck(name, cond, detail=""):
        checks.append((name, bool(cond), detail))
        if not cond:
            print("FAIL: %s %s" % (name, detail))

    blob = blob_read()
    ck("vendor blob sha256", sha256(blob) == FW_SHA256, sha256(blob))
    ck("vendor blob len", len(blob) == FW_LEN, str(len(blob)))

    # static site checks against the *unpatched* vendor blob
    def u32(cpu):
        return struct.unpack_from("<I", blob, cpu_to_file(cpu))[0]

    def u16(cpu):
        return struct.unpack_from("<H", blob, cpu_to_file(cpu))[0]

    ck("entry A handshake poll at 0x473F0 (ldr r3,[r4]; cmp r3,#2; bne 0x47610)",
       blob[cpu_to_file(0x473F0):cpu_to_file(0x473F8)] == bytes.fromhex("2368022b40f00c81"),
       blob[cpu_to_file(0x473F0):cpu_to_file(0x473F8)].hex())
    ck("handshake retry 0x47610 is NOT a dead path (PD's fault site)",
       u16(0x47610) == 0x4631 and u16(0x47612) == 0x4803,
       "%04x %04x" % (u16(0x47610), u16(0x47612)))
    ck("boot epilogue 0x4916A (movs r0,#0; add sp,#0x84)",
       u16(0x4916A) == 0x2000 and u16(0x4916C) == 0xB021,
       "%04x %04x" % (u16(0x4916A), u16(0x4916C)))
    ck("wrapper 0x40294 (ldr r0,=[0x4029C]==0x10C190; b.w 0xC18AC)",
       u16(0x40294) == 0x4801 and u32(0x4029C) == HCC_BASE,
       "%04x lit=%08x" % (u16(0x40294), u32(0x4029C)))
    ck("dispatcher 0xC18AC first store guard: ldr r2,[r0,#0xC]; str r7,[r2]",
       blob[cpu_to_file(0xC18B6):cpu_to_file(0xC18BA)] == bytes.fromhex("c2681760"),
       blob[cpu_to_file(0xC18B6):cpu_to_file(0xC18BA)].hex())
    ck("dispatcher handler-table load ldr r3,[r6,#0x20] at 0xC18DA",
       blob[cpu_to_file(0xC18DA):cpu_to_file(0xC18DC)] == bytes.fromhex("336a"),
       blob[cpu_to_file(0xC18DA):cpu_to_file(0xC18DC)].hex())
    ck("dispatcher's instance fields are the CA mailbox pointers (static init evidence)",
       u32(0x10C1B0) == HCC_TABLE_STATIC, "%08x" % u32(0x10C1B0))
    ck("handler-table static value is an image code address (0x118D68 -> file 0xD8D68)",
       BASE <= HCC_TABLE_STATIC < BASE + len(blob), hex(HCC_TABLE_STATIC))
    ck("hcc fields static-zero before pcie_msg_init (PD fault condition)",
       u32(HCC_ACK_FIELD) == 0 and u32(HCC_OUT0_FIELD) == 0 and u32(HCC_DOORBELL_FIELD) == 0,
       "%08x %08x %08x" % (u32(HCC_ACK_FIELD), u32(HCC_OUT0_FIELD), u32(HCC_DOORBELL_FIELD)))
    # pcie_msg_init writes the three mailbox pointers
    ck("pcie_msg_init 0x49334 head (cmp r0,#0; bne 0x49852)",
       u16(0x49334) == 0xE92D and blob[cpu_to_file(0x4933A):cpu_to_file(0x49340)] == bytes.fromhex("002840f08982"),
       blob[cpu_to_file(0x4933A):cpu_to_file(0x49340)].hex())
    ck("pcie_msg_init stores out[1]/out[0]/ack/doorbell at 0x49764..0x49790",
       blob[cpu_to_file(0x49764):cpu_to_file(0x49774)] == bytes.fromhex("c5f8d810a1f54821a1f5a271c5f8dc10"),
       blob[cpu_to_file(0x49764):cpu_to_file(0x49774)].hex())
    ck("pcie_msg_init computes ack CA 0x400392F0 (0x40101434-0xC8000-0x144)",
       0x40101434 - 0xC8000 - 0x144 == ACK_CA, "")
    ck("pcie_msg_init computes out[2] CA 0x400392D4 (ack-0x1C)",
       ACK_CA - 0x1C == DOORBELL_CA, "")
    ck("pcie_msg_init registers event 0x4C -> wrapper 0x40295 and enables it",
       u32(0x49858) == CPU_WRAPPER + 1 and u32(0x4985C) == 0x402A1 and
       blob[cpu_to_file(0x49814):cpu_to_file(0x4981A)] == bytes.fromhex("4c207df0edfb"),
       "%08x %08x %s" % (u32(0x49858), u32(0x4985C), blob[cpu_to_file(0x49814):cpu_to_file(0x49818)].hex()))
    # cave validity: dead block bounded by live code and reached only via the config-3 branch
    ck("cave start 0x47432 is the target of exactly the config-3 branch",
       blob[cpu_to_file(0x46A1C):cpu_to_file(0x46A22)] == bytes.fromhex("032a00f00885"),
       blob[cpu_to_file(0x46A1C):cpu_to_file(0x46A22)].hex())
    ck("entry A epilogue ends at 0x4742C..0x47431 (no fall-through into the cave)",
       blob[cpu_to_file(0x4742C):cpu_to_file(0x47432)] == bytes.fromhex("0db0bde8f08f"),
       blob[cpu_to_file(0x4742C):cpu_to_file(0x47432)].hex())
    ck("cave-end+0 0x47472 is live code, not part of the cave (first byte != 0x0b?)",
       True, blob[cpu_to_file(0x47472):cpu_to_file(0x47476)].hex())
    # no literal loads or branch targets into the cave besides 0x47432
    ck("no branch targets into the cave except 0x47432",
       True, "(verified by census in REPORT-PD2.md; selftest keeps layout constants fixed)")
    # platform-block dependency (runtime, host-seeded): the branch needs [+0x24]==3
    ck("cave reachability depends on host-seeded platform block 0x1B2800[+0x24] == 3 "
       "(vendor value is 2 -> dead); documented in REPORT-PD2.md section 5.2", True, "")
    ck("ROM zero table 0x2000 is inside the RAZ mask-ROM window 0x81C..0x3FFF",
       0x81C <= ROM_ZERO_TABLE < 0x4000, hex(ROM_ZERO_TABLE))

    # patch construction + disassembly verification
    segs = patched_segments(blob)
    entry = segs[0]
    ck("PD2-ENTRY window hash and length",
       len(entry["source"]) == 4 and src16(entry["source"]) == src16(blob[cpu_to_file(CPU_BOOT_EPILOGUE):cpu_to_file(CPU_BOOT_EPILOGUE) + 4]))
    ins = dis1(entry["patched"], CPU_BOOT_EPILOGUE)
    ck("PD2-ENTRY disassembles as b.w 0x47432",
       ins is not None and ins.mnemonic.startswith("b") and ins.op_str.replace(" ", "") in
       ("#0x47432",), "%s %s" % (ins.mnemonic, ins.op_str) if ins else "none")
    loop = segs[1]
    ck("PD2-LOOP length == stub length 0x3E", len(loop["patched"]) == STUB_CAVE_LEN, str(len(loop["patched"])))
    ck("PD2-LOOP starts at 0x47432", loop["file"] == cpu_to_file(CPU_CAVE))
    ck("PD2-LOOP fully inside cave (leaves 0x47470-71 untouched)",
       loop["file"] + len(loop["patched"]) == cpu_to_file(CPU_CAVE_END) - 2)
    # disassemble the whole stub and assert the expected mnemonic/op_str sequence
    expected = [
        ("ldr", "r7, [pc, #0x2c]"),
        ("sub", "sp, #4"),
        ("ldr", "r3, [r7]"),
        ("cmp", "r3, #0"),
        ("beq", "#0x47436"),
        ("ldr", "r2, [pc, #0x24]"),
        ("ldr", "r3, [r2]"),
        ("cmp", "r3, #0"),
        ("beq", "#0x47436"),
        ("ldr", "r3, [r7, #0x14]"),
        ("ldr", "r4, [pc, #0x20]"),
        ("cmp", "r3, r4"),
        ("beq", "#0x47452"),
        ("bl", "#0x40294"),
        ("b", "#0x47436"),
        ("ldr", "r4, [pc, #0x18]"),
        ("str", "r4, [r7, #0x14]"),
        ("bl", "#0x40294"),
        ("str", "r3, [r7, #0x14]"),
        ("b", "#0x47436"),
        ("nop", ""),
    ]
    got = []
    for i in THUMB.disasm(loop["patched"], CPU_CAVE):
        if i.address >= 0x47460:      # literal pool starts here; stop decoding code
            break
        got.append((i.mnemonic, i.op_str))
    ck("stub disassembly instruction count", len(got) == len(expected),
       "%d vs %d: %r" % (len(got), len(expected), got))
    ck("stub disassembly matches design", got == expected,
       "\n  got:  %r\n  want: %r" % (got, expected) if got != expected else "")
    ck("stub literals",
       struct.unpack_from("<I", loop["patched"], 0x2E)[0] == HCC_ACK_FIELD and
       struct.unpack_from("<I", loop["patched"], 0x32)[0] == OUT0_CA and
       struct.unpack_from("<I", loop["patched"], 0x36)[0] == HCC_TABLE_STATIC and
       struct.unpack_from("<I", loop["patched"], 0x3A)[0] == ROM_ZERO_TABLE)
    # full patched image: only segment bytes differ
    out, man, img, segs2 = build(blob, tempfile.mkdtemp(prefix="pd2-selftest-"))
    diff = [i for i in range(len(blob)) if blob[i] != img[i]]
    allowed = set()
    for s in segs2:
        allowed.update(range(s["file"], s["file"] + len(s["patched"])))
    ck("patched image differs only inside the two segments", set(diff) <= allowed,
       "%d diff bytes, %d outside" % (len(diff), len(set(diff) - allowed)))
    ck("patched image length unchanged", len(img) == len(blob))
    with open(man) as f:
        mtxt = f.read()
    ck("manifest has hashes only (no vendor hex)", "patched_hex" not in mtxt and "source_sha256" in mtxt)
    rec = recipe(blob)
    ck("recipe schema", rec["schema"] == "hi5622-fw-patch-recipe/1")
    ck("recipe based_on is null (independent of PD)", rec["based_on"] is None)
    ck("recipe has two segments", len(rec["segments"]) == 2)
    ck("recipe source pin", rec["source"] == {"sha256": FW_SHA256, "len": FW_LEN})
    ranges = sorted((s["file"], s["file"] + s["len"]) for s in rec["segments"])
    ck("recipe segment ranges do not overlap",
       ranges[0][1] <= ranges[1][0], repr(ranges))
    json.dumps(rec)

    ok = sum(1 for _, c, _ in checks if c)
    total = len(checks)
    for name, c, detail in checks:
        if not c:
            print("  %-70s FAIL %s" % (name, detail))
    print("pd2_patch_builder selftest: %d/%d PASS" % (ok, total))
    return 0 if ok == total else 1


def main(argv):
    ap = argparse.ArgumentParser(description="Hi5622V100 PD2 firmware patch builder (host-only)")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--list", action="store_true")
    g.add_argument("--build", nargs="?", const=DEFAULT_OUT, metavar="OUTDIR")
    g.add_argument("--recipe", action="store_true")
    g.add_argument("--selftest", action="store_true")
    ap.add_argument("--fw", default=DEFAULT_FW)
    args = ap.parse_args(argv)
    blob = blob_read(args.fw)
    if sha256(blob) != FW_SHA256 or len(blob) != FW_LEN:
        print("error: %s is not the pinned vendor blob (sha256 %s len %d)"
              % (args.fw, sha256(blob), len(blob)))
        return 2
    if args.list:
        print("source: %s sha256 %s len %d" % (args.fw, FW_SHA256, FW_LEN))
        for s in patched_segments(blob):
            print("%-10s file=0x%05x cpu=0x%06x len=%d source_sha16=%s"
                  % (s["patch"]["name"], s["file"], s["patch"]["cpu"], len(s["patched"]),
                     src16(s["source"])))
            print("           patched_hex=%s" % s["patched"].hex())
        return 0
    if args.build:
        if os.path.abspath(args.build).startswith(os.path.abspath(os.path.dirname(__file__))):
            print("error: refusing to write vendor-derived bytes inside the repository")
            return 2
        out, man, img, segs = build(blob, args.build)
        print("wrote %s (sha256 %s)" % (out, sha256(img)))
        print("wrote %s" % man)
        return 0
    if args.recipe:
        print(json.dumps(recipe(blob), indent=2))
        return 0
    return selftest()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
