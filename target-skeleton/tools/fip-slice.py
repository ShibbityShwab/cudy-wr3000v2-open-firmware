#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# -*- coding: utf-8 -*-
"""fip-slice.py - FIP slice checker/assembler for the HiSilicon luofu loader.

STATUS: SCHEMATIC / UNTESTED TARGET TOOL. Read the module docstring below
before using it for anything that touches a router.

WHAT THIS IS
  The stock vendor U-Boot on the Cudy WR3000 v2.0 / luofu R116 boots with
      bootcmd=mtd read kernel${bootflag} ${loadaddr}; bootfip ${loadaddr}
  "bootfip" accepts only the vendor's FIP package: a legacy uImage followed by
  a fixed-size trailer that carries the base DTB, a 96-byte overlay container
  descriptor and two DTBO overlays. The layout is documented in
  opensource/docs/TARGET-DESIGN.md section 3.2 (facts: bench ledger PHASE 91;
  phase42-decode/report.md section 2; parent tools/fire-preflight.sh).

WHAT THIS TOOL DOES NOT DO
  It does NOT generate the 40732-byte trailer, and it does not carry or copy
  vendor bytes. The descriptor fields at +8..+28 are not fully reversed, the
  loader's overlay match rule is [U], and the reason the fixed lab artifact is
  96 bytes shorter than the vendor tail is not documented
  (TARGET-DESIGN.md 3.2, 5.2, 12 item 1). Fabricating a trailer would be a
  false claim that the loader gate is understood. The "make-trailer" command
  therefore checks its inputs and refuses.

  It does NOT bypass the gate check. Every artifact this tool writes is first
  validated: uImage magic and CRCs, the fixed trailer size, the FDT chain, the
  descriptor size fields, the gate magic at 0x40 + ih_size + base-DTB totalsize
  and the 8650752-byte kernel slot budget.

SAFE MODE
  "check" is read-only: it opens the slice, validates it, prints a report and
  exits. It never writes, never touches a device and reads no vendor data
  beyond the file the operator points it at.

INPUTS FOR ASSEMBLY
  --uimage  a legacy uImage. The bench lane used:
              mkimage -A arm -O linux -T kernel -C none \
                -a 0x80608000 -e 0x80608000 -n Linux-5.10.201 -d body uImage
            but the load/entry address must be DERIVED, not copied: the stock
            loader stages the slot at 0x83200000 and the decompressor computes
            (pc & 0xf8000000) + TEXT_OFFSET (TARGET-DESIGN.md 3.2). Pass
            --loader-stage 0x83200000 --text-offset 0x00608000 and the tool
            computes/asserts the address for you.
  --trailer the fixed 40732-byte trailer. It must be produced from our own
            sources by a reviewed generator (TARGET-DESIGN.md 5.2) or be a
            legally obtained equivalent. This tool cannot produce it.

EXIT CODES
  0  pass / artifact written
  1  validation failure (bad slice, bad trailer, bad expectations)
  2  argument/usage error (argparse)
  3  refusal (an operation that would fabricate or skip verification)
  4  filesystem I/O error
"""

import argparse
import hashlib
import struct
import sys
import zlib
from pathlib import Path

VERSION = "0.1-schematic"

# ---- structural facts, not vendor data ------------------------------------
UIMAGE_MAGIC = 0x27051956
FDT_MAGIC = 0xD00DFEED
GATE_MAGIC = 0xD7B7AB1E           # TARGET-DESIGN.md 3.2; phase42-decode
UIMAGE_HDR_LEN = 0x40
TRAILER_SIZE = 40732              # fixed verified-lab-lane size (TARGET 3.2)
KERNEL_SLOT_LIMIT = 8650752       # 0x840000
ROOTFS_SLOT_LIMIT = 24313856      # 0x1740000 (informational only)
DESCRIPTOR_LEN = 96               # measured descriptor size (TARGET 3.2)
LAST_OVERLAY_PAD_ALLOWANCE = 1024 # last overlay's FDT trailing padding
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


def _sha256(data):
    return hashlib.sha256(data).hexdigest()


def _hex(val):
    return "0x%08x" % val


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
    # field zeroed. The boot path does not verify it (TARGET-DESIGN.md 3.1),
    # but a build product should be well formed.
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


def parse_trailer(trailer, source):
    """Validate the measured trailer anatomy and return its fields as a dict.

    Anatomy (TARGET-DESIGN.md 3.2, verified against the local-only bench
    artifact; values here are recomputed from the file, never embedded):
        0                      base DTB (FDT magic d00dfeed, totalsize N)
        N                      descriptor, 96 bytes, BE magic d7b7ab1e
        N + 96                 overlay for board_id=0 (FDT)
        N + 96 + ov0_len       overlay for board_id=3 (FDT)
    The fixed 40732-byte lab artifact is 96 bytes shorter than the descriptor
    container total because the missing bytes are inside the last overlay's
    1024-byte trailing FDT padding (TARGET-DESIGN.md 3.2). That tolerance is
    accepted here, documented, and not generalised into "any short trailer is
    fine": the total size must still be exactly 40732.
    """
    if len(trailer) != TRAILER_SIZE:
        raise SliceError(
            "%s: trailer is %d bytes, not the fixed %d-byte bench-lane size "
            "(TARGET-DESIGN.md 3.2)" % (source, len(trailer), TRAILER_SIZE))
    base_magic = _be32(trailer, 0, "base DTB magic")
    if base_magic != FDT_MAGIC:
        raise SliceError("%s: trailer does not start with an FDT (magic 0x%08x)"
                         % (source, base_magic))
    base_total = _be32(trailer, 4, "base DTB totalsize")
    if base_total < 0x28 or base_total > len(trailer):
        raise SliceError("%s: implausible base DTB totalsize 0x%x"
                         % (source, base_total))
    desc_off = base_total
    desc_magic = _be32(trailer, desc_off, "descriptor magic")
    if desc_magic != GATE_MAGIC:
        raise SliceError(
            "%s: gate magic at trailer+0x%x is 0x%08x, want 0x%08x "
            "(TARGET-DESIGN.md 3.2)" % (source, desc_off, desc_magic,
                                        GATE_MAGIC))
    container_total = _be32(trailer, desc_off + 4, "descriptor container total")
    desc_len = _be32(trailer, desc_off + 36, "descriptor length")
    ov0_len = _be32(trailer, desc_off + 32, "overlay 0 size")
    ov1_len = _be32(trailer, desc_off + 64, "overlay 1 size")
    word68 = _be32(trailer, desc_off + 68, "descriptor word +68")
    if desc_len != DESCRIPTOR_LEN:
        raise SliceError("%s: descriptor length is %d, not the measured %d"
                         % (source, desc_len, DESCRIPTOR_LEN))
    if container_total != desc_len + ov0_len + ov1_len:
        raise SliceError(
            "%s: descriptor container total 0x%x != descriptor %d + ov0 %d + "
            "ov1 %d" % (source, container_total, desc_len, ov0_len, ov1_len))
    ov0_off = desc_off + desc_len
    if ov0_off + 8 > len(trailer):
        raise SliceError("%s: overlay 0 starts past end of trailer" % source)
    if _be32(trailer, ov0_off, "overlay 0 magic") != FDT_MAGIC:
        raise SliceError("%s: overlay 0 is not an FDT" % source)
    if _be32(trailer, ov0_off + 4, "overlay 0 totalsize") != ov0_len:
        raise SliceError("%s: overlay 0 totalsize does not match descriptor"
                         % source)
    ov1_off = ov0_off + ov0_len
    if ov1_off + 8 > len(trailer):
        raise SliceError("%s: overlay 1 starts past end of trailer" % source)
    if _be32(trailer, ov1_off, "overlay 1 magic") != FDT_MAGIC:
        raise SliceError("%s: overlay 1 is not an FDT" % source)
    if _be32(trailer, ov1_off + 4, "overlay 1 totalsize") != ov1_len:
        raise SliceError("%s: overlay 1 totalsize does not match descriptor"
                         % source)
    nominal_end = desc_off + container_total
    if nominal_end > len(trailer):
        missing = nominal_end - len(trailer)
        if missing > LAST_OVERLAY_PAD_ALLOWANCE:
            raise SliceError(
                "%s: trailer is short by %d bytes, more than the last "
                "overlay's %d-byte padding allowance"
                % (source, missing, LAST_OVERLAY_PAD_ALLOWANCE))
        truncated = missing
    else:
        truncated = 0
    if len(trailer) > nominal_end:
        raise SliceError("%s: %d unexpected bytes after the overlay container"
                         % (source, len(trailer) - nominal_end))
    return {
        "source": source,
        "size": len(trailer),
        "base_total": base_total,
        "descriptor_offset": desc_off,
        "descriptor_len": desc_len,
        "container_total": container_total,
        "overlay0_len": ov0_len,
        "overlay1_len": ov1_len,
        "descriptor_word68": word68,
        "truncated_padding": truncated,
        "gate_magic": desc_magic,
    }


def validate_slice_bytes(data, label, max_size=KERNEL_SLOT_LIMIT):
    """Validate a full FIP slice (uImage + fixed trailer). Returns field dict."""
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
            "(TARGET-DESIGN.md 3.2)" % (label, gate_offset, gate_value,
                                        GATE_MAGIC))
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
    print("  uImage CRCs:     header ok, data %s"
          % ("ok" if u["dcrc"] not in (0, 0xFFFFFFFF) else "not set"))
    print("  trailer:         %d B (fixed lane size %d)"
          % (t["size"], TRAILER_SIZE))
    print("  base DTB:        totalsize %d B" % t["base_total"])
    print("  descriptor:      %d B, container total %d B, word@+68 0x%08x "
          "(semantics NOT reversed)" % (t["descriptor_len"],
                                        t["container_total"],
                                        t["descriptor_word68"]))
    print("  overlays:        board_id-0 %d B, board_id-3 %d B (FDTs parse)"
          % (t["overlay0_len"], t["overlay1_len"]))
    print("  gate:            magic 0x%08x at 0x%x"
          % (info["gate_value"], info["gate_offset"]))
    if t["truncated_padding"]:
        print("  note:            trailer is %d B short of the descriptor "
              "container total; inside the last overlay's %d-byte FDT padding "
              "(measured tolerance, TARGET-DESIGN.md 3.2)"
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
        "manifest-version: 1",
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
        "overlay0-size: %d" % t["overlay0_len"],
        "overlay1-size: %d" % t["overlay1_len"],
        "gate-offset: 0x%x" % info["gate_offset"],
        "gate-magic: 0x%08x" % info["gate_value"],
        "# platform_check_image (not written yet) must recompute every field "
        "above from the received bytes and refuse anything else "
        "(TARGET-DESIGN.md 5.4).",
    ]
    out = Path(args.output)
    if out.exists() and not args.force:
        raise SliceError("refusing to overwrite %s (use --force)" % out)
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("fip manifest: PASS (slice validated first)")
    print("  slice:  %s" % p)
    print("  output: %s" % out)
    return EXIT_OK


def cmd_make_trailer(args):
    """Check the inputs, then refuse: the descriptor cannot be generated yet."""
    inputs = [("base DTB", args.base_dtb)] + [
        ("overlay %d" % i, p) for i, p in enumerate(args.overlay)]
    sizes = []
    for what, path in inputs:
        data, p = _read(path, what)
        magic = _be32(data, 0, "%s magic" % what)
        if magic != FDT_MAGIC:
            raise SliceError("%s %s is not an FDT (magic 0x%08x)"
                             % (what, p, magic))
        totalsize = _be32(data, 4, "%s totalsize" % what)
        if totalsize > len(data):
            raise SliceError("%s %s: FDT totalsize 0x%x exceeds file size 0x%x"
                             % (what, p, totalsize, len(data)))
        sizes.append((what, str(p), totalsize))
    for what, path, totalsize in sizes:
        print("  input %-10s %s (FDT totalsize %d)" % (what, path, totalsize))
    raise Refusal(
        "refusing to fabricate the 96-byte overlay-container descriptor.\n"
        "  The following facts are NOT established, so any generated trailer "
        "would be a guess dressed as a build artifact (TARGET-DESIGN.md 3.2, "
        "5.2, 12):\n"
        "    - descriptor words at +8..+28 (and the semantics of +68) are "
        "unreversed;\n"
        "    - the loader's vendor_id/board_id overlay match rule is [U];\n"
        "    - why the fixed lab artifact is 96 B shorter than the vendor "
        "tail is undocumented;\n"
        "    - acceptance of a generated trailer needs one attended boot on "
        "a recovered unit.\n"
        "  Required: supply a 40732-byte trailer produced from our own "
        "sources by a reviewed generator (or a legally obtained equivalent) "
        "and use 'assemble'.\n"
        "  This refusal is intentional behaviour, not a missing feature flag.")


def build_parser():
    parser = argparse.ArgumentParser(
        prog="fip-slice.py",
        description="SCHEMATIC/UNTESTED: check, assemble and describe a "
                    "luofu FIP slice (uImage + fixed 40732-byte trailer). "
                    "It refuses to generate the trailer or skip the gate "
                    "check. TARGET-DESIGN.md 3.2, 5.2.",
        epilog="No vendor data is embedded in this tool. 'check' is "
               "read-only and safe. Exit codes: 0 pass, 1 validation "
               "failure, 2 usage, 3 refusal, 4 I/O.",
    )
    parser.add_argument("--version", action="version",
                        version="%(prog)s " + VERSION + " (schematic)")
    sub = parser.add_subparsers(dest="command", required=True)

    p_check = sub.add_parser(
        "check", help="validate an existing FIP slice (read-only)")
    p_check.add_argument("slice", help="path to the FIP slice to validate")
    p_check.add_argument("--max-size", type=int, default=KERNEL_SLOT_LIMIT,
                         help="kernel slot budget in bytes (default %d)"
                              % KERNEL_SLOT_LIMIT)
    p_check.set_defaults(func=cmd_check)

    p_asm = sub.add_parser(
        "assemble", help="concatenate a validated uImage + trailer and "
                         "re-validate the result")
    p_asm.add_argument("--uimage", required=True,
                       help="legacy uImage (zImage payload)")
    p_asm.add_argument("--trailer", required=True,
                       help="fixed %d-byte FIP trailer (must be supplied; "
                            "this tool cannot generate it)" % TRAILER_SIZE)
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
        help="check trailer inputs and then REFUSE: descriptor generation "
             "is not reversed (intentional)")
    p_mk.add_argument("--base-dtb", required=True,
                      help="board base DTB (FDT) that would start the trailer")
    p_mk.add_argument("--overlay", action="append", required=True,
                      help="one overlay DTBO (FDT); repeat for both overlays")
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
