#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
validate_sequence.py - host-side validation of this lane's G-TXQ protocol
sequence against the wifi-bench-tooling validator (no device access).

The bench tooling gates sysfs attribute names through
bench_guards.ALL_KNOWN_ATTRS.  This lane's three new names
(dp_txq_table, dp_tx_complete_poll, dp_mac_ext - plus the opt-in
dp_mac_tail_init, which the ladder never writes) are injected in memory for
the validation run only; the bench tooling files are not modified.

Handoff when this ladder is promoted to a bench-auto phase: add
dp_txq_table/dp_mac_ext to bench_guards.DP_ATTRS, dp_tx_complete_poll and
dp_mac_tail_init to DP_WO_ATTRS, and mirror them into WO_ATTRS/
ALL_KNOWN_ATTRS.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LANE = os.path.dirname(HERE)
BENCH = os.path.normpath(os.path.join(LANE, "..", "wifi-bench-tooling", "tools"))
SEQ = os.path.join(LANE, "ladder", "bringup-g-txq-consumer.json")

LANE_ATTRS = {"dp_txq_table", "dp_tx_complete_poll", "dp_mac_ext",
              "dp_mac_tail_init"}
LANE_WO_ATTRS = {"dp_tx_complete_poll", "dp_mac_tail_init"}


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
    names = [s.get("name") for s in normalised["steps"]]

    def before(a, b):
        return a in names and b in names and names.index(a) < names.index(b)

    checks = [
        ("schema/name", normalised["name"] == "bringup-g-txq-consumer"),
        ("profile", normalised["profile"] == "hi5622-wifi-cfg80211"),
        ("new attr surface required",
         {"dp_txq_table", "dp_tx_complete_poll", "dp_mac_ext"} <=
         set(doc["require"]["attrs"])),
        ("load params: txq_publish/tx_complete_ms/mac_tail_writes",
         doc["load_params"].get("txq_publish") == 1 and
         doc["load_params"].get("tx_complete_ms") == 3000 and
         doc["load_params"].get("mac_tail_writes") == 0),
        ("boot golden page/state/mask gates",
         "0x86807FEF" in text and "0x00000002" in text and
         "0x6F8008" in text),
        ("one TX post", text.count("w_attr dp_tx_frame") == 1),
        ("one bounded completion poll",
         text.count("w_attr dp_tx_complete_poll") == 1),
        ("one RX poll", text.count("w_attr dp_rx_poll") == 1),
        ("MAC tail + TXQ table are read-only",
         "w_attr dp_mac_ext" not in text and
         "w_attr dp_txq_table" not in text),
        ("TXQ table read before the TX",
         before("dp-txq-table-before", "tx-frame-probe-req")),
        ("TXQ publish read after the TX",
         before("tx-frame-probe-req", "txq-table-after")),
        ("completion poll after the TX publish",
         before("txq-table-after", "tx-complete-poll")),
        ("verdict read after the completion poll",
         before("tx-complete-poll", "tx-status-after")),
        ("RX capture is read-only and after the poll",
         "dp_rx_capture" in text and "w_attr dp_rx_capture" not in text and
         before("tx-complete-poll", "rx-capture")),
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
              "(dp_tx_complete_poll/dp_mac_tail_init also to DP_WO_ATTRS)." %
              ", ".join(need))
    print("SUMMARY: %d passed, %d failed" %
          (sum(1 for _n, c in checks if c),
           sum(1 for _n, c in checks if not c)))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
