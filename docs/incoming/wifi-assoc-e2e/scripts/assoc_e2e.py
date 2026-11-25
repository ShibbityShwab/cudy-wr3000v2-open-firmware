#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""assoc_e2e.py - host-side client reliability harness for the Cudy WR3000 v2.0
association E2E antenna-test protocol (lane wifi-assoc-e2e, shared task-158).

HOST-ONLY: this tool drives ONLY the host workstation's Wi-Fi NIC (MT7922).
It never contacts the bench router: no SSH/UART/TFTP/flash/devmem/module work.
The router side is run by the Lead from the console with
scripts/router/assoc-router-fire.sh (a separate, clearly-labeled artifact).

Canonical client sequence implemented here (the reliability fix for the flaky
MT7922: NetworkManager/supplicant conflicts, stale p2p-dev state, EBUSY scans):

    preflight -> reset (NM unmanaged, one supplicant, down/up settle)
              -> scan (active, passive, full-band fallback)
              -> assoc (ONE dedicated wpa_supplicant, status+log polling)
              -> classify -> evidence -> cleanup/restore

Subcommands:
    preflight   read-only host/iface/wiphy check (root not required)
    reset       normalize the NIC: kill stale supplicants, NM unmanaged,
                down/up settle, power_save off, optional second-vif creation
    scan        scan for the bench AP and verify the expected RSN/security
    assoc       one association attempt + full classification (exit 0 only on
                a completed association)
    watch       repeated scan(+assoc) attempts for antenna alignment (RSSI CSV)
    cleanup     remove our supplicant/ctrl dirs and restore NM management
    verify      host-side self-check of the scanners/classifiers (no RF)
    status      read-only dump of iface/driver/NM/supplicant state

Global options: --dry-run --evidence-dir DIR --label TEXT --iface IF --base IF
                --use-base

Environment: ASSOC_IFACE, ASSOC_BASE_IF, ASSOC_PROFILE, ASSOC_PSK,
ASSOC_TIMEOUT, ASSOC_SCAN_ATTEMPTS, ASSOC_MIN_RSSI, ASSOC_FREQ,
ASSOC_EVIDENCE_DIR, ASSOC_DRY_RUN, ASSOC_SSID.

Exit codes: 0 = requested check/association PASSED; 1 = completed but the
association did not complete (terminal stage is diagnostic, see the JSON);
2 = harness/safety error. Association rungs and PASS/FAIL criteria are in
../MATRIX.md; the procedure is ../RUNBOOK.md.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import shutil
import signal
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

LANE_DIR = Path(__file__).resolve().parent.parent
CONFIGS_DIR = LANE_DIR / "configs"
PROFILES_FILE = CONFIGS_DIR / "profiles.json"

DEFAULT_PROFILES = {
    "freq": 2437,
    "channel": 6,
    "min_rssi_dbm": -70,
    "good_rssi_dbm": -55,
    "profiles": {
        "open": {"ssid": "wr3000v2-apstart", "security": "open",
                 "client_template": "wpa_supplicant-open.template.conf",
                 "rsn_expect": "none"},
        "wpa2": {"ssid": "cudy-ap-test", "security": "wpa2-psk",
                 "client_template": "wpa_supplicant-wpa2.template.conf",
                 "psk_default": "test-passphrase-121", "rsn_expect": "psk-ccmp"},
        "wpa3": {"ssid": "cudy-wr3000v2-wpa3", "security": "wpa3-sae",
                 "client_template": "wpa_supplicant-wpa3-sae.template.conf",
                 "psk_default": "test-passphrase-121", "rsn_expect": "sae-ccmp-mfp"},
    },
}

TERMINAL_STAGES = ["AUTH-TIMEOUT", "SAE-AUTH-TIMEOUT", "ASSOC-TIMEOUT",
                   "FOURWAY-FAILED", "DISCONNECTED", "NETWORK-NOT-FOUND",
                   "DRIVER-ERROR", "FAILED-UNKNOWN"]

PSK_PLACEHOLDERS = {"CHANGEME-lab-only", "CHANGEME", ""}


# --------------------------------------------------------------------------
# small helpers
# --------------------------------------------------------------------------
def load_profiles() -> dict:
    if PROFILES_FILE.is_file():
        try:
            return json.loads(PROFILES_FILE.read_text())
        except (OSError, json.JSONDecodeError) as exc:  # pragma: no cover
            print(f"warn: cannot parse {PROFILES_FILE}: {exc}; using built-ins",
                  file=sys.stderr)
    return DEFAULT_PROFILES


def resolve_profile(name: str) -> dict:
    prof = load_profiles()
    profiles = prof.get("profiles", {})
    if name not in profiles:
        raise SystemExit(f"unknown profile '{name}' (have: {', '.join(sorted(profiles))})")
    out = dict(profiles[name])
    out.setdefault("freq", prof.get("freq", 2437))
    out.setdefault("channel", prof.get("channel", 6))
    out.setdefault("min_rssi_dbm", prof.get("min_rssi_dbm", -70))
    out.setdefault("good_rssi_dbm", prof.get("good_rssi_dbm", -55))
    out["psk"] = os.environ.get("ASSOC_PSK") or out.get("psk_default", "")
    out["ssid"] = os.environ.get("ASSOC_SSID") or out["ssid"]
    out["freq"] = int(os.environ.get("ASSOC_FREQ") or out["freq"])
    out["channel"] = int(os.environ.get("ASSOC_CHANNEL") or out.get("channel", 6))
    if os.environ.get("ASSOC_MIN_RSSI"):
        out["min_rssi_dbm"] = float(os.environ["ASSOC_MIN_RSSI"])
    return out


def utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def now_iso() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def evidence_base() -> Path:
    env = os.environ.get("ASSOC_EVIDENCE_DIR")
    if env:
        return Path(env)
    try:
        repo = LANE_DIR.parents[3]
        if (repo / "build").is_dir():
            return repo / "build" / "wifi-assoc-e2e"
    except IndexError:
        pass
    return LANE_DIR / "local-evidence"


def which(cmd: str):
    return shutil.which(cmd)


def run_capture(cmd, timeout=30.0, env=None):
    return subprocess.run(cmd, capture_output=True, text=True,
                          timeout=timeout, env=env)


def read_text(path, limit=2_000_000) -> str:
    try:
        with open(path, "r", errors="replace") as fh:
            return fh.read(limit)
    except OSError:
        return ""


def redact(text: str) -> str:
    """Remove any psk material from evidence copies (lab value included)."""
    text = re.sub(r"(?m)^(\s*psk=).*$", r"\1***REDACTED***", text)
    text = re.sub(r"(wpa_passphrase=)\S+", r"\1***REDACTED***", text)
    return text


# --------------------------------------------------------------------------
# parsers / assessors (pure functions - unit-tested, no RF)
# --------------------------------------------------------------------------
BSS_RE = re.compile(r"^BSS ([0-9a-fA-F:]{17})")
RSN_ITEM_RE = re.compile(r"^\s*\* (.+)$")


def parse_iw_scan(text: str) -> list:
    """Parse 'iw dev X scan' output into BSS dicts (tolerant, text-based)."""
    bss_list = []
    cur = None
    in_rsn = False
    for raw in text.splitlines():
        line = raw.rstrip("\r")
        m = BSS_RE.match(line)
        if m:
            cur = {"bssid": m.group(1).lower(), "ssid": None, "freq": None,
                   "signal": None, "ds_channel": None, "privacy": False,
                   "rsn_present": False, "rsn_group_cipher": None,
                   "rsn_pairwise": [], "rsn_akms": [], "rsn_caps": "",
                   "mfp_required": False, "mfp_capable": False,
                   "probe_response": False}
            bss_list.append(cur)
            in_rsn = False
            continue
        if cur is None:
            continue
        raw_s = line.strip()
        # iw prints some fields inside the IE list with a leading "* " bullet
        # ("* DS Parameter set: channel 6"); normalize so both forms parse.
        s = raw_s[2:].strip() if raw_s.startswith("* ") else raw_s
        if s.startswith("freq:"):
            try:
                cur["freq"] = float(s.split()[1])
            except (IndexError, ValueError):
                pass
        elif s.startswith("signal:"):
            try:
                cur["signal"] = float(s.split()[1])
            except (IndexError, ValueError):
                pass
        elif s.startswith("SSID:"):
            cur["ssid"] = s[len("SSID:"):].strip().strip("q").rstrip("\x00")
        elif s.startswith("DS Parameter set: channel"):
            try:
                cur["ds_channel"] = int(s.split()[-1])
            except ValueError:
                pass
        elif s.startswith("capability:"):
            caps = s.split(":", 1)[1]
            cur["privacy"] = "Privacy" in caps
            cur["capability_raw"] = caps.strip()
        elif s.startswith("RSN:"):
            cur["rsn_present"] = True
            in_rsn = True
        elif in_rsn and raw_s.startswith("*"):
            item = RSN_ITEM_RE.match(line)
            if not item:
                continue
            val = item.group(1).strip()
            low = val.lower()
            if low.startswith("group cipher:"):
                cur["rsn_group_cipher"] = val.split(":", 1)[1].strip()
            elif low.startswith("pairwise ciphers:"):
                cur["rsn_pairwise"] = [x.strip() for x in
                                       val.split(":", 1)[1].split(",") if x.strip()]
            elif low.startswith("authentication suites:"):
                cur["rsn_akms"] = [x.strip() for x in
                                   val.split(":", 1)[1].split(",") if x.strip()]
            elif low.startswith("capabilities:"):
                cur["rsn_caps"] = val.split(":", 1)[1].strip()
                cur["mfp_required"] = "MFP-required" in cur["rsn_caps"]
                cur["mfp_capable"] = "MFP-capable" in cur["rsn_caps"]
            elif "group management cipher suite" in low:
                cur["group_mgmt_cipher"] = val
        elif s.startswith("Information elements from Probe Response"):
            cur["probe_response"] = True
        elif s.startswith("BSS ") or s.startswith("----"):
            in_rsn = False
    return bss_list


def assess_scan(scan_text: str, ssid: str, freq: float, channel: int,
                min_rssi: float, rsn_expect: str) -> dict:
    """Return {verdict, target, checks[], seen[]} for a scan result."""
    bss_list = parse_iw_scan(scan_text)
    target = next((b for b in bss_list if b.get("ssid") == ssid), None)
    checks = []
    seen = sorted({b.get("ssid") for b in bss_list if b.get("ssid")} - {None})[:25]

    def add(name, ok, detail):
        checks.append({"name": name, "ok": bool(ok), "detail": detail})

    if target is None:
        add("ssid_found", False, f"expected SSID {ssid!r} not present in scan results")
        return {"verdict": "FAIL", "target": None, "checks": checks,
                "seen": seen, "bss_count": len(bss_list)}

    add("ssid_found", True, f"SSID {ssid!r} seen")
    add("bssid_valid", bool(re.fullmatch(r"[0-9a-f]{2}(:[0-9a-f]{2}){5}",
                                         target["bssid"])),
        f"bssid={target['bssid']}")
    tf = target.get("freq")
    add("freq_expected", tf is not None and abs(tf - freq) < 1.0,
        f"freq={tf} (expected {freq})")
    dsc = target.get("ds_channel")
    add("ds_channel_expected", dsc in (None, channel),
        f"DS channel={dsc} (expected {channel})")
    rssi = target.get("signal")
    add("rssi_present", rssi is not None, f"signal={rssi} dBm")
    add("rssi_gate", rssi is not None and rssi >= min_rssi,
        f"signal={rssi} dBm (gate {min_rssi:+.0f} dBm)")
    if rssi is not None:
        add("rssi_good", rssi >= -55,
            f"signal={rssi} dBm (good >= -55 dBm)")

    akms = [a.upper() for a in target.get("rsn_akms", [])]
    pairwise = [p.upper() for p in target.get("rsn_pairwise", [])]
    if rsn_expect == "none":
        add("security_open", not target["privacy"] and not target["rsn_present"],
            f"privacy={target['privacy']} rsn_present={target['rsn_present']}")
    elif rsn_expect == "psk-ccmp":
        add("rsn_present", target["rsn_present"], "RSN IE present")
        add("akm_psk", "PSK" in akms, f"AKMs={akms}")
        add("pairwise_ccmp", "CCMP" in pairwise, f"pairwise={pairwise}")
        add("mfp_not_required", not target["mfp_required"],
            f"MFP-required={target['mfp_required']} (client config is ieee80211w=0)")
    elif rsn_expect == "sae-ccmp-mfp":
        add("rsn_present", target["rsn_present"], "RSN IE present")
        add("akm_sae", "SAE" in akms, f"AKMs={akms}")
        add("pairwise_ccmp", "CCMP" in pairwise, f"pairwise={pairwise}")
        add("mfp_required", target["mfp_required"],
            f"MFP-required={target['mfp_required']} (WPA3-SAE requires PMF)")
    hard = [c for c in checks if c["name"] in
            ("ssid_found", "freq_expected", "rssi_gate")]
    # rssi_good is informational (PASS is allowed between the RF gate and
    # -55 dBm, with the RF NOTE below); it must not turn a valid association
    # rung into WARN by itself.
    soft = [c for c in checks if c["name"] in
            ("bssid_valid", "ds_channel_expected",
             "rsn_present", "akm_psk", "akm_sae", "pairwise_ccmp",
             "mfp_required", "mfp_not_required", "security_open")]
    verdict = "FAIL"
    if all(c["ok"] for c in hard):
        verdict = "PASS" if all(c["ok"] for c in soft) else "WARN"
    return {"verdict": verdict, "target": target, "checks": checks,
            "seen": seen, "bss_count": len(bss_list)}


def parse_supplicant_status(text: str) -> dict:
    out = {}
    for line in text.splitlines():
        if "=" not in line:
            continue
        key, val = line.split("=", 1)
        out[key.strip()] = val.strip()
    out["wpa_state"] = out.get("wpa_state", "").upper()
    return out


def classify_supplicant(log_text: str, status: dict, variant: str) -> dict:
    """Classify one association attempt from the supplicant log + status."""
    log = log_text or ""
    state = status.get("wpa_state", "")

    def has(pat):
        return re.search(pat, log, re.M) is not None

    result = {"stage": "FAILED-UNKNOWN", "complete": False, "detail": ""}

    if state == "COMPLETED" or has(r"CTRL-EVENT-CONNECTED"):
        result.update(stage="COMPLETED", complete=True,
                      detail="wpa_state=COMPLETED / CTRL-EVENT-CONNECTED")
        return result

    if has(r"CTRL-EVENT-NETWORK-NOT-FOUND") or has(r"No suitable network found"):
        result.update(stage="NETWORK-NOT-FOUND",
                      detail="supplicant saw no matching BSS (RF or scan problem)")
        return result

    if has(r"nl80211: .*(failed|not supported)|Failed to initialize driver interface|"
           r"Operation not permitted|Device or resource busy"):
        m = re.search(r"(nl80211: [^\n]+|Failed to initialize[^\n]+|"
                      r"Operation not permitted|Device or resource busy)", log)
        result.update(stage="DRIVER-ERROR",
                      detail=(m.group(1)[:200] if m else "driver/interface error"))
        return result

    if has(r"WPA: 4-Way Handshake failed|pre-shared key may be incorrect|"
           r"4-Way Handshake.*timed out"):
        result.update(stage="FOURWAY-FAILED",
                      detail="4-way handshake failed (PSK mismatch or EAPOL "
                             "TX/RX/ring path; router-side data_tx/keys logs decide)")
        return result

    if has(r"Authentication with .* timed out|SME: Authentication timed out"):
        stage = "SAE-AUTH-TIMEOUT" if variant == "wpa3" else "AUTH-TIMEOUT"
        detail = ("client AUTH sent, no AP AUTH response (router mgmt-TX/RX path "
                  "or RF)" if variant != "wpa3" else
                  "SAE auth frames sent, no AP SAE response (router mgmt-TX/RX "
                  "path or RF)")
        result.update(stage=stage, detail=detail)
        return result

    if has(r"Association with .* timed out|Association request to the driver failed|"
           r"SME: Association timed out"):
        result.update(stage="ASSOC-TIMEOUT",
                      detail="AUTH exchanged, ASSOC request/response did not complete")
        return result

    if has(r"CTRL-EVENT-DISCONNECTED") and state not in ("", "COMPLETED"):
        reason = ""
        m = re.search(r"reason=(\d+)", log)
        if m:
            reason = f" reason={m.group(1)}"
        result.update(stage="DISCONNECTED",
                      detail=f"client disconnected during the handshake{reason}")
        return result

    stage = "SCAN"
    for marker, st in ((r"Trying to authenticate|SME: Trying to authenticate", "AUTH"),
                       (r"Trying to associate|SME: Trying to associate", "ASSOC"),
                       (r"Associated with|CTRL-EVENT-CONNECTED", "FOUR-WAY"),
                       (r"WPA: Key negotiation completed", "COMPLETED")):
        if has(marker):
            stage = st
    result.update(stage=stage,
                  detail=f"no terminal error captured; last known stage={stage}, "
                         f"wpa_state={state or 'unknown'}")
    return result


def render_wpa_conf(template: str, ssid: str, psk: str, ctrl: str, freq: int) -> str:
    out = template.replace("@SSID@", ssid).replace("@PSK@", psk)
    out = out.replace("@CTRL@", ctrl).replace("@FREQ@", str(freq))
    return out


def validate_lane_secrets() -> list:
    """No real credential may be tracked: scan the lane for suspicious PSK lines."""
    bad = []
    for path in LANE_DIR.rglob("*"):
        if not path.is_file() or path.suffix == ".pyc":
            continue
        if ".git" in path.parts:
            continue
        try:
            text = path.read_text(errors="replace")
        except OSError:
            continue
        for i, line in enumerate(text.splitlines(), 1):
            m = re.match(r"^\s*(psk|wpa_passphrase|password)=(\S+)", line)
            if not m:
                continue
            val = m.group(2).strip('"')
            if val in PSK_PLACEHOLDERS or val in ("test-passphrase-121",):
                continue
            if "template" in path.name:
                continue
            bad.append(f"{path.relative_to(LANE_DIR)}:{i}: non-lab credential-like value")
    return bad


def parse_proc_cmdline(pid: str) -> list:
    data = read_text(f"/proc/{pid}/cmdline")
    return [a for a in data.split("\x00") if a]


def supplicant_pids_for_iface(iface: str) -> list:
    pids = []
    for entry in os.listdir("/proc"):
        if not entry.isdigit():
            continue
        try:
            args = parse_proc_cmdline(entry)
        except OSError:
            continue
        if not args or "wpa_supplicant" not in os.path.basename(args[0]):
            continue
        joined = " ".join(args)
        if re.search(r"(?:^|\s)-i\s*" + re.escape(iface) + r"(?:\s|$)", joined) or \
           re.search(r"(?:^|\s)" + re.escape(iface) + r"(?:\s|$)", joined):
            try:
                pids.append(int(entry))
            except ValueError:
                pass
    return pids


# --------------------------------------------------------------------------
# runner
# --------------------------------------------------------------------------
class HarnessError(RuntimeError):
    def __init__(self, message: str, rc: int = 2):
        super().__init__(message)
        self.rc = rc


class Runner:
    def __init__(self, name: str, args: argparse.Namespace):
        self.name = name
        self.args = args
        self.dry = bool(getattr(args, "dry_run", False) or
                        os.environ.get("ASSOC_DRY_RUN") == "1")
        label = getattr(args, "label", "") or os.environ.get("ASSOC_LABEL", "")
        base = Path(args.evidence_dir) if getattr(args, "evidence_dir", None) \
            else evidence_base()
        self.ev = base / (utc_stamp() + (("-" + label) if label else ""))
        self.logs = []
        if not self.dry:
            self.ev.mkdir(parents=True, exist_ok=True)
        self.log_path = self.ev / f"{name}.log"

    def write(self, name: str, text: str) -> Path:
        path = self.ev / name
        if not self.dry:
            try:
                path.write_text(redact(text))
            except OSError:
                pass
        return path

    def log(self, msg: str) -> None:
        line = f"[{self.name}] {msg}"
        print(line)
        self.logs.append(line)

    def section(self, title: str) -> None:
        self.log("")
        self.log(f"=== {title} ===")

    def flush_log(self) -> None:
        if self.dry:
            return
        try:
            with open(self.log_path, "a") as fh:
                fh.write("\n".join(self.logs) + "\n")
                fh.write(f"# finished {now_iso()} dry_run={int(self.dry)}\n")
        except OSError:
            pass

    def run(self, cmd, timeout=30.0, allow_fail=False, env=None):
        printable = " ".join(shlex.quote(c) for c in cmd)
        self.log(f"+ {printable}")
        if self.dry:
            self.log("  (dry-run: not executed)")
            return subprocess.CompletedProcess(cmd, 0, "", "")
        try:
            proc = run_capture(cmd, timeout=timeout, env=env)
        except FileNotFoundError as exc:
            raise HarnessError(f"command not found: {cmd[0]}") from exc
        except subprocess.TimeoutExpired:
            if allow_fail:
                self.log(f"  TIMEOUT after {timeout}s (allowed)")
                return subprocess.CompletedProcess(cmd, 124, "", "timeout")
            raise HarnessError(f"command timed out after {timeout}s: {printable}") from None
        for stream, data in (("out", proc.stdout), ("err", proc.stderr)):
            if data:
                lines = data.rstrip().splitlines()
                for line in lines[:40]:
                    self.log(f"  {stream}| {line}")
                if len(lines) > 40:
                    self.log(f"  {stream}| ... ({len(lines) - 40} more lines in evidence)")
        if proc.returncode != 0 and not allow_fail:
            raise HarnessError(f"command failed rc={proc.returncode}: {printable}")
        return proc

    def capture(self, cmd, timeout=30.0, env=None):
        return self.run(cmd, timeout=timeout, allow_fail=True, env=env)

    def root(self) -> None:
        if self.dry:
            self.log("dry-run: root requirement skipped")
            return
        if os.geteuid() != 0:
            raise HarnessError("live RF phases need root: run with sudo")


# --------------------------------------------------------------------------
# host / iface discovery
# --------------------------------------------------------------------------
def iw_devices(r: Runner) -> dict:
    """Return {iface: {phy, type, addr}} from 'iw dev'."""
    proc = r.capture(["iw", "dev"], timeout=10)
    out = {}
    phy = None
    cur = None
    for line in proc.stdout.splitlines():
        m = re.match(r"^phy#(\d+)$", line.strip())
        if m:
            phy = f"phy{m.group(1)}"
            continue
        m = re.match(r"\s*Interface (\S+)", line)
        if m:
            cur = {"phy": phy, "type": None, "addr": None}
            out[m.group(1)] = cur
            continue
        if cur is None:
            continue
        s = line.strip()
        if s.startswith("addr "):
            cur["addr"] = s.split()[1]
        elif s.startswith("type "):
            cur["type"] = s.split()[1]
    return out


def resolve_phy(iface: str):
    p = Path(f"/sys/class/net/{iface}/phy80211")
    try:
        return p.resolve().name
    except OSError:
        return None


def resolve_base_iface(r: Runner, target: str) -> str:
    base = os.environ.get("ASSOC_BASE_IF") or getattr(r.args, "base", None)
    if base:
        return base
    cands = [i for i in iw_devices(r) if i != target]
    for pref in ("wlan0", "wlp", "wl"):
        for cand in cands:
            if cand.startswith(pref):
                return cand
    if cands:
        return cands[0]
    if r.dry:
        return os.environ.get("ASSOC_BASE_IF") or "wlan0"
    raise HarnessError("no base Wi-Fi interface found: set ASSOC_BASE_IF/--base")


def resolve_target_iface(r: Runner) -> str:
    want = getattr(r.args, "iface", None) or os.environ.get("ASSOC_IFACE") or "auto"
    devs = iw_devices(r)
    if want != "auto":
        return want
    second = os.environ.get("ASSOC_OBSERVER_IFACE") or "wlan1"
    if second in devs:
        return second
    if getattr(r.args, "use_base", False):
        return os.environ.get("ASSOC_BASE_IF") or getattr(r.args, "base", None) or "wlan0"
    return second


def parse_wiphy_combinations(text: str) -> list:
    combos = []
    grab = False
    for line in text.splitlines():
        if "valid interface combinations" in line:
            grab = True
            continue
        if grab:
            s = line.strip()
            if not s:
                break
            if s.startswith("*"):
                combos.append(s)
    return combos


def wiphy_info(r: Runner, phy: str) -> dict:
    proc = r.capture(["iw", "phy", phy, "info"], timeout=15)
    text = proc.stdout
    combos = parse_wiphy_combinations(text)
    has_2437 = "2437" in text
    managed2 = any(re.search(r"#\{\s*managed[^}]*\}\s*<=\s*2", c) for c in combos)
    return {"phy": phy, "combos": combos, "has_2437": has_2437,
            "two_managed": managed2}


# --------------------------------------------------------------------------
# subcommands
# --------------------------------------------------------------------------
def cmd_preflight(r: Runner) -> int:
    r.section("host tools")
    required = ["iw", "wpa_supplicant", "wpa_cli", "ip"]
    optional = ["nmcli", "rfkill", "modprobe", "sudo", "python3"]
    missing = []
    for tool in required:
        path = which(tool)
        r.log(f"tool {tool:<14} {'OK ' + path if path else 'MISSING'}")
        if not path:
            missing.append(tool)
    for tool in optional:
        r.log(f"tool {tool:<14} {which(tool) or 'not present (optional)'}")
    if missing and not r.dry:
        raise HarnessError("missing required tools: " + ", ".join(missing))

    r.section("interfaces")
    devs = iw_devices(r)
    for iface, info in devs.items():
        r.log(f"iface {iface:<10} type={info['type']} phy={info['phy']} addr={info['addr']}")
    if not devs:
        r.log("no wireless interfaces in 'iw dev' (dry-run?)")
    target = resolve_target_iface(r)
    base = os.environ.get("ASSOC_BASE_IF") or getattr(r.args, "base", None) or "wlan0"
    r.log(f"client iface: {target}   base/uplink iface: {base}")
    if not r.dry and target not in devs:
        r.log(f"NOTE: client iface {target} does not exist yet; "
              f"'reset' will add it on {resolve_phy(base) or 'the base phy'} "
              f"(or pass --use-base to drop the uplink instead)")
    r.section("wiphy")
    phy = resolve_phy(target) if target in devs else resolve_phy(base)
    if phy:
        info = wiphy_info(r, phy)
        r.log(f"phy={info['phy']} two_managed_combo={info['two_managed']} "
              f"has_2437MHz={info['has_2437']}")
        for c in info["combos"]:
            r.log(f"  combo: {c}")
        if not info["has_2437"] and not r.dry:
            r.log("FAIL: phy does not advertise 2437 MHz (channel 6)")
        if not info["two_managed"]:
            r.log("NOTE: wiphy may not allow a second managed iface; "
                  "if 'reset' gets -EBUSY, run with --use-base")
    else:
        r.log("wiphy unavailable (dry-run or no Wi-Fi device)")

    r.section("NetworkManager / rfkill")
    if which("nmcli"):
        proc = r.capture(["nmcli", "-t", "-f", "DEVICE,STATE,CONNECTION", "device"],
                         timeout=10)
        for line in proc.stdout.splitlines():
            parts = line.split(":")
            if parts and parts[0] in (target, base):
                r.log(f"nmcli {parts[0]}: {line}")
    if which("rfkill"):
        proc = r.capture(["rfkill", "list"], timeout=10)
        for line in proc.stdout.splitlines():
            if "Wireless" in line or "phy" in line:
                r.log(f"rfkill: {line.strip()}")

    r.section("lane sanity")
    bad = validate_lane_secrets()
    if bad:
        for item in bad:
            r.log(f"FAIL lane-secret scan: {item}")
        raise HarnessError("tracked lane contains a non-lab credential-like value")
    r.log("lane-secret scan: PASS (only published lab placeholder values)")
    r.log("profiles: " + ", ".join(sorted(load_profiles()["profiles"])))
    r.log("PREFLIGHT PASS (read-only)")
    return 0


def cmd_status(r: Runner) -> int:
    r.section("devices")
    r.run(["iw", "dev"], allow_fail=True)
    r.section("wpa_supplicant processes")
    proc = r.capture(["ps", "wax"], timeout=10)
    for line in proc.stdout.splitlines():
        if "wpa_supplicant" in line and "grep" not in line:
            r.log(line.strip())
    r.section("NetworkManager wifi devices")
    if which("nmcli"):
        r.run(["nmcli", "-t", "-f", "DEVICE,TYPE,STATE,CONNECTION", "device"],
              allow_fail=True)
    return 0


def cmd_reset(r: Runner) -> int:
    r.root()
    target = resolve_target_iface(r)
    use_base = bool(getattr(r.args, "use_base", False))
    base_env = os.environ.get("ASSOC_BASE_IF") or getattr(r.args, "base", None) or "wlan0"
    base = resolve_base_iface(r, target) if target != base_env else base_env
    r.section(f"reset client iface {target}")
    r.log(f"base/uplink iface: {base}  use_base={use_base}")

    state = {"iface": target, "base": base, "applied": False,
             "created_iface": False, "nm_managed_before": None,
             "base_disconnected": False, "started": now_iso()}
    state_dir = Path(os.environ.get("ASSOC_STATE_DIR", "/run/wifi-assoc-e2e"))
    if not r.dry:
        try:
            state_dir.mkdir(parents=True, exist_ok=True)
        except OSError:
            state_dir = Path("/tmp/wifi-assoc-e2e")
            state_dir.mkdir(parents=True, exist_ok=True)

    pids = supplicant_pids_for_iface(target)
    for pid in pids:
        r.log(f"stale wpa_supplicant pid {pid} on {target}: terminating")
        if not r.dry:
            try:
                os.kill(pid, signal.SIGTERM)
            except OSError:
                pass
    if pids and not r.dry:
        time.sleep(1.0)
        for pid in supplicant_pids_for_iface(target):
            r.log(f"stale wpa_supplicant pid {pid} survived SIGTERM: SIGKILL")
            try:
                os.kill(pid, signal.SIGKILL)
            except OSError:
                pass
    for ctrl in (f"/run/wpa_supplicant-assoc-{target}",
                 f"/run/wpa_supplicant/{target}"):
        if Path(ctrl).exists():
            r.log(f"removing stale ctrl dir {ctrl}")
            if not r.dry:
                shutil.rmtree(ctrl, ignore_errors=True)

    if which("rfkill"):
        r.run(["rfkill", "unblock", "wifi"], allow_fail=True)

    if which("nmcli"):
        proc = r.capture(["nmcli", "-t", "-f", "GENERAL.NM-MANAGED",
                          "device", "show", target], timeout=10)
        for line in proc.stdout.splitlines():
            if line.startswith("GENERAL.NM-MANAGED:"):
                state["nm_managed_before"] = line.split(":", 1)[1]
        r.log(f"NM managed before: {state['nm_managed_before']}")
        if state["nm_managed_before"] == "yes":
            r.run(["nmcli", "device", "set", target, "managed", "no"])
        if use_base and target == base:
            r.log(f"use-base: disconnecting NM on {base} (host uplink will drop)")
            r.run(["nmcli", "device", "disconnect", base], allow_fail=True)
            state["base_disconnected"] = True

    if not r.dry and not Path(f"/sys/class/net/{target}").exists():
        if Path(f"/sys/class/net/{base}").exists():
            phy = resolve_phy(base)
            if not phy:
                raise HarnessError(f"cannot resolve the phy of {base} to create {target}")
            r.log(f"creating {target} on {phy} (managed)")
            proc = r.run(["iw", "phy", phy, "interface", "add", target,
                          "type", "managed"], allow_fail=True)
            if proc.returncode != 0:
                raise HarnessError(
                    f"cannot create {target} on {phy} (rc={proc.returncode}); "
                    "the wiphy may not allow a second managed iface while the "
                    "uplink is connected - rerun with --use-base to use the base "
                    "interface instead (drops the host uplink)")
            state["created_iface"] = True
            time.sleep(0.5)

    if not r.dry and not Path(f"/sys/class/net/{target}").exists():
        raise HarnessError(f"client iface {target} missing after reset")
    r.run(["ip", "link", "set", target, "down"], allow_fail=True)
    if not r.dry:
        time.sleep(0.5)
    r.run(["ip", "link", "set", target, "up"], allow_fail=True)
    if not r.dry:
        time.sleep(1.5)
    r.run(["iw", "dev", target, "set", "power_save", "off"], allow_fail=True)

    devs = iw_devices(r)
    info = devs.get(target, {})
    r.log(f"iface {target}: present={target in devs} type={info.get('type')} "
          f"phy={info.get('phy')}")
    if not r.dry and target in devs and info.get("type") != "managed":
        r.run(["iw", "dev", target, "set", "type", "managed"], allow_fail=True)
    left = supplicant_pids_for_iface(target)
    if left:
        r.log(f"WARN: supplicant still bound to {target}: {left}")
    state["applied"] = True
    if not r.dry:
        (state_dir / f"state-{target}.json").write_text(json.dumps(state, indent=2) + "\n")
    r.write("reset-state.json", json.dumps(state, indent=2) + "\n")
    r.log("RESET DONE - one supplicant will be started by 'assoc'; "
          "run 'cleanup' afterwards to restore NetworkManager")
    return 0


def _scan_attempts(iface: str, freq: int) -> list:
    q = ["iw", "dev", iface]
    return [
        ("active-single-channel", q + ["scan", "-u", "freq", str(freq)], 20.0),
        ("passive-single-channel", q + ["scan", "-u", "freq", str(freq),
                                        "duration", "1500", "passive"], 20.0),
        ("active-full-band", q + ["scan", "-u"], 35.0),
    ]


def cmd_scan(r: Runner) -> int:
    r.root()
    profile = resolve_profile(getattr(r.args, "profile", None) or "wpa2")
    iface = resolve_target_iface(r)
    attempts_wanted = int(os.environ.get("ASSOC_SCAN_ATTEMPTS", "3") or 3)
    r.section(f"scan for {profile['ssid']!r} on {profile['freq']} MHz via {iface}")
    if r.dry:
        for name, cmd, _timeout in _scan_attempts(iface, int(profile["freq"])):
            r.log("plan: " + name + " -> " + " ".join(shlex.quote(c) for c in cmd))
        r.log("dry-run: scan plan printed; no RF executed")
        return 0
    if not Path(f"/sys/class/net/{iface}").exists():
        raise HarnessError(f"client iface {iface} missing (run 'reset' first)")
    scan_text = ""
    last_err = ""
    attempts = _scan_attempts(iface, int(profile["freq"]))
    attempts = attempts[:max(1, min(attempts_wanted, len(attempts)))]
    for name, cmd, timeout in attempts:
        r.log(f"attempt: {name}")
        proc = r.capture(cmd, timeout=timeout)
        if proc.returncode != 0:
            last_err = (proc.stderr or proc.stdout or "").strip()
            r.log(f"  scan rc={proc.returncode}: {last_err.splitlines()[0] if last_err else ''}")
            if "busy" in last_err.lower() or "(-16)" in last_err:
                r.log("  EBUSY: interface still busy; mini-settle down/up and retry once")
                r.run(["ip", "link", "set", iface, "down"], allow_fail=True)
                if not r.dry:
                    time.sleep(0.5)
                r.run(["ip", "link", "set", iface, "up"], allow_fail=True)
                if not r.dry:
                    time.sleep(1.5)
                proc = r.capture(cmd, timeout=timeout)
            if proc.returncode != 0:
                continue
        if proc.stdout.strip():
            scan_text = proc.stdout
            r.write(f"scan-{profile['security']}-{name}.txt", proc.stdout)
            break
    if not scan_text and not r.dry:
        raise HarnessError(f"all scan attempts failed on {iface}; last error: {last_err}")

    result = assess_scan(scan_text, profile["ssid"], profile["freq"],
                         profile["channel"], profile["min_rssi_dbm"],
                         profile["rsn_expect"])
    result.update({"iface": iface, "ts": now_iso(), "profile": profile["security"],
                   "expected_ssid": profile["ssid"],
                   "expected_freq": profile["freq"],
                   "expected_channel": profile["channel"]})
    if result["target"]:
        result["rssi_dbm"] = result["target"]["signal"]
        result["bssid"] = result["target"]["bssid"]
    r.section("scan verdict")
    for check in result["checks"]:
        r.log(f"  [{'ok' if check['ok'] else 'FAIL'}] {check['name']}: {check['detail']}")
    r.log("target: " + (json.dumps(result["target"]) if result["target"] else "NOT FOUND"))
    if result["seen"]:
        r.log("other SSIDs in scan: " + ", ".join(result["seen"]))
    r.write(f"scan-{profile['security']}.json", json.dumps(result, indent=2) + "\n")
    r.log(f"SCAN {result['verdict']}" + (
        f" rssi={result.get('rssi_dbm')} dBm" if result.get("rssi_dbm") is not None else ""))
    if result.get("rssi_dbm") is not None and result["rssi_dbm"] < -55:
        r.log("RF NOTE: weak signal (< -55 dBm): attach/align antennas before the "
              "association rung; watch RSSI with 'watch --scan-only'")
    r.log("STOP RULE: if RSSI < gate -70 dBm, do not run 'assoc' - fix RF first")
    return 0 if result["verdict"] in ("PASS", "WARN") else 1


def _start_supplicant(r: Runner, iface: str, conf: Path, logf: Path, pidf: Path):
    return r.run(["wpa_supplicant", "-B", "-D", "nl80211", "-i", iface,
                  "-c", str(conf), "-P", str(pidf), "-f", str(logf), "-d"],
                 allow_fail=True, timeout=20)


def _stop_supplicant(r: Runner, iface: str, ctrl: str, pidf: Path) -> None:
    if which("wpa_cli"):
        r.capture(["wpa_cli", "-p", ctrl, "-i", iface, "terminate"], timeout=5)
    if pidf.exists():
        try:
            pid = int(pidf.read_text().strip() or 0)
        except (OSError, ValueError):
            pid = 0
        if pid:
            try:
                os.kill(pid, signal.SIGTERM)
            except OSError:
                pass
    for pid in supplicant_pids_for_iface(iface):
        try:
            os.kill(pid, signal.SIGTERM)
        except OSError:
            pass
    if not r.dry:
        time.sleep(0.5)
    for pid in supplicant_pids_for_iface(iface):
        try:
            os.kill(pid, signal.SIGKILL)
        except OSError:
            pass


def _assoc_once(r: Runner, iface: str, profile: dict, timeout: float, ctrl: str) -> dict:
    ev = r.ev
    psk = profile.get("psk", "")
    if profile["security"] != "open" and (not psk or psk in PSK_PLACEHOLDERS):
        raise HarnessError(
            f"profile {profile['security']} needs a lab PSK: export ASSOC_PSK "
            f"or use profiles.json psk_default (never a real credential)")
    template = read_text(CONFIGS_DIR / profile["client_template"])
    if not template:
        raise HarnessError(f"client template missing: {profile['client_template']}")
    conf_text = render_wpa_conf(template, profile["ssid"], psk, ctrl,
                                int(profile["freq"]))
    staged_dir = ev / "staged"
    if not r.dry:
        staged_dir.mkdir(parents=True, exist_ok=True)
        os.chmod(staged_dir, 0o700)
    conf = staged_dir / "wpa_supplicant.conf"
    logf = ev / "supplicant.log"
    pidf = staged_dir / "wpa_supplicant.pid"
    if not r.dry:
        conf.write_text(conf_text)
        os.chmod(conf, 0o600)
        logf.write_text("")
    r.write("supplicant-config.redacted.txt", redact(conf_text))

    result = {"ts": now_iso(), "iface": iface, "profile": profile["security"],
              "ssid": profile["ssid"], "complete": False,
              "stage": "FAILED-UNKNOWN", "detail": "", "status": {},
              "bssid_seen": None, "rssi_dbm": None}
    start = _start_supplicant(r, iface, conf, logf, pidf)
    if not r.dry and start.returncode != 0:
        log_text = read_text(logf)
        result.update({"stage": "DRIVER-ERROR",
                       "detail": f"wpa_supplicant did not start: rc={start.returncode}"})
        r.write("supplicant.log", log_text)
        return result
    if not r.dry:
        time.sleep(0.8)

    deadline = time.monotonic() + timeout
    status = {}
    stage = "SCAN"
    polls = 0
    while not r.dry and time.monotonic() < deadline:
        polls += 1
        if which("wpa_cli"):
            sp = r.capture(["wpa_cli", "-p", ctrl, "-i", iface, "status"], timeout=5)
            status = parse_supplicant_status(sp.stdout or "")
            state = status.get("wpa_state", "")
            if state:
                r.log(f"poll {polls}: wpa_state={state} bssid={status.get('bssid','')} "
                      f"freq={status.get('freq','')}")
            if state == "COMPLETED":
                break
        log_text = read_text(logf)
        partial = classify_supplicant(log_text, status, profile["security"])
        if partial["stage"] in TERMINAL_STAGES:
            r.log(f"poll {polls}: terminal stage {partial['stage']} - {partial['detail']}")
            break
        if partial["stage"] != stage:
            stage = partial["stage"]
            r.log(f"poll {polls}: stage -> {stage}")
        if not r.dry and not supplicant_pids_for_iface(iface):
            r.log("supplicant process exited")
            break
        if not r.dry:
            time.sleep(0.5)

    log_text = read_text(logf)
    r.write("supplicant.log", log_text)
    result.update(classify_supplicant(log_text, status, profile["security"]))
    if which("iw"):
        link = r.capture(["iw", "dev", iface, "link"], timeout=10)
        r.write("iw-link.txt", link.stdout)
        m = re.search(r"Connected to ([0-9a-f:]{17})", link.stdout)
        if m:
            result["bssid_seen"] = m.group(1)
        m = re.search(r"signal:\s*(-?\d+(?:\.\d+)?)\s*dBm", link.stdout)
        if m:
            result["rssi_dbm"] = float(m.group(1))
    if which("ip"):
        addr = r.capture(["ip", "-4", "addr", "show", iface], timeout=10)
        r.write("iface-addr.txt", addr.stdout)
    result["status"] = status
    result["polls"] = polls
    _stop_supplicant(r, iface, ctrl, pidf)
    if not r.dry:
        try:
            conf.unlink()
        except OSError:
            pass
    if which("dmesg") and not r.dry:
        d = r.capture(["dmesg"], timeout=10)
        lines = d.stdout.splitlines()[-200:]
        r.write("client-dmesg-tail.txt", "\n".join(lines) + "\n")
    return result


def cmd_assoc(r: Runner) -> int:
    r.root()
    profile = resolve_profile(getattr(r.args, "profile", None) or "wpa2")
    iface = resolve_target_iface(r)
    timeout = float(os.environ.get("ASSOC_TIMEOUT") or
                    getattr(r.args, "timeout", None) or 60)
    retries = max(1, int(getattr(r.args, "retries", 1) or 1))
    r.section(f"association: profile={profile['security']} ssid={profile['ssid']!r} "
              f"iface={iface} timeout={timeout:.0f}s retries={retries}")
    if r.dry:
        r.log("plan: one dedicated wpa_supplicant on " + iface +
              " -> poll wpa_cli status + -d log -> classify -> stop supplicant")
        r.log("plan: run 'reset' first and 'cleanup' after; evidence -> " + str(r.ev))
        return 0
    if not Path(f"/sys/class/net/{iface}").exists():
        raise HarnessError(f"client iface {iface} missing (run 'reset' first)")
    ctrl = f"/run/wpa_supplicant-assoc-{iface}"
    outcome = None
    for attempt in range(1, retries + 1):
        r.log(f"attempt {attempt}/{retries}")
        outcome = _assoc_once(r, iface, profile, timeout, ctrl)
        r.write(f"assoc-result-attempt{attempt}.json",
                json.dumps(outcome, indent=2) + "\n")
        if outcome["complete"]:
            break
        if attempt < retries:
            r.log("attempt failed - short reset before the retry (one supplicant at a time)")
            r.capture(["ip", "link", "set", iface, "down"], timeout=10)
            if not r.dry:
                time.sleep(1.0)
            r.capture(["ip", "link", "set", iface, "up"], timeout=10)
            if not r.dry:
                time.sleep(1.5)
    assert outcome is not None
    r.section("association result")
    r.log(f"stage       : {outcome['stage']}")
    r.log(f"complete    : {outcome['complete']}")
    r.log(f"detail      : {outcome['detail']}")
    if outcome.get("bssid_seen"):
        r.log(f"bssid       : {outcome['bssid_seen']}")
    if outcome.get("rssi_dbm") is not None:
        r.log(f"signal      : {outcome['rssi_dbm']} dBm")
    r.log("router-side evidence for this attempt (Lead console): "
          "assoc-router-fire.sh --stage snapshot")
    r.write("assoc-result.json", json.dumps(outcome, indent=2) + "\n")
    if outcome["complete"]:
        r.log("ASSOC PASS - client completed the association && key negotiation")
        return 0
    r.log("ASSOC NOT COMPLETED - diagnostic result (not a harness error); "
          "see ../DIAGNOSIS.md for the symptom->cause mapping")
    return 1


def cmd_watch(r: Runner) -> int:
    r.root()
    profile = resolve_profile(getattr(r.args, "profile", None) or "wpa2")
    iface = resolve_target_iface(r)
    iterations = max(1, int(getattr(r.args, "iterations", 10) or 10))
    interval = float(getattr(r.args, "interval", 3) or 3)
    scan_only = bool(getattr(r.args, "scan_only", False))
    rows = []
    r.section(f"watch: {iterations} iterations, interval {interval:.0f}s, "
              f"scan_only={scan_only} iface={iface}")
    for i in range(1, iterations + 1):
        scan_proc = r.capture(["iw", "dev", iface, "scan", "-u",
                               "freq", str(profile["freq"])], timeout=20)
        result = assess_scan(scan_proc.stdout or "", profile["ssid"],
                             profile["freq"], profile["channel"],
                             profile["min_rssi_dbm"], profile["rsn_expect"])
        tgt = result.get("target")
        rssi = tgt.get("signal") if tgt else None
        row = {"iteration": i, "ts": now_iso(), "seen": bool(tgt),
               "rssi_dbm": rssi, "verdict": result["verdict"]}
        if not scan_only:
            outcome = _assoc_once(r, iface, profile,
                                  float(os.environ.get("ASSOC_TIMEOUT") or 45),
                                  f"/run/wpa_supplicant-assoc-{iface}")
            row.update({"stage": outcome["stage"],
                        "complete": outcome["complete"]})
        rows.append(row)
        r.log(f"[{i}/{iterations}] seen={row['seen']} rssi={rssi} "
              f"verdict={row['verdict']}" + (
                  f" stage={row.get('stage')} complete={row.get('complete')}"
                  if "stage" in row else ""))
        if i < iterations and not r.dry:
            time.sleep(interval)
    csv = "iteration,ts,seen,rssi_dbm,verdict,stage,complete\n"
    for row in rows:
        csv += f"{row['iteration']},{row['ts']},{int(row['seen'])},{row['rssi_dbm']},"\
               f"{row['verdict']},{row.get('stage','')},{row.get('complete','')}\n"
    r.write("watch.csv", csv)
    good = [x["rssi_dbm"] for x in rows if x["rssi_dbm"] is not None]
    if good:
        r.log(f"RSSI: best={max(good):.1f} worst={min(good):.1f} last={good[-1]:.1f} dBm")
        if max(good) < -70:
            r.log("RF GATE FAIL: no sample reached -70 dBm; fix antennas/positioning "
                  "before the association rung")
    if not scan_only:
        done = sum(1 for x in rows if x.get("complete"))
        r.log(f"completed associations: {done}/{iterations}")
    return 0


def cmd_cleanup(r: Runner) -> int:
    r.root()
    target = resolve_target_iface(r)
    r.section(f"cleanup {target}")
    ctrl = f"/run/wpa_supplicant-assoc-{target}"
    _stop_supplicant(r, target, ctrl, r.ev / "staged" / "wpa_supplicant.pid")
    for d in (ctrl, f"/run/wpa_supplicant/{target}"):
        if Path(d).exists():
            r.log(f"removing {d}")
            if not r.dry:
                shutil.rmtree(d, ignore_errors=True)
    state_dir = Path(os.environ.get("ASSOC_STATE_DIR", "/run/wifi-assoc-e2e"))
    state_file = state_dir / f"state-{target}.json"
    state = {}
    if state_file.is_file():
        try:
            state = json.loads(state_file.read_text())
        except (OSError, json.JSONDecodeError):
            state = {}
    if which("nmcli"):
        r.run(["nmcli", "device", "set", target, "managed", "yes"], allow_fail=True)
        if state.get("base_disconnected"):
            base = state.get("base", "wlan0")
            r.log(f"reconnecting NM on base iface {base}")
            r.run(["nmcli", "device", "connect", base], allow_fail=True)
    if state.get("created_iface") and not r.dry and \
            Path(f"/sys/class/net/{target}").exists():
        r.log(f"removing the second vif {target} created by reset")
        r.run(["iw", "dev", target, "del"], allow_fail=True)
    if state_file.exists() and not r.dry:
        try:
            state_file.unlink()
        except OSError:
            pass
    r.log("cleanup done. If the host uplink is still down: "
          "sudo nmcli device connect wlan0 (or nmcli networking on)")
    return 0


# --------------------------------------------------------------------------
# host self-check (pure logic, no RF)
# --------------------------------------------------------------------------
def _load_fixtures_module():
    tests_dir = LANE_DIR / "tests"
    sys.path.insert(0, str(tests_dir))
    import fixtures  # type: ignore
    return fixtures


def self_checks() -> list:
    fx = _load_fixtures_module()
    checks = []

    def chk(name, cond, detail=""):
        checks.append((name, bool(cond), detail))

    bss = parse_iw_scan(fx.IW_SCAN_TARGET_WPA2)
    chk("parse.scan.bss_count", len(bss) == fx.IW_SCAN_TARGET_WPA2_EXPECT,
        f"got {len(bss)}")
    tgt = next((b for b in bss if b.get("ssid") == "cudy-ap-test"), None)
    chk("parse.scan.target_found", tgt is not None)
    if tgt:
        chk("parse.scan.target_freq", tgt["freq"] == 2437.0, str(tgt["freq"]))
        chk("parse.scan.target_rssi", tgt["signal"] == -63.0, str(tgt["signal"]))
        chk("parse.scan.target_psk", "PSK" in [a.upper() for a in tgt["rsn_akms"]],
            str(tgt["rsn_akms"]))
        chk("parse.scan.target_ccmp", "CCMP" in tgt["rsn_pairwise"],
            str(tgt["rsn_pairwise"]))
    res = assess_scan(fx.IW_SCAN_TARGET_WPA2, "cudy-ap-test", 2437, 6, -70, "psk-ccmp")
    chk("assess.wpa2_pass", res["verdict"] == "PASS", res["verdict"])
    res_weak = assess_scan(fx.IW_SCAN_WEAK_WPA2, "cudy-ap-test", 2437, 6, -70, "psk-ccmp")
    chk("assess.weak_fail", res_weak["verdict"] == "FAIL", res_weak["verdict"])
    res_absent = assess_scan(fx.IW_SCAN_OTHER, "cudy-ap-test", 2437, 6, -70, "psk-ccmp")
    chk("assess.absent_fail", res_absent["verdict"] == "FAIL" and res_absent["target"] is None)
    res_open = assess_scan(fx.IW_SCAN_TARGET_OPEN, "wr3000v2-apstart", 2437, 6, -70, "none")
    chk("assess.open_pass", res_open["verdict"] == "PASS", res_open["verdict"])
    res_sae = assess_scan(fx.IW_SCAN_TARGET_WPA3, "cudy-wr3000v2-wpa3", 2437, 6,
                          -70, "sae-ccmp-mfp")
    chk("assess.wpa3_pass", res_sae["verdict"] == "PASS", res_sae["verdict"])
    if res_sae.get("target"):
        chk("assess.wpa3_mfp_required", res_sae["target"]["mfp_required"],
            str(res_sae["target"]["rsn_caps"]))
    res_wrong = assess_scan(fx.IW_SCAN_TARGET_WPA3, "cudy-wr3000v2-wpa3",
                            2437, 6, -70, "psk-ccmp")
    chk("assess.profile_mismatch_warn", res_wrong["verdict"] == "WARN",
        res_wrong["verdict"])

    cases = [
        ("complete.wpa2", fx.WPA2_COMPLETED_LOG, fx.WPA2_COMPLETED_STATUS, "wpa2",
         "COMPLETED"),
        ("auth-timeout.wpa2", fx.WPA2_AUTH_TIMEOUT_LOG, fx.WPA2_AUTH_TIMEOUT_STATUS,
         "wpa2", "AUTH-TIMEOUT"),
        ("assoc-timeout.wpa2", fx.WPA2_ASSOC_TIMEOUT_LOG, fx.WPA2_ASSOC_TIMEOUT_STATUS,
         "wpa2", "ASSOC-TIMEOUT"),
        ("4way-failed.wpa2", fx.WPA2_4WAY_FAILED_LOG, fx.WPA2_4WAY_FAILED_STATUS,
         "wpa2", "FOURWAY-FAILED"),
        ("auth-timeout.wpa3", fx.WPA3_AUTH_TIMEOUT_LOG, fx.WPA3_AUTH_TIMEOUT_STATUS,
         "wpa3", "SAE-AUTH-TIMEOUT"),
        ("net-not-found", fx.WPA2_NOT_FOUND_LOG, fx.WPA2_NOT_FOUND_STATUS, "wpa2",
         "NETWORK-NOT-FOUND"),
        ("driver-error", fx.DRIVER_ERROR_LOG, fx.DRIVER_ERROR_STATUS, "wpa2",
         "DRIVER-ERROR"),
    ]
    for name, log, status_text, variant, expect in cases:
        out = classify_supplicant(log, parse_supplicant_status(status_text), variant)
        chk(f"classify.{name}", out["stage"] == expect, f"{out['stage']} != {expect}")
        chk(f"classify.{name}.complete", out["complete"] == (expect == "COMPLETED"),
            str(out["complete"]))

    for tmpl, prof in (("wpa_supplicant-open.template.conf", "open"),
                       ("wpa_supplicant-wpa2.template.conf", "wpa2"),
                       ("wpa_supplicant-wpa3-sae.template.conf", "wpa3")):
        text = read_text(CONFIGS_DIR / tmpl)
        chk(f"config.template.{prof}.present", bool(text))
        rendered = render_wpa_conf(text, "lab-ssid", "lab-psk", "/run/x", 2437)
        chk(f"config.template.{prof}.substituted",
            "@SSID@" not in rendered and "@PSK@" not in rendered and
            "@CTRL@" not in rendered and "@FREQ@" not in rendered)
        chk(f"redact.{prof}", "lab-psk" not in redact(rendered))
    chk("secrets.lane_scan", not validate_lane_secrets(),
        "; ".join(validate_lane_secrets()))

    prof = load_profiles()
    chk("profiles.count", len(prof["profiles"]) == 3, str(list(prof["profiles"])))
    for name in ("open", "wpa2", "wpa3"):
        chk(f"profiles.{name}.ssid", bool(prof["profiles"][name].get("ssid")))
    return checks


def cmd_verify(r: Runner) -> int:
    checks = self_checks()
    r.section("verify: host self-check (no RF)")
    failed = 0
    for name, ok, detail in checks:
        r.log(f"  [{'PASS' if ok else 'FAIL'}] {name}" +
              (f"  ({detail})" if detail and not ok else ""))
        failed += 0 if ok else 1
    r.log(f"self-check: {len(checks) - failed}/{len(checks)} PASS")
    r.write("host-selftest.txt",
            "\n".join(f"{'PASS' if ok else 'FAIL'} {name} {detail}"
                      for name, ok, detail in checks) + "\n")
    if failed:
        r.log(f"VERIFY FAIL ({failed} failing checks)")
        return 1
    r.log("VERIFY PASS")
    return 0


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------
def add_common(p: argparse.ArgumentParser) -> None:
    p.add_argument("--iface", help="client interface (default: auto -> second vif, "
                                   "or $ASSOC_IFACE)")
    p.add_argument("--base", help="base/uplink Wi-Fi interface (default: auto -> "
                                  "first non-client managed iface)")
    p.add_argument("--use-base", action="store_true",
                   help="use the base (uplink) interface as the client; NM is "
                        "disconnected first and reconnected by cleanup")
    p.add_argument("--evidence-dir", help="evidence directory override")
    p.add_argument("--label", help="evidence run label")
    p.add_argument("--dry-run", action="store_true",
                   help="print the planned commands, execute nothing")


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="Host-side client reliability harness for the WR3000 v2 "
                    "association E2E antenna-test protocol (HOST-ONLY: never "
                    "contacts the bench router).",
        epilog="Exit codes: 0 PASS, 1 association/scan diagnostic, 2 harness error.")
    sub = p.add_subparsers(dest="command", required=True)

    for name, hlp in (("preflight", "read-only host/iface/wiphy check"),
                      ("status", "read-only iface/supplicant/NM dump"),
                      ("reset", "normalize the NIC (NM unmanaged, one supplicant)"),
                      ("cleanup", "stop our supplicant and restore NM"),
                      ("verify", "host self-check of parsers/classifiers")):
        sp = sub.add_parser(name, help=hlp)
        add_common(sp)

    sp = sub.add_parser("scan", help="scan for the bench AP + verify the RSN IEs")
    add_common(sp)
    sp.add_argument("--profile", default=os.environ.get("ASSOC_PROFILE", "wpa2"),
                    choices=sorted(load_profiles()["profiles"]))

    sp = sub.add_parser("assoc", help="one association attempt + classification")
    add_common(sp)
    sp.add_argument("--profile", default=os.environ.get("ASSOC_PROFILE", "wpa2"),
                    choices=sorted(load_profiles()["profiles"]))
    sp.add_argument("--timeout", type=float, default=None,
                    help="seconds to wait for COMPLETED (default $ASSOC_TIMEOUT or 60)")
    sp.add_argument("--retries", type=int, default=2,
                    help="attempts, each with a fresh supplicant (default 2)")

    sp = sub.add_parser("watch", help="repeat scan(+assoc) for antenna alignment")
    add_common(sp)
    sp.add_argument("--profile", default=os.environ.get("ASSOC_PROFILE", "wpa2"),
                    choices=sorted(load_profiles()["profiles"]))
    sp.add_argument("--iterations", type=int, default=10)
    sp.add_argument("--interval", type=float, default=3.0)
    sp.add_argument("--scan-only", action="store_true",
                    help="only report RSSI (no association attempts)")
    return p


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    handlers = {"preflight": cmd_preflight, "status": cmd_status,
                "reset": cmd_reset, "scan": cmd_scan, "assoc": cmd_assoc,
                "watch": cmd_watch, "cleanup": cmd_cleanup,
                "verify": cmd_verify}
    r = Runner(args.command, args)
    try:
        rc = handlers[args.command](r)
    except HarnessError as exc:
        r.log(f"HARNESS ERROR: {exc}")
        rc = exc.rc
    except KeyboardInterrupt:
        r.log("interrupted")
        rc = 2
    finally:
        r.flush_log()
        if not r.dry:
            r.log(f"evidence dir: {r.ev}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
