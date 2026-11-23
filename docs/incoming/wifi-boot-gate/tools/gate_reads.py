#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""gate_reads.py - READ-ONLY paused-chip read plan for the boot-task 0x48820 gate
(wifi-boot-gate lane).

Prints the exact host BAR0 reads, the expected values for "gate open" vs "gate
closed", and the three-way decision.  It NEVER opens a device and NEVER issues a
write; it only prints a plan.  The single next bench action is to execute this
plan once on a parked chip (after the standard prepare/upload/verify/release),
then classify the result with the decision matrix below.

Address conventions (same as every lane of this project):
    CPU address = file offset + 0x40000
    host BAR0 (CPU RAM object, region 5) = CPU + 0x6B8000
    host BAR0 (device IO CA, region 3)   = 0x3B8000 + (CA - 0x40000000)

Gate model (static, from tools/bootgate.py):
    gate_A: 0x48820  bl 0x9438C(byte[CA 0x170E29]) ; passes iff the argument is 0
             (0x9438C returns CA 0x169E90 for arg 0, else 0).  Structural on-path.
    gate_B: 0x4882E  bl 0x961AE(byte[CA 0x170E28]=0x12)
             = 0xFE578(0,0x12) = word[CA 0x1714A0] + u16[CA 0x171504]
             passes iff that sum != 0.  Written only by the boot task's own
             post-send init chain at 0x4923A / 0x4924C.
"""
import argparse
import sys

RAM_BAR0 = 0x6B8000

# (row, name, CA, width, expected-open, expected-closed, decision)
READS = [
    (1, "gate_B word 1", 0x1714A0, 4, 0x001261D8, 0x00000000,
     "did the boot task run the 0x49204 post-send init chain?"),
    (2, "gate_B word 2 (u16)", 0x171504, 2, 0x0000, 0x0000,
     "count/offset term; nonzero = partial/corrupt init"),
    (3, "arena pointer-array entry 0", 0x171458, 4, 0x00119758, 0x00000000,
     "did the 19-entry array loop start at all"),
    (4, "gate_A argument byte", 0x170E29, 1, 0x00, 0x00,
     "0x9438C argument (structurally 0); nonzero = path corruption"),
    (5, "singleton word 0", 0x169E90, 4, 0x00170E28, 0x00000000,
     "did the per-record body reach 0x486A8"),
    (6, "bind source word (CA 0x169E98)", 0x169E98, 4, 0x00000000, 0x00000000,
     "always 0: proves the 0x48840 bind stores NULL"),
    (7, "system-object stage pair lo", 0x17D420, 4, 0x0017D420, 0x00000000,
     "entry A system-object init ran (0 would have BUGed at 0x491FE)"),
    (8, "system-object stage pair hi", 0x17D424, 4, 0x0017D420, 0x00000000,
     "same; [0x17D424] != 0 is the 0x48292 selector"),
    (9, "module-18 object + 4", 0x1261DC, 4, 0x00000002, None,
     "0xFE6BC ran (bind/present path reached)"),
    (10, "module-18 object + 0x4B0", 0x126688, 4, 0x00000000, None,
     "bind executed; expected NULL by row 6"),
    (11, "module-18 object + 0x4B8", 0x126690, 4, 0x0015CA08, None,
     "per-link object bank 0 attached"),
    (12, "per-link publish word (0x15CA08+0x1A00)", 0x15E408, 4,
     0x00174BF0, 0x00000000, "the real per-link context exists (bank0; bank1 = 0x00175A18)"),
    (13, "handshake state byte", 0x118B60, 1, 0x01, None,
     "boot task entered the setup branch at least once (measured = 1)"),
    (14, "handler table id 0x02", 0x17D438, 4, 0x000C20F9, None,
     "entry A registration block ran (prior read [V*])"),
]

DECISION = """\
Decision matrix (evaluate rows 1, 7, 11, 10, 6):
  (O) row 1 != 0 (gate open) and row 10 == 0 and row 11 == 0:
      boot task ran the post-send chain and the bind; +0x4B0 is NULL because
      CA 0x169E98 is never written.  Remaining blocker = event/link layer
      (0x44BBC / 0xB1EE4), NOT boot-task scheduling.
  (C) row 1 == 0 (gate closed):
      boot task never executed 0x49204.  Row 7 == 0x0017D420 means entry A is
      fine, so the stop is C2 (message alloc/send) or C5 (0xD1B68) - the
      D2H/message-layer blocker (wifi-abort-rca section 1).
  (P) row 1 != 0 and row 11 != 0 and row 10 == 0:
      the per-link bank exists but the 0x44BBC bind never ran; remaining
      blocker = the H2D/link-event handler owning 0x437B0-0x44BBC
      (group-2 id-9, wifi-hcc-link).
DO NOT WRITE any of these words: they are firmware RAM state and fabricating
them is the abort-rca section 10.4 stand (no targeted writes)."""


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--no-formula", action="store_true", help="omit the gate formula banner")
    p.add_argument("--width", choices=("b", "h", "l"), default="l",
                   help="access width to use on the bench (default l = 32-bit aligned)")
    a = p.parse_args()
    if not a.no_formula:
        print("boot-gate read-only paused-chip plan (wifi-boot-gate lane)")
        print("gate_B_open = (word(BAR0 0x8294A0) + u16(BAR0 0x829504)) != 0")
        print("gate_A_arg  = byte(BAR0 0x828E29) == 0  (structural on-path)")
        print()
        print("%-3s %-38s %-9s %-9s %-12s %-12s %s" %
              ("#", "read", "CA", "BAR0", "open", "closed", "decides"))
        print("-" * 132)
    for row, name, ca, width, eo, ec, why in READS:
        bar0 = ca + RAM_BAR0
        eo_s = "-" if eo is None else ("0x%08X" % eo if width > 1 else "0x%02X" % eo)
        ec_s = "-" if ec is None else ("0x%08X" % ec if width > 1 else "0x%02X" % ec)
        print("%-3d %-38s 0x%06X  0x%06X  %-12s %-12s %s" %
              (row, name, ca, bar0, eo_s, ec_s, why))
    if not a.no_formula:
        print()
        print(DECISION)
    return 0


if __name__ == "__main__":
    sys.exit(main())
