#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Generate the H2D catalogue data tables from the committed lane reports.

This script reads ONLY committed project documents (no vendor binary, no
firmware image, no device access) and emits:

    src/h2d_catalogue.inc   C data tables (vendor senders + firmware receivers)
    catalogue.md            the same catalogue as markdown for the REPORT

Sources (committed, cross-referenced by the lane):
    ../wifi-h2d-catalog/REPORT.md
        sec 5.1  vendor sender <-> firmware handler join, payload sizes
        App A    firmware HCC receive tables (group 2 / group 0 / early g0)
        App B    per-handler payload-access classes (probe-safety risk)
    ../../phase37/sr-message-catalog.md
        sec 2    the vendor H2D builder catalogue (joined in the table above)

Usage:
    python3 tools/gen_catalogue.py            # write the two files
    python3 tools/gen_catalogue.py --check    # verify committed files are current
"""

import os
import re
import sys

LANE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = os.path.normpath(os.path.join(LANE, "..", ".."))
CATALOG_REPORT = os.path.join(DOCS, "incoming", "wifi-h2d-catalog", "REPORT.md")
PHASE37 = os.path.join(DOCS, "phase37", "sr-message-catalog.md")
OUT_INC = os.path.join(LANE, "src", "h2d_catalogue.inc")
OUT_MD = os.path.join(LANE, "catalogue.md")

RISK = ["STUB", "NO_ACCESS", "BYTE_READ", "PTR_DEREF", "NO_HANDLER", "UNKNOWN"]


def fail(msg):
    sys.stderr.write("gen_catalogue: " + msg + "\n")
    sys.exit(1)


def parse_size(cell):
    """'#0xa8/0xb4' -> 168, 'dyn/dyn' -> -1."""
    payload = cell.split("/")[0].strip().lstrip("#")
    if payload == "dyn":
        return -1
    if payload.startswith("0x"):
        return int(payload, 16)
    return int(payload)


def parse_vendor_senders(text):
    """sec 5.1 table -> list of dicts (group, id, sender, payload_len, handler_ca)."""
    try:
        sec = text.split("### 5.1")[1].split("### 5.2")[0]
    except IndexError:
        fail("wifi-h2d-catalog REPORT.md sec 5.1 not found")
    rows = []
    for line in sec.splitlines():
        if not line.startswith("| "):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) != 6 or not re.fullmatch(r"\d+|0x[0-9a-f]+", cells[0]):
            continue
        g = int(cells[0], 16) if cells[0].startswith("0x") else int(cells[0])
        i = int(cells[1], 16)
        m = re.match(r"(0x[0-9a-f]+)", cells[4])
        rows.append({
            "group": g,
            "id": i,
            "sender": cells[2],
            "payload_len": parse_size(cells[3]),
            "handler": int(m.group(1), 16) if m else 0,
            "note": cells[5],
        })
    if len(rows) != 48:
        fail("expected 48 vendor senders, parsed %d" % len(rows))
    return rows


def parse_fw_tables(text):
    """Appendix A -> list of (group, id, handler_ca) incl. NULL slots (=0)."""
    try:
        app = text.split("## Appendix A")[1].split("## Appendix B")[0]
    except IndexError:
        fail("wifi-h2d-catalog REPORT.md Appendix A not found")
    out = []
    group = None
    for line in app.splitlines():
        m = re.match(r"== group (\d+)", line.strip())
        if m:
            group = int(m.group(1))
            continue
        if group is None:
            continue
        cells = line.strip().split()
        if len(cells) < 3 or not cells[0].isdigit():
            continue
        did_s, hca_s = cells[1], cells[2]
        if not re.fullmatch(r"0x[0-9a-f]+", did_s):
            continue
        did = int(did_s, 16)
        hca = 0 if hca_s == "NULL" else int(hca_s, 16)
        out.append({"group": group, "id": did, "handler": hca})
    if len(out) != 60:
        fail("expected 60 firmware receive-table slots, parsed %d" % len(out))
    return out


def parse_risk(text):
    """Appendix B -> {handler_ca: (risk_class, access_text)}."""
    try:
        app = text.split("## Appendix B")[1]
    except IndexError:
        fail("wifi-h2d-catalog REPORT.md Appendix B not found")
    risk = {}
    for line in app.splitlines():
        m = re.match(r"- id (0x[0-9a-f]+) fn (0x[0-9a-f]+): (.*)", line.strip())
        if not m:
            continue
        hca = int(m.group(2), 16)
        acc = m.group(3).strip()
        if "no early payload access" in acc:
            cls = "NO_ACCESS"
        elif "[W]" in acc:
            cls = "PTR_DEREF"
        elif acc:
            cls = "BYTE_READ"
        else:
            cls = "UNKNOWN"
        risk[hca] = (cls, acc)
    return risk


def build(vendor, fw, risk):
    fw_entries = []
    for e in fw:
        if e["handler"] == 0:
            cls = "NO_HANDLER"
            acc = "-"
        else:
            cls, acc = risk.get(e["handler"], ("UNKNOWN", "-"))
        fw_entries.append({
            "group": e["group"],
            "id": e["id"],
            "handler": e["handler"],
            "risk": cls,
            "access": acc,
        })
    by_key = {(e["group"], e["id"]): e for e in fw_entries}
    for v in vendor:
        f = by_key.get((v["group"], v["id"]))
        v["risk"] = f["risk"] if f else "UNKNOWN"
    return fw_entries


EXACT = {(0, 0x01), (0, 0x02), (0, 0x1d), (0, 0x09)}


def emit_inc(vendor, fw_entries):
    out = []
    out.append("/* SPDX-License-Identifier: GPL-2.0 */")
    out.append("/*")
    out.append(" * h2d_catalogue.inc - GENERATED by tools/gen_catalogue.py from committed")
    out.append(" * project documents; do not edit by hand (regenerate + git diff).")
    out.append(" *")
    out.append(" * Source: opensource/docs/incoming/wifi-h2d-catalog/REPORT.md")
    out.append(" *   sec 5.1 (vendor sender -> firmware handler join), Appendix A")
    out.append(" *   (firmware HCC receive tables), Appendix B (payload-access probe risk).")
    out.append(" * No vendor binary, firmware byte or device value is reproduced here.")
    out.append(" */")
    out.append("")
    out.append("/* Vendor H2D HCC senders (the frames the vendor host builds). */")
    out.append("static const struct h2d_vendor_sender h2d_vendor_senders[] = {")
    for v in vendor:
        flags = ["H2D_SND_HAS_HANDLER"] if v["handler"] else ["H2D_SND_NO_HANDLER"]
        if (v["group"], v["id"]) in EXACT:
            flags.append("H2D_SND_BUILDER_EXACT")
        out.append('\t{ "%s", %d, 0x%02x, %d, H2D_RISK_%s, %s },' % (
            v["sender"], v["group"], v["id"], v["payload_len"],
            v["risk"], " | ".join(flags)))
    out.append("};")
    out.append("")
    out.append("/* Firmware H2D HCC receive tables (what the firmware dispatches). */")
    out.append("static const struct h2d_fw_handler h2d_fw_handlers[] = {")
    for e in fw_entries:
        h = "0x%08xu" % e["handler"] if e["handler"] else "0"
        out.append('\t{ %d, 0x%02x, %s, H2D_RISK_%s },' % (
            e["group"], e["id"], h, e["risk"]))
    out.append("};")
    out.append("")
    return "\n".join(out)


def emit_md(vendor, fw_entries):
    bt = chr(96)
    out = []
    out.append("# H2D catalogue data tables (generated)")
    out.append("")
    out.append("Generated by " + bt + "tools/gen_catalogue.py" + bt + " from")
    out.append(bt + "../wifi-h2d-catalog/REPORT.md" + bt + " (sec 5.1, Appendix A, Appendix B).")
    out.append("Do not edit by hand; rerun the generator and commit the result.")
    out.append("")
    out.append("Risk classes are the *probe-safety* reading of the firmware handler's")
    out.append("payload accesses (Appendix B):")
    out.append("")
    out.append("| class | meaning |")
    out.append("| --- | --- |")
    out.append("| " + bt + "STUB" + bt + " | payload-free stub handler (the only H2D probe allowed without a capture) |")
    out.append("| " + bt + "NO_ACCESS" + bt + " | no early payload access found (payload-free-effective) |")
    out.append("| " + bt + "BYTE_READ" + bt + " | fixed byte/halfword field reads only |")
    out.append("| " + bt + "PTR_DEREF" + bt + " | word reads that may be dereferenced - never synthesize a payload |")
    out.append("| " + bt + "NO_HANDLER" + bt + " | registered slot exists but handler is NULL, or no slot |")
    out.append("| " + bt + "UNKNOWN" + bt + " | no Appendix-B entry |")
    out.append("")
    out.append("## 1. Vendor H2D senders (%d)" % len(vendor))
    out.append("")
    out.append("| grp | id | sender | payload B | fw handler risk |")
    out.append("| ---: | ---: | --- | ---: | --- |")
    for v in vendor:
        pl = "dyn" if v["payload_len"] < 0 else "0x%x" % v["payload_len"]
        out.append("| %d | 0x%02x | %s | %s | %s |" % (
            v["group"], v["id"], v["sender"], pl, v["risk"]))
    out.append("")
    out.append("## 2. Firmware H2D receive tables (%d slots)" % len(fw_entries))
    out.append("")
    out.append("| grp | id | handler CA | risk |")
    out.append("| ---: | ---: | ---: | --- |")
    for e in fw_entries:
        h = "0x%08x" % e["handler"] if e["handler"] else "NULL"
        out.append("| %d | 0x%02x | %s | %s |" % (e["group"], e["id"], h, e["risk"]))
    out.append("")
    by_risk = {}
    for e in fw_entries:
        by_risk.setdefault(e["risk"], 0)
        by_risk[e["risk"]] += 1
    out.append("Risk-class totals: " + ", ".join(
        "%s=%d" % (k, by_risk[k]) for k in RISK if k in by_risk))
    out.append("")
    return "\n".join(out)


def main():
    check = "--check" in sys.argv[1:]
    try:
        text = open(CATALOG_REPORT, encoding="utf-8").read()
        open(PHASE37, encoding="utf-8").read()
    except OSError as exc:
        fail("cannot read a source report: %s" % exc)
    vendor = parse_vendor_senders(text)
    fw = parse_fw_tables(text)
    risk = parse_risk(text)
    fw_entries = build(vendor, fw, risk)
    inc = emit_inc(vendor, fw_entries)
    md = emit_md(vendor, fw_entries)
    if check:
        bad = False
        for path, want in ((OUT_INC, inc), (OUT_MD, md)):
            try:
                have = open(path, encoding="utf-8").read()
            except OSError:
                have = None
            if have != want:
                sys.stderr.write("gen_catalogue: STALE %s\n" % path)
                bad = True
        print("gen_catalogue: %s" % ("STALE (regenerate)" if bad else "OK (committed files current)"))
        return 1 if bad else 0
    os.makedirs(os.path.dirname(OUT_INC), exist_ok=True)
    with open(OUT_INC, "w", encoding="utf-8") as f:
        f.write(inc)
    with open(OUT_MD, "w", encoding="utf-8") as f:
        f.write(md)
    print("gen_catalogue: wrote %s (%d vendor senders) and %s (%d fw slots)" % (
        OUT_INC, len(vendor), OUT_MD, len(fw_entries)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
