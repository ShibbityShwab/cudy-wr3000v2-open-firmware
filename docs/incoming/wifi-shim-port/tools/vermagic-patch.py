#!/usr/bin/env python3
"""VERMAGIC PATCH - create a LOCAL, loadable copy of a vendor 5.10 .ko for our 6.18 kernel.

Strategy (see analysis/vermagic.md): rename the old vermagic= key and write our exact
vermagic string over the first consecutive run of expendable .modinfo entries
(import_ns / srcversion / version / author / description / parm / parmtype - none of
which the 6.18 loader uses; import_ns is inert because our vmlinux exports no
namespaced symbols). The kernel compares vermagic byte-for-byte and
CONFIG_MODULE_FORCE_LOAD is off, so the string must be exactly
"6.18.60 SMP mod_unload ARMv7 p2v8 ". Load the result with insmod (never modprobe).

Usage: python3 vermagic-patch.py <input.ko> <output.ko>
Host-only. Never commits or modifies the original .ko.
"""
import re, subprocess, sys

TARGET_VERMAGIC = b"6.18.60 SMP mod_unload ARMv7 p2v8 "   # our built 6.18.60 luofu kernel
KEY_VERMAGIC = b"vermagic="
EXPENDABLE = {b"import_ns", b"srcversion", b"version", b"author", b"description", b"parm", b"parmtype"}

def sh(args):
    return subprocess.run(args, capture_output=True, text=True).stdout

def modinfo_section(path):
    out = sh(["readelf", "-S", "-W", path])
    for line in out.splitlines():
        m = re.match(r"^\s*\[\s*\d+\]\s+\.modinfo\s+\S+\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)", line)
        if m:
            return int(m.group(2), 16), int(m.group(3), 16)
    raise SystemExit("no .modinfo section in " + path)

def entries(blob):
    out, i, n = [], 0, len(blob)
    while i < n:
        if blob[i] == 0:
            i += 1
            continue
        j = blob.index(b"\x00", i)
        s = blob[i:j]
        if b"=" in s:
            out.append((i, bytes(s.split(b"=", 1)[0]), j + 1))
        i = j + 1
    return out

def main():
    src, dst = sys.argv[1], sys.argv[2]
    with open(src, "rb") as f:
        data = bytearray(f.read())
    off, size = modinfo_section(src)
    blob = data[off:off + size]
    ents = entries(blob)
    vmags = [e for e in ents if e[1] == b"vermagic"]
    if len(vmags) != 1:
        raise SystemExit("expected exactly 1 vermagic= entry, found %d" % len(vmags))
    v_start, _, v_end = vmags[0]
    new = KEY_VERMAGIC + TARGET_VERMAGIC + b"\x00"
    # find first consecutive run of expendable entries with span >= len(new)
    best = None
    i = 0
    while i < len(ents):
        if ents[i][1] in EXPENDABLE:
            j = i
            while j < len(ents) and ents[j][1] in EXPENDABLE and ents[j][0] == ents[i][0] + (ents[j][0] - ents[i][0]):
                j += 1
            # consecutive-run check: entries are contiguous by construction of entries(); use span
            if ents[j - 1][2] - ents[i][0] >= len(new):
                best = (ents[i][0], ents[j - 1][2])
                break
            i = j
        else:
            i += 1
    if best is None:
        raise SystemExit("no expendable span >= %d bytes; extend EXPENDABLE set" % len(new))
    s_start, s_end = best
    if s_start >= v_start:
        raise SystemExit("span is after old vermagic; first-match order would break")
    blob[v_start:v_start + 9] = b"xermagic="           # neutralize old key, same length
    blob[s_start:s_start + len(new)] = new             # new key first in scan order
    blob[s_start + len(new):s_end] = b"\x00" * (s_end - s_start - len(new))
    data[off:off + size] = blob
    with open(dst, "wb") as f:
        f.write(data)
    out = sh(["readelf", "-p", ".modinfo", dst])
    ok = ("vermagic=6.18.60 SMP mod_unload ARMv7 p2v8 " in out
          and out.count("vermagic=") == 1 and "xermagic=" in out
          and "name=" in out and "license=" in out)
    print("vermagic line:", [l.strip() for l in out.splitlines() if "vermagic=" in l])
    print("PATCH %s" % ("OK" if ok else "FAILED"), "->", dst)

if __name__ == "__main__":
    main()
