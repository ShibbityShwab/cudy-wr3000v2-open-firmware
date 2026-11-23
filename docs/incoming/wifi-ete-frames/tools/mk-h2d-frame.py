#!/usr/bin/env python3
"""mk-h2d-frame.py - build/validate one HCC H2D frame for the bench injector.

Rules encoded (all verified in REPORT.md sec 1.3 / 3.2):
  * 12-byte header, little-endian; total length at +4 = payload + 12;
  * transport tag 0x5A5A at +0xa (the firmware's SR receive drain drops frames
    without it: FIRMWARE.bin file 0x8699E);
  * dst resource nibble duplicated in hdr[1] high nibble and hdr[9];
  * mailbox announce is always id 3 (out[0] |= 1<<3, out[2] |= 1);
  * HCC message id 5 is refused (firmware BUG() trap, FIRMWARE.bin 0x819DC).

This tool only builds text. It does not touch a device.

usage:
  $PY tools/mk-h2d-frame.py group=0 id=0 dst=0                 # safe payload-free probe
  $PY tools/mk-h2d-frame.py group=0 id=0 dst=0 payload=aa
  $PY tools/mk-h2d-frame.py --raw 000100000c00000000005a5a     # validate raw hex
  $PY tools/mk-h2d-frame.py --all                              # print the bench table
"""
import argparse
import sys

MAX_PAYLOAD = 0x674


def build(group, mid, dst, a=0, b=0, c=0, payload=b""):
    if mid == 5:
        raise ValueError("HCC message id 5 is refused (firmware BUG() trap)")
    if not (0 <= group <= 0xF and 0 <= dst <= 0xF and 0 <= mid <= 0xFFFF):
        raise ValueError("group/dst 0..15, id 0..65535")
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload > 0x674")
    total = len(payload) + 12
    hdr = bytearray(12)
    hdr[0] = group
    hdr[1] = ((dst & 0xF) << 4) | 0x1
    hdr[2] = a & 0xFF
    hdr[3] = b & 0xFF
    hdr[4:6] = total.to_bytes(2, "little")
    hdr[6:8] = mid.to_bytes(2, "little")
    hdr[8] = c & 0xFF
    hdr[9] = dst & 0xF
    hdr[10:12] = (0x5A5A).to_bytes(2, "little")
    return bytes(hdr) + payload


def validate(raw):
    if len(raw) < 12:
        raise ValueError("short frame")
    total = int.from_bytes(raw[4:6], "little")
    mid = int.from_bytes(raw[6:8], "little")
    tag = int.from_bytes(raw[10:12], "little")
    dst9 = raw[9]
    dst1 = raw[1] >> 4
    if mid == 5:
        raise ValueError("HCC message id 5 is refused")
    if total != len(raw):
        raise ValueError("hdr[4..5]=%d != frame length %d" % (total, len(raw)))
    if tag != 0x5A5A:
        raise ValueError("tag 0x%04x != 0x5A5A at hdr[0xa]" % tag)
    if dst9 != dst1:
        raise ValueError("dst nibble mismatch hdr[1]>>4=%d hdr[9]=%d" % (dst1, dst9))
    return dict(group=raw[0] & 0xF, dst=dst1, id=mid, total=total,
                payload=raw[12:], a=raw[2], b=raw[3], c=raw[8])


def show(raw):
    print("  frame  :", raw.hex())
    print("  length :", len(raw), "B (payload", len(raw) - 12, "B)")
    v = validate(raw)
    print("  header : group=%d id=%d dst=%d a=%d b=%d c=%d tag=0x5A5A total=0x%x"
          % (v["group"], v["id"], v["dst"], v["a"], v["b"], v["c"], v["total"]))
    print("  inject : echo 'group=%d id=%d dst=%d%s' > h2d_inject"
          % (v["group"], v["id"], v["dst"],
             (" payload=" + v["payload"].hex()) if v["payload"] else ""))
    print("  raw    : echo '%s' > h2d_raw" % raw.hex())
    print("  mailbox: out[0] |= 1<<3 ; out[2] |= 1  (id 3 announce; device ack = out[5], read-only)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("kv", nargs="*", help="group=.. id=.. dst=.. [a=][b=][c=][payload=hex]")
    ap.add_argument("--raw", help="validate a raw frame hex instead of building")
    ap.add_argument("--all", action="store_true")
    args = ap.parse_args()
    if args.all:
        print("safe payload-free liveness probe (firmware group-0 id-0 handler is a stub):")
        show(build(0, 0, 0))
        print()
        print("first post-ready vendor H2D (group 0 id 1, hdpp_config_send_event): header only;")
        print("payload is the dynamic TXQ-table sync descriptor - DO NOT synthesize it yet:")
        try:
            show(build(0, 1, 0))
        except ValueError as e:
            print("  ", e)
        print()
        print("never send H2D: group 4 (no firmware receive table), id 5 (BUG trap),")
        print("                the base-transport zero-header frame (tag 0 / len 0 are dropped).")
        return 0
    if args.raw:
        raw = bytes.fromhex(args.raw.replace(" ", ""))
        show(raw)
        return 0
    kv = {}
    for item in args.kv:
        k, _, v = item.partition("=")
        kv[k] = v
    if not {"group", "id", "dst"} <= set(kv):
        print("need group= id= dst= (or --raw/--all)")
        return 2
    payload = bytes.fromhex(kv.get("payload", "").replace(" ", "")) if kv.get("payload") else b""
    raw = build(int(kv["group"], 0), int(kv["id"], 0), int(kv["dst"], 0),
                int(kv.get("a", 0)), int(kv.get("b", 0)), int(kv.get("c", 0)), payload)
    show(raw)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except ValueError as e:
        print("REFUSED:", e)
        sys.exit(1)
