#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
selftest.py - device-free driver/source selftest for the wifi-scan-ap lane
(shared task task-113): lane layout, cfg80211 scan/AP wiring, provider-contract
discipline, inherited-tree baseline, configs, docs and the fail-closed default.

No device, no kernel build, no vendor binary/firmware/calibration material.
Exit 0 = all checks passed.
"""
import hashlib
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LANE = os.path.dirname(HERE)
SRC = os.path.join(LANE, "src")
CFG = os.path.join(LANE, "configs")
KDIR = ("/home/shibbityshwab/openwrt-lab-upstream-target/build_dir/"
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
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            return fh.read()
    except OSError:
        return ""


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
        "src/hi5622_scan.h": os.path.join(SRC, "hi5622_scan.h"),
        "src/hi5622_scan.c": os.path.join(SRC, "hi5622_scan.c"),
        "src/hi5622_scan_logic.h": os.path.join(SRC, "hi5622_scan_logic.h"),
        "src/hi5622_scan_logic.c": os.path.join(SRC, "hi5622_scan_logic.c"),
        "src/hi5622_frames_api.h": os.path.join(SRC, "hi5622_frames_api.h"),
        "src/hi5622_frames_provider.c": os.path.join(SRC, "hi5622_frames_provider.c"),
        "src/hi5622_frame_post.h": os.path.join(SRC, "hi5622_frame_post.h"),
        "src/hi5622_frame_post.c": os.path.join(SRC, "hi5622_frame_post.c"),
        "src/hi5622_vapscan_map.h": os.path.join(SRC, "hi5622_vapscan_map.h"),
        "src/hi5622_vapscan_map.c": os.path.join(SRC, "hi5622_vapscan_map.c"),
        "src/hi5622_vapscan_provider.c": os.path.join(SRC, "hi5622_vapscan_provider.c"),
        "src/vapscan_frames.h": os.path.join(SRC, "vapscan_frames.h"),
        "src/vapscan_frames.c": os.path.join(SRC, "vapscan_frames.c"),
        "src/hi5622_scan_d2h.h": os.path.join(SRC, "hi5622_scan_d2h.h"),
        "src/hi5622_scan_d2h.c": os.path.join(SRC, "hi5622_scan_d2h.c"),
        "src/cfg80211-provider.symvers": os.path.join(SRC, "cfg80211-provider.symvers"),
        "tools/build.sh": os.path.join(HERE, "build.sh"),
        "tools/selftest.py": os.path.join(HERE, "selftest.py"),
        "tools/gen_cfg80211_symvers.py": os.path.join(HERE, "gen_cfg80211_symvers.py"),
        "tools/scan_logic_selftest.c": os.path.join(HERE, "scan_logic_selftest.c"),
        "tools/src-baseline.sha256": os.path.join(HERE, "src-baseline.sha256"),
        "configs/hostapd.conf": os.path.join(CFG, "hostapd.conf"),
        "configs/hostapd-open.conf": os.path.join(CFG, "hostapd-open.conf"),
        "configs/wpa_supplicant.conf": os.path.join(CFG, "wpa_supplicant.conf"),
        "README.md": os.path.join(LANE, "README.md"),
        "REPORT.md": os.path.join(LANE, "REPORT.md"),
        "BUILD-NOTES.md": os.path.join(LANE, "BUILD-NOTES.md"),
        "TEST-LADDER.md": os.path.join(LANE, "TEST-LADDER.md"),
    }
    for name, path in required.items():
        check(exists(path), "required file missing/empty: %s" % name)

    kbuild = read(os.path.join(SRC, "Kbuild"))
    for obj in ("hi5622_scan_logic.o", "hi5622_scan.o",
                "hi5622_frame_post.o", "hi5622_frames_provider.o",
                "hi5622_vapscan_map.o", "hi5622_vapscan_provider.o",
                "vapscan_frames.o", "hi5622_scan_d2h.o"):
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

    # ---- 2. inherited files really inherited / expected integrations ----
    h = read(os.path.join(SRC, "hi5622_cfg80211.h"))
    check('#include "hi5622_scan.h"' in h, "cfg80211.h missing scan.h include")
    check("struct hi5622_scan\t\tscan;" in h or
          re.search(r"struct hi5622_scan\s+scan;", h) is not None,
          "cfg80211.h missing the embedded scan state")
    w = read(os.path.join(SRC, "hi5622_cfg80211.c"))
    for needle in ("hi5622_scan_init(c)", "hi5622_scan_remove(c)",
                   "hi5622_scan_status(c,"):
        check(needle in w, "cfg80211.c missing %s" % needle)
    check(w.count("wiphy_register(c->wiphy)") == 1 and
          w.count("= wiphy_register(") == 1,
          "expected exactly one wiphy_register call site")
    check("hi5622_cfg_reg_gate" in w, "registration gate removed")
    for bad in ("writel(", "readl(", "hi5622_io_bar0", "0x400392f0"):
        check(bad not in w, "device access forbidden in cfg80211 lifecycle: %s" % bad)

    o = read(os.path.join(SRC, "hi5622_cfg80211_ops.c"))
    table = o[o.find("const struct cfg80211_ops hi5622_cfg11_ops"):]
    ops = re.findall(r"\.([a-z_]+)\s*=\s*hi5622_", table)
    expected_ops = {"add_virtual_intf", "del_virtual_intf",
                    "change_virtual_intf", "get_station", "dump_station",
                    "scan", "abort_scan", "start_ap", "stop_ap"}
    check(set(ops) == expected_ops, "ops table mismatch: got %s" % sorted(ops))
    check(len(ops) == 9, "expected exactly 9 populated cfg80211 ops")
    for need in (".scan\t\t\t= hi5622_scan_op_scan",
                 ".abort_scan\t\t= hi5622_scan_op_abort",
                 ".start_ap\t\t= hi5622_scan_op_start_ap",
                 ".stop_ap\t\t= hi5622_scan_op_stop_ap"):
        check(need in table, "ops table missing %r" % need)
    for absent in (".add_key", ".connect", ".mgmt_tx", ".set_wiphy_params"):
        check(".%s\t" % absent not in table and ".%s " % absent not in table,
              "deferred M3+ op must not be registered: %s" % absent)
    check("hi5622_vap_hcc_add(c, slot)" in o,
          "add_virtual_intf does not attempt the firmware user_add")
    check("hi5622_vap_hcc_del(c, user_id, addr, c->band, (u8)slot)" in o,
          "del_virtual_intf does not attempt the firmware user_del")
    check(o.count("cfg80211_register_netdevice(dev)") == 1,
          "cfg80211_register_netdevice call missing")
    check("cfg80211_unregister_wdev(wdev)" in o,
          "del_virtual_intf must call cfg80211_unregister_wdev")
    check("kfree(wdev);" not in o, "del path frees the wdev (use-after-free)")
    check("return -EOPNOTSUPP" in o,
          "get_station/dump_station stubs must return -EOPNOTSUPP")

    # ---- 3. scan kernel layer: one scan_done, provider-gated, no IO -----
    s = read(os.path.join(SRC, "hi5622_scan.c"))
    check(s.count("cfg80211_scan_done(req, &info)") == 2,
          "cfg80211_scan_done(req, &info) must appear exactly twice (work + remove), got %d"
          % s.count("cfg80211_scan_done(req, &info)"))
    check("cfg80211_inform_bss(" in s and "cfg80211_put_bss(" in s,
          "BSS reporting pair missing")
    check("hi5622_scan_need(" in s and "HI5622_FRAME_CAP_SCAN_REQ" in s and
          "HI5622_FRAME_CAP_SCAN_PARSE" in s,
          "scan op is not provider-gated")
    check("hi5622_scan_result_validate(r)" in s,
          "scan results are not validated before cfg80211")
    check("mutex_trylock(&s->lock)" in s,
          "delivery hook must not block the HCC service thread")
    for bad in ("writel(", "readl(", "hi5622_io_bar0", "request_firmware",
                "0x400392f0", "0x400392F0"):
        check(bad not in s, "device access/forbidden constant in scan layer: %s" % bad)
    for what in ("\"scan_req\"", "\"ap_start\"", "\"ap_stop\"",
                 "\"user_add\"", "\"user_del\""):
        check("hi5622_frame_post(c->ctx, wire, len, %s" % what in s,
              "scan layer never posts %s through the gate" % what)
    check("hi5622_scan_need(wiphy," in s and "start_ap" in s,
          "start_ap provider gating missing")
    check("hi5622_vapscan_provider_register()" in s,
          "scan init does not register the task-110 provider")
    check("p.band = c->band;" in s,
          "scan/VAP neutral params do not carry the endpoint band")
    check("EOPNOTSUPP" in s, "scan/AP layer must fail closed with -EOPNOTSUPP")
    check("hi5622_scan_model_complete(" in s,
          "g1 id16 completion status is not routed to the scan model")
    check("ops->parse_scan_channel" in s and "ops->parse_scan_sta_complete" in s,
          "g1 id17 / g2 id2 decoders are not wired into the delivery hook")
    check("hi5622_scan_model_event(&s->model, cls" in s,
          "event counter path changed unexpectedly")

    # ---- 4. fail-closed post gate --------------------------------------
    fp = read(os.path.join(SRC, "hi5622_frame_post.c"))
    check(re.search(r"unsigned int hi5622_allow_frames;", fp) is not None,
          "allow_frames must default to 0 (no initializer)")
    check("module_param_named(allow_frames" in fp, "allow_frames param missing")
    check("H2D_MBOX_ID_BUGON" in fp, "id-5 firmware BUG trap not refused")
    check("h2d_frame_decode(" in fp and "H2D_VF_HANDLER" in fp,
          "provider frames are not structurally/firmware validated")
    check("hcc_core_sr_commit(" in fp, "SR commit primitive missing")
    check("hcc_idx_plus(producer, r->depth)" in fp,
          "packed producer advance missing")
    check("HCC_PHASE_ACTIVE" in fp, "post gate does not require ACTIVE phase")
    check("mbox_inflight" in fp and "h2d_lane_closed" in fp,
          "post gate misses coalescing/lane state")
    check("hi5622_frame_wait_ready" in fp and "HI5622_POST_WAIT_MS" in fp,
          "post gate must bounded-wait for the previous announce (user_add -> "
          "user_add_notify is two back-to-back frames)")
    check("previous mailbox announce still inflight" not in fp,
          "back-to-back frames must wait, not be refused as inflight")
    check(re.search(r"unsigned int hi5622_frame_gap_ms = 20u;", fp)
          is not None, "frame_gap_ms default must be 20 ms")
    for bad in ("0x400392f0", "0x400392F0", "HI5622_CA_H2D_DOORBELL",
                "hcc_wr"):
        code_lines = [ln for ln in fp.splitlines()
                      if bad in ln and not ln.lstrip().startswith(
                          ("*", "/*", "//", "#"))]
        check(not code_lines,
              "forbidden mailbox/constant in post gate code: %s" % bad)

    pv = read(os.path.join(SRC, "hi5622_frames_provider.c"))
    check("hi5622_frame_ops_validate(ops)" in pv,
          "provider registration does not validate the ops table")
    check("EXPORT_SYMBOL_GPL(hi5622_frame_ops_register)" in pv,
          "provider registration export missing")
    check(re.search(r"static const struct hi5622_frame_ops \*hi5622_provider;",
                    pv) is not None,
          "provider registry must default to EMPTY (NULL)")

    # ---- 4b. integrated task-110 provider (vapscan builders) ------------
    vp = read(os.path.join(SRC, "hi5622_vapscan_provider.c"))
    check("hi5622_frame_ops_register(&hi5622_vapscan_ops)" in vp,
          "vapscan adapter does not register the provider")
    for cap in ("HI5622_FRAME_CAP_SCAN_REQ", "HI5622_FRAME_CAP_USER_ADD",
                "HI5622_FRAME_CAP_USER_ADD_NOTIFY",
                "HI5622_FRAME_CAP_USER_DEL"):
        check(cap in vp, "vapscan provider missing capability %s" % cap)
    check("d2h_scan_decode" not in vp,
          "no opt-in param: SCAN_PARSE must always be advertised (the cfg80211 "
          ".scan op requires SCAN_REQ|SCAN_PARSE = 0x3)")
    check("HI5622_FRAME_CAP_SCAN_PARSE" in vp and
          "HI5622_FRAME_CAP_SCAN_REQ" in vp,
          "scan caps must include SCAN_REQ|SCAN_PARSE (0x3)")
    caps_block = vp[vp.find("hi5622_vapscan_ops.caps ="):]
    caps_block = caps_block[:caps_block.find(";")]
    check(caps_block and "if (" not in caps_block,
          "scan caps must not be conditional (bench D2 needs caps 0x1f)")
    check("HI5622_FRAME_CAP_AP_START" not in vp and
          "HI5622_FRAME_CAP_AP_STOP" not in vp,
          "no AP builders exist -> AP caps must stay absent")
    check("kmalloc(sizeof(*f)" in vp,
          "vs_frame (1.7 KB) must be heap allocated in the adapter")
    check("hi5622_vapscan_scan_req" in vp and "hi5622_vapscan_user_add" in vp
          and "hi5622_vapscan_user_alg" in vp and
          "hi5622_vapscan_user_del" in vp,
          "adapter does not use the task-110 mapping functions")
    vm = read(os.path.join(SRC, "hi5622_vapscan_map.c"))
    check("vs_build_scan_req" in vm and "vs_build_user_add" in vm and
          "vs_build_user_alg" in vm and "vs_build_user_del" in vm,
          "mapping layer does not call the task-110 builders")
    check("HI5622_VAPSCAN_SCAN_FOOTER" in vm and "20u" in vm,
          "g1 id15 trailer strip (20 B [U]) missing")
    check(("signal stays 0" in vm.lower()) and "signal_mbm" in vm,
          "unknown signal fallback must be explicit (0 mBm [U])")
    check("hardcodes VAP 0" in vm,
          "scan VAP-0 bench fact must be documented in the mapping")
    check("HI5622_HOST_TYPES_DEFINED" in read(os.path.join(SRC, "vapscan_frames.h")),
          "vapscan_frames.h portability guard missing (our copy is edited)")
    check("Portability edit" in read(os.path.join(SRC, "vapscan_frames.h")),
          "integrated vapscan_frames.h must document the portability edit")

    # task-118 decoder integration
    d2h = read(os.path.join(SRC, "hi5622_scan_d2h.c"))
    d2hh = read(os.path.join(SRC, "hi5622_scan_d2h.h"))
    check("hi5622_scan_d2h_parse_bss" in d2h and
          "hi5622_scan_d2h_parse_complete" in d2h and
          "hi5622_scan_d2h_parse_chan" in d2h and
          "hi5622_scan_d2h_parse_sta_comp" in d2h,
          "integrated task-118 decoder API incomplete")
    check("HI5622_SCAN_D2H_TRAILER_LEN\t20u" in d2hh,
          "task-118 20-byte trailer constant missing")
    check("signal_mbm = 0" in d2h or "signal_from_trailer" in d2h,
          "decoder signal must stay fail-closed/opt-in")
    check("HI5622_HOST_TYPES_DEFINED" in d2hh and
          "Portability edit" in d2hh,
          "integrated hi5622_scan_d2h.h must carry the portability guard")
    check("hi5622_vapscan_parse_scan_result" not in vm,
          "old heuristic scan decoder must be removed from the mapping layer")
    check("hi5622_scan_d2h_parse_bss" in vm and
          "hi5622_scan_d2h_parse_complete" in vm and
          "hi5622_scan_d2h_parse_chan" in vm and
          "hi5622_scan_d2h_parse_sta_comp" in vm,
          "mapping layer does not call the task-118 decoders")
    check("hi5622_vapscan_decode_scan_result" in vp and
          "hi5622_vapscan_decode_scan_complete" in vp,
          "provider does not use the task-118 BSS/completion decoders")
    check("vapscan_parse_scan_complete" in vp and
          "hi5622_scan_model_complete" in s,
          "provider/model completion-status wiring missing")
    check("hi5622_vapscan_decode_scan_channel" in vp and
          "hi5622_vapscan_decode_scan_sta_complete" in vp,
          "provider channel/STA-completion decoders missing")
    lh = read(os.path.join(SRC, "hi5622_scan_logic.h"))
    for needle in ("HI5622_SCAN_D2H_ID_BSS\t\t0x0fu",
                   "HI5622_SCAN_D2H_ID_COMPLETE\t0x10u",
                   "HI5622_SCAN_D2H_ID_CHANNEL\t0x11u",
                   "HI5622_SCAN_D2H_ID_STA_RSP\t0x02u",
                   "HI5622_SCAN_TIMEOUT_MS\t\t5000u",
                   "HI5622_SCAN_MAX_RESULTS\t\t256u",
                   "hi5622_scan_model_complete",
                   "complete_failed",
                   "complete_failures"):
        check(needle in lh, "scan logic header missing %r" % needle)

    # ---- 5. D2H integration: full payload to the scan decoder -----------
    hcc = read(os.path.join(SRC, "hcc.c"))
    check("hi5622_scan_deliver(ctx, hdr, payload, payload_avail)" in hcc,
          "hcc.c delivery hook missing")
    d2h = read(os.path.join(SRC, "d2h.c"))
    check("p + HI5622_HCC_HDR_LEN, plen" in d2h,
          "d2h.c must deliver the full staged payload, not the 48-byte copy")
    check("u8 *snap = rd->snap;" in d2h,
          "d2h staging buffer must live in the reader state (stack bound)")
    contract = read(os.path.join(SRC, "hi5622_contract.h"))
    check("#define HI5622_D2H_SNAP_MAX\t\tHI5622_HCC_FRAME_MAX_TOTAL" in contract,
          "D2H snapshot capacity must hold a full HCC frame")
    dh = read(os.path.join(SRC, "hi5622_d2h.h"))
    check("u8\t\t\tsnap[HI5622_D2H_SNAP_MAX];" in dh,
          "hi5622_d2h.h missing the full-frame staging buffer")
    frames_h = read(os.path.join(SRC, "h2d_frames.h"))
    check("HI5622_HOST_TYPES_DEFINED" in frames_h,
          "h2d_frames.h missing the shared portable-type guard")
    fa = read(os.path.join(SRC, "hi5622_frames_api.h"))
    check("HI5622_HOST_TYPES_DEFINED" in fa,
          "frames_api.h missing the shared portable-type guard")

    # ---- 6. cfg80211 provider symvers ----------------------------------
    prov = read(os.path.join(SRC, "cfg80211-provider.symvers"))
    for sym in ("cfg80211_scan_done", "cfg80211_inform_bss_data",
                "cfg80211_put_bss", "wiphy_new_nm", "wiphy_register"):
        check(sym in prov, "provider symvers missing %s" % sym)
    raw_lines = [ln for ln in prov.splitlines() if ln.strip()]
    check(len(raw_lines) == 9, "expected 9 provider symbols, got %d" % len(raw_lines))
    check(all("#" not in ln for ln in raw_lines),
          "Module.symvers grammar forbids comment lines")
    check(all(ln.count("\t") == 4 for ln in raw_lines),
          "every provider line must be crc<TAB>sym<TAB>mod<TAB>export<TAB>ns")
    check(all("\tEXPORT_SYMBOL\t" in ln for ln in raw_lines),
          "provider export type must be EXPORT_SYMBOL")
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

    # ---- 7. module artifact evidence (when built) -----------------------
    ko = os.path.join(SRC, "hi5622_wifi.ko")
    if os.path.isfile(ko):
        size = os.path.getsize(ko)
        check(size > 550000, "module unexpectedly small (%d B)" % size)
        parms = subprocess.run(["modinfo", "-F", "parm", ko],
                               capture_output=True, text=True, check=False).stdout
        for p in ("allow_frames:", "frame_gap_ms:", "cfg80211_auto:",
                  "band_force:", "h2d_send_budget:"):
            check(p in parms, "module parameter missing: %s" % p)
        deps = subprocess.run(["modinfo", "-F", "depends", ko],
                              capture_output=True, text=True, check=False).stdout
        check("cfg80211" in deps, "module does not depend on cfg80211")
    else:
        print("NOTE: src/hi5622_wifi.ko not present - artifact checks skipped")

    # ---- 8. configs ----------------------------------------------------
    def conf_has(name, needles):
        text = read(os.path.join(CFG, name))
        for needle in needles:
            check(needle in text, "%s missing %r" % (name, needle))

    conf_has("hostapd.conf",
             ["interface=wlan0", "driver=nl80211", "hw_mode=g",
              "channel=6", "wpa=2", "wpa_key_mgmt=WPA-PSK",
              "rsn_pairwise=CCMP", "wpa_passphrase=", "ssid="])
    conf_has("hostapd-open.conf",
             ["interface=wlan0", "driver=nl80211", "hw_mode=g",
              "channel=6", "ssid="])
    conf_has("wpa_supplicant.conf",
             ["ctrl_interface=", "network={", "ssid=", "psk=",
              "key_mgmt=WPA-PSK", "pairwise=CCMP", "proto=RSN"])
    for c in ("hostapd.conf", "wpa_supplicant.conf"):
        text = read(os.path.join(CFG, c))
        check("00:11:22:33:44:55" not in text,
              "%s must use a placeholder PSK, not a real credential" % c)

    # ---- 9. docs / evidence discipline --------------------------------
    rep = read(os.path.join(LANE, "REPORT.md"))
    for needle in ("task-113", "task-110", "M2", "M3", "scan", "g0 id0xC",
                   "g0 id7", "g1 id15", "g1 id16", "g1 id17", "g2 id2",
                   "start_ap", "hostapd", "hi5622_frame_post",
                   "allow_frames=0", "SCAN_PARSE", "caps 0x1f", "vapscan",
                   "task-118", "scan_d2h",
                   "hi5622_vapscan_provider", "PD4", "[U]", "[I]", "[V]",
                   "no vendor"):
        check(needle.lower() in rep.lower(),
              "REPORT.md missing %r" % needle)
    ladder = read(os.path.join(LANE, "TEST-LADDER.md"))
    for needle in ("iw dev wlan0 scan", "hostapd", "wpa_supplicant",
                   "expected", "STOP", "abort", "returncode", "dmesg",
                   "allow_frames", "g0 id0xC", "VAP 0"):
        check(needle in ladder, "TEST-LADDER.md missing %r" % needle)
    readme = read(os.path.join(LANE, "README.md"))
    check("wifi-scan-ap" in readme and "task-113" in readme,
          "README.md missing lane/task identification")
    bn = read(os.path.join(LANE, "BUILD-NOTES.md"))
    check("sha256" in bn and "warnings" in bn,
          "BUILD-NOTES.md missing the artifact evidence fields")

    # ---- 10. inherited-tree baseline -----------------------------------
    baseline = os.path.join(HERE, "src-baseline.sha256")
    if exists(baseline):
        n_base = 0
        for line in read(baseline).splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) != 2:
                continue
            digest, rel = parts
            p = os.path.join(LANE, rel)
            if not os.path.isfile(p):
                check(False, "baseline file missing: %s" % rel)
                continue
            actual = hashlib.sha256(open(p, "rb").read()).hexdigest()
            check(actual == digest,
                  "inherited file changed without an integration note: %s" % rel)
            n_base += 1
        check(n_base >= 16, "baseline manifest too small (%d files)" % n_base)
    else:
        print("NOTE: baseline manifest absent - parity checks skipped")

    # ---- 11. no vendor material in the lane ---------------------------
    # Build products are expected in the working tree after tools/build.sh and
    # are covered by src/.gitignore; they are a NOTE here, not a failure.  Only
    # vendor-material formats are forbidden outright.
    vendor_ext = (".bin", ".ini", ".cal", ".dat", ".fw", ".hex", ".img")
    build_ext = (".ko", ".o", ".mod", ".cmd", ".order")
    for root, dirs, files in os.walk(LANE):
        dirs[:] = [d for d in dirs if d != "__pycache__"]
        for f in files:
            rel = os.path.relpath(os.path.join(root, f), LANE)
            if f.endswith(vendor_ext):
                check(False, "vendor-material file format in the lane: %s" % rel)
            elif f.endswith(build_ext):
                print("NOTE: gitignored build product present: %s" % rel)
    for root, dirs, files in os.walk(LANE):
        dirs[:] = [d for d in dirs if d != "__pycache__"]
        for f in files:
            if f == "selftest.py":
                continue
            if not f.endswith((".c", ".h", ".md", ".py", ".sh", ".conf",
                               ".symvers", ".sha256", ".inc", ".gitignore")):
                continue
            text = read(os.path.join(root, f))
            check("-----BEGIN" not in text,
                  "PEM block in lane file: %s" % f)

    print("\n%d/%d checks passed" % (checks - failed, checks))
    if failed:
        print("FAILED: %d check(s)" % failed)
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
