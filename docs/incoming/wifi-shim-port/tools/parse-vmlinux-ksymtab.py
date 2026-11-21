#!/usr/bin/env python3
"""Parse a built vmlinux's __ksymtab/__ksymtab_gpl into TSV: name<TAB>kind<TAB>namespace

6.18 ARM layout (hexdump-verified on the built luofu vmlinux): 12-byte entries
{ unsigned long value; const char *name; const char *namespace; }, all absolute
addresses. Non-namespaced exports share one empty-string pointer for namespace.

Namespaced exports (namespace != "") are emitted but flagged: a plain-name module
import does NOT resolve against them without MODULE_IMPORT_NS.

Host-only, read-only.
Usage: python3 parse-vmlinux-ksymtab.py <vmlinux> > exports.tsv
"""
import re, struct, subprocess, sys

def sections(path):
    out = subprocess.run(["readelf", "-S", "-W", path], capture_output=True, text=True).stdout
    secs = {}
    for line in out.splitlines():
        m = re.match(r"^\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)", line)
        if m:
            secs[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16), int(m.group(4), 16))
    return secs

def main():
    path = sys.argv[1]
    with open(path, "rb") as f:
        data = f.read()
    secs = sections(path)
    need = {"__ksymtab", "__ksymtab_gpl", "__ksymtab_strings"}
    missing = need - set(secs)
    if missing:
        sys.exit("missing sections: %s" % sorted(missing))
    str_addr, str_off, str_size = secs["__ksymtab_strings"]

    def cstr_at(addr):
        nf = str_off + (addr - str_addr)
        if not (0 <= nf < len(data)):
            return None
        end = data.index(b"\x00", nf)
        return data[nf:end].decode("ascii", "replace")

    out = {}
    for sec, kind in (("__ksymtab", "sym"), ("__ksymtab_gpl", "gpl")):
        addr, off, size = secs[sec]
        for i in range(size // 12):
            e = off + i * 12
            value, name_ptr, ns_ptr = struct.unpack_from("<III", data, e)
            name = cstr_at(name_ptr)
            if name is None:
                continue
            ns = cstr_at(ns_ptr) or "?"
            if name not in out:
                out[name] = (kind, ns)
    for k in sorted(out):
        kind, ns = out[k]
        print("%s\t%s\t%s" % (k, kind, ns))

if __name__ == "__main__":
    main()
