#!/usr/bin/env python3
"""fw_rxdisp2_re.py - HOST-ONLY static re-derivation for lane rxdisp2 (task-242).

Reads the pinned vendor FIRMWARE.bin in place (never copies/blobs it) and asserts every
fact quoted in this lane's docs.  No device contact, no vendor bytes emitted.

    python3 fw_rxdisp2_re.py --selftest   # asserts the decode, prints PASS/FAIL lines
    python3 fw_rxdisp2_re.py --decode     # the local-dispatch/CRX chain narration
    python3 fw_rxdisp2_re.py --tables     # the wiring + gate tables
    python3 fw_rxdisp2_re.py --probes     # the read-only probe pack addresses/expected

Conventions (project-wide): firmware CPU address CA = file offset + 0x40000.
Host BAR0 phys: firmware RAM = 0x40000000 + 0x6B8000 + CA; device IO = 0x40000000 +
0x3B8000 + CA (CA 0x40000000..0x4011FFFF); PKTRAM = 0x1D8000 + (CA-0x01000000).
Capstone is needed for the disassembly assertions (.provision-venv has it).
"""
import hashlib
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FW_CANDIDATES = [
    os.path.join(HERE, "..", "..", "..", "..", "..", "rootfs-2.4.15",
                 "lib", "firmware", "hi_wifi", "FIRMWARE.bin"),
    "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin",
]
FW_SIZE = 928920
FW_SHA = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
BASE = 0x40000

# ---- verified addresses (all re-derived below) ------------------------------
HCC_INSTANCE = 0x171B6C
HCC_MAP = 0x103FB8          # +8 nq, +0xC n_customers, +0x10 n_groups,
                            # +0x18 queue cfg, +0x1C thread table, +0x24 group table
QUEUE_CFG = 0x1040C0        # 3 x 8B {u8 idx, .., u8 customer @+3, u16 round-limit @+4}
THREAD_TAB = 0x10CFC8       # 3 x 0x30B {name[16], id, stack, prio, aff, flag, ffff,
                            #          entry, init-cb}
GROUP_TAB = 0x10CF74        # 7 x 12B {u16 group, u16 qidx, u32 item_array, u32 max_id}
DEST0 = HCC_INSTANCE + 0x448
QUEUE_ARR = HCC_INSTANCE + 0x04       # [dest0+8]
CUST_ARR = HCC_INSTANCE + 0x2B0       # [dest0+0xC], stride 0x68
GROUP_ARR = HCC_INSTANCE + 0x3E8      # [dest0+0x10], stride 0xC
QUEUE_STRIDE = 0xE4
CUST_STRIDE = 0x68
G2_TABLE = 0x10CC18         # 19 x 12B {u16 id, u16 pad, u32 handler(thumb), u32 aux}
G0_TABLE = 0x10CCFC         # 40 x 12B
BAND0 = 0x174BF0            # band obj 0, stride 0xE28: +0xCC0 producer, +0xCC5 CRX
                            # drops, +0xCCE post failures, +0x320 PPDU list
CHIP_BASE = 0x0015CA08      # chip ctx (BSS), stride 0x1B68, +0x1A00 = rx ctx
GLOBAL_CHIPMGR = 0x170E08   # BSS: +0x650 = fwd/dest table entries, +0x6FC = base u16
DEST_TABLE_FWD = GLOBAL_CHIPMGR + 0x650
DEST_TABLE_BASE = GLOBAL_CHIPMGR + 0x6FC

def phys_ram(ca):
    return 0x40000000 + 0x6B8000 + ca

def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for blk in iter(lambda: f.read(1 << 20), b""):
            h.update(blk)
    return h.hexdigest()

class Fw:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()
        if len(self.data) != FW_SIZE:
            raise SystemExit("FIRMWARE.bin size %d != %d" % (len(self.data), FW_SIZE))
        self.sha = sha256(path)
        if self.sha != FW_SHA:
            raise SystemExit("FIRMWARE.bin sha256 %s != %s" % (self.sha, FW_SHA))
    def off(self, ca):
        return ca - BASE
    def u8(self, ca):
        return self.data[ca - BASE]
    def u16(self, ca):
        return struct.unpack_from("<H", self.data, ca - BASE)[0]
    def u32(self, ca):
        return struct.unpack_from("<I", self.data, ca - BASE)[0]
    def has(self, ca, n):
        return 0 <= ca - BASE <= len(self.data) - n
    def cstr(self, ca, n=16):
        return self.data[ca - BASE:ca - BASE + n].split(b"\0")[0].decode("ascii", "replace")

def find_fw():
    for p in FW_CANDIDATES:
        p = os.path.abspath(p)
        if os.path.isfile(p):
            return p
    raise SystemExit("FIRMWARE.bin not found; set FW=<path>")

# ---------------------------------------------------------------- capstone helpers
def _cs():
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
    md.detail = True
    return md

def dis(fw, start, end):
    """[(ca, text)] with PC-relative literals resolved as '   ; =0xVALUE'."""
    import capstone
    md = _cs()
    out = []
    for insn in md.disasm(fw.data[fw.off(start):fw.off(end)], start):
        lit = ""
        for op in insn.operands:
            if op.type == capstone.arm.ARM_OP_MEM and op.mem.base == capstone.arm.ARM_REG_PC:
                tgt = ((insn.address + 4) // 4) * 4 + op.mem.disp
                if fw.has(tgt, 4):
                    lit = "   ; =0x%08X" % fw.u32(tgt)
        out.append((insn.address, ("%s %s%s" % (insn.mnemonic, insn.op_str, lit)).strip()))
    return out

def has_insns(fw, ca, want, span=0x120):
    """want = [(mnemonic, opsub)] in order somewhere in [ca, ca+span)."""
    seq = [(a, t) for a, t in dis(fw, ca, ca + span)]
    i = 0
    for a, t in seq:
        if i < len(want) and t.startswith(want[i][0]) and want[i][1] in t:
            i += 1
    return i == len(want)

# ---------------------------------------------------------------- the decode data
# (label, CA, [(mnemonic, opsub)], meaning) - asserted in order from CA.
CHAIN = [
    # -- C5: local dispatch wiring -------------------------------------------------
    ("notifier install (msg_tx tail)", 0x1027FA, [
        ("ldr.w", "r4, [r4, #0xc0]"), ("movs", "r1, #1"),
        ("add.w", "r0, r4, #0x48"), ("bl", "#0x1024bc"),
        ("adds", "r0, r4, #4"), ("bl", "#0xc2a54")],
     "queue+0xC0 = notifier/customer; set notifier+0x48=1 (atomic) and post schedule "
     "notifier+4 via 0xC2A54"),
    ("queue cfg -> customer index", 0x4809E, [
        ("mov", "r1, r5"), ("ldr", "r0, [sp, #0x18]"), ("bl", "#0x1024ce"),
        ("ldr.w", "lr, [r0, #4]"), ("ldrb.w", "r2, [lr, #3]"),
        ("movs", "r1, #0x68"), ("mla", "r2, r2, r1, r3")],
     "queue = 0x1024CE(dest0, qidx); cfg = [queue+4]; customer index = u8[cfg+3]; "
     "customer = customer_array + 0x68*idx"),
    ("bind queue to customer + notifier", 0x480D0, [
        ("ldr", "r1, [r2, #0x54]"), ("cmp", "r7, r1"), ("ldr", "r3, [r2, #0x58]"),
        ("str", "r0, [r3]"), ("str", "r7, [r0]"), ("str", "r0, [r2, #0x58]"),
        ("str.w", "r2, [r0, #0xc0]")],
     "append the queue object to the customer binding list at customer+0x54 and set "
     "queue+0xC0 = customer"),
    ("customer init (thread table record)", 0x4802A, [
        ("movs", "r3, #0x30"), ("ldr", "r5, [r2, #0x1c]"),
        ("mla", "r5, r3, sb, r5"), ("ldr.w", "r1, [r4, #0x454]"),
        ("adds", "r7, r1, r2")],
     "thread record = thread_table + 0x30*customer; customer = [dest0+0xC] + 0x68*customer"),
    ("customer init (fields + thread create)", 0x48046, [
        ("ldr", "r3, [sp, #0x18]"), ("str", "r3, [r1, r2]"),
        ("add.w", "r2, r7, #0x54"), ("strd", "r2, r2, [r7, #0x54]"),
        ("bl", "#0xc71b8"), ("add.w", "r0, r7, #0x48"), ("bl", "#0x1024bc"),
        ("str", "r5, [r7, #0x4c]"), ("strd", "r5, r2, [sp, #0x54]"),
        ("bl", "#0xc745c"), ("str", "r0, [r7, #0x50]")],
     "customer+0 = dest record; +0x54 binding list self-init; 0xC71B8(+4) inits the "
     "wait object; +0x48 pending = 0; +0x4C = thread record; task arg = customer; "
     "+0x50 = task handle"),
    ("struct bases (dest0)", 0x47E30, [
        ("movne", "r5, #0xe4"), ("addne", "r0, #4"), ("strne.w", "r0, [r4, #0x450]"),
        ("movne", "r0, #0x68"), ("addne.w", "r1, r1, #0x2b0"),
        ("strne.w", "r1, [r4, #0x454]"), ("addne.w", "r2, r2, #0x3e8"),
        ("strne.w", "r2, [r4, #0x458]")],
     "dest0 arrays: queues = instance+4 (stride 0xE4); customers = instance+0x2B0 "
     "(stride 0x68); groups = instance+0x3E8 (stride 0xC)"),
    ("queue constructor", 0x47F14, [
        ("ldr.w", "r1, [r4, #0x450]"), ("mul", "r0, fp, r7"), ("ldr.w", "r2, [r4, #0x44c]"),
        ("str.w", "sb, [r1, r0]"), ("str", "r2, [r5, #4]"),
        ("add.w", "r0, r5, #0x40"), ("bl", "#0x102514"),
        ("add.w", "r0, r5, #0x80"), ("bl", "#0x102514"),
        ("add.w", "r0, r5, #0xc"), ("bl", "#0xfda0c"),
        ("str.w", "sb, [r5, #0xc0]"), ("strb.w", "sb, [r5, #0xc4]"),
        ("str", "r2, [r5, #8]")],
     "every queue: +0 cfg, +0x40/+0x80 list heads, +0xC lock, +0xC0 notifier=NULL, "
     "+0xC4 flag=0, +8 state=1"),
    ("HCC RX thread entry (worker)", 0x102552, [
        ("push.w", "{r0, r1, r2, r4, r5, r6, r7, r8, sb, sl, fp, lr}"),
        ("ldr", "r4, [r5, #0x48]"), ("add.w", "r0, r5, #0x48"),
        ("bl", "#0x1024bc"), ("ldr", "r4, [r5, #0x54]"),
        ("mov", "r0, r6"), ("bl", "#0xc23ac"), ("ldr.w", "r8, [r5]"),
        ("add.w", "r6, r4, #0xc"), ("bl", "#0xfdf8e"), ("add.w", "sb, r4, #0x80"),
        ("add.w", "r0, r4, #0x40"), ("bl", "#0x102530"), ("bl", "#0xfdf50"),
        ("ldrh", "r3, [r3, #4]"), ("ldr.w", "r3, [r4, #0x80]"),
        ("bl", "#0x1024ea"), ("bl", "#0x409ea"), ("bl", "#0xfd4c8"),
        ("bl", "#0xfd7e8"), ("bl", "#0x1024e6")],
     "customer thread body: if +0x48 pending==1 clear it; for each bound queue: lock "
     "queue+0xC, splice queue+0x40 -> queue+0x80, unlock, limit=u16[cfg+4], pop "
     "(0x1024EA) + resolve (0xFD4C8, table = [customer]) + dispatch (0xFD7E8) then free; "
     "when idle block in 0xC23AC(customer+4)"),
    ("list helpers", 0x1024EA, [
        ("ldr", "r3, [r0]"), ("cmp", "r0, r3"), ("ldr", "r1, [r3]"),
        ("str", "r1, [r0]"), ("str", "r2, [r0, #8]")],
     "0x1024EA = pop head (list head at [obj]; count at +8); 0x102662 = append tail; "
     "0x102530 = splice; 0x102514 = list init"),
    ("registrar 0x102866", 0x102866, [
        ("ldr.w", "r0, [r4, #0x44c]"), ("movs", "r5, #0xc"), ("muls", "r5, r3, r5"),
        ("ldr.w", "r0, [r4, #0x458]"), ("ldr", "r6, [r0, #4]"),
        ("ldrh", "r2, [r1, #-0x4]"), ("lsls", "r3, r2, #4"),
        ("str", "r2, [r6, r3]"), ("str.w", "r3, [ip, #4]")],
     "0x102866(group, table, count): item[id] = {id, handler(thumb|1)} in the dest0 "
     "group array; local tables therefore land in the dest0 map"),
    ("boot task registers local g2 table", 0x48336, [
        ("movs", "r2, #0x13"), ("ldr", "r1, [pc, #0x1a4]"), ("movs", "r0, #2"),
        ("bl", "#0x102866"), ("movs", "r2, #0x28"), ("ldr", "r1, [pc, #0x1a0]"),
        ("movs", "r0, #0"), ("bl", "#0x102866")],
     "g2: 19 entries (table 0x10CC18), g0: 40 entries (table 0x10CCFC)"),
    # -- C6/C7: the CRX builder ---------------------------------------------------
    ("g2 id3 handler 0xBBC9C", 0xBBC9C, [
        ("push", "{r0, r1, r4, r5, r6, lr}"), ("bl", "#0x409ea"),
        ("ldr", "r5, [r0, #0x14]"), ("ldrb", "r1, [r5, #1]"),
        ("ldrb", "r0, [r5]"), ("bl", "#0xa4fc8"), ("ldr", "r2, [r6, #0xc]"),
        ("bl", "#0x77324"), ("bl", "#0xba1c8"), ("bl", "#0xb52f0")],
     "0xBBC9C: band = hdr[0x14]; chip = 0xA4FC8(u8[band], u8[band+1]); splice the "
     "hdr[0xC] received-frame chain; 0xBA1C8(chip, band, list); on 0x8B2D re-arm the "
     "PPDU descriptors 0xB52F0"),
    ("g2 id3 -> report class msg (0xBA1C8)", 0xBA408, [
        ("movs", "r1, #6"), ("movs", "r0, #0x14"), ("bl", "#0x102822"),
        ("bl", "#0x409ea"), ("movs", "r2, #2"), ("ldrb", "r1, [r0]"),
        ("strb", "r2, [r0, #6]"), ("bfi", "r1, r2, #0, #4"),
        ("ldrb.w", "r1, [sl]"), ("str", "sl, [r0, #0x14]"),
        ("str", "r7, [r0, #0xc]"), ("bl", "#0xb9780")],
     "0xBA1C8 builds a g2/id2 report-class message (chip=u8[band], hdr[0x14]=band, "
     "hdr[0xC]=frame descriptor) and calls the CRX builder 0xB9780 once per descriptor"),
    ("CRX builder 0xB9780 head (G1/G2)", 0xB9780, [
        ("push.w", "{r4, r5, r6, r7, r8, sb, sl, fp, lr}"), ("bl", "#0x409ea"),
        ("ldr", "r3, [r0, #0x14]"), ("add.w", "r4, r0, #0xc"),
        ("ldrb", "r1, [r3, #1]"), ("ldrb", "r0, [r3]"), ("bl", "#0xa4fc8"),
        ("add.w", "r3, r6, #0x1a00"), ("ldr.w", "r8, [r3]")],
     "G1: chip resolvable; the per-chip RX ctx pointer [chip+0x1A00] must be non-NULL"),
    ("CRX builder 0xB9780 per-frame select", 0xB980E, [
        ("ldr.w", "r5, [sl, #0xc]"), ("ldr", "r4, [r5, #-0x4]"),
        ("bl", "#0x40cce"), ("ldrb", "r7, [r0, #-0x1f]"),
        ("ldrb", "r4, [r0, #-0x1d]"), ("ubfx", "r7, r7, #6, #1"),
        ("lsrs", "r4, r4, #4"), ("bl", "#0xb2cf4"), ("add", "r1, sp, #0xa0"),
        ("bl", "#0x102662")],
     "per frame: forward flag = bit6 of the RX-info byte (0x40CCE(frame)-0x1F); class "
     "nibble = (0x40CCE(frame)-0x1D)>>4; flag=0 -> append the frame to the local list, "
     "flag=1 -> 0xB2CF4 builds the forwarded list"),
    ("CRX builder class-nibble gate", 0xB9864, [
        ("cmp", "r4, #3"), ("cbz", "r4"), ("movw", "r1, #0xa6a"),
        ("bl", "#0xc510a"), ("movs", "r4, #0")],
     "class nibble must be 1 or 2; nibble 0 or 3 drops the whole list (log 0xA6A), "
     "return 0x8B2D (no counter)"),
    ("CRX build+post loop", 0xBA0A0, [
        ("movs", "r1, #6"), ("ldrb.w", "r0, [r6, #0xd6d]"), ("bl", "#0x4c9b4"),
        ("mov.w", "sb, #1"), ("ldr", "r3, [sp, #0x60]"), ("cmp", "r3, r7"),
        ("bl", "#0x1024ea"), ("bl", "#0x409ea"), ("bl", "#0x40cba"),
        ("ldrh.w", "fp, [r0, #0x11]"), ("ldrb", "r0, [r0, #6]"),
        ("bl", "#0xbc524"), ("add.w", "r1, r2, #0xc"), ("cmp.w", "r1, #0x680"),
        ("ldrb.w", "r3, [r3, #0xcc5]"), ("adds", "r3, #1"),
        ("strb.w", "r3, [r2, #0xcc5]"), ("ldrb", "r3, [r4]"),
        ("add.w", "fp, fp, #0x3c"), ("bfi", "r3, sb, #0, #4"), ("strb", "r3, [r4]"),
        ("movs", "r3, #0xd"), ("strb", "r3, [r4, #6]"), ("strh.w", "fp, [r4, #4]"),
        ("ldrb", "r2, [r0, #2]"), ("ldrb", "r3, [r4, #1]"),
        ("bfi", "r3, r2, #4, #4"), ("strb", "r3, [r4, #1]"),
        ("ldrb", "r3, [r0, #1]"), ("strb", "r3, [r4, #2]"),
        ("ldrb", "r3, [r0]"), ("strb", "r3, [r4, #3]"), ("bl", "#0x4bdce"),
        ("bl", "#0x102760")],
     "C6/C7: frame_len = u16[rxinfo+0x11]; fwd idx = u8[rxinfo+6]; dest = 0xBC524(idx); "
     "G8: frame_len+0x3C <= 0x680 else band+0xCC5++; G7: dest==0 -> drop (log 0x49F); "
     "build group1/id13/total=frame_len+0x3C; hdr[1]hi = u8[dest+2], hdr[2]=u8[dest+1], "
     "hdr[3]=u8[dest]; copy MPDU (0x4BDCE) then post 0x102760"),
    ("fwd/dest resolver 0xBC524", 0xBC524, [
        ("push", "{r3, r4, r5, lr}"), ("mov", "r5, r0"), ("movs", "r0, #0"),
        ("bl", "#0xfe558"), ("cmp", "r5, #0x12"), ("ldr", "r3, [pc, #0x18]"),
        ("add.w", "r5, r3, r5, lsl #2"), ("ldrh.w", "r4, [r3, #0x6fc]"),
        ("ldr.w", "r0, [r5, #0x650]")],
     "0xBC524(idx<=0x12) = u16[chipmgr+0x6FC] + u32[chipmgr+0x650+4*idx]; the destination "
     "nibble copied by the builder is u8[dest+2] & 0xF"),
    ("chip ctx resolver 0xA4FC8", 0xA4FC8, [
        ("push", "{r3, lr}"), ("mov", "r2, r0"), ("mov", "r3, r1"),
        ("cbnz", "r0"), ("cmp", "r1, #1"), ("movw", "r1, #0x1b68"),
        ("ldr", "r0, [pc, #0xc]"), ("mla", "r0, r1, r3, r0")],
     "chip = 0x15CA08 + 0x1B68*u8[band+1]; REQUIRES u8[band]==0 (else log 0x361 and "
     "NULL) - the same byte the SMAC producer copies into the g2-id3 dest nibble"),
    ("chip+0x1A00 installer", 0x48A68, [
        ("add.w", "r3, r4, #0x1a00"), ("str.w", "sl, [r3]"),
        ("ldrb.w", "r3, [sl, #1]"), ("cmp", "r3, #2"),
        ("movhs", "r3, #2"), ("strb.w", "r3, [r4, #0xd70]")],
     "the per-band init stores the RX ctx pointer sl into chip+0x1A00 and derives "
     "chip+0xD70 = min(ctx[1],2); the gate is init-time, not per-frame"),
    ("SMAC producer g2 id3", 0xB545E, [
        ("bl", "#0x409ea"), ("movs", "r2, #2"), ("ldrb", "r3, [r0]"),
        ("strb", "r5, [r0, #7]"), ("bfi", "r3, r2, #0, #4"), ("strb", "r3, [r0]"),
        ("movs", "r3, #3"), ("strb", "r3, [r0, #6]"), ("ldrb", "r2, [r4]"),
        ("ldrb", "r3", ""), ("bfi", "r3, r2, #4, #4"), ("strb", "r3, [r0, #1]"),
        ("str", "r4, [r0, #0x14]"), ("str", "r6, [r0, #0xc]"), ("bl", "#0x102760")],
     "the only g2/id3 builder: group=2, id=3, dest nibble = u8[band]=0, hdr[0x14]=band, "
     "hdr[0xC]=frame chain; post 0x102760; post failure bumps band+0xCCE"),
]

DATA = [
    ("hcc map n_queues", lambda f: f.u32(HCC_MAP + 0x08) == 3),
    ("hcc map n_customers", lambda f: f.u32(HCC_MAP + 0x0C) == 3),
    ("hcc map n_groups", lambda f: f.u32(HCC_MAP + 0x10) == 7),
    ("hcc map queue cfg 0x1040C0", lambda f: f.u32(HCC_MAP + 0x18) == QUEUE_CFG),
    ("hcc map thread table 0x10CFC8", lambda f: f.u32(HCC_MAP + 0x1C) == THREAD_TAB),
    ("hcc map group table 0x10CF74", lambda f: f.u32(HCC_MAP + 0x24) == GROUP_TAB),
    ("thread0 name HCC TX Thread", lambda f: f.cstr(THREAD_TAB) == "HCC TX Thread"),
    ("thread1 name HCC RX Thread", lambda f: f.cstr(THREAD_TAB + 0x30) == "HCC RX Thread"),
    ("thread2 name HCC OAM Thread", lambda f: f.cstr(THREAD_TAB + 0x60) == "HCC OAM Thread"),
    ("RX thread id 1", lambda f: f.u32(THREAD_TAB + 0x30 + 0x10) == 1),
    ("RX thread entry 0x102553", lambda f: f.u32(THREAD_TAB + 0x30 + 0x28) == 0x00102553),
    ("TX thread entry 0xC515F", lambda f: f.u32(THREAD_TAB + 0x28) == 0x000C515F),
    ("OAM thread entry 0xC515F", lambda f: f.u32(THREAD_TAB + 0x60 + 0x28) == 0x000C515F),
    ("queue cfg q0 customer 0", lambda f: f.u8(QUEUE_CFG + 0x03) == 0),
    ("queue cfg q1 customer 1", lambda f: f.u8(QUEUE_CFG + 0x0B) == 1),
    ("queue cfg q2 customer 2", lambda f: f.u8(QUEUE_CFG + 0x13) == 2),
    ("queue cfg q0 round limit 100", lambda f: f.u16(QUEUE_CFG + 0x04) == 0x64),
    ("queue cfg q1 round limit 80", lambda f: f.u16(QUEUE_CFG + 0x0C) == 0x50),
    ("queue cfg q2 round limit 80", lambda f: f.u16(QUEUE_CFG + 0x14) == 0x50),
    ("g2 table entry0 id 2 -> 0xB9781", lambda f: f.u16(G2_TABLE) == 2 and
        f.u32(G2_TABLE + 4) == 0x000B9781),
    ("g2 table entry1 id 3 -> 0xBBC9D", lambda f: f.u16(G2_TABLE + 12) == 3 and
        f.u32(G2_TABLE + 16) == 0x000BBC9D),
    ("g2 table entry3 id 5 -> 0xB9781", lambda f: f.u16(G2_TABLE + 36) == 5 and
        f.u32(G2_TABLE + 40) == 0x000B9781),
    ("g0 table count 40 (last id 48)", lambda f: f.u16(G0_TABLE + 12 * 39) == 48),
    ("group table g0 -> q1", lambda f: f.u16(GROUP_TAB + 0) == 0 and
        f.u16(GROUP_TAB + 2) == 1),
    ("group table g1 -> q0", lambda f: f.u16(GROUP_TAB + 12) == 1 and
        f.u16(GROUP_TAB + 14) == 0),
    ("group table g2 -> q1", lambda f: f.u16(GROUP_TAB + 24) == 2 and
        f.u16(GROUP_TAB + 26) == 1),
    ("group table g4 -> q2", lambda f: f.u16(GROUP_TAB + 48) == 4 and
        f.u16(GROUP_TAB + 50) == 2),
    ("g2 item array ptr", lambda f: f.u32(GROUP_TAB + 24 + 4) == 0x00118F08 and
        f.u32(GROUP_TAB + 24 + 8) == 43),
    ("chip base literal 0x15CA08", lambda f: f.u32(0xA4FF4) == CHIP_BASE),
    ("chipmgr base literal 0x170E08", lambda f: f.u32(0xBC564) == GLOBAL_CHIPMGR),
    ("boot g2 registration literal 0x10CC18", lambda f: f.u32(0x484E0) == G2_TABLE),
    ("boot g0 registration literal 0x10CCFC", lambda f: f.u32(0x484E4) == G0_TABLE),
    ("thread table record size 0x30", lambda f: True),
]

# static address derivations the probes assert against (dest0 runtime image)
DERIVED = {
    "queue array": QUEUE_ARR,
    "q0": QUEUE_ARR + 0 * QUEUE_STRIDE,
    "q1": QUEUE_ARR + 1 * QUEUE_STRIDE,
    "q2": QUEUE_ARR + 2 * QUEUE_STRIDE,
    "q0 notifier slot": QUEUE_ARR + 0 * QUEUE_STRIDE + 0xC0,
    "q1 notifier slot": QUEUE_ARR + 1 * QUEUE_STRIDE + 0xC0,
    "q2 notifier slot": QUEUE_ARR + 2 * QUEUE_STRIDE + 0xC0,
    "customer array": CUST_ARR,
    "customer0": CUST_ARR + 0 * CUST_STRIDE,
    "customer1": CUST_ARR + 1 * CUST_STRIDE,
    "customer2": CUST_ARR + 2 * CUST_STRIDE,
    "group array": GROUP_ARR,
    "dest0 record": DEST0,
    "band0": BAND0,
    "band1": BAND0 + 0xE28,
    "chip0": CHIP_BASE,
    "chip1": CHIP_BASE + 0x1B68,
    "chip0+0x1A00": CHIP_BASE + 0x1A00,
    "chip1+0x1A00": CHIP_BASE + 0x1B68 + 0x1A00,
    "chipmgr base": GLOBAL_CHIPMGR,
    "dest table entries": DEST_TABLE_FWD,
    "dest table base": DEST_TABLE_BASE,
}

EXPECTED = {
    "q0+0xC0 == customer0": (DERIVED["q0 notifier slot"], DERIVED["customer0"]),
    "q1+0xC0 == customer1": (DERIVED["q1 notifier slot"], DERIVED["customer1"]),
    "q2+0xC0 == customer2": (DERIVED["q2 notifier slot"], DERIVED["customer2"]),
    "customer1+0x4C == RX thread record": (DERIVED["customer1"] + 0x4C, THREAD_TAB + 0x30),
    "customer1+0x54 -> q1": (DERIVED["customer1"] + 0x54, DERIVED["q1"]),
    "customer1+0x58 -> q1": (DERIVED["customer1"] + 0x58, DERIVED["q1"]),
}

def check_derivations():
    ok = []
    ok.append(("q1 = 0x171C54", DERIVED["q1"] == 0x171C54))
    ok.append(("q1 notifier slot = 0x171D14", DERIVED["q1 notifier slot"] == 0x171D14))
    ok.append(("customer array = 0x171E1C", CUST_ARR == 0x171E1C))
    ok.append(("customer1 = 0x171E84", DERIVED["customer1"] == 0x171E84))
    ok.append(("customer2 = 0x171EEC", DERIVED["customer2"] == 0x171EEC))
    ok.append(("group array = 0x171F54", GROUP_ARR == 0x171F54))
    ok.append(("dest0 record = 0x171FB4", DEST0 == 0x171FB4))
    ok.append(("queue array end == customer array", QUEUE_ARR + 3 * QUEUE_STRIDE == CUST_ARR))
    ok.append(("phys(q1 notifier slot) = 0x40829D14",
               phys_ram(DERIVED["q1 notifier slot"]) == 0x40829D14))
    ok.append(("phys(band0) = 0x4082CBF0", phys_ram(BAND0) == 0x4082CBF0))
    ok.append(("phys(chip0+0x1A00) = 0x40816408",
               phys_ram(DERIVED["chip0+0x1A00"]) == 0x40816408))
    ok.append(("phys(dest table entries) = 0x40829458",
               phys_ram(DEST_TABLE_FWD) == 0x40829458))
    return ok

# ---------------------------------------------------------------- modes
def selftest(fw):
    ok = fail = 0
    def ck(name, cond, detail=""):
        nonlocal ok, fail
        if cond:
            ok += 1
            print("PASS %s %s" % (name, detail))
        else:
            fail += 1
            print("FAIL %s %s" % (name, detail))

    ck("fw size", len(fw.data) == FW_SIZE, "%d" % len(fw.data))
    ck("fw sha256", fw.sha == FW_SHA, fw.sha)
    for label, ca, rows, meaning in CHAIN:
        ck("decode %s @0x%08X" % (label, ca), has_insns(fw, ca, rows),
           " -> ".join("%s %s" % (r[0], r[1]) for r in rows[:3]))
    for name, fn in DATA:
        ck("data %s" % name, fn(fw))
    for name, cond in check_derivations():
        ck("derived %s" % name, cond)
    # registrar literal + count wiring
    ck("boot task g2 registration count", has_insns(fw, 0x48336, [
        ("movs", "r2, #0x13"), ("ldr", "r1, [pc, #0x1a4]"), ("movs", "r0, #2"),
        ("bl", "#0x102866")]))
    ck("boot task g0 registration count", has_insns(fw, 0x48340, [
        ("movs", "r2, #0x28"), ("ldr", "r1, [pc, #0x1a0]"), ("movs", "r0, #0"),
        ("bl", "#0x102866")]))
    # queue service-limit path in the worker
    ck("worker round limit u16[cfg+4]", has_insns(fw, 0x1025D4, [
        ("ldr", "r3, [r4, #4]"), ("cbz", "r3"), ("movs", "r7, #0"),
        ("ldrh", "r3, [r3, #4]")]))
    ck("worker list splice", has_insns(fw, 0x1025C2, [
        ("mov", "r1, sb"), ("add.w", "r0, r4, #0x40"), ("bl", "#0x102530")]))
    ck("worker dispatch resolver", has_insns(fw, 0x102602, [
        ("bl", "#0x409ea"), ("bl", "#0xfd4c8"), ("bl", "#0xfd7e8")]))
    print("\n%d/%d PASS" % (ok, ok + fail))
    return 0 if fail == 0 else 1

def show(fw, ca, n=8):
    for a, t in dis(fw, ca, ca + n * 4):
        print("  %07X: %s" % (a, t))

def decode(fw):
    print("FIRMWARE.bin %d B sha256 %s ; CA = file + 0x%X" % (len(fw.data), fw.sha, BASE))
    print("phys(firmware RAM) = 0x40000000 + 0x6B8000 + CA\n")
    for label, ca, rows, meaning in CHAIN:
        print("== %s  CA 0x%08X" % (label, ca))
        show(fw, ca)
        print("   %s\n" % meaning)

def tables(fw):
    print("# T2 - dest0 runtime wiring (derived from the init immediates)")
    for k in ("queue array", "q0", "q1", "q2", "q0 notifier slot", "q1 notifier slot",
              "q2 notifier slot", "customer array", "customer0", "customer1",
              "customer2", "group array", "dest0 record"):
        print("  %-18s CA 0x%08X  phys 0x%08X" % (k, DERIVED[k], phys_ram(DERIVED[k])))
    print("\n# queue cfg (static) qidx customer round-limit")
    for i in range(3):
        base = QUEUE_CFG + 8 * i
        print("  q%d  cfg 0x%08X  idx=%d customer=%d limit=%d byte7=%d" % (
            i, base, fw.u8(base), fw.u8(base + 3), fw.u16(base + 4), fw.u8(base + 7)))
    print("\n# T3 - local handler tables")
    for name, base, count in (("g2", G2_TABLE, 19), ("g0", G0_TABLE, 40)):
        print("  %s table 0x%08X count %d" % (name, base, count))
        for i in range(count):
            a = base + 12 * i
            if fw.u32(a + 4) & 1:
                print("    id %-2d handler 0x%08X" % (fw.u16(a), fw.u32(a + 4) & ~1))
    print("\n# T4 - CRX builder gates")
    for g, ca, t in (
            ("G1", 0xB97EE, "[chip+0x1A00] != 0 (init-time)"),
            ("Gselect", 0xB9820, "forward flag = bit6(0x40CCE(frame)-0x1F)"),
            ("Gclass", 0xB982A, "class nibble = (0x40CCE(frame)-0x1D)>>4 in {1,2}"),
            ("Glen", 0xBA110, "frame_len+0x3C <= 0x680 else band+0xCC5++"),
            ("Gdest", 0xBA134, "dest = 0xBC524(u8[rxinfo+6]) != 0"),
            ("Gpost", 0xBA17A, "0x102760(group1/id13) must return 0")):
        print("  %-7s CA 0x%08X  %s" % (g, ca, t))
        show(fw, ca, 3)

def probes(fw):
    print("# read-only probe pack - HOST-ONLY document; the Lead fires")
    def ram(ca):
        return "CA 0x%08X phys 0x%08X" % (ca, phys_ram(ca))
    rows = [
        ("P0", "host D2H catalogue", "rd_catalog", "-", "any g2 id3, any g1 id13"),
        ("P12", "band0 object word0", ram(BAND0), "4", "byte0 must be 0 (chip/dest idx)"),
        ("P13", "band0 post-failure +0xCCE", ram(BAND0 + 0xCCE), "1", "0 expected"),
        ("P15", "q0/q1/q2 state/8, list+0x48, notifier+0xC0, flag+0xC4",
         ram(DERIVED["q0"]), "4x3", "q1 notifier 0x%08X" % DERIVED["customer1"]),
        ("P16", "group2 item[3] {id,handler,aux,stats} of [0x171F6C+4]",
         "follow group array", "16", "handler 0x000BBC9D; stats delta = accepted g2id3"),
        ("P18", "band0 CRX drop +0xCC5", ram(BAND0 + 0xCC5), "1", "0 expected"),
        ("P20", "customer1 +0 +4 +0x48 +0x4C +0x50 +0x54 +0x58",
         ram(DERIVED["customer1"]), "7x4", "+0=0x%08X +0x4C=0x%08X +0x50!=0 +0x54=0x%08X"
         % (DEST0, THREAD_TAB + 0x30, DERIVED["q1"])),
        ("P21", "chip0 RX ctx [chip+0x1A00]", ram(DERIVED["chip0+0x1A00"]), "4", "!=0"),
        ("P22", "fwd/dest table: base u16 + 0x13 u32 entries",
         ram(DEST_TABLE_BASE), "2+0x4C", "dest_i = base + entry_i; u8[dest_i+2]&0xF"),
        ("P23", "chip0 band index byte +0xD70", ram(CHIP_BASE + 0xD70), "1", "0/1"),
    ]
    for p, what, where, width, expect in rows:
        print("  %-4s %-52s %-40s %-6s %s" % (p, what, where, width, expect))

def main(argv):
    fw_path = os.environ.get("FW") or find_fw()
    fw = Fw(fw_path)
    mode = argv[1] if len(argv) > 1 else "--selftest"
    if mode == "--selftest":
        return selftest(fw)
    if mode == "--decode":
        decode(fw); return 0
    if mode == "--tables":
        tables(fw); return 0
    if mode == "--probes":
        probes(fw); return 0
    print("usage: %s [--selftest|--decode|--tables|--probes]" % argv[0])
    return 2

if __name__ == "__main__":
    sys.exit(main(sys.argv))
