#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Host selftest for tools/check-he-ie.py (lane wifi-ax-he-caps, task-177).

Builds SYNTHETIC radiotap+802.11 beacon captures in memory (no device, no
network, no vendor bytes) and asserts every verdict class of the on-air HE
checker: PASS for both bands, FAIL-ABSENT, FAIL-MISMATCH, INCONCLUSIVE, the
band override and the malformed-pcap path.
"""
from __future__ import annotations

import os
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import importlib.util

spec = importlib.util.spec_from_file_location(
    "check_he_ie", os.path.join(HERE, "check-he-ie.py"))
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)

checks = 0
failures = 0


def check_that(cond, what):
    global checks, failures
    checks += 1
    if not cond:
        failures += 1
        print("FAIL: %s" % what)


def radiotap():
    # version 0, pad, length 8, present flags 0
    return struct.pack("<BBHI", 0, 0, 8, 0)


def beacon(ssid, ds, he_cap=None, he_oper=True):
    bssid = bytes.fromhex("0200006800") + bytes([1])
    hdr = struct.pack("<H", 0x0080)                    # beacon
    hdr += struct.pack("<H", 0)                        # duration
    hdr += b"\xff" * 6                                 # DA
    hdr += bssid                                       # SA
    hdr += bssid                                       # BSSID
    hdr += struct.pack("<H", 0)                        # seq
    body = struct.pack("<QHH", 0, 100, 0)              # fixed fields
    body += bytes([0, len(ssid)]) + ssid.encode()      # SSID
    body += bytes([3, 1, ds])                          # DS
    if he_cap is not None:
        body += bytes([255, len(he_cap) + 1, 35]) + he_cap
    if he_oper:
        body += bytes([255, 7, 36]) + bytes([1, 0, 0, 1, 0xff, 0xff])
    return radiotap() + hdr + body


def write_pcap(frames):
    fd, path = tempfile.mkstemp(suffix=".pcap")
    os.close(fd)
    with open(path, "wb") as fh:
        fh.write(struct.pack("<IHHiIII", 0xa1b2c3d4, 2, 4, 0, 0, 65535, 127))
        for frame in frames:
            fh.write(struct.pack("<IIII", 0, 0, len(frame), len(frame)))
            fh.write(frame)
    return path


def judge(frames, band=None, ssid="cudy-ap-test"):
    return check.judge(frames, ssid, band)


def test_contract_pass():
    for band, ds in (("2g", 6), ("5g", 36)):
        caps = check.CONTRACT[band]
        res = judge([beacon("cudy-ap-test", ds, caps)])
        check_that(res["verdict"] == "PASS", "%s contract PASS" % band)
        check_that(res["band"] == band, "%s band detected" % band)
        res2 = judge([beacon("cudy-ap-test", ds, caps)], band=band)
        check_that(res2["verdict"] == "PASS", "%s override PASS" % band)


def test_fail_absent():
    res = judge([beacon("cudy-ap-test", 6, None)])
    check_that(res["verdict"] == "FAIL", "no HE IE -> FAIL")
    check_that("no HE" in res["detail"], "absent detail names the missing IE")


def test_fail_mismatch():
    bad = bytearray(check.CONTRACT["2g"])
    bad[6] = 0x0e                                      # wrong PHY width bits
    res = judge([beacon("cudy-ap-test", 6, bytes(bad))])
    check_that(res["verdict"] == "FAIL", "wrong caps -> FAIL")
    check_that(res["observed"][0]["he_cap_hex"] is not None,
               "mismatch records the observed bytes")


def test_missing_operation():
    res = judge([beacon("cudy-ap-test", 6, check.CONTRACT["2g"],
                        he_oper=False)])
    check_that(res["verdict"] == "FAIL", "caps without HE Operation -> FAIL")


def test_inconclusive():
    res = judge([radiotap() + b"\x08\x00" + bytes(30)])
    check_that(res["verdict"] == "INCONCLUSIVE", "no beacon -> INCONCLUSIVE")
    res = judge([beacon("other-ssid", 6, check.CONTRACT["2g"])])
    check_that(res["verdict"] == "INCONCLUSIVE", "other SSID -> INCONCLUSIVE")
    check_that(res["beacons"] == 0, "other SSID not counted")


def test_band_override_mismatch():
    res = judge([beacon("cudy-ap-test", 6, check.CONTRACT["2g"])], band="5g")
    check_that(res["verdict"] == "FAIL", "2g capture judged as 5g -> FAIL")


def test_mixed_frames():
    frames = [beacon("cudy-ap-test", 6, None),
              beacon("cudy-ap-test", 6, check.CONTRACT["2g"])]
    res = judge(frames)
    check_that(res["verdict"] == "PASS", "one good beacon is enough")
    check_that(res["beacons"] == 2 and res["matched"] == 1,
               "mixed counts are exact")


def test_pcap_roundtrip_and_exit_codes():
    good = write_pcap([beacon("cudy-ap-test", 6, check.CONTRACT["2g"])])
    absent = write_pcap([beacon("cudy-ap-test", 6, None)])
    empty = write_pcap([])
    try:
        check_that(check.main(["--pcap", good]) == 0, "main PASS exit 0")
        check_that(check.main(["--pcap", absent]) == 1, "main FAIL exit 1")
        check_that(check.main(["--pcap", empty]) == 2,
                   "main INCONCLUSIVE exit 2")
    finally:
        for p in (good, absent, empty):
            os.unlink(p)


def test_bad_pcap():
    fd, path = tempfile.mkstemp(suffix=".pcap")
    os.close(fd)
    try:
        with open(path, "wb") as fh:
            fh.write(b"not a pcap at all")
        try:
            list(check.read_pcap(path))
            check_that(False, "bad pcap must raise")
        except ValueError:
            check_that(True, "bad pcap raises ValueError")
        check_that(check.main(["--pcap", path]) == 2, "bad pcap exit 2")
    finally:
        os.unlink(path)


def main() -> int:
    test_contract_pass()
    test_fail_absent()
    test_fail_mismatch()
    test_missing_operation()
    test_inconclusive()
    test_band_override_mismatch()
    test_mixed_frames()
    test_pcap_roundtrip_and_exit_codes()
    test_bad_pcap()
    print("check_he_ie_selftest: %d checks, %d failures" % (checks, failures))
    if failures:
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
