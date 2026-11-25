#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""check-he-ie.py - offline on-air HE Capabilities check for AX-1.

Reads a radiotap pcap (e.g. the observer lane's beacons.pcap), finds the
beacon(s) of the expected SSID and classifies their HE Capabilities element
against the vendor-decoded contract (../TABLES.md T3/T10):

  PASS          an expected beacon carries ext-id 35 whose MAC/PHY/MCS bytes
                are exactly the contract for the detected band
  FAIL-ABSENT   the beacon carries no HE Capabilities element
  FAIL-MISMATCH the element is present but the caps are not the contract
  INCONCLUSIVE  no expected beacon in the capture (or no pcap)

The tool is read-only and offline: no device, no root, no network.  The
observer lane captures the pcap (../wifi-onair-observer/tools/capture-beacon.sh);
this tool only judges it.

Usage:
    python3 tools/check-he-ie.py --pcap beacons.pcap --ssid cudy-ap-test
    python3 tools/check-he-ie.py --pcap beacons.pcap --ssid cudy-ap-test \
        --band 2g --json-out result.json
Exit codes: 0 PASS, 1 FAIL, 2 INCONCLUSIVE / usage error.
"""
from __future__ import annotations

import argparse
import json
import struct
import sys

PCAP_LINKTYPE_IEEE80211_RADIOTAP = 127

# The HE capabilities element content (after EID/len/ext-id) per band, from
# ../TABLES.md T10: MAC 6 + PHY 11 + HE-MCS/NSS set (4 B for 2.4 GHz because
# the 160/80+80 PHY bits are clear; 8 B for 5 GHz because 160 MHz is set).
CONTRACT = {
    "2g": bytes([
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00,               # HE MAC caps
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   # HE PHY caps
        0x00, 0x00, 0x00,                               # (11 B total)
        0xfa, 0xff, 0xfa, 0xff,                           # MCS 80 rx/tx
    ]),
    "5g": bytes([
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00,
        0xfa, 0xff, 0xfa, 0xff, 0xfa, 0xff, 0xfa, 0xff,   # MCS 80 + 160
    ]),
}

EID_EXT = 255
EXT_HE_CAP = 35
EXT_HE_OPER = 36


def parse_ies(ies: bytes):
    """Return [(eid, body)] for a well-formed IE buffer (stops on truncation)."""
    out = []
    i = 0
    while i + 2 <= len(ies):
        eid = ies[i]
        length = ies[i + 1]
        body = ies[i + 2:i + 2 + length]
        if len(body) < length:
            break
        out.append((eid, body))
        i += 2 + length
    return out


def beacon_from_frame(data: bytes):
    """Decode a radiotap+802.11 frame.  Returns a dict for beacons, else None."""
    if len(data) < 4:
        return None
    it_len = struct.unpack("<H", data[2:4])[0]
    if it_len >= len(data):
        return None
    frame = data[it_len:]
    if len(frame) < 24:
        return None
    fc = struct.unpack("<H", frame[0:2])[0]
    ftype = (fc >> 2) & 0x3
    subtype = (fc >> 4) & 0xf
    if ftype != 0 or subtype != 8:      # management beacon only
        return None
    fixed = 24
    if len(frame) < fixed + 12:
        return None
    ies = parse_ies(frame[fixed + 12:])
    ssid = None
    ds = None
    he_cap = None
    he_oper = False
    for eid, body in ies:
        if eid == 0 and body:
            ssid = body.decode("utf-8", "replace")
        elif eid == 3 and body:
            ds = body[0]
        elif eid == EID_EXT and body:
            if body[0] == EXT_HE_CAP:
                he_cap = body[1:]
            elif body[0] == EXT_HE_OPER:
                he_oper = True
    return {"bssid": frame[16:22].hex(":"), "ssid": ssid, "ds": ds,
            "he_cap": he_cap, "he_oper": he_oper}


def read_pcap(path: str):
    """Yield radiotap frames from a pcap (raises ValueError on bad format)."""
    with open(path, "rb") as fh:
        header = fh.read(24)
        if len(header) < 24:
            raise ValueError("pcap too short")
        magic = header[:4]
        if magic not in (b"\xd4\xc3\xb2\xa1", b"\xa1\xb2\xc3\xd4"):
            raise ValueError("not a little-endian pcap")
        linktype = struct.unpack("<I", header[20:24])[0]
        if linktype != PCAP_LINKTYPE_IEEE80211_RADIOTAP:
            raise ValueError("pcap linktype %d is not radiotap 802.11" % linktype)
        while True:
            ph = fh.read(16)
            if len(ph) < 16:
                return
            _sec, _usec, incl, _orig = struct.unpack("<IIII", ph)
            data = fh.read(incl)
            if len(data) < incl:
                return
            yield data


def judge(frames, ssid: str, band):
    """Classify one capture.  Returns a result dict (never raises on content)."""
    expected = CONTRACT[band] if band else None
    result = {"verdict": "INCONCLUSIVE", "detail": "", "beacons": 0,
              "matched": 0, "band": band, "ssid": ssid, "observed": []}
    for data in frames:
        b = beacon_from_frame(data)
        if not b or b["ssid"] != ssid:
            continue
        result["beacons"] += 1
        band_seen = "2g" if (b["ds"] or 0) <= 14 else "5g"
        if expected is None:
            expected = CONTRACT[band_seen]
            result["band"] = band_seen
        caps = b["he_cap"]
        entry = {"bssid": b["bssid"], "band": band_seen,
                 "he_oper": b["he_oper"],
                 "he_cap_hex": caps.hex() if caps is not None else None}
        result["observed"].append(entry)
        if caps is None:
            continue
        if caps == expected and b["he_oper"]:
            result["matched"] += 1
    if result["beacons"] == 0:
        result["verdict"] = "INCONCLUSIVE"
        result["detail"] = "no beacon for SSID %r in the capture" % ssid
    elif result["matched"]:
        result["verdict"] = "PASS"
        result["detail"] = ("%d/%d beacon(s) carry the contract HE capabilities"
                            " (ext-id 35) + HE Operation (ext-id 36)"
                            % (result["matched"], result["beacons"]))
    else:
        result["verdict"] = "FAIL"
        any_cap = [o for o in result["observed"] if o["he_cap_hex"]]
        if not any_cap:
            result["detail"] = ("expected beacon(s) present but no HE"
                                " Capabilities element (ext 0x23): firmware"
                                " beacon has no HE IE")
        else:
            result["detail"] = ("HE element present but bytes differ from the"
                                " T10 contract (see observed)")
    return result


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="offline on-air HE caps check")
    ap.add_argument("--pcap", required=True)
    ap.add_argument("--ssid", default="cudy-ap-test")
    ap.add_argument("--band", choices=("2g", "5g"), default=None,
                    help="expected band; auto-detected from the DS IE if omitted")
    ap.add_argument("--json-out", default=None)
    args = ap.parse_args(argv)
    try:
        frames = list(read_pcap(args.pcap))
    except (OSError, ValueError) as exc:
        print("INCONCLUSIVE|%s" % exc)
        return 2
    result = judge(frames, args.ssid, args.band)
    print(json.dumps(result, indent=2))
    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as fh:
            json.dump(result, fh, indent=2)
    print("HE-IE-VERDICT|%s|%s" % (result["verdict"], result["detail"]))
    return 0 if result["verdict"] == "PASS" else (1 if result["verdict"] == "FAIL" else 2)


if __name__ == "__main__":
    sys.exit(main())
