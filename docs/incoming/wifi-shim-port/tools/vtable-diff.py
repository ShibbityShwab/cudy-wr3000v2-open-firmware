#!/usr/bin/env python3
"""M3c: vtable (function-pointer struct) member-ORDER diff between the vendor 5.10
tree and our 6.18 tree. Order drift in net_device_ops/ethtool_ops means the 6.18
core calls the WRONG slot of a 5.10-layout static table.

Usage: python3 vtable-diff.py --h510 <netdevice.h> --h618 <netdevice.h> --out-dir analysis
"""
import argparse, os

def extract(header, struct_name):
    src = open(header, errors="replace").read()
    i = src.find(struct_name + " {")
    if i < 0:
        return []
    depth, body, j = 0, [], i + len("struct " + struct_name)
    while j < len(src):
        c = src[j]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                break
        body.append(c)
        j += 1
    body = "".join(body)
    out = []
    k = 0
    while k < len(body):
        a = body.find("/*", k)
        if a < 0:
            out.append(body[k:])
            break
        out.append(body[k:a])
        b = body.find("*/", a + 2)
        k = b + 2 if b >= 0 else len(body)
    body = "".join(out)
    names = []
    for chunk in body.split(";"):
        p = chunk.find("(*")
        if p >= 0:
            q = chunk.find(")", p + 2)
            if q > p:
                names.append(chunk[p + 2:q].strip())
    return names

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--h510", required=True)
    ap.add_argument("--h618", required=True)
    ap.add_argument("--out-dir", required=True)
    a = ap.parse_args()
    os.makedirs(a.out_dir, exist_ok=True)
    NL = chr(10)
    out = []
    for struct in ("net_device_ops", "ethtool_ops"):
        n510 = extract(a.h510, struct)
        n618 = extract(a.h618, struct)
        pos618 = {}
        for i, n in enumerate(n618):
            pos618[n] = i
        out.append("== %s: 5.10 has %d members, 6.18 has %d ==" % (struct, len(n510), len(n618)))
        for i, n in enumerate(n510):
            p6 = pos618.get(n)
            if p6 is None:
                out.append("  slot %3d %-34s REMOVED in 6.18" % (i, n))
            elif p6 != i:
                out.append("  slot %3d %-34s moved to 6.18 slot %3d" % (i, n, p6))
        for i, n in enumerate(n618):
            if n not in n510:
                out.append("  NEW in 6.18 slot %3d: %s" % (i, n))
        out.append("")
    txt = NL.join(out)
    fence = "```"
    with open(os.path.join(a.out_dir, "vtable-order-drift.md"), "w") as f:
        f.write("# M3c vtable order drift" + NL + NL + fence + NL + txt + fence + NL)
    print(txt[:6500])

if __name__ == "__main__":
    main()
