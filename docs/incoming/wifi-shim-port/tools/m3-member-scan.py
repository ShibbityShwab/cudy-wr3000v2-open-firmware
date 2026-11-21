#!/usr/bin/env python3
"""M3b: disassembly member-access census for the binary wifi lane.

Parses the objdump -dr dumps (raw/*.dis), collects immediate-offset accesses on
r0-r3, and counts those that coincide with 5.10 member offsets of the audited
structs - GATED by per-function struct hints (callee-prefix sets, propagated over
local calls). This yields an upper bound; the report verifies top hits by hand.

Danger classes:
  KERNEL-OWNED on our 6.18 (must NOT be accessed at 5.10 offsets):
      net_device, sk_buff, device, genl_info
  VENDOR-OWNED (consistent between driver and vendor cfg80211.ko -> safe):
      wiphy, wireless_dev, ieee80211_channel, ieee80211_supported_band,
      cfg80211_chan_def, cfg80211_scan_request, cfg80211_connect_params, cfg80211_bss

Usage: python3 m3-member-scan.py --dis raw/<mod>.dis --tables analysis/offsets-5-10-6-18.tsv \
       --out analysis/member-audit-<mod>.csv --mod <name>
"""
import argparse, collections, re, sys

def load_tables(path):
    t = {}
    for line in open(path):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < 3:
            continue
        name, o5, o6 = parts[0], int(parts[1]), int(parts[2])
        struct, member = name.split(".", 1)
        t.setdefault(struct, {})[member] = (o5, o6)
    return t

def parse_dis(path):
    funcs = collections.OrderedDict()
    cur_func, cur_sec = None, None
    data_relocs = []
    data_sec = None
    for line in open(path, errors="replace"):
        line = line.rstrip()
        m = re.match(r"^Disassembly of section (\.\S+):", line)
        if m:
            cur_sec = m.group(1)
            cur_func = None
            data_sec = cur_sec if cur_sec.startswith((".data", ".rodata", ".rel")) else None
            continue
        m = re.match(r"^([0-9a-fA-F]{8}) <([^>]+)>:", line)
        if m:
            name = m.group(2)
            if cur_sec and cur_sec.startswith((".data", ".rodata", ".bss", ".ref.data")):
                cur_func = None
                data_sec = cur_sec
                continue
            cur_func = name
            funcs[name] = {"addr": int(m.group(1), 16), "sec": cur_sec,
                           "calls": set(), "ops": [], "relocs": {}}
            continue
        if cur_func is None:
            m = re.match(r"^\s+([0-9a-fA-F]+):\s+R_ARM_\S+\s+(\S+)$", line)
            if m and data_sec:
                data_relocs.append((data_sec, int(m.group(1), 16), m.group(2)))
            continue
        m = re.match(r"^\s+([0-9a-fA-F]+):\s+R_ARM_(\S+)\s+(\S+)$", line)
        if m:
            funcs[cur_func]["relocs"][int(m.group(1), 16)] = m.group(3)
            continue
        m = re.match(r"^\s*([0-9a-fA-F]+):\s+([0-9a-fA-F]{8})\s+(\S+)\s*(.*)$", line)
        if m:
            addr = int(m.group(1), 16)
            ins = m.group(3)
            rest = m.group(4).strip()
            f = funcs[cur_func]
            if ins.startswith("bl"):
                m2 = re.match(r"^\S+\s*<(\S+)>$", rest)
                if m2:
                    f["calls"].add(m2.group(1))
            if addr in f["relocs"]:
                f["calls"].add(f["relocs"][addr])
            m2 = re.match(r"^(r\d+),\s*\[r(\d+),\s*#(-?\d+)\]", rest)
            if m2:
                f["ops"].append((addr, ins, int(m2.group(2)), int(m2.group(3)),
                                 1 if ins.startswith("str") else 0))
    return funcs, data_relocs

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dis", required=True)
    ap.add_argument("--tables", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--mod", default="")
    a = ap.parse_args()
    tables = load_tables(a.tables)
    funcs, data_relocs = parse_dis(a.dis)

    KERNEL_OWNED = {"net_device", "sk_buff", "device", "genl_info"}
    VENDOR_OWNED = {"wiphy", "wireless_dev", "ieee80211_channel",
                    "ieee80211_supported_band", "cfg80211_chan_def",
                    "cfg80211_scan_request", "cfg80211_connect_params", "cfg80211_bss"}
    OWNED = KERNEL_OWNED | VENDOR_OWNED

    HINT_FNS = {
        "net_device": ("alloc_netdev", "free_netdev", "register_netdevice",
                       "unregister_netdevice", "netif_", "netdev_", "ether_setup",
                       "eth_type_trans", "dev_get_by_name", "dev_open", "dev_close",
                       "napi_", "alloc_etherdev", "__netdev_alloc_skb"),
        "sk_buff": ("skb_", "__alloc_skb", "alloc_skb", "__netdev_alloc_skb",
                    "kfree_skb", "consume_skb", "dev_kfree_skb_any",
                    "__dev_kfree_skb_any", "eth_type_trans", "netif_rx",
                    "netif_receive_skb", "dev_queue_xmit", "netif_skb"),
        "genl_info": ("genlmsg_", "nlmsg_", "nla_", "genl_", "netlink_"),
        "device": ("dev_", "device_", "dma_", "sysfs_"),
        "wiphy": ("wiphy_", "cfg80211_"),
        "wireless_dev": ("wiphy_", "cfg80211_"),
    }
    for s0 in VENDOR_OWNED - {"wiphy", "wireless_dev"}:
        HINT_FNS[s0] = ("wiphy_", "cfg80211_")

    hints = {}
    for fn, f in funcs.items():
        h = set()
        for struct, prefixes in HINT_FNS.items():
            if any(any(c.startswith(p) for p in prefixes) for c in f["calls"]):
                h.add(struct)
        hints[fn] = h
    for _ in range(2):
        for fn, f in funcs.items():
            for c in f["calls"]:
                if c in hints:
                    hints[fn] |= hints[c]

    hits = []
    for fn, f in funcs.items():
        fh = hints[fn]
        if not fh:
            continue
        reg_ops = collections.defaultdict(list)
        for (addr, mnem, base, imm, store) in f["ops"]:
            if base <= 3 and imm >= 0:
                reg_ops["r%d" % base].append((addr, mnem, imm, store))
        for reg, ops in reg_ops.items():
            for struct in fh:
                if struct not in tables:
                    continue
                mem_off = {}
                for mem, (o5, o6) in tables[struct].items():
                    mem_off.setdefault(o5, []).append(mem)
                for (addr, mnem, imm, store) in ops:
                    if imm in mem_off:
                        for mem in mem_off[imm]:
                            kind = "KERNEL" if struct in KERNEL_OWNED else "VENDOR"
                            hits.append((fn, addr, struct, mem, imm,
                                         tables[struct][mem][1], kind))

    agg = collections.defaultdict(lambda: collections.defaultdict(set))
    for (fn, addr, struct, mem, off510, off618, kind) in hits:
        agg[(struct, mem)][fn].add(addr)
    rows = []
    for (struct, mem), fns in sorted(agg.items()):
        o5, o6 = tables[struct][mem]
        kind = "KERNEL" if struct in KERNEL_OWNED else "VENDOR"
        drifted = o5 != o6
        rows.append((struct, mem, o5, o6, "DRIFT" if drifted else "same", kind,
                     len(fns), ",".join(sorted(fns)[:8])))
    with open(a.out, "w") as f:
        f.write("struct,member,off_5_10,off_6_18,drift,owner,hit_funcs,hit_count,examples\n")
        for r in rows:
            f.write("%s,%s,0x%x,0x%x,%s,%s,%d,%s\n" %
                    (r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]))
    kc = sum(1 for r in rows if r[5] == "KERNEL")
    kd = sum(1 for r in rows if r[5] == "KERNEL" and r[4] == "DRIFT")
    vc = sum(1 for r in rows if r[5] == "VENDOR")
    vd = sum(1 for r in rows if r[5] == "VENDOR" and r[4] == "DRIFT")
    print("module:", a.mod, "distinct member hits:", len(rows))
    print("  KERNEL-owned:", kc, "(drifted:", kd, ")")
    print("  VENDOR-owned:", vc, "(drifted:", vd, ")")
    print("top KERNEL drifted hits:")
    for r in sorted(rows, key=lambda r: -r[6]):
        if r[5] == "KERNEL" and r[4] == "DRIFT":
            print("   %s.%s 0x%x->0x%x funcs=%d e.g. %s" % (r[0], r[1], r[2], r[3], r[6], r[7][:120]))
    print("top VENDOR hits (informational):")
    for r in sorted(rows, key=lambda r: -r[6]):
        if r[5] == "VENDOR":
            print("   %s.%s 0x%x->0x%x funcs=%d e.g. %s" % (r[0], r[1], r[2], r[3], r[6], r[7][:100]))
            break

if __name__ == "__main__":
    main()
