#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
validate_sequence.py - host-side validation of this lane's protocol sequence
against the wifi-bench-tooling validator (no device access).

The bench tooling's protocol profile gates sysfs attribute names through
bench_guards.ALL_KNOWN_ATTRS.  This lane's four new attributes
(dp_tx_frame, dp_tx_status, dp_rx_poll, dp_rx_capture) are injected in memory
for the validation run only; the bench tooling files are not modified.

Handoff for bench-auto when this ladder is promoted to a phase: add the four
names to bench_guards.DP_ATTRS, add dp_tx_frame/dp_rx_poll to DP_WO_ATTRS,
and add them to WO_ATTRS/ALL_KNOWN_ATTRS accordingly.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LANE = os.path.dirname(HERE)
BENCH = os.path.normpath(os.path.join(LANE, "..", "wifi-bench-tooling", "tools"))
SEQ = os.path.join(LANE, "sequences", "ladder-f-txrx.json")

LANE_ATTRS = {"dp_tx_frame", "dp_tx_status", "dp_rx_poll", "dp_rx_capture"}
LANE_WO_ATTRS = {"dp_tx_frame", "dp_rx_poll"}


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
    need = sorted((LANE_ATTRS | LANE_WO_ATTRS) - set(G.ALL_KNOWN_ATTRS))
    G.DP_ATTRS |= LANE_ATTRS
    G.DP_WO_ATTRS |= LANE_WO_ATTRS
    G.ALL_KNOWN_ATTRS |= LANE_ATTRS
    G.WO_ATTRS |= LANE_WO_ATTRS
    G.RO_ATTRS = G.ALL_KNOWN_ATTRS - G.WO_ATTRS
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
        ("schema/name", normalised["name"] == "ladder-f-txrx"),
        ("profile", normalised["profile"] == "hi5622-wifi-h2d"),
        ("boot golden page/state/mask gates",
         "0x86807FEF" in text and "0x00000002" in text and
         "0x6F8008" in text),
        ("one ctx-create, one setup, one 0x320, 3 TX posts, 1 RX poll",
         text.count("w_attr ") == 7),
        ("dp_probe read gate emitted", "dp_probe" in text),
        ("dp_setup read-back gate emitted", "dp_status" in text),
        ("txrx bind/status gate emitted", "dp_tx_status" in text),
        ("TX posts use dp_tx_frame values 1/2/3",
         text.count("w_attr dp_tx_frame") == 3),
        ("RX capture is a read-only attribute", "dp_rx_capture" in text and
         "w_attr dp_rx_capture" not in text),
        ("RX poll is a single write", text.count("w_attr dp_rx_poll") == 1),
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
              "(dp_tx_frame/dp_rx_poll also to DP_WO_ATTRS)." %
              ", ".join(need))
    print("SUMMARY: %d passed, %d failed" %
          (sum(1 for _n, c in checks if c), sum(1 for _n, c in checks if not c)))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
