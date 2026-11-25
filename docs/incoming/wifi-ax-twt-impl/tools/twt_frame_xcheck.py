#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""twt_frame_xcheck.py - independent re-derivation of the TWT-enable protocol
constants and the frame bytes (lane wifi-ax-twt-impl, task-199).

HOST-ONLY.  This tool does not import or link the C model: it re-implements
the documented frame layout from ../TABLES.md and re-checks the constants and
the fail-closed decision table against the same committed facts
(../wifi-ax-advanced/TABLES.md T2/T3/T5/T12).  It is the cross-check for the C
selftest (src/twt_selftest.c): a transcription error in either implementation
shows up as a mismatch.

Run: ./tools/twt_frame_xcheck.py --selftest
"""
import sys

CHECKS = 0
FAILS = 0


def ck(cond, what):
    global CHECKS, FAILS
    CHECKS += 1
    if not cond:
        FAILS += 1
        print("FAIL %s" % what)


# ---- documented constants (TABLES T2/T3/T5/T12) -------------------------
HDR, TOKEN = 12, 8
KEY_TWT = 0x035D
BODY_LEN = 1
WIRE_LEN = 29              # 12 + 8 + 2 + 2 + 1 + 4
TAG = 0x5A5A
CTX0_CA = 0x00119758       # module context 0 (CPU address)
CTX_STRIDE = 0x0B40
CTX_COUNT = 19
DA_OFF = 0xDA              # ctx+0xDA bits 6/7
DA_ENABLE = 0x40
DA_REQUIRED = 0x80
MAX_FIELD_B = 18

FRAME_TWT1 = ("000100001d00010000005a5a00000000010000005d03010001"
              "ffffffff")
FRAME_TWT3 = ("000100001d00010000005a5a00000000010000005d03010003"
              "ffffffff")


def envelope(field_b, key, body):
    """g0-id1 carrier: 12-B HCC header + 8-B token + key/len/body + 4x 0xFF."""
    total = HDR + TOKEN + 2 + 2 + len(body) + 4   # + inherited 4-byte tail
    b = bytearray(total)
    b[0], b[1], b[3] = 0x00, 0x01, field_b & 0xFF
    b[4:6] = total.to_bytes(2, "little")      # total
    b[6:8] = (1).to_bytes(2, "little")        # message id 1
    b[10:12] = TAG.to_bytes(2, "little")      # 0x5A5A
    b[16] = 0x01                              # fired token
    b[20:22] = key.to_bytes(2, "little")
    b[22:24] = len(body).to_bytes(2, "little")
    b[24:24 + len(body)] = body
    b[-4:] = b"\xff\xff\xff\xff"
    return bytes(b)


def precheck(twt, beacon_static, vap_start, he_caps, band):
    """The fail-closed decision (same contract as the C model)."""
    if twt > 3:
        return "EINVAL"
    if twt == 0:
        return "off"
    if twt not in (1, 3):
        return "EINVAL"
    if beacon_static != 1:
        return "EINVAL"
    if vap_start != 1:
        return "EINVAL"
    if he_caps != 1:
        return "EINVAL"
    if band != 0:
        return "EOPNOTSUPP"
    return "post:%d:da=%02x" % (twt, DA_ENABLE |
                                (DA_REQUIRED if twt == 3 else 0))


def field_b(ca):
    if ca == 0:
        return "EAGAIN"
    if ca < CTX0_CA:
        return "ERANGE"
    off = ca - CTX0_CA
    if off % CTX_STRIDE:
        return "ERANGE"
    idx = off // CTX_STRIDE
    return str(idx) if idx < CTX_COUNT else "ERANGE"


def selftest():
    # ---- geometry ----
    ck(WIRE_LEN == HDR + TOKEN + 2 + 2 + BODY_LEN + 4, "wire len 12+8+2+2+1+4")
    ck(KEY_TWT == 0x035D, "key 0x035D")
    ck(CTX_STRIDE == 0x0B40 and CTX_COUNT == 19, "context table geometry")
    ck(CTX0_CA + 18 * CTX_STRIDE == 0x00119758 + 18 * 0x0B40,
       "last context address arithmetic")

    # ---- byte-exact frames (identical to the C model's expected bytes) ----
    for flags, want in ((1, FRAME_TWT1), (3, FRAME_TWT3)):
        got = envelope(0, KEY_TWT, bytes([flags])).hex()
        ck(got == want, "frame twt=%d byte-exact" % flags)
        ck(len(bytes.fromhex(got)) == WIRE_LEN, "frame twt=%d length" % flags)
        ck(bytes.fromhex(got)[20:22] == KEY_TWT.to_bytes(2, "little"),
           "frame twt=%d key offset 20" % flags)
        ck(bytes.fromhex(got)[22:24] == b"\x01\x00",
           "frame twt=%d body len at 22" % flags)
        ck(bytes.fromhex(got)[24] == flags, "frame twt=%d body at 24" % flags)
        ck(bytes.fromhex(got)[-4:] == b"\xff" * 4,
           "frame twt=%d inherited tail" % flags)

    # ---- field B is the AP-start module context, only hdr[3] moves ----
    for fb in (0, 1, 2, 18):
        got = envelope(fb, KEY_TWT, b"\x01")
        base = bytes.fromhex(FRAME_TWT1)
        ck(got[3] == fb, "field B %d echoed at hdr[3]" % fb)
        ck(got[:3] + got[4:] == base[:3] + base[4:],
           "field B %d changes only hdr[3]" % fb)

    # ---- field-B derivation from the live AP-start context CA ----
    ck(field_b(0) == "EAGAIN", "unresolved context refused")
    for k in range(19):
        ck(field_b(CTX0_CA + k * CTX_STRIDE) == str(k),
           "context %d -> field B %d" % (k, k))
    ck(field_b(CTX0_CA + 19 * CTX_STRIDE) == "ERANGE", "context 19 refused")
    ck(field_b(CTX0_CA + 1) == "ERANGE", "misaligned CA refused")
    ck(field_b(CTX0_CA - 1) == "ERANGE", "CA below ctx0 refused")
    ck(field_b(0x00200000) == "ERANGE", "unknown CA refused")

    # ---- fail-closed decision table ----
    cases = [
        ((0, 0, 0, 0, 1), "off"),
        ((0, 0, 0, 0, 7), "off"),
        ((1, 1, 1, 1, 0), "post:1:da=40"),
        ((3, 1, 1, 1, 0), "post:3:da=c0"),
        ((2, 1, 1, 1, 0), "EINVAL"),
        ((4, 1, 1, 1, 0), "EINVAL"),
        ((1, 0, 1, 1, 0), "EINVAL"),
        ((1, 1, 0, 1, 0), "EINVAL"),
        ((1, 1, 1, 0, 0), "EINVAL"),
        ((1, 1, 1, 2, 0), "EINVAL"),
        ((1, 1, 1, 1, 1), "EOPNOTSUPP"),
        ((3, 1, 1, 1, 1), "EOPNOTSUPP"),
    ]
    for args, want in cases:
        got = precheck(*args)
        ck(got == want, "precheck%s -> %s (got %s)" % (args, want, got))

    # ---- the read-back contract: exactly bits 6/7 ----
    ck(DA_ENABLE | DA_REQUIRED == 0xC0, "TWT bits 6/7 mask")
    ck(DA_OFF == 0xDA and (DA_ENABLE >> 6) == 1 and (DA_REQUIRED >> 7) == 1,
       "ctx+0xDA bit0/1 of the byte = TWT enable/required")

    print("twt-frame xcheck: %d/%d PASS" % (CHECKS - FAILS, CHECKS))
    return 1 if FAILS else 0


if __name__ == "__main__":
    if "--selftest" not in sys.argv:
        print("usage: %s --selftest" % sys.argv[0], file=sys.stderr)
        sys.exit(2)
    sys.exit(selftest())
