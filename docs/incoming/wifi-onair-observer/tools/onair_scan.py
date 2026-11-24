#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""onair_scan.py - parse an iw scan dump into the observer's beacon verdict.

HOST-ONLY: reads a captured text file, never touches a netdev by itself.

Input is the output of
    iw dev <observer-iface> scan [freq <mhz>] [passive] -u
(iw 6.17 format).  The parser keeps only the BSS we care about (the expected
SSID) and reports counts of everything else - neighbour SSIDs/BSSIDs are not
written to the summary, so live evidence stays privacy-clean.

Exit codes: 0 PASS, 1 FAIL, 2 HARNESS-ERROR (input missing/unparseable).
"""
from __future__ import annotations

import argparse
import json
import re
import sys

BSS_RE = re.compile(r"^BSS ([0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5})(?:\s+\(on\s+(\S+)\))?")
FREQ_RE = re.compile(r"^\s*freq:\s*(\d+)")
SIGNAL_RE = re.compile(r"^\s*signal:\s*(-?\d+(?:\.\d+)?)\s*dBm")
CAP_RE = re.compile(r"^\s*capability:\s*(.*?)\s*\((0x[0-9a-fA-F]+)\)")
BEACON_INT_RE = re.compile(r"^\s*beacon interval:\s*(\d+)\s*TUs")
SSID_RE = re.compile(r"^\s*SSID:\s?(.*)$")
DS_RE = re.compile(r"^\s*DS Parameter set:\s*channel\s*(\d+)")
HT_RE = re.compile(r"^\s*HT (capabilities|operation):")
IE_FRAME_RE = re.compile(r"^\s*Information elements from\s+(Beacon|Probe Response) frame:")
RSN_VERSION_RE = re.compile(r"\* Version:\s*(\d+)")
RSN_GROUP_RE = re.compile(r"\* Group cipher:\s*(\S+)")
RSN_PAIRWISE_RE = re.compile(r"\* Pairwise ciphers:\s*(.*)$")
RSN_AKM_RE = re.compile(r"\* Authentication suites:\s*(.*)$")
RSN_CAP_RE = re.compile(r"\* Capabilities:\s*(.*)$")
IE_LINE_RE = re.compile(r"^\t (\S[^:]*?):")


def _parse_rsn_fragment(cur: dict, text: str) -> None:
    """Consume one RSN-IE text fragment (iw puts the first field on the RSN line)."""
    m = RSN_VERSION_RE.search(text)
    if m:
        cur["rsn_version"] = int(m.group(1))
        return
    m = RSN_GROUP_RE.search(text)
    if m:
        cur["group_cipher"] = m.group(1)
        return
    m = RSN_PAIRWISE_RE.search(text)
    if m:
        cur["pairwise"] = [v for v in re.split(r"\s+", m.group(1).strip()) if v] or cur["pairwise"]
        return
    m = RSN_AKM_RE.search(text)
    if m:
        cur["akm"] = [v for v in re.split(r"\s+", m.group(1).strip()) if v] or cur["akm"]
        return
    m = RSN_CAP_RE.search(text)
    if m:
        cur["rsn_caps"] = m.group(1).strip()


def parse_scan(text: str) -> list[dict]:
    """Return one dict per BSS block, keeping only fields we need."""
    bss_list: list[dict] = []
    cur: dict | None = None
    ie_frame = None
    in_rsn = False
    for raw in text.splitlines():
        m = BSS_RE.match(raw)
        if m:
            cur = {
                "bssid": m.group(1).lower(),
                "on_iface": (m.group(2) or "").strip(),
                "freq": None,
                "channel": None,
                "signal_dbm": None,
                "beacon_interval": None,
                "capability": None,
                "privacy": False,
                "ssid": None,
                "ie_source": None,
                "ie_names": [],
                "rsn": False,
                "rsn_version": None,
                "group_cipher": None,
                "pairwise": [],
                "akm": [],
                "rsn_caps": "",
                "ht": False,
            }
            bss_list.append(cur)
            ie_frame = None
            in_rsn = False
            continue
        if cur is None:
            continue
        m = IE_FRAME_RE.match(raw)
        if m:
            ie_frame = "beacon" if m.group(1) == "Beacon" else "probe_response"
            cur["ie_source"] = ie_frame
            in_rsn = False
            continue
        m = FREQ_RE.match(raw)
        if m:
            cur["freq"] = int(m.group(1))
            continue
        m = SIGNAL_RE.match(raw)
        if m:
            cur["signal_dbm"] = float(m.group(1))
            continue
        m = BEACON_INT_RE.match(raw)
        if m:
            cur["beacon_interval"] = int(m.group(1))
            continue
        m = CAP_RE.match(raw)
        if m:
            cur["capability"] = m.group(2).lower()
            try:
                flags = int(m.group(2), 16)
            except ValueError:
                flags = 0
            cur["privacy"] = bool(flags & 0x0010)
            continue
        m = DS_RE.match(raw)
        if m:
            cur["channel"] = int(m.group(1))
            je = IE_LINE_RE.match(raw)
            if je:
                cur["ie_names"].append("DS Parameter set")
            continue
        m = SSID_RE.match(raw)
        if m:
            cur["ssid"] = m.group(1).strip()
            je = IE_LINE_RE.match(raw)
            if je and "SSID" not in cur["ie_names"]:
                cur["ie_names"].append("SSID")
            continue
        if HT_RE.match(raw):
            cur["ht"] = True
            je = IE_LINE_RE.match(raw)
            if je and je.group(1).strip() not in cur["ie_names"]:
                cur["ie_names"].append(je.group(1).strip())
            continue
        m = re.match(r"^\s+RSN:\s*(.*)$", raw)
        if m:
            cur["rsn"] = True
            in_rsn = True
            if "RSN" not in cur["ie_names"]:
                cur["ie_names"].append("RSN")
            _parse_rsn_fragment(cur, m.group(1))
            continue
        if in_rsn:
            _parse_rsn_fragment(cur, raw)
            if raw.strip() == "":
                in_rsn = False
            continue
        je = IE_LINE_RE.match(raw)
        if je:
            name = je.group(1).strip()
            if name not in cur["ie_names"]:
                cur["ie_names"].append(name)
    return bss_list


def evaluate(bss_list: list[dict], ssid: str, freq: int, channel: int,
             expect_wpa2: bool, any_freq: bool,
             allow_probe_response: bool, require_akm: str = "") -> dict:
    matches = [b for b in bss_list if (b["ssid"] or "") == ssid]
    others = [b for b in bss_list if (b["ssid"] or "") != ssid]
    result: dict = {
        "expected_ssid": ssid,
        "expected_freq": freq,
        "expected_channel": channel,
        "bss_count_total": len(bss_list),
        "bss_count_other": len(others),
        "target": None,
        "checks": [],
        "verdict": "FAIL",
        "detail": "",
    }
    checks = result["checks"]

    def add(name: str, ok: bool, detail: str) -> None:
        checks.append({"name": name, "ok": bool(ok), "detail": detail})

    if not matches:
        add("ssid_found", False, "expected SSID not present in scan results")
        result["detail"] = "expected SSID not seen on air"
        return result
    add("ssid_found", True, "%d BSS match the expected SSID" % len(matches))
    # Prefer a BSS seen in a Beacon frame (decisive on-air beacon proof).
    matches.sort(key=lambda b: (b["ie_source"] != "beacon", -(b["signal_dbm"] or -999)))
    target = matches[0]
    result["target"] = target
    add("bssid_valid", bool(re.match(r"^([0-9a-f]{2}:){5}[0-9a-f]{2}$", target["bssid"])), target["bssid"])
    add("ie_source_beacon", target["ie_source"] == "beacon" or allow_probe_response,
        "IE source: %s" % (target["ie_source"] or "unknown"))
    add("freq_expected", any_freq or target["freq"] == freq,
        "freq=%s (expected %s)" % (target["freq"], freq))
    if target["channel"] is not None:
        add("ds_channel_expected", any_freq or target["channel"] == channel,
            "DS channel=%s (expected %s)" % (target["channel"], channel))
    else:
        add("ds_channel_expected", False, "no DS Parameter set IE (beacon lacks channel element)")
    if target["beacon_interval"] is not None:
        add("beacon_interval_100tu", target["beacon_interval"] == 100,
            "beacon interval=%s TU (AP fixture requests 100)" % target["beacon_interval"])
    if expect_wpa2:
        add("capability_privacy", target["privacy"], "capability=%s" % target["capability"])
        add("rsn_ie_present", target["rsn"], "RSN IE present: %s" % target["rsn"])
        add("rsn_psk_akm", any("PSK" in a for a in target["akm"]),
            "AKM suites: %s" % ", ".join(target["akm"]) if target["akm"] else "no AKM parsed")
        add("rsn_ccmp", any("CCMP" in c for c in target["pairwise"]),
            "pairwise ciphers: %s" % ", ".join(target["pairwise"]))
        if require_akm:
            add("rsn_akm_%s" % require_akm, any(require_akm in a for a in target["akm"]),
                "AKM suites: %s (require %s)" % (", ".join(target["akm"]), require_akm))
    else:
        add("open_privacy_absent", not target["privacy"],
            "capability=%s (open fixture expectation)" % target["capability"])

    failed = [c for c in checks if not c["ok"]]
    result["verdict"] = "PASS" if not failed else "FAIL"
    if failed:
        result["detail"] = "; ".join("%s [%s]" % (c["name"], c["detail"]) for c in failed)
    else:
        result["detail"] = "beacon on air: ssid=%r bssid=%s freq=%s ch=%s signal=%sdBm security=%s" % (
            target["ssid"], target["bssid"], target["freq"],
            target["channel"], target["signal_dbm"],
            "RSN/PSK/CCMP" if target["rsn"] else "open",
        )
    return result


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--iw-scan", required=True, help="captured iw scan output file")
    ap.add_argument("--expect-ssid", default="cudy-ap-test")
    ap.add_argument("--expect-freq", type=int, default=2437)
    ap.add_argument("--expect-channel", type=int, default=6)
    ap.add_argument("--expect-wpa2", action="store_true",
                    help="require Privacy capability + RSN IE with PSK/CCMP")
    ap.add_argument("--any-freq", action="store_true",
                    help="accept the SSID on any channel (diagnosis scan)")
    ap.add_argument("--require-akm", default="",
                    help="require this AKM token in the beacon RSN IE (e.g. SAE)")
    ap.add_argument("--allow-probe-response", action="store_true",
                    help="accept Probe Response as IE source (weaker: not beacon proof)")
    ap.add_argument("--json-out", default="")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)

    try:
        with open(args.iw_scan, "r", encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    except OSError as exc:
        print("onair_scan: cannot read %s: %s" % (args.iw_scan, exc), file=sys.stderr)
        return 2
    if not text.strip():
        print("onair_scan: empty iw scan capture", file=sys.stderr)
        return 2
    bss_list = parse_scan(text)
    if not bss_list:
        print("onair_scan: no BSS blocks parsed (iw format drift?)", file=sys.stderr)
        return 2
    result = evaluate(bss_list, args.expect_ssid, args.expect_freq,
                      args.expect_channel, args.expect_wpa2, args.any_freq,
                      args.allow_probe_response, args.require_akm)
    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as fh:
            json.dump(result, fh, indent=2, sort_keys=True)
            fh.write("\n")
    if not args.quiet:
        print("scan: blocks=%d other-aps=%d target=%s" % (
            result["bss_count_total"], result["bss_count_other"],
            "yes" if result["target"] else "no"))
        for check in result["checks"]:
            print("  [%s] %-24s %s" % ("ok" if check["ok"] else "FAIL", check["name"], check["detail"]))
        print("scan: %s: %s" % (result["verdict"], result["detail"]))
    return 0 if result["verdict"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))