#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""bench-reads.py - the wifi-ready-emit READ-ONLY bench snapshot pack.

Prints (and nothing else) the exact read set that decides where the ready-emission
chain stops, with the host BAR0 mapping for each read.  It NEVER opens a device and
NEVER writes: the Lead runs the reads on the bench through the existing
observability path (sysfs/physdump) and compares the values with the decision
column here.

Mappings (project conventions, re-verified by readyemit.py selftest):
  CPU/RAM object R (region-5 window) : BAR0 = 0x6B8000 + R
  device IO CA (region-3)            : BAR0 = 0x3B8000 + (CA - 0x40000000)
  PKTRAM CA (region-2)               : BAR0 = 0x1D8000 + (CA - 0x01000000)

Usage:  bench-reads.py [--markdown]
"""
import argparse

RAM = 0          # region-5
IO = 1           # region-3
PKT = 2          # region-2


def bar(space, addr):
    if space == RAM:
        return 0x6B8000 + addr
    if space == IO:
        return 0x3B8000 + (addr - 0x40000000)
    return 0x1D8000 + (addr - 0x01000000)


# (group, label, space, addr, width, expected/decision)
ROWS = [
    # --- 0. aperture sanity (stop if these fail) --------------------------
    ("0-map", "image header w0 (CPU 0x40000)", RAM, 0x40000, 4,
     "0x00046971"),
    ("0-map", "image header w1", RAM, 0x40004, 4, "0x000C742D"),
    ("0-map", "ready routine 0xC6E14 prologue", RAM, 0xC6E14, 4, "0x43F8E92D"),
    # --- 1. boot/message-layer anchors (already known good) ---------------
    ("1-anchors", "g pointer *CA 0x172130", RAM, 0x172130, 4,
     "0x0010C0F4"),
    ("1-anchors", "out[1] pointer (g+0x9C)", RAM, 0x10C0F4 + 0x9C, 4,
     "0x40039014"),
    ("1-anchors", "out[0] pointer (g+0xA0)", RAM, 0x10C0F4 + 0xA0, 4,
     "0x40039010"),
    ("1-anchors", "D2H doorbell ptr (g+0xA4)", RAM, 0x10C0F4 + 0xA4, 4,
     "0x40101434"),
    ("1-anchors", "D2H in-flight latch (g+0xB4)", RAM, 0x10C0F4 + 0xB4, 4,
     "0 steady; == out[1] word while a D2H word is printed"),
    ("1-anchors", "D2H deferred shadow  (g+0xB8)", RAM, 0x10C0F4 + 0xB8, 4,
     "0 steady; non-zero = a notify is DEFERRED (flush pending)"),
    ("1-anchors", "flush 1-shot ptr    (g+0xB0)", RAM, 0x10C0F4 + 0xB0, 4,
     "read only (pointer the flush routine writes 1 through)"),
    # --- 2. firmware hcc/ETE instance ------------------------------------
    ("2-hcc", "hcc_instance+0x0C = ETE intr/status base", RAM, 0x10F1F0 + 0x0C, 4,
     "0x4003A86C"),
    ("2-hcc", "hcc_instance+0x14 = DR record base", RAM, 0x10F1F0 + 0x14, 4,
     "0x0010F2B8"),
    ("2-hcc", "hcc_instance+0x18 = ETE ops table", RAM, 0x10F1F0 + 0x18, 4,
     "0x0010CEDC"),
    ("2-hcc", "hcc_instance+0x1C = D2H sending trigger", RAM, 0x10F1F0 + 0x1C, 4,
     "0x000C625B"),
    ("2-hcc", "hcc_instance+0x20/+0x24/+0x28 callbacks", RAM, 0x10F1F0 + 0x20, 4,
     "0x000C6803 / 0x000C1903 / 0x000C18FF"),
    ("2-hcc", "hcc_instance+0x58 = chip id", RAM, 0x10F1F0 + 0x58, 4, "0"),
    ("2-hcc", "hcc_instance+0x5C = g object", RAM, 0x10F1F0 + 0x5C, 4,
     "0x0010C0F4"),
    # --- 3. per-DR-channel firmware records ------------------------------
    # R_i = 0x10F2B8 + 0x354*i  (fields: +0x10 send index, +0x14 node array,
    # +0x344 trigger count, +0x34C released count)
    ("3-DRrec", "DR0 R+0x10 progress index", RAM, 0x10F2B8 + 0x10, 4,
     "0..0x420; moves when the sending trigger pumps"),
    ("3-DRrec", "DR0 R+0x14 node array (device source ring)", RAM, 0x10F2B8 + 0x14, 4,
     "0x01060540"),
    ("3-DRrec", "DR0 R+0x344 trigger count", RAM, 0x10F2B8 + 0x344, 4,
     "0 = sending trigger NEVER ran (decisive)"),
    ("3-DRrec", "DR0 R+0x34C released count", RAM, 0x10F2B8 + 0x34C, 4,
     "counts nodes the trigger released"),
    ("3-DRrec", "DR1 R+0x344 trigger count", RAM, 0x10F60C + 0x344, 4,
     "same decision, channel 1"),
    ("3-DRrec", "DR2 R+0x344 trigger count", RAM, 0x10F960 + 0x344, 4,
     "same decision, channel 2"),
    ("3-DRrec", "DR3 R+0x344 trigger count", RAM, 0x10FCB4 + 0x344, 4,
     "same decision, channel 3"),
    # --- 4. ETE device registers (region-3) ------------------------------
    ("4-ETE", "ETE status word 0x4003A874", IO, 0x4003A874, 4,
     "device D2H status; bits 16-20 -> sending trigger; 0 = no D2H event"),
    ("4-ETE", "ETE send reg DR0 0x4003AC00", IO, 0x4003AC00, 4,
     "high16 = target send index; low16 = firmware progress index"),
    ("4-ETE", "ETE send reg DR1 0x4003AC04", IO, 0x4003AC04, 4, "same, ch1"),
    ("4-ETE", "ETE send reg DR2 0x4003AC08", IO, 0x4003AC08, 4, "same, ch2"),
    ("4-ETE", "ETE send reg DR3 0x4003AC0C", IO, 0x4003AC0C, 4, "same, ch3"),
    ("4-ETE", "ETE intr word 0x40039508", IO, 0x40039508, 4,
     "host-facing intr mask; 0x3F201818 seen, vendor clears with 0xFFE0F8F8"),
    ("4-ETE", "DR0 fill +0x3C", IO, 0x4003A590 + 0x3C, 4,
     "device fill; advances only on a real source->host copy"),
    ("4-ETE", "DR0 group A +0x10 source base", IO, 0x4003A590 + 0x10, 4,
     "firmware device source ring base (0x01060440)"),
    ("4-ETE", "DR0 group A +0x14/+0x18 (depth/producer)", IO, 0x4003A590 + 0x14, 4,
     "depth-1=0x1F; producer advances when nodes are staged"),
    ("4-ETE", "DR0 group B +0x30/+0x34/+0x38", IO, 0x4003A590 + 0x30, 4,
     "host destination ring: base 0x8xxxxxxx, depth-1 0x1F, pub 0x400 (B3)"),
    ("4-ETE", "DR1..DR3 fill/group A/B", IO, 0x4003A5E0 + 0x3C, 4,
     "same reads at +0x50 per channel: 0x4003A5E0/630/680"),
    # --- 5. mailbox state -------------------------------------------------
    ("5-mbox", "out[0] H2D pending", IO, 0x40039010, 4,
     "0x8 stuck = firmware H2D dispatcher never ran"),
    ("5-mbox", "out[1] D2H bitmap", IO, 0x40039014, 4,
     "record word; bit6 0x40 seen ~0.4 s, bit2 0x04 seen ~1.3 s"),
    ("5-mbox", "out[5] H2D ack (device-only)", IO, 0x400392F0, 4,
     "READ ONLY; 1 = H2D dispatcher acked a frame"),
    ("5-mbox", "D2H ack 0x40101438", IO, 0x40101438, 4, "host-written, read back"),
    ("5-mbox", "D2H re-arm 0x40101414", IO, 0x40101414, 4, "host-written, read back"),
    # --- 6. device source-ring content (PKTRAM; may need physdump) -------
    ("6-node", "DR0 source node[0].word1 (PKTRAM)", PKT, 0x01060544, 4,
     "0 = no node staged; 0x0c006d2b/0x10006d2b = a 12/16-B node WAS staged"),
    ("6-node", "DR0 source staging word0 (PKTRAM)", PKT, 0x01060440, 4,
     "device ring base word (readable only if region-2 window is mapped)"),
]

DECISION = """
Decision matrix (apply after the first out[1] bit6 has been drained):
  S0  any 0-map row wrong ....................... aperture wrong; fix reads first
  S1  all R+0x344 == 0 AND ETE 0x4003A874 bits16-20 == 0
      -> the firmware D2H 'sending trigger' NEVER ran: the device ETE
         status/interrupt dispatch layer is idle. Host cannot create it
         (no host-writable input in that path). Escalate to the off-image
         ROM/init-table stage; no further host register writes are justified.
  S2  some R+0x344 != 0 but source node tag absent and R+10 == 0
      -> trigger ran, nothing pending/eligible: the break is upstream in the
         firmware HCC TX queue path (compare g+0xB4/g+0xB8 and the out[1]
         history). Host-visible trigger: none found.
  S3  source node tag present (0x...6d2b) or source producer advanced, but
      host DR fill (+0x3C) static
      -> nodes ARE staged in the device source ring; the ETE source->host
         copy did not run. Only here is the host-owned destination side the
         suspect: verify B3 group B base/depth/pub and channel enable; the one
         benign host write class left to try is a single re-publish of DR+0x38
         with its current value (attended, new session), then re-read this pack.
"""

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--markdown", action="store_true")
    args = ap.parse_args()
    if args.markdown:
        print("| # | read | CPU/CA | BAR0 | width | expected / decision |")
        print("| --- | --- | --- | --- | --- | --- |")
        for i, (g, label, sp, addr, w, exp) in enumerate(ROWS):
            print("| %d | %s | 0x%08X | 0x%06X | %d B | %s |"
                  % (i, label, addr, bar(sp, addr), w, exp))
        print()
        print(DECISION)
        return
    cur = None
    for g, label, sp, addr, w, exp in ROWS:
        if g != cur:
            print("\n== %s ==" % g)
            cur = g
        print("  %-46s CA/CPU=0x%08X  BAR0=0x%06X  %dB  %s"
              % (label, addr, bar(sp, addr), w, exp))
    print(DECISION)


if __name__ == "__main__":
    main()
