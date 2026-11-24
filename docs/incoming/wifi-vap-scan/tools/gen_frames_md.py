#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Generate recipes/frames.md from vapscan_selftest --emit-raw output.

Host-only generator for the wifi-vap-scan lane; no device, no vendor artifact.
Usage:
    ./tools/build.sh
    /tmp/vapscan_selftest --emit-raw | ./tools/gen_frames_md.py > recipes/frames.md
"""
import sys

rows = []
for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    name, length, hexbytes, length2, node, valid = line.split("|")
    if length != length2:
        raise SystemExit("generator: length mismatch for %r" % name)
    rows.append((name, int(length), hexbytes, node, valid))
if not rows:
    raise SystemExit("generator: no frames on stdin (run vapscan_selftest --emit-raw)")

out = []
out.append("# Bench frame recipes (generated)")
out.append("")
out.append("Generated from the wifi-vap-scan builders by tools/gen_frames_md.py.")
out.append("Do not edit by hand; regenerate with:")
out.append("")
out.append("    ./tools/build.sh")
out.append("    /tmp/vapscan_selftest --emit-raw | ./tools/gen_frames_md.py > recipes/frames.md")
out.append("")
out.append("Every frame starts with the same 12-byte HCC header: group 0, alloc=1,")
out.append("dst=0, total length, message id, tag 0x5a5a. The SR node the transport")
out.append("must commit is word0 = frame devVA, word1 = (total<<16)|0x6d2b")
out.append("(the word1 value is listed per frame); announce with mailbox id 3.")
out.append("")
out.append("| frame | total B | SR node word1 | wire validate |")
out.append("| --- | ---: | --- | --- |")
for name, length, hexbytes, node, valid in rows:
    out.append("| %s | %d | %s | %s |" % (name, length, node, valid))
out.append("")
for name, length, hexbytes, node, valid in rows:
    out.append("## %s" % name)
    out.append("")
    out.append(chr(96) * 3)
    for i in range(0, len(hexbytes), 64):
        out.append(hexbytes[i:i + 64])
    out.append(chr(96) * 3)
    out.append("")
sys.stdout.write("\n".join(out) + "\n")