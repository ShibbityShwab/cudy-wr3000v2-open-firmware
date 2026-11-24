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
MIN_BYTES = {"REPORT.md": 40000, "TABLES.md": 20000, "EVIDENCE-INDEX.md": 12000,
             "REPRODUCE.md": 4000, "README.md": 1500}

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
    check("evidence index has at least 120 claims", len(uniq) >= 120, "%d" % len(uniq))
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
    ]
    for name, needle, src in checks_const:
        ok = needle in joined and needle in src
        check("cross-check: " + name + " matches the owning lane source", ok,
              "report=%s lane=%s" % (needle in joined, needle in src))

    txq_wire = "000100043000010000005a5a0000000001000000d8001400"
    ctx_wire = "000100043000010000005a5a000000000100000064001400"
    h2d_frames = get_lane("wifi-h2d-frames/REPORT.md").lower()
    h2d_send = get_lane("wifi-h2d-send/REPORT.md").lower()
    check("TXQ config sync wire frame matches the owning lane",
          txq_wire in tab.lower() and txq_wire in h2d_frames)
    check("context-create wire frame matches the owning lane",
          ctx_wire in tab.lower() and ctx_wire in h2d_send)

    ledger_path = os.path.join(REPO, "mem-entries.md")
    status_path = os.path.join(REPO, "STATUS.md")
    if os.path.isfile(ledger_path):
        ledger = read(ledger_path)
        for needle, what in [("01011448", "type-1 ctx allocation"),
                             ("FULL DUPLEX HCC OPERATIONAL LOOP", "the operational-loop entry"),
                             ("0x86807FEF", "the golden page"),
                             ("0x108240", "the PD4 cave")]:
            check("ledger contains " + what, needle.lower() in ledger.lower())
    else:
        check("superproject ledger is readable (expected at ../mem-entries.md)", False, ledger_path)
    if os.path.isfile(status_path):
        status = read(status_path)
        check("STATUS.md carries the full-duplex state section",
              "FULL DUPLEX" in status and "0x86807FEF" in status)


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
    check("TABLES.md contains at least 18 numbered tables", len(tabs) >= 18, "%d" % len(tabs))


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
