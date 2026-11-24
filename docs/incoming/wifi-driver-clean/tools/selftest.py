#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# selftest.py - device-free logic/contract checks for the clean-room
# Hi5622V100 Wi-Fi transport driver tree.  No device access, no build, no
# vendor bytes: only the tree's own sources are parsed.
#
# Verifies the corrected protocol wiring that the compile-time asserts and the
# hcc_core selftest do not cover at this level: the six-region window map, the
# F4 region-3 device-CA translation for every mailbox/D2H/out/ETE register,
# the firmware-RAM addresses, the custom-block pre-seed layout, the release-
# only boot gates, the write allowlist and the source/module layout.
import hashlib
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, "..", "src"))
CLEAN = os.path.normpath(os.path.join(HERE, ".."))

checks = []


def check(name, cond, detail=""):
    checks.append((bool(cond), name, detail))


def read(name):
    with open(os.path.join(SRC, name), "r", encoding="utf-8") as fh:
        return fh.read()


# ---------------------------------------------------------------------------
# Independent reference model (values from hi5622_contract.h evidence ledger)
# ---------------------------------------------------------------------------
REGIONS = [
    # (bar_base, bar_limit, ca_target)
    (0x0000000, 0x001BFFFF, 0x00000000),  # ROM_WRAM (phase18 A.6: 0x1c0000 B)
    (0x01C0000, 0x01D7FFF, 0x00400000),  # TCM_NOACP
    (0x01D8000, 0x03B7FFF, 0x01000000),  # PKTRAM_NOACP
    (0x03B8000, 0x04D7FFF, 0x40000000),  # IO
    (0x04D8000, 0x06B7FFF, 0x02000000),  # ACP
    (0x06B8000, 0x08CFFFF, 0x01200000),  # FW RAM
]


def region_of_bar0(off):
    for i, (base, limit, _t) in enumerate(REGIONS):
        if base <= off <= limit:
            return i
    return None


def ca_to_bar0(ca):
    for base, limit, target in REGIONS:
        size = limit - base + 1
        if target <= ca < target + size:
            return base + (ca - target)
    return None


# Expected BAR0 offsets for every device CA the driver touches (derived by
# hand from the region table; the F4 fix is visible on out[3]/out[4]).
EXPECTED_CA = {
    "out[0] H2D pending": (0x40039010, 0x3F1010),
    "out[1] D2H bitmap": (0x40039014, 0x3F1014),
    "out[2] H2D doorbell": (0x400392D4, 0x3F12D4),
    "out[3] D2H ack": (0x40101438, 0x4B9438),
    "out[4] D2H re-arm": (0x40101414, 0x4B9414),
    "out[5] never-write": (0x400392F0, 0x3F12F0),
    "twin word (unused)": (0x40039800, 0x3F1800),
    "D2H doorbell (RO)": (0x40101434, 0x4B9434),
    "CPU release": (0x40000108, 0x3B8108),
    "dual-use gate": (0x4000010C, 0x3B810C),
    "ETE block": (0x4003A000, 0x3F2000),
    "ETE intr": (0x40039508, 0x3F1508),
    "ETE status": (0x4003A874, 0x3F2874),
}

FW_CPU = {
    "handshake page": (0x001BF000, 0x877000),
    "boot state": (0x00118B60, 0x7D0B60),
    "exception mask": (0x00040008, 0x6F8008),
    "boot slot": (0x0017D42C, 0x83542C),
    "platform block": (0x001B2800, 0x86A800),
    "wifi block": (0x001B2860, 0x86A860),
}

WRITABLE_CAS = {0x40039010, 0x40039014, 0x400392D4, 0x40101438,
                0x40101414, 0x4000010C}


def hexwords(text):
    # strip C comments first: the descriptor table has hex values in comments
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return [int(w, 16) for w in re.findall(r"0x[0-9a-fA-F]+", text)]


def main():
    # -- 1. region table as committed in transport.c -----------------------
    transport = read("transport.c")
    table = re.search(r"hi5622_inbound_region hi5622_regions\[.*?\] = \{(.*?)\n\};",
                      transport, re.S)
    check("region table present in transport.c", table is not None)
    if table:
        rows = re.findall(r"\{\s*0x([0-9A-Fa-f]+),\s*0x([0-9A-Fa-f]+),\s*0x([0-9A-Fa-f]+)\s*\}",
                          table.group(1))
        parsed = [(int(a, 16), int(b, 16), int(c, 16)) for a, b, c in rows]
        check("six inbound regions", len(parsed) == 6, str(len(parsed)))
        check("region table matches the decoded contract", parsed == REGIONS,
              repr(parsed))
    check("contract region count", "HI5622_NUM_REGIONS\t6" in read("hi5622_contract.h"))

    # -- 2. regions tile BAR0 contiguously 0x0..0x8cffff -------------------
    ok = True
    for i in range(1, len(REGIONS)):
        ok &= REGIONS[i][0] == REGIONS[i - 1][1] + 1
    check("regions are contiguous (no BAR0 hole can mis-map)", ok)
    for i, (base, limit, ca) in enumerate(REGIONS):
        check("region %d size matches the decoded window" % i,
              (limit - base + 1) in (0x1C0000, 0x18000, 0x1E0000, 0x120000,
                                     0x218000))
        check("region %d host window inside BAR0 (16 MiB)" % i, limit < 0x1000000)

    # -- 3. CA -> BAR0 translation (finding F4) ----------------------------
    for name, (ca, want) in EXPECTED_CA.items():
        check("CA->BAR0 %s" % name, ca_to_bar0(ca) == want,
              "ca=0x%08x want=0x%x got=%s" % (ca, want, ca_to_bar0(ca)))
    ack = ca_to_bar0(0x40101438)
    rearm = ca_to_bar0(0x40101414)
    check("F4: out[3] does not use the broken 20-bit mask", ack == 0x4B9438)
    check("F4: out[4] does not use the broken 20-bit mask", rearm == 0x4B9414)
    check("F4: old mask value is rejected",
          ack != (0x3B8000 + (0x40101438 & 0xFFFFF)))
    check("F4: compile-time assert in transport.c",
          "HI5622_BAR0_CA(HI5622_CA_D2H_ACK) != 0x4b9438u" in transport and
          "HI5622_BAR0_CA(HI5622_CA_D2H_REARM) != 0x4b9414u" in transport)
    for name, (ca, _w) in EXPECTED_CA.items():
        if name.startswith("out[") or name.startswith("twin"):
            check("region %s maps inside region 3" % name,
                  region_of_bar0(ca_to_bar0(ca)) == 3)

    # -- 4. firmware-RAM (CPU -> BAR0 = 0x6b8000 + CPU) --------------------
    for name, (cpu, want) in FW_CPU.items():
        check("fw-RAM %s" % name, 0x6B8000 + cpu == want,
              "cpu=0x%x want=0x%x" % (cpu, want))
        check("fw-RAM %s BAR0 in region 5" % name,
              region_of_bar0(0x6B8000 + cpu) == 5)
    for name, (ca, want) in {
        "ITCM window CA 0x400000": (0x00400000, 0x1C0000),
        "DTCM window CA 0x410000": (0x00410000, 0x1D0000),
        "PKTRAM window CA 0x01000000": (0x01000000, 0x1D8000),
        "ACP window CA 0x02000000": (0x02000000, 0x4D8000),
    }.items():
        check("window %s" % name, ca_to_bar0(ca) == want)

    # -- 5. custom-block pre-seed layout -----------------------------------
    contract = read("hi5622_contract.h")
    check("platform block 0x60 B / 24 words",
          "HI5622_BLOCK_PLAT_BYTES\t\t0x60u" in contract)
    check("wifi block 0x9a8 B / 618 words",
          "HI5622_BLOCK_WIFI_BYTES\t\t0x9a8u" in contract)
    check("blocks are adjacent in firmware RAM",
          0x86A800 + 0x60 == 0x86A860)
    check("blocks inside region 5", region_of_bar0(0x86A800) == 5 and
          region_of_bar0(0x86A860 + 0x9A8 - 1) == 5)

    m = re.search(r"hi5622_platform_block\[.*?\] = \{(.*?)\n\};", transport, re.S)
    check("built-in platform descriptor present", m is not None)
    if m:
        words = hexwords(m.group(1))
        check("platform descriptor has 24 words", len(words) == 24, str(len(words)))
        check("descriptor TCM bounds are the INI values",
              words[1:5] == [0x00117BC4, 0x0000A968, 0x0012252C, 0x0000076C],
              repr(words[1:5]))
        check("descriptor points at the wifi block",
              words[7:9] == [0x001B2860, 0x000009A8])
        check("descriptor self-describes its own size", words[5:7] == [0x001B2800, 0x60])

    # -- 6. release-only boot protocol -------------------------------------
    check("transport writes the release magic through the checked helper",
          "HI5622_CPU_RELEASE_MAGIC" in transport and
          "hi5622_io_bar0(HI5622_CA_CPU_RELEASE)" in transport)
    check("transport never writes CECE (read-only guard compare only)",
          not re.search(r"(hi5622_write32|(?<![a-z_])writel)\s*\([^;]*GATE_CECE",
                        transport))
    check("release requires seeded blocks by default",
          "platform_seeded" in transport and "wifi_seeded" in transport and
          "allow_unseeded_release" in transport)
    check("release requires verified firmware", "fw_verified" in transport)
    check("gate repair writes CEED only (CECE appears only as a refusal guard)",
          "writew((u16)HI5622_GATE_CEED" in transport and
          not re.search(r"(hi5622_write32|(?<![a-z_])writel|writew)\s*\([^;]*GATE_CECE",
                        transport))
    all_c = transport + read("hcc.c") + read("service.c") + read("d2h.c")
    check("no source writes the never-write word",
          not re.search(r"(?<![a-z_])(writel|writew|writeb)\s*\([^;]*CA_NEVER_WRITE",
                        all_c))
    check("hcc.c never performs a raw CECE write (core owns it)",
          "GATE_CECE" not in read("hcc.c"))
    check("only hcc_core.c writes the gate",
          "HCC_CA_GATE" in read("hcc_core.c") and
          "HCC_GATE_CECE" in read("hcc_core.c"))

    # -- 7. hcc_core write allowlist ---------------------------------------
    core_c = read("hcc_core.c")
    m = re.search(r"bool hcc_ca_writable\(hcc_u32 ca\)\n\{(.*?)\n\}", core_c, re.S)
    check("hcc_core write allowlist present", m is not None)
    if m:
        body = m.group(1)
        # the function compares against HCC_CA_* macros, not literals: resolve
        macro_map = {}
        for mname, mca in {
            "HCC_CA_H2D_PENDING": 0x40039010, "HCC_CA_D2H_BITMAP": 0x40039014,
            "HCC_CA_H2D_DOORBELL": 0x400392D4, "HCC_CA_D2H_ACK": 0x40101438,
            "HCC_CA_D2H_REARM": 0x40101414, "HCC_CA_GATE": 0x4000010C,
            "HCC_CA_NEVER_WRITE": 0x400392F0,
            "HCC_CA_CPU_RELEASE": 0x40000108,
        }.items():
            if mname in body:
                macro_map[mca] = mname
        listed = set(macro_map.keys())
        check("core allowlist is exactly the six vendor-writable CAs",
              listed == WRITABLE_CAS, repr(sorted("%08x" % c for c in listed)))
        check("core allowlist excludes out[5] never-write",
              0x400392F0 not in listed)
        check("core allowlist excludes the transport release word",
              0x40000108 not in listed)

    # -- 8. module/source layout -------------------------------------------
    kbuild = read("Kbuild")
    m = re.search(r"hi5622_wifi-y := (.*?)(?:\n\n|\Z)", kbuild, re.S)
    check("Kbuild object list present", m is not None)
    if m:
        objs = m.group(1).replace("\\", " ").split()
        cfiles = sorted(o[:-2] + ".c" for o in objs if o.endswith(".o"))
        present = sorted(f for f in os.listdir(SRC)
                         if f.endswith(".c") and not f.endswith(".mod.c"))
        check("Kbuild lists every source file", cfiles == present,
              "kbuild=%r present=%r" % (cfiles, present))
        check("protocol core objects are linked", "hcc_core.o" in objs and
              "hcc_core_kernel.o" in objs)
    check("module name is hi5622_wifi", "obj-m := hi5622_wifi.o" in kbuild)
    for f in ("hi5622_contract.h", "hi5622_driver.h", "hi5622_d2h.h",
              "hi5622_hcc.h"):
        check("header %s present" % f, os.path.isfile(os.path.join(SRC, f)))
    for f in ("main.c", "transport.c", "ete.c", "d2h.c", "hcc.c", "service.c"):
        check("source %s present" % f, os.path.isfile(os.path.join(SRC, f)))

    # -- 9. sysfs attribute map --------------------------------------------
    main_c = read("main.c")
    for grp in ("hi5622_transport_add_attrs", "hi5622_service_add_attrs",
                "hi5622_hcc_add_attrs", "hi5622_d2h_add_attrs"):
        check("main.c wires %s" % grp, grp in main_c)
    all_src = "".join(read(f) for f in os.listdir(SRC) if f.endswith(".c"))
    for attr in ("prepare", "upload", "verify", "seed", "release",
                 "gate_repair", "boot", "dev_status", "hcc_arm", "hcc_loop",
                 "hcc_service", "hcc_sr_announce", "hcc_cece_kick",
                 "hcc_dr_republish", "hcc_status", "hcc_ready", "hcc_events",
                 "hcc_rings", "hcc_plan", "rd_poll", "rd_advance", "rd_status",
                 "rd_decode", "rd_catalog"):
        check("sysfs attr %s defined" % attr,
              ("DEVICE_ATTR_WO(%s)" % attr) in all_src or
              ("DEVICE_ATTR_RO(%s)" % attr) in all_src)
    for legacy in ("dev_attr_announce", "dev_attr_ringinit", "dev_attr_send",
                   "dev_attr_poll"):
        check("legacy unsafe attr %s removed" % legacy.split("_", 2)[2],
              legacy not in all_src)

    # -- 10. policy / provenance -------------------------------------------
    check("module license GPL", 'MODULE_LICENSE("GPL")' in main_c)
    check("firmware + local block defaults come from the contract header",
          "hi_wifi/FIRMWARE.bin" in contract and "wifi_block.bin" in contract and
          "HI5622_FW_PATH_DEFAULT" in main_c and
          "HI5622_WIFI_BLOCK_PATH_DEFAULT" in main_c)
    check("wifi custom block is loaded through the kernel firmware loader",
          "firmware_request_nowarn" in transport and
          "hi5622_wifi_block_path" in transport)
    check("no absolute /root firmware paths in the driver",
          "/root/" not in transport and "/root/" not in main_c)
    check("no ROM/firmware blob files staged under the lane root",
          not any(f.endswith((".bin", ".rom", ".ko", ".o"))
                  for f in os.listdir(CLEAN)))
    for f in ("hcc_core.c", "hcc_core.h", "hcc_core_kernel.c", "hcc_core_kernel.h"):
        src = os.path.join(SRC, f)
        upstream = os.path.normpath(os.path.join(
            CLEAN, "..", "wifi-hcc-core", "src", f))
        if os.path.isfile(upstream):
            a = hashlib.sha256(open(src, "rb").read()).hexdigest()
            b = hashlib.sha256(open(upstream, "rb").read()).hexdigest()
            check("baked-in %s matches the hcc-core lane" % f, a == b)

    # -- report -------------------------------------------------------------
    passed = sum(1 for ok, _n, _d in checks if ok)
    total = len(checks)
    for ok, name, detail in checks:
        if not ok:
            print("FAIL: %s %s" % (name, ("(%s)" % detail) if detail else ""))
    print("wifi-driver-clean selftest: %d/%d checks PASS" % (passed, total))
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
