#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""sniff.py - optional monitor-mode beacon / auth capture for the observer.

HOST-ONLY: opens a raw AF_PACKET socket on the host's monitor interface
(created by tools/iface-up.sh --monitor) and writes a radiotap pcap plus a
JSON summary.  No bench-router contact: this only receives 802.11 frames on
the host's own radio.

Why it exists: the host has no tcpdump/tshark, and the decisive on-air gate
wants more than a scan list - beacon count/interval and the client's
AUTH frame with no AP response are the strongest evidence this lane can
produce.  The scan path (tools/scan-ap.sh) stays the primary rung; this
capture is best-effort because monitor-interface concurrency depends on the
host wiphy.

Live run (root, monitor iface up):
    python3 tools/sniff.py --iface mon-onair --ssid cudy-ap-test \
        --duration 20 --pcap-out <dir>/beacons.pcap \
        --json-out <dir>/beacon-summary.json
Offline re-assessment:
    python3 tools/sniff.py --assess-only --pcap <dir>/beacons.pcap \
        --ssid cudy-ap-test --json-out <dir>/beacon-summary.json

Exit codes: 0 at least one expected-SSID beacon captured; 1 no expected
beacon; 2 HARNESS-ERROR (socket/root/parse).  The caller decides what a
FAIL/INCONCLUSIVE means; capture of the client AUTH frame is reported in the
summary regardless of the verdict.
"""
from __future__ import annotations

import argparse
import json
import os
import select
import socket
import struct
import sys
import time

PCAP_LINKTYPE_IEEE80211_RADIOTAP = 127

RADIOTAP_FIELDS = {
    # bit -> (alignment, size)
    0: (8, 8),   # TSFT
    1: (1, 1),   # Flags
    2: (1, 1),   # Rate
    3: (2, 4),   # Channel
    4: (2, 2),   # FHSS
    5: (1, 1),   # dBm antsignal
    6: (2, 2),   # dBm antnoise
    7: (1, 1),   # lock quality
    8: (2, 2),   # TX attenuator
    9: (2, 2),   # dB TX attenuator
    10: (1, 1),  # dBm TX power
    11: (1, 1),  # antenna
    12: (1, 1),  # dB antenna signal
    13: (1, 1),  # dB antenna noise
    14: (2, 2),  # RX flags
    15: (2, 2),  # TX flags
    16: (1, 1),  # RTS retries
    17: (1, 1),  # data retries
}


def align(offset: int, alignment: int) -> int:
    return (offset + alignment - 1) & ~(alignment - 1)


def parse_radiotap(data: bytes) -> dict:
    out = {"it_len": 0, "freq": None, "channel": None, "dbm_antsignal": None}
    if len(data) < 8 or data[0] != 0:
        return out
    it_len = struct.unpack("<H", data[2:4])[0]
    out["it_len"] = it_len
    if it_len < 8 or it_len > len(data):
        return out
    present_words = []
    offset = 4
    while True:
        if offset + 4 > it_len:
            return out
        word = struct.unpack("<I", data[offset:offset + 4])[0]
        present_words.append(word)
        offset += 4
        if not (word & 0x80000000):
            break
    bit = 0
    for word in present_words:
        for b in range(32):
            if not (word & (1 << b)):
                continue
            field_bit = bit + b
            if field_bit not in RADIOTAP_FIELDS:
                continue
            alignment, size = RADIOTAP_FIELDS[field_bit]
            offset = align(offset, alignment)
            if offset + size > it_len:
                return out
            raw = data[offset:offset + size]
            if field_bit == 3:
                freq, flags = struct.unpack("<HH", raw)
                out["freq"] = freq
                out["channel"] = flags & 0x0FFF
            elif field_bit == 5:
                out["dbm_antsignal"] = struct.unpack("<b", raw)[0]
            offset += size
        bit += 32
    return out


def parse_ies(ies: bytes) -> dict:
    out: dict = {"ssid": None, "ds_channel": None, "rsn": False, "akm": [],
                 "ht": False, "ie_names": []}
    i = 0
    while i + 2 <= len(ies):
        eid = ies[i]
        length = ies[i + 1]
        body = ies[i + 2:i + 2 + length]
        if len(body) < length:
            break
        i += 2 + length
        if eid == 0:
            out["ssid"] = body.decode("utf-8", "replace")
            out["ie_names"].append("SSID")
        elif eid == 3 and length >= 1:
            out["ds_channel"] = body[0]
            out["ie_names"].append("DS")
        elif eid == 48:
            out["rsn"] = True
            out["ie_names"].append("RSN")
            # walk RSN IE for AKM suite list (suite selector 00-0f-ac:<type>)
            try:
                pos = 2 + 4  # version + group cipher
                if pos + 2 <= len(body):
                    pairwise_count = struct.unpack("<H", body[pos:pos + 2])[0]
                    pos += 2 + 4 * pairwise_count
                if pos + 2 <= len(body):
                    akm_count = struct.unpack("<H", body[pos:pos + 2])[0]
                    pos += 2
                    for _ in range(akm_count):
                        suite = body[pos:pos + 4]
                        pos += 4
                        if len(suite) == 4 and suite[:3] == b"\x00\x0f\xac":
                            out["akm"].append({1: "WEP40", 2: "PSK", 4: "FT-PSK",
                                               8: "SAE", 9: "FT-SAE"}.get(suite[3], str(suite[3])))
            except (struct.error, IndexError):
                pass
        elif eid == 45:
            out["ht"] = True
            out["ie_names"].append("HT")
        else:
            out["ie_names"].append("id%d" % eid)
    return out


def parse_frame(data: bytes) -> dict | None:
    """Parse one radiotap+802.11 frame; return None when not 802.11."""
    rt = parse_radiotap(data)
    off = rt["it_len"]
    if off < 8 or len(data) < off + 24:
        return None
    fc = struct.unpack("<H", data[off:off + 2])[0]
    ftype = (fc >> 2) & 0x3
    subtype = (fc >> 4) & 0xF
    addr1 = data[off + 4:off + 10]
    addr2 = data[off + 10:off + 16]
    addr3 = data[off + 16:off + 22]
    frame = {
        "type": ftype,
        "subtype": subtype,
        "addr1": ":".join("%02x" % b for b in addr1),
        "addr2": ":".join("%02x" % b for b in addr2),
        "addr3": ":".join("%02x" % b for b in addr3),
        "radiotap": rt,
    }
    if ftype == 0 and subtype == 8:  # beacon
        fixed = off + 24
        if len(data) < fixed + 12:
            return frame
        beacon_interval = struct.unpack("<H", data[fixed + 8:fixed + 10])[0]
        cap = struct.unpack("<H", data[fixed + 10:fixed + 12])[0]
        ies = parse_ies(data[fixed + 12:])
        frame.update({"beacon_interval": beacon_interval,
                      "capability": "0x%04x" % cap,
                      "privacy": bool(cap & 0x0010), "ies": ies})
    elif ftype == 0 and subtype in (0, 2, 11):  # assoc req / reassoc req / auth
        frame["mgmt"] = {0: "assoc-req", 2: "reassoc-req", 11: "auth"}[subtype]
    elif ftype == 0 and subtype == 12:  # deauth
        frame["mgmt"] = "deauth"
    return frame


class PcapWriter:
    def __init__(self, path: str):
        self.fh = open(path, "wb")
        self.fh.write(struct.pack("<IHHiIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535,
                                  PCAP_LINKTYPE_IEEE80211_RADIOTAP))

    def write(self, ts: float, data: bytes) -> None:
        sec = int(ts)
        usec = int((ts - sec) * 1_000_000)
        self.fh.write(struct.pack("<IIII", sec, usec, len(data), len(data)))
        self.fh.write(data)

    def close(self) -> None:
        self.fh.close()


def empty_summary(ssid: str) -> dict:
    return {
        "expected_ssid": ssid,
        "frames_total": 0,
        "beacons_total": 0,
        "beacons_expected": 0,
        "beacon_bssid": "",
        "beacon_intervals_ms": [],
        "beacon_median_interval_ms": None,
        "beacon_signal_dbm": [],
        "beacon_ies": [],
        "beacon_privacy": None,
        "ap_bssid": "",
        "auth_frames": 0,
        "auth_to_ap": 0,
        "auth_from_ap": 0,
        "assoc_requests": 0,
        "deauth_frames": 0,
        "other_ssids_count": 0,
        "verdict": "INCONCLUSIVE",
        "detail": "",
    }


def finalize(summary: dict) -> None:
    beacons = summary["beacons_expected"]
    intervals = summary["beacon_intervals_ms"]
    if beacons:
        intervals_sorted = sorted(intervals)
        summary["beacon_median_interval_ms"] = round(
            intervals_sorted[len(intervals_sorted) // 2], 2) if intervals_sorted else None
    if beacons >= 3:
        summary["verdict"] = "PASS"
        summary["detail"] = ("captured %d beacon(s) from %s at ~%sms median interval; "
                             "auth_to_ap=%d auth_from_ap=%d assoc_req=%d" % (
                                 beacons, summary["beacon_bssid"],
                                 summary["beacon_median_interval_ms"],
                                 summary["auth_to_ap"], summary["auth_from_ap"],
                                 summary["assoc_requests"]))
        if summary["auth_to_ap"] and not summary["auth_from_ap"]:
            summary["detail"] += (" - client AUTH seen on air with NO AP AUTH reply "
                                  "(reproduces the fail-closed AP mgmt-TX blocker)")
    elif beacons > 0:
        summary["verdict"] = "INCONCLUSIVE"
        summary["detail"] = "only %d beacon(s) captured - capture window too short?" % beacons
    elif summary["frames_total"] == 0:
        summary["verdict"] = "FAIL"
        summary["detail"] = "monitor captured no 802.11 frames (monitor/channel/regdom issue)"
    else:
        summary["verdict"] = "FAIL"
        summary["detail"] = ("monitor captured %d frames but no beacon for SSID %r "
                             "(beacon not on air? wrong channel?)" % (
                                 summary["frames_total"], summary["expected_ssid"]))


def assess_pcap(path: str, ssid: str) -> dict:
    summary = empty_summary(ssid)
    last_beacon = 0.0
    with open(path, "rb") as fh:
        header = fh.read(24)
        if len(header) < 24:
            finalize(summary)
            return summary
        while True:
            ph = fh.read(16)
            if len(ph) < 16:
                break
            sec, usec, incl_len, _orig = struct.unpack("<IIII", ph)
            data = fh.read(incl_len)
            if len(data) < incl_len:
                break
            ts = sec + usec / 1_000_000.0
            summary["frames_total"] += 1
            frame = parse_frame(data)
            if not frame:
                continue
            if frame["type"] == 0 and frame["subtype"] == 8:
                summary["beacons_total"] += 1
                ies = frame.get("ies") or {}
                if ies.get("ssid") == ssid:
                    summary["beacons_expected"] += 1
                    summary["beacon_bssid"] = frame["addr3"]
                    summary["beacon_ies"] = ies.get("ie_names", [])
                    summary["beacon_privacy"] = frame.get("privacy")
                    dbm = frame["radiotap"].get("dbm_antsignal")
                    if dbm is not None:
                        summary["beacon_signal_dbm"].append(dbm)
                    if last_beacon:
                        summary["beacon_intervals_ms"].append(round((ts - last_beacon) * 1000.0, 2))
                    last_beacon = ts
                else:
                    summary["other_ssids_count"] += 1
            elif frame["type"] == 0 and frame.get("mgmt") == "auth":
                summary["auth_frames"] += 1
                if summary["ap_bssid"] and frame["addr1"] == summary["ap_bssid"]:
                    summary["auth_to_ap"] += 1
                if summary["ap_bssid"] and frame["addr2"] == summary["ap_bssid"]:
                    summary["auth_from_ap"] += 1
            elif frame["type"] == 0 and frame.get("mgmt") in ("assoc-req", "reassoc-req"):
                summary["assoc_requests"] += 1
            elif frame["type"] == 0 and frame.get("mgmt") == "deauth":
                summary["deauth_frames"] += 1
            if summary["beacon_bssid"]:
                summary["ap_bssid"] = summary["beacon_bssid"]
    # second pass is unnecessary: auth frames were counted relative to the learned AP BSSID
    finalize(summary)
    return summary


def capture(iface: str, ssid: str, duration: float, pcap_path: str,
            verbose: bool) -> dict:
    summary = empty_summary(ssid)
    sock = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(0x0003))
    sock.bind((iface, 0))
    sock.setblocking(False)
    writer = PcapWriter(pcap_path)
    last_beacon = 0.0
    deadline = time.time() + duration
    try:
        while time.time() < deadline:
            ready, _, _ = select.select([sock], [], [], min(0.5, max(0.0, deadline - time.time())))
            if not ready:
                continue
            try:
                data = sock.recv(4096)
            except (BlockingIOError, InterruptedError):
                continue
            ts = time.time()
            writer.write(ts, data)
            summary["frames_total"] += 1
            frame = parse_frame(data)
            if not frame:
                continue
            if frame["type"] == 0 and frame["subtype"] == 8:
                summary["beacons_total"] += 1
                ies = frame.get("ies") or {}
                if ies.get("ssid") == ssid:
                    summary["beacons_expected"] += 1
                    summary["beacon_bssid"] = frame["addr3"]
                    summary["ap_bssid"] = frame["addr3"]
                    summary["beacon_ies"] = ies.get("ie_names", [])
                    summary["beacon_privacy"] = frame.get("privacy")
                    dbm = frame["radiotap"].get("dbm_antsignal")
                    if dbm is not None:
                        summary["beacon_signal_dbm"].append(dbm)
                    if last_beacon:
                        summary["beacon_intervals_ms"].append(round((ts - last_beacon) * 1000.0, 2))
                    last_beacon = ts
                    if verbose:
                        print("beacon: bssid=%s ssid=%r ch=%s signal=%s dBm" % (
                            frame["addr3"], ies.get("ssid"),
                            ies.get("ds_channel"), frame["radiotap"].get("dbm_antsignal")))
                else:
                    summary["other_ssids_count"] += 1
            elif frame["type"] == 0 and frame.get("mgmt") == "auth":
                summary["auth_frames"] += 1
                if summary["ap_bssid"]:
                    if frame["addr1"] == summary["ap_bssid"]:
                        summary["auth_to_ap"] += 1
                    if frame["addr2"] == summary["ap_bssid"]:
                        summary["auth_from_ap"] += 1
                if verbose:
                    print("auth frame: addr1=%s addr2=%s (learned AP=%s)" % (
                        frame["addr1"], frame["addr2"], summary["ap_bssid"] or "not-yet"))
            elif frame["type"] == 0 and frame.get("mgmt") in ("assoc-req", "reassoc-req"):
                summary["assoc_requests"] += 1
            elif frame["type"] == 0 and frame.get("mgmt") == "deauth":
                summary["deauth_frames"] += 1
    finally:
        writer.close()
        sock.close()
    finalize(summary)
    return summary


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--iface", default="mon-onair")
    ap.add_argument("--ssid", default="cudy-ap-test")
    ap.add_argument("--duration", type=float, default=20.0)
    ap.add_argument("--pcap-out", default="")
    ap.add_argument("--json-out", default="")
    ap.add_argument("--assess-only", action="store_true")
    ap.add_argument("--pcap", default="")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)
    if args.assess_only:
        if not args.pcap:
            print("sniff: --assess-only needs --pcap", file=sys.stderr)
            return 2
        try:
            summary = assess_pcap(args.pcap, args.ssid)
        except OSError as exc:
            print("sniff: cannot read pcap: %s" % exc, file=sys.stderr)
            return 2
    else:
        if os.geteuid() != 0:
            print("sniff: needs root (raw AF_PACKET socket)", file=sys.stderr)
            return 2
        if not args.pcap_out:
            print("sniff: --pcap-out is required for a live capture", file=sys.stderr)
            return 2
        if not os.path.isdir("/sys/class/net/%s" % args.iface):
            print("sniff: no such interface: %s (run tools/iface-up.sh --monitor)" % args.iface,
                  file=sys.stderr)
            return 2
        try:
            summary = capture(args.iface, args.ssid, args.duration, args.pcap_out,
                              not args.quiet)
        except OSError as exc:
            print("sniff: socket error: %s" % exc, file=sys.stderr)
            return 2
    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as fh:
            json.dump(summary, fh, indent=2, sort_keys=True)
            fh.write("\n")
    if not args.quiet:
        print("sniff: frames=%d beacons=%d expected-beacons=%d auth=%d (to-ap=%d from-ap=%d) assoc-req=%d" % (
            summary["frames_total"], summary["beacons_total"], summary["beacons_expected"],
            summary["auth_frames"], summary["auth_to_ap"], summary["auth_from_ap"],
            summary["assoc_requests"]))
        print("sniff: %s: %s" % (summary["verdict"], summary["detail"]))
    return 0 if summary["verdict"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))