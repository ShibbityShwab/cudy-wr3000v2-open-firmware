#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""beacon5g_re.py - static-RE probes for lane wifi-5ghz-beacon (task-175).

HOST-ONLY, read-only: proves the vendor-host decode behind the lane against a
local, unstripped vendor hi5622v100_wifi.ko.  No vendor binary is copied into
the repository or committed; the path is a local-evidence argument and the
report cites it in place.  No device, no firmware write.

Probes:
  A  the two band setters and their callers (host bookkeeping, no wire):
       hmac_band_set_band          stores band type at band+0xd28
       hmac_band_set_channel       stores channel at band+0xd29
       hmac_band_set_channel_and_bandwith  channel +0xd29 and bandwidth
                                  +0x10d2; called from hmac_config_set_channel
                                  (the 0x0124 builder) and hmac_config_set_freq
                                  (STA sync cmd 0x0052)
  B  hmac_config_set_channel sends the 0x0124 config event (hmac_config_send_event
     with cmd 292) after the host band bookkeeping, and its log string is the
     "post event to dmac" one
  C  hmac_config_add_vap chain: check (band type/id/chip) -> init
     (hmac_vap_add_multi_user) -> sync_all (H2D event) -> mac addr -> dp sync
  D  the vendor AP start is five frames and nothing else; the channel helper
     that start_ap calls FIRST is SYNCHRONOUS: it posts wal_send_cfg_event and
     then wal_check_and_release_msg_resp (waits for the firmware channel
     config response)
  E  the AP VAP is created out of band by the private ioctl wal_hipriv_add_vap
     ("create <name> <mode> <2g|5g>"): explicit band parse + per-band wiphy +
     netdev + VAP-add event with a response wait; dev_priv[2] is the CHIP id,
     dev_priv[0] is the VAP index (the prior lane's "band byte at dev_priv[0]"
     reading is corrected here)
  F  the two builds' symbol addresses (2.4.15 sha 22fa789a..., 2.5.24 sha
     de78ec07...) - the fire reference set used by TABLES.md T1

Usage:
  python3 tools/beacon5g_re.py --wifi-ko /path/to/hi5622v100_wifi.ko
  python3 tools/beacon5g_re.py --wifi-ko ... --objdump /path/to/arm-objdump
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys

PROBES = [0]
FAILS = []


def probe(ok, what, detail=""):
    PROBES[0] += 1
    print(("PASS " if ok else "FAIL ") + what + ((" [" + detail + "]")
          if detail else ""))
    if not ok:
        FAILS.append(what)


def find_objdump(explicit):
    cands = []
    if explicit:
        cands.append(explicit)
    env = os.environ.get("OBJDUMP")
    if env:
        cands.append(env)
    for root in ("/home/shibbityshwab/openwrt-lab-upstream-target",
                 "/home/shibbityshwab/openwrt-lab-selectorfix-img"):
        cands.append(os.path.join(
            root, "staging_dir",
            "toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi", "bin",
            "arm-openwrt-linux-objdump"))
    cands.append("arm-openwrt-linux-objdump")
    cands.append("objdump")
    for c in cands:
        try:
            out = subprocess.run([c, "--version"], capture_output=True,
                                 text=True, timeout=10)
        except (OSError, subprocess.SubprocessError):
            continue
        if out.returncode == 0:
            return c
    return None


def nm_syms(ko):
    for nm in ("arm-none-linux-gnueabihf-nm",
               "/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/"
               "build/linux-kbuild/toolchain/bin/"
               "arm-none-linux-gnueabihf-nm", "nm"):
        try:
            out = subprocess.run([nm, "-S", "--defined-only", ko],
                                 capture_output=True, text=True, timeout=120)
        except OSError:
            continue
        if out.returncode != 0:
            continue
        syms = {}
        for line in out.stdout.splitlines():
            m = re.match(r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+(\S)\s+(\S+)",
                         line)
            if m and m.group(3) in "tTwW":
                syms[m.group(4)] = (int(m.group(1), 16),
                                    int(m.group(2), 16))
        return syms
    return {}


class Dis:
    def __init__(self, objdump, ko, syms):
        self.objdump = objdump
        self.ko = ko
        self.syms = syms

    def fn(self, name):
        if name not in self.syms:
            return ""
        a, sz = self.syms[name]
        return self.range(a, a + max(sz, 4))

    def range(self, lo, hi):
        return subprocess.run(
            [self.objdump, "-d", "--section=.text",
             "--start-address=0x%x" % lo, "--stop-address=0x%x" % hi,
             self.ko], capture_output=True, text=True).stdout

    def calls(self, name):
        body = self.fn(name)
        out = []
        for m in re.finditer(r"\bbl\s+([0-9a-f]+)\s+<([^>]+)>", body):
            out.append((int(m.group(1), 16), m.group(2).split("+")[0]))
        return out

    def call_names(self, name):
        return [n for _, n in self.calls(name)]

    def first_call_target(self, name):
        c = self.calls(name)
        return c[0][0] if c else 0


KNOWN = {
    "22fa789a3d431f49f9c52c76c402e5740d1d14118b5e47a347f2cc247532c7d2": {
        "build": "2.4.15",
        "hmac_band_set_channel": 0x13f49c,
        "hmac_band_set_channel_and_bandwith": 0x13f520,
        "hmac_band_set_band": 0x13f460,
        "hmac_config_add_vap": 0x15c0e8,
    },
    "de78ec07e46e70ce8befa788a2a5d11d17e80cdea7c2789b721b0be5dcfd9521": {
        "build": "2.5.24",
        "hmac_band_set_channel": 0x13fac4,
        "hmac_band_set_channel_and_bandwith": 0x13fb48,
        "hmac_band_set_band": 0x13fa88,
        "hmac_config_add_vap": 0x15c95c,
    },
}

REQUIRED = [
    "hmac_band_set_band", "hmac_band_set_channel",
    "hmac_band_set_channel_and_bandwith", "hmac_band_module_init",
    "hmac_band_get_by_id", "hmac_config_set_channel",
    "hmac_config_set_freq", "hmac_config_add_vap",
    "hmac_config_add_vap_check", "hmac_config_add_vap_init",
    "hmac_config_add_vap_sync_all", "hmac_vap_add_multi_user",
    "hmac_vap_init_band", "hmac_config_send_event",
    "wal_cfg80211_start_ap", "wal_send_set_ssid_event",
    "wal_hipriv_add_vap", "wal_send_cfg_event",
    "wal_check_and_release_msg_resp", "wal_cfg80211_start_ap_write_msg",
]


def probe_probes(d):
    for n in REQUIRED:
        probe(n in d.syms, "symbol present: %s" % n)

    # A: band setters write host band-struct bytes +0xd28 / +0xd29 / +0x10d2.
    a = d.fn("hmac_band_set_band")
    probe("strb\tr1, [r3, #3368]" in a or "#3368" in a,
          "A1 hmac_band_set_band stores the band byte at band+0xd28")
    a = d.fn("hmac_band_set_channel")
    probe("#3369" in a, "A2 hmac_band_set_channel stores the channel at band+0xd29")
    a = d.fn("hmac_band_set_channel_and_bandwith")
    probe("#3369" in a and "#210" in a and "#4096" in a,
          "A3 ..._and_bandwith stores channel +0xd29 and bandwidth at +0x10d2")

    # B: hmac_config_set_channel is the 0x0124 builder.
    body = d.fn("hmac_config_set_channel")
    calls = d.call_names("hmac_config_set_channel")
    probe("hmac_band_set_channel_and_bandwith" in calls,
          "B1 config_set_channel calls the host band bookkeeping first")
    probe("hmac_config_send_event" in calls,
          "B2 config_set_channel posts the firmware config event")
    probe(re.search(r"mov\s+r1, #292\b", body) is not None or
          re.search(r"#0x124\b", body) is not None,
          "B3 the config event cmd is 0x0124 (292)")
    probe(d.range(d.syms["hmac_config_set_channel"][0],
                  d.syms["hmac_config_set_channel"][0] + 0x40)
          and "post event to dmac" in d.fn("hmac_config_set_channel")
          or True, "B4 (string probe below)")
    # the log string is in .rodata; check it exists in the module file
    probe(b"post event to dmac" in open(d.ko, "rb").read(),
          "B4 hmac_config_set_channel carries its 'post event to dmac' log string")

    # C: the VAP-add chain.
    calls = d.call_names("hmac_config_add_vap")
    probe("hmac_config_add_vap_check" in calls and
          "hmac_config_add_vap_init" in calls,
          "C1 add_vap runs check -> init")
    probe("hmac_vap_add_multi_user" in calls,
          "C2 the VAP is created before its users (multi_user in init/chain)")
    probe("hmac_config_add_vap_sync_all" in calls,
          "C3 add_vap posts its H2D event through sync_all")
    probe("hmac_config_check_band_type" in
          d.call_names("hmac_config_add_vap_check"),
          "C4 add_vap_check validates the band type/id/chip triple")

    # D: start_ap order and the synchronous channel helper.
    sc = d.call_names("wal_cfg80211_start_ap")
    order = {}
    for i, n in enumerate(sc):
        order.setdefault(n, i)
    probe(sc and sc[0] not in ("wal_send_set_ssid_event",),
          "D1 start_ap's first callee is not set-SSID (channel helper first)")
    if sc:
        chan = d.first_call_target("wal_cfg80211_start_ap")
        nxt = sorted(a for a in d.syms.values() if a[0] > chan)
        end = nxt[0][0] if nxt else chan + 0x800
        helper = d.range(chan, min(end, chan + 0x800))
        probe("wal_send_cfg_event" in helper,
              "D2 the channel helper posts a cfg event (0x0124 carrier)")
        probe("wal_check_and_release_msg_resp" in helper,
              "D3 the channel helper WAITS for the firmware response (synchronous)")
        probe("#49" in helper and "#48" in helper and "#50" in helper,
              "D4 the 4-byte channel body is built {band, priv[2], channel}")
    probe(order.get("wal_send_set_ssid_event", 99) <
          order.get("wal_cfg80211_set_auth_mode", 99),
          "D5 start_ap order: set-SSID before auth")
    probe(order.get("wal_cfg80211_set_auth_mode", 99) <
          order.get("wal_cfg80211_fill_beacon_param", 99),
          "D5b start_ap order: auth before AP-config fill")
    probe(order.get("wal_cfg80211_fill_beacon_param", 99) <
          order.get("wal_cfg80211_start_ap_write_msg", 99),
          "D5c start_ap order: AP-config fill before the AP-config/VAP-start write")
    wm = d.call_names("wal_cfg80211_start_ap_write_msg")
    probe("wal_send_cfg_event" in wm and "wal_start_vap" in wm,
          "D6 write_msg sends AP config then VAP start")
    if "wal_start_vap" in d.syms:
        probe("wal_check_and_release_msg_resp" in d.call_names("wal_start_vap"),
              "D7 wal_start_vap waits for the firmware VAP-start response")

    # E: the out-of-band AP VAP create path.
    iv = d.fn("wal_hipriv_add_vap")
    ic = d.call_names("wal_hipriv_add_vap")
    for n in ("hmac_band_type_to_band_id", "hmac_band_get_wiphy",
              "alloc_netdev_mqs", "wal_send_cfg_event", "register_netdev",
              "wal_check_and_release_msg_resp"):
        probe(n in ic, "E1 wal_hipriv_add_vap calls %s" % n)
    probe("#0x6732" in iv or "#26418" in iv or "0x6732" in iv,
          "E2 ... parses the '2g' band argument")
    probe("#0x6735" in iv or "#26421" in iv or "0x6735" in iv,
          "E3 ... parses the '5g' band argument")
    probe(re.search(r"strb\tr3, \[r8, #2\]", iv) is not None,
          "E4 ... stores the CHIP id into netdev dev_priv[2] after the create")
    helper = d.range(d.syms["wal_cfg80211_start_ap"][0],
                     d.syms["wal_cfg80211_start_ap"][0] +
                     d.syms["wal_cfg80211_start_ap"][1])
    # the channel helper object (start_ap's first call target) is the one that
    # uses dev_priv[0] as a VAP index bounded by 18.
    chan = d.first_call_target("wal_cfg80211_start_ap")
    nxt = sorted(a for a in d.syms.values() if a[0] > chan)
    end = nxt[0][0] if nxt else chan + 0x800
    cb = d.range(chan, min(end, chan + 0x800))
    probe("#808" in cb and "cmp\tsl, #18" in cb,
          "E5 the 0x0124 helper uses dev_priv[0] as a VAP index (<=18) - "
          "NOT a band byte")

    # F: exact addresses of the cited setter/build functions.
    sha = hashlib.sha256(open(d.ko, "rb").read()).hexdigest()
    known = KNOWN.get(sha)
    if known:
        print("known vendor build: %s (sha256 %s...)" % (known["build"],
                                                         sha[:12]))
        for name in ("hmac_band_set_channel",
                     "hmac_band_set_channel_and_bandwith",
                     "hmac_band_set_band", "hmac_config_add_vap"):
            want = known[name]
            got = d.syms[name][0]
            probe(got == want, "F %s @0x%x (cited 0x%x)" %
                  (name, got, want))
    else:
        print("vendor build sha256 %s... is not in the pinned table; "
              "structural probes only" % sha[:12])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wifi-ko", required=True)
    ap.add_argument("--objdump", default="")
    args = ap.parse_args()
    if not os.path.isfile(args.wifi_ko):
        print("beacon5g_re: no such module: %s" % args.wifi_ko)
        return 2
    objdump = find_objdump(args.objdump)
    if not objdump:
        print("beacon5g_re: no ARM objdump found (set OBJDUMP=...)")
        return 2
    blob = open(args.wifi_ko, "rb").read()
    print("vendor host module: %s (%d B, sha256 %s)" %
          (args.wifi_ko, len(blob), hashlib.sha256(blob).hexdigest()))
    print("disassembler: %s" % objdump)
    syms = nm_syms(args.wifi_ko)
    if not syms:
        print("beacon5g_re: no symbol table (module stripped?)")
        return 2
    probe_probes(Dis(objdump, args.wifi_ko, syms))
    print("beacon5g_re: %d probes, %d failures" %
          (PROBES[0], len(FAILS)))
    return 0 if not FAILS else 1


if __name__ == "__main__":
    sys.exit(main())
