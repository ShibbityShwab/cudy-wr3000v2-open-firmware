#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Minimal read-only UBI image -> volume extractor (bench image verification).

Reassembles one volume of a UBI image (as produced by ubinize and embedded in
the sysupgrade container) into a plain byte stream, so the rootfs squashfs can
be inspected with the host's unsquashfs. Read-only: the input file is never
modified, no device, no network.

Layout used (programming manual / drivers/mtd/ubi/ubi-media.h):
  per PEB: [EC header @0, VID header @ec.vid_hdr_offset, data @ec.data_offset]
  EC  header: magic 0x55424923 "UBI#", +16 vid_hdr_offset, +20 data_offset
  VID header: magic 0x55424921 "UBI!", +8 vol_id, +12 lnum, +20 data_size

The PEB size is derived from the image size and validated against every EC
magic, so this works for any geometry without hardcoding 128 KiB.
"""
import argparse
import struct
import sys

EC_MAGIC = 0x55424923
VID_MAGIC = 0x55424921


def be32(buf, off):
    return struct.unpack_from(">I", buf, off)[0]


def main():
    ap = argparse.ArgumentParser(description="extract one volume from a UBI image")
    ap.add_argument("ubi", help="input UBI image file")
    ap.add_argument("-o", "--output", required=True, help="output raw volume file")
    ap.add_argument("--vol-id", type=int, default=0, help="volume id to extract (default 0)")
    ap.add_argument("--vol-name", default=None,
                    help="volume name to extract (overrides --vol-id; names are in the rootfs ubinize cfg only)")
    args = ap.parse_args()

    with open(args.ubi, "rb") as f:
        data = f.read()
    size = len(data)
    if size < 131072 or size % 4096:
        print("ERROR: not a plausible UBI image (%d B)" % size, file=sys.stderr)
        return 2

    # Derive the PEB size: a divisor of the image size where every PEB starts
    # with a valid EC magic. Prefer the classic 128 KiB geometry first.
    candidates = [131072, 262144, 65536, 126976, 256 * 1024]
    peb = None
    for cand in candidates:
        if size % cand:
            continue
        if all(be32(data, off) == EC_MAGIC for off in range(0, size, cand)):
            peb = cand
            break
    if peb is None:
        print("ERROR: cannot derive PEB size (no candidate aligns with EC magic)", file=sys.stderr)
        return 2
    preads = size // peb
    print("UBI image: %d B, PEB %d B, %d PEBs" % (size, peb, preads))

    chunks = {}
    vol_ids = {}
    skipped = 0
    for idx in range(preads):
        base = idx * peb
        ec = base
        if be32(data, ec) != EC_MAGIC:
            print("ERROR: bad EC magic in PEB %d" % idx, file=sys.stderr)
            return 2
        vid_off = be32(data, ec + 16)
        data_off = be32(data, ec + 20)
        if vid_off + 64 > peb or data_off > peb:
            print("ERROR: header offsets out of PEB in PEB %d" % idx, file=sys.stderr)
            return 2
        vid = base + vid_off
        if be32(data, vid) != VID_MAGIC:
            # UBI images can contain empty (erased) PEBs; they are all-0xff.
            if data[base:base + 4] == b"\xff\xff\xff\xff":
                skipped += 1
                continue
            print("ERROR: bad VID magic in PEB %d" % idx, file=sys.stderr)
            return 2
        vol_id = be32(data, vid + 8)
        lnum = be32(data, vid + 12)
        data_size = be32(data, vid + 20)
        vol_ids[vol_id] = vol_ids.get(vol_id, 0) + 1
        if args.vol_name is not None:
            # volume names are not stored in the VID header; unsupported here
            pass
        if vol_id != args.vol_id:
            continue
        if data_off + data_size > peb:
            print("ERROR: data_size overruns PEB %d" % idx, file=sys.stderr)
            return 2
        if lnum in chunks:
            print("WARNING: duplicate lnum %d (copy_flag PEB), keeping first" % lnum, file=sys.stderr)
            continue
        chunks[lnum] = data[base + data_off:base + data_off + data_size]

    if not chunks:
        print("ERROR: no PEB found for volume %d (volumes seen: %s)"
              % (args.vol_id, sorted(vol_ids)), file=sys.stderr)
        return 2
    order = sorted(chunks)
    if order != list(range(len(order))):
        print("ERROR: non-contiguous logical erase blocks: %s" % order[:10], file=sys.stderr)
        return 2
    volume = b"".join(chunks[i] for i in order)
    with open(args.output, "wb") as f:
        f.write(volume)
    print("volume %d: %d LEBs, %d B reassembled -> %s"
          % (args.vol_id, len(order), len(volume), args.output))
    print("header: %s" % volume[:8].hex())
    if volume[:4] == b"hsqs":
        print("format: squashfs (magic hsqs) OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
