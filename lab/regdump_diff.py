#!/usr/bin/env python3
import re
import sys

PAGE = 0x1000
LINE = re.compile(r"addr\s*=\s*([0-9a-fA-F]+)\s*,\s*value\s*=\s*([0-9a-fA-F]+)")

USAGE = "usage: regdump_diff.py <run1.txt> <run2.txt>"


def load(path):
    out = {}
    for line in open(path):
        m = LINE.search(line)
        if m:
            out[int(m.group(1), 16)] = int(m.group(2), 16)
    return out


def runs(addrs):
    out = []
    cur = [addrs[0]]
    for x in addrs[1:]:
        # a run is contiguous (+4) and stays inside one 0x1000 page
        if x == cur[-1] + 4 and (x >> 12) == (cur[-1] >> 12):
            cur.append(x)
        else:
            out.append(cur)
            cur = [x]
    out.append(cur)
    return out


def main() -> int:
    if len(sys.argv) < 3 or sys.argv[1] in ("-h", "--help"):
        print(USAGE)
        return 2
    a = load(sys.argv[1])
    b = load(sys.argv[2])
    only1 = sorted(set(a) - set(b))
    only2 = sorted(set(b) - set(a))
    changed = sorted(k for k in a if k in b and a[k] != b[k])

    print(f"run1: {sys.argv[1]} ({len(a)} addresses)")
    print(f"run2: {sys.argv[2]} ({len(b)} addresses)")
    print(f"only in run1: {len(only1)}, only in run2: {len(only2)}")
    print(f"changed addresses: {len(changed)}")

    print("change ranges:")
    if changed:
        cruns = [[changed[0]]]
        for x in changed[1:]:
            if x == cruns[-1][-1] + 4:
                cruns[-1].append(x)
            else:
                cruns.append([x])
        for r in cruns:
            print(f"  {r[0]:#010x}..{r[-1]:#010x}  n={len(r)}")

    live_pages = {x >> 12 for x in changed}
    addrs = sorted(a)
    win = runs(addrs) if addrs else []
    print(f"\nwindows (page-contiguous runs), page status static/live:")
    print(f"{'page':>12}  {'start':>10}  {'end':>10}  {'words':>6}  {'changed':>7}  status")
    for r in win:
        ch = sum(1 for x in r if x in a and x in b and a[x] != b[x])
        status = "live" if (r[0] >> 12) in live_pages else "static"
        print(f"{r[0] >> 12:#012x}  {r[0]:#010x}  {r[-1]:#010x}  {len(r):6d}  {ch:7d}  {status}")
    pages = {r[0] >> 12 for r in win}
    print(f"\npages: {len(pages)}, live: {len(live_pages)}, static: {len(pages) - len(live_pages)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
