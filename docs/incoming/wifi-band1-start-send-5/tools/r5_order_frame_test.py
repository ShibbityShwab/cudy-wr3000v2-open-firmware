#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""r5_order_frame_test.py - independent cross-check for lane
wifi-band1-start-send-5 (task-213, the R5 reorder: the AP config frames -
ap_ssid + the 0x0127 ap_cfg carrying the beacon interval/DTIM/template - are
posted BEFORE the key-0x0065 VAP start and the 0x0242 band start).

HOST-ONLY.  Two jobs:

1. Re-derive the 40-byte g0-id1 config-event key-0x0065 wire frame for the
   fire fields (field B = VAP index, body arg 0 / state 1 / band 1 / field 0
   / word 0) from the decoded carrier geometry ONLY and compare it with the
   KATs the C selftest asserts (the R5 reorder must not change one byte).
2. Source contract on the R5-patched tree (default /tmp/r5-build): the plan
   emits vap_add -> desc_table -> dfs_mode -> ap_chan -> ap_ssid -> ap_cfg ->
   vap_start_g0 -> band_start -> ap_vap_start; the cfg80211 start_ap posts
   the AP config frames BEFORE the 0x0065 probe/post, which comes before the
   0x0242 build, and a carrier-owned trailing VAP start comes after it; the
   beacon interval/DTIM/template are posted before the band start; G8 still
   runs after the ap_chan, G9'/G11' require 1 (the wifi-vap-start-gates
   polarity) and G12 still gates the 0x0242; the parameters stay
   fail-closed/read-only and the 40-B geometry is unchanged.

Usage: r5_order_frame_test.py [TREE]   (exit 0 = pass)
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LANE = os.path.dirname(HERE)
DOCS = os.path.dirname(LANE)
S4 = os.path.join(DOCS, "wifi-band1-start-send-4")
DEFAULT_TREE = "/tmp/r5-build"

checks = 0
fails = 0


def check(cond, msg):
    global checks, fails
    checks += 1
    if not cond:
        fails += 1
        print("FAIL: " + msg)


def derive_vap_start_g0(module):
    """g0 id1 config-event carrier: 12-B hdr | 8-B token | key u16 | len u16
    | 12-B body | 4 x 0xFF tail.  Header [0]=group 0, [1]=alloc|dst,
    [2]=fa, [3]=field B, [4..5]=total, [6..7]=id 1, [8]=fc, [9]=dst,
    [10..11]=0x5a5a.  Body: [0]=channel arg 0, [1]=state 1, [2]=band 1,
    [3]=field 0, [4..7]=word 0."""
    body = bytes([0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0])
    total = 12 + 8 + 2 + 2 + len(body) + 4
    f = bytearray()
    f += bytes([0x00, 0x01, 0x00, module])
    f += total.to_bytes(2, "little")
    f += (1).to_bytes(2, "little")
    f += bytes([0x00, 0x00, 0x5A, 0x5A])
    f += bytes([0, 0, 0, 0, 1, 0, 0, 0])       # token (inherited)
    f += (0x0065).to_bytes(2, "little")
    f += len(body).to_bytes(2, "little")
    f += body
    f += b"\xff" * 4
    return bytes(f)


def read(path):
    with open(path, "r", errors="replace") as fh:
        return fh.read()


def rederive():
    src = read(os.path.join(S4, "src", "vap_start_g0_selftest.c"))
    check("000100002800010000005a5a000000000100000065000c00" in src,
          "C KAT prefix present")
    check('"000101000000000000000000ffffffff"' in src,
          "C KAT suffix present")
    for name, module in (("KAT40_M0", 0), ("KAT40_M4", 4)):
        m = re.search(name + r'\[\]\s*=\s*((?:\s*"[0-9a-f]+")+);', src)
        check(m is not None, "C KAT " + name + " present")
        if not m:
            continue
        want = "".join(re.findall(r'"([0-9a-f]+)"', m.group(1)))
        got = derive_vap_start_g0(module).hex()
        check(got == want, "independent derivation == C KAT " + name)
        check(len(want) == 80, name + " is a 40-byte frame")
        check(want[8:12] == "2800", name + " total field 0x0028 (LE)")
        check(want[40:44] == "6500", name + " carries key 0x0065 (LE)")
        check(want[44:48] == "0c00", name + " body length 12 (LE)")
        check(want[48:56] == "00010100", name + " body arg/state/band/field")
        check(want[72:80] == "ffffffff", name + " inherited 4 x FF tail")
    check(derive_vap_start_g0(0)[3] == 0, "field B = module 0")
    check(derive_vap_start_g0(4)[3] == 4, "field B = module 4")
    check(len(derive_vap_start_g0(0)) == 40, "wire length 40 B")
    check((40 << 16) | 0x6D2B == 0x00286D2B, "node word1 convention")


def contract(tree):
    scan = read(os.path.join(tree, "hi5622_scan.c"))
    logic_c = read(os.path.join(tree, "hi5622_scan_logic.c"))
    logic_h = read(os.path.join(tree, "hi5622_scan_logic.h"))
    bs_h = read(os.path.join(tree, "hi5622_band_start.h"))
    vsg_c = read(os.path.join(tree, "hi5622_vap_start_g0.c"))
    vsg_h = read(os.path.join(tree, "hi5622_vap_start_g0.h"))
    kbuild = read(os.path.join(tree, "Kbuild"))

    # --- parameter: fail-closed, read-only, default 0 ---
    check("module_param_named(vap_start_g0, hi5622_vap_start_g0, uint, 0444)"
          in scan, "param vap_start_g0 is read-only 0444")
    check("static unsigned int hi5622_vap_start_g0;" in scan,
          "param default 0 (static storage)")
    check("MODULE_PARM_DESC(vap_start_g0," in scan,
          "MODULE_PARM_DESC(vap_start_g0) present")
    check("hi5622_vap_start_g0 > HI5622_VAP_START_G0_PARM_MAX" in scan,
          "gate: vap_start_g0 > 1 refused before any frame")
    check("vap_start_g0=1 needs the beacon_static=1 vendor order" in scan,
          "gate: vap_start_g0=1 requires beacon_static=1")
    check("hi5622_vap_start_g0 == HI5622_VAP_START_G0_PARM_MAX" in scan,
          "the rung activates only at exactly 1")

    # --- plan (R5): vap_add -> desc_table -> dfs_mode -> ap_chan ->
    # --- ap_ssid -> ap_cfg -> vap_start_g0 -> band_start -> ap_vap_start ---
    i_add = logic_c.find("HI5622_BEACON_STEP_VAP_ADD")
    i_dt = logic_c.find("HI5622_BEACON_STEP_DESC_TABLE")
    i_dfs = logic_c.find("HI5622_BEACON_STEP_DFS_MODE")
    i_chan = logic_c.find("HI5622_BEACON_STEP_CHAN")
    i_ssid = logic_c.find("HI5622_BEACON_STEP_SSID")
    i_apcfg = logic_c.find("HI5622_BEACON_STEP_AP_CFG")
    i_g0 = logic_c.find("if (vap_start_g0 == 1u && n < max)")
    i_bs = logic_c.find("HI5622_BEACON_STEP_BAND_START")
    i_vs = logic_c.find("HI5622_BEACON_STEP_VAP_START;")
    check(-1 not in (i_add, i_dt, i_dfs, i_chan, i_ssid, i_apcfg, i_g0,
                     i_bs, i_vs),
          "plan-order markers all present in the model")
    check(i_add < i_dt < i_dfs < i_chan < i_ssid < i_apcfg < i_g0 < i_bs <
          i_vs,
          "plan order in the model: vap_add -> desc_table -> dfs_mode -> "
          "ap_chan -> ap_ssid -> ap_cfg -> vap_start_g0 -> band_start -> "
          "ap_vap_start")
    i_step_ssid = logic_c.find("out[n++] = HI5622_BEACON_STEP_SSID;")
    i_step_cfg = logic_c.find("out[n++] = HI5622_BEACON_STEP_AP_CFG;")
    i_step_g0 = logic_c.find("out[n++] = HI5622_BEACON_STEP_VAP_START_G0;")
    i_step_bs = logic_c.find("out[n++] = HI5622_BEACON_STEP_BAND_START;")
    check(-1 not in (i_step_ssid, i_step_cfg, i_step_g0, i_step_bs),
          "plan emit statements present")
    check(i_step_ssid < i_step_cfg < i_step_g0 < i_step_bs,
          "plan emit order: ap_ssid -> ap_cfg -> vap_start_g0 -> band_start")
    i_enum_add = logic_h.find("HI5622_BEACON_STEP_VAP_ADD = 1")
    i_enum_dt = logic_h.find("HI5622_BEACON_STEP_DESC_TABLE = 2")
    i_enum_dfs = logic_h.find("HI5622_BEACON_STEP_DFS_MODE = 3")
    i_enum_chan = logic_h.find("HI5622_BEACON_STEP_CHAN = 4")
    i_enum_ssid = logic_h.find("HI5622_BEACON_STEP_SSID = 5")
    i_enum_cfg = logic_h.find("HI5622_BEACON_STEP_AP_CFG = 6")
    i_enum_g0 = logic_h.find("HI5622_BEACON_STEP_VAP_START_G0 = 7")
    i_enum_bs = logic_h.find("HI5622_BEACON_STEP_BAND_START = 8")
    i_enum_vs = logic_h.find("HI5622_BEACON_STEP_VAP_START = 9")
    check(-1 not in (i_enum_add, i_enum_dt, i_enum_dfs, i_enum_chan,
                     i_enum_ssid, i_enum_cfg, i_enum_g0, i_enum_bs,
                     i_enum_vs),
          "R5 enum values present")
    check(i_enum_add < i_enum_dt < i_enum_dfs < i_enum_chan <
          i_enum_ssid < i_enum_cfg < i_enum_g0 < i_enum_bs < i_enum_vs,
          "enum order: VAP_ADD < DESC_TABLE < DFS_MODE < CHAN < SSID < "
          "AP_CFG < VAP_START_G0 < BAND_START < VAP_START (R5)")
    check('case HI5622_BEACON_STEP_VAP_START_G0:	return "vap_start_g0";'
          in logic_c, "step name vap_start_g0")
    check('case HI5622_BEACON_STEP_CHAN:		return "ap_chan";'
          in logic_c, "step name ap_chan")
    check("wifi-band1-start-send-5 (task-213) applies the R5 reorder"
          in logic_h, "the plan header documents the R5 reorder")
    check("ap_chan -> ap_ssid -> ap_cfg -> vap_start_g0 -> band_start -> "
          "ap_vap_start" in logic_h,
          "the plan header states the R5 order")
    check("R5 (lane wifi-band1-start-send-5, task-213): the AP config frames"
          in logic_c, "the plan function documents the R5 order")
    check("unsigned int vap_start_g0," in logic_h,
          "plan signature carries the vap_start_g0 parameter")
    check("hi5622_beacon_plan(1u, hi5622_beacon_vap_start,\n\t\t\t\t\t    hi5622_vap_start_g0,"
          in scan,
          "start_ap passes vap_start_g0 into the plan")
    check("step exists only in that AP-start ladder" in scan,
          "parameter gate documents the ladder requirement")

    # --- the AP config phase helper + the R5 call sites ---
    check("static int hi5622_scan_post_ap_seq_frames(struct hi5622_cfg80211 *c,"
          in scan, "the AP-sequence helper is present")
    check("The carrier contract is unchanged: -ENODATA from build_ap_start()"
          in scan, "the helper keeps the -ENODATA carrier contract")
    i_chan_post = scan.find('"ap_chan", NULL')
    i_apcfg_phase = scan.find("hi5622_scan_post_ap_seq_frames(c, ops, p, wire, 0u,")
    i_dt_plan = scan.find("hi5622_desc_table_plan(")
    i_probe = scan.find("hi5622_scan_vap_start_g0_probe(c->ctx")
    i_g5_gate = scan.find("hi5622_vap_start_g0_precheck(&vgprobe)")
    i_post = scan.find('"vap_start_g0", &vg_out0')
    i_g12 = scan.find("hi5622_vap_start_g0_postcheck(")
    i_bs_build = scan.find("hi5622_band_start_build(")
    i_phaseb = scan.find("/* R5 phase B: a carrier-owned trailing VAP-start")
    i_legacy = scan.find("Legacy (non-beacon_static) path: the whole sequence")
    for nm, i in (("ap_chan post", i_chan_post),
                  ("AP config phase call", i_apcfg_phase),
                  ("desc_table plan", i_dt_plan),
                  ("vap_start_g0 probe", i_probe),
                  ("G1..G13 precheck", i_g5_gate),
                  ("vap_start_g0 frame post", i_post),
                  ("G12 postcheck", i_g12),
                  ("band_start build", i_bs_build),
                  ("phase-B call", i_phaseb),
                  ("legacy sequence call", i_legacy)):
        check(i != -1, "start_ap marker present: " + nm)
    check(i_apcfg_phase != -1 and i_probe != -1 and i_post != -1,
          "start_ap markers usable")
    if -1 not in (i_chan_post, i_apcfg_phase, i_probe, i_post, i_g12,
                  i_bs_build, i_phaseb):
        check(i_dt_plan < i_chan_post < i_apcfg_phase < i_probe,
              "R5 start_ap order: desc_table -> 0x0124 post -> AP config "
              "(ap_ssid/ap_cfg, beacon template) -> 0x0065 probe")
        check(i_apcfg_phase < i_post < i_g12 < i_bs_build < i_phaseb,
              "R5 start_ap order: AP config -> 0x0065 post -> G12 -> 0x0242 "
              "build -> trailing carrier VAP start")
    check("AP config posted BEFORE the 0x0065 VAP start / 0x0242 band start "
          "(R5)" in scan,
          "the R5 dmesg line names the new AP-config position")
    check("the firmware beacon template is in place for the TBTT" in scan,
          "the R5 dmesg line names the beacon-template guarantee")
    check("which carries the beacon interval, the DTIM period and the" in
          scan, "the AP config phase names the beacon data it posts")
    check("after the 0x0124 channel post (mctx[0x10..0x13] stored) and the "
          "AP config frames (R5), before the 0x0242 band start" in scan,
          "the 0x0065 post line names its R5 predecessor")
    check("after the 0x01F7 mode, the 0x0124 channel post, the AP config "
          "frames (R5) and the 0x0065 VAP start" in scan,
          "the 0x0242 post line names its R5 predecessors")
    check("already be in the VAP context so the firmware has the beacon"
          in scan, "the 0x0242 placement comment requires the AP config first")
    check("1=post the 40-byte frame after the 0x0124 channel post and the "
          "AP config frames and before the 0x0242 band start" in scan,
          "the vap_start_g0 parameter description carries the R5 order")
    check("1=post the 33-byte sync-start frame after the 0x01F7 mode, the "
          "0x0124 channel post, the AP config frames and the 0x0065 VAP "
          "start" in scan,
          "the band_start parameter description carries the R5 order")
    check("band_start REFUSED - vap_start_g0=1 but the G12 read" in scan,
          "G12 refusal string gates the 0x0242")
    check("mctx[0x0e]=%u mctx[0x11]=%u state1=%u state4=0x%02x vap=0x%08x "
          "vap+0x10=0x%08x" in scan,
          "vap_start_g0 post line carries G12 + the R4b vap/word")
    check("vap_start_g0_ok" in scan,
          "the G12 result latches into the band_start gate")
    check("restart after host-side stop - the 0x0065 VAP start is NOT re-posted"
          in scan, "never start a live VAP twice (restart guard)")
    check("hi5622_vap_start_g0_helper_ok" in scan,
          "G6 helper observation wired")

    # --- probe pack: read-only live reads, every gate wired; gates patch kept ---
    check("hi5622_scan_vap_start_g0_probe(" in scan,
          "probe reader present")
    for gate in ("G5: u32[mctx+0x190] is ZERO",
                 "the key-0x0065 worker would fault at CA 0x46470",
                 "G8: mctx[0x11] is not 1 (5 GHz)",
                 "G13: the band run-state is not fresh"):
        check(gate in vsg_c, "verdict text: " + gate[:24])
    check("HI5622_VAP_START_G0_RF_ENABLED" in vsg_h,
          "G9'/G11' RF-enable polarity marker is kept (wifi-vap-start-gates)")
    check("G9: mctx[0x440] RF-enable flag is ZERO" in vsg_c,
          "G9 requires mctx[0x440] == 1 (the corrected polarity)")
    check("G11: bandobj+0xEC8 band-up flag is ZERO" in vsg_c,
          "G11 requires bandobj+0xEC8 == 1 (the corrected polarity)")
    for ph in ("HI5622_VAP_START_G0_MCTX_VAP_OFF",
               "HI5622_VAP_START_G0_MCTX_B440_OFF",
               "HI5622_VAP_START_G0_MCTX_B4AC_OFF",
               "HI5622_VAP_START_G0_BANDOBJ_PUBLISHED_OFF",
               "HI5622_VAP_START_G0_BANDOBJ_D6E_OFF",
               "HI5622_VAP_START_G0_BANDOBJ_EC8_OFF",
               "HI5622_VAP_START_G0_PERLINK_6C6_OFF"):
        check(ph in vsg_h, "probe offset " + ph)
    check("G5" in vsg_c and "return -EFAULT;" in vsg_c,
          "G5 is a hard -EFAULT gate in the model")
    check("hi5622_vap_start_g0_postcheck" in vsg_h and
          "mctx_state != HI5622_VAP_START_G0_MCTX_STATE_1" in vsg_c,
          "G12 requires mctx[0x0E] == 1 exactly")

    # --- geometry constants + builder contract (frames byte-identical) ---
    check("HI5622_VAP_START_G0_KEY			0x0065u" in
          vsg_h, "key 0x0065 constant")
    check("HI5622_VAP_START_G0_WIRE_LEN		40u" in vsg_h,
          "wire length 40 constant")
    check("HI5622_VAP_START_G0_BODY_LEN		12u" in vsg_h,
          "body length 12 constant")
    check("HI5622_VAP_START_G0_TOTAL_FIELD		0x0028u" in vsg_h,
          "total field 0x28 constant")
    check("HI5622_VAP_START_G0_NODE_WORD1		0x00286d2bu" in vsg_h,
          "node word1 0x00286d2bu")
    check("HI5622_VAP_START_G0_WAIT_MS		300u" in vsg_h,
          "G5/G12 bounded wait 300 ms")
    check("#define HI5622_VAP_START_G0_BODY_OFF		24u" in vsg_h,
          "body offset 24")
    check("HI5622_VAP_START_G0_KEY_OFF		20u" in vsg_h,
          "key offset 20")
    check("memset(b + HI5622_VAP_START_G0_BODY_OFF + HI5622_VAP_START_G0_BODY_LEN,"
          in vsg_c, "4-byte FF tail written after the body")
    check("HI5622_VAP_START_G0_WIRE_LEN)" in vsg_c,
          "builder sets out_len = 40")
    check("!plan->post" in vsg_c, "builder refuses an unarmed plan")

    # --- the 0x0242 base is preserved: field B agreement + R4b/R5 fields ---
    check("field B = the AP-start module" in bs_h,
          "0x0242 field B documentation intact")
    check("u32	vap_object;" in bs_h and "u32	vap_object_p10;" in bs_h,
          "band_start state carries the R4b vap/word fields")
    check("vap=0x%08x vap+0x10=0x%08x" in scan,
          "band_start post line carries the R4b observables")
    check("R4b: the pre-start stored state[4] but state[1] stayed 0" in scan,
          "R4b verdict string present")
    check("the AP config frames" in bs_h and "R5 reorder send-5" in bs_h,
          "the 0x0242 model documents the R5 AP-config predecessor")
    check("BEFORE the 0x0124 channel post" not in bs_h,
          "the 0x0242 model no longer claims it runs before the 0x0124")
    check("R4b reorder" in bs_h,
          "the 0x0242 model documents the R4b reorder")
    check("R5 (lane wifi-band1-start-send-5, task-213): the step now runs "
          "AFTER" in vsg_h,
          "the 0x0065 model documents the R5 placement")
    check("R4b (lane wifi-band1-start-send-4, task-211)" in vsg_h,
          "the 0x0065 model documents the R4b reorder")
    check("R5 reorder by wifi-band1-start-send-5, task-213" in logic_c,
          "the plan model header names the R5 reorder")
    check("hi5622_vap_start_g0.o" in kbuild,
          "Kbuild builds the 0x0065 model object")
    check("hi5622_scan_logic.o" in kbuild and "hi5622_band_start.o" in kbuild,
          "Kbuild still builds the plan + band_start models")


def main():
    rederive()
    tree = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_TREE
    if os.path.isdir(tree):
        contract(tree)
    else:
        check(False, "patched tree %s absent (build first)" % tree)
    print("r5_order_frame_test: %d checks, %d failures" % (checks, fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
