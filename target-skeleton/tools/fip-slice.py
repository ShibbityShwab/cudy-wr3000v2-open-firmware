#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# -*- coding: utf-8 -*-
"""fip-slice.py - FIP slice checker/assembler for the HiSilicon luofu loader.

STATUS: SCHEMATIC / UNTESTED TARGET TOOL. Read this docstring before using it
for anything that touches a router.

WHAT THIS IS
  The stock vendor U-Boot on the Cudy WR3000 v2.0 / luofu R116 boots with
      bootcmd=mtd read kernel${bootflag} ${loadaddr}; bootfip ${loadaddr}
  "bootfip" accepts the vendor's FIP package: a legacy uImage followed by a
  trailer that carries the base DTB, a 96-byte overlay-container descriptor and
  the board DTBO overlays. The trailer anatomy and the descriptor are decoded
  in opensource/target-skeleton/docs/TRAILER-FORMAT.md (local evidence:
  2026-10-11 bench ledger PHASE 91/95/99; 2026-11-18 U-Boot disassembly of the
  on-device build; local-only vendor containers). The values below are
  recomputed from the file the operator supplies; no vendor bytes are embedded.

WHAT "decoded" MEANS HERE (2026-11-18)
  The stock bootfip applier (U-Boot FUN_800743ec in the dumped 2022.07 build)
  performs exactly these container checks, in this order:
    1. be32 at base_dtb+dtb_totalsize (the gate word) == 0xd7b7ab1e;
    2. container total (gate+4) <= 8 MiB;
    3. entry count (gate+16) <= 64;
    4. gate+8 == 32, gate+12 == 32, gate+20 == 32;
    then for entry i in [0, count): entry = gate+32+i*32;
      5. overlay offset = be32(entry+4), relative to the gate word;
      6. the FDT at gate+offset is valid and its totalsize <= 128 KiB;
    then the first overlay matching vendor_id/board_id/compatible is applied
    to the base DTB with libfdt overlay apply; if none matches the applier
    returns -1 and the loader does not jump to the kernel.
  The entry "size" word (entry+0) and gate+24/gate+28 are NOT read by
  bootfip; the two recorded vendor containers nonetheless carry meaningful
  values there (overlay FDT totalsize, zero), and this tool keeps that shape.

WHAT THIS TOOL DOES NOT DO
  It does not copy or carry vendor bytes, and it does not prove that a
  generated trailer boots: that needs one attended boot on a recovered unit
  (TARGET-DESIGN.md section 5.2, 9). "make-trailer" is a fail-closed
  *structural* generator validated against every check the decoded loader
  performs; it is not hardware-proven for a novel trailer.

SAFE MODE
  "check", "check-trailer" and "make-trailer" are read-only until they are
  asked to write. "check" opens a slice, validates it, prints a report and
  exits. It never touches a device.

INPUTS FOR ASSEMBLY
  --uimage  a legacy uImage. The bench lane used:
              mkimage -A arm -O linux -T kernel -C none \
                -a 0x80608000 -e 0x80608000 -n Linux-5.10.201 -d body uImage
            but the load/entry address must be DERIVED, not copied: the stock
            loader stages the slot at 0x83200000 and the decompressor computes
            (pc & 0xf8000000) + TEXT_OFFSET (TARGET-DESIGN.md 3.2). Pass
            --loader-stage 0x83200000 --text-offset 0x00608000 and the tool
            computes/asserts the address for you.
  --trailer a structurally valid trailer. Either the fixed 40,732-byte
            bench-lane artifact (a local, vendor-derived, truncated copy of the
            40,828-byte vendor container) or one produced by "make-trailer"
            from our own base DTB + overlays.

INPUTS FOR GENERATION ("make-trailer")
  --base-dtb  an FDT blob (exactly its totalsize bytes; it becomes trailer[0])
  --overlay   one DTBO plugin per flag (exactly its totalsize bytes, <=128 KiB)
  The tool validates the decoded overlay match rule (compatible family token,
  vendor_id, board_id), emits the 32-byte descriptor header + 32-byte entries
  + the overlays contiguously, and re-parses its own output. It refuses rather
  than guessing. Optional --overlay-apply auto|require|skip additionally runs
  "fdtoverlay" (the same libfdt operation the loader performs) when available.

EXIT CODES
  0  pass / artifact written
  1  validation failure (bad slice, bad trailer, bad expectations)
  2  argument/usage error (argparse)
  3  refusal (an operation that would fabricate or skip verification)
  4  filesystem I/O error
"""

import argparse
import hashlib
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

VERSION = "0.2-decoded"

# ---- structural facts, not vendor data ------------------------------------
UIMAGE_MAGIC = 0x27051956
FDT_MAGIC = 0xD00DFEED
GATE_MAGIC = 0xD7B7AB1E           # TRAILER-FORMAT.md; U-Boot FUN_800743ec
UIMAGE_HDR_LEN = 0x40
FIXED_TRAILER_SIZE = 40732        # fixed verified-lab-lane size (TARGET 3.2)
KERNEL_SLOT_LIMIT = 8650752       # 0x840000
ROOTFS_SLOT_LIMIT = 24313856      # 0x1740000 (informational only)
LAST_OVERLAY_PAD_ALLOWANCE = 1024 # last overlay's FDT trailing padding

# overlay-container descriptor, decoded from the stock U-Boot applier.
DESCRIPTOR_HEADER_LEN = 32
DESCRIPTOR_ENTRY_LEN = 32
DESCRIPTOR_MAX_CONTAINER = 0x800000   # bootfip: total <= 8 MiB
DESCRIPTOR_MAX_OVERLAYS = 64          # bootfip: entry count <= 64
DESCRIPTOR_MAX_OVERLAY_FDT = 0x20000  # bootfip: overlay totalsize <= 128 KiB
DESCRIPTOR_COUNT_OFFSET = 16
# Offsets whose value bootfip requires to be exactly 32. Their *meaning* is not
# established (candidates: header size / entry size / alignment); the values
# and the hard checks are measured. See TRAILER-FORMAT.md, unknowns.
DESCRIPTOR_CONST_WORDS = ((8, 32), (12, 32), (20, 32))
# Decoded overlay match table: luofu/hsan row maps to the board-strap callback.
MATCH_FAMILY = "luofu"            # compatible substring the loader's table matches
MATCH_VENDOR = "hsan"             # vendor_id / env vendorid on this board
MATCH_DEFAULT_BOARD_ID = 3        # vendor overlay for R116; live DT model evidence

FDT_BEGIN_NODE = 1
FDT_END_NODE = 2
FDT_PROP = 3
FDT_NOP = 4
FDT_END = 9
FDT_HDR_LEN = 0x28
FDT_MAX_VERSION = 17
FDT_MIN_VERSION = 16

EXIT_OK = 0
EXIT_FAIL = 1
EXIT_USAGE = 2
EXIT_REFUSED = 3
EXIT_IO = 4


class SliceError(Exception):
    """The artifact does not satisfy the measured FIP constraints."""


class Refusal(Exception):
    """The requested operation is not safe/possible and must not be faked."""


def _be32(buf, off, what):
    if off < 0 or off + 4 > len(buf):
        raise SliceError("%s: out of bounds at offset 0x%x (size 0x%x)"
                         % (what, off, len(buf)))
    return struct.unpack_from(">I", buf, off)[0]


def _align4(value):
    return (value + 3) & ~3


def _sha256(data):
    return hashlib.sha256(data).hexdigest()


def _hex(val):
    return "0x%08x" % val


def _cstring(buf, off, limit, what):
    if off < 0 or off >= limit:
        raise SliceError("%s: string offset 0x%x outside its block" % (what, off))
    end = buf.find(b"\x00", off, limit)
    if end < 0:
        raise SliceError("%s: unterminated string at 0x%x" % (what, off))
    try:
        return bytes(buf[off:end]).decode("ascii")
    except UnicodeDecodeError:
        raise SliceError("%s: non-ASCII string at 0x%x" % (what, off))


def parse_fdt_header(buf, off, what, allow_incomplete=False):
    """Validate a flattened-device-tree header and block geometry.

    allow_incomplete=True is used only for the last overlay of the fixed
    40,732-byte bench artifact, whose declared totalsize extends 96 bytes past
    the end of the file inside trailing zero padding. In that mode the struct
    and strings blocks must still be complete; only the padding may be absent.
    """
    if off < 0 or off + FDT_HDR_LEN > len(buf):
        raise SliceError("%s: too small for an FDT header" % what)
    magic = _be32(buf, off, "%s magic" % what)
    if magic != FDT_MAGIC:
        raise SliceError("%s: bad FDT magic 0x%08x" % (what, magic))
    totalsize = _be32(buf, off + 4, "%s totalsize" % what)
    off_struct = _be32(buf, off + 8, "%s off_dt_struct" % what)
    off_strings = _be32(buf, off + 12, "%s off_dt_strings" % what)
    off_rsvmap = _be32(buf, off + 16, "%s off_mem_rsvmap" % what)
    version = _be32(buf, off + 20, "%s version" % what)
    last_comp = _be32(buf, off + 24, "%s last_comp_version" % what)
    size_strings = _be32(buf, off + 32, "%s size_dt_strings" % what)
    size_struct = _be32(buf, off + 36, "%s size_dt_struct" % what)
    available = len(buf) - off
    if totalsize < FDT_HDR_LEN:
        raise SliceError("%s: implausible FDT totalsize 0x%x" % (what, totalsize))
    if not (FDT_MIN_VERSION <= version <= FDT_MAX_VERSION):
        raise SliceError("%s: unsupported FDT version %d" % (what, version))
    if last_comp < FDT_MIN_VERSION or last_comp > version:
        raise SliceError("%s: implausible FDT last_comp_version %d"
                         % (what, last_comp))
    for name, value in (
            ("off_dt_struct", off_struct),
            ("off_dt_strings", off_strings),
            ("off_mem_rsvmap", off_rsvmap)):
        if value < FDT_HDR_LEN or value >= totalsize:
            raise SliceError("%s: %s 0x%x outside the FDT (totalsize 0x%x)"
                             % (what, name, value, totalsize))
    if size_struct < 4 or size_strings < 1:
        raise SliceError("%s: empty struct/strings block" % what)
    if off_struct + size_struct > totalsize:
        raise SliceError("%s: struct block runs past totalsize" % what)
    if off_strings + size_strings > totalsize:
        raise SliceError("%s: strings block runs past totalsize" % what)
    # The blocks themselves must always be present; only trailing padding may
    # be missing in the fixed bench artifact.
    if off_struct + size_struct > available:
        raise SliceError("%s: struct block runs past the supplied bytes" % what)
    if off_strings + size_strings > available:
        raise SliceError("%s: strings block runs past the supplied bytes" % what)
    if not allow_incomplete and totalsize > available:
        raise SliceError(
            "%s: FDT totalsize 0x%x runs past the supplied bytes 0x%x"
            % (what, totalsize, available))
    if _be32(buf, off + off_struct, "%s first struct token" % what) != FDT_BEGIN_NODE:
        raise SliceError("%s: does not start with FDT_BEGIN_NODE" % what)
    end_token = _be32(buf, off + off_struct + size_struct - 4,
                      "%s last struct token" % what)
    if end_token != FDT_END:
        raise SliceError("%s: struct block does not end with FDT_END" % what)
    if buf[off + off_strings + size_strings - 1] != 0:
        raise SliceError("%s: strings block does not end with NUL" % what)
    return {
        "offset": off,
        "totalsize": totalsize,
        "off_struct": off_struct,
        "off_strings": off_strings,
        "size_struct": size_struct,
        "size_strings": size_strings,
        "version": version,
        "last_comp_version": last_comp,
        "logical_end": off + off_strings + size_strings,
    }


def fdt_root_props(buf, hdr, what):
    """Return the root node's properties (name -> raw value bytes)."""
    struct_start = hdr["offset"] + hdr["off_struct"]
    struct_end = struct_start + hdr["size_struct"]
    strings_start = hdr["offset"] + hdr["off_strings"]
    strings_end = strings_start + hdr["size_strings"]
    pos = struct_start
    token = _be32(buf, pos, "%s root token" % what)
    if token != FDT_BEGIN_NODE:
        raise SliceError("%s: struct block does not begin at the root node" % what)
    pos += 4
    end = buf.find(b"\x00", pos, struct_end)
    if end < 0:
        raise SliceError("%s: unterminated root node name" % what)
    pos = struct_start + _align4(end + 1 - struct_start)
    props = {}
    while True:
        if pos + 4 > struct_end:
            raise SliceError("%s: truncated root node" % what)
        token = _be32(buf, pos, "%s root token" % what)
        pos += 4
        if token == FDT_PROP:
            if pos + 8 > struct_end:
                raise SliceError("%s: truncated property header" % what)
            length = _be32(buf, pos, "%s property length" % what)
            nameoff = _be32(buf, pos + 4, "%s property name offset" % what)
            pos += 8
            if pos + length > struct_end:
                raise SliceError("%s: property value runs past the struct block"
                                 % what)
            name = _cstring(buf, strings_start + nameoff, strings_end,
                            "%s property name" % what)
            props[name] = bytes(buf[pos:pos + length])
            pos = struct_start + _align4(pos + length - struct_start)
        elif token == FDT_NOP:
            continue
        elif token in (FDT_END_NODE, FDT_BEGIN_NODE):
            # Root properties end at the first subnode (BEGIN_NODE) or at the
            # root END_NODE for a childless tree.
            return props
        else:
            raise SliceError("%s: unexpected token 0x%x at the root node"
                             % (what, token))


def fdt_stringlist(value, what):
    if not value:
        raise SliceError("%s: missing or empty string list" % what)
    parts = bytes(value).split(b"\x00")
    if parts and parts[-1] == b"":
        parts = parts[:-1]
    out = []
    for part in parts:
        try:
            out.append(part.decode("ascii"))
        except UnicodeDecodeError:
            raise SliceError("%s: non-ASCII compatible string" % what)
    return out


def fdt_u32(value, what):
    if value is None or len(value) != 4:
        raise SliceError("%s: expected a 32-bit cell" % what)
    return struct.unpack(">I", value)[0]


def overlay_match_info(overlay, what):
    """Decode the properties the stock loader matches overlays on."""
    hdr = parse_fdt_header(overlay, 0, what)
    if hdr["totalsize"] != len(overlay):
        raise SliceError(
            "%s: file is %d bytes but its FDT totalsize is %d; supply exactly "
            "the compiled blob" % (what, len(overlay), hdr["totalsize"]))
    if hdr["totalsize"] > DESCRIPTOR_MAX_OVERLAY_FDT:
        raise SliceError(
            "%s: overlay FDT totalsize %d B over the loader's 128 KiB cap"
            % (what, hdr["totalsize"]))
    props = fdt_root_props(overlay, hdr, what)
    compatible = fdt_stringlist(props.get("compatible"),
                                "%s compatible" % what)
    vendor_id = props.get("vendor_id")
    if vendor_id is None:
        raise SliceError(
            "%s: no vendor_id property; the decoded loader match calls "
            "strcmp(vendor_id, ...) and a missing property is not the vendor "
            "shape" % what)
    vendor_id = fdt_stringlist(vendor_id, "%s vendor_id" % what)[0]
    board_id = fdt_u32(props.get("board_id"), "%s board_id" % what)
    return {
        "compatible": compatible,
        "vendor_id": vendor_id,
        "board_id": board_id,
        "header": hdr,
    }


def parse_trailer(trailer, source):
    """Validate a FIP trailer (base DTB + overlay container) and return fields.

    Anatomy (TRAILER-FORMAT.md; verified against local-only artifacts and the
    stock U-Boot applier FUN_800743ec):
        0                        base DTB (FDT magic d00dfeed, totalsize N)
        N                        descriptor: 32-byte header + count*32 entries
        N + 32 + 32*count        overlay 0 (FDT, <=128 KiB)
        ...                      overlays 1..count-1, contiguous
    The container total in the descriptor header = 32 + 32*count + sum(sizes).
    The fixed 40,732-byte lab artifact is 96 bytes shorter than its 40,828-byte
    vendor container; the missing bytes are inside the last overlay's zero FDT
    trailing padding, so that exact artifact is accepted with a recorded
    shortfall. An arbitrary short trailer is not tolerated.
    """
    if len(trailer) < FDT_HDR_LEN + DESCRIPTOR_HEADER_LEN:
        raise SliceError("%s: too small to be a FIP trailer" % source)
    base_hdr = parse_fdt_header(trailer, 0, "%s base DTB" % source)
    base_total = base_hdr["totalsize"]
    desc_off = base_total
    if desc_off + DESCRIPTOR_HEADER_LEN > len(trailer):
        raise SliceError("%s: no overlay descriptor after the base DTB" % source)
    magic = _be32(trailer, desc_off, "%s descriptor magic" % source)
    if magic != GATE_MAGIC:
        raise SliceError(
            "%s: gate magic at trailer+0x%x is 0x%08x, want 0x%08x"
            % (source, desc_off, magic, GATE_MAGIC))
    container_total = _be32(trailer, desc_off + 4,
                            "%s descriptor container total" % source)
    if container_total == 0 or container_total > DESCRIPTOR_MAX_CONTAINER:
        raise SliceError("%s: descriptor container total 0x%x outside (0, 8 MiB]"
                         % (source, container_total))
    for word_off, wanted in DESCRIPTOR_CONST_WORDS:
        value = _be32(trailer, desc_off + word_off,
                      "%s descriptor word +%d" % (source, word_off))
        if value != wanted:
            raise SliceError(
                "%s: descriptor word +%d is %d, the loader requires %d"
                % (source, word_off, value, wanted))
    count = _be32(trailer, desc_off + DESCRIPTOR_COUNT_OFFSET,
                  "%s descriptor entry count" % source)
    if count == 0:
        raise SliceError(
            "%s: descriptor entry count is 0; the loader would find no overlay "
            "and refuse to boot" % source)
    if count > DESCRIPTOR_MAX_OVERLAYS:
        raise SliceError("%s: descriptor entry count %d over the loader's %d"
                         % (source, count, DESCRIPTOR_MAX_OVERLAYS))
    desc_len = DESCRIPTOR_HEADER_LEN + count * DESCRIPTOR_ENTRY_LEN
    if desc_off + desc_len > len(trailer):
        raise SliceError("%s: descriptor entries run past the trailer" % source)
    entries = []
    expected_offset = desc_len
    sum_sizes = 0
    for i in range(count):
        entry_off = desc_off + DESCRIPTOR_HEADER_LEN + i * DESCRIPTOR_ENTRY_LEN
        size = _be32(trailer, entry_off, "%s entry %d size" % (source, i))
        offset = _be32(trailer, entry_off + 4,
                       "%s entry %d offset" % (source, i))
        if size == 0 or size > DESCRIPTOR_MAX_OVERLAY_FDT:
            raise SliceError(
                "%s: entry %d size 0x%x outside (0, 128 KiB]; the loader "
                "requires an overlay FDT <= 128 KiB" % (source, i, size))
        if offset != expected_offset:
            raise SliceError(
                "%s: entry %d offset 0x%x is not the canonical contiguous "
                "layout (expected 0x%x)" % (source, i, offset, expected_offset))
        if offset + size > container_total:
            raise SliceError("%s: entry %d runs past the container total"
                             % (source, i))
        entries.append({"size": size, "offset": offset})
        sum_sizes += size
        expected_offset += size
    if container_total != desc_len + sum_sizes:
        raise SliceError(
            "%s: container total 0x%x != descriptor 0x%x + overlay sizes 0x%x"
            % (source, container_total, desc_len, sum_sizes))
    nominal_end = desc_off + container_total
    for i, entry in enumerate(entries):
        ov_off = desc_off + entry["offset"]
        incomplete = (i == count - 1 and ov_off + entry["size"] > len(trailer))
        ov_hdr = parse_fdt_header(trailer, ov_off, "%s overlay %d"
                                  % (source, i), allow_incomplete=incomplete)
        if ov_hdr["totalsize"] != entry["size"]:
            raise SliceError(
                "%s: entry %d size 0x%x != overlay FDT totalsize 0x%x"
                % (source, i, entry["size"], ov_hdr["totalsize"]))
        if ov_hdr["logical_end"] > len(trailer):
            raise SliceError("%s: overlay %d logical content runs past the "
                             "supplied bytes" % (source, i))
    if len(trailer) > nominal_end:
        raise SliceError("%s: %d unexpected bytes after the overlay container"
                         % (source, len(trailer) - nominal_end))
    truncated = nominal_end - len(trailer)
    if truncated:
        if len(trailer) != FIXED_TRAILER_SIZE:
            raise SliceError(
                "%s: trailer is %d bytes, %d short of the descriptor container "
                "total; only the fixed %d-byte bench artifact may be short"
                % (source, len(trailer), truncated, FIXED_TRAILER_SIZE))
        if truncated > LAST_OVERLAY_PAD_ALLOWANCE:
            raise SliceError(
                "%s: trailer is short by %d bytes, more than the last "
                "overlay's %d-byte padding allowance"
                % (source, truncated, LAST_OVERLAY_PAD_ALLOWANCE))
    return {
        "source": source,
        "size": len(trailer),
        "base_total": base_total,
        "descriptor_offset": desc_off,
        "descriptor_len": desc_len,
        "container_total": container_total,
        "overlay_count": count,
        "entries": entries,
        "overlay0_len": entries[0]["size"],
        "overlay1_len": entries[1]["size"] if count > 1 else 0,
        "truncated_padding": truncated,
        "gate_magic": magic,
    }


def parse_uimage(data, source):
    """Validate a legacy uImage header and return its fields as a dict."""
    if len(data) < UIMAGE_HDR_LEN:
        raise SliceError("%s: too small for a uImage header (0x%x < 0x40)"
                         % (source, len(data)))
    magic = _be32(data, 0, "uImage magic")
    if magic != UIMAGE_MAGIC:
        raise SliceError("%s: bad uImage magic 0x%08x (want 0x%08x)"
                         % (source, magic, UIMAGE_MAGIC))
    hcrc = _be32(data, 4, "uImage hcrc")
    ih_size = _be32(data, 12, "uImage ih_size")
    ih_load = _be32(data, 16, "uImage ih_load")
    ih_ep = _be32(data, 20, "uImage ih_ep")
    dcrc = _be32(data, 24, "uImage dcrc")
    name = bytes(data[32:64]).split(b"\0")[0].decode("utf-8", "replace")
    if ih_size == 0:
        raise SliceError("%s: uImage ih_size is 0" % source)
    if UIMAGE_HDR_LEN + ih_size > len(data):
        raise SliceError(
            "%s: uImage payload runs past end of input "
            "(0x40 + 0x%x > 0x%x)" % (source, ih_size, len(data)))
    # Header CRC: standard mkimage CRC32 over the 64-byte header with the hcrc
    # field zeroed. The boot path does not verify it (TRAILER-FORMAT.md
    # section on bootfip validation; TARGET-DESIGN.md 3.1), but a build
    # product should be well formed.
    hdr = bytearray(data[:UIMAGE_HDR_LEN])
    hdr[4:8] = b"\0\0\0\0"
    if (zlib.crc32(bytes(hdr)) & 0xFFFFFFFF) != hcrc:
        raise SliceError("%s: uImage header CRC mismatch (hcrc 0x%08x)"
                         % (source, hcrc))
    payload = data[UIMAGE_HDR_LEN:UIMAGE_HDR_LEN + ih_size]
    if dcrc not in (0, 0xFFFFFFFF):
        if (zlib.crc32(payload) & 0xFFFFFFFF) != dcrc:
            raise SliceError("%s: uImage data CRC mismatch (dcrc 0x%08x)"
                             % (source, dcrc))
    return {
        "source": source,
        "name": name,
        "ih_size": ih_size,
        "ih_load": ih_load,
        "ih_ep": ih_ep,
        "hcrc": hcrc,
        "dcrc": dcrc,
        "payload": payload,
        "payload_end": UIMAGE_HDR_LEN + ih_size,
    }


def validate_slice_bytes(data, label, max_size=KERNEL_SLOT_LIMIT):
    """Validate a full FIP slice (uImage + trailer). Returns field dict."""
    if len(data) > max_size:
        raise SliceError("%s: slice is %d bytes, over the %d-byte kernel slot "
                         "budget (TARGET-DESIGN.md 3.2)"
                         % (label, len(data), max_size))
    if len(data) <= UIMAGE_HDR_LEN:
        raise SliceError("%s: too small to be a FIP slice" % label)
    uimage = parse_uimage(data, label)
    payload_end = uimage["payload_end"]
    trailer = data[payload_end:]
    trailer_info = parse_trailer(trailer, "%s trailer" % label)
    # The loader gate: 32-bit BE at 0x40 + ih_size + base-DTB totalsize must be
    # the overlay-container magic. The descriptor offset IS that address.
    gate_offset = payload_end + trailer_info["base_total"]
    gate_value = _be32(data, gate_offset, "gate word")
    if gate_value != GATE_MAGIC:
        raise SliceError(
            "%s: gate word at 0x%x is 0x%08x, want 0x%08x "
            "(TRAILER-FORMAT.md; TARGET-DESIGN.md 3.2)"
            % (label, gate_offset, gate_value, GATE_MAGIC))
    return {
        "label": label,
        "size": len(data),
        "uimage": uimage,
        "trailer": trailer_info,
        "gate_offset": gate_offset,
        "gate_value": gate_value,
    }


def _read(path, what):
    p = Path(path)
    try:
        return p.read_bytes(), p
    except OSError as exc:
        raise OSError("cannot read %s %s: %s" % (what, p, exc))


def _check_trailer_input(path):
    data, p = _read(path, "trailer")
    return data, p, parse_trailer(data, str(p))


def _expected_load(args):
    """Derive the expected load/entry address, or return None.

    TARGET-DESIGN.md 3.2: the loader stages the slot at --loader-stage and the
    ARM decompressor computes (pc & 0xf8000000) + TEXT_OFFSET. This is the
    derivation the build must assert instead of copying 0x80608000.
    """
    if args.loader_stage is None and args.text_offset is None:
        return None
    if args.loader_stage is None or args.text_offset is None:
        raise SliceError("--loader-stage and --text-offset must be given "
                         "together to derive the load/entry address")
    expected = (args.loader_stage & 0xF8000000) + args.text_offset
    if args.expect_load is not None and args.expect_load != expected:
        raise SliceError(
            "--expect-load 0x%08x contradicts the TEXT_OFFSET derivation "
            "(0x%08x & 0xf8000000) + 0x%08x = 0x%08x"
            % (args.expect_load, args.loader_stage, args.text_offset, expected))
    if args.expect_entry is not None and args.expect_entry != expected:
        raise SliceError(
            "--expect-entry 0x%08x contradicts the TEXT_OFFSET derivation "
            "(0x%08x & 0xf8000000) + 0x%08x = 0x%08x"
            % (args.expect_entry, args.loader_stage, args.text_offset, expected))
    return expected


def _print_trailer_report(t, indent="  "):
    print("%sdescriptor:      %d B (%d-byte header + %d x %d-byte entries), "
          "container total %d B"
          % (indent, t["descriptor_len"], DESCRIPTOR_HEADER_LEN,
             t["overlay_count"], DESCRIPTOR_ENTRY_LEN, t["container_total"]))
    for i, entry in enumerate(t["entries"]):
        print("%s  overlay %-3d    size %d B, offset %d B (trailer+0x%x)"
              % (indent, i, entry["size"], entry["offset"],
                 t["descriptor_offset"] + entry["offset"]))


def _print_slice_report(info):
    u = info["uimage"]
    t = info["trailer"]
    print("FIP slice check: PASS (measurable constraints only)")
    print("  file:            %s" % info["label"])
    print("  slice size:      %d B (kernel slot limit %d B)"
          % (info["size"], KERNEL_SLOT_LIMIT))
    print("  uImage:          magic ok, name=%r, ih_size=%d (0x%x)"
          % (u["name"], u["ih_size"], u["ih_size"]))
    print("  uImage load/ep:  0x%08x / 0x%08x" % (u["ih_load"], u["ih_ep"]))
    print("  uImage CRCs:     header ok, data %s (not verified by bootfip)"
          % ("ok" if u["dcrc"] not in (0, 0xFFFFFFFF) else "not set"))
    print("  trailer:         %d B" % t["size"])
    print("  base DTB:        totalsize %d B" % t["base_total"])
    _print_trailer_report(t)
    print("  gate:            magic 0x%08x at 0x%x"
          % (info["gate_value"], info["gate_offset"]))
    if t["truncated_padding"]:
        print("  note:            trailer is %d B short of the descriptor "
              "container total; inside the last overlay's %d-byte FDT padding "
              "(measured tolerance, TRAILER-FORMAT.md)"
              % (t["truncated_padding"], LAST_OVERLAY_PAD_ALLOWANCE))
    print("")
    print("This proves the measured layout and gate magic only. It does NOT "
          "prove the stock loader accepts the slice: that needs one attended "
          "boot on a recovered unit (TARGET-DESIGN.md 5.2, 9).")


def cmd_check(args):
    data, p = _read(args.slice, "slice")
    info = validate_slice_bytes(data, str(p), args.max_size)
    _print_slice_report(info)
    return EXIT_OK


def cmd_check_trailer(args):
    data, p = _read(args.trailer, "trailer")
    t = parse_trailer(data, str(p))
    print("FIP trailer check: PASS (measured/decoded structure only)")
    print("  file:            %s" % p)
    print("  trailer size:    %d B" % t["size"])
    print("  base DTB:        totalsize %d B (gate at trailer+0x%x)"
          % (t["base_total"], t["descriptor_offset"]))
    _print_trailer_report(t)
    if t["truncated_padding"]:
        print("  note:            trailer is %d B short of the container total "
              "inside the last overlay's padding (fixed bench artifact only)"
              % t["truncated_padding"])
    print("")
    print("The stock bootfip applier performs the checks decoded in "
          "TRAILER-FORMAT.md; acceptance of a novel trailer still needs one "
          "attended boot (TARGET-DESIGN.md 5.2, 9).")
    return EXIT_OK


def cmd_assemble(args):
    uimage_data, uimage_path = _read(args.uimage, "uImage")
    parse_uimage(uimage_data, str(uimage_path))
    trailer_data, trailer_path, _ = _check_trailer_input(args.trailer)
    expected = _expected_load(args)
    if expected is not None:
        u = parse_uimage(uimage_data, str(uimage_path))
        if u["ih_load"] != expected or u["ih_ep"] != expected:
            raise SliceError(
                "uImage load/entry 0x%08x/0x%08x does not match the derived "
                "address 0x%08x ((stage & 0xf8000000) + TEXT_OFFSET)"
                % (u["ih_load"], u["ih_ep"], expected))
    else:
        print("WARNING: load/entry address was not derived (no --loader-stage/"
              "--text-offset/--expect-load given). The build must assert it "
              "(TARGET-DESIGN.md 3.2).", file=sys.stderr)
    data = uimage_data + trailer_data
    info = validate_slice_bytes(data, "assembled slice", args.max_size)
    out = Path(args.output)
    if out.exists() and not args.force:
        raise SliceError("refusing to overwrite %s (use --force)" % out)
    out.write_bytes(data)
    _print_slice_report(info)
    print("  wrote:           %s" % out)
    return EXIT_OK


def cmd_manifest(args):
    data, p = _read(args.slice, "slice")
    info = validate_slice_bytes(data, str(p), args.max_size)
    u = info["uimage"]
    t = info["trailer"]
    lines = [
        "# fip-manifest.txt - generated by fip-slice.py %s "
        "(SCHEMATIC schema, not frozen)" % VERSION,
        "# target: %s" % args.target,
        "# device: %s" % args.device,
        "manifest-version: 2",
        "slice-size: %d" % info["size"],
        "slice-sha256: %s" % _sha256(data),
        "slot-limit: %d" % KERNEL_SLOT_LIMIT,
        "uimage-name: %s" % u["name"],
        "uimage-ih-size: %d" % u["ih_size"],
        "uimage-ih-load: %s" % _hex(u["ih_load"]),
        "uimage-ih-ep: %s" % _hex(u["ih_ep"]),
        "uimage-hcrc: %s" % _hex(u["hcrc"]),
        "uimage-dcrc: %s" % _hex(u["dcrc"]),
        "trailer-size: %d" % t["size"],
        "trailer-sha256: %s" % _sha256(data[u["payload_end"]:]),
        "base-dtb-totalsize: %d" % t["base_total"],
        "descriptor-length: %d" % t["descriptor_len"],
        "descriptor-container-total: %d" % t["container_total"],
        "descriptor-overlay-count: %d" % t["overlay_count"],
        "descriptor-truncated-padding: %d" % t["truncated_padding"],
    ]
    for i, entry in enumerate(t["entries"]):
        lines.append("overlay%d-size: %d" % (i, entry["size"]))
        lines.append("overlay%d-offset: %d" % (i, entry["offset"]))
    lines.extend([
        "gate-offset: 0x%x" % info["gate_offset"],
        "gate-magic: 0x%08x" % info["gate_value"],
        "# platform_check_image (not written yet) must recompute every field "
        "above from the received bytes and refuse anything else "
        "(TARGET-DESIGN.md 5.4).",
    ])
    out = Path(args.output)
    if out.exists() and not args.force:
        raise SliceError("refusing to overwrite %s (use --force)" % out)
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("fip manifest: PASS (slice validated first)")
    print("  slice:  %s" % p)
    print("  output: %s" % out)
    return EXIT_OK


def _fdtoverlay_apply_check(base, overlays, mode):
    """Run the loader's libfdt overlay-apply step on each candidate overlay.

    The stock applier calls fdt_overlay_apply(base, overlay) for the first
    overlay that matches; a failed apply makes it return -1 and the loader
    does not jump. fdtoverlay is the same libfdt operation, so use it when the
    host has it. mode: auto (use if present, otherwise warn), require (fail if
    absent or if any apply fails), skip.
    """
    if mode == "skip":
        print("  overlay apply:   skipped (--overlay-apply skip)")
        return
    exe = shutil.which("fdtoverlay")
    if exe is None:
        if mode == "require":
            raise SliceError(
                "--overlay-apply require: fdtoverlay not found on PATH; "
                "install the dtc package or use --overlay-apply skip")
        print("  overlay apply:   NOT checked (fdtoverlay not on PATH); the "
              "build should run it before trusting the trailer",
              file=sys.stderr)
        return
    for i, data in enumerate(overlays):
        with tempfile.TemporaryDirectory(prefix="fip-slice-") as tmpdir:
            tmp = Path(tmpdir)
            (tmp / "base.dtb").write_bytes(base)
            (tmp / "overlay.dtbo").write_bytes(data)
            proc = subprocess.run(
                [exe, "-i", str(tmp / "base.dtb"), "-o",
                 str(tmp / "applied.dtb"), str(tmp / "overlay.dtbo")],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if proc.returncode != 0:
            raise SliceError(
                "overlay %d is rejected by libfdt overlay apply (the same "
                "operation the stock loader performs) against this base DTB: %s"
                % (i, proc.stderr.decode("utf-8", "replace").strip()))
    print("  overlay apply:   PASS for all %d overlay(s) via fdtoverlay"
          % len(overlays))


def _build_descriptor(overlays):
    """Emit the decoded 32-byte header + 32-byte-entry descriptor."""
    count = len(overlays)
    desc_len = DESCRIPTOR_HEADER_LEN + count * DESCRIPTOR_ENTRY_LEN
    total = desc_len + sum(len(o) for o in overlays)
    if total > DESCRIPTOR_MAX_CONTAINER:
        raise SliceError("container total %d B over the loader's 8 MiB cap"
                         % total)
    header = [
        GATE_MAGIC,
        total,
        32,   # +8  required constant (semantics [U])
        32,   # +12 required constant (semantics [U])
        count,
        32,   # +20 required constant (semantics [U])
        0,    # +24 not read by the loader
        0,    # +28 not read by the loader
    ]
    out = bytearray(struct.pack(">8I", *header))
    offset = desc_len
    for data in overlays:
        # entry+0 = overlay FDT totalsize (not read by bootfip; vendor value),
        # entry+4 = overlay offset (read by bootfip), entry+8..+28 zero.
        out += struct.pack(">8I", len(data), offset, 0, 0, 0, 0, 0, 0)
        offset += len(data)
    return bytes(out), total


def cmd_make_trailer(args):
    """Generate a structurally validated trailer from user-supplied FDTs."""
    base, base_path = _read(args.base_dtb, "base DTB")
    base_hdr = parse_fdt_header(base, 0, "base DTB")
    if base_hdr["totalsize"] != len(base):
        raise SliceError(
            "base DTB %s is %d bytes but its FDT totalsize is %d; supply "
            "exactly the compiled blob (the gate sits at totalsize)"
            % (base_path, len(base), base_hdr["totalsize"]))
    overlay_data = []
    overlay_paths = []
    infos = []
    for i, path in enumerate(args.overlay):
        data, p = _read(path, "overlay %d" % i)
        what = "overlay %d (%s)" % (i, p)
        info = overlay_match_info(data, what)
        if info["compatible"][0].find(args.match_family) < 0:
            raise SliceError(
                "%s: first compatible string %r does not contain the loader's "
                "match family token %r; the stock loader's table would not "
                "match this overlay" % (what, info["compatible"][0],
                                        args.match_family))
        if info["vendor_id"] != args.expect_vendor_id:
            raise SliceError(
                "%s: vendor_id %r != %r; the decoded vendor_id match is strcmp "
                "and the stock env sets vendorid=hsan"
                % (what, info["vendor_id"], args.expect_vendor_id))
        if info["board_id"] > 0xF:
            raise SliceError(
                "%s: board_id %d does not fit the loader's 4-bit strap reader; "
                "the stock env has no boardid override"
                % (what, info["board_id"]))
        overlay_data.append(data)
        overlay_paths.append(p)
        infos.append(info)
    if not overlay_data:
        raise SliceError("at least one --overlay is required")
    selected = [i for i, info in enumerate(infos)
                if info["board_id"] == args.expect_board_id]
    if not selected:
        raise SliceError(
            "no overlay has board_id %d (found %s); on this board the stock "
            "loader selects the first overlay whose board_id matches the "
            "strap" % (args.expect_board_id,
                       ", ".join(str(info["board_id"]) for info in infos)))
    if selected[0] != 0:
        print("note: the stock applier checks entries in descriptor order and "
              "applies the FIRST match; selected overlay is entry %d, entries "
              "before it do not match board_id %d"
              % (selected[0], args.expect_board_id))
    _fdtoverlay_apply_check(base, overlay_data, args.overlay_apply)
    descriptor, total = _build_descriptor(overlay_data)
    trailer = bytes(base) + descriptor + b"".join(overlay_data)
    info = parse_trailer(trailer, "generated trailer")
    if (info["overlay_count"] != len(overlay_data)
            or info["container_total"] != total):
        raise SliceError("generated trailer did not re-parse to its own inputs")
    if len(trailer) > KERNEL_SLOT_LIMIT:
        raise SliceError("trailer alone is %d B, over the %d-byte kernel slot"
                         % (len(trailer), KERNEL_SLOT_LIMIT))
    out = Path(args.output)
    if out.exists() and not args.force:
        raise SliceError("refusing to overwrite %s (use --force)" % out)
    out.write_bytes(trailer)
    print("FIP trailer generation: PASS (decoded bootfip checks, structural)")
    print("  base DTB:        %s (FDT totalsize %d)" % (base_path, len(base)))
    for i, (p, info_i) in enumerate(zip(overlay_paths, infos)):
        print("  overlay %-3d      %s (FDT totalsize %d, compatible %r, "
              "vendor_id %r, board_id %d)"
              % (i, p, len(overlay_data[i]), info_i["compatible"][0],
                 info_i["vendor_id"], info_i["board_id"]))
    _print_trailer_report(info)
    print("  trailer size:    %d B (NOT the fixed bench-lane %d B; the loader "
          "does not require that size)" % (len(trailer), FIXED_TRAILER_SIZE))
    print("  trailer sha256:  %s" % _sha256(trailer))
    print("  wrote:           %s" % out)
    print("")
    print("This satisfies every check the decoded stock bootfip performs "
          "(TRAILER-FORMAT.md). It is NOT hardware-proven for a novel trailer: "
          "one attended boot on a recovered unit is still required "
          "(TARGET-DESIGN.md 5.2, 9).")
    return EXIT_OK


def build_parser():
    parser = argparse.ArgumentParser(
        prog="fip-slice.py",
        description="SCHEMATIC/UNTESTED: check, assemble, describe and "
                    "structurally generate a luofu FIP slice (uImage + "
                    "overlay-container trailer). TARGET-DESIGN.md 3.2, 5.2; "
                    "TRAILER-FORMAT.md.",
        epilog="No vendor data is embedded in this tool. 'check' and "
               "'check-trailer' are read-only and safe. Exit codes: 0 pass, "
               "1 validation failure, 2 usage, 3 refusal, 4 I/O.",
    )
    parser.add_argument("--version", action="version",
                        version="%(prog)s " + VERSION)
    sub = parser.add_subparsers(dest="command", required=True)

    p_check = sub.add_parser(
        "check", help="validate an existing FIP slice (read-only)")
    p_check.add_argument("slice", help="path to the FIP slice to validate")
    p_check.add_argument("--max-size", type=int, default=KERNEL_SLOT_LIMIT,
                         help="kernel slot budget in bytes (default %d)"
                              % KERNEL_SLOT_LIMIT)
    p_check.set_defaults(func=cmd_check)

    p_ct = sub.add_parser(
        "check-trailer", help="validate a FIP trailer on its own (read-only)")
    p_ct.add_argument("trailer", help="path to the FIP trailer to validate")
    p_ct.set_defaults(func=cmd_check_trailer)

    p_asm = sub.add_parser(
        "assemble", help="concatenate a validated uImage + trailer and "
                         "re-validate the result")
    p_asm.add_argument("--uimage", required=True,
                       help="legacy uImage (zImage payload)")
    p_asm.add_argument("--trailer", required=True,
                       help="FIP trailer (fixed bench artifact or make-trailer "
                            "output)")
    p_asm.add_argument("--output", required=True, help="output slice path")
    p_asm.add_argument("--max-size", type=int, default=KERNEL_SLOT_LIMIT,
                       help="kernel slot budget in bytes (default %d)"
                            % KERNEL_SLOT_LIMIT)
    p_asm.add_argument("--loader-stage", type=lambda x: int(x, 0),
                       help="stock loader staging base, e.g. 0x83200000")
    p_asm.add_argument("--text-offset", type=lambda x: int(x, 0),
                       help="kernel TEXT_OFFSET, e.g. 0x00608000")
    p_asm.add_argument("--expect-load", type=lambda x: int(x, 0),
                       help="assert uImage ih_load (overrides derivation)")
    p_asm.add_argument("--expect-entry", type=lambda x: int(x, 0),
                       help="assert uImage ih_ep (overrides derivation)")
    p_asm.add_argument("--force", action="store_true",
                       help="overwrite an existing output file")
    p_asm.set_defaults(func=cmd_assemble)

    p_man = sub.add_parser(
        "manifest", help="validate a slice and write fip-manifest.txt for "
                         "the sysupgrade container (schema not frozen)")
    p_man.add_argument("--slice", required=True, help="validated FIP slice")
    p_man.add_argument("--output", required=True,
                       help="output fip-manifest.txt path")
    p_man.add_argument("--target", default="hisilicon/luofu",
                       help="target string (informational)")
    p_man.add_argument("--device", default="cudy,wr3000-v2",
                       help="device string (informational)")
    p_man.add_argument("--max-size", type=int, default=KERNEL_SLOT_LIMIT,
                       help="kernel slot budget in bytes (default %d)"
                            % KERNEL_SLOT_LIMIT)
    p_man.add_argument("--force", action="store_true",
                       help="overwrite an existing output file")
    p_man.set_defaults(func=cmd_manifest)

    p_mk = sub.add_parser(
        "make-trailer",
        help="generate a structurally validated trailer from our own base DTB "
             "+ overlays (never copies vendor bytes)")
    p_mk.add_argument("--base-dtb", required=True,
                      help="board base DTB (FDT blob, exactly totalsize bytes)")
    p_mk.add_argument("--overlay", action="append", required=True,
                      help="one overlay DTBO (FDT blob, <=128 KiB); repeat for "
                           "the reference and board overlays")
    p_mk.add_argument("--output", required=True, help="output trailer path")
    p_mk.add_argument("--expect-board-id", type=int,
                      default=MATCH_DEFAULT_BOARD_ID,
                      help="board_id the stock loader must select on this "
                           "board (default %d = R116 vendor overlay)"
                           % MATCH_DEFAULT_BOARD_ID)
    p_mk.add_argument("--expect-vendor-id", default=MATCH_VENDOR,
                      help="vendor_id/environment vendorid (default %r; the "
                           "decoded match is strcmp)" % MATCH_VENDOR)
    p_mk.add_argument("--match-family", default=MATCH_FAMILY,
                      help="compatible family token the loader's table "
                           "matches (default %r)" % MATCH_FAMILY)
    p_mk.add_argument("--overlay-apply", choices=("auto", "require", "skip"),
                      default="auto",
                      help="check each overlay with fdtoverlay (the loader's "
                           "libfdt overlay apply); auto=use if installed")
    p_mk.add_argument("--force", action="store_true",
                      help="overwrite an existing output file")
    p_mk.set_defaults(func=cmd_make_trailer)

    return parser


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except SliceError as exc:
        print("fip-slice: FAIL: %s" % exc, file=sys.stderr)
        return EXIT_FAIL
    except Refusal as exc:
        print("fip-slice: REFUSED: %s" % exc, file=sys.stderr)
        return EXIT_REFUSED
    except OSError as exc:
        print("fip-slice: I/O error: %s" % exc, file=sys.stderr)
        return EXIT_IO


if __name__ == "__main__":
    sys.exit(main())
