#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_desc_pool_re.py - decode the firmware descriptor-pool populator + the ini
beacon-policy path (C4) + the band-1 start/TBTT hooks (C5).

Lane: wifi-fw-desc-pool (shared task task-184).  HOST-ONLY, read-only: the
pinned LOCAL vendor artifacts are read in place; only derived addresses,
register words and flows are printed.  No vendor byte is copied into the
repository and no device is touched.

Artifacts (local, uncommitted):
  firmware: recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin
            928,920 B sha256 7fc87e20...311b ; CPU = file offset + 0x40000
  plat.ko:  recovery-extract/lib/hisilicon/ko/hi5622v100_plat.ko
  wifi.ko:  recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko
  objdump/nm: the OpenWrt lab ARM binutils (override with --objdump/--nm)

Modes: --selftest (assert every decoded window), --decode, --tables, --probes,
       --policy (C4), --tbtt (C5).
"""
import argparse
import hashlib
import os
import re
import struct
import subprocess
import sys

BASE = 0x40000            # firmware CPU = file offset + BASE
BAR0_ADD = 0x6B8000       # BAR0 offset = CPU + BAR0_ADD
PHYS_BASE = 0x40000000    # bar0read physical base
DEFAULT_FW = ("/home/shibbityshwab/recovery-extract/lib/firmware/"
              "hi_wifi/FIRMWARE.bin")
FW_SHA = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_LEN = 928920
DEFAULT_KO = "/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/"
DEFAULT_OD = ("/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/"
              "toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/"
              "arm-openwrt-linux-objdump")
DEFAULT_NM = ("/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/"
              "toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/"
              "arm-openwrt-linux-nm")

# --- firmware addresses (CPU) ------------------------------------------------
POP_POOL = 0x1ADC98       # descriptor pool (runtime RAM, outside the image)
POP_COUNT = 0x1ADC9C      # u8 descriptor count at pool+4 (boot default 2)
POP_REC_STRIDE = 16
POP_REC_WORD = 8          # u32 record word: byte0 band type, byte2 bit7 valid
POP_REC_BMAP = 0x14       # u32 channel bitmap inside a 16-byte record slot
POPULATOR = 0x1017F8      # rebuilds both channel tables from the pool
POP_F7_CALLER = 0x45A30   # H2D key 0x00F7 worker (host descriptor download)
POP_F7_CALL = 0x45A76     # bl 0x1017F8 inside it
POP_BOOT_CALLER = 0x48520 # boot init block that builds the default pool
POP_BOOT_CALL = 0x48620   # bl 0x1017F8 from it
POP_DA_CALLER = 0x43460   # H2D key 0x00DA worker (record channel edit)
CHAN_TABLE_B0 = 0x117B48  # {u8 channel, u8 slot, u8 0, u8 0} x 14
CHAN_TABLE_B1 = 0x117AD4  # {u8 channel, u8 slot, u8 0, u8 0} x 29
TABLE_A = 0x10C23C        # H2D key table {u16 key, u16 pad, u32 worker}
ROW_0F7 = 92
ROW_0DA = 22
# --- the band-1 RF predicate path (from lane wifi-fw-rf-programme) -----------
PREDICATE = 0x101890
MAPPER = 0x101650
LATCH = 0x89840
P_1A00 = 0x2820           # perlink+0xE20+0x1A00
# --- beacon engine / C5 -----------------------------------------------------
GLOBAL_G = 0x1747C4       # global context holding the band callbacks
G_CB_MAC_START = GLOBAL_G + 0x370   # 0x174B34
G_CB_BEACON_START = GLOBAL_G + 0x374  # 0x174B38
VAP_START = 0x46394
VAP_ON_BAND = 0x971D8
AP_CONFIG = 0x84DA4
BSS_COPY = 0x7E5CC
ISR_BLOB = 0x12266C
ISR_TBTT_5G = 0x1229E8
ISR_TBTT_2G = 0x1229FC
# --- plat.ko (C4) -----------------------------------------------------------
PLAT_STRSEC_OFF = 0x212A4     # .rodata.str1.4 file offset (Addr 0)
PLAT_TEXTOFF = 0x38           # .text file offset (Addr 0)
PLAT_DATAOFF = 0x31F60        # .data file offset (Addr 0)
KEY_2G_OFF = 0x1F5C           # .rodata.str1.4 offset of beacon_2g_tx_policy
KEY_5G_OFF = 0x1F70           # .rodata.str1.4 offset of beacon_5g_tx_policy
ID_2G, ID_5G = 0xAE, 0xAF     # ini item ids
KEYTAB_ENTRY_2G = 0x5D0       # .data offset of {id,key} entry for id 0xAE
KEYTAB_ENTRY_5G = 0x5D8       # .data offset of {id,key} entry for id 0xAF
PLAT_INIT_VALUE = 0x16AC      # hwifi_get_init_value(module, item_id)
PLAT_DPD_PWR = 0x4950         # hwifi_get_dts_dpd_pwr_customize_param
PLAT_DEV_CUSTOM_SEND = 0x1BE98
PLAT_DEV_CUSTOM_REG = 0x1BE30
PLAT_CUSTOM_INFO_2G = 0x91C   # g_dev_custom_info+0x91C = beacon_2g_tx_policy
PLAT_CUSTOM_INFO_5G = 0x7AE   # g_dev_custom_info+0x7AE = beacon_5g_tx_policy (u16)
PLAT_CUSTOM_INFO_5G_HI = 0x7B4

FW_PROBES = [
    ("ctx slot0 (module table)", 0x171458),
    ("module-table base u16", 0x172504),
    ("ctx0+0x004 create type", 0x11975C),
    ("ctx0+0x440 gate", 0x119B98),
    ("bandobj+0xEC8 gate", 0x15F438),
    ("ctx0+0x4B0 perlink", 0x119C08),
    ("ctx0+0x4B8 bandobj", 0x119C10),
    ("bandobj+0x1A00 publish", 0x15FF70),
    ("P+0x1A00 (O ptr)", 0x178238),
    ("P+0xF04 (RF mode)", 0x17773C),
    ("P+0xF87 (range flag)", 0x1777BF),
    ("pool count+header u32", 0x1ADC98),
    ("pool rec0 word (+8)", 0x1ADCA0),
    ("pool rec1 word (+8)", 0x1ADCB0),
    ("pool rec1 bitmap (+0x14)", 0x1ADCBC),
    ("chan-table B1 row idx1", 0x117AD8),
    ("chan-table B0 row idx0", 0x117B48),
    ("G+0x000 global head", GLOBAL_G),
    ("G+0x370 mac-start cb", G_CB_MAC_START),
    ("G+0x374 beacon-start cb", G_CB_BEACON_START),
    ("band0 object", 0x15CA08),
    ("band1 object", 0x15E570),
]

def die(msg):
    sys.stderr.write(msg + "\n")
    sys.exit(2)

class Fw:
    def __init__(self, path):
        self.path = path
        try:
            self.blob = open(path, "rb").read()
        except OSError as exc:
            die("cannot read firmware %s: %s" % (path, exc))
        self.ok = 0
        self.bad = []

    def u8(self, cpu):
        return self.blob[cpu - BASE]

    def u16(self, cpu):
        return struct.unpack_from("<H", self.blob, cpu - BASE)[0]

    def u32(self, cpu):
        return struct.unpack_from("<I", self.blob, cpu - BASE)[0]

    def find_u32(self, val):
        pat = struct.pack("<I", val)
        out = []
        i = self.blob.find(pat)
        while i != -1:
            if i % 2 == 0:
                out.append(i + BASE)
            i = self.blob.find(pat, i + 1)
        return out

    def check(self, label, got, want):
        if got == want:
            self.ok += 1
            return True
        self.bad.append("%s: got %r want %r" % (label, got, want))
        return False

    def check_true(self, label, cond):
        return self.check(label, bool(cond), True)

def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()

def find_tool(path, env, default):
    for cand in (path, os.environ.get(env), default):
        if cand and os.path.isfile(cand) and os.access(cand, os.X_OK):
            return cand
    return None

def disasm(od, fw, start, stop):
    r = subprocess.run([od, "-D", "-b", "binary", "-m", "arm",
                        "-M", "force-thumb", "--adjust-vma=0x40000",
                        "--start-address=%d" % start,
                        "--stop-address=%d" % stop, fw.path],
                       capture_output=True, text=True)
    return r.stdout

def phys(cpu):
    return PHYS_BASE + cpu + BAR0_ADD

def load_ko(path):
    try:
        return open(path, "rb").read()
    except OSError as exc:
        die("cannot read %s: %s" % (path, exc))

def ko_u32(blob, addr, textoff):
    return struct.unpack_from("<I", blob, textoff + addr)[0]

def ko_symbols(nm, path):
    r = subprocess.run([nm, path], capture_output=True, text=True)
    syms = {}
    for ln in r.stdout.splitlines():
        p = ln.split()
        if len(p) >= 3 and p[1] in ("t", "T", "d", "D", "b", "B"):
            syms[p[2]] = int(p[0], 16)
    return syms

# ---------------------------------------------------------------- assertions
def selftest(fw, plat, wifi, nm):
    fw.check("firmware length", len(fw.blob), FW_LEN)
    fw.check("firmware sha256", sha256(fw.path), FW_SHA)

    # --- the populator CA 0x1017F8 (0x1017F8..0x101890) --------------------
    fw.check("pop head ldr pool0", fw.u16(0x1017F8), 0x4A22)
    fw.check("pop clear band0 stride", fw.u16(0x101802), 0x18D1)
    fw.check("pop clear band0 step", fw.u16(0x101804), 0x3304)
    fw.check("pop clear band0 limit", fw.u16(0x101806), 0x2B38)
    fw.check("pop clear strb slot", fw.u16(0x101808), 0x7048)
    fw.check("pop clear loop band0", fw.u16(0x10180A), 0xD1FA)
    fw.check("pop load band1 literal", fw.u16(0x10180C), 0x4A1E)
    fw.check("pop clear band1 limit", fw.u16(0x101818), 0x2B74)
    fw.check("pop load base literal", fw.u16(0x10181E), 0x4B1B)
    fw.check("pop count ldrb", fw.u16(0x101822), 0xF893)
    fw.check("pop count off", fw.u16(0x101824), 0x711C)
    fw.check("pop add base", fw.u16(0x101826), 0xF503)
    fw.check("pop add cont", fw.u16(0x101828), 0x738C)
    fw.check("pop band type ldrb", fw.u16(0x10183E), 0xF891)
    fw.check("pop band type cont", fw.u16(0x101840), 0xE008)
    fw.check("pop band cbnz", fw.u16(0x101846), 0xD106)
    fw.check("pop bitmap ldr", fw.u16(0x101848), 0x694C)
    fw.check("pop itt", fw.u16(0x10184E), 0xBF44)
    fw.check("pop addmi", fw.u16(0x101850), 0xEB0C)
    fw.check("pop addmi cont", fw.u16(0x101852), 0x0482)
    fw.check("pop strb slot = idx", fw.u16(0x101854), 0x7065)
    fw.check("pop rec stride 16", fw.u16(0x10185E), 0x3110)
    fw.check("pop band1 bitmap ldr", fw.u16(0x10186A), 0x6958)
    fw.check("pop band1 strb slot", fw.u16(0x101876), 0x7044)
    fw.check("pop band1 record step", fw.u16(0x101880), 0x3310)
    fw.check("pop literal band0 table", fw.u32(0x101884), CHAN_TABLE_B0)
    fw.check("pop literal band1 table", fw.u32(0x101888), CHAN_TABLE_B1)
    fw.check("pop literal pool base", fw.u32(0x10188C), POP_POOL - 0x118)
    # the boot-init block that builds the default pool and calls the populator
    fw.check("boot pool count=2", fw.u16(0x485FA), 0x2302)
    fw.check("boot strb count at pool+4", fw.u16(0x485FC), 0x713B)
    fw.check("boot bitmap band0 movw", fw.u16(0x48604), 0xF641)
    fw.check("boot bitmap band0 imm", fw.u16(0x48606), 0x7CFF)
    fw.check("boot rec1 word movw", fw.u16(0x48608), 0xF240)
    fw.check("boot rec1 word imm", fw.u16(0x4860A), 0x2101)
    fw.check("boot strd bmap/word", fw.u16(0x48616), 0xE9C7)
    fw.check("boot strd cont", fw.u16(0x48618), 0xC105)
    fw.check("boot str rec0 word", fw.u16(0x4861E), 0x60BC)
    fw.check("boot bl populator", fw.u16(0x48620), 0xF0B9)
    fw.check("boot bl populator cont", fw.u16(0x48622), 0xF8EA)
    fw.check("boot pool literal addr", fw.u32(0x48694), POP_POOL)
    # H2D 0x00F7 / 0x00DA table rows and the 0xF7 worker
    fw.check("H2D row 0x00f7 key", fw.u16(TABLE_A + ROW_0F7 * 8), 0x00F7)
    fw.check("H2D row 0x00f7 worker",
             fw.u32(TABLE_A + ROW_0F7 * 8 + 4), POP_F7_CALLER + 1)
    fw.check("H2D row 0x00da key", fw.u16(TABLE_A + ROW_0DA * 8), 0x00DA)
    fw.check("H2D row 0x00da worker",
             fw.u32(TABLE_A + ROW_0DA * 8 + 4), POP_DA_CALLER + 1)
    fw.check("0xf7 worker pool literal", fw.u32(0x45B40), POP_POOL)
    fw.check("0xf7 worker install call", fw.u16(0x45A3C), 0xF0BD)
    fw.check("0xf7 worker install call2", fw.u16(0x45A3E), 0xEEBE)
    fw.check("0xf7 worker len 0x148", fw.u16(0x45A46), 0xF44F)
    fw.check("0xf7 worker len imm", fw.u16(0x45A48), 0x71A4)
    fw.check("0xf7 worker idx*16", fw.u16(0x45A4A), 0xEB03)
    fw.check("0xf7 worker populator call", fw.u16(0x45A76), 0xF0BB)
    fw.check("0xf7 worker populator call2", fw.u16(0x45A78), 0xFEBF)
    fw.check("0xda worker pool literal", fw.u32(0x4358C), POP_POOL)
    fw.check("0xda worker record add", fw.u16(0x434D8), 0xEB06)
    fw.check("0xda worker record add2", fw.u16(0x434DA), 0x1904)
    fw.check("0xda worker rec field ldrb", fw.u16(0x434DC), 0xF899)
    fw.check("0xda worker rec field off", fw.u16(0x434DE), 0x3008)
    fw.check("0xda worker strb chan", fw.u16(0x4352A), 0x7327)
    fw.check("0xda worker strh chan*10", fw.u16(0x4352C), 0x81E3)
    # --- C5: G callbacks + name blob -------------------------------------
    fw.check("G literal", fw.u32(0x9745C), GLOBAL_G)
    fw.check("vap-on-band ldr G", fw.u16(0x9735C), 0x683B)
    fw.check("vap-on-band cb guard", fw.u16(0x97360), 0xF8D7)
    fw.check("vap-on-band cb1 off", fw.u16(0x97362), 0x3370)
    fw.check("vap-on-band cb2 off", fw.u16(0x9736C), 0x3374)
    fw.check_true("name blob has tbtt_5g",
                  fw.blob[ISR_TBTT_5G - BASE:ISR_TBTT_5G - BASE + 16] ==
                  b"smac_tbtt_5g_isr")
    fw.check_true("name blob has tbtt_2g",
                  fw.blob[ISR_TBTT_2G - BASE:ISR_TBTT_2G - BASE + 16] ==
                  b"smac_tbtt_2g_isr")
    fw.check_true("no ref to name blob base", not fw.find_u32(ISR_BLOB))
    fw.check_true("no ref to 5g tbtt name", not fw.find_u32(ISR_TBTT_5G))
    # probe arithmetic against the committed G6a values
    fw.check("phys ctx slot0", phys(0x171458), 0x40829458)
    fw.check("phys perlink", phys(0x175A18), 0x4082DA18)
    # --- C4: plat.ko ini policy path -------------------------------------
    fw.check("plat.ko length", len(plat), 371708)
    fw.check("plat key 2g string",
             plat[PLAT_STRSEC_OFF + KEY_2G_OFF:PLAT_STRSEC_OFF + KEY_2G_OFF + 19],
             b"beacon_2g_tx_policy")
    fw.check("plat key 5g string",
             plat[PLAT_STRSEC_OFF + KEY_5G_OFF:PLAT_STRSEC_OFF + KEY_5G_OFF + 19],
             b"beacon_5g_tx_policy")
    def data_u32(off):
        return struct.unpack_from("<I", plat, PLAT_DATAOFF + off)[0]
    fw.check("keytab 2g id", data_u32(KEYTAB_ENTRY_2G), ID_2G)
    fw.check("keytab 2g keyoff", data_u32(KEYTAB_ENTRY_2G + 4), KEY_2G_OFF)
    fw.check("keytab 5g id", data_u32(KEYTAB_ENTRY_5G), ID_5G)
    fw.check("keytab 5g keyoff", data_u32(KEYTAB_ENTRY_5G + 4), KEY_5G_OFF)
    fw.check("dpd_pwr reads id 0xad", ko_u32(plat, PLAT_DPD_PWR + 4, PLAT_TEXTOFF),
             0xE3A010AD)
    fw.check("dpd_pwr reads id 0xae", ko_u32(plat, PLAT_DPD_PWR + 0x20, PLAT_TEXTOFF),
             0xE3A010AE)
    fw.check("dpd_pwr loop id 0xaf", ko_u32(plat, PLAT_DPD_PWR + 0x30, PLAT_TEXTOFF),
             0xE3A040AF)
    fw.check("dpd_pwr store item0xad lo",
             ko_u32(plat, PLAT_DPD_PWR + 0x28, PLAT_TEXTOFF), 0xE5C6379C)
    fw.check("dpd_pwr store item0xad hi",
             ko_u32(plat, PLAT_DPD_PWR + 0x34, PLAT_TEXTOFF), 0xE5C6279D)
    fw.check("dpd_pwr store item0xae lo",
             ko_u32(plat, PLAT_DPD_PWR + 0x3C, PLAT_TEXTOFF), 0xE5C6091C)
    fw.check("dpd_pwr store item0xae hi",
             ko_u32(plat, PLAT_DPD_PWR + 0x44, PLAT_TEXTOFF), 0xE5C6091D)
    fw.check("dpd_pwr loop hi store",
             ko_u32(plat, PLAT_DPD_PWR + 0x60, PLAT_TEXTOFF), 0xE1C530B6)
    fw.check("dpd_pwr loop lo store",
             ko_u32(plat, PLAT_DPD_PWR + 0x64, PLAT_TEXTOFF), 0xE0C500B2)
    fw.check("dpd_pwr 5g base addend",
             struct.unpack_from("<I", plat, PLAT_TEXTOFF + 0x4A94)[0], 0x7AE)
    fw.check("dpd_pwr second array addend",
             struct.unpack_from("<I", plat, PLAT_TEXTOFF + 0x4A98)[0], 0x950)
    syms = ko_symbols(nm, os.path.join(DEFAULT_KO, "hi5622v100_plat.ko"))
    fw.check("plat ini accessor", syms.get("hwifi_get_init_value"),
             PLAT_INIT_VALUE)
    fw.check("plat dpd_pwr func",
             syms.get("hwifi_get_dts_dpd_pwr_customize_param"), PLAT_DPD_PWR)
    fw.check("plat custom register",
             syms.get("wifi_dev_custom_register"), PLAT_DEV_CUSTOM_REG)
    fw.check("plat custom send", syms.get("dev_custom_send"),
             PLAT_DEV_CUSTOM_SEND)
    fw.check_true("plat exports custom info", "g_dev_custom_info" in syms)
    fw.check_true("wifi.ko imports g_dev_custom_info",
                  b"g_dev_custom_info" in ko_symbols_bytes(nm))
    fw.check("wifi.ko length", len(wifi), 3581748)
    return fw.ok, fw.bad

def ko_symbols_bytes(nm):
    r = subprocess.run([nm, "-u", os.path.join(DEFAULT_KO,
                                              "hi5622v100_wifi.ko")],
                       capture_output=True)
    return r.stdout

def cmd_decode(fw):
    print("== C2: the descriptor-pool populator ==")
    for line in [
        "pool CA 0x1ADC98: +0x04 u8 count (boot default 2); record i at",
        "  pool+16*i: +0x08 u32 word {byte0 band 0=2g/1=5g, byte2 bit7 valid},",
        "  +0x0C u8, +0x0E u16, +0x14 u32 channel bitmap",
        "populator CA 0x1017F8(band0 CA 0x117B48, band1 CA 0x117AD4):",
        "  1) all slot bytes (row+1) = 0xFF    (CA 0x101802 / CA 0x101814)",
        "  2) for i in 0..count-1: type=[pool+16i+8] byte0; bmap=[pool+16i+0x14]",
        "     type 0 -> for b in 0..13 if bmap&(1<<b): CA 0x117B48+4b+1 = i",
        "     type 1 -> for b in 0..28 if bmap&(1<<b): CA 0x117AD4+4b+1 = i",
        "boot pool builder: CA 0x485FA..0x4861E then bl 0x1017F8 (CA 0x48620)",
        "  count=2; rec0 bitmap 0x1FFF (13 ch); rec1 word 0x00000201 (band1),",
        "  rec1 bitmap 0x1FFFFFFF (29 ch); rec1 byte2 = 0x02 -> valid bit 0",
        "H2D 0x00F7 worker CA 0x45A30: installs a host descriptor blob into the",
        "  pool (CA 0x1037BC gate + memcpy_s CA 0xC1E44(dst=pool, max=0x148,",
        "  src=body, len=8+body[4]*16)) then bl 0x1017F8 (CA 0x45A76)",
        "H2D 0x00DA worker CA 0x43460: edits record +0x0C/+0x0E from the body",
        "consequence: without 0xF7 the tables keep slot=i (boot default) and",
        "  the band-1 descriptor word is 0x201 -> CA 0x101890 returns 0 by design;",
        "  the 0x00800000 valid bit is host-supplied in the 0xF7 blob only",
        "chain: CA 0x101650 mapper -> CA 0x117AD4 slot -> CA 0x1ADC98+16s+8",
        "       -> CA 0x101890 byte2 bit7 -> CA 0x89840 latches into R+0x410 b0",
    ]:
        print("  " + line)

def cmd_tables(fw):
    print("== C2 descriptor defaults (derived from the boot builder) ==")
    print("  rec  band  word       bitmap        channel slots")
    print("  0    2.4G  0x00000000 0x00001FFF    13 of the 14 band-0 rows")
    print("  1    5G    0x00000201 0x1FFFFFFF    all 29 band-1 rows")
    print("  valid bit (byte2 bit7 of the record word) = 0 in both boot records")
    print("")
    print("== C4 ini policy landing (plat.ko, host side) ==")
    print("  ini key                     item id  landing (g_dev_custom_info)")
    print("  beacon_2g_tx_policy=0       0xAE     +0x91C (byte) / +0x91D (b16-23)")
    print("  beacon_5g_tx_policy=2       0xAF     +0x7AE (u16 lo), +0x7B4 (b16-31)")
    print("  parse path: ini_cfg_init -> hwifi_config_init(17/18/19) ->")
    print("              hwifi_get_init_value(18,id) (CA 0x16AC) -> g_al_dts_params[id]")
    print("              -> hwifi_get_dts_dpd_pwr_customize_param (CA 0x4950)")
    print("  blob: g_dev_custom_info bss 0xC88 size 0x9A8, exported to wifi.ko;")
    print("        registered by plat_main_init CA 0x3054; send: dev_custom_send")
    print("        CA 0x1BE98 -> bal_write (bus path, 0x60-byte record)")
    print("")
    print("== C5 band-1 start/TBTT hooks ==")
    print("  VAP start-on-band CA 0x971D8 calls G+0x370 (CA 0x%08X) and"
          % G_CB_MAC_START)
    print("  G+0x374 (CA 0x%08X) with the VAP ctx after the band/link reconcile"
          % G_CB_BEACON_START)
    print("  G = CA 0x1747C4 (literal CA 0x9745C); guarded by G+0 != 0")
    print("  VAP+0x58 bit2 = band-1 VAP flag (VAP start CA 0x46394)")
    print("  VAP+0x13A u16 = beacon interval (AP config CA 0x84DA4, body+0x94)")
    print("  CA 0x7E5CC copies VAP+0x13A -> beacon descriptor +0x260")
    print("  ISR name blob CA 0x12266C: no code reference in the image (name-")

def cmd_policy(fw):
    print("== C4: hi5622v100_plat.ko beacon policy path ==")
    print("  ini: /lib/firmware/hi_wifi/cfg_hi5622v100_hisi.ini [HOST_WIFI_NORMAL]")
    print("       beacon_2g_tx_policy=0   beacon_5g_tx_policy=2")
    print("  key table (plat.ko .data+0x530/+0x538): id 0xAE/0xAF -> key strings")
    print("  parser: plat_main_init CA 0x3054 -> hwifi_config_init(17,18,19)")
    print("          -> hwifi_get_init_value(module, id) CA 0x16AC")
    print("          -> hwifi_get_dts_dpd_pwr_customize_param CA 0x4950")
    print("  landing: g_dev_custom_info bss 0xC88 (+0x91C 2g, +0x7AE 5g)")
    print("  reach: g_dev_custom_info exported (ksymtab) and imported by")
    print("         hi5622v100_wifi.ko; dev_custom_send CA 0x1BE98 writes a")
    print("         0x60-byte record through bal_write")
    print("  verdict: host-side platform/ini data; our five H2D frames carry no")
    print("           ini-derived per-band field -> cannot synthesise the value")

def cmd_tbtt(fw):
    print("== C5: band-1 start/TBTT arm ==")
    print("  G (CA 0x1747C4) callback slots: +0x370 CA 0x%08X, +0x374 CA 0x%08X"
          % (G_CB_MAC_START, G_CB_BEACON_START))
    print("  CA 0x971D8 flow: resolve band/link (CA 0xA4CA4) -> compare band/VAP")
    print("    -> if G+0 != 0: call G+0x370(ctx), G+0x374(ctx)")
    print("  band-1 VAP flags: VAP+0x58 bit2 (band != 0); VAP+0x13A beacon int")
    print("  registry: ISR names CA 0x12266C (45 names) incl smac_tbtt_5g_isr")
    print("    CA 0x1229E8; the blob has no static reference -> handler map [U]")
    print("  discriminator: read G+0x370/G+0x374 and the two band objects twice")
    print("    (working band-0 is the reference); NULL/zero band-1 cb = C5")

def cmd_probes(fw):
    print("== read-only probe pack (phys = 0x40000000 + CPU + 0x6b8000) ==")
    print("  %-30s %-10s %-10s %-10s" % ("object", "CPU", "BAR0", "phys"))
    for label, cpu in FW_PROBES:
        print("  %-30s 0x%08x 0x%08x 0x%08x"
              % (label, cpu, cpu + BAR0_ADD, phys(cpu)))
    print("  two-step: ctx0 = u32(0x171458)+u16(0x172504); perlink = u32(ctx+0x4B0)")
    print("            O = u32(perlink+0x2820); R = u32(O+0x2D8); R+0x3A8..+0x418")
    print("            VAP = u32(ctx+0x190); VAP+0x58 byte, VAP+0x13A u16")
    print("  pool: count = u8(0x1ADC9C); rec1 word = u32(0x1ADCB0)")
    print("        chan row B1 = u32(0x117AD4+4*idx); B0 = u32(0x117B48+4*idx)")
    print("  forbidden: RC misc window 0x10160000..0x1016ffff")

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fw", default=DEFAULT_FW)
    ap.add_argument("--objdump", default=None)
    ap.add_argument("--nm", default=None)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--decode", action="store_true")
    ap.add_argument("--tables", action="store_true")
    ap.add_argument("--probes", action="store_true")
    ap.add_argument("--policy", action="store_true")
    ap.add_argument("--tbtt", action="store_true")
    args = ap.parse_args(argv)
    od = find_tool(args.objdump, "RF_OBJDUMP", DEFAULT_OD)
    nm = find_tool(args.nm, "RF_NM", DEFAULT_NM)
    if not od:
        die("no objdump; pass --objdump")
    if not nm:
        die("no nm; pass --nm")
    fw = Fw(args.fw)
    plat = load_ko(os.path.join(DEFAULT_KO, "hi5622v100_plat.ko"))
    wifi = load_ko(os.path.join(DEFAULT_KO, "hi5622v100_wifi.ko"))
    any_mode = False
    if args.selftest:
        any_mode = True
        ok, bad = selftest(fw, plat, wifi, nm)
        print("selftest: %d PASS, %d FAIL" % (ok, len(bad)))
        for b in bad:
            print("  FAIL " + b)
        return 0 if not bad else 1
    if args.decode:
        any_mode = True
        cmd_decode(fw)
    if args.tables:
        any_mode = True
        cmd_tables(fw)
    if args.policy:
        any_mode = True
        cmd_policy(fw)
    if args.tbtt:
        any_mode = True
        cmd_tbtt(fw)
    if args.probes:
        any_mode = True
        cmd_probes(fw)
    if not any_mode:
        ap.print_help()
        return 2
    return 0

if __name__ == "__main__":
    sys.exit(main())
