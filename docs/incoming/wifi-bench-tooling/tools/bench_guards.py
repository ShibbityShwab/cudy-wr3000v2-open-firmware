#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""bench_guards.py - shared safety policy for the Hi5622V100 bench session tooling.

Lane: wifi-bench-tooling (shared task task-97).  HOST-ONLY: this module never
touches a device.  It is a library of pure policy checks used by
bench_session.py (one-command session runner) and fw_patch_builder.py
(patched-firmware builder/verifier), plus the device-free selftest.

What it enforces (the project hard rules, from BENCH-FACTS.md / LESSONS.md and
the 2026-11-24 GP8/PD sessions):

  * NEVER write CA 0x400392F0 (out[5] / HI5622_CA_NEVER_WRITE).  Any planned
    or generated write is refused, and a generated device script that would
    perform one fails the static audit.
  * NEVER write 0xCECE to CA 0x4000010C before the ROM<->image handshake page
    +0x8 reads 2.  The latch is "once seen": after a real +8==2 observation the
    policy stays satisfied even if the firmware later clobbers the page (the
    PD run showed exactly that).  Default cece_mode is 1 (never write CECE).
  * Release is 0x5A5A -> CA 0x40000108 only.
  * No H2D retries: at most one hcc_sr_announce store per session; a timeout
    closes the lane; payload frames are refused unless explicitly allowed.
  * Module / fw_path validation: allowlisted module names, ARM32 ET_REL ELF,
    GPL, pinned vermagic 6.18.60 SMP ARMv7; fw_path must be a RELATIVE kernel
    firmware name under /lib/firmware (never an absolute path - the 2026-11-24
    PD run lost a run to that).
  * custom-block pre-seed writes (devmem) are only allowed inside the two
    firmware block windows (platform block / wifi-custom block).
  * every write is recorded in a tamper-evident (sha256-chained) audit log.

The selftest for this module lives in tools/selftest.py.
"""
from __future__ import annotations

import dataclasses
import hashlib
import json
import os
import re
import struct
import time
from typing import Iterable, Optional

SCHEMA = "hi5622-bench-guards/1"

# ---------------------------------------------------------------------------
# Address map (project-wide conventions, see wifi-transport-port/build)
# ---------------------------------------------------------------------------
BAR0_CPU_BASE = 0x6B8000   # firmware CPU address -> host BAR0 offset
BAR0_IO_BASE = 0x3B8000    # device CA (CA >= 0x40000000) -> host BAR0 offset
IO_CA_MASK = 0x000FFFFF

CA_NEVER_WRITE = 0x400392F0   # out[5]; hard rule, read-only for the host
CA_CPU_RELEASE = 0x40000108   # host writes 0x5A5A only
CA_D2H_GATE = 0x4000010C      # dual-use 16-bit ROM CEED / image CECE gate
CA_H2D_PENDING = 0x40039010   # out[0]
CA_D2H_BITMAP = 0x40039014    # out[1]
CA_H2D_DOORBELL = 0x400392D4  # out[2]
CA_D2H_ACK = 0x40101438       # out[3]
CA_D2H_REARM = 0x40101414     # out[4]
CA_RC_MISC_WINDOW = 0x10161000  # never read (project hard rule)
CA_ETE_BASE = 0x4003A000
CA_ETE_INTR = 0x40039508
CA_ETE_STATUS = 0x4003A874

RELEASE_MAGIC = 0x00005A5A
CECE_MAGIC = 0x0000CECE
CEED_MAGIC = 0x0000CEED
HANDSHAKE_ACK = 2

# CPU-side firmware objects (BAR0 = CPU + 0x6B8000)
CPU_HANDSHAKE_PAGE = 0x1BF000
CPU_BOOT_STATE = 0x118B60
CPU_EXCP_MASK = 0x00040008
CPU_BOOT_SLOT = 0x17D42C
CPU_G_OBJECT = 0x10C0F4
CPU_G_ARMED = 0x10C1A8
CPU_G_PENDING = 0x10C1AC

# Physical addresses of the two custom blocks (CPU + 0x40000000)
PHYS_PLATFORM_BLOCK = 0x4086A800   # CPU 0x1B2800, 0x60 B / 24 words
PHYS_WIFI_BLOCK = 0x4086A860       # CPU 0x1B2860, 0x9A8 B / 618 words
PLATFORM_BLOCK_WORDS = 24
WIFI_BLOCK_WORDS = 618
PLATFORM_BLOCK_BYTES = PLATFORM_BLOCK_WORDS * 4
WIFI_BLOCK_BYTES = WIFI_BLOCK_WORDS * 4


def bar0_cpu(cpu_addr: int) -> int:
    """Host BAR0 offset for a firmware CPU address (CPU -> BAR0 = CPU + 0x6B8000)."""
    return BAR0_CPU_BASE + cpu_addr


def bar0_io(ca: int) -> int:
    """Host BAR0 offset for a device IO CA (CA -> BAR0 = 0x3B8000 + (CA & 0xFFFFF)).

    Raises ValueError for a CA outside the IO region, because the formula only
    has meaning for CA >= 0x40000000 (catching a wrong constant early is cheaper
    than a bench run on a wrong address).
    """
    if ca < 0x40000000:
        raise ValueError("not an IO-region CA: 0x%08X" % ca)
    return BAR0_IO_BASE + (ca & IO_CA_MASK)


# Convenience BAR0 offsets used by the generated device script / report reads.
BAR0_HANDSHAKE_PAGE = bar0_cpu(CPU_HANDSHAKE_PAGE)     # 0x877000
BAR0_BOOT_STATE = bar0_cpu(CPU_BOOT_STATE)             # 0x7D0B60
BAR0_EXCP_MASK = bar0_cpu(CPU_EXCP_MASK)               # 0x6F8008
BAR0_BOOT_SLOT = bar0_cpu(CPU_BOOT_SLOT)               # 0x7E542C
BAR0_G_OBJECT = bar0_cpu(CPU_G_OBJECT)                 # 0x7710F4
BAR0_G_ARMED = bar0_cpu(CPU_G_ARMED)                   # 0x7711A8
BAR0_G_PENDING = bar0_cpu(CPU_G_PENDING)               # 0x7711AC
BAR0_OUT0 = bar0_io(CA_H2D_PENDING)                    # 0x3F1010
BAR0_OUT1 = bar0_io(CA_D2H_BITMAP)                     # 0x3F1014
BAR0_OUT2 = bar0_io(CA_H2D_DOORBELL)                   # 0x3F12D4
BAR0_OUT3 = bar0_io(CA_D2H_ACK)                        # 0x3F9438
BAR0_OUT4 = bar0_io(CA_D2H_REARM)                      # 0x3F9414
BAR0_OUT5_READONLY = bar0_io(CA_NEVER_WRITE)           # 0x3F12F0
BAR0_ETE_BASE = bar0_io(CA_ETE_BASE)                   # 0x3F2000
BAR0_ETE_INTR = bar0_io(CA_ETE_INTR)                   # 0x3F1508
BAR0_ETE_STATUS = bar0_io(CA_ETE_STATUS)               # 0x3F2874

# Custom-block devmem write windows (inclusive physical ranges).
DEVWRITE_WINDOWS = (
    ("platform-block", PHYS_PLATFORM_BLOCK, PHYS_PLATFORM_BLOCK + PLATFORM_BLOCK_BYTES),
    ("wifi-block", PHYS_WIFI_BLOCK, PHYS_WIFI_BLOCK + WIFI_BLOCK_BYTES),
)

# Read-only BAR0 windows the bench tooling may touch (host BAR0 offsets; the two
# endpoint viewports the driver maps).  Anything outside them is an address
# mistake, not an experiment, so the protocol runner refuses it at plan time.
BAR0_READ_WINDOWS = (
    # the six endpoint inbound regions (hi5622_contract.h table [V*])
    ("rom-wram", 0x000000, 0x01C0000),
    ("tcm", 0x1C0000, 0x1D8000),
    ("pktram", 0x1D8000, 0x3B8000),
    ("device-io", 0x3B8000, 0x4D8000),
    ("acp", 0x4D8000, 0x6B8000),
    ("fw-ram", 0x6B8000, 0x8D0000),   # region 5 = CPU view 0x0..0x217fff
)


def check_bar0_read(off: int) -> int:
    """Validate a host BAR0 read offset against the two endpoint viewports.

    Returns the normalised offset; raises GuardError otherwise.  This is the
    plan-time half of the read-only evidence pack policy (the generated device
    script is additionally audited as a whole).
    """
    off = normalize_ca(off)
    if off % 4:
        raise GuardError("bar0-read-align", "unaligned BAR0 read offset 0x%08X" % off)
    for name, lo, hi in BAR0_READ_WINDOWS:
        if lo <= off < hi:
            return off
    raise GuardError(
        "bar0-read-not-allowed",
        "BAR0 read at 0x%08X is outside the mapped endpoint viewports "
        "(fw-ram 0x%X..0x%X, device-io 0x%X..0x%X)"
        % (off, BAR0_CPU_BASE, BAR0_CPU_BASE + 0x220000,
           BAR0_IO_BASE, BAR0_IO_BASE + 0x100000),
    )


def ca_of_bar0(bar0_off: int) -> Optional[int]:
    """Best-effort inverse of bar0_io/bar0_cpu for audit messages."""
    if BAR0_CPU_BASE <= bar0_off < BAR0_CPU_BASE + 0x200000:
        return bar0_off - BAR0_CPU_BASE
    return None


# ---------------------------------------------------------------------------
# Policy data
# ---------------------------------------------------------------------------
FORBIDDEN_WRITE_CAS = {
    CA_NEVER_WRITE: "out[5] / HI5622_CA_NEVER_WRITE - read-only for the host",
}
FORBIDDEN_READ_CAS = {
    CA_RC_MISC_WINDOW: "RC misc window - never read (project hard rule)",
}

ALLOWED_MODULES = {
    # module name (from .modinfo) -> human note; one PCI endpoint binder only
    "hi5622_pciet": "clean-room base transport (B3-era instrument)",
    "hi5622_pciet_hcc": "HCC service / direct-read D2H instrument",
    "hi5622_pciet_b3": "B3 ETE ring instrument",
    "hi5622_wifi": "clean-room Wi-Fi driver + HCC service + real H2D message path",
}
EXPECTED_VERMAGIC_PREFIX = "6.18.60"
EXPECTED_VERMAGIC_MARKERS = ("SMP", "ARMv7")

# Base transport attributes (pciet_main.c) and the HCC/rd layer attributes.
BASE_ATTRS = {
    "out1", "ready1", "ready2", "d2h_count", "fw_loaded", "cpu_released",
    "gate_announced", "windows", "status",
    "prepare", "upload", "release", "verify", "ringinit", "send", "poll",
    "announce",
}
HCC_ATTRS = {
    "hcc_arm", "hcc_loop", "hcc_service", "hcc_sr_announce", "hcc_cece_kick",
    "hcc_dr_republish", "hcc_clear",
    "hcc_status", "hcc_ready", "hcc_irq_probe", "hcc_events", "hcc_rings",
    "hcc_plan",
}
RD_ATTRS = {"rd_poll", "rd_advance", "rd_status", "rd_decode", "rd_catalog"}
# Real post-ready H2D message surface (hi5622_wifi, task-98/99/101/102).  The
# *status/plan* attributes are read-only; only the four message triggers are WO.
H2D_ATTRS = {"h2d_txq_sync", "h2d_txq_sync72", "h2d_ctx_create", "h2d_send",
             "h2d_status", "h2d_plan"}
H2D_WO_ATTRS = {"h2d_txq_sync", "h2d_txq_sync72", "h2d_ctx_create", "h2d_send"}
# hi5622_wifi transport surface: seed (built-in platform descriptor + PHY
# block from /lib/firmware) and gate_repair (the F4 translation fix).
WIFI_TRANSPORT_ATTRS = {"seed", "gate_repair"}
WIFI_TRANSPORT_WO_ATTRS = WIFI_TRANSPORT_ATTRS
WO_ATTRS = {
    "prepare", "upload", "release", "verify", "ringinit", "send", "poll",
    "announce",
    "hcc_arm", "hcc_loop", "hcc_service", "hcc_sr_announce", "hcc_cece_kick",
    "hcc_dr_republish", "hcc_clear", "rd_poll", "rd_advance",
} | H2D_WO_ATTRS | WIFI_TRANSPORT_WO_ATTRS
# Legacy attributes that the HCC-service build deliberately refuses (they
# encode the pre-GP8, wrong protocol).  Writing them in an HCC session is a
# policy violation even though the module would refuse it at runtime.
HCC_REFUSED_ATTRS = {"announce", "ringinit", "send", "poll"}
FORBIDDEN_ATTR_STORES = forbid_attr = {
    # attribute -> reason
    "announce": "blind 0xCECE announce (GP8 bug) - refused by the corrected protocol",
    "ringinit": "pre-GP8 2-channel plain ring init - refused by the corrected protocol",
    "send": "64-byte zero-header H2D frame - refused by the corrected protocol",
    "poll": "unordered base D2H scan - refused by the corrected protocol",
}

FW_PATH_RE = re.compile(r"^(?:[A-Za-z0-9._-]+/)*[A-Za-z0-9._-]+\.bin$")
FW_NAME_RE = re.compile(r"^FIRMWARE(?:\.[A-Za-z0-9][A-Za-z0-9._-]*)?\.bin$")
PATCHED_FW_NAME_RE = re.compile(r"^FIRMWARE\.(PD|PD2|ALL|[A-Z0-9_]{1,16})\.bin$")
MODULE_NAME_RE = re.compile(r"^[a-z0-9_]+\.ko$")

# Pinned local vendor firmware (local evidence; never committed).
VENDOR_FW_PINS = {
    "FIRMWARE.bin": {
        "len": 928920,
        "sha256": "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b",
    },
}

DEFAULT_FW = os.environ.get(
    "HI5622_FW",
    "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin",
)


# ---------------------------------------------------------------------------
# Errors
# ---------------------------------------------------------------------------
class GuardError(Exception):
    """A policy refusal.  code is a stable identifier used by tests/reports."""

    def __init__(self, code: str, message: str, **details):
        super().__init__("%s: %s" % (code, message))
        self.code = code
        self.message = message
        self.details = details

    def as_dict(self) -> dict:
        d = {"code": self.code, "message": self.message}
        if self.details:
            d["details"] = self.details
        return d


# ---------------------------------------------------------------------------
# Input normalisation / validation
# ---------------------------------------------------------------------------
def parse_int(value) -> int:
    """Parse an int from int/hex string; refuse bool and negative."""
    if isinstance(value, bool):
        raise GuardError("bad-int", "boolean is not an integer value")
    if isinstance(value, int):
        if value < 0:
            raise GuardError("bad-int", "negative value: %d" % value)
        return value
    if isinstance(value, str):
        s = value.strip()
        if not s:
            raise GuardError("bad-int", "empty value")
        try:
            v = int(s, 0)
        except ValueError:
            raise GuardError("bad-int", "not an integer: %r" % value) from None
        if v < 0:
            raise GuardError("bad-int", "negative value: %r" % value)
        return v
    raise GuardError("bad-int", "unsupported value type: %r" % (value,))


def normalize_ca(value) -> int:
    """Normalise a CA/address for policy comparisons (case/0x/underscores)."""
    s = value.strip() if isinstance(value, str) else None
    if s is not None:
        s = s.replace("_", "")
        if s.lower().startswith("0x"):
            s = s[2:]
        try:
            return int(s, 16)
        except ValueError:
            raise GuardError("bad-ca", "not a hex CA: %r" % value) from None
    return parse_int(value)


def validate_fw_path(name: str) -> str:
    """Validate a kernel firmware-loader name (relative to /lib/firmware)."""
    if not isinstance(name, str) or not name:
        raise GuardError("fw-path-empty", "empty fw_path")
    if len(name) > 200:
        raise GuardError("fw-path-long", "fw_path too long (%d)" % len(name))
    if "\x00" in name or name.startswith("/"):
        raise GuardError(
            "fw-path-absolute",
            "fw_path must be RELATIVE to /lib/firmware, got %r (the kernel "
            "firmware loader rejects absolute paths; the 2026-11-24 PD run "
            "lost a session to this)" % name,
        )
    parts = name.split("/")
    if any(p in ("", ".", "..") for p in parts):
        raise GuardError("fw-path-dotdot", "fw_path has empty/dot component: %r" % name)
    if not FW_PATH_RE.match(name):
        raise GuardError("fw-path-charset", "fw_path has unexpected characters: %r" % name)
    return name


def fw_basename(name: str) -> str:
    return os.path.basename(validate_fw_path(name))


def is_patched_fw_name(name: str) -> bool:
    return bool(PATCHED_FW_NAME_RE.match(fw_basename(name)))


def is_vendor_fw_name(name: str) -> bool:
    return fw_basename(name) in VENDOR_FW_PINS


def validate_fw_path_for_mode(fw_path: str, patched: bool) -> str:
    """A vendor-pinned name for stock/our golden runs, a PATCHED name otherwise."""
    name = validate_fw_path(fw_path)
    base = fw_basename(name)
    if patched:
        if not PATCHED_FW_NAME_RE.match(base):
            raise GuardError(
                "fw-name-not-patched",
                "patched mode needs a FIRMWARE.<PATCH>.bin name, got %r" % base,
            )
    else:
        if not is_vendor_fw_name(name):
            raise GuardError(
                "fw-name-not-vendor",
                "this mode loads the pinned vendor FIRMWARE.bin, got %r" % base,
            )
    return name


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


# ---------------------------------------------------------------------------
# Firmware file validation
# ---------------------------------------------------------------------------
@dataclasses.dataclass
class FirmwareInfo:
    path: str
    size: int
    sha256: str
    name: str
    role: str                 # "vendor-pinned" | "patched-verified" | "unverified-allowed"
    warnings: list[str] = dataclasses.field(default_factory=list)

    def as_dict(self) -> dict:
        return dataclasses.asdict(self)


def validate_vendor_fw(path: str, allow_other_source: bool = False) -> FirmwareInfo:
    """Validate the local vendor blob against the pinned length+sha256."""
    if not os.path.isfile(path):
        raise GuardError("fw-missing", "firmware blob not found: %s" % path)
    size = os.path.getsize(path)
    sha = sha256_file(path)
    base = os.path.basename(path)
    pin = VENDOR_FW_PINS.get(base)
    warnings: list[str] = []
    if pin is None:
        if not allow_other_source:
            raise GuardError(
                "fw-unpinned",
                "%r is not a pinned vendor firmware name (pins: %s)"
                % (base, ", ".join(sorted(VENDOR_FW_PINS))),
            )
        warnings.append("unpinned firmware name %r (allowed explicitly)" % base)
        return FirmwareInfo(path, size, sha, base, "unverified-allowed", warnings)
    if size != pin["len"] or sha != pin["sha256"]:
        msg = (
            "firmware is not the pinned vendor image: len=%d sha256=%s "
            "(expected len=%d sha256=%s)"
            % (size, sha, pin["len"], pin["sha256"])
        )
        if not allow_other_source:
            raise GuardError("fw-pin-mismatch", msg)
        warnings.append(msg + " (allowed explicitly)")
        return FirmwareInfo(path, size, sha, base, "unverified-allowed", warnings)
    return FirmwareInfo(path, size, sha, base, "vendor-pinned", warnings)


def load_patch_manifest(path: str) -> dict:
    if not os.path.isfile(path):
        raise GuardError("manifest-missing", "patch manifest not found: %s" % path)
    try:
        with open(path, "r", encoding="utf-8") as f:
            doc = json.load(f)
    except (OSError, ValueError) as e:
        raise GuardError("manifest-bad-json", "cannot parse %s: %s" % (path, e)) from None
    if not isinstance(doc, dict) or doc.get("schema") != "hi5622-fw-patch-manifest/1":
        raise GuardError(
            "manifest-schema",
            "%s is not a hi5622-fw-patch-manifest/1 document" % path,
        )
    src = doc.get("source") or {}
    if not isinstance(src.get("sha256"), str) or not isinstance(src.get("len"), int):
        raise GuardError("manifest-source", "manifest source {} missing sha256/len")
    builds = doc.get("builds")
    if not isinstance(builds, list) or not builds:
        raise GuardError("manifest-builds", "manifest lists no builds")
    for b in builds:
        for key in ("patch", "out_sha256", "out_len"):
            if key not in b:
                raise GuardError("manifest-build-entry", "build entry missing %r" % key)
        if not isinstance(b.get("segments"), list) or not b["segments"]:
            raise GuardError("manifest-build-entry", "build %r has no segments" % b.get("patch"))
    return doc


def validate_patched_fw(path: str, manifest_path: str) -> FirmwareInfo:
    """Validate a patched firmware file against a builder manifest entry."""
    if not os.path.isfile(path):
        raise GuardError("fw-missing", "patched firmware not found: %s" % path)
    doc = load_patch_manifest(manifest_path)
    size = os.path.getsize(path)
    sha = sha256_file(path)
    matches = [b for b in doc["builds"] if b["out_sha256"] == sha]
    if not matches:
        raise GuardError(
            "fw-not-in-manifest",
            "%s (sha256 %s) is not any build in %s - rebuild/re-verify with "
            "fw_patch_builder.py" % (path, sha, manifest_path),
        )
    build = matches[0]
    if int(build["out_len"]) != size:
        raise GuardError(
            "fw-len-mismatch",
            "manifest build %r says len=%d but file is %d B"
            % (build["patch"], build["out_len"], size),
        )
    base = os.path.basename(path)
    if not PATCHED_FW_NAME_RE.match(base):
        raise GuardError("fw-name-not-patched", "patched firmware name %r is not FIRMWARE.<PATCH>.bin" % base)
    warnings: list[str] = []
    if doc["source"].get("sha256") != VENDOR_FW_PINS["FIRMWARE.bin"]["sha256"]:
        warnings.append("manifest source is not the pinned vendor blob")
    return FirmwareInfo(path, size, sha, base, "patched-verified", warnings)


# ---------------------------------------------------------------------------
# Kernel module (.ko) validation - minimal ELF32 reader (stdlib only)
# ---------------------------------------------------------------------------
ELF_MAGIC = b"\x7fELF"
EM_ARM = 40
ET_REL = 1


@dataclasses.dataclass
class ModuleInfo:
    path: str
    size: int
    sha256: str
    name: str
    vermagic: str
    license: str
    params: dict
    dependencies: str = ""

    def as_dict(self) -> dict:
        return dataclasses.asdict(self)


def _read_cstr(buf: bytes, off: int) -> str:
    end = buf.find(b"\x00", off)
    if end < 0:
        end = len(buf)
    return buf[off:end].decode("utf-8", "replace")


def parse_module_elf(path: str) -> ModuleInfo:
    """Parse the .modinfo of a 32-bit ARM ET_REL kernel module (no deps)."""
    if not os.path.isfile(path):
        raise GuardError("module-missing", "module not found: %s" % path)
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 52 or data[:4] != ELF_MAGIC:
        raise GuardError("module-not-elf", "%s is not an ELF file" % path)
    ei_class, ei_data = data[4], data[5]
    if ei_class != 1:
        raise GuardError("module-not-elf32", "%s is not ELF32 (class=%d)" % (path, ei_class))
    if ei_data != 1:
        raise GuardError("module-not-le", "%s is not little-endian" % path)
    e_type, e_machine = struct.unpack_from("<HH", data, 16)
    if e_type != ET_REL:
        raise GuardError("module-not-rel", "%s is not a relocatable object (e_type=%d)" % (path, e_type))
    if e_machine != EM_ARM:
        raise GuardError(
            "module-not-arm",
            "%s is not an ARM object (e_machine=%d, want %d)"
            % (path, e_machine, EM_ARM),
        )
    e_shoff = struct.unpack_from("<I", data, 32)[0]
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", data, 46)
    if e_shoff == 0 or e_shnum == 0 or e_shoff + e_shentsize * e_shnum > len(data):
        raise GuardError("module-no-sections", "%s has no usable section table" % path)
    sections = []
    for i in range(e_shnum):
        base = e_shoff + i * e_shentsize
        (sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size,
         sh_link, sh_info, sh_addralign, sh_entsize) = struct.unpack_from(
            "<IIIIIIIIII", data, base)
        sections.append(dict(name_off=sh_name, type=sh_type, offset=sh_offset,
                             size=sh_size, link=sh_link))
    if e_shstrndx >= len(sections):
        raise GuardError("module-bad-strtab", "%s has a bad section-name table index" % path)
    strtab_off = sections[e_shstrndx]["offset"]
    modinfo = None
    for s in sections:
        s["name"] = _read_cstr(data, strtab_off + s["name_off"])
        if s["name"] == ".modinfo":
            modinfo = s
    if modinfo is None:
        raise GuardError("module-no-modinfo", "%s has no .modinfo section" % path)
    start, end = modinfo["offset"], modinfo["offset"] + modinfo["size"]
    if start < 0 or end > len(data):
        raise GuardError("module-bad-modinfo", "%s .modinfo is out of bounds" % path)
    blob = data[start:end]
    fields: dict[str, list[str]] = {}
    for chunk in blob.split(b"\x00"):
        if not chunk or b"=" not in chunk:
            continue
        k, _, v = chunk.partition(b"=")
        fields.setdefault(k.decode("ascii", "replace"), []).append(
            v.decode("utf-8", "replace"))
    name = (fields.get("name") or [""])[0]
    vermagic = (fields.get("vermagic") or [""])[0]
    license_ = (fields.get("license") or [""])[0]
    deps = (fields.get("depends") or [""])[0]
    params: dict[str, str] = {}
    # Kernels differ: some emit "parm=name:type", others "parmtype=name:type"
    # (the real 6.18.60 instrument .ko only carries the parmtype form).
    for key in ("parm", "parmtype"):
        for raw in fields.get(key, []):
            pname, _, ptype = raw.partition(":")
            params.setdefault(pname, ptype or "?")
    return ModuleInfo(path, len(data), sha256_bytes(data), name, vermagic,
                      license_, params, deps)


def validate_module(path: str,
                    expected_vermagic_prefix: str = EXPECTED_VERMAGIC_PREFIX,
                    require_fw_path: bool = False) -> ModuleInfo:
    """Validate an instrument .ko against the bench policy; return its info."""
    info = parse_module_elf(path)
    if not MODULE_NAME_RE.match(os.path.basename(path)):
        raise GuardError("module-filename", "module filename must be <name>.ko: %s" % path)
    if info.name not in ALLOWED_MODULES:
        raise GuardError(
            "module-name",
            "module name %r is not allowlisted (%s); one PCI endpoint binder "
            "only" % (info.name, ", ".join(sorted(ALLOWED_MODULES))),
        )
    if info.license != "GPL":
        raise GuardError("module-license", "module license is %r, expected GPL" % info.license)
    if not info.vermagic.startswith(expected_vermagic_prefix):
        raise GuardError(
            "module-vermagic",
            "module vermagic %r does not start with %r (wrong kernel tree?)"
            % (info.vermagic, expected_vermagic_prefix),
        )
    for marker in EXPECTED_VERMAGIC_MARKERS:
        if marker not in info.vermagic:
            raise GuardError(
                "module-vermagic",
                "module vermagic %r lacks %r" % (info.vermagic, marker),
            )
    if require_fw_path and "fw_path" not in info.params:
        raise GuardError(
            "module-no-fw-path",
            "module %r exposes no fw_path parameter; cannot load a patched "
            "firmware variant" % info.name,
        )
    return info


# ---------------------------------------------------------------------------
# CECE gate latch
# ---------------------------------------------------------------------------
@dataclasses.dataclass
class CeceLatch:
    """Latch for the ROM<->image handshake: page +0x8 == 2 (S1 completed).

    The policy is deliberately 'once seen': after a real +8==2 observation the
    firmware may later overwrite the handshake page (observed in the PD run),
    and re-reading +8 forever would wrongly re-open the ROM boot window.
    """
    latched: bool = False
    observations: list = dataclasses.field(default_factory=list)
    kicks: int = 0
    max_kicks: int = 3

    def observe_page(self, words: Iterable[int], source: str = "read") -> bool:
        vals = [parse_int(w) & 0xFFFFFFFF for w in words]
        ack2 = len(vals) >= 3 and vals[2] == HANDSHAKE_ACK
        rec = {"source": source, "words": ["0x%08x" % v for v in vals],
               "ack2": ack2, "at": time.time()}
        self.observations.append(rec)
        if ack2:
            self.latched = True
        return ack2

    def page_clobbered(self) -> bool:
        if not self.observations:
            return False
        return self.latched and not self.observations[-1]["ack2"]

    def allow_cece(self, cece_mode: int = 1) -> tuple[bool, str]:
        """Return (allowed, reason).  cece_mode 1 = never write CECE."""
        if cece_mode == 1:
            return False, "cece_mode=1 (never write CECE; post-GP8 default)"
        if cece_mode not in (0, 2):
            return False, "unknown cece_mode=%r" % (cece_mode,)
        if not self.latched:
            return False, (
                "handshake page +8 has never read 2 in this session: writing "
                "0xCECE here is the GP8 bug (clobbers the ROM 0xCEED gate)"
            )
        if self.kicks >= self.max_kicks:
            return False, "CECE kick limit reached (%d per module load)" % self.max_kicks
        return True, ("latched: +8==2 observed (%d observation(s))"
                      % len(self.observations))

    def note_kick(self, value: int) -> None:
        if (value & 0xFFFF) != CECE_MAGIC:
            raise GuardError("cece-value", "CECE gate write must be 0x%04X" % CECE_MAGIC)
        self.kicks += 1


# ---------------------------------------------------------------------------
# H2D one-shot gate
# ---------------------------------------------------------------------------
@dataclasses.dataclass
class H2dGate:
    """At most ONE hcc_sr_announce store per session; a timeout closes the lane."""
    state: str = "none"     # none | attempted | consumed | timeout | refused
    attempts: int = 0
    detail: str = ""
    payloads: int = 0

    def may_announce(self) -> tuple[bool, str]:
        if self.state == "attempted":
            return False, "an announce is already in flight in this session (no retries)"
        if self.state == "consumed":
            return False, "H2D lane already proven; one frame per session (no retries)"
        if self.state == "timeout":
            return False, "H2D lane closed: the previous announce was not consumed (no retries)"
        if self.state == "refused":
            return False, "H2D lane closed: the previous announce was refused"
        return True, "first (and only) announce of the session"

    def note_attempt(self) -> None:
        ok, why = self.may_announce()
        if not ok:
            raise GuardError("h2d-retry", why)
        self.state = "attempted"
        self.attempts += 1

    def note_result(self, result: str, detail: str = "") -> None:
        if result not in ("consumed", "timeout", "refused"):
            raise GuardError("h2d-result", "unknown H2D result %r" % result)
        if result == "consumed":
            self.state = "consumed"
        elif result == "timeout":
            self.state = "timeout"
        else:
            self.state = "refused"
        self.detail = detail

    def may_payload(self, allow_payload: bool = False) -> tuple[bool, str]:
        if not allow_payload:
            return False, ("payload frames are refused by default: the vendor "
                           "post-ready payloads are dynamic and not modelled; "
                           "some firmware handlers dereference payload words")
        return True, "explicitly allowed by operator flag"

    def note_payload(self) -> None:
        ok, why = self.may_payload(True)
        self.payloads += 1


# ---------------------------------------------------------------------------
# Write guard
# ---------------------------------------------------------------------------
@dataclasses.dataclass
class GuardContext:
    cece: CeceLatch = dataclasses.field(default_factory=CeceLatch)
    h2d: H2dGate = dataclasses.field(default_factory=H2dGate)
    cece_mode: int = 1
    allow_payload: bool = False
    allow_other_source: bool = False
    profile: str = "hcc"        # "hcc" | "base"

    def as_dict(self) -> dict:
        return {
            "cece_latched": self.cece.latched,
            "cece_kicks": self.cece.kicks,
            "cece_mode": self.cece_mode,
            "h2d_state": self.h2d.state,
            "h2d_attempts": self.h2d.attempts,
            "profile": self.profile,
        }


class WriteGuard:
    """Central policy object: every planned write goes through check_*()."""

    def __init__(self, ctx: GuardContext | None = None):
        self.ctx = ctx or GuardContext()

    # ---- device CA writes (devmem / raw) --------------------------------
    def check_ca_write(self, ca: int, value: int, step: str = "") -> None:
        ca = normalize_ca(ca)
        value = parse_int(value) & 0xFFFFFFFF
        if ca in FORBIDDEN_WRITE_CAS:
            raise GuardError(
                "forbidden-ca-write",
                "refusing to write CA 0x%08X (%s)%s"
                % (ca, FORBIDDEN_WRITE_CAS[ca], (" step=%s" % step) if step else ""),
                ca="0x%08X" % ca, step=step,
            )
        if ca == CA_D2H_GATE:
            allowed, why = self.ctx.cece.allow_cece(self.ctx.cece_mode)
            if not allowed:
                raise GuardError("cece-gate-refused", why, ca="0x%08X" % ca, step=step)
            if (value & 0xFFFF) != CECE_MAGIC:
                raise GuardError(
                    "cece-value",
                    "gate write value 0x%08X is not 0x%04X" % (value, CECE_MAGIC),
                )
            self.ctx.cece.note_kick(value)
            return
        if ca == CA_CPU_RELEASE:
            if value != RELEASE_MAGIC:
                raise GuardError(
                    "release-magic",
                    "CPU release is 16-bit 0x%04X ONLY, got 0x%08X"
                    % (RELEASE_MAGIC, value),
                )
            return
        # Everything else is not in the allowed write census.
        raise GuardError(
            "ca-not-allowed",
            "CA 0x%08X is not in the bench write census (only release 0x40000108, "
            "the latched CECE gate 0x4000010C, and the two custom blocks)" % ca,
            ca="0x%08X" % ca, step=step,
        )

    # ---- custom-block devmem writes -------------------------------------
    def check_devmem_write(self, phys: int, value: int, step: str = "") -> None:
        phys = normalize_ca(phys)
        parse_int(value)
        for name, lo, hi in DEVWRITE_WINDOWS:
            if lo <= phys < hi:
                if (phys - lo) % 4:
                    raise GuardError("devmem-align", "unaligned block write at 0x%08X" % phys)
                return
        if phys == CA_NEVER_WRITE:
            raise GuardError(
                "forbidden-ca-write",
                "refusing to write CA 0x%08X (out[5] hard rule)" % phys,
            )
        raise GuardError(
            "devmem-not-allowed",
            "devmem write at 0x%08X is outside the two custom-block windows "
            "(platform 0x%08X..0x%08X, wifi 0x%08X..0x%08X)"
            % (phys, PHYS_PLATFORM_BLOCK, PHYS_PLATFORM_BLOCK + PLATFORM_BLOCK_BYTES,
               PHYS_WIFI_BLOCK, PHYS_WIFI_BLOCK + WIFI_BLOCK_BYTES),
            step=step,
        )

    # ---- sysfs writes ----------------------------------------------------
    def check_attr_write(self, attr: str, payload: str = "1", step: str = "") -> None:
        attr = (attr or "").strip()
        payload = "" if payload is None else str(payload)
        if not attr or "/" in attr or attr.startswith("."):
            raise GuardError("attr-name", "bad sysfs attribute name %r" % attr)
        known = (BASE_ATTRS | HCC_ATTRS | RD_ATTRS | H2D_ATTRS |
                 WIFI_TRANSPORT_ATTRS)
        if attr not in known:
            raise GuardError("attr-unknown", "unknown sysfs attribute %r" % attr)
        if attr not in WO_ATTRS:
            raise GuardError("attr-readonly", "%r is read-only; never write it" % attr)
        if self.ctx.profile == "hcc" and attr in FORBIDDEN_ATTR_STORES:
            raise GuardError(
                "attr-refused",
                "attribute %r is refused by the corrected protocol: %s"
                % (attr, FORBIDDEN_ATTR_STORES[attr]),
            )
        if attr == "release":
            if payload.strip() != "1":
                raise GuardError("release-payload", "release store payload must be '1'")
            return
        if attr == "hcc_sr_announce":
            allowed, why = self.ctx.h2d.may_announce()
            if not allowed:
                raise GuardError("h2d-retry", why)
            p = payload.strip()
            if p == "1" or p == "":
                self.ctx.h2d.note_attempt()
                return
            # Explicit frame grammar (group/id/dst/payload=) is only for the
            # catalogue lane; the one-shot liveness frame is exactly "1".
            if "payload=" in p:
                ok, why2 = self.ctx.h2d.may_payload(self.ctx.allow_payload)
                if not ok:
                    raise GuardError("h2d-payload", why2)
            if not re.match(r"^[A-Za-z0-9=_, .]*$", p):
                raise GuardError("h2d-grammar", "unexpected H2D grammar %r" % payload)
            self.ctx.h2d.note_attempt()
            return
        if attr == "hcc_cece_kick":
            allowed, why = self.ctx.cece.allow_cece(self.ctx.cece_mode)
            if not allowed:
                raise GuardError("cece-gate-refused", why)
            if payload.strip() != "1":
                raise GuardError("cece-payload", "hcc_cece_kick payload must be '1'")
            return
        if attr == "hcc_dr_republish":
            if payload.strip() != "1":
                raise GuardError("republish-payload", "payload must be '1'")
            return
        if attr in ("prepare", "upload", "verify", "hcc_arm", "hcc_loop",
                    "hcc_service", "hcc_clear", "rd_poll", "hcc_dr_republish",
                    "seed", "gate_repair"):
            if payload.strip() != "1":
                raise GuardError("attr-payload", "%r store payload must be '1'" % attr)
            return
        if attr == "h2d_ctx_create":
            if payload.strip() not in ("1", "2", "3"):
                raise GuardError("h2d-payload",
                                 "h2d_ctx_create payload is the create-helper type "
                                 "1|2|3, got %r" % payload)
            return
        if attr in ("h2d_txq_sync", "h2d_txq_sync72"):
            if payload.strip() != "1":
                raise GuardError("h2d-payload", "%r store payload must be '1'" % attr)
            return
        if attr == "h2d_send":
            # Dedicated-frame grammar only; payload frames stay refused.
            if not re.match(r"^(txq_sync|txq_sync72|ctx_create|ctx_create_t1|"
                            r"ctx_create_t2|stub( dst=[0-9]+)?|"
                            r"edca( dst=[0-9]+)?|del_user( user=[0-9]+)"
                            r"( addr=[0-9a-fA-Fx]+)?( dst=[0-9]+)?)$", payload.strip()):
                raise GuardError("h2d-grammar",
                                 "h2d_send grammar rejected: %r" % payload)
            return
        raise GuardError("attr-unhandled", "no policy for attribute %r" % attr)

    def check_read(self, target: str) -> None:
        t = (target or "").lower()
        if "0x10161000" in t:
            raise GuardError(
                "forbidden-read",
                "refusing to read the RC misc window 0x10161000 (hard rule)",
            )

    def check_devmem_read(self, phys: int) -> None:
        if normalize_ca(phys) == CA_RC_MISC_WINDOW:
            raise GuardError("forbidden-read", "RC misc window 0x10161000 is never read")


# ---------------------------------------------------------------------------
# Static audit of a generated device script
# ---------------------------------------------------------------------------
WRITE_TO_ATTR_RE = re.compile(r">\s*\"?\$P/([a-z0-9_]+)")
ATTR_CALL_RE = re.compile(r"^\s*w_attr\s+([a-z0-9_]+)\b")
DEVWRITE_RE = re.compile(
    r"\bdevmem\s+(0[xX][0-9a-fA-F_]+)\s+32\s+(0[xX][0-9a-fA-F_]+|\$[A-Za-z_][A-Za-z0-9_]*)")
# Loop form used by the generated custom-block seed: addr=$((BASE + idx * 4))
VARADDR_RE = re.compile(
    r"^\s*([A-Za-z_][A-Za-z0-9_]*)=\$\(\(\s*(0[xX][0-9a-fA-F]+)?\s*([A-Za-z_][A-Za-z0-9_]*)?")
DEVWRITE_VAR_RE = re.compile(
    r"\bdevmem\s+\"?\$(?:\{)?([A-Za-z_][A-Za-z0-9_]*)")
CECE_CONST_RE = re.compile(r"0[xX]0*[cC][eE][cC][eE]\b")
FORBIDDEN_READ_TEXT_RE = re.compile(r"0[xX]0*10161000\b")
UART_WRITE_RE = re.compile(
    r"(>\s*/dev/tty|>>\s*/dev/tty|echo\s+[^\n]*>\s*/dev/tty|picocom|minicom|screen\s+/dev/tty)")
RMMOD_RE = re.compile(r"\brmmod\b")
RETRY_TEXT_RE = re.compile(
    r"^\s*(?:retry|again)\b|;\s*(?:retry|again)\b|"
    r"\b(?:while|for|until)\b[^\n]*hcc_sr_announce", re.IGNORECASE)


def _strip_comments(line: str) -> str:
    # only shell '#' comments that start a word (avoid # in hex? hex uses 0x)
    m = re.search(r"(^|\s)#", line)
    return line[:m.start()] if m else line


def audit_device_script(text: str, profile: str = "hcc") -> dict:
    """Statically audit a generated device shell script against the hard rules.

    Returns {"ok": bool, "violations": [ {code,line,text,message} ], "stats": {...}}.
    The audit is intentionally conservative: any direct devmem write outside
    the block windows, any CECE constant write that is not the gated
    hcc_cece_kick store, a second announce, a forbidden read, UART writes or
    rmmod is a violation.
    """
    violations: list[dict] = []
    stats = {
        "devmem_writes": 0,
        "attr_stores": [],
        "announce_stores": 0,
        "cece_attr_stores": 0,
        "forbidden_ca_mentions": 0,
        "reboot_calls": 0,
    }
    lines = text.splitlines()
    # collect shell address variables derived from a literal constant:
    #   addr=$((PLAT_BASE + idx * 4))  where PLAT_BASE=0x4086A800
    consts: dict[str, int] = {}
    addr_vars: dict[str, str] = {}
    for raw in lines:
        line = _strip_comments(raw).strip()
        m = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)=0[xX]([0-9a-fA-F]+)$", line)
        if m:
            consts[m.group(1)] = int(m.group(2), 16)
            continue
        m = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)=\$\(\(\s*([A-Za-z_][A-Za-z0-9_]*)", line)
        if m and m.group(2) in consts:
            addr_vars[m.group(1)] = m.group(2)
    last_gate_line = -100
    in_cece_block = False
    for lineno, raw in enumerate(lines, 1):
        line = _strip_comments(raw)
        stripped = line.strip()
        if not stripped:
            continue

        def viol(code: str, message: str) -> None:
            violations.append({"code": code, "line": lineno,
                               "text": stripped[:200], "message": message})

        if "cece_gate" in stripped or "cece_allowed" in stripped:
            last_gate_line = lineno
        if stripped.startswith("if ") and "cece_gate" in stripped:
            in_cece_block = True
        if in_cece_block and stripped == "fi":
            in_cece_block = False

        if FORBIDDEN_READ_TEXT_RE.search(stripped):
            viol("forbidden-read", "mentions the RC misc window 0x10161000")
        if RMMOD_RE.search(stripped):
            viol("rmmod", "rmmod is never allowed (power-cycle is the reset)")
        if UART_WRITE_RE.search(stripped):
            viol("uart-write", "UART is read-only; no host->router TX ever")
        if RETRY_TEXT_RE.search(stripped):
            viol("retry", "retry loop around a one-idea step is forbidden")

        for m in DEVWRITE_RE.finditer(stripped):
            phys = m.group(1)
            stats["devmem_writes"] += 1
            try:
                p = normalize_ca(phys)
            except GuardError:
                viol("devmem-bad", "unparseable devmem address %r" % phys)
                continue
            if p == CA_NEVER_WRITE:
                viol("forbidden-ca-write", "devmem write to CA 0x400392F0 (out[5])")
                stats["forbidden_ca_mentions"] += 1
            elif not any(lo <= p < hi for _, lo, hi in DEVWRITE_WINDOWS):
                viol("devmem-not-allowed",
                     "devmem write at 0x%08X outside the custom-block windows" % p)

        # loop form: devmem "$addr" 32 "$val" with addr=$((BASE + idx * 4))
        m = DEVWRITE_VAR_RE.search(stripped)
        if m and re.search(r"\bdevmem\s+[^\n]*\s32\s+(\"?\$|\"?0[xX])", stripped):
            var = m.group(1)
            stats["devmem_writes"] += 1
            base_name = addr_vars.get(var)
            if base_name is None or base_name not in consts:
                viol("devmem-unresolved",
                     "devmem write address %r cannot be resolved to a literal "
                     "custom-block base (audit cannot prove the window)" % var)
            else:
                p = consts[base_name]
                if p == CA_NEVER_WRITE:
                    viol("forbidden-ca-write", "loop-form devmem write at CA 0x400392F0")
                elif not any(lo <= p < hi for _, lo, hi in DEVWRITE_WINDOWS):
                    viol("devmem-not-allowed",
                         "loop-form devmem base 0x%08X outside the custom-block windows" % p)

        attr_hits = [m.group(1) for m in ATTR_CALL_RE.finditer(stripped)]
        attr_hits += [m.group(1) for m in WRITE_TO_ATTR_RE.finditer(stripped)]
        for attr in attr_hits:
            stats["attr_stores"].append(attr)
            if attr not in WO_ATTRS:
                viol("attr-readonly", "write to non-WO attribute %r" % attr)
            if profile == "hcc" and attr in FORBIDDEN_ATTR_STORES:
                viol("attr-refused", "store to attribute refused by the corrected protocol: %r" % attr)
            if attr == "hcc_sr_announce":
                stats["announce_stores"] += 1
                if stats["announce_stores"] > 1:
                    viol("h2d-retry", "more than one hcc_sr_announce store in one session")
            if attr == "hcc_cece_kick":
                stats["cece_attr_stores"] += 1
                if not (in_cece_block and lineno - last_gate_line <= 4):
                    viol("cece-ungated", "CECE kick store is not wrapped in a cece_gate block")

        if CECE_CONST_RE.search(stripped) and "devmem" in stripped:
            viol("cece-raw", "raw 0xCECE device write (only the gated hcc_cece_kick store is allowed)")

        if re.search(r"\breboot\b", stripped) and "reboot_" not in stripped:
            stats["reboot_calls"] += 1

    if "SESSION_RESULT:" not in text:
        violations.append({"code": "no-result", "line": 0, "text": "",
                           "message": "generated script has no SESSION_RESULT marker"})
    ok = not violations
    return {"ok": ok, "violations": violations, "stats": stats}


# ---------------------------------------------------------------------------
# Tamper-evident write audit log (sha256 chain)
# ---------------------------------------------------------------------------
class WriteAudit:
    """Append-only JSONL audit: one line per planned/executed write or action.

    Each record carries seq and prev_sha/sha so removal or reordering of
    records is detectable (verify_chain()).
    """

    def __init__(self, path: str | None = None):
        self.path = path
        self.records: list[dict] = []
        self._prev = "genesis"
        self._seq = 0
        self._fh = None
        if path:
            os.makedirs(os.path.dirname(os.path.abspath(path)) or ".", exist_ok=True)
            self._fh = open(path, "w", encoding="utf-8")

    def record(self, step: str, kind: str, target: str, value=None,
               ca=None, policy: str = "allow", detail: str = "") -> dict:
        if policy not in ("allow", "refuse", "read", "host", "dry-run"):
            raise ValueError("bad policy %r" % policy)
        rec = {
            "seq": self._seq,
            "ts": round(time.time(), 3),
            "step": step,
            "kind": kind,
            "target": target,
            "value": value if isinstance(value, (int, str, type(None))) else str(value),
            "ca": ca,
            "policy": policy,
            "detail": detail,
            "prev_sha": self._prev,
        }
        body = json.dumps({k: rec[k] for k in
                           ("seq", "ts", "step", "kind", "target", "value",
                            "ca", "policy", "detail")}, sort_keys=True)
        rec["sha"] = hashlib.sha256((self._prev + body).encode()).hexdigest()
        self._prev = rec["sha"]
        self._seq += 1
        self.records.append(rec)
        if self._fh:
            self._fh.write(json.dumps(rec, sort_keys=True) + "\n")
            self._fh.flush()
        return rec

    def counts(self) -> dict:
        out: dict[str, int] = {}
        for r in self.records:
            out[r["policy"]] = out.get(r["policy"], 0) + 1
        return out

    def close(self) -> None:
        if self._fh:
            self._fh.close()
            self._fh = None


def verify_audit_chain(records: Iterable[dict]) -> tuple[bool, str]:
    """Recompute the sha256 chain of an audit record list."""
    prev = "genesis"
    for i, rec in enumerate(records):
        if rec.get("seq") != i:
            return False, "record %d has seq %r" % (i, rec.get("seq"))
        if rec.get("prev_sha") != prev:
            return False, "record %d prev_sha mismatch" % i
        body = json.dumps({k: rec[k] for k in
                           ("seq", "ts", "step", "kind", "target", "value",
                            "ca", "policy", "detail")}, sort_keys=True)
        want = hashlib.sha256((prev + body).encode()).hexdigest()
        if rec.get("sha") != want:
            return False, "record %d sha mismatch" % i
        prev = want
    return True, "chain of %d record(s) verified" % len(list(records)) if isinstance(records, list) else "chain verified"


# ---------------------------------------------------------------------------
# Host-process safety checks (pure, injected process lists for testability)
# ---------------------------------------------------------------------------
def check_tftp_processes(processes: Iterable[str]) -> tuple[bool, list[str], list[str]]:
    """Check running TFTP command lines for unsafe write-enabled servers.

    Returns (ok, problems, notes).  A TFTP server is only acceptable RRQ-only
    (no --allow-wrq) and it must not serve the repository root.
    """
    problems: list[str] = []
    notes: list[str] = []
    for cmd in processes:
        if "tftp_server" not in cmd and "tftp-server" not in cmd:
            continue
        if "--allow-wrq" in cmd:
            problems.append("write-enabled TFTP server running: %s" % cmd[:160])
        else:
            notes.append("RRQ-only TFTP server present (recovery door): %s" % cmd[:120])
        for root_hint in ("cudy-wr3000v2/ ", "cudy-wr3000v2/build", "/opensource"):
            if root_hint in cmd and "--root" not in cmd:
                problems.append("unexpected TFTP serving pattern %r: %s" % (root_hint, cmd[:160]))
    return (not problems), problems, notes


def detect_uart_captures(uart_dir: str) -> list[str]:
    """Return descriptions of live read-only UART captures (pidfiles + procs)."""
    found: list[str] = []
    if not os.path.isdir(uart_dir):
        return found
    for name in sorted(os.listdir(uart_dir)):
        if not name.endswith(".pid"):
            continue
        pf = os.path.join(uart_dir, name)
        try:
            pid = int(open(pf).read().strip())
        except (OSError, ValueError):
            found.append("%s (unreadable pidfile)" % pf)
            continue
        alive = False
        try:
            os.kill(pid, 0)
            alive = True
        except (ProcessLookupError, PermissionError):
            alive = False
        # PermissionError means the pid exists but is not ours (root-owned capture)
        if alive or isinstance(os.path.exists(pf), bool) and os.path.exists(f"/proc/{pid}"):
            found.append("%s (pid %d)" % (pf, pid))
    return found


def uuidish_session_id(mode: str, extra: str = "") -> str:
    """Sortable session id: UTC timestamp + mode + short digest."""
    ts = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    raw = "%s|%s|%s" % (ts, mode, extra)
    return "%s-%s-%s" % (ts, re.sub(r"[^a-z0-9]+", "-", mode.lower()).strip("-"),
                         hashlib.sha256(raw.encode()).hexdigest()[:8])


def repo_roots(start: str) -> list[str]:
    """Find enclosing git roots (opensource submodule + superproject)."""
    roots: list[str] = []
    cur = os.path.abspath(start)
    if not os.path.isdir(cur):
        cur = os.path.dirname(cur)
    while True:
        if os.path.exists(os.path.join(cur, ".git")):
            roots.append(cur)
        parent = os.path.dirname(cur)
        if parent == cur:
            break
        cur = parent
    return roots


def refuse_output_inside_repo(path: str, what: str = "output") -> str:
    """Refuse writing vendor-derived artifacts inside any enclosing git repo."""
    real = os.path.realpath(path)
    for root in repo_roots(os.path.dirname(os.path.abspath(__file__))):
        if real == root or real.startswith(root + os.sep):
            raise GuardError(
                "out-inside-repo",
                "%s path %s is inside the git repo %s; vendor-derived artifacts "
                "are local evidence only (use /tmp or a gitignored build/ dir, "
                "e.g. build/wifi-bench/)" % (what, real, root),
            )
    return real


def gitignored_evidence_ok(path: str) -> tuple[bool, str]:
    """Report whether an evidence dir is outside the repo or under build/."""
    real = os.path.realpath(path)
    roots = repo_roots(os.path.dirname(os.path.abspath(__file__)))
    for root in roots:
        if real == root or real.startswith(root + os.sep):
            rel = os.path.relpath(real, root)
            if rel == "build" or rel.startswith("build" + os.sep) or rel.startswith("tmp" + os.sep):
                return True, "evidence under gitignored %s" % rel
            return False, (
                "evidence dir %s is inside git repo %s and not under build/; "
                "it could be committed" % (real, root))
    return True, "evidence outside the git repos (%s)" % real


__all__ = [name for name in dir() if not name.startswith("_")]
