#!/usr/bin/env python3
"""M3d: manual spot-verification of census hits - prints the imm-offset r0-r3
loads/stores of selected functions so a human can confirm pointer identity.

Usage: python3 m3-verify.py
"""
import os, re

def parse_dis(path):
    funcs = {}
    cur = None
    for line in open(path, errors="replace"):
        line = line.rstrip()
        m = re.match(r"^([0-9a-fA-F]{8}) <([^>]+)>:", line)
        if m:
            cur = m.group(2)
            funcs[cur] = []
            continue
        if cur is not None:
            m = re.match(r"^\s*([0-9a-fA-F]+):\s+[0-9a-fA-F]{8}\s+(\S+)\s*(.*)$", line)
            if m:
                funcs[cur].append((int(m.group(1), 16), m.group(2) + " " + m.group(3)))
    return funcs

def main():
    base = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(base)
    dis_map = {
        "wifi": os.path.join(root, "raw", "hi5622v100_wifi.dis"),
        "cfg80211": os.path.join(root, "raw", "cfg80211.dis"),
        "plat": os.path.join(root, "raw", "hi5622v100_plat.dis"),
    }
    funcs = {k: parse_dis(v) for k, v in dis_map.items()}
    targets = [
        ("wifi", "wal_netdev_set_mac_addr"),
        ("wifi", "hmac_tx_event_process"),
        ("wifi", "oal_net_device_change_mtu"),
        ("wifi", "hmac_update_fbt_scan_result"),
        ("wifi", "oal_cfg80211_external_auth_request"),
        ("wifi", "hmac_config_send_meas_req"),
        ("wifi", "hmac_ap_up_rx_mgmt"),
        ("cfg80211", "get_wiphy_regdom"),
        ("cfg80211", "nl80211_del_key"),
        ("cfg80211", "cfg80211_conn_work"),
        ("cfg80211", "cfg80211_disconnected"),
        ("cfg80211", "cfg80211_register_netdevice"),
    ]
    for mod, fn in targets:
        print("== %s [%s] ==" % (fn, mod))
        if fn not in funcs[mod]:
            print("   (not found)")
            continue
        shown = 0
        for a, t in funcs[mod][fn]:
            m = re.search(r"\[r([0-3]), #(-?\d+)\]", t)
            if m:
                print("   %-8s %s" % (hex(a), t))
                shown += 1
            if shown >= 10:
                break
        if shown == 0:
            print("   (no imm-offset r0-r3 loads/stores)")

if __name__ == "__main__":
    main()
