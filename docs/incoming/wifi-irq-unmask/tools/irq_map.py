#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""irq_map.py - host-only IRQ/vector/mailbox map for the Hi5622V100 vendor Wi-Fi image.

Lane wifi-irq-unmask (shared task task-90). Reads the LOCAL vendor firmware blob and the
LOCAL mask-ROM dump IN PLACE and prints derived annotations (addresses, constants,
instruction encodings). It never copies vendor bytes into this repository and never opens a
device.

Address convention (project-wide):
    CPU address = file offset + 0x40000      (image)
    ROM CPU address == ROM dump file offset
    host BAR0 (CPU RAM, region 5) = CPU + 0x6B8000
    host BAR0 (device IO, region 3) = 0x3B8000 + (CA - 0x40000000), CA 0x40000000..0x4011FFFF ONLY
    CA 0x40160000+ is NOT host-visible (do not wrap it into region 3; the old
    hcc_irq_probe arithmetic is an instrument artifact).

Subcommands:
    rom        ROM vector table -> image trampoline; FIQ chain
    map        image IRQ trampoline / dispatcher / scheduler and task-start chain
    unmask     cpsie/cpsid census over the whole image
    mailbox    event 0x4C (H2D mailbox) registration, enable, priority and target decode
    patch      patch-site table, verified against the local blob
    selftest   self-checking assertions (exit 0 on PASS)
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

# Patch table (shared with patch_builder.py): file offsets, source-window sha256 prefix,
# patched bytes (our encodings). Original bytes are never embedded: the window is verified by
# hash before analysis/apply.
PATCHES = {
    "PA": {
        "about": "initial task CPSR I bit clear (task create 0xC7218)",
        "segments": [
            {"file": 0x872F6, "len": 2, "sha16": "51028cac2ede4fff", "patched": "3321",
             "isa": "thumb", "expect": "movs r1, #0x33"},
        ],
    },
    "PB": {
        "about": "remove cpsid i at the head of the scheduler switch 0xC24DE",
        "segments": [
            {"file": 0x824F8, "len": 2, "sha16": "560de662284ac420", "patched": "00bf",
             "isa": "thumb", "expect": "nop"},
        ],
    },
    "PC": {
        "about": "clear I in the ARM arch context-save SPSR (0x102C14)",
        "segments": [
            {"file": 0xC2C20, "len": 8, "sha16": "1498affc745102d7",
             "patched": "8040c4e300f020e3", "isa": "arm",
             "expect": "bic r4, r4, #0x80 ; nop"},
        ],
    },
    "PD": {
        "about": "redirect the boot-task success epilogue into a permanent H2D dispatcher poll loop",
        "segments": [
            {"file": 0x7610, "len": 6, "sha16": "e81740ccb394d442",
             "patched": "f8f740fefce7", "isa": "thumb", "expect": "bl 0x40294 ; b 0x47610"},
            {"file": 0x916A, "len": 4, "sha16": "c5975cf4bd702f88",
             "patched": "fef751ba", "isa": "thumb", "expect": "b.w 0x47610"},
        ],
    },
}

UNMASK_SITES = {
    0xC2B80: "0xC2B7A idle/unmask helper - called by the task-entry trampoline 0xC2BD0 and "
             "tail-called from the scheduler switch 0xC24DE at 0xC269A (every task start/resume)",
    0xC2F46: "inside event dispatcher 0xC2EFC - temporary unmask around the handler blx (IRQ ctx)",
    0xC26C0: "deferred-dispatch loop 0xC26B8 - reached from exception-return helper 0x103B50",
    0x120A32: "CPU1/second event dispatcher 0x120A0C (IRQ context only)",
    0x120A60: "CPU1/second event dispatcher tail 0x120A0C (IRQ context only)",
    0x121BBC: "late/thread code in the image tail (secondary-core bootstrap region)",
}


class Blob:
    def __init__(self, path, base=BASE):
        with open(path, "rb") as f:
            self.d = f.read()
        self.base = base
        self.thumb = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
        self.thumb.detail = False
        self.arm = Cs(CS_ARCH_ARM, CS_MODE_ARM)
        self.arm.detail = False

    def u32(self, cpu):
        off = cpu - self.base
        if off < 0 or off + 4 > len(self.d):
            return None
        return struct.unpack_from("<I", self.d, off)[0]

    def win(self, cpu, n):
        return self.d[cpu - self.base: cpu - self.base + n]

    def dis(self, cpu, n, isa="thumb"):
        md = self.arm if isa == "arm" else self.thumb
        return list(md.disasm(self.win(cpu, n), cpu))


def iter_image(fw):
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
    md.skipdata = True
    yield from md.disasm(fw.d, BASE)


def sha16(fw, cpu, n):
    return hashlib.sha256(fw.win(cpu, n)).hexdigest()[:16]


# ----------------------------------------------------------------------------- commands
def cmd_rom(fw, rom):
    if rom is None:
        print("ROM dump not present (set HI5622_ROM); skipping ROM command")
        return 1
    print("ROM sha256:", hashlib.sha256(rom.d).hexdigest())
    names = ["reset", "undef", "svc", "prefetch", "abort", "reserved", "IRQ", "FIQ"]
    ok = True
    for i in range(8):
        word = rom.u32(i * 4)
        if word == 0xE59FF018:
            lit_addr = i * 4 + 8 + 0x18
        elif word == 0xE59FF014:
            lit_addr = i * 4 + 8 + 0x14
        else:  # reserved NOP (0xE320F000)
            lit_addr = None
        lit = rom.u32(lit_addr) if lit_addr is not None else None
        shown = "0x%08x" % lit if lit is not None else "(nop)"
        print("  vector %d %-8s -> ROM lit@0x%02x = %s"
              % (i, names[i], lit_addr if lit_addr is not None else 0, shown))
        if names[i] == "IRQ" and lit != 0x00040064:
            ok = False
        if names[i] == "FIQ" and lit != 0x7C:
            ok = False
    rom_slots = [rom.u32(0x1E0 + 4 * i) for i in range(5)]
    print("  ROM slot table 0x1E0:", [hex(x) for x in rom_slots])
    print("  derived: ROM IRQ vector -> image CPU 0x40064 (ARM second-level trampoline)")
    print("  derived: ROM FIQ -> ROM 0x7C -> slot table -> image header +0x50 -> 0xC2EEC")
    print("check:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def cmd_map(fw):
    print("image header 0x40050..0x40064:", [hex(fw.u32(a)) for a in range(0x40050, 0x40068, 4)])
    print("\n-- image IRQ/FIQ second-level trampoline CPU 0x40064 (ARM) --")
    for ins in fw.dis(0x40064, 0x60, "arm"):
        print("  %08x  %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))
    print("\n-- event dispatcher 0xC2EFC (Thumb, first 0x30) --")
    for ins in fw.dis(0xC2EFC, 0x30):
        print("  %08x  %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))
    print("  derived: idx = [CA 0x4016010C] & 0x3FF; handler=[CA 0x17D430+4*idx];")
    print("           EOI: CA 0x40160110 = status word; table CA 0x17D430 = 0x17D398+0x98")
    print("\n-- scheduler first dispatch tail 0xC70E8..0xC70FC --")
    for ins in fw.dis(0xC70E8, 0x16):
        print("  %08x  %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))
    print("  derived: 0xC70F8 b.w 0x103B60 -> Thumb 'bx pc' -> ARM b 0x102C04")
    print("\n-- ARM first-task restore 0x102C04 and arch switch 0x102C14 --")
    for ins in fw.dis(0x102C04, 0x40, "arm"):
        print("  %08x  %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))
    print("\n-- initial context build, task create 0xC7218 from 0xC72EA --")
    for ins in fw.dis(0xC72EA, 0x38):
        print("  %08x  %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))
    print("  derived: CPSR=0xB3 (SVC|I|T); PC=0xC2B89 (entry trampoline); LR=0xC2E1D (exit)")
    print("\n-- task-entry trampoline 0xC2B88 tail --")
    for ins in fw.dis(0xC2BB8, 0x2C):
        print("  %08x  %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))
    print("\n-- idle/unmask helper 0xC2B7A --")
    for ins in fw.dis(0xC2B7A, 0x12):
        print("  %08x  %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))
    print("\n-- scheduler switch exit 0xC2682..0xC269C (resume/idle path) --")
    for ins in fw.dis(0xC2682, 0x1A):
        print("  %08x  %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))
    return 0


def cmd_unmask(fw):
    seen = {}
    for ins in iter_image(fw):
        m = ins.mnemonic.split(".")[0]
        if m in ("cpsie", "cpsid"):
            seen.setdefault((ins.address, m, ins.op_str), 0)
    for (addr, m, ops) in sorted(seen):
        role = UNMASK_SITES.get(addr, "")
        print("  %08x  %-6s %-4s  %s" % (addr, m, ops, role))
    n_ie = sum(1 for (a, m, o) in seen if m == "cpsie" and o.strip() in ("i", ""))
    n_ife = sum(1 for (a, m, o) in seen if m == "cpsid" and o.strip() in ("i", ""))
    print("total cpsie i:", n_ie, " cpsid i:", n_ife, " cpsie f:", sum(1 for (a, m, o) in seen if m == "cpsie" and o.strip() == "f"))
    return 0


def cmd_mailbox(fw):
    print("-- event 0x4C registration/enable in pcie_msg_init 0x49334 --")
    for ins in fw.dis(0x496C0, 0x170):
        a = ins.address
        note = ""
        if a in (0x496CA, 0x496DC):
            note = " <- 0xC74B0 register handler for 0x2D/0x2E"
        elif a in (0x496EA, 0x496F0):
            note = " <- 0xC6FF4 enable 0x2D/0x2E"
        elif a in (0x49754, 0x4975A):
            note = " <- 0xC6DB8 raise/unmask 0x4C / 0x4E"
        elif a in (0x497D8,):
            note = " <- CA 0x40101434 D2H doorbell pointer"
        elif a in (0x497DC, 0x497E0):
            note = " <- CA 0x40039014 out[1] / CA 0x40039010 out[0] pointers"
        elif a == 0x497D4:
            note = " <- CA 0x400392D4 out[2] doorbell pointer (from 0x400392F0-0x144-0x1c)"
        if note:
            print("  %08x  %-8s %s%s" % (a, ins.mnemonic, ins.op_str, note))
    print("\n-- derived event-controller words for the mailbox ids (host-invisible CA) --")
    coreword = 0x60  # [CA 0x17D398] bench-read on the golden boot
    for eid in (0x1D, 0x2D, 0x2E, 0x4C, 0x4E, 0x46):
        bank = eid >> 5
        bit = 1 << (eid & 31)
        en_ca = 0x40161100 + 4 * bank
        pr_ca = 0x40161400 + (eid & ~3)
        tg_ca = 0x40161800 + (eid & ~3)
        pri = (eid >> 2) * 0
        tgt = fw.d[0x105F8C - BASE + 3 * eid: 0x105F8C - BASE + 3 * eid + 3]
        print("  id 0x%02X enable %s |= 0x%08x | priority byte @%08x lane%d = 0x50 | "
              "target byte @%08x lane%d = 0x%02x (table %s)"
              % (eid, hex(en_ca), bit, pr_ca, eid & 3, tg_ca, eid & 3,
                 tgt[2] if len(tgt) == 3 else 0, tgt.hex() if len(tgt) == 3 else "?"))
    print("  derived: expected enable words 0x40161100/04/08 = 0x2000 / 0xC0006000 / 0x5000")
    print("           (0x1D bit13; 0x2D/0x2E bits13/14 of word1; 0x4C bit12 + 0x4E bit14 of word2)")
    print("           dispatcher status CA 0x4016010C, EOI CA 0x40160110, global enable CA 0x40160100")
    return 0


def cmd_patch(fw):
    ok = True
    for name in sorted(PATCHES):
        p = PATCHES[name]
        print("%s: %s" % (name, p["about"]))
        for seg in p["segments"]:
            got = sha16(fw, seg["file"] + BASE, seg["len"])
            good = got == seg["sha16"]
            ok = ok and good
            orig = fw.win(seg["file"] + BASE, seg["len"])
            patched = bytes.fromhex(seg["patched"])
            print("  file 0x%06X cpu 0x%08X len %d  window sha16 %s %s"
                  % (seg["file"], seg["file"] + BASE, seg["len"], got,
                     "OK" if good else "MISMATCH(expect %s)" % seg["sha16"]))
            print("    original: %s" % orig.hex(" ").upper())
            print("    patched : %s   (%s)" % (patched.hex(" ").upper(), seg["expect"]))
            md = fw.arm if seg["isa"] == "arm" else fw.thumb
            dec = list(md.disasm(patched, seg["file"] + BASE))
            print("    decode  : %s" % (" ; ".join("%s %s" % (i.mnemonic, i.op_str) for i in dec)))
    print("patch table check:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def cmd_selftest(fw, rom):
    checks = []

    def ck(name, cond, detail=""):
        checks.append((name, bool(cond), detail))

    ck("fw sha256", hashlib.sha256(fw.d).hexdigest() == FW_SHA256)
    ck("fw length", len(fw.d) == 928920)
    ck("header entry A", fw.u32(0x40000) == 0x00046971)
    ck("header entry B", fw.u32(0x40004) == 0x000C742D)
    ck("IRQ trampoline ARM start", fw.win(0x40064, 4) == bytes.fromhex("04e04ee2"))
    ck("cpsie i @0xC2B80", fw.win(0xC2B80, 2) == bytes.fromhex("62b6"))
    ck("initial CPSR 0xB3 site", fw.win(0xC72F6, 2) == bytes.fromhex("b321"))
    ck("arch switch cpsid site", fw.win(0xC24F8, 2) == bytes.fromhex("72b6"))
    ck("timer reload literal", fw.u32(0xC7108) == 0x0061A800)
    ck("timer block literal", fw.u32(0xC7100) == 0x4016060C)
    ck("dispatcher status literal", fw.u32(0xC3010) == 0x4016010C)
    ck("dispatcher EOI literal", fw.u32(0xC3020) == 0x40160110)
    ck("handler table base", fw.u32(0xC3014) == 0x0017D398)
    ck("out[0] literal in pcie_msg_init", fw.u32(0x497E0) == 0x40039010)
    ck("H2D dispatcher wrapper", fw.u32(0x4029C) == 0x0010C190)
    tgt = fw.d[0x105F8C - BASE + 3 * 0x4C + 2]
    ck("event 0x4C target byte = 0x01", tgt == 0x01)
    ck("event 0x4C table level hint", fw.d[0x105F8C - BASE + 3 * 0x4C + 1] == 0x05)
    # patch source windows
    for name, p in PATCHES.items():
        for i, seg in enumerate(p["segments"]):
            ck("%s seg%d source window" % (name, i),
               sha16(fw, seg["file"] + BASE, seg["len"]) == seg["sha16"])
    if rom is not None:
        ck("ROM sha256", hashlib.sha256(rom.d).hexdigest() == ROM_SHA256)
        ck("ROM IRQ vector -> 0x40064", rom.u32(0x34) == 0x00040064)
        ck("ROM FIQ vector -> 0x7C", rom.u32(0x38) == 0x7C)
    passed = sum(1 for _, ok, _ in checks if ok)
    for name, ok, detail in checks:
        if not ok:
            print("FAIL:", name, detail)
    print("selftest: %d/%d PASS" % (passed, len(checks)))
    return 0 if passed == len(checks) else 1


def main():
    ap = argparse.ArgumentParser(description="host-only IRQ/vector/mailbox map (lane wifi-irq-unmask)")
    ap.add_argument("--fw", default=os.environ.get("HI5622_FW", DEFAULT_FW))
    ap.add_argument("--rom", default=os.environ.get("HI5622_ROM", DEFAULT_ROM))
    sub = ap.add_subparsers(dest="cmd", required=True)
    for c in ("rom", "map", "unmask", "mailbox", "patch", "selftest"):
        sub.add_parser(c)
    a = ap.parse_args()
    if not os.path.exists(a.fw):
        print("error: vendor firmware not found:", a.fw)
        return 2
    fw = Blob(a.fw)
    rom = Blob(a.rom, base=0) if os.path.exists(a.rom) else None
    funcs = {"rom": lambda: cmd_rom(fw, rom), "map": lambda: cmd_map(fw),
             "unmask": lambda: cmd_unmask(fw), "mailbox": lambda: cmd_mailbox(fw),
             "patch": lambda: cmd_patch(fw), "selftest": lambda: cmd_selftest(fw, rom)}
    return funcs[a.cmd]()


if __name__ == "__main__":
    raise SystemExit(main())
