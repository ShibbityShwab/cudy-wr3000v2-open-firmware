#!/usr/bin/env python3
"""M1: IMPORT CLOSURE + CLASSIFICATION for hi5622v100_{plat,wifi}.ko  (host-only, read-only)

Regenerates analysis/import-closure.csv and analysis/import-closure-summary.md.

Symbol-resolution semantics (verified in kernel/module.c of BOTH 5.10.201 and 6.18.60):
  - inter-module resolution = __ksymtab / __ksymtab_gpl exports ONLY (find_symbol);
  - our 6.18 vmlinux export pool is parsed directly from __ksymtab/__ksymtab_gpl
    (12-byte entries {value,name,namespace}); Module.symvers in build_dir is stale;
  - both kernels have CONFIG_MODVERSIONS off -> no CRC gate; CONFIG_MODULE_SIG off.

Usage:
  python3 m1-import-closure.py --plat <ko> --wifi <ko> --vendor-dirs DIR ... \
      --vmlinux <vmlinux> --out-dir DIR
"""
import argparse, collections, csv, os, re, struct, subprocess, sys

READELF = os.environ.get("READELF", "readelf")
MAX_DEPTH = 3

VENDOR_NS = re.compile(
    r"^(hi_|oal_|oam_|wal_|hal_|frw_|dmac_|hmac_|mpxx_|cal_|drv_|hcc_|scs_|"
    r"osal_|woc_|exception_|done_|ksecurec|g_|get_|msg_|wlan_)"
)
WIRELESS_HINT = re.compile(r"^(cfg80211_|ieee80211_|wiphy_|regulatory_|"
    r"reg_|nl80211|wdev_|__cfg80211|rdev_|rate_control_|rc80211|mesh_|wpan_)")
SAME_NAME_RISK = re.compile(r"^(cfg80211|ieee80211|wiphy|nl80211|netdev|netif|"
    r"skb|napi|dev_|ether|eth_|rtnl|genl|netlink|register_netdevice|"
    r"alloc_netdev|free_netdev|unregister_netdevice|gro_|nla_)")

def sh(args):
    return subprocess.run(args, capture_output=True, text=True, errors="replace").stdout

def readelf_und(path):
    out = sh([READELF, "-s", "-W", path])
    und = collections.OrderedDict()
    for line in out.splitlines():
        m = re.match(r"^\s*\d+:\s+[0-9a-fA-F]+\s+\d+\s+\S+\s+(\S+)\s+\S+\s+(\S+)\s+(.*)$", line)
        if not m:
            continue
        bind, ndx, name = m.group(1), m.group(2), m.group(3).strip()
        if ndx == "UND" and bind == "GLOBAL" and name:
            und.setdefault(name, bind)
    return und

def readelf_exports(path):
    out = sh([READELF, "-r", "-W", path])
    names, gpl, cur = set(), set(), None
    for line in out.splitlines():
        if line.startswith("Relocation section '.rel__ksymtab"):
            cur = "sym"; continue
        if line.startswith("Relocation section '.rel__ksymtab_gpl"):
            cur = "gpl"; continue
        if line.startswith("Relocation section"):
            cur = None; continue
        if cur:
            m = re.match(r"^\s*[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+R_ARM_ABS32\s+[0-9a-fA-F]+\s+(\S+)\s*$", line)
            if m:
                n = m.group(1)
                if n and not n.startswith(".") and n != "__ksymtab_strings":
                    (names if cur == "sym" else gpl).add(n)
    return names, gpl

def vmlinux_exports(path):
    """6.18 ARM ksymtab: 12-byte entries {u32 value; u32 name_ptr; u32 ns_ptr}, absolute addrs."""
    with open(path, "rb") as f:
        data = f.read()
    out = sh(["readelf", "-S", "-W", path])
    secs = {}
    for line in out.splitlines():
        m = re.match(r"^\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)", line)
        if m:
            secs[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16), int(m.group(4), 16))
    need = {"__ksymtab", "__ksymtab_gpl", "__ksymtab_strings"}
    missing = need - set(secs)
    if missing:
        sys.exit("vmlinux missing sections: %s" % sorted(missing))
    str_addr, str_off, str_size = secs["__ksymtab_strings"]
    pool = {}
    for sec, kind in (("__ksymtab", "sym"), ("__ksymtab_gpl", "gpl")):
        addr, off, size = secs[sec]
        for i in range(size // 12):
            e = off + i * 12
            value, name_ptr, ns_ptr = struct.unpack_from("<III", data, e)
            nf = str_off + (name_ptr - str_addr)
            if not (0 <= nf < len(data)):
                continue
            end = data.index(b"\x00", nf)
            name = data[nf:end].decode("ascii", "replace")
            if name and name not in pool:
                pool[name] = kind
    return pool

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--plat", required=True)
    ap.add_argument("--wifi", required=True)
    ap.add_argument("--vendor-dirs", nargs="+", required=True)
    ap.add_argument("--vmlinux", required=True)
    ap.add_argument("--out-dir", required=True)
    a = ap.parse_args()
    os.makedirs(a.out_dir, exist_ok=True)

    und_plat = readelf_und(a.plat)
    und_wifi = readelf_und(a.wifi)
    k6 = vmlinux_exports(a.vmlinux)

    modules = {}
    for d in a.vendor_dirs:
        for fn in sorted(os.listdir(d)):
            if not fn.endswith(".ko"):
                continue
            p = os.path.join(d, fn)
            exp, gpl = readelf_exports(p)
            modules[fn] = {"path": p, "exports": exp | gpl, "und": readelf_und(p)}

    wireless_510 = set()
    for mn in ("cfg80211.ko", "mac80211.ko"):
        if mn in modules:
            wireless_510 |= modules[mn]["exports"]

    prov = collections.defaultdict(set)
    for mn, mo in modules.items():
        for s in mo["exports"]:
            prov[s].add(mn)

    def classify(name):
        if name in k6:
            if SAME_NAME_RISK.match(name):
                return ("A_KERNEL_RISK", "")
            return ("A_KERNEL", "")
        p = prov.get(name)
        if p:
            return ("B_VENDOR_MODULE", ",".join(sorted(p)))
        if name in wireless_510 or WIRELESS_HINT.match(name):
            return ("C_WIRELESS_SHIM", "")
        if VENDOR_NS.match(name):
            return ("D_VENDOR_STUB", "")
        return ("E_OTHER_SHIM", "")

    need_modules = {}
    edges = collections.defaultdict(set)   # (from_mod, to_mod) -> set(symbols)
    frontier = [("hi5622v100_plat.ko", 0), ("hi5622v100_wifi.ko", 0)]
    while frontier:
        mn, depth = frontier.pop(0)
        if mn not in modules or depth > MAX_DEPTH or mn in need_modules:
            continue
        need_modules[mn] = depth
        for s in modules[mn]["und"]:
            if s in k6:
                continue
            pm = prov.get(s)
            if pm:
                for p in sorted(pm):
                    edges[(mn, p)].add(s)
                    if p not in need_modules:
                        frontier.append((p, depth + 1))

    # distinct symbol set: a symbol imported by both modules is ONE row with both flags
    union = collections.OrderedDict()
    for s in und_plat: union.setdefault(s, {"plat": True, "wifi": False})
    for s in und_wifi:
        if s in union: union[s]["wifi"] = True
        else: union[s] = {"plat": False, "wifi": True}
    rows = []
    for s, fl in union.items():
        cls, extra = classify(s)
        rows.append({"symbol": s, "plat": fl["plat"], "wifi": fl["wifi"],
                     "class": cls, "provider": extra})

    with open(os.path.join(a.out_dir, "import-closure.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["symbol", "plat", "wifi", "class", "provider"])
        w.writeheader()
        for r in sorted(rows, key=lambda r: (r["class"], r["symbol"])):
            w.writerow(r)

    stack_rows = []
    for mn in ("cfg80211.ko", "mac80211.ko", "compat.ko", "hmac.ko"):
        if mn in modules:
            for s in modules[mn]["und"]:
                cls, extra = classify(s)
                stack_rows.append({"module": mn, "symbol": s, "class": cls,
                                   "provider": extra})
    with open(os.path.join(a.out_dir, "vendor-stack-unds.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["module", "symbol", "class", "provider"])
        w.writeheader()
        for r in sorted(stack_rows, key=lambda r: (r["module"], r["class"], r["symbol"])):
            w.writerow(r)

    cnt = collections.Counter(r["class"] for r in rows)
    cp = collections.Counter(r["class"] for r in rows if r["plat"])
    cw = collections.Counter(r["class"] for r in rows if r["wifi"])
    sc = collections.Counter(r["class"] for r in stack_rows)

    # provider breakdown for the two target modules
    def prov_breakdown(rlist):
        c = collections.Counter()
        for r in rlist:
            if r["class"] == "B_VENDOR_MODULE":
                for p in r["provider"].split(","):
                    c[p] += 1
        return c
    pb = prov_breakdown(rows)
    pbp = prov_breakdown([r for r in rows if r["plat"]])
    pbw = prov_breakdown([r for r in rows if r["wifi"]])

    with open(os.path.join(a.out_dir, "import-closure-summary.md"), "w") as f:
        f.write("# M1 import closure - machine summary\n\n")
        f.write(f"- plat.ko GLOBAL UND: {len(und_plat)}   wifi.ko GLOBAL UND: {len(und_wifi)}   distinct: {len(union)}\n")
        f.write(f"- 6.18 vmlinux ksymtab export pool (parsed from __ksymtab/__ksymtab_gpl): {len(k6)} names\n")
        f.write(f"- vendor module pool: {len(modules)} .ko files\n")
        f.write(f"- vendor 5.10 wireless surface (cfg80211+mac80211 ksymtab): {len(wireless_510)}\n\n")
        f.write("## Combined classification (both modules)\n\n")
        for k in ["A_KERNEL", "A_KERNEL_RISK", "B_VENDOR_MODULE", "C_WIRELESS_SHIM", "D_VENDOR_STUB", "E_OTHER_SHIM"]:
            f.write(f"- {k}: {cnt.get(k, 0)}\n")
        f.write("\n## plat.ko\n\n")
        for k in ["A_KERNEL", "A_KERNEL_RISK", "B_VENDOR_MODULE", "C_WIRELESS_SHIM", "D_VENDOR_STUB", "E_OTHER_SHIM"]:
            f.write(f"- {k}: {cp.get(k, 0)}\n")
        f.write("\n## wifi.ko\n\n")
        for k in ["A_KERNEL", "A_KERNEL_RISK", "B_VENDOR_MODULE", "C_WIRELESS_SHIM", "D_VENDOR_STUB", "E_OTHER_SHIM"]:
            f.write(f"- {k}: {cw.get(k, 0)}\n")
        f.write("\n## vendor stack modules' own UNDs vs 6.18\n\n")
        for k in ["A_KERNEL", "A_KERNEL_RISK", "B_VENDOR_MODULE", "C_WIRELESS_SHIM", "D_VENDOR_STUB", "E_OTHER_SHIM"]:
            f.write(f"- {k}: {sc.get(k, 0)}\n")
        f.write("\n## B_VENDOR_MODULE provider breakdown (both modules)\n\n")
        for p, n in sorted(pb.items(), key=lambda kv: -kv[1]):
            f.write(f"- {p}: {n}  (plat {pbp.get(p,0)} / wifi {pbw.get(p,0)})\n")
        f.write("\n## closure (depth <= %d)\n\n" % MAX_DEPTH)
        for mn, d in sorted(need_modules.items(), key=lambda kv: (kv[1], kv[0])):
            f.write(f"- depth {d}: {mn}\n")
        f.write("\n## closure edges (dependent -> provider, symbol counts)\n\n")
        for (frm, to), syms in sorted(edges.items()):
            f.write(f"- {frm} -> {to}: {len(syms)} symbols\n")

    print("== closure modules ==")
    for mn, d in sorted(need_modules.items(), key=lambda kv: (kv[1], kv[0])):
        print("depth", d, mn)
    print("== combined ==", dict(cnt))
    print("== plat ==", dict(cp))
    print("== wifi ==", dict(cw))
    print("== stack ==", dict(sc))
    print("== provider breakdown ==")
    for p, n in sorted(pb.items(), key=lambda kv: -kv[1]):
        print("  ", p, n, "(plat", pbp.get(p, 0), "wifi", pbw.get(p, 0), ")")
    print("== shim-needed (C+D+E) ==")
    for r in sorted(rows, key=lambda r: r["symbol"]):
        if r["class"] in ("C_WIRELESS_SHIM", "D_VENDOR_STUB", "E_OTHER_SHIM"):
            print(" ", r["class"], r["symbol"], "plat" if r["plat"] else "", "wifi" if r["wifi"] else "")
    print("== A_KERNEL_RISK (same-name, family may have drifted) ==")
    for r in sorted(rows, key=lambda r: r["symbol"]):
        if r["class"] == "A_KERNEL_RISK":
            print(" ", r["symbol"], "plat" if r["plat"] else "", "wifi" if r["wifi"] else "")
    print("== wifi.ko wireless-family symbols and providers ==")
    for r in sorted(rows, key=lambda r: r["symbol"]):
        if r["wifi"] and (WIRELESS_HINT.match(r["symbol"]) or r["provider"].find("cfg80211") >= 0):
            print(" ", r["symbol"], "->", r["class"], r["provider"])

if __name__ == "__main__":
    main()
