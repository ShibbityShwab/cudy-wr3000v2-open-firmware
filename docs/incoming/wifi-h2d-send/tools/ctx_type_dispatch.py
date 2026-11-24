#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""ctx_type_dispatch.py - host-only static analysis of the context-create
helper's data[0] type dispatch (CA 0xFE6BC) and its caller (worker CA 0x449E4).

Lane wifi-h2d-send, task-102 follow-up (2026-11-24).  The bench fired the
synthesized g0 id1 key-0x64 create with data[0]=3: the firmware wrote the
module id into ctx[0] but left ctx+0x190 zero, so the key-0xd8 TXQ worker still
faulted.  This tool re-derives, from the pinned local vendor firmware blob,
what each type selects and which branches write/free ctx+0x190.  It reads the
blob in place, copies no vendor byte into the repository, and never touches a
device.

Usage:
  ctx_type_dispatch.py --types      # the data[0] truth table
  ctx_type_dispatch.py --dispatch   # annotated dispatch/alloc/type-1 listings
  ctx_type_dispatch.py --selftest   # assertions (byte + branch targets)
"""
import argparse
import hashlib
import os
import struct
import sys

try:
    import capstone
except ImportError:  # pragma: no cover
    capstone = None

BLOB = "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin"
FW_SHA256 = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_LEN = 928920
CPU_OFF = 0x40000        # image convention: CPU = file offset + 0x40000

# Objects (CPU addresses)
HANDLER = 0x43D58
TABLE_A = 0x10C23C
WORKER = 0x449E4
WORKER_REFCOUNT_BUMP = 0x44A28
WORKER_REFCOUNT_DROP = 0x44AE6
WORKER_CLEAR_190 = 0x44ADE
HELPER = 0xFE6BC
HELPER_DISPATCH = 0xFE7B0
HELPER_ALLOC = 0xFE7D4
HELPER_ALLOC_STORE = 0xFE7E4
HELPER_ALLOC_FAIL = 0xFE800
HELPER_DEFAULTS = 0xFE804
HELPER_FILL = 0xFE8BC
HELPER_TYPE1_TAIL = 0xFF832
HELPER_RET_3E8 = 0xFE8A6
HELPER_NOALLOC_TAIL = 0xFF940
ALLOC_FN = 0xBF128
ALLOC_TABLE_LITERAL = 0xBF240
POOL_BASE = 0x172134
POOL_STRIDE = 0x188
POOL_INIT_CODE = 0x47696
POOL_INIT_CLEAR = 0x402F4


class Blob:
    def __init__(self, path):
        self.d = open(path, "rb").read()
        self.sha = hashlib.sha256(self.d).hexdigest()
        self.md = None
        if capstone is not None:
            self.md = capstone.Cs(capstone.CS_ARCH_ARM,
                                  capstone.CS_MODE_THUMB)
            self.md.detail = True

    def ok(self):
        return self.sha == FW_SHA256 and len(self.d) == FW_LEN

    def u8(self, cpu):
        return self.d[cpu - CPU_OFF]

    def u16(self, cpu):
        return struct.unpack("<H", self.d[cpu - CPU_OFF:cpu - CPU_OFF + 2])[0]

    def u32(self, cpu):
        return struct.unpack("<I", self.d[cpu - CPU_OFF:cpu - CPU_OFF + 4])[0]

    def dis(self, cpu, n):
        return list(self.md.disasm(self.d[cpu - CPU_OFF:cpu - CPU_OFF + n],
                                   cpu))


def target(ins):
    """Direct branch/call target as an int, or None."""
    for op in ins.operands:
        if op.type == capstone.arm.ARM_OP_IMM:
            return op.imm
    return None


def factory(t):
    """Replay the helper's type dispatch for data[0] == t.

    Returns (target, first_alloc, note) where target follows the encoded
    branch at CA 0xFE7B0..0xFE7BE and first_alloc says whether the shared
    0x3a8 allocation call at CA 0xFE7D4 is reached (types 1/2/3 only).
    """
    if t > 6:
        return (0xFE892, False, "type > 6: log + return 0x3e8, no buffer")
    m = 1 << t
    if m & 0x58:                      # types 3, 4, 6
        if 1 <= t <= 3:
            return (0xFE7CA, True, "straight to the shared 0x3a8 allocation")
        return (0xFE7CA, False,
                "alloc dispatch rejects (type-1 > 2) -> 0xFF940")
    if m & 6:                         # types 1, 2
        return (0xFE804, True,
                "defaults block 0xFE804 then the shared allocation")
    if t == 0:
        return (0xFE7C4, False, "ctx+0x187=0x2B, alloc rejects -> 0xFF940")
    return (0xFE892, False, "type 5: log + return 0x3e8, no buffer")


def cmd_types(b):
    print("context-create helper CA 0x%06x: data[0] (-> ctx[4]) type dispatch"
          % HELPER)
    print("  bits: (1<<type) & 0x58 -> CA 0xFE7CA ; & 0x06 -> CA 0xFE804")
    print()
    print("  type  branch          first alloc   effect")
    for t in range(0, 8):
        tgt, alloc, note = factory(t)
        print("  %4d  CA 0x%06x   %-12s  %s"
              % (t, tgt, "yes" if alloc else "no", note))
    print()
    print("  shared allocation: CA 0x%06x  = 0xBF128(0x96, 0x327, 1, 0x3a8)"
          % ALLOC_FN)
    print("                      -> str r0,[ctx+0x190] at CA 0x%06x"
          % HELPER_ALLOC_STORE)
    print("  alloc failure:      CA 0x%06x  returns 0x67 (worker clears ctx+0x190)"
          % HELPER_ALLOC_FAIL)
    print("  shared fill:        CA 0x%06x  buffer +0x10=0x64, +0x14=3,"
          % HELPER_FILL)
    print("                      +0x238/+0x23c=0x92A, +0x52=1 (types 1/2/3)")
    print("  type-1 extra:       CA 0x%06x  data[5]==0 gate -> second alloc"
          % HELPER_TYPE1_TAIL)
    print("                      (0x96,0x859,0,0xc) -> ctx+0x434")
    print()


def cmd_dispatch(b):
    for tag, a, n in (
            ("helper type dispatch 0xFE758..0xFE808", 0xFE758, 0xB0),
            ("shared allocation + failure 0xFE7D4..0xFE80C", 0xFE7D4, 0x38),
            ("type-1 tail 0xFF820..0xFF8E0", 0xFF820, 0xC0),
            ("no-alloc tail 0xFF940..0xFF94C", 0xFF940, 0xC),
            ("worker: refcount bump + helper call 0x44A20..0x44AF2",
             0x44A20, 0xD2),
            ("worker: error path clears ctx+0x190 0x44AB0..0x44AEA",
             0x44AB0, 0x3A)):
        print("== %s ==" % tag)
        for ins in b.dis(a, n):
            extra = ""
            if ins.mnemonic.startswith(("b", "bl")) and \
                    not ins.mnemonic.startswith(("bic", "bfi", "bfc")):
                tg = target(ins)
                if tg is not None and (tg in (HELPER_ALLOC, HELPER_ALLOC_STORE,
                                              HELPER_ALLOC_FAIL,
                                              HELPER_DEFAULTS, HELPER_FILL,
                                              HELPER_NOALLOC_TAIL,
                                              HELPER_RET_3E8, ALLOC_FN,
                                              WORKER_REFCOUNT_BUMP,
                                              WORKER_REFCOUNT_DROP,
                                              WORKER_CLEAR_190,
                                              HELPER, 0xFE7CA, 0xFE892,
                                              0xFF86E, 0xFF900, 0xFF940,
                                              0xFE8A6)):
                    extra = "   ; -> CA 0x%06x" % tg
            print("  %06x  %-7s %-42s%s"
                  % (ins.address, ins.mnemonic, ins.op_str, extra))
        print()


def cmd_selftest(b):
    checks = []

    def ck(name, cond, detail=""):
        checks.append((name, bool(cond), detail))

    ck("pinned blob sha256", b.ok(), b.sha)
    ck("handler table A entry 1 = key 0x0064 -> worker 0x449E4",
       b.u16(TABLE_A + 8) == 0x0064 and b.u32(TABLE_A + 12) == 0x449E5)

    # dispatch encodings are a 4-instruction test + two conditional jumps
    ins = b.dis(HELPER_DISPATCH, 0x10)
    ck("dispatch: movs r3,#1 ; lsls r3,r2 ; tst #0x58 ; bne 0xFE7CA",
       [i.mnemonic for i in ins[:4]] == ["movs", "lsls", "tst.w", "bne"] and
       target(ins[3]) == 0xFE7CA, [i.op_str for i in ins[:4]])
    ins2 = b.dis(0xFE7BA, 8)
    ck("dispatch: tst #6 ; bne 0xFE804",
       target(ins2[1]) == HELPER_DEFAULTS)
    ins3 = b.dis(0xFE7C0, 6)
    ck("dispatch: cmp r2,#0 ; bne 0xFE892",
       target(ins3[1]) == 0xFE892)

    def seq(cpu, n, want):
        got = [(i.mnemonic, i.op_str) for i in b.dis(cpu, n)]
        return got[:len(want)] == want

    # shared allocation and its store
    ck("allocation call is 0xBF128(0x96,0x327,1,0x3A8)",
       seq(0xFE7D4, 0x30, [("mov.w", "r3, #0x3a8"),
                           ("movs", "r2, #1"),
                           ("movw", "r1, #0x327"),
                           ("movs", "r0, #0x96"),
                           ("bl", "#0xbf128")]))
    ck("ctx+0x190 store at CA 0xFE7E4",
       seq(HELPER_ALLOC_STORE, 8, [("str.w", "r0, [r4, #0x190]"),
                                   ("cmp", "r0, #0")]))
    ck("alloc failure returns 0x67 via the helper epilogue",
       seq(HELPER_ALLOC_FAIL, 6, [("movs", "r0, #0x67"),
                                  ("b", "#0xfe6d6")]))

    # type truth table: only the shared alloc call is the allocator
    alloc_types = [t for t in range(8) if factory(t)[1]]
    ck("types 1/2/3 all reach the shared allocation (type is NOT the switch)",
       alloc_types == [1, 2, 3], alloc_types)
    ck("type 3 (bench default) takes the shared allocation",
       factory(3) == (0xFE7CA, True,
                      "straight to the shared 0x3a8 allocation"))
    ck("types 0/4/5/6 do NOT allocate (no-alloc / error tails)",
       not factory(0)[1] and not factory(4)[1] and
       not factory(5)[1] and not factory(6)[1])

    # defaults block for types 1/2 and its jump back to the alloc dispatch
    ck("defaults block writes ctx+0x3E2/0xF8 = 0xFFF (types 1/2)",
       seq(HELPER_DEFAULTS, 0x14,
           [("movw", "r3, #0xfff"),
            ("strh.w", "r3, [r4, #0x3e2]"),
            ("strh.w", "r3, [r4, #0xf8]"),
            ("movs", "r3, #0x1e"),
            ("strb.w", "r3, [r4, #0xd1]")]))
    ck("defaults block ends with b 0xFE7CA",
       target(b.dis(0xFE890, 4)[0]) == 0xFE7CA)

    # type-1-only second allocation (data[5] gate, ctx+0x434)
    ck("type-1 tail gates on data[5]==0 and type==1",
       seq(HELPER_TYPE1_TAIL, 0x20,
           [("ldrb", "r3, [r6, #5]"), ("cbnz", "r3, #0xff86e"),
            ("ldrb", "r3, [r4, #4]"), ("cmp", "r3, #1"),
            ("bne", "#0xff86e")]))
    ck("type-1 second allocation at CA 0xFF84C -> ctx+0x434",
       target(b.dis(0xFF84C, 4)[0]) == 0xBF128 and
       seq(0xFF850, 4, [("str.w", "r0, [r4, #0x434]")]))

    # type-1 post-fill can return 0x3e8 after a successful allocation
    ck("type-1 only: post-fill 0xD4048 failure -> 0x3e8 (worker frees buffer)",
       target(b.dis(0xFF8D2, 4)[0]) == HELPER_RET_3E8)
    ck("types 2/3 return via 0xFF940 and KEEP ctx+0x190",
       target(b.dis(0xFF8BE, 4)[0]) == HELPER_NOALLOC_TAIL)

    # no-alloc tail marker and return value
    ck("no-alloc tail sets ctx+0x187=0x2B and returns 0",
       seq(HELPER_NOALLOC_TAIL, 0xC,
           [("movs", "r3, #0x2b"), ("movs", "r0, #0"),
            ("strb.w", "r3, [r4, #0x187]"), ("b.w", "#0xfe6d6")]))

    # worker: refcount pair (net zero) and ctx+0x190 clear on helper error
    ck("worker bumps refcount via 0xBC726 then drops via 0x943CC (net 0)",
       target(b.dis(WORKER_REFCOUNT_BUMP, 4)[0]) == 0xBC726 and
       target(b.dis(WORKER_REFCOUNT_DROP, 4)[0]) == 0x943CC)
    ck("worker clears ctx+0x190 when the helper returns nonzero",
       seq(WORKER_CLEAR_190, 8,
           [("str.w", "r3, [r4, #0x190]"), ("ldr", "r1, [sp, #8]")]))

    # shared fill signatures (same for all allocating types)
    ck("shared fill: buffer +0x10=0x64, +0x14=3, +0x238/+0x23c=0x92A, +0x52=1",
       seq(0xFE8CE, 0x34,
           [("movs", "r0, #0x64"),
            ("add.w", "r3, r5, r3, lsl #5"),
            ("ldrb", "r1, [r3, #1]"),
            ("ldr.w", "r3, [r4, #0x190]"),
            ("str", "r2, [r3, #0x14]"),
            ("movw", "r3, #0x92a"),
            ("ldr.w", "r2, [r4, #0x190]"),
            ("str.w", "r3, [r2, #0x238]")]) and
       target(b.dis(HELPER_FILL + 0x38, 4)[0]) is not None)

    # memory-pool table: the allocator's base literal and its first check
    ck("allocator pool table base literal = 0x00172134 (BSS pool manager)",
       b.u32(ALLOC_TABLE_LITERAL) == POOL_BASE)
    ck("allocator reads u16 pool[base+0x188*pool] and fails if < size+8",
       seq(0xBF150, 8, [("ldrh", "r3, [r1, r2]"),
                        ("adds", "r6, r1, r2"),
                        ("cmp", "r3, r4")]) and
       target(b.dis(0xBF156, 4)[0]) == 0xBF13A)
    ck("pool manager is initialised by boot code (pool descriptor fill)",
       b.u32(0x403B0) == POOL_BASE and
       b.u32(0x47950) == POOL_BASE and
       seq(0x476D4, 0x1A,
           [("movs", "r7, #0"), ("ldrb.w", "r3, [sb, #1]"),
            ("ldr.w", "r2, [sb, #4]"), ("strb", "r3, [r6, #2]"),
            ("str", "r3, [sp, #0x1c]"),
            ("add.w", "r3, r3, #0x40000000"),
            ("strh.w", "r7, [r6, #0x184]"),
            ("strh.w", "r7, [r6, #0x186]")]))

    # expected future bench values are firmware-side constants
    ck("bench pack: ctx+0x190 at CPU 0x11C5E8 (BAR0 0x7D45E8)",
       0x11C458 + 0x190 == 0x11C5E8)
    ck("bench pack: pool-1 descriptor at CPU 0x1722BC (BAR0 0x8212BC)",
       POOL_BASE + POOL_STRIDE == 0x1722BC)

    bad = 0
    for name, cond, detail in checks:
        if not cond:
            bad += 1
            print("FAIL: %s (%s)" % (name, detail))
    print("ctx_type_dispatch selftest: %d/%d PASS"
          % (len(checks) - bad, len(checks)))
    return 0 if bad == 0 else 1


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--types", action="store_true")
    ap.add_argument("--dispatch", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--blob", default=BLOB)
    args = ap.parse_args()
    if not os.path.isfile(args.blob):
        print("blob not found: %s (local evidence only, never committed)"
              % args.blob, file=sys.stderr)
        return 2
    if capstone is None:
        print("capstone not available (use .provision-venv/bin/python)",
              file=sys.stderr)
        return 2
    b = Blob(args.blob)
    if not b.ok():
        print("refusing: blob sha256 %s is not the pinned firmware" % b.sha,
              file=sys.stderr)
        return 2
    if args.selftest:
        return cmd_selftest(b)
    if args.dispatch:
        cmd_dispatch(b)
        return 0
    cmd_types(b)
    return 0


if __name__ == "__main__":
    sys.exit(main())
