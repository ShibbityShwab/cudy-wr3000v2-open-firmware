#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""selftest.py - host-only tests for the wifi-onair-observer lane.

No radio, no root, no device, no bench-router contact:

  * shell/python syntax for every tool;
  * fixture-driven unit tests for the scan parser, the association classifier,
    the DHCP packet builder/parser and the radiotap/beacon sniffer;
  * a fully mocked end-to-end dry run of tools/run-all.sh --dry-run (all host
    commands replaced by tests/mockcmd.py; the simulated netdev world lives in
    a temp dir - nothing on the host is touched);
  * static safety checks (no router access, base uplink never mutated).

Run:
    cd opensource/docs/incoming/wifi-onair-observer
    python3 tests/selftest.py
"""
from __future__ import annotations

import ast
import importlib.util
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
LANE = os.path.dirname(HERE)
FIX = os.path.join(HERE, "fixtures")
TOOLS = os.path.join(LANE, "tools")
MOCKCMD = os.path.join(HERE, "mockcmd.py")

PASSED = 0
FAILED = 0
FAILURES: list[str] = []


def check(name: str, cond: bool, detail: str = "") -> None:
    global PASSED, FAILED
    if cond:
        PASSED += 1
    else:
        FAILED += 1
        FAILURES.append("%s %s" % (name, detail))
        print("FAIL %-56s %s" % (name, detail))


def section(title: str) -> None:
    print("== %s ==" % title)


def load_module(name: str, path: str):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def read(path: str) -> str:
    with open(path, "r", encoding="utf-8") as fh:
        return fh.read()


def fixture(name: str) -> str:
    return read(os.path.join(FIX, name))


# --------------------------------------------------------------------------
def test_syntax() -> None:
    section("syntax")
    shell = sorted(f for f in os.listdir(TOOLS) if f.endswith(".sh"))
    check("tools directory has shell scripts", len(shell) >= 8, "%d scripts" % len(shell))
    for name in shell:
        path = os.path.join(TOOLS, name)
        rc = subprocess.run(["bash", "-n", path], capture_output=True).returncode
        check("bash -n %s" % name, rc == 0, "rc=%d" % rc)
    py = [os.path.join(TOOLS, f) for f in sorted(os.listdir(TOOLS)) if f.endswith(".py")]
    py.append(os.path.join(HERE, "mockcmd.py"))
    py.append(os.path.abspath(__file__))
    for path in py:
        try:
            ast.parse(read(path))
            check("ast.parse %s" % os.path.basename(path), True)
        except SyntaxError as exc:
            check("ast.parse %s" % os.path.basename(path), False, str(exc))


# --------------------------------------------------------------------------
def test_structure() -> None:
    section("structure and documents")
    for rel in ("README.md", "RUNBOOK.md", "RESULTS-TEMPLATE.md",
                "configs/README.md", "configs/wpa_supplicant-wpa2.conf",
                "configs/wpa_supplicant-wpa3-sae.conf",
                "tools/lib.sh", "tools/preflight.sh", "tools/iface-up.sh",
                "tools/scan-ap.sh", "tools/onair_scan.py",
                "tools/wpa2-assoc.sh", "tools/wpa3-sae.sh", "tools/wpa_assess.py",
                "tools/dhcp-test.sh", "tools/dhcp_probe.py",
                "tools/capture-beacon.sh", "tools/sniff.py",
                "tools/cleanup.sh", "tools/run-all.sh",
                "tests/selftest.py", "tests/mockcmd.py", ".gitignore"):
        check("exists %s" % rel, os.path.exists(os.path.join(LANE, rel)))

    runbook = read(os.path.join(LANE, "RUNBOOK.md"))
    for marker in ("PASS/FAIL", "Failure diagnosis", "cleanup", "WPA3",
                   "EXPECTED-FAIL", "Stop rules", "evidence"):
        check("RUNBOOK covers %r" % marker, marker.lower() in runbook.lower(), "missing")
    readme = read(os.path.join(LANE, "README.md"))
    check("README states NOT RUN LIVE", "NOT RUN LIVE" in readme.upper())
    check("README states no bench-router contact",
          "no bench-router contact" in readme.lower() or "never contacts the router" in readme.lower())
    template = read(os.path.join(LANE, "RESULTS-TEMPLATE.md"))
    for marker in ("verdict", "evidence", "scan", "wpa2", "cleanup"):
        check("results template mentions %r" % marker, marker.lower() in template.lower())
    gitignore = read(os.path.join(LANE, ".gitignore"))
    check(".gitignore covers local-evidence", "local-evidence" in gitignore)
    check(".gitignore covers pcap", "pcap" in gitignore)


# --------------------------------------------------------------------------
def parse_wpa_conf(path: str) -> dict:
    out: dict = {}
    for line in read(path).splitlines():
        line = line.strip()
        if not line or line.startswith("#") or line.startswith("network"):
            continue
        if line in ("{", "}"):
            continue
        if "=" in line:
            key, _, val = line.partition("=")
            out.setdefault(key.strip(), val.strip())
    return out


def test_configs() -> None:
    section("supplicant fixtures")
    wpa2 = parse_wpa_conf(os.path.join(LANE, "configs/wpa_supplicant-wpa2.conf"))
    wpa3 = parse_wpa_conf(os.path.join(LANE, "configs/wpa_supplicant-wpa3-sae.conf"))
    check("wpa2 ssid", wpa2.get("ssid") == '"cudy-ap-test"', wpa2.get("ssid"))
    check("wpa2 psk is the published lab value", wpa2.get("psk") == '"test-passphrase-121"', wpa2.get("psk"))
    check("wpa2 key_mgmt WPA-PSK", wpa2.get("key_mgmt") == "WPA-PSK", wpa2.get("key_mgmt"))
    check("wpa2 ieee80211w off", wpa2.get("ieee80211w") == "0", wpa2.get("ieee80211w"))
    check("wpa2 has no SAE", "SAE" not in wpa2.get("key_mgmt", ""))
    check("wpa2 ctrl_interface dedicated", wpa2.get("ctrl_interface") == "/run/wpa_supplicant-onair",
          wpa2.get("ctrl_interface"))
    check("wpa3 key_mgmt SAE", wpa3.get("key_mgmt") == "SAE", wpa3.get("key_mgmt"))
    check("wpa3 PMF required", wpa3.get("ieee80211w") == "2", wpa3.get("ieee80211w"))
    check("wpa3 sae_pwe", wpa3.get("sae_pwe") == "2", wpa3.get("sae_pwe"))
    check("wpa3 ctrl_interface dedicated", wpa3.get("ctrl_interface") == "/run/wpa_supplicant-onair",
          wpa3.get("ctrl_interface"))


# --------------------------------------------------------------------------
def test_scan_parser() -> None:
    section("iw scan parser")
    scan = load_module("onair_scan", os.path.join(TOOLS, "onair_scan.py"))
    wpa2 = scan.evaluate(scan.parse_scan(fixture("iw-scan-wpa2.txt")),
                         "cudy-ap-test", 2437, 6, True, False, True)
    check("wpa2 scan PASS", wpa2["verdict"] == "PASS", wpa2["detail"])
    target = wpa2["target"] or {}
    check("target bssid", target.get("bssid") == "02:00:00:00:00:01", target.get("bssid"))
    check("target freq", target.get("freq") == 2437)
    check("target DS channel", target.get("channel") == 6)
    check("target signal", target.get("signal_dbm") == -42.0)
    check("target RSN akm PSK", "PSK" in " ".join(target.get("akm", [])), target.get("akm"))
    check("target privacy", target.get("privacy") is True)
    check("neighbour blocks not in target", wpa2["bss_count_other"] == 2)

    no_ssid = scan.evaluate(scan.parse_scan(fixture("iw-scan-no-ssid.txt")),
                            "cudy-ap-test", 2437, 6, True, False, True)
    check("absent SSID FAIL", no_ssid["verdict"] == "FAIL", no_ssid["detail"])

    wrong = scan.evaluate(scan.parse_scan(fixture("iw-scan-wrong-channel.txt")),
                          "cudy-ap-test", 2437, 6, True, False, True)
    check("wrong-channel FAIL", wrong["verdict"] == "FAIL", wrong["detail"])
    wrong_any = scan.evaluate(scan.parse_scan(fixture("iw-scan-wrong-channel.txt")),
                              "cudy-ap-test", 2437, 6, True, True, True)
    check("wrong-channel diagnosis scan finds it", wrong_any["verdict"] == "PASS", wrong_any["detail"])

    sae = scan.evaluate(scan.parse_scan(fixture("iw-scan-sae.txt")),
                        "cudy-ap-test", 2437, 6, True, False, True, "SAE")
    check("SAE beacon PASS with require-akm", sae["verdict"] == "PASS", sae["detail"])
    not_sae = scan.evaluate(scan.parse_scan(fixture("iw-scan-wpa2.txt")),
                            "cudy-ap-test", 2437, 6, True, False, True, "SAE")
    check("WPA2 beacon FAILs require-akm SAE", not_sae["verdict"] == "FAIL", not_sae["detail"])
    probe_only = fixture("iw-scan-wpa2.txt").replace(
        "Information elements from Beacon frame:",
        "Information elements from Probe Response frame:")
    relay = scan.evaluate(scan.parse_scan(probe_only),
                          "cudy-ap-test", 2437, 6, True, False, False)
    check("beacon IE source required (probe-response rejected)",
          relay["verdict"] == "FAIL", relay["detail"])
    relay_ok = scan.evaluate(scan.parse_scan(probe_only),
                             "cudy-ap-test", 2437, 6, True, False, True)
    check("probe-response accepted when explicitly allowed", relay_ok["verdict"] == "PASS")


# --------------------------------------------------------------------------
def test_wpa_assess() -> None:
    section("association classifier")
    wa = load_module("wpa_assess", os.path.join(TOOLS, "wpa_assess.py"))
    ap = "02:00:00:00:00:01"
    done = wa.assess(fixture("wpa-completed.log"), fixture("status-completed.txt"),
                     "wpa2-psk", ap)
    check("completed assoc PASS", done["verdict"] == "PASS", done["detail"])
    check("completed assoc four-way PASS", done["fourway"] == "PASS", done["fourway"])
    check("completed assoc bssid", done["bssid"] == ap, done["bssid"])
    check("completed assoc UNEXPECTED-PASS label", done["expected"] == "UNEXPECTED-PASS")
    mismatch = wa.assess(fixture("wpa-completed.log"), fixture("status-completed.txt"),
                         "wpa2-psk", "02:99:99:99:99:99")
    check("bssid mismatch not PASS", mismatch["verdict"] != "PASS", mismatch["verdict"])

    auth = wa.assess(fixture("wpa-auth-timeout.log"), fixture("status-auth-timeout.txt"),
                     "wpa2-psk", ap)
    check("auth timeout = EXPECTED-FAIL", auth["verdict"] == "EXPECTED-FAIL", auth["detail"])
    check("auth timeout stage", auth["stage"] == "AUTH-TIMEOUT", auth["stage"])
    assoc = wa.assess(fixture("wpa-assoc-timeout.log"), fixture("status-auth-timeout.txt"),
                      "wpa2-psk", ap)
    check("assoc timeout = EXPECTED-FAIL", assoc["verdict"] == "EXPECTED-FAIL", assoc["detail"])
    four = wa.assess(fixture("wpa-4way-failed.log"), fixture("status-4way-failed.txt"),
                     "wpa2-psk", ap)
    check("4-way failure = FAIL-4WAY", four["verdict"] == "FAIL-4WAY", four["detail"])
    sae = wa.assess(fixture("wpa-sae-auth-timeout.log"), fixture("status-auth-timeout.txt"),
                    "wpa3-sae", ap)
    check("SAE auth timeout = EXPECTED-FAIL", sae["verdict"] == "EXPECTED-FAIL", sae["detail"])
    check("SAE observed in log", sae["sae_seen"] is True)
    unknown = wa.assess("wlan1: something unexpected\n", "", "wpa2-psk", "")
    check("unclassifiable capture is not PASS", unknown["verdict"] != "PASS", unknown["verdict"])


# --------------------------------------------------------------------------
def test_dhcp_probe() -> None:
    section("DHCP probe")
    dh = load_module("dhcp_probe", os.path.join(TOOLS, "dhcp_probe.py"))
    mac = "02:00:00:00:01:00"
    xid = 0xDEADBEEF
    pkt = dh.build_discover(mac, xid)
    check("discover length >= 300-byte BOOTP + headers", len(pkt) >= 14 + 20 + 8 + 300, str(len(pkt)))
    check("discover ethertype IPv4", pkt[12:14] == b"\x08\x00")
    ip = pkt[14:34]
    check("discover IP proto UDP", ip[9] == 17)
    check("discover IP checksum valid", dh.checksum(ip) == 0)
    check("discover src 0.0.0.0", ip[12:16] == socket.inet_aton("0.0.0.0"))
    check("discover dst broadcast", ip[16:20] == socket.inet_aton("255.255.255.255"))
    udp = pkt[34:]
    check("discover UDP 68 -> 67", struct.unpack("!HH", udp[:4]) == (68, 67))
    bootp = udp[8:]
    check("discover BOOTP op=1", bootp[0] == 1)
    check("discover xid", struct.unpack("!I", bootp[4:8])[0] == xid)
    check("discover magic cookie", bootp[236:240] == b"\x63\x82\x53\x63")
    opts = dh.parse_dhcp_options(bootp[240:])
    check("discover DHCPDISCOVER option", opts.get(53) == b"\x01")
    req = dh.build_request(mac, xid, "192.168.10.123", "192.168.10.1")
    req_bootp = req[14 + 20 + 8:]
    req_opts = dh.parse_dhcp_options(req_bootp[240:])
    check("request option 53 = DHCPREQUEST", req_opts.get(53) == b"\x03")
    check("request requested-ip option 50", req_opts.get(50) == socket.inet_aton("192.168.10.123"))
    check("request server-id option 54", req_opts.get(54) == socket.inet_aton("192.168.10.1"))

    offer = make_dhcp_reply(dh, 2, "192.168.10.123", "192.168.10.1",
                            "255.255.255.0", "192.168.10.1", xid)
    parsed = dh.parse_packet(offer)
    check("parse OFFER", parsed is not None and parsed["msg_type"] == 2, str(parsed))
    check("parse OFFER yiaddr", parsed and parsed["yiaddr"] == "192.168.10.123")
    check("parse OFFER subnet", parsed and parsed["subnet_mask"] == "255.255.255.0")
    check("parse OFFER router", parsed and parsed["router"] == "192.168.10.1")
    check("parse OFFER server id", parsed and parsed["server_id"] == "192.168.10.1")
    ack = make_dhcp_reply(dh, 5, "192.168.10.123", "192.168.10.1",
                          "255.255.255.0", "192.168.10.1", xid)
    parsed_ack = dh.parse_packet(ack)
    check("parse ACK", parsed_ack is not None and parsed_ack["msg_type"] == 5)
    check("foreign UDP traffic ignored",
          dh.parse_packet(ack[:14] + ack[14:34] + struct.pack("!HHHH", 53, 53, 8, 0) + ack[42:]) is None)


def make_dhcp_reply(dh, msg_type, yiaddr, router, mask, server, xid):
    opts = (bytes([53, 1, msg_type]) + bytes([1, 4]) + socket.inet_aton(mask) +
            bytes([3, 4]) + socket.inet_aton(router) +
            bytes([54, 4]) + socket.inet_aton(server) +
            bytes([51, 4]) + struct.pack("!I", 3600) + b"\xff")
    chaddr = bytes.fromhex("020000000100") + b"\x00" * 10
    bootp = struct.pack("!BBBBIHH", 2, 1, 6, 0, xid, 0, 0)
    bootp += b"\x00" * 4 + socket.inet_aton(yiaddr) + b"\x00" * 8
    bootp += chaddr + b"\x00" * 64 + b"\x00" * 128 + b"\x63\x82\x53\x63" + opts
    if len(bootp) < 300:
        bootp += b"\x00" * (300 - len(bootp))
    udp = struct.pack("!HHHH", 67, 68, 8 + len(bootp), 0) + bootp
    total = 20 + len(udp)
    ip = struct.pack("!BBHHHBBH4s4s", 0x45, 0, total, 0, 0, 64, 17, 0,
                     socket.inet_aton("255.255.255.255"),
                     socket.inet_aton("255.255.255.255"))
    ip = ip[:10] + struct.pack("!H", dh.checksum(ip)) + ip[12:]
    eth = b"\xff" * 6 + bytes.fromhex("020000000100") + b"\x08\x00"
    return eth + ip + udp


# --------------------------------------------------------------------------
def ie(eid: int, body: bytes) -> bytes:
    return bytes([eid, len(body)]) + body


def rsn_ie(akms=(2,)) -> bytes:
    body = struct.pack("<H", 1) + bytes([0, 0x0F, 0xAC, 4])
    body += struct.pack("<H", 1) + bytes([0, 0x0F, 0xAC, 4])
    body += struct.pack("<H", len(akms)) + b"".join(bytes([0, 0x0F, 0xAC, a]) for a in akms)
    body += struct.pack("<H", 0x000C)
    return ie(48, body)


def rt_header(dbm: int | None = None) -> bytes:
    if dbm is None:
        return struct.pack("<BBHI", 0, 0, 8, 0)
    return struct.pack("<BBHI", 0, 0, 9, 1 << 5) + struct.pack("<b", dbm)


def dot11_hdr(subtype: int, a1: bytes, a2: bytes, a3: bytes) -> bytes:
    fc = struct.pack("<H", (subtype << 4) & 0xFFF0)
    return fc + struct.pack("<H", 0) + a1 + a2 + a3 + struct.pack("<H", 0)


AP = bytes.fromhex("020000000001")
STA = bytes.fromhex("020000000100")


def beacon_frame() -> bytes:
    body = struct.pack("<Q", 12345) + struct.pack("<HH", 100, 0x0411)
    ies = (ie(0, b"cudy-ap-test") + ie(1, bytes([0x82, 0x84, 0x8B, 0x96])) +
           ie(3, b"\x06") + rsn_ie() + ie(45, b"\xad\x01"))
    return rt_header(-42) + dot11_hdr(8, b"\xff" * 6, AP, AP) + body + ies


def auth_frame() -> bytes:
    body = struct.pack("<HHH", 0, 1, 0)
    return rt_header(-50) + dot11_hdr(11, AP, STA, AP) + body


def test_sniff() -> None:
    section("monitor sniffer")
    sn = load_module("sniff", os.path.join(TOOLS, "sniff.py"))
    frame = sn.parse_frame(beacon_frame())
    check("beacon parsed", frame is not None and frame["subtype"] == 8, str(frame and frame["subtype"]))
    check("beacon ssid", frame and frame["ies"]["ssid"] == "cudy-ap-test")
    check("beacon DS channel", frame and frame["ies"]["ds_channel"] == 6)
    check("beacon privacy", frame and frame["privacy"] is True)
    check("beacon RSN akm PSK", frame and frame["ies"]["akm"] == ["PSK"], frame and frame["ies"]["akm"])
    check("beacon HT IE", frame and frame["ies"]["ht"] is True)
    check("beacon interval", frame and frame["beacon_interval"] == 100)
    rt = sn.parse_radiotap(beacon_frame())
    check("radiotap dBm signal", rt["dbm_antsignal"] == -42, str(rt))
    auth = sn.parse_frame(auth_frame())
    check("auth frame parsed", auth and auth.get("mgmt") == "auth", str(auth))
    check("auth frame AP dest", auth and auth["addr1"] == "02:00:00:00:00:01", auth and auth["addr1"])

    with tempfile.TemporaryDirectory() as tmp:
        pcap = os.path.join(tmp, "cap.pcap")
        writer = sn.PcapWriter(pcap)
        base = 1000.0
        for i in range(3):
            writer.write(base + i * 0.1024, beacon_frame())
        writer.write(base + 0.5, auth_frame())
        writer.close()
        summary = sn.assess_pcap(pcap, "cudy-ap-test")
        check("pcap beacon verdict PASS", summary["verdict"] == "PASS", summary["detail"])
        check("pcap beacon count", summary["beacons_expected"] == 3, str(summary["beacons_expected"]))
        check("pcap median interval ~102ms",
              summary["beacon_median_interval_ms"] and 95 <= summary["beacon_median_interval_ms"] <= 110,
              str(summary["beacon_median_interval_ms"]))
        check("pcap auth to AP counted", summary["auth_to_ap"] == 1, str(summary["auth_to_ap"]))
        check("pcap no AP auth reply", summary["auth_from_ap"] == 0, str(summary["auth_from_ap"]))
        check("pcap detail notes missing AP reply", "NO AP AUTH reply" in summary["detail"], summary["detail"])
        empty = os.path.join(tmp, "empty.pcap")
        with open(empty, "wb"):
            pass
        check("empty pcap FAIL", sn.assess_pcap(empty, "cudy-ap-test")["verdict"] == "FAIL")


# --------------------------------------------------------------------------
FORBIDDEN_COMMAND_RE = re.compile(
    r"(^|[^a-z])(ssh|scp|sftp|tftp|devmem|insmod|rmmod|reboot|ttyUSB|/dev/mtd)([^a-z]|$)",
    re.IGNORECASE)


def test_safety_static() -> None:
    section("static safety")
    shell_files = [os.path.join(TOOLS, f) for f in sorted(os.listdir(TOOLS)) if f.endswith(".sh")]
    for path in shell_files:
        text = read(path)
        # comments may legitimately say "no SSH/UART/..."; only executable
        # lines count as a command-surface finding
        code = "\n".join(ln for ln in text.splitlines()
                          if not ln.strip().startswith("#"))
        m = FORBIDDEN_COMMAND_RE.search(code)
        check("no router-access command in %s" % os.path.basename(path), m is None,
              m.group(0) if m else "")
        for pat in (r'dels+"$BASE_IF"', r'interfaces+add[^\n]*"$BASE_IF"',
                    r'links+sets+"$BASE_IF"', r'addrs+(add|flush|del)[^\n]*"$BASE_IF"'):
            check("base uplink not mutated in %s (%s)" % (os.path.basename(path), pat),
                  re.search(pat, text) is None)
    check("cleanup deletes only observer iface", 'del' in read(os.path.join(TOOLS, "cleanup.sh")) and
          '"$OBS_IF" del' in read(os.path.join(TOOLS, "cleanup.sh")))
    for name in ("iface-up.sh", "scan-ap.sh", "wpa2-assoc.sh", "dhcp-test.sh",
                 "cleanup.sh", "capture-beacon.sh"):
        check("require_root in %s" % name, "require_root" in read(os.path.join(TOOLS, name)))
    check("lib refuses bench-link iface", "enp195s0f3u1" in read(os.path.join(TOOLS, "lib.sh")))
    check("cleanup preserves NM's own supplicant",
          "NetworkManager" in read(os.path.join(TOOLS, "cleanup.sh")) or
          "observer wpa_supplicant" in read(os.path.join(TOOLS, "cleanup.sh")))

    binary = []
    for root, _dirs, files in os.walk(LANE):
        for f in files:
            path = os.path.join(root, f)
            with open(path, "rb") as fh:
                data = fh.read()
            if b"\x00" in data:
                binary.append(path)
            if f.endswith((".ko", ".bin", ".o", ".fw", ".img")):
                binary.append(path)
    check("no binary/vendor artifacts in the lane", not binary, str(binary))
    all_text = "\n".join(read(os.path.join(TOOLS, f)) for f in sorted(os.listdir(TOOLS))
                          if f.endswith((".sh", ".py")) or f == "lib.sh")
    check("scripts never mention a router LAN IP", "192.168.10.1" not in all_text)


# --------------------------------------------------------------------------
def make_mockbin(tmp: str) -> str:
    bin_dir = os.path.join(tmp, "mockbin")
    os.makedirs(bin_dir)
    for name in ("iw", "ip", "nmcli", "wpa_supplicant", "wpa_cli", "id", "ping"):
        path = os.path.join(bin_dir, name)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write('#!/bin/sh\nexec python3 "%s" "$(basename "$0")" "$@"\n' % MOCKCMD)
        os.chmod(path, 0o755)
    return bin_dir


def test_dryrun() -> None:
    section("mocked end-to-end dry run")
    with tempfile.TemporaryDirectory() as tmp:
        bin_dir = make_mockbin(tmp)
        state = os.path.join(tmp, "state")
        os.makedirs(state)
        # the protected base uplink exists before the run (simulated)
        base = os.path.join(state, "net", "wlan0")
        os.makedirs(base)
        with open(os.path.join(base, "type"), "w", encoding="utf-8") as fh:
            fh.write("managed")
        with open(os.path.join(base, "mac"), "w", encoding="utf-8") as fh:
            fh.write("02:00:00:00:aa:00")
        evid = os.path.join(tmp, "evidence")
        env = os.environ.copy()
        env["PATH"] = bin_dir + ":" + env.get("PATH", "")
        env["ONAIR_MOCK_DIR"] = bin_dir
        env["ONAIR_MOCK_STATE"] = state
        env["ONAIR_MOCK_FIXTURES"] = FIX
        env.pop("ONAIR_EVIDENCE_DIR", None)
        env.pop("ONAIR_DRY_RUN", None)
        env.pop("ONAIR_EVIDENCE_LABEL", None)
        proc = subprocess.run(
            [os.path.join(TOOLS, "run-all.sh"), "--dry-run", "--label", "selftest",
             "--evidence-dir", evid],
            capture_output=True, text=True, env=env, timeout=180)
        output = proc.stdout + proc.stderr
        check("dry run exits 0", proc.returncode == 0,
              "rc=%d\n%s" % (proc.returncode, output[-1500:]))
        verdicts_path = os.path.join(evid, "verdicts.txt")
        check("verdicts recorded", os.path.exists(verdicts_path))
        verdicts = read(verdicts_path) if os.path.exists(verdicts_path) else ""
        for phase, status in (("preflight", "PASS"), ("iface-up", "PASS"), ("scan", "PASS"),
                              ("wpa2-assoc", "EXPECTED-FAIL"), ("dhcp", "SKIP"),
                              ("cleanup", "PASS")):
            check("dry run %s=%s" % (phase, status),
                  re.search(r"ONAIR-VERDICT\|%s\|%s\|" % (phase, status), verdicts) is not None,
                  "missing")
        matrix = read(os.path.join(evid, "matrix.md")) if os.path.exists(os.path.join(evid, "matrix.md")) else ""
        check("matrix written", "| phase |" in matrix)
        check("matrix scan PASS row", "| scan | **PASS** |" in matrix)
        check("matrix WPA2 expected-fail row", "| wpa2-assoc | **EXPECTED-FAIL** |" in matrix)
        check("matrix cleanup PASS row", "| cleanup | **PASS** |" in matrix)
        check("dry run never created a real wlan1", not os.path.exists("/sys/class/net/wlan1"))
        for name in ("preflight.log", "scan-ch6.txt", "scan-ch6.json", "supplicant.log",
                     "assoc-result.json", "matrix.md"):
            check("dry-run evidence %s" % name, os.path.exists(os.path.join(evid, name)))

        # --- negative control: no beacon on air -> scan FAIL must gate the
        # association attempt (recorded SKIP, not a bogus AUTH-TIMEOUT) ---
        evid2 = os.path.join(tmp, "evidence-nobeacon")
        env2 = env.copy()
        env2["MOCK_SCAN_FILE"] = "iw-scan-no-ssid.txt"
        proc2 = subprocess.run(
            [os.path.join(TOOLS, "run-all.sh"), "--dry-run", "--label", "selftest-nobeacon",
             "--evidence-dir", evid2],
            capture_output=True, text=True, env=env2, timeout=180)
        check("no-beacon dry run reports unexpected finding (rc=1)", proc2.returncode == 1,
              "rc=%d\n%s" % (proc2.returncode, (proc2.stdout + proc2.stderr)[-1200:]))
        v2 = read(os.path.join(evid2, "verdicts.txt")) if os.path.exists(os.path.join(evid2, "verdicts.txt")) else ""
        check("no-beacon scan FAIL recorded",
              re.search(r"ONAIR-VERDICT\|scan\|FAIL\|", v2) is not None, v2[-500:])
        check("no-beacon association SKIP recorded",
              re.search(r"ONAIR-VERDICT\|wpa2-assoc\|SKIP\|", v2) is not None, v2[-500:])
        check("no-beacon assoc result absent (attempt gated)",
              not os.path.exists(os.path.join(evid2, "assoc-result.json")))
        check("no-beacon cleanup still runs",
              re.search(r"ONAIR-VERDICT\|cleanup\|PASS\|", v2) is not None, v2[-500:])


# --------------------------------------------------------------------------
def run_group(title: str, fn) -> None:
    global PASSED, FAILED
    p0, f0 = PASSED, FAILED
    fn()
    print("   -> %s: %d checks passed, %d failed" % (title, PASSED - p0, FAILED - f0))


def main() -> int:
    print("wifi-onair-observer selftest (host-only, no radio, no device)")
    run_group("syntax", test_syntax)
    run_group("structure and documents", test_structure)
    run_group("supplicant fixtures", test_configs)
    run_group("iw scan parser", test_scan_parser)
    run_group("association classifier", test_wpa_assess)
    run_group("DHCP probe", test_dhcp_probe)
    run_group("monitor sniffer", test_sniff)
    run_group("static safety", test_safety_static)
    run_group("mocked end-to-end dry run", test_dryrun)
    print()
    total = PASSED + FAILED
    print("selftest: %d/%d passed, %d failed" % (PASSED, total, FAILED))
    if FAILED:
        print("FAILURES:")
        for f in FAILURES:
            print("  - " + f)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())