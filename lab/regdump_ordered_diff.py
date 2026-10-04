#!/usr/bin/env python3
"""regdump_ordered_diff.py - ORDERED (sequence-aware) register-write diff.

regdump_diff.py collapses each capture into a final {addr: value} dict, so it is
blind to write ORDER: two captures that write the same addresses in a different
order compare as identical.  This tool keeps the write SEQUENCE and reports

  * the ordered-sequence diff: the FIRST DIVERGENCE INDEX with the
    expected-vs-actual (addr, value), or "identical order";
  * a per-address write-order comparison;
  * missing / extra write counts and totals.

Standalone python3 (stdlib only).  One report is written per invocation to
build/register-dumps/diffs/<UTC>/ordered-diff-<pair>.txt.

Input parsers
-------------
vendor (sequence A) - a vendor kprobe/ftrace boot capture:
  (a)  the SAME regex regdump_diff.py uses:  'addr=<hex>, value=<hex>'
  (a') the kprobe *store* form the captured traces actually carry:
       'lb_intr_w: (...) va=0xc9ab9508 val=0x3f201818'
       the addresses are kernel VAs; they are mapped to device CAs with
       VA_CA_DELTA (live-binding.md A.2: VA - CA = 0x89a80000 for this boot).

port (sequence B) - a lab/bind takeover log:
  (b)  '0x<8 hex> ... <= 0x<value>'   e.g.
       'glue chn_res 0x400392e8 [0x2e8] <= 0x00000020'
  (b') 'out[n] <= 0x<value>' labels mapped through the ctx table:
       out[0]=0x40039010 out[1]=0x40039014 out[2]=0x400392d4
       out[3]=0x40101438 out[4]=0x40101414 out[5]=0x400392f0
       (live-binding.md Part A; wifidrv1.c:104,117-119,137-139;
        061_testboot_full.txt:620-626)

exit codes: 0 ok, 1 unreadable/unrecognised input (names the file), 2 usage.
"""
import collections
import contextlib
import datetime
import io
import os
import re
import shutil
import sys
import tempfile

USAGE = """usage: regdump_ordered_diff.py <vendor.txt> <port.txt> [--outdir DIR]
       regdump_ordered_diff.py --selftest

  <vendor.txt>  vendor kprobe/ftrace boot capture   (sequence A)
  <port.txt>    lab/bind takeover log               (sequence B)

  writes build/register-dumps/diffs/<UTC>/ordered-diff-<pair>.txt
  (override the directory with --outdir DIR or $ORDERED_DIFF_OUT).
  exit 0 ok; 1 unreadable/unrecognised input (names the file); 2 usage."""

# ---------------------------------------------------------------------------
# parsers
# ---------------------------------------------------------------------------

# (a) the SAME regex regdump_diff.py uses (devmem-style 'addr=..., value=...')
V_ADDRVALUE = re.compile(r"addr\s*=\s*([0-9a-fA-F]+)\s*,\s*value\s*=\s*([0-9a-fA-F]+)")
# (a') the vendor kprobe store form actually present in the captured traces
V_KPROBE = re.compile(r"\bva=0x([0-9a-fA-F]+)\s+val=0x([0-9a-fA-F]+)")
# a generic ftrace/kprobe line; used only to recognise a vendor capture
V_TRACE = re.compile(r"\d+\.\d+:\s+\w+:\s")

# (b) port CA write: '0x<8 hex> [..] <= 0x<value>'
P_CAWRITE = re.compile(r"([0-9a-f]{8})[^=\n]*<=\s*0x([0-9a-fA-F]+)")
# (b') port label write: 'out[n] <= 0x<value>'
P_OUTWRITE = re.compile(r"out\[(\d)\][^=\n]*<=\s*0x([0-9a-fA-F]+)")
# a generic lab/bind line; used only to recognise a port capture
P_TRACE = re.compile(r"omo-bind\b")

# vendor VA -> device CA offset for these captures (live-binding.md A.2)
VA_CA_DELTA = 0x89A80000

# ctx table: out[n] -> device CA (live-binding.md Part A; wifidrv1.c:104,117-119)
OUT_CA = {
    0: 0x40039010,  # H2D mask
    1: 0x40039014,  # pending
    2: 0x400392D4,  # doorbell
    3: 0x40101438,  # ack
    4: 0x40101414,  # re-arm
    5: 0x400392F0,  # send irq / ack-clear (reference only; never written)
}

# device-CA plausibility window; the plan's port regex can also match a
# descriptor WORD that happens to be 8 hex digits followed by '<=', so any
# parsed address outside this window is flagged in the report.
CA_LO, CA_HI = 0x40000000, 0x4FFFFFFF


def load_vendor(path):
    """Parse a vendor trace -> (write_sequence, recognised_trace_lines)."""
    writes = []
    traces = 0
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if V_TRACE.search(line):
                traces += 1
            m = V_ADDRVALUE.search(line)
            if m:
                writes.append((int(m.group(1), 16), int(m.group(2), 16)))
                continue
            m = V_KPROBE.search(line)
            if m:
                writes.append((int(m.group(1), 16) - VA_CA_DELTA,
                               int(m.group(2), 16)))
    return writes, traces


def load_port(path):
    """Parse a lab/bind takeover log -> (write_sequence, recognised_lines)."""
    writes = []
    traces = 0
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if P_TRACE.search(line):
                traces += 1
            m = P_OUTWRITE.search(line)
            if m and int(m.group(1)) in OUT_CA:
                writes.append((OUT_CA[int(m.group(1))], int(m.group(2), 16)))
                continue
            m = P_CAWRITE.search(line)
            if m:
                writes.append((int(m.group(1), 16), int(m.group(2), 16)))
    return writes, traces


# ---------------------------------------------------------------------------
# ordered diff
# ---------------------------------------------------------------------------

def first_divergence(a, b):
    """Index of the first differing element, min-len if one is a prefix, else None."""
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return i
    if len(a) != len(b):
        return min(len(a), len(b))
    return None


def _utc():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d-%H%M%S")


def _stem(path):
    return os.path.splitext(os.path.basename(path))[0]


def repo_root():
    # tools live at <root>/opensource/lab/<this>.py  ->  root is two dirs up
    return os.path.abspath(os.path.join(
        os.path.dirname(os.path.abspath(__file__)), os.pardir, os.pardir))


def build_report(vpath, ppath, vw, pw, vt, pt, pair):
    now = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    L = []
    L.append(f"# ordered-diff report  (pair {pair})")
    L.append(f"generated: {now}")
    L.append("tool: opensource/lab/regdump_ordered_diff.py "
             "(sequence-aware; cf. the order-blind regdump_diff.py)")
    L.append("")
    L.append("## inputs")
    L.append(f"  vendor (sequence A): {vpath}")
    L.append(f"    parsed register writes: {len(vw)}   "
             f"recognised trace lines: {vt}")
    L.append(f"  port   (sequence B): {ppath}")
    L.append(f"    parsed register writes: {len(pw)}   "
             f"recognised trace lines: {pt}")
    L.append("  address domain: device CA")
    L.append(f"    vendor kprobe VAs mapped with VA - CA = {VA_CA_DELTA:#010x} "
             "(live-binding.md A.2)")
    L.append("    port out[n] -> CA: "
             + "  ".join(f"out[{n}]={ca:#010x}" for n, ca in sorted(OUT_CA.items())))
    L.append("")

    for tag, seq in (("vendor sequence A", vw), ("port sequence B", pw)):
        L.append(f"## {tag} ({len(seq)} writes)")
        if seq:
            for i, (a, v) in enumerate(seq):
                L.append(f"  {i:4d}: {a:#010x} <= {v:#010x}")
        else:
            L.append("  (no register writes parsed)")
        L.append("")

    L.append("## ordered-sequence diff")
    k = first_divergence(vw, pw)
    if k is None:
        L.append(f"  identical order: all {len(vw)} writes match "
                 "element-for-element")
    else:
        exp = (f"{vw[k][0]:#010x} <= {vw[k][1]:#010x}"
               if k < len(vw) else "(vendor exhausted)")
        act = (f"{pw[k][0]:#010x} <= {pw[k][1]:#010x}"
               if k < len(pw) else "(port exhausted)")
        L.append(f"  first divergence index: {k}")
        L.append(f"    expected (vendor) [{k}]: {exp}")
        L.append(f"    actual   (port)   [{k}]: {act}")
    L.append(f"  totals: vendor={len(vw)} writes, port={len(pw)} writes")
    L.append("")

    L.append("## per-address write-order comparison")
    va = collections.defaultdict(list)
    vb = collections.defaultdict(list)
    for a, v in vw:
        va[a].append(v)
    for a, v in pw:
        vb[a].append(v)
    L.append(f"  {'address':>12}  {'vendor#':>7}  {'vendor values (in order)':<30}  "
             f"{'port#':>5}  {'port values (in order)':<30}  match")
    for a in sorted(set(va) | set(vb)):
        ov = ", ".join(f"{v:#010x}" for v in va.get(a, [])) or "-"
        op = ", ".join(f"{v:#010x}" for v in vb.get(a, [])) or "-"
        ok = "YES" if va.get(a, []) == vb.get(a, []) else "NO"
        L.append(f"  {a:#012x}  {len(va.get(a, [])):7d}  {ov:<30}  "
                 f"{len(vb.get(a, [])):5d}  {op:<30}  {ok}")
    L.append("")

    ca = collections.Counter(vw)
    cb = collections.Counter(pw)
    missing = ca - cb
    extra = cb - ca
    L.append("## missing/extra (multiset over (addr, value))")
    L.append(f"  missing (in vendor, absent from port): {sum(missing.values())}")
    for (a, v), n in sorted(missing.items()):
        L.append(f"    x{n}  {a:#010x} <= {v:#010x}")
    L.append(f"  extra (in port, absent from vendor): {sum(extra.values())}")
    for (a, v), n in sorted(extra.items()):
        L.append(f"    x{n}  {a:#010x} <= {v:#010x}")
    L.append("")

    odd = [(a, v) for a, v in pw if not (CA_LO <= a <= CA_HI)]
    if odd:
        L.append(f"## warning: {len(odd)} parsed port write(s) outside the "
                 f"device-CA window {CA_LO:#010x}..{CA_HI:#010x}")
        L.append("   (the plan's port regex can match a descriptor word followed "
                 "by '<='; kept verbatim, flagged here)")
        for a, v in odd:
            L.append(f"    {a:#010x} <= {v:#010x}")
        L.append("")

    L.append(f"totals: vendor writes={len(vw)}  port writes={len(pw)}  "
             f"missing={sum(missing.values())}  extra={sum(extra.values())}")
    return "\n".join(L) + "\n"


def run_pair(vpath, ppath, outdir=None):
    """Diff one (vendor, port) pair.  Returns (rc, report_path, report_text)."""
    for p in (vpath, ppath):
        if not os.path.isfile(p):
            sys.stderr.write(
                f"regdump_ordered_diff.py: {p}: no such file\n")
            return 1, None, None
    vw, vt = load_vendor(vpath)
    if not vw and not vt:
        sys.stderr.write(f"regdump_ordered_diff.py: {vpath}: no parseable lines "
                         "(not a vendor kprobe trace)\n")
        return 1, None, None
    pw, pt = load_port(ppath)
    if not pw and not pt:
        sys.stderr.write(f"regdump_ordered_diff.py: {ppath}: no parseable lines "
                         "(not a lab/bind port log)\n")
        return 1, None, None

    if outdir is None:
        outdir = os.environ.get("ORDERED_DIFF_OUT") or os.path.join(
            repo_root(), "build", "register-dumps", "diffs", _utc())
    os.makedirs(outdir, exist_ok=True)
    pair = f"{_stem(vpath)}_vs_{_stem(ppath)}"
    rpath = os.path.join(outdir, f"ordered-diff-{pair}.txt")
    text = build_report(vpath, ppath, vw, pw, vt, pt, pair)
    with open(rpath, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)

    k = first_divergence(vw, pw)
    tag = "identical order" if k is None else f"first divergence index {k}"
    print(f"{rpath}: vendor={len(vw)} port={len(pw)} writes; {tag}")
    return 0, rpath, text


# ---------------------------------------------------------------------------
# selftest
# ---------------------------------------------------------------------------

def _w(path, text):
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    return path


def _vendor_line(addr, val, i=0):
    # kprobe store form: 'va=' is a VA -> CA mapping is exercised too
    return (f"   probe-1  [000] d...    1.0000{i:02d}: lb_w: "
            f"(f+0x90/0x158 [m]) va=0x{addr + VA_CA_DELTA:08x} "
            f"val=0x{val:08x}\n")


def _vendor_addrvalue_line(addr, val, i=0):
    # the regdump_diff.py regex form ('addr=<hex>, value=<hex>', no 0x prefix)
    return (f"   probe-1  [000] d...    2.0000{i:02d}: lb_w: (f+0x90 [m]) "
            f"addr={addr:08x}, value={val:08x}\n")


def _port_line(addr, val):
    return (f"[   1.000000] omo-bind:   glue reg 0x{addr:08x} [0x000] "
            f"<= 0x{val:08x} readback=0x{val:08x} match=YES\n")


def selftest():
    checks = []

    def check(ok, name, detail=""):
        checks.append((bool(ok), name, detail))

    tmp = tempfile.mkdtemp(prefix="ordered-diff-selftest-")
    try:
        # 1. permutation fixture: identical multiset, order differs at index 2
        vseq = [(0x40039010, 0x11), (0x40039014, 0x22),
                (0x400392d4, 0x33), (0x40101438, 0x44)]
        pseq = [(0x40039010, 0x11), (0x40039014, 0x22),
                (0x40101438, 0x44), (0x400392d4, 0x33)]
        vf = _w(os.path.join(tmp, "perm_vendor.txt"),
                "".join(_vendor_addrvalue_line(a, v, i)
                        for i, (a, v) in enumerate(vseq)))
        pf = _w(os.path.join(tmp, "perm_port.txt"),
                "".join(_port_line(a, v) for a, v in pseq))

        vw, vt = load_vendor(vf)
        pw, pt = load_port(pf)
        check(vw == vseq, "permuted fixture parses vendor sequence", repr(vw))
        check(pw == pseq, "permuted fixture parses port sequence", repr(pw))
        check(collections.Counter(vw) == collections.Counter(pw),
              "permuted fixture has an identical multiset", "")
        k = first_divergence(vw, pw)
        check(k == 2, "permuted fixture divergence INDEX == 2 (the permuted index)",
              f"got {k}")

        rc, rpath, rep = run_pair(vf, pf, tmp)
        check(rc == 0 and rpath and os.path.isfile(rpath),
              "permuted pair exits 0 and writes a report", f"rc={rc} path={rpath}")
        check("first divergence index: 2" in rep,
              "report names the exact first-divergence index", "")

        # 2. identical sequences -> 'identical order', no divergence
        same = _w(os.path.join(tmp, "same_port.txt"),
                  "".join(_port_line(a, v) for a, v in vseq))
        _, _, rep2 = run_pair(vf, same, tmp)
        check("identical order" in rep2,
              "identical sequences report 'identical order'", "")
        check(first_divergence(vseq, vseq) is None,
              "first_divergence of identical sequences is None", "")

        # 3. zero-parseable-lines files exit non-zero and name the file
        garbage = _w(os.path.join(tmp, "garbage.txt"),
                     "hello world\nnot a trace at all\n")
        goodv = _w(os.path.join(tmp, "good_v.txt"),
                   _vendor_addrvalue_line(0x40039508, 0x3F201818))
        goodp = _w(os.path.join(tmp, "good_p.txt"),
                   _port_line(0x40039508, 0x3F201818))
        e1 = io.StringIO()
        with contextlib.redirect_stderr(e1):
            rc1, _, _ = run_pair(garbage, goodp, tmp)
        check(rc1 != 0, "zero-parse VENDOR file exits non-zero", f"rc={rc1}")
        check("garbage.txt" in e1.getvalue(),
              "zero-parse vendor file is named in the error", e1.getvalue().strip())
        e2 = io.StringIO()
        with contextlib.redirect_stderr(e2):
            rc2, _, _ = run_pair(goodv, garbage, tmp)
        check(rc2 != 0, "zero-parse PORT file exits non-zero", f"rc={rc2}")
        check("garbage.txt" in e2.getvalue(),
              "zero-parse port file is named in the error", e2.getvalue().strip())

        # 4. parser coverage: kprobe VA->CA and out[n] ctx mapping
        kv, _ = load_vendor(_w(os.path.join(tmp, "kprobe.txt"), _vendor_line(0x40039508, 0x3F201818)))
        check(kv == [(0x40039508, 0x3F201818)],
              "kprobe va= form maps VA->CA (0xc9ab9508 -> 0x40039508)", repr(kv))
        ow, _ = load_port(_w(os.path.join(tmp, "outn.txt"),
                             "[   1.0] omo-bind: post out[2] <= 0x00000020\n"))
        check(ow == [(0x400392d4, 0x20)],
              "out[n] label maps through the ctx table (out[2] -> 0x400392d4)", repr(ow))

        # 5. usage exits 2
        check(main(["prog"]) == 2, "no args -> exit 2", "")
        check(main(["prog", "-h"]) == 2, "--help -> exit 2", "")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    bad = [c for c in checks if not c[0]]
    for ok, name, detail in checks:
        print(f"  {'ok  ' if ok else 'FAIL'} {name}" + (f"  -- {detail}" if detail and not ok else ""))
    print(f"selftest: {len(checks) - len(bad)}/{len(checks)} checks passed")
    return 0 if not bad else 1


# ---------------------------------------------------------------------------

def main(argv=None):
    argv = sys.argv if argv is None else argv
    args = list(argv[1:])
    if args and args[0] in ("-h", "--help"):
        print(USAGE)
        return 2
    if args and args[0] == "--selftest":
        return selftest()
    outdir = None
    if "--outdir" in args:
        i = args.index("--outdir")
        if i + 1 >= len(args):
            print(USAGE)
            return 2
        outdir = args[i + 1]
        args = args[:i] + args[i + 2:]
    if len(args) != 2:
        print(USAGE)
        return 2
    rc, _, _ = run_pair(args[0], args[1], outdir)
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
