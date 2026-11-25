#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Static validation for the wifi-image-polish lane (host-only).

Checks the shipped rootfs payload, the hostapd fixtures and the report/refs
against each other, so the lane cannot drift into a config that the driver
cannot support or a document that disagrees with the validated artifacts.

    ./tools/validate.py                 # static checks only
    ./tools/validate.py --hostapd BIN   # + hostapd v2.12 config-parse gate

Exit code 0 = all checks pass.  No device, no network, no root.
"""
import argparse
import os
import shlex
import shutil
import subprocess
import sys
import tempfile

LANE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOTFS = os.path.join(LANE, "rootfs")

FAILS = []
CHECKS = 0


def ok(msg):
    global CHECKS
    CHECKS += 1
    print("ok   " + msg)


def bad(msg):
    global CHECKS
    CHECKS += 1
    FAILS.append(msg)
    print("FAIL " + msg)


def check(cond, msg):
    ok(msg) if cond else bad(msg)
    return cond


def read(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def read_conf(path):
    """Parse key=value hostapd-style config into a dict (comments stripped)."""
    out = {}
    for raw in read(path).splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            out.setdefault("__bare__", []).append(line)
            continue
        k, v = line.split("=", 1)
        out[k.strip()] = v.strip()
    return out


def parse_uci(path):
    """Minimal /etc/config parser: {section_name: {option: value}}."""
    sections = {}
    cur = None
    for raw in read(path).splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = shlex.split(line, comments=True)
        if not parts:
            continue
        if parts[0] == "config":
            name = parts[2] if len(parts) > 2 else parts[1]
            cur = name
            sections[cur] = {"__type__": parts[1]}
        elif parts[0] == "option" and cur is not None and len(parts) >= 3:
            sections[cur][parts[1]] = parts[2]
    return sections


def code_lines(text):
    """Non-comment lines, stripped (policy words in comments are not commands)."""
    out = []
    for raw in text.splitlines():
        s = raw.strip()
        if not s or s.startswith("#"):
            continue
        out.append(s)
    return out


def command_names(text):
    """First token of every non-comment line (basename), to catch real calls."""
    names = []
    for line in code_lines(text):
        tok = line.split()[0]
        names.append(tok.rsplit("/", 1)[-1])
    return names


def parse_modprobe(path):
    opts = {}
    for raw in read(path).splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if not line.startswith("options "):
            continue
        parts = line.split()
        if len(parts) < 2:
            continue
        name = parts[1]
        for kv in parts[2:]:
            k, _, v = kv.partition("=")
            opts[k] = v
    return opts


def syntax_check(path, label):
    if not os.path.exists(path):
        bad(label + ": missing " + path)
        return
    r = subprocess.run(["sh", "-n", path], capture_output=True, text=True)
    check(r.returncode == 0, label + ": sh -n clean" + ("" if r.returncode == 0 else " -> " + r.stderr.strip()))


# ---------------------------------------------------------------------------
# artifact / driver facts pinned from the flashed self-contained image
# (docs/incoming/wifi-image-polish/refs/artifact-identity.txt)
# ---------------------------------------------------------------------------
IMAGE_MODULE_SHA = "51608fd556fa474d2da0dd88975c2a8b610301fb6b8a22d4b6e0589ade247a78"
BASE_MODULE_SHA = "44c6cc12b5c43d47b4b0ccd1da693c77b00e5ef48912625add52370ea4612c0b"
POLISHED_MODULE_SHA = "d145a2050cfcba7511d052da6eb67478dac75aa7285a1ad9c44b3dba822bd2c6"

# parameters exposed by the hardware-validated image module (19; read with
# modinfo from /lib/modules/6.18.60/hi5622_wifi.ko, sha 51608fd5...).
IMAGE_PARAMS = {
    "scan_trust_rssi", "vap_start_first", "vap_start_mode", "vap_start_module",
    "ap_start_mode", "scan_timeout_ms", "scan_dwell_ms", "allow_frames",
    "frame_gap_ms", "cfg80211_auto", "band_force", "h2d_send_budget",
    "hcc_poll_ms", "hcc_cece_mode", "rd_ab_write", "fw_path",
    "wifi_block_path", "plat_block_path", "allow_unseeded_release",
}

DEPLOYED_DEFAULTS = {
    "fw_path": "hi_wifi/FIRMWARE.PD4.bin",
    "wifi_block_path": "hi_wifi/wifi_block.bin",
    "allow_frames": "16",
    "frame_gap_ms": "100",
    "h2d_send_budget": "4",
    "vap_start_first": "1",
    "vap_start_mode": "1",
    "vap_start_module": "0",
    "scan_dwell_ms": "60",
    "hcc_poll_ms": "5",
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hostapd", default="", help="hostapd binary for the config-parse gate")
    args = ap.parse_args()

    # 1. rootfs payload present
    required = [
        "etc/config/hi5622",
        "etc/init.d/hi5622-wifi",
        "etc/rc.d/S96hi5622-wifi",
        "etc/modprobe.d/hi5622-wifi.conf",
        "etc/hi5622/FIRMWARE-POLICY.txt",
        "etc/hi5622/hostapd-open.proven.conf",
        "etc/hi5622/hostapd-open.conf",
        "etc/hi5622/hostapd-wpa2.conf",
        "etc/hi5622/hostapd-wpa2-wpa3.conf",
        "usr/sbin/hi5622-load",
    ]
    for rel in required:
        check(os.path.isfile(os.path.join(ROOTFS, rel)), "rootfs payload present: " + rel)

    # 2. executable scripts + syntax
    for rel in ("usr/sbin/hi5622-load", "etc/init.d/hi5622-wifi"):
        p = os.path.join(ROOTFS, rel)
        if os.path.isfile(p):
            check(os.access(p, os.X_OK), "rootfs exec bit: " + rel)
            syntax_check(p, "rootfs script " + rel)

    # 3. rc.d enable link
    link = os.path.join(ROOTFS, "etc/rc.d/S96hi5622-wifi")
    if os.path.islink(link):
        check(os.readlink(link) == "../init.d/hi5622-wifi",
              "rc.d S96 link -> ../init.d/hi5622-wifi")
    else:
        bad("rc.d S96 is not a symlink (a plain file would shadow enable semantics)")

    # 4. uci policy config
    uci_path = os.path.join(ROOTFS, "etc/config/hi5622")
    if os.path.isfile(uci_path):
        uci = parse_uci(uci_path)
        check(uci.get("main", {}).get("__type__") == "driver",
              "uci: section 'main' is type driver")
        check(uci.get("ap", {}).get("__type__") == "ap", "uci: section 'ap' is type ap")
        mainsec = uci.get("main", {})
        check(mainsec.get("autoload") == "0",
              "uci: bench policy default autoload=0 (driver boot ladder is attended)")
        for key, val in DEPLOYED_DEFAULTS.items():
            check(mainsec.get(key) == val,
                  "uci: main." + key + " = " + val)
        check(mainsec.get("c2_ladder") == "1", "uci: main.c2_ladder = 1")
        check(mainsec.get("strict") == "1", "uci: main.strict = 1")
        check(mainsec.get("wiphy_timeout") == "30", "uci: main.wiphy_timeout = 30")
        apsec = uci.get("ap", {})
        check(apsec.get("enabled") == "0", "uci: bench policy default ap.enabled=0")
        check(apsec.get("ifname") == "wlan0", "uci: ap.ifname = wlan0")
        check(apsec.get("type") == "__ap", "uci: ap.type = __ap (proven iftype)")
        check(apsec.get("config") == "/etc/hi5622/hostapd-open.conf",
              "uci: ap.config points at the packaged open config")

    # 5. modprobe.d mirrors the uci defaults (insmod path is the real one)
    mp_path = os.path.join(ROOTFS, "etc/modprobe.d/hi5622-wifi.conf")
    if os.path.isfile(mp_path):
        mp = parse_modprobe(mp_path)
        unknown = sorted(set(mp) - IMAGE_PARAMS)
        check(not unknown, "modprobe.d: only image-module parameters used"
              + ("" if not unknown else " -> unknown " + ", ".join(unknown)))
        check("mgmt_diag" not in mp,
              "modprobe.d: no mgmt_diag (not in the hardware-validated module)")
        for key, val in DEPLOYED_DEFAULTS.items():
            check(mp.get(key) == val,
                  "modprobe.d: " + key + " = " + val)

    # 6. loader: explicit insmod path, no modprobe, no rmmod, correct C2 attrs
    loader_path = os.path.join(ROOTFS, "usr/sbin/hi5622-load")
    if os.path.isfile(loader_path):
        ld = read(loader_path)
        code = chr(10).join(code_lines(ld))
        check("insmod" in ld, "loader: uses insmod (the proven parameter path)")
        check("modprobe hi5622_wifi" not in code,
              "loader: never relies on modprobe passing parameters")
        check("rmmod" not in command_names(ld), "loader: never unloads the driver")
        check("h2d_ctx_create=3" in code or "h2d_ctx_create" in code,
              "loader: C2 ladder uses h2d_ctx_create + h2d_txq_sync72")
        check("h2d_ctx_create_t1" not in code,
              "loader: does not use the non-existent h2d_ctx_create_t1 attr")
        for key in DEPLOYED_DEFAULTS:
            if key == "wifi_block_path":
                continue
            check(("uci_get main." + key) in ld,
                  "loader: " + key + " is configurable (uci main." + key + ")")
        check("verify_param fw_path" in ld,
              "loader: verifies the applied parameters after insmod")

    # 7. init script policy
    init_path = os.path.join(ROOTFS, "etc/init.d/hi5622-wifi")
    if os.path.isfile(init_path):
        ini = read(init_path)
        check("START=96" in ini, "init: START=96 (late, after the base system)")
        check("STOP=20" in ini, "init: STOP=20")
        check("hi5622.main.autoload" in ini,
              "init: boot load gated by hi5622.main.autoload")
        check("rmmod" not in command_names(ini),
              "init: stop never rmmods (no rmmod after CPU release)")
        check("hi5622-load" in ini, "init: delegates to /usr/sbin/hi5622-load")

    # 8. hostapd fixtures: capability-driven hardening
    hdir = os.path.join(ROOTFS, "etc/hi5622")
    common = {
        "driver": "nl80211",
        "interface": "wlan0",
        "hw_mode": "g",
        "beacon_int": "100",
        "dtim_period": "2",
        "auth_algs": "1",
        "wmm_enabled": "1",
        "ignore_broadcast_ssid": "0",
    }
    for name in ("hostapd-open.proven.conf", "hostapd-open.conf",
                 "hostapd-wpa2.conf", "hostapd-wpa2-wpa3.conf"):
        p = os.path.join(hdir, name)
        if not os.path.isfile(p):
            continue
        c = read_conf(p)
        for k, v in common.items():
            check(c.get(k) == v, "hostapd " + name + ": " + k + "=" + v)
        ch = c.get("channel", "")
        check(ch.isdigit() and 1 <= int(ch) <= 13,
              "hostapd " + name + ": channel in the advertised 2.4 GHz set")
        check(c.get("ssid", "") != "", "hostapd " + name + ": ssid set")
        check("passthrough" not in c, "hostapd " + name + ": no unknown-write passthrough")
        if name == "hostapd-open.proven.conf":
            check("wpa" not in c, "hostapd proven: open (no wpa=)")
            check("ieee80211n" not in c, "hostapd proven: no HT block (fired baseline)")
        if name == "hostapd-open.conf":
            check("wpa" not in c, "hostapd open: open (no wpa=)")
            check(c.get("ieee80211n") == "1", "hostapd open: HT enabled")
            cap = c.get("ht_capab", "")
            check("HT20" in cap, "hostapd open: HT20 advertised")
            check("HT40" not in cap, "hostapd open: no HT40 (wiphy advertises HT20-only)")
        if name == "hostapd-wpa2.conf":
            check(c.get("wpa") == "2", "hostapd wpa2: wpa=2")
            check(c.get("wpa_key_mgmt") == "WPA-PSK", "hostapd wpa2: WPA-PSK")
            check(c.get("rsn_pairwise") == "CCMP", "hostapd wpa2: CCMP")
            check(c.get("wpa_passphrase", "") == "CHANGEME-lab-only",
                  "hostapd wpa2: placeholder passphrase only")
            check("HT40" not in c.get("ht_capab", ""), "hostapd wpa2: no HT40")
        if name == "hostapd-wpa2-wpa3.conf":
            check("SAE" in c.get("wpa_key_mgmt", ""), "hostapd wpa3: SAE in key mgmt")
            check(c.get("ieee80211w") == "1", "hostapd wpa3: PMF required (ieee80211w=1)")
            check(c.get("sae_require_mfp") == "1", "hostapd wpa3: sae_require_mfp=1")
            check(c.get("wpa_passphrase", "") == "CHANGEME-lab-only",
                  "hostapd wpa3: placeholder passphrase only")

    # 9. lane does not carry vendor bytes / real credentials
    forbidden_ext = (".ko", ".o", ".bin", ".fw", ".ubi", ".squashfs", ".apk", ".mod")
    offenders = []
    for dirpath, dirnames, filenames in os.walk(LANE):
        dirnames[:] = [d for d in dirnames if d not in ("__pycache__", "build")]
        for fn in filenames:
            if fn.endswith(forbidden_ext):
                offenders.append(os.path.join(dirpath, fn))
    check(not offenders, "lane carries no binary/vendor artifacts"
          + ("" if not offenders else " -> " + ", ".join(offenders)))
    scan_targets = []
    for dirpath, dirnames, filenames in os.walk(ROOTFS):
        for fn in filenames:
            scan_targets.append(os.path.join(dirpath, fn))
    for fn in os.listdir(LANE):
        if fn.endswith(".md"):
            scan_targets.append(os.path.join(LANE, fn))
    for path in scan_targets:
        text = read(path)
        rel = os.path.relpath(path, LANE)
        for line in text.splitlines():
            if "wpa_passphrase" in line and "CHANGEME" not in line:
                bad(rel + ": non-placeholder wpa_passphrase line")
            if ("BEGIN" + " PRIVATE KEY") in line or ("BEGIN RSA" + " PRIVATE KEY") in line:
                bad(rel + ": private key material")

    # 10. report/refs consistency (numbers must match the built artifacts)
    report_path = os.path.join(LANE, "REPORT.md")
    if os.path.isfile(report_path):
        rep = read(report_path)
        check(IMAGE_MODULE_SHA in rep, "REPORT: image module sha 51608fd5... recorded")
        check(BASE_MODULE_SHA in rep, "REPORT: proven module sha 44c6cc12... recorded")
        check(POLISHED_MODULE_SHA in rep, "REPORT: polished module sha d145a205... recorded")
        check("242,920" in rep, "REPORT: polished module size 242,920 B recorded")
        check("mgmt_diag" in rep, "REPORT: mgmt_diag gate documented")
        check("cfe24bfb" in rep, "REPORT: PD4 firmware hash / manual-push policy documented")
    refs_path = os.path.join(LANE, "refs/artifact-identity.txt")
    if os.path.isfile(refs_path):
        refs = read(refs_path)
        for needle in (IMAGE_MODULE_SHA, BASE_MODULE_SHA, POLISHED_MODULE_SHA):
            check(needle in refs, "refs: artifact identity " + needle[:12] + "... present")
        params_ref = os.path.join(LANE, "refs/module-params.txt")
        check(os.path.isfile(params_ref), "refs: module-params.txt present")
        if os.path.isfile(params_ref):
            pref = read(params_ref)
            for p in IMAGE_PARAMS:
                check(p in pref, "refs: image module parameter " + p + " listed")
            check("mgmt_diag" in pref, "refs: polished-only parameter mgmt_diag noted")

    # 11. optional hostapd parse gate
    if args.hostapd:
        binary = args.hostapd
        if not (os.path.isfile(binary) and os.access(binary, os.X_OK)):
            bad("hostapd parse gate: binary not executable: " + binary)
        else:
            print("-- hostapd config-parse gate: " + binary)
            for name in ("hostapd-open.proven.conf", "hostapd-open.conf",
                         "hostapd-wpa2.conf", "hostapd-wpa2-wpa3.conf"):
                conf = os.path.join(hdir, name)
                r = subprocess.run([binary, "-dd", "-i", "wlan0", conf],
                                   capture_output=True, text=True, timeout=20)
                blob = r.stdout + r.stderr
                parse_err = ("unknown configuration item" in blob
                             or "errors found in configuration file" in blob
                             or "invalid line" in blob)
                reached = ("Configuration file: " + conf) in blob
                check(not parse_err and reached,
                      "hostapd parse: " + name + " parsed cleanly before driver init")
            with tempfile.NamedTemporaryFile("w", suffix=".conf", delete=False) as f:
                f.write("bogus_option_xyz=1" + chr(10))
                badconf = f.name
            r = subprocess.run([binary, "-dd", "-i", "wlan0", badconf],
                               capture_output=True, text=True, timeout=20)
            blob = r.stdout + r.stderr
            check("unknown configuration item" in blob or
                  "errors found in configuration file" in blob,
                  "hostapd parse: negative control (bogus option) is refused")
            os.unlink(badconf)

    print()
    if FAILS:
        print(f"RESULT: FAIL ({len(FAILS)} failed / {CHECKS} checks)")
        for f in FAILS:
            print("  - " + f)
        return 1
    print(f"RESULT: PASS ({CHECKS} checks)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
