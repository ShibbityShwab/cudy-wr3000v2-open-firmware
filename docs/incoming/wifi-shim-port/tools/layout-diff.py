#!/usr/bin/env python3
"""M3a: extract offsetof tables from probe objects and diff 5.10 vs 6.18 layouts.

Compiles are done separately (see README/regeneration commands); this tool:
  1. parses tools/layout-probe.c for the per-version declaration order (tracking
     #ifdef VERSION510 guards and SIZEOF() entries),
  2. dumps the .offs section of each probe .o (arm objcopy),
  3. zips names with 4-byte LE values,
  4. diffs member offsets 5.10 -> 6.18 into analysis/struct-layout-drift.{csv,md}.

Usage: python3 layout-diff.py --probe510 raw/probe510.o --probe618 raw/probe618.o \
       --source tools/layout-probe.c --out-dir analysis --objcopy <cross-objcopy>
"""
import argparse, os, re, struct, subprocess, sys

def parse_order(src, version):
    """Yield (kind, name) in declaration order for the given version."""
    order, cond = [], True
    inside = False
    for line in open(src):
        s = line.strip()
        if s.startswith("#ifdef VERSION510"):
            inside = True
            continue
        if s.startswith("#endif") and inside:
            inside = False
            continue
        if inside and version != "510":
            continue
        m = re.match(r"^SIZEOF\((\w+)\)", s)
        if m:
            order.append(("sizeof", m.group(1)))
            continue
        m = re.match(r"^P\((\w+_t),\s*(\w+)\)", s)
        if m:
            order.append(("member", m.group(1)[:-2] + "." + m.group(2)))
    return order

def dump_offs(objcopy, obj, out):
    subprocess.run([objcopy, "-O", "binary", "--only-section=.offs", obj, out], check=True)
    with open(out, "rb") as f:
        data = f.read()
    return [struct.unpack_from("<I", data, i)[0] for i in range(0, len(data), 4)]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--probe510", required=True)
    ap.add_argument("--probe618", required=True)
    ap.add_argument("--source", required=True)
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--objcopy", required=True)
    a = ap.parse_args()
    os.makedirs(a.out_dir, exist_ok=True)
    o510 = parse_order(a.source, "510")
    o618 = parse_order(a.source, "618")
    v510 = dump_offs(a.objcopy, a.probe510, "/tmp/offs510.bin")
    v618 = dump_offs(a.objcopy, a.probe618, "/tmp/offs618.bin")
    assert len(o510) == len(v510), (len(o510), len(v510))
    assert len(o618) == len(v618), (len(o618), len(v618))
    t510 = {k: v for k, v in zip(o510, v510)}
    t618 = {k: v for k, v in zip(o618, v618)}

    rows = []
    for k in t510:
        if k not in t618:
            continue
        a5, a6 = t510[k], t618[k]
        rows.append((k[1], a5, a6, a6 - a5, "DRIFT" if a5 != a6 else "same"))
    rows.sort()
    with open(os.path.join(a.out_dir, "offsets-5-10-6-18.tsv"), "w") as f:
        for k in sorted(t510):
            if k[0] == "sizeof":
                f.write("sizeof:%s\t%d\t\n" % (k[1], t510[k]))
            elif k in t618:
                f.write("%s\t%d\t%d\n" % (k[1], t510[k], t618[k]))
    with open(os.path.join(a.out_dir, "struct-layout-drift.csv"), "w", newline="") as f:
        f.write("item,off_5_10,off_6_18,delta,verdict\n")
        for r in rows:
            f.write("%s,%d,%d,%d,%s\n" % r)
    drifted = [r for r in rows if r[4] == "DRIFT"]
    same = [r for r in rows if r[4] == "same"]
    with open(os.path.join(a.out_dir, "struct-layout-drift.md"), "w") as f:
        f.write("# M3a struct-layout drift (vendor 5.10.201 vs our 6.18.60, ARM)\n\n")
        f.write("Probe = tools/layout-probe.c compiled against both trees; offsets in bytes.\n\n")
        f.write("## sizeofs (5.10 vendor tree)\n\n")
        for k in sorted(t510):
            if k[0] == "sizeof":
                f.write("- %s: %d\n" % (k[1], t510[k]))
        f.write("\n## Drifted members: %d   same: %d\n\n" % (len(drifted), len(same)))
        for k, a5, a6, d, v in drifted:
            f.write("- %-42s 5.10=0x%03x  6.18=0x%03x  delta=%+d\n" % (k, a5, a6, d))
        f.write("\n## Unchanged members: %d\n" % len(same))
    print("probed items:", len(rows), "drifted:", len(drifted), "same:", len(same))
    print("sizeofs (5.10 vendor tree):")
    for k in sorted(t510):
        if k[0] == "sizeof":
            print("  ", k[1], "=", t510[k])
    print("drifted members:")
    for k, a5, a6, d, v in drifted:
        print("  %-40s 0x%03x -> 0x%03x  %+d" % (k, a5, a6, d))

if __name__ == "__main__":
    main()