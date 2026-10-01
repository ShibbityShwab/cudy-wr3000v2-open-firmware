#!/usr/bin/env python3
import sys

MAGIC = b"ZZZZ"
TRAILER = b"\xa5\xa5\xa5\xa5"
RUN_MIN = 20
BLOCK = 4096

USAGE = "usage: kv_analyze.py <store.kv>"


def zero_runs(payload, minimum):
    runs = []
    i = 0
    while i < len(payload):
        if payload[i] == 0:
            j = i
            while j < len(payload) and payload[j] == 0:
                j += 1
            if j - i >= minimum:
                runs.append((i, j - i))
            i = j
        else:
            i += 1
    return runs


def nonzero_u16(block):
    return sum(1 for i in range(0, len(block) - 1, 2) if block[i] | block[i + 1])


def main() -> int:
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        print(USAGE)
        return 2
    path = sys.argv[1]
    data = open(path, "rb").read()
    payload = data[4:-4] if len(data) >= 8 else b""
    runs = zero_runs(payload, RUN_MIN)
    periods = [runs[k + 1][0] - runs[k][0] for k in range(len(runs) - 1)]

    print(f"file: {path}")
    print(f"size: {len(data)}")
    print(f"magic head: {data[:4].hex()} {'ok' if data[:4] == MAGIC else 'FAIL'}")
    print(f"magic tail: {data[-4:].hex()} {'ok' if data[-4:] == TRAILER else 'FAIL'}")
    print(f"payload: {len(payload)} bytes")
    print(f"zero runs (>= {RUN_MIN} bytes): {len(runs)}")
    for off, ln in runs:
        print(f"  start {off} length {ln}")
    print(f"implied period: {periods if periods else 'none'}")
    print("nonzero u16 halves per 4 KiB block:")
    for b in range(0, len(payload), BLOCK):
        blk = payload[b:b + BLOCK]
        print(f"  block {b:#06x} size {len(blk)} nonzero_u16 {nonzero_u16(blk)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
