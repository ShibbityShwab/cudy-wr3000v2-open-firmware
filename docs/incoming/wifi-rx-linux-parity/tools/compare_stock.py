#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# tools/compare_stock.py - diff the read-only dp_rx_parity probe output against
# the committed vendor-stock register capture, for the wifi-rx-linux-parity
# lane (shared task task-166).
#
# The probe line format is fixed by hi5622_dp_rx_parity_show():
#   live 40050000: v0 v1 ... v15
#   cfg  40040000: v0 v1 ... v15        (three lines; each starts with "cfg ")
#   blk  40042000: v0 v1 ... v15        (eight lines, one per machw block)
# The stock capture format is opensource/dumps/reg_all.txt:
#   addr = 40040000, value = 100
#
# Usage:
#   tools/compare_stock.py --probe /tmp/parity.txt [--dump ../../dumps/reg_all.txt]
#                          [--all] [--fail-on-diff]
# Exit codes: 0 = parsed (diff status reported), 2 = malformed/absent input,
#             3 = --fail-on-diff and there is at least one MAC-config DIFF.
import argparse
import os
import re
import sys

PROBE_LINE = re.compile(r"^(live|cfg|blk)\s+([0-9a-fA-F]{8}):\s*(.*)$")
DUMP_LINE = re.compile(r"^addr\s*=\s*([0-9a-fA-F]+),\s*value\s*=\s*([0-9a-fA-F]+)\s*$")

# The 14-host-write MAC words (hi5622_rx_parity.h rxp_is_program_off).
PROGRAM_OFFS = [0x00, 0x04, 0x10, 0x1c, 0x20, 0x24, 0x28, 0x2c,
                0x30, 0x34, 0x38, 0x3c, 0x40, 0x48]


def parse_probe(text):
    """Return {address: value} from a probe transcript (raise ValueError)."""
    words = {}
    for line in text.splitlines():
        m = PROBE_LINE.match(line.strip())
        if not m:
            continue
        base = int(m.group(2), 16)
        vals = m.group(3).split()
        for i, v in enumerate(vals):
            try:
                words[base + 4 * i] = int(v, 16)
            except ValueError:
                raise ValueError("bad word %r at 0x%08x" % (v, base + 4 * i))
    if not words:
        raise ValueError("no probe words found")
    return words


def parse_dump(text):
    """Return {address: value} from a reg_all.txt transcript."""
    words = {}
    for line in text.splitlines():
        m = DUMP_LINE.match(line.strip())
        if m:
            words[int(m.group(1), 16)] = int(m.group(2), 16)
    return words


def classify(addr, probe_value, dump_value, program_offs=PROGRAM_OFFS):
    """Return a short status string for one address."""
    if dump_value is None:
        return "no-stock"
    if probe_value == dump_value:
        return "match"
    if addr in [0x40040000 + o for o in program_offs]:
        return "DIFF-program"
    return "DIFF-other"


def compare(probe_words, dump_words):
    """Return list of dicts: addr, probe, stock, status, role."""
    rows = []
    for addr in sorted(probe_words):
        pv = probe_words[addr]
        dv = dump_words.get(addr)
        status = classify(addr, pv, dv)
        role = "mac-cfg" if 0x40040000 <= addr < 0x400400b0 else (
            "live-bank" if 0x40050000 <= addr < 0x40050044 else "block")
        rows.append(dict(addr=addr, probe=pv, stock=dv, status=status,
                         role=role))
    return rows


def mac_cfg_gaps(probe_words, dump_words):
    """Words that are non-zero in the vendor stock capture and that the host's
    14-write program never writes - the candidate missing host configuration."""
    gaps = []
    for addr, dv in sorted(dump_words.items()):
        if not (0x40040000 <= addr < 0x400400b0) or dv == 0:
            continue
        off = addr - 0x40040000
        if off in PROGRAM_OFFS:
            continue
        gaps.append((off, dv, probe_words.get(addr)))
    return gaps


def main(argv=None):
    here = os.path.dirname(os.path.abspath(__file__))
    default_dump = os.path.normpath(os.path.join(here, "..", "..", "..",
                                                 "..", "dumps",
                                                 "reg_all.txt"))
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--probe", required=True, help="dp_rx_parity transcript")
    ap.add_argument("--dump", default=default_dump,
                    help="vendor stock register capture (default %(default)s)")
    ap.add_argument("--all", action="store_true",
                    help="print every word, not only differences")
    ap.add_argument("--fail-on-diff", action="store_true",
                    help="exit 3 when a MAC-config DIFF is present")
    args = ap.parse_args(argv)

    try:
        probe_words = parse_probe(open(args.probe, encoding="utf-8",
                                       errors="replace").read())
    except (OSError, ValueError) as exc:
        print("compare_stock: probe error: %s" % exc, file=sys.stderr)
        return 2
    dump_words = {}
    if args.dump and os.path.exists(args.dump):
        dump_words = parse_dump(open(args.dump, encoding="utf-8",
                                     errors="replace").read())
    else:
        print("compare_stock: warning: stock dump %r not found - every word "
              "is reported as no-stock" % args.dump, file=sys.stderr)

    rows = compare(probe_words, dump_words)
    diffs = [r for r in rows if r["status"] in ("DIFF-program", "DIFF-other")]
    print("addr        probe     stock     role       status")
    for r in rows:
        if not args.all and r["status"] in ("match", "no-stock") \
                and r["role"] != "mac-cfg":
            continue
        stock = "--------" if r["stock"] is None else "%08x" % r["stock"]
        print("  %08x  %08x  %s  %-9s  %s" %
              (r["addr"], r["probe"], stock, r["role"], r["status"]))
    gaps = mac_cfg_gaps(probe_words, dump_words)
    print("\nMAC config words with vendor state our 14-write program never "
          "writes: %d" % len(gaps))
    for off, dv, pv in gaps:
        cur = "absent" if pv is None else "%08x" % pv
        print("  +0x%02x stock=%08x probe=%s" % (off, dv, cur))
    print("\nsummary: %d words, %d differences "
          "(programmed=%d, other=%d), %d missing-write candidates" %
          (len(rows), len(diffs),
           sum(1 for r in diffs if r["status"] == "DIFF-program"),
           sum(1 for r in diffs if r["status"] == "DIFF-other"),
           len(gaps)))
    if args.fail_on_diff and any(r["status"] == "DIFF-program" for r in diffs):
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())
