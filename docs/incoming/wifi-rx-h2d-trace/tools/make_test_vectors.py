#!/usr/bin/env python3
"""make_test_vectors.py - deterministic synthetic vectors for decode_h2d.py's host tests.

Lane rx-h2d-trace (task-176). The vectors are built from the DOCUMENTED wire formats
(H2D-FORMATS.md) and the committed vendor exemplar (the 0xD8 config event); no vendor binary
or firmware byte is involved. They exercise: valid config event, valid direct id, unaccounted
key 0x0004, band-core group, WAL group-2, format drift, and two quarantine classes.
"""
import argparse
import os


def cfg_event(key, data, group=0, bid=1, chip=4, tag=0x5A5A, total=None):
    total = total if total is not None else 28 + len(data)
    hdr = (bytes([group & 0xF, bid, 0, chip])
           + total.to_bytes(2, "little")
           + (1).to_bytes(2, "little")
           + bytes([0, 0]) + tag.to_bytes(2, "little"))
    return (hdr + bytes([0, 0, 0, 0, 1, 0, 0, 0])
            + key.to_bytes(2, "little") + len(data).to_bytes(2, "little")
            + data + b"\xff" * 4)


def direct_msg(group, mid, payload, bid=1, chip=0, tag=0x5A5A):
    total = 12 + len(payload)
    hdr = (bytes([group & 0xF, bid, 0, chip])
           + total.to_bytes(2, "little")
           + mid.to_bytes(2, "little")
           + bytes([0, 0]) + tag.to_bytes(2, "little"))
    return hdr + payload


def m_line(ts, ch, idx, node, frame, flags=0x0D2B, prime=0, hex_override=None,
           len_override=None):
    ln = len_override if len_override is not None else len(frame)
    total = int.from_bytes(frame[4:6], "little")
    group = frame[0] & 0xF
    bid = frame[1]
    vap, chip = frame[2], frame[3]
    mid = int.from_bytes(frame[6:8], "little")
    dst = frame[9]
    hx = hex_override if hex_override is not None else frame.hex()
    return (f"M ts_ms={ts} ch={ch} idx={idx} prime={prime} node=0x{node:08x} len={ln} "
            f"flags=0x{flags:04x} total={total} group={group} bid=0x{bid:02x} vap={vap} "
            f"chip={chip} id={mid} dst={dst} hex={hx}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)

    # A: the committed vendor exemplar - g0 id1 key 0x00D8, 20 B data, total 48 (matches ours)
    fa = cfg_event(0x00D8, bytes(20))
    assert len(fa) == 48, len(fa)
    # B: g0 direct id 7 (user add), 180 B total (matches ours)
    fb = direct_msg(0, 7, bytes([1, 0]) + bytes(166))
    assert len(fb) == 180
    # C: g0 id1 key 0x0004, 4 B data, total 32 (the F1-a candidate; NOT in ours)
    fc = cfg_event(0x0004, bytes([1, 0, 0, 0]))
    assert len(fc) == 32
    # D: band-core group 1, id 3 (a hcc_msg_tx_to_core class; NOT in ours)
    fd = direct_msg(1, 3, bytes(20))
    # G: WAL group-2 family, id 3
    fg = direct_msg(2, 3, bytes(20))
    # I: format DRIFT - key 0x00D8 with only 16 B data (ours expects total 48)
    fi = cfg_event(0x00D8, bytes(16))
    assert len(fi) == 44
    # F: quarantine - a node length that disagrees with the frame's own total field
    ff = cfg_event(0x00D8, bytes(20))
    # H: quarantine - a truncated hex body (len says 200, only 48 bytes present)

    lines = ["# omo_h2dsampler v1 ts0=1000 window=0x403f2000 module=hi5622v100_plat duration=180 period_ms=20"]
    lines.append("R ts_ms=1000 ch=0 ctrl=0x00000000 base=0x84543000 depth=0x1f prod=0x00000000 cons=0x00000000")
    lines.append(m_line(1000, 0, 0, 0x82483840, fa))
    lines.append(m_line(1100, 0, 1, 0x82483440, fb))
    lines.append(m_line(1200, 0, 2, 0x82482240, fc))
    lines.append(m_line(1300, 0, 3, 0x82481a40, fd))
    lines.append(m_line(1400, 0, 4, 0x82481c40, fg))
    lines.append(m_line(1500, 0, 5, 0x82483a40, fi))
    # F: lying node length: the node announces 48 but the frame carries total=32 (the sampler
    #    emits it because the tag is valid; the decoder must quarantine the disagreement)
    lines.append(m_line(1600, 0, 6, 0x82482840, fc, len_override=48))
    # H: truncated body: the node announces 200 bytes, the log carries only 48
    lines.append(m_line(1700, 0, 7, 0x82483640, ff, hex_override=ff.hex()[:96],
                        len_override=200))
    lines.append("R ts_ms=2000 ch=0 ctrl=0x00000000 base=0x84543000 depth=0x1f prod=0x00000008 cons=0x00000002")
    lines.append("E ts_ms=1900 msg=heartbeat")
    open(os.path.join(a.out, "sampler.log"), "w").write("\n".join(lines) + "\n")

    # kprobe vectors: frame A padded to 64 bytes as sixteen x32 words
    pad = fa + bytes(64 - len(fa))
    words = " ".join(f"w{i}=0x{int.from_bytes(pad[i*4:i*4+4],'little'):08x}" for i in range(16))
    kp = (f"          <...>-1234  [000] d... 16.145635: h2d_tx: (hcc_msg_tx+0x0/0x0) "
          f"msg=0xc1234567 hdr=0xbf940000 {words}\n"
          f"          <...>-1234  [000] d... 16.145991: h2d_tx: (hcc_msg_tx+0x0/0x0) "
          f"msg=0xc1234568 hdr=0xbf941000 {words}\n")
    open(os.path.join(a.out, "kprobe.log"), "w").write(kp)

    # bulk vector: a raw host-DRAM image at 0x82480000 with an SR node array at 0x82481000
    DUMP_BASE, NODE_BASE, SIZE = 0x82480000, 0x82481000, 0x2400
    dump = bytearray(SIZE)
    for i, (buf, frame) in enumerate(((0x82480000, fa), (0x82480200, fb))):
        off = buf - DUMP_BASE
        dump[off:off + len(frame)] = frame
        noff = NODE_BASE - DUMP_BASE + i * 8
        w1 = (len(frame) << 16) | 0x6D2B
        dump[noff:noff + 4] = buf.to_bytes(4, "little")
        dump[noff + 4:noff + 8] = w1.to_bytes(4, "little")
    open(os.path.join(a.out, "bulk.bin"), "wb").write(dump)
    print(f"vectors written to {a.out}: sampler.log kprobe.log bulk.bin (frames are synthetic "
          f"except the documented 0xD8 geometry)")


if __name__ == "__main__":
    main()
