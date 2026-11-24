#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""protocol_sequence.py - data-driven protocol-sequence steps + evidence records.

Lane: wifi-bench-tooling (shared task task-108), HOST-ONLY.
Part of the Hi5622V100 clean-room Wi-Fi bench runner.  A *sequence* is a JSON
document (schema hi5622-protocol-sequence/1) that describes the post-boot
protocol startup as an ordered list of steps with explicit per-step gates:

  read_pack      one or more read-only BAR0 word reads, each with a numeric
                 expectation (eq/ne/zero/nonzero/ge, optional mask).  A read
                 may be dynamic ("base": <earlier label>, "add": <hex>): the
                 device script computes BAR0 = RD_<base> + 0x6B8000 + add and
                 refuses to read if the result leaves the firmware-RAM window.
  attr_expect    required substrings in a sysfs attribute (device-side grep).
  attr_write     one WO sysfs store (the real H2D message trigger) plus the
                 post-write attribute expectations.
  catalog_snapshot  capture rd_catalog/rd_decode for later deltas.
  d2h_catalog    capture rd_catalog/rd_decode and require expected
                 (group,id) entries (optionally vs. an earlier snapshot).
  sleep          a bounded wait (0.1..10 s).
  note           documentation-only step (no gate).

The device script stops on the FIRST failed gate (stop-on-first-failure), and
the host evaluator recomputes the same gates independently from the captured
report text, so the evidence record is not just "the device said PASS": every
check has a captured actual value, a host verdict and the device gate verdict.

This module never talks to the device; it only validates config, emits shell
fragments (quoted via shlex-style single quoting) and evaluates report text.
"""
from __future__ import annotations

import hashlib
import json
import os
import re

import bench_guards as G

SCHEMA_SEQ = "hi5622-protocol-sequence/1"
SCHEMA_PLAN = "hi5622-protocol-plan/1"
SCHEMA_EVIDENCE = "hi5622-protocol-evidence/1"
PROFILE = "hi5622-wifi-h2d"
STEP_KINDS = ("read_pack", "attr_expect", "attr_write", "catalog_snapshot",
              "d2h_catalog", "sleep", "note")
NAME_RE = re.compile(r"^[a-z][a-z0-9-]{0,47}$")
LABEL_RE = re.compile(r"^[a-z][a-z0-9_]{0,31}$")
ATTR_RE = re.compile(r"^[a-z][a-z0-9_]{0,31}$")


# ---------------------------------------------------------------------------
# Pointer conventions (endpoint inbound region table, hi5622_contract.h)
#
# The firmware allocator returns device CAs (e.g. ctx+0x190 = 0x01011448 in the
# PKTRAM region), while firmware structures hold CPU-view addresses for the
# firmware RAM (CPU 0x0..0x217fff -> BAR0 0x6b8000+CPU).  A dynamic read must
# therefore resolve the runtime pointer through the real inbound viewports:
#   region 0 ROM/WRAM   CA 0x00000000..0x001bffff -> BAR0 0x0000000
#   region 1 TCM        CA 0x00400000..0x00417fff -> BAR0 0x01c0000
#   region 2 PKTRAM     CA 0x01000000..0x011dffff -> BAR0 0x01d8000
#   region 3 IO         CA 0x40000000..0x4011ffff -> BAR0 0x03b8000
#   region 4 ACP        CA 0x02000000..0x021dffff -> BAR0 0x04d8000
#   region 5 FW RAM CA  CA 0x01200000..0x0137ffff -> BAR0 0x06b8000
# plus the CPU view of region 5: CPU 0x0..0x217fff -> BAR0 0x6b8000+CPU.
# The resolver order tries the distinctive device-CA windows first.
# ---------------------------------------------------------------------------
POINTER_CONVENTIONS = (
    # name, ca_lo, ca_hi (inclusive), bar0_delta (bar0 = ca - delta)
    ("pktram", 0x01000000, 0x011DFFFF, 0x00E28000),
    ("acp", 0x02000000, 0x021DFFFF, 0x01B28000),
    ("fwram-ca", 0x01200000, 0x0137FFFF, 0x00B48000),
    ("io", 0x40000000, 0x4011FFFF, 0x3FC48000),
    ("tcm", 0x00400000, 0x00417FFF, 0x00240000),
    ("cpu", 0x00000000, 0x0021FFFF, -G.BAR0_CPU_BASE),
)
BAR0_MAX = 0x1000000   # 16 MiB BAR0 memory window


def resolve_pointer(base) -> dict | None:
    """Resolve a firmware pointer to its host BAR0 offset (None if unresolved)."""
    b = int(base) & 0xFFFFFFFF
    for name, lo, hi, delta in POINTER_CONVENTIONS:
        if lo <= b <= hi:
            off = b - delta if delta >= 0 else b + (-delta)
            if 0 <= off < BAR0_MAX:
                return {"conv": name, "bar0": off & 0xFFFFFFFF}
            return None
    return None


def pointer_shell() -> list:
    """POSIX shell lines implementing resolve_pointer() as $SEQ_CONV/$SEQ_OFF.

    Inputs: $SEQ_BASE (hex, no 0x prefix).  Sets SEQ_CONV (name|unresolved) and
    SEQ_OFF (BAR0 offset).  Hex constants are only evaluated inside $(( )).
    """
    L = []
    a = L.append
    a("  SEQ_BASE_N=$((0x$SEQ_BASE_HEX))")
    a("  SEQ_CONV=unresolved; SEQ_OFF=-1")
    for name, lo, hi, delta in POINTER_CONVENTIONS:
        a("  if [ \"$SEQ_BASE_N\" -ge \"$((0x%08X))\" ] && "
          "[ \"$SEQ_BASE_N\" -le \"$((0x%08X))\" ]; then SEQ_CONV=%s; "
          "SEQ_OFF=$((SEQ_BASE_N %s 0x%08X)); fi"
          % (lo, hi, name, "-" if delta >= 0 else "+", abs(delta)))
    return L

# Host-side failure triage: code -> the host-only next step.  Mirrors the
# fallback matrix of ../wifi-h2d-send/BENCH-PROCEDURE.md.
TRIAGE = {
    "read-mismatch": (
        "The boot/PACK state is not the expected PD4 golden state. Do not fire "
        "another message: re-read boot/hcc_ready/rd_status, capture the raw "
        "values and treat this as a different experiment."),
    "read-missing": (
        "The read label was never emitted: the session stopped before this step "
        "or the attribute/deploy failed. Use stop_rule + the previous step's "
        "evidence to find the first failure; do not rerun blindly."),
    "device-read-empty": (
        "The device script emitted the read label but with an EMPTY value: the "
        "raw BAR0 read path failed (the pre-2026-11-25 tooling read resource0 "
        "with dd, which this kernel does not support for read()). The protocol "
        "script now reads /dev/mem at the BAR0 physical base and gates a "
        "readpath self-check before boot; if this still fails, capture the "
        "READPATH line and /dev/mem availability - do not fire the H2D pair."),
    "attr-pattern-missing": (
        "A sysfs gate did not match. Capture h2d_status/h2d_plan/hcc_events and "
        "dmesg; compare with the expected string in the sequence step. A gate "
        "refusal is a stop, not a retry; never raise h2d_send_budget mid-session."),
    "attr-capture-missing": (
        "The post-write attribute capture is absent: the sysfs store failed "
        "(usually -ETIMEDOUT = announce not consumed) and the script stopped at "
        "attr-write-failed. Do not resend; power-cycle for another boot."),
    "write-failed": (
        "The H2D store failed (announce not consumed / driver gate refused). "
        "Capture out[0]/out[2], hcc_rings SR0 producer/consumer, boot page and "
        "mask; no retry in this session."),
    "catalog-entry-missing": (
        "The firmware did not answer with the expected D2H frame. Consumed + "
        "new responses is the known working lap; absence is a discovery: "
        "capture rd_catalog/rd_decode and stop (do not resend the message)."),
    "catalog-frames-not-increased": (
        "No new D2H frame count: the response path did not run. Capture "
        "rd_catalog/rd_decode/hcc_events and compare with the before snapshot."),
    "resolved-pointer-outside-window": (
        "A dynamic (base+offset) read resolved outside the firmware-RAM BAR0 "
        "window: the pointer convention differs from the expected CPU address. "
        "Report the raw base value; do not guess the window."),
    "device-gate-fail": (
        "The device-side gate failed; the host report should carry the same "
        "check. Capture the evidence dir and follow the check's own triage."),
    "pre-sequence-stop": (
        "The device script stopped BEFORE the first sequence step (boot ladder "
        "or the readpath/boot gates). Nothing was fired; read stop_rule and the "
        "preceding section, fix that gate, and do not resume mid-boot."),
    "step-not-run": (
        "Not executed because an earlier step stopped the session "
        "(stop-on-first-failure). Fix/report the first failing step; do not "
        "resume mid-boot."),
}

# ---------------------------------------------------------------------------
# small helpers
# ---------------------------------------------------------------------------
def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _q(s) -> str:
    return "'" + str(s).replace("'", "'\\''") + "'"


def _need(d: dict, key: str, where: str):
    if key not in d:
        raise G.GuardError("seq-schema", "%s: missing %r" % (where, key))
    return d[key]


def _refuse(where: str, message: str, code: str = "seq-schema"):
    raise G.GuardError(code, "%s: %s" % (where, message))


def _parse_num(v, where: str) -> int:
    try:
        return G.parse_int(v)
    except G.GuardError:
        _refuse(where, "not an integer: %r" % (v,))
        raise  # unreachable


def _as_hex(v) -> str:
    return "0x%08x" % (int(v) & 0xFFFFFFFF)


# ---------------------------------------------------------------------------
# Sequence validation / loading
# ---------------------------------------------------------------------------
def _validate_expect(expect, where: str) -> dict:
    if expect is None:
        return {}
    if not isinstance(expect, dict):
        _refuse(where, "expect must be an object")
    allowed = {"eq", "ne", "zero", "nonzero", "ge", "mask"}
    for k in expect:
        if k not in allowed:
            _refuse(where, "unknown expectation %r (allowed: %s)"
                    % (k, ", ".join(sorted(allowed))))
    out = {}
    for k in ("eq", "ne", "ge", "mask"):
        if k in expect:
            out[k] = _parse_num(expect[k], "%s.%s" % (where, k))
    for k in ("zero", "nonzero"):
        if k in expect:
            if not isinstance(expect[k], bool):
                _refuse(where, "%s must be a boolean" % k)
            out[k] = expect[k]
    if not any(k in out for k in ("eq", "ne", "zero", "nonzero", "ge")):
        _refuse(where, "expect has no comparison (eq/ne/zero/nonzero/ge)")
    if "mask" in out and out["mask"] == 0:
        _refuse(where, "mask must be non-zero")
    out.setdefault("mask", 0xFFFFFFFF)
    return out


def _validate_reads(step: dict, where: str, labels: dict) -> list:
    reads = step.get("reads")
    if not isinstance(reads, list) or not reads:
        _refuse(where, "read_pack needs a non-empty 'reads' list")
    out = []
    for i, r in enumerate(reads):
        w = "%s.reads[%d]" % (where, i)
        if not isinstance(r, dict):
            _refuse(w, "read must be an object")
        extra = set(r) - {"label", "bar0", "base", "add", "expect", "about"}
        if extra:
            _refuse(w, "unknown keys: %s" % ", ".join(sorted(extra)))
        label = _need(r, "label", w)
        if not isinstance(label, str) or not LABEL_RE.match(label):
            _refuse(w, "bad label %r (^[a-z][a-z0-9_]{0,31}$)" % (label,))
        if label in labels:
            _refuse(w, "duplicate read label %r (labels are session-global)" % label)
        if "bar0" in r and ("base" in r or "add" in r):
            _refuse(w, "use either 'bar0' or 'base'+'add', not both")
        if "bar0" in r:
            off = _parse_num(r["bar0"], w)
            try:
                off = G.check_bar0_read(off)
            except G.GuardError as e:
                _refuse(w, e.message, e.code)
            rec = {"label": label, "bar0": off}
        elif "base" in r:
            base = r["base"]
            if not isinstance(base, str) or base not in labels:
                _refuse(w, "dynamic read base %r is not an earlier read label "
                           "(known: %s)" % (base, ", ".join(sorted(labels)) or "none"))
            add = _parse_num(_need(r, "add", w), w + ".add")
            if add % 4:
                _refuse(w, "dynamic add 0x%X is not 4-byte aligned" % add)
            if add > 0x100000:
                _refuse(w, "dynamic add 0x%X is too large" % add)
            rec = {"label": label, "base": base, "add": add}
        else:
            _refuse(w, "read needs 'bar0' or 'base'+'add'")
        rec["expect"] = _validate_expect(r.get("expect"), w + ".expect")
        rec["about"] = str(r.get("about", ""))[:200]
        labels[label] = rec
        out.append(rec)
    return out


def _validate_patterns(patterns, where: str) -> list:
    if patterns is None:
        return []
    if not isinstance(patterns, list) or not patterns:
        _refuse(where, "patterns must be a non-empty list")
    out = []
    for i, p in enumerate(patterns):
        if not isinstance(p, str) or not p:
            _refuse("%s[%d]" % (where, i), "pattern must be a non-empty string")
        if len(p) > 240:
            _refuse("%s[%d]" % (where, i), "pattern longer than 240 chars")
        if "\n" in p or "\x00" in p:
            _refuse("%s[%d]" % (where, i), "pattern must be single-line")
        if "10161000" in p.lower():
            _refuse("%s[%d]" % (where, i), "pattern mentions the RC misc window")
        out.append(p)
    return out


def validate(doc: dict, source: str = "<sequence>") -> dict:
    """Validate + normalise a sequence document (schema hi5622-protocol-sequence/1)."""
    if not isinstance(doc, dict):
        _refuse(source, "sequence must be a JSON object")
    if doc.get("schema") != SCHEMA_SEQ:
        _refuse(source, "schema must be %r, got %r" % (SCHEMA_SEQ, doc.get("schema")))
    name = doc.get("name")
    if not isinstance(name, str) or not NAME_RE.match(name):
        _refuse(source, "bad sequence name %r" % (name,))
    about = str(doc.get("about", ""))[:400]
    profile = doc.get("profile", PROFILE)
    if profile != PROFILE:
        _refuse(source, "profile must be %r (the hi5622_wifi H2D driver), got %r"
                % (PROFILE, profile))
    module_names = doc.get("module_names")
    if module_names is None:
        module_names = ["hi5622_wifi"]
    if (not isinstance(module_names, list) or not module_names
            or not all(isinstance(m, str) and re.match(r"^[a-z0-9_]+$", m)
                       for m in module_names)):
        _refuse(source, "module_names must be a non-empty list of module names")
    require = doc.get("require", {})
    if not isinstance(require, dict):
        _refuse(source, "require must be an object")
    allowed_req = {"ready_pair", "boot_page", "fw_sha256", "fw_len", "budget_min"}
    for k in require:
        if k not in allowed_req:
            _refuse(source, "unknown require key %r" % (k,))
    norm_require = {
        "ready_pair": bool(require.get("ready_pair", True)),
        "boot_page": bool(require.get("boot_page", True)),
        "budget_min": int(require.get("budget_min", 0) or 0),
    }
    if "fw_sha256" in require:
        s = require["fw_sha256"]
        if not isinstance(s, str) or not re.match(r"^[0-9a-f]{64}$", s):
            _refuse(source, "require.fw_sha256 must be 64 lowercase hex chars")
        norm_require["fw_sha256"] = s
    if "fw_len" in require:
        norm_require["fw_len"] = _parse_num(require["fw_len"], source + ".require.fw_len")
    load_params = doc.get("load_params", {})
    if not isinstance(load_params, dict):
        _refuse(source, "load_params must be an object")
    norm_params = {}
    for k, v in load_params.items():
        if not isinstance(k, str) or not re.match(r"^[a-z][a-z0-9_]{0,31}$", k):
            _refuse(source, "bad load_params key %r" % (k,))
        if isinstance(v, bool) or not isinstance(v, (int, str)):
            _refuse(source, "load_params[%r] must be an int or string" % k)
        if isinstance(v, str) and (len(v) > 120 or "\n" in v or "\x00" in v):
            _refuse(source, "load_params[%r] is not a short single-line string" % k)
        norm_params[k] = v
    steps_in = doc.get("steps")
    if not isinstance(steps_in, list) or not steps_in:
        _refuse(source, "sequence needs a non-empty 'steps' list")
    if len(steps_in) > 64:
        _refuse(source, "too many steps (%d > 64)" % len(steps_in))
    labels: dict = {}
    names: set = set()
    steps = []
    catalog_snaps = set()
    for i, st in enumerate(steps_in):
        w = "%s.steps[%d]" % (source, i)
        if not isinstance(st, dict):
            _refuse(w, "step must be an object")
        sname = _need(st, "name", w)
        if not isinstance(sname, str) or not NAME_RE.match(sname):
            _refuse(w, "bad step name %r (^[a-z][a-z0-9-]{0,47}$)" % (sname,))
        if sname in names:
            _refuse(w, "duplicate step name %r" % sname)
        names.add(sname)
        kind = _need(st, "kind", w)
        if kind not in STEP_KINDS:
            _refuse(w, "unknown kind %r (allowed: %s)"
                    % (kind, ", ".join(STEP_KINDS)))
        common = {"name", "kind", "about"}
        body = {"name": sname, "kind": kind,
                "about": str(st.get("about", ""))[:400]}
        if kind == "read_pack":
            extra = set(st) - common - {"reads"}
            if extra:
                _refuse(w, "unknown keys: %s" % ", ".join(sorted(extra)))
            body["reads"] = _validate_reads(st, w, labels)
        elif kind == "attr_expect":
            extra = set(st) - common - {"attr", "patterns"}
            if extra:
                _refuse(w, "unknown keys: %s" % ", ".join(sorted(extra)))
            attr = _need(st, "attr", w)
            if not isinstance(attr, str) or not ATTR_RE.match(attr):
                _refuse(w, "bad attribute name %r" % (attr,))
            if attr not in (G.BASE_ATTRS | G.HCC_ATTRS | G.RD_ATTRS |
                            G.H2D_ATTRS | G.WIFI_TRANSPORT_ATTRS):
                _refuse(w, "unknown sysfs attribute %r" % attr)
            body["attr"] = attr
            body["patterns"] = _validate_patterns(_need(st, "patterns", w),
                                                 w + ".patterns")
        elif kind == "attr_write":
            extra = set(st) - common - {"attr", "value", "patterns"}
            if extra:
                _refuse(w, "unknown keys: %s" % ", ".join(sorted(extra)))
            attr = _need(st, "attr", w)
            if not isinstance(attr, str) or not ATTR_RE.match(attr):
                _refuse(w, "bad attribute name %r" % (attr,))
            if attr not in G.WO_ATTRS:
                _refuse(w, "attribute %r is not write-only/triggerable" % attr)
            value = _need(st, "value", w)
            if isinstance(value, bool) or not isinstance(value, (int, str)):
                _refuse(w, "value must be an int or string")
            body["attr"] = attr
            body["value"] = str(value)
            body["patterns"] = _validate_patterns(st.get("patterns") or [],
                                                  w + ".patterns")
            body["post_attr"] = attr
        elif kind == "catalog_snapshot":
            extra = set(st) - common
            if extra:
                _refuse(w, "unknown keys: %s" % ", ".join(sorted(extra)))
            catalog_snaps.add(sname)
        elif kind == "d2h_catalog":
            extra = set(st) - common - {"expect_entries", "device_patterns",
                                        "before", "require_frames_increase"}
            if extra:
                _refuse(w, "unknown keys: %s" % ", ".join(sorted(extra)))
            entries = _need(st, "expect_entries", w)
            if not isinstance(entries, list) or not entries:
                _refuse(w, "expect_entries must be a non-empty list")
            norm_entries = []
            for j, e in enumerate(entries):
                ew = "%s.expect_entries[%d]" % (w, j)
                if not isinstance(e, dict):
                    _refuse(ew, "entry must be an object")
                if set(e) - {"group", "id", "min_count", "last_total", "about"}:
                    _refuse(ew, "unknown keys")
                group = _parse_num(_need(e, "group", ew), ew + ".group")
                mid = _parse_num(_need(e, "id", ew), ew + ".id")
                if group > 0xFFFF or mid > 0xFF:
                    _refuse(ew, "group/id out of range")
                rec = {"group": group, "id": mid,
                       "min_count": _parse_num(e.get("min_count", 1),
                                               ew + ".min_count"),
                       "about": str(e.get("about", ""))[:200]}
                if "last_total" in e:
                    rec["last_total"] = _parse_num(e["last_total"],
                                                   ew + ".last_total")
                norm_entries.append(rec)
            body["expect_entries"] = norm_entries
            body["device_patterns"] = _validate_patterns(
                st.get("device_patterns") or [], w + ".device_patterns")
            body["require_frames_increase"] = bool(
                st.get("require_frames_increase", True))
            before = st.get("before")
            if before is not None:
                if not isinstance(before, str) or before not in catalog_snaps:
                    _refuse(w, "'before' must name an earlier catalog_snapshot "
                               "step (known: %s)"
                            % (", ".join(sorted(catalog_snaps)) or "none"))
                body["before"] = before
        elif kind == "sleep":
            extra = set(st) - common - {"seconds"}
            if extra:
                _refuse(w, "unknown keys: %s" % ", ".join(sorted(extra)))
            secs = st.get("seconds", 0.5)
            if isinstance(secs, bool) or not isinstance(secs, (int, float)):
                _refuse(w, "seconds must be a number")
            if not 0.1 <= float(secs) <= 10.0:
                _refuse(w, "seconds must be 0.1..10.0")
            body["seconds"] = float(secs)
        else:  # note
            extra = set(st) - common
            if extra:
                _refuse(w, "unknown keys: %s" % ", ".join(sorted(extra)))
        steps.append(body)
    norm = {
        "schema": SCHEMA_SEQ, "name": name, "about": about, "profile": profile,
        "module_names": module_names, "require": norm_require,
        "load_params": norm_params, "steps": steps,
        "source": source, "sha256": doc.get("_sha256"),
    }
    return norm


def load(path: str) -> dict:
    """Load + validate a sequence JSON file; records its sha256 in the result."""
    path = os.path.abspath(path)
    if not os.path.isfile(path):
        raise G.GuardError("seq-missing", "sequence file not found: %s" % path)
    try:
        with open(path, "r", encoding="utf-8") as f:
            doc = json.load(f)
    except (OSError, ValueError) as e:
        raise G.GuardError("seq-parse", "cannot parse sequence %s: %s" % (path, e))
    if isinstance(doc, dict):
        doc["_sha256"] = sha256_file(path)
    return validate(doc, source=path)


def default_sequence_path() -> str:
    return os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "sequences", "ladder-c-ordered.json")


# ---------------------------------------------------------------------------
# Plan (dry-run) view + host-side write list
# ---------------------------------------------------------------------------
def plan_writes(seq: dict) -> list:
    """Return [(step_name, attr, value, about)] for every attr_write step."""
    out = []
    for st in seq["steps"]:
        if st["kind"] == "attr_write":
            out.append((st["name"], st["attr"], st["value"], st["about"]))
    return out


def check_names(seq: dict) -> list:
    """Every device-side gate/check name the sequence will emit, in order."""
    names = []
    for st in seq["steps"]:
        n = st["name"]
        if st["kind"] == "read_pack":
            names += ["%s.%s" % (n, r["label"]) for r in st["reads"]
                      if r["expect"]]
        elif st["kind"] in ("attr_expect", "attr_write"):
            names.append("%s.write" % n if st["kind"] == "attr_write" else
                         "%s.read" % n)
            names += ["%s.p%d" % (n, i) for i in range(len(st["patterns"]))]
        elif st["kind"] in ("catalog_snapshot", "sleep", "note"):
            names.append("%s.captured" % n)
        elif st["kind"] == "d2h_catalog":
            names += ["%s.e%d" % (n, i) for i in range(len(st["expect_entries"]))]
            if st["require_frames_increase"]:
                names.append("%s.frames" % n)
            names += ["%s.p%d" % (n, i) for i in range(len(st["device_patterns"]))]
    return names


def plan_doc(seq: dict) -> dict:
    steps = []
    total_checks = 0
    for st in seq["steps"]:
        entry = {"name": st["name"], "kind": st["kind"], "about": st["about"]}
        if st["kind"] == "read_pack":
            entry["reads"] = [
                {"label": r["label"],
                 "bar0": ("0x%08x" % r["bar0"]) if "bar0" in r else None,
                 "base": r.get("base"), "add": r.get("add"),
                 "expect": {k: (v if isinstance(v, bool) else _as_hex(v))
                            for k, v in r["expect"].items()} if r["expect"] else None,
                 "about": r["about"]}
                for r in st["reads"]]
        elif st["kind"] in ("attr_expect", "attr_write"):
            entry["attr"] = st["attr"]
            if st["kind"] == "attr_write":
                entry["value"] = st["value"]
            entry["patterns"] = st["patterns"]
        elif st["kind"] == "d2h_catalog":
            entry["expect_entries"] = st["expect_entries"]
            entry["before"] = st.get("before")
            entry["require_frames_increase"] = st["require_frames_increase"]
            entry["device_patterns"] = st["device_patterns"]
        elif st["kind"] == "sleep":
            entry["seconds"] = st["seconds"]
        steps.append(entry)
        total_checks += len(check_names({"steps": [st]}))
    return {"schema": SCHEMA_PLAN, "name": seq["name"], "sha256": seq["sha256"],
            "profile": seq["profile"], "about": seq["about"],
            "module_names": seq["module_names"], "require": seq["require"],
            "load_params": seq["load_params"], "steps": steps,
            "totals": {"steps": len(steps), "checks": total_checks,
                       "attr_writes": len(plan_writes(seq))}}


# ---------------------------------------------------------------------------
# Device script generation
# ---------------------------------------------------------------------------
def _shell_var(label: str) -> str:
    return "RD_" + label.replace("-", "_")


def _gate_num_lines(name: str, raw: str, expect: dict) -> list:
    mode = None
    want = 0
    for k in ("eq", "ne", "ge", "zero", "nonzero"):
        if k in expect:
            mode = k
            want = expect.get(k, 0) if k not in ("zero", "nonzero") else 0
            break
    mask = expect.get("mask", 0xFFFFFFFF)
    shift = (mask & -mask).bit_length() - 1 if mask else 0
    return ['  gate_num %s "%s" %s 0x%08X 0x%08X %d'
            % (_q(name), raw, mode, mask, want & 0xFFFFFFFF, shift)]


def _gate_attr_lines(name: str, attr: str, pattern: str) -> list:
    return ["  gate_attr %s %s %s" % (_q(name), _q(attr), _q(pattern))]


def render_script(seq: dict) -> list:
    """Emit the device-side shell lines for the sequence (protocol section only).

    Requires helpers in the generated script preamble: say, stop, section,
    w_attr, r_attr_as, rd32 (which also exports RDVAL as 8 hex digits),
    gate_attr, gate_num and the shell variable RD_<label>.
    """
    L = []
    a = L.append
    a("")
    a("section protocol")
    a('say "SEQUENCE name=%s sha256=%s steps=%d"'
      % (seq["name"], seq["sha256"], len(seq["steps"])))
    for st in seq["steps"]:
        n = st["name"]
        a("")
        a("section protocol-%s" % n)
        a('say "STEP name=%s kind=%s"' % (n, st["kind"]))
        if st.get("about"):
            a('say "STEP-ABOUT %s"' % st["about"].replace("\n", " "))
        kind = st["kind"]
        if kind == "read_pack":
            for r in st["reads"]:
                gname = "%s.%s" % (n, r["label"])
                if "bar0" in r:
                    a('rd32 %s 0x%X' % (gname, r["bar0"]))
                else:
                    basevar = _shell_var(r["base"])
                    off = "SEQ_OFF_%s" % _shell_var(r["label"])
                    a("SEQ_BASE_HEX=$%s" % basevar)
                    L.extend(pointer_shell())
                    a("%s=$((SEQ_OFF + 0x%X))" % (off, r["add"]))
                    a('if [ "$SEQ_CONV" = "unresolved" ]; then stop %s.bad-pointer '
                      '"unresolved firmware pointer convention"; fi' % n)
                    a('if [ "$%s" -lt 0 ] || [ "$%s" -ge %d ]; then stop '
                      '%s.bad-pointer "resolved offset outside the 16 MiB BAR0 '
                      'window"; fi' % (off, off, BAR0_MAX, n))
                    a('say "PTR-CONV %s base=0x$%s conv=$SEQ_CONV"'
                      % (gname, basevar))
                    a('rd32 %s "$%s"' % (gname, off))
                a("%s=$RDVAL" % _shell_var(r["label"]))
                if r["expect"]:
                    if "base" in r:
                        a('say "READ-OFF %s base=0x$%s add=0x%X conv=$SEQ_CONV"'
                          % (gname, basevar, r["add"]))
                    else:
                        a('say "READ-OFF %s 0x%X"' % (gname, r["bar0"]))
                    L.extend(_gate_num_lines(gname, "$%s" % _shell_var(r["label"]),
                                             r["expect"]))
                else:
                    a('say "GATE %s CAPTURED raw=0x$%s"'
                      % (gname, _shell_var(r["label"])))
        elif kind == "attr_expect":
            attr = st["attr"]
            a("r_attr_as seq-%s %s" % (n, attr))
            for i, p in enumerate(st["patterns"]):
                L.extend(_gate_attr_lines("%s.p%d" % (n, i), attr, p))
        elif kind == "attr_write":
            attr = st["attr"]
            a("w_attr %s %s %s" % (attr, _q(st["value"]), _q(n)))
            a('say "GATE %s.write PASS"' % n)
            if st["patterns"]:
                a("r_attr_as seq-%s-%s %s" % (n, attr, attr))
                for i, p in enumerate(st["patterns"]):
                    L.extend(_gate_attr_lines("%s.p%d" % (n, i), attr, p))
        elif kind == "catalog_snapshot":
            a("r_attr_as seq-%s-rd_catalog rd_catalog" % n)
            a("r_attr_as seq-%s-rd_decode rd_decode" % n)
            a('say "GATE %s.captured PASS"' % n)
        elif kind == "d2h_catalog":
            a("r_attr_as seq-%s-rd_catalog rd_catalog" % n)
            a("r_attr_as seq-%s-rd_decode rd_decode" % n)
            for i, p in enumerate(st["device_patterns"]):
                L.extend(_gate_attr_lines("%s.p%d" % (n, i), "rd_catalog", p))
            a('say "GATE %s.captured PASS"' % n)
        elif kind == "sleep":
            a("sleep %s" % _fmt_secs(st["seconds"]))
            a('say "GATE %s.captured PASS"' % n)
        elif kind == "note":
            a('say "NOTE %s: %s"' % (n, st.get("about", "").replace("\n", " ")))
            a('say "GATE %s.captured PASS"' % n)
    a("")
    a('say "SEQUENCE-END name=%s"' % seq["name"])
    return L


def _fmt_secs(v) -> str:
    v = float(v)
    if v.is_integer():
        return str(int(v))
    return ("%.3f" % v).rstrip("0").rstrip(".")


# ---------------------------------------------------------------------------
# Host-side evaluation of a captured report
# ---------------------------------------------------------------------------
def _read_section(reads: dict, name: str) -> str:
    return reads.get(name, "")


def _parse_catalog(text: str) -> dict:
    out = {"entries": {}, "frames": None, "id1_seen": None, "id2_seen": None}
    m = re.search(r"id1_seen=(\d+).*?id2_seen=(\d+).*?frames=(\d+)", text, re.S)
    if m:
        out["id1_seen"] = int(m.group(1))
        out["id2_seen"] = int(m.group(2))
        out["frames"] = int(m.group(3))
    for m in re.finditer(
            r"g=(\d+)\s+id=0x([0-9a-fA-F]+)\s+\(\s*\d+\)\s+count=(\d+)"
            r"(?:\s+last_total=0x([0-9a-fA-F]+))?", text):
        key = (int(m.group(1)), int(m.group(2), 16))
        out["entries"][key] = {
            "count": int(m.group(3)),
            "last_total": int(m.group(4), 16) if m.group(4) else None,
        }
    return out


def _check_read(read: dict, label_map: dict, rd32: dict, step: str) -> dict:
    name = "%s.%s" % (step, read["label"])
    actual = rd32.get(name)
    resolved = None
    conv = None
    base = None
    if "base" in read:
        base = label_map.get(read["base"])
        if base is not None:
            res = resolve_pointer(base)
            if res is not None:
                resolved = (res["bar0"] + read["add"]) & 0xFFFFFFFF
                conv = res["conv"]
    else:
        resolved = read["bar0"]
    rec = {"name": name, "kind": "read", "label": read["label"],
           "resolved_bar0": _as_hex(resolved) if resolved is not None else None,
           "conv": conv,
           "base": _as_hex(base) if base is not None else None,
           "expect": ({k: (v if isinstance(v, bool) else _as_hex(v))
                       for k, v in read["expect"].items()}
                      if read["expect"] else None),
           "actual": _as_hex(actual) if actual is not None else None,
           "about": read.get("about", "")}
    if "base" in read and base is None:
        rec.update(status="FAIL", code="read-missing",
                   detail="dynamic base label %r has no value (earlier read "
                          "missing or step not run)" % read["base"])
        return rec
    if "base" in read and resolved is None:
        rec.update(status="FAIL", code="resolved-pointer-outside-window",
                   detail="firmware pointer 0x%08x matches no inbound region "
                          "convention" % (base or 0))
        return rec
    if resolved is not None and "base" in read:
        try:
            G.check_bar0_read(resolved)
        except G.GuardError as e:
            rec.update(status="FAIL", code="resolved-pointer-outside-window",
                       detail=e.message)
            return rec
    if not read["expect"]:
        if actual is None and name in rd32:
            rec.update(status="MISSING", code="device-read-empty",
                       detail="capture label present but the device read was "
                              "EMPTY (raw read path failed)")
        elif actual is None:
            rec.update(status="MISSING", code="read-missing",
                       detail="capture label not present in the report")
        else:
            rec["status"] = "CAPTURED"
        return rec
    if actual is None:
        if name in rd32:
            rec.update(status="FAIL", code="device-read-empty",
                       detail="the device read for %s returned an EMPTY value "
                              "(raw BAR0 read path failed; the session stopped "
                              "at this gate)" % name)
        else:
            rec.update(status="FAIL", code="read-missing",
                       detail="label %s not present in the report (step not "
                              "run?)" % name)
        return rec
    exp = read["expect"]
    mask = exp.get("mask", 0xFFFFFFFF)
    got = int(actual) & mask
    ok = False
    detail = ""
    for mode in ("eq", "ne", "zero", "nonzero", "ge"):
        if mode not in exp:
            continue
        if mode == "zero":
            ok = got == 0
            detail = "masked(0x%08x) == 0" % got
        elif mode == "nonzero":
            ok = got != 0
            detail = "masked(0x%08x) != 0" % got
        elif mode == "eq":
            want = exp["eq"] & mask
            ok = got == want
            detail = "masked(0x%08x) == 0x%08x" % (got, want)
        elif mode == "ne":
            want = exp["ne"] & mask
            ok = got != want
            detail = "masked(0x%08x) != 0x%08x" % (got, want)
        else:  # ge
            want = exp["ge"] & mask
            ok = got >= want
            detail = "masked(0x%08x) >= 0x%08x" % (got, want)
        break
    rec.update(status="PASS" if ok else "FAIL",
               code=None if ok else "read-mismatch", detail=detail)
    return rec


CONTROL_LINE_RE = re.compile(
    r"^(?:STOP:|GATE-FAIL|GATE |SESSION_RESULT|FAIL:|READ-FAIL|== SECTION)")


def _capture_text(reads: dict, section: str) -> str:
    """Attribute capture text with control lines removed.

    The device script appends gate/stop lines to the same report section; a
    failed pattern appears verbatim in the STOP message, so the host evaluator
    must look only at the real attribute content (otherwise it would match its
    own failure message).
    """
    return "\n".join(ln for ln in _read_section(reads, section).splitlines()
                     if not CONTROL_LINE_RE.match(ln))


def _check_patterns(step_name: str, kind: str, attr: str, patterns: list,
                    section: str, reads: dict) -> list:
    text = _capture_text(reads, section)
    out = []
    for i, p in enumerate(patterns):
        name = "%s.p%d" % (step_name, i)
        try:
            ok = bool(re.search(p, text))
        except re.error as e:
            out.append({"name": name, "kind": "attr", "attr": attr,
                        "pattern": p, "status": "FAIL",
                        "code": "attr-pattern-missing",
                        "detail": "invalid host regex: %s" % e})
            continue
        out.append({"name": name, "kind": "attr", "attr": attr,
                    "pattern": p, "status": "PASS" if ok else "FAIL",
                    "code": None if ok else "attr-pattern-missing",
                    "detail": ("pattern matched in %s" % section) if ok
                    else ("pattern not found in %s (%d B captured)"
                          % (section, len(text)))})
    return out


def _check_catalog(step: dict, reads: dict) -> list:
    n = step["name"]
    text = _read_section(reads, "seq-%s-rd_catalog" % n)
    before_text = (_read_section(reads, "seq-%s-rd_catalog" % step["before"])
                   if step.get("before") else "")
    out = []
    if not text:
        out.append({"name": "%s.catalog" % n, "kind": "catalog",
                    "status": "FAIL", "code": "attr-capture-missing",
                    "detail": "rd_catalog capture for step %r is absent" % n})
        return out
    cat = _parse_catalog(text)
    before = _parse_catalog(before_text) if before_text else None
    if before is not None and step["require_frames_increase"]:
        if cat["frames"] is None or before["frames"] is None:
            out.append({"name": "%s.frames" % n, "kind": "catalog",
                        "status": "FAIL", "code": "attr-capture-missing",
                        "detail": "cannot read frames= from the before/after "
                                  "catalogue"})
        else:
            ok = cat["frames"] > before["frames"]
            out.append({"name": "%s.frames" % n, "kind": "catalog",
                        "status": "PASS" if ok else "FAIL",
                        "code": None if ok else "catalog-frames-not-increased",
                        "detail": "frames before=%d after=%d (%+d)"
                                  % (before["frames"], cat["frames"],
                                     cat["frames"] - before["frames"])})
    for i, e in enumerate(step["expect_entries"]):
        name = "%s.e%d" % (n, i)
        key = (e["group"], e["id"])
        post = cat["entries"].get(key)
        pre = before["entries"].get(key) if before else None
        ok = post is not None and post["count"] >= e["min_count"]
        detail = ("absent: no g=%d id=0x%02x entry in the catalogue"
                  % (e["group"], e["id"]) if post is None else
                  "g=%d id=0x%02x count=%d last_total=%s (min_count=%d%s)"
                  % (e["group"], e["id"], post["count"],
                     ("0x%04x" % post["last_total"])
                     if post["last_total"] is not None else "?",
                     e["min_count"],
                     (", before count=%d" % pre["count"]) if pre is not None
                     else ""))
        out.append({"name": name, "kind": "catalog", "group": e["group"],
                    "id": e["id"], "status": "PASS" if ok else "FAIL",
                    "code": None if ok else "catalog-entry-missing",
                    "detail": detail})
    for i, p in enumerate(step["device_patterns"]):
        name = "%s.p%d" % (n, i)
        try:
            ok = bool(re.search(p, text))
        except re.error:
            ok = False
        out.append({"name": name, "kind": "catalog", "pattern": p,
                    "status": "PASS" if ok else "FAIL",
                    "code": None if ok else "device-gate-fail",
                    "detail": "pattern matched" if ok else "pattern not found"})
    return out


def evaluate(seq: dict, summary: dict, plan=None) -> dict:
    """Re-evaluate a sequence from a parsed session report -> evidence record."""
    rd32 = summary.get("rd32") or {}
    reads = summary.get("reads") or {}
    result = summary.get("result")
    stop_rule = summary.get("stop_rule")
    device_gates = {}
    for g in summary.get("gates") or []:
        device_gates[g["name"]] = "PASS" if g.get("ok") else "FAIL"
    step_names = {st["name"] for st in seq["steps"]}
    pre_stop = None
    if result == "STOP" and stop_rule:
        rule_step = stop_rule.split(".")[0]
        if rule_step not in step_names:
            pre_stop = stop_rule
    label_map = {}
    steps_out = []
    failed_at = None
    checked = 0
    passed = 0
    for st in seq["steps"]:
        n = st["name"]
        checks = []
        kind = st["kind"]
        if kind == "read_pack":
            for r in st["reads"]:
                c = _check_read(r, label_map, rd32, n)
                checks.append(c)
                if c["status"] in ("PASS", "CAPTURED") and c.get("actual"):
                    label_map[r["label"]] = int(c["actual"], 16)
        elif kind == "attr_expect":
            checks = _check_patterns(n, kind, st["attr"], st["patterns"],
                                     "seq-%s" % n, reads)
            if not checks:
                text = _read_section(reads, "seq-%s" % n)
                checks = [{"name": "%s.read" % n, "kind": "attr",
                           "status": "PASS" if text else "FAIL",
                           "code": None if text else "attr-capture-missing",
                           "detail": "attribute %r captured" % st["attr"]}]
        elif kind == "attr_write":
            post = _read_section(reads, "seq-%s-%s" % (n, st["attr"]))
            write_ok = bool(post) or not st["patterns"]
            checks = [{"name": "%s.write" % n, "kind": "write",
                       "attr": st["attr"], "value": st["value"],
                       "status": "PASS" if write_ok else "FAIL",
                       "code": None if write_ok else "attr-capture-missing",
                       "detail": ("post-write capture present (%d B)" % len(post))
                       if post else "no post-write attribute capture"}]
            if st["patterns"]:
                checks += _check_patterns(n, kind, st["attr"], st["patterns"],
                                          "seq-%s-%s" % (n, st["attr"]), reads)
        elif kind == "catalog_snapshot":
            text = _read_section(reads, "seq-%s-rd_catalog" % n)
            checks = [{"name": "%s.captured" % n, "kind": "snapshot",
                       "status": "PASS" if text else "FAIL",
                       "code": None if text else "attr-capture-missing",
                       "detail": "rd_catalog captured (%d B)" % len(text)
                       if text else "no rd_catalog capture"}]
        elif kind == "d2h_catalog":
            checks = _check_catalog(st, reads)
        elif kind == "sleep":
            checks = [{"name": "%s.captured" % n, "kind": "sleep",
                       "status": "PASS", "code": None,
                       "detail": "waited %ss" % st["seconds"]}]
        else:  # note
            checks = [{"name": "%s.captured" % n, "kind": "note",
                       "status": "PASS", "code": None,
                       "detail": "documentation-only step"}]
        for c in checks:
            dev = device_gates.get(c["name"])
            c["device_gate"] = dev
            if dev == "FAIL" and c["status"] == "PASS":
                c["status"] = "FAIL"
                c["code"] = c["code"] or "device-gate-fail"
                c["detail"] += " (device gate FAILED)"
            checked += 1
            if c["status"] == "PASS":
                passed += 1
        n_fail = sum(1 for c in checks if c["status"] == "FAIL")
        if pre_stop is not None and all(
                c["status"] in ("FAIL", "MISSING")
                and c.get("code") in ("read-missing", "attr-capture-missing",
                                        "attr-pattern-missing",
                                        "device-read-empty")
                for c in checks):
            # nothing of the sequence ran: the boot ladder/readpath stopped it
            n_fail = 0
            for c in checks:
                c["status"] = "NOT-RUN"
                c["code"] = "step-not-run"
        status = "FAIL" if n_fail else "PASS"
        if pre_stop is not None and n_fail == 0 and failed_at is None:
            status = "NOT-RUN"
        if status == "FAIL" and failed_at is None:
            failed_at = n
        if failed_at is not None and n != failed_at:
            status = "NOT-RUN"
            for c in checks:
                c["status"] = "NOT-RUN"
                c["code"] = "step-not-run"
        steps_out.append({"name": n, "kind": kind, "about": st.get("about", ""),
                          "status": status, "checks": checks})
    if failed_at is None and result == "STOP" and stop_rule:
        prefix = stop_rule.split(".")[0]
        for s in steps_out:
            if s["name"] == prefix:
                s["status"] = "FAIL"
                if not any(c["status"] == "FAIL" for c in s["checks"]):
                    s["checks"].append({
                        "name": stop_rule, "kind": "device", "status": "FAIL",
                        "code": ("write-failed" if stop_rule == "attr-write-failed"
                                 else "device-gate-fail"),
                        "detail": "device stopped: %s" % stop_rule})
                failed_at = s["name"]
                break
    verdict = "PASS"
    if any(s["status"] == "FAIL" for s in steps_out) or result == "STOP":
        verdict = "FAIL"
    if result is None:
        verdict = "NO-DATA"
    boot = {
        "page": summary.get("page"),
        "state": summary.get("state"),
        "mask": summary.get("mask"),
        "mask_bits": summary.get("mask_bits"),
        "out": summary.get("out"),
        "ready": summary.get("ready"),
        "h2d": summary.get("h2d"),
        "rd_catalog": summary.get("rd_catalog"),
    }
    if pre_stop is not None:
        evidence_pre = {"pre_sequence_failure": pre_stop}
    else:
        evidence_pre = {}
    evidence = {
        "schema": SCHEMA_EVIDENCE, "sequence": seq["name"],
        "sequence_sha256": seq["sha256"], "session_result": result,
        "stop_rule": stop_rule, "failed_step": failed_at, "verdict": verdict,
        **evidence_pre,
        "steps": steps_out,
        "totals": {"steps": len(steps_out), "checks": checked,
                   "passed": passed,
                   "failed": sum(1 for s in steps_out
                                 for c in s["checks"]
                                 if c["status"] == "FAIL"),
                   "missing": sum(1 for s in steps_out
                                  for c in s["checks"]
                                  if c["status"] == "MISSING"),
                   "not_run": sum(1 for s in steps_out
                                  if s["status"] == "NOT-RUN")},
        "boot": boot, "device_gates": device_gates,
        "empty_reads": sorted(k for k, v in rd32.items() if v is None),
        "rd32": {k: (_as_hex(v) if v is not None else None)
                 for k, v in sorted(rd32.items())},
        "captures": sorted(k for k in reads if k.startswith("seq-")),
    }
    if plan is not None:
        evidence["session"] = {
            "mode": plan.mode, "session_id": plan.session_id,
            "module": plan.module, "firmware": plan.fw,
            "blocks": plan.blocks,
        }
    return evidence


# ---------------------------------------------------------------------------
# Evidence rendering
# ---------------------------------------------------------------------------
def _triage_for(evidence: dict) -> list:
    seen = []
    if evidence.get("empty_reads"):
        seen.append("device-read-empty")
    if evidence.get("pre_sequence_failure"):
        seen.append("pre-sequence-stop")
    for s in evidence.get("steps") or []:
        for c in s["checks"]:
            code = c.get("code")
            if code and code not in seen:
                seen.append(code)
    rows = []
    for code in seen:
        rows.append((code, TRIAGE.get(code, "Report the captured evidence; do "
                                            "not repeat the message blindly.")))
    return rows


def render_markdown(evidence: dict) -> str:
    L = []
    a = L.append
    a("# protocol sequence: %s" % evidence["sequence"])
    a("")
    a("- session: **%s**" % ((evidence.get("session") or {}).get("session_id")
                             or "?"))
    a("- sequence sha256: %s" % evidence["sequence_sha256"])
    a("- schema: %s" % SCHEMA_EVIDENCE)
    a("- SESSION_RESULT: **%s%s**"
      % (evidence.get("session_result"),
         (":" + evidence["stop_rule"]) if evidence.get("stop_rule") else ""))
    a("- protocol verdict: **%s**%s"
      % (evidence["verdict"],
         (" (failed at %s)" % evidence["failed_step"])
         if evidence.get("failed_step") else ""))
    t = evidence.get("totals") or {}
    a("- checks: %s total, %s passed, %s failed, %s step(s) not run"
      % (t.get("checks"), t.get("passed"), t.get("failed"), t.get("not_run")))
    a("")
    a("## Step table")
    a("")
    a("| # | step | kind | status | detail |")
    a("| --- | --- | --- | --- | --- |")
    for i, s in enumerate(evidence.get("steps") or [], 1):
        detail = "; ".join(c.get("detail", "") for c in s["checks"]
                           if c["status"] != "PASS")[:240] or \
            "; ".join(c.get("detail", "") for c in s["checks"])[:240]
        a("| %d | %s | %s | %s | %s |"
          % (i, s["name"], s["kind"], s["status"],
             detail.replace("|", "\\|")))
    a("")
    a("## Checks")
    a("")
    for s in evidence.get("steps") or []:
        a("### %s (%s) - %s" % (s["name"], s["kind"], s["status"]))
        a("")
        if not s["checks"]:
            a("- no checks")
        for c in s["checks"]:
            extra = ""
            if c.get("actual") is not None:
                extra = " actual=%s" % c["actual"]
            if c.get("device_gate"):
                extra += " device=%s" % c["device_gate"]
            a("- **%s** %s: %s%s" % (c["status"], c["name"],
                                     c.get("detail", ""), extra))
        a("")
    boot = evidence.get("boot") or {}
    a("## Boot / mailbox observables")
    a("")
    a("~~~json")
    a(json.dumps(boot, indent=1, sort_keys=True))
    a("~~~")
    a("")
    tri = _triage_for(evidence)
    if tri:
        a("## Failure triage")
        a("")
        for code, text in tri:
            a("- %s: %s" % (code, text))
        a("")
    a("## Notes")
    a("")
    a("- host-evaluated checks use the captured report text; a device PASS with")
    a("  a host FAIL is flagged as a disagreement (should not happen).")
    a("- no retry: a failed gate ends the session; a new idea needs a fresh boot.")
    a("")
    return "\n".join(L) + "\n"


# ---------------------------------------------------------------------------
# Embedded selftest of this module (device-free)
# ---------------------------------------------------------------------------
def _selftest() -> int:
    """Tiny embedded selftest (the lane selftest has the full matrix)."""
    doc = {
        "schema": SCHEMA_SEQ, "name": "selftest", "about": "unit",
        "module_names": ["hi5622_wifi"], "require": {"budget_min": 2},
        "load_params": {"h2d_send_budget": 2},
        "steps": [
            {"name": "pack", "kind": "read_pack", "reads": [
                {"label": "v", "bar0": "0x7D45E8"},
                {"label": "sig", "base": "v", "add": "0x10",
                 "expect": {"eq": "0x64"}}]},
            {"name": "pre", "kind": "attr_expect", "attr": "h2d_status",
             "patterns": ["budget=2"]},
            {"name": "fire", "kind": "attr_write", "attr": "h2d_ctx_create",
             "value": "1", "patterns": ["frames_sent=1"]},
        ],
    }
    seq = validate(doc, "selftest")
    n = len(check_names(seq))
    assert n == 5, n
    plan = plan_doc(seq)
    assert plan["totals"]["checks"] == 5
    assert plan["totals"]["attr_writes"] == 1
    summary = {
        "result": "PASS", "stop_rule": None,
        "rd32": {"pack.v": 0x011C458, "pack.sig": 0x64},
        "reads": {"seq-pre": "H2D send path: budget=2 frames_sent=0",
                  "seq-fire-h2d_ctx_create": "frames_sent=1 ctx_created=1"},
        "gates": [{"name": "pack.sig", "ok": True},
                  {"name": "pre.p0", "ok": True},
                  {"name": "fire.write", "ok": True},
                  {"name": "fire.p0", "ok": True}],
    }
    ev = evaluate(seq, summary)
    assert ev["verdict"] == "PASS", ev
    assert ev["totals"]["checks"] == 5, ev["totals"]
    assert ev["totals"]["passed"] == 4, ev["totals"]
    md = render_markdown(ev)
    assert "protocol sequence: selftest" in md
    bad = dict(summary)
    bad["rd32"] = {"pack.v": 0x011C458, "pack.sig": 0x65}
    ev2 = evaluate(seq, bad)
    assert ev2["verdict"] == "FAIL" and ev2["failed_step"] == "pack", ev2
    scripts = render_script(seq)
    assert any("gate_num" in ln for ln in scripts)
    assert any("gate_attr" in ln for ln in scripts)
    return 0


if __name__ == "__main__":
    raise SystemExit(_selftest())
