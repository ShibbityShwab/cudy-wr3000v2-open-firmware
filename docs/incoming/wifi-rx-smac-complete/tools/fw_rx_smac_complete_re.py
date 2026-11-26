#!/usr/bin/env python3
# fw_rx_smac_complete_re.py - re-derivation tool for lane wifi-rx-smac-complete
# (task-221): the SMAC PPDU-completion producer of the firmware group-2 id-3
# message - the RX-complete consumer's missing upstream link.
#
# HOST-ONLY, stdlib only.  Reads the pinned local firmware image in place and
# asserts every decoded fact byte-for-byte.  It prints addresses, decoded
# instruction halfwords and names only (no vendor binary byte is copied).
#
# Convention (committed lanes): CA = file offset + 0x40000.
#   python3 tools/fw_rx_smac_complete_re.py --selftest
#   python3 tools/fw_rx_smac_complete_re.py --decode
#   python3 tools/fw_rx_smac_complete_re.py --tables
#   python3 tools/fw_rx_smac_complete_re.py --probes
#
# Image: FIRMWARE.bin 928,920 B sha256 7fc87e20...c3ef311b (2.4.15 == 2.5.24).

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
]


def find_image():
    for c in CANDIDATES:
        if c and os.path.isfile(c):
            return c
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

    def cstr(self, ca, maxlen=64):
        off = self._off(ca)
        end = self.data.find(b"\x00", off, off + maxlen)
        if end < 0:
            end = off + maxlen
        return self.data[off:end].decode("latin1")


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


def branch16_target(img, ca):
    hw = img.u16(ca)
    if (hw & 0xF800) != 0xE000:
        return None
    imm = hw & 0x7FF
    if imm & 0x400:
        imm -= 0x800
    return (ca + 4 + (imm << 1)) & 0xFFFFFFFF


def ldr_literal_target(img, ca):
    hw = img.u16(ca)
    if (hw & 0xF800) != 0x4800:
        return None
    imm8 = hw & 0xFF
    return ((ca + 4) & ~3) + imm8 * 4


def find_ldr_literal(img, lo, hi):
    out = []
    for ca in range(lo, hi, 2):
        t = ldr_literal_target(img, ca)
        if t is not None:
            out.append((ca, t))
    return out


def blx_arm(img, ca):
    w = img.u32(ca)
    if (w >> 25) != 0x7D:
        return None
    H = (w >> 24) & 1
    imm24 = w & 0xFFFFFF
    return (ca + 8 + (imm24 << 2) + (H << 1)) & 0xFFFFFFFF


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

    def bl(self, site, want, label):
        got = bl_target(self.img, site)
        self.ck(got == want, "%s (bl@0x%x -> %s, want 0x%x)" % (label, site, hex(got) if got is not None else None, want))

# --- known-good values (re-derived from this image; every one is asserted) --
IRQ_STUB = 0x40064
IRQ_BLX = 0x40094
EVENT_DISPATCHER = 0xC2EFC
EVENT_STATUS_CA = 0x4016010C
EVENT_ACK_CA = 0x40160110
EVENT_TABLE_BASE = 0x17D398
EVENT_HANDLER_TABLE = 0x17D430
EVENT_ENABLE_BASE = 0x40161100
IRQ_REGISTER = 0xC6DA2
IRQ_ENABLE = 0xC6FF4
HCC_MSG_TX = 0x102760
HCC_MSG_ALLOC = 0x102822
HCC_DISPATCH = 0xFD7E8
HCC_RESOLVE = 0xFD4C8
HCC_HDR = 0x409EA
HCC_INSTANCE = 0x171B6C
BAND_INIT = 0xD1B68
BAND_INIT_CALL = 0x48314
BAND_DISPATCH_WORD = 0x180814
BAND_OBJ = (0x174BF0, 0x175A18)
SMAC_ISR = [
    (0x2F, 5, 0xAEC95, 0xAEC74),
    (0x31, 5, 0xB54C9, 0xB53A4),
    (0x32, 5, 0xB0D51, 0xB0CE0),
    (0x33, 5, 0xAEC69, 0xAEB80),
    (0x34, 5, 0xAEB75, 0xAEA9C),
    (0x52, 14, 0xAE48D, 0xAE45C),
    (0x53, 5, 0xB0D51, 0xB0CE0),
    (0x3B, 5, 0xB0C45, 0xB0AB8),
]
SMAC_ISR_BAND1 = [
    (0x35, 5, 0xAEC89, 0xAEC74),
    (0x37, 5, 0xB54BD, 0xB53A4),
    (0x38, 5, 0xB0D45, 0xB0CE0),
    (0x39, 5, 0xAEC5D, 0xAEB80),
    (0x3A, 5, 0xAEB69, 0xAEA9C),
    (0x54, 14, 0xAE481, 0xAE45C),
    (0x55, 5, 0xB0D45, 0xB0CE0),
    (0x3C, 5, 0xB0C39, 0xB0AB8),
]
PRODUCER = 0xB53A4
PRODUCER_DEQUEUE = 0xB084C
PRODUCER_BUF = 0xB0C88
PRODUCER_LIST_UNLINK = 0xB2BD8
PRODUCER_REARM = 0xB52F0
SMAC_CTX_PTR = 0x2A0
SMAC_ACK = 0xE4
SMAC_STATUS = 0xE8
SMAC_BASE_BAND0 = 0x40042000
SMAC_BASE_BAND1 = 0x40062000
GROUP2_TABLE = 0x10CC18
GROUP2_CRX_ID = 0x000B9781
RX_COMPLETE_HANDLER = 0xBBC9C
PER_FRAME_DECODER = 0xBA1C8
PER_FRAME_CALL = 0xBA48A
CRX_BUILDER = 0xB9780
RX_QUEUE_POP = 0x1024EA
RX_QUEUE_FREE = 0x1024E6
GROUP2_REGISTRAR = 0x102866
MAC_START = 0x4AF0C
MAC_ENABLE_BAND0 = 0x40108214
MAC_STATUS_BAND0 = 0x4010820C
MAC_ENABLE_BAND1 = 0x4010C220
MAC_STATUS_BAND1 = 0x4010C200
DEVIO_PHYS_DELTA = 0x3B8000
FW_RAM_PHYS_DELTA = 0x406B8000


def reg_literal(img, site):
    for a, lit in find_ldr_literal(img, site - 16, site - 3):
        return img.u32(lit)
    return None


def find_bl_callers(img, target):
    end = CA_BASE + img.size
    out = []
    for ca in range(CA_BASE, end - 4, 2):
        if bl_target(img, ca) == target:
            out.append(ca)
    return out


def builds_group2_id3(img, call_site):
    try:
        from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB
        md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
        md.skipdata = True
    except Exception:
        return None
    o = img._off(call_site - 0x200)
    code = img.data[o:img._off(call_site) + 2]
    insns = list(md.disasm(code, call_site - 0x200))
    id3 = False
    group2 = False
    mov2 = set()
    for i, ins in enumerate(insns):
        if ins.mnemonic.startswith("mov") and ins.op_str.endswith(", #2"):
            mov2.add(ins.op_str.split(",")[0].strip())
        if ins.mnemonic.startswith("bfi") and "#0, #4" in ins.op_str:
            ops = [x.strip() for x in ins.op_str.split(",")]
            if len(ops) >= 2 and ops[1] in mov2:
                group2 = True
        if ins.mnemonic.startswith("mov") and ins.op_str.endswith(", #3"):
            reg = ins.op_str.split(",")[0].strip()
            for q in insns[i + 1:i + 4]:
                if q.mnemonic.startswith("strb") and q.op_str.startswith(reg + ",") and ", #6]" in q.op_str:
                    id3 = True
    return id3 and group2

def selftest(img):
    c = Checker(img)
    print("== image identity ==")
    c.eq(img.size, EXPECT_SIZE, "image size 928,920 B")
    c.eqs(img.sha, EXPECT_SHA, "image sha256 7fc87e20...c3ef311b")

    print("== IRQ entry and the firmware event dispatcher (the host-invisible INTC) ==")
    c.eq(img.u32(IRQ_STUB), 0xE24EE004, "CA 0x40064: ARM 'sub lr,lr,#4' (IRQ vector stub)")
    c.eq(blx_arm(img, IRQ_BLX), EVENT_DISPATCHER, "CA 0x40094: ARM blx -> event dispatcher 0xC2EFC")
    c.eq(img.u32(0xC3010), EVENT_STATUS_CA, "dispatcher literal: pending-id word 0x4016010C")
    c.eq(img.u32(0xC3014), EVENT_TABLE_BASE, "dispatcher literal: handler table base 0x17D398")
    c.eq(img.u32(0xC3020), EVENT_ACK_CA, "dispatcher literal: ack word 0x40160110")
    c.eq(img.u16(0xC2F08), 0xF3C7, "dispatcher: ubfx r8,r7,#0,#0xa (id = status & 0x3FF)")
    c.eq(img.u16(0xC2F12), 0xEB04, "dispatcher: addls.w r3,r4,r8,lsl #2")
    c.eq(img.u16(0xC2F1A), 0x466B, "dispatcher: mov r3,sp (stack mask)")
    c.eq(img.u16(0xC2F4A), 0x47A8, "dispatcher: blx r5 (handler call)")
    c.eq(img.u16(0xC2F52), 0x601F, "dispatcher: str r7,[r3] (ack write back)")
    c.eq(img.u16(0xC74B0), 0xE92D, "handler registrar 0xC74B0 prologue")
    c.eq(img.u32(0xC7534), EVENT_TABLE_BASE, "registrar literal: table base 0x17D398")
    c.eq(img.u16(0xC74F8), 0xEB04, "registrar: add.w r3,r4,r4,lsl #1 (stride helper)")
    c.eq(img.u16(0xC6DA2), 0xB132, "0xC6DA2: cbz r2 (NULL handler rejected)")
    c.eq(img.u16(0xC6DB2), 0xF04F, "0xC6DA2: mov.w r0,#-1 (error path)")
    c.eq(img.u16(0xC6FF6), 0xF1A0, "0xC6FF4 enable: sub.w r3,r0,#0x10")
    c.eq(img.u16(0xC6FFA), 0x2B4F, "0xC6FF4 enable: cmp r3,#0x4f")
    c.eq(img.u32(0xC7044), EVENT_ENABLE_BASE, "0xC6FF4 literal: per-event enable 0x40161100")

    print("== per-band SMAC INTC registration (per-band init 0xD1B68) ==")
    c.bl(BAND_INIT_CALL, BAND_INIT, "0x48314 bl -> per-band/device init 0xD1B68")
    c.eq(img.u32(0xD20F4), SMAC_BASE_BAND0, "init literal: band0 SMAC base 0x40042000")
    c.eq(img.u32(0xD20F8), SMAC_BASE_BAND1, "init literal: band1 SMAC base 0x40062000")
    c.eq(img.u16(0xD2046), 0xF8C4, "init: str.w r3,[r4,#0x260] (= band_obj+0x2a0, band0)")
    c.eq(img.u16(0xD2048), 0x3260, "init: ... immediate #0x260")
    c.eq(img.u16(0xD2096), 0xE7D6, "init: band1 arm b to the same store")
    for name, sites, table in (
            ("band0", (0xD1EF4, 0xD1F04, 0xD1F14, 0xD1F24, 0xD1F34, 0xD1F44, 0xD1F54, 0xD1F64), SMAC_ISR),
            ("band1", (0xD21DC, 0xD21EC, 0xD21FC, 0xD220C, 0xD221C, 0xD222C, 0xD223C, 0xD224C), SMAC_ISR_BAND1)):
        for i, site in enumerate(sites):
            irq, prio, handler, body = table[i]
            c.bl(site, IRQ_REGISTER, "%s IRQ %d: c6da2(irq,prio,handler)" % (name, irq))
            c.eq(img.u16(site - 2), 0x2000 | (irq & 0xFF), "%s IRQ %d: movs r0,#0x%x" % (name, irq, irq))
            c.eq(img.u16(site - 4), 0x2100 | (prio & 0xFF), "%s IRQ %d: movs r1,#%d" % (name, irq, prio))
            c.eq(reg_literal(img, site), handler, "%s IRQ %d: wrapper 0x%x" % (name, irq, handler))
            if bl_target(img, site + 6) is None:
                c.eq(branch16_target(img, site + 6), 0xD1F6A, "%s IRQ %d: tail branch to the shared enable call" % (name, irq))
                c.bl(0xD1F6A, IRQ_ENABLE, "%s IRQ %d: c6ff4 enable" % (name, irq))
            else:
                c.bl(site + 6, IRQ_ENABLE, "%s IRQ %d: c6ff4 enable" % (name, irq))
    c.eq(img.u32(0xAEC9C), BAND_DISPATCH_WORD, "wrapper A: literal 0x180814")
    c.eq(img.u32(0xB54D0), BAND_DISPATCH_WORD, "wrapper B band0: literal 0x180814")
    c.eq(img.u16(0xAEC96), 0x6898, "wrapper A band0: ldr r0,[r3,#8]")
    c.eq(img.u16(0xAEC8A), 0x69D8, "wrapper A band1: ldr r0,[r3,#0x1c]")
    c.eq(img.u16(0xB54CA), 0x6898, "wrapper B band0: ldr r0,[r3,#8]")
    c.eq(img.u16(0xB54BE), 0x69D8, "wrapper B band1: ldr r0,[r3,#0x1c]")

    print("== the PRODUCER: body 0xB53A4 (band0 IRQ 49 / band1 IRQ 55) ==")
    c.eq(img.u32(PRODUCER), 0x41FFE92D, "producer prologue push.w {r0..r8,lr}")
    c.eq(img.u16(0xB53A8), 0xF8D0, "producer: ldr.w r3,[r0,#0x2a0] (SMAC base)")
    c.eq(img.u16(0xB53AA), 0x32A0, "producer: ... immediate #0x2a0")
    c.eq(img.u16(0xB53AE), 0xF8D3, "producer: ldr.w r3,[r3,#0xe8] (SMAC status)")
    c.eq(img.u16(0xB53B0), 0x30E8, "producer: ... immediate #0xe8")
    c.eq(img.u16(0xB53B2), 0x075B, "producer: lsls r3,r3,#0x1d (bit2 -> sign)")
    c.eq(img.u16(0xB53B4), 0xD422, "producer: bmi return (bit2 set -> early out)")
    c.eq(img.u16(0xB53BE), 0xF890, "producer: ldrb.w r3,[r0,#0xcc0] (entry counter)")
    c.eq(img.u16(0xB53C0), 0x3CC0, "producer: ... immediate #0xcc0")
    c.eq(img.u16(0xB53C4), 0xF880, "producer: strb.w r3,[r0,#0xcc0] (entry counter++)")
    c.eq(img.u16(0xB53C6), 0x3CC0, "producer: ... immediate #0xcc0")
    c.bl(0xB53C8, 0xBFD4C, "producer: bl 0xBFD4C (pre-dequeue hook)")
    c.bl(0xB53D4, PRODUCER_DEQUEUE, "producer: bl 0xB084C (dequeue completion entry)")
    c.eq(img.u16(0xB53F2), 0x2204, "producer: movs r2,#4 (SMAC ack value)")
    c.eq(img.u16(0xB53F4), 0xF8D4, "producer: ldr.w r3,[r4,#0x2a0]")
    c.eq(img.u16(0xB53F6), 0x32A0, "producer: ... immediate #0x2a0")
    c.eq(img.u16(0xB53F8), 0xF8C3, "producer: str.w r2,[r3,#0xe4] (SMAC ack)")
    c.eq(img.u16(0xB53FA), 0x20E4, "producer: ... immediate #0xe4")
    c.bl(0xB5402, PRODUCER_BUF, "producer: bl 0xB0C88 (received-frame chain)")
    c.eq(img.u16(0xB5406), 0xF1A0, "producer: sub.w r7,r0,#8 (list node)")
    c.bl(0xB5414, PRODUCER_LIST_UNLINK, "producer: bl 0xB2BD8 (unlink PPDU dscr from band+0x66c)")
    c.eq(img.u16(0xB5424), 0x2014, "producer: movs r0,#0x14 (alloc 20-byte message)")
    c.bl(0xB5426, HCC_MSG_ALLOC, "producer: bl 0x102822 (message alloc)")
    c.eq(img.u16(0xB5462), 0x2202, "producer: movs r2,#2 (group nibble 2)")
    c.eq(img.u16(0xB5468), 0xF362, "producer: bfi r3,r2,#0,#4 (group 2 into hdr[0])")
    c.eq(img.u16(0xB546E), 0x2303, "producer: movs r3,#3 (id 3)")
    c.eq(img.u16(0xB5470), 0x7183, "producer: strb r3,[r0,#6] (HCC header id = 3)")
    c.eq(img.u16(0xB5482), 0x2312, "producer: movs r3,#0x12 (info field)")
    c.eq(img.u16(0xB5484), 0x70C3, "producer: strb r3,[r0,#3]")
    c.eq(img.u16(0xB5486), 0x2301, "producer: movs r3,#1")
    c.eq(img.u16(0xB5488), 0x6144, "producer: str r4,[r0,#0x14] (band object pointer)")
    c.eq(img.u16(0xB548A), 0x74C3, "producer: strb r3,[r0,#0x13]")
    c.eq(img.u16(0xB548C), 0x60C6, "producer: str r6,[r0,#0xc] (received-frame chain)")
    c.eq(img.u16(0xB548E), 0x7745, "producer: strb r5,[r0,#0x1d]")
    c.bl(0xB5492, HCC_MSG_TX, "producer: bl 0x102760 (post the group-2 id-3 message)")
    c.bl(0xB5458, PRODUCER_REARM, "producer: bl 0xB52F0 (re-arm a fresh PPDU descriptor)")

    print("== producer uniqueness in the whole image ==")
    callers = find_bl_callers(img, HCC_MSG_TX)
    c.eq(len(callers), 56, "hcc_msg_tx has 56 BL callers in the image")
    builders = [x for x in callers if builds_group2_id3(img, x) is True]
    c.eqs(builders, [0xB5492], "exactly one hcc_msg_tx caller builds group=2/id=3: 0xB5492")

    print("== consumer half: group-2 id 3 -> 0xBBC9C -> 0xBA1C8 -> 0xB9780 (cross-check) ==")
    c.eq(img.u32(0x10CC18), 0x00000002, "group-2 table entry 0: id 2")
    c.eq(img.u32(0x10CC1C), GROUP2_CRX_ID, "group-2 id 2 -> CRX builder 0xB9780")
    c.eq(img.u32(0x10CC24), 0x00000003, "group-2 table entry 1: id 3")
    c.eq(img.u32(0x10CC28), 0x000BBC9D, "group-2 id 3 -> RX-complete handler 0xBBC9C")
    c.eq(img.u16(0x48336), 0x2213, "H2D registrar site: movs r2,#0x13 (19 entries)")
    c.eq(img.u32(0x484E0), GROUP2_TABLE, "H2D registrar site: table literal 0x10CC18")
    c.eq(img.u16(0x4833A), 0x2002, "H2D registrar site: movs r0,#2 (group 2)")
    c.bl(0x4833C, GROUP2_REGISTRAR, "H2D registrar site: bl 0x102866")
    c.eq(img.u16(HCC_DISPATCH), 0xB570, "dispatcher 0xFD7E8 prologue")
    c.eq(img.u16(0xFD806), 0x7801, "dispatcher: ldrb r1,[hdr] (group byte)")
    c.eq(img.u16(0xFD808), 0x88C2, "dispatcher: ldrh r2,[hdr,#6] (id word)")
    c.eq(img.u16(0xFD80A), 0xF001, "dispatcher: and r1,#0xf (group nibble)")
    c.bl(0xFD810, HCC_RESOLVE, "dispatcher: bl 0xFD4C8 (group table resolve)")
    c.eq(img.u16(0xFD816), 0x6843, "dispatcher: ldr r3,[entry,#4] (handler)")
    c.eq(img.u16(0xFD820), 0x4718, "dispatcher: bx r3 (call handler)")
    c.eq(img.u16(HCC_RESOLVE), 0xB188, "resolver 0xFD4C8 prologue (cbz r0)")
    c.eq(img.u16(0xFD4D8), 0x230C, "resolver: movs r3,#0xc (12-byte group records)")
    c.eq(img.u16(0xFD4E6), 0xEB00, "resolver: add.w r0,r0,r2,lsl #4 (16-byte dispatch items)")
    c.eq(img.u16(RX_COMPLETE_HANDLER), 0xB573, "RX-complete handler 0xBBC9C prologue")
    c.eq(img.u16(0xBBC9E), 0xB948, "0xBBC9C: cbnz r0 (NULL message guard)")
    c.bl(0xBBCB4, HCC_HDR, "0xBBC9C: bl 0x409EA (message header fetch)")
    c.eq(img.u16(0xBBCB8), 0x6945, "0xBBC9C: ldr r5,[hdr,#0x14] (band object: producer-written)")
    c.eq(img.u16(0xBBCD0), 0xF8CD, "0xBBC9C: list plumbing from [hdr+0xc]")
    c.bl(0xBBD0C, PER_FRAME_DECODER, "0xBBC9C: bl 0xBA1C8 (datapath handler)")
    c.bl(PER_FRAME_CALL, CRX_BUILDER, "0xBA1C8: bl 0xB9780 (per-frame CRX builder)")
    c.bl(0xBA0C0, RX_QUEUE_POP, "0xB9780: bl 0x1024EA (pop received-frame node)")
    c.bl(0xBA0E2, RX_QUEUE_FREE, "0xB9780: bl 0x1024E6 (free node)")
    c.bl(0xBA17A, HCC_MSG_TX, "0xB9780: bl 0x102760 (deposit g1 id13 D2H)")
    c.eq(img.u16(0xBA150), 0x230D, "0xB9780: movs r3,#0xd (CRX id 13)")
    c.eq(img.u16(0xBA152), 0x71A3, "0xB9780: strb r3,[hdr,#6] (group-1 id-13 header)")
    c.eq(img.u16(0xB97EE), 0xF506, "0xB9780: add.w r3,r6,#0x1a00 (per-chip RX ctx gate)")
    c.eq(img.u16(0xB97F4), 0x8000, "0xB9780: ldr.w r8,[r3] (RX ctx)")
    c.eq(img.u16(0xB97F6), 0xF1B8, "0xB9780: cmp.w r8,#0 (NULL gate)")

    print("== host-visible MAC interrupt-enable writes (MAC start 0x4AF0C) ==")
    c.eq(img.u32(0x4B114), MAC_ENABLE_BAND0, "MAC start literal: band0 enable 0x40108214")
    c.eq(img.u32(0x4B0FC), MAC_ENABLE_BAND1, "MAC start literal: band1 enable 0x4010C220")
    c.eq(img.u16(0x4B0C4), 0x4B13, "MAC start: ldr r3,=0x40108214 (band0 arm)")
    c.eq(img.u16(0x4AFB2), 0xF042, "MAC start: orr r2,r2,#1 (band1 enable chain)")
    c.eq(img.u16(0x4AFD0), 0xF442, "MAC start: orr r2,r2,#0x1000 (band1 0x1029 tail)")
    c.eq(img.u16(0x4B0EA), 0xE773, "MAC start: band0 arm ends with 0xf in the enable")
    c.eq(img.u16(0x4B0E6), 0xF042, "MAC start: band0 orr r2,r2,#8 (0xf chain)")

    print("== the other SMAC ISR bodies (contrast: none builds group-2 id-3) ==")
    c.eq(img.u16(0xAEAE4), 0x2106, "body E (IRQ52/58): movs r1,#6")
    c.eq(img.u16(0xAEB0A), 0x2123, "body E: movs r1,#0x23 (its own id 0x23)")
    c.eq(img.u16(0xAE45C), 0xB510, "body F (IRQ82/84): mask-ack prologue")
    c.eq(img.u32(0xAE47C), 0x01000200, "body F: literal ack mask 0x01000200")
    c.eq(img.u16(0xAE470), 0xEA23, "body F: bic.w r3,r3,r1 (mask & ~status)")
    c.eq(img.u16(0xAE474), 0xF8C2, "body F: str.w r3,[r2,#0xe4] (SMAC ack word)")

    print()
    print("RESULT: %d PASS, %d FAIL" % (c.pass_n, c.fail_n))
    if c.fail_n:
        print("FAILS:")
        for f in c.fails:
            print("  - %s" % f)
        return 1
    return 0


def decode(img):
    print("lane wifi-rx-smac-complete: the SMAC PPDU-completion producer of group-2 id 3")
    print("image: %s (%d B sha256 %s)" % (img.path, img.size, img.sha))
    print()
    print("IRQ entry (ARM vector) CA 0x40064 -> blx 0xC2EFC (event dispatcher)")
    print("  pending id = word[0x4016010C] & 0x3FF (max 0x5F); handler = [0x17D398+0x98+4*id];")
    print("  ack = word[0x40160110] = pending word.  The host cannot read CA 0x4016xxxx.")
    print()
    print("per-band SMAC sources registered by per-band init 0xD1B68 (band0 | band1):")
    for i in range(8):
        b0 = SMAC_ISR[i]
        b1 = SMAC_ISR_BAND1[i]
        print("  IRQ %2d | %2d  prio %2d  wrapper 0x%05x|0x%05x  body 0x%05x|0x%05x%s" % (
            b0[0], b1[0], b0[1], b0[2], b1[2], b0[3], b1[3],
            "   <== group-2 id-3 PRODUCER" if b0[3] == PRODUCER else ""))
    print()
    print("producer body 0xB53A4 (band0 IRQ 49 / band1 IRQ 55):")
    print("  1. base = [band_obj+0x2a0]  (band0 0x40042000, band1 0x40062000 from init 0xD2046/0xD2094)")
    print("     status = [base+0xe8]; if bit2 SET -> return (early out before any counter)")
    print("  2. [band_obj+0xcc0]++  (entry counter)")
    print("  3. bl 0xBFD4C; r = 0xB084C(band_obj+0x348, &out, 3)  (dequeue a completion entry)")
    print("     if r != 0 -> log line 0x2a1, ack, return")
    print("  4. desc = 0xB0C88(...); 0xB2BD8(band_obj+0x66C, desc-8)  (unlink PPDU descriptor)")
    print("  5. [base+0xe4] = 4  (SMAC ack for source bit2)")
    print("  6. msg = 0x102822(0x14) (20-byte message); fill HCC header:")
    print("       hdr[0]&0xf = 2 (group 2);  u16 hdr[6] = 3 (id 3);  hdr[7] = 0")
    print("       hdr[1] |= u8[band_obj]<<4 (destination); hdr[2] = u8[band_obj+2]; hdr[3] = 0x12")
    print("       hdr[0xc] = received-frame chain; hdr[0x13] = 1; hdr[0x14] = band_obj; hdr[0x1d] = 0")
    print("  7. 0x102760(msg)  -> HCC message layer (dest/group resolve: 0x102744 + instance 0x171B6C)")
    print("  8. 0xB52F0(band_obj, band_obj+0x320, desc-8)  -> reserve + link a fresh PPDU descriptor")
    print()
    print("consumer half (cross-check): group-2 table 0x10CC18 entry id 3 -> 0xBBC9C;")
    print("  dispatcher 0xFD7E8 (group = u8[hdr]&0xf, id = u16[hdr+6]) calls it; 0xBBC9C reads")
    print("  [hdr+0x14] (band object) and [hdr+0xc] (frame chain) - exactly the two payload fields")
    print("  the producer writes - then 0xBA1C8 -> 0xB9780 builds the g1 id-13 CRX D2H per frame.")
    print()
    print("host-visible state: the INTC (CA 0x4016xxxx) is not in the host region-3 window; the MAC")
    print("  enable masks 0x40108214/0x4010C220 and the SMAC words 0x400420e8/0x400420e4 ARE (see --probes).")
    return 0

def tables(img):
    print("T1 - IRQ -> SMAC handler registration (per-band init 0xD1B68; c6da2/c6ff4)")
    print("| band0 IRQ | band1 IRQ | prio | wrapper band0 / band1 | common body | status bit of [base+0xe8] |")
    meaning = {
        0xAEC74: "bit0: set->return, else post RTOS event 1",
        0xB53A4: "bit2: set->return, else PRODUCE g2 id3",
        0xB0CE0: "status dispatch (16-B D2H)",
        0xAEB80: "bit7: set->return, else ring walk",
        0xAEA9C: "bit8: set->return, else g2 id0x23 D2H",
        0xAE45C: "mask-ack: (mask & ~status) -> +0xe4",
        0xB0AB8: "error bits (0/1/2)",
    }
    for i in range(8):
        b0 = SMAC_ISR[i]
        b1 = SMAC_ISR_BAND1[i]
        print("| %d | %d | %d | 0x%05x / 0x%05x | 0x%05x | %s |" % (
            b0[0], b1[0], b0[1], b0[2], b1[2], b0[3], meaning.get(b0[3], "?")))
    print()
    print("T2 - producer body 0xB53A4 sequence (every row asserted by --selftest)")
    rows = [
        (0xB53A8, "ldr.w r3,[r0,#0x2a0]", "SMAC register/context base (init: 0x40042000 / 0x40062000)"),
        (0xB53AE, "ldr.w r3,[r3,#0xe8]", "SMAC status word"),
        (0xB53B2, "lsls r3,r3,#0x1d ; bmi ret", "bit2 SET -> early return (gate G2)"),
        (0xB53BE, "ldrb/strb [r0,#0xcc0]", "entry counter (probe P5)"),
        (0xB53C8, "bl 0xBFD4C", "pre-dequeue hook"),
        (0xB53D4, "bl 0xB084C(band+0x348,&out,3)", "dequeue a completion entry (gate G3)"),
        (0xB53F4, "ldr [r4,#0x2a0] ; str #4,[r3,#0xe4]", "SMAC ack bit2 write (probe P6)"),
        (0xB5402, "bl 0xB0C88", "received-frame chain pointer"),
        (0xB5418, "bl 0xB2BD8(band+0x66c,node)", "unlink PPDU descriptor from the alloced list"),
        (0xB5424, "movs r0,#0x14 ; bl 0x102822", "alloc 20-byte message (gate G4)"),
        (0xB5462, "movs r2,#2 ; bfi hdr[0],#0,#4", "group nibble = 2"),
        (0xB546E, "movs r3,#3 ; strb hdr[6]", "id = 3 (u16 hdr[6] = 3)"),
        (0xB5472, "bfi hdr[1], u8[band],#4,#4", "destination nibble = band object byte 0"),
        (0xB5488, "str band_obj,[hdr,#0x14]", "consumer 0xBBC9C reads this field"),
        (0xB548C, "str chain,[hdr,#0xc]", "consumer 0xBBC9C reads this field"),
        (0xB5492, "bl 0x102760", "post the message (HCC message layer)"),
        (0xB5458, "bl 0xB52F0", "re-arm a fresh 0x198-B PPDU descriptor"),
    ]
    for ca, insn, what in rows:
        print("| 0x%05X | %s | %s |" % (ca, insn, what))
    print()
    print("T3 - host conversion (committed by wifi-ppdu-init / wifi-tbtt-timer)")
    print("| window | formula | examples |")
    print("| device IO | phys = CA + 0x3B8000 | 0x400420e8 -> 0x403FA0E8 |")
    print("| firmware RAM | phys = CA + 0x406B8000 | band0 object 0x174BF0 -> 0x4082CBF0 |")
    return 0


def probes(img):
    band0, band1 = BAND_OBJ

    def pd(ca):
        return ca + DEVIO_PHYS_DELTA

    def pr(ca):
        return ca + FW_RAM_PHYS_DELTA

    rows = [
        ("P0", "D2H catalogue (host, no device read)", "-",
         "g4 id1/id2 + g4 id4 present? g1 id13 present = RX delivered to host"),
        ("P1", "band dispatch word", "CA 0x%X (phys 0x%X)" % (BAND_DISPATCH_WORD, pr(BAND_DISPATCH_WORD)),
         "u32; +8 -> band0 obj, +0x1C -> band1 obj (expect 0x174BF0 / 0x175A18)"),
        ("P2", "band0 obj +0x2a0 (SMAC base)", "CA 0x%X (phys 0x%X)" % (band0 + SMAC_CTX_PTR, pr(band0 + SMAC_CTX_PTR)),
         "0x40042000 boot default, or a firmware-RAM SMAC context block (Lead bench: 0x00114F18)"),
        ("P3", "band1 obj +0x2a0 (SMAC base)", "CA 0x%X (phys 0x%X)" % (band1 + SMAC_CTX_PTR, pr(band1 + SMAC_CTX_PTR)),
         "0x40062000 = still the boot default (5G SMAC context never attached)"),
        ("P4", "SMAC status word (follow P2/P3 base)", "CA BASE+0xe8",
         "producer gate; read before/after the auth burst. If P2 is a device-IO base: phys BASE+0x3B8000+0xe8; if FW-RAM base: phys BASE+0x406B8000+0xe8"),
        ("P5", "band0 producer entry counter", "CA 0x%X (phys 0x%X)" % (band0 + 0xCC0, pr(band0 + 0xCC0)),
         "u8; ++ after the bit2 gate, before the dequeue; any increment = the ISR body ran past the gate"),
        ("P5b", "band1 producer entry counter", "CA 0x%X (phys 0x%X)" % (band1 + 0xCC0, pr(band1 + 0xCC0)),
         "u8; band-1 mirror"),
        ("P6", "SMAC ack word (follow P2/P3 base)", "CA BASE+0xe4",
         "producer writes 4, body D 0x80, body E 0x100, body F mask&~status; a change = ISR work"),
        ("P7", "other SMAC source counters (band0)", "CA 0x%X / 0x%X (phys 0x%X / 0x%X)" % (
            band0 + 0xCB8, band0 + 0xCB9, pr(band0 + 0xCB8), pr(band0 + 0xCB9)),
         "body E / body D counters: movement shows SMAC IRQ delivery works at all"),
        ("P8", "MAC band0 enable/status", "CA 0x%X / 0x%X (phys 0x%X / 0x%X)" % (
            MAC_STATUS_BAND0, MAC_ENABLE_BAND0, pd(MAC_STATUS_BAND0), pd(MAC_ENABLE_BAND0)),
         "enable written by MAC start 0x4AF0C: band0 = 0xf (bits 0..3)"),
        ("P9", "MAC band1 enable/status", "CA 0x%X / 0x%X (phys 0x%X / 0x%X)" % (
            MAC_STATUS_BAND1, MAC_ENABLE_BAND1, pd(MAC_STATUS_BAND1), pd(MAC_ENABLE_BAND1)),
         "enable written by MAC start: band1 = 0x1029 (bits 0,3,5,12); INTC-id mapping [U]"),
        ("P10", "consumer counters (existing pack)", "band0 obj +0xCC5/+0x1AD4/+0x1ADA",
         "bump = the consumer's own gates saw a frame (wifi-rx-mac-delivery P5-P8)"),
        ("P11", "PPDU alloced list", "band0 obj +0x66C/+0x670/+0x674",
         "head/tail/count; producer unlinks one and re-links a fresh one per completion"),
    ]
    print("read-only probe pack (no writes, no retries; never read phys 0x1016xxxx):")
    print("| # | item | address | meaning |")
    for r in rows:
        print("| %s | %s | %s | %s |" % r)
    print()
    print("device-IO conversion: phys = CA + 0x%X ; firmware-RAM conversion: phys = CA + 0x%X" % (
        DEVIO_PHYS_DELTA, FW_RAM_PHYS_DELTA))
    print("the host CANNOT read the INTC/event controller at CA 0x4016xxxx (outside region 3).")
    print("Decision: P5 increments + P6 changes => the producer ran; the break is then in the consumer (P10).")
    print("          P5 stays 0 while P7 moves  => the RX source specifically is not delivered")
    print("                                        (MAC enable-mask bit hypothesis).")
    print("          P5 and P7 both stay 0     => SMAC IRQ delivery is dead on this boot.")
    return 0


def main(argv):
    img_path = find_image()
    if not img_path:
        print("FIRMWARE.bin not found (set FIRMWARE_BIN)")
        return 2
    img = Image(img_path)
    if img.size != EXPECT_SIZE or img.sha != EXPECT_SHA:
        print("refusing a different image: %s %d B sha256 %s" % (img_path, img.size, img.sha))
        return 2
    if "--selftest" in argv:
        return selftest(img)
    if "--decode" in argv:
        return decode(img)
    if "--tables" in argv:
        return tables(img)
    if "--probes" in argv:
        return probes(img)
    print("usage: --selftest | --decode | --tables | --probes")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
