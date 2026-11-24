#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""proto_seq_frames.py - frame builders/decoders for the next startup messages after the
TXQ config sync, and decoders for the two NEW device->host frames (Hi5622V100, WR3000 v2.0).

Lane: wifi-proto-seq (shared task task-103, 2026-11-24), host-only.
Original clean-room code written from the project's decoded contract; no vendor binary
byte, firmware byte or calibration value is embedded here.

What is built here
------------------
* the six vendor calibration config events (alg set_*): HCC group 0 / message id 2,
  config key 0x0101, payload = {u32 cookie0; u32 cookie1; u16 key; u16 len; alg_cfg_blob;
  4 B inherited pad}; the alg_cfg_blob is a fixed 270-byte "equipment param" blob.
* the general g0 config-event carrier (id 1 or 2) with the vendor header rules.
* decoders/checkers for the two D2H frames the firmware answered with:
    - g1 id3  = hmac_event_config_syn: payload {cookie0,cookie1,syn_id,body_len,body}
      (the observed one is syn_id 0x07E0 = WLAN_CFGID_VAP_MIB_UPDATE for vap 4);
    - g1 id0x12 = hmac_event_acs_response frame family: payload
      {u8 a; u8 core; u8 b; u8 pad; u32 payload_len; u32 c; u8 body[payload_len-0xC]}.
* the SR node word for the carrier: (declared_total << 16) | 0x6D2B.

Evidence: see REPORT.md in this lane directory.  All constants below are derived values;
the [V]/[U] markers in the report carry the evidence grade.
"""
import argparse
import json
import struct
import sys

# ---------------------------------------------------------------------------
# contract constants
# ---------------------------------------------------------------------------
HDR_LEN = 12
TAG = 0x5A5A
NODE_FLAGS = 0x6D2B
MAX_TOTAL = 12 + 0x674
MAX_PAYLOAD_CLASS = 0x3

GROUP_CHIP = 0
ID_CFG_EVENT = 2
KEY_ALG_CFG = 0x0101
KEY_CALI_CFG = 0x0102
KEY_ALG_PARAM = 0x00FE

SYN_NAMES = {
    0x07E0: "WLAN_CFGID_VAP_MIB_UPDATE (2016) -> hmac_config_d2h_vap_mib_update",
    0x0101: "WLAN_CFGID_ALG_CFG -> hmac_sync_dmac_alg_cfg_rsp_entry",
    0x0102: "WLAN_CFGID_CALI_CFG -> hmac_sync_dmac_cali_cfg_rsp_entry",
    0x07D1: "WLAN_CFGID_USR_INFO_SYN -> hmac_config_d2h_user_info_syn",
}

ALG_BLOB_TOTAL = 0x010E
ALG_VALUE_OFF = 0x0A
ALG_NEED_W4_OFF = 0x08
ALG_DEV_RET_OFF = 0x04
ALG_CFG_TYPE_SET = 0

CAL_COMMANDS = (
    ("set_2g_power_param",       3502, 0x68, "2g power table"),
    ("set_2g_low_power_param",   3503, 0x68, "2g low-power table"),
    ("set_5g_power_param",       3504, 0x46, "5g power table"),
    ("set_5g_low_power_param",   3505, 0x46, "5g low-power table"),
    ("set_xo_ppm_cali_param",    3506, 0x04, "u32 ppm value at value offset 0"),
    ("set_rssi_param",           3507, 0x28, "40-byte rssi parameter block"),
)
CAL_BY_ID = {c[1]: c for c in CAL_COMMANDS}

DEFAULT_FB = 0x04


class ProtoError(Exception):
    pass


def put16(v):
    return struct.pack("<H", v)


def put32(v):
    return struct.pack("<I", v)


def get16(b, off):
    return struct.unpack_from("<H", b, off)[0]


def get32(b, off):
    return struct.unpack_from("<I", b, off)[0]


def hcc_header(group, mid, dst, fa, fb, total, fc=0, tag=TAG):
    if not 0 <= group <= 0x0F:
        raise ProtoError("group out of range")
    if not 0 <= dst <= 0x0F:
        raise ProtoError("dst out of range")
    if not 0 <= total <= MAX_TOTAL:
        raise ProtoError("total out of range")
    if ((total >> 9) & 7) > MAX_PAYLOAD_CLASS:
        raise ProtoError("header class field out of range")
    return (bytes([group & 0x0F, ((dst & 0x0F) << 4) | 0x01, fa & 0xFF, fb & 0xFF])
            + put16(total) + put16(mid) + bytes([fc & 0xFF, dst & 0x0F]) + put16(tag))


def build_g0_cfg_event(mid, dst, fa, fb, token8, key, data, tail4, fc=0):
    if mid not in (1, 2):
        raise ProtoError("only config-event ids 1/2 are modelled")
    if len(token8) != 8 or len(tail4) != 4:
        raise ProtoError("token must be 8 B, tail 4 B")
    if len(data) > 0x674:
        raise ProtoError("data too long")
    payload = bytes(token8) + put16(key) + put16(len(data)) + bytes(data) + bytes(tail4)
    total = HDR_LEN + len(payload)
    return hcc_header(GROUP_CHIP, mid, dst, fa, fb, total, fc) + payload


def alg_cfg_blob(cfg_id, value=b"", total=ALG_BLOB_TOTAL, need_w4=1,
                 cfg_type=ALG_CFG_TYPE_SET, pkt_type=0, dev_ret=0):
    if total != ALG_BLOB_TOTAL:
        raise ProtoError("equipment-param blobs are 270 bytes")
    if len(value) > total - ALG_VALUE_OFF:
        raise ProtoError("value too long")
    b = bytearray(total)
    struct.pack_into("<HBB", b, 0, cfg_id, cfg_type, pkt_type)
    struct.pack_into("<I", b, ALG_DEV_RET_OFF, dev_ret)
    b[ALG_NEED_W4_OFF] = 1 if need_w4 else 0
    b[ALG_VALUE_OFF:ALG_VALUE_OFF + len(value)] = bytes(value)
    return bytes(b)


def build_cal_alg(name_or_id, value=b"", fb=DEFAULT_FB, dst=0, token8=None, tail4=None):
    if isinstance(name_or_id, str) and name_or_id.isdigit():
        name_or_id = int(name_or_id)
    if isinstance(name_or_id, str):
        for c in CAL_COMMANDS:
            if c[0] == name_or_id:
                cfg_id = c[1]
                break
        else:
            raise ProtoError("unknown calibration command %r" % name_or_id)
    else:
        cfg_id = int(name_or_id)
        if cfg_id not in CAL_BY_ID:
            raise ProtoError("cfg id %d is not one of the six calibration commands" % cfg_id)
    if token8 is None:
        token8 = bytes(8)
    if tail4 is None:
        tail4 = bytes(4)
    blob = alg_cfg_blob(cfg_id, value)
    frame = build_g0_cfg_event(ID_CFG_EVENT, dst, 0x00, fb, token8, KEY_ALG_CFG,
                               blob, tail4)
    return frame, blob


def node_word1(total):
    if not 0 < total <= 0xFFFF:
        raise ProtoError("bad node length")
    return (total << 16) | NODE_FLAGS


def decode_header(frame):
    if len(frame) < HDR_LEN:
        raise ProtoError("short frame")
    total = get16(frame, 4)
    if total != len(frame):
        raise ProtoError("declared total %#x != buffer %#x" % (total, len(frame)))
    return {
        "group": frame[0] & 0x0F,
        "alloc": frame[1] & 0x0F,
        "core": frame[1] >> 4,
        "fa": frame[2],
        "fb": frame[3],
        "total": total,
        "id": get16(frame, 6),
        "fc": frame[8],
        "dst": frame[9],
        "tag": get16(frame, 10),
        "payload": frame[HDR_LEN:],
    }


def decode_config_syn(frame):
    h = decode_header(frame)
    if h["group"] != 1 or h["id"] != 3:
        raise ProtoError("not a g1 id3 config-syn frame (group %d id %d)"
                         % (h["group"], h["id"]))
    p = h["payload"]
    if len(p) < 0x10:
        raise ProtoError("config-syn payload < 0x10")
    cookie0, cookie1 = get32(p, 0), get32(p, 4)
    syn_id, body_len = get16(p, 8), get16(p, 10)
    if 0x10 + body_len != len(p):
        raise ProtoError("config-syn body_len %d does not match payload %d"
                         % (body_len, len(p)))
    body = p[0x0C:0x0C + body_len]
    return {
        "header": {k: h[k] for k in ("group", "id", "total", "core", "fa", "fb", "fc", "dst", "tag")},
        "cookie0": cookie0,
        "cookie1": cookie1,
        "syn_id": syn_id,
        "syn_name": SYN_NAMES.get(syn_id, "unknown/unlisted syn code"),
        "body_len": body_len,
        "body": body,
        "pad": p[0x0C + body_len:],
    }


def decode_acs_response(frame):
    h = decode_header(frame)
    if h["group"] != 1 or h["id"] != 0x12:
        raise ProtoError("not a g1 id0x12 frame (group %d id %d)" % (h["group"], h["id"]))
    p = h["payload"]
    if len(p) < 0x0C:
        raise ProtoError("id0x12 payload < 0xC")
    plen = get32(p, 4)
    if plen != len(p):
        raise ProtoError("id0x12 payload_len %d != buffer %d" % (plen, len(p)))
    return {
        "header": {k: h[k] for k in ("group", "id", "total", "core", "fa", "fb", "fc", "dst", "tag")},
        "a": p[0],
        "core": p[1],
        "b": p[2],
        "payload_len": plen,
        "c": get32(p, 8),
        "body": p[0x0C:],
    }


def alg_blob_decode(blob):
    if len(blob) < ALG_VALUE_OFF:
        raise ProtoError("short alg blob")
    return {
        "cfg_id": get16(blob, 0),
        "cfg_type": blob[2],
        "pkt_type": blob[3],
        "dev_ret": get32(blob, ALG_DEV_RET_OFF),
        "need_w4_dev_return": blob[ALG_NEED_W4_OFF],
        "resv": blob[9],
        "value": blob[ALG_VALUE_OFF:],
    }


def check_alg_response(frame, expect_cfg_id):
    errs = []
    rep = {}
    try:
        d = decode_config_syn(frame)
    except ProtoError as exc:
        return False, rep, [str(exc)]
    rep["syn_id"] = d["syn_id"]
    rep["cookies"] = (d["cookie0"], d["cookie1"])
    rep["body_len"] = d["body_len"]
    if d["syn_id"] != KEY_ALG_CFG:
        errs.append("syn_id %#06x != 0x0101 (alg-cfg response)" % d["syn_id"])
    try:
        blob = alg_blob_decode(d["body"])
        rep["blob"] = {k: blob[k] for k in ("cfg_id", "cfg_type", "dev_ret",
                                            "need_w4_dev_return")}
        if blob["cfg_id"] != expect_cfg_id:
            errs.append("cfg_id %d != expected %d" % (blob["cfg_id"], expect_cfg_id))
        if blob["dev_ret"] != 0:
            errs.append("dev_ret %#x != 0 (OAL_SUCC)" % blob["dev_ret"])
    except ProtoError as exc:
        errs.append(str(exc))
    return (not errs), rep, errs


G1ID3 = bytes.fromhex(
    "0100000424" + "00030000005a5a"
    + "0000000000000000e0070800000101040100010000000000")
G1ID12 = bytes.fromhex(
    "010000122800120000005a5a"
    + "0a0000001c0000000000000000000000000001040a01010100000000")


def selftest():
    checks = 0

    def ck(cond, msg):
        nonlocal checks
        checks += 1
        if not cond:
            raise AssertionError("FAIL: " + msg)

    f, blob = build_cal_alg("set_rssi_param")
    ck(len(f) == 298, "cal frame is 298 B")
    ck(len(blob) == 270, "alg blob is 270 B")
    h = decode_header(f)
    ck(h["group"] == 0 and h["id"] == 2, "cal frame is g0 id2")
    ck(h["total"] == len(f), "declared total matches")
    ck(h["tag"] == TAG, "transport tag")
    ck(h["alloc"] == 1, "alloc nibble 1")
    ck(h["dst"] == 0 and h["core"] == 0, "dst/core 0")
    ck(h["fb"] == DEFAULT_FB, "field B default = 4")
    ck((h["total"] >> 9) & 7 <= 3, "header class field within firmware bound")
    ck(node_word1(len(f)) == (298 << 16) | NODE_FLAGS, "SR node word1")
    p = h["payload"]
    ck(len(p) == 0x11E, "payload = 0x10 + 270")
    ck(p[0:8] == bytes(8), "request cookies zero")
    ck(get16(p, 8) == KEY_ALG_CFG, "key 0x0101")
    ck(get16(p, 10) == 270, "payload len field 270")
    ck(p[12:12 + 270] == blob, "blob at payload+0xC")
    ck(p[-4:] == bytes(4), "4-byte tail")

    b = alg_blob_decode(blob)
    ck(b["cfg_id"] == 3507, "default probe is set_rssi_param 3507")
    ck(b["cfg_type"] == 0, "SET type")
    ck(b["pkt_type"] == 0, "pkt type irrelevant")
    ck(b["dev_ret"] == 0, "dev_ret 0")
    ck(b["need_w4_dev_return"] == 1, "need_w4 set -> device answers")
    ck(len(b["value"]) == 260, "value area 260 B")
    for name, cfg_id, vsize, _ in CAL_COMMANDS:
        fr, bl = build_cal_alg(cfg_id)
        ck(len(fr) == 298, "%s frame size" % name)
        ck(get16(bl, 0) == cfg_id, "%s cfg id embedded" % name)
        ck(len(bl) == 270 and vsize <= 260, "%s value fits" % name)
        ck(node_word1(len(fr)) == (298 << 16) | NODE_FLAGS, "%s node word" % name)

    d = decode_config_syn(G1ID3)
    ck(d["header"]["total"] == 36 and d["header"]["fb"] == 4, "id3 header")
    ck(d["cookie0"] == 0 and d["cookie1"] == 0, "id3 cookies")
    ck(d["syn_id"] == 0x07E0, "id3 syn id 0x07E0")
    ck(d["body_len"] == 8 and len(d["body"]) == 8, "id3 body len 8")
    ck(d["pad"] == bytes(4), "id3 4-byte alloc pad")
    mib = d["body"]
    ck(mib == bytes.fromhex("0001010401000100"), "id3 mib body bytes")
    # only the bandwidth field has a hard instruction-level anchor (the handler feeds it to
    # mac_device_trans_bandwith_to_vht_capinfo); the other field names are [I] in REPORT sec 2
    ck(mib[3] == 4, "wlan_bw_max field data[3] = 4")

    a = decode_acs_response(G1ID12)
    ck(a["header"]["total"] == 40 and a["header"]["id"] == 0x12, "id12 header")
    ck(a["header"]["fb"] == 0x12, "id12 field B 0x12")
    ck(a["a"] == 0x0A, "id12 a=0x0A")
    ck(a["core"] == 0 and a["b"] == 0, "id12 core/b")
    ck(a["payload_len"] == 28, "id12 payload_len 28")
    ck(a["c"] == 0, "id12 c=0")
    ck(len(a["body"]) == 16, "id12 body 16 B")
    ck(a["body"] == bytes.fromhex("00000000000001040a01010100000000"),
       "id12 body bytes")

    ans_body = bytearray(blob)
    ans_payload = put32(0) + put32(0) + put16(KEY_ALG_CFG) + put16(len(ans_body))         + bytes(ans_body) + bytes(4)
    ans = hcc_header(1, 3, 0, 0, DEFAULT_FB, HDR_LEN + len(ans_payload)) + ans_payload
    ok, rep, errs = check_alg_response(ans, 3507)
    ck(ok and not errs, "synthetic expected response accepted")
    ok2, _, errs2 = check_alg_response(ans, 3502)
    ck(not ok2 and errs2, "wrong cfg id rejected")
    ok3, _, errs3 = check_alg_response(G1ID3, 3507)
    ck(not ok3 and errs3, "vap-mib event is not an alg response")

    try:
        hcc_header(0, 2, 0x10, 0, 4, 0x100)
    except ProtoError:
        checks += 1
    else:
        raise AssertionError("FAIL: bad dst accepted")
    try:
        alg_cfg_blob(3502, value=bytes(261))
    except ProtoError:
        checks += 1
    else:
        raise AssertionError("FAIL: oversized value accepted")

    print("proto_seq_frames: %d/%d checks PASS" % (checks, checks))
    return 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--emit", metavar="CFG_ID_OR_NAME")
    ap.add_argument("--values-hex", default="")
    ap.add_argument("--fb", default=DEFAULT_FB, type=int)
    ap.add_argument("--decode-id3", metavar="HEX")
    ap.add_argument("--decode-id12", metavar="HEX")
    ap.add_argument("--check-response", nargs=2, metavar=("HEX", "CFG_ID"))
    args = ap.parse_args(argv)
    if args.selftest:
        return selftest()
    if args.emit:
        val = bytes.fromhex(args.values_hex) if args.values_hex else b""
        frame, blob = build_cal_alg(args.emit, value=val, fb=args.fb)
        print("frame_len=%d node_len=%d node_word1=%#010x"
              % (len(frame), len(frame), node_word1(len(frame))))
        print(frame.hex())
        return 0
    if args.decode_id3:
        d = decode_config_syn(bytes.fromhex(args.decode_id3))
        d["body"] = d["body"].hex()
        d["pad"] = d["pad"].hex()
        print(json.dumps(d, indent=2))
        return 0
    if args.decode_id12:
        d = decode_acs_response(bytes.fromhex(args.decode_id12))
        d["body"] = d["body"].hex()
        print(json.dumps(d, indent=2))
        return 0
    if args.check_response:
        ok, rep, errs = check_alg_response(bytes.fromhex(args.check_response[0]),
                                           int(args.check_response[1]))
        print(json.dumps({"ok": ok, "report": rep, "errors": errs}, indent=2))
        return 0 if ok else 1
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
