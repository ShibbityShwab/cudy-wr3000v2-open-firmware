#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# mkblocks.py - stage the two pre-release custom blocks for the clean-room
# Hi5622V100 driver from a LOCAL stock-boot capture.
#
# The platform descriptor (24 words / 0x60 B at CPU 0x1b2800) is built into
# the driver from the device-INI contract.  The Wi-Fi PHY/radio custom block
# (618 words / 0x9a8 B at CPU 0x1b2860) is vendor configuration data: it is
# captured from a working stock boot at bench time, staged into the kernel
# firmware directory and NEVER committed (project publication rule).
#
# Input formats (local capture file, any mix):
#      0x86A800: 00000000 00117BC4 ...
#      00000000 00117BC4 ...
#      0x86A860
#      00000000
# Only the word sequence is used; addresses are informational and force the
# expected block size.
#
# Usage:
#   mkblocks.py --wifi-capture /path/to/local_capture.txt --outdir /tmp/blocks
#   mkblocks.py --selftest
#
# Then stage the files (bench host, attended):
#   install -D -m 0644 /tmp/blocks/wifi_block.bin /lib/firmware/hi_wifi/wifi_block.bin
# (The platform block is built in; --plat-capture / --outdir can produce a
#  platform_block.bin for the optional plat_block_path override.)
import argparse
import hashlib
import os
import re
import sys

PLAT_WORDS = 24
WIFI_WORDS = 618

# The built-in platform descriptor (hi5622_transport.c); --check-plat compares
# a local platform capture against it.
BUILTIN_PLATFORM = [
    0x00000000, 0x00117BC4, 0x0000A968, 0x0012252C, 0x0000076C, 0x001B2800,
    0x00000060, 0x001B2860, 0x000009A8, 0x00000002, 0x00000001, 0x00000028,
    0x00000064, 0x000001CC, 0xFFFFC92D, 0xFFFFFFC0, 0x000001DA, 0xFFFFC97D,
    0xFFFFFFA0, 0x000001ED, 0xFFFFC92D, 0xFFFFFFC0, 0x80000000, 0x00000000,
]


def parse_words(text):
    words = []
    for line in text.splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        # strip an "ADDRESS:" prefix if present (informational only)
        line = re.sub(r"^(0x[0-9a-fA-F]+|[0-9a-fA-F]{8}):", "", line).strip()
        for tok in re.split(r"[\s,]+", line):
            if not tok:
                continue
            tok = tok[2:] if tok.lower().startswith("0x") else tok
            if not re.fullmatch(r"[0-9a-fA-F]{1,8}", tok):
                raise SystemExit("mkblocks: bad word token %r in line %r" %
                                 (tok, line))
            words.append(int(tok, 16))
    return words


def words_to_bin(words):
    return b"".join(w.to_bytes(4, "little") for w in words)


def sha256(path):
    with open(path, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


def emit(words, name, want, outdir):
    if len(words) != want:
        raise SystemExit("mkblocks: %s: got %d words, expected %d (0x%x B)" %
                         (name, len(words), want, want * 4))
    path = os.path.join(outdir, "wifi_block.bin" if name == "wifi" else
                        "platform_block.bin")
    with open(path, "wb") as fh:
        fh.write(words_to_bin(words))
    print("%s: %d words / 0x%x B -> %s sha256=%s" %
          (name, len(words), len(words) * 4, path, sha256(path)))
    return path


def selftest():
    checks = []

    def check(name, cond):
        checks.append((bool(cond), name))

    sample = """
# local capture (text form)
0x86A800: 00000000 00117BC4 0000A968 0012252C
0000076C 001B2800 00000060 001B2860
# comment line
0x000009A8 00000002 00000001 00000028
00000064 000001CC FFFFC92D FFFFFFC0
000001DA FFFFC97D FFFFFFA0 000001ED
FFFFC92D FFFFFFC0 80000000 00000000
"""
    words = parse_words(sample)
    check("parse mixed address/word lines", len(words) == PLAT_WORDS)
    check("parse values", words == BUILTIN_PLATFORM)
    blob = words_to_bin(words)
    check("little-endian encoding", len(blob) == 0x60 and
          blob[4:8] == bytes.fromhex("c47b1100"))
    check("platform descriptor round-trips", len(blob) == PLAT_WORDS * 4)
    check("wifi block size constant", WIFI_WORDS * 4 == 0x9A8)
    bad = 0
    try:
        parse_words("zzzz")
    except SystemExit:
        bad = 1
    check("invalid tokens are refused", bad == 1)
    ok = sum(1 for c, _ in checks if c)
    for c, name in checks:
        if not c:
            print("FAIL:", name)
    print("mkblocks selftest: %d/%d checks PASS" % (ok, len(checks)))
    return 0 if ok == len(checks) else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--wifi-capture", help="local text capture of the 618-word wifi block")
    ap.add_argument("--plat-capture", help="local text capture of the 24-word platform block")
    ap.add_argument("--outdir", default=".", help="output directory (default: .)")
    ap.add_argument("--check-plat", action="store_true",
                    help="compare a --plat-capture against the built-in descriptor")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if not args.wifi_capture and not args.plat_capture:
        ap.error("give --wifi-capture and/or --plat-capture (or --selftest)")
    os.makedirs(args.outdir, exist_ok=True)
    if args.plat_capture:
        with open(args.plat_capture, "r", encoding="utf-8") as fh:
            words = parse_words(fh.read())
        if args.check_plat and words != BUILTIN_PLATFORM:
            print("WARNING: captured platform block differs from the built-in "
                  "descriptor; the driver's built-in values are the INI/ledger "
                  "contract - review before overriding with plat_block_path")
        emit(words, "platform", PLAT_WORDS, args.outdir)
    if args.wifi_capture:
        with open(args.wifi_capture, "r", encoding="utf-8") as fh:
            words = parse_words(fh.read())
        emit(words, "wifi", WIFI_WORDS, args.outdir)
        print("stage locally: install -D -m 0644 %s /lib/firmware/hi_wifi/wifi_block.bin" %
              os.path.join(args.outdir, "wifi_block.bin"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
