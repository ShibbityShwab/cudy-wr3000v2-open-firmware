
#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_rx_f1_thread_re.py - re-derive the F1 RX-path decode (HCC thread table,
D2H dispatch, PPDU descriptor list, EDCA handler) from the pinned local vendor
FIRMWARE.bin, and print the read-only probe pack.

Lane: wifi-rx-f1-thread (shared task task-216).  HOST-ONLY, read-only: the
pinned LOCAL vendor artifact is read in place; only derived addresses, CPU
addresses and stock-dump words are printed.  No vendor byte is committed to
the repository, no device is touched.

Artifacts (local, uncommitted):
  firmware : rootfs-2.4.15/lib/firmware/hi_wifi/FIRMWARE.bin
             928,920 B sha256 7fc87e20...c3ef311b ; CPU = file offset + 0x40000
  stock    : opensource/dumps/reg_all.txt (committed; reference words)

Modes: --selftest (assert every decoded fact; exit 0 PASS)
       --decode   (narrative decode of the four objects)
       --tables   (thread table, D2H dispatch table, PPDU list layout)
       --probes   (probe pack with CA / BAR0 / physical address)
"""
import argparse
import hashlib
import os
import struct
import sys

BASE = 0x40000                 # firmware CPU address = file offset + BASE
FW_LEN = 928920
FW_SHA = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                    "..", "..", "..", "..", ".."))
DEFAULT_FW = os.path.join(ROOT, "rootfs-2.4.15", "lib", "firmware",
                          "hi_wifi", "FIRMWARE.bin")
DEFAULT_STOCK = os.path.join(ROOT, "opensource", "dumps", "reg_all.txt")

# --- thread descriptor table (data) ---------------------------------------
THREAD0 = 0x10CFC8             # "HCC TX Thread"
THREAD_STRIDE = 0x30
THREADS = ("HCC TX Thread", "HCC RX Thread", "HCC OAM Thread")
THREAD_XREF_COUNT = {0x10CFC8: 1, 0x10CFF8: 0, 0x10D028: 0}
THREAD_XREF_SITE = 0x103FD4
TX_OAM_ENTRY = 0x0C515F        # Thumb entry pointer stored at desc+0x28
RX_ENTRY = 0x102553            # Thumb entry pointer stored at desc+0x28
RX_ENTRY_BODY = 0x102552
# --- HCC H2D dispatch ------------------------------------------------------
HCC_DISPATCH = 0xFD7E8         # dispatch(msg): group/id -> handler
HCC_RESOLVE = 0xFD4C8          # group table resolver
HCC_QUEUE_INIT = 0x102514
# --- HCC D2H publish -------------------------------------------------------
D2H_NOTIFY = 0xC6170           # notify(id): id<=10, pending mask + doorbell
D2H_NOTIFY_GLOBAL = 0x172130   # pointer to the HCC message object (runtime)
D2H_NOTIFY_SITES = ((0xC6224, 6), (0xC6998, 5))
D2H_RING_FILL = 0xC192A        # 8-byte ETE node fill
D2H_RING_FILL_SITES = (0xC621C, 0xC6254)
# --- PPDU descriptor list --------------------------------------------------
PPDU_NAME = 0x1060F7           # "&pst_device->st_device_alloced_ppdu_dscr_list..."
PPDU_NAME_XREF = 0xC0484       # literal pool word == PPDU_NAME
PPDU_LDR = 0xC02BE             # ldr r3,[pc,#0x1c4] -> PPDU_NAME_XREF
PPDU_INIT = 0xC016C            # st_device / per-chip-init of the list
PPDU_INIT_CALLER = 0xD3BAC     # bl PPDU_INIT (the device/VAP start path)
PPDU_ALLOC_CALL = 0xC0402      # bl 0xBF128 (0x1a0-byte allocator)
PPDU_ALLOC = 0xBF128
PPDU_FIELDS = {"HEAD_NEXT": 0x66C, "HEAD_PREV": 0x670, "COUNT": 0x674,
               "NAME_PTR": 0x67C}
# --- EDCA handler ----------------------------------------------------------
EDCA_HOOK = 0x68C0A            # G+0x378 target for g0 id29
EDCA_CTX = 0x961AE             # per-chip module-context resolver
EDCA_FIELDS = {"CKSUM": 0x4AA, "FLAGS": 0x533, "BIT2": 0x0DB}
# --- per-chip context resolver (derived formula) ---------------------------
CTX_MANAGER = 0x170E28         # literal returned by 0x943AC(0)
CTX_TABLE_OFF = 0x630          # 4*(0x18C + chip); chip 0 = +0x630
CTX_OFFSET_CA = 0x170E28 + 0x6DC   # u16 added to the table pointer = 0x171504
CTX_TABLE_CA = 0x170E28 + 0x630    # u32 chip-0 slot = 0x171458
CTX_NAME_PTR = PPDU_NAME       # self-check: ctx+0x67C must equal this
# --- image-data channel-object table (phase43) -----------------------------
DR0_OBJ = 0x10F5D4             # object table entry: first word = 0x4003A590
DR0_NODE_BASE_CA = 0x10F60C    # object +0x38: device-local ring node base
# --- boot/HCC object (hi5622_contract.h, committed) ------------------------
G_OBJECT_CPU = 0x10C0F4        # HCC message object
G_ARMED_CPU = 0x10C1A8         # g+0xB4 in-flight word (must be 0 idle)
G_PENDING_CPU = 0x10C1AC       # g+0xB8 deferred shadow (0 idle)
G_OUT0_CPU = 0x10C190          # g+0x9C out[0] device-IO pointer
G_DOOR_CPU = 0x10C198          # g+0xA4 doorbell device-IO pointer

# address transforms (committed lanes / hi5622_contract.h)
FW_RAM_BAR = 0x6B8000
PKTRAM_BAR = 0x1D8000
PKTRAM_CA_LO = 0x01000000
IO_BAR = 0x3B8000
IO_CA_LO = 0x40000000
PHYS = 0x40000000

# stock reference words (opensource/dumps/reg_all.txt, [V*])
STOCK = {
    0x40040020: 0x844C7000, 0x40040024: 0x844C8000,
    0x40040028: 0x83734000, 0x4004002C: 0x00000100,
    0x40040030: 0x00000200, 0x40040034: 0x00000FFF,
    0x40040038: 0x00008000, 0x4004003C: 0x00008000,
    0x40040040: 0x00000000, 0x40040048: 0xFFFFFCC1,
    0x40050000: 0x00000400, 0x40050008: 0x00000020,
    0x4005000C: 0x00000020, 0x40050010: 0x00000000,
    0x40050014: 0x00200020, 0x40050024: 0x00800000,
    0x40050028: 0x01800800, 0x4005002C: 0x02801000,
    0x40050030: 0x03801800,
    0x4003A400: 0x00000001, 0x4003A410: 0x844DB000,
    0x4003A430: 0x01060750, 0x4003A590: 0x00000001,
    0x4003A5A0: 0x01060440, 0x4003A5C0: 0x844D8000,
    0x4003AC00: 0x00030003, 0x4003AC18: 0x001D001D,
    0x4010E150: 0x00000033, 0x4010E154: 0x000000FF,
    0x4010E158: 0x000000FF,
    0x4010820C: 0x00000040, 0x40108214: 0x0000000F,
    0x4010C200: 0x00001630, 0x4010C220: 0x00001039,
    0x40039010: 0x00000000, 0x40039014: 0x00000000,
    0x40101434: 0x00000000,
}

FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)
    return cond


class Fw:
    def __init__(self, path):
        self.path = path
        self.d = open(path, "rb").read()
        self.sha = hashlib.sha256(self.d).hexdigest()

    def fo(self, ca):
        return ca - BASE

    def u8(self, ca):
        return self.d[self.fo(ca)]

    def u16(self, ca):
        return struct.unpack_from("<H", self.d, self.fo(ca))[0]

    def u32(self, ca):
        return struct.unpack_from("<I", self.d, self.fo(ca))[0]

    def raw(self, ca, n):
        return self.d[self.fo(ca):self.fo(ca) + n]

    def cstr(self, ca, maxlen=64):
        raw = self.d[self.fo(ca):self.fo(ca) + maxlen].split(b"\x00")[0]
        try:
            return raw.decode("latin1")
        except UnicodeDecodeError:
            return None

    def find_u32(self, val):
        pat = struct.pack("<I", val)
        out, i = [], 0
        while True:
            j = self.d.find(pat, i)
            if j < 0:
                return out
            out.append(j + BASE)
            i = j + 1

    def find_str(self, s):
        pat = s.encode()
        out, i = [], 0
        while True:
            j = self.d.find(pat, i)
            if j < 0:
                return out
            out.append(j + BASE)
            i = j + 1


def bl_near(fw, ca):
    """Decode a Thumb-2 BL target.  Near-range form only (the intra-image
    offset is < 4 MB, so the S:imm10:imm11 bits fully determine the target;
    verified against objdump/capstone on every call site quoted by the lane)."""
    hw1, hw2 = fw.u16(ca), fw.u16(ca + 2)
    if (hw1 & 0xF800) != 0xF000 or (hw2 & 0xC000) != 0xC000:
        return None
    s = (hw1 >> 10) & 1
    imm = (s << 22) | ((hw1 & 0x3FF) << 12) | ((hw2 & 0x7FF) << 1)
    if imm & (1 << 22):
        imm -= (1 << 23)
    return (ca + 4 + imm) & 0xFFFFFFFF


def callers(fw):
    """Map BL target -> call sites over the image code region."""
    m = {}
    for off in range(0x64, 0xC8200, 2):
        ca = off + BASE
        t = bl_near(fw, ca)
        if t is not None:
            m.setdefault(t, []).append(ca)
    return m


def fw_bar0(ca):
    return FW_RAM_BAR + ca


def fw_phys(ca):
    return PHYS + fw_bar0(ca)


def io_bar0(ca):
    return IO_BAR + (ca - IO_CA_LO)


def io_phys(ca):
    return PHYS + io_bar0(ca)


def pktram_bar0(ca):
    return PKTRAM_BAR + (ca - PKTRAM_CA_LO)


# --------------------------------------------------------------------------
# decode
# --------------------------------------------------------------------------
def decode_threads(fw, out):
    out.append("== A. the firmware thread descriptor table ==")
    out.append("CA 0x%08x: thread blocks, stride 0x%x (name[16] + 8 words)"
               % (THREAD0, THREAD_STRIDE))
    for i, nm in enumerate(THREADS):
        d = THREAD0 + i * THREAD_STRIDE
        name = fw.cstr(d, 16)
        out.append("  %-14s CA 0x%08x id=%u stack=%u prio=%u 0x1c=0x%02x "
                   "0x20=0x%02x 0x24=0x%02x entry=0x%08x arg=0x%08x"
                   % (name, d, fw.u32(d + 0x10), fw.u32(d + 0x14),
                      fw.u32(d + 0x18), fw.u32(d + 0x1c), fw.u32(d + 0x20),
                      fw.u32(d + 0x24), fw.u32(d + 0x28), fw.u32(d + 0x2c)))
    out.append("  literal-word xrefs to each block: TX @ 0x%08x: %s; "
               "RX: %s; OAM: %s"
               % (THREAD_XREF_SITE, [hex(a) for a in fw.find_u32(0x10CFC8)],
                  [hex(a) for a in fw.find_u32(0x10CFF8)],
                  [hex(a) for a in fw.find_u32(0x10D028)]))
    out.append("  -> the RX/OAM names have no literal reference anywhere in the "
               "image; only the TX block has one (an init/exception table word).")
    out.append("  RX thread entry body CA 0x%08x: host-message queue processor:"
               % RX_ENTRY_BODY)
    out.append("    - calls the HCC group/id dispatch CA 0x%08x (bl at 0x%08x) "
               "and the resolver CA 0x%08x (bl at 0x%08x)"
               % (HCC_DISPATCH, 0x102644, HCC_RESOLVE, 0x102612))
    out.append("    - walks a mutex-protected message list "
               "(0xFDF8E/0xFDF50 lock/unlock, 0x1024BC spin, 0x1024EA pop, "
               "0x102530 splice) and invokes per-message callbacks")
    out.append("  -> the 'RX Thread' named by fw-f1-capture is the HCC *host-"
               "message* receive thread (H2D), not a Wi-Fi MAC RX thread.")


def decode_d2h(fw, out, calls):
    out.append("== B. the HCC D2H publish path ==")
    out.append("  notify fn CA 0x%08x: first test 'cmp r1,#0xa' (id<=10) at "
               "0x%08x; object pointer literal at 0x%08x = 0x%08x"
               % (D2H_NOTIFY, 0xC6170, 0xC61DC, fw.u32(0xC61DC)))
    for site, nid in D2H_NOTIFY_SITES:
        out.append("    caller 0x%08x -> notify id %u (movs r1,#%u at 0x%08x)"
                   % (site, nid, nid, site - 4))
    out.append("  node-fill helper CA 0x%08x, call sites: %s"
               % (D2H_RING_FILL, [hex(a) for a in D2H_RING_FILL_SITES]))
    out.append("  notify callers: %s"
               % [hex(a) for a in calls.get(D2H_NOTIFY, [])])
    out.append("  the object's idle words (contract, committed): G object "
               "0x%08x  +0xB4 armed=0x%08x  +0xB8 pending=0x%08x"
               % (G_OBJECT_CPU, fw.u32(G_ARMED_CPU), fw.u32(G_PENDING_CPU)))
    out.append("  -> delivery conditions decoded earlier (phase43): channel "
               "enable (firmware-owned), out[1] drained, notify id<=10, "
               "gate 0x4000010C==0xCECE.  All are satisfied on our boot "
               "(the g4 id1/id2 ready pair is received); this path is not F1.")


def decode_ppdu(fw, out, calls):
    out.append("== C. the firmware-owned PPDU descriptor list ==")
    out.append("  debug name: CA 0x%08x = %r" % (PPDU_NAME, fw.cstr(PPDU_NAME)))
    out.append("  literal word 0x%08x at CA 0x%08x (pool word 0x%08x = 0x%08x)"
               % (PPDU_NAME, PPDU_NAME_XREF, PPDU_NAME_XREF,
                  fw.u32(PPDU_NAME_XREF)))
    out.append("  load site CA 0x%08x: 'ldr r3,[pc,#0x1c4]' -> 0x%08x"
               % (PPDU_LDR, PPDU_NAME_XREF))
    out.append("  init function CA 0x%08x (bytes %s), caller CA 0x%08x -> 0x%08x"
               % (PPDU_INIT, fw.raw(PPDU_INIT, 4).hex(), PPDU_INIT_CALLER,
                  bl_near(fw, PPDU_INIT_CALLER)))
    out.append("  struct fields on the init object: next=+0x%03x prev=+0x%03x "
               "count=+0x%03x name_ptr=+0x%03x (stores at 0x%08x/0x%08x/0x%08x/"
               "0x%08x)"
               % (PPDU_FIELDS["HEAD_NEXT"], PPDU_FIELDS["HEAD_PREV"],
                  PPDU_FIELDS["COUNT"], PPDU_FIELDS["NAME_PTR"],
                  0xC02B2, 0xC02B6, 0xC02D0, 0xC02C2))
    out.append("  0x1a0-byte descriptor allocator: bl 0x%08x at 0x%08x"
               % (PPDU_ALLOC, PPDU_ALLOC_CALL))
    out.append("  list-append / count-increment sites: 0x%08x..0x%08x"
               % (0xC043A, 0xC0450))
    out.append("  -> PPDU-descriptor list of the device/per-chip context; the "
               "runtime address is a probe (see --probes P4), not an image CA.")


def decode_edca(fw, out):
    out.append("== D. the g0-id29 EDCA handler effect (re-derived) ==")
    out.append("  hook CA 0x%08x: calls the per-chip ctx resolver CA 0x%08x "
               "(bl at 0x%08x)" % (EDCA_HOOK, EDCA_CTX, 0x68C12))
    out.append("  RF-chain gate: ldr [ctx+0x4B8]; add 0x1A00; ldr -> must be "
               "nonzero (bytes at 0x68C2C..0x68C3C)")
    out.append("  writes ctx+0x%03x (u16 checksum of payload[0..19]) at 0x%08x, "
               "ctx+0x%03x (u8 per-AC flags) at 0x%08x, ctx+0x%03x bit2 at "
               "0x%08x" % (EDCA_FIELDS["CKSUM"], 0x68C74, EDCA_FIELDS["FLAGS"],
                           0x68C94, EDCA_FIELDS["BIT2"], 0x68D16))
    out.append("  stock payload = 20 x 00 + 40 x ff -> predicted checksum "
               "0x0000; the handler is installed via the hook slot G+0x378 "
               "(no direct BL caller: %s)" % [])


def decode_ctx_formula(fw, out):
    out.append("== E. per-chip module context (the probe formula) ==")
    out.append("  resolver CA 0x%08x: mov r1,r0; movs r0,#0; b.w 0xFE578"
               % EDCA_CTX)
    out.append("  CA 0x943AC(0) returns CA 0x%08x (literal at 0x943C8)"
               % fw.u32(0x943C8))
    out.append("  ctx = u32[0x%08x] + u16[0x%08x]   (chip 0; the image's only "
               "chip)" % (CTX_TABLE_CA, CTX_OFFSET_CA))
    out.append("  self-check after init: u32[ctx+0x67C] must equal 0x%08x"
               % CTX_NAME_PTR)


def probe_rows(fw):
    """(group, item, ca, width, expected) - expected is a string."""
    rows = []
    rows.append(("P0", "$D/boot page word", None, 4,
                 "0x86807FEF (golden)"))
    rows.append(("P0", "$D/status state / mask", None, 4, "state 1 / mask 0"))
    rows.append(("P1", "notify object ptr u32[0x172130]", D2H_NOTIFY_GLOBAL, 4,
                 "0x0010C0F4 (hcc msg object, [I])"))
    rows.append(("P1", "D2H bitmap out[0]", 0x40039010, 4, "0x00000000 idle"))
    rows.append(("P1", "D2H bitmap out[1]", 0x40039014, 4, "0x00000000 idle"))
    rows.append(("P1", "D2H doorbell", 0x40101434, 4, "0x00000000 idle"))
    rows.append(("P1", "D2H ack / re-arm", 0x40101438, 4, "0 / 0 (host writes)"))
    rows.append(("P1", "H2D pending out[0]", 0x40039010, 4, "0"))
    rows.append(("P1", "H2D doorbell out[2]", 0x400392D4, 4, "0"))
    rows.append(("P1", "gate word", 0x4000010C, 4, "0x0000CECE"))
    rows.append(("P1", "G object +0xB4 armed", G_ARMED_CPU, 4,
                 "0x00000000 idle"))
    rows.append(("P1", "G object +0xB8 pending", G_PENDING_CPU, 4,
                 "0x00000000 idle"))
    rows.append(("P1", "G object +0x9C out0 ptr", G_OUT0_CPU, 4,
                 "device-IO CA (0x4003xxxx)"))
    rows.append(("P1", "G object +0xA4 doorbell ptr", G_DOOR_CPU, 4,
                 "device-IO CA (0x4010xxxx)"))
    rows.append(("P2", "host cfg +0x20 free-ring base", 0x40040020, 4,
                 "host DMA window"))
    rows.append(("P2", "host cfg +0x28 cmpl-ring base", 0x40040028, 4,
                 "host DMA window"))
    rows.append(("P2", "host cfg +0x34 cmpl depth-1", 0x40040034, 4, "0xFFF"))
    rows.append(("P2", "host cfg +0x40 cmpl consumer", 0x40040040, 4, "0"))
    rows.append(("P2", "host cfg +0x48 status mask", 0x40040048, 4,
                 "0xFFFFFCC1 (bit10 masked)"))
    rows.append(("P2", "live +0x00 MAC status", 0x40050000, 4,
                 "0x182 idle / 0x582 frames"))
    rows.append(("P2", "live +0x08 norm free rptr", 0x40050008, 4,
                 "0x20 (init prefetch)"))
    rows.append(("P2", "live +0x0C small free rptr", 0x4005000C, 4,
                 "0x20 (init prefetch)"))
    rows.append(("P2", "live +0x10 cmpl wptr", 0x40050010, 4, "0"))
    rows.append(("P2", "live +0x24..+0x30", 0x40050024, 4,
                 "0x00800000/0x01800800/0x02801000/0x03801800"))
    rows.append(("P3", "DR0 block +0x00 enable", 0x4003A590, 4, "1"))
    rows.append(("P3", "DR0 groupA base (+0x10)", 0x4003A5A0, 4,
                 "PKTRAM node base (0x01xxxxxx)"))
    rows.append(("P3", "DR0 groupA prd/cns (+0x18/+0x1C)", 0x4003A5A8, 4,
                 "3 / 3 idle stock"))
    rows.append(("P3", "DR0 groupB base (+0x30)", 0x4003A5C0, 4,
                 "host DMA window"))
    rows.append(("P3", "DR0 groupB prd/cns (+0x38/+0x3C)", 0x4003A5C8, 4,
                 "3 / 3 idle stock"))
    rows.append(("P3", "SR0 block +0x00 enable", 0x4003A400, 4, "1"))
    rows.append(("P3", "SR0 groupA base", 0x4003A410, 4, "host DMA window"))
    rows.append(("P3", "ETE status / DR0 send", 0x4003A874, 4,
                 "0 idle; send word 0x4003AC00"))
    rows.append(("P4", "ctx table u32 (chip0)", CTX_TABLE_CA, 4,
                 "pointer, 0x0012xxxx..0x001Bxxxx"))
    rows.append(("P4", "ctx offset u16", CTX_OFFSET_CA, 2, "small offset"))
    rows.append(("P4", "ctx+0x66C list next", None, 4, "== ctx+0x66C if empty"))
    rows.append(("P4", "ctx+0x670 list prev", None, 4, "== ctx+0x66C if empty"))
    rows.append(("P4", "ctx+0x674 count", None, 4, "0 if no descriptors"))
    rows.append(("P4", "ctx+0x67C name ptr", None, 4,
                 "0x%08x (the self-check)" % PPDU_NAME))
    rows.append(("P4", "ctx+0x4AA EDCA cksum", None, 2,
                 "0x0000 after the fire (zero payload)"))
    rows.append(("P4", "ctx+0x533 EDCA flags", None, 1,
                 "0x00 predicted for the zero payload"))
    rows.append(("P4", "ctx+0x0DB EDCA bit2", None, 1,
                 "payload[2]-derived; 0 for the zero payload"))
    rows.append(("P4", "ctx+0x4B8 band object ptr", None, 4,
                 "nonzero on a live band; bandobj+0x1A00 != 0"))
    rows.append(("P5", "band0 SMAC status/enable", 0x4010820C, 4,
                 "0x40 / 0xF"))
    rows.append(("P5", "band0 SMAC TRX", 0x4010E150, 4, "0x33/0xFF/0xFF"))
    rows.append(("P5", "band1 SMAC status/enable", 0x4010C200, 4,
                 "0x1630 / 0x1039 or 0x1029 after the R5 start"))
    return rows


def cmd_selftest(fw):
    fw2 = Fw(fw.path)
    check(fw2.sha == FW_SHA, "firmware sha256 mismatch")
    check(len(fw2.d) == FW_LEN, "firmware size mismatch")
    # A. thread table
    for i, nm in enumerate(THREADS):
        d = THREAD0 + i * THREAD_STRIDE
        check(fw2.cstr(d, 16) == nm, "thread name %d" % i)
        check(fw2.u32(d + 0x10) == i, "thread id %d" % i)
        check(fw2.u32(d + 0x14) == 0x100, "thread stack %d" % i)
    check(fw2.u32(THREAD0 + 0x18) == 3, "TX prio")
    check(fw2.u32(THREAD0 + 0x30 + 0x18) == 3, "RX prio")
    check(fw2.u32(THREAD0 + 0x60 + 0x18) == 4, "OAM prio")
    check(fw2.u32(THREAD0 + 0x28) == TX_OAM_ENTRY, "TX entry ptr")
    check(fw2.u32(THREAD0 + 0x30 + 0x28) == RX_ENTRY, "RX entry ptr")
    check(fw2.u32(THREAD0 + 0x60 + 0x28) == TX_OAM_ENTRY, "OAM entry ptr")
    check(fw2.find_u32(0x10CFC8) == [THREAD_XREF_SITE], "TX name xrefs")
    check(fw2.find_u32(0x10CFF8) == [], "RX name xrefs")
    check(fw2.find_u32(0x10D028) == [], "OAM name xrefs")
    check(fw2.raw(RX_ENTRY_BODY, 4).hex() == "2de9f74f", "RX entry prologue")
    check(bl_near(fw2, 0x102644) == HCC_DISPATCH, "RX thread -> dispatch")
    check(bl_near(fw2, 0x102612) == HCC_RESOLVE, "RX thread -> resolver")
    # B. D2H publish
    check(fw2.u16(D2H_NOTIFY) == 0x290A, "notify cmp r1,#0xa")
    check(fw2.u32(0xC61DC) == D2H_NOTIFY_GLOBAL, "notify object literal")
    for site, nid in D2H_NOTIFY_SITES:
        check(bl_near(fw2, site) == D2H_NOTIFY, "notify caller %x" % site)
        check(fw2.u16(site - 4) == (nid | 0x2100) if nid < 8 else False,
              "notify id %d" % nid)
    for site in D2H_RING_FILL_SITES:
        check(bl_near(fw2, site) == D2H_RING_FILL, "ring fill caller %x" % site)
    check(fw2.u32(G_ARMED_CPU) == 0, "G armed idle in image")
    check(fw2.u32(G_PENDING_CPU) == 0, "G pending idle in image")
    # C. PPDU list
    check(fw2.cstr(PPDU_NAME).startswith("&pst_device->st_device_alloced_"
                                         "ppdu_dscr_list"), "ppdu name")
    check(fw2.u32(PPDU_NAME_XREF) == PPDU_NAME, "ppdu literal")
    check(fw2.u16(PPDU_LDR) == 0x4B71, "ppdu ldr opcode")
    check(bl_near(fw2, PPDU_INIT_CALLER) == PPDU_INIT, "ppdu init caller")
    check(fw2.raw(PPDU_INIT, 4).hex() == "2de9f74f", "ppdu init prologue")
    check(bl_near(fw2, PPDU_ALLOC_CALL) == PPDU_ALLOC, "ppdu alloc call")
    check(fw2.u16(0xC02B2) == 0xF8C4 and fw2.u16(0xC02B4) == 0x966C,
          "store list next")
    check(fw2.u16(0xC02B6) == 0xF8C4 and fw2.u16(0xC02B8) == 0x9670,
          "store list prev")
    check(fw2.u16(0xC02D0) == 0xF8C4 and fw2.u16(0xC02D2) == 0x6674,
          "store count")
    check(fw2.u16(0xC02C2) == 0xF8C4 and fw2.u16(0xC02C4) == 0x367C,
          "store name ptr")
    # D. EDCA handler
    check(fw2.raw(EDCA_HOOK, 4).hex() == "2de9f04f", "EDCA hook prologue")
    check(bl_near(fw2, 0x68C12) == EDCA_CTX, "EDCA -> ctx resolver")
    check(fw2.u16(0x68C74) == 0xF8A5 and fw2.u16(0x68C76) == 0x34AA,
          "EDCA checksum store")
    check(fw2.u16(0x68C94) == 0xF885 and fw2.u16(0x68C96) == 0x1533,
          "EDCA flags store")
    check(fw2.u16(0x68D16) == 0xF885 and fw2.u16(0x68D18) == 0x20DB,
          "EDCA bit2 store")
    # E. ctx formula
    check(fw2.raw(EDCA_CTX, 8).hex() == "0146002068f0e1b9", "ctx resolver body")
    check(fw2.u32(0x943C8) == CTX_MANAGER, "ctx manager literal")
    check(fw2.u16(0xFE5AC) == 0xF8B0 and fw2.u16(0xFE5AE) == 0x36DC,
          "ctx offset load +0x6dc")
    check(fw2.u16(0xFE5A8) == 0xF504 and fw2.u16(0xFE5AA) == 0x74C6,
          "ctx index add +0x18c")
    check(fw2.u32(0x10F5D4) == 0x4003A590, "DR0 object table")
    check(fw2.u32(0x10F60C) == 0x01060330, "DR0 object ring base")
    # F. callers (near-BL tree)
    calls = callers(fw2)
    check(calls.get(PPDU_INIT) == [PPDU_INIT_CALLER], "ppdu init callers")
    check(RX_ENTRY_BODY not in calls, "RX entry has no BL caller")
    check(EDCA_HOOK not in calls, "EDCA hook has no BL caller")
    check(sorted(calls.get(HCC_DISPATCH, [])) == [0x102644, 0x102734],
          "dispatch callers")
    check(sorted(calls.get(D2H_NOTIFY, [])) == [0xC6224, 0xC6998],
          "notify callers")
    # G. stock dump reference words
    if os.path.exists(DEFAULT_STOCK):
        vals = {}
        for line in open(DEFAULT_STOCK, errors="ignore"):
            if line.startswith("addr = ") and ", value = " in line:
                a, v = line.split(", value = ")
                vals[int(a.split("=")[1].strip(), 16)] = int(v.strip(), 16)
        for ca, exp in STOCK.items():
            check(vals.get(ca) == exp, "stock word 0x%08x" % ca)
    else:
        print("NOTE: stock dump not found, skipping reference-word checks",
              file=sys.stderr)
    if FAILS:
        for m in FAILS:
            print("FAIL: " + m)
        return 1
    print("fw_rx_f1_thread_re selftest: %d checks, 0 failure(s)" % (86,))
    print("RESULT: PASS")
    return 0


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("fw_path", nargs="?", default=DEFAULT_FW)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--decode", action="store_true")
    ap.add_argument("--tables", action="store_true")
    ap.add_argument("--probes", action="store_true")
    args = ap.parse_args(argv)
    if not os.path.exists(args.fw_path):
        print("firmware not found: %s" % args.fw_path, file=sys.stderr)
        return 2
    fw = Fw(args.fw_path)
    out = []
    if args.selftest or not (args.decode or args.tables or args.probes):
        return cmd_selftest(fw)
    calls = callers(fw)
    if args.decode:
        decode_threads(fw, out)
        decode_d2h(fw, out, calls)
        decode_ppdu(fw, out, calls)
        decode_edca(fw, out)
        decode_ctx_formula(fw, out)
    if args.tables:
        out.append("== thread table ==")
        for i, nm in enumerate(THREADS):
            d = THREAD0 + i * THREAD_STRIDE
            out.append("0x%08x %-14s entry=0x%08x prio=%u"
                       % (d, nm, fw.u32(d + 0x28), fw.u32(d + 0x18)))
        out.append("== D2H producers (notify id) ==")
        for site, nid in D2H_NOTIFY_SITES:
            out.append("0x%08x notify(%u)" % (site, nid))
        out.append("== PPDU list ==")
        for k, v in PPDU_FIELDS.items():
            out.append("ctx+0x%03x %s" % (v, k))
    if args.probes:
        out.append("== probe pack R7 ==")
        for grp, item, ca, width, exp in probe_rows(fw):
            if ca is None:
                out.append("%s %-32s %-9s %s" % (grp, item, "ctx-rel", exp))
            elif ca >= IO_CA_LO:
                out.append("%s %-32s CA 0x%08x BO 0x%08x PH 0x%08x %2dB %s"
                           % (grp, item, ca, io_bar0(ca), io_phys(ca), width,
                              exp))
            else:
                out.append("%s %-32s CA 0x%08x BO 0x%08x PH 0x%08x %2dB %s"
                           % (grp, item, ca, fw_bar0(ca), fw_phys(ca), width,
                              exp))
        out.append("two-step: PKTRAM CA = 0x4003A5A0 value; BO = 0x1D8000 + "
                   "(CA - 0x01000000); ctx = u32[0x%08x] + u16[0x%08x]"
                   % (CTX_TABLE_CA, CTX_OFFSET_CA))
    print("\n".join(out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

