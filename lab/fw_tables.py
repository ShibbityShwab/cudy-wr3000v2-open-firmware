#!/usr/bin/env python3
import struct
import sys

BIAS = 0x40000
CODE = (0x0, 0xC0000)
STRS = (0xC3000, 0xE2C98)

USAGE = (
    "usage: fw_tables.py <FIRMWARE.bin> [out.csv]\n"
    "scans the blob's tables for {runtime_address, name} pairs; file offset = address - 0x40000, "
    "odd address means Thumb"
)


def main() -> int:
    if len(sys.argv) < 2:
        print(USAGE)
        return 2
    path = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else None
    data = open(path, "rb").read()

    def read_str(off: int) -> str | None:
        raw = data[off:off + 64].split(b"\x00")[0]
        if not raw or any(b < 0x20 or b > 0x7E for b in raw):
            return None
        return raw.decode("latin1")

    pairs: dict[str, int] = {}
    for off in range(STRS[0], STRS[1] - 8, 4):
        addr, name_ptr = struct.unpack_from("<II", data, off)
        file_addr, file_name = addr - BIAS, name_ptr - BIAS
        if CODE[0] <= (file_addr & ~1) < CODE[1] and STRS[0] <= file_name < STRS[1]:
            name = read_str(file_name)
            if name and 2 < len(name) < 48:
                pairs.setdefault(name, file_addr & ~1)

    rows = [f"{addr:#08x},{name}" for name, addr in sorted(pairs.items(), key=lambda kv: kv[1])]
    print(f"entries: {len(rows)}")
    print(f"smac entries: {sum(1 for r in rows if 'smac_' in r)}")
    if out:
        open(out, "w").write("\n".join(rows) + "\n")
        print(f"written: {out}")
    else:
        print("\n".join(rows[:40]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
