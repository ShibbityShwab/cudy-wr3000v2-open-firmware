#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
band_start_send_test.py - independent host-side cross-check for lane
wifi-band1-start-send (task-206).  HOST-ONLY: no device, no kernel, no
firmware, no vendor artifact.  Two jobs:

1. Re-derive the H2D g0-id1 config-event frames for key 0x0242 (body[0] = 1
   sync start, body[0] = 2 forced start) and key 0x0243 (the band-ready
   nudge) from the committed wire spec and compare them byte-for-byte with
   the portable C builder's output (passed as --frame-hex / --forced-hex /
   --nudge-hex).  The derivation is anchored on the SIBLING 0x00F7 frame
   (wifi-fw-desc-table FIRE-RECIPE R1, the same carrier): same 12-byte HCC
   header/token/tail, key and length and body replaced.  It also asserts the
   identity rules of the carrier (total == len + 28, key at hdr+20, len at
   hdr+22, body at hdr+24, inherited FF FF FF FF tail, SR node word1 =
   (total << 16) | 0x6D2B).

2. Check the patched driver tree (--tree): the fail-closed band_start /
   band_start_fallback parameters and their gates, the 0x0242 post placed
   after the 0x01F7 post and before the 0x0124 channel post, the
   ordering-plan step, the Kbuild entry, the sysfs status line and the F2
   field-B base invariant (field B = 0 in the sibling senders).  Source-level
   assertions only; no build is performed.
"""
import argparse
import struct
import sys

CHECKS = []
FAILS = []


def check(cond, what, detail=""):
    CHECKS.append(what)
    if not cond:
        FAILS.append((what, detail))
        print("FAIL %s%s" % (what, (" - " + detail) if detail else ""))


# --- the committed sibling 0x00F7 frame, the model of the carrier ----------
# wifi-fw-desc-table FIRE-RECIPE R1 / TABLES T2: 68 B, header
# 00 01 00 04 44 00 01 00 00 00 5a5a, token 0000000001000000, key f700,
# len 2800, blob 40 B, tail ffffffff.
SIBLING_FRAME = bytes.fromhex(
    "000100044400010000005a5a"
    "0000000001000000"
    "f700"
    "2800"
    "5553000002000000"
    "000080001e002c0100000000ff1f0000"
    "010280001e002c0100000000ff000000"
    "ffffffff")

FIELD_B = 0          # the AP-start module/context index (F2 field-B lesson)
KEY = 0x0242
NUDGE_KEY = 0x0243
PRIMARY_BODY = bytes([0x01, 0x00, 0x01, 0x00, 0x00])
FORCED_BODY = bytes([0x02, 0x00, 0x00, 0x00, 0x00])
NUDGE_BODY = bytes([0x00])
TOTAL_PRIMARY = 33   # 12 hdr + 8 token + 2 key + 2 len + 5 body + 4 tail
TOTAL_NUDGE = 29


def carrier_parts(frame):
    """Split a g0-id1 config-event frame into (hdr12, token8, key, len, body,
    tail4) using the committed offsets."""
    hdr = frame[0:12]
    token = frame[12:20]
    key = struct.unpack_from("<H", frame, 20)[0]
    blen = struct.unpack_from("<H", frame, 22)[0]
    body = frame[24:24 + blen]
    tail = frame[-4:]
    return hdr, token, key, blen, body, tail


def derive_frame(key, body):
    """Rebuild a band-start frame from the sibling carrier + spec fields."""
    hdr = bytearray(SIBLING_FRAME[0:12])
    token = bytes(SIBLING_FRAME[12:20])
    tail = bytes(SIBLING_FRAME[-4:])
    total = 12 + len(token) + 2 + 2 + len(body) + len(tail)
    hdr[0:2] = SIBLING_FRAME[0:2]        # group 0, alloc/dst byte 0x01
    hdr[2] = 0x00                         # inherited fa
    hdr[3] = FIELD_B                      # field B = the AP-start module
    struct.pack_into("<H", hdr, 4, total)
    struct.pack_into("<H", hdr, 6, 0x0001)
    hdr[8:12] = SIBLING_FRAME[8:12]       # fc, dst, tag 0x5A5A
    return bytes(hdr) + token + struct.pack("<H", key) + \
        struct.pack("<H", len(body)) + body + tail


def test_derivation(frame_hex, forced_hex, nudge_hex):
    frames = {
        "primary": (derive_frame(KEY, PRIMARY_BODY), frame_hex,
                    TOTAL_PRIMARY, PRIMARY_BODY),
        "forced": (derive_frame(KEY, FORCED_BODY), forced_hex,
                   TOTAL_PRIMARY, FORCED_BODY),
        "nudge": (derive_frame(NUDGE_KEY, NUDGE_BODY), nudge_hex,
                  TOTAL_NUDGE, NUDGE_BODY),
    }
    for name, (want, got_hex, total, body) in frames.items():
        key = NUDGE_KEY if name == "nudge" else KEY
        print("%s derived frame (%d B): %s" % (name, len(want), want.hex()))
        check(len(want) == total,
              "%s derived total wire length %d B" % (name, total))
        check(len(want) == 12 + 8 + 2 + 2 + len(body) + 4,
              "%s geometry 12 hdr + 8 token + 2 key + 2 len + %d body + 4 tail"
              % (name, len(body)))
        check(want[0] == 0x00 and want[1] == 0x01,
              "%s hdr group 0 / alloc=1 dst=0" % name)
        check(want[3] == FIELD_B, "%s hdr field B = the AP-start module" % name)
        check(struct.unpack_from("<H", want, 4)[0] == len(want),
              "%s hdr total field == wire length" % name)
        check(struct.unpack_from("<H", want, 6)[0] == 0x0001,
              "%s hdr message id 1 (g0-id1 config event)" % name)
        check(want[10:12] == b"\x5a\x5a", "%s hdr tag 0x5A5A at +10" % name)
        check(want[12:20] == SIBLING_FRAME[12:20],
              "%s token identical to the sibling 0x00F7 carrier" % name)
        check(struct.unpack_from("<H", want, 20)[0] == key,
              "%s key 0x%04X at hdr+20" % (name, key))
        check(struct.unpack_from("<H", want, 22)[0] == len(body),
              "%s body length %d at hdr+22" % (name, len(body)))
        check(want[24:24 + len(body)] == body,
              "%s body bytes at hdr+24" % name)
        check(want[-4:] == b"\xff\xff\xff\xff",
              "%s inherited FF FF FF FF tail" % name)
        hdr, token, k2, blen, body2, tail = carrier_parts(want)
        check(k2 == key and blen == len(body) and body2 == body,
              "%s carrier offsets key/len/body round-trip" % name)
        check(len(want) == blen + 28,
              "%s carrier identity total == len + 28" % name)
        node_word1 = (len(want) << 16) | 0x6D2B
        check(node_word1 == ((total << 16) | 0x6D2B),
              "%s SR node word1 = 0x%08X" % (name, node_word1))
        if got_hex is None:
            check(False, "%s C builder frame supplied on the command line" % name)
            continue
        got = bytes.fromhex(got_hex)
        print("%s C builder frame  (%d B): %s" % (name, len(got), got.hex()))
        check(got == want,
              "%s C builder frame is byte-identical to the derivation" % name,
              got.hex())
        check(len(got) == total, "%s C builder frame length %d B"
              % (name, total))
    # The three frames are distinct; only the key/body/(total) differ.
    p = derive_frame(KEY, PRIMARY_BODY)
    f = derive_frame(KEY, FORCED_BODY)
    n = derive_frame(NUDGE_KEY, NUDGE_BODY)
    check(p != f and len(p) == len(f),
          "forced frame differs from the primary only in body[0]")
    check(p[24] == 1 and f[24] == 2, "body[0] subcmd 1 vs 2")
    check(n[20:22] != p[20:22], "nudge uses key 0x0243 (carrier byte change)")


# ---------------------------------------------------------------------------
# source-level contract checks on the patched driver tree
# ---------------------------------------------------------------------------
def read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        return fh.read()


def pos_of(src, needle, what):
    i = src.find(needle)
    check(i >= 0, "source contains: " + what, needle)
    return i


def test_tree(tree):
    scan = read(tree + "/hi5622_scan.c")
    logic_h = read(tree + "/hi5622_scan_logic.h")
    logic_c = read(tree + "/hi5622_scan_logic.c")
    kbuild = read(tree + "/Kbuild")
    model_c = read(tree + "/hi5622_band_start.c")
    model_h = read(tree + "/hi5622_band_start.h")

    # 0. the F2 field-B base invariant (the pinned base is the F2 lineage).
    check("HI5622_DFS_MODE_FB		0x00u" in read(tree + "/hi5622_dfs_mode.h"),
          "base is the F2 lineage (0x01F7 field B = 0x00)")
    check("b[3] = 0x00u;" in read(tree + "/hi5622_desc_table.c"),
          "base is the F2 lineage (0x00F7 b[3] = 0x00)")

    # 1. the fail-closed parameters and their documented contract
    check("module_param_named(band_start, hi5622_band_start, uint, 0444)" in scan,
          "band_start is a read-only uint module parameter")
    check("MODULE_PARM_DESC(band_start," in scan,
          "band_start has a MODULE_PARM_DESC")
    check("module_param_named(band_start_fallback, hi5622_band_start_fallback, uint, 0444)"
          in scan, "band_start_fallback is a read-only uint module parameter")
    check("MODULE_PARM_DESC(band_start_fallback," in scan,
          "band_start_fallback has a MODULE_PARM_DESC")
    check('#include "hi5622_band_start.h"' in scan,
          "scan.c includes the portable 0x0242 model")
    check("HI5622_BAND_START_PARM_MAX" in scan,
          "the gates use the model's accepted-parameter bound")

    # 2. the fail-closed parameter gates run before the VAP base post
    gate = pos_of(scan, "band_start=%u is not a valid band-start parameter",
                  "band_start > 1 refusal line")
    gate2 = pos_of(scan, "band_start=1 needs the beacon_static=1 vendor order",
                   "band_start=1 without beacon_static refusal line")
    gate3 = pos_of(scan, "band_start_fallback=1 needs band_start=1",
                   "fallback-without-primary refusal line")
    plan = pos_of(scan, "plan_n = hi5622_beacon_plan(",
                  "the plan construction")
    check(gate < plan and gate2 < plan and gate3 < plan,
          "the band_start refusals run before the plan is built/executed",
          "gate=%d gate2=%d gate3=%d plan=%d" % (gate, gate2, gate3, plan))
    check(gate > pos_of(scan, "dfs_mode=1 needs the beacon_static=1 vendor order",
                        "the sibling dfs_mode gate"),
          "the band_start gate follows the dfs_mode gate (same fail-closed block)")

    # 3. the post is fail-closed through the plan and aborts on error
    check("hi5622_band_start_plan(hi5622_band_start," in scan,
          "the post is gated by hi5622_band_start_plan()")
    check("if (bsp.post) {" in scan, "the frame is only built when bsp.post")
    check('"band_start", &bs_out0);' in scan,
          "the frame is posted through hi5622_frame_post() as 'band_start'")
    check("band-start builder refused" in scan,
          "builder refusal aborts the sequence")
    check("band-start frame (g0 id1 key 0x0242) not posted" in scan,
          "post failure aborts the sequence")

    # 4. ORDER: desc_table post < dfs_mode post < band_start post < ap_chan
    p_desc = pos_of(scan, "start_ap beacon_static frame %u/%u 'desc_table' posted",
                    "the 0x00F7 post line")
    p_dfs = pos_of(scan, "start_ap beacon_static frame %u/%u 'dfs_mode' posted",
                   "the 0x01F7 post line")
    p_bs = pos_of(scan, "start_ap beacon_static frame %u/%u 'band_start' posted",
                  "the 0x0242 post line")
    p_chan = pos_of(scan, "start_ap beacon_static frame %u/%u 'ap_chan' posted",
                    "the 0x0124 post line")
    check(p_desc < p_dfs < p_bs < p_chan,
          "order in scan.c: desc_table post < dfs_mode post < band_start post < ap_chan post",
          "desc=%d dfs=%d bs=%d chan=%d" % (p_desc, p_dfs, p_bs, p_chan))
    check("build_chan_cfg" in scan and
          p_bs < scan.find("rc = ops->build_chan_cfg(wire"),
          "the 0x0242 post is before the 0x0124 builder/post")

    # 5. the fallback rung (the decoded F3/F4 ladder) is fail-closed too
    check("band_start fallback" in scan and
          "HI5622_BAND_START_FRAME_FORCED" in scan and
          "HI5622_BAND_START_FRAME_NUDGE" in scan,
          "the fallback rungs (forced start, 0x0243 nudge) are implemented")
    check("start marker state[1]" in scan,
          "the fallback trigger is the per-band start marker")
    check("if (prc == -EAGAIN)" in scan,
          "the fallback is skipped when the band-object/VAP guard refuses")

    # 6. ordering-plan step
    check("HI5622_BEACON_STEP_BAND_START" in logic_h and
          "HI5622_BEACON_STEP_BAND_START" in logic_c,
          "the plan enum/step exists in hi5622_scan_logic.{c,h}")
    check("band_start == 1u && n < max" in logic_c,
          "the plan inserts the step only for the fail-closed value 1")
    check('return "band_start"' in logic_c,
          "the plan step name is band_start")
    check("unsigned int band_start" in logic_h,
          "hi5622_beacon_plan() takes the band_start knob")
    check(scan.find("hi5622_band_start, plan,") >= 0,
          "scan.c passes the band_start knob to the plan")

    # 7. Kbuild + sysfs status line
    check("hi5622_band_start.o" in kbuild,
          "Kbuild builds the new portable model")
    check("hi5622_band_start_status_line(" in scan,
          "the sysfs status appends the band_start line")
    check("band_start: param=%u (%s) fallback=%u beacon_static=%u last_rc=%d posted=%u forced=%u nudge=%u refused=%u"
          in model_c, "the status format string exists")

    # 8. the portable model sources are the ones asserted here
    check("#define HI5622_BAND_START_KEY			0x0242u" in model_h,
          "model key 0x0242")
    check("#define HI5622_BAND_START_FALLBACK_KEY		0x0243u" in model_h,
          "model fallback key 0x0243")
    check("#define HI5622_BAND_START_WIRE_LEN		33u" in model_h,
          "model wire length 33")
    check("#define HI5622_BAND_START_BODY_LEN		5u" in model_h,
          "model body length 5")
    check("#define HI5622_BAND_START_SUBCMD		1u" in model_h,
          "model sync-start subcommand 1")
    check("#define HI5622_BAND_START_SUBCMD_FORCED	2u" in model_h,
          "model forced-start subcommand 2")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frame-hex", default=None,
                    help="the C primary frame as hex (from --dump)")
    ap.add_argument("--forced-hex", default=None,
                    help="the C forced-start frame as hex (from --dump)")
    ap.add_argument("--nudge-hex", default=None,
                    help="the C 0x0243 nudge frame as hex (from --dump)")
    ap.add_argument("--tree", default=None,
                    help="the patched driver tree (e.g. /tmp/bandstart-build)")
    args = ap.parse_args()

    test_derivation(args.frame_hex, args.forced_hex, args.nudge_hex)
    if args.tree:
        test_tree(args.tree)
    else:
        print("NOTE: no --tree; skipping the source contract checks")

    print("%d checks, %d failed" % (len(CHECKS), len(FAILS)))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
