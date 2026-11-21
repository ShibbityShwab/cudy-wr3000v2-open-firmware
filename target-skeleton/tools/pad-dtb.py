#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
#
# Pad a compiled FDT blob to an 8-byte boundary and fold the padding into its
# header totalsize.
#
# Why this exists (measured 2026-11-21): the stock U-Boot bootfip reads the
# overlay-container gate word as a 32-bit BE load at base_dtb + FDT-totalsize
# (TRAILER-FORMAT.md).  dtc does not pad the blob when the strings block ends
# unaligned; a DTB with an unaligned totalsize (the gpio-LEDs/keys DTS gave
# 13041 bytes) made that load unaligned, U-Boot data-aborted, reset the CPU and
# fell back to the stock pair.  Padding the base DTB to 8 bytes (a superset of
# the 4-byte FDT requirement) keeps the gate word aligned for both the in-uImage
# appended DTB and the FIP trailer.
import struct
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: pad-dtb.py <dtb>", file=sys.stderr)
        return 2
    path = sys.argv[1]
    with open(path, "rb") as f:
        data = bytearray(f.read())
    if len(data) < 8 or data[0:4] != b"\xd0\x0d\xfe\xed":
        print("%s: not an FDT blob" % path, file=sys.stderr)
        return 1
    totalsize = struct.unpack(">I", data[4:8])[0]
    if totalsize != len(data):
        print("%s: file is %d bytes but totalsize is %d"
              % (path, len(data), totalsize), file=sys.stderr)
        return 1
    pad = (-len(data)) % 8
    if pad:
        data += bytes(pad)
        data[4:8] = struct.pack(">I", len(data))
        with open(path, "wb") as f:
            f.write(data)
    totalsize = struct.unpack(">I", data[4:8])[0]
    if len(data) != totalsize or totalsize % 8 != 0:
        print("%s: padding failed (size %d, totalsize %d)"
              % (path, len(data), totalsize), file=sys.stderr)
        return 1
    print("pad-dtb: %s -> %d bytes (8-byte aligned totalsize)"
          % (path, totalsize))
    return 0


if __name__ == "__main__":
    sys.exit(main())
