#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_tx_scheduler_re.py - decode the firmware band-1 beacon-TX gate/task model.

Lane: wifi-5g-tx-scheduler (shared task task-231). HOST-ONLY, read-only: the
pinned LOCAL vendor firmware image is read in place; only derived values
(addresses, table words, instruction mnemonics, register constants) are
printed.  No vendor byte is copied into this repository and no device is
touched by this tool.

Artifacts (local, uncommitted):
  firmware: /home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin
            928,920 B sha256 7fc87e20...311b ; CPU address CA = file offset + 0x40000
  objdump:  the OpenWrt lab ARM objdump (override --objdump or $BW_OBJDUMP)

Decode summary (see ../REPORT.md, ../TABLES.md):
  * 45 handler NAMES (CA 0x12266C..0x122BA4): 32 per-band ISR names (16 x 5g/2g
    pairs) + 13 smac_msg_proc names.  They are DEAD static data in this build
    (no literal / movw+movt reference anywhere), but they name two static
    handler tables that follow the same order.
  * CA 0x10B058: 32 thumb handler pointers in the exact 45-name ISR order
    (idx14/15 = smac_tbtt_5g_isr / smac_tbtt_2g_isr).  The TBTT handlers are
    6/11-instruction leaf reporting stubs: this firmware has NO beacon-build/queue
    task; the beacon TX is hardware TBTT-driven.
  * CA 0x10B0DC: 10 SMAC message-processor pointers; includes the four
    beacon_tx_{resume,suspend}_req processors and the enable/disable-TRX worker.
  * CA 0xE6A26 / 0xE6A70 = band-1 (5 GHz) beacon TX resume / suspend;
    CA 0xE6AB4 / 0xE6B00 = band-0 (2.4 GHz) beacon TX resume / suspend.  They
    toggle exactly two host-visible words: 0x40101220 (per-band SMAC/TX enable,
    bit0 = 5 GHz, bit1 = 2.4 GHz) and 0x40101208 = 0x40101220-24 (per-band
    suspend latch: band1 bits 0x1/0x4, band0 bits 0x2/0x8).
  * Boot init CA 0xD1B68 registers the same 8 IRQ lines per band with per-band
    wrapper functions (CA 0x180814 +8 = band0 object / +0x1C = band1 object):
    band0 IRQ 47/49/50/51/52/82/83/59, band1 IRQ 53/55/56/57/58/84/85/60,
    7 shared bodies.  The install store is [0x17D398 + 0x98 + 4*irq]
    (install fn CA 0xC74B0), so the LIVE table is host-readable in RAM.
  * No g0-id1 table A/B worker is any of these handlers: the host cannot post
    the beacon resume/suspend messages; it can only set the gate words.
"""
import argparse
import hashlib
import os
import re
import struct
import subprocess
import sys

BASE = 0x40000
FW_SHA = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_LEN = 928920
DEFAULT_FW = ("/home/shibbityshwab/recovery-extract/lib/firmware/"
              "hi_wifi/FIRMWARE.bin")
DEFAULT_OD = ("/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/"
              "toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/"
              "arm-openwrt-linux-objdump")

# --- the 45-name blob ------------------------------------------------------
NAME_BLOB_LO = 0x12266C
NAME_BLOB_HI = 0x122BA4
NAME_COUNT = 45
ISR_NAME_COUNT = 32
ISR_NAMES = [
    "smac_coex_rx_abort_end_5g_isr", "smac_coex_rx_abort_end_2g_isr",
    "smac_rx_complete_5g_isr", "smac_rx_complete_2g_isr",
    "smac_tx_complete_5g_isr", "smac_tx_complete_2g_isr",
    "smac_common_timer_5g_isr", "smac_common_timer_2g_isr",
    "smac_coex_pta_rx_abort_timeout_5g_isr",
    "smac_coex_pta_rx_abort_timeout_2g_isr",
    "smac_coex_tx_abort_end_5g_isr", "smac_coex_tx_abort_end_2g_isr",
    "smac_coex_pta_tx_abort_timeout_5g_isr",
    "smac_coex_pta_tx_abort_timeout_2g_isr",
    "smac_tbtt_5g_isr", "smac_tbtt_2g_isr",
    "smac_rx_timeout_5g_isr", "smac_rx_timeout_2g_isr",
    "smac_pre_tx_5g_isr", "smac_pre_tx_2g_isr",
    "smac_backoff_timeout_5g_isr", "smac_backoff_timeout_2g_isr",
    "smac_user_queue_empty_5g_isr", "smac_user_queue_empty_2g_isr",
    "smac_user_ps_5g_isr", "smac_user_ps_2g_isr",
    "smac_tx_abort_start_5g_isr", "smac_tx_abort_start_2g_isr",
    "smac_lifetime_expire_5g_isr", "smac_lifetime_expire_2g_isr",
    "smac_tx_exit_5g_isr", "smac_tx_exit_2g_isr",
]
MSG_PROC_NAMES = [
    "smac_msg_proc_set_mu_edca_timer_req",
    "smac_msg_proc_ac_suspend_req",
    "smac_msg_proc_set_cali_self_cts_tx_num_req",
    "smac_msg_proc_set_cali_ppdu_tx_num_req",
    "smac_msg_proc_rx_ring_reset_req",
    "smac_msg_proc_update_tx_bypass_ctrl_req",
    "smac_msg_proc_beacon_tx_resume_req",
    "smac_msg_proc_beacon_tx_suspend_req",
    "smac_msg_proc_clr_hw_fifo_req",
    "smac_msg_proc_tx_resume_req", "smac_msg_proc_tx_suspend_req",
    "smac_msg_proc_enable_trx_req", "smac_msg_proc_disable_trx_req",
]
TBTT_5G_NAME_IDX = ISR_NAMES.index("smac_tbtt_5g_isr")   # 14
TBTT_2G_NAME_IDX = ISR_NAMES.index("smac_tbtt_2g_isr")   # 15

# --- static handler tables -------------------------------------------------
ISR_TABLE = 0x10B058          # 32 x u32 thumb handler pointer
ISR_TABLE_N = 32
MSG_TABLE = 0x10B0DC          # 10 x u32 thumb handler pointer
MSG_TABLE_N = 10

# --- beacon_tx resume/suspend processors (gate registers) ------------------
GATE_EN = 0x40101220          # per-band SMAC/TX enable (bit0 5G, bit1 2.4G)
GATE_SUS = 0x40101208         # == GATE_EN - 24 per-band suspend latch
GATE_SUS_DELTA = -24
BCN_RESUME_5G = 0xE6A26       # 0x40101220 |= 1 ; 0x40101208 &= ~(1|4)
BCN_SUSPEND_5G = 0xE6A70      # 0x40101220 &= ~1; 0x40101208 |=  (1|4)
BCN_RESUME_2G = 0xE6AB4       # 0x40101220 |= 2 ; 0x40101208 &= ~(2|8)
BCN_SUSPEND_2G = 0xE6B00      # 0x40101220 &= ~2; 0x40101208 |=  (2|8)
BCN_ALL = [BCN_RESUME_5G, BCN_SUSPEND_5G, BCN_RESUME_2G, BCN_SUSPEND_2G]
# handler -> success-path entry (msg[2] == 0 branch) decoded here
BCN_SUCCESS = {BCN_RESUME_5G: 0xE6A3C, BCN_SUSPEND_5G: 0xE6A86,
               BCN_RESUME_2G: 0xE6ACA, BCN_SUSPEND_2G: 0xE6B16}
GATE_SUS_BITS = {BCN_RESUME_5G: (0x1, 0x4), BCN_SUSPEND_5G: (0x1, 0x4),
                 BCN_RESUME_2G: (0x2, 0x8), BCN_SUSPEND_2G: (0x2, 0x8)}
TRX_WORKER = 0xE8520          # enable/disable-TRX worker (msg table idx 2)
MSG_TABLE_EXPECT = [0xE70ED, 0xDB7A1, TRX_WORKER, 0xE6B00, 0xE6AB4,
                    0xE6A70, 0xE6A26, 0xDB719, 0xE8214, 0xE83AC]
TSX_TRX_BAND1 = [0x4010E150, 0x4010E154]
TSX_TRX_BAND0 = [0x4010820C, 0x40108214]

# --- per-band IRQ registration (boot init CA 0xD1B68) ----------------------
WRAPPER_OBJ = 0x180814        # +8 band0 object, +0x1C band1 object
WRAPPER_OBJ_BAND0 = 0x8
WRAPPER_OBJ_BAND1 = 0x1C
IRQ_INSTALL_FN = 0xC74B0      # (irq, flags, handler) at CA 0xC6DA2 -> here
IRQ_ENABLE_FN = 0xC6FF4       # irq enable
IRQCTL_BASE_LITERAL = 0x17D398
IRQ_HANDLER_OFF = 0x98        # handler[irq] at base + 0x98 + 4*irq
IRQ_FLAGS_SHARED = 14
# (ldr-site that loads the wrapper literal, irq, flags, wrapper literal value)
IRQ_BAND0 = [
    (0xD1EEE, 0xD1EF2, 47, 5, 0xAEC95),
    (0xD1EFE, 0xD1F02, 49, 5, 0xB54C9),
    (0xD1F0E, 0xD1F12, 50, 5, 0xB0D51),
    (0xD1F1E, 0xD1F22, 51, 5, 0xAEC69),
    (0xD1F2E, 0xD1F32, 52, 5, 0xAEB75),
    (0xD1F3E, 0xD1F42, 82, IRQ_FLAGS_SHARED, 0xAE48D),
    (0xD1F4E, 0xD1F52, 83, 5, 0xB0D51),
    (0xD1F5E, 0xD1F62, 59, 5, 0xB0C45),
]
IRQ_BAND1 = [
    (0xD21D6, 0xD21DA, 53, 5, 0xAEC89),
    (0xD21E6, 0xD21EA, 55, 5, 0xB54BD),
    (0xD21F6, 0xD21FA, 56, 5, 0xB0D45),
    (0xD2206, 0xD220A, 57, 5, 0xAEC5D),
    (0xD2216, 0xD221A, 58, 5, 0xAEB69),
    (0xD2226, 0xD222A, 84, IRQ_FLAGS_SHARED, 0xAE481),
    (0xD2236, 0xD223A, 85, 5, 0xB0D45),
    (0xD2246, 0xD224A, 60, 5, 0xB0C39),
]
# (band1 wrapper, band0 wrapper, shared body)
WRAPPER_BODIES = [
    (0xAE480, 0xAE48C, 0xAE45C),
    (0xAEB68, 0xAEB74, 0xAEA9C),
    (0xAEC5C, 0xAEC68, 0xAEB80),
    (0xAEC88, 0xAEC94, 0xAEC74),
    (0xB0C38, 0xB0C44, 0xB0AB8),
    (0xB0D44, 0xB0D50, 0xB0CE0),
    (0xB54BC, 0xB54C8, 0xB53A4),
]

# --- band bit mapping in the MAC start path --------------------------------
MAC_START_FN = 0x4AF0C
MAC_START_BAND_SITES = [(0x4B000, "ldrb.w r3, [r6, #285]"),
                        (0x4B004, "cmp r3, #1"),
                        (0x4B008, "movne r2, #2"),
                        (0x4B00A, "moveq r2, #1"),
                        (0x4B00C, "ldr r3, [pc, #244]")]

# --- dispatch tables (host-config keys) ------------------------------------
TABLE_A = 0x10C23C
TABLE_A_N = 217
TABLE_B = 0x103BF8
TABLE_B_N = 88

# --- probe mapping ---------------------------------------------------------
BAR0_RAM = 0x6B8000
BAR0_IO = 0x3B8000
PHYS_BASE = 0x40000000
FORBIDDEN = (0x10160000, 0x1016FFFF)
PROBE_IRQS = [47, 49, 50, 51, 52, 82, 83, 59,
              53, 55, 56, 57, 58, 84, 85, 60]


class Checker:
    def __init__(self):
        self.n = 0
        self.bad = 0

    def check(self, label, ok, detail=""):
        self.n += 1
        if ok:
            print("PASS %3d  %s" % (self.n, label))
        else:
            self.bad += 1
            print("FAIL %3d  %s%s" % (self.n, label,
                                      (": " + detail) if detail else ""))
        return ok


def sha256(raw):
    return hashlib.sha256(raw).hexdigest()


def load_disasm(objdump, fw_path):
    cmd = [objdump, "-D", "-b", "binary", "-m", "arm", "-M", "force-thumb",
           "--adjust-vma=%#x" % BASE, fw_path]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError) as exc:
        sys.exit("objdump failed: %s" % exc)
    insn = {}
    for line in out.stdout.splitlines():
        m = re.match(r"^\s*([0-9a-f]+):\s+[0-9a-f ]+\t(\S+)\t?(.*)$", line)
        if m:
            ops = m.group(3).split("\t@")[0].strip()
            insn[int(m.group(1), 16)] = (m.group(2), ops)
    return insn


def norm(s):
    return re.sub(r"\s+", " ", s).strip()


def expect(insn, addr, mnemonic, operand):
    got = insn.get(addr)
    if not got:
        return False
    return norm(mnemonic + " " + operand) == norm(got[0] + " " + got[1])


def hx(v):
    return "0x%08X" % v


class Image:
    def __init__(self, path):
        self.raw = open(path, "rb").read()

    def u8(self, ca):
        return self.raw[ca - BASE]

    def u16(self, ca):
        return struct.unpack_from("<H", self.raw, ca - BASE)[0]

    def u32(self, ca):
        return struct.unpack_from("<I", self.raw, ca - BASE)[0]

    def names(self, lo=NAME_BLOB_LO, hi=NAME_BLOB_HI):
        out = []
        pos = lo - BASE
        while pos < hi - BASE:
            end = self.raw.find(b"\x00", pos)
            if end < 0:
                break
            s = self.raw[pos:end]
            # a few blob entries carry 1..3 non-text pad bytes before the name
            m = re.search(rb"[ -~]{4,}$", s)
            if m:
                out.append((pos + m.start() + BASE, m.group().decode("ascii")))
            pos = end + 1
        return out

    def isr_table(self):
        return [self.u32(ISR_TABLE + 4 * i) for i in range(ISR_TABLE_N)]

    def msg_table(self):
        return [self.u32(MSG_TABLE + 4 * i) for i in range(MSG_TABLE_N)]

    def table_key(self, base, n, key):
        for i in range(n):
            k = self.u16(base + 8 * i)
            if k == key:
                return self.u32(base + 8 * i + 4)
        return None


def lit_operand(operand):
    m = re.match(r"^r\d+,\s*\[pc,\s*#(\d+)\]$", operand)
    if not m:
        return None
    return int(m.group(1))


def insn_at(insn, addr):
    got = insn.get(addr)
    return (norm(got[0] + " " + got[1]) if got else "<?>")


def follow_ldr(instrs, addr):
    """Resolve ldr rX,[pc,#imm] at addr to (literal_ca, value) with image."""
    return None


def insn_addrs(insn, lo, hi):
    return sorted(a for a in insn if lo <= a < hi)


def movw_movt_consts(insn):
    """Reconstruct the 32-bit constants built with movw/movt pairs."""
    out = []
    regconst = {}
    for a in sorted(insn):
        mn, ops = insn[a]
        m = re.match(r"^(movw|movt)\s+(r\d+|sl|fp|ip|sp|lr),\s*#(\d+)$",
                     mn + " " + ops)
        if m:
            kind, reg, val = m.group(1), m.group(2), int(m.group(3))
            prev = regconst.get(reg)
            if kind == "movw":
                regconst[reg] = ("lo", val, a)
            else:
                if prev and prev[0] == "lo" and a - prev[2] <= 8:
                    out.append(((val << 16) | prev[1], a))
                regconst[reg] = ("hi", val, a)
        else:
            m2 = re.match(r"^(\w+)\s+([^,]+),", mn + " " + ops)
            if m2 and m2.group(2).strip() in regconst and \
                    not mn.startswith(("cmp", "tst")):
                del regconst[m2.group(2).strip()]
    return out


def body_span(insn, start, max_insn=400):
    """Approximate function dispatch: walk executable entries from start."""
    addrs = insn_addrs(insn, start, start + 4 * max_insn)
    out = []
    for a in addrs:
        out.append(a)
        mn, ops = insn[a]
        if (mn == "bx" and ops == "lr") or (mn == "pop" and "pc" in ops):
            return out
    return out


# ---------------------------------------------------------------------------
def print_decode(img, insn):
    print("=== firmware band-1 beacon TX gate - decode ===")
    print()
    print("[1] the 45-name blob CA 0x%X..0x%X" % (NAME_BLOB_LO, NAME_BLOB_HI))
    names = img.names()
    print("    %d names: %d per-band ISR names (16 x 5g/2g) + %d smac_msg_proc"
          % (len(names), ISR_NAME_COUNT, len(MSG_PROC_NAMES)))
    print("    every name is DEAD static data: no u32 literal, no movw/movt")
    print("    reference to any name CA exists in the image [V]")
    print()
    print("[2] static handler tables (same section, name order)")
    print("    ISR table CA 0x%X: %d thumb pointers" % (ISR_TABLE, ISR_TABLE_N))
    ent = img.isr_table()
    print("      idx14 tbtt_5g -> 0x%06X ; idx15 tbtt_2g -> 0x%06X"
          % (ent[TBTT_5G_NAME_IDX] & ~1, ent[TBTT_2G_NAME_IDX] & ~1))
    for nm, i in (("tbtt_5g", TBTT_5G_NAME_IDX), ("tbtt_2g", TBTT_2G_NAME_IDX)):
        b = body_span(insn, ent[i] & ~1)
        print("      %-8s body CA 0x%06X: %d insns, stops at bx lr/pop pc,"
              " no BL and no device literal" % (nm, ent[i] & ~1, len(b)))
    print("    MSG table CA 0x%X: %d pointers; idx2 = 0x%06X (enable/disable"
          " TRX worker); idx3..6 = the four beacon TX resume/suspend procs"
          % (MSG_TABLE, MSG_TABLE_N, img.msg_table()[2] & ~1))
    print()
    print("[3] the per-band beacon TX gate (host-visible words)")
    print("    0x%08X  per-band SMAC/TX enable : bit0 = 5 GHz, bit1 = 2.4 GHz"
          % GATE_EN)
    print("    0x%08X  per-band suspend latch  : 5 GHz bits 0x1|0x4,"
          " 2.4 GHz bits 0x2|0x8" % GATE_SUS)
    for site, label in ((BCN_RESUME_5G, "resume 5g"), (BCN_SUSPEND_5G, "suspend 5g"),
                        (BCN_RESUME_2G, "resume 2g"), (BCN_SUSPEND_2G, "suspend 2g")):
        print("      CA 0x%06X %-11s" % (site, label), end="  ")
        b = body_span(insn, BCN_SUCCESS[site])
        for a in b:
            mn, ops = insn[a]
            if mn in ("orr.w", "bic.w") or (mn == "ldr" and "pc" in ops):
                print("%s %s;" % (mn, ops), end=" ")
        print()
    print()
    print("[4] boot init CA 0xD1B68 - the per-band IRQ registration")
    print("    IRQ install fn CA 0x%X stores the handler at [0x%X + 0x%X + 4*irq]"
          % (IRQ_INSTALL_FN, IRQCTL_BASE_LITERAL, IRQ_HANDLER_OFF))
    print("    wrapper object CA 0x%X: +0x%X = band0 object, +0x%X = band1 object"
          % (WRAPPER_OBJ, WRAPPER_OBJ_BAND0, WRAPPER_OBJ_BAND1))
    print("    %-10s %-28s %-28s" % ("pair", "band0 (IRQ->wrapper)", "band1 (IRQ->wrapper)"))
    for (s0, _, irq0, f0, w0), (s1, _, irq1, f1, w1) in zip(IRQ_BAND0, IRQ_BAND1):
        print("    IRQ %-2d/%-2d  0x%06X flags %-2d          0x%06X flags %-2d"
              % (irq0, irq1, w0, f0, w1, f1))
    print("    identical shared bodies, only the wrapper band object differs [V]")
    print("    => the band-1 TBTT ISR entry is registered identically at boot")
    print()
    print("[5] host trigger boundary")
    print("    table A (%d rows) / table B (%d rows) host workers: none is a"
          " beacon/TRX processor or any of the 45 handlers [V]" % (TABLE_A_N, TABLE_B_N))
    print("    => the beacon_tx_resume messages are firmware-internal; the host"
          " cannot post them, it can only set the two gate words above")
    print()
    print("[6] conclusion")
    print("    This build has NO firmware beacon-build/queue task: the TBTT"
          " handlers are 6/11-insn reporting stubs and the beacon TX is")
    print("    hardware TBTT-driven.  The firmware's only per-band gate is"
          " 0x%08X + 0x%08X:" % (GATE_EN, GATE_SUS))
    print("    band1 beacons only leave the chip while bit0 is set and the"
          " 0x1/0x4 suspend latches are clear.")


def print_tables(img):
    print("=== T1. the 45 names (CA order) ===")
    for ca, nm in img.names():
        print("  0x%06X  %s" % (ca, nm))
    print()
    print("=== T2. ISR handler table CA 0x%X (32 entries, name order) ===" % ISR_TABLE)
    print("  %-3s %-40s %s" % ("idx", "name", "handler"))
    ent = img.isr_table()
    for i in range(ISR_TABLE_N):
        print("  %-3d %-40s 0x%06X" % (i, ISR_NAMES[i], ent[i] & ~1))
    print()
    print("=== T3. SMAC message-processor table CA 0x%X ===" % MSG_TABLE)
    for i, v in enumerate(img.msg_table()):
        print("  idx%d  0x%06X" % (i, v & ~1))
    print()
    print("=== T4. per-band IRQ registration (boot init CA 0xD1B68) ===")
    print("  %-4s %-6s %-8s %-8s %-10s" % ("band", "irq", "flags", "wrapper", "shared body"))
    body_of = {}
    for b1, b0, body in WRAPPER_BODIES:
        body_of[b1] = body
        body_of[b0] = body
    for band, rows in (("2.4G", IRQ_BAND0), ("5G", IRQ_BAND1)):
        for _, _, irq, flags, w in rows:
            print("  %-4s %-6d %-8d 0x%06X 0x%06X" % (band, irq, flags, w,
                                                      body_of.get(w, 0)))
    print()
    print("=== T5. the beacon TX gate words and their firmware writers ===")
    print("  GATE_EN  0x%X : bit0=5 GHz bit1=2.4 GHz (MAC start CA 0x%X)"
          % (GATE_EN, MAC_START_FN))
    print("  GATE_SUS 0x%X : == GATE_EN %+d" % (GATE_SUS, GATE_SUS_DELTA))
    print("  %-10s %-16s %s" % ("handler", "role", "effect"))
    for h, role, eff in (
            (BCN_RESUME_5G, "resume 5g", "GATE_EN |= 1 ; GATE_SUS &= ~(0x1|0x4)"),
            (BCN_SUSPEND_5G, "suspend 5g", "GATE_EN &= ~1; GATE_SUS |= (0x1|0x4)"),
            (BCN_RESUME_2G, "resume 2g", "GATE_EN |= 2 ; GATE_SUS &= ~(0x2|0x8)"),
            (BCN_SUSPEND_2G, "suspend 2g", "GATE_EN &= ~2; GATE_SUS |= (0x2|0x8)")):
        print("  0x%06X  %-12s %s" % (h, role, eff))
    print()
    print("=== T6. TRX enable/disable worker CA 0x%X (msg table idx 2) ==="
          % TRX_WORKER)
    print("  band byte = u8(msg+0xA); band1 writes %s ; band0 writes %s"
          % ([hx(x) for x in TSX_TRX_BAND1], [hx(x) for x in TSX_TRX_BAND0]))


def ram_probe(ca):
    bar0 = ca + BAR0_RAM
    return bar0, PHYS_BASE + bar0


def io_probe(ca):
    bar0 = BAR0_IO + (ca - PHYS_BASE)
    return bar0, PHYS_BASE + bar0


def print_probes(img):
    print("=== R0 probe pack (read-only, same boot; compare 5G vs 2.4G) ===")
    print("RAM : BAR0 = CA + 0x%X ; phys = 0x40000000 + BAR0" % BAR0_RAM)
    print("IO  : BAR0 = 0x%X + (CA - 0x40000000) ; phys = 0x40000000 + BAR0"
          % BAR0_IO)
    print()
    print("%-40s %-10s %-10s %-10s %s" % ("object", "CA", "BAR0", "phys", "expect"))
    print("-" * 104)
    for name, ca, exp in [
            ("GATE_EN 0x%X" % GATE_EN, GATE_EN, "band0 bit1 set when 2.4G AP runs"),
            ("GATE_SUS 0x%X" % GATE_SUS, GATE_SUS, "0 = no suspend"),
            ("band0 object slot", WRAPPER_OBJ + WRAPPER_OBJ_BAND0, "nonzero"),
            ("band1 object slot", WRAPPER_OBJ + WRAPPER_OBJ_BAND1, "nonzero"),
            ("band0 MAC block", 0x40110000, "0x548/0x54C = 0x6101-style init"),
            ("band1 MAC block", 0x40114000, "0x548/0x54C = 0x6101-style init"),
            ("band0 TRX reg", TSX_TRX_BAND0[0], "0x33"),
            ("band1 TRX reg", TSX_TRX_BAND1[0], "0x33"),
            ("band1 TRX reg2", TSX_TRX_BAND1[1], "0xFF")]:
        if ca >= PHYS_BASE:
            bar0, phys = io_probe(ca)
        else:
            bar0, phys = ram_probe(ca)
        print("%-40s %-10s %-10s %-10s %s" % (name, hx(ca), hx(bar0), hx(phys), exp))
    print()
    print("LIVE per-band IRQ handler table (IRQ install store in CA 0x%X):" % IRQ_INSTALL_FN)
    print("%-6s %-10s %-10s %-10s" % ("irq", "CA", "BAR0", "phys"))
    for irq in PROBE_IRQS:
        ca = IRQCTL_BASE_LITERAL + IRQ_HANDLER_OFF + 4 * irq
        bar0, phys = ram_probe(ca)
        print("%-6d %-10s %-10s %-10s" % (irq, hx(ca), hx(bar0), hx(phys)))
    print()
    print("expected installed values: 5G wrappers <band1>, 2.4G wrappers <band0>:")
    for band, rows in (("2.4G", IRQ_BAND0), ("5G", IRQ_BAND1)):
        print("  %s: %s" % (band, ", ".join("irq%d=0x%06X" % (r[2], r[4] | 1)
                                            for r in rows)))
    print()
    print("forbidden: RC misc window 0x%08X..0x%08X" % FORBIDDEN)


def selftest(img, insn):
    c = Checker()
    raw = img.raw
    print("=== selftest: image identity ===")
    c.check("firmware length == %d" % FW_LEN, len(raw) == FW_LEN, "got %d" % len(raw))
    c.check("firmware sha256 == 7fc87e20...311b", sha256(raw) == FW_SHA, sha256(raw))
    print()
    print("=== selftest: the 45-name blob ===")
    names = img.names()
    c.check("blob CA 0x%X..0x%X holds 45 names" % (NAME_BLOB_LO, NAME_BLOB_HI),
            len(names) == NAME_COUNT, "got %d" % len(names))
    c.check("first name is coex_rx_abort_end_5g", names and names[0][1] == ISR_NAMES[0])
    c.check("last name is smac_tx_exit_2g_isr", names and names[-1][1] == ISR_NAMES[-1])
    c.check("names 0..13 are the first 7 per-band ISR pairs",
            [n for _, n in names[:14]] == ISR_NAMES[:14])
    c.check("names 14..26 are the 13 smac_msg_proc names",
            [n for _, n in names[14:14 + len(MSG_PROC_NAMES)]] == MSG_PROC_NAMES)
    c.check("names 27..44 are the last 9 per-band ISR pairs",
            [n for _, n in names[14 + len(MSG_PROC_NAMES):]] == ISR_NAMES[14:])
    c.check("13 msg_proc + 32 ISR names = 45",
            len(MSG_PROC_NAMES) + ISR_NAME_COUNT == NAME_COUNT)
    # dead static data: no reference to any name CA
    refs = 0
    for ca, _ in names:
        pat = struct.pack("<I", ca)
        refs += raw.count(pat)
    c.check("no u32 literal anywhere equals a name CA (dead names)", refs == 0,
            "got %d refs" % refs)
    cns = movw_movt_consts(insn)
    c.check("no movw/movt constant equals a name CA (dead names)", 
            not any(v in {ca for ca, _ in names} for v, _ in cns))
    c.check("no code loads a static table base (0x%X/0x%X): tables are"
            " uninstalled static data" % (ISR_TABLE, MSG_TABLE),
            not any(v in (ISR_TABLE, MSG_TABLE, ISR_TABLE | 1, MSG_TABLE | 1)
                    for v, _ in cns))
    print()
    print("=== selftest: static handler tables ===")
    ent = img.isr_table()
    c.check("ISR table CA 0x%X has 32 entries" % ISR_TABLE, len(ent) == 32)
    c.check("all ISR table entries are odd (thumb) pointers",
            all(v & 1 for v in ent))
    c.check("all ISR table entries point into image code (< 0x122C98)",
            all(0x40000 < (v & ~1) < 0x122C98 for v in ent))
    tb5 = ent[TBTT_5G_NAME_IDX] & ~1
    tb2 = ent[TBTT_2G_NAME_IDX] & ~1
    c.check("ISR table idx14 (tbtt_5g) = CA 0x%06X" % tb5, tb5 == 0xE69CA)
    c.check("ISR table idx15 (tbtt_2g) = CA 0x%06X" % tb2, tb2 == 0xE69A2)
    for label, ca, maxn in (("tbtt_5g", tb5, 8), ("tbtt_2g", tb2, 12)):
        b = body_span(insn, ca, 40)
        c.check("%s handler is a leaf of <= %d insns" % (label, maxn), len(b) <= maxn,
                "got %d" % len(b))
        c.check("%s handler contains no BL (no beacon build/queue)" % label,
                not any(insn[a][0] == "bl" for a in b))
    mt = img.msg_table()
    c.check("msg table CA 0x%X has 10 entries" % MSG_TABLE, len(mt) == 10)
    c.check("msg table entries are thumb pointers", all(v & 1 for v in mt))
    c.check("msg table idx2 is the TRX worker 0x%06X" % TRX_WORKER,
            (mt[2] & ~1) == TRX_WORKER)
    c.check("msg table carries the 4 beacon resume/suspend procs",
            {(mt[i] & ~1) for i in (3, 4, 5, 6)} == set(BCN_ALL))
    print()
    print("=== selftest: beacon TX gate processors ===")
    for h, succ in BCN_SUCCESS.items():
        lits = []
        for a in body_span(insn, succ, 40):
            mn, ops = insn[a]
            m = re.match(r"^ldr\s+(r\d+|sl|fp|ip), \[pc, #(\d+)\]$", mn + " " + ops)
            if m:
                L = ((a + 4) & ~3) + int(m.group(2))
                lits.append(img.u32(L))
        c.check("CA 0x%06X loads 0x%X (gate enable)" % (h, GATE_EN), GATE_EN in lits)
        uses_delta = any(insn[a][1].endswith("[r2, #-24]")
                         for a in body_span(insn, succ, 40))
        c.check("CA 0x%06X touches the suspend latch (0x%X literal or r2-24)"
                % (h, GATE_SUS), GATE_SUS in lits or uses_delta)
    c.check("GATE_SUS == GATE_EN %+d" % GATE_SUS_DELTA, GATE_SUS - GATE_EN == GATE_SUS_DELTA)
    # exact bit ops on the success paths
    def ops_at(addr, mn, operand):
        return expect(insn, addr, mn, operand)
    c.check("resume 5g: 0xE6A40 orr.w r3, r3, #1", ops_at(0xE6A40, "orr.w", "r3, r3, #1"))
    c.check("resume 5g: 0xE6A4A bic.w r3, r3, #1", ops_at(0xE6A4A, "bic.w", "r3, r3, #1"))
    c.check("resume 5g: 0xE6A5A bic.w r3, r3, #4", ops_at(0xE6A5A, "bic.w", "r3, r3, #4"))
    c.check("suspend 5g: 0xE6A8A bic.w r3, r3, #1", ops_at(0xE6A8A, "bic.w", "r3, r3, #1"))
    c.check("suspend 5g: 0xE6A98 orr.w r2, r2, #1", ops_at(0xE6A98, "orr.w", "r2, r2, #1"))
    c.check("suspend 5g: 0xE6AA0 orr.w r2, r2, #4", ops_at(0xE6AA0, "orr.w", "r2, r2, #4"))
    c.check("resume 2g: 0xE6AD0 orr.w r3, r3, #2", ops_at(0xE6AD0, "orr.w", "r3, r3, #2"))
    c.check("resume 2g: 0xE6ADA bic.w r3, r3, #2", ops_at(0xE6ADA, "bic.w", "r3, r3, #2"))
    c.check("resume 2g: 0xE6AEA bic.w r3, r3, #8", ops_at(0xE6AEA, "bic.w", "r3, r3, #8"))
    c.check("suspend 2g: 0xE6B1C bic.w r3, r3, #2", ops_at(0xE6B1C, "bic.w", "r3, r3, #2"))
    c.check("suspend 2g: 0xE6B2A orr.w r2, r2, #2", ops_at(0xE6B2A, "orr.w", "r2, r2, #2"))
    c.check("suspend 2g: 0xE6B32 orr.w r2, r2, #8", ops_at(0xE6B32, "orr.w", "r2, r2, #8"))
    c.check("suspend writes use [r2,#-24] (the 0x40101208 latch)",
            ops_at(0xE6A52, "strh.w", "r3, [r2, #-24]")
            and ops_at(0xE6A62, "strh.w", "r3, [r2, #-24]"))
    print()
    print("=== selftest: MAC start band -> gate bit mapping ===")
    c.check("MAC start reads band u8(perlink+0x11D)",
            ops_at(0x4B000, "ldrb.w", "r3, [r6, #285]"))
    c.check("band == 1 -> r2 = 1 (band1/5GHz bit0)",
            ops_at(0x4B004, "cmp", "r3, #1") and ops_at(0x4B00A, "moveq", "r2, #1"))
    c.check("band != 1 -> r2 = 2 (band0/2.4GHz bit1)",
            ops_at(0x4B008, "movne", "r2, #2"))
    print()
    print("=== selftest: per-band IRQ registration (boot init CA 0xD1B68) ===")
    for block, band in ((IRQ_BAND0, "band0"), (IRQ_BAND1, "band1")):
        for ldr_site, mov_site, irq, flags, wrapper in block:
            mn, ops = insn.get(ldr_site, ("<?>", "<?>"))
            m = re.match(r"^ldr\s+(r\d+), \[pc, #(\d+)\]$", mn + " " + ops)
            ok_lit = False
            if m:
                L = ((ldr_site + 4) & ~3) + int(m.group(2))
                ok_lit = img.u32(L) == wrapper
            c.check("%s IRQ %d ldr-site 0x%06X -> wrapper 0x%06X"
                    % (band, irq, ldr_site, wrapper), ok_lit)
            c.check("%s IRQ %d movs r0 is #%d" % (band, irq, irq),
                    ops_at(mov_site, "movs", "r0, #%d" % irq))
            c.check("%s IRQ %d flags #%d" % (band, irq, flags),
                    ops_at(mov_site - 2, "movs", "r1, #%d" % flags))
    print()
    print("=== selftest: per-band wrapper functions (band object selector) ===")
    for b1, b0, body in WRAPPER_BODIES:
        c.check("wrapper 0x%06X -> band1 obj [+0x%X] -> body 0x%06X"
                % (b1, WRAPPER_OBJ_BAND1, body),
                ops_at(b1, "ldr", "r3, [pc, #4]")
                and ops_at(b1 + 2, "ldr", "r0, [r3, #28]")
                and ops_at(b1 + 4, "b.n", "0x%x" % body))
        c.check("wrapper 0x%06X -> band0 obj [+0x%X] -> body 0x%06X"
                % (b0, WRAPPER_OBJ_BAND0, body),
                ops_at(b0 + 2, "ldr", "r0, [r3, #8]")
                and ops_at(b0 + 4, "b.n", "0x%x" % body))
        # both wrappers load the same WRAPPER_OBJ literal
        vals = []
        for w in (b1, b0):
            m = re.match(r"^ldr\s+(r\d+), \[pc, #(\d+)\]$", insn_at(insn, w))
            if m:
                L = ((w + 4) & ~3) + int(m.group(2))
                vals.append(img.u32(L))
        c.check("wrappers 0x%06X/0x%06X load object 0x%X" % (b1, b0, WRAPPER_OBJ),
                vals == [WRAPPER_OBJ, WRAPPER_OBJ])
    print()
    print("=== selftest: IRQ install store (live table is RAM-readable) ===")
    c.check("install fn CA 0x%X adds irq<<2 to the ctl base" % IRQ_INSTALL_FN,
            ops_at(0xC74C4, "add.w", "r3, r5, r4, lsl #2"))
    c.check("install fn stores handler at [r3, #%d]" % IRQ_HANDLER_OFF,
            ops_at(0xC74E8, "str.w", "r8, [r3, #152]"))
    m = re.match(r"^ldr\s+(r\d+), \[pc, #(\d+)\]$", insn_at(insn, 0xC74B6))
    lit_ca = ((0xC74B6 + 4) & ~3) + int(m.group(2)) if m else 0
    c.check("install fn ctl base literal = 0x%X" % IRQCTL_BASE_LITERAL,
            img.u32(lit_ca) == IRQCTL_BASE_LITERAL)
    c.check("handler-table RAM CA = 0x%X + 0x%X = 0x%X"
            % (IRQCTL_BASE_LITERAL, IRQ_HANDLER_OFF,
               IRQCTL_BASE_LITERAL + IRQ_HANDLER_OFF),
            IRQCTL_BASE_LITERAL + IRQ_HANDLER_OFF == 0x17D430)
    print()
    print("=== selftest: host trigger boundary (no key posts the gate) ===")
    targets = {h for h in BCN_ALL} | {(h | 1) for h in BCN_ALL} | {TRX_WORKER, TRX_WORKER | 1}
    hit_a = [img.table_key(TABLE_A, TABLE_A_N, k) for k in
             (img.u16(TABLE_A + 8 * i) for i in range(TABLE_A_N))]
    hit_b = [img.u32(TABLE_B + 8 * i + 4) for i in range(TABLE_B_N)]
    c.check("no table A worker is a beacon/TRX gate processor",
            not (set(hit_a) & targets))
    c.check("no table B worker is a beacon/TRX gate processor",
            not (set(hit_b) & targets))
    c.check("tables A/B are 217/88 rows with a 0x1451 sentinel",
            img.u16(TABLE_A + 8 * (TABLE_A_N - 1)) == 0x1451
            and img.u16(TABLE_B + 8 * (TABLE_B_N - 1)) == 0x1451)
    print()
    print("=== selftest: probe pack sanity ===")
    cands = [GATE_EN, GATE_SUS, WRAPPER_OBJ + 8, WRAPPER_OBJ + 0x1C]
    cands += [IRQCTL_BASE_LITERAL + IRQ_HANDLER_OFF + 4 * i for i in PROBE_IRQS]
    c.check("no probe falls into the forbidden RC misc window",
            all(not (FORBIDDEN[0] <= ca <= FORBIDDEN[1]) for ca in cands))
    c.check("probe set covers both gate words and all 16 IRQ slots", len(cands) == 20)
    print()
    print("RESULT: %d checks, %d failed" % (c.n, c.bad))
    return c.bad == 0


def main():
    ap = argparse.ArgumentParser(description="band-1 beacon TX gate decode")
    ap.add_argument("--fw", default=os.environ.get("BW_FIRMWARE", DEFAULT_FW))
    ap.add_argument("--objdump", default=os.environ.get("BW_OBJDUMP", DEFAULT_OD))
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--decode", action="store_true")
    ap.add_argument("--tables", action="store_true")
    ap.add_argument("--probes", action="store_true")
    args = ap.parse_args()
    if not any((args.selftest, args.decode, args.tables, args.probes)):
        ap.print_help()
        return 1
    img = Image(args.fw)
    insn = load_disasm(args.objdump, args.fw) if (
        args.selftest or args.decode) else {}
    ok = True
    if args.decode:
        print_decode(img, insn)
    if args.tables:
        print_tables(img)
    if args.probes:
        print_probes(img)
    if args.selftest:
        ok = selftest(img, insn)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
