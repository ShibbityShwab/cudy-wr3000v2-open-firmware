#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
dfs_mode_send_test.py - independent host-side cross-check for lane
wifi-dfs-mode-send (task-190).  HOST-ONLY: no device, no kernel, no firmware,
no vendor artifact.  Two jobs:

1. Re-derive the H2D g0-id1 config-event frame for key 0x01F7, len 1,
   body {0x03} from the committed wire spec and compare it byte-for-byte with
   the portable C builder's output (passed as --frame-hex).  The derivation
   is anchored on the SIBLING 0x00F7 frame (wifi-fw-desc-table FIRE-RECIPE
   R1, the same carrier): same 12-byte HCC header/token/tail, key and length
   and body replaced.  It also asserts the identity rules of the carrier
   (total == len + 28, key at hdr+20, len at hdr+22, body at hdr+24,
   inherited FF FF FF FF tail, SR node word1 = (total << 16) | 0x6D2B).

2. Check the patched driver tree (--tree): the dfs_mode parameter and its
   fail-closed gate, the 0x01F7 post placed after the 0x00F7 post and before
   the 0x0124 channel post, the ordering-plan step, the Kbuild entry and the
   sysfs status line.  Source-level assertions only; no build is performed.
"""
import argparse
import re
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

KEY = 0x01F7
BODY = bytes([0x03])
MODE = 3
TOTAL = 29          # 12 hdr + 8 token + 2 key + 2 len + 1 body + 4 tail


def carrier_parts(frame):
    """Split a g0-id1 config-event frame into (hdr12, token8, key, len, body,
    tail4) using the committed offsets."""
    assert len(frame) == TOTAL
    return (frame[0:12], frame[12:20],
            struct.unpack_from("<H", frame, 20)[0],
            struct.unpack_from("<H", frame, 22)[0],
            frame[24:24 + struct.unpack_from("<H", frame, 22)[0]],
            frame[-4:])


def derive_frame():
    """Rebuild the 0x01F7 frame from the sibling carrier + the spec fields."""
    hdr = bytearray(SIBLING_FRAME[0:12])
    token = bytes(SIBLING_FRAME[12:20])
    tail = bytes(SIBLING_FRAME[-4:])
    body = BODY
    total = 12 + len(token) + 2 + 2 + len(body) + len(tail)
    assert total == TOTAL
    hdr[0:2] = SIBLING_FRAME[0:2]        # group 0, alloc/dst byte 0x01
    hdr[2:4] = SIBLING_FRAME[2:4]        # fa/fb inherited from the carrier
    struct.pack_into("<H", hdr, 4, total)  # total length changes
    struct.pack_into("<H", hdr, 6, 0x0001)  # g0 id1
    hdr[8:12] = SIBLING_FRAME[8:12]      # fc, dst, tag 0x5A5A
    return bytes(hdr) + token + struct.pack("<H", KEY) + \
        struct.pack("<H", len(body)) + body + tail


def test_derivation(frame_hex):
    want = derive_frame()
    print("derived frame (%d B): %s" % (len(want), want.hex()))
    check(len(want) == 29, "derived total wire length 29 B")
    check(len(want) == 12 + 8 + 2 + 2 + 1 + 4,
          "geometry 12 hdr + 8 token + 2 key + 2 len + 1 body + 4 tail")
    check(want[0] == 0x00 and want[1] == 0x01,
          "hdr group 0 / alloc=1 dst=0")
    check(struct.unpack_from("<H", want, 4)[0] == len(want),
          "hdr total field == wire length (29)")
    check(struct.unpack_from("<H", want, 6)[0] == 0x0001,
          "hdr message id 1 (g0-id1 config event)")
    check(want[10:12] == b"\x5a\x5a", "hdr tag 0x5A5A at +10")
    check(want[12:20] == SIBLING_FRAME[12:20],
          "token identical to the sibling 0x00F7 carrier")
    check(struct.unpack_from("<H", want, 20)[0] == 0x01F7,
          "key 0x01F7 at hdr+20")
    check(struct.unpack_from("<H", want, 22)[0] == 1,
          "body length 1 at hdr+22")
    check(want[24] == MODE, "body[0] mode 3 at hdr+24")
    check(want[-4:] == b"\xff\xff\xff\xff",
          "inherited FF FF FF FF tail")
    check(want[0:4] == SIBLING_FRAME[0:4],
          "header fa/fb inherited from the sibling carrier")
    check(want[-4:] == SIBLING_FRAME[-4:],
          "tail inherited from the sibling carrier")
    check(want[20:24] != SIBLING_FRAME[20:24],
          "key+len differ from the sibling (f7 00 28 00 -> f7 01 01 00)")

    hdr, token, key, blen, body, tail = carrier_parts(want)
    check(key == KEY and blen == 1 and body == b"\x03",
          "carrier offsets key/len/body round-trip")
    check(len(want) == blen + 28,
          "carrier identity total == len + 28")
    node_word1 = (len(want) << 16) | 0x6D2B
    check(node_word1 == 0x001D6D2B, "SR node word1 = 0x001D6D2B",
          hex(node_word1))

    if frame_hex is None:
        check(False, "C builder frame supplied on the command line")
        return
    got = bytes.fromhex(frame_hex)
    print("C builder frame  (%d B): %s" % (len(got), got.hex()))
    check(got == want, "C builder frame is byte-identical to the derivation",
          got.hex())
    check(len(got) == 29, "C builder frame length 29 B")


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

    # 1. the fail-closed parameter and its documented contract
    check("module_param_named(dfs_mode, hi5622_dfs_mode, uint, 0444)" in scan,
          "dfs_mode is a read-only uint module parameter")
    check("MODULE_PARM_DESC(dfs_mode," in scan,
          "dfs_mode has a MODULE_PARM_DESC")
    check("#include \"hi5622_dfs_mode.h\"" in scan,
          "scan.c includes the portable 0x01F7 model")
    check("HI5622_DFS_MODE_PARM_MAX" in scan,
          "the gate uses the model's accepted-parameter bound")

    # 2. the fail-closed parameter gate runs before the VAP base post
    gate = pos_of(scan, "dfs_mode=%u is not a valid DFS-mode parameter",
                  "dfs_mode > 1 refusal line")
    gate2 = pos_of(scan, "dfs_mode=1 needs the beacon_static=1 vendor order",
                   "dfs_mode=1 without beacon_static refusal line")
    plan = pos_of(scan, "plan_n = hi5622_beacon_plan(",
                  "the plan construction")
    check(gate < plan and gate2 < plan,
          "both dfs_mode refusals run before the plan is built/executed",
          "gate=%d gate2=%d plan=%d" % (gate, gate2, plan))
    check(gate > pos_of(scan, "desc_table=%u is not a valid descriptor-table",
                        "the sibling desc_table gate"),
          "the dfs_mode gate follows the desc_table gate (same fail-closed block)")

    # 3. the post is fail-closed through the plan and aborts on error
    check("hi5622_dfs_mode_plan(hi5622_dfs_mode," in scan,
          "the post is gated by hi5622_dfs_mode_plan()")
    check("if (dmp.post) {" in scan, "the frame is only built when dmp.post")
    check('"dfs_mode", &dm_out0);' in scan,
          "the frame is posted through hi5622_frame_post() as 'dfs_mode'")
    check("DFS-mode builder refused" in scan,
          "builder refusal aborts the sequence")
    check("DFS-mode frame (g0 id1 key 0x01F7) not posted" in scan,
          "post failure aborts the sequence")

    # 4. ORDER: desc_table post < dfs_mode post < ap_chan post, all in
    #    the beacon_static branch before the channel builder.
    p_desc = pos_of(scan, "start_ap beacon_static frame %u/%u 'desc_table' posted",
                    "the 0x00F7 post line")
    p_dfs = pos_of(scan, "start_ap beacon_static frame %u/%u 'dfs_mode' posted",
                   "the 0x01F7 post line")
    p_chan = pos_of(scan, "start_ap beacon_static frame %u/%u 'ap_chan' posted",
                    "the 0x0124 post line")
    check(p_desc < p_dfs < p_chan,
          "order in scan.c: desc_table post < dfs_mode post < ap_chan post",
          "desc=%d dfs=%d chan=%d" % (p_desc, p_dfs, p_chan))
    check("build_chan_cfg" in scan and
          p_dfs < scan.find("rc = ops->build_chan_cfg(wire"),
          "the 0x01F7 post is before the 0x0124 builder/post")

    # 5. ordering-plan step
    check("HI5622_BEACON_STEP_DFS_MODE" in logic_h and
          "HI5622_BEACON_STEP_DFS_MODE" in logic_c,
          "the plan enum/step exists in hi5622_scan_logic.{c,h}")
    check("dfs_mode == 1u && n < max" in logic_c,
          "the plan inserts the step only for the fail-closed value 1")
    check('return "dfs_mode"' in logic_c,
          "the plan step name is dfs_mode")
    check("unsigned int desc_table, unsigned int dfs_mode" in logic_h,
          "hi5622_beacon_plan() takes the dfs_mode knob")
    check(scan.find("hi5622_desc_table, hi5622_dfs_mode") >= 0,
          "scan.c passes both knobs to the plan")

    # 6. Kbuild + sysfs
    check("hi5622_dfs_mode.o" in kbuild,
          "Kbuild builds the new portable model")
    check("hi5622_dfs_mode_status_line(" in scan,
          "the sysfs status appends the dfs_mode line")
    check("dfs_mode: param=%u (%s) beacon_static=%u last_rc=%d posted=%u refused=%u"
          in read(tree + "/hi5622_dfs_mode.c"),
          "the status format string exists")

    # 7. the portable model sources are the ones asserted here
    hdr = read(tree + "/hi5622_dfs_mode.h")
    check("#define HI5622_DFS_MODE_KEY\t\t0x01f7u" in hdr,
          "model key 0x01F7")
    check("#define HI5622_DFS_MODE_WIRE_LEN\t29u" in hdr,
          "model wire length 29")
    check("#define HI5622_DFS_MODE_MODE\t\t3u" in hdr,
          "model mode 3")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frame-hex", default=None,
                    help="the C builder frame as hex (from --dump)")
    ap.add_argument("--tree", default=None,
                    help="the patched driver tree (e.g. /tmp/dfsmode-build)")
    args = ap.parse_args()

    test_derivation(args.frame_hex)
    if args.tree:
        test_tree(args.tree)
    else:
        print("NOTE: no --tree; skipping the source contract checks")

    print("%d checks, %d failed" % (len(CHECKS), len(FAILS)))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
