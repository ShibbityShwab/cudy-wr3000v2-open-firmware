#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""verify_report.py - device-free verifier for the wifi-re-report umbrella report.

Checks the report's internal consistency and its cross-references against the committed
lane sources and the append-only ledger. No device, no vendor artifact needed: the checks
only read committed text files (plus the superproject ledger/STATUS, which are local text).

Exit code 0 iff every check group passes.
"""
import os
import re
import sys

BT = chr(96)  # backtick, avoided literally in this source for tooling reasons

HERE = os.path.dirname(os.path.abspath(__file__))
LANE = os.path.dirname(HERE)                      # opensource/docs/incoming/wifi-re-report
INCOMING = os.path.dirname(LANE)                  # opensource/docs/incoming
OPENSOURCE = os.path.dirname(os.path.dirname(INCOMING))  # opensource
REPO = os.path.dirname(OPENSOURCE)                # superproject root

FILES = ["README.md", "REPORT.md", "TABLES.md", "EVIDENCE-INDEX.md", "REPRODUCE.md"]
MIN_BYTES = {"REPORT.md": 240000, "TABLES.md": 85000, "EVIDENCE-INDEX.md": 65000,
             "REPRODUCE.md": 14000, "README.md": 4000}

TEXT = {}
checks = []
failures = []


def check(name, ok, detail=""):
    checks.append(name)
    if not ok:
        failures.append((name, detail))
    print(("PASS  " if ok else "FAIL  ") + name + (("  [" + detail + "]") if (detail and not ok) else ""))


def read(path):
    with open(path, "r", encoding="utf-8", errors="strict") as f:
        return f.read()


def load_files():
    for f in FILES:
        p = os.path.join(LANE, f)
        if not os.path.isfile(p):
            check("file exists: " + f, False, p)
            continue
        t = read(p)
        TEXT[f] = t
        check("file exists and is non-trivial: " + f, len(t.encode()) >= MIN_BYTES[f],
              "%d < %d bytes" % (len(t.encode()), MIN_BYTES[f]))


def check_report_structure():
    t = TEXT.get("REPORT.md", "")
    heads = re.findall(r"^## (\d+)\. (.+)$", t, re.M)
    nums = {int(n) for n, _ in heads}
    check("REPORT has the 14 top-level sections 0..13", nums == set(range(14)),
          "found " + ",".join(str(n) for n in sorted(nums)))
    titles = {int(n): s for n, s in heads}
    for n, frag in [(0, "Document control"), (1, "Executive summary"), (2, "System map"),
                    (3, "boot chain"), (4, "Critical findings"), (5, "Protocol reference"),
                    (6, "duplex loop"), (7, "Bench evidence chain"), (8, "Corrections register"),
                    (9, "Verification and reproduction"), (10, "Limitations"),
                    (11, "Publication"), (12, "Lane documentation map"),
                    (13, "Conventions")]:
        check("REPORT section %d present (%s)" % (n, frag), n in titles and frag.lower() in titles[n].lower(),
              titles.get(n, "missing"))
    for must in ["release-only", "0xCEED", "0xCECE", "PD3", "PD4", "0x108240", "0x86807FEF"]:
        check("REPORT covers %s" % must, must in t)
    for must in ["cfg80211", "wiphy", "calibration", "1794a531", "E1a", "data-plane"]:
        check("REPORT covers the v2 milestone vocabulary: %s" % must, must in t)
    for must in ["user_add", "user_add_notify", "scan_req", "BLOCKED-DRIVER", "consume-only",
                 "4a1c3b9a", "8fc693ad", "0x0320", "one-command", "VAP start"]:
        check("REPORT covers the v3 milestone vocabulary: %s" % must, must in t)
    for must in ["AP-ENABLED", "cudy-ap-test", "fd8e1439", "ca32aec4", "REPORTS_OBSS",
                 "mgmt_stypes", "POST-ONLY", "mode-1 VAP", "0x01010F18", "flashed image",
                 "wiphy_new_nm", "g1 id0x23"]:
        check("REPORT covers the v4 milestone vocabulary: %s" % must, must in t)
    for must in ["0x40050000", "0xfffffcc1", "0x0078", "0x0a5825", "0x0124", "0x10cd5c",
                 "0x0123", "0x00d0", "0x0062", "0x00400064", "five-message", "txq-table",
                 "rx_arm", "gtk", "slot 0 only", "wpa2", "5b587949", "all-patch", "308,080"]:
        check("REPORT covers the v5 static-wave vocabulary: %s" % must, must in t.lower())
    for must in ["ab9e18a4", "251d08f0", "0x01f3", "0x01f2", "g0 id6", "g1 id13",
                 "set_rx_filter", "field10", "44 db", "0x182", "association module",
                 "ap-first", "60-byte prefix", "r11", "rf"]:
        check("REPORT covers the v6 association/management vocabulary: %s" % must, must in t.lower())
    subs = re.findall(r"^### (6\.\d+) ", t, re.M)
    check("REPORT section 6 has the v2-v6 subsections 6.1-6.25",
          subs == ["6." + str(i) for i in range(1, 26)], ",".join(subs))
    check("REPORT carries findings F1-F27", all(("### F%d." % i) in t for i in range(1, 28)))
    check("REPORT is the v6 revision", "**v6**" in t)


def check_markers():
    allowed = {"[V]", "[V*]", "[I]", "[U]"}
    bad = []
    for f, t in TEXT.items():
        for m in re.finditer(r"\[([A-Z][A-Z*?]{0,2})\]", t):
            tok = "[" + m.group(1) + "]"
            if tok not in allowed:
                bad.append(f + ": " + tok)
    check("only the four evidence markers are used", not bad, "; ".join(bad[:5]))
    counts = {k: 0 for k in allowed}
    for t in TEXT.values():
        for k in allowed:
            counts[k] += t.count(k)
    check("all four markers are actually used", all(counts[k] > 0 for k in allowed), str(counts))
    rep = TEXT.get("REPORT.md", "")
    i = rep.find("## 4. Critical findings")
    j = rep.find("## 5. Protocol reference")
    seg = rep[i:j]
    marked = len(re.findall(r"\[(?:V\*?|I|U)\]", seg))
    check("findings section carries evidence markers", marked >= 20, "only %d" % marked)


def resolve_path(tok):
    tok = tok.strip()
    if not tok or " " in tok or "*" in tok or "<" in tok or ">" in tok:
        return True
    if tok.startswith("opensource/"):
        p = os.path.join(REPO, tok)
    elif tok.startswith("wifi-"):
        p = os.path.join(INCOMING, tok)
    else:
        return True
    if any(f in tok for f in ("...", "?")) and not os.path.exists(p):
        return True
    return os.path.exists(p)


def check_paths():
    missing = set()
    for f, t in TEXT.items():
        for m in re.finditer(BT + r"([^" + BT + r"\n]+)" + BT, t):
            tok = m.group(1).strip()
            if not (tok.startswith("wifi-") or tok.startswith("opensource/")):
                continue
            if not resolve_path(tok):
                missing.add(tok)
    check("every cited lane path resolves", not missing, "; ".join(sorted(missing)[:8]))


def check_evidence_index():
    t = TEXT.get("EVIDENCE-INDEX.md", "")
    ids = re.findall(r"\bEV-(\d{3})\b", t)
    nums = [int(x) for x in ids]
    uniq = sorted(set(nums))
    check("evidence claim IDs are unique", len(set(nums)) == len(nums), "duplicates")
    check("evidence index has at least 341 claims", len(uniq) >= 341, "%d" % len(uniq))
    check("evidence index carries the v5 range EV-278..EV-308", 278 in nums and 308 in nums,
          "278=%s 308=%s" % (278 in nums, 308 in nums))
    check("evidence index carries the v6 range EV-309..EV-341", 309 in nums and 341 in nums,
          "309=%s 341=%s" % (309 in nums, 341 in nums))
    check("evidence index carries the v3 range EV-196..EV-244", 196 in nums and 244 in nums,
          "196=%s 244=%s" % (196 in nums, 244 in nums))
    check("evidence index carries the v4 range EV-245..EV-277", 245 in nums and 277 in nums,
          "245=%s 277=%s" % (245 in nums, 277 in nums))
    check("evidence claims are numbered 1..N without gaps", uniq == list(range(1, len(uniq) + 1)),
          "max %d, count %d" % (uniq[-1] if uniq else 0, len(uniq)))
    rep = TEXT.get("REPORT.md", "")
    heads = {int(n) for n in re.findall(r"^## (\d+)\. ", rep, re.M)}
    rows = [ln for ln in t.splitlines() if ln.startswith("| EV-")]
    covered = set()
    bad = []
    for ln in rows:
        cells = [c.strip() for c in ln.strip("|").split("|")]
        if len(cells) < 4:
            bad.append(ln[:40])
            continue
        m = re.match(r"^(\d+)", cells[1])
        if not m:
            bad.append("bad section: " + cells[0])
            continue
        n = int(m.group(1))
        covered.add(n)
        if n not in heads:
            bad.append(cells[0] + " -> section " + str(n))
        if not re.search(r"\[(?:V\*?|I|U)\]", ln):
            bad.append(cells[0] + " has no marker")
    check("every evidence row maps to a real report section and has a marker", not bad,
          "; ".join(bad[:6]))
    check("evidence index covers report sections 1..13", set(range(1, 14)) <= covered,
          "missing " + str(sorted(set(range(1, 14)) - covered)))


def get_lane(rel):
    p = os.path.join(INCOMING, rel)
    return read(p) if os.path.isfile(p) else ""


def check_constants():
    rep, tab = TEXT.get("REPORT.md", ""), TEXT.get("TABLES.md", "")
    joined = (rep + tab).lower()
    checks_const = [
        ("golden page word 0", "0x86807fef",
         get_lane("wifi-hcc-core/src/hcc_core.h").lower()),
        ("golden page word 3", "0x0000abcd",
         get_lane("wifi-hcc-core/src/hcc_core.h").lower()),
        ("release magic 0x5A5A", "0x00005a5a",
         get_lane("wifi-rom-dump/REPORT.md").lower()),
        ("ready payload 0x01060850", "01060850",
         get_lane("wifi-ete-frames/REPORT.md").lower()),
        ("ctx precondition ctx+0x190", "ctx+0x190",
         get_lane("wifi-h2d-send/REPORT.md").lower()),
        ("TXQ sync cmd 0xD8", "d80014",
         get_lane("wifi-h2d-frames/src/h2d_selftest.c").lower()),
        ("PD3 patched image hash", "eea9f2ff",
         get_lane("wifi-irq-unmask/REPORT-PD3.md").lower()),
        ("PD4 patched image hash", "cfe24bfb194899f362bfcb0301d7e28a3b4dce0ef4e70b705d54a1f78d3037b1",
         get_lane("wifi-irq-unmask/REPORT-PD4.md").lower()),
        ("PD4 cave CPU 0x108240", "0x108240",
         get_lane("wifi-irq-unmask/REPORT-PD4.md").lower()),
        ("context-create key 0x64", "64001400",
         get_lane("wifi-h2d-send/REPORT.md").lower()),
        ("v2 image container", "22b72852",
         get_lane("wifi-upstream-target/BUILD-VERIFICATION.md").lower()),
        ("v2 kernel slice", "13e81214",
         get_lane("wifi-upstream-target/BUILD-VERIFICATION.md").lower()),
        ("v2 slot hash", "1794a531",
         get_lane("wifi-upstream-target/BUILD-VERIFICATION.md").lower()),
        ("cfg80211 module", "949393f9",
         get_lane("wifi-upstream-target/BUILD-VERIFICATION.md").lower()),
        ("clean driver hash", "d2b3cf79",
         get_lane("wifi-upstream-target/BUILD-VERIFICATION.md").lower()),
        ("calibration node length", "012a6d2b",
         get_lane("wifi-proto-seq/REPORT.md").lower()),
        ("calibration key 0x0101", "0x0101",
         get_lane("wifi-proto-seq/REPORT.md").lower()),
        ("MAC host-interface block", "0x40040000",
         get_lane("wifi-datapath/REPORT.md").lower()),
        ("ETE E1a/E1b ladder", "e1a",
         get_lane("wifi-ete-wake/REPORT.md").lower()),
        ("cfg80211 ready gate", "g4 id1",
         get_lane("wifi-cfg80211/REPORT.md").lower()),
        ("PD4 audit footprint", "68 patched bytes",
         get_lane("wifi-fw-audit/REPORT.md").lower()),
        ("PD4 audit harness", "31 checks",
         get_lane("wifi-fw-audit/REPORT.md").lower()),
        ("PD4 audit variant hash", "0bc1b3e8",
         get_lane("wifi-fw-audit/REPORT.md").lower()),
        ("PD4 audit H1 read pack", "0x7c41b0",
         get_lane("wifi-fw-audit/REPORT.md").lower()),
        ("PD4 audit raise CA", "0x40161188",
         get_lane("wifi-fw-audit/REPORT.md").lower()),
        ("VAP user_add SR node word1", "00b46d2b",
         get_lane("wifi-vap-scan/recipes/frames.md").lower()),
        ("scan request SR node word1", "02086d2b",
         get_lane("wifi-vap-scan/recipes/frames.md").lower()),
        ("provider caps 0x1f", "0x1f",
         get_lane("wifi-scan-ap/REPORT.md").lower()),
        ("data-plane module hash", "8fc693ad",
         get_lane("wifi-datapath-bringup/REPORT.md").lower()),
        ("TXQ-table address sync key 0x0320", "0x0320",
         get_lane("wifi-datapath-bringup/REPORT.md").lower()),
        ("bench-tooling selftest count 503", "503",
         get_lane("wifi-bench-tooling/README.md")),
        ("phase-matrix BLOCKED-DRIVER vocabulary", "blocked-driver",
         get_lane("wifi-bench-tooling/PHASES.md").lower()),
        ("H1 static sentinel pointer", "0x00118d68",
         get_lane("wifi-fw-audit/REPORT.md").lower()),
        ("AP-start module hash", "fd8e1439",
         get_lane("wifi-ap-start/HOSTAPD-BRINGUP.md").lower()),
        ("AP-start module size 206,708 B", "206,708",
         get_lane("wifi-ap-start/HOSTAPD-BRINGUP.md").lower()),
        ("hostapd AP mgmt bitmap 0x3c15", "0x3c15",
         get_lane("wifi-ap-start/HOSTAPD-BRINGUP.md").lower()),
        ("REPORTS_OBSS gate", "reports_obss",
         get_lane("wifi-ap-start/HOSTAPD-BRINGUP.md").lower()),
        ("wiphy_new_nm WARN line", "core.c:532",
         get_lane("wifi-ap-start/HOSTAPD-BRINGUP.md").lower()),
        ("merged data-plane module hash", "ca32aec4",
         get_lane("wifi-dp-air/BUILD-NOTES.md").lower()),
        ("merged data-plane module size 886,864 B", "886,864",
         get_lane("wifi-dp-air/BUILD-NOTES.md").lower()),
        ("F-AIR sequence hash", "96354365",
         get_lane("wifi-dp-air/LADDER-F-AIR.md").lower()),
        ("g1 id17 channel record size 124 B", "124 b",
         get_lane("wifi-scan-complete/REPORT.md").lower()),
        ("g1 id15 beacon size 509 B", "509 b",
         get_lane("wifi-scan-complete/REPORT.md").lower()),
        ("hostapd-mini package size 283,790 B", "283,790",
         get_lane("wifi-hostapd-deploy/REPORT.md").lower()),
        ("AP interface type rule", "type __ap",
         get_lane("wifi-hostapd-deploy/REPORT.md").lower()),
        ("ctx0 BAR0 correction 0x7D1758", "0x7d1758",
         get_lane("wifi-hostapd-deploy/REPORT.md").lower()),
        ("ABI image container hash", "df6a78c3",
         get_lane("wifi-abi-image/REPORT.md").lower()),
        ("ABI image rootfs UBI hash", "62344e80",
         get_lane("wifi-abi-image/REPORT.md").lower()),
        ("ABI image kernel package pin", "6f995278",
         get_lane("wifi-abi-image/REPORT.md").lower()),
        ("MAC RX live bank", "0x40050000",
         get_lane("wifi-rx-static/REPORT.md").lower()),
        ("MAC RX live status/mask", "0xfffffcc1",
         get_lane("wifi-rx-static/REPORT.md").lower()),
        ("MAC RX 36-byte device header", "36-byte",
         get_lane("wifi-rx-static/REPORT.md").lower()),
        ("WPA2 add_key command", "0x0078",
         get_lane("wifi-wpa-static/REPORT.md").lower()),
        ("WPA2 add_key worker", "0x0a5825",
         get_lane("wifi-wpa-static/REPORT.md").lower()),
        ("WPA2 set_default worker", "0x0a5c9d",
         get_lane("wifi-wpa-static/REPORT.md").lower()),
        ("WPA2 del_key command", "0x007a",
         get_lane("wifi-wpa-static/REPORT.md").lower()),
        ("five-message channel command", "0x0124",
         get_lane("wifi-beacon-static/REPORT.md").lower()),
        ("five-message channel worker", "0x841b8",
         get_lane("wifi-beacon-static/REPORT.md").lower()),
        ("scan dispatch row", "0x10cd5c",
         get_lane("wifi-scan-full/REPORT.md").lower()),
        ("g0 id10 handler", "0x5ff64",
         get_lane("wifi-scan-full/REPORT.md").lower()),
        ("STA connect opcode", "0x0123",
         get_lane("wifi-sta-static/REPORT.md").lower()),
        ("STA disconnect opcode", "0x00d0",
         get_lane("wifi-sta-static/REPORT.md").lower()),
        ("HT cap word", "0x0062",
         get_lane("wifi-sta-static/REPORT.md").lower()),
        ("VHT 160 MHz cap", "0x00400064",
         get_lane("wifi-sta-static/REPORT.md").lower()),
        ("final all-patch module hash", "5b587949",
         get_lane("wifi-stability/FINAL-INTEGRATION.md").lower()),
        ("final all-patch module size 308,080 B", "308,080",
         get_lane("wifi-stability/FINAL-INTEGRATION.md").lower()),
        ("final association module hash", "251d08f0",
         get_lane("wifi-stability/ASSOCIATION-INTEGRATION.md").lower()),
        ("final association module size 344,472 B", "344,472",
         get_lane("wifi-stability/ASSOCIATION-INTEGRATION.md").lower()),
        ("final association module parameters 56", "56 params",
         get_lane("wifi-stability/ASSOCIATION-INTEGRATION.md").lower()),
        ("corrected-carrier module hash", "ab9e18a4",
         get_lane("wifi-sibling-gpl/MGMT-CARRIER-CORRECTION.md").lower()),
        ("corrected-carrier module size 344,372 B", "344,372",
         get_lane("wifi-sibling-gpl/MGMT-CARRIER-CORRECTION.md").lower()),
        ("mgmt-TX completion event", "0x01f3",
         get_lane("wifi-sibling-gpl/MGMT-CARRIER-CORRECTION.md").lower()),
        ("WAL host-internal event", "0x01f2",
         get_lane("wifi-sibling-gpl/MGMT-CARRIER-CORRECTION.md").lower()),
        ("mgmt-TX device carrier", "g0 id6",
         get_lane("wifi-sibling-gpl/MGMT-CARRIER-CORRECTION.md").lower()),
        ("mgmt-RX CRX event", "g1 id13",
         get_lane("wifi-sibling-gpl/MGMT-CARRIER-CORRECTION.md").lower()),
        ("mgmt-RX 60-byte prefix", "60-byte prefix",
         get_lane("wifi-sibling-gpl/MGMT-CARRIER-CORRECTION.md").lower()),
        ("luofu SET_RX_FILTER negative", "set_rx_filter",
         get_lane("wifi-sibling-gpl/MGMT-CARRIER-CORRECTION.md").lower()),
        ("Hi3881 RX-filter constant", "0xf7b9faea",
         get_lane("wifi-sibling-gpl/TABLES.md").lower()),
        ("RF path-loss observation", "44 db path loss",
         get_lane("wifi-sibling-gpl/REPORT.md").lower()),
        ("R11 sweep field10 lever", "field10=25",
         get_lane("wifi-scan-tuning/FINDINGS-R10C.md").lower()),
        ("public-research fetch manifest", "51/51",
         get_lane("wifi-public-research-2/README.md").lower()),
        ("image-2 container hash", "0d9429ec",
         get_lane("wifi-final-image-2/HASHES.txt").lower()),
        ("image-2 container size 7,188,799 B", "7,188,799",
         get_lane("wifi-final-image-2/HASHES.txt").lower()),
        ("image-2 rootfs UBI hash", "44df3a63",
         get_lane("wifi-final-image-2/HASHES.txt").lower()),
        ("image-2 installed module hash", "4f1b45c1",
         get_lane("wifi-final-image-2/HASHES.txt").lower()),
        ("image-2 build verify 82/82", "82/82",
         get_lane("wifi-final-image-2/REPORT.md").lower()),
    ]
    for name, needle, src in checks_const:
        ok = needle in joined and needle in src
        check("cross-check: " + name + " matches the owning lane source", ok,
              "report=%s lane=%s" % (needle in joined, needle in src))

    mgmt_carrier = get_lane("wifi-sibling-gpl/MGMT-CARRIER-CORRECTION.md").lower()
    sibling_rep = get_lane("wifi-sibling-gpl/REPORT.md").lower()
    check("the g0 id 6 device carrier is cross-checked against the sibling lane",
          "g0 id6" in joined and "g0 id 6" in mgmt_carrier)
    check("the RX-ordering 0x02-vs-0x182 observation is cross-checked against the sibling lane",
          "0x182" in joined and "0x02 vs 0x182" in sibling_rep)
    check("the 56-parameter association module is cross-checked against the stability lane",
          "56 parameters" in joined and "56 params" in
          get_lane("wifi-stability/ASSOCIATION-INTEGRATION.md").lower())
    ledger_p2 = os.path.join(REPO, "mem-entries.md")
    ledger_t2 = read(ledger_p2).lower() if os.path.isfile(ledger_p2) else ""
    check("the v6 ledger tail records the association wave",
          "final association module" in ledger_t2 and "corrected mgmt-tx" in ledger_t2)

    txq_wire = "000100043000010000005a5a0000000001000000d8001400"
    ctx_wire = "000100043000010000005a5a000000000100000064001400"
    h2d_frames = get_lane("wifi-h2d-frames/REPORT.md").lower()
    h2d_send = get_lane("wifi-h2d-send/REPORT.md").lower()
    check("TXQ config sync wire frame matches the owning lane",
          txq_wire in tab.lower() and txq_wire in h2d_frames)
    check("context-create wire frame matches the owning lane",
          ctx_wire in tab.lower() and ctx_wire in h2d_send)

    hostapd_lane = get_lane("wifi-ap-start/HOSTAPD-BRINGUP.md").lower()
    ap_sizes = ["48 b", "40 b", "64 b", "184 b"]
    check("AP-start frame sizes 48/40/64/184 B match the owning lane",
          all(x in joined for x in ap_sizes) and all(x in hostapd_lane for x in ap_sizes))
    scan_lane = get_lane("wifi-scan-complete/REPORT.md").lower()
    check("scan-result numbers (6 results/6 beacons, 124/509 B) match the owning lane",
          "6 x 124" in tab.lower() and "124 b" in scan_lane and "509 b" in scan_lane)
    ledger_p = os.path.join(REPO, "mem-entries.md")
    ledger_text = read(ledger_p).lower() if os.path.isfile(ledger_p) else ""
    check("the matrix all-PASS evidence id appears in the report and the ledger",
          "20261124t094716z" in joined and "20261124t094716z" in ledger_text)
    check("the historical fired provider module (676,180 B / 4a1c3b9a...) is recorded in the report and the ledger",
          "4a1c3b9a" in joined and "4a1c3b9a" in ledger_text)
    scan_ap_notes = get_lane("wifi-scan-ap/BUILD-NOTES.md").lower()
    check("the owning lane's current provider build (704,720 B / 20d38c5c...) is cross-checked",
          "704,720" in joined and "20d38c5c" in joined and "20d38c5c" in scan_ap_notes)

    ledger_path = os.path.join(REPO, "mem-entries.md")
    status_path = os.path.join(REPO, "STATUS.md")
    if os.path.isfile(ledger_path):
        ledger = read(ledger_path)
        for needle, what in [("01011448", "type-1 ctx allocation"),
                             ("FULL DUPLEX HCC OPERATIONAL LOOP", "the operational-loop entry"),
                             ("0x86807FEF", "the golden page"),
                             ("0x108240", "the PD4 cave"),
                             ("LADDER C1", "the calibration ladder C1 entry"),
                             ("FIRST REAL WiFi INTERFACE", "the first Wi-Fi interface milestone"),
                             ("1794a531", "the cfg80211 image slot hash"),
                             ("cfg80211", "the cfg80211 milestone"),
                             ("THREE REAL FIRMWARE MANAGEMENT FRAMES", "the three-real-frames milestone"),
                             ("user_add_notify", "the real VAP notify frame"),
                             ("ONE-COMMAND FULL BRING-UP MATRIX", "the one-command matrix entry"),
                             ("PHASE-MATRIX", "the phase-matrix evidence"),
                             ("61.5 s", "the bounded reboot observation"),
                             ("H1 CONFIRMED", "the H1 consume-only verdict"),
                             ("HISTORIC MILESTONE", "the first-live-AP milestone"),
                             ("AP-ENABLED", "the AP-ENABLED state"),
                             ("cudy-ap-test", "the AP SSID"),
                             ("0x01010f18", "the VAP pointer"),
                             ("0x70094810", "the ctx0 flag change"),
                             ("FLASH COMPLETE", "the flash milestone"),
                             ("handover2", "the handover script"),
                             ("6f995278", "the kernel package ABI pin"),
                             ("38 gates", "the post-flash T0 verify"),
                             ("g1 id0x23", "the new post-AP D2H family"),
                             ("12 bss", "the 12-BSS decode"),
                             ("rx arm passed", "the RX arm fire"),
                             ("status=0x00000182", "the RX live-bank status"),
                             ("key_add", "the GTK key_add frame"),
                             ("key_default", "the GTK default-key frame"),
                             ("five-message", "the five-message AP order"),
                             ("ap_chan", "the channel-first AP step"),
                             ("completed (mac)", "the MAC TX completion"),
                             ("txq-table", "the TXQ-table publish"),
                             ("242 samples", "the 40-minute soak"),
                             ("e2 restart", "the E2 restart"),
                             ("all 5 static lanes", "the static-wave close"),
                             ("slot 0 only", "the scan slot-0 ceiling"),
                             ("0x10cd5c", "the corrected scan dispatch row"),
                             ("0x40050000", "the RX live bank"),
                             ("0x123", "the STA connect opcode"),
                             ("0x00d0", "the STA disconnect opcode"),
                             ("0x0062", "the HT cap word"),
                             ("0x00400064", "the 5 GHz VHT cap"),
                             ("0x0a5825", "the add_key worker"),
                             ("0x0124", "the AP channel command"),
                             ("final all-patch", "the final all-patch module"),
                             ("5b587949", "the final module hash"),
                             ("g0 id6", "the corrected g0 id6 carrier"),
                             ("86 b", "the 86 B management frame"),
                             ("field10=25", "the R11 sweep value"),
                             ("best remains 8", "the R11 8-channel best"),
                             ("sibling", "the sibling research lane"),
                             ("public-research-2", "the public research lane"),
                             ("association module", "the association module"),
                             ("0x182", "the RX armed status"),
                             ("set_rx_filter", "the SET_RX_FILTER negative"),
                             ("r11", "the R11 sweep")]:
            check("ledger contains " + what, needle.lower() in ledger.lower())
    else:
        check("superproject ledger is readable (expected at ../mem-entries.md)", False, ledger_path)
    if os.path.isfile(status_path):
        status = read(status_path)
        check("STATUS.md carries the full-duplex state section",
              "FULL DUPLEX" in status and "0x86807FEF" in status)
        check("STATUS.md carries the first-Wi-Fi-interface section and the image hash",
              "FIRST REAL WiFi INTERFACE" in status and "1794a531" in status)
        check("STATUS.md carries the automation-matrix + real-frame section (3l)",
              "3l." in status and "user_add" in status and "consume-only" in status)


def check_forbidden():
    bad = []
    forbidden = ["PRIVATE KEY", "BEGIN CERTIFICATE", ".omo-secrets", "/.ssh/", "router_ed25519"]
    for f, t in TEXT.items():
        for pat in forbidden:
            if pat in t:
                bad.append(f + ": " + pat)
    check("no secret/private-key material", not bad, "; ".join(bad))
    mac = re.compile(r"\b(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}\b")
    hits = []
    for f, t in TEXT.items():
        for m in mac.finditer(t):
            hits.append(f + ": " + m.group(0))
    check("no per-unit MAC addresses", not hits, "; ".join(hits[:3]))
    longhex = re.compile(r"\b[0-9a-fA-F]{400,}\b")
    big = []
    for f, t in TEXT.items():
        for m in longhex.finditer(t):
            big.append(f + " (%d hex chars)" % len(m.group(0)))
    check("no oversized hex blobs (vendor-binary smell)", not big, "; ".join(big[:3]))
    b64 = re.compile(r"[A-Za-z0-9+/]{2000,}={0,2}")
    bigb = []
    for f, t in TEXT.items():
        if b64.search(t):
            bigb.append(f)
    check("no oversized base64 blobs", not bigb, "; ".join(bigb))


def check_links():
    bad = []
    link = re.compile(r"\]\(([^)]+)\)")
    for f, t in TEXT.items():
        for m in link.finditer(t):
            target = m.group(1).split("#")[0].strip()
            if not target or target.startswith(("http://", "https://", "mailto:")):
                continue
            if not os.path.isfile(os.path.join(LANE, target)):
                bad.append(f + " -> " + target)
    check("relative markdown links resolve to companion files", not bad, "; ".join(bad[:6]))


def check_tables():
    bad = []
    for f in ["REPORT.md", "TABLES.md", "EVIDENCE-INDEX.md"]:
        for i, ln in enumerate(TEXT.get(f, "").splitlines(), 1):
            if ln.startswith("|") and ln.count("|") < 2:
                bad.append("%s:%d" % (f, i))
    check("markdown table rows are well formed", not bad, "; ".join(bad[:5]))
    tab = TEXT.get("TABLES.md", "")
    tabs = re.findall(r"^## (T\d+)\. ", tab, re.M)
    check("TABLES.md contains at least 45 numbered tables", len(tabs) >= 45, "%d" % len(tabs))
    nums = [int(x[1:]) for x in tabs]
    check("TABLES.md tables are numbered T1..T45 without gaps", nums == list(range(1, len(nums) + 1)), ",".join(map(str, nums)))
    for t_ in ["T21", "T22", "T23", "T24", "T25"]:
        check("TABLES.md carries the v2 table " + t_, ("## " + t_ + ". ") in tab)
    for t_ in ["T26", "T27", "T28"]:
        check("TABLES.md carries the v3 table " + t_, ("## " + t_ + ". ") in tab)
    for t_ in ["T29", "T30", "T31"]:
        check("TABLES.md carries the v4 table " + t_, ("## " + t_ + ". ") in tab)
    for t_ in ["T32", "T33", "T34", "T35", "T36", "T37", "T38"]:
        check("TABLES.md carries the v5 table " + t_, ("## " + t_ + ". ") in tab)
    for t_ in ["T39", "T40", "T41", "T42", "T43", "T44", "T45"]:
        check("TABLES.md carries the v6 table " + t_, ("## " + t_ + ". ") in tab)


def main():
    load_files()
    if len(TEXT) < len(FILES):
        print("")
        print("RESULT: FAIL (%d/%d checks)" % (len(checks) - len(failures), len(checks)))
        return 1
    check_report_structure()
    check_markers()
    check_paths()
    check_evidence_index()
    check_constants()
    check_forbidden()
    check_links()
    check_tables()
    print("")
    if failures:
        print("RESULT: FAIL (%d/%d checks, %d failure(s))" % (len(checks) - len(failures), len(checks), len(failures)))
        return 1
    print("RESULT: PASS (%d/%d checks)" % (len(checks), len(checks)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
