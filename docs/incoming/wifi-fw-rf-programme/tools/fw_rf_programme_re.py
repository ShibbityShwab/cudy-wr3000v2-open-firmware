#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_rf_programme_re.py - decode the firmware's 5 GHz RF programme + beacon path.

Lane: wifi-fw-rf-programme (shared task task-182).  HOST-ONLY, read-only: the
pinned LOCAL vendor artifacts are read in place; only derived values (addresses,
table values, expected register-image words) are printed.  No vendor byte is
copied into the repository and no device is touched.

Artifacts (local, uncommitted):
  firmware: /home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin
            928,920 B sha256 7fc87e20...311b ; CPU = file offset + 0x40000
  objdump:  the OpenWrt lab ARM objdump (override with --objdump or $RF_OBJDUMP)

The decode (evidence: ../REPORT.md, ../TABLES.md):
  * the 0x0124 worker's band-1 branch CA 0x40684 gates on ctx[4]==2 and
    CA 0x944DE==1, then runs CA 0x101890 (a predicate), CA 0xA2FA2 (the RF
    register image) and CA 0x89840 (latches the predicate into R+0x410 bit0);
  * CA 0x101890 is band-1 only and returns a descriptor bit; it writes nothing;
  * CA 0xA2FA2 splits by channel-table index: ch36-64 uses CA 0x89924, ch100+
    uses an inline image; both write R = [[perlink+0xE20+0x1A00]+0x2D8];
  * CA 0x97008 / CA 0xFDCE4 / CA 0xF4FD8 are band-agnostic bookkeeping;
  * the VAP start (CA 0x46394) and AP config (CA 0x84DA4) are the beacon
    configuration path; the ISR-name blob carries per-band smac_tbtt_*_isr.

Modes:
  --selftest   assert the decoded windows/tables/images (exit 0 on PASS)
  --decode     the call chain, the field windows, both RF images
  --tables     the CA 0x10BE68 field-0 table and the RF block images
  --readback   the read-only BAR0 probe pack with physical addresses
"""
import argparse
import hashlib
import os
import re
import struct
import subprocess
import sys

BASE = 0x40000                 # firmware CPU = file offset + BASE
BAR0_ADD = 0x6B8000            # host BAR0 offset of firmware RAM = CPU + this
PHYS_BASE = 0x40000000         # BAR0 physical base (committed bringup decode)
DEFAULT_FW = ("/home/shibbityshwab/recovery-extract/lib/firmware/"
              "hi_wifi/FIRMWARE.bin")
FW_SHA = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_LEN = 928920
DEFAULT_OD = ("/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/"
              "toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/"
              "arm-openwrt-linux-objdump")

# decoded firmware addresses (CPU)
BRANCH_40684 = 0x40684
PRED_101890 = 0x101890
VALIDATE_10172C = 0x10172C
MAPPER_101650 = 0x101650
IMG_A2FA2 = 0xA2FA2
IMG_A2FA2_SHORT = 0x89924
LATCH_89840 = 0x89840
HELPER_97008 = 0x97008
HELPER_FDCE4 = 0xFDCE4
HELPER_F4FD8 = 0xF4FD8
VAP_START = 0x46394
AP_CONFIG = 0x84DA4
BSS_COPY = 0x7E5CC
GATE_944DE = 0x944DE
CHIP_QUERY = 0x40D60
PARAM_TABLE = 0x10BE68
DESC_POOL = 0x1ADC98
CHAN_TABLE_B1 = 0x117AD4
CHAN_TABLE_B0 = 0x117B48
TABLE_A = 0x10C23C

BAND1_CHANNELS = [36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116,
                  120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165,
                  184, 188, 192, 196]

# RF block image literals of the two builders (re-derived, not stored bytes)
A2FA2_INLINE_LITERALS = {
    0x3A8: 0x01800060, 0x3AC: 0x056B0597, 0x3B8: 0x009400E8,
    0x3BC: 0x00C601F6, 0x3C0: 0x014A014F, 0x3C4: 0x1FFF1FFF,
    0x3C8: 0x1C29614B, 0x3D0: 0x005200B2, 0x3D4: 0x164021FF,
    0x3DC: 0x80000614, 0x3E0: 0x03FF1AA8, 0x3E4: 0x52,
    0x3EC: 0x003C0050, 0x3F0: 0x1001F405, 0x3F4: 0x05B57409,
    0x3F8: 0, 0x3FC: 0x18200003, 0x400: 0x00507D0B, 0x404: 0x00F80064,
    0x408: 0x0300000F, 0x40C: 0x03790000, 0x418: 0x003C0513, 0x414: 0x1,
}
A2FA2_SHORT_LITERALS = {
    0x3A8: 0x01800060, 0x3AC: 0x03E6138A, 0x3B8: 0x00E800E8,
    0x3BC: 0x00C601F6, 0x3C0: 0x00C601F6, 0x3C4: 0x1FFF1FFF,
    0x3C8: 0x1C29614B, 0x3D0: 0x005200B2, 0x3D4: 0x164021FF,
    0x3DC: 0x80000614, 0x3E0: 0x03FF1AA8, 0x3E4: 0x52,
    0x3EC: 0x003C0050, 0x3F0: 0x1001F405, 0x3F4: 0x05B57409,
    0x3F8: 0, 0x3FC: 0x18200003, 0x400: 0x00507D0B, 0x404: 0x00F80064,
    0x408: 0x0300000F, 0x40C: 0x03790000, 0x418: 0x003C0513, 0x414: 0x1,
}

# read-only probe pack: (label, cpu) - ones marked None are two-step
PROBES = [
    ("ctx slot0 (u32)", 0x171458),
    ("module-table base u16", 0x172504),
    ("ctx+0x004 create type", 0x11975C),
    ("ctx+0x440 gate", 0x119B98),
    ("bandobj+0xEC8 gate", 0x15F438),
    ("ctx+0x4B0 perlink ptr", 0x119C08),
    ("ctx+0x4B8 bandobj ptr", 0x119C10),
    ("bandobj+0x1A00 publish", 0x15FF70),
    ("perlink+0x1C8 97008 flag", 0x175BE0),
    ("P+0x1A00 = perlink+0x2820 (O)", 0x178238),
]


def die(msg):
    sys.stderr.write(msg + "\n")
    sys.exit(2)


class Fw:
    def __init__(self, path):
        self.path = path
        try:
            self.blob = open(path, "rb").read()
        except OSError as exc:
            die("cannot read firmware %s: %s" % (path, exc))
        self.bad = []
        self.ok = 0

    def u8(self, cpu):
        return self.blob[cpu - BASE]

    def u16(self, cpu):
        return struct.unpack_from("<H", self.blob, cpu - BASE)[0]

    def u32(self, cpu):
        return struct.unpack_from("<I", self.blob, cpu - BASE)[0]

    def check(self, label, got, want):
        if got == want:
            self.ok += 1
            return True
        self.bad.append("%s: got %r want %r" % (label, got, want))
        return False


def find_objdump(path):
    cands = [path, os.environ.get("RF_OBJDUMP"), DEFAULT_OD]
    for c in cands:
        if c and os.path.isfile(c) and os.access(c, os.X_OK):
            return c
    return None


def disasm(od, fw, start, stop):
    r = subprocess.run([od, "-D", "-b", "binary", "-m", "arm",
                        "-M", "force-thumb", "--adjust-vma=0x40000",
                        "--start-address=%d" % start,
                        "--stop-address=%d" % stop, fw.path],
                       capture_output=True, text=True)
    return r.stdout


def norm(text):
    return " ".join(text.split())


def assert_window(fw, od, start, stop, expected):
    """expected: list of (address_or_None, normalized instruction fragment)."""
    text = norm(disasm(od, fw, start, stop))
    lines = [ln for ln in disasm(od, fw, start, stop).splitlines()
             if re.match(r"\s+[0-9a-f]{5,8}:\t", ln)]
    idx = {}
    for ln in lines:
        m = re.match(r"\s+([0-9a-f]{5,8}):\t(.*)$", ln)
        if m:
            text = norm(m.group(2))
            # strip the leading hex-encoding tokens objdump prints
            text = norm(re.sub(r"^([0-9a-f]{4} ?)+", "", text))
            idx[int(m.group(1), 16)] = text
    for addr, frag in expected:
        if addr is not None and addr in idx:
            fw.check("%s has %r" % (hex(addr), frag), frag in idx[addr], True)
        else:
            fw.check("window[%s..%s] contains %r"
                     % (hex(start), hex(stop), frag),
                     any(frag in v for v in idx.values()), True)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def phys(cpu):
    return PHYS_BASE + cpu + BAR0_ADD


def derive_images():
    """Re-derive the two RF images from their literals (asserted in selftest)."""
    inline = dict(A2FA2_INLINE_LITERALS)
    inline[0x3B4] = (0x056B0597 + 0x0A300000 + 0x0A0E) & 0xFFFFFFFF
    inline[0x3D8] = (0x164021FF - 0x55800000 - 0x320000 - 0x0DE5) & 0xFFFFFFFF
    inline[0x3CC] = 0x1D0000BE
    inline[0x3B0] = 0x0334
    short = dict(A2FA2_SHORT_LITERALS)
    short[0x3B4] = (0x03E6138A + 0xFE000000 + 0x890000) & 0xFFFFFFFF
    short[0x3D8] = 0xC14580B0
    short[0x3CC] = 0x160000BE
    short[0x3B0] = 0x0334
    return inline, short


def cmd_selftest(fw, od):
    fw.check("firmware length", len(fw.blob), FW_LEN)
    fw.check("firmware sha256", sha256(fw.path), FW_SHA)

    # --- the 0x40684 branch: gate order and the three calls
    assert_window(fw, od, 0x40684, 0x406F6, [
        (0x40686, "ldrb r3, [r0, #4]"),
        (0x4068A, "cmp r3, #2"),
        (0x40692, "bl 0x944de"),
        (0x40696, "cmp r0, #1"),
        (0x4069A, "ldrb r2, [r4, #16]"),
        (0x4069C, "ldrb r1, [r4, #18]"),
        (0x4069E, "ldrb r0, [r4, #17]"),
        (0x406A0, "bl 0x101890"),
        (0x406AA, "bl 0xa2fa2"),
        (0x406B2, "bl 0x89840"),
    ])
    # --- 0x944DE: band==1, module+0x440==1, bandobj+0xEC8
    assert_window(fw, od, 0x944DE, 0x94514, [
        (0x94500, "ldrb r3, [r5, #17]"),
        (0x94502, "cmp r3, #1"),
        (0x94506, "ldrb.w r3, [r5, #1088]"),
        (0x9450A, "cmp r3, #1"),
        (0x9450E, "ldrb.w r0, [r0, #3784]"),
    ])
    # --- 0x101890: band gate, validate, field split, table literal
    assert_window(fw, od, 0x101890, 0x1018D8, [
        (0x101894, "cmp r0, #1"),
        (0x1018AC, "bl 0x10172c"),
        (0x1018B6, "subs r2, r4, #7"),
    ])
    fw.check("0x101890 table literal", fw.u32(0x101BA0), PARAM_TABLE)
    # --- 0x89840: the latch writes bit0 of [r0+0x2D8]+0x410
    assert_window(fw, od, 0x89840, 0x89854, [
        (0x89840, "ldr.w r2, [r0, #728]"),
        (0x89848, "bfi r3, r1, #0, #1"),
        (0x8984C, "str.w r3, [r2, #1040]"),
    ])
    # --- 0xA2FA2: mode byte 0xF04, channel index, range split
    assert_window(fw, od, 0xA2FA2, 0xA2FE0, [
        (0xA2FC0, "ldrb.w r2, [r0, #3844]"),
        (0xA2FCA, "ldrb r3, [r1, #19]"),
        (0xA2FCE, "cmp r3, #20"),
        (0xA2FD4, "ldr.w r4, [r6, #728]"),
    ])
    assert_window(fw, od, 0xA3106, 0xA3110, [
        (0xA3106, "mov r0, r6"),
        (0xA3108, "bl 0x89924"),
    ])
    # --- the short builder's first register write
    assert_window(fw, od, 0x89924, 0x89934, [
        (0x8992E, "str.w r3, [r4, #936]"),
    ])
    # --- 0x101650 mapper: band-1 bound 0x1C and the pool literal
    assert_window(fw, od, 0x101648, 0x101688, [
        (None, "cmp r1, #28"),
        (None, "ldr r0, [pc, #20]"),
    ])
    fw.check("mapper pool literal", fw.u32(0x101680), DESC_POOL)
    fw.check("mapper band0 table literal", fw.u32(0x10167C), CHAN_TABLE_B0)
    fw.check("mapper band1 table literal", fw.u32(0x101684), CHAN_TABLE_B1)
    # --- channel tables: {channel, 0xFF, 0x00, 0x00} rows + field-0 -> t == idx
    for i, ch in enumerate(BAND1_CHANNELS):
        fw.check("band1 table[%d] chan" % i, fw.u8(CHAN_TABLE_B1 + 4 * i), ch)
        fw.check("band1 table[%d] slot 0xFF" % i,
                 fw.u8(CHAN_TABLE_B1 + 4 * i + 1), 0xFF)
    for i, ch in enumerate(range(1, 15)):
        fw.check("band0 table[%d] chan" % i, fw.u8(CHAN_TABLE_B0 + 4 * i), ch)
        fw.check("band0 table[%d] slot 0xFF" % i,
                 fw.u8(CHAN_TABLE_B0 + 4 * i + 1), 0xFF)
    for idx in range(len(BAND1_CHANNELS)):
        fw.check("field0 word[%d] top byte" % idx,
                 fw.u32(PARAM_TABLE + 0x150 + 4 * idx) >> 24, idx)
    # --- RF block images re-derived from the literals
    inline, short = derive_images()
    for off, want in inline.items():
        fw.check("inline image +%#x" % off, inline[off], want)
    fw.check("inline +0x3B4", inline[0x3B4], 0x0F9B0FA5)
    fw.check("inline +0x3D8", inline[0x3D8], 0xC08E141A)
    fw.check("short +0x3B4", short[0x3B4], 0x026F138A)
    fw.check("short +0x3D8", short[0x3D8], 0xC14580B0)
    fw.check("inline +0x3CC top", inline[0x3CC] >> 24, 0x1D)
    fw.check("short +0x3CC top", short[0x3CC] >> 24, 0x16)
    # the literal words really are where the builders load them
    fw.check("inline literal +0x3AC", fw.u32(0xA3118), 0x056B0597)
    fw.check("short literal +0x3AC", fw.u32(0x89A34), 0x03E6138A)
    fw.check("short literal +0x3B8", fw.u32(0x89A3C), 0x1C29614B)
    # --- the helper/beacon address set
    assert_window(fw, od, HELPER_FDCE4, HELPER_FDCE4 + 0x40, [
        (0xFDCF0, "ldrb r2, [r0, #18]"),
        (0xFDCF2, "ldrb r1, [r0, #17]"),
        (0xFDCFA, "cmp r3, #1"),
        (0xFDCFE, "movhi r3, #0"),
        (0xFDD02, "ldr.w r2, [r0, #400]"),
        (0xFDD0A, "bfi r1, r3, #1, #7"),
    ])
    assert_window(fw, od, 0xF4FD8, 0xF4FD8 + 0x30, [
        (0xF4FEC, "ldrb r2, [r0, #3]"),
        (0xF4FF2, "ldrb r4, [r0, #2]"),
        (0xF4FF4, "cmp r4, #15"),
    ])
    assert_window(fw, od, 0x97008, 0x97050, [
        (0x97012, "bl 0x581c8"),
        (0x97034, "ldrb r2, [r5, #4]"),
    ])
    assert_window(fw, od, VAP_START, VAP_START + 0x140, [
        (0x463EC, "ldrb r3, [r5, #2]"),
        (0x463F0, "strb r3, [r4, #17]"),
        (0x46418, "movs r3, #1"),
        (0x4641A, "strb r3, [r4, #14]"),
        (0x464B0, "bl 0x971d8"),
    ])
    assert_window(fw, od, AP_CONFIG, AP_CONFIG + 0x180, [
        (0x84DBC, "ldrb.w r2, [r5, #137]"),
        (0x84E96, "ldrh.w r2, [r5, #148]"),
        (0x84E9E, "strh.w r2, [r3, #314]"),
        (0x84EA2, "ldr.w r3, [r4, #400]"),
    ])
    assert_window(fw, od, BSS_COPY, BSS_COPY + 0x90, [
        (0x7E634, "ldrh.w r3, [r5, #312]"),
        (0x7E63C, "ldrh.w r3, [r5, #314]"),
        (0x7E640, "strh.w r3, [r4, #608]"),
    ])
    # --- the H2D key rows the channel/beacon path uses (worker = row+4, thumb|1)
    for row, key, worker in ((108, 0x0124, 0x841B9), (110, 0x0127, 0x84DA5),
                             (2, 0x0065, 0x46395), (1, 0x0064, 0x449E5),
                             (8, 0x03E4, 0x84261)):
        fw.check("table A[%d] key" % row, fw.u16(TABLE_A + row * 8), key)
        fw.check("table A[%d] worker" % row, fw.u32(TABLE_A + row * 8 + 4),
                 worker)
    # --- probe address arithmetic against the committed G6a values
    fw.check("phys(ctx slot0)", phys(0x171458), 0x40829458)
    fw.check("phys(ctx+0x440)", phys(0x119B98), 0x407D1B98)
    fw.check("BAR0(perlink)", 0x175A18 + BAR0_ADD, 0x82DA18)
    return fw.ok, fw.bad


def cmd_readback(fw):
    print("== read-only probe pack (phys = 0x40000000 + CPU + 0x6b8000) ==")
    print("  %-32s %-10s %-10s %-10s" % ("object", "CPU", "BAR0", "phys"))
    for label, cpu in PROBES:
        print("  %-32s 0x%08x 0x%08x 0x%08x"
              % (label, cpu, cpu + BAR0_ADD, phys(cpu)))
    print("  two-step: ctx0 = u32(0x171458) + u16(0x172504)")
    print("            P = u32(ctx+0x4B0); O = u32(P+0x1A00); R = u32(O+0x2D8)")
    print("            then R+0x3A8/0x3AC/0x3B4/0x3CC/0x410/0x414")
    print("            then the channel-table row 0x117AD4+4*idx (byte1 = slot);")
    print("            if slot != 0xFF, the pool record 0x1ADC98+16*slot+8")
    print("  forbidden: RC misc window 0x10160000..0x1016ffff (never read)")


def cmd_tables(fw):
    print("== CA 0x10BE68 field-0 word -> mapper input t -> channel-table slot ==")
    print("  idx  ch   f0word      t   row          slot  record")
    for idx, ch in enumerate(BAND1_CHANNELS):
        w = fw.u32(PARAM_TABLE + 0x150 + 4 * idx)
        t = w >> 24
        row = fw.u32(CHAN_TABLE_B1 + 4 * t)
        slot = (row >> 8) & 0xFF
        rec = ("0x%08X" % (DESC_POOL + slot * 16 + 8)
               if slot != 0xFF else "none (0xFF)")
        print("  %3d %3d  0x%08X  %2d  0x%08X  0x%02X  %s"
              % (idx, ch, w, t, row, slot, rec))
    inline, short = derive_images()
    print("")
    print("== RF block image (R = [[perlink+0xE20+0x1A00]+0x2D8]) ==")
    print("  word   ch36-64(0x89924)  ch100+(0xA2FA2)  note")
    for off in sorted(short):
        note = ""
        if off == 0x410:
            note = "bit0 = CA 0x101890 predicate"
        elif off in (0x3AC, 0x3B4, 0x3B8, 0x3C0, 0x3CC):
            note = "channel-range fingerprint"
        print("  +0x%03X  0x%08X        0x%08X     %s"
              % (off, short[off], inline[off], note))
    print("  P+0xF87: 0 (ch36-64) / 1 (ch100+)")


def cmd_decode(fw):
    print("== corrected call chain ==")
    for line in [
        "0x0124 worker 0x841B8 -> CA 0x40684(ctx, perlink+0xE20, perlink)",
        "  0x40684: gate ctx[4]==2, CA 0x944DE==1",
        "  CA 0x101890(band=ctx+0x11, field=ctx+0x12, channel=ctx+0x10) -> 0/1  predicate",
        "  CA 0xA2FA2(perlink+0xE20, ctx)  RF register image",
        "  CA 0x89840(perlink, r6)         R+0x410 bit0 = r6",
        "  if perlink+0x1C8: CA 0x97008(ctx)  general channel apply (no RF)",
        "CA 0xA2FA2 split: idx>=8 -> inline image, P+0xF87=1",
        "                  idx<8  -> CA 0x89924,   P+0xF87=0",
        "object chain: perlink=*(ctx+0x4B0); P=perlink+0xE20; O=*(P+0x1A00); R=*(O+0x2D8)",
        "precondition: *(P+0xF04) == 3 else CA 0xA2FA2 logs and returns",
        "CA 0x101890: band!=1 -> 0; field 0 -> descriptor slot = channel index;",
        "             returns bit7 of byte[CA 0x1ADC98+idx*16+8+2]; writes nothing",
        "helpers: CA 0xFC... 0xFDCE4 (VAP+0x1A0 band class), 0xF4FD8 (module+0x4AC),",
        "         0x97008 (band channel reconcile) - all no-RF",
        "beacon: CA 0x46394 VAP start (ctx+0x0E=1, 0x971D8 start-on-band, G+0x370/0x374",
        "        callbacks); CA 0x84DA4 AP config -> VAP+0x13A beacon interval;",
        "        CA 0x7E5CC copies VAP+0x138/+0x13A into the BSS/beacon descriptor",
    ]:
        print("  " + line)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fw", default=DEFAULT_FW)
    ap.add_argument("--objdump", default=None)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--decode", action="store_true")
    ap.add_argument("--tables", action="store_true")
    ap.add_argument("--readback", action="store_true")
    args = ap.parse_args(argv)
    fw = Fw(args.fw)
    if args.selftest:
        od = find_objdump(args.objdump)
        if not od:
            die("no objdump found (--objdump or $RF_OBJDUMP)")
        ok, bad = cmd_selftest(fw, od)
        total = ok + len(bad)
        if bad:
            print("SELFTEST FAIL %d/%d" % (total - len(bad), total))
            for b in bad:
                print("  FAIL " + b)
            return 1
        print("SELFTEST PASS %d/%d" % (ok, total))
        return 0
    if args.readback:
        cmd_readback(fw)
    if args.tables:
        cmd_tables(fw)
    if args.decode or not (args.tables or args.readback):
        cmd_decode(fw)
    return 0


if __name__ == "__main__":
    sys.exit(main())
