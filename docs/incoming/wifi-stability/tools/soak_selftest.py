#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""soak_selftest.py - host tests for the wifi-stability soak harness/analyzer.

Runs with no device: builds synthetic sampler TSVs from the documented format,
replays them through tools/soak.py:analyze() and asserts the verdict class for
each scenario, then exercises the static device-script audit and the dry-run
plan builder.  This is what makes PASS/FAIL objective before an attended soak.

    cd opensource/docs/incoming/wifi-stability
    python3 tools/soak_selftest.py
"""
from __future__ import annotations

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import soak  # noqa: E402

FIELDS = soak.FIELDS
GOLDEN = [0x86807FEF, 0x00000007, 0x00000002, 0x0000ABCD]
passed = 0
failed = 0


def check(name, cond, detail=""):
    global passed, failed
    if cond:
        passed += 1
    else:
        failed += 1
        print("FAIL %-42s %s" % (name, detail))


def row(t, **kw):
    d = {
        "uptime": t, "page0": GOLDEN[0], "page1": GOLDEN[1],
        "page2": GOLDEN[2], "page3": GOLDEN[3], "state": 1, "mask": 0,
        "staging": 0, "hb": t, "excp": 0, "frames": t, "overruns": 0,
        "d2h_drop": 0, "phase": "ACTIVE", "memavail": 66784,
        "memfree": 30000, "load1": 0.15, "hapdm": 2,
        "hapd_rss_kb": 1500, "hapd_cpu": 200, "ap_ok": 1, "dm_new": 0,
    }
    d.update(kw)
    return d


def series(n=13, interval=10, **kw):
    return [row(i * interval, **kw) for i in range(n)]


def meta(duration=120, interval=10):
    return {"duration_s": duration, "interval_s": interval,
            "ssid": "cudy-ap-test", "module_sha": "test", "when": "selftest"}


def verdict(rows, dmesg=None, m=None):
    return soak.analyze(rows, dmesg or [], m or meta())["verdict"]


def tsv(rows):
    lines = [soak.SOAK_HEADER]
    for r in rows:
        lines.append(" ".join(str(r[f]) for f in FIELDS))
    lines.append("#END rc=0 elapsed=120 samples=%d" % len(rows))
    return "\n".join(lines) + "\n"


def format_roundtrip():
    rows = series()
    fd, path = tempfile.mkstemp(suffix=".tsv")
    try:
        with os.fdopen(fd, "w") as f:
            f.write(tsv(rows))
        parsed, end = soak.parse_samples(path)
    finally:
        os.unlink(path)
    check("tsv-rows", len(parsed) == len(rows), "%d != %d" % (len(parsed), len(rows)))
    check("tsv-end", end.get("samples") == str(len(rows)))
    check("tsv-values", parsed == rows)


def scenario(name, want, rows, dmesg=None, m=None):
    got = verdict(rows, dmesg, m)
    check(name, got == want, "got=%s want=%s" % (got, want))


def analyzer_cases():
    scenario("golden-passes", "PASS", series())
    # EXCEPTION: mask/staging/page deviation anywhere
    rows = series()
    rows[5] = row(50, mask=0x18)
    scenario("mask-exception", "FAIL-EXCEPTION", rows)
    rows = series()
    rows[7] = row(70, page0=0x00119758, staging=0xFFFFFFFF)
    scenario("page-lost-exception", "FAIL-EXCEPTION", rows)
    # heartbeat gap > 15s
    rows = series()
    for i in range(4, 10):
        rows[i]["hb"] = rows[4]["hb"]
    scenario("heartbeat-gap", "FAIL-HEARTBEAT", rows)
    # D2H overrun
    rows = series()
    rows[3]["overruns"] = 2
    scenario("d2h-overrun", "FAIL-D2H", rows)
    # hostapd death / AP down
    rows = series()
    for i in range(6, 13):
        rows[i]["hapdm"] = 0
    scenario("hostapd-dead", "FAIL-HOSTAPD", rows)
    rows = series()
    for i in range(6, 13):
        rows[i]["ap_ok"] = 0
    scenario("ap-iface-lost", "FAIL-HOSTAPD", rows)
    # memory drift
    rows = series()
    for i, r in enumerate(rows):
        r["memavail"] = 66784 - i * 2000
    scenario("memory-drift", "FAIL-MEMORY", rows)
    # dmesg anomaly
    scenario("dmesg-warning", "FAIL-DMESG", series(),
             ["[  120.1] WARNING: CPU: 0 PID: 812 at x.c:1"][:])
    scenario("dmesg-lane-closed", "FAIL-DMESG", series(),
             ["[  121.4] frame post ap_cfg: announce NOT consumed"])
    # sampling gap / short run / no rows
    rows = series()
    for i in range(4, len(rows)):
        rows[i]["uptime"] = rows[i]["uptime"] + 60
        rows[i]["hb"] = rows[i]["uptime"]
        rows[i]["frames"] = rows[i]["uptime"]
    scenario("sample-gap", "FAIL-SAMPLING", rows)
    scenario("no-rows", "FAIL-SAMPLING", [])
    scenario("short-run", "FAIL-SAMPLING", series(n=3))
    # reboot (uptime backwards)
    rows = series()
    rows[8]["uptime"] = 5
    scenario("reboot-detected", "FAIL-REBOOT", rows)


def audit_cases():
    cfg = {"duration": 600, "interval": 10, "ssid": "cudy-ap-test",
           "endpoint": soak.ENDPOINT}
    script = soak.build_bringup_script(cfg)
    check("plan-audit-clean", soak.audit_script(script) == [],
          str(soak.audit_script(script)))
    check("plan-has-bringup-ok", "BRINGUP OK" in script)
    check("plan-has-golden-gate", "0x86807fef" in script)
    check("plan-no-rmmod", "rmmod" not in script)
    bad = audit_cases.cfg = dict(cfg)
    cases = [
        ("audit-rmmod", script + "\nrmmod hi5622_wifi\n", "rmmod"),
        ("audit-never-write", script + "\ndevmem 0x400392f0 1\n",
         "0x400392F0"),
        ("audit-rc-misc", script + "\ncat /dev/mem 0x10161000\n",
         "RC misc"),
        ("audit-attr-allowlist", script + "\necho 1 > $D/h2d_send\n",
         "allowlist"),
        ("audit-flash", script + "\nubiformat /dev/mtd14\n", "flash"),
    ]
    for name, text, want in cases:
        hits = " ".join(soak.audit_script(text))
        check(name, want in hits, "hits=%r" % hits)


def policy_cases():
    # heartbeat rate policy: 11 sampled heartbeats over 120 s is still PASS
    rows = series()
    for i, r in enumerate(rows):
        r["hb"] = i * 11
        r["frames"] = i * 11
    check("hb-slower-still-pass", verdict(rows) == "PASS", verdict(rows))
    # phase must be ACTIVE
    rows = series()
    rows[4]["phase"] = "READY_TXQ_SYNC"
    check("phase-not-active", verdict(rows) == "FAIL-HOSTAPD", verdict(rows))


def cli_cases():
    p = subprocess.run([sys.executable, os.path.join(HERE, "soak.py"),
                        "dry-run", "--duration", "120", "--interval", "10"],
                       capture_output=True, text=True, timeout=120)
    check("cli-dry-run-rc", p.returncode == 0, p.stderr[-300:])
    check("cli-dry-run-ok", "DRY-RUN OK" in p.stdout)
    check("cli-dry-run-audit", "AUDIT OK" in p.stdout)
    fd, path = tempfile.mkstemp(suffix=".tsv")
    try:
        with os.fdopen(fd, "w") as f:
            f.write(tsv(series()))
        p = subprocess.run([sys.executable, os.path.join(HERE, "soak.py"),
                            "analyze", "--samples", path, "--duration", "120",
                            "--interval", "10"], capture_output=True, text=True,
                           timeout=120)
        check("cli-analyze-pass-rc", p.returncode == 0, p.stdout[-300:])
        check("cli-analyze-pass", "verdict: **PASS**" in p.stdout)
    finally:
        os.unlink(path)


def main():
    format_roundtrip()
    analyzer_cases()
    audit_cases()
    policy_cases()
    cli_cases()
    print("soak_selftest: %d passed, %d failed" % (passed, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
