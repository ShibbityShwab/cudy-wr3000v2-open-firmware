#!/usr/bin/env python3
"""decode_h2d.py - decode / validate / diff a vendor H2D capture.

Lane: rx-h2d-trace (shared task-176). Stdlib only (python3 >= 3.8).

Inputs it understands:
  * sampler format  : the log written by tools/omo_h2dsampler.c (one M record per HCC frame)
  * kprobe format   : ftrace trace_pipe output of the h2d_tx probe set (REPORT section 4.3)
  * bulk format     : a raw host-DRAM dump (e.g. wifi_debug.ko save_hostmem) + the SR node-array
                      physical address; decodes the nodes and the frames that live inside the dump

Outputs: a normalised ordered list (optionally JSONL), a validation/quarantine report, and - with
--ours - the vendor-vs-ours diff buckets. See H2D-FORMATS.md for every rule implemented here.

Exit codes: 0 = ran; 2 = usage/input error; 3 = capture invalid (no valid messages); 4 = selftest failed.
"""
import argparse
import json
import re
import sys

HCC_TAG = 0x5A5A
NODE_MAX_LEN = 0x700
NODE_FLAGS = 0x0D2B          # node[1] & 0x1fff  (the full word is 0x6d2b)
G0, G1, G2, G3 = 0, 1, 2, 3

M_RE = re.compile(
    r"^M ts_ms=(?P<ts>\d+) ch=(?P<ch>\d+) idx=(?P<idx>\d+) prime=(?P<prime>\d+) "
    r"node=0x(?P<node>[0-9a-fA-F]+) len=(?P<len>\d+) flags=0x(?P<flags>[0-9a-fA-F]+) "
    r"total=(?P<total>\d+) group=(?P<group>\d+) bid=0x(?P<bid>[0-9a-fA-F]+) "
    r"vap=(?P<vap>\d+) chip=(?P<chip>\d+) id=(?P<id>\d+) dst=(?P<dst>\d+) "
    r"hex=(?P<hex>[0-9a-fA-F]*)$")
R_RE = re.compile(
    r"^R ts_ms=(?P<ts>\d+) ch=(?P<ch>\d+) ctrl=0x(?P<ctrl>[0-9a-fA-F]+) "
    r"base=0x(?P<base>[0-9a-fA-F]+) depth=0x(?P<depth>[0-9a-fA-F]+) "
    r"prod=0x(?P<prod>[0-9a-fA-F]+) cons=0x(?P<cons>[0-9a-fA-F]+)")
E_RE = re.compile(r"^E ts_ms=(?P<ts>\d+) msg=(?P<msg>.*)$")
KP_RE = re.compile(r"(?P<ts>\d+\.\d+):\s+h2d_tx:.*?msg=0x(?P<msg>[0-9a-fA-F]+)(?P<rest>.*)$")
KP_W = re.compile(r"w(\d+)=0x([0-9a-fA-F]+)")


def u16(b, o):
    return b[o] | (b[o + 1] << 8)


def u32(b, o):
    return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)


# ---------------------------------------------------------------------------------------------
# frame validation + classification
# ---------------------------------------------------------------------------------------------
def validate_frame(frame, node_len, flags):
    """Return (ok, hard_reasons, soft_reasons). Hard reasons quarantine the record."""
    hard, soft = [], []
    if node_len < 12 or node_len > NODE_MAX_LEN:
        hard.append("node-len-range")
        return False, hard, soft
    if len(frame) != node_len:
        hard.append("read-truncated")
        return False, hard, soft
    group = frame[0] & 0x0F
    mid = u16(frame, 4 + 2)  # id at +6
    total = u16(frame, 4)
    if u16(frame, 10) != HCC_TAG:
        hard.append("tag")
    if total != node_len:
        hard.append("total!=node_len")
    if group > 3:
        hard.append("group-range")
    if mid > 0x3F:
        hard.append("id-range")
    if flags not in (NODE_FLAGS, NODE_FLAGS | 0x2000, 0x6D2B):
        soft.append("flags-unexpected")
    if group == G0 and mid == 1:
        if node_len < 28:
            hard.append("cfg-short")
        else:
            key = u16(frame, 20)
            dlen = u16(frame, 22)
            if 28 + dlen != node_len:
                hard.append("cfg-geometry")
            tail = frame[24 + dlen:28 + dlen]
            if tail != b"\xff\xff\xff\xff":
                soft.append("cfg-tail")
            if key == 0 and dlen == 0:
                soft.append("cfg-empty-key")
    return (not hard), hard, soft


def classify(frame):
    """Return a category tuple and the family key used for the diff."""
    group = frame[0] & 0x0F
    mid = u16(frame, 6)
    bid = frame[1]
    vap = frame[2]
    chip = frame[3]
    dst = frame[9]
    base = {"group": group, "id": mid, "bid": bid, "vap": vap, "chip": chip, "dst": dst}
    if group == G0 and mid == 1:
        key = u16(frame, 20)
        dlen = u16(frame, 22)
        base.update(key=key, data_len=dlen,
                    data=frame[24:24 + dlen].hex())
        return ("g0", "key", key), base
    return (f"g{group}", "id", mid), base


def normalise(rec):
    frame = bytes.fromhex(rec["hex"])
    node_len = rec["len"]
    ok, hard, soft = validate_frame(frame, node_len, rec["flags"])
    cat, meta = classify(frame) if len(frame) >= 12 else (("g?", "?", None), {})
    out = dict(rec)
    out.update(valid=ok, hard=hard, soft=soft, category=cat, meta=meta,
               family=f"{cat[0]}/{cat[1]}/{cat[2]:#06x}" if cat[2] is not None else cat[0])
    return out


# ---------------------------------------------------------------------------------------------
# input parsers
# ---------------------------------------------------------------------------------------------
def parse_sampler(path):
    recs, ring, events, errors = [], [], [], []
    with open(path, "r", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            m = M_RE.match(line)
            if m:
                recs.append({
                    "format": "sampler",
                    "ts": int(m.group("ts")),
                    "ch": int(m.group("ch")), "idx": int(m.group("idx")),
                    "prime": int(m.group("prime")),
                    "node": int(m.group("node"), 16), "len": int(m.group("len")),
                    "flags": int(m.group("flags"), 16), "hex": m.group("hex"),
                })
                continue
            m = R_RE.match(line)
            if m:
                ring.append({k: (int(v, 16) if k in ("ctrl", "base", "depth", "prod", "cons")
                                 else int(v)) for k, v in m.groupdict().items()})
                continue
            m = E_RE.match(line)
            if m:
                events.append({"ts": int(m.group("ts")), "msg": m.group("msg")})
                continue
            if line.startswith("#"):
                events.append({"ts": 0, "msg": line})
                continue
            if line.strip():
                errors.append(line)
    return recs, ring, events, errors


def parse_kprobe(path):
    recs = []
    with open(path, "r", errors="replace") as f:
        for line in f:
            m = KP_RE.search(line.rstrip("\n"))
            if not m:
                continue
            words = {}
            for idx, val in KP_W.findall(m.group("rest")):
                words[int(idx)] = int(val, 16)
            if not words:
                continue
            n = max(words) + 1
            frame = bytearray()
            for i in range(n):
                frame += (words.get(i, 0)).to_bytes(4, "little")
            # the probe fetches a fixed 64-byte window: trim to the HCC total field when sane
            if len(frame) >= 6:
                total = u16(frame, 4)
                if 12 <= total <= len(frame):
                    frame = frame[:total]
            ts = int(float(m.group("ts")) * 1_000_000)
            recs.append({"format": "kprobe", "ts": ts, "ch": -1, "idx": -1, "prime": 0,
                         "node": 0, "len": len(frame), "flags": NODE_FLAGS,
                         "hex": bytes(frame).hex()})
    return recs


def parse_bulk(path, dump_base, node_base, depth):
    """Decode an SR node array inside a raw host-DRAM dump (save_hostmem output)."""
    data = open(path, "rb").read()
    recs = []
    if node_base < dump_base or node_base - dump_base + depth * 8 > len(data):
        raise SystemExit("bulk: node array outside the dump")
    off = node_base - dump_base
    for k in range(depth):
        w0 = u32(data, off + k * 8)
        w1 = u32(data, off + k * 8 + 4)
        if not w0:
            continue
        ln = w1 >> 16
        if ln < 12 or ln > NODE_MAX_LEN:
            continue
        if w0 < dump_base or w0 - dump_base + ln > len(data):
            continue       # buffer not inside this dump: reported as a node-only record
        frame = data[w0 - dump_base: w0 - dump_base + ln]
        recs.append({"format": "bulk", "ts": 0, "ch": -1, "idx": k, "prime": 1,
                     "node": w0, "len": ln, "flags": w1 & 0x1FFF, "hex": frame.hex()})
    return recs


# ---------------------------------------------------------------------------------------------
# diff against our driver's committed H2D set
# ---------------------------------------------------------------------------------------------
def parse_ours(path):
    ours = []
    with open(path, "r") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) < 3:
                continue
            grp, kind, val = parts[0], parts[1], int(parts[2], 16)
            total = int(parts[3], 0) if len(parts) > 3 else 0
            name = parts[4] if len(parts) > 4 else ""
            ours.append({"group": grp, "kind": kind, "value": val,
                         "total": total, "name": name})
    return ours


def diff(records, ours):
    by_key = {}
    for o in ours:
        if o["group"] == "g0" and o["kind"] == "key":
            by_key[("g0", "key", o["value"])] = o
        elif o["group"] == "g0" and o["kind"] == "id":
            by_key[("g0", "id", o["value"])] = o
    buckets = {"matched": [], "drift": [], "missing": [], "core": [], "g2": [], "g3": [],
               "quarantined": []}
    for r in records:
        if not r["valid"]:
            buckets["quarantined"].append(r)
            continue
        cat = r["category"]
        if cat[0] == "g0":
            ent = by_key.get(cat)
            if ent:
                if ent["total"] and r["len"] != ent["total"]:
                    r["expect"] = ent
                    buckets["drift"].append(r)
                else:
                    r["match"] = ent
                    buckets["matched"].append(r)
            else:
                buckets["missing"].append(r)
        elif cat[0] == "g1":
            buckets["core"].append(r)
        elif cat[0] == "g2":
            buckets["g2"].append(r)
        else:
            buckets["g3"].append(r)
    return buckets


# ---------------------------------------------------------------------------------------------
# report
# ---------------------------------------------------------------------------------------------
def short(r):
    m = r["meta"] or {}
    extra = ""
    if "key" in m:
        extra = f" key=0x{m['key']:04x} dlen={m['data_len']}"
    return (f"  {r['family']:<18} len={r['len']:4d} "
            f"g={m.get('group')} id={m.get('id')} bid=0x{m.get('bid', 0):02x} "
            f"vap={m.get('vap')} chip={m.get('chip')}{extra}")


def report(records, buckets, args):
    valid = [r for r in records if r["valid"]]
    t0 = min((r["ts"] for r in valid), default=0)
    t1 = max((r["ts"] for r in valid), default=0)
    span = round((t1 - t0) / 1000.0, 1)
    fams = sorted({r["family"] for r in valid})
    print(f"records={len(records)} valid={len(valid)} quarantined={len(records) - len(valid)}")
    print(f"families={len(fams)} span_s={span} formats={sorted({r['format'] for r in records})}")
    for f in fams:
        n = sum(1 for r in valid if r["family"] == f)
        print(f"  family {f:<18} x{n}")
    if buckets:
        for name in ("matched", "drift", "missing", "core", "g2", "g3", "quarantined"):
            rows = buckets[name]
            print(f"\n[{name}] {len(rows)}")
            for r in rows[: args.max_rows]:
                hint = ""
                if name == "drift" and r.get("expect"):
                    hint = f"  (ours expects total={r['expect']['total']} {r['expect']['name']})"
                if r.get("hard"):
                    hint += f"  hard={r['hard']}"
                if r.get("soft"):
                    hint += f"  soft={r['soft']}"
                print(short(r) + hint)
            if len(rows) > args.max_rows:
                print(f"  ... {len(rows) - args.max_rows} more")
    if valid:
        print(f"\nCAPTURE VALID: {len(valid)} messages, {len(fams)} families, {span} seconds")
    else:
        print("\nCAPTURE INVALID: 0 valid messages")
    return 0 if valid else 3


# ---------------------------------------------------------------------------------------------
# selftest
# ---------------------------------------------------------------------------------------------
def selftest():
    def cfg(group, bid, mid, key, data, tag=HCC_TAG, total=None):
        total = total if total is not None else 28 + len(data)
        hdr = bytes([group & 0xF, bid, 0, 4]) + total.to_bytes(2, "little") + \
            mid.to_bytes(2, "little") + bytes([0, 0]) + tag.to_bytes(2, "little")
        return hdr + bytes([0, 0, 0, 0, 1, 0, 0, 0]) + key.to_bytes(2, "little") + \
            len(data).to_bytes(2, "little") + data + b"\xff" * 4

    f_ok = cfg(G0, 1, 1, 0x00D8, bytes(20))
    ok, hard, soft = validate_frame(f_ok, len(f_ok), NODE_FLAGS)
    assert ok and not hard, (hard, soft)
    cat, meta = classify(f_ok)
    assert cat == ("g0", "key", 0x00D8), cat
    assert meta["data_len"] == 20
    # tag failure
    bad = cfg(G0, 1, 1, 0x0004, bytes(4), tag=0)
    ok, hard, _ = validate_frame(bad, len(bad), NODE_FLAGS)
    assert not ok and "tag" in hard
    # total-field vs node-len mismatch
    bad_total = cfg(G0, 1, 1, 0x00D8, bytes(20), total=len(f_ok) - 4)
    ok, hard, _ = validate_frame(bad_total, len(bad_total), NODE_FLAGS)
    assert not ok and "total!=node_len" in hard, hard
    # g0 id7 (direct id) classification
    hdr = bytes([0, 1, 0, 0]) + (180).to_bytes(2, "little") + (7).to_bytes(2, "little") + \
        bytes([0, 0, 0x5A, 0x5A])
    f7 = hdr + bytes(168)
    ok, hard, _ = validate_frame(f7, len(f7), NODE_FLAGS)
    assert ok, hard
    assert classify(f7)[0] == ("g0", "id", 7)
    # ours-set diff logic
    ours = parse_ours(__file__.replace("decode_h2d.py", "ours_h2d_set.txt")) \
        if __import__("os").path.exists(__file__.replace("decode_h2d.py", "ours_h2d_set.txt")) else []
    if ours:
        recs = [normalise({"format": "t", "ts": 1, "ch": 0, "idx": 0, "prime": 0,
                           "node": 0x82483840, "len": len(f_ok), "flags": NODE_FLAGS,
                           "hex": f_ok.hex()}),
                normalise({"format": "t", "ts": 2, "ch": 0, "idx": 1, "prime": 0,
                           "node": 0x82483840, "len": len(bad), "flags": NODE_FLAGS,
                           "hex": bad.hex()})]
        b = diff(recs, ours)
        assert len(b["matched"]) == 1, b
        assert len(b["quarantined"]) == 1, b
    print("selftest: PASS (validation + classification + diff)")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description="decode/validate/diff a vendor H2D capture")
    ap.add_argument("--input", help="capture file (sampler or kprobe log)")
    ap.add_argument("--format", choices=["auto", "sampler", "kprobe", "bulk"], default="auto")
    ap.add_argument("--ours", help="our driver's H2D set (tools/ours_h2d_set.txt)")
    ap.add_argument("--jsonl", help="write the normalised records here")
    ap.add_argument("--diff", action="store_true", help="print the vendor-vs-ours buckets")
    ap.add_argument("--dump", help="bulk format: raw host-DRAM dump file")
    ap.add_argument("--dump-base", type=lambda s: int(s, 0), default=0)
    ap.add_argument("--nodebase", type=lambda s: int(s, 0), default=0)
    ap.add_argument("--depth", type=int, default=32)
    ap.add_argument("--max-rows", type=int, default=200)
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()
    if not args.input and not args.dump:
        ap.error("--input or --dump is required (or --selftest)")

    fmt = args.format
    if fmt == "auto":
        fmt = "kprobe" if (args.input and "kprobe" in args.input) else "sampler"
    if fmt == "kprobe":
        records = parse_kprobe(args.input)
    elif fmt == "bulk":
        if not args.dump or not args.nodebase:
            ap.error("bulk format needs --dump FILE --nodebase PHY [--dump-base PHY]")
        records = parse_bulk(args.dump, args.dump_base, args.nodebase, args.depth)
    else:
        records, ring, events, errors = parse_sampler(args.input)
        if errors:
            print(f"unparsed lines: {len(errors)} (first: {errors[0][:80]})", file=sys.stderr)
        print(f"ring-state records: {len(ring)}; event records: {len(events)}")
        if ring:
            print(f"  first ring: {ring[0]}")
            print(f"  last  ring: {ring[-1]}")

    records = [normalise(r) for r in records]
    buckets = None
    if args.ours:
        buckets = diff(records, parse_ours(args.ours))
    if args.jsonl:
        with open(args.jsonl, "w") as f:
            for r in records:
                f.write(json.dumps(r) + "\n")
        print(f"wrote {args.jsonl}")
    return report(records, buckets if args.diff or args.ours else None, args)


if __name__ == "__main__":
    sys.exit(main())
