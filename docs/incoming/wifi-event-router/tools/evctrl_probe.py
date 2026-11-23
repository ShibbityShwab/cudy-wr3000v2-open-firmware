#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""evctrl_probe.py - the next bench action for the event-router lane, as a plan
(prints addresses/values only; it never opens or talks to a device).

Phase 1 is a read-only 24-word fingerprint.  Phase 2 is ONE conditional 4-byte
write that replays the firmware's own event-raise primitive for event 0x4C
(function 0xC6DB8 writes CA 0x40161180+i*4 and CA 0x40161280+i*4).  Phase 3 is a
firmware-RAM diagnostic that requires a separate Lead decision.

Address mapping used (project-wide):
    device IO CA        -> BAR0 = 0x3B8000 + (CA - 0x40000000)
    firmware-internal R -> BAR0 = 0x6B8000 + R          (image, RAM, ROM window)

The expected values are derived in REPORT.md sections 2/3/7.
"""
import sys

IO = lambda ca: 0x3B8000 + (ca - 0x40000000)
INT = lambda cpu: 0x6B8000 + cpu

READS = [
    # (label, address-kind, address, expected/meaning)
    ("event controller global enable", "io", 0x40160100, "0x00000001 (else entry A never ran 0xC3024)"),
    ("priority threshold (low byte)", "io", 0x40160104, "0x000000FF"),
    ("controller config word", "io", 0x40160108, "0x00000003"),
    ("core/controller presence word", "io", 0x40161000, "0x00000001 (entry A reached 0x46ED4)"),
    ("route/config word", "io", 0x40161C00, "0x00000000 (entry A reached 0x46E98)"),
    ("event status / current vector", "io", 0x4016010C, "0 no IRQ being taken; nonzero = id in low10"),
    ("event ack / last EOI word (READ ONLY)", "io", 0x40160110, "0 = dispatcher never ran; else last acked word"),
    ("enable word0 ids 0x10-0x1F", "io", 0x40161100, "0x2000 if scheduler enabled timer 0x1D, else 0"),
    ("enable word1 ids 0x20-0x3F", "io", 0x40161104, "mask 0xC0006000: 0x2D/0x2E/0x3E/0x3F enabled"),
    ("enable word2 ids 0x40-0x5F", "io", 0x40161108, "mask 0x00005000: 0x4C/0x4E enabled"),
    ("pending bank A ids 0x40-0x5F", "io", 0x40161188, "0x5000 = 0x4C+0x4E latched, undelivered; 0 = consumed/never"),
    ("pending bank A word+1", "io", 0x4016118C, "0x00000000"),
    ("pending bank B ids 0x40-0x5F", "io", 0x40161288, "same as bank A (mirror written by 0xC6DB8)"),
    ("timer reload lo", "io", 0x40160600, "0x0061A800 if scheduler 0xC704C ran"),
    ("timer reload hi", "io", 0x40160604, "0x0061A800"),
    ("timer control", "io", 0x40160608, "0x00000007"),
    ("timer start/rearm", "io", 0x4016060C, "0x00000001"),
    ("priority word ids 0x1C-0x1F", "io", 0x4016141C, "byte1 (id 0x1D) = 0xE0 -> 0x0000E000"),
    ("priority word ids 0x2C-0x2F", "io", 0x4016142C, "bytes1/2 (0x2D/0x2E) = 0x50 -> 0x00505000"),
    ("priority word ids 0x3C-0x3F", "io", 0x4016143C, "bytes2/3 (0x3E/0x3F) = 0x50 -> 0x50500000"),
    ("priority word ids 0x4C-0x4F", "io", 0x4016144C, "bytes0/2 (0x4C/0x4E) = 0x50 -> 0x00500050"),
    ("route/table word ids 0x3C-0x3F", "io", 0x4016183C, "byte2 (0x3E) = 0x01, byte3 (0x3F) = 0x02"),
    ("inter-core last trigger value", "io", 0x40161F00, "0x00020000 was the entry-A wake value"),
    ("inter-core handshake status", "int", 0x1BF008, "0 = entry A still spinning at 0x473F0; 2 = released"),
    ("inter-core handshake magic", "int", 0x1BF00C, "0xABCD (written by entry A)"),
]

ROM_READS = [
    ("ROM vector 0 reset", 0x00), ("ROM vector 1 undef", 0x04),
    ("ROM vector 2 svc", 0x08), ("ROM vector 3 prefetch", 0x0C),
    ("ROM vector 4 abort", 0x10), ("ROM vector 5 reserved", 0x14),
    ("ROM vector 6 IRQ target", 0x18), ("ROM vector 7 FIQ target", 0x1C),
]

WRITE = [
    # (label, io CA, value) - exact equivalent of the firmware raise 0xC6DB8(0x4C)
    ("pending bank A, event 0x4C", 0x40161188, 0x00001000),
    ("pending bank B, event 0x4C", 0x40161288, 0x00001000),
]

FOLLOWUP = [
    ("event status", "io", 0x4016010C),
    ("event ack word (last EOI)", "io", 0x40160110),
    ("pending bank A", "io", 0x40161188),
    ("pending bank B", "io", 0x40161288),
    ("mailbox out[0] (H2D/D2H bitmap)", "io", 0x40039010),
    ("mailbox out[1] (D2H bitmap)", "io", 0x40039014),
    ("handler 0x4C slot", "int", 0x17D430 + 4 * 0x4C),
]

BAR0 = {"io": IO, "int": INT}


def main():
    print("=" * 78)
    print("event-router lane - next bench action (attended, recovery armed)")
    print("prints addresses only; this tool never opens a device")
    print("=" * 78)
    print("\nPHASE 1 - read-only fingerprint (24 + 9 words, NO WRITES)\n")
    print("%-36s %-10s %-10s %s" % ("what", "CA/CPU", "BAR0", "expected / meaning"))
    for label, kind, addr, exp in READS:
        print("%-36s 0x%08X 0x%08X %s" % (label, addr, BAR0[kind](addr), exp))
    print("\nROM window (identity/region-5 window at BAR0 = CPU + 0x6B8000):")
    for label, off in ROM_READS:
        print("%-36s 0x%08X 0x%08X %s" % (label, off, INT(off),
              "0xE59FF018 stub -> target word below" if off < 0x18 else ""))
    print("%-36s 0x%08X 0x%08X %s" % ("ROM handler-table pointer", 0x1E4, INT(0x1E4),
          "0x0004005C [V*] = image header+0x5C handler-pointer slot"))

    print("\nDECISION (phase 1):")
    print("  pending bank A/B == 0x5000 and status == 0 and enables present -> events are")
    print("    latched but never delivered; if CPU 0x1BF008 != 2 and timer words == 0, entry A")
    print("    is still spinning at 0x473F0, the scheduler 0xC704C never ran and no context")
    print("    ever executes cpsie i -> the delivery gap is firmware/CPU/ROM-internal.")
    print("  pending == 0 -> raise path also dead: stop, re-read phase-1 rows 1-5 and the")
    print("    handler table before any write.")
    print("  CPU 0x1BF008 == 2 and timer 0x4016060C == 1 -> scheduler did run: blocker is")
    print("    purely CPU/ROM IRQ delivery; use phase 2 and the ROM vector words.")

    print("\nPHASE 2 - ONE conditional write (only if phase 1 rows 1-3 are healthy):")
    print("  replay the firmware raise 0xC6DB8(0x4C) exactly:")
    for label, ca, val in WRITE:
        print("    write 0x%08X to CA 0x%08X  (BAR0 0x%08X)   %s" % (val, ca, IO(ca), label))
    print("  then poll for <= 500 ms (plus a few seconds):\n")
    print("%-36s %-10s %-10s" % ("what", "CA/CPU", "BAR0"))
    for label, kind, addr in FOLLOWUP:
        print("%-36s 0x%08X 0x%08X" % (label, addr, BAR0[kind](addr)))
    print("\nEXPECTED OBSERVABLES:")
    print("  SUCCESS (delivery works): handler 0x40295 -> mailbox dispatcher 0xC18AC runs;")
    print("    CA 0x40160110 gets a status word written; pending bits clear; out[0]/out[1]")
    print("    show the dispatcher's reset pattern; then re-run the existing id-1 wake")
    print("    probe (out[0] |= 2, out[2] |= 1) and expect out[0] to clear and out[2]=8.")
    print("  NULL (delivery dead): pending bit stays set, 0x4016010C/0x40160110 stay 0 ->")
    print("    definitive: CPU/ROM IRQ delivery is the missing link (not event sources).")

    print("\nPHASE 3 - NOT part of this action (Lead decision only):")
    print("  if CPU 0x1BF008 reads 0 (entry A spinning), a firmware-RAM write of 2 to")
    print("  CPU 0x1BF008 (BAR0 0x%08X) fakes the inter-core handshake reply; risky and" % INT(0x1BF008))
    print("  separate.  Cleaner: decode the ROM IRQ entry / take a vendor golden trace.")

    print("\nDO NOT WRITE: CA 0x40160110 (EOI), any 0x17D4xx handler slot, CA 0x400392F0,")
    print("out[0]/out[2] outside the existing wake probe, firmware RAM objects (phase 3")
    print("excepted), CA 0x400392F0.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
