#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""check-ladder.py - host-only consistency gate for the AP-start ladder recipe.

Validates recipes/ap_start_ladder.json against the hardware-validated BAR0
conventions (wifi-bench-tooling bench_guards.py) and the hostapd facts this
lane established on the host:

  * the boot/mask/staging readback addresses are the proven ones;
  * the firmware-context chain is arithmetically consistent
    (module-table slot0 0x829458 -> context-array CA 0x119758 ->
    ctx0 BAR0 = CA + 0x6B8000 = 0x7D2758 -> ctx0+0x190 = 0x7D28E8);
  * the ladder creates wlan0 as iw type __ap (hostapd v2.12 cannot change a
    managed interface into AP with this driver: change_virtual_intf returns
    -EOPNOTSUPP and hostapd aborts before NEW_BEACON);
  * the hostapd command only uses flags the tree-source hostapd supports;
  * the documented module parameters match the AP-start provider build;
  * the fixtures referenced by the recipe exist and are semantically sane.

It never contacts the device.  Exit 0 = PASS, 1 = FAIL, 2 = usage error.
"""
import argparse
import json
import os
import re
import shlex
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LANE = os.path.dirname(HERE)

BAR0_CPU_BASE = 0x6B8000          # firmware CPU address -> host BAR0 offset
CPU_G_SLOT0 = 0x829458            # BAR0 of the radio-object slot0 (G+0x630)
CTX0_CA = 0x119758                # boot-static context-array CA (TABLES T18)
CTX0_BAR0 = CTX0_CA + BAR0_CPU_BASE
BOOT_GATES = {
    "0x877000": "0x86807FEF",
    "0x877004": "0x7",
    "0x877008": "0x2",
    "0x87700C": "0xABCD",
    "0x7D0B60": "1",
    "0x6F8008": "0",
    "0x6F800C": "0",
}
HOSTAPD_FLAGS = {"-B", "-d", "-dd", "-i", "-P", "-v", "-q"}
REQUIRED_PARAMS = {
    "allow_frames", "frame_gap_ms", "h2d_send_budget", "vap_start_first",
    "vap_start_mode", "vap_start_module", "ap_start_mode",
}
STEPS = ["B0", "C2", "H0", "V1", "V2", "A1/A2", "A3", "A4", "Z"]

PASS = 0
FAIL = 0


def ok(msg):
    global PASS
    PASS += 1
    print("ok   %s" % msg)


def bad(msg):
    global FAIL
    FAIL += 1
    print("FAIL %s" % msg)


def check(cond, msg):
    if cond:
        ok(msg)
    else:
        bad(msg)
    return cond


def hexint(s):
    try:
        return int(str(s), 16)
    except (TypeError, ValueError):
        return None


def parse_config(path):
    kv = {}
    with open(path, "r", encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if "=" not in line:
                continue
            k, v = line.split("=", 1)
            kv[k.strip()] = v.strip()
    return kv


def main():
    ap = argparse.ArgumentParser(description="validate the AP-start ladder recipe")
    ap.add_argument("--ladder", default=os.path.join(LANE, "recipes",
                                                      "ap_start_ladder.json"))
    ap.add_argument("--json", action="store_true", help="machine-readable summary")
    args = ap.parse_args()
    try:
        with open(args.ladder, "r", encoding="utf-8") as fh:
            d = json.load(fh)
    except (OSError, ValueError) as exc:
        print("FAIL cannot read ladder JSON: %s" % exc)
        return 1
    print("ladder: %s" % args.ladder)

    # --- identity / scope -------------------------------------------------
    for key, want in (("lane", "wifi-hostapd-deploy"), ("task", "task-125"),
                      ("bench_only", True), ("attended", True),
                      ("never_contact_vendor_router", True)):
        check(d.get(key) == want, "top-level %s == %r" % (key, want))

    steps = d.get("steps") or []
    names = [s.get("step") for s in steps]
    check(names == STEPS, "step order is the documented ladder (%s)" % ",".join(STEPS))
    for s in steps:
        check(bool(s.get("action")), "step %s has an action" % s.get("step"))
        check(bool(s.get("expect")), "step %s has PASS evidence" % s.get("step"))
        check(bool(s.get("on_fail")), "step %s has a fail action" % s.get("step"))

    # --- hostapd facts ----------------------------------------------------
    hp = d.get("hostapd") or {}
    check(hp.get("binary") == "/usr/sbin/hostapd", "hostapd binary path")
    check("2.12" in str(hp.get("version", "")), "hostapd version is the tree-source v2.12")
    cmd = hp.get("start_command") or ""
    try:
        argv = shlex.split(cmd)
    except ValueError as exc:
        argv = []
        bad("hostapd start_command does not shell-split: %s" % exc)
    if argv:
        check(argv[0] == "hostapd", "start_command starts with hostapd")
        flags = [a for a in argv if a.startswith("-")]
        check(set(flags) <= HOSTAPD_FLAGS, "start_command uses supported flags only (%s)" % " ".join(flags))
        check("-B" in flags, "start_command daemonizes with -B (rc reflects start_ap)")
        if "-i" in argv:
            i = argv.index("-i")
            ifname = argv[i + 1] if i + 1 < len(argv) else ""
        else:
            ifname = ""
        check(ifname == "wlan0", "start_command interface is wlan0")
        check(bool(argv[-1].startswith("/root/hostapd-deploy/")),
              "start_command config lives under /root/hostapd-deploy/")
    rule = str(hp.get("iface_rule", ""))
    check("type __ap" in rule, "iface_rule demands iw type __ap")
    check("managed" in rule and "__ap" in rule, "iface_rule explains the managed->AP failure")
    v1 = next((s for s in steps if s.get("step") == "V1"), {})
    v1act = str(v1.get("action", ""))
    check("type __ap" in v1act, "V1 creates the VAP as type __ap")
    check("type managed" not in v1act, "V1 does not create a managed VAP")
    v2act = str(next((s for s in steps if s.get("step") == "V2"), {}).get("action", ""))
    check("iw dev wlan0 scan" in v2act, "V2 posts the mode-1 VAP base via a single-channel scan")
    v2text = v2act + " " + str(next((s for s in steps if s.get("step") == "V2"), {}).get("expect", ""))
    check("vap_add" in v2text and "vap_start" in v2text,
          "V2 documents the g0 id1 0x64/0x65 VAP-add/start pair")

    # --- module parameters ------------------------------------------------
    mp = d.get("module_params") or {}
    check(set(mp) - {"note"} == REQUIRED_PARAMS, "module parameter key set")
    check(mp.get("ap_start_mode") == 0, "ap_start_mode 0 (static g0 carrier)")
    check(mp.get("vap_start_first") == 1, "vap_start_first 1 (scan-triggered VAP base)")
    check(mp.get("vap_start_mode") == 1, "vap_start_mode 1 (g0 id1 keys)")

    # --- readback pack arithmetic vs the validated conventions ------------
    pack = {str(r.get("bar0", "")).upper(): r for r in (d.get("readback_pack") or [])}
    for addr, want in BOOT_GATES.items():
        entry = pack.get(addr.upper())
        check(entry is not None, "readback %s present" % addr)
        if entry is not None:
            check(str(entry.get("expect", "")).upper().startswith(str(want).upper()),
                  "readback %s expects %s" % (addr, want))
    slot = pack.get("0X829458")
    check(slot is not None and "0x00119758" in str(slot.get("expect", "")).lower(),
          "module-table slot0 readback expects 0x00119758")
    check(CTX0_BAR0 == 0x7D1758 and "0x119758" in str(pack.get(hex(CTX0_BAR0).upper(), {}).get("name", "")),
          "ctx0 CA 0x119758 -> BAR0 0x%X arithmetic follows the validated +0x6B8000 convention" % CTX0_BAR0)
    check(pack.get(hex(CTX0_BAR0).upper()) is not None,
          "ctx0 BAR0 0x%X present in the readback pack" % CTX0_BAR0)
    ctx_entry = pack.get(hex(CTX0_BAR0).upper(), {})
    check("0x7D1758" not in str(ctx_entry.get("expect", "")) and "7D2758" not in str(ctx_entry.get("expect", "")).upper(),
          "ctx0 readback does not carry the stale 0x7D2758 constant")
    vap_ptr_addr = hex(CTX0_BAR0 + 0x190).upper()
    check(pack.get(vap_ptr_addr) is not None,
          "ctx0+0x190 VAP pointer readback at BAR0 %s" % vap_ptr_addr)
    flags_addr = hex(CTX0_BAR0 + 0x18C).upper()
    check(pack.get(flags_addr) is not None,
          "ctx0+0x18C flags readback at BAR0 %s" % flags_addr)
    deref = [r for r in (d.get("readback_pack") or []) if "expr" in r]
    check(bool(deref), "VAP deref readbacks present (beacon interval / DTIM)")
    for r in deref:
        check("0x6B8000" in str(r.get("expr", "")), "deref expr uses the +0x6B8000 window")
    expect100 = any("100" in str(r.get("expect", "")) for r in deref)
    expect2 = any(str(r.get("expect", "")).startswith("2") for r in deref)
    check(expect100, "a deref readback expects beacon interval 100")
    check(expect2, "a deref readback expects DTIM 2")

    # --- PASS-gate text ---------------------------------------------------
    a = next((s for s in steps if s.get("step") == "A1/A2"), {})
    aexp = str(a.get("expect", ""))
    for pat in ("start_ap sequence frame 1/4", "ap_ssid", "ap_cfg",
                "refused=0", "timeout=0", "mask 0"):
        check(pat in aexp, "A1/A2 PASS gate mentions %r" % pat)
    stop = " ".join(d.get("stop_rules") or []).lower()
    for pat in ("exception mask", "staging", "same boot", "0x400392f0", "rmmod"):
        check(pat in stop, "stop rules mention %r" % pat)
    claim = " ".join(d.get("claim_rules") or []).lower()
    check("beacon" in claim and ("not observable" in claim or "out of scope" in claim),
          "claim rules bound the beacon claim")

    # --- config fixtures --------------------------------------------------
    cfg = d.get("hostapd_configs") or {}
    seen = 0
    for key, rel in cfg.items():
        m = re.search(r"(\.\./configs/[A-Za-z0-9_.-]+\.conf)", str(rel))
        if not m:
            bad("%s does not reference a configs/*.conf fixture" % key)
            continue
        path = os.path.normpath(os.path.join(LANE, "recipes", m.group(1)))
        if not os.path.isfile(path):
            bad("%s fixture missing: %s" % (key, path))
            continue
        seen += 1
        c = parse_config(path)
        check(c.get("interface") == "wlan0", "%s interface=wlan0" % os.path.basename(path))
        check(c.get("hw_mode") == "g", "%s hw_mode=g" % os.path.basename(path))
        check(c.get("channel") == "6", "%s channel=6" % os.path.basename(path))
        check(c.get("beacon_int") == "100", "%s beacon_int=100" % os.path.basename(path))
        check(c.get("dtim_period") == "2", "%s dtim_period=2" % os.path.basename(path))
        check(c.get("driver") == "nl80211", "%s driver=nl80211" % os.path.basename(path))
        if "open" in os.path.basename(path):
            check("wpa" not in c, "%s is open (no wpa= line)" % os.path.basename(path))
        else:
            check(c.get("wpa") == "2", "%s is WPA2" % os.path.basename(path))
    check(seen >= 2, "both fixture families referenced")

    # --- artifact pins ----------------------------------------------------
    pins = os.path.join(LANE, "tools", "expected-packages.txt")
    if os.path.isfile(pins):
        with open(pins, "r", encoding="utf-8") as fh:
            text = fh.read()
        check(bool(re.search(r"^hostapd-mini ", text, re.M)), "expected-packages lists hostapd-mini")
        check(bool(re.search(r"^hostapd-common ", text, re.M)), "expected-packages lists hostapd-common")
        check(bool(re.search(r"^ucode-mod-nl80211 ", text, re.M)), "expected-packages lists ucode-mod-nl80211")
        check(bool(re.search(r"^ucode-mod-rtnl ", text, re.M)), "expected-packages lists ucode-mod-rtnl")
    else:
        bad("missing tools/expected-packages.txt")

    print("\n== check-ladder: %d passed, %d failed ==" % (PASS, FAIL))
    if args.json:
        print(json.dumps({"passed": PASS, "failed": FAIL,
                          "result": "PASS" if FAIL == 0 else "FAIL"}))
    if FAIL:
        return 1
    print("RESULT: PASS - ladder recipe is consistent with the validated conventions")
    return 0


if __name__ == "__main__":
    sys.exit(main())