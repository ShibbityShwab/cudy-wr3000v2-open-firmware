#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# selftest.py - device-free logic/contract checks for the clean-room
# Hi5622V100 Wi-Fi transport driver tree, extended by the wifi-h2d-send lane
# (task-98) with the first REAL post-ready H2D message path checks.
# No device access, no build, no vendor bytes: only the tree's own sources are
# parsed.
#
# Verifies the corrected protocol wiring that the compile-time asserts and the
# hcc_core selftest do not cover at this level: the six-region window map, the
# F4 region-3 device-CA translation for every mailbox/D2H/out/ETE register,
# the firmware-RAM addresses, the custom-block pre-seed layout, the release-
# only boot gates, the write allowlist, the source/module layout, and the H2D
# send path (version parity with the reference driver tree, the h2d_frames
# library provenance, the six EXACT message builders incl. the ordered
# context-create precondition, the boot/phase/one-frame/consumption gates and
# the absence of any direct mailbox write).
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
    all_c = transport + read("hcc.c") + read("service.c") + read("d2h.c") + \
        read("h2d_send.c") + read("h2d_send_logic.c") + read("h2d_frames.c")
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
                 "rd_decode", "rd_catalog", "h2d_txq_sync", "h2d_txq_sync72",
                 "h2d_ctx_create", "h2d_send", "h2d_status", "h2d_plan"):
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

    # -- 11. H2D send path: source layout + h2d_frames provenance ----------
    kbuild2 = read("Kbuild")
    send_h = read("h2d_send.h")
    send_c = read("h2d_send.c")
    logic_h = read("h2d_send_logic.h")
    logic_c = read("h2d_send_logic.c")
    frames_c = read("h2d_frames.c")
    for obj in ("h2d_frames.o", "h2d_send_logic.o", "h2d_send.o"):
        check("Kbuild links %s" % obj, obj in kbuild2)
    for f in ("h2d_frames.c", "h2d_frames.h", "h2d_catalogue.inc",
              "h2d_send_logic.c", "h2d_send_logic.h", "h2d_send.c",
              "h2d_send.h"):
        check("H2D source %s present" % f,
              os.path.isfile(os.path.join(SRC, f)))
    check("main.c wires the H2D attribute group",
          "hi5622_h2d_add_attrs" in main_c and
          '"h2d"' in main_c)
    check("H2D API declared in h2d_send.h",
          "hi5622_h2d_add_attrs" in send_h and
          "hi5622_h2d_del_attrs" in send_h and
          "hi5622_h2d_send_budget" in send_h)
    # the library is a byte-identical copy of the wifi-h2d-frames deliverable
    frames_lane = os.path.normpath(os.path.join(
        CLEAN, "..", "wifi-h2d-frames", "src"))
    for f in ("h2d_frames.c", "h2d_frames.h", "h2d_catalogue.inc"):
        up = os.path.join(frames_lane, f)
        if os.path.isfile(up):
            a = hashlib.sha256(open(os.path.join(SRC, f), "rb").read()).hexdigest()
            b = hashlib.sha256(open(up, "rb").read()).hexdigest()
            check("h2d library %s matches the h2d-frames lane" % f, a == b)

    # -- 12. H2D send path policy ------------------------------------------
    for fn in ("h2d_build_g0_id1_hdpp_txq_sync", "h2d_build_safe_probe",
               "h2d_build_g0_id1d_edca_opt", "h2d_build_g0_id09_del_user",
               "h2d_build_ctx_create", "h2d_frame_encode"):
        check("H2D emission reuses %s" % fn, fn in logic_c)
    check("H2D send path plans through h2d_plan_post", "h2d_plan_post(" in send_c)
    check("H2D send path commits through the core allowlisted writer",
          send_c.count("hcc_core_sr_commit(") == 1)
    check("H2D send path writes no mailbox CA directly",
          not re.search(r"(?<![a-z_])(writel|writew|writeb)\s*\([^;]*"
                        r"CA_H2D_(PENDING|DOORBELL)", send_c))
    check("H2D send path contains no gate write",
          "HCC_CA_GATE" not in send_c and "HI5622_CA_GATE" not in send_c)
    check("H2D send path cannot touch out[5]",
          "CA_NEVER_WRITE" not in send_c)
    check("H2D send path refuses id 5 / group 4 (via the library policy)",
          "H2D_MSG_GROUP" not in logic_h and
          "HCC_H2D_ID_BUGON" not in logic_c)
    check("H2D gate requires the latched boot",
          "H2D_GATE_NOT_LATCHED" in logic_h and "boot_latched" in logic_c)
    check("H2D gate requires READY_TXQ_SYNC or later",
          "h2d_phase_ok" in logic_c and
          "HCC_PHASE_READY_TXQ_SYNC" in logic_c and
          "HCC_PHASE_ACTIVE" in logic_c)
    check("H2D gate enforces the one-frame idea (liveness + budget)",
          "H2D_GATE_LIVENESS_USED" in logic_c and
          "H2D_GATE_BUDGET" in logic_c)
    check("H2D gate refuses an unconsumed announce",
          "H2D_GATE_INFLIGHT" in logic_c and
          "H2D_GATE_OUT0_PENDING" in logic_c)
    check("H2D liveness stub cross-gated off after a real frame",
          "h2d_frames_sent" in read("hcc.c"))
    check("H2D per-load budget module parameter",
          "module_param_named(h2d_send_budget" in send_c and
          "h2d_send_budget" in send_h)
    check("H2D consumption poll is a bounded scheduled sleep, no busy-wait",
          "msleep_interruptible" in send_c and "udelay" not in send_c and
          "HI5622_H2D_POLL_MAX" in send_c)
    check("H2D post path has no retry loop around the commit",
          send_c.count("hcc_core_sr_commit(") == 1 and
          "while (ret" not in send_c)
    # -- 12a. txq_sync72: the 72-byte LIVE node-length variant -------------
    check("H2D 72-byte node-length variant selector in the logic header",
          "H2D_MSG_TXQ_SYNC72" in logic_h and
          "H2D_TXQ_NODE_LEN" in logic_h and "H2D_TXQ_PAD_BYTE" in logic_h)
    check("H2D emit plan defaults node_len to the frame total",
          "h2d_msg_emit_plan" in logic_h and
          "h2d_msg_emit_plan" in logic_c and
          "*node_len = frame_total" in logic_c)
    check("H2D 72-byte variant keeps the declared frame total 0x30",
          "frame_total != H2D_TXQ_TOTAL" in logic_c and
          "H2D_TXQ_TOTAL" in logic_h)
    check("H2D txq_sync72 shares the byte-exact builder",
          "case H2D_MSG_TXQ_SYNC72:" in logic_c and
          "h2d_build_g0_id1_hdpp_txq_sync" in logic_c)
    check("H2D txq_sync72 is READY-only and once-only (same gate block)",
          "msg == H2D_MSG_TXQ_SYNC || msg == H2D_MSG_TXQ_SYNC72" in logic_c)
    check("H2D 72-byte variant grammar token",
          '{ "txq_sync72"' in logic_c)
    check("H2D 72-byte variant sysfs attribute defined",
          "DEVICE_ATTR_WO(h2d_txq_sync72)" in send_c)
    check("H2D send path pads the SR node buffer to the emit-plan length",
          "memset((u8 *)r->bufs[slot] + total" in send_c and
          "node_len > total" in send_c)
    check("H2D send path sets node word1 from the emit-plan node length",
          "(node_len << 16) | H2D_NODE_FLAGS" in send_c and
          "h2d_msg_emit_plan(msg, total, &node_len, &pad_byte)" in send_c)
    check("H2D telemetry records the SR node length",
          "h2d_last_node_len" in send_c and
          "h2d_last_node_len" in read("hi5622_hcc.h"))
    check("H2D plan/status expose both TXQ node lengths",
          "txq_sync72 node_len=" in send_c and
          "txq_sync72=%s" in send_c)
    check("H2D telemetry fields in the service state",
          "h2d_frames_sent" in read("hi5622_hcc.h") and
          "h2d_send_timeout" in read("hi5622_hcc.h"))
    check("hcc_clear never resets the one-frame budget",
          "h2d_frames_sent = 0" not in read("service.c"))

    # -- 12c. task-102: the ordered context-create (g0 id1 key 0x64) ---------
    check("H2D ctx-create selector in the logic header",
          "H2D_MSG_CTX_CREATE" in logic_h and "H2D_CTX_TOTAL" in logic_h and
          "H2D_CTX_KEY" in logic_h and "H2D_CTX_LEN" in logic_h and
          "H2D_CTX_TYPE" in logic_h and "H2D_CTX_VAP" in logic_h and
          "H2D_CTX_USER" in logic_h)
    check("H2D ctx-create constants: total 0x30, key 0x0064, len 0x14",
          re.search(r"#define H2D_CTX_TOTAL\s+0x30u", logic_h) is not None and
          re.search(r"#define H2D_CTX_KEY\s+0x0064u", logic_h) is not None and
          re.search(r"#define H2D_CTX_LEN\s+0x14u", logic_h) is not None)
    check("H2D ctx-create uses the byte-exact config-event builder",
          "h2d_build_g0_cfg_event" in logic_c)
    check("H2D ctx-create builder is declared and defined",
          "h2d_build_ctx_create" in logic_h and
          "int h2d_build_ctx_create" in logic_c)
    check("H2D ctx-create payload is type 3 / vap 4 / user 4",
          re.search(r"h2d_ctx_data\[.*?\] = \{(.*?)\n\};", logic_c, re.S) is not None)
    m = re.search(r"h2d_ctx_data\[.*?\] = \{(.*?)\n\};", logic_c, re.S)
    if m:
        words = hexwords(m.group(1))
        check("H2D ctx-create data block: 20 B, type 3, vap 4, user 4",
              len(words) == 20 and words[0] == 3 and words[1] == 4 and
              words[4] == 4 and words[0x11] == 0 and words[0x12] == 0,
              repr(words[:8]))
    check("H2D ctx-create keeps a 0xffffffff inherited tail",
          "h2d_ctx_tail" in logic_c and "0xff, 0xff, 0xff, 0xff" in logic_c)
    check("H2D ctx-create refuses type 0/4/5 (no +0x190 allocation)",
          "ctx_type < 1u || ctx_type > 3u" in logic_c)
    check("H2D ctx-create creates the same module index it looks up",
          "h2d_build_g0_cfg_event(f, 1u, dst, H2D_CTX_FA, user" in logic_c)
    check("H2D ctx-create is READY-only and once-only",
          "H2D_GATE_DUP_CTX" in logic_c and "H2D_GATE_CTX_PHASE" in logic_c and
          "live->ctx_created" in logic_c)
    check("H2D ordered ladder: TXQ allowed in ACTIVE only after ctx_create",
          "live->phase == HCC_PHASE_ACTIVE && live->ctx_created" in logic_c)
    check("H2D ctx_create does not set the real TXQ once-only flag",
          "live->txq_submitted = h->h2d_txq_sync_sent > 0u" in send_c)
    check("H2D ctx-create grammar token", '{ "ctx_create"' in logic_c)
    check("H2D ctx-create sysfs attribute defined",
          "DEVICE_ATTR_WO(h2d_ctx_create)" in send_c and
          "&dev_attr_h2d_ctx_create.attr" in send_c)
    check("H2D ctx-create dst != 0 refused (byte-exact)",
          "msg == H2D_MSG_CTX_CREATE) && req->dst != 0u" in logic_c)
    check("H2D ctx-create telemetry counter",
          "h2d_ctx_create_sent" in send_c and
          "h2d_ctx_create_sent" in read("hi5622_hcc.h"))
    check("H2D ctx-create telemetry is not reset by hcc_clear",
          "h2d_ctx_create_sent = 0" not in read("service.c"))
    check("H2D plan shows the ctx_create message",
          "msg=ctx_create wire_total=" in send_c and
          "msg=ctx_create node_len=" in send_c)
    check("H2D status exposes the ctx_create gate",
          "ctx_create=%s" in send_c and "ctx_sent=%u" in send_c)
    check("H2D ctx-create reuses the same post path (single commit site)",
          send_c.count("hcc_core_sr_commit(") == 1 and
          "h2d_post(ctx, H2D_MSG_CTX_CREATE" in send_c)

    # -- 12b. version parity with the reference driver tree -----------------
    ref = os.path.normpath(os.path.join(CLEAN, "..", "wifi-driver-clean",
                                        "src"))
    if os.path.isdir(ref):
        modified = {"main.c", "hcc.c", "service.c", "hi5622_hcc.h", "Kbuild"}
        for f in sorted(os.listdir(ref)):
            if not (f.endswith((".c", ".h")) or f in ("Kbuild", "Makefile",
                                                      ".gitignore")):
                continue
            a = os.path.join(SRC, f)
            b = os.path.join(ref, f)
            if not (os.path.isfile(a) and os.path.isfile(b)):
                continue
            same = (hashlib.sha256(open(a, "rb").read()).hexdigest() ==
                    hashlib.sha256(open(b, "rb").read()).hexdigest())
            if f in modified:
                check("driver copy deviates exactly where intended: %s" % f,
                      not same)
            else:
                check("driver copy is byte-identical: %s" % f, same)
    check("H2D build products are gitignored",
          "*.ko" in read(".gitignore") and "*.o" in read(".gitignore"))

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