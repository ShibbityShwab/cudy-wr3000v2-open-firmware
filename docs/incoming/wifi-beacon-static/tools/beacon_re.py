#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""beacon_re.py - static-RE probes for the wifi-beacon-static lane (task-140).

HOST-ONLY, read-only: proves the claims in ../REPORT.md against the local
vendor artifacts.  No vendor binary is copied into the repository; the paths
are local evidence referenced by argument.

Probes (all must PASS):
  F1  firmware config-key table A rows for the whole AP-start family:
        0x0064 -> 0x449e5 (VAP add), 0x0065 -> 0x46395 (VAP start),
        0x0005 -> 0x82879 (SSID), 0x0124 -> 0x841b9 (channel config),
        0x0127 -> 0x84da5 (AP config).
  F2  channel-config worker (CA 0x841B8) body store sequence: body[2]->ctx+0x10,
        body[0]->ctx+0x11, body[1]->ctx+0x12 (the 4-byte body map).
  F3  AP-config worker (CA 0x84DA4) MAC-start call bytes (calls CA 0xD41AC,
        0x94518 and then 0x961AE: the VAP/MAC start path).
  F4  VAP-start worker (CA 0x46394) body field map: body[2]->ctx+0x11,
        body[3]->ctx+0x12, body[4..7]->ctx+0x444, body[1]->protocol arg.
  F5  firmware has an AUTONOMOUS beacon engine: the SMAC beacon TX
        suspend/resume request handler names exist (host never builds a
        beacon frame; it can only suspend/resume the firmware beacon TX).
  F6  no fixed 802.11 beacon template (frame control 0x0080 + broadcast DA)
        in the firmware image: the beacon frame is built at runtime, so no
        host "beacon template/bcn ring" can exist in this design.
  H1  vendor host module (2.5.24 or 2.4.15): wal_cfg80211_start_ap calls the
        channel helper FIRST (direct branch to the next local function), then
        wal_send_set_ssid_event, wal_cfg80211_set_auth_mode and
        wal_cfg80211_start_ap_write_msg.
  H2  the channel helper's function body calls wal_send_cfg_event and carries
        the "wal_cfg80211_do_chan_set"/"wal_cfg80211_fill_channel_param" log
        strings (the 0x0124 channel command builder).
  H3  wal_cfg80211_start_ap_write_msg calls wal_send_cfg_event (AP config
        0x0127) and then wal_start_vap (0x0065): the vendor order is AP config
        BEFORE VAP start.

Usage:
  python3 tools/beacon_re.py --firmware /tmp/hi5622-patched/FIRMWARE.PD4.bin \
                             --wifi-ko  /tmp/vmods/hi5622v100_wifi.ko
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
    print(("PASS " if ok else "FAIL ") + what + ((" [" + detail + "]") if detail else ""))
    if not ok:
        FAIL.append(what)

# ---------------------------------------------------------------- firmware
def fw_u16(d, ca, base=0x40000):
    return struct.unpack_from("<H", d, ca - base)[0]

def fw_u32(d, ca, base=0x40000):
    return struct.unpack_from("<I", d, ca - base)[0]

def fw_hex(d, ca, n, base=0x40000):
    return d[ca - base:ca - base + n].hex()

def firmware_probes(path, base=0x40000):
    d = open(path, "rb").read()
    print("firmware: %s (%d B, sha256 %s)" % (path, len(d),
          hashlib.sha256(d).hexdigest()))
    TAB = 0x10C23C
    rows = {}
    for i in range(217):
        rows[fw_u16(d, TAB + i * 8)] = fw_u32(d, TAB + i * 8 + 4)
    expect = {0x0064: 0x449e5, 0x0065: 0x46395, 0x0005: 0x82879,
              0x0124: 0x841b9, 0x0127: 0x84da5}
    for key, worker in sorted(expect.items()):
        check(rows.get(key) == worker, "F1 table-A key 0x%04x -> worker 0x%x"
              % (key, worker), "got 0x%x" % rows.get(key, 0))

    # F2: body stores in worker CA 0x841B8 (bytes at 0x8420A):
    # ldrb r3,[r5,#2]/strb r3,[r4,#0x10]; ldrb r3,[r5]/strb r3,[r4,#0x11];
    # ldrb r3,[r5,#1]/strb r3,[r4,#0x12]; strb r2,[r4,#0x13]
    check(fw_hex(d, 0x8420A, 0x10) == "ab78204623742b7863746b78e274a374",
          "F2 channel worker body map (body[2]->ctx+0x10, body[0]->ctx+0x11,"
          " body[1]->ctx+0x12)", fw_hex(d, 0x8420A, 0x0E))
    check(fw_hex(d, 0x841E6, 0x10).startswith("0df10702a97828787df0"),
          "F2b channel worker validates band/body[0] and channel/body[2]",
          fw_hex(d, 0x841E6, 0x10))

    # F3: AP-config worker CA 0x84DA4 calls the MAC/VAP-start path.
    ap = fw_hex(d, 0x84EF6, 0x1E)
    check(ap.startswith("627c002a40f0208243f0040384f88d3120464ff050f9"),
          "F3 AP-config worker CA 0x84DA4 calls CA 0xD41AC/0x94518 then "
          "0x961AE (VAP/MAC start path)", ap)

    # F4: VAP-start worker body map (CA 0x46394).
    check(fw_hex(d, 0x46394, 0x10) == "2de9f04f0446a1b00021154617a82122",
          "F4 VAP-start worker CA 0x46394 prologue/body setup")
    check(fw_hex(d, 0x463EC, 0x10) == "ab7820466374eb78a3746b68c4f84434",
          "F4b VAP-start worker body map (body[2]->ctx+0x11,"
          " body[3]->ctx+0x12, body[4..7]->ctx+0x444)")

    # F5: SMAC autonomous beacon-engine handler names.
    for name in (b"smac_msg_proc_beacon_tx_resume_req",
                 b"smac_msg_proc_beacon_tx_suspend_req",
                 b"smac_tbtt_2g_isr", b"smac_tbtt_5g_isr"):
        check(name in d, "F5 firmware owns beacon TX: '%s' present"
              % name.decode())

    # F6: no fixed beacon template (FC 0x0080 + DA broadcast).
    tmpl = bytes.fromhex("80000000ffffffffffff")
    check(tmpl not in d,
          "F6 no fixed 802.11 beacon template in the firmware image "
          "(frame is built at runtime)")

# ---------------------------------------------------------------- host .ko
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
    syms = []
    for line in out.splitlines():
        m = re.match(r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+(\S)\s+(\S+)", line)
        if m:
            syms.append((int(m.group(1), 16), int(m.group(2), 16),
                         m.group(4)))
    syms.sort()
    return syms

def relocs(ko):
    out = run(["readelf", "-r", "-W", ko])
    rel = []
    for line in out.splitlines():
        m = re.match(r"([0-9a-fA-F]{8,16})\s+([0-9a-fA-F]+)\s+(\S+)\s+"
                     r"([0-9a-fA-F]+)\s+(\S+)", line)
        if m:
            rel.append((int(m.group(1), 16), m.group(3),
                        int(m.group(4), 16), m.group(5)))
    return rel

def sym_name(syms, addr):
    lo, hi = 0, len(syms)
    while lo < hi:
        mid = (lo + hi) // 2
        if syms[mid][0] <= addr:
            lo = mid + 1
        else:
            hi = mid
    if lo == 0:
        return "?"
    a, sz, name = syms[lo - 1]
    if a <= addr < a + max(sz, 1):
        return name
    return "?"

def tooldump(ko, lo, hi):
    exe = "llvm-objdump"
    out = subprocess.run([exe, "-d", "--section=.text",
                          "--start-address=0x%x" % lo,
                          "--stop-address=0x%x" % hi, ko],
                         capture_output=True, text=True)
    if out.returncode != 0 or "Disassembly" not in out.stdout:
        return ""
    return out.stdout

def cstr(ko, secs, ro_addend):
    """Resolve a .LC MOVW/MOVT addend to its string.  The compiler pools log
    strings in .rodata.str1.4 and data tables in .rodata; a local label has no
    symtab entry of its own, so try the string section first (offset must be
    inside it and the bytes must be printable)."""
    blob = open(ko, "rb").read()
    for secname in (".rodata.str1.4", ".rodata"):
        if secname not in secs:
            continue
        addr, off, size = secs[secname]
        if ro_addend >= size:
            continue
        data = blob[off + ro_addend:off + ro_addend + 240]
        txt = data.split(b"\x00")[0]
        if len(txt) >= 6 and all(32 <= c < 127 for c in txt):
            return txt.decode("latin-1", "replace")
    return ""

def host_probes(ko):
    blob = open(ko, "rb").read()
    print("vendor host module: %s (%d B, sha256 %s)" % (ko, len(blob),
          hashlib.sha256(blob).hexdigest()))
    syms = symbols(ko)
    secs = sections(ko)
    rel = relocs(ko)
    S = {name: (a, sz) for a, sz, name in syms}
    for need in ("wal_cfg80211_start_ap", "wal_cfg80211_start_ap_write_msg",
                 "wal_send_set_ssid_event", "wal_cfg80211_set_auth_mode",
                 "wal_start_vap", "wal_send_cfg_event"):
        check(need in S, "H0 symbol '%s' present" % need)
    if any(n not in S for n in ("wal_cfg80211_start_ap",
                                "wal_cfg80211_start_ap_write_msg")):
        return
    sa, sa_sz = S["wal_cfg80211_start_ap"]
    wm, wm_sz = S["wal_cfg80211_start_ap_write_msg"]
    s_calls = [(off, sym) for off, typ, val, sym in rel
               if sa <= off < sa + sa_sz and "CALL" in typ]
    s_names = [sym for _, sym in s_calls]
    # The channel helper is an intra-section direct branch: llvm-objdump
    # annotates it as the next local function after chan_dfs_check_resume.
    dump = tooldump(ko, sa, sa + sa_sz)
    chan_target = None
    chan_off = None
    for line in dump.splitlines():
        m = re.search(r"\bbl\s+(?:0x)?([0-9a-f]+)\s+<([^>]+)>", line)
        if m and "wal_cfg80211_chan_dfs_check_resume+" in m.group(2):
            chan_target = int(m.group(1), 16)
            chan_off = int(line.strip().split(":")[0], 16)
    ssid_off = [off for off, sym in s_calls
                if sym == "wal_send_set_ssid_event"]
    check(any(sym == "wal_send_set_ssid_event" for sym in s_names),
          "H1a start_ap calls wal_send_set_ssid_event")
    check(any(sym == "wal_cfg80211_set_auth_mode" for sym in s_names),
          "H1b start_ap calls wal_cfg80211_set_auth_mode")
    check(any(sym == "wal_cfg80211_start_ap_write_msg" for sym in s_names),
          "H1c start_ap calls wal_cfg80211_start_ap_write_msg")
    check(chan_target is not None,
          "H1d start_ap calls the channel helper",
          "target=0x%x" % (chan_target or 0))
    if chan_off is not None and ssid_off:
        check(chan_off < ssid_off[0],
              "H1e channel helper runs BEFORE set-SSID (vendor order)",
              "chan@0x%x < ssid@0x%x" % (chan_off, ssid_off[0]))
    # H2: the channel helper body.
    if chan_target is not None:
        nxt = [a for a, _, _ in syms if a > chan_target]
        end = nxt[0] if nxt else chan_target + 0x800
        h_calls = [sym for off, typ, val, sym in rel
                   if chan_target <= off < end and "CALL" in typ]
        check("wal_send_cfg_event" in h_calls,
              "H2a channel helper calls wal_send_cfg_event (cfg command)")
        strs = []
        for off, typ, val, sym in rel:
            if chan_target <= off < end and typ.startswith("R_ARM_MOV") and \
               sym.startswith(".LC"):
                strs.append(cstr(ko, secs, val))
        joined = " ".join(strs)
        check("wal_cfg80211_do_chan_set" in joined,
              "H2b channel helper carries the do_chan_set log string")
        check("wal_cfg80211_fill_channel_param" in joined,
              "H2c channel helper carries the fill_channel_param log string")
        check("wal_cfg80211_set_channel_info" in joined,
              "H2d channel helper carries the set_channel_info log string")
    # H3: write_msg -> AP config cfg event then VAP start.
    w_calls = [(off, sym) for off, typ, val, sym in rel
               if wm <= off < wm + wm_sz and "CALL" in typ]
    w_syms = [sym for _, sym in w_calls]
    check("wal_send_cfg_event" in w_syms,
          "H3a write_msg sends the AP-config cfg event (cmd 0x0127)")
    check("wal_start_vap" in w_syms,
          "H3b write_msg then calls wal_start_vap (VAP start AFTER ap config)")
    cfg_off = [off for off, sym in w_calls if sym == "wal_send_cfg_event"]
    vap_off = [off for off, sym in w_calls if sym == "wal_start_vap"]
    if cfg_off and vap_off:
        check(cfg_off[0] < vap_off[0],
              "H3c AP config is sent BEFORE the VAP start",
              "cfg@0x%x < vap@0x%x" % (cfg_off[0], vap_off[0]))

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--firmware", default="/tmp/hi5622-patched/FIRMWARE.PD4.bin",
                    help="local vendor firmware blob (928,920 B research copy)")
    ap.add_argument("--wifi-ko", default="/tmp/vmods/hi5622v100_wifi.ko",
                    help="local vendor hi5622v100_wifi.ko (2.5.24 or 2.4.15)")
    args = ap.parse_args()
    try:
        firmware_probes(args.firmware)
        host_probes(args.wifi_ko)
    except FileNotFoundError as e:
        print("MISSING local evidence: %s" % e)
        return 2
    print("\nbeacon_re: %d probes, %d failures" % (CHECKS[0], len(FAIL)))
    return 1 if FAIL else 0

if __name__ == "__main__":
    sys.exit(main())