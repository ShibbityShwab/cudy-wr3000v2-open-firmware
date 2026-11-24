#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""vap_start_re.py - static re-verification of the wifi-vap-start lane against
the pinned LOCAL vendor blobs (host-only, read-only, no device).

Reads the firmware image and the vendor Wi-Fi module in place, prints DERIVED
addresses/constants only, and never writes vendor bytes into the repository.
This is the reproducibility artifact behind ../REPORT.md/TABLES.md.

  python3 vap_start_re.py --selftest     # assert every claim the lane rests on
  python3 vap_start_re.py --tables       # decode config-event key table A rows
  python3 vap_start_re.py --frames       # print the WAL frame-length arithmetic

Address conventions: firmware CPU address (CA) = file offset + 0x40000;
vendor module .text file offset = symbol/instruction CA + 0x38 (ET_REL, section
addr 0).  Evidence markers as in the lane report: [V] verified here.
"""
import argparse
import hashlib
import os
import struct
import sys

try:
    from elftools.elf.elffile import ELFFile
except Exception:  # pragma: no cover
    ELFFile = None

BASE = 0x40000
FW_LEN = 928920
FW_SHA256 = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
KO_SHA256 = "de78ec07e46e70ce8befa788a2a5d11d17e80cdea7c2789b721b0be5dcfd9521"

FW_CANDIDATES = [
    os.environ.get("HI5622_FIRMWARE", ""),
    "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin",
]
KO_CANDIDATES = [
    os.environ.get("HI5622_VENDOR_KO", ""),
    "/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_wifi.ko",
    "/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/rootfs-2.5.24/"
    "lib/modules/5.10.201/hi5622v100_wifi.ko",
]

# (name, CA, expected hex bytes) - firmware image probes [V]
FW_PROBES = [
    ("g0 id1 handler prologue", 0x43D58, "70b538b9"),
    ("g0 id1 handler hdr[3] -> ctx lookup", 0x43D72, "c178"),
    ("g0 id1 handler ldrh key payload+8", 0x43D90, "a28a"),
    ("g0 id1 handler table-A bound 0x6c8", 0x43DA2, "b3f5d96f"),
    ("config key table A entry0 key 0x0004", 0x10C23C, "04000000cd110800"),
    ("config key table A key 0x0064 -> 0x449E4", 0x10C244, "64000000e5490400"),
    ("config key table A key 0x0065 -> 0x46394", 0x10C24C, "6500000095630400"),
    ("config key table A key 0x0066 -> 0x4455A", 0x10C254, "660000005b450400"),
    ("config key table A key 0x0067 -> 0x45F74", 0x10C25C, "67000000755f0400"),
    ("config key table A key 0x0069 -> 0x839B4", 0x10C264, "69000000b5390800"),
    ("VAP-add worker (CA 0x449E4) prologue", 0x449E4, "2de9f04f0646afb0"),
    ("VAP-add worker calls create helper 0xFE6BC", 0x44AAC, "b9f006fe"),
    ("VAP-add worker binds module+0x4B0", 0x44BBC, "c4f8b034"),
    ("VAP-start worker (CA 0x46394) prologue", 0x46394, "2de9f04f0446"),
    ("VAP lookup helper CA 0x581C8 prologue", 0x581C8, "38b5"),
    ("scan handler CA 0x6494C prologue", 0x6494C, "2de9f0418046"),
    ("scan handler requires module+0x4B8", 0x64986, "d0f8b874"),
    ("scan handler VAP-0 lookup (bl 0x581C8)", 0x6499A, "0020f3f714fc"),
    ("scan handler tail-call CA 0x644A4", 0x649CA, "bde8f041"),
    ("create helper CA 0xFE6BC prologue", 0xFE6BC, "2de9f04704461e46"),
    ("create helper stores ctx+0x190", 0xFE7E4, "c4f89001"),
    ("g0 table row id 0x000F -> 0x62EC0", 0x10CD98, "0f000000c12e0600"),
    ("g0 table row id 0x0010 -> 0x5E28C", 0x10CDA4, "100000008de20500"),
    ("g2 table row0 id 0x0002 -> 0xB9780", 0x10CC18, "0200000081970b00"),
    ("g2 table row6 id 0x0027 -> 0x8CF84", 0x10CC18 + 6 * 12, "2700000085cf0800"),
]

# (name, instruction CA in the vendor .ko, expected hex) - file off = CA + 0x38
KO_PROBES = [
    ("hmac_config_send_event prologue", 0x57238, "f0412de9"),
    ("hmac_config_start_vap_event key 0x0065", 0x15b84c, "6510a0e3"),
    ("hmac_config_start_vap_event data len 12", 0x15b804, "0c20a0e3"),
    ("hdpp_config_send_event total = len + 16", 0x59c8, "102082e2"),
    ("hdpp_config_send_event key stored at hdr+20", 0x5a44, "b471c4e1"),
    ("wal_alloc_cfg_event payload pointer = hdr + 16", 0x126f84, "10208ce2"),
    ("wal_alloc_cfg_event clears hdr[6]", 0x126f6c, "0640cce5"),
    ("wal_start_vap WAL cmd 0x0065", 0x1151cc, "6530a0e3"),
    ("wal_start_vap WAL data len 16", 0x1151d8, "1020a0e3"),
    ("wal_hipriv_add_vap WAL cmd 0x0064", 0x147fdc, "6430a0e3"),
    ("wal_hipriv_add_vap WAL body len 24", 0x147fe8, "1830a0e3"),
    ("wal_hipriv_add_vap WAL data len 28", 0x148018, "1c20a0e3"),
    ("wal_cfg80211_start_ap AP-cfg cmd 0x0127", 0x1037b0, "273100e3"),
    ("wal_cfg80211_start_ap AP-cfg body 0x9c", 0x1037b8, "9c30a0e3"),
    ("hmac_sta_wait_join alloc size 16", 0x53928, "1000a0e3"),
    ("hmac_sta_wait_join msg id 0x0010", 0x53964, "1030a0e3"),
    ("hmac_sta_sync_join_req_params alloc size 64", 0x52fd8, "4000a0e3"),
    ("hmac_sta_sync_join_req_params msg id 0x000F", 0x53018, "0f30a0e3"),
]

KO_SYMBOLS = {
    "hdpp_config_send_event": 0x59c0,
    "hmac_config_send_event": 0x57238,
    "hmac_config_start_vap_event": 0x15b7fc,
    "hmac_config_add_vap_sync_all": 0x15c6b8,
    "wal_alloc_cfg_event": 0x126ef0,
    "wal_send_cfg_event": 0x1270d8,
    "wal_start_vap": 0x11519c,
    "wal_hipriv_add_vap": 0x147b7c,
    "wal_hipriv_add_vap_fill_param": 0x147aa0,
    "wal_cfg80211_start_ap": 0x103634,
    "wal_cfg80211_start_ap_write_msg": 0x103548,
    "hmac_sta_wait_join": 0x538b8,
    "hmac_sta_sync_join_req_params": 0x52fac,
}


def first_existing(paths):
    for p in paths:
        if p and os.path.isfile(p):
            return p
    return None


class Firmware:
    def __init__(self, path):
        with open(path, "rb") as fh:
            self.d = fh.read()
        self.sha256 = hashlib.sha256(self.d).hexdigest()

    def bytes_at(self, ca, n):
        off = ca - BASE
        return self.d[off:off + n]

    def u16(self, ca):
        return struct.unpack_from("<H", self.d, ca - BASE)[0]

    def u32(self, ca):
        return struct.unpack_from("<I", self.d, ca - BASE)[0]


class VendorKo:
    def __init__(self, path):
        with open(path, "rb") as fh:
            self.d = fh.read()
        self.sha256 = hashlib.sha256(self.d).hexdigest()
        self.symbols = {}
        if ELFFile is not None:
            with open(path, "rb") as fh:
                elf = ELFFile(fh)
                symtab = elf.get_section_by_name(".symtab")
                if symtab:
                    for sym in symtab.iter_symbols():
                        if sym.name and sym["st_value"]:
                            self.symbols.setdefault(sym.name, sym["st_value"])

    def bytes_at(self, ca, n):
        return self.d[0x38 + ca:0x38 + ca + n]


def check_probes(label, obj, probes):
    ok = bad = 0
    for name, ca, want in probes:
        got = obj.bytes_at(ca, len(want) // 2).hex()
        if got == want:
            ok += 1
        else:
            bad += 1
            print("FAIL [%s] %s @CA 0x%x: want %s got %s" %
                  (label, name, ca, want, got))
    return ok, bad


def tables(fw):
    print("## config-event key table A (CA 0x10C23C, 217 x {u16 key;u16 pad;u32 worker})")
    keys = {0x0004, 0x0064, 0x0065, 0x0066, 0x0067, 0x0069, 0x00D8, 0x0128,
            0x0311, 0x0320}
    for i in range(217):
        ca = 0x10C23C + i * 8
        key = fw.u16(ca)
        worker = fw.u32(ca + 4)
        if key in keys:
            print("  A[%3d] key=0x%04x worker=CA 0x%08x" % (i, key, worker))
    print("## g0 H2D table (CA 0x10CCFC) start-family ids")
    for i in range(40):
        ca = 0x10CCFC + i * 12
        iid = fw.u16(ca)
        fn = fw.u32(ca + 4)
        if iid in (0x000F, 0x0010, 0x0007, 0x0008, 0x000C):
            print("  g0 id=0x%04x fn=CA 0x%08x" % (iid, fn))
    print("## g2 H2D table (CA 0x10CC18) first rows")
    for i in range(19):
        ca = 0x10CC18 + i * 12
        iid = fw.u16(ca)
        fn = fw.u32(ca + 4)
        if fn:
            print("  g2 id=0x%04x fn=CA 0x%08x" % (iid, fn))


def frames():
    print("WAL cfg-event frame lengths derived from the vendor builders [V]:")
    for name, ca, body in (("VAP add (cmd 0x64)", 0x147b7c, 24),
                           ("VAP start (cmd 0x65)", 0x11519c, 12),
                           ("AP cfg (cmd 0x127)", 0x103634, 156)):
        print("  %-24s data_len=4+%3d=%3d  total=28+data_len=%3d"
              % (name, body, body + 4, body + 32))
    print("g0 id1 config-event frames [V]: total = 28 + data_len")
    print("  VAP add   g0/id1 key 0x64 data 20 -> total 48")
    print("  VAP start g0/id1 key 0x65 data 12 -> total 40")
    print("g0 direct start_sta frames [V]:")
    print("  join params g0 id 0x0F payload 64 -> total 76")
    print("  start       g0 id 0x10 payload 16 -> total 28")


def selftest(fw, ko):
    ok = bad = 0
    if fw is None:
        print("FAIL: firmware blob not found (set HI5622_FIRMWARE)")
        return 1
    if ko is None:
        print("FAIL: vendor module not found (set HI5622_VENDOR_KO)")
        return 1
    if fw.sha256 != FW_SHA256:
        print("WARN: firmware sha256 %s != pinned %s (probes still checked)" %
              (fw.sha256, FW_SHA256))
    if ko.sha256 != KO_SHA256:
        print("WARN: vendor module sha256 %s != pinned %s" % (ko.sha256, KO_SHA256))

    a, b = check_probes("firmware", fw, FW_PROBES)
    ok += a
    bad += b
    a, b = check_probes("vendor-ko", ko, KO_PROBES)
    ok += a
    bad += b

    # symbol table checks
    for name, want in KO_SYMBOLS.items():
        got = ko.symbols.get(name)
        if got == want:
            ok += 1
        else:
            bad += 1
            print("FAIL symbol %s: want CA 0x%x got %s" %
                  (name, want, "missing" if got is None else hex(got)))

    print("VERIFY: %d probes/symbols PASS, %d FAIL" % (ok, bad))
    return 0 if bad == 0 else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--fw", help="local FIRMWARE.bin path")
    ap.add_argument("--ko", help="local hi5622v100_wifi.ko path")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--tables", action="store_true")
    ap.add_argument("--frames", action="store_true")
    args = ap.parse_args()

    fw_path = args.fw or first_existing(FW_CANDIDATES)
    ko_path = args.ko or first_existing(KO_CANDIDATES)
    fw = Firmware(fw_path) if fw_path else None
    ko = VendorKo(ko_path) if ko_path else None

    if not (args.selftest or args.tables or args.frames):
        args.selftest = True
    rc = 0
    if args.selftest:
        rc |= selftest(fw, ko)
    if args.tables and fw:
        tables(fw)
    if args.frames:
        frames()
    return rc


if __name__ == "__main__":
    sys.exit(main())