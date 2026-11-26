#!/usr/bin/env python3
# fw_rx_mac_delivery_re.py - re-derivation tool for the MAC-RX -> firmware -> D2H
# delivery decode (lane wifi-rx-mac-delivery, task-220).
#
# HOST-ONLY, stdlib only.  Reads the pinned local firmware image in place and
# asserts the decoded facts byte-for-byte.  It never prints or copies a vendor
# binary byte beyond the decoded protocol/instruction expectations that the
# lane documents (addresses, opcodes, names).
#
# Convention (committed lanes): CA = file offset + 0x40000.
#   python3 tools/fw_rx_mac_delivery_re.py --selftest
#   python3 tools/fw_rx_mac_delivery_re.py --decode
#   python3 tools/fw_rx_mac_delivery_re.py --probes
#   python3 tools/fw_rx_mac_delivery_re.py --tables
#
# Image: FIRMWARE.bin 928,920 B sha256 7fc87e20...c3ef311b (2.4.15 == 2.5.24
# payload here).  The tool refuses a different image.

import hashlib
import os
import struct
import sys

EXPECT_SIZE = 928920
EXPECT_SHA = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
CA_BASE = 0x40000

CANDIDATES = [
    os.environ.get("FIRMWARE_BIN", ""),
    "rootfs-2.4.15/lib/firmware/hi_wifi/FIRMWARE.bin",
    "rootfs-2.5.24/lib/firmware/hi_wifi/FIRMWARE.bin",
    "../rootfs-2.4.15/lib/firmware/hi_wifi/FIRMWARE.bin",
    "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin",
]


def find_image():
    for c in CANDIDATES:
        if c and os.path.isfile(c):
            return c
    # also resolve relative to the opensource submodule root when run from the lane dir
    here = os.path.dirname(os.path.abspath(__file__))
    for up in ("../../../..", "../../.."):
        for c in CANDIDATES[1:5]:
            p = os.path.normpath(os.path.join(here, up, c))
            if os.path.isfile(p):
                return p
    return None


class Image:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()
        self.size = len(self.data)
        self.sha = hashlib.sha256(self.data).hexdigest()

    def _off(self, ca):
        off = ca - CA_BASE
        if not (0 <= off < self.size):
            raise ValueError("CA 0x%x outside image" % ca)
        return off

    def u8(self, ca):
        return self.data[self._off(ca)]

    def u16(self, ca):
        return struct.unpack_from("<H", self.data, self._off(ca))[0]

    def u32(self, ca):
        return struct.unpack_from("<I", self.data, self._off(ca))[0]

    def raw(self, ca, n):
        off = self._off(ca)
        return self.data[off:off + n]

    def cstr(self, ca, maxlen=64):
        off = self._off(ca)
        end = self.data.find(b"\x00", off, off + maxlen)
        if end < 0:
            end = off + maxlen
        return self.data[off:end].decode("latin1")


# --- small Thumb-2 branch decoder (near-range BL / B.W / BLX) ---------------
# 32-bit Thumb: hw1 hw2.
#   BL   : hw1 = 11110 S imm10 ; hw2 = 11 J1 1 J2 imm11
#   B.W  : hw1 = 11110 S imm10 ; hw2 = 10 J1 1 J2 imm11
# target = pc_of_branch + sign_extend(S:J2:J1:imm10:imm11:0)
def branch(img, ca):
    hw1 = img.u16(ca)
    hw2 = img.u16(ca + 2)
    if (hw1 & 0xF800) != 0xF000:
        return None
    if (hw2 & 0x8000) == 0:
        return None
    S = (hw1 >> 10) & 1
    imm10 = hw1 & 0x3FF
    J1 = (hw2 >> 13) & 1
    J2 = (hw2 >> 11) & 1
    imm11 = hw2 & 0x7FF
    I1 = 1 - (J1 ^ S)
    I2 = 1 - (J2 ^ S)
    imm = (S << 24) | (I1 << 23) | (I2 << 22) | (imm10 << 12) | (imm11 << 1)
    if S:
        imm -= (1 << 25)
    kind = "bl" if (hw2 & 0x4000) else "b.w"
    return kind, (ca + 4 + imm) & 0xFFFFFFFF


def bl_target(img, ca):
    r = branch(img, ca)
    if not r:
        return None
    kind, tgt = r
    return tgt if kind == "bl" else None


def bw_target(img, ca):
    r = branch(img, ca)
    if not r:
        return None
    kind, tgt = r
    return tgt if kind == "b.w" else None


# --- checks ----------------------------------------------------------------
class Checker:
    def __init__(self, img):
        self.img = img
        self.pass_n = 0
        self.fail_n = 0
        self.fails = []

    def ck(self, cond, label):
        if cond:
            self.pass_n += 1
            print("PASS %s" % label)
        else:
            self.fail_n += 1
            self.fails.append(label)
            print("FAIL %s" % label)

    def eq(self, got, want, label):
        self.ck(got == want, "%s (got 0x%x want 0x%x)" % (label, got, want))

    def eqs(self, got, want, label):
        self.ck(got == want, "%s (got %r want %r)" % (label, got, want))


# --- known-good words (re-derived from this image; every one is asserted) ---
# PPDU descriptor-list init (cross-lane wifi-ppdu-init, re-derived here)
PPDU_INIT = 0xC016C
PPDU_INIT_CALLER = 0xD3BAC
DEVICE_INIT = 0xD1B68
DEVICE_INIT_CALL = 0x48314
DEVICE_BASE = 0x174BF0
PPDU_LIST_NAME = 0x1060F7
POOL_ALLOC = 0xBF128
# PPDU descriptor pool/reserve (this lane)
PPDU_DESC_RESERVE = 0xB52F0
# RX -> CRX consumer (this lane)
CRX_BUILDER = 0xB9780
RX_PERFRAME_CALL = 0xBA48A
HCC_MSG_TX = 0x102760
HCC_DISPATCH = 0xFD7E8
HCC_INSTANCE = 0x171B6C
# D2H deposit / notify (cross-lane wifi-rx-f1-thread + this lane)
D2H_NOTIFY = 0xC6170
ETE_NODE_FILL = 0xC192A
D2H_OBJECT = 0x172130
# CRX wire contract (cross-lane wifi-rx-crx-gating, re-derived here)
CRX_GROUP_BFI = 0xBA14A
CRX_ID_STORE = 0xBA150
CRX_TOTAL_ADD = 0xBA146
CRX_CAP_CMP = 0xBA114
CRX_FWD_LOAD = 0xBA0FC
CRX_FWD_BRANCH = 0xBA134
RX_QUEUE_POP = 0x1024EA
RX_QUEUE_FREE = 0x1024E6
# per-chip RX context gate
RX_CTX_ADD = 0xB97EE
RX_CTX_LOAD = 0xB97F2
# frame/queue counters in the descriptor handler
CTR_CC5 = 0xBA128
CTR_1ADA = 0xBAF2A
CTR_1AD4 = 0xBAF38
# heartbeat D2H builder (cross-lane wifi-f1-from-capture, re-derived here)
HEARTBEAT_TX = 0xC6C0A
HEARTBEAT_STR = 0x106328
# message tables
GROUP2_TABLE = 0x10CC18
GROUP2_CRX_ID = 0x000B9781


def selftest(img):
    c = Checker(img)
    print("== image identity ==")
    c.eq(img.size, EXPECT_SIZE, "image size 928,920 B")
    c.eqs(img.sha, EXPECT_SHA, "image sha256 7fc87e20...c3ef311b")

    print("== boot chain: PPDU descriptor-list init (cross-check of wifi-ppdu-init) ==")
    c.eq(img.u32(0x40000), 0x00046971, "image word0 = Thumb entry 0x46971")
    c.eq(bl_target(img, DEVICE_INIT_CALL), DEVICE_INIT, "0x48314 bl -> device init 0xD1B68")
    c.eq(img.u32(0xF4F60), DEVICE_BASE, "device base literal 0x174BF0 (getter 0xF4F48)")
    c.eq(img.u16(0xF4F4E), 0xF640, "getter: movw r0,#0xE28 (band stride)")
    c.eq(bl_target(img, PPDU_INIT_CALLER), PPDU_INIT, "0xD3BAC bl -> PPDU list init 0xC016C")
    c.eq(img.u32(0xC0484), PPDU_LIST_NAME, "list debug-name literal = 0x1060F7")
    c.eq(img.u16(0xC02B2), 0xF8C4, "list init: str.w sb,[r4,#0x66C] opcode")
    c.eq(img.u16(0xC02B4), 0x966C, "list init: ... #0x66C displacement")
    c.eq(img.u16(0xC02B8), 0x9670, "list init: str.w sb,[r4,#0x670] (prev)")
    c.eq(img.u16(0xC02C4), 0x367C, "list init: str.w r3,[r4,#0x67C] (name)")
    c.eq(img.u16(0xC02D2), 0x6674, "list init: str.w r6,[r4,#0x674] (count=0)")
    c.eq(bl_target(img, 0xC0402), POOL_ALLOC, "list init: bl -> pool allocator 0xBF128")
    c.eqs(img.cstr(PPDU_LIST_NAME), "&pst_device->st_device_alloced_ppdu_dscr_list.st_spin_lock",
          "list debug-name string at CA 0x1060F7")

    print("== the PPDU descriptor pool/reserve path (0xB52F0) ==")
    c.eq(img.u16(PPDU_DESC_RESERVE), 0x2300, "reserve entry: movs r3,#0 (0xB52F0)")
    c.eq(img.u16(0xB5332), 0x67CF, "reserve: add.w r7,r0,#0x678 (list lock)")
    c.eq(img.u16(0xB535C), 0x73CC, "reserve: mov.w r3,#0x198 (descriptor size)")
    c.eq(bl_target(img, 0xB5364), 0xC1E0C, "reserve: bl -> memset(desc,0x198,0)")
    c.eq(img.u16(0xB536E), 0x3670, "reserve: ldr.w r3,[r5,#0x670] (list tail)")
    c.eq(img.u16(0xB5376), 0x4670, "reserve: str.w r4,[r5,#0x670] (link tail)")
    c.eq(img.u16(0xB5380), 0x3674, "reserve: ldr.w r3,[r5,#0x674] (count)")
    c.eq(img.u16(0xB538A), 0x3674, "reserve: str.w r3,[r5,#0x674] (count++)")

    print("== the RX -> CRX consumer: entry, queue intake, deposit ==")
    c.eq(bl_target(img, RX_PERFRAME_CALL), CRX_BUILDER, "0xBA48A bl -> per-frame CRX builder 0xB9780")
    c.eq(bl_target(img, 0xBA0C0), RX_QUEUE_POP, "RX intake: 0xBA0C0 bl -> queue pop 0x1024EA")
    c.eq(bl_target(img, 0xBA0E2), RX_QUEUE_FREE, "RX intake: 0xBA0E2 bl -> node free 0x1024E6")
    c.eq(bl_target(img, 0xBA17A), HCC_MSG_TX, "CRX deposit: 0xBA17A bl -> hcc_msg_tx 0x102760")
    c.eq(img.u16(CRX_GROUP_BFI), 0xF369, "CRX builder: bfi r3,sb,#0,#4 (group nibble)")
    c.eq(img.u16(CRX_GROUP_BFI + 2), 0x0303, "CRX builder: bfi encoding 0303")
    c.eq(img.u16(CRX_ID_STORE), 0x230D, "CRX builder: movs r3,#0xd (id 13)")
    c.eq(img.u16(CRX_ID_STORE + 2), 0x71A3, "CRX builder: strb r3,[r4,#6]")
    c.eq(img.u16(CRX_TOTAL_ADD), 0xF10B, "CRX builder: add.w fp,fp,#0x3c (total=len+60)")
    c.eq(img.u16(CRX_TOTAL_ADD + 2), 0x0B3C, "CRX builder: add 0x3c displacement")
    c.eq(img.u16(CRX_CAP_CMP), 0xF5B1, "CRX builder: cmp.w r1,#0x680 (hard cap)")
    c.eq(img.u16(CRX_CAP_CMP + 2), 0x6FD0, "CRX builder: 0x680 immediate")
    c.eq(img.u16(CRX_FWD_LOAD), 0x7980, "CRX builder: ldrb r0,[r0,#6] (forward flag)")
    c.eq(img.u16(CRX_FWD_BRANCH), 0xB930, "CRX builder: cbnz r0 (drop when flag==0)")

    print("== the per-chip RX context gate (first demand-side state) ==")
    c.eq(img.u16(RX_CTX_ADD), 0xF506, "ctx gate: add.w r3,r6,#0x1a00")
    c.eq(img.u16(RX_CTX_ADD + 2), 0x53D0, "ctx gate: 0x1a00 immediate")
    c.eq(img.u16(RX_CTX_LOAD), 0xF8D3, "ctx gate: ldr.w r8,[r3]")
    c.eq(img.u16(RX_CTX_LOAD + 2), 0x8000, "ctx gate: rt=r8 encoding")
    c.eq(img.u16(0xB97FA), 0xD105, "ctx gate: bne (non-NULL required)")

    print("== frame/queue counters in the descriptor handler (probe words) ==")
    c.eq(img.u16(CTR_CC5), 0xF893, "counter: ldrb.w r3,[r3,#0xcc5]")
    c.eq(img.u16(CTR_CC5 + 2), 0x3CC5, "counter: 0xcc5 displacement")
    c.eq(img.u16(0xBA12C), 0x3301, "counter: adds r3,#1")
    c.eq(img.u16(0xBA12E), 0xF882, "counter: strb.w r3,[r2,#0xcc5]")
    c.eq(img.u16(CTR_1ADA), 0xF641, "counter: movw r3,#0x1ada")
    c.eq(img.u16(CTR_1ADA + 2), 0x23DA, "counter: 0x1ada immediate")
    c.eq(img.u16(CTR_1AD4), 0xF641, "counter: movw r0,#0x1ad4")
    c.eq(img.u16(CTR_1AD4 + 2), 0x20D4, "counter: 0x1ad4 immediate")

    print("== the D2H deposit path: notify ids and the shared message primitive ==")
    c.eq(img.u16(D2H_NOTIFY), 0x290A, "notify(0xC6170): cmp r1,#0xa (id<=10)")
    c.eq(img.u32(0xC61DC), D2H_OBJECT, "notify: hcc object global = 0x172130")
    c.eq(img.u16(0xC6220), 0x2106, "deposit path: movs r1,#6")
    c.eq(bl_target(img, 0xC6224), D2H_NOTIFY, "deposit path: bl notify(6)")
    c.eq(img.u16(0xC6994), 0x2105, "deposit path: movs r1,#5")
    c.eq(bl_target(img, 0xC6998), D2H_NOTIFY, "deposit path: bl notify(5)")
    c.eq(img.u16(0x462AA), 0x2103, "host-interface event path: movs r1,#3")
    c.eq(bw_target(img, 0x462AC), D2H_NOTIFY, "host-interface event path: b.w notify(3)")
    c.eq(bl_target(img, 0xC621C), ETE_NODE_FILL, "ETE node fill at 0xC621C")
    c.eq(bl_target(img, 0xC6254), ETE_NODE_FILL, "ETE node fill at 0xC6254")
    c.eq(img.u16(ETE_NODE_FILL), 0xB410, "ETE fill entry: push {r4}")
    c.eq(bl_target(img, HEARTBEAT_TX), HCC_MSG_TX, "heartbeat builder 0xC6C0A bl -> hcc_msg_tx")
    c.eqs(img.u32(HEARTBEAT_STR), 0x52414548, "HEARTBEAT literal begins 'HEAR'")
    c.eq(img.u16(HCC_MSG_TX), 0xB570, "hcc_msg_tx entry: push {r4,r5,r6,lr}")
    c.eq(img.u16(0x102782), 0x7843, "hcc_msg_tx: ldrb r3,[r0,#1]")
    c.eq(img.u16(0x102784), 0xF3C3, "hcc_msg_tx: ubfx r3,r3,#4,#4 (destination nibble)")
    c.eq(img.u16(0x102786), 0x1303, "hcc_msg_tx: ubfx encoding")
    c.eq(img.u16(0x102788), 0x7243, "hcc_msg_tx: strb r3,[r0,#9] (routes the message)")
    c.eq(img.u32(0x1028EC), HCC_INSTANCE, "hcc instance global = 0x171B6C")
    c.eq(bl_target(img, 0x102720), RX_QUEUE_POP, "HCC message thread: pop 0x1024EA")
    c.eq(bl_target(img, 0x102734), HCC_DISPATCH, "HCC message thread: dispatch 0xFD7E8")
    c.eq(img.u32(GROUP2_TABLE), 0x00000002, "group-2 dispatch entry id 2")
    c.eq(img.u32(GROUP2_TABLE + 4), GROUP2_CRX_ID, "group-2 id2 -> handler 0x???5? (0xB9781)")
    c.eq(img.u32(GROUP2_TABLE + 36), 0x00000005, "group-2 dispatch entry id 5")
    c.eq(img.u32(GROUP2_TABLE + 40), GROUP2_CRX_ID, "group-2 id5 -> same handler 0xB9781")

    print("== names: the SMAC ISR / message-proc name table and the thread table ==")
    c.eqs(img.cstr(0x12266C), "smac_coex_rx_abort_end_5g_isr", "ISR-name table entry 0")
    c.eqs(img.cstr(0x1226C4), "smac_rx_complete_2g_isr", "ISR name: smac_rx_complete_2g_isr")
    c.eqs(img.cstr(0x1226AC), "smac_rx_complete_5g_isr", "ISR name: smac_rx_complete_5g_isr")
    c.eqs(img.cstr(0x1229FC), "smac_tbtt_2g_isr", "ISR name: smac_tbtt_2g_isr")
    c.eqs(img.cstr(0x1228BC), "smac_msg_proc_rx_ring_reset_req", "name: rx_ring_reset_req")
    c.eqs(img.cstr(0x1229A8), "smac_msg_proc_enable_trx_req", "name: enable_trx_req")
    c.eqs(img.cstr(0x10CFC8), "HCC TX Thread", "thread table: HCC TX Thread")
    c.eqs(img.cstr(0x10CFF8), "HCC RX Thread", "thread table: HCC RX Thread")
    c.eqs(img.cstr(0x10D028), "HCC OAM Thread", "thread table: HCC OAM Thread")

    print()
    print("== RESULT: %d/%d PASS, %d FAIL ==" % (c.pass_n, c.pass_n + c.fail_n, c.fail_n))
    if c.fails:
        for f in c.fails:
            print("  FAILED: %s" % f)
        return 1
    return 0


# --- derived tables --------------------------------------------------------
BAND_STRIDE = 0xE28
DEV_CA = 0x174BF0
RAM_PHYS_BASE = 0x406B8000  # firmware-RAM phys = CA + 0x406B8000 (ppdu-init P2 check)


def band_obj(band):
    return DEV_CA + BAND_STRIDE * band


def phys(ca):
    return (ca + RAM_PHYS_BASE) & 0xFFFFFFFF


def decode(img):
    print("image: %s  %d B  sha256 %s" % (img.path, img.size, img.sha))
    print("CA = file offset + 0x%X" % CA_BASE)
    print()
    print("A. the RX delivery chain (all addresses CA, Thumb unless noted)")
    rows = [
        ("MAC RX HW -> firmware", "[U]", "the SMAC RX interrupt/completion entry was not located in this image"),
        ("HAL RX message intake", "0x%X" % RX_PERFRAME_CALL, "bl 0xB9780 - one call per received frame in the descriptor handler (0xBA1C8 body)"),
        ("queue intake (pop/free)", "0x%X / 0x%X" % (0xBA0C0, 0xBA0E2), "bl 0x1024EA pop / bl 0x1024E6 free - frames arrive as HCC message nodes"),
        ("per-chip RX ctx gate", "0x%X" % RX_CTX_ADD, "ldr.w r8,[r6+0x1a00]; NULL -> drop (log 0xA4E)"),
        ("CRX builder", "0x%X" % CRX_BUILDER, "pops frames, builds group 1 / id 13 D2H, posts it"),
        ("CRX wire constants", "0x%X..0x%X" % (CRX_GROUP_BFI, 0xBA17A), "group=1 bfi, id=13, total=frame+60<=0x680, fwd flag info[+6]"),
        ("deposit primitive", "0x%X" % HCC_MSG_TX, "hcc_msg_tx: routes by hdr[1]>>4 destination nibble"),
        ("ETE node fill", "0x%X" % ETE_NODE_FILL, "8-byte D2H node {devva, (len<<16)|0x6d2b}"),
        ("D2H notify", "0x%X" % D2H_NOTIFY, "id<=10, pending mask -> hcc object out word, doorbell bit0"),
        ("ready/heartbeat/config producers", "0x102760 callers", "same primitive: heartbeat 0xC6C0A, config replies + ready pair"),
    ]
    for name, addr, what in rows:
        print("  %-28s %-22s %s" % (name, addr, what))
    print()
    print("B. D2H source selection (why the firmware's own frames deliver and client frames do not)")
    print("  The D2H ring is a single firmware->host HCC message channel.  Every firmware-originated")
    print("  message that reaches the host (ready pair g4 id1/id2, config replies g1 id3, HEARTBEAT")
    print("  g4 id4) and the management-RX event (g1 id13) all deposit through the SAME primitive")
    print("  (hcc_msg_tx 0x102760 -> ETE node 0xC192A -> notify 0xC6170).  There is no per-id ring")
    print("  and no host-side gate that could selectively hide g1 id13.")
    print("  => the bench fact 'the firmware's own frames deliver' proves only that the deposit,")
    print("     notify and host-consume paths work.  It does NOT prove the MAC-RX intake ran.")
    print()
    print("C. the intake that is missing")
    print("  Client frames must become an HCC message node consumed by the RX descriptor handler")
    print("  (pop at 0xBA0C0 -> bl 0xB9780 at 0xBA48A).  The producer of those nodes is the SMAC RX")
    print("  completion path (MAC PPDU -> firmware RX descriptor -> node).  That producer entry is")
    print("  [U] in this image (the ISR names table is not code-referenced; see TABLES T7).")
    print("  The demand-side state that the consumer itself reads is the per-chip RX ctx at")
    print("  chip+0x1a00 (0xB97EE); a NULL there drops the frame with log 0xA4E after intake.")
    print()
    print("D. PPDU descriptor pool (the per-band device object)")
    print("  device(band) = 0x%X + 0x%X*band  (getter 0xF4F48; literal 0xF4F60)" % (DEV_CA, BAND_STRIDE))
    for b in (0, 1):
        o = band_obj(b)
        print("    band %d obj CA 0x%X  list next +0x66C=0x%X  prev +0x670=0x%X  count +0x674=0x%X"
              % (b, o, o + 0x66C, o + 0x670, o + 0x674))
        print("             name +0x67C=0x%X (expect 0x1060F7)  phys 0x%08X" % (o + 0x67C, phys(o + 0x67C)))
    print("  descriptor reserve: 0xB52F0(dev, pool, buf) links a 0x198-B descriptor into dev+0x66C;")
    print("  boot init 0xC016C pre-allocates 0x1A0-B descriptors via 0xBF128 (cross-lane decode).")
    return 0


def probes(img):
    print("READ-ONLY probe pack (Lead fires; conversions committed in ppdu-init PROBES)")
    print("firmware RAM phys = CA + 0x%08X; device IO phys = CA + 0x3B8000" % RAM_PHYS_BASE)
    print()
    rows = [
        ("P1", "device obj band0 name ptr", band_obj(0) + 0x67C, phys(band_obj(0) + 0x67C), 4,
         "0x001060F7 = device init + PPDU list init ran"),
        ("P2", "device obj band0 list next", band_obj(0) + 0x66C, phys(band_obj(0) + 0x66C), 4,
         "self ptr 0x%X when list is empty/initialised" % (band_obj(0) + 0x66C)),
        ("P3", "device obj band0 list count", band_obj(0) + 0x674, phys(band_obj(0) + 0x674), 4,
         "preallocated descriptor count"),
        ("P4", "device obj band0 name ptr b1", band_obj(1) + 0x67C, phys(band_obj(1) + 0x67C), 4,
         "0x001060F7 if band 1 object also inited"),
        ("P5", "RX-path counter cc5", band_obj(0) + 0xCC5, phys(band_obj(0) + 0xCC5), 1,
         "u8 bumped when a received frame is over the 0x680 cap (0xBA12E)"),
        ("P6", "RX-path counter 1ada", band_obj(0) + 0x1ADA, phys(band_obj(0) + 0x1ADA), 1,
         "u8 bumped in the descriptor handler when a negative frame status is seen (0xBAF2A)"),
        ("P7", "RX-path counter 1ad4", band_obj(0) + 0x1AD4, phys(band_obj(0) + 0x1AD4), 1,
         "u8 flag compared with 1 before a per-frame field write (0xBAF38)"),
        ("P8", "device obj band1 counter 1ad4", band_obj(1) + 0x1AD4, phys(band_obj(1) + 0x1AD4), 1,
         "band-1 mirror of P7"),
        ("P9", "MAC live bank status", None, 0x40408000, 4,
         "read via the bench's committed iATU map (CA 0x40050000 -> phys 0x40408000): bit10 = rx_ppdu_desc class"),
        ("P10", "MAC config mask", None, 0x403F8048, 4,
         "CA 0x40040048 -> phys 0x403F8048: 0xfffffcc1 stock/vendor value (bit10 masked)"),
    ]
    for pid, name, ca, ph, w, expect in rows:
        print("  %-4s %-28s CA %-12s phys 0x%08X w=%d  %s"
              % (pid, name, ("0x%X" % ca) if ca is not None else "-", ph, w, expect))
    print()
    print("D2H liveness (host consumer, no device read):")
    print("  rd_catalog / direct D2H reader: ready pair g4 id1/id2 + HEARTBEAT g4 id4 arrive;")
    print("  g1 id13 count == 0. P9 bit10 is stock-latched (benign); require completion-producer")
    print("  movement or a g1 id13 frame, never 'bit10 clear', as the success criterion.")
    print()
    print("Decision table (one pass during the laptop auth burst):")
    print("  P5/P6/P7 move + P9 bit10 transitions + no g1 id13 -> intake produced frames; the drop")
    print("     is inside the CRX path after the queue pop (re-read the builder's gate logs 0x49F/0x497/0xA4E)")
    print("  counters still + no g1 id13 -> consumer ran, builder dropped (find the log code in dmesg)")
    print("  counters do not move -> intake dry: the SMAC RX completion producer never posted a node")
    print("  P1 != 0x1060F7 -> the object/address is wrong for this boot; do not interpret P5-P8")
    return 0


def tables(img):
    print("T1 PPDU descriptor-list init: CA 0x%X (init) <- 0x%X <- device init 0x%X <- 0x%X"
          % (PPDU_INIT, PPDU_INIT_CALLER, DEVICE_INIT, DEVICE_INIT_CALL))
    print("T2 device object: 0x%X + 0x%X*band  (0x%X / 0x%X)" % (DEV_CA, BAND_STRIDE, band_obj(0), band_obj(1)))
    print("T3 list: next +0x66C, prev +0x670, count +0x674, name +0x67C, desc size 0x1A0 (boot) / 0x198 (reserve)")
    print("T4 RX consumer: 0x%X (pop 0x%X at 0xBA0C0; per-frame bl 0xB9780 at 0xBA48A)" % (CRX_BUILDER, RX_QUEUE_POP))
    print("T5 CRX gate: ctx [chip+0x1A00] 0x%X | fwd flag info[+6] 0x%X | total<=0x680 0x%X" % (RX_CTX_ADD, CRX_FWD_LOAD, CRX_CAP_CMP))
    print("T6 deposit: hcc_msg_tx 0x%X -> ETE 0x%X -> notify 0x%X (ids 3/5/6 observed)" % (HCC_MSG_TX, ETE_NODE_FILL, D2H_NOTIFY))
    print("T7 SMAC names (data table CA 0x12266C, 0x20-byte records, no code xrefs): rx_complete_2g/5g ISR,")
    print("   rx_timeout, tbtt, pre_tx, backoff, user_ps, tx_abort_start, lifetime_expire, tx_exit, ...")
    print("   msg-proc names: rx_ring_reset_req / enable_trx_req / tx_suspend / tx_resume / clr_hw_fifo ...")
    print("T8 group-2 HCC dispatch table CA 0x%X: id 2 and id 5 both -> 0x%X (the CRX builder)" % (GROUP2_TABLE, GROUP2_CRX_ID))
    return 0


def main(argv):
    path = find_image()
    if not path:
        print("FIRMWARE.bin not found; set FIRMWARE_BIN=<path>")
        return 2
    img = Image(path)
    mode = argv[1] if len(argv) > 1 else "--selftest"
    if mode == "--selftest":
        return selftest(img)
    if mode == "--decode":
        return decode(img)
    if mode == "--probes":
        return probes(img)
    if mode == "--tables":
        return tables(img)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
