#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""r1_band_model.py - host model for lane wifi-r1-band-diff (task-183).

WHAT THIS PROVES
----------------
The firmware module channel context word at ctx+0x10 (4 bytes) is written by
two different H2D messages, in this fixed order on a clean-room AP start:

  g0 id1 key 0x0124  "ap_chan"      worker CA 0x841B8
      validates (band, channel) against the per-band tables, then stores
      ctx+0x10 = body[2] (channel), ctx+0x11 = body[0] (band),
      ctx+0x12 = body[1] (field),     ctx+0x13 = table index
      body = {band, vap_id, channel, 0}
      (committed: docs/incoming/wifi-fw-chan-worker/TABLES.md T2/T4)

  g0 id1 key 0x0065  "ap_vap_start" worker CA 0x46394
      stores ctx+0x11 = body[2], ctx+0x12 = body[3], u32 body[4..7] -> ctx+0x444
      body = {channel, state, cfg_a, cfg_b, cfg_word u32}
      vendor fills data[2] from vap+0x11 (band) and data[3] from vap+0x12
      (committed: docs/incoming/wifi-beacon-static/TABLES.md T5,
       docs/incoming/wifi-fw-rf-programme/REPORT.md 6.1,
       docs/incoming/wifi-vap-start/TABLES.md T6)

The clean-room provider tree before this lane's patch left cfg_a = cfg_b = 0,
so the second store changes the successful band-1 ch40 word
0x01000128 (28 01 00 01) into 0x01000028 (28 00 00 01) -- the exact word the
Lead read live at the CW-2 rung and the R1 restart wait refused on.

The model builds the real wire frames (g0 id1 header layout at the decoded
offsets), applies the two decoded store maps in order, runs the fired old
gate and the decoded gate, and can derive cfg_a/cfg_b from the actual patched
source file.  No vendor byte is copied: only decoded layout/address facts.

Usage:
  ./r1_band_model.py --selftest
  ./r1_band_model.py --source <patched hi5622_vapscan_provider.c> \
                     [--base-source <unpatched hi5622_vapscan_provider.c>]
"""
import argparse
import re
import sys

# ---------------------------------------------------------------------------
# decoded constants (committed by the fw-chan-worker / vap-start / beacon
# lanes; no vendor byte is reproduced here)
# ---------------------------------------------------------------------------
BAND_TABLES = {
    0: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14],
    1: [36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124,
        128, 132, 136, 140, 144, 149, 153, 157, 161, 165, 184, 188, 192, 196],
}
KEY_CHAN = 0x0124
KEY_VAP_START = 0x0065
G0_HDR_LEN = 12
G0_TOKEN_LEN = 8
G0_OVERHEAD = 28          # hdr[12] + token[8] + key[2] + len[2] + pad[4]
G0_BODY_OFF = 24
G0_KEY_OFF = 20
G0_LEN_OFF = 22
HCC_TAG = 0x5a5a

# the two bench words this lane explains (Lead's fire logs, [V*])
CW0_WAIT_WORD = 0x01000128     # first start, wait PASS (CW-0 11:30)
CW2_LIVE_WORD = 0x01000028     # live read of the same ctx (CW-2 12:00);
                               # the R1 restart then refused on this word

checks = []


def check(name, cond, detail=""):
    checks.append((name, bool(cond), detail))


def put16(buf, off, val):
    buf[off] = val & 0xff
    buf[off + 1] = (val >> 8) & 0xff


def get16(buf, off):
    return buf[off] | (buf[off + 1] << 8)


def g0_cfg_event(key, body, module=0):
    """g0 id1 config event: hdr[12] token[8] key u16 len u16 body pad[4]."""
    total = G0_OVERHEAD + len(body)
    b = bytearray(total)
    b[0] = 0x00                      # HCC group = chip
    b[1] = 0x01                      # alloc nibble 1 | dst 0 << 4
    b[2] = 0x00
    b[3] = module & 0x0f             # module index -> dispatcher lookup
    put16(b, 4, total)
    put16(b, 6, 1)                   # HCC id = 1 (config event)
    b[8] = 0
    b[9] = 0
    put16(b, 10, HCC_TAG)
    b[16] = 0x01                     # inherited g0 token pattern
    put16(b, G0_KEY_OFF, key)
    put16(b, G0_LEN_OFF, len(body))
    b[G0_BODY_OFF:G0_BODY_OFF + len(body)] = body
    b[G0_BODY_OFF + len(body):total] = b"\xff" * 4   # inherited pad
    return bytes(b)


def build_ap_chan(band, vap_id, channel, module=0):
    """ap_chan frame body {band, vap_id, channel, 0} (key 0x0124)."""
    return g0_cfg_event(KEY_CHAN, bytes([band & 0xff, vap_id & 0xff,
                                         channel & 0xff, 0x00]), module)


def build_vap_start(channel, cfg_a, cfg_b, state=1, cfg_word=0, module=0):
    """g0 id1 key 0x0065 12-byte body {channel, state, cfg_a, cfg_b, u32}."""
    body = bytearray(12)
    body[0] = channel & 0xff
    body[1] = state & 0xff
    body[2] = cfg_a & 0xff
    body[3] = cfg_b & 0xff
    body[4] = cfg_word & 0xff
    body[5] = (cfg_word >> 8) & 0xff
    body[6] = (cfg_word >> 16) & 0xff
    body[7] = (cfg_word >> 24) & 0xff
    return g0_cfg_event(KEY_VAP_START, bytes(body), module)


def chan_index(band, channel):
    """CA 0x10172C: 0-based per-band table index, None = refused (0x3E8)."""
    tbl = BAND_TABLES.get(band)
    if tbl is None:
        return None
    try:
        return tbl.index(channel)
    except ValueError:
        return None


def worker_0124(body):
    """Decoded ap_chan worker store map.  Returns (word, rc); rc != 0 = no store."""
    band, field, channel = body[0], body[1], body[2]
    idx = chan_index(band, channel)
    if idx is None:
        return None, 0x3E8
    word = channel | (band << 8) | (field << 16) | (idx << 24)
    return word, 0


def worker_0065(ctx_word, body):
    """Decoded VAP-start worker store map (only the ctx word bytes)."""
    raw = bytearray(ctx_word.to_bytes(4, "little"))
    raw[1] = body[2]                 # ctx+0x11
    raw[2] = body[3]                 # ctx+0x12
    return int.from_bytes(raw, "little")


def gate_fired_old(word, band, channel):
    """The predicate in the fired module bc1cc0ea (hi5622_beacon5g.c):
    channel at +0x10, band at +0x11, byte +0x13 == 1 ("valid")."""
    return ((word & 0xff) == channel and
            ((word >> 8) & 0xff) == band and
            ((word >> 24) & 0xff) == 1)


def gate_decoded(word, band, channel):
    """The decoded store map as a predicate: channel, band, table index."""
    idx = chan_index(band, channel)
    return (idx is not None and
            (word & 0xff) == channel and
            ((word >> 8) & 0xff) == band and
            ((word >> 24) & 0xff) == idx)


# ---------------------------------------------------------------------------
# source model: derive cfg_a / cfg_b from the actual provider source
# ---------------------------------------------------------------------------
def source_model(path):
    """Return {'cfg_a': 'band'|'zero'|'unset', 'cfg_b': 0|'unset', 'ok': bool}."""
    try:
        src = open(path, "r", encoding="utf-8", errors="replace").read()
    except OSError as exc:
        return {"ok": False, "error": str(exc)}
    m = re.search(r"static int vapscan_vap_start\b.*?\n\}", src, re.S)
    if not m:
        return {"ok": False, "error": "vapscan_vap_start() not found"}
    seg = m.group(0)
    out = {"ok": True,
           "cfg_a": "unset",   # vsf_vap_start_defaults() zero-fills
           "cfg_b": "unset"}
    if re.search(r"gp\.cfg_a\s*=\s*p->band\s*;", seg):
        out["cfg_a"] = "band"
    elif re.search(r"gp\.cfg_a\s*=", seg):
        out["cfg_a"] = "other"
    if re.search(r"gp\.cfg_b\s*=\s*0u?\s*;", seg):
        out["cfg_b"] = 0
    elif re.search(r"gp\.cfg_b\s*=", seg):
        out["cfg_b"] = "other"
    return out


def simulate_start(word, band, channel, cfg_a, cfg_b, fresh_store=True):
    """One clean-room AP start: ap_chan -> wait -> ap_ssid/ap_cfg -> vap start.

    word = the ctx word already in firmware RAM before this start (0 on a
    fresh boot; the previous start's persisted word on a restart without a
    module reload).  fresh_store=False models a start whose 0x0124 worker
    never stored (e.g. an already-faulted chip): the wait then reads the
    persisted word.
    """
    if fresh_store:
        frame = build_ap_chan(band, 0, channel)
        assert get16(frame, G0_KEY_OFF) == KEY_CHAN
        word, rc = worker_0124(frame[G0_BODY_OFF:G0_BODY_OFF + 4])
        if rc:
            return word, rc
    vs = build_vap_start(channel, cfg_a, cfg_b)
    assert get16(vs, G0_KEY_OFF) == KEY_VAP_START
    word = worker_0065(word, vs[G0_BODY_OFF:G0_BODY_OFF + 12])
    return word, 0


# ---------------------------------------------------------------------------
# checks
# ---------------------------------------------------------------------------
def run(source=None, base_source=None):
    # ---- frame layout (decoded g0 id1 offsets) ----
    ap = build_ap_chan(1, 0, 40)
    check("ap_chan frame total = 28 + 4 = 32 B", len(ap) == 32, str(len(ap)))
    check("ap_chan key at wire+0x14 = 0x0124", get16(ap, 20) == 0x0124,
          hex(get16(ap, 20)))
    check("ap_chan body length at wire+0x16 = 4", get16(ap, 22) == 4)
    check("ap_chan body at wire+0x18 = {1, 0, 40, 0}",
          ap[24:28] == bytes([1, 0, 40, 0]), ap[24:28].hex())
    check("ap_chan inherited pad = ff ff ff ff", ap[28:32] == b"\xff" * 4)
    vs = build_vap_start(40, 0, 0)
    check("vap_start frame total = 28 + 12 = 40 B", len(vs) == 40, str(len(vs)))
    check("vap_start key at wire+0x14 = 0x0065", get16(vs, 20) == 0x0065)
    check("vap_start body length at wire+0x16 = 12", get16(vs, 22) == 12)
    check("vap_start body[0..3] = {channel, state, cfg_a, cfg_b}",
          vs[24:28] == bytes([40, 1, 0, 0]), vs[24:28].hex())

    # ---- the word arithmetic the brief gets wrong ----
    check("0x01000128 is little-endian bytes 28 01 00 01",
          CW0_WAIT_WORD.to_bytes(4, "little") == b"\x28\x01\x00\x01")
    check("0x01000028 is little-endian bytes 28 00 00 01 (NOT 28 00 01 00)",
          CW2_LIVE_WORD.to_bytes(4, "little") == b"\x28\x00\x00\x01" and
          CW2_LIVE_WORD.to_bytes(4, "little") != b"\x28\x00\x01\x00")
    check("the two words differ in exactly the +0x11 byte (XOR 0x100)",
          CW0_WAIT_WORD ^ CW2_LIVE_WORD == 0x00000100)
    check("+0x13 stays 1 = ch40's band-1 table index (ch40 -> index 1)",
          ((CW0_WAIT_WORD >> 24) & 0xff) == 1 and chan_index(1, 40) == 1)

    # ---- ap_chan store map and validation ----
    w, rc = worker_0124(bytes([1, 0, 40, 0]))
    check("0x0124 ch40 band1 store = 0x01000128", rc == 0 and w == CW0_WAIT_WORD,
          "%r %r" % (w, rc))
    w, rc = worker_0124(bytes([0, 0, 40, 0]))
    check("0x0124 band0 ch40 is REFUSED (0x3E8, no store)", rc == 0x3E8 and w is None,
          "%r %r" % (w, rc))
    w, _ = worker_0124(bytes([0, 0, 6, 0]))
    check("0x0124 band0 ch6 store = 0x05000006", w == 0x05000006, hex(w or 0))
    w, _ = worker_0124(bytes([1, 0, 36, 0]))
    check("0x0124 band1 ch36 store = 0x00000124", w == 0x00000124, hex(w or 0))

    # ---- the VAP-start clobber (the root cause) ----
    pre = worker_0065(CW0_WAIT_WORD, bytes([40, 1, 0, 0]) + b"\x00" * 8)
    check("VAP start with cfg_a=0/cfg_b=0 clobbers 0x01000128 -> 0x01000028",
          pre == CW2_LIVE_WORD, hex(pre))
    post = worker_0065(CW0_WAIT_WORD, bytes([40, 1, 1, 0]) + b"\x00" * 8)
    check("VAP start with cfg_a=band/cfg_b=0 keeps 0x01000128", post == CW0_WAIT_WORD,
          hex(post))
    w36 = worker_0065(0x00000124, bytes([36, 1, 0, 0]) + b"\x00" * 8)
    check("VAP start clobbers ch36 0x00000124 -> 0x00000024 (invisible: gate "
          "aborts before it)", w36 == 0x00000024, hex(w36))
    w6 = worker_0065(0x05000006, bytes([6, 1, 0, 0]) + b"\x00" * 8)
    check("VAP start leaves a band-0 word unchanged (0x05000006)", w6 == 0x05000006,
          hex(w6))

    # ---- gates ----
    check("fired old gate PASSES 0x01000128 (band1 ch40 idx1)",
          gate_fired_old(CW0_WAIT_WORD, 1, 40))
    check("fired old gate REFUSES 0x01000028 (the R1 line)",
          not gate_fired_old(CW2_LIVE_WORD, 1, 40))
    check("decoded gate REFUSES 0x01000028 as well",
          not gate_decoded(CW2_LIVE_WORD, 1, 40))
    check("decoded gate PASSES 0x01000128", gate_decoded(CW0_WAIT_WORD, 1, 40))
    check("decoded gate PASSES ch36 0x00000124 (index 0)",
          gate_decoded(0x00000124, 1, 36))
    check("fired old gate REFUSES ch36 0x00000124 (the G6b false negative - "
          "why the clobber never ran on ch36)",
          not gate_fired_old(0x00000124, 1, 36))

    # ---- start scenarios ----
    word, rc = simulate_start(0, 1, 40, 0, 0, fresh_store=True)
    check("pre-fix first start ends at 0x01000028 (the CW-2 live word)",
          rc == 0 and word == CW2_LIVE_WORD, hex(word or 0))
    check("pre-fix restart wait on the stale word REFUSES",
          not gate_fired_old(word, 1, 40))
    word, _ = simulate_start(0, 1, 40, 1, 0, fresh_store=True)
    check("post-fix first start ends at 0x01000128 (band byte survives)",
          word == CW0_WAIT_WORD, hex(word))
    check("post-fix restart wait on the persisted word PASSES",
          gate_fired_old(word, 1, 40))
    word, _ = simulate_start(CW0_WAIT_WORD, 1, 40, 1, 0, fresh_store=False)
    check("post-fix restart with no fresh 0x0124 store still reads band 1",
          word == CW0_WAIT_WORD, hex(word))
    word, _ = simulate_start(CW2_LIVE_WORD, 1, 40, 0, 0, fresh_store=False)
    check("pre-fix restart with no fresh store reads 0x01000028 (the R1 case)",
          word == CW2_LIVE_WORD, hex(word))
    check("second start ap_chan frame is byte-identical to the first start's",
          build_ap_chan(1, 0, 40) == build_ap_chan(1, 0, 40))

    # ---- source-level verification of the actual patch ----
    if source is not None:
        sm = source_model(source)
        check("patched source: vapscan_vap_start() found", sm.get("ok"), str(sm))
        if sm.get("ok"):
            check("patched source: cfg_a derives from p->band",
                  sm["cfg_a"] == "band", str(sm["cfg_a"]))
            check("patched source: cfg_b derives 0", sm["cfg_b"] == 0,
                  str(sm["cfg_b"]))
            cfg_a = 1 if sm["cfg_a"] == "band" else 0
            cfg_b = 0 if sm["cfg_b"] == 0 else 0
            word, _ = simulate_start(0, 1, 40, cfg_a, cfg_b)
            check("source-driven model ends at 0x01000128", word == CW0_WAIT_WORD,
                  hex(word))
    if base_source is not None:
        bm = source_model(base_source)
        check("base source: cfg_a is unset/zero (the bug)", bm.get("ok") and
              bm["cfg_a"] != "band", str(bm.get("cfg_a")))
    return checks


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--selftest", action="store_true",
                    help="run the built-in model checks (default)")
    ap.add_argument("--source", help="patched hi5622_vapscan_provider.c")
    ap.add_argument("--base-source", help="unpatched provider (negative control)")
    args = ap.parse_args()
    if not (args.selftest or args.source or args.base_source):
        args.selftest = True
    checks[:] = []
    result = run(source=args.source, base_source=args.base_source)
    failed = [n for (n, ok, _) in result if not ok]
    for name, ok, detail in result:
        print(("PASS  " if ok else "FAIL  ") + name +
              (("   [" + detail + "]") if (detail and not ok) else ""))
    print()
    print("R1-BAND-MODEL: %d/%d PASS" % (len(result) - len(failed), len(result)))
    if args.source:
        print("source:      " + args.source)
    if args.base_source:
        print("base source: " + args.base_source)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
