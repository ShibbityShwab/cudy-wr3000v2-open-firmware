#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_vap_start_gates_re.py - settle the key-0x0065 G9/G11 gate semantics.

Lane: wifi-vap-start-gates (shared task-212).  HOST-ONLY, read-only: the
pinned LOCAL vendor artifacts are read in place and only derived values
(addresses, decoded mnemonics, table values) are printed.  No vendor byte is
copied into the repository and no device is touched.

Artifacts (local, uncommitted):
  firmware: /home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin
            928,920 B sha256 7fc87e20...311b ; CPU = file offset + 0x40000
  module:   /home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko
            3,581,748 B sha256 22fa789a...c7d2, unstripped (symbols are
            .text-relative link-time offsets; objdump resolves them)
  objdump:  the OpenWrt lab ARM objdump (--objdump or $VSG_OBJDUMP)

The decode (evidence: ../REPORT.md, ../TABLES.md):
  * the g0-id1 table A CA 0x10C23C row 2 is key 0x0065 -> worker CA 0x46394;
  * worker CA 0x46394 requires only: CA 0x961AE(mctx[0]) != 0 and
    mctx[0x4B8] != 0; then stores body[2]->mctx[0x11], body[3]->mctx[0x12],
    body[4..7]->mctx[0x444], sets mctx[0x0E]=1 for the AP type (2) and calls
    CA 0xD4048/0x94518/0xBD114/D41AC/F593C ... ; it NEVER reads mctx[0x440]
    or bandobj[0xEC8];
  * the CA 0x944DE gate that opens the 5 GHz RF programme (CA 0x40684 ->
    0x101890) returns 1 only when mctx[0x11]==1 && mctx[0x440]==1 &&
    bandobj[0xEC8]==1: those bytes are the RF-enable / band-up status,
    1 = normal; the drivers on-board 0x00 gates (G9/G11) were wrong;
  * writers: CA 0x455D0 (mctx[0x440] <- bandobj[0x19A1], band/config apply),
    CA 0x7100C (mctx[0x440..] <- a config block), CA 0x48D26
    (bandobj[0xEC8] <- 0x01000101, band bring-up), CA 0xB6C44 (band up/down);
  * the stock emitter hmac_config_start_vap_event (wifi.ko .text+0x15AF88,
    key 0x0065) is called from hmac_start_bss_in_available_channel
    (.text+0x48204, call site 0x4826C), which is the BSS-start/channel-switch
    consumer: the 0x0124 channel config (hmac_config_set_channel, key 0x124
    at 0x76744) and the AP config transaction precede it (write_msg:
    AP cfg event -> wal_start_vap), i.e. start_vap AFTER the channel config.

Modes:
  --decode    print the full decode (default)
  --tables    print the key table + the writer/reader map
  --selftest  self-checking assertions (exit 0 on PASS, 1 on FAIL)
"""
import argparse
import hashlib
import os
import re
import struct
import subprocess
import sys

BASE = 0x40000                 # firmware CPU = file offset + BASE
DEFAULT_FW = ("/home/shibbityshwab/recovery-extract/lib/firmware/"
              "hi_wifi/FIRMWARE.bin")
FW_SHA = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_LEN = 928920
DEFAULT_KO = ("/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/"
              "hi5622v100_wifi.ko")
KO_SHA = "22fa789a3d431f49f9c52c76c402e5740d1d14118b5e47a347f2cc247532c7d2"
KO_LEN = 3581748
DEFAULT_OD = ("/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/"
              "toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/"
              "arm-openwrt-linux-objdump")

# ---- firmware CPU addresses (derived in this lane) ---------------------
TABLE_A = 0x10C23C             # 217 rows x 8 B {u16 key, u16 ?, u32 worker|1}
TABLE_B = 0x103BF8             # 88 rows x 8 B
KEY_VAP_ADD = 0x0064           # table A row 1  -> CA 0x449E4 (thumb 0x449E5)
KEY_VAP_START = 0x0065         # table A row 2  -> CA 0x46394 (thumb 0x46395)
KEY_BAND_START = 0x0242        # table A row 76 -> CA 0x4C524 (thumb 0x4C525)
KEY_CHAN_CFG = 0x0124          # table A row 108 -> CA 0x841B8 (thumb 0x841B9)
WORKER_VAP_START = 0x46394     # key 0x0065 worker (end 0x467C8)
WORKER_VAP_START_END = 0x467C8
C_961AE = 0x961AE              # owner/user-id -> module ctx lookup
C_D4048 = 0xD4048              # start helper (arg = body[1])
C_94518 = 0x94518              # MAC VAP start
C_BD114 = 0xBD114              # AP context-buffer path, arg mctx[0x190]
C_D41AC = 0xD41AC              # publish helper
C_F593C = 0xF593C              # arg (mctx[0x4AC], type)
C_FDCE4 = 0xFDCE4              # VAP band-class (only when mctx[0x0F]==10)
C_5E95C = 0x5E95C              # VAP publish helper (gated by mctx[0x4A0])
C_971D8 = 0x971D8              # band start / VAP-on-band hook
C_5F450 = 0x5F450              # post-start helper; 0 -> pre-start tail
C_4E4DC = 0x4E4DC              # (module, 19, 0) pre-start notify
C_4C3FC = 0x4C3FC              # 0x0242 pre-start: state[0]bit0, 0xD6E, 0x4AE7C
C_4AE7C = 0x4AE7C              # band VAP resolver: count==1 -> CA 0x944DE
C_944DE = 0x944DE              # RF-enable gate: band==1, +0x440==1, +0xEC8==1
C_40684 = 0x40684              # 5 GHz RF branch (needs CA 0x944DE == 1)
C_101890 = 0x101890            # 5 GHz RF descriptor predicate
C_7100C_WRITER = 0x7100C       # mctx[0x440..] <- config block +0x30..
C_455D0_WRITER = 0x455D0       # mctx[0x440] <- bandobj[0x19A1]
C_48D26_WRITER = 0x48D26       # bandobj[0xEC8] <- 0x01000101 (band bring-up)
C_B6C44_WRITER = 0xB6C44       # bandobj[0xEC8] <- parsed band up/down byte
LIT_48DFC = 0x48DFC            # literal stored by CA 0x48D26

# ---- module (.text-relative) addresses --------------------------------
K_HMAC_START_BSS = 0x48204     # hmac_start_bss_in_available_channel
K_EMITTER = 0x15AF88           # hmac_config_start_vap_event (key 0x0065)
K_EMITTER_CALL = 0x4826C       # its call site in K_HMAC_START_BSS
K_CHAN_START_BSS = 0x48358     # hmac_chan_start_bss (tail-b K_HMAC_START_BSS)
K_START_ALL_BSS = 0x13FD88     # hmac_start_all_bss_of_band (calls 0x48358)
K_START_AP = 0x10342C          # wal_cfg80211_start_ap
K_START_AP_WRITE = 0x103340    # wal_cfg80211_start_ap_write_msg
K_WAL_START_VAP = 0x114E10     # wal_start_vap: g2 cmd 0x0065 send
K_CONFIG_SET_CHAN = 0x764E4    # hmac_config_set_channel: key 0x0124 send
K_WAL_SET_CHAN = 0x12AAB0      # wal_config_set_channel (calls 0x764E4)
K_KEY_EMITTER = 0x0065


class Blob:
    def __init__(self, path, want_sha, want_len, label):
        with open(path, "rb") as f:
            self.d = f.read()
        self.path = path
        self.sha = hashlib.sha256(self.d).hexdigest()
        if len(self.d) != want_len:
            raise SystemExit("%s: length %d != pinned %d (%s)"
                             % (label, len(self.d), want_len, path))
        if self.sha != want_sha:
            raise SystemExit("%s: sha256 %s != pinned %s (%s)"
                             % (label, self.sha, want_sha, path))

    def u8(self, ca):
        return self.d[ca - BASE]

    def u16(self, ca):
        return struct.unpack_from("<H", self.d, ca - BASE)[0]

    def u32(self, ca):
        return struct.unpack_from("<I", self.d, ca - BASE)[0]


def find_objdump(arg):
    cand = [arg, os.environ.get("VSG_OBJDUMP"), DEFAULT_OD]
    for c in cand:
        if c and os.path.exists(c):
            return c
    for c in ("arm-openwrt-linux-objdump", "arm-none-eabi-objdump",
              "objdump"):
        p = subprocess.run(["sh", "-c", "command -v %s" % c],
                           capture_output=True, text=True).stdout.strip()
        if p:
            return p
    return None


def fw_disasm(od, fw, start, stop):
    r = subprocess.run([od, "-D", "-b", "binary", "-m", "arm",
                        "-M", "force-thumb", "--adjust-vma=0x40000",
                        "--start-address=%d" % start,
                        "--stop-address=%d" % stop, fw.path],
                       capture_output=True, text=True)
    return parse_disasm(r.stdout)


def ko_disasm(od, ko, start, stop):
    r = subprocess.run([od, "-d", "--start-address=%d" % start,
                        "--stop-address=%d" % stop, ko],
                       capture_output=True, text=True)
    return parse_disasm(r.stdout)


def parse_disasm(text):
    out = []
    for ln in text.splitlines():
        m = re.match(r"^\s*([0-9a-f]+):\s+[0-9a-f ]+\t(.*)$", ln)
        if m:
            out.append((int(m.group(1), 16), m.group(2).strip()))
    return out


def find_ins(ins, addr):
    for a, t in ins:
        if a == addr:
            return t
    return None


def has_ins(ins, sub):
    return any(sub in t for _, t in ins)


def window(ins, start, stop):
    return [(a, t) for a, t in ins if start <= a < stop]


# ---------------------------------------------------------------------------
# the decoded model (pure functions, also used by --selftest)
# ---------------------------------------------------------------------------
def worker_entry(fw):
    """key 0x0065 -> table A row 2 -> worker CA 0x46394 (thumb pointer)."""
    key = fw.u16(TABLE_A + 2 * 8)
    ptr = fw.u32(TABLE_A + 2 * 8 + 4)
    return key, ptr


def rf_enable_gate(mctx_band, mctx_440, bandobj_ec8):
    """CA 0x944DE: returns 1 only when all three are 1 (1 = normal)."""
    return mctx_band == 1 and mctx_440 == 1 and bandobj_ec8 == 1


def old_g9_g11_ok(corrected, mctx_440, bandobj_ec8):
    """The fired driver's G9/G11 required both to be 0 (WRONG)."""
    return (not corrected and mctx_440 == 0 and bandobj_ec8 == 0)


def corrected_g9_g11_ok(mctx_440, bandobj_ec8):
    """The corrected gates require both to be 1 (RF enable / band up)."""
    return mctx_440 == 1 and bandobj_ec8 == 1


def corrected_precheck(p):
    """The corrected G1..G13 order (G9/G11 inverted vs the fired R4b code)."""
    if not p.get("mctx"):
        return -19                      # -ENODEV
    if not p.get("bandobject") or p.get("count") != 1:
        return -2                       # -ENOENT
    if p.get("entry0") != p.get("expected_index"):
        return -17                      # -EEXIST
    if not p.get("published"):
        return -11                      # -EAGAIN
    if not p.get("b190"):
        return -14                      # -EFAULT
    if p.get("type") != 2:
        return -71                      # -EPROTO
    if p.get("band") != 1:
        return -22                      # -EINVAL
    if p.get("b440") != 1:              # G9': RF-enable must be ON (1 = normal)
        return -11                      # -EAGAIN
    if p.get("d6e") != 0:
        return -22                      # -EINVAL
    if p.get("ec8") != 1:               # G11': band must be UP (1 = normal)
        return -11                      # -EAGAIN
    if p.get("state0") != 0 or p.get("state1") != 0 or \
            p.get("state4") not in (0x00, 0xFF):
        return -16                      # -EBUSY
    return 0


# ---------------------------------------------------------------------------
# decode steps
# ---------------------------------------------------------------------------
def decode_firmware(od, fw, out):
    ok = True

    def ck(name, cond, detail=""):
        nonlocal ok
        out.append("%s %s%s" % ("PASS" if cond else "FAIL", name,
                                (": " + detail) if detail else ""))
        ok = ok and cond

    key, ptr = worker_entry(fw)
    ck("table A row 2 is key 0x0065 -> worker 0x46394",
       key == 0x0065 and ptr == (WORKER_VAP_START | 1),
       "key=0x%04x ptr=0x%x" % (key, ptr))
    ck("table A row 1 is key 0x0064 -> CA 0x449E4",
       fw.u16(TABLE_A + 8) == 0x0064 and
       fw.u32(TABLE_A + 12) == (0x449E4 | 1),
       "key=0x%04x ptr=0x%x" % (fw.u16(TABLE_A + 8), fw.u32(TABLE_A + 12)))
    ck("table A row 76 is key 0x0242 -> CA 0x4C524",
       fw.u16(TABLE_A + 76 * 8) == 0x0242 and
       fw.u32(TABLE_A + 76 * 8 + 4) == (0x4C524 | 1),
       "key=0x%04x ptr=0x%x" % (fw.u16(TABLE_A + 76 * 8),
                                fw.u32(TABLE_A + 76 * 8 + 4)))
    ck("table A row 108 is key 0x0124 -> CA 0x841B8",
       fw.u16(TABLE_A + 108 * 8) == 0x0124 and
       fw.u32(TABLE_A + 108 * 8 + 4) == (0x841B8 | 1),
       "key=0x%04x ptr=0x%x" % (fw.u16(TABLE_A + 108 * 8),
                                fw.u32(TABLE_A + 108 * 8 + 4)))

    # ---- the key-0x0065 worker -------------------------------------------
    w = fw_disasm(od, fw, WORKER_VAP_START, WORKER_VAP_START_END)
    ck("worker prologue at CA 0x46394",
       find_ins(w, WORKER_VAP_START) is not None and
       "sp!" in find_ins(w, WORKER_VAP_START),
       str(find_ins(w, WORKER_VAP_START)))
    ck("worker looks up the module ctx by mctx[0] (CA 0x961AE)",
       has_ins(w, "bl\t0x961ae"))
    ck("worker requires mctx[0x4B8] (band object) nonzero",
       has_ins(w, "[r4, #1208]") and has_ins(w, "cmp.w\tsl, #0"))
    ck("worker stores body[2]->mctx[0x11], body[3]->mctx[0x12],"
       " body[4..7]->mctx[0x444]",
       has_ins(w, "[r4, #17]") and has_ins(w, "[r4, #18]") and
       has_ins(w, "[r4, #1092]"))
    ck("worker sets mctx[0x0E]=1 on the AP branch (CA 0x46418-0x4641A)",
       any(a in (0x46418, 0x4641A) and "strb" in t and "[r4, #14]" in t
           for a, t in w))
    ck("worker dereferences mctx[0x190] for the AP branch (CA 0x46470)",
       any(a == 0x46470 and "[r2, #88]" in t for a, t in w))
    ck("worker calls the band start CA 0x971D8 and the pre-start CA 0x4C3FC",
       has_ins(w, "bl\t0x971d8") and has_ins(w, "bl\t0x4c3fc"))
    bad = [(a, t) for a, t in w
           if "#1088" in t or "#3784" in t or "0x440" in t or "0xec8" in t]
    ck("worker NEVER reads mctx[0x440] or bandobj[0xEC8]", not bad,
       "hits=%r" % (bad[:4],))

    # ---- the CA 0x944DE RF-enable gate -----------------------------------
    g = fw_disasm(od, fw, C_944DE, 0x94530)
    ck("CA 0x944DE reads mctx[0x11] == 1", has_ins(g, "[r5, #17]") or
       has_ins(g, "[r5, #0x11]"))
    ck("CA 0x944DE reads mctx[0x440] and compares it to 1",
       any("#1088" in t for _, t in g) and has_ins(g, "cmp\tr3, #1"))
    ck("CA 0x944DE returns bandobj[0xEC8] as the last gate",
       any("#3784" in t and "[r0" in t for _, t in g))
    ck("the 5 GHz RF branch CA 0x40684 calls CA 0x944DE",
       has_ins(fw_disasm(od, fw, C_40684, 0x40700), "bl\t0x944de"))
    ck("the band VAP resolver CA 0x4AE7C calls CA 0x944DE and CA 0x101890",
       has_ins(fw_disasm(od, fw, C_4AE7C, 0x4AF00), "bl\t0x944de") and
       has_ins(fw_disasm(od, fw, C_4AE7C, 0x4AF00), "bl\t0x101890"))

    # ---- the writers -----------------------------------------------------
    ck("CA 0x455D0 writes mctx[0x440]",
       has_ins(fw_disasm(od, fw, 0x455C0, 0x455E0), "strb.w\tr3, [r6, #1088]"))
    ck("CA 0x7100C writes mctx[0x440..] from a config block",
       has_ins(fw_disasm(od, fw, 0x71000, 0x71040), "strb.w\tr3, [r6, #1088]"))
    w48 = fw_disasm(od, fw, C_48D26_WRITER - 8, C_48D26_WRITER + 8)
    ck("CA 0x48D26 writes bandobj[0xEC8]",
       has_ins(w48, "str.w\tr3, [r4, #3784]"))
    ck("CA 0x48D26 stores the literal word 0x01000101",
       fw.u32(LIT_48DFC) == 0x01000101,
       "0x%08x" % fw.u32(LIT_48DFC))
    ck("CA 0xB6C44 writes bandobj[0xEC8] from the band up/down path",
       has_ins(fw_disasm(od, fw, 0xB6C30, 0xB6C50), "strb.w\tr0, [r5, #3784]"))
    return ok


def decode_module(od, ko, out):
    ok = True

    def ck(name, cond, detail=""):
        nonlocal ok
        out.append("%s %s%s" % ("PASS" if cond else "FAIL", name,
                                (": " + detail) if detail else ""))
        ok = ok and cond

    em = ko_disasm(od, ko, K_EMITTER, K_EMITTER + 0xB8)
    ck("hmac_config_start_vap_event @0x15AF88 builds the key-0x0065 body",
       has_ins(em, "mov\tr1, #101") and
       has_ins(em, "[r0, #15]") and has_ins(em, "[r0, #17]") and
       has_ins(em, "[r0, #18]") and has_ins(em, "[r0, #1044]"))
    ck("the emitter calls hmac_config_send_event",
       has_ins(em, "hmac_config_send_event"))
    bss = ko_disasm(od, ko, K_HMAC_START_BSS, K_HMAC_START_BSS + 0x154)
    ck("the start_vap emitter is called from"
       " hmac_start_bss_in_available_channel @0x4826C",
       any(a == K_EMITTER_CALL and "hmac_config_start_vap_event" in t
           for a, t in bss))
    ck("hmac_chan_start_bss tail-calls hmac_start_bss_in_available_channel",
       has_ins(ko_disasm(od, ko, K_CHAN_START_BSS, K_CHAN_START_BSS + 0x9C),
               "48204 <hmac_start_bss_in_available_channel>"))
    ck("hmac_start_all_bss_of_band calls hmac_chan_start_bss",
       has_ins(ko_disasm(od, ko, K_START_ALL_BSS, K_START_ALL_BSS + 0x220),
               "48358 <hmac_chan_start_bss>"))
    ck("hmac_config_set_channel sends key 0x0124 (mov r1,#292 ->"
       " hmac_config_send_event)",
       has_ins(ko_disasm(od, ko, K_CONFIG_SET_CHAN, K_CONFIG_SET_CHAN + 0x868),
               "mov\tr1, #292") and
       has_ins(ko_disasm(od, ko, K_CONFIG_SET_CHAN, K_CONFIG_SET_CHAN + 0x868),
               "hmac_config_send_event"))
    ck("wal_config_set_channel calls hmac_config_set_channel",
       has_ins(ko_disasm(od, ko, K_WAL_SET_CHAN, K_WAL_SET_CHAN + 0xBC),
               "764e4 <hmac_config_set_channel>"))
    wm = ko_disasm(od, ko, K_START_AP_WRITE, K_START_AP_WRITE + 0xEC)
    ck("wal_cfg80211_start_ap_write_msg sends the AP config cfg-event",
       has_ins(wm, "wal_send_cfg_event"))
    ck("wal_cfg80211_start_ap_write_msg sends the AP config BEFORE"
       " wal_start_vap",
       any("wal_send_cfg_event" in t for _, t in wm) and
       any("wal_start_vap" in t for _, t in wm) and
       [a for a, t in wm if "wal_send_cfg_event" in t][0] <
       [a for a, t in wm if "wal_start_vap" in t][0])
    sv = ko_disasm(od, ko, K_WAL_START_VAP, K_WAL_START_VAP + 0x50)
    ck("wal_start_vap sends the g2 cmd 0x0065 frame (mov r3,#101)",
       has_ins(sv, "mov\tr3, #101") and has_ins(sv, "strh\tr3, [sp, #12]"))
    return ok


def print_tables(fw, out):
    out.append("== g0-id1 table A (CA 0x10C23C, 8-B rows), rows used here")
    for row, name in ((1, "0x0064 VAP add"), (2, "0x0065 VAP start"),
                      (76, "0x0242 band start"), (108, "0x0124 channel cfg")):
        out.append("  A[%3d] key=0x%04x worker=0x%05x  %s"
                   % (row, fw.u16(TABLE_A + row * 8),
                      fw.u32(TABLE_A + row * 8 + 4) - 1, name))
    out.append("")
    out.append("== the 0x440 / 0xEC8 writer and reader map")
    out.append("  mctx[0x440]   writer CA 0x455D0  <- bandobj[0x19A1]"
               " (band/config apply)")
    out.append("  mctx[0x440..] writer CA 0x7100C  <- config block +0x30..")
    out.append("  bandobj[0xEC8] writer CA 0x48D26 <- 0x01000101"
               " (band bring-up; literal CA 0x48DFC)")
    out.append("  bandobj[0xEC8] writer CA 0xB6C44 <- parsed band up/down byte")
    out.append("  reader CA 0x944DE: 1 iff mctx[0x11]==1 && mctx[0x440]==1")
    out.append("                        && bandobj[0xEC8]==1")
    out.append("  consumers: CA 0x40684 (5 GHz RF programme),"
               " CA 0x4AE7C (band VAP resolver)")
    out.append("  => 1 = RF programme enabled / band up = NORMAL;"
               " 0 = skipped silently")


def selftest(argv, fw, ko, od):
    out = []
    ok = True
    ok = decode_firmware(od, fw, out) and ok
    ok = decode_module(od, ko, out) and ok

    # the logical model of the gates
    def ck(name, cond):
        nonlocal ok
        out.append("%s %s" % ("PASS" if cond else "FAIL", name))
        ok = ok and cond

    ck("CA 0x944DE model: (band,0x440,0xEC8) = (1,1,1) -> 1",
       rf_enable_gate(1, 1, 1))
    ck("CA 0x944DE model: any 0 -> 0 (RF programme skipped)",
       not rf_enable_gate(1, 0, 1) and not rf_enable_gate(1, 1, 0) and
       not rf_enable_gate(0, 1, 1))
    base = dict(mctx=1, bandobject=2, count=1, entry0=0, expected_index=0,
                published=1, b190=3, type=2, band=1, d6e=0,
                state0=0, state1=0, state4=0x00, b440=1, ec8=1)
    ck("corrected precheck: R4b working state (b440=1, ec8=1) PASSES",
       corrected_precheck(base) == 0)
    bad440 = dict(base, b440=0)
    ck("corrected G9': b440==0 refused -EAGAIN (RF programme skipped)",
       corrected_precheck(bad440) == -11)
    bad_ec8 = dict(base, ec8=0)
    ck("corrected G11': ec8==0 refused -EAGAIN (band not up)",
       corrected_precheck(bad_ec8) == -11)
    ck("the old G9/G11 (==0) would have refused the R4b working state",
       old_g9_g11_ok(False, 1, 1) is False)
    ck("the R4b fire's own refusal is exactly this contradiction",
       corrected_precheck(base) == 0 and
       not old_g9_g11_ok(False, base["b440"], base["ec8"]))

    if argv.verbose:
        for ln in out:
            print(ln)
    npass = sum(1 for ln in out if ln.startswith("PASS"))
    nfail = sum(1 for ln in out if ln.startswith("FAIL"))
    print("fw_vap_start_gates_re: %d checks, %d failures" % (npass, nfail))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--firmware", default=DEFAULT_FW)
    ap.add_argument("--module", default=DEFAULT_KO)
    ap.add_argument("--objdump", default=None)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--tables", action="store_true")
    ap.add_argument("--decode", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    fw = Blob(args.firmware, FW_SHA, FW_LEN, "firmware")
    ko_path = args.module
    ko = None
    if os.path.exists(ko_path):
        with open(ko_path, "rb") as f:
            kd = f.read()
        if len(kd) != KO_LEN or hashlib.sha256(kd).hexdigest() != KO_SHA:
            raise SystemExit("module: pin mismatch (%s)" % ko_path)
        ko = ko_path
    od = find_objdump(args.objdump)
    if od is None:
        raise SystemExit("no ARM objdump found (--objdump or $VSG_OBJDUMP)")
    print("artifacts: firmware sha256 %s (%d B), module sha256 %s (%d B)"
          % (fw.sha, len(fw.d), KO_SHA, KO_LEN))
    print("objdump: %s" % od)
    if args.selftest:
        return selftest(args, fw, ko, od)
    out = []
    if args.tables:
        print_tables(fw, out)
    if not args.tables or args.decode:
        ok = decode_firmware(od, fw, out)
        ok = decode_module(od, ko, out) and ok
        out.append("")
        out.append("RESULT: %s" % ("PASS" if ok else "FAIL"))
    print("\n".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
