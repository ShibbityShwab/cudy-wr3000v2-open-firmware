#!/usr/bin/env python3
"""decode_f1_capture.py - decode the stock H2D SR-ring trace into real message families.

Lane fw-f1-capture (task-198). Input = the log of the v2 mmap sampler
(opensource/docs/incoming/wifi-rx-h2d-window/tools/omo_h2dsampler-mmap.c), one line
per ring node it saw:

  M ts_ms=.. ch=0 idx=.. prime=0 node=0x.. len=<node word1 len> flags=0x.. total=<frame +4>
    group=<frame[0]&0xf> bid=.. vap=.. chip=.. id=<u16 frame+6> dst=.. hex=<len bytes>

The sampler reads a node {word0 = buffer, word1 = (len<<16)|flags} and then the L bytes
at that buffer in two separate /dev/mem preads. A node read across a host update - or a
recycled buffer - gives a record whose node len != the frame's own total field, and
whose leading bytes may be stale. In the 2026-11-28 stock capture 1112 of 1716 records
are torn; a family split on (group,id) alone therefore invents families.

The honest split implemented here:
  1. VALID records: node_len == frame total, 12 <= total <= 0x700, tag 0x5a5a at +10.
     These are true host->device (H2D) sends.
  2. ALIASED valid records: two different (group,id) headers occur with byte-identical
     (total, payload). The minority header is a partially rewritten 12-byte HCC header
     (only bytes 0..3 differ), so those records collapse onto the canonical message.
     Example: the "g12 id1" family is the g0-id1 key-0x0127 beacon config event with
     four stale leading bytes.
  3. TORN records: node_len != total. Their content is collapsed onto a VALID content
     signature when one exists; otherwise it is RESIDUE. Residue is either a real H2D
     message whose node was always seen torn (e.g. the 16-byte plat.ko HCC timer
     message) or device->host (D2H) content in recycled buffers (e.g. the firmware's
     22-byte HEARTBEAT, its OAM logs). Residue is reported, never trusted as an H2D send.

No vendor binary or firmware byte is read or committed. Usage:

  decode_f1_capture.py CAPTURE.log [--json]
  decode_f1_capture.py --selftest

--selftest builds synthetic records from the documented wire geometry only.
"""
import json
import re
import sys

TAG = 0x5A5A
TAG_OFF = 10
MAX_FRAME = 0x700


def parse(path):
    out = []
    with open(path, "r", errors="replace") as fh:
        for line in fh:
            if not line.startswith("M "):
                continue
            f = dict(re.findall(r"(\w+)=(\S+)", line))
            try:
                rec = {
                    "ts_ms": int(f["ts_ms"]), "ch": int(f["ch"]),
                    "idx": int(f["idx"]), "prime": int(f["prime"]),
                    "node": int(f["node"], 16), "len": int(f["len"]),
                    "flags": int(f["flags"], 16), "total": int(f["total"]),
                    "group": int(f["group"]), "chip": int(f["chip"]),
                    "id": int(f["id"]), "b": bytes.fromhex(f["hex"]),
                }
            except (KeyError, ValueError):
                continue
            out.append(rec)
    return out


def tag_ok(b):
    return len(b) >= TAG_OFF + 2 and int.from_bytes(b[TAG_OFF:TAG_OFF + 2], "little") == TAG


def frame_valid(rec):
    return (12 <= rec["total"] <= MAX_FRAME and rec["total"] == rec["len"]
            and len(rec["b"]) >= rec["total"] and tag_ok(rec["b"]))


def content_sig(rec):
    """(declared total, payload) - payload is where a torn header rewrite does not land."""
    t = rec["total"]
    if not (12 <= t <= MAX_FRAME) or len(rec["b"]) < t:
        return None
    return (t, rec["b"][12:t])


def key_of(rec):
    """g0 id1 config-event key = u16 at frame+20 (payload+8); TABLES-config-senders."""
    b = rec["b"]
    return int.from_bytes(b[20:22], "little") if len(b) >= 22 else None


def family_key(r):
    if r["group"] == 0 and r["id"] == 1:
        return (0, 1, key_of(r))
    return (r["group"], r["id"])


def classify(recs):
    valid = [r for r in recs if frame_valid(r)]
    torn = [r for r in recs if not frame_valid(r)]
    sigfam = {}
    for r in valid:
        sig = content_sig(r)
        if sig is None:
            continue
        k = family_key(r)
        d = sigfam.setdefault(sig, {})
        d[k] = d.get(k, 0) + 1
    alias = {}
    for sig, fams in sigfam.items():
        if len(fams) < 2:
            continue
        canon = sorted(fams.items(), key=lambda kv: (-kv[1], str(kv[0])))[0][0]
        for k in fams:
            if k != canon:
                alias[k] = canon
    vfam = {}
    for r in valid:
        k = family_key(r)
        k = alias.get(k, k)
        vfam[k] = vfam.get(k, 0) + 1
    collapse, residue, bad = {}, {}, 0
    for r in torn:
        sig = content_sig(r)
        if sig is None:
            bad += 1
            continue
        if sig in sigfam:
            canon = sorted(sigfam[sig].items(), key=lambda kv: (-kv[1], str(kv[0])))[0][0]
            k = ("collapse", canon, r["total"])
            collapse[k] = collapse.get(k, 0) + 1
        else:
            k = ("residue", r["group"], r["id"], r["total"])
            residue[k] = residue.get(k, 0) + 1
    return valid, torn, vfam, collapse, residue, bad, alias


def pairs(counter):
    return sorted(counter.items(), key=lambda kv: (-kv[1], str(kv[0])))


def fmt_key(k):
    if len(k) == 3 and k[0] == 0 and k[1] == 1:
        return "g0 id1 key 0x%04x" % k[2]
    if len(k) == 2:
        return "g%d id%d" % k
    return str(k)


def report(recs, as_json=False):
    valid, torn, vfam, collapse, residue, bad, alias = classify(recs)
    out = {
        "records": len(recs), "valid_h2d": len(valid), "torn": len(torn),
        "torn_unreadable": bad,
        "valid_families": {" / ".join(map(str, k)): v for k, v in
                           sorted(vfam.items(), key=lambda kv: -kv[1])},
        "aliased": {" / ".join(map(str, k)): " / ".join(map(str, v))
                    for k, v in alias.items()},
        "collapse": {" / ".join(map(str, k)): v for k, v in pairs(collapse)},
        "residue": {" / ".join(map(str, k)): v for k, v in pairs(residue)},
    }
    if as_json:
        print(json.dumps(out, indent=1))
        return
    print("records %d ; valid H2D %d ; torn %d (unreadable %d)" %
          (len(recs), len(valid), len(torn), bad))
    print("\nVALID H2D families (node_len == frame total, aliases resolved):")
    for k, v in sorted(vfam.items(), key=lambda kv: -kv[1]):
        print("  %-28s n=%4d" % (fmt_key(k), v))
    if alias:
        print("\nALIASED headers folded onto a canonical message (stale first 4 bytes):")
        for k, v in sorted(alias.items()):
            print("  %-24s -> %s" % (fmt_key(k), fmt_key(v)))
    print("\nTORN records that collapse onto a valid H2D message (aggregate only):")
    for k, v in pairs(collapse):
        print("  %-64s n=%4d" % (" / ".join(map(str, k)), v))
    print("\nRESIDUE content classes (no valid H2D node; NOT trusted as H2D sends):")
    for k, v in pairs(residue):
        print("  %-64s n=%4d" % (" / ".join(map(str, k)), v))
    keys = {}
    for r in valid:
        if (r["group"], r["id"]) == (0, 1):
            k = key_of(r)
            keys[k] = keys.get(k, 0) + 1
    print("\ng0 id1 config keys in the stock boot (valid records): %d distinct" % len(keys))
    for k, v in sorted(keys.items(), key=lambda kv: -kv[1]):
        print("  key 0x%04x n=%4d" % (k, v))


# --------------------------------------------------------------------------- selftest
def _mk(group, idv, total, payload, chip=0, dst=0, alloc=1):
    """Synthetic HCC frame from the documented geometry (no vendor bytes)."""
    hdr = bytearray(12)
    hdr[0] = group & 0xF
    hdr[1] = ((dst & 0xF) << 4) | (alloc & 0xF)
    hdr[3] = chip
    hdr[4:6] = total.to_bytes(2, "little")
    hdr[6:8] = idv.to_bytes(2, "little")
    hdr[9] = dst
    hdr[10:12] = TAG.to_bytes(2, "little")
    body = bytes(hdr) + payload
    assert len(body) == total, (len(body), total)
    return body


def _rec(b, node_len, ts=1000):
    return {"ts_ms": ts, "ch": 0, "idx": 0, "prime": 0, "node": 0x84000000,
            "len": node_len, "flags": 0,
            "total": int.from_bytes(b[4:6], "little"), "group": b[0] & 0xF,
            "chip": b[3], "id": int.from_bytes(b[6:8], "little"), "b": b}


def _cfg(total, key, data_len, chip=2, token=b"\x00\x00\x00\x00\x01\x00\x00\x00"):
    payload = token + key.to_bytes(2, "little") + data_len.to_bytes(2, "little")
    payload += bytes(data_len) + b"\xff\xff\xff\xff"
    return _mk(0, 1, 12 + len(payload), payload, chip=chip)


def selftest():
    ok = 0
    # 1. valid g0 id1 key-0xd8 config event (48 B: 8 B token + key/len + 20 B + 4 x 0xff)
    cfg = _cfg(48, 0xD8, 20)
    r = _rec(cfg, 48)
    assert frame_valid(r) and key_of(r) == 0xD8 and family_key(r) == (0, 1, 0xD8)
    ok += 1
    # 2. valid g0 id 0x1d EDCA frame: 72 B, 20 zero + 40 x 0xff
    edca = _mk(0, 0x1D, 72, bytes(20) + bytes([0xFF]) * 40, chip=0)
    r2 = _rec(edca, 72)
    assert frame_valid(r2) and r2["total"] == 72
    ok += 1
    # 3. aliased header: the key-0x0127 beacon event with four stale leading bytes
    beacon = _cfg(184, 0x127, 156, chip=1)
    stale = bytes.fromhex("0c1830") + beacon[3:]
    rec_v = _rec(beacon, 184)
    rec_s = _rec(stale, 184)
    assert frame_valid(rec_v) and frame_valid(rec_s) and rec_s["group"] == 12
    v, t, vf, col, res, bad, alias = classify([rec_v, rec_v, rec_s])
    assert len(v) == 3 and (12, 1) in alias and alias[(12, 1)] == (0, 1, 0x127), alias
    assert (0, 1, 0x127) in vf and (12, 1) not in vf, vf
    ok += 1
    # 4. residue: the firmware-D2H HEARTBEAT content at a torn node (node len != 22)
    hb = _mk(4, 4, 22, b"HEARTBEAT\x00", chip=0)
    rec_hb = _rec(hb, 72)
    v, t, vf, col, res, bad, alias = classify([rec_hb])
    assert len(v) == 0 and len(t) == 1 and any(k[0] == "residue" for k in res), res
    ok += 1
    # 5. mixed aggregate classifies deterministically and collapses torn content
    recs = [_rec(cfg, 48, 1), _rec(edca, 72, 2), _rec(beacon, 184, 3),
            _rec(beacon, 72, 4), rec_hb]       # 4th: torn beacon (node len 72)
    v, t, vf, col, res, bad, alias = classify(recs)
    assert (len(v), len(t)) == (3, 2), (len(v), len(t))
    assert any(k[0] == "collapse" and k[2] == 184 for k in col), col
    assert any(k[0] == "residue" for k in res), res
    ok += 1
    print("selftest: %d/5 PASS" % ok)


def main(argv):
    if "--selftest" in argv:
        selftest()
        return 0
    if len(argv) < 2:
        print(__doc__)
        return 2
    report(parse(argv[1]), as_json="--json" in argv)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
