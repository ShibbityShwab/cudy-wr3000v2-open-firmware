#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""mgmt_re.py - static-RE probes for the wifi-ap-mgmt lane (task-143).

HOST-ONLY, read-only: proves the claims in ../REPORT.md / ../TABLES.md
against the local vendor artifacts.  No vendor binary is copied into the
repository; the paths are local evidence passed as arguments.

Probes:
  H1  vendor host module symbol table has the whole cfg80211 mgmt-tx family
      (wal_cfg80211_mgmt_tx/_pre_proc/_try/_status) and wal_send_cfg_event.
  H2  the unnamed helper between wal_cfg80211_get_channel and
      wal_cfg80211_mgmt_tx_pre_proc is wal_mgmt_do_tx: exactly one
      movw rX,#0x1F2 / strh / strh TLV-build sequence in .text, preceded by
      mov r3,#12 and followed by a memcpy_s call.
  H2b the TLV-build sequence calls wal_send_cfg_event (R_ARM_CALL).
  H3  wal_send_cfg_event builds {u32 event=1; u16 pad; u16 len; u32 seq;
      payload} at hcc_msg->data+16 (the WAL cfg-event body layout).
  H4  the RX handler chain: hmac_rx_event_pre_process strips a 60-byte prefix
      (12 B HCC header + 48 B RX info), copies 48 bytes from data+12 and
      cross-checks the u16 at data+29 against total-60.
  H5  hmac_rx_mgmt_send_to_host reads the channel at msg->cb+7 and the frame
      length at msg->cb+17, then tail-calls hmac_send_mgmt_to_host.
  H6  hmac_rx_mgmt_frame_notify_func takes the RSSI from msg->cb+39 and clamps
      100+rssi into 0..100 for the frame-notify callback.
  H7  oal_cfg80211_rx_mgmt calls cfg80211_rx_mgmt_khz with freq*1000.
  H8  both shipped modules import cfg80211_rx_mgmt_khz / mgmt_tx_status.
  F1  PD4 firmware static config-key table A: has the g0-carrier AP keys
      (0x0124, 0x0127) and 0x01F0, but NO 0x01F2 row - i.e. the vendor WAL
      mgmt-tx command is not reachable through the bench-proven g0 id1
      carrier (it rides the WAL group-2 cfg-event envelope instead).
  F2  the table-A 0x01F0 neighbour worker (CA 0x49C8C) is a 6-byte-body
      dispatcher, not a frame TX worker (its first instructions are pinned).

Usage:
  python3 tools/mgmt_re.py --firmware /tmp/hi5622-patched/FIRMWARE.PD4.bin \
      --wifi-ko /tmp/vmods/hi5622v100_wifi.ko \
      --wifi-ko-new /tmp/bootkick/hi5622v100_wifi.ko
"""
import argparse
import hashlib
import re
import struct
import subprocess
import sys

FAIL = []
CHECKS = [0]


def check(ok, what, detail=""):
    CHECKS[0] += 1
    print(("PASS " if ok else "FAIL ") + what +
          ((" [" + detail + "]") if detail else ""))
    if not ok:
        FAIL.append(what)


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True).stdout


def sections(ko):
    out = run(["readelf", "-S", "-W", ko])
    secs = {}
    for line in out.splitlines():
        m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9a-fA-F]+)\s+"
                     r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)", line)
        if m:
            secs[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16),
                                int(m.group(4), 16))
    return secs


def symbols(ko):
    out = run(["nm", "-S", "--defined-only", ko])
    syms = {}
    for line in out.splitlines():
        m = re.match(r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+(\S)\s+(\S+)", line)
        if m:
            syms[m.group(4)] = (int(m.group(1), 16), int(m.group(2), 16))
    return syms


def imports(ko):
    return set(run(["nm", ko]).split())


def relocs(ko):
    out = run(["readelf", "-r", "-W", ko])
    rel = []
    for line in out.splitlines():
        m = re.match(r"([0-9a-fA-F]{8,16})\s+([0-9a-fA-F]+)\s+(\S+)\s+"
                     r"([0-9a-fA-F]+)\s+(\S+)", line)
        if m:
            rel.append((int(m.group(1), 16), m.group(3), m.group(5)))
    return rel


def blob(ko):
    return open(ko, "rb").read()


def text_word(ko, secs, data, addr):
    """Read one little-endian u32 at a .text section-relative address."""
    off = secs[".text"][1] + addr
    return struct.unpack_from("<I", data, off)[0]


def find_pattern(data, secs, pattern):
    start = secs[".text"][1]
    end = start + secs[".text"][2]
    hits = []
    i = data.find(pattern, start, end)
    while i >= 0:
        hits.append(i - start)
        i = data.find(pattern, start if i < start else i + 1, end)
    return hits


def host_probes(ko, tag):
    print("== vendor host module (%s): %s (%d B, sha256 %s)" %
          (tag, ko, len(blob(ko)),
           hashlib.sha256(blob(ko)).hexdigest()))
    secs = sections(ko)
    syms = symbols(ko)
    data = blob(ko)
    rel = relocs(ko)

    need = ["wal_cfg80211_mgmt_tx", "wal_cfg80211_mgmt_tx_pre_proc",
            "wal_cfg80211_mgmt_tx_try", "wal_cfg80211_mgmt_tx_status",
            "wal_send_cfg_event", "wal_add_cookie_to_array",
            "wal_check_cookie_timeout", "wal_cfg80211_mgmt_frame_register"]
    for n in need:
        check(n in syms, "%s H1 symbol %s present" % (tag, n))
    if "wal_cfg80211_mgmt_tx" not in syms:
        return
    sizes = {n: syms[n][1] for n in need if n in syms}
    check(sizes.get("wal_cfg80211_mgmt_tx") == 0x754,
          "%s H1 mgmt_tx function size 0x754" % tag,
          hex(sizes.get("wal_cfg80211_mgmt_tx", 0)))
    check(sizes.get("wal_cfg80211_mgmt_tx_pre_proc") == 0x1dc,
          "%s H1 mgmt_tx_pre_proc size 0x1dc" % tag)
    check(sizes.get("wal_cfg80211_mgmt_tx_try") == 0x22c,
          "%s H1 mgmt_tx_try size 0x22c" % tag)
    check(sizes.get("wal_cfg80211_mgmt_tx_status") == 0x1f8,
          "%s H1 mgmt_tx_status size 0x1f8" % tag)
    # H2: exactly one movw #0x1F2 TLV-build sequence in .text
    pat = struct.pack("<III", 0xE300C1F2, 0xE1CD31BE, 0xE1CDC1BC)
    hits = find_pattern(data, secs, pat)
    check(len(hits) == 1, "%s H2 unique WAL cmd 0x01F2 TLV build" % tag,
          "hits=%s" % [hex(h) for h in hits])
    if len(hits) == 1:
        h = hits[0]
        check(text_word(ko, secs, data, h - 0x10) == 0xE3A0300C,
              "%s H2 mov r3,#12 (descriptor len) precedes the TLV" % tag)
        check(text_word(ko, secs, data, h + 12) == 0xEBFFFFFE,
              "%s H2 TLV build is followed by a BL (memcpy_s)" % tag)
        check(text_word(ko, secs, data, h + 8) == 0xE1CDC1BC,
              "%s H2 strh stores cmd 0x1F2 into the TLV" % tag)
        calls = [(o, s) for o, t, s in rel
                 if "CALL" in t and h <= o < h + 0x40]
        check(any(s == "wal_send_cfg_event" for _, s in calls),
              "%s H2b do_tx calls wal_send_cfg_event" % tag)
    # H3: wal_send_cfg_event body layout
    base = syms["wal_send_cfg_event"][0]
    check(text_word(ko, secs, data, base + 0x9c) == 0xE59D3014 and
          text_word(ko, secs, data, base + 0xa0) == 0xE5839000 and
          text_word(ko, secs, data, base + 0xa8) == 0xE1C250B6,
          "%s H3 wal_send_cfg_event stores event/len at body+0/+6" % tag)
    check(text_word(ko, secs, data, base + 0xe4) == 0xE280000C,
          "%s H3 body starts 12 bytes after the WAL header" % tag)
    # H4: RX pre-process 60-byte prefix strip
    if "hmac_rx_event_pre_process" in syms:
        b = syms["hmac_rx_event_pre_process"][0]
        check(text_word(ko, secs, data, b + 4) == 0xE2804018 and
              text_word(ko, secs, data, b + 8) == 0xE59030C8 and
              text_word(ko, secs, data, b + 0x10) == 0xE5902118 and
              text_word(ko, secs, data, b + 0x14) == 0xE353003B and
              text_word(ko, secs, data, b + 0x18) == 0x8282103C and
              text_word(ko, secs, data, b + 0x1c) == 0x8243303C,
              "%s H4 RX strips 60-byte prefix when total > 59" % tag)
        check(text_word(ko, secs, data, b + 0x24) == 0xE282500C,
              "%s H4 RX info starts at data+12" % tag)
        check(text_word(ko, secs, data, b + 0x60) == 0xE3A03030 and
              text_word(ko, secs, data, b + 0x64) == 0xE1A02005,
              "%s H4 RX copies 48-byte info block" % tag)
    # H5: channel at msg+31 (= cb+7), frame len at msg+41 (= cb+17)
    if "hmac_rx_mgmt_send_to_host" in syms:
        b = syms["hmac_rx_mgmt_send_to_host"][0]
        check(text_word(ko, secs, data, b) == 0xE5D1301F,
              "%s H5 RX channel read from msg+31" % tag)
        check(text_word(ko, secs, data, b + 0x30) == 0xE1D122B9,
              "%s H5 RX frame length read from msg+41" % tag)
        check(any("JUMP" in t and o == b + 0x34 and
                  s == "hmac_send_mgmt_to_host" for o, t, s in rel),
              "%s H5 tail-call hmac_send_mgmt_to_host" % tag)
    # H6: RSSI at msg+63 (= cb+39) clamped into 0..100
    if "hmac_rx_mgmt_frame_notify_func" in syms:
        b = syms["hmac_rx_mgmt_frame_notify_func"][0]
        check(text_word(ko, secs, data, b + 0x38) == 0xE1D113DF and
              text_word(ko, secs, data, b + 0x3c) == 0xE2811064,
              "%s H6 RSSI read from msg+63 then +100" % tag)
        check(text_word(ko, secs, data, b + 0x4c) == 0xE59430C8 and
              text_word(ko, secs, data, b + 0x54) == 0xE5942118,
              "%s H6 callback gets msg length/data for the frame" % tag)
    # H7: oal_cfg80211_rx_mgmt -> cfg80211_rx_mgmt_khz(freq*1000)
    if "oal_cfg80211_rx_mgmt" in syms:
        b = syms["oal_cfg80211_rx_mgmt"][0]
        check(text_word(ko, secs, data, b + 8) == 0xE59001F0,
              "%s H7 wdev loaded from the netdev ieee80211_ptr slot" % tag)
        check(text_word(ko, secs, data, b + 0x18) == 0xE3A0CFFA and
              text_word(ko, secs, data, b + 0x20) == 0xE001019C,
              "%s H7 freq multiplied by 1000 (kHz)" % tag)
        check(any(o == b + 0x24 and s == "cfg80211_rx_mgmt_khz"
                  for o, t, s in rel),
              "%s H7 calls cfg80211_rx_mgmt_khz" % tag)
    # H8: imports
    imp = imports(ko)
    check("cfg80211_rx_mgmt_khz" in imp,
          "%s H8 imports cfg80211_rx_mgmt_khz" % tag)
    check("cfg80211_mgmt_tx_status" in imp,
          "%s H8 imports cfg80211_mgmt_tx_status" % tag)


def firmware_probes(path, base=0x40000):
    d = open(path, "rb").read()
    print("== firmware: %s (%d B, sha256 %s)" %
          (path, len(d), hashlib.sha256(d).hexdigest()))
    TAB = 0x10C23C
    rows = {}
    for i in range(217):
        off = TAB + i * 8 - base
        k = struct.unpack_from("<H", d, off)[0]
        w = struct.unpack_from("<I", d, off + 4)[0]
        rows[k] = w
    check(rows.get(0x0124) == 0x841B9,
          "F1 table A key 0x0124 -> channel worker 0x841B9")
    check(rows.get(0x0127) == 0x84DA5,
          "F1 table A key 0x0127 -> AP-config worker 0x84DA5")
    check(0x01F2 not in rows,
          "F1 table A has NO 0x01F2 row (WAL mgmt-tx is not a g0 config key)")
    check(rows.get(0x01F0) == 0x49C8D,
          "F1 table A neighbour 0x01F0 -> worker 0x49C8D")
    # F2: the 0x01F0 neighbour's first Thumb instructions (not a frame TX)
    raw = d[0x49C8C - base:0x49C8C - base + 10]
    check(raw.hex() == "10b513461278062a0bd9",
          "F2 table-A 0x01F0 worker is a 6-byte-body dispatcher",
          raw.hex())


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--firmware", default="/tmp/hi5622-patched/FIRMWARE.PD4.bin")
    ap.add_argument("--wifi-ko", default="/tmp/vmods/hi5622v100_wifi.ko",
                    help="vendor host module 2.4.15 (local evidence)")
    ap.add_argument("--wifi-ko-new", default="/tmp/bootkick/hi5622v100_wifi.ko",
                    help="vendor host module 2.5.24 (local evidence)")
    args = ap.parse_args()
    try:
        firmware_probes(args.firmware)
        host_probes(args.wifi_ko, "2.4.15")
        host_probes(args.wifi_ko_new, "2.5.24")
    except (FileNotFoundError, KeyError) as e:
        print("MISSING local evidence / section: %s" % e)
        return 2
    print("\nmgmt_re: %d probes, %d failures" % (CHECKS[0], len(FAIL)))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
