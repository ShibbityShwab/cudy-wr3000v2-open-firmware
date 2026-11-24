#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
selftest.py - device-free driver selftest for the wifi-cfg80211 lane
(shared task task-104): source layout, cfg80211 wiring, op table/stubs,
op<->HCC mapping table shape, provider symvers and safety checks.

No device, no kernel, no vendor binary/firmware/calibration material.
Exit 0 = all checks passed.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LANE = os.path.dirname(HERE)
SRC = os.path.join(LANE, "src")
REF = os.path.join(LANE, "..", "wifi-h2d-send", "src")
KDIR = ("/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/"
        "target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60")

checks = 0
failed = 0


def check(cond, what):
    global checks, failed
    checks += 1
    if not cond:
        failed += 1
        print("FAIL: %s" % what)


def read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        return fh.read()


def exists(path):
    return os.path.isfile(path) and os.path.getsize(path) > 0


def main(argv):
    global KDIR
    i = 1
    while i < len(argv):
        if argv[i] == "--kdir" and i + 1 < len(argv):
            KDIR = argv[i + 1]
            i += 2
        else:
            i += 1

    # ---- 1. lane layout -------------------------------------------------
    required = {
        "src/Kbuild": os.path.join(SRC, "Kbuild"),
        "src/Makefile": os.path.join(SRC, "Makefile"),
        "src/.gitignore": os.path.join(SRC, ".gitignore"),
        "src/hi5622_cfg80211.h": os.path.join(SRC, "hi5622_cfg80211.h"),
        "src/hi5622_cfg80211.c": os.path.join(SRC, "hi5622_cfg80211.c"),
        "src/hi5622_cfg80211_ops.c": os.path.join(SRC, "hi5622_cfg80211_ops.c"),
        "src/hi5622_cfg80211_logic.h": os.path.join(SRC, "hi5622_cfg80211_logic.h"),
        "src/hi5622_cfg80211_logic.c": os.path.join(SRC, "hi5622_cfg80211_logic.c"),
        "src/cfg80211-provider.symvers": os.path.join(SRC, "cfg80211-provider.symvers"),
        "tools/build.sh": os.path.join(HERE, "build.sh"),
        "tools/gen_cfg80211_symvers.py": os.path.join(HERE, "gen_cfg80211_symvers.py"),
        "tools/cfg80211_logic_selftest.c": os.path.join(HERE, "cfg80211_logic_selftest.c"),
        "tools/selftest.py": os.path.join(HERE, "selftest.py"),
        "cfg80211.config": os.path.join(LANE, "cfg80211.config"),
        "README.md": os.path.join(LANE, "README.md"),
        "REPORT.md": os.path.join(LANE, "REPORT.md"),
        "BUILD-NOTES.md": os.path.join(LANE, "BUILD-NOTES.md"),
    }
    for name, path in required.items():
        check(exists(path), "required file missing/empty: %s" % name)

    kbuild = read(os.path.join(SRC, "Kbuild"))
    for obj in ("hi5622_cfg80211_logic.o", "hi5622_cfg80211.o",
                "hi5622_cfg80211_ops.o"):
        check(obj in kbuild, "Kbuild missing object %s" % obj)
    check("KBUILD_EXTRA_SYMBOLS" in kbuild and
          "cfg80211-provider.symvers" in kbuild,
          "Kbuild missing KBUILD_EXTRA_SYMBOLS provider declaration")
    check("obj-m := hi5622_wifi.o" in kbuild, "Kbuild module name changed")

    gitignore = read(os.path.join(SRC, ".gitignore"))
    for pat in ("*.o", "*.ko", ".*.cmd", "*.order", "modules.order"):
        check(pat in gitignore, ".gitignore missing build product %s" % pat)
    check("!cfg80211-provider.symvers" in gitignore,
          ".gitignore must un-ignore cfg80211-provider.symvers")

    # ---- 2. driver integration -----------------------------------------
    main_c = read(os.path.join(SRC, "main.c"))
    check('#include "hi5622_cfg80211.h"' in main_c,
          "main.c does not include hi5622_cfg80211.h")
    check("hi5622_cfg80211_init(ctx)" in main_c,
          "main.c does not call hi5622_cfg80211_init")
    check("hi5622_cfg80211_remove(ctx)" in main_c,
          "main.c does not call hi5622_cfg80211_remove")
    check('"cfg80211"' in main_c and "hi5622_cfg80211_add_attrs" in main_c,
          "main.c does not register the cfg80211 sysfs group")

    svc = read(os.path.join(SRC, "service.c"))
    check('#include "hi5622_cfg80211.h"' in svc,
          "service.c does not include hi5622_cfg80211.h")
    check("hi5622_cfg80211_ready(ctx)" in svc,
          "service.c does not call the cfg80211 readiness hook")

    drv = read(os.path.join(SRC, "hi5622_driver.h"))
    check("struct hi5622_cfg80211" in drv and "*cfg;" in drv,
          "hi5622_driver.h missing the cfg80211 state pointer")

    # ---- 3. cfg80211 capabilities + gated registration ------------------
    w = read(os.path.join(SRC, "hi5622_cfg80211.c"))
    check("wiphy_new_nm(&hi5622_cfg11_ops, sizeof(*c), NULL)" in w,
          "wiphy_new_nm call changed/absent")
    check(w.count("wiphy_register(c->wiphy)") == 1 and
          w.count("= wiphy_register(") == 1,
          "expected exactly one wiphy_register call site")
    reg_work = w[w.find("static void hi5622_cfg11_reg_work"):]
    reg_work = reg_work[:reg_work.find("\n}")]
    check("wiphy_register(c->wiphy)" in reg_work,
          "wiphy_register not inside the readiness work item")
    check("hi5622_cfg_reg_gate" in w, "registration not gated by the ready handshake")
    check("wiphy_unregister(c->wiphy)" in w, "wiphy_unregister missing in remove")
    check("wiphy_free(c->wiphy)" in w, "wiphy_free missing in remove")
    check("cancel_work_sync(&c->reg_work)" in w, "remove does not cancel the reg work")
    check("set_wiphy_dev(wiphy" in w, "set_wiphy_dev missing (wiphy parent)")
    check("BIT(NL80211_IFTYPE_STATION)" in w and "BIT(NL80211_IFTYPE_AP)" in w and
          "BIT(NL80211_IFTYPE_MONITOR)" in w, "interface_modes incomplete")
    check("NL80211_IFTYPE_WDS" not in w,
          "WDS must not be advertised (wiphy_register WARNs in 6.18)")
    check("max_scan_ssids = 12" in w, "vendor-derived max_scan_ssids missing")
    for ciph in ("WLAN_CIPHER_SUITE_WEP40", "WLAN_CIPHER_SUITE_TKIP",
                 "WLAN_CIPHER_SUITE_CCMP", "WLAN_CIPHER_SUITE_GCMP",
                 "WLAN_CIPHER_SUITE_AES_CMAC"):
        check(ciph in w, "cipher suite missing: %s" % ciph)
    check("0x02, 0x00, 0x68, 0x69, 0x35, 0x00" in w,
          "documentation perm_addr prefix changed")
    check(w.count("HI5622_CH2G(") - 1 == 13,
          "2.4 GHz channel table must have 13 channels")
    check(w.count("HI5622_CH5G(") - 1 == 19,
          "5 GHz channel table must have the 19 LU-plan channels")
    for absent in ("HI5622_CH5G(144", "HI5622_CH5G(149", "HI5622_CH5G(165"):
        check(absent not in w, "out-of-plan 5 GHz channel present: %s" % absent)
    check("NL80211_BAND_2GHZ" in w and "NL80211_BAND_5GHZ" in w,
          "per-band wiphy tables missing")
    check("hi5622_cfg80211_ready" in w, "readiness hook missing")
    check("cfg80211_auto" in w and "band_force" in w,
          "module parameters missing")
    check("dev_warn" in w, "no warnings for the unproven domain->band map")
    for bad in ("writel(", "readl(", "hi5622_io_bar0", "0x400392f0",
                "0x400392F0", "request_firmware"):
        check(bad not in w, "device access forbidden in cfg80211 layer: %s" % bad)

    # ---- 4. ops table + stubs ------------------------------------------
    o = read(os.path.join(SRC, "hi5622_cfg80211_ops.c"))
    table = o[o.find("const struct cfg80211_ops hi5622_cfg11_ops"):]
    ops = re.findall(r"\.([a-z_]+)\s*=\s*hi5622_", table)
    expected_ops = {"add_virtual_intf", "del_virtual_intf",
                    "change_virtual_intf", "get_station", "dump_station",
                    "scan"}
    check(set(ops) == expected_ops,
          "ops table mismatch: got %s" % sorted(ops))
    check(len(ops) == 6, "expected exactly 6 populated cfg80211 ops")
    for absent in ("start_ap", "stop_ap", "add_key", "connect", "mgmt_tx",
                   "set_wiphy_params", "set_power_mgmt"):
        check(".%s\t" % absent not in table and ".%s " % absent not in table,
              "deferred op must not be registered: %s" % absent)
    check(o.count("return -EOPNOTSUPP") >= 5,
          "M2 stubs must return -EOPNOTSUPP cleanly")
    check("ndo_start_xmit" in o and "dev_kfree_skb" in o,
          "netdev TX drop stub missing (bring-up oops protection)")
    check("needs_free_netdev = true" in o,
          "VAP netdev must be freed by the netdev core")
    check("cfg80211_register_netdevice(dev)" in o,
          "cfg80211_register_netdevice call missing")
    check("cfg80211_unregister_wdev(wdev)" in o,
          "del_virtual_intf must call cfg80211_unregister_wdev")
    check("cfg80211_unregister_netdevice(" not in o,
          "must not use the CONFIG_CFG80211-conditional inline")
    check("Do NOT kfree(wdev)" in o,
          "del path must not free the wdev inside the netdev priv")
    check("kfree(wdev);" not in o,
          "del path frees the wdev (use-after-free risk)")
    check("ARPHRD_IEEE80211_RADIOTAP" in o, "monitor interface type not set")
    for bad in ("writel(", "readl(", "hi5622_io_bar0", "0x400392f0"):
        check(bad not in o, "device access forbidden in ops layer: %s" % bad)

    # ---- 5. portable model / op<->HCC map ------------------------------
    lc = read(os.path.join(SRC, "hi5622_cfg80211_logic.c"))
    lh = read(os.path.join(SRC, "hi5622_cfg80211_logic.h"))
    check("HI5622_CFG_MAX_VAPS\t16" in lh or
          re.search(r"#define HI5622_CFG_MAX_VAPS\s+16", lh) is not None,
          "VAP slot count not 16")
    op_section = lc[lc.find("static const struct hi5622_op_map_entry hi5622_op_map[]"):
                    lc.find("const struct hi5622_op_map_entry *hi5622_cfg_op_map(")]
    map_rows = len(re.findall(r'\t\{ "[a-z_0-9]+",', op_section))
    check(map_rows == 39,
          "op<->HCC map must cover the vendor's 39 populated ops (got %d)" % map_rows)
    ref_section = lc[lc.find("static const struct hi5622_hcc_msg_ref hi5622_msg_refs[]"):
                     lc.find("const struct hi5622_hcc_msg_ref *hi5622_cfg_hcc_msg_refs(")]
    ref_rows = len(re.findall(r'\t\{ "', ref_section))
    check(ref_rows >= 19, "HCC message reference table too small (%d)" % ref_rows)
    for needle in ('"add_virtual_intf"', '"del_virtual_intf"', '"scan"',
                   '"get_station"', '"start_ap"', '"connect"'):
        check(needle in op_section, "op map missing %s" % needle)
    check("0x0007" in op_section and "0x0009" in op_section and
          "0x000c" in op_section,
          "op map missing the vendor user/vap and scan ids")
    check("ctx_create" in lc and "0x64" in lc,
          "context-create precondition not referenced in the frame table")
    check("task-103" in lc and "task-103" in o,
          "pending builders must reference task-103 (wifi-proto-seq)")
    check("hi5622_cfg_reg_gate" in lc and "HI5622_REG_NOW" in lc,
          "registration gate logic missing")

    # ---- 6. cfg80211 provider symbols + kernel config ------------------
    check("wiphy_new_nm" in read(os.path.join(SRC, "cfg80211-provider.symvers")),
          "provider symvers missing wiphy_new_nm")
    gen = os.path.join(HERE, "gen_cfg80211_symvers.py")
    if os.path.isfile(gen):
        try:
            res = subprocess.run([sys.executable, gen, "--check", "--kdir", KDIR,
                                  "--src", SRC], capture_output=True, text=True,
                                 check=False)
            check(res.returncode == 0,
                  "provider symvers check failed: %s" % res.stdout.strip())
        except OSError:
            check(False, "cannot execute gen_cfg80211_symvers.py")
    cfg = read(os.path.join(LANE, "cfg80211.config"))
    check("CONFIG_CFG80211=m" in cfg, "cfg80211.config must set CONFIG_CFG80211=m")
    check("CONFIG_WIRELESS=y" in cfg and "CONFIG_RFKILL=y" in cfg,
          "cfg80211.config must list the required base options")
    check("CONFIG_CFG80211_HEADERS" in cfg,
          "cfg80211.config must mention CFG80211_HEADERS (selected by CFG80211)")

    # ---- 7. docs / evidence discipline --------------------------------
    rep = read(os.path.join(LANE, "REPORT.md"))
    for needle in ("M0", "M1", "M2", "add_virtual_intf", "g0 id7",
                   "g0 id0xC", "cfg80211_register_netdevice", "task-103",
                   "PD4"):
        check(needle in rep, "REPORT.md missing %s" % needle)
    check("no vendor binary" in rep.lower() or "no vendor" in rep.lower(),
          "REPORT.md must state the no-vendor-material policy")
    readme = read(os.path.join(LANE, "README.md"))
    check("wifi-cfg80211" in readme and "M0-M2" in readme,
          "README.md missing lane/milestone identification")

    # ---- 8. inherited tree unchanged except the documented files -------
    allowed = {"main.c", "service.c", "hi5622_driver.h", "Kbuild",
               ".gitignore"}
    if os.path.isdir(REF):
        for name in sorted(os.listdir(REF)):
            rp = os.path.join(REF, name)
            sp = os.path.join(SRC, name)
            if not os.path.isfile(rp) or not os.path.isfile(sp):
                continue
            if name.endswith(".mod.c") or name.startswith("."):
                continue
            if not name.endswith((".c", ".h", ".inc", ".md")) and \
               name not in ("Kbuild", "Makefile", ".gitignore"):
                continue
            same = read(rp) == read(sp)
            if name in allowed:
                continue
            check(same, "copied protocol file changed unexpectedly: %s" % name)
    else:
        print("NOTE: reference tree %s absent - parity checks skipped" % REF)

    # ---- 9. no vendor/secret material in the lane ----------------------
    bad_ext = (".bin", ".ko", ".o", ".mod", ".ini", ".cal", ".dat", ".fw")
    ignored = ("src/.gitignore",)
    for root, dirs, files in os.walk(LANE):
        dirs[:] = [d for d in dirs if d != "__pycache__"]
        for f in files:
            p = os.path.join(root, f)
            rel = os.path.relpath(p, LANE)
            if rel in ignored:
                continue
            if f.endswith(bad_ext):
                print("NOTE: build/local artifact present (must stay "
                      "gitignored): %s" % rel)
    check(exists(os.path.join(LANE, "cfg80211.config")),
          "kernel config fragment missing")

    print("\n%d/%d checks passed" % (checks - failed, checks))
    if failed:
        print("FAILED: %d check(s)" % failed)
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
