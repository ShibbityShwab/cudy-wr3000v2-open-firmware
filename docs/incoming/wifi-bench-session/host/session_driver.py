#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""session_driver.py - host-side driver for the attended wifi-bench-session.

Lane: wifi-bench-session (shared task task-127), HOST-ONLY package; the Lead
fires it with a human at the bench.  Dry-run (the default) never contacts the
router: it validates every artifact pin, builds the exact bench_session plans
for phases S and A and renders the exact device-side scripts for review.

PHASES (one fresh golden boot per phase, module reload needs a reboot):

  S  13-channel scan        cfg80211 mode, scan-complete provider module
                           (748,888 b42ff4ec / alt 748,564 02162d4a),
                           iw iface add -> one iw scan -> 13/13 + BSS gates
  A  AP start               cfg80211 mode, wifi-ap-start provider module
                           (770,304 11511980): base ctx_create+txq_sync72,
                           then device/ap-fire.sh (iface type __ap, mode-1
                           VAP base via one-channel scan, hostapd NEW_BEACON,
                           4 AP frames, firmware readback gate) and
                           device/ap-readback.sh
  X  first MAC data-plane TX/RX   the dp-merge lane's merged provider module
                           + F-AIR sequence (artifact pins PENDING until that
                           lane commits; a real run refuses while pending)

Real runs need --yes-really --attended; the bench_session.py preflight
(bench-link, recovery door, UART capture, TFTP state, single-session, attended,
gitignored evidence dir) runs inside every bench_session invocation, and each
phase is preceded by a hardened, deadline-bounded reboot.

Evidence: build/wifi-bench-session/<UTC stamp>/ (gitignored):
  <phase>/                     bench_session evidence dir (plan, script,
                               report.txt, summary.json, protocol evidence)
  <phase>/ap-fire-report.txt   phase A device-side report + readback pack
  <phase>/ap-readback-report.txt
  SESSION-REPORT.md / session.json
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
import time

LANE = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
ROOT = os.path.abspath(os.path.join(LANE, "..", "..", "..", ".."))
BENCH_TOOLS = os.path.join(ROOT, "opensource", "docs", "incoming",
                           "wifi-bench-tooling", "tools")
BENCH_SESSION = os.path.join(BENCH_TOOLS, "bench_session.py")
PY = sys.executable

PHASES = ("s", "a", "x")
PHASE_LABEL = {
    "s": "13-channel scan + cfg80211 BSS",
    "a": "AP start: hostapd NEW_BEACON + 4 AP frames + firmware readback",
    "x": "first MAC data-plane TX/RX (merged provider module)",
}
PHASE_SEQ = {
    "s": os.path.join(LANE, "sequences", "session-scan13.json"),
    "a": os.path.join(LANE, "sequences", "session-ap-base.json"),
}


# ---------------------------------------------------------------------------
# small helpers
# ---------------------------------------------------------------------------
def die(msg, rc=2):
    print("error: %s" % msg, file=sys.stderr)
    raise SystemExit(rc)


def load_env(path):
    env = {}
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            env[k.strip()] = v.strip().strip('"')
    # explicit environment variables override the file (operator safety valve)
    for k in list(env):
        if os.environ.get(k):
            env[k] = os.environ[k]
    return env


def sha256(path):
    import hashlib
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_json(path):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def resolve_rel(path):
    """Resolve an artifacts.env path: absolute stays, relative is ROOT-relative."""
    if not path or path == "pending":
        return path
    if os.path.isabs(path):
        return path
    return os.path.join(ROOT, path)


class OnBoxPin:
    """An artifact that lives on the box, installed by a dependency lane.

    The host pin is the authoritative on-box sha256 from that lane's manifest;
    the driver verifies it over ssh in the phase-A deploy stage.  Dry-run only
    checks that the pin is filled.
    """

    def __init__(self, name, sha, remote):
        self.name, self.want_sha, self.remote = name, sha, remote
        self.path = remote

    @property
    def pending(self):
        return self.want_sha in ("", "pending")

    def check(self):
        if self.pending:
            return "pending", "lane pin not delivered"
        return "ok", "on-box %s sha256=%s.." % (self.remote, self.want_sha[:16])


class Pin:
    """One artifact pin: path/sha/size (+ documented size-alternates)."""

    def __init__(self, name, path, want_sha, want_size="", accept=""):
        self.name, self.path = name, path
        self.want_sha, self.want_size = want_sha, str(want_size or "")
        self.accept = accept or ""

    @property
    def pending(self):
        return self.path == "pending" or self.want_sha == "pending"

    def check(self):
        if self.pending:
            return "pending", "lane pin not delivered"
        if not os.path.isfile(self.path):
            return "missing", "missing %s" % self.path
        got_sha, got_size = sha256(self.path), str(os.path.getsize(self.path))
        accept = []
        for pair in self.accept.split():
            a_sha, _, a_size = pair.partition(":")
            accept.append((a_sha, a_size))
        if got_sha == self.want_sha or any(s == got_sha for s, _ in accept):
            return "ok", "sha256=%s.. size=%s" % (got_sha[:16], got_size)
        return "sha-mismatch", "sha256=%s size=%s (want %s %s)" % (
            got_sha, got_size, self.want_sha, self.want_size)


def phase_pins(env, phase):
    """Artifact pins a phase needs, in display order."""
    common = [
        Pin("PD4 firmware", env["FW_PD4"], env["FW_PD4_SHA"],
            env["FW_PD4_SIZE"]),
        Pin("fw manifest", env["FW_MANIFEST"], env["FW_MANIFEST_SHA"]),
        Pin("PHY block", env["WIFI_BLOCK"], env["WIFI_BLOCK_SHA"],
            env["WIFI_BLOCK_SIZE"]),
        Pin("cfg80211.ko", env["CFG80211_KO"], env["CFG80211_KO_SHA"],
            env["CFG80211_KO_SIZE"]),
        Pin("iw", env["IW_BIN"], env["IW_BIN_SHA"]),
        Pin("libnl-tiny", env["LIBNL_TINY"], env["LIBNL_TINY_SHA"]),
    ]
    if phase == "s":
        return common + [Pin("scan module", env["SCAN_MODULE"],
                             env["SCAN_MODULE_SHA"], env["SCAN_MODULE_SIZE"],
                             env.get("SCAN_MODULE_ACCEPT", ""))]
    if phase == "a":
        hostapd = [Pin("AP module", env["AP_MODULE"], env["AP_MODULE_SHA"],
                       env["AP_MODULE_SIZE"])]
        if env.get("HOSTAPD_MODE", "apk") == "apk":
            hostapd += [
                OnBoxPin("hostapd on-box", env.get("HOSTAPD_ONBOX_SHA", ""),
                         env.get("HOSTAPD_REMOTE_BIN", "")),
                OnBoxPin("hostapd conf", env.get("HOSTAPD_CONF_SHA", ""),
                         env.get("HOSTAPD_REMOTE_CONF", "")),
            ]
        else:
            hostapd.append(Pin("hostapd bin", env["HOSTAPD_BIN"],
                               env["HOSTAPD_BIN_SHA"]))
        return common + hostapd
    if phase == "x":
        return common + [
            Pin("X merged module", resolve_rel(env["X_MODULE"]),
                env["X_MODULE_SHA"], env.get("X_MODULE_SIZE", "")),
            Pin("X sequence", resolve_rel(env["X_SEQUENCE"]),
                env.get("X_SEQUENCE_SHA", "")),
        ]
    raise KeyError(phase)


def module_and_params(env, phase):
    if phase == "s":
        return env["SCAN_MODULE"], env.get("SCAN_PARAMS", "")
    if phase == "a":
        return env["AP_MODULE"], env.get("AP_PARAMS", "")
    return resolve_rel(env["X_MODULE"]), env.get("X_PARAMS", "")


def sequence_for(env, phase):
    if phase in PHASE_SEQ:
        return PHASE_SEQ[phase]
    return resolve_rel(env["X_SEQUENCE"])


def mode_for(env, phase):
    return env.get("X_MODE", "cfg80211") if phase == "x" else "cfg80211"


# ---------------------------------------------------------------------------
# bench_session invocation + evidence classification
# ---------------------------------------------------------------------------
def bench_argv(env, phase, report_dir, real):
    module, params = module_and_params(env, phase)
    argv = [PY, BENCH_SESSION, "--mode", mode_for(env, phase),
            "--sequence", sequence_for(env, phase),
            "--report-dir", report_dir,
            "--endpoint", env["ENDPOINT"],
            "--session-timeout", env["SESSION_TIMEOUT"],
            "--ready-id1", "2.0", "--ready-id2", "20.0",
            "--module", module,
            "--fw-file", env["FW_PD4"],
            "--fw-manifest", env["FW_MANIFEST"],
            "--wifi-block-bin", env["WIFI_BLOCK"],
            "--ssh-host", env["SSH_HOST"]]
    if mode_for(env, phase) == "cfg80211":
        argv += ["--cfg80211-ko", env["CFG80211_KO"],
                 "--iw-bin", env["IW_BIN"],
                 "--libnl-tiny", env["LIBNL_TINY"]]
    for kv in (params or "").split():
        argv += ["--module-param", kv]
    # fresh golden boot before every phase (module reload needs it); the last
    # phase's cleanup reboot is issued by this driver after the matrix.
    argv += ["--reboot", "before"]
    if real:
        argv += ["--yes-really", "--attended"]
    else:
        argv += ["--dry-run"]
    return argv


def run_streamed(argv, timeout, log_path):
    print("$ %s" % " ".join(shlex.quote(a) for a in argv), flush=True)
    t0 = time.time()
    with open(log_path, "w", encoding="utf-8") as log:
        log.write("$ %s\n" % " ".join(shlex.quote(a) for a in argv))
        proc = subprocess.Popen(argv, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        try:
            out, _ = proc.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            proc.kill()
            out, _ = proc.communicate()
            out = (out or "") + "\n[driver] subprocess killed after %ss\n" % timeout
            log.write(out or "")
            return 124, out or "", time.time() - t0
        log.write(out or "")
        return proc.returncode, out or "", time.time() - t0


def bench_classify(phase_dir):
    summary = read_json(os.path.join(phase_dir, "summary.json")) or {}
    evidence = read_json(os.path.join(phase_dir, "protocol-evidence.json")) or {}
    plan = read_json(os.path.join(phase_dir, "protocol-plan.json")) or {}
    sp = read_json(os.path.join(phase_dir, "session-plan.json")) or {}
    out = {
        "session_result": summary.get("result"),
        "verdict": evidence.get("verdict"),
        "failed_step": evidence.get("failed_step"),
        "blocked_step": evidence.get("blocked_step"),
        "stop_rule": summary.get("stop_rule"),
        "sequence": plan.get("name"),
        "sequence_sha256": plan.get("sha256"),
        "module_sha256": (sp.get("module") or {}).get("sha256"),
        "module_size": (sp.get("module") or {}).get("size"),
        "load_params": (sp.get("opts") or {}).get("module_params"),
    }
    return out


def bench_status(env, phase_dir, rc):
    """Map a bench_session exit code + evidence to a driver status."""
    c = bench_classify(phase_dir)
    if rc == 3:
        return "HOST-PREFLIGHT-FAIL", "bench-link/recovery/UART/attended preflight failed", c
    if rc == 2:
        return "REFUSED", "bench_session refused at plan time (artifact/param/pin)", c
    if c["session_result"] == "PASS" and c["verdict"] in (None, "PASS"):
        return "PASS", "session PASS; protocol verdict PASS", c
    if c["session_result"] == "BLOCKED" or c["verdict"] == "BLOCKED":
        return "BLOCKED-DRIVER", "documented pending-feature outcome: %s" % (
            c.get("blocked_step") or c.get("stop_rule")), c
    if c["session_result"] == "STOP":
        return "STOP", "stopped at %s: %s" % (
            c.get("failed_step"), c.get("stop_rule")), c
    if c["session_result"] == "FAIL" or c["verdict"] == "FAIL":
        return "FAIL", "session FAIL: %s" % (c.get("failed_step") or c.get("stop_rule")), c
    return "ERROR", "no session result (rc=%s)" % rc, c


# ---------------------------------------------------------------------------
# phase S extra host gates (the "iw scan output non-empty" gate)
# ---------------------------------------------------------------------------
SCAN_LINE_RE = re.compile(
    r"scan: phase=(?P<phase>\S+) results=(?P<results>\d+)/(?P<max>\d+) "
    r"bss=(?P<bss>\d+) complete=(?P<complete>\d+) channel=(?P<channel>\d+) "
    r"chans=(?P<chans>\d+)/(?P<total>\d+)")
FINISH_RE = re.compile(r"scan finish: last=(?P<last>\S+) ok=(?P<ok>\d+) "
                       r"abort=(?P<abort>\d+)")


def scan_host_gates(phase_dir):
    report = ""
    try:
        with open(os.path.join(phase_dir, "report.txt"), "r",
                  encoding="utf-8", errors="replace") as f:
            report = f.read()
    except OSError:
        pass
    bss_lines = len(re.findall(r"(?m)^BSS ", report))
    bss_entries = len(re.findall(r"(?m)^\s*BSS ", report))
    m = SCAN_LINE_RE.search(report)
    fin = FINISH_RE.search(report)
    chans = "%s/%s" % (m.group("chans"), m.group("total")) if m else None
    results = int(m.group("results")) if m else None
    bss = int(m.group("bss")) if m else None
    last = fin.group("last") if fin else None
    gates = {
        "iw_bss_lines": bss_lines + bss_entries,
        "scan_chans": chans,
        "scan_results": results,
        "scan_bss": bss,
        "scan_finish": last,
    }
    problems = []
    if gates["iw_bss_lines"] < 1:
        problems.append("iw scan output has no BSS lines")
    if chans != "13/13":
        problems.append("scan coverage %s != 13/13" % chans)
    if results is None or results < 1:
        problems.append("scan results %s < 1" % results)
    if bss is None or bss < 1:
        problems.append("scan bss %s < 1" % bss)
    if last is not None and last != "finish-ok":
        problems.append("scan finish %s != finish-ok" % last)
    return gates, problems


DP_TX_POSTED_RE = re.compile(r"posted=(\d+)")
DP_PENDING_RE = re.compile(r"completion_pending=(\d+)")
DP_CAPTURES_RE = re.compile(r"captures=(\d+)")


def txrx_host_gates(phase_dir):
    """Phase X evidence extraction: dp_tx_status completion + RX capture counts.

    The F-AIR sequence JSON is the gate authority; this only names the outcome
    the report must state: COMPLETED (device consumed the MSDU entries),
    POST-ONLY (posted, device did not consume) or NO-RX (capture window empty).
    """
    report = ""
    try:
        with open(os.path.join(phase_dir, "report.txt"), "r",
                  encoding="utf-8", errors="replace") as f:
            report = f.read()
    except OSError:
        pass
    posted = [int(m) for m in DP_TX_POSTED_RE.findall(report)]
    pending = [int(m) for m in DP_PENDING_RE.findall(report)]
    captures = [int(m) for m in DP_CAPTURES_RE.findall(report)]
    last_posted = posted[-1] if posted else 0
    last_pending = pending[-1] if pending else None
    last_captures = captures[-1] if captures else None
    if last_posted < 1:
        verdict = "NO-TX-POST"
    elif last_pending == 0:
        verdict = "COMPLETED"
    elif last_pending is not None:
        verdict = "POST-ONLY"
    else:
        verdict = "POST-ONLY? (no completion_pending read)"
    gates = {"tx_posted": last_posted, "completion_pending": last_pending,
             "rx_captures": last_captures, "txrx_verdict": verdict}
    problems = []
    if last_posted < 1:
        problems.append("no dp_tx_status posted evidence")
    if last_captures is None:
        problems.append("no dp_rx_capture evidence")
    return gates, problems


# ---------------------------------------------------------------------------
# phase A device-side orchestration (hostapd stage)
# ---------------------------------------------------------------------------
AP_DEV_DIR = "/root/hostapd-deploy"


def _transport(env):
    sys.path.insert(0, BENCH_TOOLS)
    import bench_session as BS  # noqa: WPS433 - deliberate lazy import
    return BS, BS.SshTransport(env["SSH_HOST"])


def deploy_ap_stage(env, transport, phase_dir):
    """Push hostapd + config + the two device scripts to the box. Returns note."""
    def put(local, remote, executable=False):
        rc, _out, err = transport.put(local, remote)
        if rc != 0:
            raise RuntimeError("put %s -> %s failed: %s" % (local, remote,
                                                            err.strip()[:160]))
        rc, out, err = transport.run("sha256sum %s" % shlex.quote(remote),
                                     timeout=60)
        if rc != 0:
            raise RuntimeError("sha256sum %s failed: %s" % (remote,
                                                            (out + err)[:160]))
        want = sha256(local)
        got = (out or "").split()[0] if (out or "").split() else ""
        if got != want:
            raise RuntimeError("deployed sha mismatch for %s: %s != %s"
                               % (remote, got, want))
        if executable:
            rc, out, err = transport.run("chmod +x %s" % shlex.quote(remote),
                                         timeout=30)
            if rc != 0:
                raise RuntimeError("chmod +x %s failed" % remote)

    rc, out, err = transport.run("mkdir -p %s/evidence" % AP_DEV_DIR,
                                 timeout=30)
    if rc != 0:
        raise RuntimeError("mkdir %s failed: %s" % (AP_DEV_DIR,
                                                    (out + err)[:160]))
    if env.get("HOSTAPD_MODE", "apk") == "apk":
        # The wifi-hostapd-deploy T0 zero-flash apk-add installed hostapd in
        # the persistent rootfs overlay (run it BEFORE this session; it is
        # serialized with this driver's phase reboots).  Verify the on-box
        # binary/config against that lane's authoritative manifest hashes.
        for remote, want, what in (
                (env["HOSTAPD_REMOTE_BIN"], env.get("HOSTAPD_ONBOX_SHA", ""),
                 "hostapd binary"),
                (env["HOSTAPD_REMOTE_CONF"], env.get("HOSTAPD_CONF_SHA", ""),
                 "hostapd config")):
            rc, out, err = transport.run(
                "sha256sum %s" % shlex.quote(remote), timeout=30)
            if rc != 0:
                raise RuntimeError(
                    "apk mode: %s missing on the box (%s) - run the "
                    "wifi-hostapd-deploy T0 deploy first: %s"
                    % (what, remote, env.get("HOSTAPD_DEPLOY_CMD", "?"))
                    if env.get("HOSTAPD_DEPLOY_CMD", "preexisting") != "preexisting"
                    else "apk mode: %s missing on the box (%s) - run the "
                         "wifi-hostapd-deploy T0 deploy first"
                         % (what, remote))
            got = (out or "").split()[0] if (out or "").split() else ""
            if want and want != "pending" and got != want:
                raise RuntimeError("apk mode: on-box %s sha %s != lane pin %s"
                                   % (what, got, want))
        deploy_note = ("apk: verified %s and %s against the wifi-hostapd-deploy "
                       "manifest" % (env["HOSTAPD_REMOTE_BIN"],
                                     env["HOSTAPD_REMOTE_CONF"]))
    else:
        put(env["HOSTAPD_BIN"], env["HOSTAPD_REMOTE_BIN"], executable=True)
        put(os.path.join(LANE, env["HOSTAPD_CONF"]), env["HOSTAPD_REMOTE_CONF"])
        deploy_note = "static: pushed %s + config" % env["HOSTAPD_REMOTE_BIN"]
    put(os.path.join(LANE, "device", "ap-fire.sh"),
        AP_DEV_DIR + "/ap-fire.sh", executable=True)
    put(os.path.join(LANE, "device", "ap-readback.sh"),
        AP_DEV_DIR + "/ap-readback.sh", executable=True)
    return "%s; conf=%s; scripts=ap-fire.sh,ap-readback.sh" % (
        deploy_note, env["HOSTAPD_REMOTE_CONF"])


def _dev_run(env, transport, script, phase_dir, tag, timeout):
    rdir = AP_DEV_DIR + "/evidence/" + os.path.basename(phase_dir)
    run_env = " ".join([
        "WB_RDIR=%s" % shlex.quote(rdir),
        "WB_HOSTAPD=%s" % shlex.quote(env["HOSTAPD_REMOTE_BIN"]),
        "WB_HOSTAPD_CONF=%s" % shlex.quote(env["HOSTAPD_REMOTE_CONF"]),
        "WB_HOSTAPD_PIDFILE=%s" % shlex.quote(env["HOSTAPD_PIDFILE"]),
        "WB_BAR0READ=/root/hi5622-bar0read",
        "WB_ENDPOINT=%s" % shlex.quote(env["ENDPOINT"]),
        "WB_VAPBASE_TIMEOUT=30", "WB_HOSTAPD_WAIT=30",
        "WB_AP_START_MODE=%s" % shlex.quote(env.get("AP_START_MODE", "0")),
    ])
    cmd = "%s sh %s/%s" % (run_env, AP_DEV_DIR, script)
    rc, out, err = transport.run(cmd, timeout=timeout)
    with open(os.path.join(phase_dir, tag + ".log"), "w",
              encoding="utf-8") as f:
        f.write("$ " + cmd + "\n" + (out or "") + (err or ""))
    result = None
    for line in (out or "").splitlines():
        if line.startswith("SESSION_RESULT:"):
            result = line.split(":", 1)[1].strip()
    remote_report = rdir + "/" + tag + "-report.txt"
    rc2, rtext, _e2 = transport.get(remote_report,
                                    os.path.join(phase_dir,
                                                 tag + "-report.txt"))
    return {"rc": rc, "result": result, "stdout": out or "",
            "stderr": err or "", "report_present": rc2 == 0}


def phase_a_stage(env, bench_dir, phase_dir, evidence):
    """Run the AP device stage after the base session PASSed."""
    BS, transport = _transport(env)
    try:
        detail = deploy_ap_stage(env, transport, phase_dir)
    except Exception as e:  # noqa: BLE001 - report and fail the phase
        return {"status": "DEPLOY-FAIL", "detail": str(e)[:300],
                "evidence": evidence}
    fire = _dev_run(env, transport, "ap-fire.sh", phase_dir, "ap-fire", 240)
    readback = _dev_run(env, transport, "ap-readback.sh", phase_dir,
                        "ap-readback", 120)
    res = fire["result"]
    if res == "PASS":
        status, why = "PASS", "AP frames consumed + firmware VAP readback 100/2"
    elif res == "PASS-PARTIAL":
        status, why = "PARTIAL", ("AP frames consumed + clean packs; vap0+0x10/"
                                  "+0x14 deref unresolved (evidence captured)")
    elif res and res.startswith("STOP:"):
        status, why = "STOP", "AP stage stopped at %s" % res[5:]
    else:
        status, why = "ERROR", "AP stage rc=%s result=%r" % (fire["rc"], res)
    return {"status": status, "detail": why, "deploy": detail,
            "fire_rc": fire["rc"], "fire_result": res,
            "readback_rc": readback["rc"],
            "readback_result": readback["result"], "evidence": evidence}


# ---------------------------------------------------------------------------
# phase runner
# ---------------------------------------------------------------------------
def run_phase(env, phase, base_dir, dry_run, evidence):
    phase_dir = os.path.join(base_dir, phase)
    os.makedirs(phase_dir, exist_ok=True)
    label = PHASE_LABEL[phase]
    rec = {"phase": phase, "label": label,
           "sequence": os.path.basename(sequence_for(env, phase)),
           "mode": mode_for(env, phase), "status": None, "detail": "",
           "evidence_dir": phase_dir}
    argv = bench_argv(env, phase, phase_dir, real=not dry_run)
    timeout = int(env["SESSION_TIMEOUT"]) + int(env["REBOOT_DEADLINE_S"]) + 180
    rc, out, elapsed = run_streamed(argv, timeout,
                                    os.path.join(phase_dir, "driver-run.log"))
    rec["bench_rc"], rec["elapsed_s"] = rc, round(elapsed, 1)
    if dry_run:
        if rc == 0:
            rec["status"] = "PLAN-OK"
            rec["detail"] = "dry-run plan validated (no device contacted)"
        else:
            rec["status"] = "PLAN-FAIL"
            rec["detail"] = "bench_session dry-run rc=%s" % rc
        # keep the exact generated device script for review + sh -n audit
        gen = os.path.join(LANE, "generated")
        os.makedirs(gen, exist_ok=True)
        src = os.path.join(phase_dir, "device-script.sh")
        if os.path.isfile(src):
            dst = os.path.join(gen, "phase-%s-device-script.sh" % phase)
            with open(src, "r", encoding="utf-8") as f:
                script = f.read()
            with open(dst, "w", encoding="utf-8") as f:
                f.write(script)
            rec["rendered_script"] = dst
            rc_sh = subprocess.call(["sh", "-n", dst])
            rec["script_syntax_ok"] = rc_sh == 0
        return rec
    status, why, c = bench_status(env, phase_dir, rc)
    rec.update({"status": status, "detail": why, "bench": c})
    if status == "PASS" and phase == "s":
        gates, problems = scan_host_gates(phase_dir)
        rec["scan_gates"] = gates
        if problems:
            rec["status"] = "STOP"
            rec["detail"] = "host gates: " + "; ".join(problems)
    if phase == "x" and status == "PASS":
        gates, problems = txrx_host_gates(phase_dir)
        rec["txrx_gates"] = gates
        if problems:
            rec["detail"] = (rec["detail"] + " | evidence: " +
                             "; ".join(problems))
        else:
            rec["detail"] = (rec["detail"] + " | txrx=%s (posted=%s "
                             "completion_pending=%s rx_captures=%s)"
                             % (gates["txrx_verdict"], gates["tx_posted"],
                                gates["completion_pending"],
                                gates["rx_captures"]))
    if phase == "a" and status == "PASS":
        stage = phase_a_stage(env, base_dir, phase_dir, evidence)
        rec["ap_stage"] = stage
        if stage["status"] != "PASS":
            rec["status"] = stage["status"]
            rec["detail"] = "base PASS; " + stage["detail"]
    return rec


def hostapd_preflight(env):
    """Real-run preflight: verify the wifi-hostapd-deploy on-box artifacts.

    Cheaper than discovering the missing deploy after the phase-A base session
    (deploy-hostapd.sh itself runs this check, but the driver must not burn
    bench time on a session that cannot reach the AP stage).
    """
    if env.get("HOSTAPD_MODE", "apk") != "apk":
        return True, "static hostapd mode: binary pushed in the AP stage"
    BS, transport = _transport(env)
    for remote, want, what in (
            (env["HOSTAPD_REMOTE_BIN"], env.get("HOSTAPD_ONBOX_SHA", ""),
             "hostapd binary"),
            (env["HOSTAPD_REMOTE_CONF"], env.get("HOSTAPD_CONF_SHA", ""),
             "hostapd config")):
        rc, out, err = transport.run("sha256sum %s" % shlex.quote(remote),
                                     timeout=30)
        if rc != 0:
            return False, ("%s missing on the box (%s); run the "
                           "wifi-hostapd-deploy attended deploy first: %s"
                           % (what, remote,
                              env.get("HOSTAPD_DEPLOY_CMD", "?")))
        got = (out or "").split()[0] if (out or "").split() else ""
        if want and want != "pending" and got != want:
            return False, ("%s sha %s != lane pin %s (stale deploy?)"
                           % (what, got, want))
    return True, "hostapd on-box artifacts match the lane manifest"


def cleanup_reboot(env, log_path):
    BS, transport = _transport(env)
    opts = BS.SessionOptions(
        mode="protocol", ssh_host=env["SSH_HOST"], reboot="after",
        reboot_poll_s=float(env["REBOOT_POLL_S"]),
        reboot_deadline_s=float(env["REBOOT_DEADLINE_S"]))
    print("[driver] final cleanup reboot (leave the box freshly booted on our image)",
          flush=True)
    ok, note = BS.reboot_and_wait(transport, opts,
                                  poll_s=float(env["REBOOT_POLL_S"]),
                                  deadline_s=float(env["REBOOT_DEADLINE_S"]))
    with open(log_path, "w", encoding="utf-8") as f:
        f.write(note + "\n")
    print("[driver] reboot %s: %s" % ("OK" if ok else "FAILED", note), flush=True)
    return ok, note


# ---------------------------------------------------------------------------
# report
# ---------------------------------------------------------------------------
def render_report(env, run_kind, stamp, records, pending, evidence_dir,
                  reboot_note=None):
    lines = []
    a = lines.append
    a("# wifi-bench-session - attended command package run")
    a("")
    a("- generated: %s" % time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()))
    a("- run kind: **%s**" % run_kind)
    a("- package: opensource/docs/incoming/wifi-bench-session")
    a("- evidence: %s" % evidence_dir)
    a("- transport: ssh %s (tools/rsh.sh; dropbear, cat|ssh transfer)"
      % env["SSH_HOST"])
    a("")
    a("| phase | label | sequence | module sha256 | status | detail |")
    a("| --- | --- | --- | --- | --- | --- |")
    for r in records:
        b = r.get("bench") or {}
        a("| %s | %s | %s | %s | **%s** | %s |"
          % (r["phase"], r["label"], r.get("sequence"),
             (b.get("module_sha256") or "")[:12] or "-",
             r["status"], (r.get("detail") or "").replace("|", "\\|")[:200]))
    if pending:
        a("")
        a("## DEPENDENCY-PENDING phases (not run)")
        for p in pending:
            a("- **%s** (%s): %s" % (p["phase"], PHASE_LABEL[p["phase"]],
                                     p["why"]))
    if reboot_note:
        a("")
        a("## cleanup reboot")
        a("")
        a("- %s" % reboot_note)
    a("")
    a("## PASS gates this run enforced")
    a("")
    for ph in ("s", "a", "x"):
        a("- **%s** %s: see GATES.md; the bench_session sequence JSON is the "
          "single source of truth for the per-step gates." % (ph, PHASE_LABEL[ph]))
    return "\n".join(lines) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="host-side driver for the attended wifi-bench-session "
                    "(dry-run by default; never contacts the router)")
    ap.add_argument("--artifacts", default=os.environ.get(
        "WB_ARTIFACTS", os.path.join(LANE, "artifacts.env")))
    ap.add_argument("--phases", default="s,a,x",
                    help="comma list of s,a,x (default all)")
    ap.add_argument("--evidence-dir", default=None)
    ap.add_argument("--dry-run", action="store_true",
                    help="plan only (default when --yes-really is absent)")
    ap.add_argument("--yes-really", action="store_true",
                    help="execute on the bench box (needs --attended)")
    ap.add_argument("--attended", action="store_true",
                    help="a human is at the bench with the recovery door armed")
    ap.add_argument("--allow-partial", action="store_true",
                    help="real run: skip phases whose lane pins are still "
                         "pending instead of refusing the whole session")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args(argv)
    if not os.path.isfile(args.artifacts):
        die("missing artifacts env: %s" % args.artifacts)
    env = load_env(args.artifacts)
    phases = [p.strip() for p in args.phases.split(",") if p.strip()]
    bad = [p for p in phases if p not in PHASES]
    if bad:
        die("unknown phase(s): %s (choose from %s)" % (", ".join(bad),
                                                       ", ".join(PHASES)))
    real = bool(args.yes_really) and not args.dry_run
    if real and not args.attended:
        die("a real run needs --attended (recovery needs a human at the bench)")
    dry_run = not real

    # ---- plan the run: artifact pins + dependency-pending phase handling ----
    pending, pin_fail = [], []
    pin_table = []
    for p in phases:
        checks = []
        for pin in phase_pins(env, p):
            st, why = pin.check()
            checks.append({"phase": p, "name": pin.name, "path": pin.path,
                           "sha": pin.want_sha, "result": st, "detail": why})
            if st == "pending" and pin.pending:
                pending.append({"phase": p, "why": "%s: %s" % (pin.name, why)})
            elif st != "ok":
                pin_fail.append({"phase": p, "why": "%s: %s" % (pin.name, why)})
        pin_table.extend(checks)
    # dedupe pending by phase (report one line per phase)
    seen, pend = set(), []
    for p in pending:
        if p["phase"] in seen:
            continue
        seen.add(p["phase"])
        pend.append(p)
    pend_set = {x["phase"] for x in pend}

    def planning_blockers(phase):
        """Reasons a phase cannot even be dry-run planned (missing sequence or
        module pin).  A phase whose only pending pin is the hostapd binary can
        still be planned (its bench_session base ladder is complete)."""
        out = []
        if phase == "x":
            if env.get("X_SEQUENCE", "pending") == "pending" or not os.path.isfile(resolve_rel(env.get("X_SEQUENCE", ""))):
                out.append("X sequence pin pending (wifi-dp-air lane)")
            if env.get("X_MODULE", "pending") == "pending" or not os.path.isfile(resolve_rel(env.get("X_MODULE", ""))):
                out.append("X merged module pin pending (wifi-dp-air lane)")
        return out

    runnable, dep_records = [], []
    for p in phases:
        blockers = planning_blockers(p)
        if blockers:
            dep_records.append({"phase": p, "label": PHASE_LABEL[p],
                                "status": "DEPENDENCY-PENDING",
                                "detail": "; ".join(blockers)})
            continue
        if real and p in pend_set:
            continue
        runnable.append(p)
    if real and pin_fail:
        for f in pin_fail:
            print("PIN-FAIL %s: %s" % (f["phase"], f["why"]), file=sys.stderr)
        die("artifact pins failed; a real run is refused (fix artifacts.env)")
    if real and pend and not args.allow_partial:
        for p in pend:
            print("DEPENDENCY-PENDING phase %s: %s" % (p["phase"], p["why"]),
                  file=sys.stderr)
        die("phases still waiting on a lane commit; use --allow-partial to run "
            "the ready phases only")
    if not runnable:
        die("no runnable phase (all selected phases are dependency-pending)")

    stamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    evidence_dir = args.evidence_dir or os.path.join(
        ROOT, "build", "wifi-bench-session", stamp)
    os.makedirs(evidence_dir, exist_ok=True)
    run_kind = "attended-real" if real else "dry-run"
    print("[driver] run kind=%s evidence=%s phases=%s"
          % (run_kind, evidence_dir, ",".join(runnable)), flush=True)
    if dry_run:
        print("[driver] DRY-RUN: no device is contacted; bench_session is "
              "invoked with --dry-run (plans + audits only)", flush=True)

    if real and "a" in runnable and env.get("HOSTAPD_MODE", "apk") == "apk":
        ok_hp, why_hp = hostapd_preflight(env)
        if not ok_hp:
            die("phase A refused before any bench write: %s" % why_hp)
        print("[driver] hostapd preflight: %s" % why_hp, flush=True)

    records, evidence = list(dep_records), {}
    for p in sorted((r for r in records if r["status"] == "DEPENDENCY-PENDING"),
                    key=lambda r: r["phase"]):
        print("[driver] phase %s -> DEPENDENCY-PENDING (%s)"
              % (p["phase"].upper(), p["detail"]), flush=True)
    for p in runnable:
        print("\n[driver] ==== phase %s: %s ====" % (p.upper(), PHASE_LABEL[p]),
              flush=True)
        try:
            rec = run_phase(env, p, evidence_dir, dry_run, evidence)
        except Exception as e:  # noqa: BLE001 - never leave the box un-cycled
            rec = {"phase": p, "label": PHASE_LABEL[p], "status": "ERROR",
                   "detail": "driver exception: %r" % (e,),
                   "evidence_dir": os.path.join(evidence_dir, p)}
        records.append(rec)
        print("[driver] phase %s -> %s (%s)"
              % (p.upper(), rec["status"], rec["detail"][:160]), flush=True)
        if rec["status"] in ("HOST-PREFLIGHT-FAIL", "REFUSED"):
            print("[driver] aborting remaining phases: host preflight/refusal "
                  "(the box was not modified by this phase)", flush=True)
            for rest in runnable[runnable.index(p) + 1:]:
                records.append({"phase": rest, "label": PHASE_LABEL[rest],
                                "status": "NOT-RUN",
                                "detail": "skipped after %s reported %s"
                                          % (p, rec["status"])})
            break
        if real and rec["status"] == "STOP":
            print("[driver] STOP in phase %s: continuing to the cleanup reboot "
                  "(no retry in the same boot; analyse offline)" % p.upper(),
                  flush=True)
            break

    reboot_note = None
    if real and any(r["status"] not in ("NOT-RUN",) for r in records):
        ok, note = cleanup_reboot(env, os.path.join(evidence_dir,
                                                    "cleanup-reboot.log"))
        reboot_note = note
        if not ok:
            print("[driver] WARNING: cleanup reboot failed - the box may be in "
                  "an unknown state; power-cycle it manually before the next "
                  "session", file=sys.stderr)

    report_path = os.path.join(evidence_dir, "SESSION-REPORT.md")
    with open(report_path, "w", encoding="utf-8") as f:
        f.write(render_report(env, run_kind, stamp, records, pend,
                              evidence_dir, reboot_note))
    session = {"run_kind": run_kind, "generated": stamp,
               "evidence_dir": evidence_dir, "phases": records,
               "pending": pend, "pin_table": pin_table,
               "cleanup_reboot": reboot_note}
    with open(os.path.join(evidence_dir, "session.json"), "w",
              encoding="utf-8") as f:
        json.dump(session, f, indent=1, sort_keys=True)
        f.write("\n")
    print("\n[driver] report: %s" % report_path)
    if args.json:
        print(json.dumps(session, indent=1, sort_keys=True))
    # exit semantics
    statuses = [r["status"] for r in records]
    if dry_run:
        planned = [s for s in statuses if s not in ("DEPENDENCY-PENDING",)]
        return 0 if planned and all(s == "PLAN-OK" for s in planned) else 1
    if all(s == "PASS" for s in statuses) and not pend:
        return 0
    if any(s in ("PASS", "PARTIAL") for s in statuses) or pend:
        return 12
    return 10


if __name__ == "__main__":
    raise SystemExit(main())