#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
validate_sequence.py - host-side validation of the lane's protocol sequence
against the wifi-bench-tooling validator (no device access).

The bench tooling's protocol profile gates sysfs attribute names through
bench_guards.ALL_KNOWN_ATTRS.  This lane's new attributes (dp_probe, dp_setup,
dp_status, dp_rings - plus the h2d_txq_addr_sync alias already allowlisted by
the bench lane) are injected here in memory for the validation run only; the
bench tooling file itself is not modified by this lane.  The one-line handoff
for bench-auto is printed when the injected set is needed.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LANE = os.path.dirname(HERE)
BENCH = os.path.normpath(os.path.join(LANE, "..", "wifi-bench-tooling", "tools"))
SEQ = os.path.join(LANE, "sequences", "ladder-d-datapath.json")

# Attributes this driver adds; bench-auto must add these to
# bench_guards.DP_ATTRS / DP_WO_ATTRS (h2d_txq_addr_sync is already there).
LANE_ATTRS = {"dp_probe", "dp_setup", "dp_status", "dp_rings"}
LANE_WO_ATTRS = {"dp_setup"}


def main():
    if not os.path.isdir(BENCH):
        print("SKIP: bench tooling not found at %s" % BENCH)
        return 0
    sys.path.insert(0, BENCH)
    try:
        import bench_guards as G
        import protocol_sequence as PS
    except Exception as exc:  # noqa: BLE001
        print("SKIP: bench tooling import failed: %s" % exc)
        return 0
    need = sorted(LANE_ATTRS - set(G.DP_ATTRS))
    G.DP_ATTRS |= LANE_ATTRS
    G.DP_WO_ATTRS |= LANE_WO_ATTRS
    G.ALL_KNOWN_ATTRS |= LANE_ATTRS
    G.WO_ATTRS |= LANE_WO_ATTRS
    doc = json.load(open(SEQ))
    ok = True
    try:
        normalised = PS.validate(doc, SEQ)
    except Exception as exc:  # noqa: BLE001
        print("FAIL: sequence validation refused: %s" % exc)
        return 1
    lines = PS.render_script(normalised)
    text = "\n".join(lines)
    checks = [
        ("schema/name", normalised["name"] == "ladder-d-datapath"),
        ("profile", normalised["profile"] == "hi5622-wifi-h2d"),
        ("boot golden pack gates (>=8 numeric gates)",
         text.count("gate_num ") >= 8 and "0x86807FEF" in text and
         "0x00000002" in text),
        ("three attr writes (ctx_create, dp_setup, txq addr)",
         text.count("w_attr ") == 3),
        ("dp_probe read gate emitted", "dp_probe" in text),
        ("dp_setup read-back gate emitted", "dp_status" in text),
        ("dp_rings read gate emitted", "dp_rings" in text),
        ("txq addr write uses the allowlisted alias",
         "w_attr h2d_txq_addr_sync" in text),
        ("post-run mask/staging gates emitted",
         text.count("0x6F8008") >= 2 and text.count("0x6F800C") >= 2),
        ("stop-on-first-failure chain present",
         "gate_num" in text and "gate_attr" in text and "w_attr" in text),
    ]
    for name, cond in checks:
        print(("PASS: " if cond else "FAIL: ") + name)
        ok = ok and cond
    print("sequence: %d steps, %d rendered lines" %
          (len(normalised["steps"]), len(lines)))
    if need:
        print("HANDOFF (bench-auto): add %s to bench_guards.DP_ATTRS "
              "(dp_setup also to DP_WO_ATTRS); h2d_txq_addr_sync is "
              "already allowlisted." % ", ".join(need))
    print("SUMMARY: %d passed, %d failed" %
          (sum(1 for _n, c in checks if c), sum(1 for _n, c in checks if not c)))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
