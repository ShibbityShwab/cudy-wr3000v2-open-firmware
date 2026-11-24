#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""ctx_create_frame.py - build the synthesized HCC group-0 / id-1 context-create frame.

Lane wifi-ctx-create (shared task task-101). Host-only; emits a frame our own host posts.
No vendor binary/byte is read or reproduced here: the *shape* (12-byte HCC header +
8-byte token + u16 key + u16 len + data + 4-byte inherited pad) is the config-event shape
already committed by ../wifi-h2d-frames (phase38 capture); the field values are derived
from the firmware parser in ../wifi-ctx-create/REPORT.md.

Frame (key 0x0064 = config table A entry 1 -> worker CA 0x449E4 -> context-create helper
CA 0xFE6BC):

  header : 00 01 00 04  30 00 01 00  00 00 5a 5a    group 0, alloc=1, dst=0,
                                                      fa=0, fb=<fieldB>, total=0x30, id=1,
                                                      fc=0, dst=0, tag=0x5a5a
  payload: token[8] = 00 00 00 00 01 00 00 00
           u16 key  = 0x0064
           u16 len  = 0x0014 (20)
           data[20]:
             +0x00 type      1..3 accepted by the create helper (3 = minimal init)
             +0x01 vap/parent module index (lookup gate; 0..18, ctx[1] must be 0)
             +0x04 user id -> module index that gets created (must equal the TXQ sync
                   field B, i.e. 4)
             +0x11/+0x12      flags (0)
           pad[4]  = ff ff ff ff (inherited bytes; not written by the builder)

Usage:
  ctx_create_frame.py --frame             # print hex + C array of the default frame
  ctx_create_frame.py --frame --type 1 --vap 4 --user 4 --fb 4 --pad72
  ctx_create_frame.py --recipe            # JSON recipe consumed by the bench tooling
  ctx_create_frame.py --selftest          # self-checking assertions
"""
import argparse
import json
import sys

FRAME_TOTAL = 0x30          # declared total = 12 header + 8 token + 4 key/len + 20 data + 4 pad
LEN = 0x14
KEY_CREATE = 0x0064
ID_CFG_EVENT = 1
TOKEN = bytes([0, 0, 0, 0, 1, 0, 0, 0])
PAD = b"\xff\xff\xff\xff"
DEFAULT_TYPE = 3
DEFAULT_VAP = 4
DEFAULT_USER = 4
DEFAULT_FB = 4


def build(ctx_type=DEFAULT_TYPE, vap=DEFAULT_VAP, user=DEFAULT_USER,
          field_b=DEFAULT_FB, dst=0, field_a=0, field_c=0,
          token=TOKEN, pad=PAD):
    """Return the frame buffer (declared-total bytes) or raise ValueError."""
    if ctx_type not in (1, 2, 3):
        raise ValueError("ctx type must be 1, 2 or 3 (the create helper allocates +0x190 "
                         "only for these; 0/4/5/6 take the no-alloc path)")
    for name, v in (("vap", vap), ("user", user), ("field_b", field_b)):
        if not 0 <= v <= 0x12:
            raise ValueError("%s must be 0..0x12 (module-table index)" % name)
    if field_b != user:
        raise ValueError("field_b (%d) must equal user (%d): the handler looks up the same "
                         "module index the worker then creates" % (field_b, user))
    if not 0 <= dst <= 0xF:
        raise ValueError("dst nibble 0..0xF")
    if len(token) != 8 or len(pad) != 4:
        raise ValueError("token must be 8 B, pad 4 B")

    data = bytearray(LEN)
    data[0] = ctx_type
    data[1] = vap
    data[4] = user
    # data[0x11], data[0x12] stay 0 (helper flag reads; 0 is the boot-time default)

    hdr = bytes([0x00 | (0 << 4),                      # group 0
                 0x01 | (dst << 4),                    # alloc state 1, dst in high nibble
                 field_a & 0xFF,
                 field_b & 0xFF,
                 FRAME_TOTAL & 0xFF, (FRAME_TOTAL >> 8) & 0xFF,
                 ID_CFG_EVENT & 0xFF, 0x00,
                 field_c & 0xFF,
                 dst & 0x0F,
                 0x5A, 0x5A])
    body = hdr + token + bytes([KEY_CREATE & 0xFF, KEY_CREATE >> 8,
                                LEN & 0xFF, LEN >> 8]) + bytes(data) + pad
    assert len(body) == FRAME_TOTAL, (len(body), FRAME_TOTAL)
    return body


def node_buffer(frame, pad72=False, pad_byte=0xFF):
    """SR node buffer: declared total (48 B), or the vendor-capture-style 72 B buffer."""
    if not pad72:
        return frame
    return frame + bytes([pad_byte]) * (72 - len(frame))


def recipe(ctx_type=DEFAULT_TYPE, vap=DEFAULT_VAP, user=DEFAULT_USER,
           field_b=DEFAULT_FB, pad72=False):
    f = build(ctx_type, vap, user, field_b)
    return {
        "schema": "hi5622-hcc-h2d-frame/1",
        "lane": "wifi-ctx-create",
        "name": "ctx_create_g0id1_key0x64",
        "direction": "host->device",
        "carrier": {"ring": "SR", "announce_mailbox_id": 3,
                    "node_len_rule": "header total (0x30)" if not pad72 else "72 (capture style)"},
        "hcc": {"group": 0, "id": 1, "dst": 0, "alloc": 1,
                "field_a": 0, "field_b": field_b, "field_c": 0,
                "total": FRAME_TOTAL, "tag": "0x5a5a"},
        "payload": {"key": "0x0064", "len": LEN, "data_len": LEN,
                    "data": {"type": ctx_type, "vap": vap, "user": user}},
        "node_buffer_hex": node_buffer(f, pad72).hex(),
        "derived_from": {
            "handler": "CA 0x43D58 (group 0 id 1, config-event demultiplexer)",
            "key_table": "CA 0x10C23C entry 1 (u16 key 0x64 -> worker CA 0x449E4)",
            "create_helper": "CA 0xFE6BC (str.w r0,[ctx,#0x190] at CA 0xFE7E4)",
            "status": "SYNTHESIZED payload, not a vendor capture",
        },
    }


def c_array(buf, name="ctx_create_g0id1_key0x64"):
    lines = []
    for i in range(0, len(buf), 12):
        chunk = ", ".join("0x%02x" % b for b in buf[i:i + 12])
        lines.append("    " + chunk + ",")
    return ("/* wifi-ctx-create: HCC group 0 / id 1 config event, key 0x0064.\n"
            " * total 0x30, fb = created module index. SYNTHESIZED from the firmware\n"
            " * parser; see opensource/docs/incoming/wifi-ctx-create/REPORT.md. */\n"
            "static const unsigned char %s[%d] = {\n%s\n};\n"
            % (name, len(buf), "\n".join(lines)))


def cmd_selftest():
    checks = []

    def ck(name, cond, detail=""):
        checks.append((name, bool(cond), detail))

    f = build()
    ck("default frame length == declared total", len(f) == FRAME_TOTAL, len(f))
    ck("header bytes", f[:12].hex() == "000100043000010000005a5a", f[:12].hex())
    ck("token bytes", f[12:20].hex() == "0000000001000000", f[12:20].hex())
    ck("key u16 == 0x64", f[20] == 0x64 and f[21] == 0x00, f[20:22].hex())
    ck("len u16 == 0x14", f[22] == 0x14 and f[23] == 0x00, f[22:24].hex())
    ck("data[0] type == 3", f[24] == 3, f[24])
    ck("data[1] vap == 4", f[25] == 4, f[25])
    ck("data[4] user == 4", f[28] == 4, f[28])
    ck("data[0x11] == 0", f[24 + 0x11] == 0)
    ck("data[0x12] == 0", f[24 + 0x12] == 0)
    ck("pad == ffffffff", f[28 + 16:] == b"\xff" * 4, f[44:].hex())
    ck("node buffer default == frame", node_buffer(f) == f)
    ck("node buffer 72 B", len(node_buffer(f, True)) == 72, len(node_buffer(f, True)))
    ck("node buffer 72 tail", node_buffer(f, True)[48:] == b"\xff" * 24)
    for t in (1, 2, 3):
        ck("type %d accepted" % t, build(t)[24] == t)
    for t in (0, 4, 5, 6, 7):
        try:
            build(t)
            ck("type %d refused" % t, False)
        except ValueError:
            ck("type %d refused" % t, True)
    try:
        build(user=3)   # fb mismatch
        ck("fb/user mismatch refused", False)
    except ValueError:
        ck("fb/user mismatch refused", True)
    try:
        build(field_b=0x13)
        ck("fb > 0x12 refused", False)
    except ValueError:
        ck("fb > 0x12 refused", True)
    ck("dst nibble in hdr[1]/hdr[9]", build(dst=2)[1] == 0x21 and build(dst=2)[9] == 0x02)
    ck("c array has 48 bytes", "unsigned char" in c_array(f) and "0x03, 0x04" in c_array(f))

    bad = 0
    for name, ok, detail in checks:
        print("%s  %s%s" % ("PASS" if ok else "FAIL", name,
                            ("  [%s]" % (detail,)) if not ok else ""))
        bad += 0 if ok else 1
    print("\n%d/%d PASS" % (len(checks) - bad, len(checks)))
    return 0 if bad == 0 else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--frame", action="store_true")
    ap.add_argument("--recipe", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--type", type=lambda x: int(x, 0), default=DEFAULT_TYPE)
    ap.add_argument("--vap", type=lambda x: int(x, 0), default=DEFAULT_VAP)
    ap.add_argument("--user", type=lambda x: int(x, 0), default=DEFAULT_USER)
    ap.add_argument("--fb", type=lambda x: int(x, 0), default=DEFAULT_FB)
    ap.add_argument("--dst", type=lambda x: int(x, 0), default=0)
    ap.add_argument("--pad72", action="store_true")
    ap.add_argument("--c-array", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        return cmd_selftest()
    if args.recipe:
        print(json.dumps(recipe(args.type, args.vap, args.user, args.fb, args.pad72),
                         indent=2))
        return 0
    if args.frame or args.c_array:
        f = build(args.type, args.vap, args.user, args.fb, dst=args.dst)
        buf = node_buffer(f, args.pad72)
        if args.c_array:
            print(c_array(buf))
        else:
            print("frame   (%d B) %s" % (len(f), f.hex()))
            print("node    (%d B) %s" % (len(buf), buf.hex()))
            print("reads: ctx+0x190 at CA 0x11C5E8 = BAR0 0x7D45E8")
        return 0
    ap.print_help()
    return 0


if __name__ == "__main__":
    sys.exit(main())
