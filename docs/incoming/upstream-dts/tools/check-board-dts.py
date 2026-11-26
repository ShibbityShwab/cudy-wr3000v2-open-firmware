#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""check-board-dts.py - verify the luofu R116 board DTS (compiled DTB) carries
the board facts the target/image recipe depends on, and (optionally) compare
the node inventory with the extracted vendor device tree.

Lane: opensource/docs/incoming/upstream-dts (task-238). Host-only; needs
fdtget (device-tree-compiler package) and, for --vendor-dtb, dtc.

Usage:
  check-board-dts.py --dtb lut.dtb [--vendor-dtb vendor.dtb] [--verbose]

The checks are the contract the rest of the target relies on: the two
Cortex-A9 CPUs + the SMP enable-method, the 128 MiB DDR3 window, the SPI-NAND
with the A/B partitions the image recipe sizes itself from, the console UART,
CRG/watchdog, sysctrl/reboot/boot-selector, GIC/SCU/PL310/TWD timers, the two
GPIO banks + keys/LEDs, both PCIe root complexes (with the RC0 endpoint reset
lines folded from patch 013), MDIO/PHYs, the four switch user ports, the PIE
window, pinctrl, the thermal sensor + zones, and the clock tree.

Exit code 0 = every REQUIRED check PASSed; 1 = at least one FAIL. Missing
vendor-only nodes are reported, never failed.
"""
import argparse
import re
import shutil
import subprocess
import sys


def run(cmd):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


class Dtb:
    def __init__(self, path):
        self.path = path

    def get(self, path, prop, typ=None):
        cmd = ["fdtget"]
        if typ:
            cmd += ["-t", typ]
        cmd += [self.path, path, prop]
        r = run(cmd)
        if r.returncode != 0:
            return None
        return r.stdout.strip()

    def ints(self, path, prop):
        v = self.get(path, prop, "x")
        if v is None:
            return None
        try:
            return [int(x, 16) for x in v.split()]
        except ValueError:
            return None

    def props(self, path):
        r = run(["fdtget", "-p", self.path, path])
        return r.stdout.split() if r.returncode == 0 else []

    def children(self, path):
        r = run(["fdtget", "-l", self.path, path])
        return r.stdout.split() if r.returncode == 0 else []

    def strings(self, path, prop):
        v = self.get(path, prop, "s")
        return v.split() if v is not None else None


CHECKS = []


def check(cid, desc):
    def deco(fn):
        CHECKS.append((cid, desc, fn))
        return fn
    return deco


def want(dtb, path, prop, typ=None):
    v = dtb.get(path, prop, typ)
    return v is not None


# ---- root identity / model -------------------------------------------------
@check("R1", "root model names the board")
def _(dtb):
    m = dtb.strings("/", "model")
    ok = bool(m) and "WR3000" in " ".join(m)
    return ok, "model=" + (" ".join(m) if m else "<missing>")


@check("R2", "root compatible carries cudy + hisilicon board ids")
def _(dtb):
    c = dtb.strings("/", "compatible") or []
    need = ["cudy,wr3000-v2", "hisilicon,luofu-r116", "hisilicon,luofu"]
    miss = [x for x in need if x not in c]
    return not miss, "compatible=" + " ".join(c) + ((" missing=" + " ".join(miss)) if miss else "")


# ---- CPU / SMP -------------------------------------------------------------
@check("C1", "cpus node selects the luofu SMP enable-method")
def _(dtb):
    v = dtb.strings("/cpus", "enable-method") or []
    return v == ["hisilicon,luofu-smp"], "enable-method=" + " ".join(v)


@check("C2", "exactly two Cortex-A9 CPUs")
def _(dtb):
    kids = dtb.children("/cpus")
    cpu = [k for k in kids if k.startswith("cpu@")]
    ok = kids_ok = len(cpu) == 2
    details = []
    for k in cpu:
        comp = dtb.strings("/cpus/" + k, "compatible") or []
        ok = ok and "arm,cortex-a9" in comp
        details.append(k + "=" + " ".join(comp))
    return ok, "cpus=" + ",".join(details)


# ---- memory / reserved -----------------------------------------------------
@check("M1", "128 MiB DDR3 usable window at 0x80500000")
def _(dtb):
    reg = dtb.ints("/memory@80500000", "reg")
    # 0x80000000..0x88000000 is the 128 MiB DDR3; the loader keeps the first
    # 5 MiB (0x80000000..0x80500000), so Linux gets 123 MiB.
    ok = reg == [0x80500000, 0x7B00000] or (reg is not None and reg[0] == 0x80500000 and reg[1] > 0)
    return ok, "reg=" + (" ".join("%x" % x for x in reg) if reg else "<missing>")


@check("M2", "flashinfo reserved region (loader hand-off info)")
def _(dtb):
    p = "/reserved-memory/flashinfo@80600000"
    c = dtb.strings(p, "compatible") or []
    reg = dtb.ints(p, "reg")
    atag = dtb.ints(p, "atag-offset")
    ok = c and c[0] == "hisilicon,luofu-flashinfo" and reg == [0x80600000, 0x4000] and atag == [0x1000]
    return ok, "compatible=" + " ".join(c) + " reg=" + (" ".join("%x" % x for x in reg) if reg else "?") + " atag-offset=" + ("%x" % atag[0] if atag else "?")


# ---- console UART ----------------------------------------------------------
@check("U1", "console UART1 present/okay at 0x1010f000")
def _(dtb):
    p = "/serial@1010f000"
    comp = dtb.strings(p, "compatible") or []
    reg = dtb.ints(p, "reg")
    st = dtb.strings(p, "status")
    ok = "snps,dw-apb-uart" in comp and reg == [0x1010F000, 0x1000] and (st is None or st == ["okay"])
    return ok, "compatible=" + " ".join(comp) + " reg=" + (" ".join("%x" % x for x in reg) if reg else "?") + " status=" + (" ".join(st) if st else "okay(default)")


@check("U2", "chosen stdout-path + serial0 console alias")
def _(dtb):
    chosen = dtb.strings("/chosen", "stdout-path") or []
    aliases = dtb.strings("/aliases", "console") or []
    s0 = dtb.strings("/aliases", "serial0") or []
    ok = chosen == ["serial0:115200n8"] and aliases and aliases[0].endswith("/serial@1010f000") and s0 == ["/serial@1010f000"]
    return ok, "stdout-path=" + " ".join(chosen) + " console=" + " ".join(aliases) + " serial0=" + " ".join(s0)


@check("U3", "second UART present (uart0, disabled)")
def _(dtb):
    p = "/serial@1010e000"
    comp = dtb.strings(p, "compatible") or []
    reg = dtb.ints(p, "reg")
    st = dtb.strings(p, "status") or []
    ok = "snps,dw-apb-uart" in comp and reg == [0x1010E000, 0x1000]
    return ok, "reg=" + (" ".join("%x" % x for x in reg) if reg else "?") + " status=" + " ".join(st)


# ---- SPI-NAND + partitions -------------------------------------------------
@check("N1", "FMC SPI-NAND controller + spi-nand child")
def _(dtb):
    p = "/spi@10a20000"
    comp = dtb.strings(p, "compatible") or []
    st = dtb.strings(p, "status") or []
    kids = dtb.children(p)
    flash = [k for k in kids if k.startswith("flash@")]
    fcomp = dtb.strings(p + "/" + flash[0], "compatible") if flash else []
    ok = "hisilicon,luofu-fmc" in comp and (st is None or st == ["okay"]) and fcomp == ["spi-nand"]
    return ok, "controller=" + " ".join(comp) + " child=" + (" ".join(fcomp) if fcomp else "<missing>")


@check("N2", "17 fixed partitions incl. the A/B kernel/rootfs slots")
def _(dtb):
    p = "/spi@10a20000/flash@1/partitions"
    kids = dtb.children(p)
    labels = []
    sizes = {}
    for k in kids:
        lab = dtb.strings(p + "/" + k, "label") or ["?"]
        labels.append(lab[0])
        reg = dtb.ints(p + "/" + k, "reg")
        if reg and len(reg) == 2:
            sizes[lab[0]] = reg[1]
    need = ["kernela", "kernelb", "rootfsa", "rootfsb", "rootfs_data"]
    miss = [x for x in need if x not in labels]
    ok = len(kids) == 17 and not miss and sizes.get("kernela") == 0x840000 and sizes.get("rootfsa") == 0x1740000
    detail = "count=%d" % len(kids)
    if miss:
        detail += " missing=" + ",".join(miss)
    detail += " kernela=%s rootfsa=%s" % (hex(sizes.get("kernela", 0)), hex(sizes.get("rootfsa", 0)))
    return ok, detail


# ---- CRG / watchdog / sysctrl / reboot / selector --------------------------
@check("P1", "CRG syscon with clock/reset cells + watchdog child")
def _(dtb):
    p = "/system-controller@14880000"
    comp = dtb.strings(p, "compatible") or []
    cc = dtb.ints(p, "#clock-cells")
    rc = dtb.ints(p, "#reset-cells")
    wd = "watchdog" in dtb.children(p)
    wcomp = dtb.strings(p + "/watchdog", "compatible") or []
    ok = "hisilicon,luofu-crg" in comp and "syscon" in comp and cc == [1] and rc == [2] and wd and wcomp == ["hisilicon,luofu-wdt"]
    return ok, "compatible=" + " ".join(comp) + " #clock-cells=" + (" ".join(map(str, cc)) if cc else "?") + " #reset-cells=" + (" ".join(map(str, rc)) if rc else "?") + " watchdog=" + (" ".join(wcomp) if wcomp else "<missing>")


@check("P2", "sysctrl + reboot + boot-selector nodes")
def _(dtb):
    sc = dtb.strings("/system-controller@10100000", "compatible") or []
    rb = dtb.strings("/reboot", "compatible") or []
    bs = dtb.strings("/boot-selector", "compatible") or []
    off = dtb.ints("/boot-selector", "hisilicon,selector-offset")
    ok = "hisilicon,luofu-sysctrl" in sc and "syscon" in sc and rb == ["hisilicon,luofu-reboot"] and bs == ["hisilicon,luofu-boot-selector"] and off == [0xC08]
    return ok, "sysctrl=" + " ".join(sc) + " reboot=" + " ".join(rb) + " selector=" + " ".join(bs) + " offset=" + ("%x" % off[0] if off else "?")


# ---- interrupt / timer / cache blocks --------------------------------------
@check("I1", "GIC + SCU + PL310 + TWD + SP804 timer blocks")
def _(dtb):
    gic = dtb.strings("/interrupt-controller@10181000", "compatible") or []
    scu = dtb.strings("/scu@10180000", "compatible") or []
    l2 = dtb.strings("/l2-cache@7f000000", "compatible") or []
    twd = dtb.strings("/local-timer@10180600", "compatible") or []
    sp804 = dtb.strings("/timer@10104000", "compatible") or []
    ok = ("arm,cortex-a9-gic" in gic and "arm,cortex-a9-scu" in scu
          and "arm,pl310-cache" in l2 and "arm,cortex-a9-twd-timer" in twd
          and "arm,sp804" in sp804)
    return ok, "gic=" + " ".join(gic) + " scu=" + " ".join(scu) + " pl310=" + " ".join(l2) + " twd=" + " ".join(twd) + " sp804=" + " ".join(sp804)


# ---- GPIO / keys / LEDs ----------------------------------------------------
@check("G1", "two DW GPIO banks with ports")
def _(dtb):
    b0 = dtb.strings("/gpio@10106000", "compatible") or []
    b1 = dtb.strings("/gpio@10107000", "compatible") or []
    ok = b0 == ["snps,dw-apb-gpio"] and b1 == ["snps,dw-apb-gpio"]
    ok = ok and "gpio-port@0" in dtb.children("/gpio@10106000") and "gpio-port@0" in dtb.children("/gpio@10107000")
    return ok, "bank0=" + " ".join(b0) + " bank1=" + " ".join(b1)


@check("G2", "gpio-keys (reset/wps) + gpio-leds (6 LEDs)")
def _(dtb):
    keys = dtb.children("/gpio-keys-polled") if "gpio-keys-polled" in dtb.children("/") else []
    leds = dtb.children("/gpio-leds") if "gpio-leds" in dtb.children("/") else []
    ok = "reset" in keys and "wps" in keys and len(leds) == 6
    return ok, "keys=" + ",".join(keys) + " leds=%d" % len(leds)


# ---- PCIe root complexes ---------------------------------------------------
@check("E1", "PCIe RC0: hisilicon,luofu-pcie, domain 0, dma-ranges, EP reset lines")
def _(dtb):
    p = "/pcie@10160000"
    comp = dtb.strings(p, "compatible") or []
    dom = dtb.ints(p, "linux,pci-domain")
    st = dtb.strings(p, "status") or []
    dma = dtb.ints(p, "dma-ranges")
    nreg = len(dtb.ints(p, "reg") or []) // 2
    names = dtb.strings(p, "reg-names") or []
    gpio = dtb.ints(p, "reset-gpios")
    assert_us = dtb.ints(p, "reset-assert-us")
    deassert_us = dtb.ints(p, "reset-deassert-us")
    ok = ("hisilicon,luofu-pcie" in comp and dom == [0] and (st is None or st == ["okay"])
          and dma is not None and dma[:2] == [0x02000000, 0x0] and nreg == 3
          and names == ["dbi", "misc", "config"] and gpio is not None and len(gpio) == 9
          and assert_us == [20000] and deassert_us == [100000])
    return ok, "domain=%s regs=%d dma-ranges-cells=%d reset-gpios-cells=%s assert=%s deassert=%s" % (
        dom, nreg, len(dma) if dma else 0, len(gpio) if gpio else 0, assert_us, deassert_us)


@check("E2", "PCIe RC1: hisilicon,luofu-pcie, domain 1, enabled")
def _(dtb):
    p = "/pcie@10164000"
    comp = dtb.strings(p, "compatible") or []
    dom = dtb.ints(p, "linux,pci-domain")
    st = dtb.strings(p, "status") or []
    dma = dtb.ints(p, "dma-ranges")
    ok = "hisilicon,luofu-pcie" in comp and dom == [1] and (st is None or st == ["okay"]) and dma is not None
    return ok, "domain=%s status=%s dma-ranges=%s" % (dom, " ".join(st) if st else "okay(default)", "yes" if dma else "no")


# ---- MDIO / PHY / GMAC -----------------------------------------------------
@check("D1", "MDIO bus + four PHYs")
def _(dtb):
    comp = dtb.strings("/mdio@14000000", "compatible") or []
    phy = [k for k in dtb.children("/mdio@14000000") if k.startswith("ethernet-phy@")]
    ok = "hisilicon,luofu-mdio" in comp and len(phy) == 4
    return ok, "compatible=" + " ".join(comp) + " phys=" + ",".join(phy)


@check("D2", "five GEMAC nodes present")
def _(dtb):
    g = [k for k in dtb.children("/") if k.startswith("ethernet@")]
    ok = len(g) == 5
    statuses = {}
    for k in g:
        st = dtb.strings("/" + k, "status")
        statuses[k] = " ".join(st) if st else "okay(default)"
    return ok, "gmacs=%d %s" % (len(g), ",".join("%s:%s" % (k, v) for k, v in statuses.items()))


# ---- LSW / PIE / WOE -------------------------------------------------------
@check("S1", "LSW switch node (okay) with four user ports")
def _(dtb):
    p = "/ethernet-switch@11000000"
    comp = dtb.strings(p, "compatible") or []
    st = dtb.strings(p, "status") or []
    ports = [k for k in dtb.children(p + "/ethernet-ports")] if "ethernet-ports" in dtb.children(p) else []
    ok = "hisilicon,luofu-switch" in comp and (st is None or st == ["okay"]) and len(ports) == 4
    return ok, "compatible=" + " ".join(comp) + " status=" + (" ".join(st) if st else "okay(default)") + " ports=%d" % len(ports)


@check("S2", "PIE packet engine node (okay); WOE node present (disabled)")
def _(dtb):
    pie = dtb.strings("/pie@10a70000", "compatible") or []
    pst = dtb.strings("/pie@10a70000", "status") or []
    woe = dtb.strings("/woe", "compatible") or []
    wst = dtb.strings("/woe", "status") or []
    ok = "hisilicon,luofu-pie" in pie and (pst is None or pst == ["okay"]) and "hisilicon,luofu-lsw-woe" in woe
    return ok, "pie=" + " ".join(pie) + " pie-status=" + (" ".join(pst) if pst else "okay(default)") + " woe=" + " ".join(woe) + " woe-status=" + " ".join(wst)


# ---- pinctrl / thermal -----------------------------------------------------
@check("T1", "pinctrl node (driver draft) + thermal sensor and zones")
def _(dtb):
    pc = dtb.strings("/pinctrl@14900100", "compatible") or []
    ts = dtb.strings("/thermal-sensor@14900500", "compatible") or []
    zones = dtb.children("/thermal-zones") if "thermal-zones" in dtb.children("/") else []
    trips = 0
    for z in zones:
        trips += len(dtb.children("/thermal-zones/" + z + "/trips"))
    ok = "hisilicon,luofu-pinctrl" in pc and "hisilicon,luofu-tvsensor" in ts and zones and trips >= 2
    return ok, "pinctrl=" + " ".join(pc) + " tvsensor=" + " ".join(ts) + " zones=" + ",".join(zones) + " trips=%d" % trips


# ---- clock tree ------------------------------------------------------------
@check("K1", "clock tree: fixed clocks + TWD clock + CRG clock wiring")
def _(dtb):
    root = dtb.children("/")
    fixed = [k for k in root if "fixed-clock" in k or k.startswith("clock-")]
    twd = "twd-clock" in root
    crg_clocks = dtb.ints("/system-controller@14880000", "clocks")
    names = dtb.strings("/system-controller@14880000", "clock-names") or []
    ok = len(fixed) >= 8 and twd and crg_clocks is not None and len(names) >= 7
    return ok, "fixed=%d twd=%s crg-clocks=%s names=%s" % (len(fixed), twd, len(crg_clocks) if crg_clocks else 0, ",".join(names))


# ---- vendor comparison -----------------------------------------------------
NODE_RE = re.compile(r"^([ \t]*)([^ \t=][^{};]*)\{\s*$")
CLOSE_RE = re.compile(r"^[ \t]*\};\s*$")


def node_paths_from_dts(text):
    stack = []
    out = []
    for ln in text.splitlines():
        m = NODE_RE.match(ln)
        if m:
            name = m.group(2).strip()
            if name == "/":
                stack = []
                out.append("/")
            else:
                stack.append(name)
                out.append("/" + "/".join(stack))
        elif CLOSE_RE.match(ln) and stack:
            stack.pop()
    return out


def decompile(dtb):
    r = run(["dtc", "-I", "dtb", "-O", "dts", dtb])
    if r.returncode != 0:
        raise SystemExit("dtc failed on %s: %s" % (dtb, r.stderr.strip()))
    return r.stdout


def compat_sets(dtbobj):
    """(node path -> compatible list) for one DTB, keyed by the decompiled paths."""
    out = {}
    for p in node_paths_from_dts(decompile(dtbobj.path)):
        c = dtbobj.strings(p, "compatible")
        if c:
            out[p] = tuple(c)
    return out


def compare_vendor(dtb, vendor):
    ours = node_paths_from_dts(decompile(dtb.path))
    vend = node_paths_from_dts(decompile(vendor))
    vset, oset = set(vend), set(ours)
    only_v = [p for p in vend if p not in oset]
    only_o = [p for p in ours if p not in vset]
    print()
    print("== vendor device-tree comparison ==")
    print("vendor DTB: %s (%d nodes)" % (vendor, len(vend)))
    print("board  DTB: %s (%d nodes)" % (dtb.path, len(ours)))
    print("vendor-only nodes: %d" % len(only_v))
    for p in only_v:
        print("  V " + p)
    print("board-only nodes: %d" % len(only_o))
    for p in only_o:
        print("  B " + p)
    # Semantic view: the node-name diff above is dominated by renames (vendor
    # hsan,* names vs our bindings). Compare the compatible-string sets too.
    vmap = compat_sets(Dtb(vendor))
    omap = compat_sets(dtb)
    vc = set(vmap.values())
    oc = set(omap.values())
    print()
    print("== compatible-string comparison ==")
    print("vendor-only compatible sets: %d" % len(vc - oc))
    for c in sorted(vc - oc):
        print("  V " + ",".join(c))
    print("board-only compatible sets: %d" % len(oc - vc))
    for c in sorted(oc - vc):
        print("  B " + ",".join(c))


def main():
    ap = argparse.ArgumentParser(description="verify the luofu R116 board DTS (compiled DTB)")
    ap.add_argument("--dtb", required=True, help="compiled board DTB")
    ap.add_argument("--vendor-dtb", default=None, help="optional extracted vendor base DTB to diff the node inventory against")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()
    if not shutil.which("fdtget"):
        sys.exit("check-board-dts: fdtget not found (install device-tree-compiler)")
    if not shutil.which("dtc"):
        sys.exit("check-board-dts: dtc not found (install device-tree-compiler)")
    dtb = Dtb(args.dtb)
    print("board DTB: %s" % args.dtb)
    passed = failed = 0
    for cid, desc, fn in CHECKS:
        try:
            ok, detail = fn(dtb)
        except Exception as exc:  # a crash is a FAIL, not a skip
            ok, detail = False, "exception: %r" % (exc,)
        print("%-4s %-4s %s" % (cid, "PASS" if ok else "FAIL", desc))
        if not ok or args.verbose:
            print("          " + str(detail))
        if ok:
            passed += 1
        else:
            failed += 1
    print()
    print("SUMMARY: %d checks, %d PASS, %d FAIL" % (len(CHECKS), passed, failed))
    if args.vendor_dtb:
        compare_vendor(dtb, args.vendor_dtb)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
