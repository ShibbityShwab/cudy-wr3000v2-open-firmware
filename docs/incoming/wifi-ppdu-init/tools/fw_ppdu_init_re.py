#!/usr/bin/env python3
"""fw_ppdu_init_re.py - re-derive the PPDU descriptor-list INIT decode (lane wifi-ppdu-init).

HOST-ONLY: reads the pinned local vendor firmware image in place.  No device,
no flash, no writes.  Pure stdlib (no capstone needed): a small Thumb-2 BL
decoder and fixed-width word reads are enough for every fact quoted here.

CA (CPU address) = file offset + 0x40000   (image CPU base = file+0x40000)
Image: rootfs-2.4.15/lib/firmware/hi_wifi/FIRMWARE.bin, 928,920 B,
sha256 7fc87e20...c3ef311b.

Usage:
  python3 fw_ppdu_init_re.py --selftest [--fw PATH]
  python3 fw_ppdu_init_re.py --probes   [--fw PATH]   # read-only probe table
  python3 fw_ppdu_init_re.py --chain    [--fw PATH]   # the decoded call chain
"""
import argparse, hashlib, struct, sys

BASE = 0x40000
DEFAULT_FW = "rootfs-2.4.15/lib/firmware/hi_wifi/FIRMWARE.bin"
SHA16 = "7fc87e2051e80b5e"   # first 16 hex of sha256

class Img:
    def __init__(self, data):
        self.d = data
    def off(self, ca): return ca - BASE
    def u8(self, ca): return self.d[self.off(ca)]
    def u16(self, ca): return struct.unpack("<H", self.d[self.off(ca):self.off(ca)+2])[0]
    def u32(self, ca): return struct.unpack("<I", self.d[self.off(ca):self.off(ca)+4])[0]
    def strz(self, ca, n=120):
        b = self.d[self.off(ca):self.off(ca)+n]
        e = b.find(b"\0")
        return b[:e if e >= 0 else n].decode("latin1", "replace")

def bl_target(img, ca):
    """Thumb-2 BL/BLX: decode the 32-bit pair at CA ca; return target CA or None."""
    hw1, hw2 = img.u16(ca), img.u16(ca + 2)
    if (hw1 & 0xF800) != 0xF000:
        return None
    if (hw2 & 0xD000) not in (0xD000, 0xC000):   # BL / BLX
        return None
    S = (hw1 >> 10) & 1
    imm10 = hw1 & 0x3FF
    J1 = (hw2 >> 13) & 1
    J2 = (hw2 >> 11) & 1
    I1 = 1 - (J1 ^ S)
    I2 = 1 - (J2 ^ S)
    imm = (S << 24) | (I1 << 23) | (I2 << 22) | (imm10 << 12) | ((hw2 & 0x7FF) << 1)
    if S:
        imm -= 1 << 25
    return (ca + 4 + imm) & ~1

def bl_callers(img, target):
    out = []
    for ca in range(BASE, BASE + len(img.d) - 4, 2):
        if bl_target(img, ca) == target:
            out.append(ca)
    return out

def word_refs(img, value):
    return [ca for ca in range(BASE, BASE + len(img.d) - 4 + 1, 4)
            if img.u32(ca) == value]

def kchk(ok, fail, name, cond, detail=""):
    if cond: ok[0] += 1
    else:    fail[0] += 1
    print(("PASS " if cond else "FAIL ") + name + (("  " + detail) if detail else ""))

# ---------------------------------------------------------------- the checks
def selftest(img):
    ok, fail = [0], [0]
    def chk(name, cond, detail=""):
        kchk(ok, fail, name, cond, detail)
    # -- A. the boot entry table (image word 0)
    chk("A1 image word0 == 0x46971 (boot entry, Thumb fn 0x46970)",
        img.u32(0x40000) == 0x46971, hex(img.u32(0x40000)))
    chk("A2 image word4 == 0xC742D (second entry pointer)",
        img.u32(0x40004) == 0xC742D, hex(img.u32(0x40004)))
    # -- B. the boot entry registers the device-init task
    chk("B1 lit CA 0x4751C == 0x47625 (task entry, Thumb bit set)",
        img.u32(0x4751C) == 0x47625, hex(img.u32(0x4751C)))
    ins = img.u16(0x47416); imm = ins & 0xFF
    la = ((0x47416 + 4) & ~3) + imm * 4
    chk("B2 0x47416 is ldr r1,[pc,#imm] resolving to CA 0x4751C",
        (ins & 0xF800) == 0x4800 and la == 0x4751C, hex(la))
    chk("B3 entry fn 0x46970 has no BL caller (reached from image word0)",
        bl_callers(img, 0x46970) == [], str([hex(x) for x in bl_callers(img, 0x46970)]))
    chk("B4 fn 0x47624 has no BL caller (installed as a task entry pointer)",
        bl_callers(img, 0x47624) == [], str([hex(x) for x in bl_callers(img, 0x47624)]))
    chk("B5 task entry bytes: 2 literal loads, then push.w (0x4FF0E92D at 0x47628)",
        (img.u16(0x47624) & 0xF800) == 0x4800 and img.u32(0x47628) == 0x4FF0E92D,
        img.d[img.off(0x47624):img.off(0x47624)+8].hex())
    chk("B6 registration helper CA 0xC745C takes the descriptor in r0 (0x47412 add r0,sp)",
        img.u16(0x47412) == 0xA806, img.d[img.off(0x47412):img.off(0x47412)+2].hex())
    # -- C. the task body calls the device init exactly once
    callers = bl_callers(img, 0xD1B68)
    chk("C1 device init 0xD1B68 has exactly one BL caller",
        callers == [0x48314], str([hex(x) for x in callers]))
    chk("C2 that call site is inside the registered task body (0x47624..0x49300)",
        0x47624 <= 0x48314 < 0x49300)
    # -- D. device-init gate
    lb = ((0xD1B7C + 4) & ~3) + (img.u16(0xD1B7C) & 0xFF) * 4
    chk("D1 gate: 0xD1B7C loads CA 0x17FCA8 (module ctx) from pool 0xD1CE8",
        lb == 0xD1CE8 and img.u32(0xD1CE8) == 0x17FCA8, hex(img.u32(0xD1CE8)))
    chk("D2 gate reads u8[0x17FCA8+0xB68] == u8[0x180810] (ldrb.w r3,[r3,#0xb68])",
        img.u16(0xD1B7E) == 0xF893 and img.u16(0xD1B80) == 0x3B68,
        img.d[img.off(0xD1B7E):img.off(0xD1B7E)+4].hex())
    chk("D3 gate: cmp #1; bls 0xD3F9E (flag==0 -> skip, ==1 -> init, >1 -> error 0x65)",
        img.u16(0xD1B82) == 0x2B01 and img.u32(0xD1B86) == 0x820AF242,
        img.d[img.off(0xD1B82):img.off(0xD1B82)+6].hex())
    # -- E. per-device count written by the init itself
    le = ((0xD1BA6 + 4) & ~3) + (img.u16(0xD1BA6) & 0xFF) * 4
    chk("E1 0xD1BA6 loads CA 0x174BE0 (device array base)", img.u32(le) == 0x174BE0,
        hex(img.u32(le)))
    chk("E2 device count u8[0x174BEE] := 2 (0xD1BAC movs r2,#2; 0xD1BAE strb r2,[r3,#0xe])",
        img.u16(0xD1BAC) == 0x2202 and img.u16(0xD1BAE) == 0x739A,
        img.d[img.off(0xD1BAC):img.off(0xD1BAC)+4].hex())
    l3 = ((0xD3F40 + 4) & ~3) + (img.u16(0xD3F40) & 0xFF) * 4
    chk("E3 loop head 0xD3F40 loads CA 0x174BE0; 0xD3F44 is ldrb r1,[r2,#0xe]",
        img.u32(l3) == 0x174BE0 and img.u16(0xD3F44) == 0x7B91,
        hex(img.u32(l3)) + " " + img.d[img.off(0xD3F44):img.off(0xD3F44)+2].hex())
    # -- F. the object passed to the list init
    chk("F1 r4 base literal CA 0x174C30 (device array + 0x40) at pool 0xD1D18",
        img.u32(0xD1D18) == 0x174C30, hex(img.u32(0xD1D18)))
    chk("F2 r5 = r4 - 0x40 (0xD1D92 sub.w r5,r4,#0x40)",
        img.d[img.off(0xD1D92):img.off(0xD1D92)+4].hex() == "a4f14005",
        img.d[img.off(0xD1D92):img.off(0xD1D92)+4].hex())
    chk("F3 band-0 init object = 0x174C30-0x40 = 0x174BF0 (= get_device(0))",
        0x174C30 - 0x40 == 0x174BF0)
    chk("F4 call site: 0xD3BAA mov r0,r5 ; 0xD3BAC bl 0xC016C",
        img.u16(0xD3BAA) == 0x4628 and bl_target(img, 0xD3BAC) == 0xC016C,
        hex(bl_target(img, 0xD3BAC)))
    # -- G. the get_device getter (the only place 0x174BF0 is materialised)
    chk("G1 getter CA 0xF4F48: movw #0xE28 / mla / pool word 0xF4F60 == 0x174BF0",
        img.u32(0xF4F4E) == 0x6028F640 and img.u32(0xF4F60) == 0x174BF0,
        hex(img.u32(0xF4F60)))
    chk("G2 getter bound: cmp r1,#1; bhi (bands 0..1 only)",
        img.u16(0xF4F4A) == 0x2901 and img.u16(0xF4F4C) == 0xD806,
        img.d[img.off(0xF4F4A):img.off(0xF4F4A)+4].hex())
    # -- H. the list init itself
    chk("H1 name literal at CA 0x1060F7 is &pst_device->st_device_alloced_ppdu_dscr_list...",
        img.strz(0x1060F7).startswith("&pst_device->st_device_alloced_ppdu_dscr_list"))
    chk("H2 pool word CA 0xC0484 == 0x1060F7 (the name ptr)",
        img.u32(0xC0484) == 0x1060F7, hex(img.u32(0xC0484)))
    chk("H3 list init CA 0xC016C has the single BL caller 0xD3BAC",
        bl_callers(img, 0xC016C) == [0xD3BAC], str([hex(x) for x in bl_callers(img, 0xC016C)]))
    chk("H4 head/prev self stores at 0xC02B2/0xC02B6 (str.w sb,[r4,#0x66c]/[r4,#0x670])",
        img.d[img.off(0xC02B2):img.off(0xC02B2)+8].hex() == "c4f86c96c4f87096",
        img.d[img.off(0xC02B2):img.off(0xC02B2)+8].hex())
    chk("H5 name store 0xC02C2 is str.w r3,[r4,#0x67c]",
        img.d[img.off(0xC02C2):img.off(0xC02C2)+4].hex() == "c4f87c36",
        img.d[img.off(0xC02C2):img.off(0xC02C2)+4].hex())
    chk("H6 count store 0xC02D0 is str.w r6,[r4,#0x674]",
        img.d[img.off(0xC02D0):img.off(0xC02D0)+4].hex() == "c4f87466",
        img.d[img.off(0xC02D0):img.off(0xC02D0)+4].hex())
    chk("H7 descriptor allocator CA 0xBF128 at 0xC0402 with size 0x1A0 (0xC03F4)",
        img.u32(0xC03F4) == 0x73D0F44F and bl_target(img, 0xC0402) == 0xBF128,
        hex(bl_target(img, 0xC0402)))
    chk("H8 append: 0xC043A ldr [r4,#0x670]; 0xC0450 count++ str [r4,#0x674]",
        img.d[img.off(0xC043A):img.off(0xC043A)+4].hex() == "d4f87036" and
        img.d[img.off(0xC0450):img.off(0xC0450)+4].hex() == "c4f87436",
        img.d[img.off(0xC043A):img.off(0xC043A)+4].hex())
    # -- I. the resolver and the 19 module contexts (why the old probe is not the list)
    chk("I1 manager literal pool CA 0x943C8 == 0x170E28", img.u32(0x943C8) == 0x170E28,
        hex(img.u32(0x943C8)))
    chk("I2 resolver 0xFE578: add.w r4,r4,#0x18c; ldrh.w r3,[r0,#0x6dc]; ldr.w r0,[r0,r4,lsl#2]",
        img.u32(0xFE5A8) == 0x74C6F504 and img.u32(0xFE5AC) == 0x36DCF8B0,
        img.d[img.off(0xFE5A8):img.off(0xFE5A8)+8].hex())
    chk("I3 module-context array base literal CA 0x49330 == 0x119758 (ctx id 0)",
        img.u32(0x49330) == 0x119758, hex(img.u32(0x49330)))
    l4923e = ((0x4923E + 4) & ~3)
    chk("I4 19 contexts, stride 0xB40: manager+0x630 table, loop bound 0xD5C0/0xB40 == 19",
        (0xD5C0 // 0xB40) == 19 and img.u32(0x4924C) == 0x0F04F848
        and img.u32(0x49256) == 0x53C0F24D,
        str(0xD5C0 // 0xB40) + " " + img.d[img.off(0x4923E):img.off(0x4923E)+4].hex())
    chk("I5 probe formula object (ctx id 0) 0x119758 != init object 0x174BF0",
        0x119758 != 0x174BF0)
    chk("I6 the probed +0x67C (0x119DD4) is not the PPDU-list field (0x17526C)",
        0x119758 + 0x67C != 0x174BF0 + 0x67C,
        hex(0x119758 + 0x67C) + " vs " + hex(0x174BF0 + 0x67C))
    # -- J. the per-band device-base chain (independently measured by task-215)
    chk("J1 band-select literals: 0xD20F4 = 0x40042000 (band0), 0xD20F8 = 0x40062000 (band1)",
        img.u32(0xD20F4) == 0x40042000 and img.u32(0xD20F8) == 0x40062000,
        hex(img.u32(0xD20F4)) + " / " + hex(img.u32(0xD20F8)))
    chk("J2 device-base chain: str.w r3,[r4,#0x260] then +0x2000 steps (0xD2046/0xD204A)",
        img.u32(0xD2046) == 0x3260F8C4 and img.u32(0xD204A) == 0x5300F503,
        img.d[img.off(0xD2046):img.off(0xD2046)+8].hex())
    chk("J3 instance select: ldrb [r4,#0xdd]; cmp 0 -> band0 path; cmp 1 -> band1 path",
        img.u16(0xD1D96) == 0x2B00 and img.u32(0xD1D98) == 0x8154F000 and
        img.u16(0xD1D9C) == 0x2B01 and img.u32(0xD1D9E) == 0x8179F000,
        img.d[img.off(0xD1D96):img.off(0xD1D96)+12].hex())
    print()
    print("selftest: %d/%d PASS" % (ok[0], ok[0] + fail[0]))
    return 0 if fail[0] == 0 else 1

def probes(img):
    def phys(ca, win=0x6B8000):
        return 0x40000000 + win + ca
    rows = [
        ("P1", "device-init gate byte (1 = run)", 0x180810, 1,
         "u8[0x17FCA8+0xB68]: 1=init, 0=skip, >1=error 0x65"),
        ("P2", "PPDU list name ptr (band 0)", 0x174BF0 + 0x67C, 4,
         "expect 0x001060F7 after the init ran"),
        ("P3", "PPDU list head next (band 0)", 0x174BF0 + 0x66C, 4,
         "expect 0x0017525C (=object+0x66C) after the init"),
        ("P4", "PPDU list head prev (band 0)", 0x174BF0 + 0x670, 4,
         "expect 0x0017525C too"),
        ("P5", "PPDU descriptor count (band 0)", 0x174BF0 + 0x674, 4,
         "preallocated 0x1a0-B descriptors count"),
        ("P6", "band-1 list name ptr", 0x175A18 + 0x67C, 4,
         "expect 0x001060F7 if band 1 inited"),
        ("P7", "device count in the init array", 0x174BEE, 1,
         "the init writes 2; 0 on a never-inited array"),
        ("P8", "the OLD probe object (resolver ctx id 0)", 0x119758 + 0x67C, 4,
         "NOT the list field; its zero is not a PPDU-list proof"),
    ]
    for pid, what, ca, w, expect in rows:
        print("%s CA %08X phys %08X w%d  %-42s %s" %
              (pid, ca, phys(ca), w, what, expect))

def chain(img):
    print("chain: image word0 CA 0x40000 = 0x%08X (fn 0x46970, boot entry)" % img.u32(0x40000))
    print("  fn 0x46970 @0x47416 loads 0x47625 and registers it via CA 0xC745C")
    print("  fn 0x47624 (entry 0x47625) @0x48314 bl 0xD1B68 (after a bounded wait loop)")
    print("  fn 0xD1B68: memset(0x174BE0,0,0x1C68); gate u8[0x180810]==1;")
    print("              device count u8[0x174BEE]=2; loop band=0..count-1")
    print("              r4 = 0x174C30 + band*0xE28 ; r5 = r4-0x40 = get_device(band)")
    print("  fn 0xD1B68 @0xD3BAC bl 0xC016C(r5) = PPDU descriptor-list create/init")
    print("  fn 0xC016C: head/prev self at obj+0x66C/+0x670; count 0 at +0x674;")
    print("              debug name 0x1060F7 at +0x67C; preallocates 0x1a0-B")
    print("              descriptors via CA 0xBF128 and appends them (0xC043A..0xC0450)")
    print("  getter CA 0xF4F48: 0x174BF0 + band*0xE28 for band 0..1 (else NULL)")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fw", default=DEFAULT_FW)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--probes", action="store_true")
    ap.add_argument("--chain", action="store_true")
    a = ap.parse_args()
    if not (a.selftest or a.probes or a.chain):
        ap.print_help(); return 2
    data = open(a.fw, "rb").read()
    img = Img(data)
    h = hashlib.sha256(data).hexdigest()
    print("fw: %s  %d B  sha256 %s..." % (a.fw, len(data), h[:16]))
    if not h.startswith(SHA16):
        print("WARNING: sha256 does not match the pinned 7fc87e20... image;"
              " every offset is only valid for that image.")
    rc = 0
    if a.chain: chain(img)
    if a.probes: probes(img)
    if a.selftest: rc = selftest(img)
    return rc

if __name__ == "__main__":
    sys.exit(main())
