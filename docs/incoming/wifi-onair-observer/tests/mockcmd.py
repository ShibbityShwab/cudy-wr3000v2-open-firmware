#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""mockcmd.py - host-command simulator for the wi-fi-onair-observer dry-run tests.

The selftest creates a temporary bin directory with one wrapper per command
name (iw, ip, nmcli, wpa_supplicant, wpa_cli, id, ping) that execs

    python3 mockcmd.py <command-name> <args...>

so tools/run-all.sh --dry-run exercises the complete orchestration, parsers and
verdict/matrix path with ZERO host mutation and ZERO radio activity.  The
simulated netdev world lives under ONAIR_MOCK_STATE/net/<iface>/.

Fixtures are read from ONAIR_MOCK_FIXTURES (tests/fixtures).  Scenario knobs:
  MOCK_SCAN_FILE       iw scan output fixture (default iw-scan-wpa2.txt)
  MOCK_WPA_STATUS      wpa_cli status fixture (default status-auth-timeout.txt)
  MOCK_WPA_LOG_FIXTURE wpa_supplicant debug log fixture (default wpa-auth-timeout.log)
"""
from __future__ import annotations

import os
import shutil
import sys

STATE = os.environ.get("ONAIR_MOCK_STATE", "/nonexistent")
FIXTURES = os.environ.get("ONAIR_MOCK_FIXTURES", "/nonexistent")
NET = os.path.join(STATE, "net")


def fixture(name: str) -> str:
    path = os.path.join(FIXTURES, name)
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8") as fh:
            return fh.read()
    return ""


def out(text: str) -> None:
    sys.stdout.write(text if text.endswith("\n") else text + "\n")


def iface_dir(name: str) -> str:
    return os.path.join(NET, name)


def create_iface(name: str, itype: str = "managed") -> None:
    os.makedirs(iface_dir(name), exist_ok=True)
    mac_path = os.path.join(iface_dir(name), "mac")
    if not os.path.exists(mac_path):
        # deterministic locally-administered mock MAC, never a real one
        idx = sum(ord(c) for c in name) % 250
        with open(mac_path, "w", encoding="utf-8") as fh:
            fh.write("02:00:00:00:%02x:%02x" % (idx, (idx * 7) % 256))
    with open(os.path.join(iface_dir(name), "type"), "w", encoding="utf-8") as fh:
        fh.write(itype)


def iface_exists(name: str) -> bool:
    return os.path.isdir(iface_dir(name))


def iface_mac(name: str) -> str:
    try:
        with open(os.path.join(iface_dir(name), "mac"), "r", encoding="utf-8") as fh:
            return fh.read().strip()
    except OSError:
        return "02:00:00:00:00:01"


def cmd_iw(args: list[str]) -> int:
    if not args:
        return 0
    if args[0] == "--version":
        out("iw version 6.17-mock")
        return 0
    if args[0] == "dev":
        if len(args) < 3:
            return 0
        iface, sub = args[1], args[2]
        if not iface_exists(iface) and sub != "info":
            out("command failed: No such device (-19)")
            return 1
        if sub == "info":
            if not iface_exists(iface):
                out("command failed: No such device (-19)")
                return 1
            with open(os.path.join(iface_dir(iface), "type"), "r", encoding="utf-8") as fh:
                itype = fh.read().strip() or "managed"
            out("Interface %s\n\tifindex 4\n\twdev 0x2\n\taddr %s\n\ttype %s\n\twiphy 0\n"
                "\ttxpower 18.00 dBm" % (iface, iface_mac(iface), itype))
            return 0
        if sub == "link":
            if iface == "wlan0":
                out("Connected to aa:bb:cc:dd:ee:ff (on wlan0)\n"
                    "\tSSID: mock-uplink\n\tfreq: 5210.0\n"
                    "\tsignal: -55 [-57, -58] dBm\n\ttx bitrate: 433.3 MBit/s")
                return 0
            out("Not connected.")
            return 0
        if sub == "scan":
            if "dump" in args:
                out(fixture(os.environ.get("MOCK_SCAN_FILE", "iw-scan-wpa2.txt")))
                return 0
            out(fixture(os.environ.get("MOCK_SCAN_FILE", "iw-scan-wpa2.txt")))
            return 0
        if sub == "del":
            if iface_exists(iface):
                shutil.rmtree(iface_dir(iface))
            return 0
        if sub == "set":
            return 0
        return 0
    if args[0] == "phy":
        if len(args) >= 2 and args[1] == "phy0" and len(args) >= 3 and args[2] == "info":
            out(fixture("iw-phy-info.txt"))
            return 0
        if len(args) >= 3 and args[2] == "interface":
            if "add" in args:
                name = args[args.index("add") + 1]
                itype = args[args.index("type") + 1] if "type" in args else "managed"
                create_iface(name, itype)
                return 0
        out(fixture("iw-phy-info.txt"))
        return 0
    return 0


def cmd_ip(args: list[str]) -> int:
    if not args:
        return 0
    if args[:2] == ["link", "set"] and len(args) >= 3:
        return 0
    if args[:2] == ["link", "del"] and len(args) >= 3:
        if iface_exists(args[2]):
            shutil.rmtree(iface_dir(args[2]))
        return 0
    if len(args) >= 4 and args[0] == "addr" and args[1] == "add":
        # remember the staged address for evidence/debug
        os.makedirs(os.path.join(STATE, "addr"), exist_ok=True)
        with open(os.path.join(STATE, "addr", args[3]), "w", encoding="utf-8") as fh:
            fh.write(args[2])
        return 0
    if args[:3] == ["-4", "addr", "show"] and "dev" in args:
        iface = args[args.index("dev") + 1]
        out("4: %s: <BROADCAST,MULTICAST,UP> mtu 1500\n\tinet 192.168.10.123/24 brd 192.168.10.255 scope global %s" % (iface, iface))
        return 0
    return 0


def cmd_nmcli(args: list[str]) -> int:
    if not args:
        return 0
    if args[0] == "--version":
        out("nmcli tool, version 1.58.1-mock")
        return 0
    if "-f" in args and "DEVICE" in args:
        out("lo:loopback\nwlan0:wifi")
        return 0
    if args[0] == "general" and "reload" in args:
        return 0
    if args[0] == "device" and len(args) >= 2 and args[1] == "set":
        return 0
    if args[0] == "device" and "show" in args:
        out("GENERAL.DEVICE:wlan0\nGENERAL.STATE:100 (connected)")
        return 0
    return 0


def cmd_wpa_supplicant(args: list[str]) -> int:
    if "-v" in args or "--version" in args:
        out("wpa_supplicant v2.12-mock")
        return 0
    log_path = pid_path = None
    i = 0
    while i < len(args):
        if args[i] == "-f" and i + 1 < len(args):
            log_path = args[i + 1]
            i += 2
            continue
        if args[i] == "-P" and i + 1 < len(args):
            pid_path = args[i + 1]
            i += 2
            continue
        i += 1
    log_fixture = os.environ.get("MOCK_WPA_LOG_FIXTURE", "wpa-auth-timeout.log")
    if log_path:
        with open(log_path, "w", encoding="utf-8") as fh:
            fh.write(fixture(log_fixture))
    if pid_path:
        with open(pid_path, "w", encoding="utf-8") as fh:
            fh.write("999999\n")
    return 0


def cmd_wpa_cli(args: list[str]) -> int:
    if "status" in args:
        out(fixture(os.environ.get("MOCK_WPA_STATUS", "status-auth-timeout.txt")))
        return 0
    out("OK")
    return 0


def cmd_id(args: list[str]) -> int:
    if "-u" in args:
        out("0")
        return 0
    out("uid=0(root) gid=0(root) groups=0(root)")
    return 0


def cmd_ping(args: list[str]) -> int:
    if "-V" in args:
        out("ping from mock")
        return 0
    if os.environ.get("MOCK_PING_OK", "0") == "1":
        out("3 packets transmitted, 3 received, 0% packet loss")
        return 0
    out("3 packets transmitted, 0 received, 100% packet loss")
    return 1


DISPATCH = {
    "iw": cmd_iw,
    "ip": cmd_ip,
    "nmcli": cmd_nmcli,
    "wpa_supplicant": cmd_wpa_supplicant,
    "wpa_cli": cmd_wpa_cli,
    "id": cmd_id,
    "ping": cmd_ping,
}


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        sys.stderr.write("mockcmd: missing command name\n")
        return 2
    name = argv[1]
    handler = DISPATCH.get(name)
    if handler is None:
        return 0
    return handler(argv[2:])


if __name__ == "__main__":
    sys.exit(main(sys.argv))
