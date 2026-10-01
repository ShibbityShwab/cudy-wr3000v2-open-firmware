#!/usr/bin/env python3
import os
import struct
import sys

USAGE = "usage: dt2dts.py <extracted device-tree dir> [out.dts]"


def read_prop(path: str) -> str:
    raw = open(path, "rb").read()
    if len(raw) == 0:
        return "true"
    if all(0x20 <= b < 0x7F or b == 0 for b in raw):
        parts = [p.decode("latin1") for p in raw.split(b"\x00") if p]
        if parts:
            return " " .join('"%s"' % p for p in parts)
    if len(raw) % 4 == 0:
        words = struct.unpack(">%dI" % (len(raw) // 4), raw)
        return "<%s>" % " ".join(f"0x{w:x}" for w in words)
    return "[%s]" % " ".join(f"{b:02x}" for b in raw)


def walk(node: str, name: str, out: "list[str]", indent: int) -> None:
    pad = "\t" * indent
    entries = sorted(os.listdir(node))
    props = [e for e in entries if os.path.isfile(os.path.join(node, e))]
    kids = [e for e in entries if os.path.isdir(os.path.join(node, e))]
    out.append(f"{pad}{name} {{")
    for prop in props:
        out.append(f"{pad}\t{prop} = {read_prop(os.path.join(node, prop))};")
    for kid in kids:
        walk(os.path.join(node, kid), kid, out, indent + 1)
    out.append(f"{pad}}};")


def main() -> int:
    if len(sys.argv) < 2:
        print(USAGE)
        return 2
    root = sys.argv[1]
    out_path = sys.argv[2] if len(sys.argv) > 2 else None
    lines = ["/dts-v1/;", ""]
    walk(root, "/", lines, 0)
    text = "\n".join(lines) + "\n"
    if out_path:
        open(out_path, "w").write(text)
        print(f"written: {out_path} ({len(text)} bytes, {len(lines)} lines)")
    else:
        print(text[:2000])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
