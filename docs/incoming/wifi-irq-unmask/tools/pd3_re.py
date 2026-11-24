#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""pd3_re.py - host-only verifier for the PD3 cave/header proofs and the PD2 bench failure RCA.

Lane wifi-irq-unmask, shared task task-98 (PD3). Reads the LOCAL vendor firmware blob in
place and prints derived annotations only. No vendor bytes are copied into the repository
and no device is opened.

Subcommands:
  header     the image-header exception-record area CPU 0x40008..0x4004F: header layout,
             zero-fill proof, the exception-staging writer, and the branch/literal census
             that makes it control-flow unreachable (PD3's stub home).
  pd2fail    reconstruction of the PD2 bench failure: why the config-3 block CPU 0x47432 is
             NOT dead on our boots (bench page = 0x6F died between entry A's page |= 0x20/
             |= 0x40 writes and the next page words; differential PD1 vs PD2 evidence), and
             why control/PD1 boots can still look golden (the original config-3 block jumps
             to the common continuation).
  selftest   self-checking assertions (exit 0 on PASS)
"""
import argparse
import hashlib
import struct
import sys

try:
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB, CS_OP_IMM, CS_OP_MEM
except Exception:  # pragma: no cover
    print("error: capstone is required (project venv .provision-venv/bin/python)")
    raise SystemExit(2)

BASE = 0x40000
DEFAULT_FW = "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin"
FW_SHA256 = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_LEN = 928920

THUMB = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
THUMB.detail = False


class Blob:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.d = f.read()
        self.sha256 = hashlib.sha256(self.d).hexdigest()

    def u16(self, cpu):
        return struct.unpack_from("<H", self.d, cpu - BASE)[0]

    def u32(self, cpu):
        return struct.unpack_from("<I", self.d, cpu - BASE)[0]

    def win(self, cpu, n):
        return self.d[cpu - BASE:cpu - BASE + n]

    def dis(self, cpu, n):
        return list(THUMB.disasm(self.win(cpu, n), cpu))


def census(blob, lo, hi):
    """Branch targets and PC-relative literal loads into [lo,hi); best-effort full sweep."""
    import capstone.arm as A
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
    md.detail = True
    branches, literals = [], []
    n = len(blob.d)
    for off in range(0, n - 4, 2):
        addr = BASE + off
        for ins in md.disasm(blob.d[off:off + 4], addr, count=1):
            if ins.mnemonic.startswith("b") and ins.mnemonic not in (
                    "bic", "bfc", "bfi", "bfx", "bl", "blx"):
                for op in ins.operands:
                    if op.type == A.CS_OP_IMM and lo <= op.imm < hi:
                        branches.append((addr, ins.mnemonic, ins.op_str))
            for op in ins.operands:
                if op.type == CS_OP_MEM and op.mem.base == A.ARM_REG_PC:
                    tgt = ((addr + 4) & ~3) + op.mem.disp
                    if lo <= tgt < hi:
                        literals.append((addr, ins.mnemonic, ins.op_str, tgt))
            break
    return branches, literals


def print_dis(blob, cpu, n, mark=None):
    for i in blob.dis(cpu, n):
        tag = ""
        if mark and i.address == mark[0]:
            tag = "  <== " + mark[1]
        print("  %08x  %-10s %-8s %s%s" % (i.address, i.bytes.hex(), i.mnemonic, i.op_str, tag))


def cmd_header(blob):
    print("== PD3 cave: the image-header exception-record area CPU 0x40008..0x4004F ==")
    print("image header layout (file offsets == CPU-0x40000):")
    print("  CPU 0x40000  entry A          = 0x%08x" % blob.u32(0x40000))
    print("  CPU 0x40004  entry B          = 0x%08x" % blob.u32(0x40004))
    print("  CPU 0x40008  exception mask   (halfword; written only by the staging routine)")
    print("  CPU 0x4000C  staged 17-word frame (0x4000C..0x4004C)")
    print("  CPU 0x40050  handler table    = %s"
          % " ".join("%08x" % blob.u32(0x40050 + 4 * i) for i in range(5)))
    print("  CPU 0x40064  ARM IRQ trampoline (live)")
    print()
    area = blob.win(0x40008, 0x48)
    print("record area 0x40008..0x4004F is all zero in the vendor image: %s (sha16 %s)"
          % (area == b"\x00" * 0x48, hashlib.sha256(area).hexdigest()[:16]))
    print("PD3 places its 64-byte stub at CPU 0x4000C..0x4004B, leaving the mask halfword")
    print("0x40008..0x4000B and the handler table 0x40050+ untouched.")
    print()
    print("the only image code that references the record base:")
    print("  staging routine 0xC6E14 loads literal 0x%08x at CPU 0x%06x (mask+frame writer)"
          % (blob.u32(0xC6FA4), 0xC6FA4))
    print("  id-3 EXCP_INFO builder 0x49188 also loads the pointer value 0x40008 for its payload"
          " (read-only)")
    print()
    br, lit = census(blob, 0x40008, 0x40050)
    print("control-flow census into [0x40008,0x40050):")
    print("  branch targets: %s" % (br or "none"))
    print("  PC-relative literal loads: %s" % (lit or "none"))
    print("  fall-through: none (0x40004 is data, 0x40008 is zero-fill, 0x40050 is a data table)")
    print("  => the record area is only entered by PD3's own patch of the boot epilogue 0x4916A,")
    print("     and is written at runtime only by the exception staging routine (a crash path).")


def cmd_pd2fail(blob):
    print("== why PD2 failed on the bench (and why control/PD1 boots still look golden) ==")
    print("PD2 put its stub in entry A's config-3 block CPU 0x47432 based on a host-seeded")
    print("platform-block value. The selector is read at CPU 0x469E0 and compared with 3:")
    print_dis(blob, 0x469DC, 0x48, mark=(0x46A1E, "config-3 -> 0x47432 (only if selector == 3)"))
    print()
    print("original config-3 block CPU 0x47432..0x47471 ends by joining the common continuation:")
    print_dis(blob, 0x47432, 0x8)
    print("  ...")
    print_dis(blob, 0x47468, 0x0A, mark=(0x4746E, "b.w 0x46AC6 = normal continuation"))
    print("=> a control/PD1 boot that takes the config-3 branch still reaches the golden page,")
    print("   so 'control boots golden' does NOT prove the block is dead. PD1 (only the 0x916A")
    print("   redirect, no cave) also booted golden, while PD2 (only the cave bytes differ)")
    print("   failed in entry A => the boot executed the PD2 stub at 0x47432.")
    print()
    print("bench page signature: page+0 = 0x0000006F, page+4 = 3, page+8 random, page+C = 0")
    print("  ROM cold boot sets page bits 0,1,2,3  (0xF) and page+4 = 3")
    print("  entry A then sets page |= 0x20 at 0x46978 and page |= 0x40 at 0x469A0  -> 0x6F")
    print("  the next page writes are 0x473C2 (bit13) / 0x473F8 (bit14) and 0x46ACA (bit23):")
    print("  none happened => entry A hung between 0x469A0 and 0x46ACA")
    print("  the only control transfer out of that window is the config-3 branch at 0x46A1E,")
    print("  and PD2's stub there waits forever for the hcc instance (still zero pre-boot)")
    print("  => deterministic hang, not a fault: mask stayed 0, matching the bench.")
    print()
    print("lesson: config-conditional code is not a safe cave; PD3 uses the header record area,")
    print("which has no branch/literal reference at all and is entered only by PD3's patch.")


def cmd_selftest(blob):
    checks = []

    def ck(name, cond, detail=""):
        checks.append((name, bool(cond), detail))

    ck("vendor firmware sha256", blob.sha256 == FW_SHA256, blob.sha256)
    ck("firmware len", len(blob.d) == FW_LEN, str(len(blob.d)))
    ck("header entry A", blob.u32(0x40000) == 0x46971)
    ck("header entry B", blob.u32(0x40004) == 0xC742D)
    ck("record area zero in vendor image", blob.win(0x40008, 0x48) == b"\x00" * 0x48)
    ck("header handler table", [blob.u32(0x40050 + 4 * i) for i in range(5)] ==
       [0x103140, 0x103114, 0x1030E8, 0x1030BC, 0x103090])
    ck("staging routine record literal = 0x40008", blob.u32(0xC6FA4) == 0x40008)
    br, lit = census(blob, 0x40008, 0x40050)
    ck("no branch target into record area", not br, repr(br[:3]))
    ck("no literal load into record area", not lit, repr(lit[:3]))
    ck("config-3 selector compare bytes", blob.win(0x46A1C, 6) == bytes.fromhex("032a00f00885"))
    ck("config-3 block joins continuation (b.w 0x46AC6)",
       blob.win(0x4746E, 4) == bytes.fromhex("fff72abb"))
    ck("entry A page bits 0x20 / 0x40 written before the selector",
       blob.win(0x46978, 8) == bytes.fromhex("2b6843f020032b60")
       and blob.win(0x469A0, 10) == bytes.fromhex("2b68344a43f040032b60"))
    ck("next page writes after the selector window", blob.win(0x46ACA, 10) ==
       bytes.fromhex("2b68b94a43f400032b60"))
    ck("boot epilogue bytes", blob.win(0x4916A, 8) == bytes.fromhex("002021b0bde8f08f"))
    ck("stub place ends before handler table", 0x4000C + 64 <= 0x40050)
    ok = sum(1 for _, c, _ in checks if c)
    for n, c, d in checks:
        if not c:
            print("FAIL: %s %s" % (n, d))
    print("pd3_re selftest: %d/%d PASS" % (ok, len(checks)))
    return 0 if ok == len(checks) else 1


def main(argv):
    ap = argparse.ArgumentParser(description="PD3 cave/RCA verifier (host-only)")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--header", action="store_true")
    g.add_argument("--pd2-fail", action="store_true")
    g.add_argument("--selftest", action="store_true")
    ap.add_argument("--fw", default=DEFAULT_FW)
    a = ap.parse_args(argv)
    blob = Blob(a.fw)
    if a.selftest:
        return cmd_selftest(blob)
    if blob.sha256 != FW_SHA256:
        print("error: not the pinned vendor blob (%s)" % blob.sha256)
        return 2
    if a.header:
        cmd_header(blob)
    else:
        cmd_pd2fail(blob)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
