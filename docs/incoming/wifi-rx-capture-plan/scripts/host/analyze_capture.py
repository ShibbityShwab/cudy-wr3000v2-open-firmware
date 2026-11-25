#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""analyze_capture.py - classify an rx-capture-router evidence bundle for the
Cudy WR3000 v2.0 MAC-RX investigation (shared task-165).

The bundle is the directory written by rx-capture-router.sh (a
snap-LABEL directory with raw/, rings/, headers/, sysfs/, dmesg.txt,
samples.tsv, markers.log) optionally next to the host-side client files
(client-assoc-*.txt, client-dmesg-auth.txt, host-markers.log).

Output: a verdict (V-...) plus an evidence table and the next action. The
classifier implements REPORT.md section 7. It never invents observations: a
missing channel is reported as unavailable, which yields V-INCONCLUSIVE rather
than a guess.

usage: analyze_capture.py --dir DIR [--json OUT.json] | selftest
"""
import argparse
import json
import os
import re
import sys
import tempfile

BIT_NAMES = {
    0: "tx_complete_alias",
    1: "tx_complete",
    2: "RX_complete",
    3: "norm_ring_empty",
    4: "small_ring_empty",
    5: "data_ring_overrun",
    6: "location_complete_error",
    7: "ba_info_ring_overrun",
    8: "ba_info_ring_overrun",
    9: "ba_win_exceed_error",
    10: "rx_ppdu_desc_error",
}
RX_BITS = (2, 5, 10)          # RX completion / overrun / ppdu-desc error
RX_ERR_BITS = (5, 6, 10)      # classes that mean MAC-side RX trouble
BA_BITS = (7, 8, 9)

VERDICTS = {
    "V-MAC-NOSEE": "No host-visible RX evidence while the client provably transmitted.",
    "V-MAC-SAW-NOWRITE": "MAC-side RX/error status changed but no frame reached the host path.",
    "V-MAC-WROTE-NOEVENT": "A completion/buffer write appeared, but no management-RX event was delivered.",
    "V-EVENT-NOT-DELIVERED": "A device management-RX event (g1 id13) appeared but the host did not deliver it.",
    "V-DELIVERED-NO-AP": "The management RX event was delivered to cfg80211 but the AP layer stayed silent.",
    "V-CRASH": "A firmware exception (page/mask/staging/excp_announce) was observed; RX classification is contaminated.",
    "V-NOTRIGGER": "No client-transmission evidence in the bundle; the experiment did not run.",
    "V-INCONCLUSIVE": "A required observation channel is missing; no verdict can be justified.",
}

NEXT_ACTION = {
    "V-MAC-NOSEE": "RF/PHY/MAC-enable path, not host delivery: verify the AP MAC channel/mode readback and the air-side probe/ACK discriminator (RUNBOOK phase 0b); do not change the mgmt-RX/event decoder.",
    "V-MAC-SAW-NOWRITE": "Audit the host-visible RX ring config vs vendor writes (ring addresses/sizes/buffer lengths/entry format, section 6); the MAC reached the RX path but could not hand a frame to host memory.",
    "V-MAC-WROTE-NOEVENT": "Data-path RX works; the mgmt-RX/event path (g1 id13) is the gap. Next: firmware VAP/user-state and the g1 id13 trigger, not ring config.",
    "V-EVENT-NOT-DELIVERED": "The device sent g1 id13; the host decoder/delivery hook is the gap. Audit the 60-byte prefix decode (ch/len/RSSI) and the cfg80211 delivery path.",
    "V-DELIVERED-NO-AP": "cfg80211 saw the frame; the AP FSM (hostapd/auth response) is the gap. Re-run with mgmt RX logging and hostapd -d.",
    "V-CRASH": "Power-cycle and isolate the crash first (RUNBOOK phase B isolation ladder); RX capture is invalid under an exception.",
    "V-NOTRIGGER": "Re-run the client trigger; confirm client-dmesg-auth.txt contains 'send auth' lines.",
    "V-INCONCLUSIVE": "Fix the missing channel (raw /dev/mem, sampler, or client evidence) and re-run.",
}


def rd(path, default=None):
    try:
        with open(path, "r", errors="replace") as f:
            return f.read()
    except OSError:
        return default


def parse_hex_listing(text):
    out = []
    if not text:
        return out
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        tok = line.split()[0]
        try:
            out.append(int(tok, 16))
        except ValueError:
            continue
    return out


def parse_words(text):
    """raw_words output: one bare 8-hex word per line (od or hexdump)."""
    return parse_hex_listing(text)


def word_or_none(words, idx):
    if idx < len(words):
        return words[idx]
    return None


def bit_names(value):
    if value is None:
        return ""
    names = []
    for b in range(0, 16):
        if value & (1 << b):
            names.append("bit%d=%s" % (b, BIT_NAMES.get(b, "unknown")))
    return " ".join(names) if names else "none"


def parse_live_bank(snap):
    """Return the live host-MAC bank (status/norm/small/cmp) for a snapshot."""
    words = parse_words(rd(os.path.join(snap, "raw", "live-bank.txt"), ""))
    if not words:
        return None
    out = {
        "status": word_or_none(words, 0),
        "status_plus04": word_or_none(words, 1),
        "norm_rptr": word_or_none(words, 2),
        "small_rptr": word_or_none(words, 3),
        "cmp_wptr": word_or_none(words, 4),
    }
    return out


def parse_page(snap):
    words = parse_words(rd(os.path.join(snap, "raw", "page.txt"), ""))
    return {
        "page0": word_or_none(words, 0),
        "page4": word_or_none(words, 1),
        "page8": word_or_none(words, 2),
        "page12": word_or_none(words, 3),
    }


def parse_excp(snap):
    mask = parse_words(rd(os.path.join(snap, "raw", "excp-mask.txt"), ""))
    frame = parse_words(rd(os.path.join(snap, "raw", "excp-frame.txt"), ""))
    return {
        "mask": word_or_none(mask, 0),
        "staging": word_or_none(frame, 0),
        "frame": frame,
    }


def parse_meta(snap):
    txt = rd(os.path.join(snap, "meta.txt"), "") or ""
    out = {}
    for line in txt.splitlines():
        if ":" in line:
            k, v = line.split(":", 1)
            out[k.strip()] = v.strip()
    return out


def parse_sysfs(snap, name):
    return rd(os.path.join(snap, "sysfs", name + ".txt"), None)


def parse_hcc_events(text):
    out = {"raw": text or "", "excp_announce": None, "vector_mask": None,
           "d2h_words": None, "per_id": {}}
    if not text:
        return out
    first = text.splitlines()[0] if text.splitlines() else ""
    m = re.search(r"d2h words=(\d+)", first)
    if m:
        out["d2h_words"] = int(m.group(1))
    m = re.search(r"excp_announce=(-?\d+)", first)
    if m:
        out["excp_announce"] = int(m.group(1))
    m = re.search(r"vector_mask=0x([0-9a-fA-F]+)", first)
    if m:
        out["vector_mask"] = int(m.group(1), 16)
    for m in re.finditer(r"id(\d+)=(\d+)", text):
        out["per_id"][int(m.group(1))] = int(m.group(2))
    return out


def parse_rd_catalog(text):
    out = {"raw": text or "", "counts": {}}
    if not text:
        return out
    for m in re.finditer(r"g=(\d+)\s+id=0x([0-9a-fA-F]+)\s+\(\s*(\d+)\)\s+count=(\d+)", text):
        g = int(m.group(1))
        i = int(m.group(2), 16)
        n = int(m.group(4))
        out["counts"][(g, i)] = n
    return out


def parse_rd_decode(text):
    out = {"raw": text or "", "frames": []}
    if not text:
        return out
    for line in text.splitlines():
        m = re.search(r"t=(\d+)ms dr(\d+) node=0x([0-9a-fA-F]+) len=(\d+) g=(\d+) id=(\d+) .*payload\[(\d+)\]=([0-9a-fA-F]*)", line)
        if m:
            out["frames"].append({
                "t_ms": int(m.group(1)), "ch": int(m.group(2)),
                "node": int(m.group(3), 16), "len": int(m.group(4)),
                "group": int(m.group(5)), "id": int(m.group(6)),
                "payload_len": int(m.group(7)), "payload": m.group(8),
            })
    return out


def parse_rings(snap):
    meta = rd(os.path.join(snap, "rings", "meta.txt"), "") or ""
    out = {"norm_devva": None, "small_devva": None, "cmp_devva": None,
           "cmp_nonzero": [], "norm_entries": 0, "small_entries": 0}
    m = re.search(r"norm_ring_devva=0x([0-9a-fA-F]+)", meta)
    if m:
        out["norm_devva"] = int(m.group(1), 16)
    m = re.search(r"small_ring_devva=0x([0-9a-fA-F]+)", meta)
    if m:
        out["small_devva"] = int(m.group(1), 16)
    m = re.search(r"cmp_ring_devva=0x([0-9a-fA-F]+)", meta)
    if m:
        out["cmp_devva"] = int(m.group(1), 16)
    nz = rd(os.path.join(snap, "rings", "cmp-nonzero.txt"), "") or ""
    for line in nz.splitlines():
        parts = line.split()
        if len(parts) >= 2:
            try:
                out["cmp_nonzero"].append((int(parts[0]), int(parts[1], 16)))
            except ValueError:
                pass
    for kind in ("norm", "small"):
        words = parse_words(rd(os.path.join(snap, "rings", kind + ".txt"), ""))
        out[kind + "_entries"] = len([w for w in words if w])
    return out


def parse_headers(snap):
    """header sweep lines: INDEX 0xDEVVVA HEXBYTES"""
    out = {}
    for kind in ("norm", "small"):
        txt = rd(os.path.join(snap, "headers", kind + ".txt"), "") or ""
        d = {}
        for line in txt.splitlines():
            parts = line.split()
            if len(parts) >= 3:
                try:
                    d[int(parts[0])] = parts[2]
                except ValueError:
                    continue
        out[kind] = d
    return out


def parse_samples(snap):
    txt = rd(os.path.join(snap, "samples.tsv"), "") or ""
    rows = []
    header = []
    for line in txt.splitlines():
        if not line.strip():
            continue
        if line.startswith("#"):
            header.append(line)
            continue
        parts = line.split("\t")
        if len(parts) < 14:
            continue
        try:
            row = {
                "tick": int(parts[0]), "epoch": int(parts[1]),
                "page0": parts[3], "page8": parts[4], "golden": parts[5],
                "mask": parts[6], "staging": parts[7], "excp_ann": parts[8],
                "status": parts[9], "norm_rptr": parts[11],
                "small_rptr": parts[12], "cmp_wptr": parts[13],
            }
        except ValueError:
            continue
        rows.append(row)
    return {"header": header, "rows": rows}


def parse_client(parent):
    out = {"attempts": 0, "send_auth_lines": [], "assoc_files": [], "markers": []}
    mk = rd(os.path.join(parent, "host-markers.log"), None)
    if mk:
        out["markers"] = mk.splitlines()
        out["attempts"] = len(re.findall(r"client auth attempt \d+ start", mk))
    for name in sorted(os.listdir(parent)) if os.path.isdir(parent) else []:
        if name.startswith("client-assoc-"):
            out["assoc_files"].append(name)
    dmesg = rd(os.path.join(parent, "client-dmesg-auth.txt"), "") or ""
    out["send_auth_lines"] = [ln for ln in dmesg.splitlines() if "send auth" in ln.lower()]
    return out


def parse_dmesg(snap):
    txt = rd(os.path.join(snap, "dmesg.txt"), "") or ""
    pats = {
        "mgmt_rx_delivered": r"mgmt RX delivered|mgmt_rx|cfg80211:.*mgmt",
        "mgmt_tx_post": r"frame post .mgmt_tx|mgmt_tx",
        "exception": r"\bEXCEPTION\b|excp_announce=1|vector_capture|golden:0|golden: 0",
        "rx_arm": r"DP rx_arm",
        "rx_drain": r"DP rx_drain",
        "rx_poll": r"DP rx_poll",
        "d2h_g1id13": r"g=1 id=13|g1 id13",
    }
    out = {}
    for k, p in pats.items():
        out[k] = re.findall(p, txt, flags=re.IGNORECASE)
    out["raw"] = txt
    return out



def crash_flags(pre, post, samples, excp_pre, excp_post, dmesg_post):
    flags = []
    for name, snap in (("pre", pre), ("post", post)):
        p = parse_page(snap) if snap else {}
        if p:
            if p.get("page0") not in (0x86807FEF, None):
                flags.append("%s: page0 lost golden (0x%08x)" % (name, p["page0"]))
            elif p.get("page8") not in (2, None):
                flags.append("%s: page+8 != 2 (0x%08x)" % (name, p["page8"]))
        if excp_pre is not None and name == "pre" and excp_pre.get("mask") not in (0, None):
            flags.append("pre: vector mask 0x%08x" % excp_pre["mask"])
        if excp_post is not None and name == "post" and excp_post.get("mask") not in (0, None):
            flags.append("post: vector mask 0x%08x" % excp_post["mask"])
        if excp_pre is not None and name == "pre" and excp_pre.get("staging") not in (0, None):
            flags.append("pre: staging 0x%08x" % excp_pre["staging"])
        if excp_post is not None and name == "post" and excp_post.get("staging") not in (0, None):
            flags.append("post: staging 0x%08x" % excp_post["staging"])
        c = rd(os.path.join(snap, "crash.txt"), "") if snap else ""
        if c and "CRASH: YES" in c:
            flags.append("%s: crash.txt reports deviation" % name)
    for row in samples.get("rows", []):
        if row.get("golden") == "0":
            flags.append("sampler tick %d: golden flag 0" % row["tick"])
            break
    try:
        ann = int(samples.get("rows", [{}])[0].get("excp_ann") or 0) if samples.get("rows") else 0
    except ValueError:
        ann = 0
    if ann:
        flags.append("sampler: excp_announce=%d" % ann)
    if dmesg_post:
        if dmesg_post.get("exception"):
            flags.append("dmesg: exception marker")
    return flags


def classify(root):
    root = os.path.abspath(root)
    if not os.path.isdir(root):
        return {"verdict": "V-INCONCLUSIVE", "reason": "directory does not exist", "evidence": []}
    parent = root
    snapdirs = []
    if os.path.isfile(os.path.join(root, "meta.txt")):
        snapdirs = [root]
        parent = os.path.dirname(root)
    else:
        for name in sorted(os.listdir(root)):
            if name.startswith("snap-") and os.path.isdir(os.path.join(root, name)):
                snapdirs.append(os.path.join(root, name))
    def pick(suffix):
        for s in snapdirs:
            if s.endswith(suffix):
                return s
        return None
    pre = pick("-pre")
    post = pick("-post")
    sampler = None
    for s in snapdirs:
        if not s.endswith("-pre") and not s.endswith("-post"):
            sampler = s
            break
    if post is None and pre is None:
        post = snapdirs[0] if snapdirs else None
    effective = sampler or post or pre
    evidence = []

    def ev(channel, value, note=""):
        evidence.append({"channel": channel, "value": value, "note": note})

    if effective is None:
        return {"verdict": "V-INCONCLUSIVE", "reason": "no snapshot directory in the bundle",
                "evidence": evidence}

    client = parse_client(parent)
    snap = effective
    live_pre = parse_live_bank(pre) if pre else None
    live_post = parse_live_bank(post) if post else parse_live_bank(snap)
    ring = parse_rings(post or snap)
    hdr_pre = parse_headers(pre) if pre else {"norm": {}, "small": {}}
    hdr_post = parse_headers(post or snap) if post else {"norm": {}, "small": {}}
    samples = parse_samples(effective)
    events_pre = parse_hcc_events(parse_sysfs(pre, "hcc_events") if pre else None)
    events_post = parse_hcc_events(parse_sysfs(post, "hcc_events") if post else None)
    rd_pre = parse_rd_catalog(parse_sysfs(pre, "rd_catalog") if pre else None)
    rd_post = parse_rd_catalog(parse_sysfs(post, "rd_catalog") if post else None)
    if not rd_post["counts"]:
        rd_post = parse_rd_catalog(parse_sysfs(snap, "rd_catalog"))
    if not rd_pre["counts"] and pre:
        rd_pre = parse_rd_catalog(parse_sysfs(pre, "rd_catalog"))
    rd_dec = parse_rd_decode(parse_sysfs(post or snap, "rd_decode"))
    dmesg_post = parse_dmesg(post) if post else parse_dmesg(snap)
    excp_pre = parse_excp(pre) if pre else None
    excp_post = parse_excp(post) if post else None

    ev("client attempts (host markers)", client["attempts"],
       "assoc files: %s" % (", ".join(client["assoc_files"]) or "none"))
    ev("client 'send auth' dmesg lines", len(client["send_auth_lines"]),
       (client["send_auth_lines"][-1][:120] if client["send_auth_lines"] else "none"))
    if live_pre:
        ev("live bank pre (CA 0x40050000)", "status=0x%08x norm=0x%04x small=0x%04x cmp=0x%04x"
           % (live_pre["status"] or 0, live_pre["norm_rptr"] or 0,
              live_pre["small_rptr"] or 0, live_pre["cmp_wptr"] or 0))
    if live_post:
        ev("live bank post", "status=0x%08x norm=0x%04x small=0x%04x cmp=0x%04x"
           % (live_post["status"] or 0, live_post["norm_rptr"] or 0,
              live_post["small_rptr"] or 0, live_post["cmp_wptr"] or 0))
        ev("live status bits (post)", bit_names(live_post["status"]))
    statuses = []
    for row in samples.get("rows", []):
        try:
            statuses.append(int(row["status"], 16))
        except (ValueError, KeyError):
            pass
    any_rx_status = any(s & (1 << b) for s in statuses for b in RX_BITS)
    any_rx_err = any(s & (1 << b) for s in statuses for b in RX_ERR_BITS)
    cmp_values = []
    for row in samples.get("rows", []):
        try:
            cmp_values.append(int(row["cmp_wptr"], 16))
        except (ValueError, KeyError):
            pass
    if live_post:
        cmp_values.append(live_post["cmp_wptr"] or 0)
    cmp_moved = any(v not in (0, None) for v in cmp_values)
    ev("sampler rows", len(samples.get("rows", [])),
       "status RX bits seen: %s; cmp_wptr nonzero seen: %s" % (any_rx_status, cmp_moved))
    ev("completion ring non-zero entries", len(ring["cmp_nonzero"]),
       "cmp ring devVA: %s" % (("0x%08x" % ring["cmp_devva"]) if ring["cmp_devva"] else "unavailable"))
    ev("free ring entries (norm/small)", "%d/%d" % (ring["norm_entries"], ring["small_entries"]))
    changed_headers = 0
    headers_available = bool(hdr_pre.get("norm")) and bool(hdr_post.get("norm"))
    if headers_available:
        for kind in ("norm", "small"):
            keys = set(hdr_pre.get(kind, {})) | set(hdr_post.get(kind, {}))
            for k in keys:
                if hdr_pre.get(kind, {}).get(k) != hdr_post.get(kind, {}).get(k):
                    changed_headers += 1
    ev("buffer device headers changed (pre vs post)", changed_headers,
       "header sweep available: %s" % headers_available)
    rd_delta = {}
    for key, cnt in rd_post["counts"].items():
        old = rd_pre["counts"].get(key, 0)
        if cnt > old:
            rd_delta[key] = cnt - old
    mgmt_events = [f for f in rd_dec["frames"] if f["group"] == 1 and f["id"] == 13]
    g1id13_delta = rd_delta.get((1, 13), 0)
    ev("D2H catalogue delta", {("%d/%d" % k): v for k, v in sorted(rd_delta.items())} or "none",
       "g1 id13 delta: %d; decoded g1 id13 frames kept: %d" % (g1id13_delta, len(mgmt_events)))
    ev("dmesg mgmt RX delivery lines", len(dmesg_post.get("mgmt_rx_delivered", [])))
    ev("dmesg exception markers", len(dmesg_post.get("exception", [])))
    crash = crash_flags(pre, post, samples, excp_pre, excp_post, dmesg_post)
    ev("crash evidence", crash or "none")

    trigger_seen = client["attempts"] > 0 or bool(client["send_auth_lines"])
    raw_seen = live_post is not None
    sampler_seen = len(samples.get("rows", [])) > 0

    if not trigger_seen:
        verdict = "V-NOTRIGGER"
        reason = "no client-transmission evidence in the bundle"
    elif crash:
        verdict = "V-CRASH"
        reason = "; ".join(crash)
    elif g1id13_delta > 0 or mgmt_events:
        if dmesg_post.get("mgmt_rx_delivered"):
            verdict = "V-DELIVERED-NO-AP"
            reason = "g1 id13 event seen and a cfg80211 delivery line exists; no AP-layer progress"
        else:
            verdict = "V-EVENT-NOT-DELIVERED"
            reason = "g1 id13 event seen in the D2H catalogue but no host delivery line"
    elif cmp_moved or len(ring["cmp_nonzero"]) > 0 or changed_headers > 0:
        verdict = "V-MAC-WROTE-NOEVENT"
        reason = "completion/buffer memory changed but no management-RX event"
    elif any_rx_err or any_rx_status:
        verdict = "V-MAC-SAW-NOWRITE"
        reason = "MAC RX/error status bits changed without a host-visible completion"
    elif not raw_seen or not sampler_seen:
        verdict = "V-INCONCLUSIVE"
        reason = "no raw live-bank sample and/or no sampler rows: channels missing"
    else:
        verdict = "V-MAC-NOSEE"
        reason = "all host-visible RX channels stayed at baseline while the client transmitted"
    return {
        "verdict": verdict,
        "reason": reason,
        "next_action": NEXT_ACTION.get(verdict, ""),
        "evidence": evidence,
        "client": {"attempts": client["attempts"], "send_auth_lines": len(client["send_auth_lines"])},
        "notes": "A zero data-ring delta is expected for management-only traffic (CRX/DMAC split); it is not by itself a MAC-blindness proof.",
    }


def render(result):
    lines = []
    lines.append("RX CAPTURE ANALYSIS (E-RX-1)")
    lines.append("============================")
    lines.append("verdict: %s" % result.get("verdict"))
    lines.append("reason:  %s" % result.get("reason"))
    lines.append("")
    lines.append("evidence channels:")
    for item in result.get("evidence", []):
        if "channel" not in item:
            continue
        lines.append("  - %-46s %s" % (item["channel"], item["value"]))
        if item.get("note"):
            lines.append("      %s" % item["note"])
    lines.append("")
    lines.append("next action: %s" % result.get("next_action", ""))
    if result.get("notes"):
        lines.append("note: %s" % result["notes"])
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# selftest: synthetic bundles (no device, no vendor data)
# ---------------------------------------------------------------------------
def write(p, s):
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "w") as f:
        f.write(s)


def make_snap(root, name, *, golden=1, status=0x182, cmp_wptr=0,
              mask=0, staging=0, cmp_nonzero=None, hdr_hex="00" * 64,
              cmp_rptr=0x0, dmesg="", rd_catalog="", hcc_excp=0,
              page0=0x86807FEF, attempts=0):
    snap = os.path.join(root, name)
    write(os.path.join(snap, "meta.txt"), "label: %s\nraw_ok: 1 (selftest)\n" % name)
    write(os.path.join(snap, "raw", "live-bank.txt"),
          "%08x\n%08x\n%08x\n%08x\n%08x\n" % (status, 0, 0x20, 0x20, cmp_wptr))
    write(os.path.join(snap, "raw", "page.txt"),
          "%08x\n%08x\n%08x\n%08x\n" % (page0, 7, 2 if golden else 1, 0xabcd))
    write(os.path.join(snap, "raw", "excp-mask.txt"), "%08x\n" % mask)
    write(os.path.join(snap, "raw", "excp-frame.txt"), "%08x\n" % staging + "0\n" * 16)
    write(os.path.join(snap, "raw", "summary.txt"), "selftest\n")
    write(os.path.join(snap, "rings", "meta.txt"),
          "norm_ring_devva=0x80000000\nsmall_ring_devva=0x80001000\ncmp_ring_devva=0x80002000\n")
    write(os.path.join(snap, "rings", "norm.txt"), "%08x\n" % 0x80010004 + "0\n" * 255)
    write(os.path.join(snap, "rings", "small.txt"), "%08x\n" % 0x80020004 + "0\n" * 511)
    write(os.path.join(snap, "rings", "cmp.txt"), "0\n" * 4095)
    nz = cmp_nonzero or []
    write(os.path.join(snap, "rings", "cmp-nonzero.txt"),
          "".join("%d 0x%08x\n" % (i, v) for i, v in nz))
    write(os.path.join(snap, "headers", "norm.txt"), "0 0x80010004 %s\n" % hdr_hex)
    write(os.path.join(snap, "headers", "small.txt"), "0 0x80020004 %s\n" % hdr_hex)
    write(os.path.join(snap, "sysfs", "hcc_events.txt"),
          "HCC events: d2h words=1 last=0x4 dropped=0 stop_gt10=0 need_dr=0 excp_announce=%d vector_mask=0x%x\n" % (hcc_excp, mask))
    write(os.path.join(snap, "sysfs", "rd_catalog.txt"),
          "D2H DIRECT catalogue (this boot):\n" + rd_catalog)
    write(os.path.join(snap, "sysfs", "rd_decode.txt"), "D2H DIRECT decode log: 0 frame(s) kept\n")
    write(os.path.join(snap, "sysfs", "dp_status.txt"),
          "host rings: txq_table=0x835d4000 (0 B) tx_msdu_rings=0x0 (0 B) txba=0x0 (0 B) norm_ring=0x80000000 small_ring=0x80001000 cmp_ring=0x80002000\n")
    write(os.path.join(snap, "dmesg.txt"), dmesg)
    write(os.path.join(snap, "crash.txt"),
          "page0=0x%08x page8=0x%08x excp_mask=0x%08x staging=0x%08x\nCRASH: %s\n"
          % (page0, 2 if golden else 1, mask, staging, "no" if (golden and mask == 0 and staging == 0) else "YES"))
    write(os.path.join(snap, "samples.tsv"),
          "#tick\tepoch\tuptime_s\tpage0\tpage8\tgolden\tmask\tstaging\texcp_ann\tstatus\tstatus_bits\tnorm_rptr\tsmall_rptr\tcmp_wptr\n"
          "1\t100\t10\t0x%08x\t0x2\t%d\t0x%08x\t0x%08x\t%d\t0x%08x\t-\t0x20\t0x20\t0x%04x\n"
          % (page0, golden, mask, staging, hcc_excp, status, cmp_wptr))
    write(os.path.join(snap, "markers.log"), "SAMPLE END\n")
    return snap


def make_bundle(root, snap_opts=None, pre_opts=None, post_opts=None,
                attempts=0, raw=True):
    snap_opts = snap_opts or {}
    pre_opts = pre_opts or {}
    post_opts = post_opts or {}
    make_snap(root, "snap-t-pre", **dict({"status": 0x182, "cmp_wptr": 0}, **pre_opts))
    make_snap(root, "snap-t-post", **dict({"status": 0x182, "cmp_wptr": 0}, **post_opts))
    snapshot = make_snap(root, "snap-t", **dict({"status": 0x182, "cmp_wptr": 0}, **snap_opts))
    if not raw:
        for d in ("snap-t-pre", "snap-t-post", "snap-t"):
            p = os.path.join(root, d, "raw", "live-bank.txt")
            if os.path.exists(p):
                os.remove(p)
    if attempts:
        write(os.path.join(root, "host-markers.log"),
              "".join("client auth attempt %d start epoch=%d\n" % (i, 100 + i) for i in range(1, attempts + 1)))
        write(os.path.join(root, "client-assoc-1.txt"), "stage: AUTH-TIMEOUT\n")
        write(os.path.join(root, "client-dmesg-auth.txt"),
              "[123.4] wlan0: send auth to 06:05:04:03:02:01 (try 1/3)\n")
    return snapshot


def selftest():
    fails = 0
    tmp = tempfile.mkdtemp(prefix="rxcap-selftest-")
    cases = []

    def run_case(name, expected, **kwargs):
        root = os.path.join(tmp, name)
        os.makedirs(root, exist_ok=True)
        make_bundle(root, **kwargs)
        res = classify(root)
        ok = res["verdict"] == expected
        print("%s %-28s got=%-24s expected=%s" % ("ok  " if ok else "FAIL", name, res["verdict"], expected))
        nonlocal fails
        if not ok:
            fails += 1
        cases.append(res)

    run_case("mac-nosee", "V-MAC-NOSEE", attempts=3)
    run_case("mac-saw-nowrite", "V-MAC-SAW-NOWRITE", attempts=3,
             snap_opts={"status": 0x182 | (1 << 10)})
    run_case("mac-wrote-noevent", "V-MAC-WROTE-NOEVENT", attempts=3,
             snap_opts={"cmp_wptr": 0x20, "cmp_nonzero": [(0, 0x80010004)]},
             post_opts={"cmp_wptr": 0x20, "cmp_nonzero": [(0, 0x80010004)]})
    run_case("event-not-delivered", "V-EVENT-NOT-DELIVERED", attempts=3,
             snap_opts={"rd_catalog": "  g=1  id=0x0d ( 13) count=1    last_total=0x00b4 last_len=120 unknown\n"})
    run_case("delivered-no-ap", "V-DELIVERED-NO-AP", attempts=3,
             snap_opts={"rd_catalog": "  g=1  id=0x0d ( 13) count=1    last_total=0x00b4 last_len=120 unknown\n"},
             post_opts={"dmesg": "cfg80211: mgmt RX delivered (subtype 0x0b)\n"})
    run_case("crash", "V-CRASH", attempts=3,
             post_opts={"golden": 0, "mask": 0x04000010, "hcc_excp": 1})
    run_case("notrigger", "V-NOTRIGGER", attempts=0)
    run_case("inconclusive", "V-INCONCLUSIVE", attempts=3, raw=False)
    if os.path.isdir(tmp):
        for name in os.listdir(tmp):
            path = os.path.join(tmp, name)
            for dp, dn, fn in os.walk(path, topdown=False):
                for f in fn:
                    os.remove(os.path.join(dp, f))
                for d in dn:
                    os.rmdir(os.path.join(dp, d))
            os.rmdir(path)
        os.rmdir(tmp)
    if fails == 0:
        print("SELFTEST PASS (%d cases)" % len(cases))
        return 0
    print("SELFTEST FAIL (%d/%d)" % (fails, len(cases)))
    return 1


def main(argv):
    ap = argparse.ArgumentParser(description="classify an RX-capture evidence bundle (read-only)")
    ap.add_argument("--dir", help="bundle directory (contains snap-*)")
    ap.add_argument("--json", help="write the result as JSON to this path")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("cmd", nargs="?", choices=["selftest"], help="run the host selftest")
    args = ap.parse_args(argv)
    if args.cmd == "selftest":
        return selftest()
    if not args.dir:
        ap.error("--dir DIR is required (or the selftest command)")
    result = classify(args.dir)
    if args.json:
        try:
            with open(args.json, "w") as f:
                json.dump(result, f, indent=2, sort_keys=True)
        except OSError as exc:
            print("WARN: cannot write %s: %s" % (args.json, exc), file=sys.stderr)
    if not args.quiet:
        sys.stdout.write(render(result))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

