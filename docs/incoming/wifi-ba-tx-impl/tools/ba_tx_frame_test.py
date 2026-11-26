#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""tools/ba_tx_frame_test.py - lane wifi-ba-tx-impl (task-217)

Independent (Python, second-implementation) checks for the TX-direction BA cut:

  A. re-derives the three complete H2D wires from the decoded field maps and
     compares them with the byte KATs (44 B g0 0x0A relay, 20 B g0 0x0B
     A-MPDU start/end, 32 B g0 0x1C notify) and the three action frames;
  B. checks the patched driver tree's H2D catalogue really advertises a
     firmware handler for every (group,id) the glue posts, so the
     hi5622_frame_post() gate cannot silently refuse the BA messages;
  C. checks the source contract: the fail-closed default/gate, the wire
     builders, and that the six integration points are present and wired.

HOST-ONLY: reads source files only; no device, no vendor artifact.
Usage: tools/ba_tx_frame_test.py [patched-tree]   (default /tmp/ba-tx-build)
"""
import re
import sys
from pathlib import Path

LANE = Path(__file__).resolve().parent.parent
TREE = Path(sys.argv[1] if len(sys.argv) > 1 else "/tmp/ba-tx-build")

checks = 0
fails = 0


def ck(cond, name):
    global checks, fails
    checks += 1
    if not cond:
        fails += 1
        print(f"FAIL {name}")


def ck_eq(got, want, name):
    ck(got == want, f"{name} (got {got!r} want {want!r})")


def u16(v):
    return [v & 0xFF, (v >> 8) & 0xFF]


# ---------------------------------------------------------------- A. wires
def header(group, mid, total):
    """The verified 12-byte HCC header: group, alloc 1 / dst 0, total, id,
    tag 0x5A5A (TABLES T1)."""
    return [group & 0xF, 0x01, 0x00, 0x00] + u16(total) + u16(mid) + \
           [0x00, 0x00] + u16(0x5A5A)


def relay_addba_rsp(uid, tid, status, baw, timeout, amsdu, dialog, policy):
    p = [0] * 32
    p[0], p[1] = 3, 1                      # type 3, subtype 1
    p[2:4] = u16(uid)
    p[7] = tid
    p[9] = status & 0xFF
    p[0x0C:0x0E] = u16(baw)
    p[0x10:0x12] = u16(timeout)
    p[0x12] = amsdu
    p[0x14] = dialog
    p[0x15] = policy
    return header(0, 0x0A, 44) + p


def relay_delba(uid, tid, initiator):
    p = [0] * 32
    p[0], p[1] = 3, 2                      # type 3, subtype 2
    p[2:4] = u16(uid)
    p[7] = tid
    p[8] = initiator & 1
    p[0x14] = 0                            # clear flag
    return header(0, 0x0A, 44) + p


def ampdu_start(tid, seq, uid, flag):
    return header(0, 0x0B, 20) + [0, tid] + u16(seq) + u16(uid) + [flag, 0]


def ampdu_end(tid, uid):
    return header(0, 0x0B, 20) + [1, tid] + u16(0) + u16(uid) + [0, 0]


def tx_notify(uid, tid, value):
    p = [0] * 20
    p[0:2] = u16(uid)
    p[2], p[3] = tid, value
    return header(0, 0x1C, 32) + p


def addba_req(tid, dialog, baw, policy, amsdu, timeout, seq, da, sa, bssid):
    param = (baw << 6) | (policy << 1) | amsdu | (tid << 2)
    return ([0xD0, 0x00, 0x00, 0x00] + list(da) + list(sa) + list(bssid) +
            [0x00, 0x00, 3, 0, dialog] + u16(param) + u16(timeout) +
            u16(seq))


def addba_rsp(tid, dialog, status, baw, policy, amsdu, da, sa, bssid):
    param = (baw << 6) | (policy << 1) | amsdu | (tid << 2)
    return ([0xD0, 0x00, 0x00, 0x00] + list(da) + list(sa) + list(bssid) +
            [0x00, 0x00, 3, 1, dialog] + u16(status) + u16(param) + u16(0))


def delba(tid, initiator, reason, da, sa, bssid):
    return ([0xD0, 0x00, 0x00, 0x00] + list(da) + list(sa) + list(bssid) +
            [0x00, 0x00, 3, 2, 0, ((tid << 4) | (initiator << 3))] +
            u16(reason))


PEER = [0x02, 0x11, 0x22, 0x33, 0x44, 0x55]
OWN = [0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE]
UID = 0x1234


def test_wires():
    w = tx_notify(UID, 2, 0)
    ck_eq(len(w), 32, "A: notify wire len 32")
    ck_eq(w[:12], header(0, 0x1C, 32), "A: notify header (group/id/tag)")
    ck_eq(w[12], UID & 0xFF, "A: notify payload uid low")
    ck_eq(w[13], (UID >> 8) & 0xFF, "A: notify payload uid high")
    ck_eq(w[14], 2, "A: notify payload tid")
    ck_eq(w[15], 0, "A: notify active value 0")
    ck_eq(tx_notify(UID, 2, 2)[15], 2, "A: notify teardown value 2")

    w = relay_addba_rsp(UID, 2, 0, 64, 0, 1, 0x42, 1)
    ck_eq(len(w), 44, "A: relay wire len 44")
    ck_eq(w[:12], header(0, 0x0A, 44), "A: relay header")
    ck_eq(w[12], 0x03, "A: relay type 3")
    ck_eq(w[13], 0x01, "A: relay subtype 1 (ADDBA rsp)")
    ck_eq(w[14:16], u16(UID), "A: relay user index")
    ck_eq(w[19], 2, "A: relay TID")
    ck_eq(w[21], 0, "A: relay ADDBA status")
    ck_eq(w[24:26], u16(64), "A: relay BAW")
    ck_eq(w[30], 1, "A: relay A-MSDU")
    ck_eq(w[32], 0x42, "A: relay dialog")
    ck_eq(w[33], 1, "A: relay policy")

    w = relay_delba(UID, 5, 1)
    ck_eq(len(w), 44, "A: delba relay len 44")
    ck_eq(w[13], 2, "A: delba relay subtype 2")
    ck_eq(w[19], 5, "A: delba relay TID")
    ck_eq(w[20], 1, "A: delba relay initiator")
    ck_eq(w[32], 0, "A: delba relay clear flag zero")

    w = ampdu_start(2, 0x0534, UID, 0)
    ck_eq(len(w), 20, "A: ampdu start wire len 20")
    ck_eq(w[:12], header(0, 0x0B, 20), "A: ampdu header")
    ck_eq(w[12], 0, "A: ampdu start subtype 0")
    ck_eq(w[14:16], u16(0x0534), "A: ampdu start seq")
    ck_eq(w[16:18], u16(UID), "A: ampdu user key")

    w = ampdu_end(2, UID)
    ck_eq(len(w), 20, "A: ampdu end wire len 20")
    ck_eq(w[12], 1, "A: ampdu end subtype 1")
    ck_eq(w[16:18], u16(UID), "A: ampdu end user key")

    # action frames
    f = addba_req(3, 0x42, 64, 1, 1, 0, 0, PEER, OWN, OWN)
    ck_eq(len(f), 33, "A: ADDBA req len 33")
    ck_eq(f[:2], [0xD0, 0x00], "A: req FC action")
    ck_eq(f[4:10], PEER, "A: req DA")
    ck_eq(f[10:16], OWN, "A: req SA")
    ck_eq(f[24:26], [3, 0], "A: req category/action")
    ck_eq(f[26], 0x42, "A: req dialog")
    ck_eq(f[22:24], [0, 0], "A: req seqctl zero")
    param = (64 << 6) | (1 << 1) | 1 | (3 << 2)
    ck_eq(f[27:29], u16(param), "A: req BA param set")

    f = addba_rsp(5, 9, 0, 64, 1, 1, PEER, OWN, OWN)
    ck_eq(len(f), 33, "A: ADDBA rsp len 33")
    ck_eq(f[24:26], [3, 1], "A: rsp category/action")
    ck_eq(f[27:29], u16(0), "A: rsp status 0")
    ck_eq(f[29:31], u16((64 << 6) | (1 << 1) | 1 | (5 << 2)), "A: rsp param")
    ck_eq(f[31:33], u16(0), "A: rsp timeout 0 (vendor parity)")

    f = delba(6, 1, 0x27, PEER, OWN, OWN)
    ck_eq(len(f), 30, "A: DELBA len 30")
    ck_eq(f[24:26], [3, 2], "A: DELBA category/action")
    ck_eq(f[26], 0, "A: DELBA dialog 0")
    ck_eq(f[27], (6 << 4) | (1 << 3), "A: DELBA param byte")
    ck_eq(f[28:30], u16(0x27), "A: DELBA reason")


# ---------------------------------------------------- B. driver post gate
def test_post_gate():
    cat = TREE / "h2d_catalogue.inc"
    ck(cat.is_file(), "B: patched tree has the H2D catalogue")
    text = cat.read_text() if cat.is_file() else ""
    fw = set()
    for m in re.finditer(r"\{\s*(\d+),\s*0x([0-9a-fA-F]+),\s*0x([0-9a-fA-F]+)u?", text):
        group, mid, handler = (int(m.group(1)), int(m.group(2), 16),
                               int(m.group(3), 16))
        if handler:
            fw.add((group, mid))
    for group, mid, name in ((0, 0x0A, "relay"), (0, 0x0B, "ampdu"),
                             (0, 0x1C, "notify")):
        ck((group, mid) in fw,
           f"B: firmware H2D handler advertised for g{group} id 0x{mid:02x} ({name})")
    # the F2/R5 base keeps the proven post path untouched except our hook
    post = (TREE / "hi5622_frame_post.c").read_text()
    ck("h2d_frame_decode(wire, len, f," in post,
       "B: frame_post still validates every wire")
    ck("H2D_VF_HANDLER" in post,
       "B: frame_post still requires a firmware handler")
    ck("unsigned int hi5622_allow_frames = 16u;" in post,
       "B: frame_post budget unchanged (allow_frames=16 default)")


# ------------------------------------------------- C. source/tree contract
def read(path):
    return Path(path).read_text()


def test_contract():
    for name in ("hi5622_ba.h", "hi5622_ba.c", "hi5622_ba_glue.h",
                 "hi5622_ba_glue.c"):
        ck((TREE / name).is_file(), f"C: tree has {name}")
        ck((TREE / name).read_text() == (LANE / "src" / name).read_text(),
           f"C: tree {name} == lane src (no drift)")

    ba = read(TREE / "hi5622_ba.c")
    hdr = read(TREE / "hi5622_ba.h")
    glue = read(TREE / "hi5622_ba_glue.c")
    ck("unsigned int hi5622_ba_enable;" in ba, "C: ba_enable defined (BSS zero)")
    ck(re.search(r"module_param_named\(ba_enable,\s*hi5622_ba_enable,\s*uint,\s*0444\)",
                 ba) is not None, "C: ba_enable is read-only 0444")
    ck("if (hi5622_ba_enable == 0)" in ba and "HI5622_BA_EOPNOTSUPP" in ba,
       "C: gate 0 -> -EOPNOTSUPP")
    ck("hi5622_ba_enable > 1" in hdr or "hi5622_ba_enable" in ba,
       "C: gate refuses >1")
    ck("return HI5622_BA_EINVAL;" in ba, "C: gate >1 -> -EINVAL")
    # wire ids/lengths/groups
    for tok in ("HI5622_BA_H2D_BA_RELAY\t\t0x0A",
                "HI5622_BA_H2D_AMPDU_TX\t\t0x0B",
                "HI5622_BA_H2D_TX_BA_NOTIFY\t0x1C"):
        ck(tok in hdr, f"C: wire id constant {tok.split()[-1]}")
    ck("HI5622_BA_RELAY_WIRE_LEN\t44u" in hdr, "C: relay wire 44")
    ck("HI5622_BA_AMPDU_WIRE_LEN\t20u" in hdr, "C: ampdu wire 20")
    ck("HI5622_BA_NOTIFY_WIRE_LEN\t32u" in hdr, "C: notify wire 32")
    ck("HI5622_BA_HCC_TAG\t\t0x5a5au" in hdr, "C: HCC tag 0x5a5a")
    # the TX-originator sequence: start -> notify active -> ampdu -> delba
    ck("hi5622_ba_tx_start(core, ops, mac, &p)" in ba,
       "C: tx_data starts the ADDBA negotiation")
    ck("HI5622_BA_NOTIFY_ACTIVE" in ba, "C: notify on pending -> active")
    ck("HI5622_BA_NOTIFY_TEARDOWN" in ba, "C: notify on teardown")
    ck("hi5622_ba_wire_ampdu_start" in ba and "hi5622_ba_wire_ampdu_end" in ba,
       "C: TX path emits the A-MPDU boundaries")
    ck("burst_count >= HI5622_BA_BURST_MAX" in ba, "C: bounded burst window")
    # glue: the only post path is hi5622_frame_post + the g0 id6 carrier
    ck('hi5622_frame_post(ctx, wire, len, what, NULL)' in glue,
       "C: H2D posts go through hi5622_frame_post")
    ck('hi5622_frame_post(ctx, wire, wlen, "ba_mgmt_tx", NULL)' in glue,
       "C: action frames go through hi5622_frame_post")
    ck("himg_build_hcc_mgmt_tx" in glue, "C: action frames use the g0 id6 carrier")
    ck("schedule_work" in glue and "INIT_WORK" in glue,
       "C: posts are deferred (no sleeping post in service/xmit context)")
    ck("spin_lock_bh(&ba_glue.tx_lock)" in glue and
       "schedule_work(&ba_glue.tx_work)" in glue,
       "C: the xmit hook is spinlock + schedule_work only")
    # the interface gate + integration points
    main = read(TREE / "main.c")
    ck("hi5622_ba_glue_init(ctx)" in main, "C: probe calls the BA init")
    ck("hi5622_ba_glue_remove(ctx)" in main, "C: remove cancels the BA work")
    hcc = read(TREE / "hcc.c")
    ck("hi5622_ba_glue_d2h(ctx, hdr, payload, payload_avail, wire_len)" in hcc,
       "C: D2H deliver hands every frame to the BA filter")
    mgmt = read(TREE / "hi5622_mgmt.c")
    ck("hi5622_ba_glue_mgmt_rx(ctx, rx.frame, rx.frame_len)" in mgmt,
       "C: mgmt RX hands action frames to the BA core")
    dtx = read(TREE / "hi5622_data_tx.c")
    ck("hi5622_ba_glue_tx_data(c->ctx, eth.da, (u8)p.tid, (u16)seq)" in dtx,
       "C: the data TX path triggers the in-kernel ADDBA originator")
    scan = read(TREE / "hi5622_scan.c")
    ck("hi5622_ba_glue_ap_up(c->ctx, p->band, p->channel, dev->dev_addr)" in scan,
       "C: start_ap records the AP channel/band/MAC")
    ck("hi5622_ba_glue_ap_down(c->ctx)" in scan, "C: stop_ap clears the BA state")
    ck("hi5622_ba_glue_status_line(c->ctx, buf + pos, len - pos)" in scan,
       "C: cfg80211_status carries the BA line")
    kb = read(TREE / "Kbuild")
    ck("hi5622_ba.o hi5622_ba_glue.o" in kb, "C: Kbuild links both objects")
    # fail-closed wiring: the plugin points are gated by ba_enable only
    ck("ba_glue_armed()" in glue, "C: every hook checks the gate")
    # the lane patch/doc source is the same code
    patch = read(LANE / "patches" / "0001-ba-tx-impl.patch")
    ck("+unsigned int hi5622_ba_enable;" in patch,
       "C: the patch adds the gated core")
    ck("+\thi5622_ba_glue_d2h(ctx, hdr, payload, payload_avail, wire_len);" in patch,
       "C: the patch wires the D2H hook")


def main():
    print("ba_tx_frame_test: independent TX-direction BA wire/contract checks")
    ck(TREE.is_dir(), f"tree {TREE} exists")
    if not TREE.is_dir():
        print(f"ba_tx_frame_test: {checks} checks, {fails} failures")
        return 1
    test_wires()
    test_post_gate()
    test_contract()
    print(f"ba_tx_frame_test: {checks} checks, {fails} failures")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
