#!/usr/bin/env python3
"""regmap_probe.py - host-only audit of the *instrument*, not the device.

Question it answers: when the kernel service does
    readl(bar0 + HI5622_BAR0_IO(ca)),   HI5622_BAR0_IO(ca) = 0x3b8000 + (ca & 0xfffff)
which inbound region does that BAR0 offset really fall in, and which device CA does
the endpoint iATU decode it to?  HI5622_BAR0_IO() masks CA to 20 bits, so for any CA
whose IO offset is >= 0x100000 the macro silently wraps: the read is then NOT a read of
the intended device register.  The GIC/"event controller" block at CA 0x40160000 is
past the end of the region-3 IO window (size 0x120000, CA 0x40000000..0x4011ffff), so
every 0x4016xxxx CA wraps by -0x100000 and decodes to CA 0x4006xxxx instead.

Sources (all local, read-only):
  * six inbound regions: docs/phase18/inbound-map.md A.6 (proven values), mirrored in
    wifi-transport-port/build/hi5622_pciet.h
  * BAR0 macro: wifi-transport-port/build/hi5622_pciet.h HI5622_BAR0_IO
  * probe list: wifi-hcc-service/src/hi5622_hcc_service.h HI5622_HCC_CA_EV_*

Prints derived addresses/values only; never opens a device and never copies vendor bytes.

Usage:
  $PY tools/regmap_probe.py             # audit the hcc_irq_probe register list
  $PY tools/regmap_probe.py 0x40160100  # audit one CA
"""
import sys

IO_BASE = 0x3B8000          # HI5622_REGION_IO_BASE
IO_MASK = 0x000FFFFF        # HI5622_REGION_IO_MASK

# (bar_base, bar_limit, ca_target, size) - phase18 A.6 / hi5622_pciet.h
REGIONS = [
    (0x0000000, 0x001BFFF, 0x00000000, 0x01C0000),   # 0 ROM_WRAM
    (0x001C000, 0x001D7FF, 0x00400000, 0x0018000),   # 1 TCM_NOACP
    (0x001D800, 0x03B7FFF, 0x01000000, 0x01E0000),   # 2 PKTRAM_NOACP
    (0x03B8000, 0x04D7FFF, 0x40000000, 0x0120000),   # 3 IO   (CA 0x40000000..0x4011ffff)
    (0x04D8000, 0x06B7FFF, 0x02000000, 0x01E0000),   # 4 ACP
    (0x06B8000, 0x08CFFFF, 0x01200000, 0x0218000),   # 5 ACP (firmware target)
]

# hcc_irq_probe list (hi5622_hcc_service.h + pciet_hcc_service.c hcc_irq_probe_show)
PROBE = [
    ("EV_CTRL",   0x40160100, "expect 1 (ROM 0x5C8 + image 0xC3024)"),
    ("EV_MAXPRI", 0x40160104, "expect 0xFF (ROM 0x5C4 + image 0xC3024)"),
    ("EV_CFG",    0x40160108, "expect 3 (image 0xC3024)"),
    ("EV_VECTOR", 0x4016010C, "current vector (0 when idle)"),
    ("EV_EOI",    0x40160110, "EOI history"),
    ("EV_EN0",    0x40161100, "expect 0x2000 (event 0x1D)"),
    ("EV_EN1",    0x40161104, "expect 0xC0006000"),
    ("EV_EN2",    0x40161108, "expect 0x5000"),
    ("EV_PEND0",  0x40161188, "latched raise word A"),
    ("EV_PEND1",  0x40161288, "latched raise word B"),
    ("TIMER_RELOAD", 0x40160600, "expect 0x0061A800"),
    ("TIMER_CTRL",   0x40160608, "expect 7"),
    ("TIMER_START",  0x4016060C, "expect 1"),
]


def bar0_io(ca):
    return IO_BASE + (ca & IO_MASK)


def region_of(bar0):
    for i, (base, limit, target, size) in enumerate(REGIONS):
        if base <= bar0 <= limit:
            return i, base, limit, target, size
    return None


def audit(ca):
    off = bar0_io(ca)
    hit = region_of(off)
    if hit is None:
        return ca, off, None, None, "BAR0 offset outside every region (unmapped)"
    i, base, limit, target, size = hit
    real_ca = target + (off - base)
    if real_ca == ca:
        verdict = "OK: region 3 decodes back to the intended CA"
    else:
        verdict = (f"WRONG: BAR0 0x{off:06x} is region {i} "
                   f"(0x{base:06x}-0x{limit:06x} -> CA 0x{target:08x}) and decodes "
                   f"device CA 0x{real_ca:08x}, NOT the intended CA 0x{ca:08x}")
    return ca, off, i, real_ca, verdict


def main():
    cas = PROBE
    if len(sys.argv) > 1:
        cas = [("user", int(sys.argv[1], 0), "")]
    print("inbound region table (phase18 A.6 / hi5622_pciet.h):")
    for i, (base, limit, target, size) in enumerate(REGIONS):
        print(f"  region {i}: BAR0 0x{base:07x}-0x{limit:07x} size 0x{size:06x} -> CA 0x{target:08x}")
    print()
    bad = 0
    for name, ca, expect in cas:
        c, off, reg, real_ca, verdict = audit(ca)
        print(f"{name:13s} CA 0x{c:08x} -> HI5622_BAR0_IO = 0x{off:06x}: {verdict}")
        if real_ca != c:
            bad += 1
        if expect:
            print(f"              ({expect})")
    print()
    if bad:
        print(f"{bad}/{len(cas)} probed CAs do NOT decode to themselves.")
        print("The GIC block at CA 0x40160000 lies past the end of inbound region 3")
        print("(0x40000000-0x4011ffff): the vendor programs only six inbound regions, so")
        print("CA 0x4016xxxx is not host-visible through any BAR0 offset. HI5622_BAR0_IO()")
        print("wraps it by -0x100000 to the unrelated CA 0x4006xxxx IO registers (zeros).")
        print("=> zeros read there are NOT device-register state. A correct readout needs a")
        print("7th iATU viewport mapped at target CA 0x40160000 (read-only diagnostic) or a")
        print("device-side instrument.")
    else:
        print("all audited CAs decode to themselves.")
    print()
    print("cross-check with the measured golden handshake page (CA 0x1BF000):")
    print("  ROM S1 (romdis.py flow): ROM 0x5C4 [0x40160104]=0xFF, ROM 0x5C8 [0x40160100]=1,")
    print("  then WFI, then page +4 |= 4 at ROM 0x5FA and page +8 = 2 at ROM 0x5FE.")
    print("  Golden page +4 = 0x7 (bit2 set) and +8 = 2 therefore prove ROM 0x5C8 executed,")
    print("  so a correctly decoded read of CA 0x40160100 MUST return 1 on that boot.")


if __name__ == "__main__":
    main()
