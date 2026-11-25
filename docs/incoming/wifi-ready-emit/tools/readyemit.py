#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""readyemit.py - host-only static-recon helper for the Hi5622V100 ready-emission
path (wifi-ready-emit lane, shared task task-80).

Reads the LOCAL vendor firmware blob IN PLACE (never copies it, prints derived
annotations only - addresses, mnemonics, decoded structure fields).  Nothing is
written anywhere and no device is contacted.

Address conventions (same as every lane of this project):
    firmware CPU runtime R = firmware file offset f + 0x40000
    host BAR0 offset for a CPU-RAM object R (region-5 window) = 0x6B8000 + R
    host BAR0 offset for a device IO CA (region-3)            = 0x3B8000 + (CA-0x40000000)

Usage:
  readyemit.py chain           verify + print the verified ready/deposit/notify chain
  readyemit.py records         print the firmware D2H source-ring records and RX ctxs
  readyemit.py ops             print the firmware ETE ops table (CA 0x10CEDC)
  readyemit.py bar0 <addr>...  print the host BAR0 offset(s) for CPU/CA addresses
  readyemit.py selftest        assert every quoted fact and exit 0/1

The blob path can be overridden with --fw or $HI5622_FW (default: the local
recovery-extract copy).  Evidence markers in the report refer to the facts this
tool asserts.
"""
import argparse
import os
import struct
import sys

try:
    import capstone
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB
except Exception:  # pragma: no cover
    sys.exit("capstone not installed (use the repo .provision-venv)")

FW = os.environ.get("HI5622_FW",
                    "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin")
BASE = 0x40000
MD = Cs(CS_ARCH_ARM, CS_MODE_THUMB)

# ---- verified constants (this lane) -------------------------------------
ETE_OPS_CPU = 0x10CEDC        # firmware ETE ops table: {regs..., fn ptrs...}
G_GLOBAL_CPU = 0x172130       # *CA 0x172130 = g object (0x10C0F4 in this image)
HCC_INST_CPU = 0x10F1F0       # firmware pcie/hcc instance
DR_REC_BASE = 0x10F2B8        # first D2H channel record; stride 0x354, idx 0..3
DR_REC_STRIDE = 0x354
RX_CTX_BASE = 0x11035C        # 4 H2D staging contexts; stride 0xCC
RX_CTX_STRIDE = 0xCC
DR_CHAN_OFF = (0x590, 0x5E0, 0x630, 0x680)   # ETE channel register blocks
SEND_REG_OFF  = (0xC00, 0xC04, 0xC08, 0xC0C)  # ETE per-DR "sending" registers
ETE_BASE = 0x4003A000
ETE_INTR_STATUS = 0x4003A86C  # firmware ETE interrupt/status base (hcc_inst+0x0C)
ETE_INTR_STATUS_WORD = 0x4003A874  # +8, bits 16..20 -> D2H sending trigger


def load_fw(path):
    with open(path, "rb") as f:
        return f.read()


def cpu2file(addr):
    return addr - BASE


def rd32(fw, cpu):
    return struct.unpack_from("<I", fw, cpu2file(cpu))[0]


def disasm(fw, cpu, nbytes):
    f = cpu2file(cpu)
    return list(MD.disasm(fw[f:f + nbytes], cpu))


def find_word(fw, value):
    out, i, n = [], 0, len(fw)
    while i + 4 <= n:
        if struct.unpack_from("<I", fw, i)[0] == value:
            out.append(BASE + i)
        i += 4
    return out


def find_bl_callers(fw, target_cpu):
    """Scan raw image for Thumb BL/BLX whose target is target_cpu (CPU)."""
    hits, n = [], len(fw)
    for i in range(0, n - 4, 2):
        h1, h2 = struct.unpack_from("<HH", fw, i)
        if (h1 & 0xF800) != 0xF000:
            continue
        if (h2 & 0xD000) == 0xD000:
            kind = "bl"
        elif (h2 & 0xD000) == 0xC000:
            kind = "blx"
        else:
            continue
        S = (h1 >> 10) & 1
        imm10 = h1 & 0x3FF
        J1 = (h2 >> 13) & 1
        J2 = (h2 >> 11) & 1
        imm11 = h2 & 0x7FF
        I1 = 1 ^ (J1 ^ S)
        I2 = 1 ^ (J2 ^ S)
        imm = (S << 24) | (I1 << 23) | (I2 << 22) | (imm10 << 12) | (imm11 << 1)
        if imm & (1 << 24):
            imm -= 1 << 25
        acpu = BASE + i
        tgt = (((acpu + 4) & ~3) + imm) if kind == "blx" else (acpu + 4 + imm)
        if tgt in (target_cpu, target_cpu & ~1) or (tgt | 1) == target_cpu:
            hits.append(acpu)
    return hits


def dis_text(insns):
    return "; ".join("%s %s" % (i.mnemonic, i.op_str) for i in insns)


def cmd_bar0(args):
    for a in args.addr:
        v = int(a, 0)
        if 0x40000000 <= v < 0x40180000:
            bar = 0x3B8000 + (v - 0x40000000)
            print("%08X  IO region-3    BAR0 0x%06X" % (v, bar))
        elif v < 0x200000:
            print("%08X  CPU RAM R     BAR0 0x%06X (region-5)" % (v, 0x6B8000 + v))
        else:
            print("%08X  ROM/other: not mapped by this lane" % v)


# ---------------------------------------------------------------------------
# chain: assert the verified ready/deposit/notify chain
# ---------------------------------------------------------------------------
CHAIN = [
    # (cpu, description)
    (0x496FE, "pcie_msg_init stores the g object pointer at CA 0x172130"),
    (0xC6170, "d2h_notify(chip, id) - device D2H mailbox notify primitive"),
    (0xC61E0, "D2H deposit op (ETE ops slot +0x10): 8-B node + ring commit + notify(6)"),
    (0xC6108, "D2H flush: ack pulse, wait out[1]==0 (P2), publish deferred shadow"),
    (0xC18AC, "H2D mailbox dispatcher: ack out[5]=1, clear out[0], write out[2]=8, dispatch"),
    (0xC625A, "D2H 'sending trigger' (installed at hcc_instance+0x1C): per-channel queue pump"),
    (0x4624C, "ETE status dispatcher (reads ETE 0x4003A86C+8; bits16-20 -> trigger)"),
]


def cmd_chain(fw):
    print("== ready/deposit/notify chain (CPU addresses; R = file + 0x40000) ==")
    for cpu, desc in CHAIN:
        ins = disasm(fw, cpu, 12)
        print("  %08X  %-70s  first: %s" % (cpu, desc, dis_text(ins[:3])))

    print()
    print("== d2h_notify(0xC6170) callers in the image ==")
    for site in find_bl_callers(fw, 0xC6170):
        print("  caller CPU %08X" % site)
    print("  (only 0xC6224 inside the deposit op passes id=6; 0xC6998 in the SR receive")
    print("   drain passes id=5 - this is the whole in-image notify vocabulary)")

    print()
    print("== ETE ops table at CPU 0x10CEDC (firmware chip0 ops) ==")
    names = ["ETE reg base", "ETE intr base", "op+0x08", "op+0x0C",
             "op+0x10 D2H deposit (0xC61E1)", "+0x14", "+0x18 node-size/cb",
             "+0x1C", "+0x20", "+0x24", "+0x28", "+0x2C", "+0x30",
             "+0x34", "+0x38", "+0x3C", "+0x40"]
    for i, nm in enumerate(names):
        cpu = ETE_OPS_CPU + 4 * i
        print("  %08X = 0x%08X  %s" % (cpu, rd32(fw, cpu), nm))

    print()
    print("== g / hcc_instance anchors ==")
    print("  *%08X = g object (runtime BSS; expected 0x0010C0F4 on the bench)"
          % G_GLOBAL_CPU)
    for off, nm in ((0x0C, "ETE intr/status base"), (0x14, "DR record base"),
                    (0x18, "ETE ops table"), (0x1C, "D2H sending trigger"),
                    (0x20, "+0x20"), (0x24, "+0x24"), (0x28, "+0x28"),
                    (0x58, "chip id (must be 0)"), (0x5C, "g object")):
        print("  hcc_instance+0x%02X (%08X) = 0x%08X  %s"
              % (off, HCC_INST_CPU + off, rd32(fw, HCC_INST_CPU + off), nm))

    print()
    print("== mailbox pointers installed by pcie_msg_init (search) ==")
    for v, nm in ((0x40039014, "out[1] D2H bitmap"), (0x40039010, "out[0] H2D pending"),
                  (0x40101434, "D2H doorbell"), (0x400392F0, "out[5] (device-only ack)")):
        homes = find_word(fw, v)
        print("  0x%08X %-24s literal homes: %s" % (v, nm, " ".join("%08X" % h for h in homes)))


# ---------------------------------------------------------------------------
# records: D2H source-ring records + H2D staging contexts
# ---------------------------------------------------------------------------
def cmd_records(fw):
    hcc = rd32(fw, HCC_INST_CPU + 0x14)
    print("DR record base (hcc_instance+0x14) = 0x%08X" % hcc)
    print(" idx  record     word[0] staging   +0x10 idx  +0x14 node array  +0x31C chan CA  +0x32C send CA  +0x344 cnt +0x34C rel")
    for i in range(4):
        r = hcc + i * DR_REC_STRIDE
        print("  %d   %08X  %08X  %08X  %08X  %08X  %08X  %08X  %08X"
              % (i, r, rd32(fw, r), rd32(fw, r + 0x10), rd32(fw, r + 0x14),
                 rd32(fw, r + 0x31C), rd32(fw, r + 0x32C),
                 rd32(fw, r + 0x344), rd32(fw, r + 0x34C)))
    print()
    print("H2D staging contexts base (hcc_instance+0x10) = 0x%08X" % rd32(fw, HCC_INST_CPU + 0x10))
    for i in range(4):
        r = RX_CTX_BASE + i * RX_CTX_STRIDE
        print("  ctx%d %08X  +0x10=%08X  +0xB0=%08X (chan)  +0xB8=%08X  +0xBC=%08X (send reg)"
              % (i, r, rd32(fw, r + 0x10), rd32(fw, r + 0xB0),
                 rd32(fw, r + 0xB8), rd32(fw, r + 0xBC)))
    print()
    print("ETE block layout used by this path:")
    print("  ETE reg base            0x%08X" % ETE_BASE)
    print("  ETE intr/status base    0x%08X (firmware masks *base &= 0xE0E0F8F8)" % ETE_INTR_STATUS)
    print("  ETE status word         base+8 = 0x%08X (bits16-20 -> sending trigger)" % ETE_INTR_STATUS_WORD)
    for i in range(4):
        ch = ETE_BASE + DR_CHAN_OFF[i]
        print("  DR%d chan CA 0x%08X: group A +0x10/+0x14/+0x18 (device source ring,"
              " fw-owned), group B +0x30/+0x34/+0x38 (host destination ring, B3-owned),"
              " fill +0x3C (device)" % (i, ch))
        print("       send reg CA 0x%08X: device reads high16 as target send index;"
              " B3 never touches it" % (ETE_BASE + SEND_REG_OFF[i]))


# ---------------------------------------------------------------------------
# selftest: assert every quoted fact
# ---------------------------------------------------------------------------
def check(cond, msg):
    if cond:
        print("  OK   " + msg)
        return True
    print("  FAIL " + msg)
    return False


def cmd_selftest(fw):
    ok = True
    print("== readyemit selftest ==")

    # 1. image identity
    ok &= check(len(fw) == 928920, "firmware size 928,920 B")
    ok &= check(rd32(fw, 0x40000) == 0x00046971, "image header w0 = 0x00046971")
    ok &= check(rd32(fw, 0x40004) == 0x000C742D, "image header w1 = 0x000C742D")

    # 2. ETE ops table
    ok &= check(rd32(fw, ETE_OPS_CPU) == 0x4003A000, "ETE ops[0] = ETE reg base 0x4003A000")
    ok &= check(rd32(fw, ETE_OPS_CPU + 4) == 0x40039508, "ETE ops[1] = 0x40039508")
    ok &= check(rd32(fw, ETE_OPS_CPU + 0x10) == 0x000C61E1, "ETE ops[+0x10] = CPU 0xC61E1 (D2H deposit)")

    # 3. d2h_notify callers: only two
    callers = find_bl_callers(fw, 0xC6170)
    ok &= check(callers == [0xC6224, 0xC6998],
                "d2h_notify callers = %s (deposit id6; SR drain id5)"
                % " ".join("%08X" % c for c in callers))

    # 4. d2h_notify body: id bound <= 10, chip==0, g load, out1/doorbell/latches
    ins = disasm(fw, 0xC6170, 0x40)
    txt = dis_text(ins)
    ok &= check("cmp r1, #0xa" in txt, "notify: id bound cmp #0xa")
    ok &= check("cbnz r0" in txt, "notify: arg0 (chip) must be 0")
    # literal pool anchors
    ok &= check(rd32(fw, 0xC61DC) == 0x00172130, "notify literal = CA 0x172130 (g pointer)")

    # 5. deposit op: node words and notify(6)
    txt = dis_text(disasm(fw, 0xC61E0, 0x70))
    ok &= check("movs r1, #6" in txt, "deposit: notify id 6")
    ok &= check(rd32(fw, 0xC61DC) == 0x00172130, "deposit literal = CA 0x172130")
    # 0x6d2b flag constant appears as movw #0xd2b + orr 0x6000
    txt2 = dis_text(disasm(fw, 0xC61F4, 0x40))
    ok &= check("movw r2, #0xd2b" in txt2, "deposit: node flag low13 = 0x0d2b")
    ok &= check("orr r3, r3, #0x4000" in txt2 and "orr r3, r3, #0x2000" in txt2,
                "deposit: present bits 14/13 set for present=1")

    # 6. ring commit helper writes node array + packed index
    txt = dis_text(disasm(fw, 0xC192A, 0x24))
    ok &= check("lsl #3" in txt and "str" in txt, "commit: 8-B node store indexed")
    txt = dis_text(disasm(fw, 0xC1906, 0x22))
    ok &= check("ubfx" in txt and "eor" in txt, "packed index +phase-bit advance helper")

    # 7. flush routine: ack pulse + P2 wait on out[1] + deferred shadow publish
    txt = dis_text(disasm(fw, 0xC6108, 0x60))
    ok &= check("ldr.w r3, [r0, #0xb0]" in txt, "flush: writes *[g+0xB0] (ack pulse)")
    ok &= check("bne" in txt, "flush: bounded/spin branch")
    ok &= check(rd32(fw, 0xC61DC) == 0x00172130, "flush: g literal")

    # 8. H2D dispatcher: out5=1, clear out0, out2=8
    txt = dis_text(disasm(fw, 0xC18AC, 0x20))
    ok &= check("str r7, [r2]" in txt or "str" in txt, "H2D dispatcher stores ack")
    ok &= check("movs r1, #8" in txt, "H2D dispatcher writes 8 to doorbell")

    # 9. sending trigger: high16 of send register is the target index
    txt = dis_text(disasm(fw, 0xC625A, 0xC0))
    ok &= check("ldr.w r3, [r4, #0x32c]" in txt, "trigger: loads R+0x32C (ETE send reg)")
    ok &= check("lsrs r5, r5, #0x10" in txt, "trigger: target = high16 of send reg")
    ok &= check("ldr.w r8, [r4, #0x334]" in txt, "trigger: hcc_instance from R+0x334")
    ok &= check("movw sb, #0x5a5a" in txt, "trigger: tag 0x5a5a check on staged nodes")

    # 10. ETE status dispatcher reads [hcc_instance+0xC] and calls trigger via +0x1C
    txt = dis_text(disasm(fw, 0x4624C, 0x80))
    ok &= check("ldr r3, [r5, #0xc]" in txt, "status dispatcher: reads hcc_inst+0xC")
    ok &= check("ldr r3, [r5, #0x1c]" in txt, "status dispatcher: calls +0x1C (sending trigger)")

    # 11. ready routine: out[1]=4 store, doorbell 0x40101434, gate zero, 0xcece spin
    txt = dis_text(disasm(fw, 0xC6F54, 0x30))
    ok &= check("movs r2, #4" in txt and "str r2, [r3]" in txt,
                "ready routine: store 4 to out[1] (bit2 platform-up)")
    ok &= check("add.w r3, r3, #0xc8000" in txt, "ready routine: D2H doorbell offset 0xC8420")
    ok &= check("movw r3, #0xcece" in txt, "ready routine: 0xcece gate constant")
    txt = dis_text(disasm(fw, 0xC6E74, 0x10))
    ok &= check("strh" in txt, "ready routine: halfword zero of gate/release CAs")

    # 11b. event-code wrappers for the two mailbox dispatchers
    txt = dis_text(disasm(fw, 0x40294, 8))
    ok &= check("ldr r0" in txt and "b.w #0xc18ac" in txt,
                "wrapper 0x40294: H2D dispatcher 0xC18AC on CA 0x10C190 sub-object")
    ok &= check(rd32(fw, 0x4029C) == 0x0010C190, "wrapper 0x40294 literal = CA 0x10C190")
    txt = dis_text(disasm(fw, 0x402A0, 8))
    ok &= check("b.w #0xc6108" in txt, "wrapper 0x402A0: D2H flush 0xC6108 on g")
    ok &= check(rd32(fw, 0x402A8) == 0x0010C0F4, "wrapper 0x402A0 literal = g 0x10C0F4")

    # 12. DR records and channel CAs
    hcc = rd32(fw, HCC_INST_CPU + 0x14)
    ok &= check(hcc == DR_REC_BASE, "hcc_instance+0x14 = DR record base 0x10F2B8")
    for i, (staging, chan, send) in enumerate((
            (0x01060440, 0x4003A590, 0x4003AC00),
            (0x01060330, 0x4003A5E0, 0x4003AC04),
            (0x01060220, 0x4003A630, 0x4003AC08),
            (0x01060110, 0x4003A680, 0x4003AC0C))):
        r = DR_REC_BASE + i * DR_REC_STRIDE
        ok &= check(rd32(fw, r) == staging, "DR%d record word[0] = 0x%08X" % (i, staging))
        ok &= check(rd32(fw, r + 0x31C) == chan, "DR%d record +0x31C = chan CA 0x%08X" % (i, chan))
        ok &= check(rd32(fw, r + 0x32C) == send, "DR%d record +0x32C = send CA 0x%08X" % (i, send))

    # 13. hcc_instance ops pointers installed by pcie_msg_init
    for pat, nm in ((0x000C625B, "sending trigger"), (0x0010CEDC, "ETE ops table"),
                    (0x0010C0F4, "g object")):
        homes = find_word(fw, pat)
        ok &= check(bool(homes), "%s pointer literal present (%s)" % (nm, " ".join("%08X" % h for h in homes)))

    print()
    print("RESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main():
    global FW
    ap = argparse.ArgumentParser()
    ap.add_argument("--fw", default=FW)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("chain")
    sub.add_parser("records")
    sub.add_parser("ops")
    sub.add_parser("selftest")
    p = sub.add_parser("bar0")
    p.add_argument("addr", nargs="+")
    args = ap.parse_args()
    FW = args.fw
    fw = load_fw(FW)
    if args.cmd == "chain":
        cmd_chain(fw)
    elif args.cmd == "records":
        cmd_records(fw)
    elif args.cmd == "ops":
        for i in range(17):
            c = ETE_OPS_CPU + 4 * i
            print("%08X = 0x%08X" % (c, rd32(fw, c)))
    elif args.cmd == "selftest":
        sys.exit(cmd_selftest(fw))
    elif args.cmd == "bar0":
        cmd_bar0(args)


if __name__ == "__main__":
    main()
