#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""bench_session.py - one-command attended bench session runner (Hi5622V100 Wi-Fi).

Lane: wifi-bench-tooling (shared task task-97).  HOST-ONLY by construction: in
--dry-run (the default test mode for this lane) it never contacts the device;
it validates the local instrument/firmware/custom-block files, builds the
device-side session script, statically audits it against the hard rules, and
prints every planned device write.  A real run is one command for a human at
the bench and is gated by the same guards before anything is deployed.

Modes
  stock-golden   read-only golden-state capture on the stock vendor stack
                 (telnet; no module load, no writes at all)
  our-golden     clean-room boot to the golden state + D2H ready-pair check
  patched        same boot with a patched firmware image (fw_path variant,
                 PD/PD2); captures the exception/vector-path observables
  h2d            our-golden + exactly ONE safe H2D SR announce after the ready
                 pair (one frame, one idea, no retries; a timeout closes the lane)

Hard safety policy is in bench_guards.py; this file never writes CA
0x400392F0, never writes 0xCECE (cece_mode 1 by default), never retries an H2D
frame, and refuses an absolute fw_path.

Usage (host-only):
    python3 tools/bench_session.py --list
    python3 tools/bench_session.py --mode our-golden --dry-run --module /path/hi5622_pciet_hcc.ko --wifi-block /path/wifi_block.txt --plat-block /path/plat_block.txt
    python3 tools/bench_session.py --mode our-golden --yes-really --attended --module ... --wifi-block ... --plat-block ...
    python3 tools/bench_session.py --mode patched --yes-really --attended --module ... --fw-file /tmp/hi5622-patched/FIRMWARE.PD2.bin --fw-manifest /tmp/hi5622-patched/manifest.json --fw-path hi_wifi/FIRMWARE.PD2.bin ...
    python3 tools/bench_session.py --mode h2d --yes-really --attended ...
    python3 tools/bench_session.py --selftest-report
"""
from __future__ import annotations

import argparse
import dataclasses
import glob
import json
import os
import re
import shlex
import struct
import subprocess
import sys
import tempfile
import time

TOOLDIR = os.path.dirname(os.path.abspath(__file__))
if TOOLDIR not in sys.path:
    sys.path.insert(0, TOOLDIR)
import bench_guards as G  # noqa: E402
import fw_patch_builder as FWB  # noqa: E402
import protocol_sequence as PS  # noqa: E402

SCHEMA_PLAN = "hi5622-bench-session-plan/1"
TOOL_VERSION = "wifi-bench-tooling/2"

# Bundled data-driven protocol sequence (the current hardware-validated lap:
# PD4 golden boot -> ctx_create type 1 -> TXQ config sync 72 -> D2H delta).
PROTOCOL_SEQUENCE_DIR = os.path.join(TOOLDIR, "sequences")

# Read-only BAR0 helper (task-108 follow-up).  The image's busybox has no
# devmem applet and cannot read() PCI MMIO through /dev/mem on ARM (__va() of a
# device address is unmapped), and the PCI sysfs resource0 file has no read():
# a static PROT_READ mmap helper is the reliable userspace read path.
HELPER_SRC = os.path.join(TOOLDIR, "bar0read.c")
HELPER_REMOTE = "/root/hi5622-bar0read"
HELPER_CACHE = os.path.join(tempfile.gettempdir(), "hi5622-bench-tools")

# cfg80211 bench-mode userspace: the new image's rootfs carries neither iw nor
# libnl-tiny (the 2026-11-25 milestone pushed the stock rootfs binaries to /tmp),
# so the runner deploys them itself and points iw at the library with
# LD_LIBRARY_PATH=/tmp.  cfg80211.ko is loaded from /root before the instrument.
CFG80211_KO_REMOTE = "/root/cfg80211.ko"
IW_REMOTE = "/tmp/hi5622-bench-iw"
LIBNL_REMOTE_DIR = "/tmp"

DEFAULT_ENDPOINT = "0000:00:00.0"
REMOTE_BASE = "/tmp/hi5622-bench"
REMOTE_MODULE_DIR = "/root"
REMOTE_FW_DIR = "/lib/firmware"

GOLDEN_WORDS = (0x86807FEF, 0x00000007, 0x00000002, 0x0000ABCD)
GFX_EXCP_BITS = {0x02: "undefined", 0x04: "svc", 0x08: "prefetch-abort", 0x10: "data-abort"}
OUT1_BITS = {0x04: "bit2-startup-vector-announce", 0x08: "bit3-ete-done", 0x40: "bit6-d2h-notify"}
OBSERVED_ATTRS = ("hcc_status", "hcc_ready", "hcc_events", "hcc_irq_probe",
                  "hcc_rings", "hcc_plan", "rd_status", "rd_decode", "rd_catalog")


# ---------------------------------------------------------------------------
# Small utilities
# ---------------------------------------------------------------------------
def repo_root() -> str:
    cur = os.path.dirname(TOOLDIR)
    while True:
        if os.path.isfile(os.path.join(cur, "tools", "bench-link.sh")):
            return cur
        parent = os.path.dirname(cur)
        if parent == cur:
            return os.getcwd()
        cur = parent


ROOT = repo_root()

# Evidence locations: build/ is the project evidence tree but some of its
# subdirectories are root-owned on this checkout, so pick the first writable
# gitignored candidate and fall back to a temp dir (with a loud warning).
EVIDENCE_CANDIDATES = (
    os.path.join(ROOT, "build", "wifi-bench"),
    os.path.join(ROOT, "build", "evidence-bench-gates", "wifi-bench"),
)


def _writable_dir(path: str) -> bool:
    try:
        os.makedirs(path, exist_ok=True)
        probe = os.path.join(path, ".write-probe")
        with open(probe, "w") as f:
            f.write("x")
        os.unlink(probe)
        return True
    except OSError:
        return False


def evidence_base() -> tuple:
    """Return (dir, is_project_evidence).  Never raises; warns on /tmp fallback."""
    for cand in EVIDENCE_CANDIDATES:
        if _writable_dir(cand):
            return cand, True
    fallback = os.path.join(tempfile.gettempdir(), "wifi-bench-evidence")
    os.makedirs(fallback, exist_ok=True)
    print("WARNING: no writable project evidence dir (%s); using %s - /tmp is "
          "NOT project evidence, copy it out before it matters"
          % (", ".join(EVIDENCE_CANDIDATES), fallback), file=sys.stderr)
    return fallback, False


def _merged_env(env=None):
    if not env:
        return None
    base = os.environ.copy()
    base.update({k: str(v) for k, v in env.items()})
    return base


def run_local_capture(cmd, timeout: float = 120, env=None, input_bytes=None):
    """Run a local command; return (rc, stdout_bytes, stderr_bytes).

    env entries are layered over os.environ (used to pin ROUTER_HOST for
    tools/rsh.sh).  input_bytes feeds stdin (binary-safe file uploads).
    """
    try:
        p = subprocess.run(cmd, capture_output=True, input=input_bytes,
                           timeout=timeout, env=_merged_env(env))
        return p.returncode, p.stdout, p.stderr
    except FileNotFoundError:
        return 127, b"", ("not found: %s" % cmd[0]).encode()
    except subprocess.TimeoutExpired:
        return 124, b"", ("timeout after %ss: %s" % (timeout, " ".join(cmd))).encode()
    except OSError as e:
        return 126, b"", ("exec failed: %s" % e).encode()


def run_local(cmd, timeout: float = 120, env=None, input_bytes=None):
    rc, out, err = run_local_capture(cmd, timeout=timeout, env=env,
                                     input_bytes=input_bytes)
    return (rc, out.decode("utf-8", "replace"),
            err.decode("utf-8", "replace"))


def sha256_text(s: str) -> str:
    return G.sha256_bytes(s.encode())


def norm_int(v) -> int:
    return G.parse_int(v) & 0xFFFFFFFF


# ---------------------------------------------------------------------------
# Modes
# ---------------------------------------------------------------------------
@dataclasses.dataclass(frozen=True)
class ModeSpec:
    name: str
    about: str
    uses_module: bool
    fw_role: str            # "vendor" | "patched" | "none"
    read_only: bool
    needs_blocks: bool
    needs_attended: bool
    transport: str          # "ssh" | "telnet"
    announce: bool
    strict_boot_gate: bool
    require_ready_option: bool
    require_ready_for_announce: bool
    observe_s: float


MODES = {
    "stock-golden": ModeSpec(
        "stock-golden",
        "read-only golden-state capture on the stock vendor stack (no module, no writes)",
        uses_module=False, fw_role="none", read_only=True, needs_blocks=False,
        needs_attended=False, transport="telnet", announce=False,
        strict_boot_gate=False, require_ready_option=False,
        require_ready_for_announce=False, observe_s=0.0),
    "our-golden": ModeSpec(
        "our-golden",
        "clean-room boot to the golden handshake state + D2H ready-pair check",
        uses_module=True, fw_role="vendor", read_only=False, needs_blocks=True,
        needs_attended=True, transport="ssh", announce=False,
        strict_boot_gate=True, require_ready_option=True,
        require_ready_for_announce=False, observe_s=22.0),
    "patched": ModeSpec(
        "patched",
        "clean-room boot with a patched FIRMWARE.<PATCH>.bin (PD2/PD3/PD3B) + observables",
        uses_module=True, fw_role="patched", read_only=False, needs_blocks=True,
        needs_attended=True, transport="ssh", announce=False,
        strict_boot_gate=False, require_ready_option=False,
        require_ready_for_announce=False, observe_s=20.0),
    "h2d": ModeSpec(
        "h2d",
        "our-golden + exactly one safe H2D SR announce after the ready pair",
        uses_module=True, fw_role="vendor", read_only=False, needs_blocks=True,
        needs_attended=True, transport="ssh", announce=True,
        strict_boot_gate=True, require_ready_option=False,
        require_ready_for_announce=True, observe_s=22.0),
    "protocol": ModeSpec(
        "protocol",
        "PD4 golden boot on hi5622_wifi + a data-driven H2D protocol sequence "
        "with per-step gates and a JSON/markdown evidence record",
        uses_module=True, fw_role="patched", read_only=False, needs_blocks=False,
        needs_attended=True, transport="ssh", announce=False,
        strict_boot_gate=True, require_ready_option=False,
        require_ready_for_announce=False, observe_s=0.0),
    "cfg80211": ModeSpec(
        "cfg80211",
        "protocol mode on the cfg80211-capable hi5622_wifi: deploy/load "
        "cfg80211.ko + iw + libnl-tiny, gate the wiphy registration after the "
        "ready handshake, then run a cfg80211/VAP/scan sequence",
        uses_module=True, fw_role="patched", read_only=False, needs_blocks=False,
        needs_attended=True, transport="ssh", announce=False,
        strict_boot_gate=True, require_ready_option=False,
        require_ready_for_announce=False, observe_s=0.0),
}


# ---------------------------------------------------------------------------
# Custom-block files
# ---------------------------------------------------------------------------
def load_block_file(path: str) -> list:
    """Load an '<index>=0x<value>' custom-block word file (0-based indices).

    The format is the one captured from the vendor golden state
    (/root/wifi_block.txt: 618 lines for the wifi block at CPU 0x1B2860 /
    phys 0x4086A860).  The file is local evidence and is never committed.
    """
    if not os.path.isfile(path):
        raise G.GuardError("block-missing", "custom-block file not found: %s" % path)
    words = []
    seen = set()
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if "=" not in line:
                raise G.GuardError("block-format",
                                   "%s:%d: expected '<index>=0x<value>', got %r"
                                   % (path, lineno, line[:40]))
            k, _, v = line.partition("=")
            try:
                idx = int(k.strip(), 0)
                val = int(v.strip(), 0)
            except ValueError:
                raise G.GuardError("block-format",
                                   "%s:%d: unparseable index/value %r"
                                   % (path, lineno, line[:40])) from None
            if idx < 0 or idx > 0x10000:
                raise G.GuardError("block-index", "%s:%d: index %d out of range"
                                   % (path, lineno, idx))
            if val < 0 or val > 0xFFFFFFFF:
                raise G.GuardError("block-value", "%s:%d: value 0x%X is not a 32-bit word"
                                   % (path, lineno, val))
            if idx in seen:
                raise G.GuardError("block-duplicate", "%s:%d: duplicate index %d"
                                   % (path, lineno, idx))
            seen.add(idx)
            words.append((idx, val))
    return words


def validate_block(words, base: int, expected_words: int, label: str,
                   guard) -> dict:
    indices = sorted(i for i, _ in words)
    if len(words) != expected_words:
        raise G.GuardError("block-count",
                           "%s block has %d word(s), expected %d (the GP8-pre-seeded "
                           "firmware block)" % (label, len(words), expected_words))
    if indices != list(range(expected_words)):
        missing = sorted(set(range(expected_words)) - set(indices))[:5]
        raise G.GuardError("block-indices",
                           "%s block indices are not 0..%d (missing e.g. %s)"
                           % (label, expected_words - 1, missing))
    for idx, val in words:
        guard.check_devmem_write(base + idx * 4, val, step="%s-block" % label)
    return {"label": label, "base": "0x%08X" % base, "words": len(words),
            "bytes": len(words) * 4,
            "sha256": sha256_text("".join("%d=%08x\n" % (i, v) for i, v in words)),
            "first": "0x%08X" % words[0][1] if words else None,
            "last": "0x%08X" % words[-1][1] if words else None}


# ---------------------------------------------------------------------------
# Plan
# ---------------------------------------------------------------------------
@dataclasses.dataclass
class PlannedWrite:
    step: str
    kind: str            # attr | devmem | insmod | host-deploy | host-run
    target: str
    value: str
    ca: int | None = None
    rationale: str = ""

    def as_dict(self) -> dict:
        return dataclasses.asdict(self)


@dataclasses.dataclass
class Step:
    name: str
    phase: str
    gate: str
    writes: list = dataclasses.field(default_factory=list)

    def as_dict(self) -> dict:
        return {"name": self.name, "phase": self.phase, "gate": self.gate,
                "writes": [w.as_dict() for w in self.writes]}


@dataclasses.dataclass
class SessionPlan:
    mode: str
    session_id: str
    spec: ModeSpec
    steps: list
    module: dict | None
    fw: dict | None
    blocks: list
    deploy: list
    script: str
    audit: dict
    warnings: list
    opts: dict
    sequence: dict | None = None

    def as_dict(self) -> dict:
        return {
            "schema": SCHEMA_PLAN, "tool": TOOL_VERSION, "mode": self.mode,
            "session_id": self.session_id, "about": self.spec.about,
            "module": self.module, "fw": self.fw, "blocks": self.blocks,
            "deploy": self.deploy, "steps": [s.as_dict() for s in self.steps],
            "sequence": (PS.plan_doc(self.sequence) if self.sequence else None),
            "writes": {"total": sum(len(s.writes) for s in self.steps),
                       "by_kind": _counts(w.kind for s in self.steps for w in s.writes)},
            "audit": self.audit, "warnings": self.warnings, "options": self.opts,
        }


def _counts(values) -> dict:
    out = {}
    for v in values:
        out[v] = out.get(v, 0) + 1
    return out


# ---------------------------------------------------------------------------
# Device script generation
# ---------------------------------------------------------------------------
def _q(s: str) -> str:
    """Single-quote a constant for POSIX shell."""
    return "'" + str(s).replace("'", "'\\''") + "'"


def generate_device_script(cfg: dict) -> str:
    """Generate the device-side POSIX shell session script.

    Every device write is either a w_attr helper call (attribute literal in the
    call) or a devmem write through an addr variable whose base literal is
    inside a custom-block window; bench_guards.audit_device_script() verifies
    both statically.
    """
    mode = cfg["mode"]
    want_announce = bool(cfg.get("announce"))
    ready_mode = cfg.get("ready_mode", "informational")
    plat = cfg.get("plat")
    wifi = cfg.get("wifi")
    module = cfg.get("module")
    fw = cfg.get("fw")
    check_module_sha = bool(module and module.get("sha256") and module["sha256"] != "dry-run-unverified")
    check_fw_sha = bool(fw and fw.get("sha256") and fw["sha256"] != "dry-run-unverified")
    insmod_params = cfg.get("insmod_params") or []
    expected_kernel = cfg.get("expected_kernel", "6.18.60")
    vendor_modules_expected = bool(cfg.get("vendor_modules_expected", False))
    attr_checks = list(cfg.get("attr_checks") or [])
    reads = list(cfg.get("attr_reads") or [])
    boot_wait = float(cfg.get("boot_wait_s", 0.4))
    ready_id1_s = float(cfg.get("ready_id1_s", 2.0))
    ready_id2_s = float(cfg.get("ready_id2_s", 20.0))
    cece_mode = int(cfg.get("cece_mode", 1))

    insmod_line = ""
    if module:
        insmod_line = "insmod %s" % _q(module["remote"])
        for p in insmod_params:
            insmod_line += " " + _q(p)
    module_name = module["name"] if module else ""

    lines = []
    a = lines.append
    a("#!/bin/sh")
    a("# generated by wifi-bench-tooling/bench_session.py - DO NOT EDIT")
    a("# mode=%s session=%s" % (mode, cfg["sid"]))
    a("# hard rules encoded: never write CA 0x400392F0; never write 0xCECE unless")
    a("# the +8==2 latch is set (default cece_mode=1 = never); release 0x5A5A only;")
    a("# one H2D announce max; every device write is logged to writes.log.")
    a("set -u")
    a("SID=%s" % _q(cfg["sid"]))
    a("MODE=%s" % _q(mode))
    a("P=/sys/bus/pci/devices/%s" % cfg["endpoint"])
    a("R0=$P/resource0")
    a("RDIR=%s" % _q("/tmp/hi5622-bench/" + cfg["sid"]))
    a("R=$RDIR/report.txt")
    a("W=$RDIR/writes.log")
    a("EXPECTED_KERNEL=%s" % _q(expected_kernel))
    a("VENDOR_MODULES_EXPECTED=%d" % (1 if vendor_modules_expected else 0))
    a("CECE_MODE=%d" % cece_mode)
    a("PAGE_LATCHED=0")
    a("DMESG_MARK=0")
    a("mkdir -p \"$RDIR\" || exit 11")
    a(": > \"$R\"")
    a(": > \"$W\"")
    a("trap 'cat \"$R\"' EXIT")
    a("")
    a('say() { printf "%s\\n" "$*" >> "$R"; }')
    a('wlog() { printf "%s\\n" "$*" >> "$W"; }')
    a('stop() { say "STOP:$1: $2"; say "GATE-FAIL name=$1"; say "SESSION_RESULT: STOP:$1"; exit 10; }')
    a('fail() { say "FAIL:$1: $2"; say "SESSION_RESULT: FAIL:$1"; exit 11; }')
    a('section() { say ""; say "== SECTION $1"; }')
    a('w_attr() { a=$1; v=$2; s=$3; wlog "WRITE attr=$a step=$s value=$v";')
    a('  if ! printf \'%s\' "$v" > "$P/$a" 2>>"$R"; then stop attr-write-failed "$a($s)"; fi; }')
    a('r_attr() { a=$1; section "$a"; cat "$P/$a" >> "$R" 2>&1 || say "READ-FAIL attr=$a"; }')
    a('rd32() { lbl=$1; off=$2;')
    a('  if command -v hexdump >/dev/null 2>&1; then')
    a('    val=$(dd if="$R0" bs=4 skip=$((off / 4)) count=1 2>/dev/null | hexdump -v -e \'1/4 "%08x"\')')
    a('  else')
    a('    val=$(dd if="$R0" bs=4 skip=$((off / 4)) count=1 2>/dev/null | od -An -tx4 | tr -d \' \\n\')')
    a('  fi')
    a('  say "RD32 label=$lbl bar0=0x$(printf %x "$off") v=0x$val"; }')
    a('rd_page() { t=$1; section "page-$t";')
    a('  p0=$(dd if="$R0" bs=4 skip=$((0x877000 / 4)) count=1 2>/dev/null | od -An -tx4 | tr -d \' \\n\')')
    a('  p4=$(dd if="$R0" bs=4 skip=$((0x877004 / 4)) count=1 2>/dev/null | od -An -tx4 | tr -d \' \\n\')')
    a('  p8=$(dd if="$R0" bs=4 skip=$((0x877008 / 4)) count=1 2>/dev/null | od -An -tx4 | tr -d \' \\n\')')
    a('  pc=$(dd if="$R0" bs=4 skip=$((0x87700c / 4)) count=1 2>/dev/null | od -An -tx4 | tr -d \' \\n\')')
    a('  say "PAGE t=$t w=0x$p0 0x$p4 0x$p8 0x$pc ack2=$([ "$p8" = "00000002" ] && printf 1 || printf 0)"')
    a('  if [ "$p8" = "00000002" ]; then PAGE_LATCHED=1; say "PAGE-LATCH t=$t ack2=1"; fi; }')
    a('norm_hex() { v=$(printf \'%s\' "$1" | tr -d \' \\t\\n\' | tr \'A-F\' \'a-f\');')
    a('  case "$v" in 0x*) v=${v#0x};; esac; [ -n "$v" ] || { printf 0x0; return; };')
    a('  while [ "${v#0}" != "$v" ]; do v=${v#0}; done; printf \'0x%s\' "${v:-0}"; }')
    a('gate_grep() { f=$1; p=$2; n=$3; if grep -q "$p" "$f" 2>/dev/null; then say "GATE $n PASS ($p)";')
    a('  else say "GATE $n FAIL ($p)"; stop "$n" "pattern not found: $p"; fi; }')
    a('gate_status() { p=$1; n=$2; if grep -q "$p" "$P/status" 2>/dev/null; then say "GATE $n PASS ($p)";')
    a('  else say "GATE $n FAIL ($p)"; stop "$n" "status pattern not found: $p"; fi; }')
    a('gate_hcc() { p=$1; n=$2; if grep -q "$p" "$P/hcc_status" 2>/dev/null; then say "GATE $n PASS ($p)";')
    a('  else say "GATE $n FAIL ($p)"; stop "$n" "hcc_status pattern not found: $p"; fi; }')
    a('dmesg_since() { f=$1; if [ "$DMESG_MARK" -gt 0 ]; then dmesg | tail -n +$((DMESG_MARK + 1)) > "$f" 2>/dev/null; fi;')
    a('  dmesg > "$f.full" 2>/dev/null || :; tail -n 400 "$f.full" > "$f" 2>/dev/null || :; }')
    a("")

    a("section preflight")
    a('say "SESSION mode=$MODE sid=$SID started=$(date +%s)"')
    a('say "KERNEL $(uname -r)"')
    a('say "CMDLINE $(cat /proc/cmdline 2>/dev/null)"')
    a('[ -d "$P" ] || stop endpoint-missing "no PCI endpoint %s"' % cfg["endpoint"])
    a('[ -r "$R0" ] || stop resource-missing "resource0 not readable"')
    a('say "GATE image PASS kernel=$(uname -r)"')
    a('MODS=$(cat /proc/modules 2>/dev/null | awk \'{print $1}\')')
    a('if printf "%s\\n" "$MODS" | grep -q "^hi5622_pciet"; then stop module-already-loaded "hi5622_pciet* already loaded - reboot first"; fi')
    a('say "GATE module-absent PASS"')
    a('VEND=$(printf "%s\\n" "$MODS" | grep -E "^hi5622v100_(plat|wifi)" | wc -l)')
    a('if [ "$VENDOR_MODULES_EXPECTED" = "1" ]; then')
    a('  [ "$VEND" -gt 0 ] && say "GATE vendor-stack PASS vendor_modules=$VEND" || stop vendor-stack-missing "stock golden trace needs the vendor stack"')
    a('else')
    a('  [ "$VEND" -eq 0 ] && say "GATE vendor-absent PASS" || stop vendor-modules-loaded "vendor hi5622v100 modules are loaded; this is not our image"')
    a('fi')
    a('command -v sha256sum >/dev/null 2>&1 || stop tool-missing "sha256sum"')
    a('DMESG_MARK=$(dmesg 2>/dev/null | wc -l)')
    a('say "DMESG_MARK $DMESG_MARK"')

    if module:
        a("section module-file")
        a('M=%s' % _q(module["remote"]))
        a('[ -f "$M" ] || stop module-deploy-missing "module not deployed: $M"')
        a('MSHA=$(sha256sum "$M" | awk \'{print $1}\')')
        a('say "MODULE name=%s sha256=$MSHA"' % module["name"])
        if check_module_sha:
            a('if [ "$MSHA" != "%s" ]; then stop module-sha-mismatch "expected %s"; fi'
              % (module["sha256"], module["sha256"]))
            a('say "GATE module-sha PASS"')
        else:
            a('say "WARN module-sha unchecked (dry-run placeholder)"')
    if fw:
        a("section firmware-file")
        a('FWP=%s' % _q(fw["remote"]))
        a('[ -f "$FWP" ] || stop firmware-missing "on-box firmware not found: $FWP"')
        a('FSZ=$(wc -c < "$FWP" | tr -d " ")')
        a('FSHA=$(sha256sum "$FWP" | awk \'{print $1}\')')
        a('say "FIRMWARE path=%s size=$FSZ sha256=$FSHA"' % fw["remote"])
        if check_fw_sha:
            a('if [ "$FSHA" != "%s" ]; then stop firmware-sha-mismatch "expected %s"; fi'
              % (fw["sha256"], fw["sha256"]))
            a('if [ "$FSZ" != "%d" ]; then stop firmware-size-mismatch "expected %d B"; fi'
              % (fw["len"], fw["len"]))
            a('say "GATE firmware-sha PASS"')
        else:
            a('say "WARN firmware-sha unchecked (dry-run placeholder)"')
    for blk in (plat, wifi):
        if not blk:
            continue
        a("section block-file-%s" % blk["label"])
        a('BF=%s' % _q(blk["remote"]))
        a('[ -f "$BF" ] || stop block-missing "block file not deployed: $BF"')
        a('BN=$(grep -c "=" "$BF" 2>/dev/null || printf 0)')
        a('BSHA=$(sha256sum "$BF" | awk \'{print $1}\')')
        a('say "BLOCKFILE label=%s words=$BN sha256=$BSHA want=%d"'
          % (blk["label"], blk["words"]))
        a('if [ "$BN" != "%d" ]; then stop block-count "expected %d words"; fi'
          % (blk["words"], blk["words"]))

    if module:
        a("section load")
        a('wlog "HOST ACTION insmod %s"' % module["remote"])
        a('%s 2>>"$R"' % insmod_line)
        a('RC=$?')
        a('if [ "$RC" -ne 0 ]; then fail insmod "insmod rc=$RC"; fi')
        a('grep -q "^%s " /proc/modules || fail insmod-verify "module not in /proc/modules"'
          % module_name)
        a('say "GATE insmod PASS"')
        a('sleep 1')
        a('dmesg_since "$RDIR/dmesg-load.txt"')
        a("section dmesg-load")
        a('cat "$RDIR/dmesg-load.txt" >> "$R" 2>/dev/null || :')
        a('grep -q "probe" "$RDIR/dmesg-load.txt" 2>/dev/null && say "GATE probe PASS" || say "WARN probe-line-not-found"')
        for attr in attr_checks:
            a('[ -e "$P/%s" ] || stop module-profile-mismatch "missing attribute %s"'
              % (attr, attr))
        a('say "GATE module-profile PASS"')

    a("section boot-baseline")
    for lbl, off in (("hs0", 0x877000), ("hs4", 0x877004), ("hs8", 0x877008),
                     ("hsC", 0x87700C), ("state", 0x7D0B60), ("mask", 0x6F8008),
                     ("out0", 0x3F1010), ("out1", 0x3F1014), ("out2", 0x3F12D4),
                     ("out5ro", 0x3F12F0)):
        a('rd32 %s 0x%X' % (lbl, off))

    if module:
        a("section prepare")
        a('w_attr prepare 1 prepare')
        a('gate_status "windows=1" prepare')
        a('r_attr status')
        a("section upload")
        a('w_attr upload 1 upload')
        a('gate_status "fw_loaded=1" upload')
        a('r_attr status')
        a("section verify")
        a('w_attr verify 1 verify')
        a('gate_status "fw_loaded=1" verify')
        a('dmesg_since "$RDIR/dmesg-verify.txt"')
        a("section dmesg-verify")
        a('cat "$RDIR/dmesg-verify.txt" >> "$R" 2>/dev/null || :')
        a('gate_grep "$RDIR/dmesg-verify.txt" "diffs=0" verify')
        a('r_attr status')

    if module and (plat or wifi):
        a("section blocks")
        for blk in (plat, wifi):
            if not blk:
                continue
            tag = blk["label"].upper()
            a("%s_BASE=0x%08X" % (tag, blk["base_int"]))
            a("%s_FILE=%s" % (tag, _q(blk["remote"])))
            a('n=0; mm=0')
            a('while IFS="=" read -r idx val; do')
            a('  case "$idx" in ""|*[!0-9]*) continue;; esac')
            a('  case "$val" in 0x*|0X*) : ;; *) stop block-format "%s non-hex value";; esac' % blk["label"])
            a('  addr=$((%s_BASE + idx * 4))' % tag)
            a('  if ! devmem "$addr" 32 "$val" >/dev/null 2>>"$R"; then stop block-write "%s idx=$idx addr=$addr"; fi' % blk["label"])
            a('  wlog "WRITE devmem label=%s idx=$idx addr=$addr value=$val"' % blk["label"])
            a('  rb=$(devmem "$addr" 32 2>/dev/null || :)')
            a('  if [ "$(norm_hex "$rb")" = "$(norm_hex "$val")" ]; then n=$((n + 1)); else mm=$((mm + 1)); wlog "MISMATCH %s idx=$idx want=$val got=$rb"; fi' % blk["label"])
            a('done < "$%s_FILE"' % tag)
            a('say "BLOCK label=%s words=$n readback_mismatch=$mm base=$%s_BASE"'
              % (blk["label"], tag))
            a('if [ "$mm" -ne 0 ]; then stop block-readback "%s mismatch=$mm"; fi' % blk["label"])
            a('if [ "$n" -ne %d ]; then stop block-count "%s seeded=$n"; fi'
              % (blk["words"], blk["label"]))

    if module:
        a("section arm")
        a('w_attr hcc_arm 1 hcc_arm')
        a('gate_hcc "armed=1" hcc-arm')
        a('r_attr hcc_status')
        a("section loop")
        a('w_attr hcc_loop 1 hcc_loop')
        a('gate_hcc "loop=running" hcc-loop')
        a('r_attr hcc_status')
        a("section release")
        a('wlog "HOST ACTION release 0x5A5A -> CA 0x40000108 (release only, never CECE)"')
        a('w_attr release 1 release')
        a('gate_status "cpu_released=1" release')
        a('sleep %s' % _fmt_secs(boot_wait))
        a('rd_page t0.2')
        a('rd32 state0.2 0x7D0B60')
        a('rd32 mask0.2 0x6F8008')
        a('rd32 out1_0.2 0x3F1014')
        a('sleep 1')
        a('rd_page t1.2')
        a('rd32 state1.2 0x7D0B60')
        a('rd32 mask1.2 0x6F8008')
        a('rd32 out1_1.2 0x3F1014')
        a('dmesg_since "$RDIR/dmesg-release.txt"')
        a("section dmesg-release")
        a('cat "$RDIR/dmesg-release.txt" >> "$R" 2>/dev/null || :')

        a("section ready")
        a('H1=0; H2=0')
        a('NOW=$(date +%s); D1=$((NOW + ' + str(int(ready_id1_s)) + '))')
        a('while [ "$(date +%s)" -lt "$D1" ]; do')
        a('  if grep -q "id1_seen=1" "$P/hcc_ready" 2>/dev/null; then H1=1; break; fi')
        a('  sleep 1')
        a('done')
        a('say "READY phase=id1 seen=$H1 deadline_s=%d"' % int(ready_id1_s))
        if ready_mode in ("strict", "announce"):
            a('if [ "$H1" != "1" ]; then stop ready-id1-timeout "no g4 id1 device_plat_ready within %d s"; fi'
              % int(ready_id1_s))
        a('NOW=$(date +%s); D2=$((NOW + ' + str(int(ready_id2_s)) + '))')
        a('while [ "$(date +%s)" -lt "$D2" ]; do')
        a('  if grep -q "id2_seen=1" "$P/hcc_ready" 2>/dev/null; then H2=1; break; fi')
        a('  sleep 1')
        a('done')
        a('say "READY phase=id2 seen=$H2 deadline_s=%d"' % int(ready_id2_s))
        if ready_mode in ("strict", "announce"):
            a('if [ "$H2" != "1" ]; then stop ready-id2-timeout "no g4 id2 DEVICE READY within %d s"; fi'
              % int(ready_id2_s))
        a('r_attr hcc_ready')
        a('r_attr hcc_events')
        a('r_attr hcc_irq_probe')
        a('r_attr hcc_rings')

        if want_announce:
            a("section h2d")
            a('if [ "$PAGE_LATCHED" != "1" ]; then stop h2d-no-boot-latch "announce needs a real +8==2 observation"; fi')
            a('if [ "$H1" != "1" ] || [ "$H2" != "1" ]; then stop h2d-not-ready "announce only after the id1+id2 ready pair"; fi')
            a('wlog "HOST ACTION hcc_sr_announce one-shot (no retry)"')
            a('if printf \'%s\' 1 > "$P/hcc_sr_announce" 2>>"$R"; then')
            a('  H2DRES=consumed')
            a('else')
            a('  H2DRES=timeout')
            a('fi')
            a('wlog "H2D attempt=1 result=$H2DRES"')
            a('say "H2D attempt=1 result=$H2DRES (single frame, no retries; a timeout closes the lane)"')
            a('sleep 1')
            a('r_attr hcc_status')
            a('rd32 out0_after 0x3F1010')
            a('rd32 out2_after 0x3F12D4')
            a('rd32 out5_after_ro 0x3F12F0')
            a('if [ "$H2DRES" = "timeout" ]; then say "H2D-LANE CLOSED: out[0] bit3 stuck; no retry"; fi')

    a("section observe")
    if module:
        for attr in reads:
            if attr in OBSERVED_ATTRS:
                a('r_attr %s' % attr)
    else:
        for lbl, off in (("hs0", 0x877000), ("hs4", 0x877004), ("hs8", 0x877008),
                         ("hsC", 0x87700C), ("state", 0x7D0B60), ("mask", 0x6F8008),
                         ("out0", 0x3F1010), ("out1", 0x3F1014), ("out2", 0x3F12D4),
                         ("out3", 0x3F9438), ("out4", 0x3F9414),
                         ("out5ro", 0x3F12F0), ("ete_intr", 0x3F1508),
                         ("ete_status", 0x3F2874), ("dr0fill", 0x3F2000),
                         ("g_obj", 0x7710F4), ("g_armed", 0x7711A8),
                         ("g_pending", 0x7711AC), ("boot_slot", 0x7E542C)):
            a('rd32 %s 0x%X' % (lbl, off))
        a("section stock-stack")
        a('lsmod 2>/dev/null | grep -E "^hi5622v100|^hi_pcie|^hi_wifi" >> "$R" 2>&1 || :')
        a("section stock-dmesg")
        a('dmesg 2>/dev/null | tail -n 200 >> "$R" 2>&1 || :')
        a("section stock-iwinfo")
        a('iwinfo 2>/dev/null | head -40 >> "$R" 2>&1 || :')

    a("section dmesg")
    a('dmesg 2>/dev/null | tail -n 300 >> "$R" 2>&1 || :')
    a("section writes")
    a('say "WRITES-DEVICE total=$(wc -l < "$W")"')
    a('say "GATE no-cece-default PASS"')
    a('say "SESSION_RESULT: PASS"')
    a("section end")
    a('say "SESSION end=$(date +%s)"')
    return "\n".join(lines) + "\n"


def _fmt_secs(v: float) -> str:
    if float(v).is_integer():
        return str(int(v))
    return ("%.3f" % v).rstrip("0").rstrip(".")


def generate_protocol_script(cfg: dict) -> str:
    """Generate the device-side POSIX script for the data-driven protocol mode.

    Boot skeleton for the hi5622_wifi driver (prepare/upload/verify/seed via the
    built-in platform descriptor + the PHY block from /lib/firmware, arm/loop,
    release-only 0x5A5A, strict ready pair), followed by the sequence steps
    emitted by protocol_sequence.render_script() and the final observable dump.
    Every device write is an auditable w_attr store; every read is a read-only
    BAR0 rd32; the sequence stops on the first failed gate.
    """
    seq = cfg["sequence"]
    module = cfg["module"]
    fw = cfg["fw"]
    wblock = cfg["wifi_block"]
    params = list(cfg.get("insmod_params") or [])
    expected_kernel = cfg.get("expected_kernel", "6.18.60")
    attr_checks = list(cfg.get("attr_checks") or [])
    boot_wait = float(cfg.get("boot_wait_s", 0.4))
    ready_id1_s = float(cfg.get("ready_id1_s", 2.0))
    ready_id2_s = float(cfg.get("ready_id2_s", 20.0))
    ready_strict = bool(cfg.get("ready_strict", True))
    require_phase = cfg.get("require_phase", "READY_TXQ_SYNC")
    expected_budget = cfg.get("expected_budget")
    helper = cfg.get("helper") or {}
    helper_remote = helper.get("remote") or HELPER_REMOTE
    helper_sha = helper.get("sha256") or "dry-run-unverified"
    cfg11 = cfg.get("cfg80211") or None
    cfg11_ready_s = float((cfg11 or {}).get("ready_s") or 20.0)

    insmod_line = "insmod %s" % _q(module["remote"])
    for p in params:
        insmod_line += " " + _q(p)
    module_name = module["name"]

    lines = []
    a = lines.append
    a("#!/bin/sh")
    a("# generated by wifi-bench-tooling/bench_session.py - DO NOT EDIT")
    a("# mode=%s session=%s sequence=%s"
      % (cfg.get("mode", "protocol"), cfg["sid"], seq["name"]))
    a("# hard rules encoded: never write CA 0x400392F0; never write 0xCECE during")
    a("# boot; release 0x5A5A only; one frame per idea; stop on first failed gate.")
    a("set -u")
    a("SID=%s" % _q(cfg["sid"]))
    a("MODE='protocol'")
    a("P=/sys/bus/pci/devices/%s" % cfg["endpoint"])
    a("R0=$P/resource0")
    a("HELPER=" + _q(helper_remote))
    a("BAR0_PHYS=0")
    a("RDIR=/tmp/hi5622-bench/$SID")
    a("R=$RDIR/report.txt")
    a("W=$RDIR/writes.log")
    a("EXPECTED_KERNEL=%s" % _q(expected_kernel))
    a("PAGE_LATCHED=0")
    a("DMESG_MARK=0")
    a("RDVAL=")
    a("mkdir -p \"$RDIR\" || exit 11")
    a(": > \"$R\"")
    a(": > \"$W\"")
    a("trap 'cat \"$R\"' EXIT")
    a("")
    a('say() { printf "%s\\n" "$*" >> "$R"; }')
    a('wlog() { printf "%s\\n" "$*" >> "$W"; }')
    a('stop() { say "STOP:$1: $2"; say "GATE-FAIL name=$1"; say "SESSION_RESULT: STOP:$1"; exit 10; }')
    a('fail() { say "FAIL:$1: $2"; say "SESSION_RESULT: FAIL:$1"; exit 11; }')
    a('section() { say ""; say "== SECTION $1"; }')
    a('w_attr() { at=$1; v=$2; s=$3; wlog "WRITE attr=$at step=$s value=$v";')
    a('  if ! printf \'%s\' "$v" > "$P/$at" 2>>"$R"; then stop attr-write-failed "$at($s)"; fi; }')
    a('r_attr() { at=$1; section "$at"; cat "$P/$at" >> "$R" 2>&1 || say "READ-FAIL attr=$at"; }')
    a('r_attr_as() { n=$1; at=$2; section "$n"; cat "$P/$at" >> "$R" 2>&1 || say "READ-FAIL attr=$at"; }')
    a('norm_reg() { v=$(printf \'%s\' "$1" | tr -d \' \\t\\r\\n\' | tr \'A-F\' \'a-f\');')
    a('  case "$v" in 0x*) v=${v#0x};; esac;')
    a('  [ -n "$v" ] || { printf \'\'; return 0; };')
    a('  while [ "${#v}" -gt 8 ]; do v=${v#?}; done;')
    a('  while [ "${#v}" -lt 8 ]; do v=0$v; done;')
    a('  printf \'%s\' "$v"; }')
    a('# raw BAR0 reads go through /dev/mem at the BAR0 physical base: the PCI')
    a('# sysfs resource0 file does not support read() on this kernel (the first')
    a('# live fire of 2026-11-25 got empty values from dd if=resource0).  The')
    a('# image has CONFIG_DEVMEM=y and CONFIG_STRICT_DEVMEM=n.')
    a('rd32() { lbl=$1; off=$2;')
    a('  case "$off" in')
    a('    0x*|0X*) oh=${off#0x}; oh=${oh#0X}; offd=$((0x$oh));;')
    a('    *) offd=$((off));;')
    a('  esac')
    a('  pa=$((BAR0_PHYS + offd))')
    a('  raw=$("$HELPER" "$pa" 2>>"$R")')
    a('  val=$(norm_reg "$raw")')
    a('  if [ -z "$val" ]; then')
    a('    if command -v hexdump >/dev/null 2>&1; then')
    a('      raw=$(dd if=/dev/mem bs=1 skip=$pa count=4 2>/dev/null | hexdump -v -e \'1/4 "%08x"\')')
    a('    else')
    a('      raw=$(dd if=/dev/mem bs=1 skip=$pa count=4 2>/dev/null | od -An -tx4 2>/dev/null)')
    a('    fi')
    a('    val=$(norm_reg "$raw")')
    a('  fi')
    a('  RDVAL=$val')
    a('  say "RD32 label=$lbl bar0=0x$(printf %x "$offd") v=0x$val"; }')
    a('norm_hex() { v=$(printf \'%s\' "$1" | tr -d \' \\t\\n\' | tr \'A-F\' \'a-f\');')
    a('  case "$v" in 0x*) v=${v#0x};; esac; [ -n "$v" ] || { printf 0x0; return; };')
    a('  while [ "${v#0}" != "$v" ]; do v=${v#0}; done; printf \'0x%s\' "${v:-0}"; }')
    a("# attr/output pattern gates normalise whitespace first: a long sysfs")
    a("# line that arrives wrapped is joined back to single spaces, so a")
    a("# multi-token pattern matches whether or not the capture wrapped.")
    a("gate_attr() { n=$1; at=$2; p=$3; if tr '\\n\\t' '  ' < \"$P/$at\" 2>/dev/null | tr -s ' ' | grep -q \"$p\"; then say \"GATE $n PASS (attr=$at)\";")
    a("  else stop \"$n\" \"pattern not found in $at: $p\"; fi; }")
    a("gate_attr_absent() { n=$1; at=$2; p=$3; if tr '\\n\\t' '  ' < \"$P/$at\" 2>/dev/null | tr -s ' ' | grep -q \"$p\"; then")
    a("  stop \"$n\" \"pattern unexpectedly present in $at: $p\"; else say \"GATE $n PASS (attr=$at absent-pattern)\"; fi; }")
    a("gate_grep() { f=$1; p=$2; n=$3; if tr '\\n\\t' '  ' < \"$f\" 2>/dev/null | tr -s ' ' | grep -q \"$p\"; then say \"GATE $n PASS ($p)\";")
    a("  else stop \"$n\" \"pattern not found: $p\"; fi; }")
    a("# userspace (cfg80211 mode) gate helpers: one allowlisted command per step")
    a("gate_rc() { n=$1; got=$2; want=$3; if [ \"$got\" = \"$want\" ]; then say \"GATE $n PASS rc=$got\";")
    a("  else stop \"$n\" \"exit code $got, expected $want\"; fi; }")
    a("gate_out_has() { n=$1; f=$2; p=$3; if tr '\\n\\t' '  ' < \"$f\" 2>/dev/null | tr -s ' ' | grep -q \"$p\"; then say \"GATE $n PASS (shell)\";")
    a("  else stop \"$n\" \"pattern not found in shell output: $p\"; fi; }")
    a("gate_out_absent() { n=$1; f=$2; p=$3; if tr '\\n\\t' '  ' < \"$f\" 2>/dev/null | tr -s ' ' | grep -q \"$p\"; then")
    a("  stop \"$n\" \"pattern unexpectedly present in shell output: $p\"; else say \"GATE $n PASS (shell absent)\"; fi; }")
    a('gate_present() { n=$1; raw=$2; if [ -n "$raw" ]; then say "GATE $n PASS raw=0x$raw (reachable)";')
    a('  else stop "$n" "empty register read (window unreachable?)"; fi; }')
    a('IW=iw')
    a('gate_num() { n=$1; raw=$2; mode=$3; wmask=$4; wval=$5; shift=$6;')
    a('  [ -n "$raw" ] || stop "$n" "empty register read";')
    a('  raw=$(printf "%s" "$raw" | tr "A-F" "a-f"); ok=0;')
    a('  if [ "$wmask" = "ffffffff" ] && [ "$shift" = "0" ]; then')
    a('    wh=$(printf "%08x" "$((wval))")')
    a('    case "$mode" in')
    a('      eq) [ "$raw" = "$wh" ] && ok=1;;')
    a('      ne) [ "$raw" != "$wh" ] && ok=1;;')
    a('      zero) [ "$raw" = "00000000" ] && ok=1;;')
    a('      nonzero) [ "$raw" != "00000000" ] && ok=1;;')
    a('      ge) [ "$((0x$raw))" -ge "$((wval))" ] && ok=1;;')
    a('      *) stop "$n" "unknown gate mode $mode";;')
    a('    esac')
    a('  else')
    a('    v=$(( (0x$raw) >> shift )); m=$(( wmask >> shift )); got=$((v & m)); want=$(( (wval >> shift) & m ))')
    a('    case "$mode" in')
    a('      eq) [ "$got" -eq "$want" ] && ok=1;;')
    a('      ne) [ "$got" -ne "$want" ] && ok=1;;')
    a('      zero) [ "$got" -eq 0 ] && ok=1;;')
    a('      nonzero) [ "$got" -ne 0 ] && ok=1;;')
    a('      ge) [ "$got" -ge "$want" ] && ok=1;;')
    a('      *) stop "$n" "unknown gate mode $mode";;')
    a('    esac')
    a('  fi')
    a('  if [ "$ok" = "1" ]; then say "GATE $n PASS raw=0x$raw mask=$wmask want=$wval";')
    a('  else stop "$n" "expectation failed: raw=0x$raw mask=$wmask want=$wval"; fi; }')
    a('dmesg_since() { f=$1; if [ "$DMESG_MARK" -gt 0 ]; then dmesg | tail -n +$((DMESG_MARK + 1)) > "$f" 2>/dev/null; fi;')
    a('  dmesg > "$f.full" 2>/dev/null || :; tail -n 600 "$f.full" > "$f" 2>/dev/null || :; }')
    a("")
    a("section preflight")
    a('say "SESSION mode=$MODE sid=$SID started=$(date +%%s) sequence=%s"'
      % seq["name"])
    a('say "SEQUENCE-SHA %s"' % seq.get("sha256"))
    a('say "KERNEL $(uname -r)"')
    a('say "CMDLINE $(cat /proc/cmdline 2>/dev/null)"')
    a('[ -d "$P" ] || stop endpoint-missing "no PCI endpoint %s"' % cfg["endpoint"])
    a('[ -r "$R0" ] || stop resource-missing "resource0 not readable"')
    a("section helper-file")
    a('H=' + _q(helper_remote))
    a('[ -f "$H" ] || stop helper-missing "read helper not deployed: $H"')
    a('HSHA=$(sha256sum "$H" | awk \'{print $1}\')')
    a('say "HELPER path=$H sha256=$HSHA size=$(wc -c < "$H" | tr -d " ")"')
    a('if [ "$HSHA" != ' + _q(helper_sha) + ' ]; then stop helper-sha-mismatch "expected ' + helper_sha[:16] + '..."; fi')
    a('chmod +x "$H" 2>/dev/null || :')
    a('[ -x "$H" ] || stop helper-not-executable "$H is not executable (noexec mount?)"')
    a('say "GATE helper PASS"')
    a("")
    a("section readpath")
    a('RPHYS=$(awk \'NR==1{print $1}\' "$P/resource" 2>/dev/null)')
    a('case "$RPHYS" in 0x*|0X*) RH=${RPHYS#0x}; RH=${RH#0X}; BAR0_PHYS=$((0x$RH));; *) BAR0_PHYS=0;; esac')
    a('if [ "$BAR0_PHYS" -le 0 ]; then stop bar0-phys "cannot parse the BAR0 physical base from $P/resource (RPHYS=$RPHYS)"; fi')
    a('say "READPATH /dev/mem BAR0_PHYS=0x$(printf %x "$BAR0_PHYS") helper=$HELPER (resource0 read() is unsupported)"')
    a('RPV=$("$HELPER" "$BAR0_PHYS" 2>>"$R")')
    a('RDVAL=$(norm_reg "$RPV")')
    a('if [ -z "$RDVAL" ]; then stop bar0-readpath "read helper returned nothing for BAR0+0 (out=$RPV); check /dev/mem, exec permission and the helper sha"; fi')
    a('say "GATE readpath PASS v=0x$RDVAL"')
    a('say "GATE image PASS kernel=$(uname -r)"')
    a('MODS=$(cat /proc/modules 2>/dev/null | awk \'{print $1}\')')
    a('if printf "%s\\n" "$MODS" | grep -q "^%s "; then stop module-already-loaded "%s already loaded - reboot first"; fi'
      % (module_name, module_name, module_name))
    a('if printf "%s\\n" "$MODS" | grep -Eq "^hi5622v100_(plat|wifi)"; then stop vendor-modules-loaded "vendor stack is loaded; this is not our image"; fi')
    a('say "GATE module-absent PASS"')
    a('command -v sha256sum >/dev/null 2>&1 || stop tool-missing "sha256sum"')
    a('DMESG_MARK=$(dmesg 2>/dev/null | wc -l)')
    a('say "DMESG_MARK $DMESG_MARK"')
    a("")
    a("section module-file")
    a('M=%s' % _q(module["remote"]))
    a('[ -f "$M" ] || stop module-deploy-missing "module not deployed: $M"')
    a('MSHA=$(sha256sum "$M" | awk \'{print $1}\')')
    a('say "MODULE name=%s sha256=$MSHA"' % module_name)
    a('if [ "$MSHA" != "%s" ]; then stop module-sha-mismatch "expected %s"; fi'
      % (module["sha256"], module["sha256"]))
    a('say "GATE module-sha PASS"')
    a("")
    a("section firmware-file")
    a('FWP=%s' % _q(fw["remote"]))
    a('[ -f "$FWP" ] || stop firmware-missing "on-box firmware not found: $FWP"')
    a('FSZ=$(wc -c < "$FWP" | tr -d " ")')
    a('FSHA=$(sha256sum "$FWP" | awk \'{print $1}\')')
    a('say "FIRMWARE path=%s size=$FSZ sha256=$FSHA"' % fw["remote"])
    a('if [ "$FSHA" != "%s" ]; then stop firmware-sha-mismatch "expected %s"; fi'
      % (fw["sha256"], fw["sha256"]))
    a('if [ "$FSZ" != "%d" ]; then stop firmware-size-mismatch "expected %d B"; fi'
      % (fw["len"], fw["len"]))
    a('say "GATE firmware-sha PASS"')
    a("")
    a("section wifi-block-file")
    a('B=%s' % _q(wblock["remote"]))
    a('[ -f "$B" ] || stop wifi-block-missing "PHY block not deployed: $B"')
    a('BSZ=$(wc -c < "$B" | tr -d " ")')
    a('BSHA=$(sha256sum "$B" | awk \'{print $1}\')')
    a('say "WIFI-BLOCK path=%s size=$BSZ sha256=$BSHA"' % wblock["remote"])
    a('if [ "$BSHA" != "%s" ]; then stop wifi-block-sha-mismatch "expected %s"; fi'
      % (wblock["sha256"], wblock["sha256"]))
    a('if [ "$BSZ" != "%d" ]; then stop wifi-block-size-mismatch "expected %d B"; fi'
      % (wblock["size"], wblock["size"]))
    a('say "GATE wifi-block-sha PASS"')
    a("")
    if cfg11:
        a("section cfg80211-files")
        for role, label, remote_key in (
                ("module", "cfg80211.ko", "module"),
                ("iw", "iw", "iw"),
                ("library", "libnl-tiny", "library")):
            art = cfg11.get(role) or {}
            if not art.get("path"):
                a('stop tool-missing "no %s artifact supplied (--%s)"'
                  % (label, {"module": "cfg80211-ko", "iw": "iw-bin",
                             "library": "libnl-tiny"}[role]))
                continue
            var = "CFG11_" + role.upper()
            a("%s=%s" % (var, _q(art["remote"])))
            a('[ -f "$%s" ] || stop tool-missing "%s not deployed: $%s"'
              % (var, label, var))
            a('%s_SHA=$(sha256sum "$%s" | awk \'{print $1}\')' % (var, var))
            a('say "SUPPORT-%s path=$%s sha256=$%s_SHA"'
              % (role, var, var))
            a('if [ "$%s_SHA" != %s ]; then stop tool-sha-mismatch "%s sha256 '
              'mismatch"; fi' % (var, _q(art.get("sha256", "")), label))
            if role == "iw":
                a('chmod +x "$%s" 2>/dev/null || :' % var)
                a('[ -x "$%s" ] || stop tool-not-executable "$%s"' % (var, var))
        a('say "GATE cfg80211-files PASS"')
        a("")
        a("section cfg80211-preload")
        a("# module order: cfg80211 must be present before the instrument probes")
        a('if [ -d /sys/module/cfg80211 ]; then')
        a('  say "GATE cfg80211-present PASS (built-in or already loaded)"')
        a("else")
        a('  insmod "$CFG11_MODULE" 2>>"$R" || stop cfg80211-insmod "insmod cfg80211.ko failed"')
        a('  grep -q "^cfg80211 " /proc/modules || stop cfg80211-insmod "cfg80211 not in /proc/modules"')
        a('  say "GATE cfg80211-load PASS"')
        a("fi")
        a('grep -q "^cfg80211 " /proc/modules || [ -d /sys/module/cfg80211 ] || stop cfg80211-insmod "cfg80211 absent"')
        a('say "GATE module-order PASS (cfg80211 before %s)"' % module_name)
        a('# userspace: deployed iw if the rootfs has none; libnl-tiny from /tmp')
        a('if [ -x "$CFG11_IW" ]; then IW="$CFG11_IW"')
        a('elif command -v iw >/dev/null 2>&1; then IW=$(command -v iw)')
        a('else stop tool-missing "no iw binary (deploy --iw-bin)"; fi')
        # Set the support dir explicitly: the script runs under set -u and
        # LD_LIBRARY_PATH is not guaranteed to exist in the remote environment
        # (the 2026-11-25 D1 fire died on the bare $LD_LIBRARY_PATH expansion,
        # remote rc=2).  No unset-var expansion in the generated script.
        a('export LD_LIBRARY_PATH=%s' % LIBNL_REMOTE_DIR)
        a('say "USERSPACE iw=$IW libnl=%s/%s"'
          % (LIBNL_REMOTE_DIR,
             os.path.basename((cfg11.get("library") or {}).get("remote")
                              or "libnl-tiny.so")))
        a('say "GATE iw-present PASS iw=$IW"')
        a("")
    a("section load")
    a('wlog "HOST ACTION insmod %s"' % module["remote"])
    a('%s 2>>"$R"' % insmod_line)
    a('RC=$?')
    a('if [ "$RC" -ne 0 ]; then fail insmod "insmod rc=$RC"; fi')
    a('grep -q "^%s " /proc/modules || fail insmod-verify "module not in /proc/modules"' % module_name)
    a('say "GATE insmod PASS"')
    a('sleep 1')
    a('dmesg_since "$RDIR/dmesg-load.txt"')
    a("section dmesg-load")
    a('cat "$RDIR/dmesg-load.txt" >> "$R" 2>/dev/null || :')
    a('grep -q "probe" "$RDIR/dmesg-load.txt" 2>/dev/null && say "GATE probe PASS" || say "WARN probe-line-not-found"')
    a('grep -q "HCC service ready" "$RDIR/dmesg-load.txt" 2>/dev/null && say "GATE service-ready PASS" || say "WARN service-ready-line-not-found"')
    for attr in attr_checks:
        a('[ -e "$P/%s" ] || stop module-profile-mismatch "missing attribute %s"' % (attr, attr))
    a('say "GATE module-profile PASS"')
    a('r_attr_as seq-load-h2d_status h2d_status')
    if expected_budget is not None:
        a('gate_attr load-budget h2d_status "budget=%d"' % int(expected_budget))
    a("")
    a("section prepare")
    a('w_attr prepare 1 prepare')
    a('gate_attr prepare-windows status "windows=1"')
    a('say "GATE prepare PASS"')
    a('r_attr_as seq-prepare-status status')
    a("section upload")
    a('w_attr upload 1 upload')
    a('gate_attr upload-loaded status "fw_loaded=1"')
    a('say "GATE upload PASS"')
    a('r_attr_as seq-upload-status status')
    a("section verify")
    a('w_attr verify 1 verify')
    a('gate_attr verify-verified status "fw_verified=1"')
    a('dmesg_since "$RDIR/dmesg-verify.txt"')
    a("section dmesg-verify")
    a('cat "$RDIR/dmesg-verify.txt" >> "$R" 2>/dev/null || :')
    a('gate_grep "$RDIR/dmesg-verify.txt" "diffs=0" verify')
    a('say "GATE verify PASS"')
    a('r_attr_as seq-verify-status status')
    a("section seed")
    a('w_attr seed 1 seed')
    a('gate_attr seed-platform status "platform_seeded=1"')
    a('gate_attr seed-wifi status "wifi_seeded=1"')
    a('gate_attr seed-blocks status "blocks_seeded=1"')
    a('say "GATE seed PASS"')
    a('r_attr_as seq-seed-status status')
    a("section arm")
    a('w_attr hcc_arm 1 hcc_arm')
    a('gate_attr hcc-arm hcc_rings "armed=1"')
    a('say "GATE hcc_arm PASS"')
    a('r_attr_as seq-arm-hcc_rings hcc_rings')
    a("section loop")
    a('w_attr hcc_loop 1 hcc_loop')
    a('dmesg_since "$RDIR/dmesg-loop.txt"')
    a('gate_grep "$RDIR/dmesg-loop.txt" "HCC loop: service thread started" hcc-loop')
    a('say "GATE hcc_loop PASS"')
    a('r_attr_as seq-loop-hcc_status hcc_status')
    a("section release")
    a('wlog "HOST ACTION release 0x5A5A -> CA 0x40000108 (release only, never CECE)"')
    a('w_attr release 1 release')
    a('gate_attr release-done status "cpu_released=1"')
    a('say "GATE release PASS"')
    a('sleep %s' % _fmt_secs(boot_wait))
    a('rd32 hs0 0x877000')
    a('rd32 hs4 0x877004')
    a('rd32 hs8 0x877008')
    a('rd32 hsC 0x87700C')
    a('rd32 state0 0x7D0B60')
    a('rd32 mask0 0x6F8008')
    a('rd32 out1_0 0x3F1014')
    a("section ready")
    a('H1=0; H2=0')
    a('NOW=$(date +%s); D1=$((NOW + ' + str(int(ready_id1_s)) + '))')
    a('while [ "$(date +%s)" -lt "$D1" ]; do')
    a('  if grep -q "id1_seen=1" "$P/hcc_ready" 2>/dev/null; then H1=1; break; fi')
    a('  sleep 1')
    a('done')
    a('say "READY phase=id1 seen=$H1 deadline_s=%d"' % int(ready_id1_s))
    if ready_strict:
        a('if [ "$H1" != "1" ]; then stop ready-id1-timeout "no g4 id1 device_plat_ready within %d s"; fi'
          % int(ready_id1_s))
    a('NOW=$(date +%s); D2=$((NOW + ' + str(int(ready_id2_s)) + '))')
    a('while [ "$(date +%s)" -lt "$D2" ]; do')
    a('  if grep -q "id2_seen=1" "$P/hcc_ready" 2>/dev/null; then H2=1; break; fi')
    a('  sleep 1')
    a('done')
    a('say "READY phase=id2 seen=$H2 deadline_s=%d"' % int(ready_id2_s))
    if ready_strict:
        a('if [ "$H2" != "1" ]; then stop ready-id2-timeout "no g4 id2 DEVICE READY within %d s"; fi'
          % int(ready_id2_s))
    a('say "GATE ready-pair PASS"')
    if require_phase:
        a('gate_attr ready-phase hcc_ready "phase=%s"' % require_phase)
    a('r_attr_as seq-ready-hcc_ready hcc_ready')
    a('r_attr_as seq-ready-hcc_events hcc_events')
    a('r_attr_as seq-ready-rd_status rd_status')
    a("")
    if cfg11:
        a("section cfg80211-ready")
        a("# the wiphy is registered by a workqueue AFTER the ready handshake")
        a('WDEAD=$(($(date +%%s) + %d))' % int(cfg11_ready_s))
        a("WPHY=0")
        a('while [ "$(date +%s)" -lt "$WDEAD" ]; do')
        a('  if ls /sys/class/ieee80211/ >/dev/null 2>&1; then WPHY=1; break; fi')
        a("  sleep 1")
        a("done")
        a('say "WIPHY present=$WPHY deadline_s=%d"' % int(cfg11_ready_s))
        a('if [ "$WPHY" != "1" ]; then stop wiphy-timeout "no /sys/class/ieee80211 phy within %d s of the ready pair"; fi'
          % int(cfg11_ready_s))
        a('say "GATE wiphy PASS"')
        a('say "WIPHY phy=$(ls /sys/class/ieee80211/)"')
        a('r_attr_as seq-cfg80211-ready cfg80211_status')
        a('gate_attr cfg80211-registered cfg80211_status "registered=1"')
        a('say "GATE cfg80211-registered PASS"')
        a('"$IW" phy > "$RDIR/iw-phy.out" 2>&1; IRC=$?')
        a('cat "$RDIR/iw-phy.out" >> "$R" 2>/dev/null || :')
        a('say "SHELL-RC cfg-iw-phy rc=$IRC"')
        a('gate_rc cfg-iw-phy.rc "$IRC" 0')
        a('gate_out_has cfg-iw-phy.p0 "$RDIR/iw-phy.out" "Wiphy"')
        a('say "GATE cfg-iw-phy PASS"')
        a("")
    lines.extend(PS.render_script(seq))
    a("")
    a("section final")
    final_attrs = ["h2d_status", "h2d_plan", "hcc_events", "hcc_ready",
                   "hcc_status", "boot", "rd_status", "rd_catalog",
                   "rd_decode"]
    if cfg11:
        final_attrs.append("cfg80211_status")
    for attr in final_attrs:
        a('r_attr_as seq-final-%s %s' % (attr, attr))
    a('dmesg 2>/dev/null | tail -n 400 > "$RDIR/dmesg-final.txt" 2>/dev/null || :')
    a("section dmesg")
    a('cat "$RDIR/dmesg-final.txt" >> "$R" 2>/dev/null || :')
    a("section writes")
    a('say "WRITES-DEVICE total=$(wc -l < "$W")"')
    a('say "GATE no-cece-default PASS"')
    a('say "SESSION_RESULT: PASS"')
    a("section end")
    a('say "SESSION end=$(date +%s)"')
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# Planning
# ---------------------------------------------------------------------------
@dataclasses.dataclass
class SessionOptions:
    mode: str
    module: str | None = None
    fw_file: str | None = None
    fw_manifest: str | None = None
    fw_path: str | None = None
    plat_block: str | None = None
    wifi_block: str | None = None
    announce: bool = False
    cece_mode: int = 1
    attended: bool = False
    dry_run: bool = True
    report_dir: str | None = None
    endpoint: str = DEFAULT_ENDPOINT
    session_timeout: int = 240
    ready_id1_s: float = 2.0
    ready_id2_s: float = 20.0
    observe_s: float | None = None
    require_ready: bool = False
    allow_other_source: bool = False
    transport: str | None = None
    reboot: str = "none"
    reboot_poll_s: float = 4.0
    reboot_deadline_s: float = 300.0
    skip_preflight: bool = False
    skip_blocks: bool = False
    telnet_host: str = "192.168.10.1"
    telnet_port: int = 23
    ssh_host: str | None = None
    sequence: str | None = None
    wifi_block_bin: str | None = None
    module_params: list | None = None
    bar0read: str | None = None
    cfg80211_ko: str | None = None
    iw_bin: str | None = None
    libnl_tiny: str | None = None
    cfg_ready_s: float = 20.0


def stage_wifi_block(opts, sid: str, guard) -> dict:
    """Stage the Wi-Fi PHY custom block for the driver seed attribute.

    The hi5622_wifi driver's 'seed' attribute reads the PHY block from
    /lib/firmware (wifi_block_path) and writes it to the custom-block window
    itself, so the protocol mode deploys a BINARY block instead of doing the
    devmem pre-seed.  Input: --wifi-block-bin (2472 B, already binary) or
    --wifi-block (the text 'idx=0xvalue' capture, converted here).
    """
    fw_name = "hi_wifi/wifi_block.bin"
    if opts.wifi_block_bin:
        path = os.path.abspath(opts.wifi_block_bin)
        if not os.path.isfile(path):
            raise G.GuardError("wifi-block-missing",
                               "PHY block binary not found: %s" % path)
        size = os.path.getsize(path)
        if size != G.WIFI_BLOCK_BYTES:
            raise G.GuardError("wifi-block-size",
                               "PHY block %s is %d B, expected %d B"
                               % (path, size, G.WIFI_BLOCK_BYTES))
        return {"path": path, "sha256": G.sha256_file(path), "size": size,
                "remote": os.path.join(REMOTE_FW_DIR, fw_name), "fw_name": fw_name,
                "source": "bin"}
    if not opts.wifi_block:
        raise G.GuardError(
            "wifi-block-required",
            "protocol mode needs --wifi-block-bin <2472 B PHY block> (or "
            "--wifi-block <text capture> to convert); the driver seed refuses "
            "to release without it")
    words = load_block_file(opts.wifi_block)
    validate_block(words, G.PHYS_WIFI_BLOCK, G.WIFI_BLOCK_WORDS, "wifi", guard)
    data = b"".join(struct.pack("<I", v) for _, v in words)
    stage = os.path.join(tempfile.gettempdir(), "hi5622-bench-staging", sid)
    os.makedirs(stage, exist_ok=True)
    path = os.path.join(stage, "wifi_block.bin")
    with open(path, "wb") as f:
        f.write(data)
    return {"path": path, "sha256": G.sha256_bytes(data), "size": len(data),
            "remote": os.path.join(REMOTE_FW_DIR, fw_name), "fw_name": fw_name,
            "source": os.path.abspath(opts.wifi_block), "words": len(words)}


def find_bar0read_cc():
    """Find a cross compiler able to build the static ARM read helper.

    Order: $BAR0READ_CC (env), any OpenWrt musl toolchain in the local lab
    trees, then the kernel toolchain.  Returns (cc, staging_dir_or_None).
    """
    env_cc = os.environ.get("BAR0READ_CC")
    if env_cc:
        return env_cc, os.environ.get("STAGING_DIR")
    patterns = [
        "/home/shibbityshwab/openwrt-lab*/staging_dir/toolchain-*/bin/"
        "arm-*-linux-muslgnueabi-gcc",
        os.path.join(ROOT, "build", "linux-kbuild", "toolchain", "bin",
                     "arm-none-linux-gnueabihf-gcc"),
    ]
    for pat in patterns:
        for cc in sorted(glob.glob(pat), reverse=True):
            if not os.access(cc, os.X_OK):
                continue
            # <staging_dir>/toolchain-*/bin/<cc>  (or just a flat path)
            staging = os.path.dirname(os.path.dirname(os.path.dirname(cc)))
            if not staging.endswith("staging_dir"):
                staging = None
            return cc, staging
    return None, None


def build_bar0read(opts: "SessionOptions", warnings: list) -> dict:
    """Build/locate the static read-only BAR0 helper for the protocol mode.

    The host-side build is pure-local (cross gcc -static); the binary is a
    local evidence artifact, deployed to the box by the session and never
    committed.  --bar0read may name a prebuilt helper.
    """
    if opts.bar0read:
        info = G.validate_arm_executable(opts.bar0read)
        return {"path": info.path, "sha256": info.sha256, "size": info.size,
                "remote": HELPER_REMOTE, "source": "explicit"}
    if not os.path.isfile(HELPER_SRC):
        raise G.GuardError("bar0read-src-missing",
                           "read helper source not found: %s" % HELPER_SRC)
    src_sha = G.sha256_file(HELPER_SRC)
    cache_dir = os.path.join(HELPER_CACHE, src_sha[:16])
    cached = os.path.join(cache_dir, "hi5622-bar0read")
    if os.path.isfile(cached):
        try:
            info = G.validate_arm_executable(cached)
            return {"path": info.path, "sha256": info.sha256,
                    "size": info.size, "remote": HELPER_REMOTE,
                    "source": "cache"}
        except G.GuardError:
            pass
    cc, staging = find_bar0read_cc()
    if not cc:
        msg = ("no ARM cross compiler found (set BAR0READ_CC, or pass "
               "--bar0read <prebuilt static ARM helper>)")
        if opts.dry_run:
            warnings.append("read helper NOT built: %s; the dry-run plan is "
                            "incomplete and the real session would refuse" % msg)
            return {"path": None, "sha256": "dry-run-unverified", "size": 0,
                    "remote": HELPER_REMOTE, "source": "unbuilt",
                    "warning": msg}
        raise G.GuardError("bar0read-toolchain", msg)
    os.makedirs(cache_dir, exist_ok=True)
    tmp = cached + ".tmp"
    env = {"STAGING_DIR": staging} if staging else None
    rc, out, err = run_local(
        [cc, "-static", "-O2", "-Wall", "-Wextra", "-o", tmp, HELPER_SRC],
        timeout=180, env=env)
    if rc != 0 or not os.path.isfile(tmp):
        raise G.GuardError(
            "bar0read-build",
            "static ARM helper build failed with %s (rc=%s): %s"
            % (cc, rc, (err or out).strip()[-400:]))
    os.chmod(tmp, 0o755)
    info = G.validate_arm_executable(tmp)
    os.replace(tmp, cached)
    warnings.append("read helper built with %s (%d B sha256 %s)"
                    % (cc, info.size, info.sha256[:16]))
    return {"path": cached, "sha256": info.sha256, "size": info.size,
            "remote": HELPER_REMOTE, "source": "built", "cc": cc}


def stage_cfg80211(opts, sid: str, guard) -> dict:
    """Stage the cfg80211 bench-mode support files (module + iw + libnl-tiny).

    Validates every local artifact before a single byte is deployed: the
    cfg80211.ko must be a GPL ARM module with this kernel's vermagic, iw must be
    a static ARM executable and libnl-tiny a 32-bit ARM shared object.  A real
    session refuses if any is missing; a dry-run records unverified
    placeholders and warns so the plan is visibly incomplete.
    """
    out = {}
    specs = (
        ("cfg80211-module", opts.cfg80211_ko, CFG80211_KO_REMOTE, "module"),
        ("userspace", opts.iw_bin, IW_REMOTE, "iw"),
        ("library", opts.libnl_tiny,
         os.path.join(LIBNL_REMOTE_DIR,
                      os.path.basename(opts.libnl_tiny or "libnl-tiny.so")),
         "libnl"),
    )
    for kind, local, remote, role in specs:
        label = {"module": "cfg80211.ko", "iw": "iw",
                 "libnl": "libnl-tiny"}[role]
        if not local:
            msg = ("cfg80211 mode needs --%s <local artifact> (the image rootfs "
                   "has no %s)" % ({"module": "cfg80211-ko", "iw": "iw-bin",
                                    "libnl": "libnl-tiny"}[role], label))
            if opts.dry_run:
                out[role] = {"path": None, "sha256": "dry-run-unverified",
                             "size": 0, "remote": remote, "role": role,
                             "warning": msg}
                continue
            raise G.GuardError("cfg80211-artifact-missing", msg)
        if not os.path.isfile(local):
            if opts.dry_run:
                out[role] = {"path": os.path.abspath(local),
                             "sha256": "dry-run-unverified", "size": 0,
                             "remote": remote, "role": role,
                             "warning": "artifact %r does not exist; dry-run "
                                        "sha unverified" % local}
                continue
            raise G.GuardError("cfg80211-artifact-missing",
                               "cfg80211 mode artifact not found: %s" % local)
        if role == "module":
            info = G.validate_support_module(local, expected_name="cfg80211")
            out[role] = {"path": info.path, "sha256": info.sha256,
                         "size": info.size, "remote": remote,
                         "vermagic": info.vermagic, "name": info.name,
                         "role": role}
        elif role == "iw":
            info = G.validate_arm_executable(local)
            out[role] = {"path": info.path, "sha256": info.sha256,
                         "size": info.size, "remote": remote, "role": role}
        else:
            info = G.validate_arm_shared_object(local)
            out[role] = {"path": info.path, "sha256": info.sha256,
                         "size": info.size, "remote": remote, "role": role}
    if out.get("library", {}).get("remote") and not out["library"]["remote"].startswith(
            LIBNL_REMOTE_DIR + "/"):
        raise G.GuardError("cfg80211-artifact-path",
                           "libnl-tiny must deploy inside %s" % LIBNL_REMOTE_DIR)
    return out


def make_protocol_plan(opts: SessionOptions, guard) -> SessionPlan:
    """Plan the data-driven protocol-sequence session (PD4 golden boot + H2D)."""
    spec = MODES[opts.mode]
    cfg_mode = opts.mode == "cfg80211"
    sid = G.uuidish_session_id(opts.mode, opts.fw_path or (opts.module or ""))
    warnings = []
    steps = []
    deploy = []

    warnings.append(ssh_host_note(opts))
    seq_path = os.path.abspath(opts.sequence or PS.default_sequence_path())
    seq = PS.load(seq_path)
    if not seq.get("sha256"):
        seq["sha256"] = PS.sha256_file(seq_path)
    req = seq["require"]
    expected_profile = PS.PROFILE_CFG80211 if cfg_mode else PS.PROFILE
    if seq["profile"] != expected_profile:
        raise G.GuardError(
            "sequence-profile",
            "mode %r needs a %r sequence, this one is %r (use --mode %s)"
            % (opts.mode, expected_profile, seq["profile"],
               "cfg80211" if seq["profile"] == PS.PROFILE_CFG80211
               else "protocol"))
    if bool(req.get("cfg80211")) != cfg_mode:
        raise G.GuardError(
            "sequence-cfg80211",
            "sequence require.cfg80211=%s does not match mode %r"
            % (req.get("cfg80211"), opts.mode))

    # ---- module ---------------------------------------------------------
    if not opts.module:
        raise G.GuardError("module-required",
                           "protocol mode needs --module <hi5622_wifi.ko>")
    if os.path.isfile(opts.module):
        info = G.validate_module(opts.module, require_fw_path=True)
        module = {"path": os.path.abspath(opts.module), "name": info.name,
                  "sha256": info.sha256, "size": info.size,
                  "vermagic": info.vermagic, "params": info.params,
                  "remote": os.path.join(REMOTE_MODULE_DIR,
                                         os.path.basename(opts.module))}
    elif opts.dry_run:
        info = None
        module = {"path": os.path.abspath(opts.module), "name": "hi5622_wifi",
                  "sha256": "dry-run-unverified", "size": 0,
                  "vermagic": "6.18.60 SMP mod_unload ARMv7 p2v8",
                  "params": {"fw_path": "charp", "wifi_block_path": "charp",
                             "h2d_send_budget": "uint", "hcc_poll_ms": "uint"},
                  "remote": os.path.join(REMOTE_MODULE_DIR,
                                         os.path.basename(opts.module))}
        warnings.append("module file %r does not exist; dry-run uses a placeholder "
                        "sha (unverified)" % opts.module)
    else:
        raise G.GuardError("module-missing", "module not found: %s" % opts.module)
    if module["name"] not in seq["module_names"]:
        raise G.GuardError(
            "module-profile",
            "sequence %r targets module(s) %s, this module is %r"
            % (seq["name"], seq["module_names"], module["name"]))
    if info is not None and req.get("module_sha256") \
            and module["sha256"] != req["module_sha256"]:
        raise G.GuardError("module-pin-mismatch",
                           "module sha256 %s does not match the sequence pin"
                           % module["sha256"])

    # ---- module capability probe (host-only .ko string scan) -----------
    # Every attr the sequence touches plus require.attrs is a *driver
    # deliverable*: if the target module has no such C string, the phase is
    # BLOCKED, not a bench failure.  This turns a missing dp-bringup/VAP token
    # into an honest plan-time refusal instead of a mid-boot surprise.  It is
    # a capability hint only; the real session still gates on the attribute
    # file existing under the PCI device.
    needed_attrs = set(req.get("attrs") or [])
    for st in seq["steps"]:
        if st["kind"] in ("attr_expect", "attr_write"):
            needed_attrs.add(st["attr"])
    if info is not None and needed_attrs and module["size"] < 4096:
        # Synthetic/selftest fixture (a real kernel module is > 100 KiB): the
        # C-string capability probe is meaningless on a hand-written ELF stub.
        warnings.append("module is only %d B - treated as a synthetic fixture; "
                        "the host-side attribute capability probe is SKIPPED"
                        % module["size"])
    elif info is not None and needed_attrs:
        present = G.module_has_strings(module["path"], sorted(needed_attrs))
        missing = sorted(a for a, ok in present.items() if not ok)
        if missing:
            raise G.GuardError(
                "module-attr-missing",
                "module %s (%d B sha256 %s..) carries no C string for: %s - "
                "this is a driver deliverable gap, not a bench failure; the "
                "sequence JSON is the single edit point when the token lands"
                % (module["name"], module["size"], module["sha256"][:12],
                   ", ".join(missing)))

    # ---- patched firmware (PD4) ----------------------------------------
    if not opts.fw_file or not opts.fw_manifest:
        raise G.GuardError(
            "patched-fw-args",
            "protocol mode needs --fw-file and --fw-manifest (build the PD4 "
            "image with: python3 tools/fw_patch_builder.py --build PD4)")
    if opts.fw_path:
        fw_path = G.validate_fw_path_for_mode(opts.fw_path, patched=True)
    else:
        manifest = G.load_patch_manifest(opts.fw_manifest)
        sha = G.sha256_file(opts.fw_file)
        match = [b for b in manifest["builds"] if b["out_sha256"] == sha]
        if not match:
            raise G.GuardError("fw-not-in-manifest",
                               "%s is not in %s - rebuild/verify first"
                               % (opts.fw_file, opts.fw_manifest))
        fw_path = "hi_wifi/" + match[0]["deploy_name"]
    finfo = G.validate_patched_fw(opts.fw_file, opts.fw_manifest)
    if G.fw_basename(fw_path) != finfo.name:
        raise G.GuardError("fw-path-name-mismatch",
                           "--fw-path basename %r does not match the patched "
                           "file %r" % (G.fw_basename(fw_path), finfo.name))
    fw = {"path": finfo.path, "name": finfo.name, "sha256": finfo.sha256,
          "len": finfo.size, "role": finfo.role, "patched": True,
          "remote": os.path.join(REMOTE_FW_DIR, fw_path)}
    warnings.extend(finfo.warnings)
    if req.get("fw_sha256") and fw["sha256"] != req["fw_sha256"]:
        raise G.GuardError(
            "fw-pin-mismatch",
            "firmware sha256 %s does not match the sequence's pinned PD4 image "
            "%s" % (fw["sha256"], req["fw_sha256"]))
    if req.get("fw_len") and fw["len"] != int(req["fw_len"]):
        raise G.GuardError("fw-pin-mismatch",
                           "firmware length %d does not match the sequence pin %d"
                           % (fw["len"], int(req["fw_len"])))

    # ---- PHY custom block + insmod parameters --------------------------
    wblock = stage_wifi_block(opts, sid, guard)
    wblock.setdefault("label", "wifi")
    wblock["words"] = G.WIFI_BLOCK_WORDS
    wblock["base"] = "0x%08X" % G.PHYS_WIFI_BLOCK
    helper = build_bar0read(opts, warnings)
    params = {}
    module_params = module["params"] or {}
    if "fw_path" in module_params:
        params["fw_path"] = fw_path
    if "wifi_block_path" in module_params:
        params["wifi_block_path"] = wblock["fw_name"]
    for k, v in seq["load_params"].items():
        if k not in module_params:
            raise G.GuardError("module-param-unknown",
                               "sequence load_params[%r] is not a parameter of "
                               "module %r (has: %s)"
                               % (k, module["name"],
                                  ", ".join(sorted(module_params)) or "none"))
        params[k] = str(v)
    for raw in (opts.module_params or []):
        k, sep, v = raw.partition("=")
        k, v = k.strip(), v.strip()
        if not sep or not k or not v:
            raise G.GuardError("module-param-syntax",
                               "--module-param expects KEY=VALUE, got %r" % raw)
        if k not in module_params:
            raise G.GuardError("module-param-unknown",
                               "--module-param %r is not a parameter of module %r "
                               "(has: %s)" % (k, module["name"],
                                              ", ".join(sorted(module_params))
                                              or "none"))
        params[k] = v
    budget = params.get("h2d_send_budget")
    need_budget = int(req.get("budget_min") or 0)
    if need_budget:
        if budget is None:
            raise G.GuardError("budget-required",
                               "sequence needs h2d_send_budget>=%d but the module "
                               "exposes no such parameter" % need_budget)
        try:
            got_budget = int(budget, 0)
        except ValueError:
            raise G.GuardError("budget-invalid",
                               "h2d_send_budget=%r is not an integer" % budget)
        if got_budget < need_budget:
            raise G.GuardError(
                "budget-required",
                "sequence needs h2d_send_budget>=%d (one frame per idea), got %d; "
                "raise it at insmod, never mid-session" % (need_budget, got_budget))
    order = ["fw_path", "wifi_block_path"] + sorted(
        k for k in params if k not in ("fw_path", "wifi_block_path"))
    insmod_params = ["%s=%s" % (k, params[k]) for k in order if k in params]
    expected_budget = params.get("h2d_send_budget")

    # ---- cfg80211 support files (module + iw + libnl-tiny) -------------
    cfg = {"mode": opts.mode, "sid": sid, "endpoint": opts.endpoint,
           "sequence": seq, "module": module, "fw": fw, "wifi_block": wblock,
           "helper": helper, "insmod_params": insmod_params,
           "expected_kernel": "6.18.60",
           "boot_wait_s": 0.4,
           "ready_id1_s": opts.ready_id1_s,
           "ready_id2_s": opts.ready_id2_s,
           "ready_strict": bool(req.get("ready_pair", True)),
           "require_phase": ("READY_TXQ_SYNC" if req.get("ready_pair", True)
                             else None),
           "expected_budget": expected_budget,
           "cfg80211": None}
    support = None
    if cfg_mode:
        if "iw" in (req.get("tools") or []):
            if not opts.iw_bin:
                msg = ("sequence needs iw but no --iw-bin was supplied and the "
                       "image rootfs has no iw")
                if not opts.dry_run:
                    raise G.GuardError("cfg80211-artifact-missing", msg)
                warnings.append(msg + " (dry-run placeholder)")
        support = stage_cfg80211(opts, sid, guard)
        if support.get("cfg80211-module", {}).get("warning"):
            warnings.append(support["cfg80211-module"]["warning"])
        if support.get("iw", {}).get("warning"):
            warnings.append(support["iw"]["warning"])
        if support.get("library", {}).get("warning"):
            warnings.append(support["library"]["warning"])
        cfg["cfg80211"] = {
            "module": support.get("module"),
            "iw": support.get("iw"),
            "library": support.get("libnl"),
            "ready_s": float(opts.cfg_ready_s or 20.0),
        }
    elif req.get("tools") and "iw" in (req.get("tools") or []):
        raise G.GuardError("sequence-cfg80211",
                           "sequence requires iw but mode %r has no cfg80211 "
                           "userspace" % opts.mode)

    # ---- required sysfs attributes -------------------------------------
    attr_checks = ["status", "boot", "prepare", "upload", "verify", "seed",
                   "release", "hcc_arm", "hcc_loop", "hcc_status", "hcc_ready",
                   "hcc_events", "hcc_rings", "rd_status", "rd_catalog",
                   "rd_decode", "h2d_status", "h2d_plan"]
    if cfg_mode:
        attr_checks.append("cfg80211_status")
    for a in req.get("attrs") or []:
        if a not in attr_checks:
            attr_checks.append(a)
    for st in seq["steps"]:
        if st["kind"] == "attr_expect":
            if st["attr"] not in attr_checks:
                attr_checks.append(st["attr"])
        elif st["kind"] == "attr_write":
            if st["attr"] not in attr_checks:
                attr_checks.append(st["attr"])
            rb = st.get("readback")
            if rb and rb not in attr_checks:
                # the post-write gates capture the read-only status attr, not
                # the write-only trigger (which returns EACCES on read)
                attr_checks.append(rb)
    cfg["attr_checks"] = attr_checks
    script = generate_protocol_script(cfg)
    audit = G.audit_device_script(script, profile="hcc")

    # ---- plan steps -----------------------------------------------------
    def add(name, phase, gate, writes=None):
        steps.append(Step(name, phase, gate, writes or []))

    add("host-preflight", "preflight",
        "bench-link PASS, recovery READY, UART capture, TFTP safe, single-session "
        "lock, local module/fw/PHY-block validation, sequence validation, script audit")
    add("readpath", "preflight",
        "static ARM read helper deployed+sha-verified+executable; /dev/mem "
        "BAR0+0 read non-empty (raw register reads work)")
    if opts.reboot == "before":
        add("reboot-before", "preflight", "box rebooted and back (ssh, uptime fresh)")
    add("load", "boot", "insmod with %s; probe dmesg + h2d attributes present"
        % " ".join(insmod_params))
    for name, gate in (("prepare", "six endpoint-iATU windows (status windows=1)"),
                       ("upload", "PD4 firmware uploaded (fw_loaded=1)"),
                       ("verify", "upload verified, dmesg diffs=0"),
                       ("seed", "built-in platform descriptor + PHY block seeded "
                                "(platform_seeded/wifi_seeded/blocks_seeded=1)"),
                       ("hcc_arm", "7-channel ETE arm readback OK (hcc_rings armed=1)"),
                       ("hcc_loop", "service thread started before release"),
                       ("release", "0x5A5A -> CA 0x40000108 ONLY; cpu_released=1")):
        add(name, "boot", gate,
            [PlannedWrite(name, "attr", name, "1",
                          ca=(G.CA_CPU_RELEASE if name == "release" else None),
                          rationale=gate)])
    add("ready-pair", "observe",
        "LATCHED golden page, state 1, mask 0; g4 id1 <=%.1f s, g4 id2 <=%.1f s; "
        "phase READY_TXQ_SYNC" % (opts.ready_id1_s, opts.ready_id2_s))
    if cfg_mode:
        add("cfg80211-load", "boot",
            "cfg80211.ko loaded (or already built-in) BEFORE hi5622_wifi; "
            "/proc/modules reports cfg80211 + hi5622_wifi",
            [PlannedWrite("cfg80211-load", "insmod",
                          CFG80211_KO_REMOTE, "load",
                          rationale="cfg80211 must be present before the "
                                    "instrument's probe (module order gate)")])
        add("wiphy-register", "observe",
            "/sys/class/ieee80211/phy* appears within %.0f s of the ready pair; "
            "iw phy enumerates it (wiphy registration gate)" % opts.cfg_ready_s,
            [PlannedWrite("wiphy-register", "host-run", "iw", "phy",
                          rationale="read-only wiphy enumeration")])
    for st in seq["steps"]:
        writes = []
        if st["kind"] == "attr_write":
            writes = [PlannedWrite("seq-" + st["name"], "attr", st["attr"],
                                   st["value"], rationale=st.get("about", ""))]
        elif st["kind"] == "shell":
            writes = [PlannedWrite("seq-" + st["name"], "shell",
                                   PS.shell_command_text(st["cmd"], st["args"]),
                                   "", rationale=st.get("about", ""))]
        add("seq-" + st["name"], "protocol",
            "per-step gates: %s" % (", ".join(
                PS.check_names({"steps": [st]})) or "capture only"), writes)
    add("observe", "observe",
        "final h2d_status/h2d_plan/hcc_events/hcc_ready/hcc_status/boot/"
        "rd_status/rd_catalog/rd_decode + dmesg + writes log")

    if helper.get("path"):
        deploy.append({"kind": "tool", "local": helper["path"],
                       "remote": helper["remote"], "sha256": helper["sha256"],
                       "size": helper["size"]})
    deploy.append({"kind": "module", "local": module["path"],
                   "remote": module["remote"], "sha256": module["sha256"],
                   "size": module["size"]})
    deploy.append({"kind": "firmware", "local": fw["path"], "remote": fw["remote"],
                   "sha256": fw["sha256"], "size": fw["len"]})
    deploy.append({"kind": "wifi-block", "local": wblock["path"],
                   "remote": wblock["remote"], "sha256": wblock["sha256"],
                   "size": wblock["size"]})
    if cfg_mode and support:
        for role, kind in (("module", "support-module"),
                           ("iw", "userspace"), ("libnl", "library")):
            art = support.get(role)
            if art:
                deploy.append({"kind": kind, "local": art.get("path"),
                               "remote": art["remote"],
                               "sha256": art["sha256"],
                               "size": art["size"]})

    for step in steps:
        for w in step.writes:
            if w.kind == "attr":
                guard.check_attr_write(w.target, w.value, step=w.step)
    G.validate_fw_path(fw["remote"][len(REMOTE_FW_DIR) + 1:])
    if audit["violations"]:
        warnings.append("script audit FAILED: %s"
                        % json.dumps(audit["violations"][:5]))
    else:
        warnings.append("script audit PASS")
    if not req.get("ready_pair", True):
        warnings.append("sequence disables the strict ready-pair gate; the "
                        "protocol steps assume READY - review before use")

    return SessionPlan(
        mode=opts.mode, session_id=sid, spec=spec, steps=steps,
        module=module, fw=fw, blocks=[wblock], deploy=deploy, script=script,
        audit=audit, warnings=warnings,
        opts={"endpoint": opts.endpoint, "announce": False,
              "cece_mode": opts.cece_mode,
              "ready_mode": "strict" if req.get("ready_pair", True) else "informational",
              "ready_id1_s": opts.ready_id1_s,
              "ready_id2_s": opts.ready_id2_s,
              "observe_s": 0.0, "reboot": opts.reboot,
              "transport": spec.transport,
              "sequence": seq["name"], "sequence_sha256": seq["sha256"],
              "sequence_path": seq_path,
              "insmod_params": insmod_params,
              "module_params": {k: params[k] for k in sorted(params)},
              "cfg80211": ({
                  "module": support["module"],
                  "iw": support["iw"],
                  "library": support["libnl"],
                  "ready_s": float(opts.cfg_ready_s or 20.0),
              } if cfg_mode and support else None)},
        sequence=seq)


def make_plan(opts: SessionOptions, guard) -> SessionPlan:
    if opts.mode in ("protocol", "cfg80211"):
        return make_protocol_plan(opts, guard)
    spec = MODES[opts.mode]
    sid = G.uuidish_session_id(opts.mode, opts.fw_path or (opts.module or ""))
    warnings = []
    steps = []
    deploy = []
    if spec.transport == "ssh":
        warnings.append(ssh_host_note(opts))
    cfg = {
        "mode": opts.mode, "sid": sid, "endpoint": opts.endpoint,
        "strict_boot_gate": spec.strict_boot_gate,
        "announce": bool(spec.announce and opts.announce),
        "cece_mode": opts.cece_mode,
        "boot_wait_s": 0.4,
        "ready_id1_s": opts.ready_id1_s,
        "ready_id2_s": opts.ready_id2_s,
        "observe_s": opts.observe_s if opts.observe_s is not None else spec.observe_s,
        "attr_checks": ["status", "prepare", "upload", "verify", "release"],
        "attr_reads": list(OBSERVED_ATTRS),
        "profile": "hcc", "vendor_modules_expected": False,
        "expected_kernel": "6.18.60", "insmod_params": [],
    }
    if opts.cece_mode != 1:
        warnings.append("cece_mode=%d: CECE writes are enabled only after a real +8==2 "
                        "latch; default cece_mode=1 (never) is the post-GP8 policy"
                        % opts.cece_mode)

    module = None
    if spec.uses_module:
        if opts.module:
            if os.path.isfile(opts.module):
                info = G.validate_module(opts.module, require_fw_path=(spec.fw_role == "patched"))
                module = {"path": os.path.abspath(opts.module), "name": info.name,
                          "sha256": info.sha256, "size": info.size,
                          "vermagic": info.vermagic, "params": info.params,
                          "remote": os.path.join(REMOTE_MODULE_DIR, os.path.basename(opts.module))}
                if "fw_path" in info.params:
                    cfg["insmod_params"].append(
                        "fw_path=%s" % ("hi_wifi/FIRMWARE.bin"
                                        if spec.fw_role == "vendor" else
                                        (opts.fw_path or "")))
                if "poll_ms" in info.params:
                    cfg["insmod_params"].append("poll_ms=0")
            elif opts.dry_run:
                module = {"path": os.path.abspath(opts.module), "name": "hi5622_pciet_hcc",
                          "sha256": "dry-run-unverified", "size": 0,
                          "vermagic": "6.18.60 SMP mod_unload ARMv7 p2v8",
                          "params": {"fw_path": "charp", "poll_ms": "uint"},
                          "remote": os.path.join(REMOTE_MODULE_DIR, os.path.basename(opts.module))}
                warnings.append("module file %r does not exist; dry-run uses a placeholder "
                                "(sha256 unverified)" % opts.module)
            else:
                raise G.GuardError("module-missing", "module not found: %s" % opts.module)
        else:
            raise G.GuardError("module-required", "mode %r needs --module <ko>" % opts.mode)

    fw = None
    if spec.fw_role != "none":
        if spec.fw_role == "vendor":
            fw_path = G.validate_fw_path_for_mode(
                opts.fw_path or "hi_wifi/FIRMWARE.bin", patched=False)
            local_fw = opts.fw_file or G.DEFAULT_FW
            if os.path.isfile(local_fw):
                info = G.validate_vendor_fw(local_fw, allow_other_source=opts.allow_other_source)
                warnings.extend(info.warnings)
                fw = {"path": os.path.abspath(local_fw), "name": info.name,
                      "sha256": info.sha256, "len": info.size, "role": info.role,
                      "remote": os.path.join(REMOTE_FW_DIR, fw_path),
                      "deploy": bool(opts.fw_file)}
            elif opts.dry_run:
                fw = {"path": os.path.abspath(local_fw), "name": "FIRMWARE.bin",
                      "sha256": "dry-run-unverified", "len": 928920,
                      "role": "unverified-allowed",
                      "remote": os.path.join(REMOTE_FW_DIR, fw_path), "deploy": False}
                warnings.append("firmware blob %r does not exist; dry-run uses a placeholder "
                                "sha (unverified)" % local_fw)
            else:
                raise G.GuardError("fw-missing", "firmware blob not found: %s (pass --fw-file)"
                                   % local_fw)
        else:
            if not opts.fw_file or not opts.fw_manifest:
                raise G.GuardError("patched-fw-args",
                                   "patched mode needs --fw-file and --fw-manifest (build with "
                                   "tools/fw_patch_builder.py)")
            if opts.fw_path:
                fw_path = G.validate_fw_path_for_mode(opts.fw_path, patched=True)
            else:
                manifest = G.load_patch_manifest(opts.fw_manifest)
                sha = G.sha256_file(opts.fw_file)
                match = [b for b in manifest["builds"] if b["out_sha256"] == sha]
                if not match:
                    raise G.GuardError("fw-not-in-manifest",
                                       "%s is not in %s - rebuild/verify first"
                                       % (opts.fw_file, opts.fw_manifest))
                fw_path = "hi_wifi/" + match[0]["deploy_name"]
            info = G.validate_patched_fw(opts.fw_file, opts.fw_manifest)
            if G.fw_basename(fw_path) != info.name:
                raise G.GuardError("fw-path-name-mismatch",
                                   "--fw-path basename %r does not match the patched file %r"
                                   % (G.fw_basename(fw_path), info.name))
            fw = {"path": info.path, "name": info.name, "sha256": info.sha256,
                  "len": info.size, "role": info.role,
                  "remote": os.path.join(REMOTE_FW_DIR, fw_path), "deploy": True,
                  "patched": True}
            warnings.extend(info.warnings)
        cfg["fw_path"] = fw_path

    blocks = []
    plat = wifi = None
    if spec.needs_blocks:
        if opts.skip_blocks:
            warnings.append("--skip-blocks: custom-block pre-seed is SKIPPED (degraded "
                            "session; S1/CPU1 boot is not expected to complete)")
        else:
            if not opts.plat_block or not opts.wifi_block:
                raise G.GuardError("blocks-required",
                                   "mode %r needs --plat-block and --wifi-block (local evidence "
                                   "files from the vendor golden state; or pass --skip-blocks "
                                   "for a deliberately degraded run)" % opts.mode)
            for label, path, base, expected in (
                    ("plat", opts.plat_block, G.PHYS_PLATFORM_BLOCK, G.PLATFORM_BLOCK_WORDS),
                    ("wifi", opts.wifi_block, G.PHYS_WIFI_BLOCK, G.WIFI_BLOCK_WORDS)):
                words = load_block_file(path)
                meta = validate_block(words, base, expected, label, guard)
                meta["path"] = os.path.abspath(path)
                # the sha256 pinned in deploy/read-back must hash the FILE bytes
                # (the on-box sha256sum reads the deployed file, not our model)
                meta["sha256"] = G.sha256_file(meta["path"])
                meta["base_int"] = base
                meta["remote"] = os.path.join(REMOTE_BASE, sid, "%s_block.txt" % label)
                blocks.append(meta)
                if label == "plat":
                    plat = meta
                else:
                    wifi = meta
    cfg["plat"] = plat
    cfg["wifi"] = wifi
    cfg["module"] = module
    cfg["fw"] = fw
    if fw and fw.get("patched"):
        cfg["insmod_params"] = [p for p in cfg["insmod_params"]
                                if not p.startswith("fw_path=")]
        if module and "fw_path" in (module.get("params") or {}):
            cfg["insmod_params"].insert(0, "fw_path=%s" % cfg["fw_path"])
    if spec.announce and opts.announce:
        cfg["ready_mode"] = "announce"
    elif spec.uses_module and spec.strict_boot_gate and opts.require_ready:
        cfg["ready_mode"] = "strict"
    elif spec.uses_module and spec.strict_boot_gate and spec.require_ready_for_announce:
        cfg["ready_mode"] = "strict"
    else:
        cfg["ready_mode"] = "informational"
    cfg["attr_reads"] = list(OBSERVED_ATTRS) if spec.uses_module else []

    script = generate_device_script(cfg)
    audit = G.audit_device_script(script, profile="hcc")

    def add(name, phase, gate, writes=None):
        steps.append(Step(name, phase, gate, writes or []))

    add("host-preflight", "preflight",
        "bench-link PASS, recovery READY, UART capture, TFTP safe, single-session "
        "lock, local module/fw/block validation, script audit")
    if opts.reboot == "before":
        add("reboot-before", "preflight", "box rebooted and back (ssh, uptime fresh)")
    if spec.uses_module:
        for attr in ("prepare", "upload", "verify"):
            add(attr, "boot", "status/%s gate" % attr,
                [PlannedWrite(attr, "attr", attr, "1",
                              rationale="base transport step")])
    if plat:
        add("seed-platform-block", "boot", "24 words written+readback",
            [PlannedWrite("seed-platform-block", "devmem",
                          "0x%08X" % (plat["base_int"] + idx * 4), "0x%08X" % v,
                          ca=plat["base_int"] + idx * 4)
             for idx, v in load_block_file(plat["path"])])
    if wifi:
        add("seed-wifi-block", "boot", "618 words written+readback",
            [PlannedWrite("seed-wifi-block", "devmem",
                          "0x%08X" % (wifi["base_int"] + idx * 4), "0x%08X" % v,
                          ca=wifi["base_int"] + idx * 4)
             for idx, v in load_block_file(wifi["path"])])
    if spec.uses_module:
        add("hcc_arm", "boot", "7/7 ETE ring readback OK",
            [PlannedWrite("hcc_arm", "attr", "hcc_arm", "1")])
        add("hcc_loop", "boot", "service thread running before release",
            [PlannedWrite("hcc_loop", "attr", "hcc_loop", "1")])
        add("release", "boot", "0x5A5A -> CA 0x40000108 ONLY; golden page +8==2",
            [PlannedWrite("release", "attr", "release", "1", ca=G.CA_CPU_RELEASE,
                          rationale="CPU release magic 0x5A5A (never CECE)")])
        add("ready-pair", "observe", "g4 id1 <=2.0 s, g4 id2 <=20.0 s (strict/announce)")
        add("observe", "observe", "structured page/state/mask/out0-5/rd_catalog/"
                                   "hcc_events/dmesg dump")
    if cfg["announce"]:
        add("h2d-announce", "observe",
            "exactly one hcc_sr_announce; consumed or lane closed, no retry",
            [PlannedWrite("h2d-announce", "attr", "hcc_sr_announce", "1",
                          rationale="single safe H2D liveness frame (no retry)")])

    if module:
        deploy.append({"kind": "module", "local": module["path"],
                       "remote": module["remote"], "sha256": module["sha256"],
                       "size": module["size"]})
    if fw and fw.get("deploy"):
        deploy.append({"kind": "firmware", "local": fw["path"], "remote": fw["remote"],
                       "sha256": fw["sha256"], "size": fw["len"]})
    for blk in blocks:
        deploy.append({"kind": "block", "local": blk["path"], "remote": blk["remote"],
                       "sha256": blk["sha256"], "words": blk["words"]})

    for step in steps:
        for w in step.writes:
            if w.kind == "attr":
                guard.check_attr_write(w.target, w.value, step=w.step)
            elif w.kind == "devmem" and w.ca is not None:
                guard.check_devmem_write(w.ca, norm_int(w.value), step=w.step)
    for d in deploy:
        if d["kind"] == "firmware":
            G.validate_fw_path(d["remote"][len(REMOTE_FW_DIR) + 1:])
    if audit["violations"]:
        warnings.append("script audit FAILED: %s"
                        % json.dumps(audit["violations"][:5]))
    else:
        warnings.append("script audit PASS")

    return SessionPlan(mode=opts.mode, session_id=sid, spec=spec, steps=steps,
                       module=module, fw=fw, blocks=blocks, deploy=deploy,
                       script=script, audit=audit, warnings=warnings,
                       opts={"endpoint": opts.endpoint,
                             "announce": cfg["announce"],
                             "cece_mode": opts.cece_mode,
                             "ready_mode": cfg["ready_mode"],
                             "ready_id1_s": opts.ready_id1_s,
                             "ready_id2_s": opts.ready_id2_s,
                             "observe_s": cfg["observe_s"],
                             "reboot": opts.reboot,
                             "transport": spec.transport})


# ---------------------------------------------------------------------------
# Host preflight
# ---------------------------------------------------------------------------
@dataclasses.dataclass
class Check:
    name: str
    ok: bool
    required: bool
    detail: str = ""

    def line(self) -> str:
        status = "PASS" if self.ok else ("FAIL" if self.required else "WARN")
        return "  %-4s  %-24s %s" % (status, self.name, self.detail)


def _pgrep(pattern: str) -> list:
    rc, out, _ = run_local(["pgrep", "-af", pattern], timeout=10)
    if rc not in (0, 1):
        return []
    return [ln for ln in out.splitlines() if ln.strip()]


def host_preflight(opts: SessionOptions, spec: ModeSpec) -> list:
    checks = []
    if opts.skip_preflight:
        checks.append(Check("preflight", True, False,
                            "skipped by --skip-preflight (testing only)"))
        return checks
    rc, out, err = run_local([os.path.join(ROOT, "tools", "bench-link.sh")], timeout=60)
    detail = "PASS" if rc == 0 else (out.strip().splitlines()[-1] if out.strip()
                                     else err.strip()[:120])
    checks.append(Check("bench-link", rc == 0, True, detail))
    rc, out, _ = run_local([os.path.join(ROOT, "tools", "recovery-card.sh")], timeout=120)
    checks.append(Check("recovery-door", rc == 0, not spec.read_only,
                        "READY" if rc == 0 else "NOT READY (tools/recovery-card.sh)"))
    caps = G.detect_uart_captures(os.path.join(ROOT, "build", "uart-logs"))
    checks.append(Check("uart-capture", bool(caps), not spec.read_only,
                        "; ".join(caps) if caps else
                        "no live read-only capture (tools/uart-capture.sh start --label <label>)"))
    procs = _pgrep("tftp_server")
    ok_tftp, problems, notes = G.check_tftp_processes(procs)
    checks.append(Check("tftp-state", ok_tftp, True,
                        "; ".join(problems) if problems
                        else ("; ".join(notes) or "no TFTP server running")))
    others = []
    for p in _pgrep("bench_session[.]py"):
        parts = p.split()
        if parts and parts[0] != str(os.getpid()):
            others.append(p)
    checks.append(Check("single-session", not others, True,
                        "another bench session is running: %s" % others if others else "none"))
    checks.append(Check("attended", opts.attended, spec.needs_attended,
                        "operator marked the bench attended" if opts.attended else
                        "not --attended: recovery needs a human at the bench"))
    if (opts.transport or spec.transport) == "ssh":
        checks.append(Check("ssh-host", True, False, ssh_host_note(opts)))
    base, is_project = evidence_base()
    report_dir = opts.report_dir or os.path.join(base, G.uuidish_session_id(opts.mode))
    ok_ev, why = G.gitignored_evidence_ok(report_dir)
    if not is_project:
        ok_ev = False
        why = "no writable project evidence dir; fallback %s is not project evidence" % report_dir
    checks.append(Check("evidence-dir", ok_ev, True, why))
    return checks


# ---------------------------------------------------------------------------
# Transports
# ---------------------------------------------------------------------------
DEFAULT_ROUTER_HOST = "root@192.168.10.1"   # stock vendor LAN; our image is .1.1


def normalize_router_host(host: str) -> str:
    """A bare host/IP means root@host (both LANs are root-only over dropbear)."""
    host = (host or "").strip()
    if host and "@" not in host:
        return "root@" + host
    return host


def resolve_router_host(host=None) -> str:
    """--ssh-host > ROUTER_HOST env > stock default (root@192.168.10.1).

    A bare --ssh-host 192.168.1.1 is normalised to root@192.168.1.1 so the
    documented one-liner works; our image normally answers at 192.168.1.1.
    """
    return normalize_router_host(
        host or os.environ.get("ROUTER_HOST") or DEFAULT_ROUTER_HOST)


class SshTransport:
    """SSH transport via tools/rsh.sh (dropbear; NO legacy scp).

    run/run_script/put/get all go through tools/rsh.sh with ROUTER_HOST pinned
    to self.host, so --ssh-host actually takes effect (the stock default is
    root@192.168.10.1; our own image normally answers at 192.168.1.1).  File
    transfer is the project convention: put = cat local | rsh 'cat > remote',
    get = rsh 'cat remote' > local - the lab dropbear rejects scp -O.
    """
    name = "ssh"

    def __init__(self, host=None):
        self.host = resolve_router_host(host)
        self.rsh = os.path.join(ROOT, "tools", "rsh.sh")

    def _env(self) -> dict:
        return {"ROUTER_HOST": self.host}

    def run(self, command: str, timeout: float = 60):
        return run_local([self.rsh, command], timeout=timeout, env=self._env())

    def run_script(self, script: str, timeout: float = 60):
        fd, path = tempfile.mkstemp(prefix="bench-script-", suffix=".sh")
        try:
            with os.fdopen(fd, "w") as f:
                f.write(script)
            return self.run("sh %s" % shlex.quote(path), timeout=timeout)
        finally:
            try:
                os.unlink(path)
            except OSError:
                pass

    def put(self, local: str, remote: str):
        try:
            with open(local, "rb") as f:
                data = f.read()
        except OSError as e:
            return 1, "", "cannot read local file %s: %s" % (local, e)
        rc, out, err = run_local_capture(
            [self.rsh, "cat > %s" % shlex.quote(remote)],
            timeout=300, env=self._env(), input_bytes=data)
        return rc, out.decode("utf-8", "replace"), err.decode("utf-8", "replace")

    def get(self, remote: str, local: str):
        rc, out, err = run_local_capture(
            [self.rsh, "cat %s" % shlex.quote(remote)],
            timeout=300, env=self._env())
        if rc != 0:
            return rc, out.decode("utf-8", "replace"), err.decode("utf-8", "replace")
        try:
            with open(local, "wb") as f:
                f.write(out)
        except OSError as e:
            return 1, "", "cannot write local file %s: %s" % (local, e)
        return 0, out.decode("utf-8", "replace"), err.decode("utf-8", "replace")


def ssh_host_note(opts: "SessionOptions") -> str:
    """Plan-time note: which router host the SSH transport will target."""
    env_host = os.environ.get("ROUTER_HOST")
    host = resolve_router_host(opts.ssh_host)
    how = ("--ssh-host" if opts.ssh_host else
           ("ROUTER_HOST" if env_host else "default (stock vendor LAN)"))
    note = ("SSH transport target %s via tools/rsh.sh (%s); the lab dropbear "
            "rejects legacy scp - put/get use cat|ssh" % (host, how))
    if host == DEFAULT_ROUTER_HOST and not opts.ssh_host and not env_host:
        note += ("; OUR image normally answers at root@192.168.1.1 -> set "
                 "ROUTER_HOST=root@192.168.1.1 or --ssh-host root@192.168.1.1")
    return note


class TelnetTransport:
    name = "telnet"

    def __init__(self, host: str = "192.168.10.1", port: int = 23):
        self.host = host
        self.port = port

    def run(self, command: str, timeout: float = 60):
        return run_local([sys.executable, os.path.join(ROOT, "tools", "telnet-exec.py"),
                          "--host", self.host, "--port", str(self.port),
                          "--timeout", str(int(timeout)), "--", command],
                         timeout=timeout + 10)

    def run_script(self, script: str, timeout: float = 60):
        lines = [ln for ln in script.splitlines()
                 if ln.strip() and not ln.strip().startswith("#")]
        return run_local([sys.executable, os.path.join(ROOT, "tools", "telnet-exec.py"),
                          "--host", self.host, "--port", str(self.port),
                          "--timeout", str(int(timeout)), "--"] + lines,
                         timeout=timeout + 10)

    def put(self, local: str, remote: str):
        return 1, "", "telnet transport cannot copy files (read-only stock mode only)"

    def get(self, remote: str, local: str):
        return 1, "", "telnet transport cannot copy files"


class DryTransport:
    name = "dry-run"

    def _refuse(self, *a, **k):
        raise RuntimeError("dry-run must never contact the device")

    run = run_script = put = get = _refuse


def make_transport(opts: SessionOptions, spec: ModeSpec):
    if opts.dry_run:
        return DryTransport()
    kind = opts.transport or spec.transport
    if kind == "telnet":
        return TelnetTransport(opts.telnet_host, opts.telnet_port)
    return SshTransport(opts.ssh_host)


# ---------------------------------------------------------------------------
# Report parsing
# ---------------------------------------------------------------------------
PAGE_RE = re.compile(r"^PAGE t=(\S+) w=0x([0-9a-fA-F]+) 0x([0-9a-fA-F]+) "
                     r"0x([0-9a-fA-F]+) 0x([0-9a-fA-F]+) ack2=([01])$")
RD32_RE = re.compile(r"^RD32 label=(\S+) bar0=0x([0-9a-fA-F]+) v=0x([0-9a-fA-F]*)$")
GATE_RE = re.compile(r"^GATE (\S+) (PASS|FAIL)")
GATE_FAIL_RE = re.compile(r"^GATE-FAIL name=(\S+)$")
READY_RE = re.compile(r"^READY phase=(\S+) seen=([01])")
BLOCK_RE = re.compile(r"^BLOCK label=(\S+) words=(\d+) readback_mismatch=(\d+)")
H2D_RE = re.compile(r"^H2D attempt=(\d+) result=(\w+)")
RESULT_RE = re.compile(r"^SESSION_RESULT: (\w+)(?::(\S+))?")
STOP_RE = re.compile(r"^STOP:(\S+): (.*)$")


def parse_report(text: str) -> dict:
    sections = {}
    cur = "_preamble"
    pages = []
    rd32 = {}
    gates = []
    reads = {}
    blocks = []
    h2d = {"attempted": 0, "result": None}
    result = None
    stop_rule = None
    current_section = None
    for raw in text.splitlines():
        line = raw.rstrip()
        m = re.match(r"^== SECTION (.+)$", line)
        if m:
            current_section = m.group(1).strip().lower().replace(" ", "-")
            sections.setdefault(current_section, [])
            cur = current_section
            continue
        sections.setdefault(cur, []).append(line)
        if current_section:
            reads[current_section] = reads.get(current_section, "") + line + "\n"
        if not line.strip():
            continue
        m = PAGE_RE.match(line)
        if m:
            pages.append({"t": m.group(1),
                          "words": [int(m.group(i), 16) for i in (2, 3, 4, 5)],
                          "ack2": m.group(6) == "1"})
            continue
        m = RD32_RE.match(line)
        if m:
            # An EMPTY value (v=0x) is kept as None on purpose: the device's
            # raw-read path failed, and the host evaluator reports that as
            # device-read-empty instead of silently treating it as absent.
            rd32[m.group(1)] = int(m.group(3), 16) if m.group(3) else None
            continue
        m = GATE_RE.match(line)
        if m:
            gates.append({"name": m.group(1), "ok": m.group(2) == "PASS"})
            continue
        m = GATE_FAIL_RE.match(line)
        if m:
            gates.append({"name": m.group(1), "ok": False})
            continue
        m = READY_RE.match(line)
        if m:
            h2d.setdefault("ready", {})[m.group(1)] = m.group(2) == "1"
            continue
        m = BLOCK_RE.match(line)
        if m:
            blocks.append({"label": m.group(1), "words": int(m.group(2)),
                           "readback_mismatch": int(m.group(3))})
            continue
        m = H2D_RE.match(line)
        if m:
            h2d["attempted"] = int(m.group(1))
            h2d["result"] = m.group(2)
            continue
        m = RESULT_RE.match(line)
        if m:
            result = m.group(1)
            stop_rule = m.group(2)
            continue
        m = STOP_RE.match(line)
        if m:
            stop_rule = stop_rule or m.group(1)
            continue
    return _summarise(text, sections, reads, pages, rd32, gates, blocks, h2d,
                      result, stop_rule)


def _grab(text: str, pattern: str):
    m = re.search(pattern, text)
    return m.group(1) if m else None


def _summarise(text, sections, reads, pages, rd32, gates, blocks, h2d,
               result, stop_rule) -> dict:
    hcc_status = reads.get("hcc_status", "")
    hcc_ready = reads.get("hcc_ready", "")
    hcc_events = reads.get("hcc_events", "")
    rd_catalog = reads.get("rd_catalog", "")
    rd_status = reads.get("rd_status", "")
    rd_decode = reads.get("rd_decode", "")
    page = {"words": None, "ack2": False, "source": None}
    canon = re.search(
        r"handshake_page CA 0x1bf000=0x([0-9a-f]+)/0x([0-9a-f]+)/0x([0-9a-f]+)/"
        r"0x([0-9a-f]+) \(\+8==2:([01])\)", hcc_status)
    if canon:
        page = {"words": [int(canon.group(i), 16) for i in (1, 2, 3, 4)],
                "ack2": canon.group(5) == "1", "source": "hcc_status"}
    elif pages:
        page = {"words": pages[-1]["words"], "ack2": pages[-1]["ack2"],
                "source": "PAGE t=%s" % pages[-1]["t"]}
    state = _int_or_none(_grab(hcc_status, r"state CA 0x118b60=0x([0-9a-f]+)"))
    mask = _int_or_none(_grab(hcc_status, r"mask CA 0x40008=0x([0-9a-f]+)"))
    boot_slot = _int_or_none(_grab(hcc_status, r"boot_slot CA 0x17d42c=0x([0-9a-f]+)"))
    out = {}
    mo = re.search(r"out0=0x([0-9a-f]+) out1=0x([0-9a-f]+) out2=0x([0-9a-f]+) "
                   r"out3\(ack,read\)=0x([0-9a-f]+) out4\(rearm,read\)=0x([0-9a-f]+) "
                   r"out5\(READ-ONLY\)=0x([0-9a-f]+)", hcc_status)
    if mo:
        out = {"out0": int(mo.group(1), 16), "out1": int(mo.group(2), 16),
               "out2": int(mo.group(3), 16), "out3": int(mo.group(4), 16),
               "out4": int(mo.group(5), 16), "out5_read": int(mo.group(6), 16)}
    else:
        for k in ("out0", "out1", "out2"):
            if k in rd32:
                out[k] = rd32[k]
        if "out5ro" in rd32:
            out["out5_read"] = rd32["out5ro"]
    ready = {
        "id1_seen": _int_or_none(_grab(hcc_ready, r"id1_seen=(\d)")),
        "id2_seen": _int_or_none(_grab(hcc_ready, r"id2_seen=(\d)")),
        "id1_count": _dec_or_none(_grab(hcc_ready, r"id1_seen=\d+ count=(\d+)")),
        "id2_count": _dec_or_none(_grab(hcc_ready, r"id2_seen=\d+ count=(\d+)")),
        "id2_payload": _grab(hcc_ready, r"id2_seen=\d+ count=\d+ payload=(\S+)"),
        "plat_timeout": _dec_or_none(_grab(hcc_ready, r"plat_timeout=(\d+)")),
        "dev_timeout": _dec_or_none(_grab(hcc_ready, r"dev_timeout=(\d+)")),
        "direct_line": bool(re.search(r"DIRECT\(device group-A\)", hcc_ready)),
        "raw": hcc_ready.strip(),
    }
    layouts = re.findall(r"layout=(\w+)", rd_status)
    cat = {
        "id1_seen": _int_or_none(_grab(rd_catalog, r"id1_seen=(\d)")),
        "id2_seen": _int_or_none(_grab(rd_catalog, r"id2_seen=(\d)")),
        "frames": _dec_or_none(_grab(rd_catalog, r"frames=(\d+)")),
        "excp_info": _dec_or_none(_grab(rd_catalog, r"excp_info=(\d+)")),
        "raw_first": "\n".join(rd_catalog.splitlines()[:6]),
    }
    summ = {
        "result": result, "stop_rule": stop_rule, "page": page,
        "rd32": rd32,
        "page_golden": bool(page["words"] and page["words"][2] == G.HANDSHAKE_ACK
                            and (page["words"][3] & 0xFFFF) == 0xABCD),
        "page_ack2": bool(page["ack2"]),
        "page_latched": "PAGE-LATCH" in text or bool(page["ack2"]),
        "state": state, "mask": mask,
        "mask_bits": ([name for bit, name in sorted(GFX_EXCP_BITS.items())
                       if mask is not None and (mask & bit)]
                      if mask is not None else []),
        "boot_slot": boot_slot, "out": out,
        "g_armed": _int_or_none(_grab(hcc_status, r"armed\(g\+0xb4 CA 0x10c1a8\)=0x([0-9a-f]+)")),
        "g_pending": _int_or_none(_grab(hcc_status, r"pending\(g\+0xb8 CA 0x10c1ac\)=0x([0-9a-f]+)")),
        "ete_status": _int_or_none(_grab(hcc_status, r"status CA 0x4003a874=0x([0-9a-f]+)")),
        "cece_writes": _dec_or_none(_grab(hcc_status, r"cece policy \(corrected\): writes=(\d+)")),
        "cece_blocked_boot": _dec_or_none(_grab(hcc_status, r"blocked_in_boot=(\d+)")),
        "ready": ready, "rd_catalog": cat,
        "rd_status_layout": ("OK" if layouts and all(x == "OK" for x in layouts)
                             else ("MISMATCH" if layouts else None)),
        "rd_overruns": _dec_or_none(_grab(rd_status, r"overruns=(\d+)")),
        "blocks": blocks, "h2d": h2d, "gates": gates,
        "gates_failed": [g["name"] for g in gates if not g["ok"]],
        "sections": sorted(sections), "reads": reads,
        "dmesg": reads.get("dmesg", ""), "hcc_events": hcc_events,
        "rd_decode": rd_decode,
        "writes_device": _dec_or_none(_grab(text, r"WRITES-DEVICE total=(\d+)")),
    }
    summ["classify"] = classify_observations(summ, reads)
    return summ


def _int_or_none(v, base: int = 16):
    if v is None:
        return None
    if isinstance(v, str):
        return int(v, base)
    return int(v)


def _dec_or_none(v):
    return _int_or_none(v, base=10)


def classify_observations(s: dict, reads: dict) -> dict:
    page = s.get("page") or {}
    words = page.get("words")
    mask = s.get("mask")
    out1 = (s.get("out") or {}).get("out1")
    c = {
        "page_golden": bool(words and words[2] == G.HANDSHAKE_ACK
                            and (words[3] & 0xFFFF) == 0xABCD),
        "page_ack2": bool(words and words[2] == G.HANDSHAKE_ACK),
        "page_bit14": bool(words and (words[0] & 0x4000)),
        "page_words": ["0x%08x" % w for w in words] if words else [],
        "state_1": s.get("state") == 1,
        "mask_zero": mask == 0,
        "mask_bits": s.get("mask_bits") or [],
        "out1_bits": [OUT1_BITS[b] for b in sorted(OUT1_BITS)
                      if out1 is not None and (out1 & b)],
        "ready_pair": bool((s.get("ready") or {}).get("id1_seen") == 1
                           and (s.get("ready") or {}).get("id2_seen") == 1),
        "rd_catalog_ready": bool((s.get("rd_catalog") or {}).get("id1_seen") == 1
                                 and (s.get("rd_catalog") or {}).get("id2_seen") == 1),
        "h2d_consumed": (s.get("h2d") or {}).get("result") == "consumed",
        "exception_path_ran": bool(mask and any(mask & b for b in
                                                (0x02, 0x04, 0x08, 0x10))),
        "page_clobbered": False,
    }
    page_lines = []
    for key, body in reads.items():
        if key == "page" or key.startswith("page-"):
            page_lines += [ln for ln in body.splitlines() if ln.startswith("PAGE t=")]
    seen_ack2 = False
    for p in page_lines:
        m = PAGE_RE.match(p)
        if not m:
            continue
        if m.group(6) == "1":
            seen_ack2 = True
        elif seen_ack2:
            c["page_clobbered"] = True
    return c


# ---------------------------------------------------------------------------
# Artifacts
# ---------------------------------------------------------------------------
def write_artifacts(report_dir: str, plan: SessionPlan, audit,
                    preflight, report_text, summary, extra=None,
                    protocol_evidence=None) -> dict:
    os.makedirs(report_dir, exist_ok=True)
    paths = {}

    def w(name, content):
        p = os.path.join(report_dir, name)
        with open(p, "w", encoding="utf-8") as f:
            f.write(content)
        paths[name] = p
        return p

    w("session-plan.json", json.dumps(plan.as_dict(), indent=1, sort_keys=True) + "\n")
    w("device-script.sh", plan.script)
    if preflight is not None:
        w("preflight.txt", "\n".join(c.line() for c in preflight) + "\n")
    if report_text is not None:
        w("report.txt", report_text)
    if summary is not None:
        w("summary.json", json.dumps(summary, indent=1, sort_keys=True) + "\n")
    w("writes-host.jsonl",
      "\n".join(json.dumps(r, sort_keys=True) for r in audit.records) + "\n")
    if extra:
        for name, content in extra.items():
            if content is not None:
                w(name, content if isinstance(content, str)
                  else json.dumps(content, indent=1, sort_keys=True) + "\n")
    if plan.sequence is not None:
        w("protocol-plan.json",
          json.dumps(PS.plan_doc(plan.sequence), indent=1, sort_keys=True) + "\n")
    if protocol_evidence is not None:
        w("protocol-evidence.json",
          json.dumps(protocol_evidence, indent=1, sort_keys=True) + "\n")
        w("PROTOCOL-SEQUENCE.md", PS.render_markdown(protocol_evidence))
    w("REPORT.md", render_session_markdown(plan, summary, audit, preflight,
                                           protocol_evidence))
    return paths


def render_session_markdown(plan: SessionPlan, summary, audit,
                            preflight, protocol_evidence=None) -> str:
    out = []
    out.append("# bench session %s" % plan.session_id)
    out.append("")
    out.append("- mode: **%s** - %s" % (plan.mode, plan.spec.about))
    out.append("- tool: %s (%s)" % (TOOL_VERSION, SCHEMA_PLAN))
    if plan.module:
        out.append("- module: %s sha256 %s vermagic %s"
                   % (plan.module["name"], plan.module["sha256"][:16] + "...",
                      plan.module["vermagic"]))
    if plan.fw:
        out.append("- firmware: %s (%s, %d B) sha256 %s"
                   % (plan.fw["name"], plan.fw["role"], plan.fw["len"],
                      plan.fw["sha256"][:16] + "..."))
    for blk in plan.blocks:
        out.append("- block %s: %s words at %s sha256 %s"
                   % (blk.get("label", "?"), blk.get("words", "?"),
                      blk.get("base", "?"),
                      str(blk.get("sha256", "?"))[:16] + "..."))
    out.append("")
    out.append("## Writes (audit)")
    out.append("")
    out.append("- audit records: %s (sha256 chain)"
               % json.dumps(audit.counts(), sort_keys=True))
    out.append("- planned device writes by kind: %s"
               % json.dumps(_counts(w.kind for st in plan.steps for w in st.writes),
                            sort_keys=True))
    out.append("- policy: never CA 0x400392F0; release 0x5A5A only; CECE cece_mode=%s; "
               "H2D one shot" % plan.opts.get("cece_mode"))
    out.append("")
    if preflight is not None:
        out.append("## Host preflight")
        out.append("")
        out.append("| check | result | detail |")
        out.append("| --- | --- | --- |")
        for c in preflight:
            out.append("| %s | %s | %s |"
                       % (c.name, "PASS" if c.ok else ("FAIL" if c.required else "WARN"),
                          c.detail.replace("|", "\\|")))
        out.append("")
    if summary:
        out.append("## Result")
        out.append("")
        out.append("- **SESSION_RESULT: %s%s**"
                   % (summary.get("result"),
                      (":" + summary["stop_rule"]) if summary.get("stop_rule") else ""))
        ci = summary.get("classify") or {}
        out.append("- page: %s (ack2=%s, golden=%s)"
                   % (" / ".join(ci.get("page_words") or []),
                      ci.get("page_ack2"), ci.get("page_golden")))
        out.append("- state=%s mask=%s (%s)"
                   % (summary.get("state"), _hex(summary.get("mask")),
                      ", ".join(summary.get("mask_bits") or []) or "none"))
        out.append("- out: %s" % json.dumps(summary.get("out") or {}, sort_keys=True))
        r = summary.get("ready") or {}
        out.append("- ready pair: id1_seen=%s id2_seen=%s payload=%s"
                   % (r.get("id1_seen"), r.get("id2_seen"), r.get("id2_payload")))
        out.append("- D2H catalogue: %s"
                   % json.dumps(summary.get("rd_catalog") or {}, sort_keys=True))
        out.append("- H2D: %s" % json.dumps(summary.get("h2d") or {}, sort_keys=True))
        if "data-abort" in (summary.get("mask_bits") or []):
            out.append("- **data-abort observed**: the exception path ran; this is a "
                       "PD-style firmware observation, not a host-tooling failure")
        out.append("- gates failed: %s"
                   % (", ".join(summary.get("gates_failed") or []) or "none"))
        out.append("")
        out.append("### Classifier")
        out.append("")
        out.append("~~~json")
        out.append(json.dumps(summary.get("classify") or {}, indent=1, sort_keys=True))
        out.append("~~~")
        out.append("")
        out.append("### Warnings")
        out.append("")
        for warn in plan.warnings:
            out.append("- %s" % warn)
        out.append("")
        if protocol_evidence:
            out.append("## Protocol sequence: %s"
                       % protocol_evidence.get("sequence"))
            out.append("")
            out.append("- verdict: **%s**%s (sequence sha256 %s)"
                       % (protocol_evidence.get("verdict"),
                          (" failed at %s" % protocol_evidence["failed_step"])
                          if protocol_evidence.get("failed_step") else "",
                          str(protocol_evidence.get("sequence_sha256"))[:16]))
            t = protocol_evidence.get("totals") or {}
            out.append("- checks: %s total, %s passed, %s failed, %s not run"
                       % (t.get("checks"), t.get("passed"), t.get("failed"),
                          t.get("not_run")))
            out.append("")
            out.append("| step | kind | status |")
            out.append("| --- | --- | --- |")
            for st in protocol_evidence.get("steps") or []:
                out.append("| %s | %s | %s |"
                           % (st["name"], st["kind"], st["status"]))
            out.append("")
    return "\n".join(out) + "\n"


def _hex(v):
    return "0x%08x" % v if isinstance(v, int) else str(v)


# ---------------------------------------------------------------------------
# Execution
# ---------------------------------------------------------------------------
class BenchError(Exception):
    pass


DEPLOY_RETRY_DELAYS = (2.0, 5.0, 10.0)
REBOOT_POLL_S = 4.0
REBOOT_DEADLINE_S = 300.0


def _retry_op(label: str, fn, audit=None, delays=DEPLOY_RETRY_DELAYS):
    """Retry an IDEMPOTENT host->box operation across a transient link drop.

    Used for deploy/files/sha256sum only - never for the session script itself
    (one boot, one idea: the actual experiment is never retried).
    """
    last = (1, "", "not attempted")
    for i, delay in enumerate((0.0,) + tuple(delays)):
        if i:
            time.sleep(delay)
        rc, out, err = fn()
        if rc == 0:
            if i and audit is not None:
                audit.record(label + "-retry", "host-run", label,
                             "ok after %d retry(ies)" % i, policy="host")
            return rc, out, err
        last = (rc, out, err)
        if audit is not None:
            audit.record(label + "-attempt", "host-run", label, "rc=%s" % rc,
                         policy="host", detail=(out or err).strip()[:160])
    return last


def deploy_plan(plan: SessionPlan, transport, opts: SessionOptions, audit) -> None:
    remote_dir = "%s/%s" % (REMOTE_BASE, plan.session_id)
    rc, out, err = _retry_op(
        "mkdir", lambda: transport.run(
            "mkdir -p %s && echo OK" % shlex.quote(remote_dir), timeout=30),
        audit=audit)
    if rc != 0:
        raise BenchError("cannot create remote session dir after retries: %s"
                         % (out or err).strip()[:200])
    checks = []
    for d in plan.deploy:
        audit.record(d["kind"] + "-deploy", "host-deploy", d["remote"],
                     d["sha256"], policy="host",
                     detail="%s -> %s" % (d["local"], d["remote"]))
        rc, out, err = _retry_op(
            d["kind"] + "-put",
            lambda d=d: transport.put(d["local"], d["remote"]), audit=audit)
        if rc != 0:
            raise BenchError("deploy failed after retries (%s -> %s): %s"
                             % (d["local"], d["remote"],
                                (out or err).strip()[:200]))
        checks.append((d["remote"], d["sha256"]))
    script_remote = os.path.join(remote_dir, "session.sh")
    fd, local_script = tempfile.mkstemp(prefix="bench-session-", suffix=".sh")
    try:
        with os.fdopen(fd, "w") as f:
            f.write(plan.script)
        rc, out, err = _retry_op(
            "script-put", lambda: transport.put(local_script, script_remote),
            audit=audit)
    finally:
        os.unlink(local_script)
    if rc != 0:
        raise BenchError("deploy failed after retries (session script): %s"
                         % (out or err).strip()[:200])
    checks.append((script_remote, sha256_text(plan.script)))
    cmd = "sha256sum %s" % " ".join(shlex.quote(c[0]) for c in checks)
    rc, out, err = _retry_op(
        "sha256sum", lambda: transport.run(cmd, timeout=60), audit=audit)
    if rc != 0:
        raise BenchError("remote sha256sum failed after retries: %s"
                         % (out or err).strip()[:200])
    remote_shas = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 2:
            remote_shas[parts[-1]] = parts[0]
    bad = []
    for remote, want in checks:
        got = remote_shas.get(remote)
        if got != want:
            bad.append("%s (want %s got %s)"
                       % (remote, want[:16], (got or "missing")[:16]))
        else:
            audit.record("deploy-verify", "host-deploy", remote, got,
                         policy="host", detail="sha256 read-back OK")
    if bad:
        raise BenchError("deployed file sha256 mismatch: %s" % "; ".join(bad))
    audit.record("deploy", "host-run", remote_dir, len(plan.deploy) + 1,
                 policy="host", detail="all deployed files sha256-verified on the box")


def _uptime(transport, timeout: float = 15.0):
    rc, out, err = transport.run("cut -d. -f1 /proc/uptime", timeout=timeout)
    if rc == 0 and out.strip().isdigit():
        return int(out.strip())
    return None


def reboot_and_wait(transport, opts: SessionOptions, poll_s: float = None,
                    deadline_s: float = None):
    """Harden the optional --reboot before/after path.

    The old version polled uptime and accepted the PRE-reboot uptime as
    "back" as soon as it was <240 s, which raced the deploy during shutdown
    (the first live fire lost the deploy this way).  The hardened version:

      1. samples the pre-reboot uptime;
      2. issues the reboot and treats a poll as proof only when the box was
         observed unreachable at least once, or the uptime went backwards, or
         the uptime is very small (<= 8 s);
      3. requires a second confirming uptime read before declaring success;
      4. waits up to deadline_s (default 300 s) and reports the last state.
    """
    poll_s = REBOOT_POLL_S if poll_s is None else float(poll_s)
    deadline_s = REBOOT_DEADLINE_S if deadline_s is None else float(deadline_s)
    pre_up = _uptime(transport)
    rc, out, err = transport.run("reboot", timeout=15)
    note = ("reboot issued (rc=%s, pre_uptime=%s)" % (rc, pre_up))
    t0 = time.time()
    seen_down = False
    last = "no poll completed"
    while time.time() - t0 < deadline_s:
        time.sleep(poll_s)
        up = _uptime(transport)
        if up is None:
            seen_down = True
            last = "unreachable (seen_down=1)"
            continue
        last = ("uptime=%ds seen_down=%d pre_uptime=%s"
                % (up, 1 if seen_down else 0, pre_up))
        fresh = False
        if seen_down:
            fresh = up < 240
        elif pre_up is not None and up < pre_up - 2:
            fresh = True
        elif up <= 8:
            fresh = True
        if not fresh:
            continue
        time.sleep(poll_s)
        up2 = _uptime(transport)
        if up2 is not None and up2 < 240:
            return True, ("%s; stable after reboot, uptime=%ds "
                          "(seen_down=%d)" % (note, up2,
                                              1 if seen_down else 0))
        last = "%s; stability re-read failed" % last
    return False, ("%s; device did not come back within %.0f s (last: %s)"
                   % (note, deadline_s, last))


def execute(plan: SessionPlan, opts: SessionOptions, transport, audit,
            preflight) -> dict:
    rc, out, err = transport.run("uname -r", timeout=30)
    if rc != 0:
        raise BenchError("cannot reach the bench box: %s"
                         % (out or err).strip()[:200])
    audit.record("transport", "host-run", transport.name, "uname -r",
                 policy="host", detail=out.strip()[:80])
    if opts.reboot == "before":
        ok, note = reboot_and_wait(transport, opts,
                                   poll_s=opts.reboot_poll_s,
                                   deadline_s=opts.reboot_deadline_s)
        audit.record("reboot-before", "host-run", "reboot", note, policy="host")
        if not ok:
            raise BenchError(note)
        rc, out, err = _retry_op(
            "post-reboot-uname",
            lambda: transport.run("uname -r", timeout=30), audit=audit)
        if rc != 0:
            raise BenchError("box not answering after the reboot: %s"
                             % (out or err).strip()[:200])
    deploy_plan(plan, transport, opts, audit)
    remote_dir = "%s/%s" % (REMOTE_BASE, plan.session_id)
    script_remote = os.path.join(remote_dir, "session.sh")
    rc, out, err = transport.run("sh %s" % shlex.quote(script_remote),
                                 timeout=opts.session_timeout)
    report_text = out if out.strip() else err
    if rc not in (0, 10, 11, None):
        report_text += ("\n[runner] remote session rc=%s stderr=%s"
                        % (rc, (err or "").strip()[:300]))
    extra = {}
    for name in ("writes.log",):
        local = os.path.join(tempfile.gettempdir(),
                             "bench-%s-%s" % (plan.session_id, name))
        rc2, _, _ = transport.get(os.path.join(remote_dir, name), local)
        if rc2 == 0 and os.path.isfile(local):
            extra["writes-device.log"] = open(local, "r", encoding="utf-8",
                                              errors="replace").read()
            os.unlink(local)
    summary = parse_report(report_text)
    summary["remote_rc"] = rc
    summary["transport"] = transport.name
    if plan.sequence is not None:
        # Host-side re-evaluation of every sequence gate from the captured
        # report text; the raw report is kept even if this fails.
        try:
            summary["protocol"] = PS.evaluate(plan.sequence, summary, plan)
        except Exception as e:  # noqa: BLE001 - never lose the session evidence
            summary["protocol"] = {
                "schema": PS.SCHEMA_EVIDENCE, "sequence": plan.sequence["name"],
                "verdict": "ERROR",
                "error": "host evaluation failed: %r" % (e,)}
    if opts.reboot == "after":
        ok, note = reboot_and_wait(transport, opts,
                                   poll_s=opts.reboot_poll_s,
                                   deadline_s=opts.reboot_deadline_s)
        summary["reboot_after"] = note
        audit.record("reboot-after", "host-run", "reboot", note, policy="host")
    return {"summary": summary, "report_text": report_text, "extra": extra}


# ---------------------------------------------------------------------------
# Dry-run text and synthetic fixtures
# ---------------------------------------------------------------------------
def plan_to_text(plan: SessionPlan, opts: SessionOptions) -> str:
    L = []
    L.append("== bench session plan (DRY-RUN; no device will be contacted) ==")
    L.append("mode:        %s - %s" % (plan.mode, plan.spec.about))
    L.append("session id:  %s" % plan.session_id)
    L.append("transport:   %s" % plan.spec.transport)
    L.append("attended:    %s (required=%s)" % (opts.attended, plan.spec.needs_attended))
    L.append("")
    L.append("-- host preflight checks (executed only on a real run) --")
    L.append("  tools/bench-link.sh (PASS required)")
    L.append("  tools/recovery-card.sh (READY required for write modes)")
    L.append("  read-only UART capture running in build/uart-logs/ (required)")
    L.append("  TFTP state: no --allow-wrq server; recovery door state")
    L.append("  single bench session (no other bench_session.py running)")
    L.append("  --attended for write modes")
    L.append("")
    L.append("-- deploy (sha256 verified on the box before the session) --")
    if plan.deploy:
        for d in plan.deploy:
            L.append("  %-9s %s -> %s (sha256 %s..)"
                     % (d["kind"], d.get("local", "?"), d["remote"],
                        str(d.get("sha256", "?"))[:16]))
    else:
        L.append("  (nothing - read-only mode)")
    L.append("  session script -> %s/%s/session.sh"
             % (REMOTE_BASE, plan.session_id))
    if plan.sequence is not None:
        pdoc = PS.plan_doc(plan.sequence)
        L.append("")
        L.append("-- protocol sequence (data-driven; per-step gates) --")
        helper = [d for d in plan.deploy if d["kind"] == "tool"]
        if helper:
            L.append("  read helper: %s -> %s sha256 %s"
                     % (helper[0]["local"], helper[0]["remote"],
                        helper[0]["sha256"][:16] + "..."))
        else:
            L.append("  read helper: NOT BUILT (dry-run warning; real run refuses)")
        L.append("  sequence:   %s" % pdoc["name"])
        L.append("  sha256:     %s" % pdoc["sha256"])
        L.append("  profile:    %s  module(s): %s"
                 % (pdoc["profile"], ", ".join(pdoc["module_names"])))
        L.append("  load:       %s" % json.dumps(pdoc["load_params"],
                                                  sort_keys=True))
        L.append("  requires:   %s" % json.dumps(pdoc["require"],
                                                  sort_keys=True))
        L.append("  totals:     %s" % json.dumps(pdoc["totals"],
                                                  sort_keys=True))
        cfgopts = plan.opts.get("cfg80211")
        if cfgopts:
            L.append("  cfg80211:   support-module=%s iw=%s libnl=%s load-order="
                     "cfg80211-first wiphy-gate=%.0fs"
                     % ((cfgopts.get("module") or {}).get("remote"),
                        (cfgopts.get("iw") or {}).get("remote"),
                        (cfgopts.get("library") or {}).get("remote"),
                        float(cfgopts.get("ready_s") or 0.0)))
            L.append("  userspace:  LD_LIBRARY_PATH=/tmp; the sequence shell "
                     "steps run allowlisted iw/ip/cat commands only")
        L.append("  steps:")
        for st in pdoc["steps"]:
            nchecks = len(PS.check_names({"steps": [
                s for s in plan.sequence["steps"] if s["name"] == st["name"]]}))
            extra = ""
            if st.get("command"):
                extra = " [%s]" % st["command"]
            L.append("    %-22s %-16s checks=%-2d %s%s"
                     % (st["name"], st["kind"], nchecks, st["about"][:80],
                        extra))
        L.append("")
    L.append("")
    L.append("-- planned device writes (all policy-checked) --")
    n = 0
    total = sum(len(s.writes) for s in plan.steps)
    for st in plan.steps:
        for w in st.writes:
            if n >= 80:
                break
            n += 1
            L.append("  %-22s %-7s %s = %s" % (w.step, w.kind, w.target, w.value))
        if n >= 80:
            break
    if total > n:
        L.append("  ... (%d more planned writes; full list in session-plan.json)"
                 % (total - n))
    counts = _counts(w.kind for st in plan.steps for w in st.writes)
    L.append("  total: %d write(s) %s"
             % (sum(counts.values()), json.dumps(counts, sort_keys=True)))
    L.append("  invariants: no CA 0x400392F0 write; no 0xCECE (cece_mode=%d); "
             "release 0x5A5A only; one H2D announce max; devmem only inside the "
             "custom-block windows" % opts.cece_mode)
    L.append("")
    L.append("-- generated device script audit --")
    if plan.audit["ok"]:
        L.append("  PASS (%d devmem write site(s), %d attr store(s), %d announce store(s), "
                 "no violations)"
                 % (plan.audit["stats"]["devmem_writes"],
                    len(plan.audit["stats"]["attr_stores"]),
                    plan.audit["stats"]["announce_stores"]))
    else:
        L.append("  FAIL:")
        for v in plan.audit["violations"]:
            L.append("    line %s: %s - %s" % (v["line"], v["code"], v["message"]))
    L.append("")
    for warn in plan.warnings:
        L.append("warning: %s" % warn)
    L.append("")
    L.append("DRY-RUN: no device was contacted, no file was deployed, no firmware built.")
    return "\n".join(L) + "\n"


def selftest_report_fixtures() -> dict:
    """Synthetic report fixtures for parser/classifier tests (our own bench values)."""
    golden = "\n".join([
        "== SECTION page-t0.2",
        "PAGE t=t0.2 w=0x86807fef 0x00000007 0x00000002 0x0000abcd ack2=1",
        "PAGE-LATCH t=t0.2 ack2=1",
        "== SECTION page-t1.2",
        "PAGE t=t1.2 w=0x86807fef 0x00000007 0x00000002 0x0000abcd ack2=1",
        "== SECTION hcc_status",
        "HCC status: state=READY armed=1 prepost=0x400 loop=running poll_ms=5 cece_mode=0",
        "boot: fw_loaded=1 cpu_released=0 windows=1 handshake_page CA 0x1bf000="
        "0x86807fef/0x00000007/0x00000002/0x0000abcd (+8==2:1) state CA 0x118b60=0x00000001 "
        "mask CA 0x40008=0x00000000 boot_slot CA 0x17d42c=0x000c19d1",
        "mailbox: out0=0x00000000 out1=0x00000040 out2=0x00000000 out3(ack,read)=0x00000000 "
        "out4(rearm,read)=0x00000000 out5(READ-ONLY)=0x00000000 d2h_doorbell=0x00000000",
        "g: armed(g+0xb4 CA 0x10c1a8)=0x00000040 pending(g+0xb8 CA 0x10c1ac)=0x00000040",
        "ete: intr CA 0x40039508=0x00000000 status CA 0x4003a874=0x00000000 send words DR0-3="
        "0x00000000 0x00000000 0x00000000 0x00000000",
        "cece policy (corrected): writes=0 blocked_in_boot=0 evidence_events=0 kicks=0 mode=0",
        "== SECTION hcc_ready",
        "HCC ready: state=READY id1_seen=1 count=1 id2_seen=1 count=1 payload=0x01060850 "
        "plat_timeout=0 dev_timeout=0",
        "DIRECT(device group-A): polls=40 nodes=21 frames=21 id1_seen=1 id2_seen=1 overruns=0 "
        "layout=OK - this is the path that works with the ETE engine idle",
        "== SECTION rd_status",
        "D2H DIRECT status: init=1 boot_ok=1 windows=1 polls=40 overruns=0 bad_node=0 "
        "bad_tag=0 bad_len=0 bad_ptr=0",
        "dr3 layout=OK src=shadow prod_shadow=0x00000011 consumed=17",
        "== SECTION rd_catalog",
        "id1_seen=1 count=1 | id2_seen=1 count=1 payload=0x01060850 | excp_info=1 frames=21",
        "g=4 id=0x01 count=1 g4_id1_plat_ready",
        "== SECTION hcc_events",
        "D2H events: bit6=1 bit2=0 bit3=0",
        "== SECTION dmesg",
        "HCC boot: handshake page ... state 1 mask 0",
        "== SECTION writes",
        "WRITES-DEVICE total=12",
        "GATE no-cece-default PASS",
        "SESSION_RESULT: PASS",
        "== SECTION end",
        "SESSION end=1",
    ]) + "\n"
    golden_missing_pair = golden.replace(
        "id1_seen=1 count=1 id2_seen=1 count=1 payload=0x01060850",
        "id1_seen=0 count=0 id2_seen=0 count=0 payload=")
    pd = "\n".join([
        "== SECTION page-t0.2",
        "PAGE t=t0.2 w=0x86803fef 0x00000017 0x00000002 0x0000abcd ack2=1",
        "PAGE-LATCH t=t0.2 ack2=1",
        "== SECTION page-t1.2",
        "PAGE t=t1.2 w=0x0010c190 0x00000000 0x00000000 0x001bee50 ack2=0",
        "== SECTION hcc_status",
        "HCC status: state=READY armed=1 prepost=0x400 loop=running poll_ms=5 cece_mode=0",
        "boot: fw_loaded=1 cpu_released=1 windows=1 handshake_page CA 0x1bf000="
        "0x0010c190/0x00000000/0x00000000/0x001bee50 (+8==2:0) state CA 0x118b60=0x00000000 "
        "mask CA 0x40008=0x00000010 boot_slot CA 0x17d42c=0x000c19d1",
        "mailbox: out0=0x00000008 out1=0x00000004 out2=0x00000000 out3(ack,read)=0x00000000 "
        "out4(rearm,read)=0x00000000 out5(READ-ONLY)=0x00000000 d2h_doorbell=0x00000000",
        "cece policy (corrected): writes=3 blocked_in_boot=0 evidence_events=4 kicks=0 mode=0",
        "== SECTION hcc_events",
        "startup/vector announce observed 4x (out[1] bit2)",
        "== SECTION dmesg",
        "HCC CECE: manual kick refused",
        "data abort",
        "== SECTION writes",
        "WRITES-DEVICE total=25",
        "SESSION_RESULT: PASS",
    ]) + "\n"
    stop = golden.replace("SESSION_RESULT: PASS",
                          "STOP:ready-id2-timeout: no g4 id2") + \
        "GATE ready-id2-timeout FAIL (id2_seen=1)\nSESSION_RESULT: STOP:ready-id2-timeout\n"
    return {"golden": golden, "golden_missing_pair": golden_missing_pair,
            "pd": pd, "stop": stop}


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(
        description="one-command attended bench session runner for the Hi5622V100 "
                    "Wi-Fi transport (host-only tooling; dry-run never contacts the device)")
    ap.add_argument("--mode", choices=sorted(MODES), help="session mode (see --list)")
    ap.add_argument("--list", action="store_true", help="list modes")
    ap.add_argument("--list-sequences", action="store_true",
                    help="list the bundled protocol sequences (no device)")
    ap.add_argument("--module", help="local instrument .ko to load (write modes)")
    ap.add_argument("--bar0read", help="protocol mode: prebuilt static ARM read helper "
                                       "(ELF32 ET_EXEC; normally auto-built from "
                                       "tools/bar0read.c with the lab cross compiler)")
    ap.add_argument("--cfg80211-ko", help="cfg80211 mode: local cfg80211.ko to deploy "
                                          "and insmod BEFORE the instrument module")
    ap.add_argument("--iw-bin", help="cfg80211 mode: local ARM iw binary to deploy to /tmp "
                                     "(the image rootfs has none)")
    ap.add_argument("--libnl-tiny", help="cfg80211 mode: local ARM libnl-tiny shared object "
                                         "deployed to /tmp (iw's LD_LIBRARY_PATH)")
    ap.add_argument("--cfg-ready", type=float, default=20.0,
                    help="cfg80211 mode: seconds to wait for /sys/class/ieee80211 "
                         "after the ready pair (default 20)")
    ap.add_argument("--fw-file", help="local firmware file to deploy (patched mode)")
    ap.add_argument("--fw-manifest", help="builder manifest for the patched firmware")
    ap.add_argument("--fw-path", help="kernel firmware name under /lib/firmware "
                                      "(e.g. hi_wifi/FIRMWARE.PD.bin); relative only")
    ap.add_argument("--plat-block", help="platform-custom block file (24 words, 0x4086A800)")
    ap.add_argument("--wifi-block", help="wifi-custom block file (618 words, 0x4086A860); "
                                        "protocol mode converts it to the PHY block binary")
    ap.add_argument("--wifi-block-bin", help="protocol mode: the 2472 B PHY custom-block "
                                             "binary to deploy as /lib/firmware/hi_wifi/"
                                             "wifi_block.bin (local evidence only)")
    ap.add_argument("--sequence", help="protocol mode: JSON sequence file (default: the "
                                       "bundled tools/sequences/ladder-c-ordered.json)")
    ap.add_argument("--module-param", action="append", metavar="KEY=VALUE",
                    help="extra insmod parameter (repeatable; must be a module "
                         "parameter, e.g. h2d_send_budget=2)")
    ap.add_argument("--no-announce", action="store_true",
                    help="h2d mode without the announce step (informational only)")
    ap.add_argument("--cece-mode", type=int, default=1, choices=[0, 1, 2],
                    help="CECE policy: 1=never (default), 0/2=gated by the +8==2 latch")
    ap.add_argument("--attended", action="store_true",
                    help="operator marked the bench attended")
    ap.add_argument("--dry-run", action="store_true",
                    help="validate + print the full plan; never contact the device")
    ap.add_argument("--yes-really", action="store_true",
                    help="execute the session (needs --attended for write modes)")
    ap.add_argument("--report-dir", help="evidence dir (default build/wifi-bench/<session-id>)")
    ap.add_argument("--endpoint", default=DEFAULT_ENDPOINT)
    ap.add_argument("--session-timeout", type=int, default=240)
    ap.add_argument("--ready-id1", type=float, default=2.0, help="g4 id1 deadline (s)")
    ap.add_argument("--ready-id2", type=float, default=20.0, help="g4 id2 deadline (s)")
    ap.add_argument("--observe", type=float, default=None, help="observation window (s)")
    ap.add_argument("--require-ready", action="store_true",
                    help="treat a missing ready pair as a stop rule (strict)")
    ap.add_argument("--allow-other-source", action="store_true",
                    help="allow a non-pinned vendor firmware file (testing only)")
    ap.add_argument("--transport", choices=["ssh", "telnet"], default=None)
    ap.add_argument("--telnet-host", default="192.168.10.1")
    ap.add_argument("--telnet-port", type=int, default=23)
    ap.add_argument("--ssh-host", default=None)
    ap.add_argument("--reboot", choices=["none", "before", "after"], default="none",
                    help="optional box reboot before/after the session (attended only)")
    ap.add_argument("--skip-preflight", action="store_true",
                    help="skip host preflight (testing only)")
    ap.add_argument("--skip-blocks", action="store_true",
                    help="deliberately skip the custom-block pre-seed (degraded run)")
    ap.add_argument("--print-script", action="store_true",
                    help="print the generated device script")
    ap.add_argument("--print-plan", action="store_true", help="print the plan JSON")
    ap.add_argument("--json", action="store_true", help="print the summary as JSON")
    ap.add_argument("--selftest-report", action="store_true",
                    help="parse/classify the built-in synthetic report fixtures (no device)")
    return ap


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    if args.list:
        for name in sorted(MODES):
            m = MODES[name]
            print("%-13s %s" % (name, m.about))
            print("              module=%s fw=%s writes=%s attended=%s transport=%s"
                  % (m.uses_module, m.fw_role, not m.read_only,
                     m.needs_attended, m.transport))
        return 0
    if args.list_sequences:
        d = PROTOCOL_SEQUENCE_DIR
        files = sorted(os.path.join(d, f) for f in os.listdir(d)
                       if f.endswith(".json")) if os.path.isdir(d) else []
        if not files:
            print("no bundled sequences in %s" % d)
            return 1
        for path in files:
            try:
                seq = PS.load(path)
                pdoc = PS.plan_doc(seq)
                print("%-24s %s" % (seq["name"], path))
                print("    sha256 %s  steps=%d checks=%d writes=%d"
                      % (seq["sha256"], pdoc["totals"]["steps"],
                         pdoc["totals"]["checks"],
                         pdoc["totals"]["attr_writes"]))
                print("    %s" % seq["about"])
            except G.GuardError as e:
                print("%-24s REFUSED: %s" % (os.path.basename(path), e))
        return 0
    if args.selftest_report:
        fixtures = selftest_report_fixtures()
        ok = True
        for name, text in fixtures.items():
            s = parse_report(text)
            c = s["classify"]
            print("fixture %-20s result=%-5s golden=%s ack2=%s state=%s mask=%s "
                  "ready=%s h2d=%s mask_bits=%s"
                  % (name, s["result"], c["page_golden"], c["page_ack2"],
                     s["state"], _hex(s["mask"]), c["ready_pair"],
                     (s["h2d"] or {}).get("result"),
                     ",".join(s["mask_bits"])))
            if name == "golden":
                ok = ok and c["page_golden"] and c["ready_pair"] and s["result"] == "PASS"
            if name == "golden_missing_pair":
                ok = ok and not c["ready_pair"] and s["result"] == "PASS"
            if name == "pd":
                ok = ok and s["page_latched"] and c["page_clobbered"] and \
                    "data-abort" in s["mask_bits"] and s["cece_writes"] == 3
            if name == "stop":
                ok = ok and s["result"] == "STOP" and \
                    s["stop_rule"] == "ready-id2-timeout"
        print("selftest-report:", "PASS" if ok else "FAIL")
        return 0 if ok else 1
    if not args.mode:
        build_parser().print_help()
        return 2
    dry_run = args.dry_run or not args.yes_really
    if not dry_run and not args.attended and MODES[args.mode].needs_attended:
        print("error: a write session needs --attended (recovery needs a human at the "
              "bench); use --dry-run to inspect the plan", file=sys.stderr)
        return 2
    opts = SessionOptions(
        mode=args.mode, module=args.module, fw_file=args.fw_file,
        fw_manifest=args.fw_manifest, fw_path=args.fw_path,
        plat_block=args.plat_block, wifi_block=args.wifi_block,
        sequence=args.sequence, wifi_block_bin=args.wifi_block_bin,
        module_params=args.module_param, bar0read=args.bar0read,
        cfg80211_ko=args.cfg80211_ko, iw_bin=args.iw_bin,
        libnl_tiny=args.libnl_tiny, cfg_ready_s=args.cfg_ready,
        announce=(not args.no_announce), cece_mode=args.cece_mode,
        attended=args.attended, dry_run=dry_run, report_dir=args.report_dir,
        endpoint=args.endpoint, session_timeout=args.session_timeout,
        ready_id1_s=args.ready_id1, ready_id2_s=args.ready_id2,
        observe_s=args.observe, require_ready=args.require_ready,
        allow_other_source=args.allow_other_source, transport=args.transport,
        reboot=args.reboot, skip_preflight=args.skip_preflight,
        skip_blocks=args.skip_blocks, telnet_host=args.telnet_host,
        telnet_port=args.telnet_port, ssh_host=args.ssh_host)
    guard = G.WriteGuard(G.GuardContext(cece_mode=args.cece_mode, profile="hcc"))
    try:
        plan = make_plan(opts, guard)
    except G.GuardError as e:
        print("REFUSED: %s" % e, file=sys.stderr)
        return 2
    base, _is_project = evidence_base()
    report_dir = opts.report_dir or os.path.join(base, plan.session_id)
    preflight = []
    if not dry_run:
        preflight = host_preflight(opts, plan.spec)
        if any(not c.ok and c.required for c in preflight):
            print("HOST PREFLIGHT: FAIL")
            for c in preflight:
                print(c.line())
            audit = G.WriteAudit(os.path.join(report_dir, "writes-host.jsonl"))
            write_artifacts(report_dir, plan, audit, preflight, None, None)
            return 3
    audit = G.WriteAudit(os.path.join(report_dir, "writes-host.jsonl"))
    for st in plan.steps:
        for w in st.writes:
            audit.record(w.step, w.kind, w.target, w.value, ca=w.ca,
                         policy="dry-run" if dry_run else "allow",
                         detail=w.rationale)
    if args.print_script:
        print(plan.script)
    if args.print_plan:
        print(json.dumps(plan.as_dict(), indent=1, sort_keys=True))
    if dry_run:
        print(plan_to_text(plan, opts))
        write_artifacts(report_dir, plan, audit, preflight, None,
                        {"result": "DRY-RUN", "writes": plan.as_dict()["writes"],
                         "audit_ok": plan.audit["ok"]},
                        {"dryrun": "no device contacted; no deploy performed"})
        print("dry-run artifacts: %s" % report_dir)
        if args.json:
            out = {"result": "DRY-RUN", "report_dir": report_dir,
                   "audit_ok": plan.audit["ok"],
                   "writes": sum(len(s.writes) for s in plan.steps)}
            if plan.sequence is not None:
                out["sequence"] = PS.plan_doc(plan.sequence)["totals"]
            print(json.dumps(out, sort_keys=True))
        return 0 if plan.audit["ok"] else 1
    transport = make_transport(opts, plan.spec)
    try:
        res = execute(plan, opts, transport, audit, preflight)
    except (BenchError, G.GuardError) as e:
        audit.record("session", "host-run", opts.mode, "aborted",
                     policy="host", detail=str(e)[:300])
        audit.close()
        write_artifacts(report_dir, plan, audit, preflight, None, {"error": str(e)})
        print("SESSION ABORTED: %s" % e, file=sys.stderr)
        return 4
    summary = res["summary"]
    audit.record("session-result", "host-run", opts.mode,
                 summary.get("result") or "?", policy="host",
                 detail=summary.get("stop_rule") or "")
    audit.close()
    write_artifacts(report_dir, plan, audit, preflight, res["report_text"],
                    summary, res.get("extra"),
                    protocol_evidence=summary.get("protocol"))
    print("== SESSION SUMMARY ==")
    print("mode:          %s" % plan.mode)
    print("session id:    %s" % plan.session_id)
    print("result:        %s%s"
          % (summary.get("result"),
             (":" + summary["stop_rule"]) if summary.get("stop_rule") else ""))
    ci = summary.get("classify") or {}
    print("page:          %s ack2=%s golden=%s"
          % (" / ".join(ci.get("page_words") or []), ci.get("page_ack2"),
             ci.get("page_golden")))
    print("state/mask:    %s / %s (%s)"
          % (summary.get("state"), _hex(summary.get("mask")),
             ", ".join(summary.get("mask_bits") or []) or "none"))
    print("ready pair:    id1=%s id2=%s payload=%s"
          % ((summary.get("ready") or {}).get("id1_seen"),
             (summary.get("ready") or {}).get("id2_seen"),
             (summary.get("ready") or {}).get("id2_payload")))
    print("h2d:           %s" % json.dumps(summary.get("h2d") or {}, sort_keys=True))
    proto = summary.get("protocol")
    if proto:
        pt = proto.get("totals") or {}
        print("protocol:      %s (%s/%s checks, failed at %s)"
              % (proto.get("verdict"), pt.get("passed"), pt.get("checks"),
                 proto.get("failed_step") or "none"))
    print("artifacts:     %s" % report_dir)
    if args.json:
        print(json.dumps(summary, sort_keys=True))
    if summary.get("result") == "PASS":
        return 0
    if summary.get("result") == "STOP":
        return 10
    return 11


if __name__ == "__main__":
    raise SystemExit(main())
