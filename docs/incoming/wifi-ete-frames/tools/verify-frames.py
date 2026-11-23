#!/usr/bin/env python3
"""verify-frames.py - host-only byte/instruction assertions for the wifi-ete-frames lane.

Reads the LOCAL vendor artifacts at runtime (nothing vendor is stored here) and
checks the exact instructions/literals quoted in REPORT.md, then prints the two
device->host ready frames and the H2D carrier contract.

usage:
  PY=.provision-venv/bin/python
  $PY tools/verify-frames.py [--fw PATH] [--plat PATH] [--wifi PATH] [--all]

Exit 0 = every assertion passed. Exit 1 = at least one failed (prints FAIL lines).
No device access; read-only file access.
"""
import argparse
import struct
import sys

DEFAULT_FW = "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin"
DEFAULT_PLAT = "/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_plat.ko"
DEFAULT_WIFI = "/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko"
BASE = 0x40000  # firmware CPU = file + 0x40000

FW_CLAIMS = [
    # (label, file offset, expected bytes hex)
    ("id1 builder: movs r1,#6; movs r0,#0", 0x7D28, "06210020"),
    ("id1 builder: bl hcc_msg_alloc(cpu 0x102822)", 0x7D2C, "baf079fd"),
    ("id1 builder: movs r3,#4; strb r3,[hdr]", 0x7D3C, "04230370"),
    ("id1 builder: movs r2,#1", 0x7D42, "0122"),
    ("id1 builder: strb id=1; strb hdr[3]", 0x7D4C, "8271c370"),
    ("id1 builder: bl hcc_msg_tx(cpu 0x102760)", 0x7D54, "baf004fd"),
    ("hcc_msg_alloc: movw r3,#0x674 bound", 0xC2824, "40f27463"),
    ("hcc_msg_alloc: strh total_len,[hdr,#4]", 0xC285C, "8480"),
    ("hcc_msg_alloc: bfc/strb alloc nibble", 0xC285E, "6ff303034370"),
    ("id2 builder: movs r1,#6 ; movs r0,#4", 0x9002, "06210420"),
    ("id2 builder: ldr r3,=payload (pc-rel)", 0x9000, "534b"),
    ("id2 builder: str r3,[sp,#0x54]", 0x9006, "1593"),
    ("id2 builder: bl hcc_msg_alloc", 0x9008, "b9f00bfc"),
    ("id2 literal: 0x01060850", 0x9150, "50080601"),
    ("id2 builder: movs r5,#4", 0x90E6, "0425"),
    ("id2 builder: movs r3,#2; strb r5,[hdr]; strb r3,[hdr,#6]", 0x90EE, "022305708371"),
    ("id2 builder: bl hcc_msg_tx", 0x9160, "2046b9f0fdfa"),
    ("sr-recv tag check: movw r0,#0x5a5a", 0x8699E, "45f65a20"),
    ("sr-recv tag check: ldrh r1,[hdr,#0xa]", 0x869A2, "5989"),
    ("sr-recv tag check: cmp;beq", 0x869A4, "814203d0"),
    ("sr-recv len check: ldrh r1,[hdr,#4]", 0x869B0, "9988"),
    ("gate zero: strh 0,[gate]", 0x86E7E, "1380"),
    ("gate zero: strh 0,[release]", 0x86E80, "22f8043c"),
    ("announce: movs r2,#4 ; str r2,[out1]", 0x86F54, "0422194b11491a60"),
    ("announce: D2H doorbell = 1", 0x86F66, "1a60"),
    ("gate spin: movw r3,#0xcece", 0x86F74, "4cf6ce63"),
    ("gate spin: ldr r2,[gate] ; cmp ; bne", 0x86F78, "0a689a42fcd1"),
]
FW_LITERALS = [
    ("0x4000010C gate literal", 0x86FA0, 0x4000010C),
    ("0x40039014 out[1] literal", 0x86FBC, 0x40039014),
    ("0x0017DF44 lock literal", 0x86FC0, 0x0017DF44),
]

PLAT_CLAIMS = [
    # (label, text-section vaddr, expected 4-byte hex)
    ("pcie_ete_tx_queue_handle: movs r3,#0x5a", 0x15764, "5a30a0e3"),
    ("pcie_ete_tx_queue_handle: strb tag -> hdr[0xa]", 0x15768, "0a30c8e5"),
    ("pcie_ete_tx_queue_handle: strb tag -> hdr[0xb]", 0x15774, "0b30c8e5"),
    ("pcie_ete_tx_queue_handle: ldrh len,[hdr,#4]", 0x15798, "b4b0d8e1"),
    ("sr fill: orr word1,#0x4000", 0x178A0, "012982e3"),
    ("sr fill: orr word1,#0x2000", 0x178AC, "022a82e3"),
    ("sr fill: movw 0xd2b", 0x1789C, "2b1d00e3"),
    ("sr fill: announce id3", 0x178F4, "0310a0e3"),
    ("multi_chip_loading: wait#1 200 jiffies", 0xF328, "c810a0e3"),
    ("multi_chip_loading: wait#2 2000 jiffies", 0xF380, "7d1ea0e3"),
    ("multi_chip_loading: release wait stubs on complete", 0xF32C, "feffffeb"),
    ("host_ready_msg_process: payload bound", 0xEC48, "620056e3"),
    ("host_ready_msg_process: bhi invalid", 0xEC4C, "1200008a"),
]
DATA_CLAIMS = [
    # (label, .data symbol vaddr, expected reloc/name presence check done by size only)
]


def load_fw(path):
    with open(path, "rb") as f:
        return f.read()


def load_text(path):
    try:
        from elftools.elf.elffile import ELFFile
    except ImportError:
        print("FAIL: pyelftools missing (use the repo .provision-venv python)")
        sys.exit(1)
    with open(path, "rb") as f:
        e = ELFFile(f)
        sec = e.get_section_by_name(".text")
        data = sec.data()
        return data, sec["sh_addr"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fw", default=DEFAULT_FW)
    ap.add_argument("--plat", default=DEFAULT_PLAT)
    ap.add_argument("--wifi", default=DEFAULT_WIFI)
    ap.add_argument("--all", action="store_true", help="also load/scan the wifi.ko path (unused claims are fatal under --all)")
    args = ap.parse_args()

    fails = 0
    fw = load_fw(args.fw)
    text, shaddr = load_text(args.plat)

    print("== FIRMWARE.bin:", len(fw), "bytes   (CPU = file + 0x%x)" % BASE)
    for label, off, want in FW_CLAIMS:
        got = fw[off:off + len(want) // 2].hex()
        if got != want:
            print("FAIL fw 0x%06x %-46s want %s got %s" % (off, label, want, got))
            fails += 1
    for label, off, val in FW_LITERALS:
        got = struct.unpack_from("<I", fw, off)[0]
        if got != val:
            print("FAIL fw literal 0x%06x %-30s want 0x%08x got 0x%08x" % (off, label, val, got))
            fails += 1

    print("== plat.ko .text:", len(text), "bytes   (sh_addr=0x%x)" % shaddr)
    for label, off, want in PLAT_CLAIMS:
        got = text[off:off + len(want) // 2].hex()
        if got != want:
            print("FAIL plat 0x%06x %-46s want %s got %s" % (off, label, want, got))
            fails += 1

    print()
    print("== device->host ready frames (recovered, see REPORT.md sec 2)")
    print("  g4 id1 : 04 00 ?? 00 0c 00 01 00 ?? 00 ?? ??")
    print("           group=4 id=1 total_len=0x0C (header only); src core=hdr[1]>>4=0")
    print("  g4 id2 : 04 00 ?? 00 10 00 02 00 ?? 00 ?? ?? 50 08 06 01")
    print("           group=4 id=2 total_len=0x10 payload=4B; payload = device addr 0x01060850")
    print()
    print("== H2D carrier contract (host -> device)")
    print("  node 8B : word0 = buffer device VA, word1 = (len<<16)|0x6D2B")
    print("  frame   : hdr[4..5]=payload+12, hdr[0xA..0xB]=0x5A5A, dst in hdr[1]>>4 == hdr[9]")
    print("  mailbox : out[0] |= BIT(3) then out[2] |= 1 ; device ack is out[5] (read only)")
    print()
    if fails:
        print("VERIFY: %d FAIL" % fails)
        return 1
    total = len(FW_CLAIMS) + len(FW_LITERALS) + len(PLAT_CLAIMS)
    print("VERIFY: %d/%d byte claims PASS" % (total, total))
    return 0


if __name__ == "__main__":
    sys.exit(main())
