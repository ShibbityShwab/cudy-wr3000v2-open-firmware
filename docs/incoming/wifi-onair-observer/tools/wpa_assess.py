#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""wpa_assess.py - classify a host wpa_supplicant association attempt.

HOST-ONLY: reads captured text (wpa_supplicant debug log + wpa_cli status),
never touches a netdev.

The bench expectation is part of the verdict: the clean-room AP driver's
management-TX path is fail-closed and there is no management RX, so a client
authentication/association attempt is EXPECTED to stop at the AUTH stage (the
documented usable-AP blocker; see the parent notebook ledger 2026-11-28/29 and
docs/incoming/wifi-beacon-static/REPORT.md).  That outcome is classified as
EXPECTED-FAIL, not as a harness defect.  A completed association is a genuine
surprise and is recorded as such (its 4-way-handshake result decides the key
path question).

Exit codes: 0 verdict PASS or EXPECTED-FAIL (harness worked), 1 unexpected
failure, 2 HARNESS-ERROR (missing/empty captures).
"""
from __future__ import annotations

import argparse
import json
import re
import sys

# Ordered terminal patterns: (name, regex, stage).
PATTERNS = [
    ("auth_timeout", re.compile(r"Authentication with .* timed out"), "AUTH"),
    ("auth_reject", re.compile(r"CTRL-EVENT-AUTH-REJECT"), "AUTH"),
    ("assoc_timeout", re.compile(r"Association with .* timed out"), "ASSOC"),
    ("assoc_reject", re.compile(r"CTRL-EVENT-ASSOC-REJECT"), "ASSOC"),
    ("fourway_failed", re.compile(r"4-Way Handshake failed"), "4WAY"),
    ("disconnected", re.compile(r"CTRL-EVENT-DISCONNECTED"), "DISCONNECTED"),
    ("connected", re.compile(r"CTRL-EVENT-CONNECTED"), "CONNECTED"),
    ("negotiated", re.compile(r"WPA: Key negotiation completed"), "4WAY-DONE"),
    ("trying_assoc", re.compile(r"Trying to associate"), "TRYING"),
    ("sme_auth", re.compile(r"SME: Trying to authenticate"), "TRYING"),
    ("sae", re.compile(r"SAE: "), "SAE"),
]
EVIDENCE_RES = [
    re.compile(r"Trying to associate"),
    re.compile(r"SME: Trying to authenticate"),
    re.compile(r"Authentication"),
    re.compile(r"Associated with"),
    re.compile(r"CTRL-EVENT-CONNECTED"),
    re.compile(r"CTRL-EVENT-DISCONNECTED"),
    re.compile(r"CTRL-EVENT-AUTH"),
    re.compile(r"CTRL-EVENT-ASSOC"),
    re.compile(r"4-Way Handshake"),
    re.compile(r"WPA: Key negotiation completed"),
    re.compile(r"EAPOL-Key"),
    re.compile(r"SAE: "),
    re.compile(r"reason="),
    re.compile(r"deauthenticat"),
]
REASON_RE = re.compile(r"reason[=:]\s*(\d+)")
BSSID_RE = re.compile(r"bssid=([0-9a-fA-F:]{17})")


def parse_status(text: str) -> dict:
    status: dict = {}
    for line in text.splitlines():
        if "=" not in line:
            continue
        key, _, val = line.partition("=")
        key = key.strip()
        if key and key not in status:
            status[key] = val.strip()
    return status


def collect_evidence(log: str) -> list[str]:
    out = []
    for idx, line in enumerate(log.splitlines(), 1):
        for rx in EVIDENCE_RES:
            if rx.search(line):
                out.append("L%04d %s" % (idx, line.strip()))
                break
    return out[-60:]


def assess(log: str, status_text: str, variant: str, expect_bssid: str) -> dict:
    status = parse_status(status_text)
    evidence = collect_evidence(log)
    result: dict = {
        "variant": variant,
        "status": status,
        "stage": "UNKNOWN",
        "assoc": False,
        "fourway": "not-reached",
        "bssid": status.get("bssid", ""),
        "ssid": status.get("ssid", ""),
        "key_mgmt": status.get("key_mgmt", ""),
        "pairwise_cipher": status.get("pairwise_cipher", ""),
        "group_cipher": status.get("group_cipher", ""),
        "wpa_state": status.get("wpa_state", ""),
        "reason": "",
        "sae_seen": bool(re.search(r"SAE: ", log)),
        "fourway_evidence": "",
        "evidence_lines": evidence,
        "checks": [],
        "verdict": "FAIL-UNKNOWN",
        "expected": "UNEXPECTED",
        "detail": "",
    }
    checks = result["checks"]

    def add(name: str, ok: bool, detail: str) -> None:
        checks.append({"name": name, "ok": bool(ok), "detail": detail})

    found = {name: bool(rx.search(log)) for name, rx, _stage in PATTERNS}
    state = result["wpa_state"].upper()
    for name, rx in (("negotiated", re.compile(r"WPA: Key negotiation completed")),
                     ("fourway_ok", re.compile(r"Key negotiation completed"))):
        found[name] = bool(rx.search(log))

    if state == "COMPLETED":
        result["assoc"] = True
        result["stage"] = "COMPLETED"
    elif found["connected"] and found["negotiated"]:
        result["assoc"] = True
        result["stage"] = "COMPLETED"
        result["stage_note"] = "COMPLETED inferred from log (final status poll stale)"
    elif found["fourway_failed"]:
        result["stage"] = "4WAY-FAILED"
    elif found["auth_timeout"] or found["auth_reject"]:
        result["stage"] = "AUTH-TIMEOUT" if found["auth_timeout"] else "AUTH-REJECT"
    elif found["assoc_timeout"] or found["assoc_reject"]:
        result["stage"] = "ASSOC-TIMEOUT" if found["assoc_timeout"] else "ASSOC-REJECT"
    elif found["negotiated"]:
        result["stage"] = "4WAY-DONE"
    elif found["connected"]:
        result["stage"] = "CONNECTED-NO-KEYS"
    elif found["disconnected"]:
        result["stage"] = "DISCONNECTED"
    elif found["trying_assoc"]:
        result["stage"] = "AUTH-PENDING"

    m = REASON_RE.search(log)
    if m:
        result["reason"] = m.group(1)
    if not result["bssid"]:
        m = BSSID_RE.search(log)
        if m:
            result["bssid"] = m.group(1).lower()

    # Four-way handshake result: decisive once association completed.
    if result["assoc"]:
        if found["negotiated"]:
            result["fourway"] = "PASS"
            result["fourway_evidence"] = "WPA: Key negotiation completed (PTK/GTK installed; see evidence lines)"
            add("fourway_handshake", True, result["fourway_evidence"])
            add("wpa_state_completed", state == "COMPLETED", "wpa_state=%s" % (state or "?"))
            if result["key_mgmt"]:
                expected_km = "SAE" if variant == "wpa3-sae" else "WPA2-PSK"
                add("key_mgmt_%s" % expected_km.replace("-", "_"), expected_km in result["key_mgmt"],
                    "key_mgmt=%s (expected %s)" % (result["key_mgmt"], expected_km))
            if expect_bssid:
                add("bssid_matches_beacon", result["bssid"] == expect_bssid.lower(),
                    "associated bssid=%s (beacon bssid=%s)" % (result["bssid"], expect_bssid))
        else:
            result["fourway"] = "FAIL"
            add("fourway_handshake", False, "association completed but no key-negotiation evidence")
        if variant == "wpa3-sae" and not result["sae_seen"]:
            add("sae_used", False, "WPA3 variant but no SAE exchange in the log")
        failed = [c for c in checks if not c["ok"]]
        result["verdict"] = "PASS" if not failed else "FAIL-4WAY"
        result["expected"] = "UNEXPECTED-PASS"
        result["detail"] = ("association COMPLETED - UNEXPECTED relative to the known AP mgmt-TX "
                            "blocker; fourway=%s. Record this as new evidence and check the "
                            "AP-side key/4-way logs." % result["fourway"])
        return result

    # Not associated: classify against the documented bench expectation.
    expected_fail_stages = {
        "AUTH-TIMEOUT": "client AUTH frame sent, no AP management response (AP .mgmt_tx fail-closed / no mgmt RX: wal_cfg80211_mgmt_tx family is the usable-AP blocker)",
        "AUTH-REJECT": "AP rejected AUTH (management frame path)",
        "ASSOC-TIMEOUT": "AUTH answered but ASSOC got no response (AP mgmt path partially present?)",
        "ASSOC-REJECT": "AP rejected ASSOC",
        "AUTH-PENDING": "attempt still in AUTH at the timeout (client never got a reply)",
        "DISCONNECTED": "AP/client disconnected before completion (reason %s)" % (result["reason"] or "?"),
    }
    if variant == "wpa3-sae" and (result["sae_seen"] or result["stage"].startswith("AUTH")):
        result["expected"] = "EXPECTED-FAIL"
        result["verdict"] = "EXPECTED-FAIL"
        result["detail"] = ("WPA3-SAE attempt stopped at %s (SAE seen=%s). EXPECTED: SAE needs the "
                            "AUTH management TX/RX path, which is fail-closed. %s" % (
                                result["stage"], result["sae_seen"],
                                expected_fail_stages.get(result["stage"], "")))
        add("stopped_at_auth_or_assoc", True, result["stage"])
        return result
    if result["stage"] in expected_fail_stages:
        result["expected"] = "EXPECTED-FAIL"
        result["verdict"] = "EXPECTED-FAIL"
        result["detail"] = ("attempt stopped at %s - EXPECTED: %s. This reproduces the documented "
                            "usable-AP blocker, it is not a harness defect." % (
                                result["stage"], expected_fail_stages[result["stage"]]))
        add("stopped_at_expected_blocker", True, result["stage"])
        return result
    if result["stage"] == "4WAY-DONE":
        result["verdict"] = "FAIL-4WAY"
        result["detail"] = "4-way negotiation started but no completion evidence; check AP key logs"
        return result
    if result["stage"] == "4WAY-FAILED":
        result["verdict"] = "FAIL-4WAY"
        result["detail"] = ("4-way handshake FAILED (association completed): key install mismatch or "
                            "GTK/PTK path defect - capture the AP-side key logs")
        return result
    result["verdict"] = "FAIL-UNKNOWN"
    result["detail"] = "attempt outcome not classifiable from the captured log (see evidence lines)"
    return result


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--supplicant-log", required=True)
    ap.add_argument("--status", required=True, help="final wpa_cli status capture")
    ap.add_argument("--variant", choices=("wpa2-psk", "wpa3-sae"), default="wpa2-psk")
    ap.add_argument("--expect-bssid", default="")
    ap.add_argument("--json-out", default="")
    args = ap.parse_args(argv)
    try:
        with open(args.supplicant_log, "r", encoding="utf-8", errors="replace") as fh:
            log = fh.read()
        with open(args.status, "r", encoding="utf-8", errors="replace") as fh:
            status_text = fh.read()
    except OSError as exc:
        print("wpa_assess: cannot read capture: %s" % exc, file=sys.stderr)
        return 2
    if not log.strip():
        print("wpa_assess: supplicant log is empty (wpa_supplicant did not start)", file=sys.stderr)
        return 2
    result = assess(log, status_text, args.variant, args.expect_bssid)
    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as fh:
            json.dump(result, fh, indent=2, sort_keys=True)
            fh.write("\n")
    print("assoc stage: %s" % result["stage"])
    print("assoc complete: %s   four-way: %s   wpa_state: %s" % (
        result["assoc"], result["fourway"], result["wpa_state"] or "?"))
    if result["bssid"]:
        print("bssid: %s   ssid: %s" % (result["bssid"], result["ssid"]))
    if result["key_mgmt"]:
        print("key_mgmt: %s   pairwise: %s   group: %s" % (
            result["key_mgmt"], result["pairwise_cipher"], result["group_cipher"]))
    if result["reason"]:
        print("disconnect reason: %s" % result["reason"])
    print("expectation: %s" % result["expected"])
    print("verdict: %s: %s" % (result["verdict"], result["detail"]))
    for check in result["checks"]:
        print("  [%s] %-28s %s" % ("ok" if check["ok"] else "FAIL", check["name"], check["detail"]))
    return 0 if result["verdict"] in ("PASS", "EXPECTED-FAIL") else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
