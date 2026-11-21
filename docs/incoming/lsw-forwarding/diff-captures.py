#!/usr/bin/env python3
"""diff-captures.py - host-side diff of two capture-runtime-state.sh captures.

Read-only analysis. Inputs are directories containing regs.txt lines of the form
  "0xADDR 0xVALUE"   (one word per line; READ_FAIL lines are skipped)
Outputs a markdown report and an optional plan file (plain text, one line per proposed write:
  write 0xADDR VALUE ROLLBACK   # comment
used by plan-writes.sh).

Classification:
  - counter-like: known runtime-counter pages from the release-line W14/W15 work are labelled
    and excluded from write candidates by default (they are runtime state, not config);
  - new-on-stock:  nonzero on stock, zero on ours  -> candidate persistent config of the vendor;
  - diff-on-stock: nonzero on both but different   -> candidate, lower confidence;
  - ours-only:     zero on stock, nonzero on ours  -> informational (our own init state).
A MAC-shaped scan looks for the recorded bench host MAC and the box MAC in the L2 window, in
both byte orders. A tag-action decode prints the 16-byte entries at pub+0x54000 (0x124D4000)
with the fields recovered from the vendor tag_write function (field meanings [I]/[U]).
"""

import argparse
import json
import os
import re
import sys

# window bases (physical) -> short names, from luofu-switch-regs.h / LSW-SPEC
WINDOWS = [
    (0x11000000, 0x80000, "dp"),
    (0x11080000, 0x80000, "ips"),
    (0x11100000, 0x80000, "fam"),
    (0x11180000, 0x80000, "pe"),
    (0x11200000, 0x80000, "eqm"),
    (0x11280000, 0x80000, "eps"),
    (0x11380000, 0x80000, "prbs"),
    (0x12000000, 0x80000, "pfe_glb"),
    (0x12080000, 0x80000, "pa"),
    (0x12100000, 0x80000, "ifc"),
    (0x12180000, 0x80000, "l2"),
    (0x12280000, 0x80000, "napt"),
    (0x12380000, 0x80000, "mcc"),
    (0x12400000, 0x80000, "opp"),
    (0x12480000, 0x80000, "pub"),
    (0x10a70000, 0x10000, "pie"),
]

# runtime-counter pages/regions observed in W14/W15 (do not replay)
COUNTERS = [
    ("pe", 0x01000), ("pe", 0x41000),
    ("eqm", 0x02000), ("eqm", 0x06000), ("eqm", 0x42000), ("eqm", 0x46000),
    ("eps", 0x06000), ("pa", 0x01000), ("ifc", 0x01000), ("napt", 0x01000),
    ("mcc", 0x01000), ("mcc", 0x06000), ("opp", 0x01000),
    ("fam", 0x01000), ("fam", 0x41000), ("l2", 0x40000),
]
COUNTER_PAGE = 0x1000

TAGACT_ADDR = 0x12480000 + 0x54000
IFC_BANK0 = 0x12100000 + 0x50000
IFC_BANK1 = 0x12100000 + 0x60000

# recorded bench identities (2026-11-20 evidence; documentation values only)
HOST_MAC = bytes.fromhex("00e04c631758")

COUNTER_RE = re.compile(r"^0x([0-9a-fA-F]{8})\s+0x([0-9a-fA-F]{1,8})$")


def window_of(addr):
    for base, size, name in WINDOWS:
        if base <= addr < base + size:
            return name, addr - base
    return None, addr


def is_counter(addr):
    name, off = window_of(addr)
    for wname, woff in COUNTERS:
        if name == wname and woff <= off < woff + COUNTER_PAGE:
            return "known runtime-counter page (W14/W15 classification)"
    return None


def load_capture(path):
    regs = {}
    files = []
    if os.path.isdir(path):
        files = [os.path.join(path, f) for f in sorted(os.listdir(path))]
    else:
        files = [path]
    for fn in files:
        try:
            with open(fn, "r", errors="replace") as fh:
                for line in fh:
                    m = COUNTER_RE.match(line.strip())
                    if not m:
                        continue
                    regs[int(m.group(1), 16)] = int(m.group(2), 16)
        except (OSError, UnicodeError):
            continue
    return regs


def scan_mac(regs, mac, label, out):
    if not mac:
        return
    bywin = {}
    for addr, val in regs.items():
        name, off = window_of(addr)
        if name is None:
            continue
        bywin.setdefault(name, {})[off] = val
    for name, words in bywin.items():
        for endian in ("little", "big"):
            stream = bytearray()
            for off in sorted(words):
                stream += words[off].to_bytes(4, endian)
            idx = stream.find(mac)
            while idx >= 0:
                out.append((f"{label}: MAC-shaped bytes for {mac.hex(':')} in {name} window",
                            f"{name}+{idx:#x} (byte offset in {endian}-endian stream, "
                            f"entries are 4-byte words)"))
                idx = stream.find(mac, idx + 1)


def decode_tagact(regs, label, out):
    for idx in range(64):
        addr = TAGACT_ADDR + idx * 16
        words = [regs.get(addr + 4 * w) for w in range(4)]
        if any(w is None for w in words):
            continue
        if all(w == 0 for w in words):
            continue
        w0, w1, w2, w3 = words
        vid = w0 & 0xFFF
        flag40000 = bool(w0 & 0x40000)
        mode = (w1 >> 4) & 0x7
        out.append((f"{label}: tagact entry {idx}",
                    f"0x{TAGACT_ADDR + idx*16:08x}: w0=0x{w0:08x} w1=0x{w1:08x} "
                    f"w2=0x{w2:08x} w3=0x{w3:08x} [I] vid~{vid} "
                    f"0x40000={int(flag40000)} mode~{mode}"))


def main():
    ap = argparse.ArgumentParser(description="diff two LSW runtime captures")
    ap.add_argument("--ours", required=True, help="capture dir/file from our 6.18 image")
    ap.add_argument("--stock", required=True, help="capture dir/file from the stock vendor boot")
    ap.add_argument("--out", required=True, help="markdown report path")
    ap.add_argument("--plan", default=None, help="write plan file for plan-writes.sh")
    ap.add_argument("--json", default=None, help="JSON dump of the candidate list")
    ap.add_argument("--box-mac", default=None, help="box MAC (bdinfo) to scan for, AA:BB:CC:DD:EE:FF")
    ap.add_argument("--include-counter-candidates", action="store_true",
                    help="also list counter-page differences as candidates (not recommended)")
    args = ap.parse_args()

    ours = load_capture(args.ours)
    stock = load_capture(args.stock)
    if not stock:
        sys.exit(f"no register words parsed from stock capture {args.stock!r}")
    if not ours:
        sys.exit(f"no register words parsed from ours capture {args.ours!r}")

    all_addrs = sorted(set(ours) | set(stock))
    rows = []
    for addr in all_addrs:
        o = ours.get(addr)
        s = stock.get(addr)
        if s is None or o is None:
            continue
        if s == o:
            continue
        name, off = window_of(addr)
        counter = is_counter(addr)
        if counter and not args.include_counter_candidates:
            kind = "counter-like (excluded)"
        elif s != 0 and o == 0:
            kind = "new-on-stock"
        elif s != 0 and o != 0:
            kind = "diff-on-stock"
        else:
            kind = "ours-only"
        rows.append({"addr": addr, "ours": o, "stock": s, "window": name or "?",
                     "off": off, "kind": kind, "counter": counter})

    notes = []
    macs = [HOST_MAC]
    if args.box_mac:
        try:
            macs.append(bytes.fromhex(args.box_mac.replace(":", "")))
        except ValueError:
            pass
    for mac in macs:
        scan_mac(ours, mac, "ours", notes)
        scan_mac(stock, mac, "stock", notes)
    decode_tagact(stock, "stock", notes)
    decode_tagact(ours, "ours", notes)

    candidates = [r for r in rows if r["kind"] in ("new-on-stock", "diff-on-stock")]
    counter_rows = [r for r in rows if r["kind"].startswith("counter")]

    with open(args.out, "w") as fh:
        fh.write("# LSW runtime capture diff (ours vs stock)\n\n")
        fh.write(f"- ours: {args.ours}\n- stock: {args.stock}\n")
        fh.write(f"- words: ours={len(ours)} stock={len(stock)} common-diff={len(rows)}\n\n")
        fh.write("## Candidate persistent differences\n\n")
        if not candidates:
            fh.write("None. Every differing readable word is classified counter-like or "
                     "ours-only - the vendor runtime forwarding state is not in these reads.\n\n")
        else:
            fh.write("| window | offset | addr | stock | ours | class |\n")
            fh.write("| --- | --- | --- | --- | --- | --- |\n")
            for r in candidates:
                fh.write(f"| {r['window']} | {r['off']:#07x} | 0x{r['addr']:08x} | "
                         f"0x{r['stock']:08x} | 0x{r['ours']:08x} | {r['kind']} |\n")
            fh.write("\nEach new-on-stock word is a replay candidate only after the owning "
                     "vendor writer is identified; the first patch must not write a word whose "
                     "provenance is not recorded.\n\n")
        fh.write("## Counter-like differences (do not replay)\n\n")
        if counter_rows:
            fh.write("| window | offset | addr | stock | ours |\n| --- | --- | --- | --- | --- |\n")
            for r in counter_rows:
                fh.write(f"| {r['window']} | {r['off']:#07x} | 0x{r['addr']:08x} | "
                         f"0x{r['stock']:08x} | 0x{r['ours']:08x} |\n")
        else:
            fh.write("None.\n")
        fh.write("\n## MAC-shaped bytes / tag-action decode\n\n")
        if notes:
            for title, detail in notes:
                fh.write(f"- {title}: {detail}\n")
        else:
            fh.write("None found in the captured windows.\n")
        fh.write("\n## Next step\n\n")
        fh.write("1. For each candidate row, identify the vendor writer (hi_lsw_pfe/hi_lsw_dp/"
                 "hi_lsw_pub fixture disassembly) or the stock capture context; drop anything "
                 "unexplained.\n")
        fh.write("2. Feed the surviving rows to plan-writes.sh (it emits an on-box script with "
                 "rollback capture, readback verification and revert-on-mismatch).\n")
        fh.write("3. One write set per attended session; mirror stays on every port except the "
                 "experimented one.\n")

    plan_rows = []
    for r in candidates:
        if r["window"] == "pie":
            continue
        plan_rows.append(f"write 0x{r['addr']:08x} 0x{r['stock']:08x} 0x{r['ours']:08x} "
                         f"# {r['window']}+{r['off']:#07x} {r['kind']}")
    if args.plan:
        with open(args.plan, "w") as fh:
            fh.write("# candidate write plan (generated by diff-captures.py)\n")
            fh.write("# addr/value/rollback are physical words from the captures\n")
            for line in plan_rows:
                fh.write(line + "\n")
        print(f"diff-captures: wrote plan {args.plan} ({len(plan_rows)} rows)")
    if args.json:
        with open(args.json, "w") as fh:
            json.dump({"candidates": rows, "notes": notes}, fh, indent=1)
        print(f"diff-captures: wrote json {args.json}")
    print(f"diff-captures: wrote report {args.out}")
    print(f"diff-captures: candidates={len(candidates)} counter_rows={len(counter_rows)} "
          f"notes={len(notes)}")


if __name__ == "__main__":
    main()
