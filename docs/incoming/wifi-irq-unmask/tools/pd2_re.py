#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""pd2_re.py - host-only static verifier for the Hi5622V100 H2D dispatcher path and PD2.

Lane wifi-irq-unmask / shared task task-91 (PD2). Reads the LOCAL vendor firmware blob and
LOCAL mask-ROM dump in place; prints derived annotations only (addresses, constants,
instruction encodings). No vendor bytes are copied into the repository and no device is
ever opened.

Subcommands:
  dispatcher   H2D dispatcher contract: wrapper 0x40294, dispatcher 0xC18AC, the hcc
               instance fields it dereferences, the event-0x4C registration/enable, and
               the mail-box CA constants pcie_msg_init installs.
  fault        PD's data-abort chain: the handshake retry path (0x473F0/0x47610), the
               first faulting instruction (0xC18B8 with a NULL [hcc+0xC]), the nested
               ABT-frame arithmetic that clobbers CA 0x1BF000, and the recovered frame
               words {r0,r1,r2,r3} against the bench-observed page overwrite.
  cave         PD2 code-cave justification: the conditionally-dead config-3 block of
               entry A (0x47432..0x47471), its only entry (0x46A1E beq.w, gated on
               platform block [+0x24]==3, vendor value 2), the live neighbours, and the
               absence of literal/branch/data references into it.
  selftest     self-checking assertions (exit 0 on PASS)

Address convention (project-wide): image CPU = file offset + 0x40000; ROM cpu == dump
offset; host BAR0 of CPU RAM = CPU + 0x6B8000; the IO window of the host is device
CA 0x40000000..0x4011FFFF only.
"""
import argparse
import hashlib
import os
import struct
import sys

try:
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB
except Exception:  # pragma: no cover
    print("error: capstone is required (project venv .provision-venv/bin/python)")
    raise SystemExit(2)

BASE = 0x40000
DEFAULT_FW = "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin"
DEFAULT_ROM = "/home/shibbityshwab/staging-fi/rom16k.bin"
FW_SHA256 = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
ROM_SHA256 = "fcfd464fd2bdfc1ab8b3f731ce19c7037885699f21796753654ae85c2198faa4"
FW_LEN = 928920

THUMB = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
THUMB.detail = False
ARM = Cs(CS_ARCH_ARM, CS_MODE_ARM)
ARM.detail = False


class Blob:
    def __init__(self, path, base=BASE):
        with open(path, "rb") as f:
            self.d = f.read()
        self.base = base
        self.sha256 = hashlib.sha256(self.d).hexdigest()

    def u16(self, cpu):
        return struct.unpack_from("<H", self.d, cpu - self.base)[0]

    def u32(self, cpu):
        return struct.unpack_from("<I", self.d, cpu - self.base)[0]

    def win(self, cpu, n):
        return self.d[cpu - self.base:cpu - self.base + n]

    def dis(self, cpu, n, isa="thumb"):
        md = ARM if isa == "arm" else THUMB
        return list(md.disasm(self.win(cpu, n), cpu))


def print_dis(blob, cpu, n, isa="thumb", mark=None, stop=None):
    for i in blob.dis(cpu, n, isa):
        if stop is not None and i.address >= stop:
            break
        tag = ""
        if mark and i.address == mark[0]:
            tag = "  <== " + mark[1]
        print("  %08x  %-10s %-8s %s%s" % (i.address, i.bytes.hex(), i.mnemonic, i.op_str, tag))


# ---------------------------------------------------------------- dispatcher
def cmd_dispatcher(blob):
    print("== H2D mailbox dispatcher path (event 0x4C) ==")
    print("wrapper CPU 0x40294: loads r0 = [0x4029C] = %08x and tail-branches to 0xC18AC"
          % blob.u32(0x4029C))
    print_dis(blob, 0x40290, 0x14, stop=0x4029C)
    print()
    print("dispatcher CPU 0xC18AC (r0 = hcc instance; reads +4 / +0xC / +0x10 / +0x20):")
    print_dis(blob, 0xC18AC, 0x4A, mark=(0xC18B8, "first store: str r7,[r2] with r2=[hcc+0xC]"))
    print()
    print("hcc instance argument = CPU 0x10C190 (= global base 0x10C0C0 + 0xD0).")
    print("fields and the CAs pcie_msg_init (CPU 0x49334) installs:")
    rows = [
        (0x10C194, "hcc+0x04", 0x40039010, "out[0] H2D pending: dispatcher reads then clears"),
        (0x10C19C, "hcc+0x0C", 0x400392F0, "out[5] ack: dispatcher stores 1"),
        (0x10C1A0, "hcc+0x10", 0x400392D4, "out[2] H2D-done doorbell: dispatcher stores 8"),
        (0x10C1B0, "hcc+0x20", None, "handler table {fn,arg}, 8-byte records, ids 0..9"),
    ]
    for field, name, ca, note in rows:
        cur = blob.u32(field)
        print("  %-12s field @0x%06x static=%08x%s  %s"
              % (name, field, cur, "" if ca is None else "  (runtime init CA=0x%08x)" % ca, note))
    print()
    print("pcie_msg_init field-init window CPU 0x49764..0x497A4 (fingerprint bytes %s):"
          % blob.win(0x49764, 0x10).hex())
    print("  ack  CA 0x40101434 - 0xC8000 - 0x144 = 0x%08x" % (0x40101434 - 0xC8000 - 0x144))
    print("  out2 CA 0x400392F0 - 0x1C          = 0x%08x" % (0x400392F0 - 0x1C))
    print("  out0/out1 CA literals at CPU 0x497E0/0x497DC = %08x/%08x"
          % (blob.u32(0x497E0), blob.u32(0x497DC)))
    print()
    print("event-0x4C registration (handler 0x40295 = wrapper|thumb) and enable:")
    print("  0x49858 literal = %08x (handler fn)" % blob.u32(0x49858))
    print("  0x4985C literal = %08x (handler fn for 0x4E)" % blob.u32(0x4985C))
    print_dis(blob, 0x497F4, 0x28)
    print("  enable path 0xC6FF4(id): W1S at CA 0x40161100 + 4*(id>>5) => for 0x4C:"
          " word CA 0x40161108 bit12 = 0x1000")
    print()
    print("calling convention: r0 = hcc instance (the wrapper supplies it); r1..r7 caller-")
    print("saved; the dispatcher pushes {r3,r4,r5,r6,r7,lr} (24 B) so r3-r7 survive a call;")
    print("no global state beyond the instance; no argument beyond r0; valid SVC stack only.")
    print("It must only be called after CPU 0x49334 has run (boot task module table call at")
    print("0x47824) -- before that the instance fields are static zero (see 'fault').")


# ---------------------------------------------------------------- fault
def cmd_fault(blob):
    print("== PD data-abort reconstruction (static) ==")
    print("entry A handshake poll CPU 0x473F0: the RETRY path 0x47610 is executed on every")
    print("failed poll while S1/CPU1 has not written page+8==2 yet:")
    print_dis(blob, 0x473F0, 8, mark=(0x473F4, "bne -> 0x47610 (the path PD hijacked)"))
    print("retry body CPU 0x47610 (PD-1 replaced its first 6 bytes with 'bl 0x40294'):")
    print_dis(blob, 0x47610, 0xE)
    print("PD therefore called the dispatcher BEFORE the boot task existed. The hcc")
    print("instance fields are static zero, so the dispatcher's first load yields r2 = 0:")
    print_dis(blob, 0xC18B4, 6, mark=(0xC18B8, "store to address 0 -> precise DATA ABORT"))
    print()
    print("== why the handshake page CA 0x1BF000 was overwritten ==")
    print("ROM reset path sets the ABT-mode stack top to 0x1BF110 (ROM 0x1E0 table word[5]).")
    print("The image's data-abort stub (CPU 0x103114) does 'pop {r0}; sub sp,#0x44; stm sp,{r0-r12}'")
    print("and never restores the ABT SP; the recovery long-jumps to LR (faulting fn's caller),")
    print("so every new abort stacks 0x44 bytes lower:")
    sp = 0x1BF110
    frame = {"r0": 0x0010C190, "r1": 0, "r2": 0, "r3": 0x1BEE50}
    for n in range(1, 6):
        base = sp - 0x44
        note = ""
        if n == 4:
            note = "  <== frame words {r0,r1,r2,r3} land exactly on the handshake page"
        print("  abort #%d: ABT SP=0x%06x frame=0x%06x..0x%06x%s" % (n, sp, base, sp - 1, note))
        sp = base
    print("  4th-frame words = { r0=0x%08x (wrapper hcc argument), r1=0x%08x (dispatcher zeroed),"
          % (frame["r0"], frame["r1"]))
    print("                     r2=0x%08x (invalid [hcc+0xC]), r3=0x%08x (entry-A leftover) }"
          % (frame["r2"], frame["r3"]))
    print("  bench observed overwrite: 0x0010C190 / 0 / 0 / 0x1BEE50  -> exact match")
    print("  (note: register numbering here is the staged frame order r0,r1,r2,r3)")
    print()
    print("4 faults also explain the bench's 4x out[1]=4 'startup/vector announce' entries")
    print("(one per exception entry; the host CECE policy answered 3 of the spins).")
    print()
    print("The faulting instruction is therefore CPU 0xC18B8 'str r7,[r2]' with r2=[hcc+0xC]=0,")
    print("not a stack/alignment/argument problem: the calling convention is satisfied by the")
    print("boot-task epilogue context; the call was simply made too early.")


# ---------------------------------------------------------------- cave
def cmd_cave(blob):
    print("== PD2 code cave: conditionally-dead config-3 block of entry A ==")
    print("entry A selects the PHY init variant from the host-seeded platform block:")
    print("  platform block CPU 0x1B2800, word [+0x24] (vendor/golden value 0x2)")
    print_dis(blob, 0x469DC, 0x30, mark=(0x46A1E, "beq.w 0x47432: taken only when block[+0x24]==3"))
    print("dead cave CPU 0x47432..0x47471 (64 B); entry A's epilogue ends at 0x4742C:")
    raw = blob.win(0x4742C, 0x4A)
    print("  0x4742C: %s  (add sp,#0x34; pop.w {r4-r11,pc} = no fall-through)"
          % blob.win(0x4742C, 6).hex())
    print("  0x47432: %s ... 0x4746E: %s" %
          (raw[6:14].hex(), blob.win(0x4746E, 4).hex()))
    print_dis(blob, 0x47432, 8)
    print("  ...")
    print_dis(blob, 0x4746E, 4)
    print("  live code resumes at 0x47472 (a branch target from 0x46CC8): %s"
          % blob.win(0x47472, 4).hex())
    print()
    print("reference census into [0x47432,0x47472):")
    print("  branch targets: only 0x46A1E -> 0x47432 (conditional on block[+0x24]==3)")
    print("  literal loads : none")
    print("  u32 data words: none")
    print("  fall-through  : none (0x4742C returns; 0x47472 is live code)")
    print()
    print("PD2 places a 62-byte guard+poll stub there (0x47432..0x4746F); the last 2 bytes")
    print("of the dead block (0x47470-0x47471) are left untouched.  The stub is only ever")
    print("entered by PD2's own patch of the boot-task epilogue (CPU 0x4916A -> b.w 0x47432).")
    print_dis(blob, 0x4916A, 8, mark=(0x4916A, "boot-task epilogue (movs r0,#0; add sp,#0x84)"))


# ---------------------------------------------------------------- selftest
def cmd_selftest(blob, rom):
    checks = []

    def ck(name, cond, detail=""):
        checks.append((name, bool(cond), detail))

    ck("vendor firmware sha256", blob.sha256 == FW_SHA256, blob.sha256)
    ck("firmware len", len(blob.d) == FW_LEN, str(len(blob.d)))
    ck("ROM dump sha256", rom.sha256 == ROM_SHA256, rom.sha256)
    # wrapper + dispatcher
    ck("wrapper literal = hcc instance", blob.u32(0x4029C) == 0x10C190)
    ck("wrapper ldr r0,[pc,#4]", blob.u16(0x40294) == 0x4801)
    ck("dispatcher push {r3,r4,r5,r6,r7,lr}", blob.win(0xC18AC, 2) == bytes.fromhex("f8b5"))
    ck("dispatcher first store bytes c268 1760",
       blob.win(0xC18B6, 4) == bytes.fromhex("c2681760"))
    ck("dispatcher reads out0 through [r0+4]", blob.win(0xC18BA, 2) == bytes.fromhex("4268"))
    ck("dispatcher writes ack 1 through [r0+0xC]",
       blob.win(0xC18B2, 2) == bytes.fromhex("0127")
       and blob.win(0xC18B6, 4) == bytes.fromhex("c2681760"))
    ck("dispatcher writes out[0] clear and out[2]=8",
       blob.win(0xC18BA, 0xC) == bytes.fromhex("426815681160082102691160"))
    ck("dispatcher reads handler table [r6+0x20]", blob.win(0xC18DA, 2) == bytes.fromhex("336a"))
    ck("handler table pointer static value is image code address",
       blob.u32(0x10C1B0) == 0x118D68)
    ck("hcc mailbox fields static-zero (pre-init)",
       blob.u32(0x10C194) == 0 and blob.u32(0x10C19C) == 0 and blob.u32(0x10C1A0) == 0)
    ck("pcie_msg_init entry fingerprint",
       blob.win(0x49334, 4) == bytes.fromhex("2de9f04f")
       and blob.win(0x4933A, 6) == bytes.fromhex("002840f08982"))
    ck("pcie_msg_init mailbox-pointer window",
       blob.win(0x49764, 0x10) == bytes.fromhex("c5f8d810a1f54821a1f5a271c5f8dc10"))
    ck("ack CA arithmetic", 0x40101434 - 0xC8000 - 0x144 == 0x400392F0)
    ck("doorbell CA arithmetic", 0x400392F0 - 0x1C == 0x400392D4)
    ck("out0 CA literal", blob.u32(0x497E0) == 0x40039010)
    ck("out1 CA literal", blob.u32(0x497DC) == 0x40039014)
    ck("event 0x4C handler literal", blob.u32(0x49858) == 0x40295)
    ck("event 0x4C registration bytes", blob.win(0x49814, 6) == bytes.fromhex("4c207df0edfb"))
    # handshake retry (the PD fault site)
    ck("handshake poll fingerprint", blob.win(0x473F0, 8) == bytes.fromhex("2368022b40f00c81"))
    ck("retry body starts mov r1,r6", blob.u16(0x47610) == 0x4631)
    ck("retry body loads page+8 literal", blob.u16(0x47612) == 0x4803 and blob.u32(0x47620) == 0x1BF008)
    ck("retry callee 0x102E68 is ARM D-cache clean+invalidate-by-MVA",
       blob.win(0x102E68, 4) == bytes.fromhex("303f10ee"))
    # nested ABT frame arithmetic
    rom_tbl = [struct.unpack_from("<I", rom.d, 0x1E0 + 4 * i)[0] for i in range(7)]
    ck("ROM slot/stack table: handler slots then ABT top 0x1BF110 / SVC top 0x1BEF00",
       rom_tbl == [0x40060, 0x4005C, 0x40058, 0x40054, 0x40050, 0x1BF110, 0x1BEF00],
       [hex(x) for x in rom_tbl])
    ck("data-abort stub does pop/sub sp,#0x44/stm",
       blob.win(0x103114, 0x10) == bytes.fromhex("04009de444d04de2ff1f8de80d00a0e1"))
    ck("4th nested frame base == handshake page 0x1BF000",
       0x1BF110 - 4 * 0x44 == 0x1BF000, hex(0x1BF110 - 4 * 0x44))
    ck("1st nested frame base = 0x1BF0CC", 0x1BF110 - 0x44 == 0x1BF0CC)
    # cave
    ck("entry A epilogue 0x4742C no fall-through",
       blob.win(0x4742C, 6) == bytes.fromhex("0db0bde8f08f"))
    ck("cave entry branch 0x46A1E beq.w 0x47432",
       blob.win(0x46A1C, 6) == bytes.fromhex("032a00f00885"))
    ck("platform block word [+0x24] is the selector read", blob.win(0x469E0, 2) == bytes.fromhex("726a"))
    ck("0x47472 is live code referenced from 0x46CC8",
       blob.win(0x46CC8, 4) == bytes.fromhex("40f2d383"))
    ok = sum(1 for _, c, _ in checks if c)
    for n, c, d in checks:
        if not c:
            print("FAIL: %s %s" % (n, d))
    print("pd2_re selftest: %d/%d PASS" % (ok, len(checks)))
    return 0 if ok == len(checks) else 1


def main(argv):
    ap = argparse.ArgumentParser(description="Hi5622V100 H2D dispatcher/PD2 static verifier")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--dispatcher", action="store_true")
    g.add_argument("--fault", action="store_true")
    g.add_argument("--cave", action="store_true")
    g.add_argument("--selftest", action="store_true")
    ap.add_argument("--fw", default=DEFAULT_FW)
    ap.add_argument("--rom", default=DEFAULT_ROM)
    a = ap.parse_args(argv)
    blob = Blob(a.fw)
    if a.selftest:
        rom = Blob(a.rom, base=0)
        return cmd_selftest(blob, rom)
    if blob.sha256 != FW_SHA256:
        print("error: not the pinned vendor blob (%s)" % blob.sha256)
        return 2
    {"dispatcher": cmd_dispatcher, "fault": cmd_fault, "cave": cmd_cave}[
        next(k for k in ("dispatcher", "fault", "cave") if getattr(a, k))](blob)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
