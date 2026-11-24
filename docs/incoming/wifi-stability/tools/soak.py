#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""soak.py - long-run AP soak harness + offline analyzer (wifi-stability lane).

Shared task task-136, Cudy WR3000 v2.0 RE project.  HOST-ONLY by construction:
--dry-run/--analyze/--selftest never contact the device; --run is an attended,
explicitly flagged bench action (--run --yes-really --attended
--recovery-armed).

What it does
  dry-run   validate the bring-up + sampler plan, statically audit every device
            action (writer allowlist; rmmod/devmem/flash/network refused),
            print the exact commands.  No device contact.
  analyze   offline analysis of a sampler TSV + dmesg copy into a PASS/FAIL
            verdict with a metrics table (this is what --run uses).
  selftest  see tools/soak_selftest.py (imports this module).
  run       attended one-command soak: preflight identity, single-boot AP
            bring-up (image driver + PD4 firmware staged by the operator),
            read-only sampler for --duration seconds, then analysis.

PASS criteria (policy defaults, see POLICY):
  * golden boot page 0x86807FEF/7/2/0xABCD, state 1, mask 0, staging 0 in every
    sample (any deviation => FAIL-EXCEPTION, stop, power-cycle per BENCH-FACTS)
  * no reboot (uptime monotonic)
  * heartbeat (g4 id4) continuous: rate >= 0.2/s and no gap > 15 s
  * D2H direct reader overruns == 0, d2h drop == 0, frames monotonic
  * hostapd alive (>=1 proc) and iw dev wlan0 info shows type AP + the ssid
    in every sample
  * memory drift bounded (MemAvailable and hostapd RSS), no fatal/error dmesg
  * duration/sample coverage complete (no sampling gaps)

HARD RULES: the sampler and the analyzer are read-only; the bring-up performs
only the documented boot-ladder/C2/AP writes.  Never rmmod after release, never
write CA 0x400392f0, never read the RC misc window 0x10161000, no flash/UART.
"""
from __future__ import annotations

import argparse
import datetime
import json
import os
import re
import shlex
import subprocess
import sys
import time

SCHEMA = "hi5622-wifi-soak/1"
ENDPOINT = "0000:00:00.0"
GOLDEN_PAGE = ("0x86807fef", "0x00000007", "0x00000002", "0x0000abcd")
GOLDEN_STATE = "0x00000001"
PD4_SHA = "cfe24bfb194899f362bfcb0301d7e28a3b4dce0ef4e70b705d54a1f78d3037b1"
PD4_LEN = 928920
SOAK_HEADER = ("# wifi-soak/1 uptime page0 page1 page2 page3 state mask staging hb "
               "excp frames overruns d2h_drop phase memavail memfree load1 hapdm "
               "hapd_rss_kb hapd_cpu ap_ok dm_new")
FIELDS = SOAK_HEADER.split()[2:]
REMOTE_DIR = "/tmp/wifi-soak"
DEFAULT_SSID = "cudy-ap-test"

POLICY = {
    "interval_s": 10,
    "duration_s": 3600,
    "sample_coverage": 0.95,        # samples vs duration/interval
    "max_sample_gap_s": 30.0,       # 3x interval
    "hb_min_rate_per_s": 0.2,       # bench baseline ~1.01/s
    "hb_max_gap_s": 15.0,
    "frames_min_ratio": 0.5,        # frames >= 0.5 * duration (heartbeat ~1/s)
    "mem_max_drift_kb": 8192,       # + 128 KB per soak minute (dynamic)
    "hapd_rss_max_growth_kb": 4096,
    "hostapd_min_procs": 1,
    "ap_ssid": DEFAULT_SSID,
}

FATAL_DMESG = re.compile(
    r"(BUG:|Oops|kernel panic|Call trace|Unable to handle|Internal error|"
    r"WARNING:|watchdog|EXCEPTION|exception mask|golden page|page_lost|"
    r"lane closed|announce NOT consumed|wire not ready|not posted|REFUSED)",
    re.IGNORECASE)


# ---------------------------------------------------------------------------
# sampler TSV parsing
# ---------------------------------------------------------------------------
def parse_samples(path: str):
    rows = []
    end = {}
    with open(path, "r", errors="replace") as f:
        for raw in f:
            line = raw.rstrip("\n")
            if not line:
                continue
            if line.startswith("#END"):
                for tok in line.split()[1:]:
                    if "=" in tok:
                        k, v = tok.split("=", 1)
                        end[k] = v
                continue
            if line.startswith("# wifi-soak/"):
                if line.strip() != SOAK_HEADER:
                    raise SystemExit("soak: unexpected sampler header: %r" % line)
                continue
            if line.startswith("#"):
                continue
            vals = line.split(" ")
            if len(vals) != len(FIELDS):
                raise SystemExit("soak: row has %d fields, want %d: %r"
                                 % (len(vals), len(FIELDS), line[:120]))
            row = {}
            for name, val in zip(FIELDS, vals):
                if name == "phase":
                    row[name] = val
                elif name == "load1":
                    try:
                        row[name] = float(val)
                    except ValueError:
                        row[name] = None
                else:
                    try:
                        row[name] = int(val)
                    except ValueError:
                        row[name] = val
            rows.append(row)
    return rows, end


def parse_dmesg(path: str):
    if not path or not os.path.isfile(path):
        return []
    with open(path, "r", errors="replace") as f:
        return [ln.rstrip("\n") for ln in f if ln.strip()]


# ---------------------------------------------------------------------------
# analyzer
# ---------------------------------------------------------------------------
def _is_num(v):
    return isinstance(v, int)


def analyze(rows, dmesg_lines, meta, policy=None):
    """Return the machine-readable soak verdict (dict)."""
    pol = dict(POLICY)
    if policy:
        pol.update(policy)
    duration = float(meta.get("duration_s") or pol["duration_s"])
    interval = float(meta.get("interval_s") or pol["interval_s"])
    checks = []

    def chk(cid, ok, detail):
        checks.append({"id": cid, "status": "PASS" if ok else "FAIL",
                       "detail": detail})
        return ok

    metrics = {}
    if not chk("samples", len(rows) >= 2, "rows=%d" % len(rows)):
        return _verdict("FAIL-SAMPLING", checks, metrics,
                        "no sampler rows", duration, pol)

    t0 = rows[0].get("uptime") or 0
    t1 = rows[-1].get("uptime") or 0
    span = float(t1) - float(t0)
    metrics["rows"] = len(rows)
    metrics["span_s"] = span
    chk("duration", span >= duration * 0.98,
        "span=%.0fs duration=%.0fs" % (span, duration))
    want = duration / interval
    got = len(rows) - 1
    chk("sample-coverage", got >= want * pol["sample_coverage"],
        "samples=%d want>=%.1f" % (got, want * pol["sample_coverage"]))

    gaps = []
    reboot = False
    for a, b in zip(rows, rows[1:]):
        ua, ub = a.get("uptime"), b.get("uptime")
        if not _is_num(ua) or not _is_num(ub) or ub <= ua:
            reboot = True
            continue
        gaps.append(float(ub) - float(ua))
    chk("no-reboot", not reboot, "uptime monotonic across %d rows" % len(rows))
    bad_gaps = [g for g in gaps if g > pol["max_sample_gap_s"]
                or g < interval * 0.5]
    chk("sample-gaps", not bad_gaps,
        "gaps min/max=%s/%s bad=%d"
        % ("%.1f" % min(gaps) if gaps else "-",
           "%.1f" % max(gaps) if gaps else "-", len(bad_gaps)))

    state_bad = []
    for r in rows:
        page = (r.get("page0"), r.get("page1"), r.get("page2"), r.get("page3"))
        state = r.get("state")
        mask = r.get("mask")
        stg = r.get("staging")
        if not all(_is_num(x) for x in page) or not all(
                _is_num(x) for x in (state, mask, stg)):
            state_bad.append("uptime=%s bar0read-unavailable" % r.get("uptime"))
            continue
        gotpage = tuple("0x%08x" % int(v) for v in page)
        if (gotpage != GOLDEN_PAGE or "0x%08x" % int(state) != GOLDEN_STATE
                or int(mask) != 0 or int(stg) != 0):
            state_bad.append(
                "uptime=%s page=%s/%s/%s/%s state=%s mask=%s staging=%s"
                % (r.get("uptime"), gotpage[0], gotpage[1], gotpage[2],
                   gotpage[3], state, mask, stg))
    metrics["boot_state_violations"] = len(state_bad)
    chk("boot-page", not state_bad,
        state_bad[0] if state_bad else "golden page/state/mask/staging in all rows")

    hb = [(int(r["uptime"]), r["hb"]) for r in rows
          if _is_num(r.get("uptime")) and _is_num(r.get("hb"))]
    hb_gap = 0.0
    hb_ok = True
    hb_detail = "n/a"
    if len(hb) >= 2:
        prev_t, prev_c = hb[0]
        min_rate = 1e9
        gap_run = 0.0
        for t, c in hb[1:]:
            dc = c - prev_c
            dt = t - prev_t
            if dc < 0:
                hb_ok = False
                hb_detail = "heartbeat count went backwards at t=%s" % t
                gap_run = 0.0
            elif dc == 0:
                gap_run += dt
                hb_gap = max(hb_gap, gap_run)
            else:
                gap_run = 0.0
                min_rate = min(min_rate, dc / dt if dt else 0.0)
            prev_t, prev_c = t, c
        if min_rate == 1e9:
            min_rate = 0.0
        metrics["hb_min_rate_per_s"] = min_rate
        metrics["hb_max_gap_s"] = hb_gap
        hb_ok = (hb_ok and hb_gap <= pol["hb_max_gap_s"]
                 and min_rate >= pol["hb_min_rate_per_s"])
        hb_detail = "min_rate=%.2f/s max_gap=%.0fs" % (min_rate, hb_gap)
    else:
        hb_ok = False
    chk("heartbeat", hb_ok, hb_detail)

    overruns = sum(int(r["overruns"]) for r in rows if _is_num(r.get("overruns")))
    drops = sum(int(r["d2h_drop"]) for r in rows if _is_num(r.get("d2h_drop")))
    frames = [int(r["frames"]) for r in rows if _is_num(r.get("frames"))]
    mono = all(b >= a for a, b in zip(frames, frames[1:]))
    last_frames = frames[-1] if frames else 0
    d2h_ok = (overruns == 0 and drops == 0 and mono
              and last_frames >= duration * pol["frames_min_ratio"])
    metrics.update({"d2h_overruns": overruns, "d2h_drops": drops,
                    "d2h_frames": last_frames})
    chk("d2h", d2h_ok,
        "overruns=%d drop=%d frames=%d monotonic=%s"
        % (overruns, drops, last_frames, mono))

    excp = 0
    for r in rows:
        if _is_num(r.get("excp")):
            excp = max(excp, int(r["excp"]))
    metrics["excp_info"] = excp
    chk("excp-info", excp == 0, "g4 id3 exception_info frames=%d" % excp)

    ssid = meta.get("ssid") or pol["ap_ssid"]
    ap_bad = []
    for r in rows:
        if (r.get("hapdm") or 0) < pol["hostapd_min_procs"] or r.get("ap_ok") != 1:
            ap_bad.append("uptime=%s hostapd_n=%s ap_ok=%s"
                          % (r.get("uptime"), r.get("hapdm"), r.get("ap_ok")))
    chk("ap-liveness", not ap_bad,
        ap_bad[0] if ap_bad else
        "hostapd procs>=%d and iface type AP/ssid %s"
        % (pol["hostapd_min_procs"], ssid))

    phases = sorted({str(r.get("phase")) for r in rows})
    chk("hcc-phase", phases == ["ACTIVE"], "phases=%s" % ",".join(phases))

    mem = [int(r["memavail"]) for r in rows if _is_num(r.get("memavail"))]
    rss = [int(r["hapd_rss_kb"]) for r in rows if _is_num(r.get("hapd_rss_kb"))]
    drift_cap = pol["mem_max_drift_kb"] + int(duration / 60.0) * 128
    mem_drift = (mem[0] - mem[-1]) if len(mem) >= 2 else 0
    rss_growth = (rss[-1] - rss[0]) if len(rss) >= 2 else 0
    metrics.update({"memavail_kb_first": mem[0] if mem else 0,
                    "memavail_kb_last": mem[-1] if mem else 0,
                    "mem_drift_kb": mem_drift,
                    "hapd_rss_growth_kb": rss_growth})
    chk("memory",
        mem_drift <= drift_cap and rss_growth <= pol["hapd_rss_max_growth_kb"],
        "mem_drift=%dKB<=%dKB rss_growth=%dKB<=%dKB"
        % (mem_drift, drift_cap, rss_growth, pol["hapd_rss_max_growth_kb"]))

    hits = [ln for ln in dmesg_lines if FATAL_DMESG.search(ln)]
    metrics["dmesg_hits"] = len(hits)
    chk("dmesg", not hits,
        ("%d anomaly line(s); first: %s" % (len(hits), hits[0][:160]))
        if hits else "no fatal/error dmesg lines")

    failed = [c for c in checks if c["status"] == "FAIL"]
    if not failed:
        return _verdict("PASS", checks, metrics, "", duration, pol)
    order = [
        ("boot-page", "FAIL-EXCEPTION"),
        ("no-reboot", "FAIL-REBOOT"),
        ("dmesg", "FAIL-DMESG"),
        ("excp-info", "FAIL-EXCEPTION"),
        ("heartbeat", "FAIL-HEARTBEAT"),
        ("d2h", "FAIL-D2H"),
        ("ap-liveness", "FAIL-HOSTAPD"),
        ("hcc-phase", "FAIL-HOSTAPD"),
        ("memory", "FAIL-MEMORY"),
        ("samples", "FAIL-SAMPLING"),
        ("duration", "FAIL-SAMPLING"),
        ("sample-coverage", "FAIL-SAMPLING"),
        ("sample-gaps", "FAIL-SAMPLING"),
    ]
    vmap = dict(order)
    code = vmap.get(failed[0]["id"], "FAIL-OTHER")
    return _verdict(code, checks, metrics, failed[0]["detail"], duration, pol)


def _verdict(code, checks, metrics, reason, duration, pol):
    advice = {
        "FAIL-EXCEPTION": "chip exception state: STOP, capture dmesg + UART, power-cycle (never rmmod after release, no retry in this boot), then analyse offline.",
        "FAIL-REBOOT": "uptime went backwards: the box rebooted or the sampler restarted; re-run and capture the reboot cause.",
        "FAIL-DMESG": "fatal/error dmesg lines appeared during the soak; capture them and classify before any re-run.",
        "FAIL-HEARTBEAT": "heartbeat continuity lost: the firmware or the D2H service stalled; capture hcc_status/rd_status, then power-cycle.",
        "FAIL-D2H": "D2H direct reader overruns/drops or a frame regression; capture rd_status/rd_catalog and power-cycle.",
        "FAIL-HOSTAPD": "the AP or hostapd died during the soak; capture the hostapd log + iw info and treat as a state-machine bug (do not restart in-boot if the firmware AP state is unknown).",
        "FAIL-MEMORY": "memory drift beyond policy; capture /proc/meminfo and slabinfo before calling it a leak.",
        "FAIL-SAMPLING": "sampler coverage/gaps invalid: fix the sampler run, do not score the soak.",
        "PASS": "soak PASS: no exception, heartbeat continuous, D2H clean, AP alive, memory bounded.",
        "FAIL-OTHER": "unclassified failure; inspect the check table.",
    }
    return {"schema": SCHEMA, "verdict": code,
            "reason": reason or advice.get(code, ""),
            "advice": advice.get(code, ""),
            "duration_s": duration, "policy": pol,
            "checks": checks, "metrics": metrics}


def render_markdown(result, meta, extra=""):
    lines = ["# AP soak result", ""]
    lines.append("- verdict: **%s**" % result["verdict"])
    lines.append("- when: %s" % (meta.get("when") or "unknown"))
    lines.append("- duration: %ss, samples: %s, span: %ss"
                 % (meta.get("duration_s"), result["metrics"].get("rows"),
                    result["metrics"].get("span_s")))
    if meta.get("module_sha"):
        lines.append("- module sha256: %s" % meta["module_sha"])
    if meta.get("fw_sha"):
        lines.append("- firmware sha256: %s" % meta["fw_sha"])
    lines.append("")
    if result["reason"]:
        lines.append("> %s" % result["reason"])
        lines.append("")
    lines.append(result["advice"] + "")
    lines.append("")
    lines.append("| check | status | detail |")
    lines.append("| --- | --- | --- |")
    for c in result["checks"]:
        lines.append("| %s | %s | %s |" % (c["id"], c["status"], c["detail"]))
    lines.append("")
    lines.append("## metrics")
    lines.append("")
    for k in sorted(result["metrics"]):
        lines.append("- %s: %s" % (k, result["metrics"][k]))
    if extra:
        lines.append("")
        lines.append(extra)
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# plan + static audit
# ---------------------------------------------------------------------------
HOSTAPD_CONF = """# generated by wifi-stability/tools/soak.py (attended soak)
interface=wlan0
driver=nl80211
ctrl_interface=/var/run/hostapd
ssid=cudy-ap-test
hw_mode=g
channel=6
beacon_int=100
dtim_period=2
auth_algs=1
"""

BRINGUP_TEMPLATE = r"""#!/bin/sh
# generated by wifi-stability/tools/soak.py - ATTENDED single-boot AP bring-up
set -u
D=/sys/bus/pci/devices/@ENDPOINT@
LOG=@REMOTE_DIR@
say() { echo "SOAK $*"; }
stop() { echo "SOAK-STOP $*"; exit 1; }
mkdir -p "$LOG"
[ -d "$D" ] || stop endpoint-missing
K=$(uname -r)
case "$K" in 6.18.60*) : ;; *) stop kernel-mismatch "$K" ;; esac
if grep -q '^hi5622_wifi ' /proc/modules 2>/dev/null; then stop module-already-loaded; fi
modprobe cfg80211 2>/dev/null || insmod /lib/modules/$K/cfg80211.ko || stop cfg80211-load
M=/lib/modules/$K/hi5622_wifi.ko
[ -f "$M" ] || stop module-missing "$M"
say "MODULE sha256=$(sha256sum "$M" | cut -d' ' -f1) size=$(wc -c < "$M")"
F=/lib/firmware/hi_wifi/FIRMWARE.PD4.bin
[ -f "$F" ] || stop firmware-missing "$F (policy artifact: stage it, never package it)"
say "FIRMWARE sha256=$(sha256sum "$F" | cut -d' ' -f1)"
insmod "$M" fw_path=hi_wifi/FIRMWARE.PD4.bin allow_frames=16 frame_gap_ms=100 h2d_send_budget=4 vap_start_first=1 vap_start_mode=1 vap_start_module=0 hcc_poll_ms=5 || stop insmod
say "INSMOD OK"
# Boot ladder: release-only protocol, exactly one release per boot.
echo 1 > "$D/prepare" || stop prepare
echo 1 > "$D/upload" || stop upload
echo 1 > "$D/verify" || stop verify
echo 1 > "$D/seed" || stop seed
echo 1 > "$D/release" || stop release
say "RELEASE OK"
# Bounded ready wait: poll the phase, no fixed sleep ladder.
n=0
ph=unknown
while [ "$n" -lt 60 ]; do
	ph=$(sed -n 's/.*phase=\([A-Z_]*\).*/\1/p' "$D/hcc_ready" 2>/dev/null | head -1)
	[ "$ph" = "ACTIVE" ] && break
	n=$((n + 1)); sleep 0.5
done
[ "$ph" = "ACTIVE" ] || stop ready-timeout "phase=$ph"
say "READY phase=$ph"
# C2 ladder: one context-create (type 1) + one TXQ sync72.
echo 3 > "$D/h2d_ctx_create_t1" || stop ctx-create
echo 1 > "$D/h2d_txq_sync72" || stop txq-sync72
hs=$(cat "$D/h2d_status")
case "$hs" in *"ok=2"*) : ;; *) stop h2d-gates "$hs" ;; esac
case "$hs" in *"timeout=0"*) : ;; *) stop h2d-timeout "$hs" ;; esac
say "C2 OK $(echo "$hs" | head -1)"
# AP interface: enumerate the wiphy (the index advances across load cycles).
PHY=$(ls /sys/class/ieee80211/ 2>/dev/null | head -1)
[ -n "$PHY" ] || stop no-wiphy
iw dev wlan0 del 2>/dev/null || true
iw phy "$PHY" interface add wlan0 type __ap || stop iface-add
ip link set wlan0 up || stop iface-up
/usr/sbin/hostapd -B -t "$LOG/hostapd-open.conf" > "$LOG/hostapd.log" 2>&1 || stop hostapd-start
n=0
info=
while [ "$n" -lt 60 ]; do
	info=$(iw dev wlan0 info 2>/dev/null)
	case "$info" in
	*"type AP"*) case "$info" in *"ssid cudy-ap-test"*) break ;; esac ;;
	esac
	n=$((n + 1)); sleep 0.5
done
case "$info" in *"type AP"*) : ;; *) stop hostapd-not-enabled "$info" ;; esac
case "$info" in *"ssid cudy-ap-test"*) : ;; *) stop hostapd-ssid "$info" ;; esac
pgrep -f hostapd >/dev/null 2>&1 || stop hostapd-died
say "AP ok $(echo "$info" | tr '\n' ';')"
# Read-only golden gate before the soak window.
H=/root/hi5622-bar0read
[ -x "$H" ] || stop helper-missing "$H"
RPHYS=$(awk 'NR==1{print $1}' "$D/resource" 2>/dev/null)
BAR0=0
case "$RPHYS" in
0x*|0X*) RH=$RPHYS; RH=$(echo "$RH" | sed -e 's/^0[xX]//'); BAR0=$((0x$RH)) ;;
esac
[ "$BAR0" -gt 0 ] || stop bar0-phys "$RPHYS"
rd() { "$H" $((BAR0 + 0x$1)) 2>/dev/null | head -1; }
P0=$(rd 877000); P1=$(rd 877004); P2=$(rd 877008); P3=$(rd 87700c)
ST=$(rd 7d0b60); MK=$(rd 6f8008); SG=$(rd 6f800c)
say "GOLDEN page=$P0/$P1/$P2/$P3 state=$ST mask=$MK staging=$SG"
case "$P0 $P1 $P2 $P3 $ST $MK $SG" in
"0x86807fef 0x00000007 0x00000002 0x0000abcd 0x00000001 0x00000000 0x00000000") : ;;
*) stop golden-readback "$P0 $P1 $P2 $P3 $ST $MK $SG" ;;
esac
say "BRINGUP OK"
"""

SAMPLER_CMD = ("nohup sh @REMOTE_DIR@/soak_sampler.sh @REMOTE_DIR@/soak.tsv "
               "@REMOTE_DIR@/dmesg.log @DURATION@ @INTERVAL@ "
               "/sys/bus/pci/devices/@ENDPOINT@ /root/hi5622-bar0read wlan0 "
               "@SSID@ > @REMOTE_DIR@/sampler.out 2>&1 & echo SAMPLER_PID=$!")

FORBIDDEN = [
    (r"\brmmod\b", "rmmod is forbidden after release (power-cycle is the reset)"),
    (r"0[xX]0*400392[fF]0", "CA 0x400392F0 is never written"),
    (r"0[xX]0*10161000", "the RC misc window is never read"),
    (r"\bdevmem\b", "no raw /dev/mem writes in the soak path"),
    (r"\bmtd\b|ubiformat|fw_setenv|flash", "no flash/env writes in the soak path"),
    (r"\breboot\b|poweroff|halt", "no reboot/power action inside the soak"),
    (r"\bwget\b|\bcurl\b|tftp", "no network transfers inside the soak"),
    (r"\bdump_stack\b", "no diagnostic stack dumps in generated scripts"),
]
ALLOWED_ATTR_WRITES = {
    "prepare", "upload", "verify", "seed", "release",
    "h2d_ctx_create_t1", "h2d_txq_sync72",
}
ECHO_RE = re.compile(r"echo\s+[^>]*>\s*\"?\$D/([a-z0-9_]+)")
REDIR_RE = re.compile(r">\s*(\S+)")


def audit_script(text):
    """Return the list of violations for a generated device script."""
    bad = []
    # Hard-rule patterns apply to EXECUTED lines; comment lines (documentation of
    # a step) are not actions.  Inline comments are stripped as well.
    exec_text = "\n".join(ln.split("#", 1)[0] if not ln.lstrip().startswith("#")
                         else "" for ln in text.splitlines())
    for pat, why in FORBIDDEN:
        if re.search(pat, exec_text):
            bad.append(why)
    for m in ECHO_RE.finditer(text):
        if m.group(1) not in ALLOWED_ATTR_WRITES:
            bad.append("write to driver attr not on the allowlist: %s" % m.group(1))
    for line in text.splitlines():
        line = line.split("#", 1)[0]
        if "echo" in line and ">" in line and "$D/" not in line:
            for m in REDIR_RE.finditer(line):
                target = m.group(1).strip("\"'")
                if target.startswith("/tmp/") or target.startswith("$LOG/"):
                    continue
                if not target.startswith("$D/"):
                    bad.append("unexpected redirect target: %s" % target)
    return sorted(set(bad))


def build_bringup_script(cfg):
    return (BRINGUP_TEMPLATE.replace("@ENDPOINT@", cfg["endpoint"])
            .replace("@REMOTE_DIR@", REMOTE_DIR))


def sampler_cmd(cfg):
    return (SAMPLER_CMD.replace("@REMOTE_DIR@", REMOTE_DIR)
            .replace("@DURATION@", str(cfg["duration"]))
            .replace("@INTERVAL@", str(cfg["interval"]))
            .replace("@ENDPOINT@", cfg["endpoint"])
            .replace("@SSID@", cfg["ssid"]))


def preflight_script(cfg):
    return """#!/bin/sh
# generated by wifi-stability/tools/soak.py - ATTENDED preflight (read-only)
set -u
D=/sys/bus/pci/devices/@ENDPOINT@
say() { echo "PRE $*"; }
stop() { echo "PRE-STOP $*"; exit 1; }
[ -d "$D" ] || stop endpoint-missing
[ "$(uname -r)" = "6.18.60" ] || stop kernel-mismatch "$(uname -r)"
[ -x /usr/sbin/iw ] || stop iw-missing
[ -x /usr/sbin/hostapd ] || stop hostapd-missing
[ -x /root/hi5622-bar0read ] || stop helper-missing
if grep -q '^hi5622_wifi ' /proc/modules 2>/dev/null; then stop module-already-loaded; fi
if pgrep -f hostapd >/dev/null 2>&1; then stop hostapd-already-running; fi
M=/lib/modules/$(uname -r)/hi5622_wifi.ko
[ -f "$M" ] || stop module-missing "$M"
say "MODULE $(sha256sum "$M" | cut -d' ' -f1) $(wc -c < "$M")"
F=/lib/firmware/hi_wifi/FIRMWARE.PD4.bin
[ -f "$F" ] || stop firmware-missing "$F"
say "FIRMWARE $(sha256sum "$F" | cut -d' ' -f1) $(wc -c < "$F")"
say "PREFLIGHT OK"
""".replace("@ENDPOINT@", cfg["endpoint"])


# ---------------------------------------------------------------------------
# host transport (attended runs only; repo rsh convention, cat|ssh transfers)
# ---------------------------------------------------------------------------
def find_rsh():
    env = os.environ.get("STAB_RSH")
    if env:
        return env
    cur = os.path.dirname(os.path.abspath(__file__))
    for _ in range(8):
        cand = os.path.join(cur, "tools", "rsh.sh")
        if os.path.isfile(cand):
            return cand
        nxt = os.path.dirname(cur)
        if nxt == cur:
            break
        cur = nxt
    return None


class Rsh:
    def __init__(self, rsh):
        self.rsh = rsh
        self.host = os.environ.get("ROUTER_HOST", "root@192.168.1.1")

    def _env(self):
        env = dict(os.environ)
        env["ROUTER_HOST"] = self.host
        return env

    def run(self, command, timeout=300, check=True):
        p = subprocess.run([self.rsh, command], capture_output=True,
                           text=True, timeout=timeout, env=self._env())
        if check and p.returncode != 0:
            raise SystemExit("soak: remote command failed rc=%d: %s\n%s\n%s"
                             % (p.returncode, command, p.stdout[-2000:],
                                p.stderr[-2000:]))
        return p

    def put(self, local, remote, timeout=300):
        with open(local, "rb") as f:
            data = f.read()
        p = subprocess.run([self.rsh, "cat > %s" % shlex.quote(remote)],
                           input=data, capture_output=True, timeout=timeout,
                           env=self._env())
        if p.returncode != 0:
            raise SystemExit("soak: put failed: %s" % p.stderr[-1000:])

    def get(self, remote, local, timeout=300):
        p = subprocess.run([self.rsh, "cat %s" % shlex.quote(remote)],
                           capture_output=True, timeout=timeout, env=self._env())
        if p.returncode != 0:
            raise SystemExit("soak: get failed: %s" % p.stderr[-1000:])
        with open(local, "wb") as f:
            f.write(p.stdout)


# ---------------------------------------------------------------------------
# CLI commands
# ---------------------------------------------------------------------------
def make_cfg(args):
    duration = int(args.duration)
    interval = int(args.interval)
    if not 60 <= duration <= 86400:
        raise SystemExit("soak: --duration must be 60..86400 s")
    if not 5 <= interval <= 60:
        raise SystemExit("soak: --interval must be 5..60 s")
    if duration < 2 * interval:
        raise SystemExit("soak: --duration must cover at least two samples")
    return {"duration": duration, "interval": interval,
            "ssid": args.ssid, "endpoint": ENDPOINT}


def cmd_dry_run(args):
    cfg = make_cfg(args)
    script = build_bringup_script(cfg)
    pre = preflight_script(cfg)
    print("== soak dry-run (no device contact)")
    print("endpoint    : %s" % cfg["endpoint"])
    print("duration    : %ss (interval %ss, %d samples)"
          % (cfg["duration"], cfg["interval"], cfg["duration"] // cfg["interval"]))
    print("ssid        : %s" % cfg["ssid"])
    print("sampler     : %s" % sampler_cmd(cfg))
    print("")
    print("== generated bring-up script")
    print(script)
    print("== generated preflight script")
    print(pre)
    bad = audit_script(script) + audit_script(pre)
    conf = HOSTAPD_CONF
    for key in ("interface=wlan0", "driver=nl80211", "ssid=%s" % cfg["ssid"],
                "hw_mode=g", "channel=6", "beacon_int=100", "dtim_period=2"):
        if key not in conf:
            bad.append("hostapd config missing: %s" % key)
    if bad:
        for b in bad:
            print("AUDIT-FAIL: %s" % b)
        return 1
    print("AUDIT OK (writer allowlist, no rmmod/devmem/flash/network)")
    print("DRY-RUN OK")
    return 0


def cmd_analyze(args):
    rows, end = parse_samples(args.samples)
    dmesg = parse_dmesg(args.dmesg)
    meta = {"duration_s": int(args.duration), "interval_s": int(args.interval),
            "ssid": args.ssid, "module_sha": args.module_sha,
            "fw_sha": args.fw_sha,
            "when": args.when or datetime.datetime.now().strftime("%Y-%m-%d %H:%M")}
    result = analyze(rows, dmesg, meta)
    md = render_markdown(result, meta, extra=args.extra or "")
    if args.out:
        with open(args.out, "w") as f:
            f.write(md)
        with open(args.out.replace(".md", ".json"), "w") as f:
            json.dump(result, f, indent=2, sort_keys=True)
    print(md)
    return 0 if result["verdict"] == "PASS" else 1


def cmd_run(args):
    if not (args.yes_really and args.attended and args.recovery_armed):
        raise SystemExit("soak: --run requires --yes-really --attended --recovery-armed")
    rsh_path = args.rsh or find_rsh()
    if not rsh_path:
        raise SystemExit("soak: no tools/rsh.sh found; pass --rsh or set STAB_RSH")
    cfg = make_cfg(args)
    r = Rsh(rsh_path)
    when = datetime.datetime.now().strftime("%Y%m%dT%H%M%SZ")
    outdir = args.outdir or os.path.join("/tmp", "wifi-stability-results", when)
    os.makedirs(outdir, exist_ok=True)
    conf_path = os.path.join(outdir, "hostapd-open.conf")
    with open(conf_path, "w") as f:
        f.write(HOSTAPD_CONF)
    script = build_bringup_script(cfg)
    pre = preflight_script(cfg)
    sampler = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "soak_sampler.sh")
    r.run("mkdir -p %s" % REMOTE_DIR)
    r.put(conf_path, REMOTE_DIR + "/hostapd-open.conf")
    pre_remote = REMOTE_DIR + "/preflight.sh"
    with open(os.path.join(outdir, "preflight.sh"), "w") as f:
        f.write(pre)
    r.put(os.path.join(outdir, "preflight.sh"), pre_remote)
    p = r.run("sh %s" % pre_remote, check=False)
    print(p.stdout)
    if p.returncode != 0 or "PREFLIGHT OK" not in p.stdout:
        print("soak: PREFLIGHT FAILED", file=sys.stderr)
        return 1
    r.put(sampler, REMOTE_DIR + "/soak_sampler.sh")
    bring_remote = REMOTE_DIR + "/bringup.sh"
    with open(os.path.join(outdir, "bringup.sh"), "w") as f:
        f.write(script)
    r.put(os.path.join(outdir, "bringup.sh"), bring_remote)
    if args.skip_bringup:
        print("soak: --skip-bringup: reusing the already-running AP")
    else:
        p = r.run("sh %s" % bring_remote, timeout=180, check=False)
        print(p.stdout)
        with open(os.path.join(outdir, "bringup.log"), "w") as f:
            f.write(p.stdout + p.stderr)
        if p.returncode != 0 or "BRINGUP OK" not in p.stdout:
            print("soak: BRINGUP FAILED - no soak window; capture the log",
                  file=sys.stderr)
            return 1
    r.run("rm -f %s/stop" % REMOTE_DIR)
    p = r.run(sampler_cmd(cfg))
    pid = ""
    for tok in p.stdout.split():
        if tok.startswith("SAMPLER_PID="):
            pid = tok.split("=", 1)[1]
    print("soak: sampler pid=%s duration=%ss" % (pid, cfg["duration"]))
    t_end = time.time() + cfg["duration"]
    last_print = time.time()
    while time.time() < t_end:
        time.sleep(min(30, max(1, t_end - time.time())))
        if time.time() - last_print >= 60:
            last_print = time.time()
            q = r.run("wc -l < %s/soak.tsv 2>/dev/null" % REMOTE_DIR, check=False)
            print("soak: %ds remaining, rows=%s"
                  % (int(t_end - time.time()), (q.stdout or "0").strip()))
    r.run("touch %s/stop" % REMOTE_DIR, check=False)
    for _ in range(30):
        q = r.run("kill -0 %s 2>/dev/null && echo RUN || echo DONE" % pid,
                  check=False)
        if "DONE" in q.stdout:
            break
        time.sleep(1)
    local_tsv = os.path.join(outdir, "soak.tsv")
    local_dm = os.path.join(outdir, "dmesg.log")
    r.get(REMOTE_DIR + "/soak.tsv", local_tsv)
    r.get(REMOTE_DIR + "/dmesg.log", local_dm)
    for f in ("sampler.out", "hostapd.log", "bringup.log"):
        try:
            r.get(REMOTE_DIR + "/" + f, os.path.join(outdir, f))
        except SystemExit:
            pass
    rows, end = parse_samples(local_tsv)
    dmesg = parse_dmesg(local_dm)
    mod_sha = ""
    try:
        with open(os.path.join(outdir, "bringup.log"), errors="replace") as f:
            for line in f:
                if line.startswith("SOAK MODULE"):
                    for tok in line.split():
                        if tok.startswith("sha256="):
                            mod_sha = tok.split("=", 1)[1]
    except OSError:
        pass
    meta = {"duration_s": cfg["duration"], "interval_s": cfg["interval"],
            "ssid": cfg["ssid"], "module_sha": mod_sha,
            "when": datetime.datetime.now().strftime("%Y-%m-%d %H:%M")}
    result = analyze(rows, dmesg, meta)
    md = render_markdown(result, meta)
    with open(os.path.join(outdir, "RESULT.md"), "w") as f:
        f.write(md)
    with open(os.path.join(outdir, "result.json"), "w") as f:
        json.dump(result, f, indent=2, sort_keys=True)
    print(md)
    print("soak: evidence dir %s" % outdir)
    return 0 if result["verdict"] == "PASS" else 1


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Hi5622 AP soak harness (wifi-stability lane; host-only "
                    "except --run which is attended)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    d = sub.add_parser("dry-run", help="validate + print the plan (no device)")
    d.add_argument("--duration", default=3600, type=int)
    d.add_argument("--interval", default=10, type=int)
    d.add_argument("--ssid", default=DEFAULT_SSID)
    d.set_defaults(func=cmd_dry_run)
    a = sub.add_parser("analyze", help="offline analysis of a sampler TSV")
    a.add_argument("--samples", required=True)
    a.add_argument("--dmesg", default=None)
    a.add_argument("--duration", required=True, type=int)
    a.add_argument("--interval", default=10, type=int)
    a.add_argument("--ssid", default=DEFAULT_SSID)
    a.add_argument("--module-sha", default="")
    a.add_argument("--fw-sha", default="")
    a.add_argument("--when", default="")
    a.add_argument("--extra", default="")
    a.add_argument("--out", default=None, help="write RESULT .md/.json here")
    a.set_defaults(func=cmd_analyze)
    r = sub.add_parser("run", help="attended soak run")
    r.add_argument("--duration", default=3600, type=int)
    r.add_argument("--interval", default=10, type=int)
    r.add_argument("--ssid", default=DEFAULT_SSID)
    r.add_argument("--yes-really", action="store_true")
    r.add_argument("--attended", action="store_true")
    r.add_argument("--recovery-armed", action="store_true")
    r.add_argument("--skip-bringup", action="store_true")
    r.add_argument("--rsh", default=None)
    r.add_argument("--outdir", default=None)
    r.set_defaults(func=cmd_run)
    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())

