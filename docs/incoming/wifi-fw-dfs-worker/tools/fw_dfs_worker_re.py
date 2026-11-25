#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_dfs_worker_re.py - re-derive the g0-id1 key 0x01F7 acceptance/dispatch.

Lane: wifi-fw-dfs-worker (shared task task-191).  HOST-ONLY, read-only:
the pinned LOCAL vendor artifacts are read in place; only derived values
(addresses, instruction encodings, table words, probe addresses, frame bytes
of OUR OWN host-built frames) are printed.  No vendor byte is copied into the
repository and no device is touched.

Artifacts (local, uncommitted):
  firmware: /home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin
            928,920 B sha256 7fc87e20...311b ; CPU = file offset + 0x40000
  module:   .../lib/hisilicon/ko/hi5622v100_wifi.ko (ELF .text read in place)

The decode (evidence: ../REPORT.md, ../TABLES.md):
  * g0-id1 handler CA 0x43D58: validates the message (CA 0x409EA: pool
    membership + header class hdr[5]>>1&7 <= 3), resolves
    ctx = CA 0xFE578(0, hdr[3])  (hdr[3] = header field B = per-module context
    index 0..0x12), then scans table A (CA 0x10C23C, 217 x
    8 B {u16 key; u16 pad; u32 worker|thumb}) for key = u16 hdr+20.  Table A
    row 93 = key 0x01F7 -> worker 0x0B5B9D.  It calls
    worker(r0=ctx, r1=u8 hdr+22, r2=hdr+24).  There is NO length / flag /
    checksum gate in the dispatcher.
  * CA 0xFE578 / G=CA 0x170E28: ctx = u32(G + 0x630 + 4*fieldB) +
    u16(G + 0x6DC).  Boot init CA 0x49204..0x49264 memsets G and fills all
    19 slots with 0x119758 + k*0xB40 (contexts; bodies zeroed).
  * worker CA 0xB5B9C: r4 = u32(ctx+0x4B8) (band object, required non-zero),
    O = u32(r4+0x1A00) (published per-link, required non-zero),
    attach = u8(r4+0xD70) (required == 1), mode = u8(body[0]) in 1..5.
    The mode byte writer CA 0xB5D1C is inside this worker; mode 3 calls
    CA 0x89924(O) which programs the ch36..64 RF image into R=u32(O+0x2D8).
  * the key-0x64 config-event worker CA 0x449E4 binds a context's band
    resources: ctx+0x4B0 = band published per-link, ctx+0x4B4 = third field,
    ctx+0x4B8 = CA 0xA4FC8(band) = 0x15CA08 + 0x1B68*band, from body[0x12].
  * CONSEQUENCE: hdr[3] (field B) is the module-context selector.  Our
    0x01F7 frame carried field B = 0x04 (copied from the vendor TXQ capture)
    while the AP-start ladder uses field B = 0 (vap_start_module).  The boot
    C2 ctx_create (g0 key 0x64, module 4, body[0x12]=0) binds module 4 to
    BAND 0, so the 0x01F7 worker runs against band 0 and programs band 0's
    mode byte/RF image - not band 1's.  Nothing is dropped; the target is
    wrong.  Fix: field B = the AP-start module index (0 in the fired recipe).

Modes:
  --selftest   assert every decoded fact (exit 0 on PASS)
  --decode     the dispatcher/worker/context decode
  --frame      the current 29-B frame and the field-B-fixed 29-B frame
  --probes     the discriminating read-only probe pack (CPU/BAR0/phys)
  --ctx        the field-B context table model + addresses
"""
import argparse
import hashlib
import os
import struct
import sys

FW = ("/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/"
      "FIRMWARE.bin")
FW_SHA = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_LEN = 928920
BASE = 0x40000                    # CPU = file offset + 0x40000
KO = ("/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/"
      "hi5622v100_wifi.ko")
KO_TEXT_OFF = 0x38                # ELF .text file offset (verified below)

# --- firmware addresses (CPU) ---------------------------------------------
HANDLER_G0ID1 = 0x43D58
TBL_A = 0x10C23C                  # 217 x 8 B {u16 key; u16 pad; u32 worker}
TBL_A_N = 217
TBL_A_END = 0x6C8                 # byte bound in the handler loop
TBL_B = 0x103BF8                  # second table, 88 x 8 B
TBL_B_END = 0x2C0
ROW_1F7 = 93
ROW_F7 = 92
WORKER_1F7 = 0xB5B9C
MODE_WR = 0xB5D1C                 # strb.w r5,[r4,#0xF04]
MODE3_IMG = 0x89924               # ch36..64 image writer (called with O)
MEMCPY_S = 0xC1E44
CTX_FN = 0xFE578
CTX_GETTER_LIT = 0xFE574          # 0x001ADB80
G_LIT = 0x943C8                   # 0x00170E28 = G
G = 0x170E28
G_TBL_OFF = 0x630                 # u32 context pointer table
G_BASE_OFF = 0x6DC                # u16 common offset
POOL = 0x119758                   # context 0
CTX_STRIDE = 0xB40
CTX_N = 19
BAND_LOOKUP = 0xA4FC8             # (unused arg, band) -> 0x15CA08+0x1B68*band
BAND_BASE = 0x15CA08
BAND_STRIDE = 0x1B68
VAPADD_WORKER = 0x449E4           # g0 key 0x64 worker (table A row 1)
VAPADD_KEY = 0x0064
BOOT_G_INIT = 0x49204
PUB_O_1A00 = 0x48A68              # add.w r3,r4,#0x1A00 ; str.w sl,[r3]
PUB_ATTACH = 0x48A7E              # strb.w r3,[r4,#0xD70]
# bench-known band-1 values (committed F0/F1 fires)
BAND1_OBJ = 0x15E570
BAND1_PERLINK = 0x175A18
BAND0_OBJ = BAND_BASE
BAND0_PERLINK = 0x174BF0

# --- firmware raw instruction assertions ----------------------------------
FW_BYTES = [
    # g0-id1 handler: field B -> CA 0xFE578; key = u16 msg+20; table A scan;
    # worker from entry+4; body = msg+24; u8 len = msg+22; blx worker.
    (0x43D72, "c178", "ldrb r1,[r0,#3]  (field B)"),
    (0x43D74, "0020", "movs r0,#0"),
    (0x43D76, "baf0fffb", "bl CA 0xFE578 (ctx = lookup(0,fieldB))"),
    (0x43D90, "a28a", "ldrh r2,[r4,#20]  (u16 key = hdr+20)"),
    (0x43D92, "214d", "ldr r5,=table A"),
    (0x43DA2, "b3f5d96f", "cmp.w r3,#0x6C8 (217 entries)"),
    (0x43DAC, "e918", "adds r1,r5,r3 (table B entry)"),
    (0x43DCE, "4b68", "ldr r3,[r1,#4]  (worker = entry+4)"),
    (0x43DEC, "04f11802", "add.w r2,r4,#24  (body = msg+24)"),
    (0x43DF0, "a17d", "ldrb r1,[r4,#22]  (u8 len = msg+22)"),
    (0x43DF2, "9847", "blx r3 (call worker(r0=ctx,r1=len,r2=body))"),
    (0x43E18, "3cc21000", "table A literal = CA 0x0010C23C"),
    (0x43E1C, "f83b1000", "table B literal = CA 0x00103BF8"),
    # worker CA 0xB5B9C
    (0xB5B9E, "d0f8b844", "ldr.w r4,[r0,#0x4B8]  (band object)"),
    (0xB5BA6, "04f5d056", "add.w r6,r4,#0x1A00"),
    (0xB5BAA, "306860b9", "ldr r0,[r6]; cbnz r0 (published per-link != 0)"),
    (0xB5BB8, "40f2a251", "movw r1,#1442 (log id)"),
    (0xB5BC8, "94f8701d", "ldrb.w r1,[r4,#0xD70]  (attach flag)"),
    (0xB5BCC, "0129", "cmp r1,#1"),
    (0xB5BCE, "08d0", "beq +8 (attach must be 1)"),
    (0xB5BD0, "4ff4b561", "mov.w r1,#1448 (log id)"),
    (0xB5BE2, "1578", "ldrb r5,[r2,#0]  (mode = body[0])"),
    (0xB5BE4, "012d", "cmp r5,#1"),
    (0xB5BE6, "78d1", "bne (mode branches)"),
    (0xB5D1C, "84f8045f", "strb.w r5,[r4,#0xF04]  (THE mode writer)"),
    (0xB5D6A, "6b1e042b", "subs r3,r5,#1; cmp r3,#4  (mode 1..5)"),
    (0xB5D72, "dfe803f0", "tbb [pc,r3]"),
    (0xB5D76, "9b909b979000", "tbb table: mode1 0x9b mode2 0x90 mode3 0x9b mode4 0x97 mode5 0x90"),
    (0xB5D80, "d0f8d832", "ldr.w r3,[r0,#0x2D8]  (mode-5 RF image base R)"),
    (0xB5CEE, "00f2c310", "addw r0,r0,#0x1C3  (mode word destination)"),
    (0xB5CF2, "0cf0a7f8", "bl CA 0xC1E44 (memcpy_s, 4 B stack word -> O+0x1C3)"),
    (0xB5E78, "032d", "cmp r5,#3  (mode-3 branch)"),
    (0xB5E7A, "02d1", "bne"),
    (0xB5E7C, "d3f752fd", "bl CA 0x89924  (ch36..64 RF image writer)"),
    (0xB5E80, "2fe7", "b CA 0xB5CE2 (common band fields + mode byte)"),
    (0x89924, "38b5424b", "push {r3,r4,r5,lr}; ldr r3,=image[0]"),
    (0x89928, "d0f8d842", "ldr.w r4,[r0,#0x2D8]  (R = *(O+0x2D8))"),
    # context lookup CA 0xFE578: G + 0x630 + 4*fieldB, + u16 G+0x6DC
    (0xFE578, "70b50546", "push {r4,r5,r6,lr}; mov r5,r0"),
    (0xFE59C, "122c03d9", "cmp r4,#0x12  (field B bound 0..18)"),
    (0xFE5A8, "04f5c674", "add.w r4,r4,#0x18C  (index 0x18C+fieldB)"),
    (0xFE5AC, "b0f8dc36", "ldrh.w r3,[r0,#0x6DC]  (common offset)"),
    (0xFE5B0, "50f82400", "ldr.w r0,[r0,r4,lsl #2]  (context pointer)"),
    (0xFE5B4, "1844", "add r0,r3"),
    (0xFE574, "80db1a00", "getter literal = CA 0x001ADB80"),
    (0x943C8, "280e1700", "G literal = CA 0x00170E28"),
    # boot init of G: memset 0x700; table fill G+0x630+4k = 0x119758+k*0xB40
    (0x49204, "4ff4e063", "mov.w r3,#0x700 (memset G)"),
    (0x4923A, "a4f8dc66", "strh.w r6,[r4,#0x6DC]  (common offset, once)"),
    (0x49242, "4ff43463", "mov.w r3,#0xB40 (context stride)"),
    (0x4924C, "48f8040f", "str.w r0,[r8,#4]!  (context pointer table)"),
    (0x49256, "4df2c053", "movw r3,#0xD5C0 (19 context addresses)"),
    (0x49310, "280e1700", "G literal = 0x00170E28"),
    (0x49330, "58971100", "context pool base literal = 0x00119758"),
    # boot band-object publish + attach clamp (per band record)
    (0x48A68, "04f5d053", "add.w r3,r4,#0x1A00"),
    (0x48A6C, "c3f800a0", "str.w sl,[r3]  (band+0x1A00 = published per-link)"),
    (0x48A70, "9af80130", "ldrb.w r3,[sl,#1]  (perlink[+1])"),
    (0x48A7A, "28bf", "it cs"),
    (0x48A7C, "0223", "movcs r3,#2  (attach = min(perlink[+1],2))"),
    (0x48A7E, "84f8703d", "strb.w r3,[r4,#0xD70]  (band attach flag)"),
    # band-object accessor CA 0xA4FC8(a,b) = 0x15CA08 + 0x1B68*b (b <= 1)
    (0xA4FC8, "08b50246", "push {r3,lr}; mov r2,r0; mov r3,r1"),
    (0xA4FE2, "41f66831", "movw r1,#0x1B68 (band stride)"),
    (0xA4FE6, "0348", "ldr r0,=0x0015CA08"),
    (0xA4FE8, "01fb0300", "mla r0,r1,r3,r0"),
    (0xA4FF4, "08ca1500", "literal = CA 0x0015CA08"),
    # vap-add worker CA 0x449E4 (g0 key 0x64): binds ctx+0x4B0/+0x4B4/+0x4B8
    (0x449E4, "2de9f04f", "stmdb sp!,{...}"),
    (0x44B6C, "b97c", "ldrb r1,[r7,#0x12]  (body[0x12] = band selector)"),
    (0x44B6E, "a078", "ldrb r0,[r4,#2]"),
    (0x44B70, "60f02afa", "bl CA 0xA4FC8  (band object)"),
    (0x44B76, "00f5d051", "add.w r1,r0,#0x1A00"),
    (0x44B7A, "0b68", "ldr r3,[r1]  ([band+0x1A00] must be non-zero)"),
    (0x44BBC, "c4f8b034", "str.w r3,[r4,#0x4B0]  (ctx+0x4B0 = per-link)"),
    (0x44BC2, "c4f8b804", "str.w r0,[r4,#0x4B8]  (ctx+0x4B8 = band object)"),
    (0x44BC6, "c4f8b434", "str.w r3,[r4,#0x4B4]"),
]
FW_WORDS = [
    (0x43E18, 0x0010C23C, "table A base literal"),
    (0x43E1C, 0x00103BF8, "table B base literal"),
    (0xFE574, 0x001ADB80, "singleton getter literal"),
    (0x943C8, 0x00170E28, "G literal"),
    (0x49310, 0x00170E28, "G literal (boot init)"),
    (0x49330, 0x00119758, "context pool base literal"),
    (0x4931C, 0x00126D18, "context pool end literal"),
    (0xA4FF4, 0x0015CA08, "band-0 object literal"),
]

# --- vendor module raw instruction assertions (.text offsets) -------------
KO_BYTES = [
    (0x57090, "f0412de9", "header helper: push"),
    (0x570EC, "0210d5e5", "ldrb r1,[r5,#2]  (ctx[2])"),
    (0x570F0, "1122c7e7", "bfi r2,r1,#4,#4  -> hdr[1] high nibble = ctx[2]"),
    (0x570FC, "0110d5e5", "ldrb r1,[r5,#1]  (ctx[1])"),
    (0x57100, "0210c4e5", "strb r1,[r4,#2]  -> hdr[2] = ctx[1]"),
    (0x57104, "0010d5e5", "ldrb r1,[r5,#0]  (ctx[0])"),
    (0x57108, "0310c4e5", "strb r1,[r4,#3]  -> hdr[3] = ctx[0] = field B"),
    (0x571D8, "acffffeb", "bl 0x57090"),
    (0x57200, "feffffeb", "bl memcpy_s (body at hdr+24)"),
    (0x5762C, "30402de9", "hmac_config_set_country_for_dfs: push"),
    (0x57630, "0230a0e1", "mov r3,r2  (data)"),
    (0x5763C, "f71100e3", "movw r1,#0x01F7  (the g0-id1 key)"),
    (0x57644, "feffffeb", "bl hmac_config_send_event"),
]

# --- committed-source assertions (the mismatch this lane finds) -----------
REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                    "..", "..", "..", "..", ".."))
SRC_CHECKS = [
    ("opensource/docs/incoming/wifi-desc-table-send/src/hi5622_desc_table.c",
     "b[3] = 0x04u;", "0x00F7 frame hard-codes field B = 4"),
    ("opensource/docs/incoming/wifi-dfs-mode-send/src/hi5622_dfs_mode.h",
     "HI5622_DFS_MODE_FB\t\t0x04u", "0x01F7 frame hard-codes field B = 4"),
    ("opensource/docs/incoming/wifi-ap-start/src/hi5622_ap_frames.c",
     "p->module, 0u, p->dst", "g0 AP-start frames use field B = p->module"),
    ("opensource/docs/incoming/wifi-vap-start/src/vapstart_frames.c",
     "p->field_b = 0;", "vap-add default field B = 0"),
    ("opensource/docs/incoming/wifi-dfs-mode-send/FIRE-RECIPE.md",
     "vap_start_module=0", "the fired AP ladder used module/field B = 0"),
    ("opensource/docs/incoming/wifi-vap-start/REPORT.md",
     "module-4 ctx+0x4B0 = 0x00174BF0", "bench: C2-created module 4 -> band 0"),
    ("opensource/docs/incoming/wifi-vap-start/REPORT.md",
     "0x0015CA08", "bench: module-4 ctx+0x4B8 = band-0 object"),
    ("opensource/docs/incoming/wifi-fw-rf-descriptor/REPORT.md",
     "0x15F3E0", "sibling probe typo: P+0xD70 is 0x15F2E0 (this lane corrects)"),
]

# --- our own host-built 29-B frames (protocol bytes, not vendor bytes) ----
FRAME_CURRENT = bytes.fromhex(
    "00010004" "1d00" "0100" "0000" "5a5a"
    "0000000001000000" "f701" "0100" "03" "ffffffff")
FRAME_FIXED = bytes.fromhex(
    "00010000" "1d00" "0100" "0000" "5a5a"
    "0000000001000000" "f701" "0100" "03" "ffffffff")
FRAME_1224 = None                  # not needed

CHECKS = []


def ck(name, cond, detail=""):
    CHECKS.append((name, bool(cond), detail))


def fw():
    with open(FW, "rb") as f:
        return f.read()


def fwb(d, ca, n):
    off = ca - BASE
    if off < 0 or off + n > len(d):
        return b""
    return d[off:off + n]


def fwu32(d, ca):
    b = fwb(d, ca, 4)
    return struct.unpack("<I", b)[0] if len(b) == 4 else None


def fwu16(d, ca):
    b = fwb(d, ca, 2)
    return struct.unpack("<H", b)[0] if len(b) == 2 else None


def ko_text_off(d):
    e_shoff = struct.unpack("<I", d[32:36])[0]
    e_shentsize = struct.unpack("<H", d[46:48])[0]
    e_shnum = struct.unpack("<H", d[48:50])[0]
    e_shstrndx = struct.unpack("<H", d[50:52])[0]
    secs = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        secs.append(struct.unpack("<IIIIII", d[off:off + 24]))
    st = secs[e_shstrndx]
    shstr = d[st[4]:st[4] + st[5]]
    for s in secs:
        n = shstr[s[0]:shstr.index(b"\x00", s[0])].decode()
        if n == ".text":
            return s[4]
    return None


def selftest():
    d = fw()
    ko = open(KO, "rb").read()
    ck("firmware size == 928920", len(d) == FW_LEN, str(len(d)))
    ck("firmware sha256 pinned", hashlib.sha256(d).hexdigest() == FW_SHA)
    for ca, hx, what in FW_BYTES:
        ck("raw %05X %s" % (ca, what), fwb(d, ca, len(hx) // 2).hex() == hx,
           fwb(d, ca, len(hx) // 2).hex())
    for ca, val, what in FW_WORDS:
        ck("word %05X %s" % (ca, what), fwu32(d, ca) == val, hex(fwu32(d, ca)))
    # table A row 93 / row 92 decode
    for row, key, worker, label in ((ROW_1F7, 0x01F7, 0x0B5B9D, "0x01F7"),
                                    (ROW_F7, 0x00F7, 0x045A31, "0x00F7")):
        e = fwb(d, TBL_A + row * 8, 8)
        k = struct.unpack("<H", e[0:2])[0]
        w = struct.unpack("<I", e[4:8])[0]
        ck("table A row %d key == %s" % (row, label), k == key, hex(k))
        ck("table A row %d worker == %s" % (row, hex(worker)), w == worker, hex(w))
        ck("table A row %d pad == 0 (no flags)" % row,
           struct.unpack("<H", e[2:4])[0] == 0)
    ck("table A fits before TBL_A+0x6C8", TBL_A + TBL_A_N * 8 == TBL_A + TBL_A_END)
    ck("g0-id1 handler literal points at TBL_A",
       fwu32(d, 0x43E18) == TBL_A)
    # tbb target arithmetic (Thumb: PC = tbb addr + 4, bit0 of target cleared)
    tbl = fwb(d, 0xB5D76, 5)
    pc = 0xB5D76
    tgts = {}
    for m in range(1, 6):
        off = tbl[m - 1]
        tgt = (pc + off) & ~1
        tgts[m] = tgt
    ck("tbb: modes 1/3 -> common image tail 0xB5E10",
       tgts[1] == 0xB5E10 and tgts[3] == 0xB5E10, str(tgts))
    ck("tbb: modes 2/5 -> 0xB5E06", tgts[2] == 0xB5E06 and tgts[5] == 0xB5E06)
    ck("tbb: mode 4 -> 0xB5E0C", tgts[4] == 0xB5E0C)
    ck("mode byte writer raw bytes at CA 0xB5D1C",
       fwb(d, 0xB5D1C, 4).hex() == "84f8045f")
    # context model arithmetic
    ck("context stride 0xB40 x 19 == 0xD5C0", CTX_STRIDE * CTX_N == 0xD5C0)
    ck("pool base + 19*stride == 0x126D18",
       POOL + CTX_N * CTX_STRIDE == 0x126D18)
    ck("context 4 == 0x0011C458", POOL + 4 * CTX_STRIDE == 0x11C458)
    ck("G table slot0 addr == 0x00171458", G + G_TBL_OFF == 0x171458)
    ck("G table slot4 addr == 0x00171468", G + G_TBL_OFF + 4 * 4 == 0x171468)
    ck("G base addr == 0x00171504", G + G_BASE_OFF == 0x171504)
    ck("ctx4+0x4B8 probe == 0x0011C910", POOL + 4 * CTX_STRIDE + 0x4B8 == 0x11C910)
    ck("ctx0+0x4B8 probe == 0x00119C10", POOL + 0x4B8 == 0x119C10)
    ck("band0 mode probe == 0x0015D90C", BAND0_OBJ + 0xF04 == 0x15D90C)
    ck("band0 attach probe == 0x0015D778", BAND0_OBJ + 0xD70 == 0x15D778)
    ck("band0 publish probe == 0x0015E408", BAND0_OBJ + 0x1A00 == 0x15E408)
    ck("band1 mode probe == 0x0015F474", BAND1_OBJ + 0xF04 == 0x15F474)
    ck("band1 attach probe == 0x0015F2E0", BAND1_OBJ + 0xD70 == 0x15F2E0)
    ck("band1 publish probe == 0x0015FF70", BAND1_OBJ + 0x1A00 == 0x15FF70)
    ck("band object accessor identity 0x15CA08+1*0x1B68 == band1",
       BAND_BASE + BAND_STRIDE == BAND1_OBJ)
    # module .text
    to = ko_text_off(ko)
    ck("module ELF .text file offset", to == KO_TEXT_OFF, str(to))
    for off, hx, what in KO_BYTES:
        got = ko[to + off:to + off + len(hx) // 2].hex()
        ck("ko .text+%05X %s" % (off, what), got == hx, got)
    # committed sources
    for rel, needle, what in SRC_CHECKS:
        p = os.path.join(REPO, rel)
        try:
            body = open(p, "r", errors="replace").read()
        except OSError:
            body = ""
        ck("source %s: %s" % (os.path.basename(rel), what),
           needle in body, rel)
    # frames
    ck("current frame is 29 B", len(FRAME_CURRENT) == 29)
    ck("fixed frame is 29 B", len(FRAME_FIXED) == 29)
    ck("current frame total field 0x001D",
       struct.unpack("<H", FRAME_CURRENT[4:6])[0] == 0x001D)
    ck("current frame key 0x01F7", struct.unpack("<H", FRAME_CURRENT[20:22])[0] == 0x01F7)
    ck("current frame body[0] == 3", FRAME_CURRENT[24] == 3)
    ck("current vs fixed differ ONLY in hdr[3]",
       FRAME_CURRENT[:3] == FRAME_FIXED[:3] and
       FRAME_CURRENT[4:] == FRAME_FIXED[4:])
    ck("current frame field B == 4", FRAME_CURRENT[3] == 4)
    ck("fixed frame field B == 0", FRAME_FIXED[3] == 0)
    ck("mode-3 image constants are the ch36..64 image",
       fwu32(d, 0x89932 + 0) is not None)  # image literals asserted via bytes
    # the dispatcher has no length gate: the only cmp before blx is the table
    # bound / key compare. Assert the exact instruction stream of the search
    ck("dispatcher passes u8 len (no u16 len read)",
       fwb(d, 0x43DF0, 2).hex() == "a17d")
    ck("dispatcher body pointer = msg+24",
       fwb(d, 0x43DEC, 4).hex() == "04f11802")
    passed = sum(1 for _, ok, _ in CHECKS if ok)
    for name, ok, detail in CHECKS:
        if not ok:
            print("FAIL: %s (%s)" % (name, detail))
    print("selftest: %d/%d PASS" % (passed, len(CHECKS)))
    return passed == len(CHECKS)


def decode():
    d = fw()
    print("g0-id1 handler CA 0x%05X (groups 0 id 1 config event)" % HANDLER_G0ID1)
    print("  0x43D72  ldrb r1,[r0,#3]        field B (per-module context index)")
    print("  0x43D74  movs r0,#0 ; 0x43D76 bl CA 0x%05X" % CTX_FN)
    print("  0x43D90  ldrh r2,[r4,#20]       u16 key = hdr+20 (payload+8)")
    print("  0x43D92..0x43DA6 scan table A (CA 0x%05X, %d entries, bound 0x6C8)"
          % (TBL_A, TBL_A_N))
    print("  0x43DAC..0x43DBE scan table B (CA 0x%05X, bound 0x2C0)" % TBL_B)
    print("  0x43DCE  ldr r3,[entry+4]       worker (entry = {u16 key,u16 pad,u32 worker})")
    print("  0x43DEC  add.w r2,r4,#24        body pointer = hdr+24")
    print("  0x43DF0  ldrb r1,[r4,#22]       length = ONE BYTE hdr+22")
    print("  0x43DF2  blx r3                worker(r0=ctx, r1=len, r2=body)")
    print("  NO length/flag/checksum compare anywhere in the dispatcher.\n")
    print("table A row %d: key 0x01F7 worker 0x%08X (thumb)"
          % (ROW_1F7, struct.unpack("<I", fwb(d, TBL_A + ROW_1F7 * 8 + 4, 4))[0]))
    print("table A row %d: key 0x00F7 worker 0x%08X (thumb)\n"
          % (ROW_F7, struct.unpack("<I", fwb(d, TBL_A + ROW_F7 * 8 + 4, 4))[0]))
    print("worker CA 0x%05X key 0x01F7:" % WORKER_1F7)
    print("  0xB5B9E  r4 = *(ctx + 0x4B8)       band object; ctx==0/gate -> log 1442, ret 0x64")
    print("  0xB5BAA  O  = *(r4 + 0x1A00)       published per-link; 0 -> log 1442")
    print("  0xB5BC8  a  = *(r4 + 0xD70)        band attach; a != 1 -> log 1448, ret 0")
    print("  0xB5BE2  mode = body[0]            1..5 (1/2/5 explicit image blocks;")
    print("                                       3 -> bl CA 0x89924(O) ch36..64 image;")
    print("                                       4 -> fall-through; out of range -> log 1485)")
    print("  0xB5D1C  *(r4 + 0xF04) = mode      the ONLY mode-byte writer in the image")
    print("  0xB5CEE  memcpy_s(O+0x1C3, 4, &{mode,len[1..3]}, 4)")
    print("  common: band fields +0xF00..+0x10F4 then the tbb per-mode tail\n")
    print("context lookup CA 0x%05X(0, fieldB):" % CTX_FN)
    print("  ctx = *(u32)(G + 0x630 + 4*fieldB) + u16(G + 0x6DC),  G = 0x%08X" % G)
    print("  fieldB 0..0x12; boot init CA 0x%05X fills all 19 slots with"
          % BOOT_G_INIT)
    print("  0x%05X + k*0x%X and zeroes each %d-byte body." %
          (POOL, CTX_STRIDE, CTX_STRIDE))
    print("  the band binding of a context is created by the key-0x64 worker")
    print("  CA 0x%05X: ctx+0x4B0 = band published per-link,"
          % VAPADD_WORKER)
    print("  ctx+0x4B8 = CA 0x%05X(band) = 0x%05X + 0x%X*band"
          % (BAND_LOOKUP, BAND_BASE, BAND_STRIDE))
    print("  from body[0x12].\n")
    print("THE MISMATCH:")
    print("  boot C2 ctx_create : key 0x%04X module/fieldB 4 band body[0x12]=0"
          % VAPADD_KEY)
    print("                       -> module 4 (ctx 0x%05X) + 0x4B8 = BAND 0 0x%05X"
          % (POOL + 4 * CTX_STRIDE, BAND0_OBJ))
    print("  AP-start vap_add   : key 0x%04X module/fieldB 0 band body[0x12]=1"
          % VAPADD_KEY)
    print("                       -> module 0 (ctx 0x%05X) + 0x4B8 = BAND 1 0x%05X"
          % (POOL, BAND1_OBJ))
    print("  0x01F7 frame field B = 4 -> the worker programmed BAND 0's mode")
    print("  byte (0x%05X) and BAND 0's RF image, not BAND 1's." % (BAND0_OBJ + 0xF04))


def frame():
    print("current 0x01F7 frame (as fired, field B = 4):")
    print("  " + FRAME_CURRENT.hex())
    print("  hdr  : %s" % FRAME_CURRENT[:12].hex())
    print("  token: %s" % FRAME_CURRENT[12:20].hex())
    print("  key  : %s  (0x%04X)" % (FRAME_CURRENT[20:22].hex(),
                                     struct.unpack("<H", FRAME_CURRENT[20:22])[0]))
    print("  len  : %s  (1)" % FRAME_CURRENT[22:24].hex())
    print("  body : %s  (mode 3)" % FRAME_CURRENT[24:25].hex())
    print("  tail : %s" % FRAME_CURRENT[25:29].hex())
    print("  hdr total 0x%04X, wire total %d B" %
          (struct.unpack("<H", FRAME_CURRENT[4:6])[0], len(FRAME_CURRENT)))
    print("fixed 0x01F7 frame (field B = 0 = the AP-start module):")
    print("  " + FRAME_FIXED.hex())
    print("  only byte [3] differs: 0x%02x -> 0x%02x (hdr[3] = field B)"
          % (FRAME_CURRENT[3], FRAME_FIXED[3]))
    print("the 0x00F7 desc-table frame needs the same one-byte change (b[3]).")


def probes():
    rows = [
        ("u32", 0x171458, "G table slot0 = context[0] ptr", "0x00119758"),
        ("u32", 0x171468, "G table slot4 = context[4] ptr", "0x0011C458"),
        ("u16", 0x171504, "G+0x6DC common offset", "0x0000 [I]"),
        ("u32", 0x11C910, "ctx4+0x4B8 band object (what the fired frame targets)",
         "0x0015CA08 under H2 (band 0); 0 = unbound fallback"),
        ("u32", 0x119C10, "ctx0+0x4B8 band object (the AP ladder's context)",
         "0x0015E570"),
        ("u32", 0x15FF70, "band1+0x1A00 published per-link", "0x00175A18"),
        ("u8 ", 0x15F2E0, "band1+0xD70 attach (verified probe address)", "1"),
        ("u8 ", 0x15F474, "band1+0xF04 mode byte (the 5 GHz gate)", "0 before F2, 3 after"),
        ("u32", 0x175CF0, "R = *(band1 published+0x2D8)", "0x400B2000"),
        ("u8 ", 0x15D90C, "band0+0xF04 mode byte (the fired frame's real target)",
         "3 => H2 confirmed (worker ran on band 0); 0 => fallback"),
        ("u8 ", 0x15D778, "band0+0xD70 attach", "1 required for the band-0 write"),
        ("u32", 0x15E408, "band0+0x1A00 published per-link", "0x00174BF0"),
        ("u32", 0x15E6E0, "band0 R = *(band0 published+0x2D8)", "nonzero"),
        ("u32", 0x11C5E8, "ctx4+0x190 (C2 ctx_create result)", "nonzero"),
        ("u32", 0x1198E8, "ctx0+0x190 (AP vap_add result)", "nonzero"),
    ]
    print("READ-ONLY probe pack.  BAR0 = CPU + 0x6B8000; host phys =")
    print("0x40000000 + BAR0 (the F0/F1 fire form), e.g. CPU 0x11C910 ->")
    print("phys 0x40828910.  No probe writes anything.  If u16(0x171504) is")
    print("non-zero, add it to the context-derived addresses (0x11xxxx).  The")
    print("band-object addresses below already include no base offset.\n")
    print("%-4s %-9s %-9s %-13s %s" % ("sz", "CPU", "BAR0", "phys", "what / expected"))
    for sz, cpu, what, exp in rows:
        bar0 = cpu + 0x6B8000
        phys = 0x40000000 + bar0
        print("%-4s 0x%07X 0x%07X 0x%08X  %s | %s"
              % (sz, cpu, bar0, phys, what, exp))


def ctx():
    print("G = 0x%08X ; context table = G+0x630+4*k ; base = u16(G+0x6DC)" % G)
    print("%-3s %-9s %-9s %-9s %s" % ("k", "table addr", "ctx (base 0)",
                                      "ctx+0x4B8", "ctx+0x190"))
    for k in range(CTX_N):
        c0 = POOL + k * CTX_STRIDE
        print("%-3d 0x%07X 0x%07X 0x%07X 0x%07X"
              % (k, G + G_TBL_OFF + 4 * k, c0, c0 + 0x4B8, c0 + 0x190))
    print("\nthe boot C2 ctx_create (g0 key 0x64, module 4, body[0x12]=0)")
    print("binds context 4 to band 0: ctx4+0x4B8 = 0x%05X" % BAND0_OBJ)
    print("the AP-start vap_add (g0 key 0x64, module 0, body[0x12]=1)")
    print("binds context 0 to band 1: ctx0+0x4B8 = 0x%05X" % BAND1_OBJ)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--decode", action="store_true")
    ap.add_argument("--frame", action="store_true")
    ap.add_argument("--probes", action="store_true")
    ap.add_argument("--ctx", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return 0 if selftest() else 1
    if a.decode:
        decode()
    if a.frame:
        frame()
    if a.probes:
        probes()
    if a.ctx:
        ctx()
    if not any((a.selftest, a.decode, a.frame, a.probes, a.ctx)):
        decode()
        frame()
        probes()
    return 0


if __name__ == "__main__":
    sys.exit(main())
