#!/usr/bin/env python3
"""decode_stock_census.py - census / alias-decode a STOCK vendor H2D capture.

Lane: wifi-stock-h2d-census (shared task-232). Stdlib only (python3 >= 3.8).

Input: the v1/v2 omo_h2dsampler log (one M record per ring node, the full node read).
Committed sibling tools: wifi-rx-h2d-trace/tools/decode_h2d.py (protocol formatter +
validation rules) and wifi-f1-from-capture/tools/decode_f1_capture.py (family tables).

WHAT THIS ADDS OVER decode_h2d.py
  * a validity split that reports the RAW family census next to the length-valid one
    (the raw counts over-count; see TABLES.md sec. 1 of the f1 lane);
  * alias folding for stale-header reads (the f1 lane's "stale first 4 bytes" rule,
    generalised to head+over-read tails) - g12 id1 -> g0 id1 key 0x00d8/0x0127 and
    g12 id29 -> the 16-B HCC-timer class;
  * an over-read detector: a node whose stale length word is longer than the frame's
    own total makes the sampler read past the message buffer into adjacent host DRAM
    (this is where the IEEE 1905.1 Ethernet fragment in the stock capture comes from);
  * a redacting mode: MAC addresses in quoted frames are replaced by documentation
    examples before anything is committed.

Exit codes: 0 ran, 2 usage/input error, 3 capture unreadable, 4 selftest failed.
"""
import argparse
import collections
import hashlib
import json
import re
import sys

HCC_TAG = 0x5A5A
M_RE = re.compile(
    r"^M ts_ms=(?P<ts>\d+) ch=(?P<ch>\d+) idx=(?P<idx>\d+) prime=(?P<prime>\d+) "
    r"node=0x(?P<node>[0-9a-fA-F]+) len=(?P<len>\d+) flags=0x(?P<flags>[0-9a-fA-F]+) "
    r"total=(?P<total>\d+) group=(?P<group>\d+) bid=0x(?P<bid>[0-9a-fA-F]+) "
    r"vap=(?P<vap>\d+) chip=(?P<chip>\d+) id=(?P<id>\d+) dst=(?P<dst>\d+) "
    r"hex=(?P<hex>[0-9a-fA-F]*)$")

# builder attribution for the length-valid H2D families seen in the stock captures.
# (group, id) -> (symbol, evidence grade, short decode)
H2D_BUILDERS = {
    (0, 1): ("hmac_config_send_event", "V*", "config event: preamble 8B, key u16@+20, dlen u16@+22, total=28+dlen"),
    (0, 2): ("hmac_config_alg_send_event", "V*", "alg request: token, key 0x0101, subkey 0x0b, 15 B data"),
    (0, 12): ("hmac_enc_scan_send_req", "V*", "scan request, 508 B payload"),
    (0, 22): ("hmac_chan_do_sync", "V", "40 B channel-sync struct (payload[0]=channel number)"),
    (0, 29): ("hmac_edca_opt_timeout_fn", "V*", "EDCA per-chip context sync, 60 B payload"),
    (0, 32): ("hmac_config_h2d_send_app_ie", "V", "app IE: alloc(dlen+11), payload = caller struct"),
    (0, 36): ("hmac_scan_config_bss_color", "V", "8 B bitmap of per-entry BSS-color indices"),
}
# device->host (D2H) classes the recycled H2D node buffers reveal (read through a stale node).
D2H_CLASSES = {
    (4, 4): ("D2H heartbeat (plat.ko chip 4)", "V*", "22 B; payload 'HEARTBEAT\\0'"),
    (4, 0): ("D2H OAM/device log (plat.ko chip 4)", "V*", "variable; 0x7e-prefixed log records"),
    (2, 9): ("D2H log class (wifi.ko chip 2)", "I", "text-ish, carries 'vapN' strings"),
}
# out-of-range group nibbles seen only on torn records (never a valid H2D send).
STALE_GROUPS = {8, 9, 12, 0x0c}
MAC_RE = re.compile(r"d4" r"0dab[0-9a-f]{6}", re.I)   # device/radio MAC prefix seen in this capture
REDACTED_MAC = "d40dabXXXXXX"


def u16(b, o):
    return b[o] | (b[o + 1] << 8)


def u32(b, o):
    return u16(b, o) | (u16(b, o + 2) << 16)


def parse(path):
    recs = []
    with open(path, "r", errors="replace") as f:
        for line in f:
            m = M_RE.match(line.rstrip("\n"))
            if not m:
                continue
            g = m.groupdict()
            recs.append(dict(
                ts=int(g["ts"]), ch=int(g["ch"]), idx=int(g["idx"]), prime=int(g["prime"]),
                node=int(g["node"], 16), len=int(g["len"]), flags=int(g["flags"], 16),
                total=int(g["total"]), group=int(g["group"]), bid=int(g["bid"], 16),
                vap=int(g["vap"]), chip=int(g["chip"]), mid=int(g["id"]),
                dst=int(g["dst"]), hex=g["hex"].lower(), raw=bytes.fromhex(g["hex"])))
    return recs


def tag_ok(r):
    b = r["raw"]
    return len(b) >= 12 and u16(b, 10) == HCC_TAG


def length_valid(r):
    return (12 <= r["len"] <= 0x700 and len(r["raw"]) == r["len"]
            and tag_ok(r) and r["len"] == r["total"])


def config_geometry(b):
    """g0 id1 geometry: total == 28 + u16@+22 -> (key, dlen) else None."""
    if len(b) < 28 or u16(b, 6) != 1:
        return None
    dlen = u16(b, 22)
    if 28 + dlen != u16(b, 4):
        return None
    return u16(b, 20), dlen


def is_1905_fragment(b):
    """True iff b contains an IEEE 1905.1 Ethernet header prefix: the 1905.1 group MAC
    01:80:c2:00:00:13 followed 12 bytes later by EtherType 0x893A (src MAC in between)."""
    dst = bytes.fromhex("0180c2000013")
    off = b.find(dst)
    while off >= 0:
        if b[off + 12:off + 14] == bytes.fromhex("893a"):
            return True
        off = b.find(dst, off + 1)
    return False


def classify(r):
    """Return a classification dict for one M record."""
    b = r["raw"]
    out = dict(r)
    if not tag_ok(r):
        out.update(kind="unparsed", status="torn", note="tag@+10 != 0x5a5a")
        return out
    # the frame's own header fields (bytes 4..11) are the most reliable: the builders
    # write total/id/tag last, while bytes 0..3 and payload edges can be stale.
    c_id, c_total = u16(b, 6), u16(b, 4)
    geom = config_geometry(b)
    head = b[:4].hex()
    overrun = r["len"] > c_total
    if is_1905_fragment(b):
        out["ieee1905"] = True
    if length_valid(r):
        if r["group"] in STALE_GROUPS:
            # valid length but an out-of-range group nibble: the f1 lane's stale-head class.
            if geom:
                out.update(kind="alias-config", status="alias",
                           canonical="g0 id1 key 0x%04x" % geom[0], note="stale head %s" % head)
            elif c_id == 0x1D and c_total == 16:
                out.update(kind="alias-timer", status="alias",
                           canonical="hcc-timer 0x1d", note="stale head %s" % head)
            else:
                out.update(kind="alias-unknown", status="alias", note="stale head %s" % head)
            return out
        if r["group"] == 0 and geom and r["mid"] == 1:
            key, dlen = geom
            tail = b[24 + dlen:28 + dlen]
            out.update(kind="config", status="valid", key=key, dlen=dlen,
                       tail_ok=(tail == b"\xff\xff\xff\xff"))
            return out
        out.update(kind="h2d", status="valid")
        return out
    # ---- torn (node length word != frame total) ----
    if c_total == 16 and c_id == 0x1D:
        out.update(kind="torn-timer", status="torn",
                   note="16-B HCC timer (0x1d) class; payload=host pointer")
    elif head == "04010000" and c_id == 4 and c_total == 22:
        out.update(kind="torn-d2h-heartbeat", status="torn", note="D2H heartbeat content")
    elif head == "04010000" and c_id == 0:
        out.update(kind="torn-d2h-oam", status="torn", note="D2H OAM/log content")
    elif geom:
        out.update(kind="torn-config", status="torn", key=geom[0],
                   note="config event content behind a stale node/head")
    else:
        out.update(kind="torn-unknown", status="torn")
    if overrun:
        out["overrun"] = r["len"] - c_total
    return out


SPACED_MAC_RE = re.compile(r"(?i)d4 0d ab (?:[0-9a-f]{2} ){2}[0-9a-f]{2}")


def redact(s):
    """MAC-redact both contiguous hex and the spaced hexdump form."""
    s = MAC_RE.sub(REDACTED_MAC, s)
    return SPACED_MAC_RE.sub("d4 0d ab XX XX XX", s)


def redact_hex(h):
    """MAC-redact a lower-case hex blob (returns the same length)."""
    return MAC_RE.sub(REDACTED_MAC, h)


def show_hex(b, indent="  "):
    lines = []
    for i in range(0, len(b), 16):
        c = b[i:i + 16]
        asc = "".join(chr(x) if 32 <= x < 127 else "." for x in c)
        lines.append("%s%3d: %-48s %s" % (indent, i, " ".join("%02x" % x for x in c), asc))
    return "\n".join(lines)


def family_name(group, mid, key=None):
    if group == 0 and mid == 1 and key is not None:
        return "g0 id1 key 0x%04x" % key
    name = H2D_BUILDERS.get((group, mid))
    if name:
        return "g%d id%d  [%s %s]" % (group, mid, name[0], name[1])
    return "g%d id%d" % (group, mid)


def census(recs):
    C = [classify(r) for r in recs]
    valid = [c for c in C if c["status"] == "valid"]
    alias = [c for c in C if c["status"] == "alias"]
    torn = [c for c in C if c["status"] == "torn"]
    t0 = recs[0]["ts"] if recs else 0
    out = []
    out.append("records=%d valid=%d alias=%d torn=%d span_s=%.1f"
               % (len(recs), len(valid), len(alias), len(torn),
                  (max(r["ts"] for r in recs) - min(r["ts"] for r in recs)) / 1000.0 if recs else 0))
    out.append("ch=%s prime=%d flags=%s tag@+10=%d/%d"
               % (dict(collections.Counter(r["ch"] for r in recs)),
                  sum(1 for r in recs if r["prime"]),
                  dict(collections.Counter(r["flags"] for r in recs)),
                  sum(1 for r in recs if tag_ok(r)), len(recs)))
    out.append("")
    out.append("[valid-by-length families]  (node len == frame total)")
    vf = collections.Counter((c["group"], c["mid"]) for c in valid)
    for g, i in sorted(vf, key=lambda k: -vf[k]):
        name = family_name(g, i)
        out.append("  %-46s n=%4d" % (name, vf[(g, i)]))
    kk = collections.Counter(c["key"] for c in valid if c["kind"] == "config")
    out.append("  config keys: %d distinct; %s"
               % (len(kk), ", ".join("0x%04x=%d" % (k, kk[k]) for k in sorted(kk, key=lambda k: -kk[k]))))
    out.append("")
    out.append("[aliases folded]  (valid length, stale head)")
    for c in alias:
        out.append("  %s -> %s   len=%d  head=%s  ts=%d"
                   % ("g%d id%d" % (c["group"], c["mid"]), c["canonical"],
                      c["len"], c["raw"][:4].hex(), c["ts"]))
    out.append("")
    out.append("[torn records by raw family / content class]")
    cls = collections.Counter()
    for c in torn:
        cls[(c["group"], c["mid"], c["kind"])] += 1
    for g, i, k in sorted(cls, key=lambda x: -cls[x]):
        over = sum(1 for c in torn if (c["group"], c["mid"], c["kind"]) == (g, i, k) and c.get("ieee1905"))
        out.append("  g%-2d id%-3d %-22s n=%4d%s" % (g, i, k, cls[(g, i, k)],
                                                     "  (IEEE1905 frag in over-read: %d)" % over if over else ""))
    ieee = [c for c in C if c.get("ieee1905")]
    out.append("")
    out.append("[IEEE 1905.1 (Multi-AP) frag in sampler over-read] n=%d" % len(ieee))
    for c in ieee[:8]:
        off = c["raw"].find(bytes.fromhex("0180c2000013"))
        out.append("  ts=%d %s len=%d total=%d frag@%d overrun=%s"
                   % (c["ts"], "g%d id%d" % (c["group"], c["mid"]), c["len"], c["total"],
                      off, c.get("overrun")))
    return out, C


def dump_family(C, group, mid, include_torn=False):
    sub = [c for c in C if (c["group"], c["mid"]) == (group, mid)
           and (include_torn or c["status"] != "torn")]
    out = ["family g%d id%d: n=%d unique=%d" % (group, mid, len(sub), len({c["hex"] for c in sub}))]
    seen = set()
    for c in sub:
        if c["hex"] in seen:
            continue
        seen.add(c["hex"])
        out.append(" len=%d group=%d id=%d bid=0x%02x vap=%d chip=%d dst=%d prime=%d flags=0x%04x"
                   % (c["len"], c["group"], c["mid"], c["bid"], c["vap"], c["chip"], c["dst"],
                      c["prime"], c["flags"]))
        out.append(show_hex(c["raw"]))
    return out


def aliases(C):
    """Evidence: for each unique stale-head alias, name the canonical message it matches."""
    valid = [c for c in C if c["status"] == "valid"]
    out = []
    seen = set()
    for c in C:
        if c["status"] != "alias" or c["hex"] in seen:
            continue
        seen.add(c["hex"])
        cands = [v for v in valid if v["total"] == c["total"]]
        best, score = None, -1
        for v in cands:
            eq = sum(1 for i in range(4, c["total"]) if c["raw"][i] == v["raw"][i])
            if eq > score:
                best, score = v, eq
        out.append("alias g%d id%d len=%d head=%s ts=%d" %
                   (c["group"], c["mid"], c["len"], c["raw"][:4].hex(), c["ts"]))
        if best is None:
            out.append("  canonical: no same-length valid frame")
            continue
        diffs = [i for i in range(4, c["total"]) if c["raw"][i] != best["raw"][i]]
        out.append("  canonical: g%d id%d len=%d node=0x%08x  identical-after-head=%d/%d"
                   % (best["group"], best["mid"], best["len"], best["node"], score, c["total"] - 4))
        out.append("  diff offsets: %s" % (", ".join(str(i) for i in diffs[:16]) or "none"))
    # the torn 16-B HCC-timer class: head variants (the canonical head is the majority head)
    timers = [c for c in C if c["kind"] in ("torn-timer", "alias-timer")]
    if timers:
        heads = collections.Counter(c["raw"][:4].hex() for c in timers)
        out.append("")
        out.append("16-B HCC-timer class: n=%d; head variants: %s" %
                   (len(timers), ", ".join("%s x%d" % (h, n) for h, n in heads.most_common())))
        out.append("  canonical head 02 01 00 00 = plat.ko hcc_timer_timeout_proc "
                   "(hdr[0]=2, hdr[3]=0; payload u32 host pointer); no firmware group-2 id-0x1d handler.")
    ieee = [c for c in C if c.get("ieee1905")]
    if ieee:
        out.append("")
        out.append("IEEE 1905.1 fragment carriers: %d records (sampler over-read past the message)" % len(ieee))
        for c in ieee[:10]:
            off = c["raw"].find(bytes.fromhex("0180c2000013"))
            out.append("  ts=%d g%d id%d len=%d total=%d overrun=%d frag@%d"
                       % (c["ts"], c["group"], c["mid"], c["len"], c["total"],
                          c.get("overrun", 0), off))
    return out


def selftest():
    def frame(group, mid, total, payload, bid=1, vap=0, chip=0, tag=HCC_TAG):
        hdr = bytes([group & 0xF, bid, vap, chip]) + total.to_bytes(2, "little") + \
            mid.to_bytes(2, "little") + bytes([0, 0]) + tag.to_bytes(2, "little")
        return hdr + payload

    recs = []
    def add(ts, raw, node=0x82483840, flen=None, flags=0):
        recs.append(dict(ts=ts, ch=0, idx=0, prime=0, node=node,
                         len=len(raw) if flen is None else flen, flags=flags,
                         total=u16(raw, 4), group=raw[0] & 0xF, bid=raw[1],
                         vap=raw[2], chip=raw[3], mid=u16(raw, 6), dst=raw[9],
                         hex=raw.hex(), raw=raw))

    # 1) a valid g0 id1 config event (key 0x00d8, 20 B data)
    cfg = frame(0, 1, 48, bytes(8) + b"\xd8\x00" + b"\x14\x00" + bytes(20) + b"\xff" * 4)
    add(1000, cfg)
    # 2) a valid g0 id29 EDCA frame
    edca = frame(0, 29, 72, bytes(20) + b"\xff" * 40, chip=4)
    add(2000, edca)
    # 3) its torn twin (stale node length 184, frame total 72)
    add(3000, edca, flen=184)
    # 4) a torn 16-B HCC timer message (head stale: 0c 18 30 00) + over-read junk
    timer = b"\x0c\x18\x30\x00" + b"\x10\x00\x1d\x00\x00\x00\x5a\x5a" + b"\xc0\xdd\xbc\xbf" + bytes(56)
    add(4000, timer)
    # 5) a stale-head alias of the config event (g12 head, valid length, stale tail)
    alias = bytes.fromhex("0c183000") + cfg[4:]
    add(5000, alias)
    # 6) an over-read of a valid-length config event whose junk tail carries the 1905.1 fragment
    j1905 = bytes.fromhex("0180c2000013") + bytes.fromhex("d40dab000001") + bytes.fromhex("893a") + bytes(8)
    over = frame(0, 29, 72, bytes(20) + b"\xff" * 30 + j1905[:22])  # 12+20+30+22 = 84 -> trim
    over = frame(0, 29, 72, bytes(20) + b"\xff" * 18 + j1905[:22], chip=8)
    add(6000, over)

    out, C = census(recs)
    text = "\n".join(out)
    assert "records=6 valid=3 alias=1 torn=2" in text, text
    kinds = collections.Counter(c["kind"] for c in C)
    assert kinds["torn-timer"] == 1, kinds
    assert kinds["alias-config"] == 1, kinds
    assert kinds["config"] == 1 and C[0]["tail_ok"], C[0]
    # alias folds onto key 0x00d8
    assert C[4]["canonical"] == "g0 id1 key 0x00d8", C[4]
    # tail over-read for the timer twin: len - total
    assert C[2].get("overrun") == 112, C[2]
    # 1905.1 detector
    assert is_1905_fragment(j1905)
    assert C[5].get("ieee1905"), C[5]
    # MAC redaction keeps the byte count
    h = redact_hex("aaaad40dab000001bbbb")
    assert h == "aaaad40dabXXXXXXbbbb", h
    # tag invariant helper
    assert tag_ok(recs[0]) and not tag_ok(dict(raw=bytes(12)))
    # classify never raises on a short record
    short = dict(ts=0, ch=0, idx=0, prime=0, node=0, len=5, flags=0, total=0,
                 group=0, bid=0, vap=0, chip=0, mid=0, dst=0, hex="0000000000",
                 raw=bytes(5))
    assert classify(short)["status"] == "torn"
    print("selftest: PASS (12 checks: census split, alias fold, over-read, 1905 detector, redaction)")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description="census/decode a stock vendor H2D capture")
    ap.add_argument("--input", help="sampler log")
    ap.add_argument("--families", action="store_true", help="dump unique valid family frames")
    ap.add_argument("--family", nargs=2, type=int, metavar=("G", "ID"), help="dump one family")
    ap.add_argument("--torn", action="store_true", help="with --family: include torn records")
    ap.add_argument("--aliases", action="store_true", help="alias-fitting evidence table")
    ap.add_argument("--json", help="write the classified records as JSONL")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--no-redact", action="store_true", help="do NOT redact MACs (local use only)")
    args = ap.parse_args(argv)
    if args.selftest:
        return selftest()
    if not args.input:
        ap.error("--input is required (or --selftest)")
    recs = parse(args.input)
    if not recs:
        print("capture unreadable: no M records", file=sys.stderr)
        return 3
    out, C = census(recs)
    text = "\n".join(out)
    if not args.no_redact:
        text = redact(text)
    print(text)
    if args.aliases:
        lines = aliases(C)
        print("\n".join(lines if args.no_redact else [redact(x) for x in lines]))
    if args.family:
        g, i = args.family
        lines = dump_family(C, g, i, include_torn=args.torn)
        print("\n".join(lines if args.no_redact else [redact(x) for x in lines]))
    if args.families:
        for g, i in sorted({(c["group"], c["mid"]) for c in C if c["status"] == "valid"}):
            lines = dump_family(C, g, i)
            print("\n".join(lines if args.no_redact else [redact(x) for x in lines]))
    if args.json:
        with open(args.json, "w") as f:
            for c in C:
                f.write(json.dumps({k: v for k, v in c.items()
                                    if k not in ("raw", "hex")}) + "\n")
        print("wrote %s (%d records)" % (args.json, len(C)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
