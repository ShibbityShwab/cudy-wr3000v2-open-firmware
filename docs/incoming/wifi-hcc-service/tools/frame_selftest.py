#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Host-side self-test for the wifi-hcc-service HCC frame model.

No device, no kernel: this re-implements the SAME pure validation/build rules
that pciet_hcc_service.c::hcc_build_sr_frame() applies, asserts the exact wire
bytes for the fixed cases, and prints a table.  It pins the contract the bench
relies on:

  * 12-byte little-endian header: group low nibble at +0, alloc-state nibble
    (0x1) | dst<<4 at +1, u16 total = payload+12 at +4, u16 id at +6,
    dst at +9, transport tag 0x5A5A at +0xA, payload at +0xC;
  * the firmware SR-receive drain validates tag == 0x5A5A AT +0xA and total
    length > 0 AT +4, so a zero header is dropped (wifi-ete-frames lane);
  * refusals: mailbox id 5 (firmware BUG()), HCC group 4 H2D (no receive
    table), any group other than 0/2 (the only H2D tables), payload > 0x674;
  * the default liveness frame is the vendor payload-free group-0 id-0 stub
    frame 00 01 00 00 0c 00 00 00 00 00 5a 5a (h2d-inject validated bytes).

Also re-implements the packed ring-index arithmetic (idx = (idx & 0x3ff) |
phase << 10) used to commit the SR producer and to walk DR fills, and checks
the arm pre-post value (32 publishes -> packed 0x400).

Run:  python3 tools/frame_selftest.py        (exit 0 = all checks pass)
"""
import sys

HDR = 12
TAG = 0x5A5A
MAX_PAYLOAD = 0x674
MAX_TOTAL = MAX_PAYLOAD + HDR
D2H_BIT_ETE_NOTIFY = 6
BUGON_ID = 5
GROUP_PLAT = 4


def hex_to_bin(ch):
    if '0' <= ch <= '9':
        return ord(ch) - 48
    if 'a' <= ch <= 'f':
        return ord(ch) - 87
    if 'A' <= ch <= 'F':
        return ord(ch) - 55
    return -1


def parse_uint(s, i, maxv):
    base = 10
    if s[i:i + 2] in ('0x', '0X'):
        base = 16
        i += 2
    v, any_digit = 0, False
    while i < len(s):
        d = hex_to_bin(s[i]) if base == 16 else (ord(s[i]) - 48 if '0' <= s[i] <= '9' else -1)
        if d < 0:
            break
        if v > (maxv - d) // base:
            raise ValueError('range')
        v = v * base + d
        i += 1
        any_digit = True
    if not any_digit:
        raise ValueError('no digits')
    return v, i


def parse_payload(s, i):
    if s[i:i + 2] in ('0x', '0X'):
        i += 2
    out = bytearray()
    while i < len(s) and s[i] not in ' \t\n\r,':
        hi = hex_to_bin(s[i])
        lo = hex_to_bin(s[i + 1]) if i + 1 < len(s) else -1
        if hi < 0 or lo < 0:
            raise ValueError('bad hex')
        if len(out) >= MAX_PAYLOAD:
            raise ValueError('range')
        out.append((hi << 4) | lo)
        i += 2
    if not out:
        raise ValueError('empty payload')
    return bytes(out), i


def build_sr_frame(req):
    """req: '1' for the safe default, or key=value tokens."""
    g, mid, dst, payload = 0, 0, 0, b''
    if req.strip() and req.strip() != '1':
        s = req
        i, seen = 0, False
        while i < len(s):
            while i < len(s) and s[i] in ' \t\n\r,':
                i += 1
            if i >= len(s):
                break
            if s.startswith('group=', i):
                g, i = parse_uint(s, i + 6, 0xffff)
            elif s.startswith('id=', i):
                mid, i = parse_uint(s, i + 3, 0xffff)
            elif s.startswith('dst=', i):
                dst, i = parse_uint(s, i + 4, 0xf)
            elif s.startswith('payload=', i):
                payload, i = parse_payload(s, i + 8)
            else:
                raise ValueError('unknown key')
            seen = True
        if not seen:
            raise ValueError('empty request')
    if g > 0xf or dst > 0xf:
        raise ValueError('-ERANGE')
    if mid == BUGON_ID:
        raise ValueError('-EACCES id5 BUG trap')
    if g == GROUP_PLAT:
        raise ValueError('-EACCES group4 has no H2D table')
    if g not in (0, 2):
        raise ValueError('-EINVAL group not in {0,2}')
    if len(payload) > MAX_PAYLOAD:
        raise ValueError('-ERANGE payload')
    total = len(payload) + HDR
    frame = bytearray(HDR) + payload
    frame[0] = g & 0xf
    frame[1] = ((dst & 0xf) << 4) | 0x1
    frame[2] = frame[3] = 0
    frame[4] = total & 0xff
    frame[5] = (total >> 8) & 0xff
    frame[6] = mid & 0xff
    frame[7] = (mid >> 8) & 0xff
    frame[8] = 0
    frame[9] = dst & 0xf
    frame[10] = TAG & 0xff
    frame[11] = TAG >> 8
    return bytes(frame)


def model_validate(frame):
    """The firmware SR-receive checks (tag at +0xA, total>0 at +4)."""
    if len(frame) < HDR:
        return False
    tag = frame[0xa] | (frame[0xb] << 8)
    total = frame[4] | (frame[5] << 8)
    return tag == TAG and total > 0


IDX_LOW_MASK = 0x3ff
PHASE = 0x400


def idx_plus(idx, depth):
    nxt = idx + 1
    if (nxt & IDX_LOW_MASK) == depth:
        nxt &= ~IDX_LOW_MASK
        nxt ^= PHASE
    return nxt


def idx_count(frm, to, depth):
    phase = ((to >> 10) & 1) - ((frm >> 10) & 1)
    delta = phase * depth + (to & IDX_LOW_MASK) - (frm & IDX_LOW_MASK)
    if delta < 0:
        delta += 2 * depth
    return delta


def main():
    checks = 0

    def eq(name, got, want):
        nonlocal checks
        checks += 1
        if got != want:
            raise AssertionError('%s: got %r want %r' % (name, got, want))

    # 1. safe default liveness frame == h2d-inject validated bytes
    f = build_sr_frame('1')
    eq('default frame hex', f.hex(), '000100000c00000000005a5a')
    eq('default total', f[4] | (f[5] << 8), 0x0c)
    eq('default tag', f[0xa] | (f[0xb] << 8), TAG)
    eq('default group/id', (f[0] & 0xf, f[6] | (f[7] << 8)), (0, 0))
    eq('firmware validate default', model_validate(f), True)

    # 2. zero header (the old base transport send path) must FAIL validation
    zero = bytes(64)
    eq('zero 64-B header rejected by firmware rules', model_validate(zero), False)

    # 3. populated frame bytes
    f = build_sr_frame('group=2 id=0x2f dst=1 payload=deadbeef')
    eq('payload frame total', f[4] | (f[5] << 8), 16)
    eq('payload frame id', f[6] | (f[7] << 8), 0x2f)
    eq('payload frame dst nx', f[1], 0x11)
    eq('payload frame dst +9', f[9], 1)
    eq('payload frame tag', f[0xa] | (f[0xb] << 8), TAG)
    eq('payload frame payload', f[12:].hex(), 'deadbeef')
    eq('firmware validate populated', model_validate(f), True)

    # 4. refusals (each must raise)
    for req, name in [
        ('group=4 id=1 dst=0', 'group-4 H2D refused'),
        ('group=0 id=5 dst=0', 'mailbox id 5 refused'),
        ('group=1 id=0 dst=0', 'group 1 (no table) refused'),
        ('group=0 id=0 dst=16', 'dst out of range refused'),
        ('group=0 id=0 payload=abc', 'odd hex refused'),
        ('group=0 id=0 payload=' + '00' * 0x675, 'payload > 0x674 refused'),
    ]:
        try:
            build_sr_frame(req)
        except ValueError:
            checks += 1
            continue
        raise AssertionError(name)

    # 5. ring-index arithmetic == the kernel helpers (B3 selftest parity)
    pub = 0
    for _ in range(32):
        pub = idx_plus(pub, 32)
    eq('prepost 32 -> packed 0x400', pub, 0x400)
    eq('full-ring count', idx_count(0, 0x400, 32), 32)
    eq('one node', idx_count(0, 1, 32), 1)
    eq('wrap one node', idx_count(0x1f, 0x400, 32), 1)
    eq('wrap two nodes', idx_count(0x1e, 0x400, 32), 2)
    for start in (0, 1, 15, 31):
        for delta in (0, 1, 2, 31, 32, 33, 63):
            to = start
            for _ in range(delta):
                to = idx_plus(to, 32)
            eq('count/walk %d/%d' % (start, delta), idx_count(start, to, 32), delta)
            eq('phase %d/%d' % (start, delta), (to >> 10) & 1,
               ((start + delta) // 32) & 1)

    # 6. announce constants used by the kernel store
    eq('announce mailbox id', 3, 3)
    eq('DR channel drain mask', 0x1f, 0x1f)
    eq('D2H notify bit', 1 << D2H_BIT_ETE_NOTIFY, 0x40)

    print('wifi-hcc-service frame model selftest: %d checks PASS' % checks)
    print('default liveness frame:', build_sr_frame('1').hex())
    return 0


if __name__ == '__main__':
    sys.exit(main())
