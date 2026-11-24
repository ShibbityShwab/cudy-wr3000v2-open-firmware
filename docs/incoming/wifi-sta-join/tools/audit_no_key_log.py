#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""audit_no_key_log.py - static safety audit for the wifi-sta-join lane.

Asserts, over the lane's sources/patches only (no device, no vendor text):

  S1 no logging helper that could dump key material or raw frame bytes
     (print_hex_dump, hex_dump_to_buffer, %*ph, %*pE)
  S2 no cfg80211 request key POINTER ever reaches a dev_* log call
     (sme->key must only be validated/copied, never formatted)
  S3 no join-buffer/frame dump in a log call
  S4 dry run (sta_join=1) never copies key material (string + code check)
  S5 the fail-closed gates are present: one-shot latch, allow_frames>0,
     health stop (mask/staging), no-retry wording
  S6 no vendor binary/object/firmware artifact in the lane directory

Exit 0 = PASS.  Prints only derived facts (file/line/regex), never a secret.
"""
import os
import re
import sys

LANE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(LANE, "src")
PATCH = os.path.join(LANE, "patches")

checks = 0
fails = 0


def ck(cond, msg):
    global checks, fails
    checks += 1
    if not cond:
        fails += 1
        print("FAIL: " + msg)


def read_dir(d):
    out = []
    for name in sorted(os.listdir(d)):
        p = os.path.join(d, name)
        if os.path.isfile(p):
            with open(p, "r", errors="replace") as fh:
                out.append((name, fh.read()))
    return out


def main():
    files = read_dir(SRC) + read_dir(PATCH)
    ck(bool(files), "no source/patch files found")
    blob = "\n".join("\n".join((name, text)) for name, text in files)

    # S1: no hex/byte dump helpers
    for pat in (r"print_hex_dump", r"hex_dump_to_buffer", r"%\*ph",
                r"%\*pE", r"%\*pV"):
        ck(re.search(pat, blob) is None, "forbidden dump helper/regex: " + pat)

    # S2: the params key pointer must never be a log argument
    #     (sme->key, then end-of-word; sme->key_len is allowed)
    bad = re.findall(r"dev_(?:info|warn|err|dbg|notice)[^;\n]*"
                     r"sme->key\b", blob)
    ck(not bad, "sme->key pointer reaches a dev_* log call (%d)" % len(bad))
    # and no local key pointer/array dumped
    bad = re.findall(r"dev_(?:info|warn|err|dbg|notice)[^;\n]*"
                     r"(?:join_buf|sme->key)", blob)
    ck(not bad, "join buffer/key reaches a log call (%d)" % len(bad))

    # S3: no frame/body dump in logs: a dev_* call may print lengths, not
    #     a raw frame/body buffer address or content
    bad = re.findall(r"dev_(?:info|warn|err|dbg)[^;\n]*%(?:\*ph|s)", blob)
    ck(not bad, "log call formats a raw string/buffer (%d)" % len(bad))

    # S4: dry run must not copy key material
    sta_c = dict(files).get("hi5622_sta.c", "")
    ck("with_key" in sta_c, "no with_key parameter in the join buffer builder")
    ck(re.search(r"if \(with_key && sme->key_len\)", sta_c) is not None,
       "key copy is not gated by with_key")
    ck("key NOT copied" in sta_c, "dry-run log does not state key not copied")
    ck("sta_join=1" in sta_c, "dry-run module-parameter value missing")

    # S5: fail-closed gates present
    for needle, why in [
        ("join_attempted", "one-join-per-load latch"),
        ("HI5622_STA_JOIN_POST", "single-shot post gate"),
        ("allow_frames", "allow_frames>0 gate"),
        ("hi5622_sta_health", "mask/staging health gate"),
        ("mask CA 0x400008", "mask stop rule documented"),
        ("staging CA 0x40000c", "staging stop rule documented"),
        ("NO RETRY", "no-retry wording"),
        ("EALREADY", "second-join refusal"),
    ]:
        ck(needle in blob, "missing gate evidence: " + why + " (" + needle + ")")

    # S6: no vendor binary/object/firmware artifacts in the lane tree
    # (two aggregate checks so the count does not depend on doc file count)
    bad_suffix = []
    bad_elf = []
    for root, _dirs, names in os.walk(LANE):
        for n in names:
            p = os.path.join(root, n)
            if n.endswith((".ko", ".o", ".a", ".bin", ".elf", ".img")):
                bad_suffix.append(os.path.relpath(p, LANE))
            try:
                with open(p, "rb") as fh:
                    head = fh.read(4)
                if head == b"\x7fELF":
                    bad_elf.append(os.path.relpath(p, LANE))
            except OSError:
                pass
    ck(not bad_suffix, "vendor-like binary artifact(s) in lane: %s"
       % (", ".join(bad_suffix) or "-"))
    ck(not bad_elf, "ELF object(s) in lane: %s"
       % (", ".join(bad_elf) or "-"))

    print("checks=%d fails=%d" % (checks, fails))
    if fails:
        print("STA-JOIN-AUDIT-FAIL")
        return 1
    print("STA-JOIN-AUDIT-PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
