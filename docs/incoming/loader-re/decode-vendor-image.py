
#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""decode-vendor-image.py - host-side parser for the Cudy WR3000 v2.0 (luofu R116)
vendor recovery/sysupgrade container.

READ-ONLY. Takes a vendor image path (e.g. tftp/recovery.bin) and prints its
structure: hboot1tag header, signature block, FIT, the /images/ubi data property,
the HiSilicon hi_upgrade header/layers, the uImage + base DTB + FIP gate +
overlay descriptor, and the two fwtool metadata + FWx0 trailers. With
--uboot <a dumped uboota.bin> it extracts the vendor RSA public key from the
U-Boot image and verifies the package signature (RSA-2048 PKCS#1 v1.5 SHA-1
over image[0,0x80000) || image[0x80800,EOF)).

No vendor bytes are embedded in this script. Everything is derived from the
files the operator passes in. See REPORT.md in this directory for the decoded
format and the evidence behind every field.

Exit codes: 0 pass / 1 parse or verify failure / 2 usage.
"""
import argparse
import hashlib
import struct
import sys
import zlib

MAGIC_HBOOT = b"hboot1tag"
MAGIC_FDT = 0xD00DFEED
MAGIC_UIMAGE = 0x27051956
GATE_MAGIC = 0xD7B7AB1E
HIUPG_MAGIC = 0x76543210
FWX0 = b"FWx0"
DIGEST_INFO_SHA1 = bytes.fromhex("3021300906052b0e03021a05000414")
FIT_SEARCH_OFFSETS = (0x80800, 0x100800, 0x20800)
KEY_STRUCT_OFFSETS = (0x55008, 0x54FD8)   # 2.5.24 build / older 2.4.15 build


class DecodeError(Exception):
    pass


def be16(buf, off):
    return struct.unpack_from(">H", buf, off)[0]


def be32(buf, off):
    return struct.unpack_from(">I", buf, off)[0]


def le32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def crc32_raw(data):
    """CRC-32 with init 0xFFFFFFFF and no final XOR (what the hi_upgrade header stores)."""
    return (~zlib.crc32(data)) & 0xFFFFFFFF


def check(cond, msg):
    if not cond:
        raise DecodeError(msg)


def parse_outer(d):
    print("== outer container: %d bytes" % len(d))
    check(len(d) >= 0x10000, "file too small for an hboot1tag container")
    check(d[:9] == MAGIC_HBOOT, "no hboot1tag magic at offset 0")
    version = le32(d, 0x10)
    code_off = le32(d, 0x58)
    code_len = le32(d, 0x5C)
    field60 = le32(d, 0x60)
    region = le32(d, 0x64)
    code = d[code_off:code_off + code_len]
    sha_ok = len(code) == code_len and hashlib.sha256(code).digest() == d[0x18:0x38]
    print("   hboot1tag header: version=%d code_off=0x%x code_len=0x%x (+0x60=0x%x, region=0x%x)"
          % (version, code_off, code_len, field60, region))
    print("   code SHA-256 matches header: %s" % sha_ok)
    check(code_len > 0 and code_off + code_len <= len(d), "hboot1tag code range out of file")
    for off in FIT_SEARCH_OFFSETS:
        if off + 8 <= len(d) and be32(d, off) == MAGIC_FDT:
            fit = off
            break
    else:
        raise DecodeError("no FIT image found at any known bootloader offset")
    print("   FIT image at file offset 0x%x" % fit)
    sig = fit - 0x800
    if sig >= 0 and sig + 0x100 <= len(d):
        print("   signature block candidate at 0x%x (256 B); gap [0x%x,0x%x) is not signed"
              % (sig, sig + 0x100, fit))
    return {"fit": fit, "sig": sig}


def parse_fdt(d, off):
    magic = be32(d, off)
    check(magic == MAGIC_FDT, "not an FDT at 0x%x" % off)
    total, off_struct, off_strings, off_rsv, ver, lastcp, cpuid, size_str, size_struct = \
        struct.unpack_from(">9I", d, off + 4)
    print("== FIT: totalsize=0x%x struct=0x%x strings=0x%x ver=%d" % (total, off_struct, off_strings, ver))
    strings = d[off + off_strings:off + off_strings + size_str]

    def name_of(o):
        end = strings.find(b"\0", o)
        return strings[o:end].decode("utf-8", "replace")

    props = {}
    path = []
    pos = off + off_struct
    guard = 0
    while True:
        guard += 1
        check(guard < 100000, "FDT structure loop")
        tag = be32(d, pos)
        if tag == 1:
            end = d.find(b"\0", pos + 4)
            path.append(d[pos + 4:end].decode("utf-8", "replace"))
            pos = (end + 4) & ~3
        elif tag == 2:
            if path:
                path.pop()
            pos += 4
        elif tag == 3:
            ln, nmoff = struct.unpack_from(">II", d, pos + 4)
            p = "/".join([x for x in path if x])
            props[(p, name_of(nmoff))] = (pos + 12, ln)
            pos = (pos + 12 + ln + 3) & ~3
        elif tag == 4:
            pos += 4
        elif tag == 9:
            break
        else:
            raise DecodeError("bad FDT token 0x%x at 0x%x" % (tag, pos))
    desc = None
    if ("", "description") in props:
        start, ln = props[("", "description")]
        desc = d[start:start + ln].split(b"\0")[0].decode("ascii", "replace")
    print("   root description: %r" % desc)
    key = ("images/ubi", "data")
    check(key in props, "FIT has no /images/ubi data property")
    data_off, data_len = props[key]
    print("   /images/ubi data: file 0x%x length 0x%x (end 0x%x)" % (data_off, data_len, data_off + data_len))
    return {"total": total, "description": desc, "data_off": data_off, "data_len": data_len}


def parse_hiupg(d, off):
    hdr = d[off:off + 0x5B8]
    check(le32(hdr, 0) == HIUPG_MAGIC, "no hi_upgrade magic at 0x%x" % off)
    product = hdr[4:24].split(b"\0")[0].decode("ascii", "replace")
    version = hdr[0x44:0x54].split(b"\0")[0].decode("ascii", "replace")
    pkgver = hdr[0x84:0x94].split(b"\0")[0].decode("ascii", "replace")
    chip = hdr[0x94:0xA4].split(b"\0")[0].decode("ascii", "replace")
    vendor = hdr[0xA4:0xB4].split(b"\0")[0].decode("ascii", "replace")
    subfiles = be32(hdr, 0xEC)
    pkg_total = be32(hdr, 0x5B0)
    hdr_crc = be32(hdr, 0x5B4)
    print("== hi_upgrade header at 0x%x" % off)
    print("   product=%r version=%r package=%r chip=%r vendor=%r subfiles=%d"
          % (product, version, pkgver, chip, vendor, subfiles))
    print("   package total=0x%x header crc stored=0x%08x computed=0x%08x crc_ok=%s"
          % (pkg_total, hdr_crc, crc32_raw(hdr[:0x5B4]), hdr_crc == crc32_raw(hdr[:0x5B4])))
    check(subfiles in (1, 2), "unexpected sub-file count %d" % subfiles)
    layers = {}
    for i in range(subfiles):
        base = 0xF0 + i * 0x4C
        ftype = be32(hdr, base)
        foff = be32(hdr, base + 4)
        flen = be32(hdr, base + 8)
        fver = hdr[base + 0xC:base + 0x1C].split(b"\0")[0].decode("ascii", "replace")
        layers[ftype] = (foff, flen)
        print("   layer%d type=0x%08x offset=0x%x length=0x%x version=%r" % (i, ftype, foff, flen, fver))
        check(off + foff + flen <= len(d), "layer %d runs past EOF" % i)
    kernel = layers.get(0x00020002)
    rootfs = layers.get(0x00020003)
    check(kernel and rootfs, "package has no kernel/rootfs layer")
    code_off, code_len = kernel
    check(off + code_off + 0x40 <= len(d), "kernel layer out of file")
    magic, hcrc, timestamp, ih_size, load, ep, dcrc, os_, arch, typ, comp = \
        struct.unpack_from(">IIIIIIIBBBB", d, off + code_off)
    image_name = d[off + code_off + 32:off + code_off + 64].split(b"\0")[0].decode("ascii", "replace")
    print("   uImage at 0x%x: magic=0x%08x size=0x%x load=0x%x ep=0x%x os=%d arch=%d type=%d comp=%d name=%r"
          % (off + code_off, magic, ih_size, load, ep, os_, arch, typ, comp, image_name))
    check(magic == MAGIC_UIMAGE, "kernel layer does not start with a legacy uImage")
    dtb = off + code_off + 0x40 + ih_size
    check(be32(d, dtb) == MAGIC_FDT, "no base DTB after the uImage payload")
    dtb_total = be32(d, dtb + 4)
    gate = dtb + dtb_total
    print("   base DTB at 0x%x totalsize=0x%x (4-byte %s / 8-byte %s aligned) gate at 0x%x"
          % (dtb, dtb_total,
             "ok" if dtb_total % 4 == 0 else "NOT",
             "ok" if dtb_total % 8 == 0 else "NOT", gate))
    check(be32(d, gate) == GATE_MAGIC, "gate magic missing (0x%08x)" % be32(d, gate))
    count = be32(d, gate + 0x10)
    container_total = be32(d, gate + 4)
    print("   descriptor: total=0x%x count=%d consts=%d/%d/%d"
          % (container_total, count, be32(d, gate + 8), be32(d, gate + 12), be32(d, gate + 20)))
    for i in range(count):
        entry = gate + 32 + i * 32
        size = be32(d, entry)
        oval = be32(d, entry + 4)
        check(be32(d, gate + oval) == MAGIC_FDT, "overlay %d has no FDT magic" % i)
        print("   overlay %d: size=%d offset=0x%x" % (i, size, oval))
    check(gate + container_total <= len(d), "FIP container runs past EOF")
    print("   kernel layer [0x%x,0x%x); rootfs layer [0x%x,0x%x)"
          % (off + code_off, off + code_off + code_len, off + rootfs[0], off + rootfs[0] + rootfs[1]))
    return {"kernel": (off + code_off, code_len), "rootfs": (off + rootfs[0], rootfs[1]),
            "gate": gate, "dtb": dtb}


def parse_fwtool(d, start, end, label):
    """Find a fwtool metadata JSON + 'FWx0' trailer in [start,end)."""
    marker = b'"metadata_version"'
    mpos = d.rfind(marker, start, end)
    if mpos < 0:
        print("   (no fwtool metadata JSON in %s)" % label)
        return
    j_start = d.rfind(b"{", start, mpos)
    j = d.find(FWX0, start, end) if mpos >= 0 else -1
    # locate the trailer after the JSON
    j = d.find(FWX0, mpos, end)
    if j < 0:
        print("   (metadata JSON in %s has no FWx0 trailer)" % label)
        return
    trailer = d[j:j + 16]
    fields = struct.unpack(">4I", trailer)
    print("== fwtool metadata (%s): JSON at 0x%x len=%d; trailer at 0x%x %s"
          % (label, j_start, j - j_start, j, trailer.hex(" ")))
    print("   fields: magic=%r word4=0x%08x (LE 0x%08x) version_byte=%d size_last_byte=%d "
          "(len+24=%d)" % (trailer[:4], fields[1], struct.unpack("<I", trailer[4:8])[0],
                           trailer[8], trailer[-1], (j - j_start) + 24))


def find_key(uboot, key_off=None):
    candidates = [key_off] if key_off is not None else KEY_STRUCT_OFFSETS
    for off in candidates:
        if off is None or off + 8 + 256 > len(uboot):
            continue
        len_words = le32(uboot, off)
        if len_words != 64:
            continue
        n0inv = le32(uboot, off + 4)
        mod0 = le32(uboot, off + 8)
        if mod0 % 2 == 0:
            continue
        if (-pow(mod0, -1, 1 << 32)) & 0xFFFFFFFF != n0inv:
            continue
        modulus = uboot[off + 8:off + 8 + 256]
        print("== RSA public key: struct at U-Boot file offset 0x%x, n0inv=0x%08x, "
              "modulus sha256=%s" % (off, n0inv, hashlib.sha256(modulus).hexdigest()))
        return int.from_bytes(modulus, "little")
    raise DecodeError("could not locate a consistent vendor RSA public key in the U-Boot dump")


def verify_rsa(d, key, sig, fit):
    sig_bytes = d[sig:sig + 0x100]
    digest = hashlib.sha1(d[0:0x80000] + d[0x80800:]).digest()
    m = pow(int.from_bytes(sig_bytes, "big"), 65537, key)
    block = m.to_bytes(256, "big")
    ff = 256 - 2 - 1 - len(DIGEST_INFO_SHA1) - len(digest)
    expected = b"\x00\x01" + b"\xff" * ff + b"\x00" + DIGEST_INFO_SHA1
    ok = block[:len(expected)] == expected and block[len(expected):] == digest
    print("== RSA verify (PKCS#1 v1.5 SHA-1 over [0,0x80000)||[0x80800,EOF)):")
    print("   sha1=%s" % digest.hex())
    print("   signature decrypts to: starts=0x%02x 0x%02x, digest tail=%s, result=%s"
          % (block[0], block[1], block[-len(digest):].hex(), "PASS" if ok else "FAIL"))
    return ok


def main(argv):
    ap = argparse.ArgumentParser(description="Decode a Cudy WR3000 v2.0 (luofu) vendor "
                                             "recovery/sysupgrade container. Read-only.")
    ap.add_argument("--image", required=True, help="vendor recovery.bin / sysupgrade .bin")
    ap.add_argument("--uboot", help="dumped uboota.bin to extract the RSA key and verify")
    ap.add_argument("--key-offset", type=lambda x: int(x, 0),
                    help="public-key struct file offset inside --uboot (default: known builds)")
    args = ap.parse_args(argv)
    try:
        d = open(args.image, "rb").read()
        outer = parse_outer(d)
        fit = parse_fdt(d, outer["fit"])
        hi = parse_hiupg(d, fit["data_off"])
        parse_fwtool(d, hi["rootfs"][0] + hi["rootfs"][1],
                     fit["data_off"] + fit["data_len"], "inner")
        parse_fwtool(d, outer["fit"] + fit["total"], len(d), "outer")
        ok = True
        if args.uboot:
            uboot = open(args.uboot, "rb").read()
            key = find_key(uboot, args.key_offset)
            ok = verify_rsa(d, key, outer["sig"], outer["fit"])
        else:
            print("== RSA verify skipped (pass --uboot <dumped uboota.bin> to verify)")
        print("== done (structure parsed%s)" % ("" if ok else "; RSA verification FAILED"))
        return 0 if ok else 1
    except (DecodeError, OSError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
