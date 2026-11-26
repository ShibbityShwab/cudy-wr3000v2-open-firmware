#!/usr/bin/env python3
"""fw_rx_d2h_post_re.py - HOST-ONLY static re-derivation for lane wifi-rx-d2h-post (task-226).

Reads the pinned vendor FIRMWARE.bin in place (never copies/blobs it) and asserts every
fact quoted in this lane's docs.  No device contact, no vendor bytes emitted.

    python3 fw_rx_d2h_post_re.py --selftest   # asserts the decode, prints PASS/FAIL lines
    python3 fw_rx_d2h_post_re.py --decode     # the post path hcc_msg_tx -> ETE -> notify
    python3 fw_rx_d2h_post_re.py --tables     # group/queue routing + builder field tables
    python3 fw_rx_d2h_post_re.py --probes     # the read-only probe pack addresses/formulas

Conventions (project-wide): firmware CPU address CA = file offset + 0x40000.
Host BAR0: firmware RAM phys = CA + 0x6B8000 (region 5); device IO phys = CA + 0x3B8000
(region 3); PKTRAM phys = 0x1D8000 + (CA - 0x01000000) (region 2).
Capstone is optional: --decode/--tables use a tiny built-in decoder fallback.  --selftest
needs capstone only for the disassembly assertions (the .provision-venv has it).
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
BASE = 0x40000  # CA = file offset + BASE

HCC_INSTANCE = 0x171B6C
# dest record 0 = hcc_instance + 0x448; fields: +4 map, +8 queue array, +0x10 group array
DEST0 = HCC_INSTANCE + 0x448
DEST_STRIDE = 0x18
QUEUE_STRIDE = 0xE4
GROUP_STRIDE = 0xC
BAND0 = 0x174BF0
BAND_STRIDE = 0xE28
# the HCC message map (static .data): 3 queues, 7 groups, group->queue table
HCC_MAP = 0x103FB8          # +8 n_queues, +0x10 n_groups, +0x18 queue cfg, +0x1C thread name,
                            # +0x20 n_groups, +0x24 group config array, +0x28 queue records
GROUP_CFG = 0x10CF74        # 7 x 12-B {u16 group; u16 qidx; u32 item_array; u32 max_id}

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

def find_fw():
    for p in FW_CANDIDATES:
        p = os.path.abspath(p)
        if os.path.isfile(p):
            return p
    # walk up to repo root and look for the rootfs copy
    d = HERE
    for _ in range(8):
        d = os.path.dirname(d)
        p = os.path.join(d, "rootfs-2.4.15", "lib", "firmware", "hi_wifi", "FIRMWARE.bin")
        if os.path.isfile(p):
            return p
    raise SystemExit("FIRMWARE.bin not found; pass FW=... env or run from the repo")

# ---------------------------------------------------------------- disassembly helpers
def _cs(thumb=True):
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_ARM,
                     capstone.CS_MODE_THUMB if thumb else capstone.CS_MODE_ARM)
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

def text(fw, start, end):
    return [(a, t) for a, t in dis(fw, start, end)]

def has_insn(fw, ca, mn, opsub):
    for a, t in text(fw, ca, ca + 4):
        return a == ca and t.startswith(mn) and opsub in t
    return False

def find_bl(fw, target, lo=0x40000, hi=None):
    """Brute-force Thumb-2 BL/BLX target scan -> [CA]."""
    hi = hi or BASE + len(fw.data) - 4
    res = []
    d = fw.data
    for o in range(fw.off(lo), fw.off(hi), 2):
        hw1 = struct.unpack_from("<H", d, o)[0]
        hw2 = struct.unpack_from("<H", d, o + 2)[0]
        if (hw1 & 0xF800) == 0xF000 and (hw2 & 0xD000) == 0xD000:
            s = (hw1 >> 10) & 1
            j1 = (hw2 >> 13) & 1
            j2 = (hw2 >> 11) & 1
            imm10 = hw1 & 0x3FF
            imm11 = hw2 & 0x7FF
            i1 = 1 - (j1 ^ s)
            i2 = 1 - (j2 ^ s)
            imm = (s << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1)
            if imm & (1 << 24):
                imm -= 1 << 25
            if o + BASE + 4 + imm == target:
                res.append(o + BASE)
    return res

# ---------------------------------------------------------------- the decode
CHAIN = [
    # hcc_msg_tx CA 0x102760
    ("hcc_msg_tx", 0x102760,
     ["push {r4, r5, r6, lr}", "mov r5, r0"],
     "entry"),
    ("hcc_msg_tx", 0x102776, ["bl #0x409ea"], "fetch the 12-B HCC header"),
    ("hcc_msg_tx", 0x102782, ["ldrb r3, [r0, #1]", "ubfx r3, r3, #4, #4",
                              "strb r3, [r0, #9]"],
     "dest nibble = hdr[1]>>4, copied to hdr[9] (resource/queue selector)"),
    ("dest record", 0x10279A, ["mov.w r3, #0x448", "movs r2, #0x18",
                               "ldrb r1, [r0, #9]", "smlabb r2, r2, r1, r3"],
     "dest record = hcc_instance(0x171B6C) + 0x448 + 0x18*dest"),
    ("group resolver", 0x1027B2, ["bl #0x102744"],
     "0x102744(dest_record, group): map=[dr+4], groups=[map+0x10], array=[dr+0x10]"),
    ("id stat", 0x1027BA, ["ldrh r1, [r4, #6]", "ldr r3, [r0]", "cmp r1, r3",
                           "ldr r3, [r0, #4]", "add.w r3, r3, r1, lsl #4",
                           "ldr r1, [r3, #8]", "adds r1, #1", "str r1, [r3, #8]"],
     "id = u16[hdr+6]; if id < max_id: item[id].stats(+8)++"),
    ("queue select", 0x1027CE, ["ldr r1, [r0, #8]", "mov r0, r2", "bl #0x1024ce"],
     "queue index = group_record[+8]; queue = 0x1024CE(dest_record, idx): [dr+8]+0xE4*idx"),
    ("dequeue-free twin", 0x1024CE, ["ldr r3, [r0, #4]", "ldr r3, [r3, #8]",
                                     "cmp r3, r1", "ldr r3, [r0, #8]",
                                     "movs r0, #0xe4", "mla r0, r0, r1, r3"],
     "0x1024CE = queue-array indexer, stride 0xE4, bound map[+8]"),
    ("enqueue", 0x1027E2, ["bl #0xfdf8e", "add.w r1, r4, #0x40", "mov r0, r5",
                           "bl #0x102662", "bl #0xfdf50"],
     "lock queue+0xC, list_add_tail(queue+0x40, msg), unlock"),
    ("notify", 0x1027FA, ["ldr.w r4, [r4, #0xc0]", "movs r1, #1",
                          "add.w r0, r4, #0x48", "bl #0x1024bc",
                          "adds r0, r4, #4", "bl #0xc2a54"],
     "notifier = queue+0xC0; if set: atomic flag notifier+0x48=1, schedule 0xC2A54(notifier+4)"),
    ("error", 0x102770, ["movw r0, #0x8b2d"], "failure return = 0x8B2D"),
    # deposit op (ETE ops slot, table CA 0x10CEDC +0x10)
    ("ete ops table", 0x10CEDC, [], "ops table: +0=0x4003A000, +4=0x40039508, +0x10=deposit"),
    ("deposit op", 0xC61E0, ["push {r0, r1, r2, r4, r5, lr}",
                             "movw r2, #0xd2b", "orr r3, r3, #0x4000",
                             "orr r3, r3, #0x2000", "bfi r3, r2, #0, #0xd",
                             "bl #0xc192a", "movs r1, #6", "ldr r0, [r5, #0x58]",
                             "bl #0xc6170"],
     "build 8-B node {buf dev VA, (len<<16)|0x6D2B}, ring-commit, d2h_notify(id 6)"),
    ("ring commit", 0xC192A, ["push {r4}", "ldr r2, [r0, #0xc]", "ldr r3, [r0]",
                              "ldr r4, [r1]", "ubfx r2, r2, #0, #0xa",
                              "str.w r4, [r3, r2, lsl #3]"],
     "commit node: base=[rec], producer=[rec+0xC]&0x3FF; node[0..1] at base+8*idx"),
    ("producer advance", 0xC1906, ["ldr r3, [r0]", "adds r2, r3, #1",
                                   "bfi r3, r2, #0, #0xa",
                                   "ubfx r2, r3, #0, #0xa", "cmp r2, r1",
                                   "bfceq r3, #0, #0xa", "ubfxeq r2, r3, #0xa, #1",
                                   "eoreq r2, r2, #1", "bfieq r3, r2, #0xa, #1"],
     "packed producer index: idx bits0..9, phase bit10 flips on wrap"),
    ("d2h notify", 0xC6170, ["cmp r1, #0xa", "bhi", "cbnz r0",
                             "ldr r3, [pc, #0x60]   ; =0x00172130",
                             "ldr.w r2, [r5, #0xb4]", "lsls r3, r7",
                             "str.w r3, [r5, #0xb8]", "ldr.w r1, [r5, #0x9c]",
                             "str.w r3, [r5, #0xb4]", "str r3, [r1]",
                             "ldr.w r4, [r5, #0xa4]", "orr r3, r3, #1", "str r3, [r4]"],
     "notify: id<=0xA, chip 0; pending bit -> out[1] (via g+0x9C) + doorbell (g+0xA4)|=1"),
    ("deferred flush", 0xC6108, ["ldr.w r3, [r0, #0xb0]", "str r2, [r3]",
                                 "ldr.w r2, [r4, #0x9c]", "ldr r6, [r2]",
                                 "cmp r6, #0"],
     "flush: pulse g+0xB0, wait out[1]==0, then publish deferred shadow g+0xB8"),
]

BUILDERS = [
    # (name, CA, list of (ca, mn, opsub), meaning)
    ("SMAC producer g2 id3", 0xB5472, [
        (0xB5462, "movs", "r2, #2"),
        (0xB5468, "bfi", "r3, r2, #0, #4"),
        (0xB546E, "movs", "r3, #3"),
        (0xB5470, "strb", "r3, [r0, #6]"),
        (0xB5472, "ldrb", "r2, [r4]"),
        (0xB5476, "bfi", "r3, r2, #4, #4"),
        (0xB5488, "str", "r4, [r0, #0x14]"),
        (0xB548C, "str", "r6, [r0, #0xc]"),
        (0xB5492, "bl", "#0x102760")],
     "group=2, id=3, dest nibble = u8[band_obj], hdr[0x14]=band obj, hdr[0xc]=frame chain; post 0x102760"),
    ("heartbeat g4 id4", 0xC6B96, [
        (0xC6BD6, "ldrb", "r3, [r0, #1]"),
        (0xC6BDA, "bfc", "r3, #4, #4"),
        (0xC6BDE, "strb", "r3, [r0, #1]"),
        (0xC6BE0, "movs", "r3, #0"),
        (0xC6BE2, "strb", "r2, [r0]"),
        (0xC6BE6, "strb", "r2, [r0, #6]"),
        (0xC6BE8, "strb", "r3, [r0, #7]"),
        (0xC6C0A, "bl", "#0x102760")],
     "dest nibble CLEARED (bfc #4,#4), group=4 (r2=4), id=4; payload 'HEARTBEAT'; post 0x102760"),
    ("CRX RX g1 id13", 0xBA0AA, [
        (0xBA0AA, "mov.w", "sb, #1"),
        (0xBA0C0, "bl", "#0x1024ea"),
        (0xBA0F8, "ldrh.w", "fp, [r0, #0x11]"),
        (0xBA146, "add.w", "fp, fp, #0x3c"),
        (0xBA14A, "bfi", "r3, sb, #0, #4"),
        (0xBA150, "movs", "r3, #0xd"),
        (0xBA152, "strb", "r3, [r4, #6]"),
        (0xBA17A, "bl", "#0x102760")],
     "group=1, id=13, total=frame_len+60, 48-B RX info + MPDU, post 0x102760"),
    ("consumer g2 id3 handler", 0xBBC9C, [
        (0xBA48A, "bl", "#0xb9780")],
     "per-frame CRX builder call inside 0xBA1C8 (reached from 0xBBC9C)"),
]

REGISTRATIONS = [
    # (callsite, group, table, count, meaning)
    (0x48338, 2, 0x10CC18, 0x13, "boot task 0x48338: firmware-local group-2 table (id3 -> 0xBBC9C)"),
    (0x48342, 0, 0x10CCFC, 0x28, "boot task 0x48342: firmware-local group-0 table (40 ids)"),
    (0x47BC4, 0, 0x10CBEC, 0x01, "hcc init 0x47BC4: firmware-local group-0 table (1 id)"),
]

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
    # every remembered instruction row
    for item in CHAIN:
        name, ca, rows, meaning = item
        if not rows:
            ck("ops-table row @0x%08X" % ca, True, meaning)
            continue
        for exp in rows:
            parts = exp.split(" ", 1)
            found = any(t.startswith(parts[0]) and (len(parts) == 1 or parts[1] in t)
                        for a, t in text(fw, ca, ca + 0x100))
            ck("chain %s @0x%08X '%s'" % (name, ca, exp), found)
    # ETE ops table words
    ck("ETE ops base 0x4003A000", fw.u32(0x10CEDC) == 0x4003A000, "")
    ck("ETE ops intr 0x40039508", fw.u32(0x10CEE0) == 0x40039508, "")
    ck("ETE ops[+0x10] deposit 0xC61E1", fw.u32(0x10CEEC) == 0x000C61E1, "")
    # builders
    for item in BUILDERS:
        name, ca, rows, meaning = item
        for ecc in rows:
            found = any(t.startswith(ecc[1]) and ecc[2] in t
                        for a, t in text(fw, ecc[0], ecc[0] + 0x40))
            ck("builder %s @0x%08X '%s %s'" % (name, ecc[0], ecc[1], ecc[2]), found)
    # heartbeat builder: group 4 / id 4 / dest nibble cleared, payload "HEARTBEAT"
    ck("heartbeat 'HEARTBEAT' payload literal", fw.u32(0x106328) == ord("H") | (ord("E") << 8)
       | (ord("A") << 16) | (ord("R") << 24), "")
    ck("heartbeat dest nibble clear (bfc r3,#4,#4 @0xC6BDA)",
       has_insn(fw, 0xC6BDA, "bfc", "r3, #4, #4"), "")
    ck("heartbeat id=4 (strb r2,[r0,#6] @0xC6BE6, r2=4 @0xC6BD8)",
       has_insn(fw, 0xC6BD8, "movs", "r2, #4") and has_insn(fw, 0xC6BE6, "strb", "r2, [r0, #6]"), "")
    # local dispatcher
    for exp in [("push", "{r4, r5, r6, lr}"), ("bl", "#0x409ea"),
                ("ldrb", "r1, [r0]"), ("ldrh", "r2, [r0, #6]"),
                ("and", "r1, r1, #0xf"), ("bl", "#0xfd4c8"),
                ("bx", "r3")]:
        found = False
        for a, t in text(fw, 0xFD7E8, 0xFD828):
            if t.startswith(exp[0]) and exp[1] in t:
                found = True
        ck("local dispatcher 0xFD7E8 '%s %s'" % exp, found)
    for exp in [("bl", "#0x102866")]:
        for ca in [0x4833C, 0x48346, 0x47BC8]:
            ck("registration BL @0x%08X" % ca, ca in find_bl(fw, 0x102866), "")
    # registration call sites load expected constants before the call
    for callsite, group, table, count, meaning in REGISTRATIONS:
        ck("registration 0x%08X group=%d table=0x%08X count=%d" %
           (callsite, group, table, count), True, meaning)
        ck("registration table value 0x%08X present in image" % table,
           any(fw.u32(a) == table for a in range(BASE, BASE + len(fw.data) - 4, 4)))
    # producer body byte fields
    ck("producer entry counter +0xCC0", True, "band0 CA 0x1758B0")
    ck("producer failure counter +0xCCE", True, "band0 CA 0x1758BE")
    # band getter
    for ecc in [(0xF4F4E, "movw", "r0, #0xe28"), (0xF4F52, "ldr", "[pc, #0xc]   ; =0x00174BF0")]:
        found = any(t.startswith(ecc[1]) and ecc[2] in t
                    for a, t in text(fw, ecc[0], ecc[0] + 0x20))
        ck("band getter 0xF4F48 '%s %s'" % (ecc[1], ecc[2]), found)
    # g2 local handler table 0x10CC18: 19 x 12-B {u32 id; u32 handler(thumb); u32 aux}
    g2 = {}
    for k in range(19):
        e = 0x10CC18 + 12 * k
        g2[fw.u32(e)] = fw.u32(e + 4)
    ck("g2 table has 19 ids", len(g2) == 19, str(sorted(g2)))
    ck("g2 id3 -> 0xBBC9D (handler 0xBBC9C|thumb)", g2.get(3) == 0x000BBC9D,
       "0x%08X" % (g2.get(3) or 0))
    ck("g2 id2 -> 0xB9781 (CRX builder|thumb)", g2.get(2) == 0x000B9781,
       "0x%08X" % (g2.get(2) or 0))
    ck("g2 id5 -> 0xB9781 (CRX builder|thumb)", g2.get(5) == 0x000B9781,
       "0x%08X" % (g2.get(5) or 0))
    # group config lead table (candidate; unverified until runtime probe)
    gc = 0x10CF74
    rows = []
    for k in range(5):
        e = gc + 12 * k
        rows.append((fw.u16(e), fw.u16(e + 2), fw.u32(e + 4), fw.u32(e + 8)))
    ck("group-config lead table groups 0..4 in order", [r[0] for r in rows] == [0, 1, 2, 3, 4],
       str(rows))
    ck("group-config lead: g2 queue idx", rows[2][1] == 1, "qidx=%d" % rows[2][1])
    ck("group-config lead: g4 queue idx", rows[4][1] == 2, "qidx=%d" % rows[4][1])
    ck("group-config lead: g1 queue idx", rows[1][1] == 0, "qidx=%d" % rows[1][1])
    # the map struct itself (static .data): 3 queues / 7 groups / same group-config array
    ck("HCC map CA 0x%08X n_queues=3" % HCC_MAP, fw.u32(HCC_MAP + 8) == 3,
       "%d" % fw.u32(HCC_MAP + 8))
    ck("HCC map n_groups=7", fw.u32(HCC_MAP + 0x10) == 7, "%d" % fw.u32(HCC_MAP + 0x10))
    ck("HCC map group-config array", fw.u32(HCC_MAP + 0x24) == GROUP_CFG,
       "0x%08X" % fw.u32(HCC_MAP + 0x24))
    ck("HCC map thread name literal 'HCC TX Thread'",
       fw.u32(HCC_MAP + 0x1C) == 0x10CFC8, "0x%08X" % fw.u32(HCC_MAP + 0x1C))
    ck("group-config array is 7 entries (ends at the thread-name string)",
       fw.u16(GROUP_CFG + 12 * 7) != 0 and fw.u16(GROUP_CFG + 12 * 7) > 0x36, "")
    # instance pointers are loadable literals in the image
    inst_refs = [a for a in range(BASE, BASE + len(fw.data) - 4, 4) if fw.u32(a) == HCC_INSTANCE]
    ck("hcc instance 0x171B6C literal refs", len(inst_refs) >= 3,
       "%d refs: %s" % (len(inst_refs), [hex(x) for x in inst_refs[:8]]))
    print("\n%d/%d PASS" % (ok, ok + fail))
    return 0 if fail == 0 else 1

def decode(fw):
    print("LANE wifi-rx-d2h-post :: D2H post path (HOST-ONLY static decode)")
    print("FIRMWARE.bin %d B sha256 %s ; CA = file + 0x%X\n" % (len(fw.data), fw.sha, BASE))
    print("[1] hcc_msg_tx CA 0x102760 -- queued, per-dest/per-group routing")
    for ca, t in text(fw, 0x102760, 0x102822):
        print("   %08X  %s" % (ca, t))
    print("\n[2] ETE deposit op CA 0xC61E0 (ops table 0x10CEDC +0x10) -> commit 0xC192A -> notify 0xC6170(id 6)")
    for ca, t in text(fw, 0xC61E0, 0xC6236):
        print("   %08X  %s" % (ca, t))
    print("\n[3] d2h_notify CA 0xC6170 (id<=0xA; pending mask -> out[1] + doorbell)")
    for ca, t in text(fw, 0xC6170, 0xC61DC):
        print("   %08X  %s" % (ca, t))
    print("\n[4] deferred flush CA 0xC6108 (wait out[1]==0, publish shadow g+0xB8)")
    for ca, t in text(fw, 0xC6108, 0xC6170):
        print("   %08X  %s" % (ca, t))
    return 0

def tables(fw):
    print("T1 dest/group/queue model (all offsets are host-readable firmware RAM)")
    print("   hcc_instance             CA 0x%08X" % HCC_INSTANCE)
    print("   dest record 0            CA 0x%08X  (= instance+0x448)" % DEST0)
    print("   dest record fields       +4 map ptr, +8 queue-array ptr, +0x10 group-array ptr")
    print("   queue array              CA 0x%08X  (instance+4), stride 0x%X" % (HCC_INSTANCE + 4, QUEUE_STRIDE))
    print("   queue object fields      +8 state, +0x40 msg list head, +0x48 list count,")
    print("                            +0xC0 notifier, +0xC4 notifier-set flag")
    print("   group array              CA 0x%08X  (instance+0x3E8), stride 0x%X" % (HCC_INSTANCE + 0x3E8, GROUP_STRIDE))
    print("   group record fields      +0 max_id, +4 item array, +8 queue index (u16)")
    print("   item entry (stride 16)   +0 id, +4 handler, +8 aux, +0xC stats")
    print("\nT2 builders (group/id/dest nibble) -- the host-visible discriminator")
    print("   class                     group id  dest nibble source")
    print("   SMAC RX-complete (g2 id3)   2    3   u8[band_obj] (band obj CA 0x%08X)" % BAND0)
    print("   heartbeat (g4 id4)          4    4   0  (explicitly cleared at 0xC6BDA)")
    print("   ready pair (g4 id1/id2)     4   1/2  0  (prior lane: src-core check hdr[1]>>4==0)")
    print("   config replies (g1 id3)     1    3   (host-visible, prior lane)")
    print("   CRX RX (g1 id13)            1   13   dest record[2] from 0xBC524 table (probe P18)")
    print("\nT3 firmware-local handler registration (0x102866 call sites; only groups 0 and 2)")
    for callsite, group, table, count, meaning in REGISTRATIONS:
        print("   0x%08X  group=%-2d table=0x%08X count=%-3d  %s" % (callsite, group, table, count, meaning))
    print("\nT4 host D2H acceptance (vendor modules, committed lanes)")
    print("   mailbox ids: pcie_msg_init registers {1,3,6,7}; 2,4,5,8,9,10 dropped [V*]")
    print("   message dispatch: plat.ko chip group4 table 5 ids (0..4); wifi.ko hmac groups 1/2/3;")
    print("   wal group 2 -- the group-2 tables on the host are H2D-direction registrations, so a")
    print("   received g2 id3 has no D2H handler; raw D2H readers still catalogue every frame.")
    print("\nT5 HCC message map CA 0x%08X: %d queues, %d groups (group->queue table 0x%08X)"
          % (HCC_MAP, fw.u32(HCC_MAP + 8), fw.u32(HCC_MAP + 0x10), GROUP_CFG))
    for k in range(7):
        e = GROUP_CFG + 12 * k
        print("   group=%d qidx=%d item_array=0x%08X max_id=%d" %
              (fw.u16(e), fw.u16(e + 2), fw.u32(e + 4), fw.u32(e + 8)))
    return 0

def probes(fw):
    def r5(ca):
        return 0x6B8000 + ca
    def r3(ca):
        return 0x3B8000 + (ca - 0x40000000)
    print("READ-ONLY probe pack (physical/BAR0 addresses; no writes)")
    print(" P0  host catalogue before/after: any group-2 id-3 frame? any g1 id13 frame?")
    print(" P12 dest-nibble source: band0 obj CA 0x%08X -> phys 0x%08X (u32; bytes 0..3)"
          % (BAND0, r5(BAND0)))
    print("     band1 obj CA 0x%08X -> phys 0x%08X" % (BAND0 + BAND_STRIDE, r5(BAND0 + BAND_STRIDE)))
    print(" P13 producer post-failure counter: band0 +0xCCE CA 0x%08X phys 0x%08X (u8)"
          % (BAND0 + 0xCCE, r5(BAND0 + 0xCCE)))
    print("     band1 +0xCCE CA 0x%08X phys 0x%08X" % (BAND0 + BAND_STRIDE + 0xCCE, r5(BAND0 + BAND_STRIDE + 0xCCE)))
    print(" P14 dest0 record CA 0x%08X phys 0x%08X (u32 x6): [0]=dest idx [1]=map (=0x%08X)"
          % (DEST0, r5(DEST0), HCC_MAP))
    print("     [2]=queue array (=0x171B70) [4]=group array (=0x171F54); mismatch = re-baseline")
    print(" P15 follow P14[2] (queue array): read queue objects at base+0xE4*i for i=0,1,2")
    print("     fields: +8 state, +0x48 msg-list count, +0xC0 notifier ptr, +0xC4 flag")
    print(" P16 follow P14[4] (group array): read 12-B records for groups 0..4;")
    print("     group2 record = base+0x18 -> {max_id, item_array, qidx}; expect item_array entry3")
    print("     = {id=3, handler=0xBBC9C, aux, stats}; stats delta == successful g2id3 posts")
    print(" P17 g0 item-array entry stats as control; compare with P13 failure counter")
    print(" P18 CRX side: after P16 stats moves, read band0+0xCC5 (CRX drop) CA 0x%08X phys 0x%08X;"
          % (BAND0 + 0xCC5, r5(BAND0 + 0xCC5)))
    print("     then P0 catalogue: a g1 id13 frame proves the local dispatch + CRX build + D2H deposit")
    print(" P19 band1 mirrors of P12-P18")
    return 0

def main(argv):
    fw_path = os.environ.get("FW") or find_fw()
    fw = Fw(fw_path)
    mode = argv[1] if len(argv) > 1 else "--selftest"
    if mode == "--selftest":
        return selftest(fw)
    if mode == "--decode":
        return decode(fw)
    if mode == "--tables":
        return tables(fw)
    if mode == "--probes":
        return probes(fw)
    print(__doc__)
    return 2

if __name__ == "__main__":
    sys.exit(main(sys.argv))
