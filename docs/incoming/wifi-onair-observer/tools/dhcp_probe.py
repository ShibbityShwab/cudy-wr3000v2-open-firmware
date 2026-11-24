#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""dhcp_probe.py - minimal DHCP client probe for the observer interface.

HOST-ONLY, and only needed because this workstation has no dhclient/dhcpcd/
udhcpc/busybox.  Sends a real DHCPDISCOVER (then DHCPREQUEST) over the
observer interface and reports whether a DHCP server answered.  It never
configures the interface itself (tools/dhcp-test.sh does that after a lease).

On this bench the AP is hostapd started by hand; whether a DHCP server is
reachable on the VAP depends on the router-side bridge/dnsmasq setup.  "no
reply" therefore classifies as BLOCKED, not as a harness failure.

Raw AF_PACKET socket -> requires root.  Build/parse functions are importable
and unit-tested offline by tests/selftest.py.

Exit codes: 0 lease completed (OFFER+ACK), 1 no DHCP reply / incomplete,
2 HARNESS-ERROR (root/socket/interface).
"""
from __future__ import annotations

import argparse
import json
import os
import random
import select
import socket
import struct
import sys
import time

MAGIC = b"\x63\x82\x53\x63"
DHCP_SERVER_PORT = 67
DHCP_CLIENT_PORT = 68
ETH_P_IP = 0x0800
IPPROTO_UDP = 17
BROADCAST = "ff:ff:ff:ff:ff:ff"

OPT_SUBNET_MASK = 1
OPT_ROUTER = 3
OPT_DNS = 6
OPT_HOSTNAME = 12
OPT_REQUESTED_IP = 50
OPT_LEASE_TIME = 51
OPT_MSG_TYPE = 53
OPT_SERVER_ID = 54
OPT_PARAM_LIST = 55
OPT_END = 255

MSG_DISCOVER = 1
MSG_OFFER = 2
MSG_REQUEST = 3
MSG_ACK = 5
MSG_NAK = 6


def mac_to_bytes(mac: str) -> bytes:
    return bytes(int(part, 16) for part in mac.split(":"))


def checksum(data: bytes) -> int:
    if len(data) % 2:
        data += b"\x00"
    total = 0
    for i in range(0, len(data), 2):
        total += (data[i] << 8) + data[i + 1]
    while total >> 16:
        total = (total & 0xFFFF) + (total >> 16)
    return (~total) & 0xFFFF


def build_dhcp_options(msg_type: int, xid_options: bytes = b"") -> bytes:
    opts = bytearray()
    opts += bytes([OPT_MSG_TYPE, 1, msg_type])
    opts += bytes([OPT_PARAM_LIST, 7, OPT_SUBNET_MASK, OPT_ROUTER, OPT_DNS,
                   OPT_LEASE_TIME, OPT_SERVER_ID, 28, 26])
    hostname = b"onair-observer"
    opts += bytes([OPT_HOSTNAME, len(hostname)]) + hostname
    opts += xid_options
    opts += bytes([OPT_END])
    return bytes(opts)


def build_bootp(op: int, xid: int, mac: str, flags: int = 0x8000,
                yiaddr: bytes = b"\x00\x00\x00\x00", options: bytes = b"") -> bytes:
    chaddr = mac_to_bytes(mac) + b"\x00" * 10
    pkt = struct.pack("!BBBBIHH", op, 1, 6, 0, xid, 0, flags)
    pkt += b"\x00" * 4  # ciaddr
    pkt += yiaddr      # yiaddr
    pkt += b"\x00" * 4  # siaddr
    pkt += b"\x00" * 4  # giaddr
    pkt += chaddr
    pkt += b"\x00" * 64   # sname
    pkt += b"\x00" * 128  # file
    pkt += MAGIC
    pkt += options
    if len(pkt) < 300:
        pkt += b"\x00" * (300 - len(pkt))
    return pkt


def build_udp_ip_eth(src_mac: str, payload: bytes, sport: int = DHCP_CLIENT_PORT,
                     dport: int = DHCP_SERVER_PORT) -> bytes:
    udp = struct.pack("!HHHH", sport, dport, 8 + len(payload), 0) + payload
    total_len = 20 + len(udp)
    ip = struct.pack("!BBHHHBBH4s4s", 0x45, 0, total_len, 0, 0x4000, 64,
                     IPPROTO_UDP, 0, b"\x00\x00\x00\x00", b"\xff\xff\xff\xff")
    ip = ip[:10] + struct.pack("!H", checksum(ip)) + ip[12:]
    eth = mac_to_bytes(BROADCAST) + mac_to_bytes(src_mac) + struct.pack("!H", ETH_P_IP)
    return eth + ip + udp


def build_discover(mac: str, xid: int) -> bytes:
    bootp = build_bootp(1, xid, mac, options=build_dhcp_options(MSG_DISCOVER))
    return build_udp_ip_eth(mac, bootp)


def build_request(mac: str, xid: int, requested_ip: str, server_id: str) -> bytes:
    req_ip = socket.inet_aton(requested_ip)
    extra = bytes([OPT_REQUESTED_IP, 4]) + req_ip
    if server_id:
        extra += bytes([OPT_SERVER_ID, 4]) + socket.inet_aton(server_id)
    bootp = build_bootp(1, xid, mac, options=build_dhcp_options(MSG_REQUEST, extra))
    return build_udp_ip_eth(mac, bootp)


def parse_dhcp_options(data: bytes) -> dict:
    opts: dict = {}
    i = 0
    while i < len(data):
        code = data[i]
        if code == OPT_END or code == 0:
            i += 1
            if code == OPT_END:
                break
            continue
        if i + 1 >= len(data):
            break
        length = data[i + 1]
        val = data[i + 2:i + 2 + length]
        if len(val) < length:
            break
        opts.setdefault(code, val)
        i += 2 + length
    return opts


def parse_packet(data: bytes) -> dict | None:
    """Return a dict for a DHCP server reply, else None (ignores our own traffic)."""
    if len(data) < 14 + 20 + 8:
        return None
    ethertype = struct.unpack("!H", data[12:14])[0]
    if ethertype != ETH_P_IP:
        return None
    ip = data[14:]
    ihl = (ip[0] & 0x0F) * 4
    if ihl < 20 or len(ip) < ihl + 8:
        return None
    if ip[9] != IPPROTO_UDP:
        return None
    udp = ip[ihl:]
    sport, dport = struct.unpack("!HH", udp[0:4])
    if sport != DHCP_SERVER_PORT or dport != DHCP_CLIENT_PORT:
        return None
    bootp = udp[8:]
    if len(bootp) < 240 or bootp[236:240] != MAGIC:
        return None
    op, htype, hlen, hops, xid = struct.unpack("!BBBBI", bootp[0:8])
    if op != 2:
        return None
    yiaddr = socket.inet_ntoa(bootp[16:20])
    siaddr = socket.inet_ntoa(bootp[20:24])
    opts = parse_dhcp_options(bootp[240:])
    msg_type = opts.get(OPT_MSG_TYPE, b"\x00")[0]
    out = {
        "xid": xid,
        "yiaddr": yiaddr,
        "siaddr": siaddr,
        "msg_type": msg_type,
        "server_id": (socket.inet_ntoa(opts[OPT_SERVER_ID])
                      if OPT_SERVER_ID in opts and len(opts[OPT_SERVER_ID]) == 4 else ""),
        "subnet_mask": (socket.inet_ntoa(opts[OPT_SUBNET_MASK])
                        if OPT_SUBNET_MASK in opts and len(opts[OPT_SUBNET_MASK]) == 4 else ""),
        "router": (socket.inet_ntoa(opts[OPT_ROUTER][:4])
                   if OPT_ROUTER in opts and len(opts[OPT_ROUTER]) >= 4 else ""),
        "dns": ([socket.inet_ntoa(opts[OPT_DNS][i:i + 4])
                 for i in range(0, len(opts.get(OPT_DNS, b"")), 4)] if OPT_DNS in opts else []),
        "lease_time": (struct.unpack("!I", opts[OPT_LEASE_TIME])[0]
                       if OPT_LEASE_TIME in opts and len(opts[OPT_LEASE_TIME]) == 4 else 0),
    }
    return out


def iface_mac(iface: str) -> str:
    with open("/sys/class/net/%s/address" % iface, "r", encoding="utf-8") as fh:
        return fh.read().strip()


def open_socket(iface: str) -> socket.socket:
    sock = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(0x0003))
    sock.bind((iface, 0))
    sock.setblocking(False)
    return sock


def wait_for(sock: socket.socket, xid: int, want_types: set[int], timeout: float) -> dict | None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        remaining = max(0.0, deadline - time.time())
        ready, _, _ = select.select([sock], [], [], remaining)
        if not ready:
            return None
        try:
            data = sock.recv(4096)
        except (BlockingIOError, InterruptedError):
            continue
        reply = parse_packet(data)
        if reply and reply["xid"] == xid and reply["msg_type"] in want_types:
            return reply
    return None


def probe(iface: str, timeout: float, retries: int, request: bool, verbose: bool) -> dict:
    mac = iface_mac(iface)
    xid = random.getrandbits(32)
    events: list[dict] = []
    result: dict = {"iface": iface, "client_mac": mac, "xid": xid, "events": events,
                    "offer": None, "ack": None, "result": "TIMEOUT"}
    sock = open_socket(iface)
    try:
        for attempt in range(1, retries + 1):
            if verbose:
                print("dhcp: DISCOVER attempt %d (xid=0x%08x)" % (attempt, xid))
            sock.send(build_discover(mac, xid))
            events.append({"attempt": attempt, "sent": "DISCOVER"})
            offer = wait_for(sock, xid, {MSG_OFFER, MSG_NAK}, timeout / retries)
            if not offer:
                continue
            events.append({"received": "OFFER", "ip": offer["yiaddr"],
                           "server_id": offer["server_id"]})
            result["offer"] = offer
            if offer["msg_type"] == MSG_NAK:
                result["result"] = "NAK"
                return result
            if not request:
                result["result"] = "OFFER-ONLY"
                return result
            if verbose:
                print("dhcp: OFFER %s from %s; sending REQUEST" % (offer["yiaddr"], offer["server_id"]))
            for req_attempt in range(2):
                sock.send(build_request(mac, xid, offer["yiaddr"], offer["server_id"]))
                events.append({"sent": "REQUEST", "ip": offer["yiaddr"]})
                ack = wait_for(sock, xid, {MSG_ACK, MSG_NAK}, 5.0)
                if ack:
                    events.append({"received": "ACK" if ack["msg_type"] == MSG_ACK else "NAK",
                                   "ip": ack["yiaddr"]})
                    result["ack"] = ack
                    result["result"] = "ACK" if ack["msg_type"] == MSG_ACK else "NAK"
                    return result
            result["result"] = "OFFER-NO-ACK"
            return result
        return result
    finally:
        sock.close()


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--iface", required=True)
    ap.add_argument("--timeout", type=float, default=20.0)
    ap.add_argument("--retries", type=int, default=3)
    ap.add_argument("--no-request", action="store_true")
    ap.add_argument("--json-out", default="")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args(argv)
    if os.geteuid() != 0:
        print("dhcp_probe: needs root (raw AF_PACKET socket)", file=sys.stderr)
        return 2
    if not os.path.isdir("/sys/class/net/%s" % args.iface):
        print("dhcp_probe: no such interface: %s" % args.iface, file=sys.stderr)
        return 2
    try:
        result = probe(args.iface, args.timeout, max(1, args.retries),
                       not args.no_request, args.verbose)
    except OSError as exc:
        print("dhcp_probe: socket error: %s" % exc, file=sys.stderr)
        return 2
    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as fh:
            json.dump(result, fh, indent=2, sort_keys=True)
            fh.write("\n")
    print("dhcp result: %s" % result["result"])
    if result["offer"]:
        print("dhcp offer: ip=%s server=%s subnet=%s router=%s" % (
            result["offer"]["yiaddr"], result["offer"]["server_id"],
            result["offer"]["subnet_mask"], result["offer"]["router"]))
    if result["ack"]:
        print("dhcp ack: ip=%s subnet=%s router=%s lease=%ss" % (
            result["ack"]["yiaddr"], result["ack"]["subnet_mask"],
            result["ack"]["router"], result["ack"]["lease_time"]))
    return 0 if result["result"] == "ACK" else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))