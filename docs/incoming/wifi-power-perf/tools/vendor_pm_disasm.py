#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""vendor_pm_disasm.py - regenerate the PM-path evidence for REPORT.md sec 2
from a LOCAL vendor module (never committed; the project publication rule bans
vendor binaries and firmware in tracked text).

The script reads the vendor .ko from an operator-supplied path, resolves the
PM functions by symbol, annotates their disassembly with the .rodata log
strings (via the ELF relocation table, which is exact even for local .LC
labels), and prints the functions named on the command line.  Nothing vendor-
derived is written into the repository.

Usage:
  vendor_pm_disasm.py --ko /home/.../hi5622v100_plat.ko \
      wlan_pm_init wlan_pm_sleep_work wlan_pm_wakeup_work plat_pm_wakeup \
      wlan_pm_try_to_sleep wlan_power_on wlan_power_off low_power_init \
      plat_pm_vote wlan_pm_open wlan_pm_close
  vendor_pm_disasm.py --ko /home/.../hi5622v100_wifi.ko \
      hmac_config_pm hmac_config_set_sta_pm_on hmac_config_pcie_pm_level \
      hmac_config_pwr_on hmac_config_pwr_off wal_dfr_power_down_dev
"""
import argparse
import re
import subprocess
import sys

TC = ("/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/"
      "build/linux-kbuild/toolchain/bin/arm-none-linux-gnueabihf-")


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True).stdout


class Ko:
    def __init__(self, path):
        self.path = path
        self.sections = {}      # idx -> (name, vma, file_off, size)
        self.syms = {}          # idx -> (value, shndx, name, type)
        self.relocs = {}        # (section name, offset) -> symbol index
        self._bytes = {}
        self._load()

    def _load(self):
        out = run([TC + "readelf", "-SW", self.path])
        for line in out.splitlines():
            m = re.match(r"^\s*\[\s*(\d+)\]\s+(\S+)\s+(\S+)\s+"
                         r"([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)", line)
            if m:
                self.sections[int(m.group(1))] = (
                    m.group(2), int(m.group(4), 16),
                    int(m.group(5), 16), int(m.group(6), 16))
        out = run([TC + "readelf", "-sW", self.path])
        for line in out.splitlines():
            m = re.match(r"^\s*(\d+):\s+([0-9a-f]+)\s+\d+\s+(\S+)\s+"
                         r"\S+\s+\S+\s+(\d+)\s+(\S+)", line)
            if m:
                self.syms[int(m.group(1))] = (
                    int(m.group(2), 16), int(m.group(4)),
                    m.group(5), m.group(3))
        cur = None
        out = run([TC + "readelf", "-rW", self.path])
        for line in out.splitlines():
            m = re.match(r"^Relocation section '\.rel(?:a)?(\S+)'", line)
            if m:
                cur = m.group(1).lstrip(".")
                continue
            m = re.match(r"^([0-9a-f]{8,})\s+([0-9a-f]+)\s+(R_ARM_\S+)",
                         line)
            if m and cur:
                self.relocs[(cur, int(m.group(1), 16))] = \
                    int(m.group(2), 16) >> 8

    def sec_bytes(self, name):
        if name in self._bytes:
            return self._bytes[name]
        out = run([TC + "objdump", "-s", "-j", name, self.path])
        data = bytearray()
        for line in out.splitlines():
            m = re.match(r"^\s([0-9a-f]{4,})\s+"
                         r"((?:[0-9a-f]{8}\s+){1,4})", line)
            if not m:
                continue
            off = int(m.group(1), 16)
            while len(data) < off:
                data.append(0)
            for hx in m.group(2).split():
                data.extend(bytes.fromhex(hx))
        self._bytes[name] = bytes(data)
        return self._bytes[name]

    def cstr(self, secname, addr):
        data = self.sec_bytes(secname)
        if addr >= len(data):
            return None
        end = data.find(b"\x00", addr)
        raw = data[addr:end if end >= 0 else min(len(data), addr + 120)]
        if not raw:
            return ""
        try:
            return raw.decode("utf-8")
        except UnicodeDecodeError:
            return None

    def funcs(self, pattern):
        out = []
        for _, (value, shndx, name, typ) in self.syms.items():
            if typ == "FUNC" and shndx in self.sections and \
                    re.search(pattern, name):
                out.append((self.sections[shndx][0], value, name))
        return sorted(out)

    def dotted(self, secname, addr):
        """String annotation for a relocation, if any."""
        si = self.relocs.get((secname.lstrip("."), addr))
        if si is None or si not in self.syms:
            return ""
        value, shndx, name, _ = self.syms[si]
        if shndx in self.sections:
            s = self.cstr(self.sections[shndx][0], value)
            if s:
                return '   ; "%s"' % s.replace("\n", "\\n")
        return "   ; -> %s" % name if name.startswith(("wlan", "hmac_",
                                                      "plat_", "low_")) else ""

    def disasm(self):
        out = run([TC + "objdump", "-dr", "--no-show-raw-insn", self.path])
        cur = None
        rows = []
        for line in out.splitlines():
            m = re.match(r"^Disassembly of section (\S+):", line)
            if m:
                cur = m.group(1)
                continue
            m = re.match(r"^\s*([0-9a-f]+):\t(.*)", line)
            if m and cur:
                rows.append((cur, int(m.group(1), 16), m.group(2)))
        return rows

    def dump(self, pattern):
        sizes = {}
        for line in run([TC + "objdump", "-t", self.path]).splitlines():
            m = re.match(r"^([0-9a-f]{8}) l\s+\S+\s+(\S+)\s+"
                         r"([0-9a-f]{8}) (\S+)$", line)
            if m and m.group(2) != "*ABS*":
                sizes[(m.group(2), m.group(4))] = int(m.group(3), 16)
        rows = self.disasm()
        count = 0
        for secname, addr, name in self.funcs(pattern):
            size = sizes.get((secname, name), 0)
            body = [r for r in rows
                    if r[0] == secname and addr <= r[1] < addr + size]
            if not body:
                continue
            count += 1
            print("##### %-22s %-28s 0x%x size=%d"
                  % (secname, name, addr, size))
            for sec, off, text in body:
                print("  %04x: %s%s" % (off, text, self.dotted(sec, off)))
            print()
        return count


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--ko", required=True)
    ap.add_argument("functions", nargs="+")
    args = ap.parse_args(argv)
    ko = Ko(args.ko)
    printed = 0
    for pat in args.functions:
        printed += ko.dump(re.escape(pat) + "$")
    print("vendor_pm_disasm: %d function(s) printed from %s (LOCAL evidence;"
          " never commit vendor output)" % (printed, args.ko))
    return 0 if printed else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
