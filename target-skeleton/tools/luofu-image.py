#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# -*- coding: utf-8 -*-
"""luofu-image.py - host-side image assembly for the HiSilicon luofu R116 target.

STATUS: HOST-ONLY / NEVER BOOTED. This tool builds and validates host-side
artifacts; it touches no device and proves nothing about booting. The
rootfs_data overlay layout it now requires has NOT been booted: the previous
booted image had no rootfs_data volume (fstools mount_root fell back to tmpfs).
The empty-volume/autoresize behaviour is read from the kernel and fstools
sources (fs/ubifs/sb.c, drivers/mtd/ubi/build.c, fstools libfstools/ubi.c),
not measured on this board.

WHAT IT IS
  The target-side helper that image/luofu.mk calls after the kernel slice has
  been assembled. It covers the two image products the decoded boot layout
  needs and the pipeline that used to be a sketch:

  1. rootfs UBI: squashfs (from the normal OpenWrt rootfs build) -> the
     standard OpenWrt two-volume shape with the measured geometry (p=131072,
     m=2048, s=2048, O=2048):
       - "squashfs"    static, vol_id 0: the read-only rootfs payload;
       - "rootfs_data" dynamic, vol_id 1, vol_size=1MiB, autoresize: the
         persistent overlay, intentionally EMPTY in the image (the kernel's
         first read-write UBIFS mount formats it; fstools mount_root finds it
         by name and grows it to all free PEBs at attach).
     Validated by re-parsing the ubinize output: EC/VID headers, CRCs,
     image_seq, layout volume table, both volume names/types/flags/reserved
     PEBs, the per-LEB data CRCs of the static volume and the slot budget.
  2. sysupgrade container: kernel slice (legacy uImage + FIP trailer) + rootfs
     UBI + fip-manifest.txt + CONTROL + INSTALL-STATUS.txt in one deterministic
     tar, with every field re-derived from the bytes.
  3. check-container: independent read-back of a built sysupgrade container
     (host-side mirror of what platform_check_image must do on the device).

WHAT IT REFUSES (fail-closed)
  - A kernel slice that fip-slice.py does not validate, that exceeds the
    8,650,752-byte kernel slot, or whose fip-manifest.txt does not match the
    bytes that carry it.
  - A rootfs UBI that does not carry exactly the two expected volumes
    ("squashfs" static + "rootfs_data" dynamic/autoresize/empty), whose
    header/data CRCs do not verify, or that exceeds the 24,313,856-byte
    rootfs slot. The rootfs_data volume must be EMPTY in the image: the
    kernel's first read-write UBIFS mount formats it (fs/ubifs/sb.c
    create_default_filesystem); a pre-filled rootfs_data volume would be a
    different, unverified design.
  - Unknown ubinize-cfg keys and a cfg whose volumes are not the measured
    shape.
  - Vendor-derived input paths (dumps/, firmware/, stock/, vendor rootfs dumps).

WHAT IT DOES NOT CLAIM
  The A/B slot selector protocol (sysenv 0x10100C08), the SMP second-CPU
  bring-up and the install path are [U] on hardware (TARGET-DESIGN.md 3.4,
  5.2, 5.4, 5.5). The 2026-11-19 attended bring-up booted an earlier build of
  this image recipe to userspace (UP, 1 CPU; no rootfs_data volume), but THIS
  EXACT REBUILD has not been flashed or booted. INSTALL-STATUS.txt says so:
  "BOOT-VERIFIED RECIPE / NOT FLASHABLE-UNTIL-INSTALLED" (the string
  "NOT FLASHABLE" is kept because first-install-stock.sh and the target
  platform_check_image grep for it).

EXIT CODES
  0 pass / artifact written
  1 validation failure
  2 argument/usage error
  3 refusal (would fabricate / skip verification / claim an unverified step)
  4 filesystem I/O error
"""

import argparse
import hashlib
import importlib.util
import io
import os
import struct
import sys
import tarfile
import tempfile
import zlib
from pathlib import Path

VERSION = "0.2-overlay"

# ---- measured geometry / limits (TARGET-DESIGN.md 3.2, 3.3, 5.1) -----------
KERNEL_SLOT_LIMIT = 8650752        # 0x840000, kernela/kernelb
ROOTFS_SLOT_LIMIT = 24313856       # 0x1740000, rootfsa/rootfsb
DEFAULT_PAGE_SIZE = 2048
DEFAULT_PEB_SIZE = 131072
DEFAULT_SUBPAGE_SIZE = 2048
DEFAULT_VID_HDR_OFFSET = 2048
UBI_VOLUME_NAME = "squashfs"
ROOTFS_DATA_VOLUME_NAME = "rootfs_data"
EXPECTED_TARGET = "hisilicon/luofu"
EXPECTED_BOARD = "cudy_wr3000-v2"
FIP_MANIFEST_NAME = "fip-manifest.txt"
INSTALL_STATUS_NAME = "INSTALL-STATUS.txt"

# ---- on-flash UBI format (linux-6.18 drivers/mtd/ubi/ubi-media.h) ----------
UBI_EC_MAGIC = b"UBI#"
UBI_VID_MAGIC = b"UBI!"
UBI_LAYOUT_VOLUME_ID = 0x7FFFEFFF
UBI_VID_DYNAMIC = 1
UBI_VID_STATIC = 2
UBI_VTBL_DYNAMIC = 1
UBI_VTBL_STATIC = 2
UBI_VTBL_AUTORESIZE_FLG = 0x01
UBI_VTBL_SKIP_CRC_CHECK_FLG = 0x02
UBI_EC_HDR_LEN = 64
UBI_VID_HDR_LEN = 64
UBI_VTBL_RECORD_LEN = 172
UBI_MAX_VOLUMES = 128
UBI_CRC32_INIT = 0xFFFFFFFF

# ---- squashfs --------------------------------------------------------------
SQUASHFS_MAGICS = (b"hsqs", b"sqsh", b"shsq", b"qshs")

EXIT_OK = 0
EXIT_FAIL = 1
EXIT_USAGE = 2
EXIT_REFUSED = 3
EXIT_IO = 4


class ImageError(Exception):
    """The artifact does not satisfy the measured image constraints."""


class Refusal(Exception):
    """The requested operation would fabricate or claim something unverified."""


def _sha256(data):
    return hashlib.sha256(data).hexdigest()


def _be16(buf, off):
    return struct.unpack_from(">H", buf, off)[0]


def _be32(buf, off):
    return struct.unpack_from(">I", buf, off)[0]


def _be64(buf, off):
    return struct.unpack_from(">Q", buf, off)[0]


def _ubi_crc(buf):
    """UBI's crc32(UBI_CRC32_INIT, buf) as zlib expresses it."""
    return (zlib.crc32(bytes(buf)) ^ 0xFFFFFFFF) & 0xFFFFFFFF


def _read_file(path, what):
    p = Path(path)
    if not p.is_file():
        raise ImageError("%s not found: %s" % (what, p))
    try:
        return p.read_bytes(), p
    except OSError as exc:
        raise ImageError("%s unreadable (%s): %s" % (what, exc, p))


VENDOR_MARKERS = ("/dumps/", "/firmware/", "/stock/", "/rootfs-2.4.15/",
                  "/rootfs-2.5.24/", "/vendor/")


def _refuse_vendor_path(path):
    text = "/" + str(Path(path).resolve()).replace(os.sep, "/").strip("/") + "/"
    for marker in VENDOR_MARKERS:
        if marker in text:
            raise Refusal(
                "refusing vendor-derived input path %s; this tool must never "
                "consume a vendor dump (TARGET-DESIGN.md 11, publication "
                "boundary)" % path)


# ---------------------------------------------------------------------------
# fip-slice.py bridge
# ---------------------------------------------------------------------------
def load_fip_slice(tool_path):
    p = Path(tool_path)
    if not p.is_file():
        raise Refusal(
            "fip-slice.py not found at %s; the skeleton mapping must install "
            "target-skeleton/tools/fip-slice.py as scripts/fip-slice.py "
            "(README section 4)" % p)
    spec = importlib.util.spec_from_file_location("luofu_fip_slice", p)
    if spec is None or spec.loader is None:
        raise Refusal("cannot load fip-slice.py from %s" % p)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# ---------------------------------------------------------------------------
# ubinize cfg handling
# ---------------------------------------------------------------------------
CFG_KEYS = ("mode", "image", "vol_id", "vol_type", "vol_name", "vol_size",
            "vol_flags", "alignment")


SIZE_SUFFIXES = (("KiB", 1024), ("MiB", 1024 * 1024),
                 ("GiB", 1024 * 1024 * 1024))


def parse_size_bytes(value, source, what):
    """Parse a ubinize vol_size value (plain bytes or KiB/MiB/GiB suffix)."""
    text = (value or "").strip()
    mult = 1
    for suffix, factor in SIZE_SUFFIXES:
        if text.endswith(suffix):
            text = text[: -len(suffix)]
            mult = factor
            break
    try:
        number = int(text, 0)
    except ValueError:
        raise ImageError("%s: %s %r is not a byte size" % (source, what, value))
    if number <= 0:
        raise ImageError("%s: %s %r must be positive" % (source, what, value))
    return number * mult


def _cfg_section_checks(name, keys, source):
    if keys.get("mode") != "ubi":
        raise ImageError("%s: section %r mode is %r, want ubi"
                         % (source, name, keys.get("mode")))
    if "alignment" in keys:
        raise Refusal("%s: section %r sets \"alignment\"; the rootfs layout "
                      "uses ubinize's default alignment 1 (the measured shape)"
                      % (source, name))
    if name == UBI_VOLUME_NAME:
        if keys.get("vol_type") != "static":
            raise Refusal("%s: volume %r type is %r; the measured layout "
                          "requires a static squashfs volume (TARGET-DESIGN.md "
                          "5.3)" % (source, name, keys.get("vol_type")))
        if "vol_size" in keys:
            raise Refusal("%s: volume %r must not set vol_size; a static "
                          "volume is sized by its image and ubinize rejects an "
                          "image larger than vol_size" % (source, name))
        if "vol_flags" in keys:
            raise Refusal("%s: volume %r must not set vol_flags (autoresize is "
                          "reserved for the empty rootfs_data volume)"
                          % (source, name))
        if not keys.get("image"):
            raise ImageError("%s: volume %r has no image= line" % (source, name))
    elif name == ROOTFS_DATA_VOLUME_NAME:
        if keys.get("vol_type") != "dynamic":
            raise Refusal("%s: volume %r type is %r; the overlay volume must "
                          "be dynamic (a static volume is read-only and cannot "
                          "carry the overlay)" % (source, name,
                                                  keys.get("vol_type")))
        if keys.get("image"):
            raise Refusal("%s: volume %r must not carry an image=; it is "
                          "intentionally EMPTY in the image and the kernel's "
                          "first read-write UBIFS mount formats it (fs/ubifs/"
                          "sb.c create_default_filesystem)" % (source, name))
        if "vol_size" not in keys:
            raise Refusal("%s: volume %r needs an explicit vol_size (the "
                          "OpenWrt shape reserves 1MiB and autoresizes)"
                          % (source, name))
        if keys.get("vol_flags") != "autoresize":
            raise Refusal("%s: volume %r vol_flags is %r; the overlay volume "
                          "must have exactly vol_flags=autoresize so the "
                          "kernel grows it to all free PEBs at attach"
                          % (source, name, keys.get("vol_flags")))


def parse_ubinize_cfg(text, source):
    """Parse the checked-in ubinize cfg and require the two-volume rootfs shape.

    The shape is the standard OpenWrt squashfs+UBI layout and is the contract
    between this tool, image/luofu.mk and fstools mount_root on the target:
      [squashfs]     static,  vol_id 0, image=@ROOTFS@   read-only rootfs
      [rootfs_data]  dynamic, vol_id 1, vol_size=1MiB, vol_flags=autoresize
                     (empty; the kernel creates the UBIFS on the first
                     read-write mount, mount_root finds it by name)
    """
    sections = []
    current = None
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#") or line.startswith(";"):
            continue
        if line.startswith("[") and line.endswith("]"):
            name = line[1:-1].strip()
            if not name:
                raise ImageError("%s:%d: empty section name" % (source, lineno))
            current = {"name": name, "keys": {}}
            sections.append(current)
            continue
        if "=" not in line:
            raise ImageError("%s:%d: not a key=value line: %r"
                             % (source, lineno, raw))
        if current is None:
            raise ImageError("%s:%d: key outside a section: %r"
                             % (source, lineno, raw))
        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()
        if key not in CFG_KEYS:
            raise Refusal(
                "%s:%d: unexpected ubinize cfg key %r; the image recipe may "
                "only use the measured geometry/volume keys %s"
                % (source, lineno, key, ", ".join(CFG_KEYS)))
        if key in current["keys"]:
            raise ImageError("%s:%d: duplicate key %r" % (source, lineno, key))
        current["keys"][key] = value
    if len(sections) != 2:
        raise Refusal(
            "%s: expected exactly two UBI volume sections (%r, %r), found %d; "
            "the rootfs slot carries the squashfs rootfs plus the empty "
            "rootfs_data overlay volume (TARGET-DESIGN.md 5.1/5.3)"
            % (source, UBI_VOLUME_NAME, ROOTFS_DATA_VOLUME_NAME,
               len(sections)))
    by_name = {}
    for section in sections:
        name = section["name"]
        if name in by_name:
            raise ImageError("%s: duplicate volume section %r" % (source, name))
        by_name[name] = section
    expected_names = {UBI_VOLUME_NAME, ROOTFS_DATA_VOLUME_NAME}
    if set(by_name) != expected_names:
        raise Refusal(
            "%s: volume sections are %s, want exactly %s (names are part of "
            "the fstools mount_root contract)"
            % (source, ", ".join(sorted(by_name)),
               ", ".join(sorted(expected_names))))
    parsed = {}
    for index, name in enumerate((UBI_VOLUME_NAME, ROOTFS_DATA_VOLUME_NAME)):
        section = by_name[name]
        keys = section["keys"]
        _cfg_section_checks(name, keys, source)
        try:
            vol_id = int(keys.get("vol_id", ""), 0)
        except ValueError:
            raise ImageError("%s: volume %r vol_id %r is not an integer"
                             % (source, name, keys.get("vol_id")))
        if vol_id != index:
            raise ImageError("%s: volume %r vol_id is %d, want %d (squashfs "
                             "first, rootfs_data second)"
                             % (source, name, vol_id, index))
        if keys.get("vol_name") != name:
            raise ImageError("%s: volume section %r vol_name is %r; the name "
                             "must be %r (mount_root looks the overlay volume "
                             "up by this name)"
                             % (source, name, keys.get("vol_name"), name))
        parsed[name] = {"section": name, "keys": keys, "vol_id": vol_id}
        if name == UBI_VOLUME_NAME:
            parsed[name]["image"] = keys.get("image")
        else:
            parsed[name]["vol_size"] = parse_size_bytes(
                keys.get("vol_size"), source, "volume %r vol_size" % name)
    return {"sections": sections,
            "squashfs": parsed[UBI_VOLUME_NAME],
            "rootfs_data": parsed[ROOTFS_DATA_VOLUME_NAME]}


# ---------------------------------------------------------------------------
# UBI validation
# ---------------------------------------------------------------------------
class UbiInfo(dict):
    pass


def validate_ubi(data, source, page_size=DEFAULT_PAGE_SIZE,
                 peb_size=DEFAULT_PEB_SIZE,
                 subpage_size=DEFAULT_SUBPAGE_SIZE,
                 vid_hdr_offset=DEFAULT_VID_HDR_OFFSET,
                 max_size=ROOTFS_SLOT_LIMIT,
                 verify_crcs=True,
                 rootfs_data_bytes=None):
    """Re-parse a ubinize image and require the two-volume rootfs shape.

    Required shape (standard OpenWrt squashfs+UBI):
      vol 0  "squashfs"     static, PEBs present in the image, data CRCs valid
      vol 1  "rootfs_data"  dynamic, AUTORESIZE flag, reserved_pebs >= 1,
                            ZERO PEBs in the image (the kernel formats the
                            empty volume and autoresize sizes it at attach)
    rootfs_data_bytes, when given, is the cfg's vol_size; it must match the
    volume table's reserved_pebs (ceil(vol_size / LEB size)) - this ties the
    checked-in cfg to the bytes ubinize produced. The two reserved regions plus
    the two layout PEBs must fit the rootfs slot (budget check).
    """
    if peb_size <= 0 or page_size <= 0 or subpage_size <= 0:
        raise ImageError("%s: invalid geometry (peb %d, page %d, subpage %d)"
                         % (source, peb_size, page_size, subpage_size))
    if peb_size % page_size:
        raise ImageError("%s: peb size %d is not a multiple of page size %d"
                         % (source, peb_size, page_size))
    if subpage_size > page_size:
        raise ImageError("%s: subpage size %d > page size %d"
                         % (source, subpage_size, page_size))
    if len(data) == 0:
        raise ImageError("%s: empty UBI image" % source)
    if len(data) > max_size:
        raise ImageError("%s: UBI image is %d B, over the %d B rootfs slot"
                         % (source, len(data), max_size))
    if len(data) % peb_size:
        raise ImageError("%s: UBI image size %d B is not a whole number of "
                         "%d B PEBs" % (source, len(data), peb_size))
    pebs = len(data) // peb_size
    if pebs < 3:
        raise ImageError("%s: only %d PEBs; a UBI image with layouts + one "
                         "data LEB needs at least 3" % (source, pebs))

    peb_info = []
    image_seq = None
    data_offset_seen = None
    layout = {}
    volumes = {}
    for index in range(pebs):
        base = index * peb_size
        peb = data[base:base + peb_size]
        if peb[:4] != UBI_EC_MAGIC:
            raise ImageError("%s: PEB %d has no UBI EC magic (%r)"
                             % (source, index, peb[:4]))
        if _be32(peb, 16) != vid_hdr_offset:
            raise ImageError(
                "%s: PEB %d vid_hdr_offset is %d, expected %d (the measured "
                "geometry)" % (source, index, _be32(peb, 16), vid_hdr_offset))
        data_offset = _be32(peb, 20)
        if data_offset < vid_hdr_offset + UBI_VID_HDR_LEN or data_offset >= peb_size:
            raise ImageError("%s: PEB %d data_offset %d is out of range"
                             % (source, index, data_offset))
        if data_offset_seen is None:
            data_offset_seen = data_offset
        elif data_offset != data_offset_seen:
            raise ImageError("%s: PEB %d data_offset %d differs from %d; the "
                             "LEB size must be uniform across the UBI image"
                             % (source, index, data_offset, data_offset_seen))
        seq = _be32(peb, 24)
        if image_seq is None:
            image_seq = seq
        elif seq != image_seq:
            raise ImageError("%s: PEB %d image_seq %d differs from %d; UBI "
                             "rejects mixed sequence numbers"
                             % (source, index, seq, image_seq))
        ec_hdr = peb[:UBI_EC_HDR_LEN]
        if verify_crcs and _be32(ec_hdr, 60) != _ubi_crc(ec_hdr[:60]):
            raise ImageError("%s: PEB %d EC header CRC mismatch"
                             % (source, index))

        vid = peb[vid_hdr_offset:vid_hdr_offset + UBI_VID_HDR_LEN]
        if vid[:4] != UBI_VID_MAGIC:
            raise ImageError("%s: PEB %d has no UBI VID magic (%r)"
                             % (source, index, vid[:4]))
        if verify_crcs and _be32(vid, 60) != _ubi_crc(vid[:60]):
            raise ImageError("%s: PEB %d VID header CRC mismatch"
                             % (source, index))
        vol_type = vid[5]
        if vol_type not in (UBI_VID_DYNAMIC, UBI_VID_STATIC):
            raise ImageError("%s: PEB %d VID vol_type %d is not a UBI type"
                             % (source, index, vol_type))
        vol_id = _be32(vid, 8)
        lnum = _be32(vid, 12)
        data_size = _be32(vid, 20)
        data_crc = _be32(vid, 32)
        payload_off = base + data_offset
        payload_len = peb_size - data_offset
        if data_size > payload_len:
            raise ImageError("%s: PEB %d data_size %d > LEB size %d"
                             % (source, index, data_size, payload_len))
        # Static-volume LEBs always carry a data CRC; dynamic volumes only do
        # so on a wear-leveling copy (copy_flag). The layout volume is dynamic
        # with data_size 0 and its data_crc field is zero, so it is skipped -
        # exactly what the kernel's attach path does.
        copy_flag = vid[6]
        if verify_crcs and (vol_type == UBI_VID_STATIC or copy_flag):
            payload = data[payload_off:payload_off + data_size]
            if _ubi_crc(payload) != data_crc:
                raise ImageError("%s: PEB %d data CRC mismatch (stored 0x%08x, "
                                 "computed 0x%08x)"
                                 % (source, index, data_crc,
                                    _ubi_crc(payload)))
        item = {
            "peb": index, "vid_hdr_offset": vid_hdr_offset,
            "data_offset": data_offset, "leb_size": payload_len,
            "vol_type": vol_type, "vol_id": vol_id, "lnum": lnum,
            "data_size": data_size, "used_ebs": _be32(vid, 24),
        }
        peb_info.append(item)
        if vol_id == UBI_LAYOUT_VOLUME_ID:
            if vol_type != UBI_VID_DYNAMIC:
                raise ImageError("%s: PEB %d is the layout volume but VID "
                                 "type is %d (want dynamic)"
                                 % (source, index, vol_type))
            if lnum > 1:
                raise ImageError("%s: PEB %d layout volume lnum %d is not 0/1"
                                 % (source, index, lnum))
            layout[lnum] = item
        else:
            volumes.setdefault(vol_id, []).append(item)
    if set(layout) != {0, 1}:
        raise ImageError("%s: layout volume copies missing (found LNUMs %s)"
                         % (source, sorted(layout)))
    if set(volumes) != {0}:
        raise ImageError(
            "%s: data volumes found for vol_id(s) %s; only the squashfs "
            "volume (vol_id 0) may have PEBs in the image. The rootfs_data "
            "volume is intentionally EMPTY and gets its PEBs from the "
            "kernel's autoresize at attach (a PEB with vol_id 1 means the "
            "volume was pre-filled: a different, unverified design)"
            % (source, ", ".join("0x%x" % v for v in sorted(volumes))))
    data_pebs = volumes[0]
    lnums = sorted(item["lnum"] for item in data_pebs)
    if lnums != list(range(len(lnums))):
        raise ImageError("%s: volume LEB numbers are not contiguous from 0: %s"
                         % (source, lnums))
    if len(set(lnums)) != len(lnums):
        raise ImageError("%s: volume LEB numbers repeat: %s"
                         % (source, lnums))

    # volume table: the layout volume copy at LNUM 0 carries it
    lay = layout[0]
    vtbl_base = lay["peb"] * peb_size + lay["data_offset"]
    max_records = min(UBI_MAX_VOLUMES,
                      (peb_size - lay["data_offset"]) // UBI_VTBL_RECORD_LEN)
    records = {}
    for vol in range(max_records):
        off = vtbl_base + vol * UBI_VTBL_RECORD_LEN
        rec = data[off:off + UBI_VTBL_RECORD_LEN]
        if len(rec) != UBI_VTBL_RECORD_LEN:
            raise ImageError("%s: truncated volume table record %d"
                             % (source, vol))
        if verify_crcs and _be32(rec, 168) != _ubi_crc(rec[:168]):
            raise ImageError("%s: volume table record %d CRC mismatch"
                             % (source, vol))
        name_len = _be16(rec, 14)
        if name_len == 0:
            continue
        if name_len > 127:
            raise ImageError("%s: volume table record %d name_len %d invalid"
                             % (source, vol, name_len))
        name = rec[16:16 + name_len].decode("ascii", "replace")
        records[vol] = {
            "reserved_pebs": _be32(rec, 0),
            "vol_type": rec[12],
            "flags": rec[144],
            "name": name,
        }
    if set(records) != {0, 1}:
        raise ImageError("%s: volume table must define exactly volumes 0 "
                         "(squashfs) and 1 (rootfs_data); found %s"
                         % (source, ", ".join(str(v)
                                              for v in sorted(records))))
    rec = records[0]
    if rec["name"] != UBI_VOLUME_NAME:
        raise ImageError("%s: volume 0 name is %r, want %r (the loader/mount "
                         "path expects that name)"
                         % (source, rec["name"], UBI_VOLUME_NAME))
    if rec["vol_type"] != UBI_VTBL_STATIC:
        raise ImageError("%s: volume 0 vtbl type is %d, want static (%d)"
                         % (source, rec["vol_type"], UBI_VTBL_STATIC))
    if rec["flags"] & (UBI_VTBL_AUTORESIZE_FLG | UBI_VTBL_SKIP_CRC_CHECK_FLG):
        raise ImageError("%s: static squashfs volume 0 carries volume-table "
                         "flags 0x%02x; a static rootfs volume must have none"
                         % (source, rec["flags"]))
    rd_rec = records[1]
    if rd_rec["name"] != ROOTFS_DATA_VOLUME_NAME:
        raise ImageError("%s: volume 1 name is %r, want %r (fstools "
                         "mount_root looks the overlay volume up by this name)"
                         % (source, rd_rec["name"], ROOTFS_DATA_VOLUME_NAME))
    if rd_rec["vol_type"] != UBI_VTBL_DYNAMIC:
        raise ImageError("%s: volume 1 vtbl type is %d, want dynamic (%d)"
                         % (source, rd_rec["vol_type"], UBI_VTBL_DYNAMIC))
    if not rd_rec["flags"] & UBI_VTBL_AUTORESIZE_FLG:
        raise ImageError("%s: volume 1 (rootfs_data) lacks the autoresize "
                         "flag; the kernel would not grow the overlay to the "
                         "free PEBs of the slot" % source)
    if rd_rec["flags"] & UBI_VTBL_SKIP_CRC_CHECK_FLG:
        raise ImageError("%s: volume 1 (rootfs_data) carries skip-check; that "
                         "is only valid for static volumes" % source)
    if rd_rec["reserved_pebs"] < 1:
        raise ImageError("%s: volume 1 (rootfs_data) reserved_pebs is 0"
                         % source)
    used_ebs = max(item["used_ebs"] for item in data_pebs)
    if used_ebs == 0:
        raise ImageError("%s: static volume has used_ebs 0" % source)
    if len(data_pebs) != used_ebs:
        raise ImageError("%s: volume 0 occupies %d PEBs but used_ebs is %d"
                         % (source, len(data_pebs), used_ebs))
    if rec["reserved_pebs"] != used_ebs:
        raise ImageError("%s: volume table reserved_pebs %d != used_ebs %d"
                         % (source, rec["reserved_pebs"], used_ebs))
    for item in data_pebs:
        if item["vol_type"] != UBI_VID_STATIC:
            raise ImageError("%s: volume 0 PEB %d VID type %d is not static"
                             % (source, item["peb"], item["vol_type"]))
        if item["used_ebs"] != used_ebs:
            raise ImageError("%s: volume 0 PEB %d used_ebs %d != %d"
                             % (source, item["peb"], item["used_ebs"],
                                used_ebs))
    leb_size = data_pebs[0]["leb_size"]
    if lnums and data_pebs[0]["leb_size"] != leb_size:
        raise ImageError("%s: inconsistent LEB size across volume 0 PEBs"
                         % source)
    if rootfs_data_bytes is not None:
        wanted_pebs = (rootfs_data_bytes + leb_size - 1) // leb_size
        if rd_rec["reserved_pebs"] != wanted_pebs:
            raise ImageError(
                "%s: rootfs_data reserved_pebs is %d, but the cfg vol_size %d "
                "B needs %d PEBs at LEB size %d (ubinize computes "
                "ceil(vol_size/LEB))"
                % (source, rd_rec["reserved_pebs"], rootfs_data_bytes,
                   wanted_pebs, leb_size))
    expected_pebs = 2 + used_ebs
    if pebs != expected_pebs:
        raise ImageError(
            "%s: image has %d PEBs, expected 2 layout + %d squashfs volume "
            "PEBs = %d (the rootfs_data volume has no image PEBs: it must be "
            "empty in the image and is sized by the kernel autoresize)"
            % (source, pebs, used_ebs, expected_pebs))
    slot_pebs = max_size // peb_size
    reserved_pebs_total = 2 + used_ebs + rd_rec["reserved_pebs"]
    if reserved_pebs_total > slot_pebs:
        raise ImageError(
            "%s: the volume table reserves %d PEBs (2 layout + %d squashfs + "
            "%d rootfs_data), but the %d B rootfs slot holds only %d PEBs of "
            "%d B" % (source, reserved_pebs_total, used_ebs,
                      rd_rec["reserved_pebs"], max_size, slot_pebs, peb_size))
    total = {
        "source": source,
        "size": len(data),
        "sha256": _sha256(data),
        "peb_size": peb_size,
        "page_size": page_size,
        "subpage_size": subpage_size,
        "vid_hdr_offset": vid_hdr_offset,
        "data_offset": data_pebs[0]["data_offset"] if data_pebs else None,
        "pebs": pebs,
        "layout_pebs": 2,
        "volume_pebs": len(data_pebs),
        "leb_size": leb_size,
        "used_ebs": used_ebs,
        "volume_name": rec["name"],
        "volume_type": "static",
        "rootfs_data_volume_name": rd_rec["name"],
        "rootfs_data_vol_id": 1,
        "rootfs_data_reserved_pebs": rd_rec["reserved_pebs"],
        "rootfs_data_flags": rd_rec["flags"],
        "rootfs_data_autoresize": bool(
            rd_rec["flags"] & UBI_VTBL_AUTORESIZE_FLG),
        "rootfs_data_image_pebs": 0,
        "reserved_pebs_total": reserved_pebs_total,
        "slot_pebs": slot_pebs,
        "image_seq": image_seq,
        "max_size": max_size,
        "headroom": max_size - len(data),
        "data_crc_verified": bool(verify_crcs),
        "header_crc_verified": bool(verify_crcs),
    }
    return total


# ---------------------------------------------------------------------------
# fip-manifest cross-check
# ---------------------------------------------------------------------------
def parse_fip_manifest(text, source):
    fields = {}
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if ":" not in line:
            raise ImageError("%s:%d: not a key: value line: %r"
                             % (source, lineno, raw))
        key, value = line.split(":", 1)
        fields[key.strip()] = value.strip()
    return fields


def check_kernel_against_manifest(slice_data, slice_label, manifest_text,
                                  fip, max_size=KERNEL_SLOT_LIMIT):
    try:
        info = fip.validate_slice_bytes(slice_data, slice_label, max_size)
    except fip.SliceError as exc:
        raise ImageError("kernel slice rejected by fip-slice.py: %s" % exc)
    except fip.Refusal as exc:
        raise Refusal("kernel slice refused by fip-slice.py: %s" % exc)
    fields = parse_fip_manifest(manifest_text, "fip-manifest.txt")
    required = ("manifest-version", "slice-size", "slice-sha256", "slot-limit",
                "uimage-ih-size", "uimage-ih-load", "uimage-ih-ep",
                "trailer-size", "trailer-sha256", "base-dtb-totalsize",
                "descriptor-length", "descriptor-container-total",
                "gate-offset", "gate-magic")
    missing = [key for key in required if key not in fields]
    if missing:
        raise ImageError("fip-manifest.txt is missing field(s): %s"
                         % ", ".join(missing))
    problems = []
    if fields["manifest-version"] != "2":
        problems.append("manifest-version %r != '2'"
                        % fields["manifest-version"])
    try:
        if int(fields["slice-size"]) != info["size"]:
            problems.append("slice-size %s != actual %d"
                            % (fields["slice-size"], info["size"]))
        if int(fields["slot-limit"]) != max_size:
            problems.append("slot-limit %s != %d"
                            % (fields["slot-limit"], max_size))
        if int(fields["uimage-ih-size"]) != info["uimage"]["ih_size"]:
            problems.append("uimage-ih-size %s != %d"
                            % (fields["uimage-ih-size"],
                               info["uimage"]["ih_size"]))
        if int(fields["uimage-ih-load"], 0) != info["uimage"]["ih_load"]:
            problems.append("uimage-ih-load %s != 0x%08x"
                            % (fields["uimage-ih-load"],
                               info["uimage"]["ih_load"]))
        if int(fields["uimage-ih-ep"], 0) != info["uimage"]["ih_ep"]:
            problems.append("uimage-ih-ep %s != 0x%08x"
                            % (fields["uimage-ih-ep"],
                               info["uimage"]["ih_ep"]))
        if int(fields["trailer-size"]) != info["trailer"]["size"]:
            problems.append("trailer-size %s != %d"
                            % (fields["trailer-size"],
                               info["trailer"]["size"]))
        if int(fields["base-dtb-totalsize"]) != info["trailer"]["base_total"]:
            problems.append("base-dtb-totalsize %s != %d"
                            % (fields["base-dtb-totalsize"],
                               info["trailer"]["base_total"]))
        if int(fields["descriptor-length"]) != info["trailer"]["descriptor_len"]:
            problems.append("descriptor-length mismatch")
        if (int(fields["descriptor-container-total"])
                != info["trailer"]["container_total"]):
            problems.append("descriptor-container-total mismatch")
        if int(fields["gate-offset"], 0) != info["gate_offset"]:
            problems.append("gate-offset %s != 0x%x"
                            % (fields["gate-offset"], info["gate_offset"]))
        if int(fields["gate-magic"], 0) != info["gate_value"]:
            problems.append("gate-magic mismatch")
    except ValueError as exc:
        raise ImageError("fip-manifest.txt has a non-numeric field: %s" % exc)
    if fields["slice-sha256"] != _sha256(slice_data):
        problems.append("slice-sha256 does not match the kernel bytes")
    trailer = slice_data[info["uimage"]["payload_end"]:]
    if fields["trailer-sha256"] != _sha256(trailer):
        problems.append("trailer-sha256 does not match the trailer bytes")
    if problems:
        raise ImageError("fip-manifest.txt does not match the kernel slice: %s"
                         % "; ".join(problems))
    return info, fields


# ---------------------------------------------------------------------------
# INSTALL-STATUS.txt
# ---------------------------------------------------------------------------
def install_status_text(board, target):
    return (
        "hisilicon/luofu image container - STATUS: BOOT-VERIFIED RECIPE / "
        "NOT FLASHABLE-UNTIL-INSTALLED\n"
        "================================================================="
        "===================\n"
        "\n"
        "board:  %s\n"
        "target: %s\n"
        "built:  by luofu-image.py %s (host-only)\n"
        "\n"
        "BOOT EVIDENCE (2026-11-19 attended bring-up, this board)\n"
        "  An earlier build of this image recipe booted to userspace on a\n"
        "  WR3000 v2: U-Boot -> our kernel 6.18.60 -> rootfs UBI slot ->\n"
        "  ubiblock0_0 (squashfs) -> /sbin/init -> preinit -> procd ->\n"
        "  console. That build carried the same image chain and the same\n"
        "  boot fixes as this one (CONFIG_CMDLINE_FORCE, 4 KiB squashfs\n"
        "  padding before ubinize, CONFIG_XZ_DEC_ARM), but:\n"
        "    - it was built from kernel patches 001-017 only (UP, 1 CPU);\n"
        "      the SMP patch 018 and patches 019-021 were not part of that\n"
        "      build and have never run on hardware;\n"
        "    - it had no rootfs_data volume: mount_root fell back to a tmpfs\n"
        "      overlay, i.e. the persistent overlay this container carries\n"
        "      has NOT been observed on hardware;\n"
        "    - its board.d/02_network sourced a missing\n"
        "      /lib/functions/board.sh (the board_detect warning of that\n"
        "      boot); this container ships the corrected board.d.\n"
        "  THIS EXACT REBUILD (patches 001-021, rootfs_data overlay volume,\n"
        "  corrected board.d) has NOT been flashed and NOT been booted.\n"
        "\n"
        "Structural validation of THIS file (host-side, from its own bytes):\n"
        "  kernel slice: legacy uImage + generated FIP trailer + gate magic;\n"
        "  rootfs: static UBI volume %r plus the empty dynamic volume\n"
        "  %r with vol_flags=autoresize. fstools mount_root finds the\n"
        "  overlay volume by name and the kernel's first read-write UBIFS\n"
        "  mount formats it (fs/ubifs/sb.c create_default_filesystem); the\n"
        "  kernel autoresize grows it to the free PEBs of the slot. That\n"
        "  path is read from the sources, not measured on this board.\n"
        "\n"
        "NOT FLASHABLE-UNTIL-INSTALLED: do not flash this file as a stock\n"
        "sysupgrade and do not add a factory/install stanza for it. The A/B\n"
        "boot-selector handover, the SMP second-CPU bring-up and the\n"
        "rootfs_data overlay are all host-validated but not yet observed on\n"
        "hardware in this combination (TARGET-DESIGN.md 3.4, 5.2, 5.3, 9;\n"
        "TARGET-SKELETON README). Use the attended first-install procedure.\n"
        % (board, target, VERSION, UBI_VOLUME_NAME, ROOTFS_DATA_VOLUME_NAME))


# ---------------------------------------------------------------------------
# deterministic tar container
# ---------------------------------------------------------------------------
def _tarinfo(name, size, mode, epoch, typ=tarfile.REGTYPE):
    info = tarfile.TarInfo(name)
    info.size = size
    info.mode = mode
    info.uid = 0
    info.gid = 0
    info.uname = ""
    info.gname = ""
    info.mtime = epoch
    info.type = typ
    return info


def build_sysupgrade_tar(board_dir, members, epoch):
    """members: ordered list of (name, bytes, mode). Deterministic ustar tar."""
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w", format=tarfile.USTAR_FORMAT,
                      encoding="utf-8") as tar:
        tar.addfile(_tarinfo(board_dir + "/", 0, 0o755, epoch,
                             tarfile.DIRTYPE))
        for name, data, mode in members:
            info = _tarinfo(board_dir + "/" + name, len(data), mode, epoch)
            tar.addfile(info, io.BytesIO(data))
    return buf.getvalue()


# ---------------------------------------------------------------------------
# subcommands
# ---------------------------------------------------------------------------
def cmd_rootfs_ubi(args):
    rootfs, rootfs_path = _read_file(args.rootfs, "squashfs rootfs")
    _refuse_vendor_path(rootfs_path)
    if len(rootfs) < 96 or rootfs[:4] not in SQUASHFS_MAGICS:
        raise ImageError("%s: not a squashfs image (magic %r); the target "
                         "rootfs filesystem must be squashfs"
                         % (rootfs_path, rootfs[:4]))
    if len(rootfs) > ROOTFS_SLOT_LIMIT:
        raise ImageError("%s: squashfs is %d B, over the %d B rootfs slot"
                         % (rootfs_path, len(rootfs), ROOTFS_SLOT_LIMIT))
    pad_align = getattr(args, "pad_align", 1) or 1
    if pad_align < 1:
        raise ImageError("--pad-align must be >= 1")
    pad = (-len(rootfs)) % pad_align
    if pad:
        # ubiblock rounds a static volume down to 512 B, so an unpadded
        # squashfs loses its last <512 bytes and cannot mount (measured,
        # first attended boot 2026-11-19). Pad the squashfs payload itself.
        rootfs = rootfs + b"\x00" * pad
    cfg_text, cfg_path = _read_file(args.ubinize_cfg, "ubinize cfg")
    try:
        cfg_text = cfg_text.decode("utf-8")
    except UnicodeDecodeError:
        raise ImageError("%s: ubinize cfg is not UTF-8 text" % cfg_path)
    if "@ROOTFS@" not in cfg_text:
        raise Refusal("%s: ubinize cfg has no @ROOTFS@ placeholder; the recipe "
                      "must not hardcode a build path" % cfg_path)
    resolved = cfg_text.replace("@ROOTFS@", str(rootfs_path.resolve()))
    cfg = parse_ubinize_cfg(resolved, str(cfg_path))
    ubinize = resolve_executable(args.ubinize, "ubinize (build the OpenWrt "
                                 "host tools first)")
    image_seq = args.image_seq
    if image_seq is None:
        if args.source_date_epoch not in (None, ""):
            try:
                image_seq = int(args.source_date_epoch) & 0xFFFFFFFF
            except ValueError:
                raise ImageError("--source-date-epoch is not an integer: %r"
                                 % args.source_date_epoch)
        else:
            # Deterministic fallback: derive the sequence from the rootfs
            # content instead of ubinize's random default.
            image_seq = int(_sha256(rootfs)[:8], 16)
    with tempfile.TemporaryDirectory(prefix="luofu-ubi-") as tmpdir:
        if pad:
            padded = Path(tmpdir) / "rootfs-padded.squashfs"
            padded.write_bytes(rootfs)
            resolved = resolved.replace(str(rootfs_path.resolve()), str(padded))
        cfg_file = Path(tmpdir) / "luofu-ubinize.cfg"
        cfg_file.write_text(resolved + "\n", encoding="utf-8")
        out = Path(tmpdir) / "rootfs.ubi"
        cmd = [str(ubinize), "-o", str(out),
               "-m", str(args.page_size), "-p", str(args.peb_size),
               "-s", str(args.subpage_size), "-O", str(args.vid_hdr_offset),
               "-Q", str(image_seq), str(cfg_file)]
        proc = subprocess_run(cmd, args, "ubinize")
        if proc.returncode != 0:
            raise ImageError("ubinize failed (rc %d): %s"
                             % (proc.returncode,
                                _tail(proc.stderr) or _tail(proc.stdout)))
        data = out.read_bytes()
    info = validate_ubi(
        data, "ubinize output",
        page_size=args.page_size, peb_size=args.peb_size,
        subpage_size=args.subpage_size,
        vid_hdr_offset=args.vid_hdr_offset,
        max_size=args.rootfs_slot_size, verify_crcs=not args.no_crc_check,
        rootfs_data_bytes=cfg["rootfs_data"]["vol_size"])
    out_path = Path(args.output)
    write_output(out_path, data, args.force)
    print("rootfs-ubi: PASS (squashfs static volume + empty rootfs_data "
          "dynamic volume, re-parsed from the bytes)")
    print("  rootfs:          %s (%d B, squashfs)" % (rootfs_path, len(rootfs)))
    print("  geometry:        p=%d m=%d s=%d O=%d image_seq=%d"
          % (info["peb_size"], info["page_size"], info["subpage_size"],
             info["vid_hdr_offset"], info["image_seq"]))
    print("  ubi:             %d B, %d PEBs = 2 layout + %d squashfs volume; "
          "LEB %d B"
          % (info["size"], info["pebs"], info["volume_pebs"],
             info["leb_size"]))
    print("  volume 0:        %r vol_id 0 type=%s used_ebs=%d (static)"
          % (info["volume_name"], info["volume_type"], info["used_ebs"]))
    print("  volume 1:        %r vol_id 1 type=dynamic autoresize=%s "
          "reserved_pebs=%d image_pebs=%d (empty; UBIFS created by the "
          "kernel on the first read-write mount)"
          % (info["rootfs_data_volume_name"],
             "yes" if info["rootfs_data_autoresize"] else "no",
             info["rootfs_data_reserved_pebs"],
             info["rootfs_data_image_pebs"]))
    print("  crc:             %d data LEB CRC(s) and all UBI header/volume-"
          "table CRCs verified" % info["volume_pebs"])
    print("  slot:            image %d B <= %d B (headroom %d B)"
          % (info["size"], info["max_size"], info["headroom"]))
    print("  reserve budget:  %d PEBs reserved = 2 layout + %d squashfs + %d "
          "rootfs_data, slot holds %d PEBs"
          % (info["reserved_pebs_total"], info["used_ebs"],
             info["rootfs_data_reserved_pebs"], info["slot_pebs"]))
    print("  ubi-sha256:      %s" % info["sha256"])
    print("  wrote:           %s" % out_path)
    return EXIT_OK


def cmd_sysupgrade(args):
    slice_data, slice_path = _read_file(args.kernel_slice, "kernel slice")
    _refuse_vendor_path(slice_path)
    manifest_text, manifest_path = _read_file(args.manifest, "fip-manifest")
    _refuse_vendor_path(manifest_path)
    rootfs_data, rootfs_path = _read_file(args.rootfs_ubi, "rootfs UBI")
    _refuse_vendor_path(rootfs_path)
    fip = load_fip_slice(args.fip_tool)
    slice_info, manifest_fields = check_kernel_against_manifest(
        slice_data, str(slice_path), manifest_text.decode("utf-8", "replace"),
        fip, max_size=args.kernel_slot_size)
    ubi_info = validate_ubi(
        rootfs_data, str(rootfs_path),
        page_size=args.page_size, peb_size=args.peb_size,
        subpage_size=args.subpage_size,
        vid_hdr_offset=args.vid_hdr_offset,
        max_size=args.rootfs_slot_size, verify_crcs=not args.no_crc_check)
    try:
        epoch = int(args.source_date_epoch) if args.source_date_epoch else 0
    except ValueError:
        raise ImageError("--source-date-epoch is not an integer: %r"
                         % args.source_date_epoch)
    version = args.version or "unknown"
    control = ("BOARD=%s\nTARGET=%s\nVERSION=%s\n"
               % (args.board, args.target, version)).encode("ascii", "replace")
    status = install_status_text(args.board, args.target).encode("utf-8")
    board_dir = "sysupgrade-" + args.board
    members = [
        ("CONTROL", control, 0o644),
        (INSTALL_STATUS_NAME, status, 0o644),
        (FIP_MANIFEST_NAME, manifest_text, 0o644),
        ("kernel", slice_data, 0o644),
        ("root", rootfs_data, 0o644),
    ]
    container = build_sysupgrade_tar(board_dir, members, epoch)
    out_path = Path(args.output)
    write_output(out_path, container, args.force)
    print("sysupgrade container: PASS (structurally validated; NOT flashable)")
    print("  board dir:       %s/" % board_dir)
    print("  members:         CONTROL, %s, %s, kernel, root"
          % (INSTALL_STATUS_NAME, FIP_MANIFEST_NAME))
    print("  kernel slice:    %d B (sha256 %s), gate 0x%08x at 0x%x"
          % (slice_info["size"], _sha256(slice_data), slice_info["gate_value"],
             slice_info["gate_offset"]))
    print("  rootfs UBI:      %d B (sha256 %s), volumes %r (static) + %r "
          "(dynamic, empty, autoresize)"
          % (ubi_info["size"], ubi_info["sha256"], ubi_info["volume_name"],
             ubi_info["rootfs_data_volume_name"]))
    print("  container:       %d B, sha256 %s" % (len(container),
                                                  _sha256(container)))
    print("  wrote:           %s" % out_path)
    print("  install status:  INSTALL-STATUS.txt carries the "
          "\"NOT FLASHABLE-UNTIL-INSTALLED\" marker (boot-verified recipe, "
          "this exact rebuild not flashed/booted)")
    return EXIT_OK


def cmd_check_container(args):
    data, path = _read_file(args.container, "sysupgrade container")
    _refuse_vendor_path(path)
    try:
        tar = tarfile.open(fileobj=io.BytesIO(data), mode="r:")
    except tarfile.TarError as exc:
        raise ImageError("%s: not a readable tar container: %s" % (path, exc))
    members_all = tar.getmembers()
    dirs = [m.name.rstrip("/") for m in members_all
            if m.isdir() and m.name.rstrip("/").startswith("sysupgrade-")]
    if len(dirs) != 1:
        raise ImageError("%s: expected exactly one sysupgrade-*/ directory, "
                         "found %r" % (path, dirs))
    board_dir = dirs[0]
    if board_dir != "sysupgrade-" + args.board:
        raise ImageError("%s: container board dir %r, want %r"
                         % (path, board_dir, "sysupgrade-" + args.board))
    wanted = ("CONTROL", INSTALL_STATUS_NAME, FIP_MANIFEST_NAME, "kernel",
              "root")
    members = {}
    for name in wanted:
        try:
            handle = tar.extractfile(board_dir + "/" + name)
        except KeyError:
            raise ImageError("%s: container is missing %s/%s"
                             % (path, board_dir, name))
        if handle is None:
            raise ImageError("%s: %s/%s is not a regular file"
                             % (path, board_dir, name))
        members[name] = handle.read()
    control = members["CONTROL"].decode("ascii", "replace")
    fields = {}
    for line in control.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            fields[key] = value
    if fields.get("BOARD") != args.board:
        raise ImageError("%s: CONTROL BOARD %r != %r"
                         % (path, fields.get("BOARD"), args.board))
    if fields.get("TARGET") != args.target:
        raise ImageError("%s: CONTROL TARGET %r != %r"
                         % (path, fields.get("TARGET"), args.target))
    fip = load_fip_slice(args.fip_tool)
    slice_info, manifest_fields = check_kernel_against_manifest(
        members["kernel"], board_dir + "/kernel",
        members[FIP_MANIFEST_NAME].decode("utf-8", "replace"), fip,
        max_size=args.kernel_slot_size)
    ubi_info = validate_ubi(
        members["root"], board_dir + "/root",
        page_size=args.page_size, peb_size=args.peb_size,
        subpage_size=args.subpage_size,
        vid_hdr_offset=args.vid_hdr_offset,
        max_size=args.rootfs_slot_size, verify_crcs=not args.no_crc_check)
    status = members[INSTALL_STATUS_NAME].decode("utf-8", "replace")
    if "NOT FLASHABLE" not in status:
        raise ImageError("%s: %s does not carry the NOT FLASHABLE-Until-"
                         "installed provenance marker"
                         % (path, INSTALL_STATUS_NAME))
    print("check-container: PASS (independent read-back of the built bytes)")
    print("  file:            %s (%d B)" % (path, len(data)))
    print("  board dir:       %s/" % board_dir)
    print("  control:         BOARD=%s TARGET=%s VERSION=%s"
          % (fields.get("BOARD"), fields.get("TARGET"),
             fields.get("VERSION")))
    print("  kernel slice:    %d B, load/ep 0x%08x/0x%08x, gate 0x%08x at 0x%x"
          % (slice_info["size"], slice_info["uimage"]["ih_load"],
             slice_info["uimage"]["ih_ep"], slice_info["gate_value"],
             slice_info["gate_offset"]))
    print("  rootfs UBI:      %d B, volume %r (%s) used_ebs %d; volume %r "
          "(dynamic, empty, autoresize) reserved_pebs %d"
          % (ubi_info["size"], ubi_info["volume_name"],
             ubi_info["volume_type"], ubi_info["used_ebs"],
             ubi_info["rootfs_data_volume_name"],
             ubi_info["rootfs_data_reserved_pebs"]))
    print("  slot budgets:    kernel <= %d, rootfs <= %d"
          % (args.kernel_slot_size, args.rootfs_slot_size))
    print("  install status:  NOT FLASHABLE-UNTIL-INSTALLED marker present")
    return EXIT_OK


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------
def _tail(data, limit=400):
    if isinstance(data, bytes):
        data = data.decode("utf-8", "replace")
    data = (data or "").strip()
    if len(data) > limit:
        data = data[-limit:]
    return data


def resolve_executable(value, what):
    """Resolve an executable argument, allowing a bare name from PATH."""
    import shutil
    path = Path(value)
    if path.is_file() and os.access(str(path), os.X_OK):
        return path
    found = shutil.which(value)
    if found:
        return Path(found)
    raise Refusal("%s not found or not executable: %r" % (what, value))


def subprocess_run(cmd, args, what):
    import subprocess
    return subprocess.run(cmd, stdout=sys.stderr, stderr=sys.stderr)


def write_output(path, data, force):
    p = Path(path)
    if p.exists() and not force:
        raise ImageError("refusing to overwrite %s (use --force)" % p)
    try:
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
    except OSError as exc:
        raise ImageError("cannot write %s: %s" % (p, exc))


def add_common_fip_args(parser):
    parser.add_argument("--fip-tool", default=str(Path(__file__).resolve().parent
                                                 / "fip-slice.py"),
                        help="path to fip-slice.py (sibling by default)")
    parser.add_argument("--kernel-slot-size", type=lambda x: int(x, 0),
                        default=KERNEL_SLOT_LIMIT,
                        help="kernel slot budget in bytes")
    parser.add_argument("--rootfs-slot-size", type=lambda x: int(x, 0),
                        default=ROOTFS_SLOT_LIMIT,
                        help="rootfs slot budget in bytes")
    parser.add_argument("--page-size", type=int, default=DEFAULT_PAGE_SIZE,
                        help="UBI min I/O size in bytes")
    parser.add_argument("--peb-size", type=int, default=DEFAULT_PEB_SIZE,
                        help="UBI PEB size in bytes")
    parser.add_argument("--subpage-size", type=int,
                        default=DEFAULT_SUBPAGE_SIZE,
                        help="UBI subpage size in bytes")
    parser.add_argument("--vid-hdr-offset", type=int,
                        default=DEFAULT_VID_HDR_OFFSET,
                        help="UBI VID header offset in bytes")
    parser.add_argument("--no-crc-check", action="store_true",
                        help="skip UBI CRC verification (test only; refuses "
                             "to be a normal build option)")
    parser.add_argument("--force", action="store_true",
                        help="overwrite an existing output")


def build_parser():
    parser = argparse.ArgumentParser(
        prog="luofu-image.py",
        description="Host-side image assembly/validation for the HiSilicon "
                    "luofu R116 OpenWrt target (SCHEMATIC, never booted).")
    parser.add_argument("--version", action="version",
                        version="luofu-image.py " + VERSION)
    sub = parser.add_subparsers(dest="command", required=True)

    p_ubi = sub.add_parser(
        "rootfs-ubi",
        help="squashfs -> static UBI volume %r + empty dynamic %r "
             "(autoresize), validated" % (UBI_VOLUME_NAME,
                                          ROOTFS_DATA_VOLUME_NAME))
    p_ubi.add_argument("--pad-align", type=lambda x: int(x, 0), default=1,
                       help="pad the squashfs payload to a multiple of N bytes "
                            "(512 for ubiblock; default 1 = no padding)")
    p_ubi.add_argument("--rootfs", required=True,
                       help="squashfs rootfs (the normal OpenWrt rootfs build)")
    p_ubi.add_argument("--ubinize-cfg", required=True,
                       help="ubinize cfg template with an @ROOTFS@ placeholder")
    p_ubi.add_argument("--output", required=True, help="output UBI image")
    p_ubi.add_argument("--ubinize", default="ubinize",
                       help="ubinize binary (default: PATH lookup)")
    p_ubi.add_argument("--image-seq", type=lambda x: int(x, 0), default=None,
                       help="UBI image sequence number (deterministic; "
                            "default: SOURCE_DATE_EPOCH or rootfs hash)")
    p_ubi.add_argument("--source-date-epoch", default=None,
                       help="SOURCE_DATE_EPOCH for the image sequence")
    add_common_fip_args(p_ubi)
    p_ubi.set_defaults(func=cmd_rootfs_ubi)

    p_sy = sub.add_parser(
        "sysupgrade",
        help="validated kernel slice + rootfs UBI + manifest -> sysupgrade "
             "container (documented artifact, NOT installable)")
    p_sy.add_argument("--kernel-slice", required=True,
                      help="validated FIP kernel slice (uImage + trailer)")
    p_sy.add_argument("--rootfs-ubi", required=True,
                      help="validated rootfs UBI image")
    p_sy.add_argument("--manifest", required=True,
                      help="fip-manifest.txt for the kernel slice")
    p_sy.add_argument("--output", required=True,
                      help="output sysupgrade container")
    p_sy.add_argument("--board", default=EXPECTED_BOARD,
                      help="device/board name recorded in CONTROL")
    p_sy.add_argument("--target", default=EXPECTED_TARGET,
                      help="target id recorded in CONTROL")
    p_sy.add_argument("--version", default=os.environ.get("REVISION", ""),
                      help="version recorded in CONTROL (default: $REVISION)")
    p_sy.add_argument("--source-date-epoch", default=None,
                      help="SOURCE_DATE_EPOCH for deterministic tar mtimes")
    add_common_fip_args(p_sy)
    p_sy.set_defaults(func=cmd_sysupgrade)

    p_chk = sub.add_parser(
        "check-container",
        help="independent read-back of a built sysupgrade container")
    p_chk.add_argument("container", help="sysupgrade container to validate")
    p_chk.add_argument("--board", default=EXPECTED_BOARD)
    p_chk.add_argument("--target", default=EXPECTED_TARGET)
    add_common_fip_args(p_chk)
    p_chk.set_defaults(func=cmd_check_container)
    return parser


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except Refusal as exc:
        print("luofu-image: REFUSED: %s" % exc, file=sys.stderr)
        return EXIT_REFUSED
    except ImageError as exc:
        print("luofu-image: FAIL: %s" % exc, file=sys.stderr)
        return EXIT_FAIL
    except OSError as exc:
        print("luofu-image: I/O error: %s" % exc, file=sys.stderr)
        return EXIT_IO


if __name__ == "__main__":
    sys.exit(main())
