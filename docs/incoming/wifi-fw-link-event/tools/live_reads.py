#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""live_reads.py - READ-ONLY bench read plan for the firmware event/link layer
(wifi-fw-link-event lane).  Host-only; this tool prints addresses and expected
values, it never talks to a device and never writes anything.

Address map (established by the preceding lanes, re-derived in REPORT.md):
    CPU address  = file offset + 0x40000
    BAR0 (CPU RAM, region 5)  = CPU + 0x6B8000
    BAR0 (device IO, region 3)= 0x3B8000 + (CA - 0x40000000)
    BAR0 (ROM/WRAM identity window, region 0) = CPU   (for CPU < 0x40000)

The plan answers ONE question: is the firmware's event dispatch layer wired
and is anything pending - i.e. is the missing input an undispatched hardware
event (ROM/IRQ vector side) or a never-asserted event source (device event
router side)?  Every step is a read.  Writes are enumerated separately as
DO-NOT-WRITE.

Usage:
    live_reads.py            # full plan
    live_reads.py rom        # only the ROM vector/identity-window reads
    live_reads.py ram        # only the CPU-RAM object reads
    live_reads.py io         # only the device-IO reads
"""
import sys

BAR0_RAM = 0x6B8000
BAR0_IO = 0x3B8000


def bar0(ca):
    if ca >= 0x40000000:
        return BAR0_IO + (ca - 0x40000000)     # device-IO window (region 3)
    if ca >= 0x40000:
        return ca + BAR0_RAM                    # CPU-RAM / image window (region 5)
    return ca                                   # ROM identity window (region 0)


def line(ca, size, name, expect, note):
    print("  CA 0x%08X  BAR0 0x%08X  %-2s  %-34s expect: %-28s %s"
          % (ca, bar0(ca), size, name, expect, note))


def io_plan():
    print("\n== 1. DEVICE-IO window (region 3): the event controller and the mailbox ==")
    print("   The event dispatcher CPU 0xC2EFC reads CA 0x4016010C and writes the")
    print("   same word back to CA 0x40160110 (ack).  Low 10 bits of the status word")
    print("   are the handler-table index.  NEVER write any of these registers.")
    line(0x4016010C, "w", "event-status/IRQ word", "0 = no event; nonzero = pending", "dispatcher index = bits0..9")
    line(0x40160110, "w", "event-status ack/clear", "n/a - DO NOT WRITE", "dispatcher write-back target")
    line(0x40036800, "w", "event pump status (ids 0x3e/0x3f)", "bit map, 0 = idle", "consumed by 0x46848")
    line(0x40039010, "w", "out[0] host->device mailbox", "0 after firmware consumes", "ours stuck 0x8 = not consumed")
    line(0x40039014, "w", "out[1] device->host mailbox", "0 or announce bits", "0x4 = announce pending")
    line(0x400392D4, "w", "out[2] mailbox re-arm", "read-only checkpoint", "never write from host")
    line(0x400392F0, "w", "out[5] firmware ack", "read-only; NEVER write", "rom-flow rule")


def ram_plan():
    print("\n== 2. CPU-RAM window (region 5): system object, handler table, boot global ==")
    print("   System object CA 0x17D398 (entry A writes +0x00 and the handler table):")
    line(0x17D398, "w", "system object +0x00", "(core_id+1)<<5", "entry A ran [V*]")
    line(0x17D40C, "w", "system object +0x74", "dispatcher branch flag", "used by 0xC2EFC")
    print("   Handler table CA 0x17D430 (0x60 x 4-byte fn pointers, index = IRQ status):")
    for idx, h in ((0x02, "0xC20F9"), (0x2D, "0x462F9"), (0x2E, "0x4624D"),
                   (0x3E, "0x46849"), (0x3F, "0x46849"), (0x40, "0x40671"),
                   (0x42, "0x4065D"), (0x46, "0x40525"), (0x4C, "0x40295"),
                   (0x4E, "0x402A1")):
        line(0x17D430 + 4 * idx, "w", "handler[0x%02X] -> %s" % (idx, h), h, "registered, must be nonzero")
    print("   Boot global CA 0x117C00 (entry A writes the pump wiring, boot task +0xEEC):")
    line(0x118AE0, "w", "+0xEE0 pump status base", "0x00000000", "entry A 0x471CA")
    line(0x118AE4, "w", "+0xEE4 pump callback A", "0x000461B7", "entry A 0x471FC")
    line(0x118AE8, "w", "+0xEE8 pump callback B", "0x000467D9", "entry A 0x471F2")
    line(0x118AEC, "w", "+0xEEC event kick hook", "0x000C19D7 (BUG stub)", "boot task 0x47D02")
    line(0x118B60, "w", "+0xF60 boot state", "1", "boot task ran [V*]")
    line(0x172130, "w", "g message-object pointer", "0x0010C0F4", "pcie_msg_init ran [V*]")


def rom_plan():
    print("\n== 3. ROM identity window (region 0): how does the ROM vector IRQs? ==")
    print("   Prior lanes proved CPU 0x0 and CPU 0x50 decode through this window while")
    print("   the ROM body at 0x37160 reads zero.  The 8 ARM vector words are cheap to")
    print("   read and decide whether IRQ/FIQ are vectored into the image's own")
    print("   trampoline at CPU 0x40064 (which calls the event dispatcher 0xC2EFC).")
    for i, ca in ((0, 0x00), (1, 0x04), (2, 0x08), (3, 0x0C),
                  (4, 0x10), (5, 0x14), (6, 0x18), (7, 0x1C)):
        line(ca, "w", "ROM vector[%d]" % i, "code/ldr-pc stub", "IRQ=+0x18 FIQ=+0x1C")
    line(0x1E4, "w", "ROM trampoline table ptr", "target of [0x0] stub", "rom-flow open item 11.2")
    print("   Decision: if vector[6]/[7] contain a branch/ldr-pc into the image range")
    print("   0x40064..0x400C4, IRQ delivery is wired to the image event trampoline")
    print("   and the gap is a never-asserted event source.  If they contain only ROM")
    print("   addresses below 0x40000, the IRQ path stays ROM-internal (not readable)")


def decisions():
    print("\n== 4. Decision matrix (all reads; take them on ONE fresh cold boot) ==")
    print("  A. handler[0x3E/0x3F] == 0x46849 AND handler[0x4C] == 0x40295")
    print("     -> all registrations ran; the event layer is wired but idle.")
    print("     Then read 0x4016010C: nonzero while out[0] is still pending =>")
    print("     events exist but are NOT dispatched => IRQ delivery (ROM vector) missing.")
    print("     Definitive firmware/ROM-internal conclusion; needs ROM/vendor-trace.")
    print("  B. handler[...] set, 0x4016010C == 0 while out[0] pending and ETE rings armed")
    print("     -> the device event router never raises the events; event-source arming is")
    print("     firmware/ROM-owned (host writes cannot substitute - proven by the")
    print("     wifi-hcc-link H2D probe: out[0] stayed set, ack never written).")
    print("  C. handler[0x3E/0x3F] or handler[0x4C] == 0")
    print("     -> entry A and/or pcie_msg_init did not reach their registration block.")
    print("     Read boot-global +0xEE0..+0xEEC and system object +0x00: this localises")
    print("     the missing write inside the image (still firmware-internal; no host")
    print("     write may be substituted - see REPORT.md section 9).")
    print("\n  DO NOT WRITE: 0x400392F0 (out[5]), 0x40160110, 0x40039010/0x40039014,")
    print("  any 0x17D4xx handler slot, boot-global +0xEE0..+0xEEC, or any RAM object.")


def main():
    what = sys.argv[1] if len(sys.argv) > 1 else "all"
    print("wifi-fw-link-event: firmware event/IRQ-layer READ-ONLY read plan")
    print("(no device contact by this tool; addresses only)")
    if what in ("all", "io"):
        io_plan()
    if what in ("all", "ram"):
        ram_plan()
    if what in ("all", "rom"):
        rom_plan()
    if what in ("all",):
        decisions()


if __name__ == "__main__":
    raise SystemExit(main())
